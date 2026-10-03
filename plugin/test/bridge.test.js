import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import httpMod from 'node:http';
import { startFakeDevice } from './fakes/fake-device.js';
import { DeviceClient } from '../lib/device-client.js';
import { DeviceStore } from '../lib/device-store.js';
import { createBridge } from '../bin/bridge.js';

async function started(bridge) {
  await new Promise((r) => bridge.server.listen(0, '127.0.0.1', r));
  const base = `http://127.0.0.1:${bridge.server.address().port}`;
  // As the hooks, the status line and the CLI do: with the bridge key (bridge-auth.js).
  const auth = { 'x-miblo-key': bridge.key };
  const post = (p, body) => fetch(base + p, { method: 'POST', headers: { 'content-type': 'application/json', ...auth }, body: typeof body === 'string' ? body : JSON.stringify(body) });
  const get = (p) => fetch(base + p, { headers: auth });
  return { base, post, get, stop: () => new Promise((r) => bridge.server.close(r)) };
}

test('hook events and statusline data reach a paired device', async () => {
  const dev = await startFakeDevice();
  const dataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-bridge-'));
  const client = new DeviceClient();
  const token = await client.pair(dev.addr, '4827', 'test');
  new DeviceStore(dataDir).upsert({ id: 'miblo-4f2a', name: 'Miblo-4F2A', addr: dev.addr, token });

  const bridge = createBridge({ dataDir, client, discoverFn: async () => [], host: 'test-host' });
  const http = await started(bridge);
  try {
    assert.equal((await (await fetch(http.base + '/health')).json()).app, 'miblo-bridge');

    await http.post('/event', { session_id: 's1', hook_event_name: 'PermissionRequest', cwd: '/w/api', tool_name: 'Bash', tool_input: { command: 'ls' } });
    await http.post('/statusline', { session_id: 's1', model: { display_name: 'Opus' }, context_window: { used_percentage: 12 }, rate_limits: { five_hour: { used_percentage: 50, resets_at: Math.floor(Date.now() / 1000) + 3600 } } });
    await bridge.push();

    const snap = dev.state.snapshots.at(-1);
    assert.equal(snap.host, 'test-host');
    assert.equal(snap.sessions[0].st, 'perm');
    assert.equal(snap.sessions[0].ctx, 12);
    assert.equal(snap.usage.h5.pct, 50);
    assert.equal(snap.alerts[0].kind, 'perm');

    const status = await (await http.get('/status')).json();
    assert.equal(status.devices[0].online, true);
    assert.equal(status.statuslineSeen, true);
    assert.equal(status.sessions.length, 1);
  } finally {
    await http.stop();
    await dev.close();
  }
});

test('seq increases on every push', async () => {
  const dev = await startFakeDevice();
  const dataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-bridge-'));
  const client = new DeviceClient();
  const token = await client.pair(dev.addr, '4827', 'test');
  new DeviceStore(dataDir).upsert({ id: 'x', name: 'X', addr: dev.addr, token });
  const bridge = createBridge({ dataDir, client, discoverFn: async () => [] });
  try {
    await bridge.push();
    await bridge.push();
    const [a, b] = dev.state.snapshots;
    assert.ok(b.seq > a.seq);
  } finally {
    await dev.close();
  }
});

test('SessionEnd forgets metrics and invalid JSON returns 400', async () => {
  const dataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-bridge-'));
  const bridge = createBridge({ dataDir, discoverFn: async () => [] });
  const http = await started(bridge);
  try {
    await http.post('/statusline', { session_id: 's1', context_window: { used_percentage: 5 } });
    await http.post('/event', { session_id: 's1', hook_event_name: 'SessionEnd' });
    assert.equal(bridge.metrics.forSession('s1'), undefined);
    assert.equal((await http.post('/event', '{bad')).status, 400);
    assert.equal((await http.get('/nope')).status, 404);
  } finally {
    await http.stop();
  }
});

test('cost counts from zero only for sessions whose SessionStart the bridge saw', async () => {
  const dataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-bridge-'));
  const bridge = createBridge({ dataDir, discoverFn: async () => [] });
  const http = await started(bridge);
  try {
    await http.post('/event', { session_id: 'new', hook_event_name: 'SessionStart', cwd: '/w/a' });
    await http.post('/statusline', { session_id: 'new', cost: { total_cost_usd: 2 } });
    await http.post('/statusline', { session_id: 'old', cost: { total_cost_usd: 40 } });
    assert.deepEqual(bridge.metrics.today(), { usd: 2 });
  } finally {
    await http.stop();
  }
});

test('a finished response and the time worked reach the snapshot, /status and the data dir', async () => {
  const dev = await startFakeDevice();
  const dataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-bridge-'));
  const client = new DeviceClient();
  const token = await client.pair(dev.addr, '4827', 'test');
  new DeviceStore(dataDir).upsert({ id: 'x', name: 'X', addr: dev.addr, token });
  let t = new Date(2026, 8, 29, 10).getTime();
  const bridge = createBridge({ dataDir, client, discoverFn: async () => [], now: () => t });
  const http = await started(bridge);
  try {
    await http.post('/event', { session_id: 's1', hook_event_name: 'UserPromptSubmit', cwd: '/w/a' });
    await bridge.push();
    t += 20_000;
    await http.post('/event', { session_id: 's1', hook_event_name: 'Stop' });
    await bridge.push();
    assert.deepEqual(dev.state.snapshots.at(-1).today, { usd: 0, turns: 1, work: 20 });
    const status = await (await http.get('/status')).json();
    assert.deepEqual(status.today, { usd: 0, turns: 1, work: 20, top: [{ name: 'a', work: 20 }] });
    assert.ok(fs.existsSync(path.join(dataDir, 'day-stats.json')));
  } finally {
    await http.stop();
    await dev.close();
  }
});

test('the cached latest release reaches the snapshot, and is omitted when unknown', async () => {
  const dev = await startFakeDevice();
  const dataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-bridge-'));
  const client = new DeviceClient();
  const token = await client.pair(dev.addr, '4827', 'test');
  new DeviceStore(dataDir).upsert({ id: 'x', name: 'X', addr: dev.addr, token });
  try {
    await createBridge({ dataDir, client, discoverFn: async () => [] }).push();
    assert.ok(!('latest' in dev.state.snapshots.at(-1)));
    fs.writeFileSync(path.join(dataDir, 'update-check.json'), JSON.stringify({ checkedAt: Date.now(), latest: '1.0.2' }));
    await createBridge({ dataDir, client, discoverFn: async () => [] }).push();
    assert.equal(dev.state.snapshots.at(-1).latest, '1.0.2');
  } finally {
    await dev.close();
  }
});

function rawRequest(port, { method = 'GET', path: p = '/health', headers = {}, body } = {}) {
  return new Promise((resolve, reject) => {
    const req = httpMod.request({ host: '127.0.0.1', port, method, path: p, headers, setHost: false }, (res) => {
      res.resume();
      res.on('end', () => resolve(res.statusCode));
    });
    req.on('error', reject);
    req.end(body);
  });
}

test('rejects foreign Host, any Origin, and non-JSON POST bodies', async () => {
  const dataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-bridge-'));
  const bridge = createBridge({ dataDir, discoverFn: async () => [] });
  const http = await started(bridge);
  const port = bridge.server.address().port;
  const json = { 'content-type': 'application/json', 'x-miblo-key': bridge.key };
  try {
    assert.equal(await rawRequest(port, { headers: { host: `127.0.0.1:${port}` } }), 200);
    assert.equal(await rawRequest(port, { headers: { host: `localhost:${port}` } }), 200);
    assert.equal(await rawRequest(port, { headers: { host: `evil.example:${port}` } }), 403);
    assert.equal(await rawRequest(port, { headers: { host: '127.0.0.1:1' } }), 403);
    assert.equal(await rawRequest(port, { headers: {} }), 400); // node rejects HTTP/1.1 without Host
    assert.equal(await rawRequest(port, { headers: { host: `127.0.0.1:${port}`, origin: 'https://evil.example' } }), 403);
    const ev = JSON.stringify({ session_id: 's1', hook_event_name: 'SessionStart' });
    assert.equal(await rawRequest(port, { method: 'POST', path: '/event', headers: { host: `127.0.0.1:${port}`, 'x-miblo-key': bridge.key, 'content-type': 'text/plain' }, body: ev }), 415);
    assert.equal(await rawRequest(port, { method: 'POST', path: '/event', headers: { host: `127.0.0.1:${port}`, 'x-miblo-key': bridge.key }, body: ev }), 415);
    assert.equal(bridge.tracker.sessions().length, 0);
    assert.equal(await rawRequest(port, { method: 'POST', path: '/event', headers: { host: `127.0.0.1:${port}`, ...json, 'content-type': 'application/json; charset=utf-8' }, body: ev }), 200);
    assert.equal(bridge.tracker.sessions().length, 1);
  } finally {
    await http.stop();
  }
});

test('/health and every /event reply report the version; POST /shutdown invokes the shutdown callback', async () => {
  const dataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-bridge-'));
  let shutdowns = 0;
  const bridge = createBridge({ dataDir, discoverFn: async () => [], version: '9.9.9', onShutdown: () => { shutdowns++; } });
  const http = await started(bridge);
  try {
    assert.deepEqual(await (await fetch(http.base + '/health')).json(), { ok: true, app: 'miblo-bridge', version: '9.9.9' });
    const ev = await http.post('/event', { session_id: 's1', hook_event_name: 'SessionStart' });
    assert.deepEqual(await ev.json(), { ok: true, app: 'miblo-bridge', version: '9.9.9' });
    assert.equal((await fetch(http.base + '/shutdown', { method: 'POST', headers: { 'x-miblo-key': bridge.key } })).status, 415);
    assert.equal(shutdowns, 0);
    const r = await http.post('/shutdown', {});
    assert.equal(r.status, 200);
    await r.text();
    await new Promise((res) => setTimeout(res, 20));
    assert.equal(shutdowns, 1);
  } finally {
    await http.stop();
  }
});

test('a failing debounced push is logged, not thrown', async () => {
  const dataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-bridge-'));
  const lines = [];
  const badHost = { toString() { throw new Error('boom'); } };
  const bridge = createBridge({ dataDir, discoverFn: async () => [], host: badHost, log: (m) => lines.push(m) });
  bridge.schedule();
  await new Promise((r) => setTimeout(r, 300));
  assert.equal(lines.length, 1);
  assert.match(lines[0], /push failed: Error: boom/);
});

test('a rising 5-hour limit is forecast in the snapshot and in /status', async () => {
  const dev = await startFakeDevice();
  const dataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-bridge-'));
  const client = new DeviceClient();
  const token = await client.pair(dev.addr, '4827', 'test');
  new DeviceStore(dataDir).upsert({ id: 'x', name: 'X', addr: dev.addr, token });
  let t = new Date(2026, 8, 29, 10).getTime();
  const reset = Math.floor(t / 1000) + 4 * 3600;
  const bridge = createBridge({ dataDir, client, discoverFn: async () => [], now: () => t });
  const http = await started(bridge);
  const reading = (pct) => http.post('/statusline', { session_id: 's1', rate_limits: { five_hour: { used_percentage: pct, resets_at: reset } } });
  try {
    await reading(40);
    await bridge.push();
    assert.equal(dev.state.snapshots.at(-1).usage.h5.eta, 0);  // one reading is no pace
    assert.equal((await (await http.get('/status')).json()).forecast, null);
    for (let i = 1; i <= 20; i++) {
      t += 60_000;
      await reading(40 + i);  // 1 point a minute
    }
    await bridge.push();
    const eta = Math.floor(t / 1000) + 40 * 60;  // 60% now, 40 points left
    assert.equal(dev.state.snapshots.at(-1).usage.h5.eta, eta);
    const status = await (await http.get('/status')).json();
    assert.equal(status.forecast, eta);
    assert.equal(status.usage.h5.eta, eta);
  } finally {
    await http.stop();
    await dev.close();
  }
});

test('last week goes out on Mondays only; a running command carries when it started', async () => {
  const dev = await startFakeDevice();
  const dataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-bridge-'));
  const client = new DeviceClient();
  const token = await client.pair(dev.addr, '4827', 'test');
  new DeviceStore(dataDir).upsert({ id: 'x', name: 'X', addr: dev.addr, token });
  let t = new Date(2026, 8, 25, 10).getTime();  // Friday 25/09
  const bridge = createBridge({ dataDir, client, discoverFn: async () => [], now: () => t });
  const http = await started(bridge);
  try {
    await http.post('/event', { session_id: 's1', hook_event_name: 'SessionStart', cwd: '/w/a' });
    await http.post('/statusline', { session_id: 's1', cost: { total_cost_usd: 1.5 } });
    await http.post('/event', { session_id: 's1', hook_event_name: 'PreToolUse', tool_name: 'Bash', tool_input: { command: 'npm test' } });
    const started_ = Math.floor(t / 1000);
    await bridge.push();
    t += 30_000;
    await bridge.push();
    let snap = dev.state.snapshots.at(-1);
    assert.equal(snap.sessions[0].ts, started_);
    assert.ok(!('week' in snap));  // Friday
    await http.post('/event', { session_id: 's1', hook_event_name: 'Stop' });
    await bridge.push();

    t = new Date(2026, 8, 27, 10).getTime();   // Sunday: still no summary
    await bridge.push();
    assert.ok(!('week' in dev.state.snapshots.at(-1)));
    t = new Date(2026, 8, 28, 9).getTime();    // Monday
    await bridge.push();
    snap = dev.state.snapshots.at(-1);
    assert.deepEqual(snap.week, { work: 30, turns: 1, usd: 1.5, top: 5 });
    t = new Date(2026, 8, 29, 9).getTime();    // Tuesday
    await bridge.push();
    assert.ok(!('week' in dev.state.snapshots.at(-1)));
  } finally {
    await http.stop();
    await dev.close();
  }
});

// F5: on a shared computer another local user can reach 127.0.0.1 too. The bridge answers them
// only /health; it proves itself to its own clients with the key in <data>/bridge.key.
test('every request but /health needs the bridge key; /health proves the bridge knows it', async () => {
  const { proofFor } = await import('../lib/bridge-auth.js');
  const dataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-bridge-'));
  let shutdowns = 0;
  const bridge = createBridge({ dataDir, discoverFn: async () => [], onShutdown: () => { shutdowns++; } });
  const http = await started(bridge);
  const json = { 'content-type': 'application/json' };
  try {
    const key = fs.readFileSync(path.join(dataDir, 'bridge.key'), 'utf8').trim();
    assert.match(key, /^[0-9a-f]{64}$/);
    assert.equal(bridge.key, key);
    if (process.platform !== 'win32') assert.equal(fs.statSync(path.join(dataDir, 'bridge.key')).mode & 0o777, 0o600);
    const ev = JSON.stringify({ session_id: 's1', hook_event_name: 'SessionStart' });
    for (const k of [undefined, '', '0'.repeat(64), key.toUpperCase(), key.slice(0, 63)]) {
      const headers = { ...json, ...(k === undefined ? {} : { 'x-miblo-key': k }) };
      for (const [p, method] of [['/event', 'POST'], ['/statusline', 'POST'], ['/shutdown', 'POST'], ['/status', 'GET']]) {
        const r = await fetch(http.base + p, { method, headers: method === 'POST' ? headers : { 'x-miblo-key': k ?? '' }, body: method === 'POST' ? ev : undefined });
        assert.equal(r.status, 401, `${p} ${k}`);
        await r.text();
      }
    }
    assert.equal(bridge.tracker.sessions().length, 0);
    assert.equal(shutdowns, 0);
    const nonce = '0123456789abcdef0123456789abcdef';
    const h = await fetch(http.base + '/health', { headers: { 'x-miblo-nonce': nonce } });
    assert.equal(h.headers.get('x-miblo-proof'), proofFor(key, nonce));
    assert.equal((await h.json()).app, 'miblo-bridge');
    // No nonce, or a malformed one: no proof.
    for (const n of [undefined, 'xyz', nonce + '0']) {
      const r = await fetch(http.base + '/health', { headers: n ? { 'x-miblo-nonce': n } : {} });
      assert.equal(r.headers.get('x-miblo-proof'), null, String(n));
      await r.text();
    }
    assert.equal((await http.post('/event', ev)).status, 200);
    assert.equal(bridge.tracker.sessions().length, 1);
    // A second bridge on the same data dir shares the key.
    assert.equal(createBridge({ dataDir, discoverFn: async () => [] }).key, key);
  } finally {
    await http.stop();
  }
});
