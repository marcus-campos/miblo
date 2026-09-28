import { test } from 'node:test';
import assert from 'node:assert/strict';
import { MetricsStore } from '../lib/metrics-store.js';

const T0 = new Date(2026, 8, 28, 14, 0, 0).getTime(); // 28/09/2026 14:00 local
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

test('today sums positive deltas across sessions', () => {
  const { store } = setup();
  store.ingest(sl('a', { inTok: 1000, outTok: 100, usd: 0.5 }));
  store.ingest(sl('a', { inTok: 1500, outTok: 150, usd: 0.75 }));
  store.ingest(sl('b', { inTok: 10, outTok: 5, usd: 0.01 }));
  store.ingest(sl('a', { inTok: 1400, outTok: 150, usd: 0.75 })); // regressão ignorada
  assert.deepEqual(store.today(), { tok: 1665, usd: 0.76 });
});

test('today resets at local midnight but keeps per-session baselines', () => {
  const { store, advance } = setup();
  store.ingest(sl('a', { inTok: 1000 }));
  advance(11 * 3600_000); // 01:00 do dia seguinte
  assert.deepEqual(store.today(), { tok: 0, usd: 0 });
  store.ingest(sl('a', { inTok: 1300 }));
  assert.deepEqual(store.today(), { tok: 300, usd: 0 });
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
