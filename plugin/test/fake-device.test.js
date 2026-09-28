import { test } from 'node:test';
import assert from 'node:assert/strict';
import { startFakeDevice, LOCKOUT_MS } from './fakes/fake-device.js';
import { DeviceClient } from '../lib/device-client.js';

test('after 5 wrong codes, pairing answers 429 for 60 s', async () => {
  let t = 1_000_000;
  const dev = await startFakeDevice({ now: () => t });
  const client = new DeviceClient();
  try {
    for (let i = 0; i < 5; i++) await assert.rejects(client.pair(dev.addr, '0000', 'h'), (e) => e.status === 403);
    // even the right code; body matches the firmware's {"error":"locked","retryAfter":<s>} contract
    await assert.rejects(client.pair(dev.addr, '4827', 'h'), (e) => e.status === 429 && e.data.error === 'locked' && e.data.retryAfter === 60);
    t += LOCKOUT_MS - 1;
    await assert.rejects(client.pair(dev.addr, '0000', 'h'), (e) => e.status === 429 && e.data.retryAfter === 1);
    t += 1;
    assert.ok(await client.pair(dev.addr, '4827', 'h'));
  } finally {
    await dev.close();
  }
});

test('a correct code resets the wrong-code counter', async () => {
  const dev = await startFakeDevice({ now: () => 0 });
  const client = new DeviceClient();
  try {
    for (let i = 0; i < 4; i++) await assert.rejects(client.pair(dev.addr, '0000', 'h'), (e) => e.status === 403);
    await client.pair(dev.addr, '4827', 'h');
    for (let i = 0; i < 4; i++) await assert.rejects(client.pair(dev.addr, '0000', 'h'), (e) => e.status === 403);
    assert.ok(await client.pair(dev.addr, '4827', 'h'));
  } finally {
    await dev.close();
  }
});

test('keeps up to 4 tokens and evicts the oldest', async () => {
  const dev = await startFakeDevice();
  const client = new DeviceClient();
  try {
    const tokens = [];
    for (let i = 0; i < 5; i++) tokens.push(await client.pair(dev.addr, '4827', `host${i}`));
    assert.equal(dev.state.tokens.length, 4);
    await assert.rejects(client.pushState(dev.addr, tokens[0], { v: 1 }), (e) => e.status === 401);
    for (const tok of tokens.slice(1)) await client.pushState(dev.addr, tok, { v: 1 });
    assert.equal(dev.state.snapshots.length, 4);
  } finally {
    await dev.close();
  }
});
