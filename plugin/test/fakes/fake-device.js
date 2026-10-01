import http from 'node:http';
import crypto from 'node:crypto';

// Executable contract of the firmware's HTTP API: after 5 wrong
// pairing codes, /api/pair answers 429 for 60 s; up to 4 tokens are kept and
// the oldest is evicted.
export const MAX_TOKENS = 4;
export const MAX_BAD_CODES = 5;
export const LOCKOUT_MS = 60_000;

// OTA (src/platform/ota.cpp): POST /update/open (JSON) opens the presence gate and shows
// `otaCode` on screen ({ok, codeRequired}); multipart POST /update?code=XXXX with a "firmware"
// part answers "OK", then the unit "reboots" (drops connections for `rebootMs`) and comes back
// with the version parsed from the uploaded file name. 5 wrong codes lock OTA for 60 s.
export function startFakeDevice({
  id = 'miblo-4f2a', name = 'Miblo-4F2A', code = '4827', now = () => Date.now(),
  fw = '0.0.0-fake', board = 'geekmagic_ultra', otaCode = '1234', otaCodeRequired = true, rebootMs = 30,
  tokens = [],  // already paired with these tokens (e.g. to another computer)
} = {}) {
  const state = {
    token: null, tokens: [...tokens], snapshots: [], config: {}, resets: 0, badCodes: 0, lockedUntil: 0,
    fw, gateOpen: false, otaBadCodes: 0, otaLockedUntil: 0, uploads: [], rebooting: false,
  };
  const readRaw = (req) =>
    new Promise((resolve) => {
      const chunks = [];
      req.on('data', (c) => chunks.push(c));
      req.on('end', () => resolve(Buffer.concat(chunks)));
    });
  // Overview/Limits rotation, as the firmware reports it in /api/info (defaults until configured)
  // and validates it in /api/config (lib/miblo_core/src/miblo_config.cpp).
  const rotation = () => ({
    rotate: state.config.rotate ?? false,
    rotateEverySec: state.config.rotateEverySec ?? 60,
    rotateShowSec: state.config.rotateShowSec ?? 10,
  });
  const badRotationField = (patch) => {
    const intIn = (v, lo, hi) => Number.isInteger(v) && v >= lo && v <= hi;
    if ('rotate' in patch && typeof patch.rotate !== 'boolean') return 'rotate';
    if ('rotateEverySec' in patch && !intIn(patch.rotateEverySec, 10, 3600)) return 'rotateEverySec';
    if ('rotateShowSec' in patch && !intIn(patch.rotateShowSec, 3, 300)) return 'rotateShowSec';
    const merged = { ...rotation(), ...patch };
    if (merged.rotateShowSec >= merged.rotateEverySec) return 'rotateShowSec' in patch ? 'rotateShowSec' : 'rotateEverySec';
    return null;
  };
  // Night mode, as /api/info reports it and /api/config validates it (miblo_config.cpp).
  const nightCfg = () => ({
    night: state.config.night ?? false,
    nightFrom: state.config.nightFrom ?? 1320,
    nightTo: state.config.nightTo ?? 420,
    nightBrightness: state.config.nightBrightness ?? 10,
  });
  const badNightField = (patch) => {
    const intIn = (v, lo, hi) => Number.isInteger(v) && v >= lo && v <= hi;
    if ('night' in patch && typeof patch.night !== 'boolean') return 'night';
    if ('nightFrom' in patch && !intIn(patch.nightFrom, 0, 1439)) return 'nightFrom';
    if ('nightTo' in patch && !intIn(patch.nightTo, 0, 1439)) return 'nightTo';
    if ('nightBrightness' in patch && !intIn(patch.nightBrightness, 1, 100)) return 'nightBrightness';
    const merged = { ...nightCfg(), ...patch };
    if (merged.nightFrom === merged.nightTo) return 'nightTo' in patch ? 'nightTo' : 'nightFrom';
    return null;
  };
  // Device name (miblo_config.cpp): at most 20 characters and under 64 UTF-8 bytes; "" restores
  // the default. /api/info reports the configured name, or the default one.
  const currentName = () => (typeof state.config.name === 'string' && state.config.name !== '' ? state.config.name : name);
  const badNameField = (patch) => {
    if (!('name' in patch)) return null;
    const n = patch.name;
    if (typeof n !== 'string' || [...n].length > 20 || Buffer.byteLength(n, 'utf8') >= 64) return 'name';
    return null;
  };
  // Owner name and birthday (never reported by /api/info): owner like the device name,
  // birthday "MM-DD" with a real day (02-29 allowed); "" clears either.
  const DAYS_IN_MONTH = [31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31];
  const badOwnerField = (patch) => {
    const n = patch.owner;
    if ('owner' in patch && (typeof n !== 'string' || [...n].length > 20 || Buffer.byteLength(n, 'utf8') >= 64)) return 'owner';
    if ('birthday' in patch) {
      const b = patch.birthday;
      if (typeof b !== 'string') return 'birthday';
      if (b === '') return null;
      const m = /^(\d{2})-(\d{2})$/.exec(b);
      const [mo, da] = m ? [Number(m[1]), Number(m[2])] : [0, 0];
      if (!m || mo < 1 || mo > 12 || da < 1 || da > DAYS_IN_MONTH[mo - 1]) return 'birthday';
    }
    return null;
  };
  const asJson = (buf) => {
    const text = buf.toString('utf8');
    try { return text ? JSON.parse(text) : {}; } catch { return null; }
  };
  const authed = (req) => state.tokens.some((t) => req.headers.authorization === `Bearer ${t}`);

  const server = http.createServer(async (req, res) => {
    const send = (code, obj) => { res.writeHead(code, { 'content-type': 'application/json' }); res.end(JSON.stringify(obj)); };
    const raw = req.method === 'POST' ? await readRaw(req) : null;
    if (state.rebooting) return req.socket.destroy();
    const url = new URL(req.url, 'http://x');
    if (req.method === 'POST' && url.pathname === '/update/open') return otaOpen(req, send);
    if (req.method === 'POST' && url.pathname === '/update') return otaUpload(req, res, url, raw, send);
    const body = raw ? asJson(raw) : null;
    if (req.method === 'GET' && req.url === '/api/info') {
      // A paired gadget tells a caller without one of its tokens only who it is (src/api.cpp,
      // miblo::infoView): id, paired and proto. Before pairing, or with a token: everything.
      if (state.tokens.length > 0 && !authed(req)) return send(200, { id, paired: true, proto: 1 });
      // lang = the language the screen uses (automatic mode: en here); langSet = chosen explicitly.
      const langSet = Boolean(state.config.lang);
      return send(200, { id, name: currentName(), fw: state.fw, board, build: 'fake', proto: 1, paired: state.tokens.length > 0, lang: state.config.lang || 'en', langSet, ...rotation(), ...nightCfg() });
    }
    if (req.method === 'POST' && req.url === '/api/pair') {
      if (now() < state.lockedUntil) {
        // Matches the firmware's 429 {"error":"locked","retryAfter":<seconds>} contract
        // (src/web.cpp sendLocked): remaining ms rounded up to whole seconds.
        const retryAfter = Math.ceil((state.lockedUntil - now()) / 1000);
        return send(429, { error: 'locked', retryAfter });
      }
      if (String(body?.code) !== code) {
        state.badCodes += 1;
        if (state.badCodes >= MAX_BAD_CODES) {
          state.badCodes = 0;
          state.lockedUntil = now() + LOCKOUT_MS;
        }
        return send(403, { error: 'bad code' });
      }
      state.badCodes = 0;
      state.token = crypto.randomBytes(16).toString('hex');
      state.tokens = [...state.tokens, state.token].slice(-MAX_TOKENS);
      return send(200, { token: state.token });
    }
    if (!authed(req)) return send(401, { error: 'unauthorized' });
    if (req.method === 'POST' && req.url === '/api/state') { state.snapshots.push(body); return send(200, { ok: true }); }
    if (req.method === 'POST' && req.url === '/api/config') {
      const bad = badRotationField(body ?? {}) ?? badNightField(body ?? {}) ?? badNameField(body ?? {}) ?? badOwnerField(body ?? {});
      if (bad) return send(400, { error: 'invalid', field: bad });  // all-or-nothing, like the firmware
      Object.assign(state.config, body);
      return send(200, { ok: true });
    }
    // POST /api/demo {minutes: 0..30, default 10} (firmware src/api.cpp handleDemo).
    if (req.method === 'POST' && req.url === '/api/demo') {
      const m = body?.minutes ?? 10;
      if (!Number.isInteger(m) || m < 0 || m > 30) return send(400, { error: 'invalid', field: 'minutes' });
      state.demoMinutes = m;
      return send(200, { ok: true });
    }
    if (req.method === 'POST' && req.url === '/api/reset') { state.resets++; state.tokens = []; state.token = null; return send(200, { ok: true }); }
    send(404, { error: 'not found' });
  });

  function otaLocked(send) {
    if (now() >= state.otaLockedUntil) return false;
    send(429, { error: 'locked', retryAfter: Math.ceil((state.otaLockedUntil - now()) / 1000) });
    return true;
  }

  function otaOpen(req, send) {
    if (!String(req.headers['content-type'] ?? '').startsWith('application/json')) return send(415, { error: 'json only' });
    if (!otaCodeRequired) return send(200, { ok: true, codeRequired: false });
    if (otaLocked(send)) return undefined;
    state.gateOpen = true;
    return send(200, { ok: true, codeRequired: true });
  }

  function otaUpload(req, res, url, raw, send) {
    if (otaLocked(send)) return undefined;
    if (otaCodeRequired && (!state.gateOpen || url.searchParams.get('code') !== otaCode)) {
      state.otaBadCodes += 1;
      if (state.otaBadCodes >= MAX_BAD_CODES) {
        state.otaBadCodes = 0;
        state.otaLockedUntil = now() + LOCKOUT_MS;
        return send(429, { error: 'locked', retryAfter: LOCKOUT_MS / 1000 });
      }
      return send(403, { error: 'bad code' });
    }
    const boundary = /boundary=(.+)$/.exec(String(req.headers['content-type'] ?? ''))?.[1];
    const text = raw.toString('latin1');
    const m = boundary && /Content-Disposition: form-data; name="firmware"; filename="([^"]+)"\r\n(?:[^\r\n]+\r\n)*\r\n/.exec(text);
    if (!m) return send(400, { error: 'no firmware file' });
    const start = m.index + m[0].length;
    const bytes = raw.subarray(start, text.indexOf(`\r\n--${boundary}--`, start));
    if (bytes[0] !== 0xe9) {
      res.writeHead(500, { 'content-type': 'text/plain' });
      return res.end('Magic byte is wrong, not 0xE9');
    }
    state.uploads.push({ filename: m[1], bytes: Buffer.from(bytes), contentLength: Number(req.headers['content-length']) });
    state.otaBadCodes = 0;
    state.gateOpen = false;
    res.writeHead(200, { 'content-type': 'text/plain' });
    res.end('OK');
    state.rebooting = true;
    setTimeout(() => {
      state.fw = /-(\d+\.\d+\.\d+)\.bin$/.exec(m[1])?.[1] ?? state.fw;
      state.rebooting = false;
    }, rebootMs).unref();
    return undefined;
  }

  return new Promise((resolve) =>
    server.listen(0, '127.0.0.1', () =>
      resolve({
        addr: `127.0.0.1:${server.address().port}`,
        state,
        close: () => new Promise((c) => server.close(c)),
      })));
}
