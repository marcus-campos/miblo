// `miblo.js update ...`: firmware updates for a paired gadget, driven step by step so a
// slash command (whose Bash tool cannot prompt) can ask the user in between:
//   update check [id] [--file path]   compare the gadget's fw with the latest release (JSON)
//   update open  [id] [--file path]   fetch + verify the image, open the OTA gate (JSON)
//   update send  [id] [code]          upload with the on-screen code, wait for the reboot
//   update [id] [--file path] [--check]  check only, or open (+ send when no code is needed)
// Device OTA contract: firmware/src/platform/ota.cpp.
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { cleanId, cleanName } from './mdns.js';
import { BUSY_RETRY_MS, busyLine, isBusy, isReducedInfo } from './device-client.js';

export const GITHUB_API = 'https://api.github.com';
export const GITHUB_RAW = 'https://raw.githubusercontent.com';
export const REPO = 'marcus-campos/miblo';
export const REBOOT_TIMEOUT_MS = 120_000;

const ESP_IMAGE_MAGIC = 0xe9;
const MAX_IMAGE_BYTES = 4 * 1024 * 1024;
const PENDING_FILE = 'update-pending.json';
const NO_RELEASES = 'No firmware releases are published yet. Use --file <path> to update from a local miblo-<board>-<version>.bin.';

class UpdateError extends Error {
  constructor(code, message) {
    super(message);
    this.exitCode = code;
  }
}
const fail = (code, msg) => { throw new UpdateError(code, msg); };

// "miblo-<board>-<version>.bin" -> { board, version } (null if the name does not match).
// The stage-1 loader ("miblo-loader-<board>-...") parses as board "loader-<board>" and is
// therefore refused by the board check.
export function parseImageName(name) {
  const m = /^miblo-(.+)-(\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?)\.bin$/.exec(path.basename(String(name)));
  return m ? { board: m[1], version: m[2] } : null;
}

// Numeric x.y.z compare; a pre-release suffix ("-rc1", "-fake") sorts below the plain version.
export function compareVersions(a, b) {
  const parse = (v) => {
    const m = /^v?(\d+)\.(\d+)\.(\d+)(-[^+]*)?/.exec(String(v ?? '').trim());
    return m ? { n: [Number(m[1]), Number(m[2]), Number(m[3])], pre: !!m[4] } : { n: [0, 0, 0], pre: true };
  };
  const x = parse(a), y = parse(b);
  for (let i = 0; i < 3; i++) if (x.n[i] !== y.n[i]) return x.n[i] < y.n[i] ? -1 : 1;
  if (x.pre !== y.pre) return x.pre ? -1 : 1;
  return 0;
}

export function parseUpdateArgs(args) {
  const out = { step: null, id: null, code: null, file: null, check: false };
  const rest = [];
  for (let i = 0; i < args.length; i++) {
    const a = args[i];
    if (a === '--check') out.check = true;
    else if (a === '--file') {
      out.file = args[++i];
      if (!out.file) fail(2, '--file needs a path to a .bin');
    } else rest.push(a);
  }
  if (['check', 'open', 'send'].includes(rest[0])) out.step = rest.shift();
  for (const a of rest) {
    if (out.step === 'send' && /^\d{4}$/.test(a)) out.code = a;
    else if (!out.id) out.id = a;
  }
  return out;
}

// A reset this soon after the upload starts is the gadget refusing it, not a crash mid-flash.
const UPLOAD_RESET_WINDOW_MS = 5000;
const RESET_CODES = new Set(['ECONNRESET', 'EPIPE', 'UND_ERR_SOCKET']);
const isReset = (err) => RESET_CODES.has(err?.cause?.code) || RESET_CODES.has(err?.code);
const sha256 = (buf) => crypto.createHash('sha256').update(buf).digest('hex');

export class FirmwareUpdater {
  constructor({ store, dataDir, fetchImpl = globalThis.fetch, githubApi = GITHUB_API, rawBase = GITHUB_RAW, repo = REPO, pluginVersion = '',
    pollMs = 2000, rebootTimeoutMs = REBOOT_TIMEOUT_MS, deviceTimeoutMs = 3000, sleep } = {}) {
    Object.assign(this, { store, dataDir, fetch: fetchImpl, githubApi, rawBase, repo, pluginVersion, pollMs, rebootTimeoutMs, deviceTimeoutMs });
    this.sleep = sleep ?? ((ms) => new Promise((r) => setTimeout(r, ms)));
    this.cacheDir = path.join(dataDir, 'firmware');
    this.pendingFile = path.join(dataDir, PENDING_FILE);
  }

  // ---- entry point: returns { code, out } like the rest of miblo.js ----
  async run(args) {
    try {
      const o = parseUpdateArgs(args);
      if (o.step === 'check' || (!o.step && o.check)) return this.#ok(JSON.stringify(await this.check(o), null, 2));
      if (o.step === 'open') return this.#ok(JSON.stringify(await this.open(o), null, 2));
      if (o.step === 'send') return this.#ok(await this.send(o));
      const opened = await this.open(o);
      if (opened.codeRequired) {
        return this.#ok(`Type the 4-digit code shown on ${opened.name}, then run: update send ${opened.id} <code>`);
      }
      return this.#ok(await this.send({ id: opened.id }));
    } catch (e) {
      if (e instanceof UpdateError) return { code: e.exitCode, out: e.message + '\n' };
      throw e;
    }
  }

  #ok(out) { return { code: 0, out: out + '\n' }; }

  // ---- steps ----
  async check({ id, file }) {
    const devices = id ? this.#targets(id) : this.store.list();
    let release = null, releaseError = null;
    try {
      release = await this.#release();
    } catch (e) {
      if (!(e instanceof UpdateError)) throw e;
      releaseError = e.message;
    }
    const plugin = await this.#pluginStatus(release);
    const source = file ? this.#localImage(file) : release;
    const firmware = { source: file ? 'file' : 'github', latest: source?.version ?? null };
    if (!file && !release) firmware.error = releaseError ?? NO_RELEASES;

    const results = [];
    for (const d of devices) {
      const row = { id: cleanId(d.id), name: cleanName(d.name) };
      let info;
      try {
        info = await this.#info(d.addr, d.token);
      } catch (e) {
        // busy: it answered (503, low on memory) but could not say its version just now
        results.push({ ...row, online: isBusy(e), ...(isBusy(e) ? { busy: true } : {}), needsUpdate: null });
        continue;
      }
      const board = String(info?.board ?? '');
      const fw = String(info?.fw ?? '');
      if (!fw) {
        // Only {id, paired, proto}: the gadget no longer accepts this computer's pairing (or an
        // odd reply). The version is unknown, so never "up to date" nor "needs an update".
        results.push({ ...row, online: true, fw, board, needsUpdate: null, ...(isReducedInfo(info) ? { unauthorized: true } : {}) });
        continue;
      }
      if (!source) {
        results.push({ ...row, online: true, fw, board, needsUpdate: false });
        continue;
      }
      const hasImage = file ? source.board === board : !!this.#assetFor(source, board);
      results.push({
        ...row, online: true, fw, board, latest: source.version,
        boardMatch: hasImage,
        // a local file may also reinstall or downgrade; a release only moves forward
        needsUpdate: hasImage && (file ? compareVersions(fw, source.version) !== 0 : compareVersions(fw, source.version) < 0),
      });
    }
    return { plugin, firmware, devices: results };
  }

  // Installed plugin version vs the latest release tag, or (no release yet) vs plugin.json on
  // the default branch.
  async #pluginStatus(release) {
    const current = this.pluginVersion || null;
    let latest = release?.version ?? null;
    let source = latest ? 'release' : null;
    if (!latest) {
      try {
        const res = await this.fetch(`${this.rawBase}/${this.repo}/main/plugin/.claude-plugin/plugin.json`, {
          headers: { 'user-agent': 'miblo-plugin' }, signal: AbortSignal.timeout(15_000),
        });
        const v = res.ok ? String((await res.json())?.version ?? '') : '';
        if (/^\d+\.\d+\.\d+/.test(v)) { latest = v; source = 'main'; }
      } catch {
        // offline: unknown
      }
    }
    const needsUpdate = current && latest ? compareVersions(current, latest) < 0 : null;
    return { current, latest, source, needsUpdate };
  }

  async open({ id, file }) {
    const d = this.#one(id);
    const info = await this.#infoOrOffline(d);
    if (isReducedInfo(info)) fail(1, `${cleanName(d.name)} no longer accepts this computer's pairing: run /miblo:pair again.`);
    const board = String(info?.board ?? '');
    if (!board) fail(1, `${cleanName(d.name)} did not report its board; refusing to update.`);
    const image = file ? this.#fromFile(file, board) : await this.#fromRelease(board);

    let res;
    try {
      // With the pairing token: an authorised open replaces a code a stranger opened anonymously
      // (else anyone on the LAN could keep the owner out of updates by re-opening codes).
      res = await this.fetch(`http://${d.addr}/update/open`, {
        method: 'POST', headers: { 'content-type': 'application/json', ...(d.token ? { authorization: `Bearer ${d.token}` } : {}) }, body: '{}',
        signal: AbortSignal.timeout(this.deviceTimeoutMs),
      });
    } catch {
      fail(1, `Could not reach ${cleanName(d.name)}.`);
    }
    const data = await res.json().catch(() => null);
    if (res.status === 429) fail(2, lockedMsg(data));
    if (isBusy(res)) fail(1, busyLine(cleanName(d.name)));
    if (!res.ok || !data?.ok) fail(1, `${cleanName(d.name)} refused to start the update (HTTP ${res.status}).`);
    const codeRequired = data.codeRequired !== false;
    this.#writePending({ id: d.id, file: image.file, sha256: image.sha256, version: image.version, board, from: String(info.fw ?? ''), codeRequired });
    return { id: cleanId(d.id), name: cleanName(d.name), from: String(info.fw ?? ''), to: image.version, codeRequired };
  }

  async send({ id, code }) {
    const pending = this.#readPending();
    if (!pending || (id && pending.id !== id)) fail(2, 'No update is open for this gadget; run `update open` first.');
    const d = this.#one(pending.id);
    if (pending.codeRequired && !/^\d{4}$/.test(String(code ?? ''))) fail(2, `Type the 4-digit code shown on ${cleanName(d.name)}.`);
    let bin;
    try { bin = fs.readFileSync(pending.file); } catch { bin = null; }
    if (!bin || sha256(bin) !== pending.sha256) fail(1, 'The firmware image changed or disappeared; run `update open` again.');

    const boundary = `----miblo${crypto.randomBytes(12).toString('hex')}`;
    const filename = `miblo-${pending.board}-${pending.version}.bin`;
    const body = Buffer.concat([
      Buffer.from(`--${boundary}\r\nContent-Disposition: form-data; name="firmware"; filename="${filename}"\r\n` +
        'Content-Type: application/octet-stream\r\n\r\n'),
      bin,
      Buffer.from(`\r\n--${boundary}--\r\n`),
    ]);
    const qs = pending.codeRequired ? `?code=${encodeURIComponent(code)}` : '';
    let res, text;
    const startedAt = Date.now();
    try {
      res = await this.fetch(`http://${d.addr}/update${qs}`, {
        method: 'POST',
        headers: { 'content-type': `multipart/form-data; boundary=${boundary}`, 'content-length': String(body.length) },
        body,
        signal: AbortSignal.timeout(this.rebootTimeoutMs),
      });
      text = await res.text();
    } catch (err) {
      // The gadget refuses an upload it cannot take (window closed, locked out) before reading the
      // body, so its answer is often lost to a connection reset: say what that most likely means.
      if (isReset(err) && Date.now() - startedAt < UPLOAD_RESET_WINDOW_MS) {
        fail(2, `${cleanName(d.name)} stopped the upload: the update window may have closed or the gadget is locked after wrong codes — run /miblo:update again.`);
      }
      fail(1, `Could not reach ${cleanName(d.name)} during the upload. Its previous firmware stays in place.`);
    }
    let data = null;
    try { data = JSON.parse(text); } catch { /* plain text */ }
    if (res.status === 403) fail(2, 'Wrong code.');
    if (res.status === 429) fail(2, lockedMsg(data));
    if (res.status === 400 && data?.error === 'update not open') {
      fail(2, `The update window on ${cleanName(d.name)} closed (it lasts 5 minutes); run \`update open\` again.`);
    }
    if (isBusy(res)) fail(1, `${busyLine(cleanName(d.name))} Its previous firmware stays in place.`);
    if (!res.ok) fail(1, `Update failed: ${String(text).slice(0, 120) || `HTTP ${res.status}`}. The previous firmware stays in place.`);

    this.#clearPending();
    const deadline = Date.now() + this.rebootTimeoutMs;
    while (Date.now() < deadline) {
      await this.sleep(this.pollMs);
      try {
        const info = await this.#info(d.addr, d.token);
        if (String(info?.fw) === pending.version) {
          return `${cleanName(d.name)} updated from ${pending.from} to ${pending.version}.`;
        }
      } catch {
        // rebooting
      }
    }
    fail(1, `Firmware sent, but ${cleanName(d.name)} did not come back with ${pending.version} within ${Math.round(this.rebootTimeoutMs / 1000)} s. Check the gadget screen.`);
  }

  // ---- devices ----
  #targets(id) {
    const all = this.store.list();
    if (!all.length) fail(2, 'No paired gadgets. Run /miblo:pair first.');
    if (!id) return all;
    const d = all.find((x) => x.id === id);
    if (!d) fail(2, `No paired gadget with id ${cleanId(id)}.`);
    return [d];
  }

  #one(id) {
    const list = this.#targets(id);
    if (list.length > 1) fail(2, `Several gadgets are paired; pass an id: ${list.map((d) => cleanId(d.id)).join(', ')}.`);
    return list[0];
  }

  // GET /api/info; a busy gadget (503) is asked again after BUSY_RETRY_MS, as DeviceClient does.
  async #info(addr, token) {
    const headers = token ? { authorization: `Bearer ${token}` } : {};
    for (let i = 0; ; i++) {
      const res = await this.fetch(`http://${addr}/api/info`, { headers, signal: AbortSignal.timeout(this.deviceTimeoutMs) });
      if (res.ok) return res.json();
      await res.body?.cancel();
      if (!isBusy(res) || i >= BUSY_RETRY_MS.length) {
        const err = new Error(`HTTP ${res.status}`);
        err.status = res.status;
        throw err;
      }
      await this.sleep(BUSY_RETRY_MS[i]);
    }
  }

  async #infoOrOffline(d) {
    try {
      return await this.#info(d.addr, d.token);
    } catch (e) {
      if (isBusy(e)) return fail(1, busyLine(cleanName(d.name)));
      return fail(1, `Could not reach ${cleanName(d.name)}. Is it on and on the same network?`);
    }
  }

  // ---- images ----
  #localImage(file) {
    const meta = parseImageName(file);
    if (!meta) fail(2, 'The file name must look like miblo-<board>-<version>.bin.');
    return { ...meta, file };
  }

  #fromFile(file, board) {
    const meta = this.#localImage(file);
    if (meta.board !== board) fail(2, `Refusing: ${path.basename(file)} is for board ${meta.board}, but the gadget is ${board}.`);
    let bin;
    try { bin = fs.readFileSync(file); } catch { fail(2, `Cannot read ${file}.`); }
    this.#validate(bin);
    return { file: path.resolve(file), version: meta.version, sha256: sha256(bin) };
  }

  #validate(bin) {
    if (!bin.length || bin.length > MAX_IMAGE_BYTES || bin[0] !== ESP_IMAGE_MAGIC) {
      fail(1, 'This does not look like a Miblo firmware image.');
    }
  }

  async #gh(url, accept) {
    try {
      return await this.fetch(url, {
        headers: { accept, 'user-agent': 'miblo-plugin' },
        signal: AbortSignal.timeout(60_000),
      });
    } catch {
      return fail(1, 'Could not reach GitHub to look for firmware releases.');
    }
  }

  async #latestRelease() {
    return (await this.#release()) ?? fail(1, NO_RELEASES);
  }

  // The latest GitHub release, or null when none is published yet.
  async #release() {
    const res = await this.#gh(`${this.githubApi}/repos/${this.repo}/releases/latest`, 'application/vnd.github+json');
    if (res.status === 404) return null;
    if (!res.ok) fail(1, `GitHub answered HTTP ${res.status} when looking for firmware releases.`);
    const rel = await res.json();
    const version = String(rel.tag_name ?? '').replace(/^v/, '');
    if (!/^\d+\.\d+\.\d+/.test(version)) fail(1, `The latest release has an unexpected tag (${String(rel.tag_name).slice(0, 32)}).`);
    return { version, assets: Array.isArray(rel.assets) ? rel.assets : [] };
  }

  #assetFor(rel, board) {
    return rel.assets.find((a) => a.name === `miblo-${board}-${rel.version}.bin`) ?? null;
  }

  async #fromRelease(board) {
    const rel = await this.#latestRelease();
    const asset = this.#assetFor(rel, board);
    if (!asset) fail(2, `Release ${rel.version} has no firmware for board ${board}.`);
    const sums = rel.assets.find((a) => a.name === 'SHA256SUMS.txt');
    let expected = null;
    if (sums) {
      const r = await this.#gh(sums.browser_download_url, 'application/octet-stream');
      if (!r.ok) fail(1, `Could not download SHA256SUMS.txt (HTTP ${r.status}).`);
      const line = (await r.text()).split(/\r?\n/).map((l) => l.trim().split(/\s+\*?/)).find((p) => p[1] === asset.name);
      if (!line) fail(1, `SHA256SUMS.txt does not list ${asset.name}.`);
      expected = line[0].toLowerCase();
    }
    const file = path.join(this.cacheDir, asset.name);
    let bin = null;
    try { bin = fs.readFileSync(file); } catch { /* not cached */ }
    if (!bin || (expected && sha256(bin) !== expected)) {
      const r = await this.#gh(asset.browser_download_url, 'application/octet-stream');
      if (!r.ok) fail(1, `Could not download ${asset.name} (HTTP ${r.status}).`);
      bin = Buffer.from(await r.arrayBuffer());
      if (expected && sha256(bin) !== expected) fail(1, `Checksum mismatch for ${asset.name}; not installing it.`);
      this.#validate(bin);
      fs.mkdirSync(this.cacheDir, { recursive: true });
      const tmp = `${file}.${process.pid}.tmp`;
      fs.writeFileSync(tmp, bin);
      fs.renameSync(tmp, file);
    }
    this.#validate(bin);
    return { file, version: rel.version, sha256: sha256(bin) };
  }

  // ---- pending state between `open` and `send` ----
  #writePending(p) {
    fs.mkdirSync(this.dataDir, { recursive: true });
    fs.writeFileSync(this.pendingFile, JSON.stringify(p, null, 2), { mode: 0o600 });
  }

  #readPending() {
    try { return JSON.parse(fs.readFileSync(this.pendingFile, 'utf8')); } catch { return null; }
  }

  #clearPending() {
    try { fs.unlinkSync(this.pendingFile); } catch { /* already gone */ }
  }
}

function lockedMsg(data) {
  const secs = Math.max(1, Math.ceil(Number(data?.retryAfter ?? 60)));
  // "busy": a code for something else (settings, reset, Wi-Fi) is on the screen; the gadget never
  // replaces it, so wait for it to expire.
  if (data?.error === 'busy') return `Another code is on the gadget screen. Try again in ${secs} s.`;
  return `Too many wrong codes. Try again in ${secs} s.`;
}
