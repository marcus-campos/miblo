import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { DayStats } from '../lib/day-stats.js';

// Local times, so the midnight rollover is tested in the machine's own time zone.
const at = (h, m = 0, s = 0, d = 29) => new Date(2026, 8, d, h, m, s).getTime();
const tmp = () => fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-day-'));

function stats({ dataDir, start = at(10) } = {}) {
  let t = start;
  const day = new DayStats({ dataDir, now: () => t });
  return {
    day,
    set: (ms) => { t = ms; },
    tick: (sec) => { t += sec * 1000; },
    see: (...sts) => day.observe(sts.map((st, i) => ({ id: `s${i}`, st }))),
  };
}

test('starts at zero', () => {
  assert.deepEqual(stats().day.today(), { turns: 0, work: 0 });
});

test('a running -> done transition counts one turn', () => {
  const w = stats();
  w.see('running');
  w.see('done');
  w.see('done');  // staying done (heartbeats) does not count again
  assert.equal(w.day.today().turns, 1);
  w.see('running');
  w.see('done');
  assert.equal(w.day.today().turns, 2);
});

test('done without having been running does not count', () => {
  const w = stats();
  w.see('done');       // first sighting already done (bridge restarted)
  w.see('idle', 'done');
  w.see('done', 'idle');
  w.see('perm');
  w.see('done');
  assert.equal(w.day.today().turns, 0);
});

test('turns are counted per session', () => {
  const w = stats();
  w.see('running', 'running');
  w.see('done', 'running');
  w.see('done', 'done');
  assert.equal(w.day.today().turns, 2);
});

test('a session that disappears and returns done is not counted', () => {
  const w = stats();
  w.see('running');
  w.see();
  w.see('done');
  assert.equal(w.day.today().turns, 0);
});

test('work accumulates only while some session is running', () => {
  const w = stats();
  w.see('running');
  w.tick(10);
  w.see('running', 'idle');
  w.tick(10);
  w.see('done');  // it ran until this push
  assert.equal(w.day.today().work, 20);
  w.tick(30);
  w.see('done');
  w.tick(10);
  w.see('perm');  // waiting on the user is not work
  w.tick(10);
  w.see('idle');
  assert.equal(w.day.today().work, 20);
});

test('work ignores gaps longer than 60 s', () => {
  const w = stats();
  w.see('running');
  w.tick(60);
  w.see('running');
  assert.equal(w.day.today().work, 60);
  w.tick(61);  // asleep laptop: not counted
  w.see('running');
  assert.equal(w.day.today().work, 60);
  w.tick(5);
  w.see('running');
  assert.equal(w.day.today().work, 65);
});

test('rolls over at local midnight', () => {
  const w = stats({ start: at(23, 59, 30) });
  w.see('running');
  w.tick(10);
  w.see('done');
  assert.deepEqual(w.day.today(), { turns: 1, work: 10 });
  w.set(at(0, 0, 5, 30));
  assert.deepEqual(w.day.today(), { turns: 0, work: 0 });
});

test('work straddling midnight counts from 00:00 on the new day', () => {
  const w = stats({ start: at(23, 59, 50) });
  w.see('running');
  w.set(at(0, 0, 20, 30));
  w.see('running');
  assert.deepEqual(w.day.today(), { turns: 0, work: 20 });
});

test('persists to the data dir: the same day resumes, another day starts at zero', () => {
  const dataDir = tmp();
  const w = stats({ dataDir });
  w.see('running');
  w.tick(30);
  w.see('done');
  assert.ok(fs.existsSync(path.join(dataDir, 'day-stats.json')));

  const again = stats({ dataDir, start: at(18) });
  assert.deepEqual(again.day.today(), { turns: 1, work: 30 });
  again.see('running');
  again.tick(5);
  again.see('done');
  assert.deepEqual(stats({ dataDir, start: at(19) }).day.today(), { turns: 2, work: 35 });

  assert.deepEqual(stats({ dataDir, start: at(9, 0, 0, 30) }).day.today(), { turns: 0, work: 0 });
});

test('tolerates a corrupt or missing file', () => {
  const dataDir = tmp();
  fs.writeFileSync(path.join(dataDir, 'day-stats.json'), '{not json');
  const w = stats({ dataDir });
  assert.deepEqual(w.day.today(), { turns: 0, work: 0 });
  w.see('running');
  w.see('done');
  assert.equal(stats({ dataDir }).day.today().turns, 1);

  fs.writeFileSync(path.join(dataDir, 'day-stats.json'), JSON.stringify({ day: '2026-9-29', turns: -3, workMs: 'x' }));
  assert.deepEqual(stats({ dataDir }).day.today(), { turns: 0, work: 0 });
});

test('works without a data dir', () => {
  const w = stats();
  w.see('running');
  w.see('done');
  assert.equal(w.day.today().turns, 1);
});

test('last week totals, busiest day and only 14 days kept', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-day-'));
  let t = new Date(2026, 8, 21, 10, 0).getTime();  // Monday 21/09/2026
  const d = new DayStats({ dataDir: dir, now: () => t });
  const run = (mins) => { for (let i = 0; i <= mins; i++) { d.observe([{ id: 'a', name: 'app', st: 'running' }], { usd: 1 }); t += 60_000; } };
  for (let day = 0; day < 7; day++) { run(day === 2 ? 120 : 30); t = new Date(2026, 8, 22 + day, 10, 0).getTime(); }
  // now Monday 28/09
  const w = d.week();
  assert.equal(w.top, 3);                  // Wednesday 23/09 had the most work
  assert.ok(w.work >= (6 * 30 + 120) * 60);
  assert.equal(w.usd, 7);
  for (let i = 0; i < 20; i++) { t += 86_400_000; d.observe([], { usd: 0 }); }
  const kept = JSON.parse(fs.readFileSync(path.join(dir, 'day-stats.json'), 'utf8')).days.length;
  assert.ok(kept <= 14);
});

test('week: exactly the Monday-to-Sunday before this week, from any day, and survives a restart', () => {
  const dir = tmp();
  let t = new Date(2026, 8, 20, 10, 0).getTime();  // Sunday 20/09: the week before last
  let d = new DayStats({ dataDir: dir, now: () => t });
  const see = (st, name = 'app') => d.observe([{ id: name, name, st }], { usd: 2.5 });
  see('running'); t += 60_000; see('done');
  t = new Date(2026, 8, 27, 23, 0).getTime();     // Sunday 27/09: last week's last day
  see('running'); t += 30_000; see('done');
  t = new Date(2026, 8, 28, 9, 0).getTime();      // Monday 28/09
  d = new DayStats({ dataDir: dir, now: () => t }); // a new bridge
  assert.deepEqual(d.week(), { work: 30, turns: 1, usd: 2.5, top: 0 });
  t = new Date(2026, 9, 4, 22, 0).getTime();      // Sunday 04/10: still the same "last week"
  assert.deepEqual(d.week(), { work: 30, turns: 1, usd: 2.5, top: 0 });
  t = new Date(2026, 9, 5, 9, 0).getTime();       // Monday 05/10: nothing recorded last week
  assert.equal(d.week(), null);
});

test('week: cost alone counts, with no busiest day', () => {
  let t = new Date(2026, 8, 22, 10, 0).getTime();
  const d = new DayStats({ now: () => t });
  d.observe([], { usd: 0.4 });
  t = new Date(2026, 8, 28, 10, 0).getTime();
  assert.deepEqual(d.week(), { work: 0, turns: 0, usd: 0.4, top: null });
});

test('today top sessions by work', () => {
  let t = new Date(2026, 8, 29, 10, 0).getTime();
  const d = new DayStats({ now: () => t });
  for (let i = 0; i <= 20; i++) {
    d.observe([{ id: '1', name: 'api', st: 'running' }, { id: '2', name: 'web', st: i < 5 ? 'running' : 'idle' }]);  // counted until seen idle
    t += 60_000;
  }
  assert.deepEqual(d.topSessions(), [{ name: 'api', work: 1200 }, { name: 'web', work: 300 }]);
  assert.deepEqual(d.topSessions(1), [{ name: 'api', work: 1200 }]);
  assert.equal(d.today().work, 1200);
  t = new Date(2026, 8, 30, 10, 0).getTime();
  assert.deepEqual(d.topSessions(), []);  // a new day
});

test('today top sessions survive a restart', () => {
  const dir = tmp();
  let t = at(10);
  const d = new DayStats({ dataDir: dir, now: () => t });
  d.observe([{ id: '1', name: 'api', st: 'running' }]);
  t += 40_000;
  d.observe([{ id: '1', name: 'api', st: 'done' }]);
  assert.deepEqual(new DayStats({ dataDir: dir, now: () => t }).topSessions(), [{ name: 'api', work: 40 }]);
});

test('an old day-stats.json (single day) still loads', () => {
  const dir = tmp();
  fs.writeFileSync(path.join(dir, 'day-stats.json'), JSON.stringify({ day: '2026-9-29', turns: 4, workMs: 90_000 }));
  const d = new DayStats({ dataDir: dir, now: () => at(12) });
  assert.deepEqual(d.today(), { turns: 4, work: 90 });
  assert.deepEqual(d.topSessions(), []);
  // An old file from an earlier day becomes history: last week's Tuesday for Monday 05/10.
  const later = new DayStats({ dataDir: dir, now: () => new Date(2026, 9, 5, 9).getTime() });
  assert.deepEqual(later.today(), { turns: 0, work: 0 });
  assert.deepEqual(later.week(), { work: 90, turns: 4, usd: 0, top: 2 });
});
