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
