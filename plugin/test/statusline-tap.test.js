import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import http from 'node:http';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';

import { ensureKey, proofFor } from '../lib/bridge-auth.js';

const src = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../bin/statusline-tap.mjs');
// The data dir the tap reads the bridge key from (never the user's own).
const DATA = fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-tap-data-'));
process.env.CLAUDE_PLUGIN_DATA = DATA;

// A listener on the bridge port. `own`: proves on /health that it knows the key (bridge-auth.js),
// as the bridge does. Records every request.
async function listener({ own = true, claim = false } = {}) {
  const key = own ? ensureKey(DATA) : null;
  const requests = [];
  const server = http.createServer((req, res) => {
    let b = '';
    req.on('data', (c) => { b += c; });
    req.on('end', () => {
      requests.push({ method: req.method, url: req.url, headers: req.headers, body: b });
      const nonce = req.headers['x-miblo-nonce'];
      res.writeHead(200, { 'content-type': 'application/json', ...(own && nonce ? { 'x-miblo-proof': proofFor(key, nonce) } : {}) });
      res.end(own || claim ? '{"ok":true,"app":"miblo-bridge"}' : '{}');
    });
  });
  await new Promise((r) => server.listen(0, '127.0.0.1', r));
  return {
    requests, key, port: String(server.address().port),
    got: () => requests.filter((q) => q.url !== '/health').map((q) => ({ url: q.url, type: q.headers['content-type'], key: q.headers['x-miblo-key'], body: JSON.parse(q.body) })),
    close: () => new Promise((r) => server.close(r)),
  };
}

function installed(original) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-tap-'));
  fs.copyFileSync(src, path.join(dir, 'statusline-tap.mjs'));
  fs.writeFileSync(path.join(dir, 'statusline-original.json'), JSON.stringify(original));
  return path.join(dir, 'statusline-tap.mjs');
}

function run(tap, input, env = {}) {
  return new Promise((resolve) => {
    const child = spawn(process.execPath, [tap], { env: { ...process.env, ...env } });
    let out = '';
    child.stdout.on('data', (d) => { out += d; });
    child.on('close', (code) => resolve({ code, out }));
    child.stdin.end(input);
  });
}

const INPUT = JSON.stringify({ session_id: 's1', model: { display_name: 'Opus' } });

test('passes stdin to the original command and returns its exact output and code', async () => {
  const tap = installed({ type: 'command', command: `node -e "let s='';process.stdin.on('data',d=>s+=d).on('end',()=>{process.stdout.write('[' + JSON.parse(s).model.display_name + ']');process.exit(3)})"` });
  const r = await run(tap, INPUT, { MIBLO_PORT: '1' });
  assert.equal(r.out, '[Opus]');
  assert.equal(r.code, 3);
});

test('prints nothing when there is no original command', async () => {
  const r = await run(installed({}), INPUT, { MIBLO_PORT: '1' });
  assert.equal(r.out, '');
  assert.equal(r.code, 0);
});

test('forwards the statusline JSON to the bridge, with the key, once it proved it knows it', async () => {
  const bridge = await listener();
  try {
    await run(installed({}), INPUT, { MIBLO_PORT: bridge.port });
    assert.equal(bridge.requests[0].url, '/health');
    assert.match(bridge.requests[0].headers['x-miblo-nonce'], /^[0-9a-f]{32}$/);
    assert.deepEqual(bridge.got(), [{ url: '/statusline', type: 'application/json', key: bridge.key, body: JSON.parse(INPUT) }]);
  } finally {
    await bridge.close();
  }
});

test('reads the key from --data when the launcher passes it', async () => {
  const bridge = await listener();
  const other = fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-tap-other-'));
  fs.copyFileSync(path.join(DATA, 'bridge.key'), path.join(other, 'bridge.key'));
  fs.chmodSync(path.join(other, 'bridge.key'), 0o600);
  try {
    const tap = installed({});
    await new Promise((resolve) => {
      const child = spawn(process.execPath, [tap, '--data', other], { env: { ...process.env, CLAUDE_PLUGIN_DATA: '/nonexistent', MIBLO_PORT: bridge.port } });
      child.on('close', resolve);
      child.stdin.end(INPUT);
    });
    assert.equal(bridge.got().length, 1);
  } finally {
    await bridge.close();
  }
});

// F5: on a shared computer another local user may hold the port.
for (const claim of [false, true]) {
  test(`a foreign listener${claim ? ' that says it is the bridge' : ''} gets no status line data and no key; noted once`, async () => {
    ensureKey(DATA);
    const key = fs.readFileSync(path.join(DATA, 'bridge.key'), 'utf8').trim();
    fs.rmSync(path.join(DATA, 'foreign-bridge'), { force: true });
    fs.rmSync(path.join(DATA, 'bridge.log'), { force: true });
    const squatter = await listener({ own: false, claim });
    try {
      const tap = installed({ type: 'command', command: 'printf ok' });
      for (let i = 0; i < 2; i++) assert.equal((await run(tap, INPUT, { MIBLO_PORT: squatter.port })).out, 'ok');
      assert.ok(squatter.requests.length > 0);
      for (const q of squatter.requests) {
        assert.equal(q.url, '/health');
        assert.equal(q.body, '');
        assert.ok(!JSON.stringify(q.headers).includes(key));
      }
      assert.equal(fs.readFileSync(path.join(DATA, 'bridge.log'), 'utf8').trim().split('\n').length, 1);
    } finally {
      await squatter.close();
    }
  });
}

test('without a bridge key nothing is sent', async () => {
  const bridge = await listener();
  try {
    await run(installed({}), INPUT, { MIBLO_PORT: bridge.port, CLAUDE_PLUGIN_DATA: fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-tap-none-')) });
    assert.deepEqual(bridge.requests, []);
  } finally {
    await bridge.close();
  }
});

test('a slow original (500 ms) still lets the forward reach the bridge; output stays exact', async () => {
  const bridge = await listener();
  try {
    const tap = installed({ type: 'command', command: `node -e "setTimeout(()=>{process.stdout.write('slow\\nline');process.exit(0)},500)"` });
    const r = await run(tap, INPUT, { MIBLO_PORT: bridge.port });
    assert.equal(r.out, 'slow\nline');
    assert.equal(r.code, 0);
    assert.deepEqual(bridge.got(), [{ url: '/statusline', type: 'application/json', key: bridge.key, body: JSON.parse(INPUT) }]);
  } finally {
    await bridge.close();
  }
});

test('exits 1 when the original is killed by a signal', async () => {
  const tap = installed({ type: 'command', command: `node -e "process.kill(process.pid,'SIGKILL')"` });
  const r = await run(tap, INPUT, { MIBLO_PORT: '1' });
  assert.equal(r.code, 1);
});

// As linked by /miblo:link-statusline: through the launcher copied next to the tap, under the bare
// environment of an app started from the desktop (no node on PATH; Node found in ~/.volta here).
test('runs through the launcher copied next to it, with no node on PATH', { skip: process.platform === 'win32' }, async () => {
  const tap = installed({ type: 'command', command: 'printf "[%s]" orig' });
  const dir = path.dirname(tap);
  fs.copyFileSync(path.resolve(path.dirname(src), 'miblo-run'), path.join(dir, 'miblo-run'));
  const home = fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-tap-home-'));
  fs.mkdirSync(path.join(home, '.volta/bin'), { recursive: true });
  fs.symlinkSync(process.execPath, path.join(home, '.volta/bin/node'));
  const data = path.join(home, 'data');
  const r = await new Promise((resolve) => {
    const child = spawn('/bin/sh', [path.join(dir, 'miblo-run'), '--no-wait', 'statusline-tap.mjs', '--data', data], {
      env: { HOME: home, PATH: '/usr/bin:/bin', SHELL: '/bin/sh', MIBLO_PORT: '1', MIBLO_SYSROOT: home },
    });
    let out = '';
    child.stdout.on('data', (d) => { out += d; });
    child.on('close', (code) => resolve({ code, out }));
    child.stdin.end(INPUT);
  });
  assert.equal(r.out, '[orig]');
  assert.equal(r.code, 0);
  assert.equal(fs.readFileSync(path.join(data, 'runtime/node-path'), 'utf8').trim(), path.join(home, '.volta/bin/node'));
});
