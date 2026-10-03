import { test } from 'node:test';
import assert from 'node:assert/strict';
import http from 'node:http';
import { spawnSync, spawn } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { ensureKey, proofFor } from '../lib/bridge-auth.js';

const hook = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../bin/hook.js');
// Never the user's own data dir (the hook makes the bridge key there).
process.env.CLAUDE_PLUGIN_DATA = fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-hook-'));
// A bridge a hook spawns refreshes the status line link: never in the user's own settings.
process.env.CLAUDE_CONFIG_DIR = fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-hook-conf-'));

function runHook(input, env) {
  return new Promise((resolve) => {
    const child = spawn(process.execPath, [hook], { env: { ...process.env, ...env } });
    let out = '';
    child.stdout.on('data', (d) => { out += d; });
    child.on('close', (code) => resolve({ code, out }));
    child.stdin.end(input);
  });
}

// A listener on the bridge port: `own` proves it knows the key in `dataDir` as the bridge does
// (bridge-auth.js); otherwise it is another program, which may even say it is the bridge.
function listener({ own, dataDir = process.env.CLAUDE_PLUGIN_DATA, claim = false }) {
  const key = own ? ensureKey(dataDir) : null;
  const requests = [];
  const server = http.createServer((req, res) => {
    let body = '';
    req.on('data', (c) => { body += c; });
    req.on('end', () => {
      requests.push({ method: req.method, url: req.url, headers: req.headers, body });
      const nonce = req.headers['x-miblo-nonce'];
      const headers = { 'content-type': 'application/json', ...(own && nonce ? { 'x-miblo-proof': proofFor(key, nonce) } : {}) };
      res.writeHead(200, headers);
      res.end(JSON.stringify(own || claim ? { ok: true, app: 'miblo-bridge', version: '0.0.0' } : {}));
    });
  });
  return new Promise((r) => server.listen(0, '127.0.0.1', () => r({
    server, requests, key, port: String(server.address().port),
    received: () => requests.filter((q) => q.url === '/event').map((q) => JSON.parse(q.body)),
    close: () => new Promise((c) => { server.closeAllConnections?.(); server.close(c); }),
  })));
}

test('forwards whitelisted fields, adding pid on SessionStart and UserPromptSubmit', async () => {
  const bridge = await listener({ own: true });
  const env = { MIBLO_PORT: bridge.port, MIBLO_NO_SPAWN: '1' };
  try {
    const r = await runHook(JSON.stringify({ session_id: 's1', hook_event_name: 'SessionStart' }), env);
    assert.equal(r.code, 0);
    assert.equal(r.out, '');
    const received = () => bridge.received();
    assert.equal(received()[0].session_id, 's1');
    assert.ok('pid' in received()[0]);
    // With the key, after /health proved the bridge knows it.
    const ev = bridge.requests.find((q) => q.url === '/event');
    assert.equal(ev.headers['x-miblo-key'], bridge.key);
    assert.equal(bridge.requests[0].url, '/health');

    await runHook(JSON.stringify({ session_id: 's1', hook_event_name: 'UserPromptSubmit', prompt: 'secret', transcript_path: '/t' }), env);
    assert.ok('pid' in received()[1]);
    assert.equal(received()[1].prompt, undefined);
    assert.equal(received()[1].transcript_path, undefined);

    await runHook(JSON.stringify({ session_id: 's1', hook_event_name: 'PreToolUse', tool_name: 'Write', tool_input: { file_path: '/a.js', content: 'BODY' } }), env);
    assert.deepEqual(received()[2], { session_id: 's1', hook_event_name: 'PreToolUse', tool_name: 'Write', tool_input: { file_path: '/a.js' } });
  } finally {
    await bridge.close();
  }
});

// F5: on a shared computer another local user may hold the bridge port first.
for (const claim of [false, true]) {
  test(`a foreign listener on the port${claim ? ' that says it is the bridge' : ''} receives no event and no key`, async () => {
    const dataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-hook-'));
    const squatter = await listener({ own: false, claim });
    try {
      for (const evt of [{ session_id: 's1', hook_event_name: 'SessionStart', cwd: '/home/me/secret-project' },
        { session_id: 's1', hook_event_name: 'PreToolUse', tool_name: 'Bash', tool_input: { command: 'cat secrets' } }]) {
        assert.equal((await runHook(JSON.stringify(evt), { MIBLO_PORT: squatter.port, CLAUDE_PLUGIN_DATA: dataDir })).code, 0);
      }
      const key = fs.readFileSync(path.join(dataDir, 'bridge.key'), 'utf8').trim();
      for (const q of squatter.requests) {
        assert.ok(['/health', '/shutdown'].includes(q.url), q.url);
        assert.equal(q.headers['x-miblo-key'], undefined);
        assert.ok(!q.body.includes('secret') && !q.body.includes('s1') && !q.body.includes(key), q.body);
        assert.ok(!JSON.stringify(q.headers).includes(key));
      }
      assert.equal(squatter.requests.some((q) => q.url === '/shutdown'), claim);
      const log = fs.readFileSync(path.join(dataDir, 'bridge.log'), 'utf8').trim().split('\n');
      assert.equal(log.length, 1);
      assert.match(log[0], /answers without the bridge key/);
    } finally {
      await squatter.close();
    }
  });
}

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

test('the bridge is spawned from the home dir with the shared data-dir fallback', async () => {
  const fs = await import('node:fs');
  const src = fs.readFileSync(hook, 'utf8');
  assert.match(src, /cwd: os\.homedir\(\)/);
  assert.match(src, /const dataDir = defaultDataDir\(\)/);
  assert.match(src, /'--data', dataDir\]/);
});
