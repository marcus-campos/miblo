import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { startFakeDevice } from './fakes/fake-device.js';
import { DeviceClient } from '../lib/device-client.js';
import { DeviceStore } from '../lib/device-store.js';
import { run } from '../bin/miblo.js';

const pluginRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

function deps(extra = {}) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-cli-'));
  return {
    dataDir: path.join(root, 'data'),
    pluginRoot,
    settingsPath: path.join(root, 'settings.json'),
    client: new DeviceClient(),
    discoverFn: async () => [],
    hostname: 'test-host',
    fetchStatus: async () => null,
    ...extra,
  };
}

test('discover lists gadgets or says none were found', async () => {
  assert.match((await run(['discover'], deps())).out, /No Miblo gadgets found/);
  const d = deps({ discoverFn: async () => [{ id: 'g1', name: 'Miblo-4F2A', addr: '10.0.0.5:80' }] });
  assert.equal((await run(['discover'], d)).out.trim(), 'g1\tMiblo-4F2A\t10.0.0.5:80');
});

test('pair stores the device; wrong code returns 2', async () => {
  const dev = await startFakeDevice();
  const d = deps();
  try {
    const bad = await run(['pair', dev.addr, '0000'], d);
    assert.equal(bad.code, 2);
    assert.match(bad.out, /Wrong pairing code/);

    const ok = await run(['pair', dev.addr, '4827'], d);
    assert.equal(ok.code, 0);
    assert.match(ok.out, /Paired with Miblo-4F2A \(miblo-4f2a\)/);
    assert.ok(!ok.out.includes(dev.state.token));
    const [saved] = new DeviceStore(d.dataDir).list();
    assert.equal(saved.token, dev.state.token);
  } finally {
    await dev.close();
  }
});

test('pair adds :80 to a bare IP', async () => {
  const seen = [];
  const client = { info: async (addr) => { seen.push(addr); return { id: 'g', name: 'G' }; }, pair: async () => 'tok' };
  await run(['pair', '192.168.0.42', '1234'], deps({ client }));
  assert.equal(seen[0], '192.168.0.42:80');
});

test('mode sends config to all or to one gadget; invalid mode returns 2', async () => {
  const dev = await startFakeDevice();
  const d = deps();
  try {
    await run(['pair', dev.addr, '4827'], d);
    assert.equal((await run(['mode', 'banana'], d)).code, 2);
    const r = await run(['mode', 'limits'], d);
    assert.match(r.out, /Mode set to limits on 1 gadget/);
    assert.equal(dev.state.config.mode, 'limits');
  } finally {
    await dev.close();
  }
});

test('reset sends the command and forgets the pairing', async () => {
  const dev = await startFakeDevice();
  const d = deps();
  try {
    await run(['pair', dev.addr, '4827'], d);
    const r = await run(['reset', 'miblo-4f2a'], d);
    assert.match(r.out, /Factory reset sent to Miblo-4F2A/);
    assert.equal(dev.state.resets, 1);
    assert.deepEqual(new DeviceStore(d.dataDir).list(), []);
  } finally {
    await dev.close();
  }
});

test('link and unlink the statusline', async () => {
  const d = deps();
  assert.equal((await run(['link-statusline'], d)).out.trim(), 'Statusline linked.');
  assert.equal((await run(['link-statusline'], d)).out.trim(), 'Statusline already linked.');
  assert.equal((await run(['unlink-statusline'], d)).out.trim(), 'Statusline unlinked.');
  assert.equal((await run(['unlink-statusline'], d)).out.trim(), 'Statusline was not linked.');
});

test('status reports bridge state, statusline and devices without tokens', async () => {
  const d = deps();
  new DeviceStore(d.dataDir).upsert({ id: 'g1', name: 'G1', addr: 'x:80', token: 'secret-token' });
  const r = await run(['status'], d);
  assert.ok(!r.out.includes('secret-token'));
  const s = JSON.parse(r.out);
  assert.equal(s.bridge, 'stopped');
  assert.equal(s.statusline, 'not linked');
  assert.equal(s.devices[0].id, 'g1');
});

test('unknown subcommand prints usage with code 2', async () => {
  const r = await run(['wat'], deps());
  assert.equal(r.code, 2);
  assert.match(r.out, /Usage/);
});

test('discover and pair sanitize gadget-provided strings', async () => {
  const d = deps({ discoverFn: async () => [{ id: 'g1`x`', name: 'Evil\n\u001b[31mname-that-is-way-too-long', addr: '10.0.0.5:80' }, { id: '!!!', name: 'x', addr: '1.2.3.4:80' }] });
  assert.equal((await run(['discover'], d)).out.trim(), 'g1x\tEvil31mname-that-is-\t10.0.0.5:80');

  const client = { info: async () => ({ id: 'id<script>', name: 'Na\u0000me; ls' }), pair: async () => 'tok' };
  const p = deps({ client });
  const r = await run(['pair', '10.0.0.7', '1234'], p);
  assert.equal(r.out.trim(), 'Paired with Name ls (idscript) at 10.0.0.7:80.');
  assert.deepEqual(new DeviceStore(p.dataDir).list().map(({ id, name }) => ({ id, name })), [{ id: 'idscript', name: 'Name ls' }]);

  const bad = await run(['pair', '10.0.0.7', '1234'], deps({ client: { info: async () => ({ id: '###' }), pair: async () => 'tok' } }));
  assert.equal(bad.code, 1);
});

test('status survives a corrupt settings.json', async () => {
  const d = deps();
  fs.writeFileSync(d.settingsPath, '{broken');
  const r = await run(['status'], d);
  assert.equal(r.code, 0);
  assert.equal(JSON.parse(r.out).statusline, 'settings.json unreadable');
});
