import { test } from 'node:test';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import net from 'node:net';
import { DeviceManager } from '../lib/device-manager.js';
import { DeviceClient } from '../lib/device-client.js';
import { tokenTag, challengeMac, isLanAddr, sameSlash24 } from '../lib/relocation.js';
import { startFakeDevice, startChallengeRelay } from './fakes/fake-device.js';

// M3: after 3 failed pushes the bridge rediscovers a gadget by mDNS. The new address must prove
// it holds this computer's token (GET /api/challenge, HMAC of a nonce) before any token goes to it.

test('challengeMac is hex HMAC-SHA256(token, nonce || id || ip)', () => {
  const want = crypto.createHmac('sha256', 'tok').update('00ff' + 'miblo-4f2a' + '192.168.1.20').digest('hex');
  assert.equal(challengeMac('tok', '00ff', 'miblo-4f2a', '192.168.1.20'), want);
});

// A v2 answer as the gadget at `ip` gives it (firmware 1.14.0+).
const v2 = (n, { id = 'g1', ip, token = 'tok' }) => ({ id, ip, v: 2, mac: challengeMac(token, n, id, ip) });

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
async function relocateWith({ hit, challenge, info, oldAddr = '192.168.1.20:80', fw }) {
  let t = 0;
  const sent = [];  // [addr, token] of every request that carried a token
  const client = {
    async info(addr, token) { if (token) sent.push([addr, token]); if (addr === oldAddr) throw new Error('down'); return info(addr); },
    async pushState(addr, token) { sent.push([addr, token]); if (addr === oldAddr) throw new Error('down'); },
    async challenge(addr, nonce, tag) { return challenge(addr, nonce, tag); },
  };
  const store = memStore([{ id: 'g1', name: 'G1', addr: oldAddr, token: 'tok', ...(fw ? { fw } : {}) }]);
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
    challenge: (addr, n, tg) => { asked = { n, tg }; return v2(n, { ip: '192.168.7.9' }); },
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
  ['a wrong mac', (n) => ({ ...v2(n, { ip: '192.168.1.66' }), mac: '0'.repeat(64) })],
  ['another id', (n) => v2(n, { id: 'g2', ip: '192.168.1.66' })],
  ['a non-hex mac', (n) => ({ ...v2(n, { ip: '192.168.1.66' }), mac: 'zz' })],
  ['no body', () => null],
  // A relay: the real gadget (at .20) answered for its own address, not the one connected to.
  ['the real gadget\'s answer relayed (its own ip)', (n) => v2(n, { ip: '192.168.1.20' })],
  ['an ip that is not the one connected to, mac over it', (n) => v2(n, { ip: '192.168.1.67' })],
  ['the connected ip with a mac over another ip', (n) => ({ ...v2(n, { ip: '192.168.1.20' }), ip: '192.168.1.66' })],
  ['no ip', (n) => ({ id: 'g1', v: 2, mac: challengeMac('tok', n, 'g1', '') })],
  ['a version 1 answer (mac over n || id)', (n) => ({ id: 'g1', mac: crypto.createHmac('sha256', 'tok').update(n + 'g1').digest('hex') })],
  ['v 3', (n) => ({ ...v2(n, { ip: '192.168.1.66' }), v: 3 })],
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

for (const fw of [undefined, '1.13.2', '0.9.0']) {
  test(`a gadget last seen on an old firmware (${fw ?? 'unknown'}, no /api/challenge) on the same /24 with the same id is still followed, and asked to update`, async () => {
    const { store, mgr } = await relocateWith({
      hit: { id: 'g1', addr: '192.168.1.77:80' },
      challenge: notFound,
      info: () => ({ id: 'g1', paired: true, proto: 1 }),
      fw,
    });
    assert.equal(store.list()[0].addr, '192.168.1.77:80');
    assert.equal(mgr.status()[0].needsPair, false);
    assert.equal(mgr.status()[0].oldFirmware, true);
  });
}

// F1: a spoofer on the same /24 answers 404 and echoes the public id: a gadget known to run
// 1.14.0+ has the challenge, so a 404 is not it.
for (const fw of ['1.14.0', '1.14.1', '2.0.0']) {
  test(`a gadget last seen on ${fw} is never followed to an address without the challenge (404 + its id, same /24)`, async () => {
    let infoAsked = false;
    const { store, mgr, sent } = await relocateWith({
      hit: { id: 'g1', addr: '192.168.1.77:80' },
      challenge: notFound,
      info: () => { infoAsked = true; return { id: 'g1', paired: true, proto: 1 }; },
      fw,
    });
    assert.equal(infoAsked, false);
    assert.equal(store.list()[0].addr, '192.168.1.20:80');
    assert.ok(!sent.some(([a]) => a === '192.168.1.77:80'));
    assert.equal(mgr.status()[0].needsPair, true);
  });
}

test('the firmware version of an authenticated /api/info is kept in the device record', async () => {
  const store = memStore([{ id: 'g1', name: 'G1', addr: '192.168.1.20:80', token: 'tok' }]);
  const client = { async info(addr, token) { return token === 'tok' ? { id: 'g1', fw: '1.14.0' } : { id: 'g1', paired: true, proto: 1 }; }, async pushState() {} };
  const mgr = new DeviceManager({ client, store, now: () => 0 });
  await mgr.pushAll({});
  assert.equal(store.list()[0].fw, '1.14.0');
  assert.equal(mgr.status()[0].oldFirmware, false);
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

async function relocateTo(dev, id, { fw = '1.14.0' } = {}) {
  let t = 0;
  const store = memStore([{ id, name: 'G', addr: await closedAddr(), token: 'tok-1234', fw }]);
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

// F1: a relay forwards the plugin's nonce to the real gadget's open /api/challenge. The real one
// (here at 127.0.0.2 as far as it knows) answers for its own address, not the relay's.
for (const rewriteIp of [false, true]) {
  test(`a relay to the real gadget${rewriteIp ? ' that rewrites the ip to its own' : ''} never sees the token`, async () => {
    const real = await startFakeDevice({ id: 'miblo-4f2a', tokens: ['tok-1234'], ip: '127.0.0.2' });
    const relay = await startChallengeRelay(real.addr, { rewriteIp: rewriteIp ? '127.0.0.1' : null });
    try {
      const { store, mgr } = await relocateTo(relay, 'miblo-4f2a');
      assert.ok(real.state.challenges > 0);
      assert.deepEqual(relay.state.authHeaders, []);
      assert.deepEqual(real.state.authHeaders, []);
      assert.notEqual(store.list()[0].addr, relay.addr);
      assert.equal(mgr.status()[0].needsPair, true);
    } finally {
      await relay.close();
      await real.close();
    }
  });
}

test('a 404 + public id impostor on the same /24 never sees the token of a gadget last seen on 1.14.0', async () => {
  const imp = await startFakeDevice({ id: 'miblo-4f2a', tokens: ['someone-else'], challenge: 'none' });
  try {
    const { store, mgr } = await relocateTo(imp, 'miblo-4f2a');
    assert.deepEqual(imp.state.authHeaders, []);
    assert.notEqual(store.list()[0].addr, imp.addr);
    assert.equal(mgr.status()[0].needsPair, true);
  } finally {
    await imp.close();
  }
});
