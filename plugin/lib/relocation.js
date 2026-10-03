import crypto from 'node:crypto';
import { compareVersions } from './firmware-update.js';

// Checks that a gadget found at a new address (mDNS, after failed pushes) is really ours before
// any token goes there. Anyone on the LAN can announce a gadget's id with their own address.

// The tag a gadget knows this computer's token by: the first 8 hex of FNV-1a-64 over the pairing
// token, as the gadget computes it (miblo::tokenTag). Only 32 bits of a 128-bit random token: it
// says which token is meant and cannot give the token back (2^96 tokens share each tag).
export function tokenTag(token) {
  let h = 0xcbf29ce484222325n;
  for (const b of Buffer.from(String(token ?? ''), 'utf8')) h = ((h ^ BigInt(b)) * 0x100000001b3n) & 0xffffffffffffffffn;
  return h.toString(16).padStart(16, '0').slice(0, 8);
}

// The gadget's answer to GET /api/challenge?n=<nonce>&t=<tag> (v 2): {id, ip, v: 2, mac}, mac =
// hex HMAC-SHA256 keyed with the token over the nonce (the 32 hex characters as sent), the
// gadget's id and its own station IPv4 (dotted). The ip binds the answer to the gadget's address:
// a relay to the real gadget gets back the real gadget's ip, not its own.
export const challengeMac = (token, nonce, id, ip) =>
  crypto.createHmac('sha256', String(token)).update(String(nonce) + String(id) + String(ip)).digest('hex');

// The first firmware with the challenge: a gadget last seen on it (or later) must answer it.
export const CHALLENGE_FW = '1.14.0';
const VERSION_RE = /^\d+\.\d+\.\d+/;
export const hasChallenge = (fw) => VERSION_RE.test(String(fw ?? '')) && compareVersions(fw, CHALLENGE_FW) >= 0;

// Constant-time check of a challenge answer from `host` (the IPv4 the request went to).
export function challengeOk(reply, { token, nonce, id, host }) {
  if (reply?.v !== 2 || reply.id !== id || typeof reply.ip !== 'string' || reply.ip !== host) return false;
  if (typeof reply.mac !== 'string' || !/^[0-9a-f]{64}$/i.test(reply.mac)) return false;
  return crypto.timingSafeEqual(Buffer.from(reply.mac.toLowerCase(), 'hex'), Buffer.from(challengeMac(token, nonce, id, reply.ip), 'hex'));
}

const IPV4_PORT = /^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3}):(\d{1,5})$/;
const octets = (addr) => {
  const m = IPV4_PORT.exec(String(addr ?? ''));
  if (!m) return null;
  const o = m.slice(1, 5).map(Number);
  return o.every((x) => x <= 255) ? { o, port: Number(m[5]) } : null;
};

// A gadget lives on the LAN on port 80: RFC 1918, link-local or CGNAT IPv4. Never loopback or a
// public address (that would make the bridge an authenticated client of anything).
export function isLanAddr(addr) {
  const p = octets(addr);
  if (!p || p.port !== 80) return false;
  const [a, b] = p.o;
  return a === 10 || (a === 172 && b >= 16 && b <= 31) || (a === 192 && b === 168) ||
    (a === 169 && b === 254) || (a === 100 && b >= 64 && b <= 127);
}

export function sameSlash24(x, y) {
  const p = octets(x);
  const q = octets(y);
  return !!p && !!q && p.o[0] === q.o[0] && p.o[1] === q.o[1] && p.o[2] === q.o[2];
}

// -> 'ok' (the new address is the gadget), 'legacy' (followed with the weaker check of a firmware
// before the challenge: it should be updated), 'refused' (it is not, or cannot prove it: the
// user must pair again) or 'retry' (no answer just now: try again later). Never sends the token.
export async function verifyNewAddr({ client, dev, addr, addrOk = isLanAddr }) {
  if (!addrOk(addr)) return 'refused';
  const nonce = crypto.randomBytes(16).toString('hex');
  const host = String(addr).replace(/:\d+$/, '');
  try {
    const reply = await client.challenge(addr, nonce, tokenTag(dev.token));
    return challengeOk(reply, { token: dev.token, nonce, id: dev.id, host }) ? 'ok' : 'refused';
  } catch (e) {
    if (e?.status !== 404) return e?.status >= 400 && e.status < 500 ? 'refused' : 'retry';
  }
  // No challenge here. A gadget last seen on a firmware with it is not this address: anyone can
  // answer 404 and echo the public id. Only one last seen on an older (or unknown) firmware is
  // followed, and only nearby (same /24) if its reduced /api/info, asked without a token, gives
  // the same id.
  if (hasChallenge(dev.fw) || !sameSlash24(addr, dev.addr)) return 'refused';
  try {
    const info = await client.info(addr);
    return info?.id === dev.id ? 'legacy' : 'refused';
  } catch (e) {
    return e?.status >= 400 && e.status < 500 ? 'refused' : 'retry';
  }
}
