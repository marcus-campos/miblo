import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import http from 'node:http';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const src = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../bin/statusline-tap.mjs');

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

test('forwards the statusline JSON to the bridge', async () => {
  const got = [];
  const server = http.createServer((req, res) => {
    let b = '';
    req.on('data', (c) => { b += c; });
    req.on('end', () => { got.push({ url: req.url, body: JSON.parse(b) }); res.end('{}'); });
  });
  await new Promise((r) => server.listen(0, '127.0.0.1', r));
  try {
    await run(installed({}), INPUT, { MIBLO_PORT: String(server.address().port) });
    assert.deepEqual(got, [{ url: '/statusline', body: JSON.parse(INPUT) }]);
  } finally {
    await new Promise((r) => server.close(r));
  }
});

test('a slow original (500 ms) still lets the forward reach the bridge; output stays exact', async () => {
  const got = [];
  const server = http.createServer((req, res) => {
    let b = '';
    req.on('data', (c) => { b += c; });
    req.on('end', () => { got.push({ url: req.url, type: req.headers['content-type'], body: JSON.parse(b) }); res.end('{}'); });
  });
  await new Promise((r) => server.listen(0, '127.0.0.1', r));
  try {
    const tap = installed({ type: 'command', command: `node -e "setTimeout(()=>{process.stdout.write('slow\\nline');process.exit(0)},500)"` });
    const r = await run(tap, INPUT, { MIBLO_PORT: String(server.address().port) });
    assert.equal(r.out, 'slow\nline');
    assert.equal(r.code, 0);
    assert.deepEqual(got, [{ url: '/statusline', type: 'application/json', body: JSON.parse(INPUT) }]);
  } finally {
    await new Promise((r) => server.close(r));
  }
});

test('exits 1 when the original is killed by a signal', async () => {
  const tap = installed({ type: 'command', command: `node -e "process.kill(process.pid,'SIGKILL')"` });
  const r = await run(tap, INPUT, { MIBLO_PORT: '1' });
  assert.equal(r.code, 1);
});
