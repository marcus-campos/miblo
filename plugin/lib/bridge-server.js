import http from 'node:http';

const MAX_BODY = 256 * 1024;

function readJson(req) {
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
    req.on('end', () => {
      try {
        resolve(JSON.parse(Buffer.concat(chunks).toString('utf8') || '{}'));
      } catch (e) {
        reject(e);
      }
    });
    req.on('error', reject);
  });
}

export function createBridgeServer({ onEvent, onStatusline, getStatus }) {
  return http.createServer(async (req, res) => {
    const send = (code, obj) => {
      res.writeHead(code, { 'content-type': 'application/json' });
      res.end(JSON.stringify(obj));
    };
    try {
      if (req.method === 'GET' && req.url === '/health') return send(200, { ok: true, app: 'miblo-bridge' });
      if (req.method === 'GET' && req.url === '/status') return send(200, await getStatus());
      if (req.method === 'POST' && req.url === '/event') {
        onEvent(await readJson(req));
        return send(200, { ok: true });
      }
      if (req.method === 'POST' && req.url === '/statusline') {
        onStatusline(await readJson(req));
        return send(200, { ok: true });
      }
      return send(404, { error: 'not found' });
    } catch (e) {
      return send(400, { error: String(e?.message ?? e) });
    }
  });
}
