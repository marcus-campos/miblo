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
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-daily-'));
  return {
    dataDir: path.join(root, 'data'),
    pluginRoot,
    settingsPath: path.join(root, 'settings.json'),
    client: new DeviceClient(),
    discoverFn: async () => [],
    hostname: 'test-host',
    fetchStatus: async () => null,
    openUrl: async () => {},
    ...extra,
  };
}

// Pairs fake gadgets straight into the store, with the token each one accepts.
function pair(d, ...devs) {
  const store = new DeviceStore(d.dataDir);
  devs.forEach((dev, i) => {
    const token = `tok-${i}`;
    dev.state.tokens.push(token);
    store.upsert({ id: dev.id, name: dev.name, addr: dev.addr, token });
  });
}

async function fake(opts = {}) {
  const dev = await startFakeDevice(opts);
  return { ...dev, id: opts.id ?? 'miblo-4f2a', name: opts.name ?? 'Miblo-4F2A' };
}

test('focus starts 25/5/4 everywhere, shows status, stops', async () => {
  const a = await fake({ id: 'miblo-aaaa', name: 'Amon' });
  const b = await fake({ id: 'miblo-bbbb', name: 'Shiru' });
  const d = deps();
  try {
    pair(d, a, b);
    assert.match((await run(['focus'], d)).out, /Focus on: 25 min, breaks of 5, 4 rounds on Amon, Shiru\./);
    assert.deepEqual(a.state.focus, { focusMin: 25, breakMin: 5, rounds: 4 });
    assert.match((await run(['focus'], d)).out, /round 1\/4, \d+ min left \(Amon\)/);
    const r = await run(['focus', '50'], d);
    assert.equal(r.code, 0);
    assert.match(r.out, /Focus on: 50 min, breaks of 10, 4 rounds/);
    assert.deepEqual(b.state.focus, { focusMin: 50 });  // the gadget derives the break
    assert.deepEqual((await run(['focus', '25', '5', '2'], d), a.state.focus), { focusMin: 25, breakMin: 5, rounds: 2 });
    for (const bad of [['4'], ['121'], ['25', '0'], ['25', '61'], ['25', '5', '13'], ['25', '5', '4', '1'], ['ten'], ['2.5']]) {
      assert.equal((await run(['focus', ...bad], d)).code, 2, bad.join(' '));
    }
    assert.match((await run(['focus', 'stop'], d)).out, /Focus off on Amon, Shiru\./);
    assert.match((await run(['focus', 'status'], d)).out, /No focus on Amon, Shiru\./);
  } finally { await a.close(); await b.close(); }
});

test('an old firmware gets told to update', async () => {
  const a = await fake({ name: 'Amon', legacy: true });
  const d = deps();
  try {
    pair(d, a);
    for (const cmd of [['focus'], ['meeting'], ['find'], ['timer', '5'], ['say', 'oi'], ['focus', 'status']]) {
      const r = await run(cmd, d);
      assert.equal(r.code, 1, cmd.join(' '));
      assert.match(r.out, /Amon does not support this yet: update it with \/miblo:update\./);
    }
  } finally { await a.close(); }
});

test('say validates text like the gadget', async () => {
  const a = await fake({ name: 'Amon' });
  const d = deps();
  try {
    pair(d, a);
    assert.equal((await run(['say', 'x'.repeat(41)], d)).code, 2);
    assert.equal((await run(['say', '一'.repeat(16)], d)).code, 2);  // 48 bytes
    assert.equal((await run(['say', '一'.repeat(15)], d)).code, 0);  // 45 bytes
    assert.equal((await run(['say', 'a\u0007b'], d)).code, 2);
    assert.equal((await run(['say', '\ud800x'], d)).code, 2);  // a lone surrogate
    assert.equal((await run(['say', '   '], d)).code, 2);
    assert.equal((await run(['say'], d)).code, 2);
    for (const m of ['0', '481', 'x']) assert.equal((await run(['say', 'oi', '--min', m], d)).code, 2);
    assert.equal((await run(['say', 'oi', '--min'], d)).code, 2);
    const r = await run(['say', 'volto', 'em', '10', 'min'], d);
    assert.match(r.out, /Message on Amon for 30 min: "volto em 10 min"\./);
    assert.deepEqual(a.state.say, { text: 'volto em 10 min' });
    assert.match((await run(['say', 'almoço\n', '--min', '60', 'já', 'volto'], d)).out, /for 60 min: "almoço já volto"/);
    assert.deepEqual(a.state.say, { text: 'almoço já volto', min: 60 });
    assert.equal((await run(['say', 'x'.repeat(40)], d)).code, 0);
    assert.match((await run(['say', 'off'], d)).out, /Message off on Amon\./);
    assert.deepEqual(a.state.say, { off: true });
  } finally { await a.close(); }
});

test('meeting, timer and find; --id restricts to one gadget', async () => {
  const a = await fake({ id: 'miblo-aaaa', name: 'Amon' });
  const b = await fake({ id: 'miblo-bbbb', name: 'Shiru' });
  const d = deps();
  try {
    pair(d, a, b);
    assert.match((await run(['meeting', '30'], d)).out, /Meeting mode on for 30 min on Amon, Shiru\./);
    assert.deepEqual(a.state.meeting, { min: 30 });
    assert.match((await run(['meeting'], d)).out, /for 60 min/);
    assert.deepEqual(a.state.meeting, {});
    assert.match((await run(['meeting', 'off'], d)).out, /Meeting mode off/);
    assert.deepEqual(b.state.meeting, { off: true });
    for (const bad of [['0'], ['481'], ['soon'], ['30', '40']]) assert.equal((await run(['meeting', ...bad], d)).code, 2);

    assert.match((await run(['timer', '10'], d)).out, /Timer: 10 min on Amon, Shiru\./);
    assert.deepEqual(a.state.timer, { min: 10 });
    for (const bad of [[], ['0'], ['181'], ['1.5']]) assert.equal((await run(['timer', ...bad], d)).code, 2);
    assert.match((await run(['timer', 'stop'], d)).out, /Timer stopped/);
    assert.deepEqual(b.state.timer, { stop: true });

    const f = await run(['find', '--id', 'miblo-bbbb'], d);
    assert.match(f.out, /on Shiru\./);
    assert.equal(a.state.finds, 0);
    assert.equal(b.state.finds, 1);
    assert.equal((await run(['find', '--id', 'miblo-zzzz'], d)).code, 2);
    assert.equal((await run(['find', '--id'], d)).code, 2);
    assert.match((await run(['say', 'oi', '--id', 'miblo-aaaa'], d)).out, /Message on Amon for/);
    assert.equal(b.state.say, null);
  } finally { await a.close(); await b.close(); }
});

test('one gadget offline or unpaired does not hide the others; never prints tokens', async () => {
  const a = await fake({ id: 'miblo-aaaa', name: 'Amon' });
  const d = deps();
  try {
    pair(d, a);
    const store = new DeviceStore(d.dataDir);
    store.upsert({ id: 'miblo-cccc', name: 'Gone', addr: '127.0.0.1:9', token: 'secret-gone' });
    store.upsert({ id: 'miblo-dddd', name: 'Stale', addr: a.addr, token: 'secret-stale' });
    const r = await run(['timer', '5'], d);
    assert.equal(r.code, 0);
    assert.match(r.out, /Timer: 5 min on Amon\.\nGone is offline\.\nStale no longer knows this computer \(run \/miblo:pair again\)\./);
    assert.ok(!/secret|tok-/.test(r.out));
    assert.equal((await run(['timer', '5', '--id', 'miblo-cccc'], d)).code, 1);
  } finally { await a.close(); }
});

test('no paired gadget', async () => {
  const r = await run(['find'], deps());
  assert.equal(r.code, 2);
  assert.match(r.out, /No paired Miblo gadgets/);
});

test('a field the gadget rejects is explained', async () => {
  const client = { say: async () => { const e = new Error('400'); e.status = 400; e.data = { error: 'invalid', field: 'text' }; throw e; } };
  const d = deps({ client });
  new DeviceStore(d.dataDir).upsert({ id: 'miblo-aaaa', name: 'Amon', addr: 'x:80', token: 't' });
  const r = await run(['say', 'oi'], d);
  assert.equal(r.code, 1);
  assert.match(r.out, /Amon rejected the text \(up to 40 characters, one line\)\./);
});

// The fake device is the executable contract of the firmware's daily-life handlers
// (firmware/test/test_focus, test_meeting, test_notes): the same bodies, the same answers.
test('the fake device validates daily-life requests like the firmware', async () => {
  const dev = await startFakeDevice({ tokens: ['t'] });
  const c = new DeviceClient();
  const rejects = (p, status, field) => assert.rejects(p, (e) => e.status === status && e.data?.field === field);
  try {
    for (const [body, field] of [[{ focusMin: 4 }, 'focusMin'], [{ focusMin: 121 }, 'focusMin'], [{ breakMin: 0 }, 'breakMin'],
      [{ rounds: 13 }, 'rounds'], [{ focusMin: '25' }, 'focusMin'], [{ stop: 1 }, 'stop']]) {
      await rejects(c.focus(dev.addr, 't', body), 400, field);
    }
    for (const body of [{ min: 0 }, { min: 481 }, { min: '5' }]) await rejects(c.meeting(dev.addr, 't', body), 400, 'min');
    await rejects(c.meeting(dev.addr, 't', { off: false }), 400, 'off');
    for (const text of ['', '   ', 'x'.repeat(41), '一'.repeat(16), 'a\u0007b', 5]) await rejects(c.say(dev.addr, 't', { text }), 400, 'text');
    await rejects(c.say(dev.addr, 't', { text: 'ok', min: 481 }), 400, 'min');
    for (const min of [0, 181, 1.5]) await rejects(c.timer(dev.addr, 't', { min }), 400, 'min');
    await assert.rejects(c.focus(dev.addr, 'wrong', {}), (e) => e.status === 401);
    const res = await fetch(`http://${dev.addr}/api/focus`, { method: 'POST', headers: { authorization: 'Bearer t' }, body: '[1]' });
    assert.equal(res.status, 400);
  } finally { await dev.close(); }
});
