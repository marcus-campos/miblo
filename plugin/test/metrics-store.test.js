import { test } from 'node:test';
import assert from 'node:assert/strict';
import { MetricsStore } from '../lib/metrics-store.js';

const T0 = new Date(2026, 8, 28, 14, 0, 0).getTime(); // 2026-09-28 14:00 local time
const sec = (ms) => Math.floor(ms / 1000);

function sl(sid, { inTok = 0, outTok = 0, usd = 0, ctx = 10, model = 'Opus', rl } = {}) {
  return {
    session_id: sid,
    model: { display_name: model },
    cost: { total_cost_usd: usd },
    context_window: { total_input_tokens: inTok, total_output_tokens: outTok, used_percentage: ctx },
    ...(rl ? { rate_limits: rl } : {}),
  };
}

function setup() {
  let t = T0;
  const store = new MetricsStore({ now: () => t });
  return { store, advance: (ms) => { t += ms; }, now: () => t };
}

test('ignores payloads without session_id', () => {
  const { store } = setup();
  assert.equal(store.ingest({}), false);
  assert.equal(store.hasReadings(), false);
});

test('per-session model, ctx and tokens', () => {
  const { store } = setup();
  store.ingest(sl('a', { inTok: 1000, outTok: 200, ctx: 41.6, model: 'Sonnet' }));
  assert.deepEqual(store.forSession('a'), { model: 'Sonnet', ctx: 42, tok: 1200 });
  assert.equal(store.forSession('zzz'), undefined);
});

test('null used_percentage becomes null ctx', () => {
  const { store } = setup();
  store.ingest({ session_id: 'a', context_window: { used_percentage: null } });
  assert.equal(store.forSession('a').ctx, null);
});

test('today sums positive cost deltas across sessions (fresh sessions count from zero)', () => {
  const { store } = setup();
  store.ingest(sl('a', { inTok: 1000, outTok: 100, usd: 0.5 }), { fresh: true });
  store.ingest(sl('a', { inTok: 1500, outTok: 150, usd: 0.75 }));
  store.ingest(sl('b', { inTok: 10, outTok: 5, usd: 0.01 }), { fresh: true });
  store.ingest(sl('a', { inTok: 1400, outTok: 150, usd: 0.7 })); // regression ignored
  assert.deepEqual(store.today(), { usd: 0.76 });
});

test('first reading of a non-fresh session only sets the cost baseline', () => {
  const { store } = setup();
  store.ingest(sl('a', { usd: 12.5 }));
  assert.deepEqual(store.today(), { usd: 0 });
  store.ingest(sl('a', { usd: 13 }));
  assert.deepEqual(store.today(), { usd: 0.5 });
});

test('tok is the current context size, not accumulated', () => {
  const { store } = setup();
  store.ingest(sl('a', { inTok: 5000, outTok: 500 }), { fresh: true });
  store.ingest(sl('a', { inTok: 3000, outTok: 100 })); // context compacted
  assert.equal(store.forSession('a').tok, 3100);
  assert.deepEqual(Object.keys(store.today()), ['usd']);
});

test('today resets at local midnight but keeps per-session baselines', () => {
  const { store, advance } = setup();
  store.ingest(sl('a', { usd: 1 }), { fresh: true });
  assert.deepEqual(store.today(), { usd: 1 });
  advance(11 * 3600_000); // 01:00 next day
  assert.deepEqual(store.today(), { usd: 0 });
  store.ingest(sl('a', { usd: 1.3 }));
  assert.deepEqual(store.today(), { usd: 0.3 });
});

test('usage takes the latest rate_limits and keeps them across readings without it', () => {
  const { store, now } = setup();
  const reset5 = sec(now()) + 7200;
  const reset7 = sec(now()) + 86400;
  store.ingest(sl('a', { rl: { five_hour: { used_percentage: 61.7, resets_at: reset5 }, seven_day: { used_percentage: 38.2, resets_at: reset7 } } }));
  store.ingest(sl('b'));
  assert.deepEqual(store.usage(), { h5: { pct: 62, reset: reset5 }, d7: { pct: 38, reset: reset7 } });
});

test('each window may be absent independently', () => {
  const { store, now } = setup();
  store.ingest(sl('a', { rl: { seven_day: { used_percentage: 10, resets_at: sec(now()) + 100 } } }));
  assert.deepEqual(Object.keys(store.usage()), ['d7']);
});

test('expired windows are dropped; usage is null when nothing remains', () => {
  const { store, now, advance } = setup();
  store.ingest(sl('a', { rl: { five_hour: { used_percentage: 90, resets_at: sec(now()) + 60 } } }));
  advance(61_000);
  assert.equal(store.usage(), null);
});

test('forget removes a session', () => {
  const { store } = setup();
  store.ingest(sl('a'));
  store.forget('a');
  assert.equal(store.forSession('a'), undefined);
});

test('limits and today\'s cost survive a bridge restart (same day), and expire as usual', async () => {
  const fs = await import('node:fs');
  const os = await import('node:os');
  const path = await import('node:path');
  const dataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-metrics-'));
  let t = new Date(2026, 8, 30, 10, 0).getTime();
  const a = new MetricsStore({ now: () => t, dataDir });
  a.ingest({ session_id: 's1', cost: { total_cost_usd: 1.5 }, rate_limits: { five_hour: { used_percentage: 42, resets_at: t / 1000 + 3600 } } }, { fresh: true });
  const b = new MetricsStore({ now: () => t, dataDir });  // the bridge restarted
  assert.deepEqual(b.usage(), { h5: { pct: 42, reset: t / 1000 + 3600 } });
  assert.equal(b.today().usd, 1.5);
  // The same session's next reading only adds what is new (its baseline was not persisted).
  b.ingest({ session_id: 's1', cost: { total_cost_usd: 2 } });
  assert.equal(b.today().usd, 1.5);
  t += 2 * 3600 * 1000;  // past the reset
  assert.equal(new MetricsStore({ now: () => t, dataDir }).usage(), null);
  t = new Date(2026, 9, 1, 9, 0).getTime();  // the next day
  assert.equal(new MetricsStore({ now: () => t, dataDir }).today().usd, 0);
  fs.writeFileSync(path.join(dataDir, 'metrics.json'), '{bad');
  assert.equal(new MetricsStore({ now: () => t, dataDir }).usage(), null);  // corrupt file: start empty
});
