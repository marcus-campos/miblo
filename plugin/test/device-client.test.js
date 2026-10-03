import { test } from 'node:test';
import assert from 'node:assert/strict';
import { startFakeDevice } from './fakes/fake-device.js';
import { DeviceClient, isBusy, busyLine } from '../lib/device-client.js';

test('info, pair, push, config and reset against the fake device', async () => {
  const dev = await startFakeDevice();
  const client = new DeviceClient();
  try {
    const info = await client.info(dev.addr);
    assert.equal(info.id, 'miblo-4f2a');
    assert.equal(info.paired, false);

    await assert.rejects(client.pair(dev.addr, '0000', 'host'), (e) => e.status === 403 && e.data?.error === 'bad code');
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

test('info sends the pairing token; a paired gadget tells anyone else only its id', async () => {
  const dev = await startFakeDevice();
  const client = new DeviceClient();
  const seen = [];
  const spy = new DeviceClient({ fetchImpl: (url, opts) => { seen.push(opts.headers); return fetch(url, opts); } });
  try {
    const token = await client.pair(dev.addr, '4827', 'host');
    assert.deepEqual(await client.info(dev.addr), { id: 'miblo-4f2a', paired: true, proto: 1 });
    assert.deepEqual(await client.info(dev.addr, 'wrong'), { id: 'miblo-4f2a', paired: true, proto: 1 });
    const full = await spy.info(dev.addr, token);
    assert.equal(full.name, 'Miblo-4F2A');
    assert.equal(full.fw, '0.0.0-fake');
    assert.equal(seen[0].authorization, `Bearer ${token}`);
    await spy.info(dev.addr);
    assert.equal(seen[1].authorization, undefined);
  } finally {
    await dev.close();
  }
});

// ---- 503 busy (heapLowForRequest): refused before anything is done ----
const quick = (delays) => new DeviceClient({ busyRetryMs: [1, 1], sleep: async (ms) => { delays?.push(ms); } });

test('a busy (503) read is retried twice with a growing backoff', async () => {
  const dev = await startFakeDevice({ busy: 2 });
  const delays = [];
  try {
    const client = new DeviceClient({ sleep: async (ms) => { delays.push(ms); } });
    const info = await client.info(dev.addr);
    assert.equal(info.id, 'miblo-4f2a');
    assert.equal(dev.state.busyHits, 2);
    assert.deepEqual(delays, [400, 800]);
  } finally {
    await dev.close();
  }
});

test('still busy after the retries: rejects with status 503 that isBusy recognises', async () => {
  const dev = await startFakeDevice({ busy: 10 });
  try {
    await assert.rejects(quick().info(dev.addr), (e) => e.status === 503 && isBusy(e));
    assert.equal(dev.state.busyHits, 3);
  } finally {
    await dev.close();
  }
});

test('GET /api/remind is retried too', async () => {
  const dev = await startFakeDevice({ busy: 1, busyPath: '/api/remind' });
  try {
    const token = await quick().pair(dev.addr, '4827', 'host');
    const r = await quick().reminders(dev.addr, token);
    assert.ok(Array.isArray(r.items));
    assert.equal(dev.state.busyHits, 1);
  } finally {
    await dev.close();
  }
});

test('pair is retried on 503 (refused before the code was checked) and pairs once', async () => {
  const dev = await startFakeDevice({ busy: 2, busyPath: '/api/pair' });
  try {
    const token = await quick().pair(dev.addr, '4827', 'host');
    assert.equal(token, dev.state.token);
    assert.equal(dev.state.tokens.length, 1);
    assert.equal(dev.state.badCodes, 0);
  } finally {
    await dev.close();
  }
});

test('pair is never resent after no answer: the code may already be used up', async () => {
  let calls = 0;
  const client = new DeviceClient({
    busyRetryMs: [1, 1], sleep: async () => {},
    fetchImpl: async () => { calls++; throw new TypeError('fetch failed'); },
  });
  await assert.rejects(client.pair('10.0.0.9', '4827', 'host'), (e) => !e.status);
  assert.equal(calls, 1);
});

test('a busy write is not retried (the caller decides, e.g. the alerts-only snapshot)', async () => {
  const dev = await startFakeDevice();
  try {
    const token = await quick().pair(dev.addr, '4827', 'host');
    Object.assign(dev.state, { busyLeft: 1, busyPath: '/api/config' });
    await assert.rejects(quick().setConfig(dev.addr, token, { mode: 'limits' }), (e) => isBusy(e));
    assert.equal(dev.state.busyHits, 1);
  } finally {
    await dev.close();
  }
});

test('busyLine names the gadget', () => {
  assert.equal(busyLine('Desk'), 'Desk is busy right now — try again in a moment.');
});

// ---- A connection dropped before any reply: a snapshot push is idempotent, so it is resent once ----
const dropped = (code) => Object.assign(new TypeError('fetch failed'), { cause: Object.assign(new Error(code), { code }) });

test('pushState is resent once at once when the connection drops before any reply', async () => {
  for (const err of [dropped('ECONNRESET'), dropped('UND_ERR_SOCKET'), dropped('EPIPE'), Object.assign(new Error('socket hang up'), { code: 'ECONNRESET' })]) {
    let calls = 0;
    const client = new DeviceClient({ fetchImpl: async () => { calls++; if (calls === 1) throw err; return new Response('{"ok":true}'); } });
    assert.deepEqual(await client.pushState('10.0.0.5:80', 't', { v: 1 }), { ok: true });
    assert.equal(calls, 2, String(err.cause?.code ?? err.message));
  }
});

test('pushState is resent only once, and never after a refusal, a timeout or an HTTP answer', async () => {
  let calls = 0;
  const twice = new DeviceClient({ fetchImpl: async () => { calls++; throw dropped('ECONNRESET'); } });
  await assert.rejects(twice.pushState('a:80', 't', {}));
  assert.equal(calls, 2);
  for (const err of [dropped('ECONNREFUSED'), new DOMException('timeout', 'TimeoutError')]) {
    calls = 0;
    const c = new DeviceClient({ fetchImpl: async () => { calls++; throw err; } });
    await assert.rejects(c.pushState('a:80', 't', {}));
    assert.equal(calls, 1, err.name);
  }
  calls = 0;
  const http500 = new DeviceClient({ fetchImpl: async () => { calls++; return new Response('{}', { status: 500 }); } });
  await assert.rejects(http500.pushState('a:80', 't', {}), (e) => e.status === 500);
  assert.equal(calls, 1);
});

test('over real HTTP: a push whose first connection is reset still lands, once', async () => {
  const dev = await startFakeDevice({ tokens: ['t'] });
  const net = await import('node:net');
  // A front that resets the first connection and passes the next ones to the gadget.
  let conns = 0;
  const front = net.createServer((sock) => {
    if (conns++ === 0) { sock.once('data', () => sock.resetAndDestroy()); return; }
    const [host, port] = dev.addr.split(':');
    const up = net.connect(Number(port), host);
    sock.pipe(up).pipe(sock);
    up.on('error', () => sock.destroy());
    sock.on('error', () => up.destroy());
  });
  await new Promise((r) => front.listen(0, '127.0.0.1', r));
  try {
    await new DeviceClient().pushState(`127.0.0.1:${front.address().port}`, 't', { v: 1, seq: 7, sessions: [] });
    assert.equal(dev.state.snapshots.length, 1);
    assert.equal(dev.state.snapshots[0].seq, 7);
  } finally {
    await new Promise((r) => front.close(r));
    await dev.close();
  }
});
