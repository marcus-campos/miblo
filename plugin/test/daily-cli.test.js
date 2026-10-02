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
    for (const [body, field] of [[{ in: 0, text: 'x' }, 'in'], [{ in: 1441, text: 'x' }, 'in'], [{ in: 5 }, 'text'],
      [{ at: '9:45', text: 'x' }, 'at'], [{ at: '24:00', text: 'x' }, 'at'], [{ at: '09:45', days: 0, text: 'x' }, 'days'],
      [{ at: '09:45', days: 128, text: 'x' }, 'days'], [{ delete: 9 }, 'delete'], [{ delete: 1 }, 'delete'], [{ dismiss: 1 }, 'dismiss']]) {
      await rejects(c.remind(dev.addr, 't', body), 400, field);
    }
    await rejects(c.remind(dev.addr, 't', { dismiss: true }), 409, 'none');
    for (const [body, field] of [[{ label: 'x', date: '2027-02-29' }, 'date'], [{ label: 'x', date: '2020-01-01' }, 'date'],
      [{ label: 'x', md: '02-30' }, 'md'], [{ label: 'x'.repeat(21), md: '10-15' }, 'label'], [{ md: '10-15' }, 'label'], [{ off: 1 }, 'off']]) {
      await rejects(c.countdown(dev.addr, 't', body), 400, field);
    }
    assert.deepEqual(await c.remind(dev.addr, 't', { at: '09:45', days: 62, text: 'daily' }), { id: 5, ok: true });
    assert.deepEqual((await c.reminders(dev.addr, 't')).items, [{ id: 5, at: '09:45', days: 62, text: 'daily' }]);
    await assert.rejects(c.focus(dev.addr, 'wrong', {}), (e) => e.status === 401);
    const res = await fetch(`http://${dev.addr}/api/focus`, { method: 'POST', headers: { authorization: 'Bearer t' }, body: '[1]' });
    assert.equal(res.status, 400);
  } finally { await dev.close(); }
});

test('remind in minutes, at a time, every weekday; list; off', async () => {
  const a = await fake({ name: 'Amon' });
  const d = deps();
  try {
    pair(d, a);
    assert.match((await run(['remind', '15', 'ligar', 'pro', 'cliente'], d)).out, /Reminder 1 on Amon in 15 min: "ligar pro cliente"\./);
    assert.deepEqual(a.state.lastRemind, { in: 15, text: 'ligar pro cliente' });
    assert.match((await run(['remind', '16:30', 'daily'], d)).out, /Reminder 2 on Amon at 16:30: "daily"\./);
    assert.deepEqual(a.state.lastRemind, { at: '16:30', text: 'daily' });
    assert.match((await run(['remind', 'dias', 'úteis', '09:45', 'daily'], d)).out, /Reminder 5 on Amon on weekdays at 09:45/);
    assert.deepEqual(a.state.lastRemind, { at: '09:45', days: 62, text: 'daily' });
    await run(['remind', 'every', 'day', '9:45', 'stand-up'], d);
    assert.equal(a.state.lastRemind.days, 127);
    assert.equal(a.state.lastRemind.at, '09:45');
    await run(['remind', 'Todo dia', '08:00', 'café'], d);  // one quoted argument, capitalized
    assert.deepEqual(a.state.lastRemind, { at: '08:00', days: 127, text: 'café' });
    await run(['remind', 'dias', 'uteis', '18:00', 'push'], d);
    assert.equal(a.state.lastRemind.days, 62);
    const list = (await run(['remind'], d)).out;
    assert.match(list, /^1 {2}in 15 min {2}ligar pro cliente$/m);
    assert.match(list, /weekdays 09:45\s+daily/);
    assert.match(list, /^6 {2}every day 09:45 {2}stand-up$/m);
    assert.match((await run(['remind', 'off', '5'], d)).out, /Deleted reminder 5 on Amon\./);
    const gone = await run(['remind', 'off', '5'], d);
    assert.equal(gone.code, 1);
    assert.match(gone.out, /Amon has no reminder 5\./);
    assert.match((await run(['remind', 'off'], d)).out, /Nothing to dismiss on Amon\./);
    a.state.held = true;
    assert.match((await run(['remind', 'off'], d)).out, /Reminder dismissed on Amon\./);
    for (const bad of [['25:00', 'x'], ['0', 'x'], ['1441', 'x'], ['15'], ['16:30'], ['every', 'day', 'x'], ['weekdays', '24:00', 'x'],
      ['off', '9'], ['off', '0'], ['soon', 'x'], ['15', 'x'.repeat(41)], ['15', '一'.repeat(16)], ['15', 'a\u0001b']]) {
      assert.equal((await run(['remind', ...bad], d)).code, 2, bad.join(' '));
    }
  } finally { await a.close(); }
});

test('a time with no clock on the gadget, and a full list', async () => {
  const a = await fake({ name: 'Amon', clockKnown: false });
  const d = deps();
  try {
    pair(d, a);
    const r = await run(['remind', '16:30', 'daily'], d);
    assert.equal(r.code, 1);
    assert.match(r.out, /Amon has not got the time yet \(it needs Wi-Fi with internet or a running bridge\); try again in a minute\./);
    for (let i = 0; i < 4; i++) assert.equal((await run(['remind', '60', `x${i}`], d)).code, 0);
    const full = await run(['remind', '60', 'x'], d);
    assert.equal(full.code, 1);
    assert.match(full.out, /Amon already has 4 reminders: delete one with \/miblo:remind off N\./);
    for (let i = 0; i < 4; i++) assert.equal((await run(['remind', 'weekdays', '10:00', `y${i}`], d)).code, 0);
    assert.match((await run(['remind', 'weekdays', '10:00', 'y'], d)).out, /already has 4 recurring reminders/);
  } finally { await a.close(); }
});

test('remind lists per gadget with several, and tells an old firmware to update', async () => {
  const a = await fake({ id: 'miblo-aaaa', name: 'Amon' });
  const b = await fake({ id: 'miblo-bbbb', name: 'Shiru', legacy: true });
  const d = deps();
  try {
    pair(d, a, b);
    const r = await run(['remind', '5', 'chá'], d);
    assert.equal(r.code, 0);
    assert.match(r.out, /Reminder 1 on Amon in 5 min: "chá"\.\nShiru does not support this yet: update it with \/miblo:update\./);
    a.state.reminders.push({ id: 2, dueAt: Date.now() + 60_000, text: 'evil\u001b[31m‮text' });
    const list = (await run(['remind'], d)).out;
    assert.match(list, /^Amon:\n {2}1 {2}in 5 min {2}chá\n {2}2 {2}in 1 min {2}evil \[31m text$/m);
    assert.match(list, /Shiru does not support this yet/);
  } finally { await a.close(); await b.close(); }
});

test('countdown with DD/MM and with a year; off; show', async () => {
  const NOW = new Date(2026, 9, 2, 10, 0).getTime();  // 02/10/2026, local
  const a = await fake({ name: 'Amon', now: () => NOW });
  const d = deps({ now: () => NOW });
  try {
    pair(d, a);
    assert.match((await run(['countdown', 'release', '15/10'], d)).out, /Countdown on Amon: "release" on 15\/10\./);
    assert.deepEqual(a.state.countdown, { label: 'release', md: '10-15' });
    await run(['countdown', 'release 2', '15/10/2027'], d);
    assert.deepEqual(a.state.countdown, { label: 'release 2', date: '2027-10-15' });
    assert.match((await run(['countdown'], d)).out, /Amon: "release 2" on 15\/10\/2027 \(in 378 days\)\./);
    await run(['countdown', 'demo', 'day', '3/10/2026'], d);
    assert.match((await run(['countdown'], d)).out, /"demo day" on 03\/10\/2026 \(tomorrow\)/);
    for (const bad of [['x', '31/02'], ['x', '29/02'], ['x', '1/10/2026'], ['x', '15/10/2030'], ['x', '15-10'], ['15/10'], ['x', '0/10'],
      ['x'.repeat(21), '15/10'], ['一'.repeat(14), '15/10']]) {
      assert.equal((await run(['countdown', ...bad], d)).code, 2, bad.join(' '));
    }
    assert.equal((await run(['countdown', 'leap', '29/02/2028'], d)).code, 0);
    assert.match((await run(['countdown', 'off'], d)).out, /Countdown off on Amon\./);
    assert.deepEqual(a.state.countdown, { off: true });
    assert.match((await run(['countdown'], d)).out, /Amon: no countdown\./);
  } finally { await a.close(); }
});

test('countdown: the gadget without a clock, and an old firmware', async () => {
  const a = await fake({ name: 'Amon', clockKnown: false });
  const d = deps();
  try {
    pair(d, a);
    assert.match((await run(['countdown', 'release', '15/10'], d)).out, /has not got the time yet/);
  } finally { await a.close(); }
  const b = await fake({ name: 'Old', legacy: true });
  const d2 = deps();
  try {
    pair(d2, b);
    for (const cmd of [['countdown'], ['countdown', 'off'], ['remind'], ['remind', 'off']]) {
      const r = await run(cmd, d2);
      assert.equal(r.code, 1, cmd.join(' '));
      assert.match(r.out, /Old does not support this yet: update it with \/miblo:update\./);
    }
  } finally { await b.close(); }
});

const NOW = new Date(2026, 9, 1, 14, 0).getTime();  // Thursday 01/10/2026 14:00, local
const S = NOW / 1000;
const status = {
  today: { usd: 4.2, turns: 47, work: 11520, top: [{ name: 'api-server', work: 6000 }, { name: 'front-app', work: 3120 }] },
  usage: { h5: { pct: 62, reset: S + 7800, eta: S + 4080 }, d7: { pct: 38, reset: new Date(2026, 9, 4, 8, 40).getTime() / 1000 } },  // Sunday, local
  sessions: [], devices: [],
};

test('today', async () => {
  const r = await run(['today'], deps({ fetchStatus: async () => status, now: () => NOW }));
  assert.equal(r.code, 0);
  assert.equal(r.out, [
    'Today: 47 responses, 3h12 with Claude working, US$ 4.20.',
    '5h 62% (resets 16:10, at this pace runs out at 15:08) · week 38% (resets Sun 08:40)',
    'Most work: api-server 1h40, front-app 52 min.',
    '',
  ].join('\n'));
});

test('today with one session, no cost and no limits; names from the bridge are sanitized', async () => {
  const one = { today: { turns: 1, work: 300, top: [{ name: 'api', work: 300 }] }, usage: null };
  const r = await run(['today'], deps({ fetchStatus: async () => one, now: () => NOW }));
  assert.equal(r.out, 'Today: 1 response, 5 min with Claude working.\nNo limits yet: link the status line with /miblo:link-statusline.\n');
  const evil = { today: { turns: 2, work: 600, top: [{ name: 'a\u001b]0;x\u0007b', work: 400 }, { name: 'c', work: 200 }] } };
  assert.match((await run(['today'], deps({ fetchStatus: async () => evil, now: () => NOW }))).out, /Most work: a \]0;x b 7 min, c 3 min\./);
});

test('limits without a forecast, and without the bridge', async () => {
  const noEta = { ...status, usage: { h5: { pct: 10, reset: S + 7800 } } };
  const r = await run(['limits'], deps({ fetchStatus: async () => noEta, now: () => NOW }));
  assert.equal(r.out, '5h 10% (resets 16:10)\n');
  assert.doesNotMatch(r.out, /runs out/);
  // A forecast past the reset, or already gone, is not shown.
  const late = { usage: { h5: { pct: 70, reset: S + 600, eta: S + 900 } } };
  assert.doesNotMatch((await run(['limits'], deps({ fetchStatus: async () => late, now: () => NOW }))).out, /runs out/);
  const gone = { usage: { h5: { pct: 70, reset: S + 600, eta: S - 60 } } };
  assert.doesNotMatch((await run(['limits'], deps({ fetchStatus: async () => gone, now: () => NOW }))).out, /runs out/);
  assert.match((await run(['limits'], deps({ fetchStatus: async () => ({ usage: null }), now: () => NOW }))).out, /No limits yet: link the status line/);
  const stopped = await run(['limits'], deps());
  assert.equal(stopped.code, 0);
  assert.match(stopped.out, /bridge isn't running/);
  assert.match((await run(['today'], deps())).out, /bridge isn't running/);
});
