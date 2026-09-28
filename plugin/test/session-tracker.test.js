import { test } from 'node:test';
import assert from 'node:assert/strict';
import { SessionTracker } from '../lib/session-tracker.js';

function setup() {
  let t = 1_000_000;
  const clock = { now: () => t, advance: (ms) => { t += ms; } };
  const alive = new Set();
  const tracker = new SessionTracker({ now: clock.now, isAlive: (pid) => alive.has(pid) });
  const ev = (session_id, hook_event_name, extra = {}) =>
    tracker.handle({ session_id, hook_event_name, cwd: '/work/api-server', ...extra });
  return { tracker, clock, alive, ev };
}

test('SessionStart creates an idle session named after cwd', () => {
  const { tracker, ev } = setup();
  assert.equal(ev('s1', 'SessionStart'), true);
  const [s] = tracker.sessions();
  assert.equal(s.name, 'api-server');
  assert.equal(s.st, 'idle');
});

test('duplicate cwd names are disambiguated', () => {
  const { tracker, ev } = setup();
  ev('s1', 'SessionStart');
  ev('s2', 'SessionStart');
  assert.deepEqual(tracker.sessions().map((s) => s.name).sort(), ['api-server', 'api-server 2']);
});

test('unknown session is created on any event', () => {
  const { tracker, ev } = setup();
  ev('s1', 'UserPromptSubmit');
  assert.equal(tracker.sessions()[0].st, 'running');
});

test('transition table', () => {
  const table = [
    [['UserPromptSubmit'], 'running', null],
    [['PreToolUse', { tool_name: 'Bash', tool_input: { command: 'npm test' } }], 'running', null],
    [['PreToolUse', { tool_name: 'AskUserQuestion', tool_input: {} }], 'question', 'question'],
    [['Notification', { notification_type: 'elicitation_dialog' }], 'question', 'question'],
    [['PermissionRequest', { tool_name: 'Bash', tool_input: { command: 'rm -rf build' } }], 'perm', 'perm'],
    [['Stop'], 'done', 'done'],
  ];
  for (const [[name, extra], st, alertKind] of table) {
    const { tracker, ev } = setup();
    ev('s1', 'SessionStart');
    ev('s1', name, extra);
    assert.equal(tracker.sessions()[0].st, st, name);
    const kinds = tracker.alerts().map((a) => a.kind);
    assert.deepEqual(kinds, alertKind ? [alertKind] : [], name);
  }
});

test('PermissionRequest records tool and detail', () => {
  const { tracker, ev } = setup();
  ev('s1', 'PermissionRequest', { tool_name: 'Bash', tool_input: { command: 'npm run migrate' } });
  const [s] = tracker.sessions();
  assert.equal(s.tool, 'Bash');
  assert.equal(s.det, 'npm run migrate');
});

test('any later event clears a pending state and its alert', () => {
  for (const next of ['PostToolUse', 'PreToolUse', 'UserPromptSubmit']) {
    const { tracker, ev } = setup();
    ev('s1', 'PermissionRequest', { tool_name: 'Bash', tool_input: {} });
    ev('s1', next, { tool_name: 'Read', tool_input: { file_path: '/a/b.js' } });
    assert.equal(tracker.sessions()[0].st, 'running', next);
    assert.deepEqual(tracker.alerts(), [], next);
  }
});

test('Stop after a denied permission goes to done', () => {
  const { tracker, ev } = setup();
  ev('s1', 'PermissionRequest', { tool_name: 'Bash', tool_input: {} });
  ev('s1', 'Stop');
  assert.equal(tracker.sessions()[0].st, 'done');
  assert.deepEqual(tracker.alerts().map((a) => a.kind), ['done']);
});

test('repeated pending event does not create a second alert', () => {
  const { tracker, ev } = setup();
  ev('s1', 'PermissionRequest', { tool_name: 'Bash', tool_input: {} });
  ev('s1', 'PermissionRequest', { tool_name: 'Bash', tool_input: {} });
  assert.equal(tracker.alerts().length, 1);
});

test('alert ids are unique and increasing', () => {
  const { tracker, ev } = setup();
  ev('s1', 'Stop');
  ev('s2', 'Stop');
  const ids = tracker.alerts().map((a) => a.id);
  assert.equal(ids.length, 2);
  assert.ok(ids[1] > ids[0]);
});

test('alerts expire after the TTL', () => {
  const { tracker, clock, ev } = setup();
  ev('s1', 'Stop');
  clock.advance(30_001);
  assert.deepEqual(tracker.alerts(), []);
});

test('since is the time the state was entered', () => {
  const { tracker, clock, ev } = setup();
  ev('s1', 'UserPromptSubmit');
  const t0 = tracker.sessions()[0].since;
  clock.advance(5000);
  ev('s1', 'PreToolUse', { tool_name: 'Bash', tool_input: {} });
  assert.equal(tracker.sessions()[0].since, t0);
  ev('s1', 'Stop');
  assert.equal(tracker.sessions()[0].since, t0 + 5000);
});

test('sessions are ordered by priority then since', () => {
  const { tracker, clock, ev } = setup();
  ev('a', 'SessionStart');
  clock.advance(1);
  ev('b', 'UserPromptSubmit');
  clock.advance(1);
  ev('c', 'Stop');
  clock.advance(1);
  ev('d', 'PreToolUse', { tool_name: 'AskUserQuestion', tool_input: {} });
  clock.advance(1);
  ev('e', 'PermissionRequest', { tool_name: 'Bash', tool_input: {} });
  clock.advance(1);
  ev('f', 'PermissionRequest', { tool_name: 'Bash', tool_input: {} });
  assert.deepEqual(tracker.sessions().map((s) => s.id), ['e', 'f', 'd', 'c', 'b', 'a']);
});

test('SessionEnd removes the session', () => {
  const { tracker, ev } = setup();
  ev('s1', 'SessionStart');
  assert.equal(ev('s1', 'SessionEnd'), true);
  assert.equal(tracker.hasActive(), false);
});

test('sweep removes sessions whose Claude process died', () => {
  const { tracker, alive, ev } = setup();
  alive.add(111);
  ev('s1', 'SessionStart', { pid: 111 });
  ev('s2', 'SessionStart', { pid: null });
  assert.equal(tracker.sweep(), false);
  alive.delete(111);
  assert.equal(tracker.sweep(), true);
  assert.deepEqual(tracker.sessions().map((s) => s.id), ['s2']);
});

test('events without session_id or with unknown names are ignored', () => {
  const { tracker, ev } = setup();
  assert.equal(tracker.handle({ hook_event_name: 'Stop' }), false);
  ev('s1', 'SessionStart');
  assert.equal(ev('s1', 'SubagentStop'), false);
  assert.equal(ev('s1', 'Notification', { notification_type: 'idle_prompt' }), false);
});

test('sawStart is true only for sessions whose SessionStart was handled', () => {
  const { tracker, ev } = setup();
  ev('s1', 'SessionStart');
  ev('s2', 'UserPromptSubmit');
  assert.equal(tracker.sawStart('s1'), true);
  assert.equal(tracker.sawStart('s2'), false);
  ev('s1', 'SessionEnd');
  assert.equal(tracker.sawStart('s1'), false);
});
