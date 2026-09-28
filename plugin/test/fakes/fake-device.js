import http from 'node:http';
import crypto from 'node:crypto';

export function startFakeDevice({ id = 'miblo-4f2a', name = 'Miblo-4F2A', code = '4827' } = {}) {
  const state = { token: null, snapshots: [], config: {}, resets: 0 };
  const readBody = (req) =>
    new Promise((resolve) => {
      const chunks = [];
      req.on('data', (c) => chunks.push(c));
      req.on('end', () => {
        const text = Buffer.concat(chunks).toString('utf8');
        try { resolve(text ? JSON.parse(text) : {}); } catch { resolve(null); }
      });
    });
  const authed = (req) => state.token && req.headers.authorization === `Bearer ${state.token}`;

  const server = http.createServer(async (req, res) => {
    const send = (code, obj) => { res.writeHead(code, { 'content-type': 'application/json' }); res.end(JSON.stringify(obj)); };
    const body = req.method === 'POST' ? await readBody(req) : null;
    if (req.method === 'GET' && req.url === '/api/info') return send(200, { id, name, fw: '0.0.0-fake', proto: 1, paired: !!state.token });
    if (req.method === 'POST' && req.url === '/api/pair') {
      if (String(body?.code) !== code) return send(403, { error: 'bad code' });
      state.token = crypto.randomBytes(16).toString('hex');
      return send(200, { token: state.token });
    }
    if (!authed(req)) return send(401, { error: 'unauthorized' });
    if (req.method === 'POST' && req.url === '/api/state') { state.snapshots.push(body); return send(200, { ok: true }); }
    if (req.method === 'POST' && req.url === '/api/config') { Object.assign(state.config, body); return send(200, { ok: true }); }
    if (req.method === 'POST' && req.url === '/api/reset') { state.resets++; return send(200, { ok: true }); }
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
