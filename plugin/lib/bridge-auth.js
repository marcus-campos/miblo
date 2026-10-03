import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';

// The bridge listens on a fixed loopback port. On a shared computer another local user could
// bind it first and receive hook and status line data. So the bridge and its clients share a
// per-user secret, <data>/bridge.key (0600):
// - a client asks GET /health with a fresh nonce (x-miblo-nonce) and sends nothing more unless
//   the answer carries x-miblo-proof = HMAC-SHA256(key, "miblo-bridge:" + nonce);
// - only then does it send its request, with the key in x-miblo-key, which the bridge requires
//   on every request but /health.
// A listener that cannot prove it knows the key is foreign: it never gets data nor the key.
// (bin/statusline-tap.mjs imports nothing from lib/ and repeats this in a few lines.)

export const KEY_FILE = 'bridge.key';
export const NONCE_HEADER = 'x-miblo-nonce';
export const PROOF_HEADER = 'x-miblo-proof';
export const KEY_HEADER = 'x-miblo-key';
const KEY_RE = /^[0-9a-f]{64}$/;
const NONCE_RE = /^[0-9a-f]{32}$/;

export const newNonce = () => crypto.randomBytes(16).toString('hex');
export const isNonce = (n) => typeof n === 'string' && NONCE_RE.test(n);

export const proofFor = (key, nonce) => crypto.createHmac('sha256', String(key)).update(`miblo-bridge:${nonce}`).digest('hex');

function sameHex(a, b) {
  if (typeof a !== 'string' || typeof b !== 'string' || !KEY_RE.test(a) || !KEY_RE.test(b)) return false;
  return crypto.timingSafeEqual(Buffer.from(a, 'hex'), Buffer.from(b, 'hex'));
}

// Whether `proof` (a /health x-miblo-proof header) shows knowledge of `key` for `nonce`.
export const proofOk = (key, nonce, proof) => !!key && isNonce(nonce) && sameHex(String(proof ?? '').toLowerCase(), proofFor(key, nonce));

// Whether a request's x-miblo-key is the key (constant time).
export const keyOk = (key, given) => !!key && sameHex(given, key);

// The key in `dataDir`, or null when there is none or it is not plainly ours (another owner).
export function readKey(dataDir) {
  const file = path.join(dataDir, KEY_FILE);
  try {
    const st = fs.statSync(file);
    if (!st.isFile() || (typeof process.getuid === 'function' && st.uid !== process.getuid())) return null;
    if (process.platform !== 'win32' && (st.mode & 0o077) !== 0) fs.chmodSync(file, 0o600);
    const key = fs.readFileSync(file, 'utf8').trim();
    return KEY_RE.test(key) ? key : null;
  } catch {
    return null;
  }
}

// The key in `dataDir`, created when missing: written 0600 to a temporary file, then linked into
// place, which never replaces a key another process created meanwhile (that one is used).
// -> the key, or null when it cannot be read or made.
export function ensureKey(dataDir) {
  const have = readKey(dataDir);
  if (have) return have;
  const file = path.join(dataDir, KEY_FILE);
  const tmp = `${file}.${process.pid}.${crypto.randomBytes(4).toString('hex')}.tmp`;
  try {
    fs.mkdirSync(dataDir, { recursive: true, mode: 0o700 });
    fs.writeFileSync(tmp, crypto.randomBytes(32).toString('hex') + '\n', { mode: 0o600, flag: 'wx' });
    try {
      fs.linkSync(tmp, file);
    } catch (e) {
      // EEXIST: another process made it first. Anything else: a file that is there but not a key
      // (cut short, garbage) is replaced.
      if (e?.code !== 'EEXIST' || readKey(dataDir) === null) fs.renameSync(tmp, file);
    }
  } catch {
    // a read-only or missing data dir: no key, so nothing is sent
  } finally {
    try { fs.rmSync(tmp, { force: true }); } catch { /* gone already */ }
  }
  return readKey(dataDir);
}

// Notes once a day, in <data>/bridge.log, that something else answers on the bridge's port.
export function logForeignOnce(dataDir, port, now = Date.now()) {
  try {
    const mark = path.join(dataDir, 'foreign-bridge');
    let last = 0;
    try { last = fs.statSync(mark).mtimeMs; } catch { /* never noted */ }
    if (now - last < 24 * 3600_000) return;
    fs.mkdirSync(dataDir, { recursive: true });
    fs.writeFileSync(mark, '');
    fs.appendFileSync(path.join(dataDir, 'bridge.log'),
      `${new Date(now).toISOString()} port ${port} answers without the bridge key: another program or user holds it; nothing was sent to it\n`);
  } catch {
    // logging is best-effort
  }
}

// Asks GET /health at `base` with a fresh nonce. -> null when nothing answers, else the health
// object with `proven`: whether it proved it knows `key`.
export async function checkedHealth(base, key, { fetchImpl = globalThis.fetch, timeoutMs = 300 } = {}) {
  const nonce = newNonce();
  let res;
  try {
    res = await fetchImpl(`${base}/health`, { headers: { [NONCE_HEADER]: nonce }, signal: AbortSignal.timeout(timeoutMs) });
  } catch {
    return null;
  }
  const proven = proofOk(key, nonce, res.headers.get(PROOF_HEADER));
  let body = {};
  try {
    body = (await res.json()) ?? {};
  } catch {
    // not JSON: not the bridge
  }
  return { ...(body && typeof body === 'object' ? body : {}), proven };
}
