import http from 'node:http';
import { AUTH_HEADER, CHALLENGE_HEADER, NONCE_HEADER, PROOF_HEADER, Challenges, isNonce, proofFor } from './bridge-auth.js';

const MAX_BODY = 256 * 1024;

function readRaw(req) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    let size = 0;
    req.on('data', (c) => {
      size += c.length;
      if (size > MAX_BODY) {
        reject(new Error('body too large'));
        req.destroy();
      } else {
        chunks.push(c);
      }
    });
    req.on('end', () => resolve(Buffer.concat(chunks)));
    req.on('error', reject);
  });
}

const parse = (raw) => JSON.parse(raw.toString('utf8') || '{}');

// Only local, non-browser clients may talk to the bridge: the Host header must
// name the loopback address (defeats DNS rebinding), browsers always send
// Origin on cross-site requests, and POST bodies must be declared as JSON
// (a plain HTML form cannot send application/json).
function guard(req, port) {
  const host = req.headers.host;
  if (host !== `127.0.0.1:${port}` && host !== `localhost:${port}`) return [403, 'forbidden host'];
  if (req.headers.origin !== undefined) return [403, 'origin not allowed'];
  if (req.method === 'POST' && !String(req.headers['content-type'] ?? '').startsWith('application/json')) {
    return [415, 'content-type must be application/json'];
  }
  return null;
}

// `key`: the bridge key (bridge-auth.js ensureKey), or a function giving the current one. Every request but /health must answer one of
// the bridge's challenges with it (x-miblo-auth); without a key all of them are refused, so
// another local user's programs get nothing and can send nothing.
export function createBridgeServer({ onEvent, onStatusline, getStatus, version = '', onShutdown = () => {}, key: keyOf = null, now }) {
  const challenges = new Challenges(now ? { now } : {});
  const server = http.createServer(async (req, res) => {
    const send = (code, obj, headers = {}) => {
      res.writeHead(code, { 'content-type': 'application/json', ...headers });
      res.end(JSON.stringify(obj));
    };
    try {
      const key = typeof keyOf === 'function' ? keyOf() : keyOf;
      const denied = guard(req, server.address()?.port);
      if (denied) return send(denied[0], { error: denied[1] });
      if (req.method === 'GET' && req.url === '/health') {
        // Proves this is the user's own bridge (HMAC of the client's nonce with the key) and
        // hands out the single-use challenge its next request answers.
        const nonce = req.headers[NONCE_HEADER];
        const proof = key && isNonce(nonce) ? { [PROOF_HEADER]: proofFor(key, nonce), [CHALLENGE_HEADER]: challenges.issue() } : {};
        return send(200, { ok: true, app: 'miblo-bridge', version }, proof);
      }
      const raw = await readRaw(req);
      if (!challenges.verify(key, req.headers[AUTH_HEADER], req.method, req.url, raw)) return send(401, { error: 'not authorised' });
      if (req.method === 'GET' && req.url === '/status') return send(200, await getStatus());
      if (req.method === 'POST' && req.url === '/event') {
        onEvent(parse(raw));
        return send(200, { ok: true, app: 'miblo-bridge', version });
      }
      if (req.method === 'POST' && req.url === '/statusline') {
        onStatusline(parse(raw));
        return send(200, { ok: true });
      }
      if (req.method === 'POST' && req.url === '/shutdown') {
        res.on('finish', () => onShutdown());
        return send(200, { ok: true });
      }
      return send(404, { error: 'not found' });
    } catch (e) {
      return send(400, { error: String(e?.message ?? e) });
    }
  });
  return server;
}
