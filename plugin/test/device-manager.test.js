import { test } from 'node:test';
import assert from 'node:assert/strict';
import { DeviceManager } from '../lib/device-manager.js';

function memStore(devices) {
  let list = devices.map((d) => ({ ...d }));
  return { list: () => list.map((d) => ({ ...d })), update: (id, p) => { list = list.map((d) => (d.id === id ? { ...d, ...p } : d)); } };
}

function setup({ failAddrs = new Set(), found = [] } = {}) {
  let t = 0;
  const pushes = [];
  const client = {
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
  await mgr.pushAll({});          // falha 1 → espera 1s
  await mgr.pushAll({});          // ignorado (em espera)
  assert.equal(pushes.length, 1);
  advance(1000);
  await mgr.pushAll({});          // falha 2 → espera 2s
  advance(1999);
  await mgr.pushAll({});          // ainda em espera
  assert.equal(pushes.length, 2);
  assert.equal(mgr.status()[0].online, false);
});

test('relocates via discovery after 3 failures', async () => {
  const s = setup({ failAddrs: new Set(['10.0.0.5:80']), found: [{ id: 'g1', name: 'G1', addr: '10.0.0.9:80' }] });
  for (const wait of [0, 1000, 2000]) { s.advance(wait); await s.mgr.pushAll({}); }
  assert.equal(s.discovers(), 1);
  assert.equal(s.store.list()[0].addr, '10.0.0.9:80');
  await s.mgr.pushAll({});        // sem espera após relocalizar
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
