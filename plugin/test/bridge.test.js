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
  const post = (p, body) => fetch(base + p, { method: 'POST', headers: { 'content-type': 'application/json' }, body: typeof body === 'string' ? body : JSON.stringify(body) });
  return { base, post, stop: () => new Promise((r) => bridge.server.close(r)) };
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

    const status = await (await fetch(http.base + '/status')).json();
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
    assert.equal((await fetch(http.base + '/nope')).status, 404);
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
    const status = await (await fetch(http.base + '/status')).json();
    assert.deepEqual(status.today, { usd: 0, turns: 1, work: 20 });
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
  const json = { 'content-type': 'application/json' };
  try {
    assert.equal(await rawRequest(port, { headers: { host: `127.0.0.1:${port}` } }), 200);
    assert.equal(await rawRequest(port, { headers: { host: `localhost:${port}` } }), 200);
    assert.equal(await rawRequest(port, { headers: { host: `evil.example:${port}` } }), 403);
    assert.equal(await rawRequest(port, { headers: { host: '127.0.0.1:1' } }), 403);
    assert.equal(await rawRequest(port, { headers: {} }), 400); // node rejects HTTP/1.1 without Host
    assert.equal(await rawRequest(port, { headers: { host: `127.0.0.1:${port}`, origin: 'https://evil.example' } }), 403);
    const ev = JSON.stringify({ session_id: 's1', hook_event_name: 'SessionStart' });
    assert.equal(await rawRequest(port, { method: 'POST', path: '/event', headers: { host: `127.0.0.1:${port}`, 'content-type': 'text/plain' }, body: ev }), 415);
    assert.equal(await rawRequest(port, { method: 'POST', path: '/event', headers: { host: `127.0.0.1:${port}` }, body: ev }), 415);
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
    assert.equal((await fetch(http.base + '/shutdown', { method: 'POST' })).status, 415);
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
