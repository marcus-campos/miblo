import { test } from 'node:test';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import net from 'node:net';
import { DeviceManager } from '../lib/device-manager.js';
import { DeviceClient } from '../lib/device-client.js';
import { tokenTag, challengeMac, isLanAddr, sameSlash24 } from '../lib/relocation.js';
import { startFakeDevice } from './fakes/fake-device.js';

// M3: after 3 failed pushes the bridge rediscovers a gadget by mDNS. The new address must prove
// it holds this computer's token (GET /api/challenge, HMAC of a nonce) before any token goes to it.

test('challengeMac is hex HMAC-SHA256(token, nonce || id)', () => {
  const want = crypto.createHmac('sha256', 'tok').update('00ff' + 'miblo-4f2a').digest('hex');
  assert.equal(challengeMac('tok', '00ff', 'miblo-4f2a'), want);
});

test('only private, link-local or CGNAT IPv4 on port 80 is a gadget address', () => {
  for (const a of ['10.1.2.3:80', '172.16.0.9:80', '172.31.255.1:80', '192.168.1.20:80', '169.254.3.4:80', '100.64.0.1:80', '100.127.9.9:80']) {
    assert.ok(isLanAddr(a), a);
  }
  for (const a of ['127.0.0.1:80', '192.168.1.20:8080', '8.8.8.8:80', '172.32.0.1:80', '100.128.0.1:80', '0.0.0.0:80',
    '224.0.0.251:80', '192.168.1.256:80', 'evil.example:80', '[::1]:80', '192.168.1.20', '']) {
    assert.ok(!isLanAddr(a), a);
  }
});

test('sameSlash24 compares the first three octets', () => {
  assert.ok(sameSlash24('192.168.1.20:80', '192.168.1.77:80'));
  assert.ok(!sameSlash24('192.168.1.20:80', '192.168.2.20:80'));
  assert.ok(!sameSlash24('x:80', 'x:80'));
});

function memStore(devices) {
  let list = devices.map((d) => ({ ...d }));
  return { list: () => list.map((d) => ({ ...d })), update: (id, p) => { list = list.map((d) => (d.id === id ? { ...d, ...p } : d)); } };
}

// The old address is down; discovery answers `hit`; `answers` says how the new address responds.
async function relocateWith({ hit, challenge, info, oldAddr = '192.168.1.20:80' }) {
  let t = 0;
  const sent = [];  // [addr, token] of every request that carried a token
  const client = {
    async info(addr, token) { if (token) sent.push([addr, token]); if (addr === oldAddr) throw new Error('down'); return info(addr); },
    async pushState(addr, token) { sent.push([addr, token]); if (addr === oldAddr) throw new Error('down'); },
    async challenge(addr, nonce, tag) { return challenge(addr, nonce, tag); },
  };
  const store = memStore([{ id: 'g1', name: 'G1', addr: oldAddr, token: 'tok' }]);
  const mgr = new DeviceManager({ client, store, now: () => t, discover: async () => [hit] });
  for (const wait of [0, 1000, 2000]) { t += wait; await mgr.pushAll({}); }
  await mgr.pushAll({});
  return { store, mgr, sent };
}
const notFound = () => { const e = new Error('404'); e.status = 404; throw e; };

test('a new address that answers the challenge right gets the gadget', async () => {
  let asked;
  const { store, mgr, sent } = await relocateWith({
    hit: { id: 'g1', addr: '192.168.7.9:80' },
    challenge: (addr, n, tg) => { asked = { n, tg }; return { id: 'g1', mac: challengeMac('tok', n, 'g1') }; },
    info: () => ({ id: 'g1' }),
  });
  assert.match(asked.n, /^[0-9a-f]{32}$/);
  assert.equal(asked.tg, tokenTag('tok'));
  assert.equal(store.list()[0].addr, '192.168.7.9:80');
  assert.ok(sent.some(([a]) => a === '192.168.7.9:80'));
  assert.equal(mgr.status()[0].online, true);
  assert.equal(mgr.status()[0].needsPair, false);
});

for (const [what, reply] of [
  ['a wrong mac', (n) => ({ id: 'g1', mac: '0'.repeat(64) })],
  ['another id', (n) => ({ id: 'g2', mac: challengeMac('tok', n, 'g2') })],
  ['a non-hex mac', (n) => ({ id: 'g1', mac: 'zz' })],
  ['no body', () => null],
]) {
  test(`a new address answering the challenge with ${what} never gets the token`, async () => {
    const { store, mgr, sent } = await relocateWith({
      hit: { id: 'g1', addr: '192.168.1.66:80' },
      challenge: (addr, n) => reply(n),
      info: () => ({ id: 'g1', paired: true, proto: 1 }),
    });
    assert.equal(store.list()[0].addr, '192.168.1.20:80');
    assert.ok(!sent.some(([a]) => a === '192.168.1.66:80'));
    assert.equal(mgr.status()[0].online, false);
    assert.equal(mgr.status()[0].needsPair, true);
  });
}

test('an old firmware (no /api/challenge) on the same /24 with the same id is still followed', async () => {
  const { store, mgr } = await relocateWith({
    hit: { id: 'g1', addr: '192.168.1.77:80' },
    challenge: notFound,
    info: () => ({ id: 'g1', paired: true, proto: 1 }),
  });
  assert.equal(store.list()[0].addr, '192.168.1.77:80');
  assert.equal(mgr.status()[0].needsPair, false);
});

test('an old firmware on another /24 is not followed: run /miblo:pair', async () => {
  const { store, mgr, sent } = await relocateWith({
    hit: { id: 'g1', addr: '192.168.9.77:80' },
    challenge: notFound,
    info: () => ({ id: 'g1', paired: true, proto: 1 }),
  });
  assert.equal(store.list()[0].addr, '192.168.1.20:80');
  assert.ok(!sent.some(([a]) => a === '192.168.9.77:80'));
  assert.equal(mgr.status()[0].needsPair, true);
});

test('an old firmware on the same /24 reporting another id is not followed', async () => {
  const { store, mgr } = await relocateWith({
    hit: { id: 'g1', addr: '192.168.1.77:80' },
    challenge: notFound,
    info: () => ({ id: 'g9', paired: true, proto: 1 }),
  });
  assert.equal(store.list()[0].addr, '192.168.1.20:80');
  assert.equal(mgr.status()[0].needsPair, true);
});

test('an address outside the LAN (loopback, public, another port) is never challenged nor followed', async () => {
  for (const addr of ['127.0.0.1:80', '8.8.8.8:80', '192.168.1.77:8080']) {
    let challenged = false;
    const { store, mgr, sent } = await relocateWith({
      hit: { id: 'g1', addr },
      challenge: () => { challenged = true; return null; },
      info: () => ({ id: 'g1' }),
    });
    assert.equal(challenged, false, addr);
    assert.equal(store.list()[0].addr, '192.168.1.20:80');
    assert.ok(!sent.some(([a]) => a === addr));
    assert.equal(mgr.status()[0].needsPair, true);
  }
});

test('a challenge that times out leaves the gadget where it was, without asking to pair again', async () => {
  const { store, mgr } = await relocateWith({
    hit: { id: 'g1', addr: '192.168.1.77:80' },
    challenge: () => { throw new Error('timeout'); },
    info: () => ({ id: 'g1' }),
  });
  assert.equal(store.list()[0].addr, '192.168.1.20:80');
  assert.equal(mgr.status()[0].needsPair, false);
});

// ---- With the fake gadget over real HTTP (both on 127.0.0.1, so the LAN check is relaxed) ----

async function closedAddr() {
  const srv = net.createServer();
  await new Promise((r) => srv.listen(0, '127.0.0.1', r));
  const addr = `127.0.0.1:${srv.address().port}`;
  await new Promise((r) => srv.close(r));
  return addr;
}

async function relocateTo(dev, id) {
  let t = 0;
  const store = memStore([{ id, name: 'G', addr: await closedAddr(), token: 'tok-1234' }]);
  const mgr = new DeviceManager({
    client: new DeviceClient({ timeoutMs: 1000 }), store, now: () => t,
    discover: async () => [{ id, addr: dev.addr }], addrOk: () => true,
  });
  for (const wait of [0, 1000, 2000]) { t += wait; await mgr.pushAll({ v: 1, sessions: [] }); }
  await mgr.pushAll({ v: 1, sessions: [] });
  return { store, mgr };
}

test('the real gadget at a new address proves itself and gets the pushes', async () => {
  const dev = await startFakeDevice({ id: 'miblo-4f2a', tokens: ['tok-1234'] });
  try {
    const { store, mgr } = await relocateTo(dev, 'miblo-4f2a');
    assert.equal(store.list()[0].addr, dev.addr);
    assert.equal(dev.state.snapshots.length, 1);
    assert.equal(mgr.status()[0].online, true);
  } finally {
    await dev.close();
  }
});

test('an impostor that answers mDNS with the right id but a wrong mac never sees the token', async () => {
  const imp = await startFakeDevice({ id: 'miblo-4f2a', tokens: ['someone-else'], challenge: 'forge' });
  try {
    const { store, mgr } = await relocateTo(imp, 'miblo-4f2a');
    assert.ok(imp.state.challenges > 0);
    assert.deepEqual(imp.state.authHeaders, []);
    assert.notEqual(store.list()[0].addr, imp.addr);
    assert.equal(mgr.status()[0].needsPair, true);
  } finally {
    await imp.close();
  }
});
