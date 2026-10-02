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
  otherCodeSec = 0,  // another purpose's code is on the screen for this long: /update/open is busy
  legacy = false,  // a firmware before the daily-life routes: they answer 404, /api/info lacks their fields
  clockKnown = true,  // false: the gadget has no time yet (no NTP, no snapshot): HH:MM and DD/MM answer 409 clock
  busy = 0,  // the next `busy` requests (to `busyPath` only, if set) answer 503 {"error":"busy"} (heapLowForRequest)
  busyPath = null,
} = {}) {
  const state = {
    token: null, tokens: [...tokens], snapshots: [], config: {}, resets: 0, badCodes: 0, lockedUntil: 0,
    fw, gateOpen: false, otaBadCodes: 0, otaLockedUntil: 0, uploads: [], rebooting: false,
    // Daily life: the last accepted body of each route (null = never), plus what the gadget keeps.
    focus: null, focusSince: 0, meeting: null, meetingUntil: 0, say: null, timer: null, timerUntil: 0,
    countdown: null, countdownDate: '', countdownLabel: '', finds: 0,
    reminders: [],  // [{id, dueAt} one-off (ids 1..4) | {id, at, days} recurring (ids 5..8), with text]
    lastRemind: null, held: false,  // held: the cat holds a reminder now (POST {dismiss:true} clears it)
    clockKnown,
    busyLeft: busy, busyPath, busyHits: 0,  // busyHits: requests refused with 503
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
    // Low on memory: the firmware refuses before doing anything (src/api.cpp, src/web.cpp).
    if (state.busyLeft > 0 && (!state.busyPath || url.pathname === state.busyPath)) {
      state.busyLeft -= 1;
      state.busyHits += 1;
      return send(503, { error: 'busy' });
    }
    if (req.method === 'POST' && url.pathname === '/update/open') return otaOpen(req, send);
    if (req.method === 'POST' && url.pathname === '/update') return otaUpload(req, res, url, raw, send);
    const body = raw ? asJson(raw) : null;
    if (req.method === 'GET' && req.url === '/api/info') {
      // A paired gadget tells a caller without one of its tokens only who it is (src/api.cpp,
      // miblo::infoView): id, paired and proto. Before pairing, or with a token: everything.
      if (state.tokens.length > 0 && !authed(req)) return send(200, { id, paired: true, proto: 1 });
      // lang = the language the screen uses (automatic mode: en here); langSet = chosen explicitly.
      const langSet = Boolean(state.config.lang);
      return send(200, { id, name: currentName(), fw: state.fw, board, build: 'fake', proto: 1, paired: state.tokens.length > 0, lang: state.config.lang || 'en', langSet, ...rotation(), ...nightCfg(), ...(legacy ? {} : dailyInfo()) });
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
    // An old firmware has no daily-life routes at all: 404 before any token check.
    if (legacy && DAILY[`${req.method} ${url.pathname}`]) return send(404, { error: 'not found' });
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
    const daily = !legacy && DAILY[`${req.method} ${url.pathname}`];
    if (daily) {
      // src/api.cpp dailyRoute: a JSON object of at most 300 bytes (empty = {}), then the handler.
      if (raw && (raw.length > 300 || body === null || typeof body !== 'object' || Array.isArray(body))) {
        return send(400, { error: 'bad json' });
      }
      const out = {};
      const [code, field] = daily(body ?? {}, out);
      if (code === 200) return send(200, { ...out, ok: true });
      return send(code, { error: code === 409 ? 'conflict' : 'invalid', field: field ?? '' });
    }
    send(404, { error: 'not found' });
  });

  // ---- Daily life: the firmware's request handlers (miblo_focus/meeting/desknotes.cpp), each
  // -> [status, field]. Every field present is validated before anything is applied.
  const intIn = (v, lo, hi) => Number.isInteger(v) && v >= lo && v <= hi;
  // miblo_desknotes.cpp cleanText: a string, not blank after trimming spaces, well-formed (no
  // lone surrogate), no control character (C0 incl. NUL, DEL, C1), at most `maxChars` characters
  // and fewer than `cap` UTF-8 bytes.
  const cleanText = (v, maxChars, cap) => {
    if (typeof v !== 'string') return null;
    const t = v.replace(/^ +| +$/g, '');
    if (!t || !t.isWellFormed() || /[\u0000-\u001f\u007f-\u009f]/.test(t) || [...t].length > maxChars ||
        Buffer.byteLength(t, 'utf8') >= cap) return null;
    return t;
  };
  const isTrue = (b, k) => k in b && b[k] !== true;  // a flag present but not `true`
  const pad = (n) => String(n).padStart(2, '0');
  const localDate = (t) => { const d = new Date(t); return { y: d.getFullYear(), m: d.getMonth() + 1, d: d.getDate() }; };
  const realDay = (y, m, d) => m >= 1 && m <= 12 && d >= 1 && d <= new Date(y, m, 0).getDate();
  const ymd = ({ y, m, d }) => `${y}-${pad(m)}-${pad(d)}`;
  const DAILY = {
    'POST /api/focus': (b) => {
      if ('focusMin' in b && !intIn(b.focusMin, 5, 120)) return [400, 'focusMin'];
      if ('breakMin' in b && !intIn(b.breakMin, 1, 60)) return [400, 'breakMin'];
      if ('rounds' in b && !intIn(b.rounds, 1, 12)) return [400, 'rounds'];
      if (isTrue(b, 'stop')) return [400, 'stop'];
      state.focus = b;
      state.focusSince = now();
      return [200];
    },
    'POST /api/meeting': (b) => {
      if ('min' in b && !intIn(b.min, 1, 480)) return [400, 'min'];
      if (isTrue(b, 'off')) return [400, 'off'];
      state.meeting = b;
      state.meetingUntil = b.off ? 0 : now() + (b.min ?? 60) * 60_000;
      return [200];
    },
    'POST /api/say': (b) => {
      if (isTrue(b, 'off')) return [400, 'off'];
      if (!b.off) {
        if (cleanText(b.text, 40, 48) === null) return [400, 'text'];
        if ('min' in b && !intIn(b.min, 1, 480)) return [400, 'min'];
      }
      state.say = b;
      return [200];
    },
    'POST /api/timer': (b) => {
      if (isTrue(b, 'stop')) return [400, 'stop'];
      if (!b.stop && !intIn(b.min, 1, 180)) return [400, 'min'];
      state.timer = b;
      state.timerUntil = b.stop ? 0 : now() + b.min * 60_000;
      return [200];
    },
    'POST /api/find': () => { state.finds++; return [200]; },
    'GET /api/remind': (b, out) => {
      out.items = state.reminders.map((r) => (r.at
        ? { id: r.id, at: r.at, days: r.days, text: r.text }
        : { id: r.id, in: Math.max(0, Math.ceil((r.dueAt - now()) / 1000)), text: r.text }));
      return [200];
    },
    'POST /api/remind': (b, out) => {
      if ('dismiss' in b) {
        if (b.dismiss !== true) return [400, 'dismiss'];
        if (!state.held) return [409, 'none'];
        state.held = false;
        return [200];
      }
      if ('delete' in b) {
        const i = intIn(b.delete, 1, 8) ? state.reminders.findIndex((r) => r.id === b.delete) : -1;
        if (i < 0) return [400, 'delete'];
        state.reminders.splice(i, 1);
        return [200];
      }
      const text = cleanText(b.text, 40, 48);
      let item;
      if ('in' in b) {
        if (!intIn(b.in, 1, 1440)) return [400, 'in'];
        if (text === null) return [400, 'text'];
        item = { dueAt: now() + b.in * 60_000 };
      } else if ('at' in b) {
        const m = typeof b.at === 'string' ? /^(\d{2}):(\d{2})$/.exec(b.at) : null;
        if (!m || Number(m[1]) > 23 || Number(m[2]) > 59) return [400, 'at'];
        if ('days' in b && !intIn(b.days, 1, 127)) return [400, 'days'];
        if (text === null) return [400, 'text'];
        if ('days' in b) {
          item = { at: b.at, days: b.days };
        } else {
          if (!state.clockKnown) return [409, 'clock'];
          const t = new Date(now());
          const nowMin = t.getHours() * 60 + t.getMinutes();
          let ahead = Number(m[1]) * 60 + Number(m[2]) - nowMin;
          if (ahead <= 0) ahead += 1440;  // already past today: tomorrow
          item = { dueAt: now() + ahead * 60_000 };
        }
      } else {
        return [400, 'in'];
      }
      const [lo, hi] = item.at ? [5, 8] : [1, 4];
      let id = lo;
      while (id <= hi && state.reminders.some((r) => r.id === id)) id++;
      if (id > hi) return [409, 'full'];
      state.reminders.push({ id, ...item, text });
      state.reminders.sort((a, z) => a.id - z.id);
      state.lastRemind = b;
      out.id = id;
      return [200];
    },
    'POST /api/countdown': (b) => {
      if ('off' in b) {
        if (b.off !== true) return [400, 'off'];
        state.countdown = b;
        state.countdownLabel = state.countdownDate = '';
        return [200];
      }
      const label = cleanText(b.label, 20, 41);
      if (label === null) return [400, 'label'];
      const today = state.clockKnown ? localDate(now()) : null;
      let date;
      if ('date' in b) {
        const m = typeof b.date === 'string' ? /^(\d{4})-(\d{2})-(\d{2})$/.exec(b.date) : null;
        date = m && { y: Number(m[1]), m: Number(m[2]), d: Number(m[3]) };
        if (!date || !realDay(date.y, date.m, date.d)) return [400, 'date'];
      } else if ('md' in b) {
        const m = typeof b.md === 'string' ? /^(\d{2})-(\d{2})$/.exec(b.md) : null;
        const [mo, da] = m ? [Number(m[1]), Number(m[2])] : [0, 0];
        if (!m || !realDay(2000, mo, da)) return [400, 'md'];
        if (!today) return [409, 'clock'];
        // The next occurrence from today (today included).
        date = { y: today.y, m: mo, d: da };
        while (!realDay(date.y, mo, da) || ymd(date) < ymd(today)) date.y++;
      } else {
        return [400, 'date'];
      }
      if (today && ymd(date) < ymd(today)) return [400, 'date'];
      state.countdown = b;
      state.countdownLabel = label;
      state.countdownDate = ymd(date);
      return [200];
    },
  };
  // What /api/info adds for the daily-life commands (src/api.cpp handleInfo).
  const dailyInfo = () => {
    const f = state.focus && !state.focus.stop ? state.focus : null;
    const left = f ? Math.max(0, (f.focusMin ?? 25) * 60 - Math.floor((now() - state.focusSince) / 1000)) : 0;
    return {
      focus: f ? { phase: 'focus', round: 1, rounds: f.rounds ?? 4, left } : { phase: 'off', round: 0, rounds: 4, left: 0 },
      meetingLeft: Math.max(0, Math.ceil((state.meetingUntil - now()) / 1000)),
      timerLeft: Math.max(0, Math.ceil((state.timerUntil - now()) / 1000)),
      countdown: state.countdownLabel,
      countdownDate: state.countdownDate,
      daily: 1,
    };
  };

  function otaLocked(send) {
    if (now() >= state.otaLockedUntil) return false;
    send(429, { error: 'locked', retryAfter: Math.ceil((state.otaLockedUntil - now()) / 1000) });
    return true;
  }

  function otaOpen(req, send) {
    if (!String(req.headers['content-type'] ?? '').startsWith('application/json')) return send(415, { error: 'json only' });
    if (!otaCodeRequired) return send(200, { ok: true, codeRequired: false });
    if (otaLocked(send)) return undefined;
    // A code for another purpose on the screen is never replaced (PresenceGate::busyFor).
    if (otherCodeSec > 0) return send(429, { error: 'busy', retryAfter: otherCodeSec });
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
