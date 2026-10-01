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

export function createBridgeServer({ onEvent, onStatusline, getStatus, version = '', onShutdown = () => {} }) {
  const server = http.createServer(async (req, res) => {
    const send = (code, obj) => {
      res.writeHead(code, { 'content-type': 'application/json' });
      res.end(JSON.stringify(obj));
    };
    try {
      const denied = guard(req, server.address()?.port);
      if (denied) return send(denied[0], { error: denied[1] });
      if (req.method === 'GET' && req.url === '/health') return send(200, { ok: true, app: 'miblo-bridge', version });
      if (req.method === 'GET' && req.url === '/status') return send(200, await getStatus());
      if (req.method === 'POST' && req.url === '/event') {
        onEvent(await readJson(req));
        return send(200, { ok: true, app: 'miblo-bridge', version });
      }
      if (req.method === 'POST' && req.url === '/statusline') {
        onStatusline(await readJson(req));
        return send(200, { ok: true });
      }
      if (req.method === 'POST' && req.url === '/shutdown') {
        await readJson(req).catch(() => null);
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
