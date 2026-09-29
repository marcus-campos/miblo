import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { SessionTracker } from '../lib/session-tracker.js';
import { MetricsStore } from '../lib/metrics-store.js';
import { buildSnapshot } from '../lib/snapshot-builder.js';

const dir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../fixtures/snapshots');
const NOW = Date.UTC(2026, 8, 28, 14, 32, 0);
const S = Math.floor(NOW / 1000);

// `day` stands in for DayStats: today's finished responses and seconds worked.
function world(day = { turns: 12, work: 4380 }) {
  let t = NOW - 10_000; // < ALERT_TTL_MS, so alerts are still in the snapshot
  const clock = { now: () => t, set: (ms) => { t = ms; } };
  const tracker = new SessionTracker({ now: clock.now, isAlive: () => true });
  const metrics = new MetricsStore({ now: clock.now });
  const ev = (sid, name, cwd, extra = {}) => tracker.handle({ session_id: sid, hook_event_name: name, cwd, ...extra });
  const sl = (sid, model, ctx, inTok, outTok, usd, rl) =>
    metrics.ingest({ session_id: sid, model: { display_name: model }, cost: { total_cost_usd: usd },
      context_window: { used_percentage: ctx, total_input_tokens: inTok, total_output_tokens: outTok }, ...(rl ? { rate_limits: rl } : {}) }, { fresh: true });
  const rl = { five_hour: { used_percentage: 62, resets_at: S + 7800 }, seven_day: { used_percentage: 38, resets_at: S + 240000 } };
  const finish = () => { clock.set(NOW); return buildSnapshot({ seq: 42, nowMs: NOW, host: 'MacBook-Marcus', tracker, metrics, day: { today: () => day } }); };
  return { ev, sl, rl, finish };
}

const scenarios = {
  attention() {
    const w = world();
    w.ev('11111111-a', 'PermissionRequest', '/w/api-server', { tool_name: 'Bash', tool_input: { command: 'npm run migrate' } });
    w.ev('22222222-b', 'PreToolUse', '/w/infra', { tool_name: 'AskUserQuestion', tool_input: {} });
    w.ev('33333333-c', 'PreToolUse', '/w/front-app', { tool_name: 'Edit', tool_input: { file_path: '/w/front-app/src/Header.tsx' } });
    w.ev('44444444-d', 'SessionStart', '/w/docs');
    w.sl('11111111-a', 'Opus', 71, 400000, 12000, 3.1, w.rl);
    w.sl('33333333-c', 'Sonnet', 34, 90000, 8000, 0.4);
    return w.finish();
  },
  working() {
    const w = world({ turns: 31, work: 12420 });
    w.ev('33333333-c', 'PreToolUse', '/w/front-app', { tool_name: 'Edit', tool_input: { file_path: '/w/front-app/src/Header.tsx' } });
    w.ev('55555555-e', 'PreToolUse', '/w/worker', { tool_name: 'Bash', tool_input: { command: 'npm test' } });
    w.sl('33333333-c', 'Sonnet', 34, 90000, 8000, 0.4, w.rl);
    return w.finish();
  },
  idle() {
    const w = world({ turns: 0, work: 0 });
    w.ev('66666666-f', 'Stop', '/w/docs');
    w.sl('66666666-f', 'Opus', 54, 170000, 12000, 1.2, w.rl);
    return w.finish();
  },
  overflow() {
    const w = world({ turns: 999, work: 86399 });
    for (let i = 0; i < 10; i++) {
      w.ev(`7777777${i}-x`, 'PreToolUse', `/w/project-${i}`, { tool_name: 'Read', tool_input: { file_path: `/w/f${i}.js` } });
    }
    return w.finish();
  },
};

for (const [name, build] of Object.entries(scenarios)) {
  test(`contract fixture: ${name}`, () => {
    const file = path.join(dir, `${name}.json`);
    const actual = build();
    if (process.env.UPDATE_FIXTURES === '1') {
      fs.mkdirSync(dir, { recursive: true });
      fs.writeFileSync(file, JSON.stringify(actual, null, 2) + '\n');
    }
    assert.deepEqual(actual, JSON.parse(fs.readFileSync(file, 'utf8')));
    assert.ok(Buffer.byteLength(JSON.stringify(actual)) <= 3072);
  });
}
