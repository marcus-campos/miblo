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
    usage: null, today: { usd: 0 }, sessions: [], more: 0, alerts: [],
  });
});

test('today merges the day stats (responses and seconds worked) with the cost', () => {
  const { tracker, metrics } = world();
  const day = { today: () => ({ turns: 7, work: 3600 }) };
  const s = buildSnapshot({ seq: 1, nowMs: NOW, host: 'h', tracker, metrics, day });
  assert.deepEqual(s.today, { usd: 0, turns: 7, work: 3600 });
});

test('latest is sent when known and omitted otherwise', () => {
  const { tracker, metrics, snap } = world();
  assert.equal(buildSnapshot({ seq: 1, nowMs: NOW, host: 'h', tracker, metrics, latest: '1.0.2' }).latest, '1.0.2');
  assert.ok(!('latest' in snap()));
  assert.ok(!('latest' in buildSnapshot({ seq: 1, nowMs: NOW, host: 'h', tracker, metrics, latest: null })));
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

test('caps at 20 sessions and reports the rest in more', () => {
  const { tracker, snap } = world();
  for (let i = 0; i < 23; i++) tracker.handle({ session_id: `s${i}`, hook_event_name: 'SessionStart', cwd: `/w/p${i}` });
  const s = snap();
  assert.equal(s.sessions.length, 20);
  assert.equal(s.more, 3);
});

test('never exceeds 6144 bytes, moving trailing sessions to more', () => {
  const { tracker, metrics, snap } = world();
  for (let i = 0; i < 20; i++) {
    const sid = `session-${i}`;
    tracker.handle({ session_id: sid, hook_event_name: 'PermissionRequest', cwd: `/w/${'项'.repeat(30)}${i}`, tool_name: `mcp__srv__${'t'.repeat(40)}`, tool_input: {} });
    tracker.handle({ session_id: sid, hook_event_name: 'PreToolUse', tool_name: 'Bash', tool_input: { command: '命'.repeat(40) } });
    metrics.ingest({ session_id: sid, model: { display_name: '模型模型模型模型模型模型模型' }, context_window: { used_percentage: 100, total_input_tokens: 999999999, total_output_tokens: 1 } });
  }
  const s = snap(123456789);
  assert.ok(Buffer.byteLength(JSON.stringify(s)) <= 6144);
  assert.equal(s.sessions.length + s.more, 20);
});

test('trimSnapshot fits an older gadget that reports smaller caps', async () => {
  const { trimSnapshot } = await import('../lib/snapshot-builder.js');
  const { tracker, metrics, snap } = world();
  for (let i = 0; i < 20; i++) {
    const sid = `s-${i}`;
    tracker.handle({ session_id: sid, hook_event_name: 'PermissionRequest', cwd: `/w/${'项'.repeat(30)}${i}`, tool_name: `mcp__srv__${'t'.repeat(40)}`, tool_input: {} });
    metrics.ingest({ session_id: sid, model: { display_name: '模型模型模型模型模型' }, context_window: { used_percentage: 100, total_input_tokens: 999999999, total_output_tokens: 1 } });
  }
  const full = snap(123456789);
  const legacy = trimSnapshot(full, 8, 3072);
  assert.ok(Buffer.byteLength(JSON.stringify(legacy)) <= 3072);
  assert.ok(legacy.sessions.length <= 8);
  assert.equal(legacy.sessions.length + legacy.more, full.sessions.length + full.more);
  // A gadget with room for everything gets the snapshot unchanged.
  assert.equal(trimSnapshot(full, 20, 6144), full);
});

test('alerting sessions are kept over quiet working ones when trimming', () => {
  const { tracker, snap } = world();
  // 22 working sessions (no alert) + 2 that need the user: the alerting ones must survive the cap.
  for (let i = 0; i < 22; i++) {
    tracker.handle({ session_id: `work-${i}`, hook_event_name: 'SessionStart', cwd: `/w/w${i}` });
    tracker.handle({ session_id: `work-${i}`, hook_event_name: 'PreToolUse', tool_name: 'Bash', tool_input: { command: 'x' } });
  }
  tracker.handle({ session_id: 'ask-1', hook_event_name: 'PermissionRequest', cwd: '/w/a1', tool_name: 'Bash', tool_input: {} });
  tracker.handle({ session_id: 'ask-2', hook_event_name: 'Notification', notification_type: 'elicitation_dialog', cwd: '/w/a2' });
  const s = snap();
  const ids = s.sessions.map((x) => x.id);
  assert.ok(ids.includes('ask1'), 'permission session shown');
  assert.ok(ids.includes('ask2'), 'question session shown');
  // Every alert points at a session that is actually present, so the gadget can name it.
  for (const a of s.alerts) assert.ok(ids.includes(a.sid), `alert ${a.sid} has its session`);
});
