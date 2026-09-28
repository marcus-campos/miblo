import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
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
