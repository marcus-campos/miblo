import { test } from 'node:test';
import assert from 'node:assert/strict';
import { DeviceManager } from '../lib/device-manager.js';
import { challengeMac } from '../lib/relocation.js';

function memStore(devices) {
  let list = devices.map((d) => ({ ...d }));
  return { list: () => list.map((d) => ({ ...d })), update: (id, p) => { list = list.map((d) => (d.id === id ? { ...d, ...p } : d)); } };
}

function setup({ failAddrs = new Set(), found = [] } = {}) {
  let t = 0;
  const pushes = [];
  const client = {
    // the gadget at its new address proves it holds the token (relocation.js)
    async challenge(addr, nonce) { const ip = addr.split(':')[0]; return { id: 'g1', ip, v: 2, mac: challengeMac('t', nonce, 'g1', ip) }; },
    async pushState(addr, token, snap) {
      pushes.push(addr);
      if (failAddrs.has(addr)) { const e = new Error('down'); e.status = addr.endsWith(':401') ? 401 : undefined; throw e; }
    },
  };
  let discovers = 0;
  const store = memStore([{ id: 'g1', name: 'G1', addr: '10.0.0.5:80', token: 't' }]);
  const mgr = new DeviceManager({ client, store, now: () => t, discover: async () => { discovers++; return found; } });
  return { mgr, store, pushes, advance: (ms) => { t += ms; }, discovers: () => discovers };
}

test('pushes to every paired device and marks it online', async () => {
  const { mgr, pushes } = setup();
  await mgr.pushAll({ v: 1 });
  assert.deepEqual(pushes, ['10.0.0.5:80']);
  assert.equal(mgr.status()[0].online, true);
});

test('backs off after failures', async () => {
  const { mgr, pushes, advance } = setup({ failAddrs: new Set(['10.0.0.5:80']) });
  await mgr.pushAll({});          // failure 1 -> wait 1s
  await mgr.pushAll({});          // skipped (backing off)
  assert.equal(pushes.length, 1);
  advance(1000);
  await mgr.pushAll({});          // failure 2 -> wait 2s
  advance(1999);
  await mgr.pushAll({});          // still backing off
  assert.equal(pushes.length, 2);
  assert.equal(mgr.status()[0].online, false);
});

test('relocates via discovery after 3 failures', async () => {
  const s = setup({ failAddrs: new Set(['10.0.0.5:80']), found: [{ id: 'g1', name: 'G1', addr: '10.0.0.9:80' }] });
  for (const wait of [0, 1000, 2000]) { s.advance(wait); await s.mgr.pushAll({}); }
  assert.equal(s.discovers(), 1);
  assert.equal(s.store.list()[0].addr, '10.0.0.9:80');
  await s.mgr.pushAll({});        // no wait after relocating
  assert.equal(s.pushes.at(-1), '10.0.0.9:80');
  assert.equal(s.mgr.status()[0].online, true);
});

test('flags unauthorized devices', async () => {
  let t = 0;
  const store = memStore([{ id: 'g1', name: 'G1', addr: 'x:401', token: 't' }]);
  const client = { async pushState() { const e = new Error('401'); e.status = 401; throw e; } };
  const mgr = new DeviceManager({ client, store, now: () => t });
  await mgr.pushAll({});
  assert.equal(mgr.status()[0].unauthorized, true);
});

test('reads the gadget caps with its pairing token and tolerates the reduced /api/info', async () => {
  const calls = [];
  const pushes = [];
  const client = {
    async info(addr, token) { calls.push([addr, token]); return { id: 'g1', paired: true, proto: 1 }; },
    async pushState(addr, token, snap) { pushes.push(snap); },
  };
  const store = memStore([{ id: 'g1', name: 'G1', addr: '10.0.0.5:80', token: 't' }]);
  const mgr = new DeviceManager({ client, store, now: () => 0 });
  await mgr.pushAll({ v: 1, sessions: [] });
  assert.deepEqual(calls, [['10.0.0.5:80', 't']]);
  assert.equal(pushes.length, 1);
  assert.equal(mgr.status()[0].online, true);
});

// Live time zone offsets (tz-offsets.js): each gadget gets those of its own zones (/api/info tz
// and tz2), worked out on this computer; an older firmware (no tz in /api/info) gets none.
function zoneSetup(infos) {
  const pushes = [];
  let t = Date.parse('2026-10-02T12:00:00Z');
  const client = {
    async info(addr) { return infos[addr]; },
    async pushState(addr, token, snap) { pushes.push([addr, snap]); },
  };
  const store = memStore(Object.keys(infos).map((addr, i) => ({ id: `g${i}`, name: `G${i}`, addr, token: 't' })));
  const zones = { withZones: (snap, list, nowMs) => (list.length ? { ...snap, tz: list.map((z) => ({ z, at: nowMs })) } : snap) };
  const mgr = new DeviceManager({ client, store, zones, now: () => t });
  return { mgr, pushes, advance: (ms) => { t += ms; } };
}

test('sends each gadget the live offsets of its own zones', async () => {
  const { mgr, pushes } = zoneSetup({
    'a:80': { id: 'g0', fw: '1.11.0', tz: 'Europe/Lisbon', tz2: 'Asia/Tokyo' },
    'b:80': { id: 'g1', fw: '1.11.0', tz: 'America/Sao_Paulo', tz2: '' },
    'c:80': { id: 'g2', fw: '1.10.1' },  // older firmware: no tz in /api/info
  });
  await mgr.pushAll({ v: 1, sessions: [] });
  const by = Object.fromEntries(pushes);
  assert.deepEqual(by['a:80'].tz.map((e) => e.z), ['Europe/Lisbon', 'Asia/Tokyo']);
  assert.deepEqual(by['b:80'].tz.map((e) => e.z), ['America/Sao_Paulo']);
  assert.equal(by['c:80'].tz, undefined);
});

test('live offsets are worked out at push time; the zones are read with the caps', async () => {
  const { mgr, pushes, advance } = zoneSetup({ 'a:80': { id: 'g0', fw: '1.11.0', tz: 'Europe/Lisbon' } });
  await mgr.pushAll({ v: 1, sessions: [] });
  advance(60_000);
  await mgr.pushAll({ v: 1, sessions: [] });
  assert.equal(pushes[1][1].tz[0].at - pushes[0][1].tz[0].at, 60_000);
});

test('the live offsets count toward the gadget\'s byte cap', async () => {
  const pushes = [];
  const client = {
    async info() { return { id: 'g1', fw: '1.11.0', tz: 'Europe/Lisbon', maxSessions: 20, maxBytes: 400 }; },
    async pushState(addr, token, snap) { pushes.push(snap); },
  };
  const store = memStore([{ id: 'g1', name: 'G1', addr: 'a:80', token: 't' }]);
  const zones = { withZones: (snap) => ({ ...snap, tz: [{ z: 'Europe/Lisbon', pad: 'x'.repeat(200) }] }) };
  const mgr = new DeviceManager({ client, store, zones, now: () => 0 });
  const sessions = [1, 2, 3].map((i) => ({ id: `s${i}`, name: 'n'.repeat(40) }));
  await mgr.pushAll({ v: 1, sessions, more: 0 });
  assert.ok(Buffer.byteLength(JSON.stringify(pushes[0])) <= 400);
  assert.ok(pushes[0].tz);
  assert.ok(pushes[0].sessions.length < 3);
});

test('a zone that fails to work out never fails the push', async () => {
  const pushes = [];
  const client = {
    async info() { return { id: 'g1', fw: '1.11.0', tz: 'Europe/Lisbon' }; },
    async pushState(addr, token, snap) { pushes.push(snap); },
  };
  const store = memStore([{ id: 'g1', name: 'G1', addr: 'a:80', token: 't' }]);
  const zones = { withZones: () => { throw new Error('corrupt zone file'); } };
  const mgr = new DeviceManager({ client, store, zones, now: () => 0 });
  await mgr.pushAll({ v: 1, sessions: [] });
  assert.equal(pushes.length, 1);
  assert.equal(pushes[0].tz, undefined);
  assert.equal(mgr.status()[0].online, true);
});
