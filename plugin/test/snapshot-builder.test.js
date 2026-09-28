import { test } from 'node:test';
import assert from 'node:assert/strict';
import { SessionTracker } from '../lib/session-tracker.js';
import { MetricsStore } from '../lib/metrics-store.js';
import { buildSnapshot } from '../lib/snapshot-builder.js';

const NOW = 1_790_600_000_000;

function world() {
  const now = () => NOW;
  const tracker = new SessionTracker({ now, isAlive: () => true });
  const metrics = new MetricsStore({ now });
  const snap = (seq = 1) => buildSnapshot({ seq, nowMs: NOW, host: 'MacBook-Marcus', tracker, metrics });
  return { tracker, metrics, snap };
}

test('empty world', () => {
  const { snap } = world();
  assert.deepEqual(snap(7), {
    v: 1, seq: 7, now: 1_790_600_000, host: 'MacBook-Marcus',
    usage: null, today: { tok: 0, usd: 0 }, sessions: [], more: 0, alerts: [],
  });
});

test('session fields, short ids and metrics', () => {
  const { tracker, metrics, snap } = world();
  const sid = 'abcd1234-5678-90ab-cdef-000000000000';
  tracker.handle({ session_id: sid, hook_event_name: 'PermissionRequest', cwd: '/w/api-server', tool_name: 'Bash', tool_input: { command: 'npm run migrate' } });
  metrics.ingest({ session_id: sid, model: { display_name: 'Opus' }, context_window: { used_percentage: 71, total_input_tokens: 400000, total_output_tokens: 12000 } });
  const s = snap();
  assert.deepEqual(s.sessions, [{
    id: 'abcd1234', name: 'api-server', st: 'perm', tool: 'Bash', det: 'npm run migrate',
    since: 1_790_600_000, model: 'Opus', ctx: 71, tok: 412000,
  }]);
  assert.deepEqual(s.alerts, [{ id: 1, kind: 'perm', sid: 'abcd1234' }]);
});

test('session without statusline readings has null metrics', () => {
  const { tracker, snap } = world();
  tracker.handle({ session_id: 's1', hook_event_name: 'SessionStart', cwd: '/w/x' });
  const [s] = snap().sessions;
  assert.equal(s.model, '');
  assert.equal(s.ctx, null);
  assert.equal(s.tok, null);
});

test('truncates by characters, not bytes, with an ellipsis', () => {
  const { tracker, snap } = world();
  tracker.handle({ session_id: 's1', hook_event_name: 'PreToolUse', cwd: '/w/项目项目项目项目项目项目项目项目项目项目项目', tool_name: 'Bash', tool_input: { command: 'x'.repeat(50) } });
  const [s] = snap().sessions;
  assert.equal(Array.from(s.name).length, 20);
  assert.ok(s.name.endsWith('…'));
  assert.equal(s.det.length, 32);
});

test('caps at 8 sessions and reports the rest in more', () => {
  const { tracker, snap } = world();
  for (let i = 0; i < 11; i++) tracker.handle({ session_id: `s${i}`, hook_event_name: 'SessionStart', cwd: `/w/p${i}` });
  const s = snap();
  assert.equal(s.sessions.length, 8);
  assert.equal(s.more, 3);
});

test('never exceeds 3072 bytes, moving trailing sessions to more', () => {
  const { tracker, metrics, snap } = world();
  for (let i = 0; i < 8; i++) {
    const sid = `session-${i}`;
    tracker.handle({ session_id: sid, hook_event_name: 'PermissionRequest', cwd: `/w/${'项'.repeat(30)}${i}`, tool_name: `mcp__srv__${'t'.repeat(40)}`, tool_input: {} });
    tracker.handle({ session_id: sid, hook_event_name: 'PreToolUse', tool_name: 'Bash', tool_input: { command: '命'.repeat(40) } });
    metrics.ingest({ session_id: sid, model: { display_name: '模型模型模型模型模型模型模型' }, context_window: { used_percentage: 100, total_input_tokens: 999999999, total_output_tokens: 1 } });
  }
  const s = snap(123456789);
  assert.ok(Buffer.byteLength(JSON.stringify(s)) <= 3072);
  assert.equal(s.sessions.length + s.more, 8);
});
