import { test } from 'node:test';
import assert from 'node:assert/strict';
import { startFakeDevice } from './fakes/fake-device.js';
import { DeviceClient } from '../lib/device-client.js';

test('info, pair, push, config and reset against the fake device', async () => {
  const dev = await startFakeDevice();
  const client = new DeviceClient();
  try {
    const info = await client.info(dev.addr);
    assert.equal(info.id, 'miblo-4f2a');
    assert.equal(info.paired, false);

    await assert.rejects(client.pair(dev.addr, '0000', 'host'), (e) => e.status === 403);
    const token = await client.pair(dev.addr, '4827', 'host');
    assert.equal(token, dev.state.token);

    await client.pushState(dev.addr, token, { v: 1, seq: 1 });
    assert.deepEqual(dev.state.snapshots, [{ v: 1, seq: 1 }]);

    await client.setConfig(dev.addr, token, { mode: 'limits' });
    assert.equal(dev.state.config.mode, 'limits');

    await client.reset(dev.addr, token);
    assert.equal(dev.state.resets, 1);

    await assert.rejects(client.pushState(dev.addr, 'wrong', {}), (e) => e.status === 401);
  } finally {
    await dev.close();
  }
});

test('unreachable device rejects quickly', async () => {
  const client = new DeviceClient({ timeoutMs: 300 });
  const t0 = Date.now();
  await assert.rejects(client.info('127.0.0.1:1'));
  assert.ok(Date.now() - t0 < 2000);
});
