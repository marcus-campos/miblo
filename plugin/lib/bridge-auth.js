import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';

// The bridge listens on a fixed loopback port. On a shared computer another local user could
// bind it first and receive hook and status line data. So the bridge and its clients share a
// per-user secret, <data>/bridge.key (0600), which never travels:
// - a client asks GET /health with a fresh nonce (x-miblo-nonce) and sends nothing more unless
//   the answer carries x-miblo-proof = HMAC-SHA256(key, "miblo-bridge:" + nonce);
// - that answer also carries a single-use challenge (x-miblo-challenge) the bridge just made;
// - the client's request then carries x-miblo-auth = "<challenge>:<mac>", the mac being
//   HMAC-SHA256(key, "miblo-req:<challenge>:<METHOD>:<path>:<hex sha256 of the body>").
// The bridge requires it on every request but /health and accepts each challenge once, within
// CHALLENGE_TTL_MS. A listener that cannot prove it knows the key is foreign and gets nothing; a
// request seen by anyone is no use again (its challenge is spent, the key is not in it).
// (bin/statusline-tap.mjs imports nothing from lib/ and repeats the client side in a few lines.)

export const KEY_FILE = 'bridge.key';
export const NONCE_HEADER = 'x-miblo-nonce';
export const PROOF_HEADER = 'x-miblo-proof';
export const CHALLENGE_HEADER = 'x-miblo-challenge';
export const AUTH_HEADER = 'x-miblo-auth';
export const CHALLENGE_TTL_MS = 30_000;
const AUTH_RE = /^([0-9a-f]{32}):([0-9a-f]{64})$/;
const MAX_CHALLENGES = 256;
// Well above real use (parallel sessions and subagents fire bursts of hooks, plus status line
// refreshes), far below a flood.
export const CHALLENGES_PER_SECOND = 50;
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

// The mac of one request: binds the bridge's challenge, the method, the path and the body.
export function requestMac(key, challenge, method, urlPath, body = '') {
  const digest = crypto.createHash('sha256').update(body ?? '').digest('hex');
  return crypto.createHmac('sha256', String(key)).update(`miblo-req:${challenge}:${method}:${urlPath}:${digest}`).digest('hex');
}

// The x-miblo-auth header of a request answering `challenge`.
export const authHeader = (key, challenge, method, urlPath, body = '') => `${challenge}:${requestMac(key, challenge, method, urlPath, body)}`;

// The bridge's outstanding challenges: each is made for one /health answer and accepted once,
// within CHALLENGE_TTL_MS. A flood of /health cannot push out the ones real clients are about to
// answer: at most `perSecond` are made each second, and while MAX_CHALLENGES unexpired ones are
// open no new one is made (issue() -> null; the client then sends nothing this time).
export class Challenges {
  #open = new Map();  // challenge -> expiry (ms)
  #second = -1;
  #madeThisSecond = 0;
  constructor({ now = () => Date.now(), perSecond = CHALLENGES_PER_SECOND } = {}) {
    this.now = now;
    this.perSecond = perSecond;
  }

  issue() {
    const t = this.now();
    const sec = Math.floor(t / 1000);
    if (sec !== this.#second) {
      this.#second = sec;
      this.#madeThisSecond = 0;
    }
    if (this.#madeThisSecond >= this.perSecond) return null;
    for (const [c, exp] of this.#open) if (exp <= t) this.#open.delete(c);
    if (this.#open.size >= MAX_CHALLENGES) return null;
    this.#madeThisSecond += 1;
    const c = newNonce();
    this.#open.set(c, t + CHALLENGE_TTL_MS);
    return c;
  }

  // Whether `header` (x-miblo-auth) is well formed and names an open, unexpired challenge: checked
  // before a request's body is read. Spends nothing.
  isOpen(header) {
    const m = AUTH_RE.exec(String(header ?? ''));
    const exp = m ? this.#open.get(m[1]) : undefined;
    return exp !== undefined && exp > this.now();
  }

  // Whether `header` (x-miblo-auth) answers an open challenge for this request. The challenge is
  // spent whatever the outcome, so no answer can be tried twice.
  verify(key, header, method, urlPath, body = '') {
    const m = AUTH_RE.exec(String(header ?? ''));
    if (!key || !m) return false;
    const exp = this.#open.get(m[1]);
    if (exp === undefined) return false;
    this.#open.delete(m[1]);
    if (exp <= this.now()) return false;
    return sameHex(m[2], requestMac(key, m[1], method, urlPath, body));
  }
}

// The key in `dataDir`, or null when there is none or it is not plainly ours (another owner).
// Windows: there is no uid and modes are not enforced, so the owner check and the 0600 mode are
// skipped (node reports neither meaningfully). The key then relies on the data dir living under
// %USERPROFILE% (CLAUDE_PLUGIN_DATA, ~/.claude or ~/.miblo), whose ACLs let only the user and
// administrators read it. The bin/statusline-tap.mjs copy behaves the same.
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
// object with `proven` (whether it proved it knows `key`) and, when proven, `challenge` (the
// single-use challenge to answer in the next request, signedFetch).
export async function checkedHealth(base, key, { fetchImpl = globalThis.fetch, timeoutMs = 300 } = {}) {
  const nonce = newNonce();
  let res;
  try {
    res = await fetchImpl(`${base}/health`, { headers: { [NONCE_HEADER]: nonce }, signal: AbortSignal.timeout(timeoutMs) });
  } catch {
    return null;
  }
  const proven = proofOk(key, nonce, res.headers.get(PROOF_HEADER));
  const challenge = res.headers.get(CHALLENGE_HEADER);
  let body = {};
  try {
    body = (await res.json()) ?? {};
  } catch {
    // not JSON: not the bridge
  }
  return { ...(body && typeof body === 'object' ? body : {}), proven, challenge: proven && isNonce(challenge) ? challenge : null };
}

// A request to the bridge answering `challenge` (from a proven checkedHealth). `body`: a string
// or Buffer (JSON), or undefined for none.
export function signedFetch(base, key, challenge, { method = 'GET', path: urlPath, body, timeoutMs = 800, fetchImpl = globalThis.fetch }) {
  const headers = { [AUTH_HEADER]: authHeader(key, challenge, method, urlPath, body ?? '') };
  if (body !== undefined) headers['content-type'] = 'application/json';
  return fetchImpl(base + urlPath, { method, headers, body, signal: AbortSignal.timeout(timeoutMs) });
}

// One request to the bridge at `base`: /health first, then the request signed over its
// challenge. -> the Response, or null when nothing answers or the listener did not prove it
// knows `key` (it then got nothing).
export async function bridgeRequest(base, key, { method = 'GET', path: urlPath, body, timeoutMs = 800, fetchImpl = globalThis.fetch }) {
  const h = await checkedHealth(base, key, { fetchImpl, timeoutMs });
  if (!h?.proven || !h.challenge) return null;
  return signedFetch(base, key, h.challenge, { method, path: urlPath, body, timeoutMs, fetchImpl });
}
