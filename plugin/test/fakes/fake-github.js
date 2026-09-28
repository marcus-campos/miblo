import http from 'node:http';
import crypto from 'node:crypto';

// Minimal GitHub for `update`: GET /repos/<repo>/releases/latest (404 when `release` is null),
// asset downloads under /dl/<name>, and raw plugin.json under /raw/<repo>/main/plugin/...
// `files` maps asset name -> Buffer; SHA256SUMS.txt is generated unless `sums` is given
// (a string overrides it, false omits it).
export function startFakeGitHub({ repo = 'marcus-campos/miblo', release = null, files = {}, sums, rawPluginVersion = null } = {}) {
  const hits = [];
  const server = http.createServer((req, res) => {
    hits.push(req.url);
    const base = `http://127.0.0.1:${server.address().port}`;
    const all = { ...files };
    if (release && sums !== false) {
      all['SHA256SUMS.txt'] = Buffer.from(typeof sums === 'string' ? sums
        : Object.entries(files).map(([n, b]) => `${crypto.createHash('sha256').update(b).digest('hex')}  ${n}\n`).join(''));
    }
    if (req.url === `/api/repos/${repo}/releases/latest`) {
      if (!release) return json(res, 404, { message: 'Not Found' });
      return json(res, 200, {
        tag_name: release,
        assets: Object.keys(all).map((name) => ({ name, browser_download_url: `${base}/dl/${name}`, size: all[name].length })),
      });
    }
    if (req.url.startsWith('/dl/')) {
      const b = all[decodeURIComponent(req.url.slice(4))];
      if (!b) return json(res, 404, {});
      res.writeHead(200, { 'content-type': 'application/octet-stream' });
      return res.end(b);
    }
    if (req.url === `/raw/${repo}/main/plugin/.claude-plugin/plugin.json` && rawPluginVersion) {
      return json(res, 200, { name: 'miblo', version: rawPluginVersion });
    }
    return json(res, 404, {});
  });
  return new Promise((resolve) =>
    server.listen(0, '127.0.0.1', () => {
      const base = `http://127.0.0.1:${server.address().port}`;
      resolve({ githubApi: `${base}/api`, rawBase: `${base}/raw`, hits, close: () => new Promise((c) => server.close(c)) });
    }));
}

function json(res, code, obj) {
  res.writeHead(code, { 'content-type': 'application/json' });
  res.end(JSON.stringify(obj));
}

// A fake ESP image: starts with the 0xE9 magic byte.
export const fakeImage = (tag = 'x', size = 2048) => {
  const b = Buffer.alloc(size, tag);
  b[0] = 0xe9;
  return b;
};
