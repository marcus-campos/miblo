import { test } from 'node:test';
import assert from 'node:assert/strict';
import { forecastEta, LimitForecast } from '../lib/limit-forecast.js';

const ramp = (from, n, stepSec, pct0, dPct) =>
  Array.from({ length: n }, (_, i) => ({ t: from + i * stepSec, pct: pct0 + i * dPct }));

test('rising: 1 point a minute from 60% runs out 40 min later', () => {
  const s = ramp(10_000, 21, 60, 40, 1);  // 40..60 over 20 min
  assert.equal(forecastEta(s, 10_000 + 1200, 10_000 + 7200), 10_000 + 1200 + 40 * 60);
});

test('flat, falling, too little data, reset first: no forecast', () => {
  assert.equal(forecastEta(ramp(0, 21, 60, 50, 0), 1200, 99_999), null);
  assert.equal(forecastEta(ramp(0, 21, 60, 70, -1), 1200, 99_999), null);
  assert.equal(forecastEta(ramp(0, 9, 60, 40, 1), 480, 99_999), null);        // 8 min
  assert.equal(forecastEta([{ t: 0, pct: 40 }, { t: 900, pct: 41 }], 900, 99_999), null);  // 2 values
  assert.equal(forecastEta(ramp(0, 21, 60, 40, 1), 1200, 1200 + 600), null);  // resets in 10 min
});

test('already at 100%, no samples, or no reset known', () => {
  assert.equal(forecastEta(ramp(0, 21, 60, 80, 1), 1200, 99_999), null);    // 80..100
  assert.equal(forecastEta([], 1200, 99_999), null);
  assert.equal(forecastEta(undefined, 1200, 99_999), null);
  assert.equal(forecastEta(ramp(0, 21, 60, 40, 1), 1200, null), 1200 + 40 * 60);
});

test('only the last hour counts', () => {
  const old = ramp(0, 30, 60, 10, 1);              // a fast spell 2 h ago
  const recent = ramp(7200, 21, 60, 50, 0.25);     // slow now
  const eta = forecastEta([...old, ...recent], 7200 + 1200, 99_999);
  // 50 -> 55 in 20 min leaves 45 points at 0.25/min: exactly 3 h from now. With the fast spell
  // in the fit it would be far sooner.
  assert.ok(eta >= 7200 + 1200 + 3 * 3600);
});

test('integer steps (as the status line reports them) still make a pace', () => {
  // 1 point every 3 min, sampled once a minute: 40,40,40,41,41,41,...
  const s = Array.from({ length: 31 }, (_, i) => ({ t: i * 60, pct: 40 + Math.floor(i / 3) }));
  const eta = forecastEta(s, 1800, 99_999);
  assert.ok(eta > 1800 + 2.3 * 3600 && eta < 1800 + 2.7 * 3600, String(eta));  // 50 points at 1 per 3 min
});

test('LimitForecast forgets everything at a reset', () => {
  let t = 1_000_000;
  const f = new LimitForecast({ now: () => t });
  for (let i = 0; i <= 20; i++, t += 60_000) f.observe({ pct: 40 + i, reset: 2_000_000 });
  assert.ok(f.eta());
  f.observe({ pct: 3, reset: 2_018_000 });
  assert.equal(f.eta(), null);
});

test('LimitForecast: a drop of more than 5 points is a new window even with the same reset', () => {
  let t = 1_000_000;
  const f = new LimitForecast({ now: () => t });
  for (let i = 0; i <= 20; i++, t += 60_000) f.observe({ pct: 40 + i, reset: 2_000_000 });
  f.observe({ pct: 50, reset: 2_000_000 });
  assert.equal(f.eta(), null);
});

test('LimitForecast: an unchanged reading is kept at most once a minute; nothing in, nothing out', () => {
  let t = 1_000_000;
  const f = new LimitForecast({ now: () => t });
  f.observe(undefined);
  f.observe({ pct: null, reset: 2_000_000 });
  assert.equal(f.eta(), null);
  // Readings every 10 s; the percentage climbs 1 point a minute.
  for (let i = 0; i <= 120; i++, t += 10_000) f.observe({ pct: 40 + Math.floor(i / 6), reset: 2_000_000 });
  const eta = f.eta();
  const nowSec = Math.floor(t / 1000);
  // 60% now (or close), 1 point a minute: about 40 min left.
  assert.ok(eta > nowSec + 35 * 60 && eta < nowSec + 45 * 60, String(eta - nowSec));
});

test('LimitForecast keeps only two hours, so an old pace fades out', () => {
  let t = 1_000_000;
  const f = new LimitForecast({ now: () => t });
  for (let i = 0; i <= 20; i++, t += 60_000) f.observe({ pct: 40 + i, reset: 99_999_999 });
  t += 3 * 3600_000;  // the bridge heard nothing for 3 h
  assert.equal(f.eta(), null);
});

test('no forecast more than 5 hours out (a 5-hour window cannot last longer), even with no reset known', () => {
  const slow = ramp(0, 61, 60, 10, 0.05);  // 3 points an hour from 10%: about 29 h left
  assert.equal(forecastEta(slow, 3600, null), null);
  assert.equal(forecastEta(slow, 3600, 0), null);
  // Just inside 5 h still counts.
  assert.equal(forecastEta(ramp(0, 21, 60, 40, 0.2), 1200, null), 1200 + Math.round(56 / (0.2 / 60)));
});
