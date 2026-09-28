import http from 'node:http';
import crypto from 'node:crypto';

// Executable contract of the firmware's HTTP API (spec §5.4): after 5 wrong
// pairing codes, /api/pair answers 429 for 60 s; up to 4 tokens are kept and
// the oldest is evicted.
export const MAX_TOKENS = 4;
export const MAX_BAD_CODES = 5;
export const LOCKOUT_MS = 60_000;

export function startFakeDevice({ id = 'miblo-4f2a', name = 'Miblo-4F2A', code = '4827', now = () => Date.now() } = {}) {
  const state = { token: null, tokens: [], snapshots: [], config: {}, resets: 0, badCodes: 0, lockedUntil: 0 };
  const readBody = (req) =>
    new Promise((resolve) => {
      const chunks = [];
      req.on('data', (c) => chunks.push(c));
      req.on('end', () => {
        const text = Buffer.concat(chunks).toString('utf8');
        try { resolve(text ? JSON.parse(text) : {}); } catch { resolve(null); }
      });
    });
  const authed = (req) => state.tokens.some((t) => req.headers.authorization === `Bearer ${t}`);

  const server = http.createServer(async (req, res) => {
    const send = (code, obj) => { res.writeHead(code, { 'content-type': 'application/json' }); res.end(JSON.stringify(obj)); };
    const body = req.method === 'POST' ? await readBody(req) : null;
    if (req.method === 'GET' && req.url === '/api/info') return send(200, { id, name, fw: '0.0.0-fake', proto: 1, paired: state.tokens.length > 0 });
    if (req.method === 'POST' && req.url === '/api/pair') {
      if (now() < state.lockedUntil) return send(429, { error: 'too many attempts' });
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
    if (req.method === 'POST' && req.url === '/api/config') { Object.assign(state.config, body); return send(200, { ok: true }); }
    if (req.method === 'POST' && req.url === '/api/reset') { state.resets++; state.tokens = []; state.token = null; return send(200, { ok: true }); }
    send(404, { error: 'not found' });
  });

  return new Promise((resolve) =>
    server.listen(0, '127.0.0.1', () =>
      resolve({
        addr: `127.0.0.1:${server.address().port}`,
        state,
        close: () => new Promise((c) => server.close(c)),
      })));
}
