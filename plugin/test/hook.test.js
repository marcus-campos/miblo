import { test } from 'node:test';
import assert from 'node:assert/strict';
import http from 'node:http';
import { spawnSync, spawn } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const hook = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../bin/hook.js');

function runHook(input, env) {
  return new Promise((resolve) => {
    const child = spawn(process.execPath, [hook], { env: { ...process.env, ...env } });
    let out = '';
    child.stdout.on('data', (d) => { out += d; });
    child.on('close', (code) => resolve({ code, out }));
    child.stdin.end(input);
  });
}

test('forwards the event to the bridge, adding pid on SessionStart', async () => {
  const received = [];
  const server = http.createServer((req, res) => {
    let body = '';
    req.on('data', (c) => { body += c; });
    req.on('end', () => { received.push(JSON.parse(body)); res.end('{}'); });
  });
  await new Promise((r) => server.listen(0, '127.0.0.1', r));
  try {
    const r = await runHook(JSON.stringify({ session_id: 's1', hook_event_name: 'SessionStart' }), { MIBLO_PORT: String(server.address().port), MIBLO_NO_SPAWN: '1' });
    assert.equal(r.code, 0);
    assert.equal(r.out, '');
    assert.equal(received[0].session_id, 's1');
    assert.ok('pid' in received[0]);
  } finally {
    await new Promise((r) => server.close(r));
  }
});

test('exits 0 silently when the bridge is down', () => {
  const t0 = Date.now();
  const r = spawnSync(process.execPath, [hook], { input: '{"session_id":"s","hook_event_name":"Stop"}', env: { ...process.env, MIBLO_PORT: '1', MIBLO_NO_SPAWN: '1' } });
  assert.equal(r.status, 0);
  assert.equal(r.stdout.toString(), '');
  assert.ok(Date.now() - t0 < 3000);
});

test('exits 0 silently on invalid input', () => {
  const r = spawnSync(process.execPath, [hook], { input: 'not json', env: { ...process.env, MIBLO_PORT: '1', MIBLO_NO_SPAWN: '1' } });
  assert.equal(r.status, 0);
  assert.equal(r.stdout.toString(), '');
});

test('exits 0 within the watchdog when stdin never closes', async () => {
  const t0 = Date.now();
  return new Promise((resolve) => {
    const child = spawn(process.execPath, [hook], { env: { ...process.env, MIBLO_PORT: '1', MIBLO_NO_SPAWN: '1' } });
    let out = '';
    child.stdout.on('data', (d) => { out += d; });
    child.on('close', (code) => {
      assert.equal(code, 0);
      assert.equal(out, '');
      assert.ok(Date.now() - t0 < 5000);
      resolve();
    });
    // Intentionally do NOT end stdin, forcing the watchdog to trigger
  });
});
