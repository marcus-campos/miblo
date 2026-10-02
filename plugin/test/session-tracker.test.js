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

// Waiting on the user (permission, question), then still working, then finished, then idle.
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
  assert.deepEqual(tracker.sessions().map((s) => s.id), ['e', 'f', 'd', 'b', 'c', 'a']);
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
  assert.equal(ev('s1', 'Notification', { notification_type: 'agent_needs_input' }), false);
  assert.equal(tracker.sessions()[0].st, 'idle');
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

test('pid attached to UserPromptSubmit is stored and used by sweep', () => {
  const { tracker, alive, ev } = setup();
  ev('s1', 'UserPromptSubmit', { pid: 222 });
  assert.equal(tracker.sessions()[0].pid, 222);
  alive.add(222);
  assert.equal(tracker.sweep(), false);
  alive.delete(222);
  assert.equal(tracker.sweep(), true);
  assert.equal(tracker.sessions().length, 0);
});

test('sweep drops sessions with no event for 12 h, even without a pid', () => {
  const { tracker, clock, ev } = setup();
  ev('old', 'SessionStart');
  clock.advance(6 * 3600_000);
  ev('recent', 'SessionStart');
  clock.advance(6 * 3600_000);
  assert.equal(tracker.sweep(), false); // exactly 12 h: kept
  clock.advance(1);
  assert.equal(tracker.sweep(), true);
  assert.deepEqual(tracker.sessions().map((s) => s.id), ['recent']);
});

// --- background work: subagents and background tasks ---

const agent = (id, type = 'general-purpose') => ({ agent_id: id, agent_type: type });
const bg = (...types) => ({ background_tasks: types.map((type) => ({ type, status: 'running' })) });

test('Stop while background work is in flight keeps the session running, no alert', () => {
  const table = [
    // [label, events before Stop, Stop payload, expected tool, expected det]
    ['2 agents via background_tasks', [], bg('subagent', 'subagent'), '_wait_agents', '2'],
    ['1 agent via background_tasks', [], bg('subagent'), '_wait_agents', '1'],
    ['shell + agent via background_tasks', [], bg('shell', 'subagent'), '_wait_tasks', '2'],
    ['2 agents tracked, no background_tasks (older Claude Code)',
      [['SubagentStart', agent('a1')], ['SubagentStart', agent('a2')]], {}, '_wait_agents', '2'],
  ];
  for (const [label, before, stop, tool, det] of table) {
    const { tracker, ev } = setup();
    ev('s1', 'UserPromptSubmit');
    for (const [name, extra] of before) ev('s1', name, extra);
    ev('s1', 'Stop', stop);
    const [s] = tracker.sessions();
    assert.equal(s.st, 'running', label);
    assert.equal(s.tool, tool, label);
    assert.equal(s.det, det, label);
    assert.deepEqual(tracker.alerts(), [], label);
  }
});

// A monitor (e.g. Claude Code watching a published artifact for comments) can stay "running" for
// hours without the session doing anything: it never counts as work in flight. Payload as sent by
// Claude Code at the end of a turn.
test('Stop with only a long-lived monitor in background_tasks is done', () => {
  const { tracker, ev } = setup();
  ev('s1', 'UserPromptSubmit');
  ev('s1', 'Stop', {
    background_tasks: [{ id: 'szvmna6nl', type: 'monitor', status: 'running', description: 'live updates for artifact' }],
  });
  assert.equal(tracker.sessions()[0].st, 'done');
  assert.deepEqual(tracker.alerts().map((a) => a.kind), ['done']);
});

test('a monitor next to real background work is not counted', () => {
  const { tracker, ev } = setup();
  ev('s1', 'UserPromptSubmit');
  ev('s1', 'Stop', { background_tasks: [{ type: 'monitor', status: 'running' }, { type: 'shell', status: 'running' }] });
  const [s] = tracker.sessions();
  assert.equal(s.st, 'running');
  assert.equal(s.tool, '_wait_tasks');
  assert.equal(s.det, '1');
});

test('a monitor next to an agent leaves an agents-only wait', () => {
  const { tracker, ev } = setup();
  ev('s1', 'UserPromptSubmit');
  ev('s1', 'Stop', { background_tasks: [{ type: 'monitor', status: 'running' }, { type: 'subagent', status: 'running' }] });
  const [s] = tracker.sessions();
  assert.equal(s.tool, '_wait_agents');
  assert.equal(s.det, '1');
});

test('Stop with an empty or all-finished background_tasks is done, even with tracked agents', () => {
  for (const stop of [{ background_tasks: [] }, { background_tasks: [{ type: 'subagent', status: 'completed' }] }]) {
    const { tracker, ev } = setup();
    ev('s1', 'SubagentStart', agent('a1'));
    ev('s1', 'Stop', stop);
    assert.equal(tracker.sessions()[0].st, 'done');
    assert.deepEqual(tracker.alerts().map((a) => a.kind), ['done']);
  }
});

test('subagent events never flip the main session state', () => {
  const table = [
    ['PreToolUse', { tool_name: 'Bash', tool_input: { command: 'npm test' } }],
    ['PreToolUse', { tool_name: 'AskUserQuestion', tool_input: {} }],
    ['PostToolUse', { tool_name: 'Read', tool_input: {} }],
    ['Notification', { notification_type: 'elicitation_dialog' }],
    ['Stop', {}],
    ['SubagentStop', {}],
  ];
  for (const [name, extra] of table) {
    // Main session waiting on the agent: stays running, detail untouched.
    let { tracker, ev } = setup();
    ev('s1', 'SubagentStart', agent('a1'));
    ev('s1', 'Stop', bg('subagent'));
    ev('s1', name, { ...agent('a1'), ...extra });
    let [s] = tracker.sessions();
    assert.deepEqual([s.st, s.tool, s.det], ['running', '_wait_agents', '1'], name);
    assert.deepEqual(tracker.alerts(), [], name);

    // Main session already done: a straggling subagent event neither revives nor re-alerts it.
    ({ tracker, ev } = setup());
    ev('s1', 'Stop', { background_tasks: [] });
    const alerts = tracker.alerts();
    ev('s1', name, { ...agent('a1'), ...extra });
    [s] = tracker.sessions();
    assert.equal(s.st, 'done', name);
    assert.deepEqual(tracker.alerts(), alerts, name);
  }
});

test('last SubagentStop keeps it running; only the next main Stop with nothing pending is done', () => {
  const { tracker, ev } = setup();
  ev('s1', 'UserPromptSubmit');
  ev('s1', 'SubagentStart', agent('a1'));
  ev('s1', 'SubagentStart', agent('a2'));
  ev('s1', 'Stop', bg('subagent', 'subagent'));
  ev('s1', 'SubagentStop', agent('a1'));
  ev('s1', 'SubagentStop', agent('a2'));
  assert.equal(tracker.sessions()[0].st, 'running');
  assert.deepEqual(tracker.alerts(), []);
  // Woken by the task notifications, the main agent works and stops again.
  ev('s1', 'PreToolUse', { tool_name: 'Read', tool_input: { file_path: '/a/b.js' } });
  assert.equal(tracker.sessions()[0].det, 'b.js');
  ev('s1', 'Stop', { background_tasks: [] });
  assert.equal(tracker.sessions()[0].st, 'done');
  assert.deepEqual(tracker.alerts().map((a) => a.kind), ['done']);
});

test('fallback counting (no background_tasks) follows SubagentStart/Stop', () => {
  const { tracker, ev } = setup();
  ev('s1', 'SubagentStart', agent('a1'));
  ev('s1', 'SubagentStart', agent('a2'));
  ev('s1', 'SubagentStop', agent('a1'));
  ev('s1', 'Stop');
  assert.deepEqual([tracker.sessions()[0].tool, tracker.sessions()[0].det], ['_wait_agents', '1']);
  ev('s1', 'SubagentStop', agent('a2'));
  ev('s1', 'Stop');
  assert.equal(tracker.sessions()[0].st, 'done');
});

test('internal agents (empty agent_type) are not counted as background work', () => {
  const { tracker, ev } = setup();
  ev('s1', 'SubagentStart', agent('sugg', ''));
  ev('s1', 'Stop');
  assert.equal(tracker.sessions()[0].st, 'done');
});

test('tracked agents silent for 30 min expire', () => {
  const { tracker, clock, ev } = setup();
  ev('s1', 'SubagentStart', agent('a1'));
  ev('s1', 'SubagentStart', agent('a2'));
  clock.advance(20 * 60_000);
  ev('s1', 'PreToolUse', { ...agent('a2'), tool_name: 'Bash', tool_input: {} }); // a2 heartbeat
  clock.advance(10 * 60_000 + 1);
  ev('s1', 'Stop');
  assert.deepEqual([tracker.sessions()[0].tool, tracker.sessions()[0].det], ['_wait_agents', '1']);
  clock.advance(30 * 60_000 + 1);
  ev('s1', 'Stop');
  assert.equal(tracker.sessions()[0].st, 'done');
});

test('a waiting session with no event for 30 min is marked done by sweep, without an alert', () => {
  const { tracker, clock, ev } = setup();
  ev('s1', 'Stop', bg('shell'));
  clock.advance(30 * 60_000);
  assert.equal(tracker.sweep(), false);
  assert.equal(tracker.sessions()[0].st, 'running');
  clock.advance(1);
  assert.equal(tracker.sweep(), true);
  assert.equal(tracker.sessions()[0].st, 'done');
  assert.deepEqual(tracker.alerts(), []);
});

test('SessionEnd clears tracked agents', () => {
  const { tracker, ev } = setup();
  ev('s1', 'SubagentStart', agent('a1'));
  ev('s1', 'SessionEnd');
  ev('s1', 'Stop');
  assert.equal(tracker.sessions()[0].st, 'done');
});

test('a subagent PermissionRequest raises perm; its next event returns to the waiting state', () => {
  const { tracker, ev } = setup();
  ev('s1', 'SubagentStart', agent('a1'));
  ev('s1', 'Stop', bg('subagent'));
  ev('s1', 'PermissionRequest', { ...agent('a1'), tool_name: 'Bash', tool_input: { command: 'rm -rf build' } });
  let [s] = tracker.sessions();
  assert.deepEqual([s.st, s.tool, s.det], ['perm', 'Bash', 'rm -rf build']);
  assert.deepEqual(tracker.alerts().map((a) => a.kind), ['perm']);
  // Another agent's activity does not clear it.
  ev('s1', 'PostToolUse', { ...agent('a2'), tool_name: 'Read', tool_input: {} });
  assert.equal(tracker.sessions()[0].st, 'perm');
  ev('s1', 'PostToolUse', { ...agent('a1'), tool_name: 'Bash', tool_input: {} });
  [s] = tracker.sessions();
  assert.deepEqual([s.st, s.tool, s.det], ['running', '_wait_agents', '1']);
  assert.deepEqual(tracker.alerts(), []);
});

test('a main-session permission is not cleared by subagent activity', () => {
  const { tracker, ev } = setup();
  ev('s1', 'PermissionRequest', { tool_name: 'Bash', tool_input: { command: 'deploy' } });
  ev('s1', 'PostToolUse', { ...agent('a1'), tool_name: 'Read', tool_input: {} });
  assert.equal(tracker.sessions()[0].st, 'perm');
  assert.deepEqual(tracker.alerts().map((a) => a.kind), ['perm']);
});

test('manual /compact shows compacting, then idle without a finished alert', () => {
  const { tracker, ev } = setup();
  ev('s1', 'SessionStart');
  ev('s1', 'Stop');
  assert.equal(ev('s1', 'PreCompact', { trigger: 'manual' }), true);
  let [s] = tracker.sessions();
  assert.equal(s.st, 'running');
  assert.equal(s.tool, '_compact');
  assert.equal(s.det, '');
  assert.deepEqual(tracker.alerts(), []);
  ev('s1', 'SessionStart', { source: 'compact' });
  [s] = tracker.sessions();
  assert.equal(s.st, 'idle');
  assert.equal(s.tool, '');
  assert.deepEqual(tracker.alerts(), []);
});

test('automatic compaction mid-turn keeps the session running', () => {
  const { tracker, ev } = setup();
  ev('s1', 'UserPromptSubmit');
  ev('s1', 'PreCompact', { trigger: 'auto' });
  assert.equal(tracker.sessions()[0].tool, '_compact');
  ev('s1', 'PostCompact', { trigger: 'auto' });
  const [s] = tracker.sessions();
  assert.equal(s.st, 'running');
  assert.equal(s.tool, '');
  // The SessionStart(compact) that follows PostCompact changes nothing.
  assert.equal(ev('s1', 'SessionStart', { source: 'compact' }), false);
});

test('a tool call after an automatic compaction replaces the compacting activity', () => {
  const { tracker, ev } = setup();
  ev('s1', 'UserPromptSubmit');
  ev('s1', 'PreCompact', { trigger: 'auto' });
  ev('s1', 'PreToolUse', { tool_name: 'Read', tool_input: { file_path: '/a/b.js' } });
  ev('s1', 'SessionStart', { source: 'compact' });
  const [s] = tracker.sessions();
  assert.equal(s.st, 'running');
  assert.equal(s.tool, 'Read');
});

test('toolSince marks when the current tool started', () => {
  let t = 1000;
  const tr = new SessionTracker({ now: () => t, isAlive: () => true });
  tr.handle({ session_id: 's', hook_event_name: 'PreToolUse', cwd: '/w/a', tool_name: 'Bash', tool_input: { command: 'npm test' } });
  t = 5000;
  tr.handle({ session_id: 's', hook_event_name: 'PostToolUse', cwd: '/w/a' });
  assert.equal(tr.sessions()[0].toolSince, 1000);
  tr.handle({ session_id: 's', hook_event_name: 'PreToolUse', cwd: '/w/a', tool_name: 'Bash', tool_input: { command: 'npm run build' } });
  assert.equal(tr.sessions()[0].toolSince, 5000);
});

test('toolSince: the same command run again starts over; its permission prompt does not', () => {
  let t = 1000;
  const tr = new SessionTracker({ now: () => t, isAlive: () => true });
  const ev = (name, extra = {}) => tr.handle({ session_id: 's', hook_event_name: name, cwd: '/w/a', ...extra });
  const bash = { tool_name: 'Bash', tool_input: { command: 'npm test' } };
  ev('PreToolUse', bash);
  t = 2000;
  ev('PermissionRequest', bash);  // the same call asking first
  assert.equal(tr.sessions()[0].toolSince, 1000);
  t = 3000;
  ev('PostToolUse');
  t = 4000;
  assert.equal(ev('PreToolUse', bash), true);  // a new run of the same command: a change to push
  assert.equal(tr.sessions()[0].toolSince, 4000);
  t = 5000;
  ev('PermissionRequest', { tool_name: 'Bash', tool_input: { command: 'rm -rf build' } });
  assert.equal(tr.sessions()[0].toolSince, 5000);
});

test('toolSince follows a subagent permission prompt that changes the activity', () => {
  let t = 1000;
  const tr = new SessionTracker({ now: () => t, isAlive: () => true });
  tr.handle({ session_id: 's', hook_event_name: 'UserPromptSubmit', cwd: '/w/a' });
  t = 2000;
  tr.handle({ session_id: 's', hook_event_name: 'PermissionRequest', agent_id: 'ag', tool_name: 'Bash', tool_input: { command: 'make' } });
  assert.equal(tr.sessions()[0].toolSince, 2000);
});

test('a failed shell command ends its timer (PostToolUseFailure), and the hook is registered', async () => {
  const { MetricsStore } = await import('../lib/metrics-store.js');
  const { buildSnapshot } = await import('../lib/snapshot-builder.js');
  const fs = await import('node:fs');
  let t = 1_790_600_000_000;
  const tr = new SessionTracker({ now: () => t, isAlive: () => true });
  const metrics = new MetricsStore({ now: () => t });
  const row = () => buildSnapshot({ seq: 1, nowMs: t, host: 'h', tracker: tr, metrics }).sessions[0];
  tr.handle({ session_id: 's', hook_event_name: 'PreToolUse', cwd: '/w/a', tool_name: 'Bash', tool_input: { command: 'npm test' } });
  t += 5000;
  assert.ok(row().ts);
  assert.equal(tr.handle({ session_id: 's', hook_event_name: 'PostToolUseFailure', cwd: '/w/a', tool_name: 'Bash' }), true);
  assert.equal(row().st, 'running');
  assert.ok(!('ts' in row()));
  const hooks = JSON.parse(fs.readFileSync(new URL('../hooks/hooks.json', import.meta.url), 'utf8')).hooks;
  const h = hooks.PostToolUseFailure?.flatMap((m) => m.hooks).find((c) => c.command.includes('bin/hook.js'));
  assert.ok(h, 'PostToolUseFailure runs hook.js');
  assert.equal(h.async, true);
});

test('cmdLive never outlives the shell command (Esc, a new prompt, a subagent asking for Bash)', () => {
  let t = 1000;
  const tr = new SessionTracker({ now: () => t, isAlive: () => true });
  const ev = (name, extra = {}) => tr.handle({ session_id: 's', hook_event_name: name, cwd: '/w/a', ...extra });
  ev('PreToolUse', { tool_name: 'Bash', tool_input: { command: 'npm test' } });
  assert.equal(tr.sessions()[0].cmdLive, true);
  ev('UserPromptSubmit');  // Esc and a new prompt: no PostToolUse ever came
  assert.equal(tr.sessions()[0].cmdLive, false);
  // A subagent's approved Bash: the gadget does not see its run, so no command timer.
  ev('PermissionRequest', { agent_id: 'ag', tool_name: 'Bash', tool_input: { command: 'make' } });
  ev('PostToolUse', { agent_id: 'ag', tool_name: 'Bash' });
  const [s] = tr.sessions();
  assert.equal(s.st, 'running');
  assert.equal(s.tool, 'Bash');
  assert.equal(s.cmdLive, false);
  // Main thread Bash, then a subagent event that leaves the activity alone: still live.
  ev('PreToolUse', { tool_name: 'Bash', tool_input: { command: 'npm run build' } });
  ev('PreToolUse', { agent_id: 'ag2', tool_name: 'Read', tool_input: {} });
  assert.equal(tr.sessions()[0].cmdLive, true);
});

// Claude Code's permission_prompt Notification: the prompt is on screen. Auto mode's classifier
// can ask the user without a PermissionRequest reaching the hooks, so it alone must raise perm.
const permNote = (extra = {}) => ({ notification_type: 'permission_prompt', ...extra });

test('a main-thread permission_prompt Notification raises perm with one alert, keeping the activity', () => {
  const { tracker, ev } = setup();
  ev('s1', 'UserPromptSubmit');
  ev('s1', 'PreToolUse', { tool_name: 'Bash', tool_input: { command: 'npm run deploy' } });
  assert.equal(ev('s1', 'Notification', permNote({ tool_name: 'Bash' })), true);
  let [s] = tracker.sessions();
  assert.deepEqual([s.st, s.tool, s.det], ['perm', 'Bash', 'npm run deploy']);
  assert.deepEqual(tracker.alerts().map((a) => a.kind), ['perm']);
  // Repeated: no second alert.
  ev('s1', 'Notification', permNote());
  assert.equal(tracker.alerts().length, 1);
  // The tool proceeds.
  ev('s1', 'PostToolUse', { tool_name: 'Bash', tool_input: {} });
  [s] = tracker.sessions();
  assert.equal(s.st, 'running');
  assert.deepEqual(tracker.alerts(), []);
});

test('a permission_prompt naming another tool shows that tool without the stale detail', () => {
  const { tracker, ev } = setup();
  ev('s1', 'PreToolUse', { tool_name: 'Read', tool_input: { file_path: '/w/a.js' } });
  ev('s1', 'Notification', permNote({ tool_name: 'WebFetch' }));
  const [s] = tracker.sessions();
  assert.deepEqual([s.st, s.tool, s.det], ['perm', 'WebFetch', '']);
});

test('a permission_prompt never shows a reserved wait activity as the tool', () => {
  const { tracker, ev } = setup();
  ev('s1', 'SubagentStart', agent('a1'));
  ev('s1', 'Stop', bg('subagent'));
  ev('s1', 'Notification', permNote());
  const [s] = tracker.sessions();
  assert.deepEqual([s.st, s.tool, s.det], ['perm', '', '']);
});

test('a subagent permission_prompt raises perm; that agent\'s next PreToolUse clears it', () => {
  const { tracker, ev } = setup();
  ev('s1', 'SubagentStart', agent('a1'));
  ev('s1', 'Stop', bg('subagent'));
  ev('s1', 'PreToolUse', { ...agent('a1'), tool_name: 'Bash', tool_input: { command: 'make' } });
  ev('s1', 'Notification', permNote({ ...agent('a1'), tool_name: 'Bash' }));
  let [s] = tracker.sessions();
  assert.equal(s.st, 'perm');
  assert.equal(s.tool, 'Bash');
  assert.deepEqual(tracker.alerts().map((a) => a.kind), ['perm']);
  ev('s1', 'PreToolUse', { ...agent('a2'), tool_name: 'Read', tool_input: {} });
  assert.equal(tracker.sessions()[0].st, 'perm');
  ev('s1', 'PreToolUse', { ...agent('a1'), tool_name: 'Bash', tool_input: { command: 'make' } });
  [s] = tracker.sessions();
  assert.deepEqual([s.st, s.tool, s.det], ['running', '_wait_agents', '1']);
  assert.deepEqual(tracker.alerts(), []);
});

test('a permission_prompt without agent_id while a subagent works keeps the main wait, cleared by any next event', () => {
  // The live bug: a subagent's classifier-escalated Bash, only the Notification arrives.
  const { tracker, ev } = setup();
  ev('s1', 'UserPromptSubmit');
  ev('s1', 'SubagentStart', agent('a1'));
  ev('s1', 'Stop', bg('subagent'));
  ev('s1', 'PreToolUse', { ...agent('a1'), tool_name: 'Bash', tool_input: { command: 'rm -rf dist' } });
  ev('s1', 'Notification', permNote({ tool_name: 'Bash' }));
  let [s] = tracker.sessions();
  assert.equal(s.st, 'perm');
  assert.equal(s.waiting, true);
  assert.deepEqual(tracker.alerts().map((a) => a.kind), ['perm']);
  ev('s1', 'PostToolUse', { ...agent('a1'), tool_name: 'Bash', tool_input: {} });
  [s] = tracker.sessions();
  assert.deepEqual([s.st, s.tool, s.det], ['running', '_wait_agents', '1']);
  assert.deepEqual(tracker.alerts(), []);
});

test('PermissionRequest then its permission_prompt Notification alert once and keep the owner', () => {
  for (const who of [{}, agent('a1')]) {
    const { tracker, ev } = setup();
    ev('s1', 'SubagentStart', agent('a1'));
    ev('s1', 'PermissionRequest', { ...who, tool_name: 'Bash', tool_input: { command: 'rm -rf build' } });
    ev('s1', 'Notification', permNote({ tool_name: 'Bash' }));
    let [s] = tracker.sessions();
    assert.deepEqual([s.st, s.tool, s.det], ['perm', 'Bash', 'rm -rf build']);
    assert.equal(tracker.alerts().length, 1);
    // Another subagent's activity does not clear a prompt whose owner is known.
    ev('s1', 'PreToolUse', { ...agent('a2'), tool_name: 'Read', tool_input: {} });
    assert.equal(tracker.sessions()[0].st, 'perm');
  }
});

test('a permission_prompt Notification then its PermissionRequest alert once', () => {
  const { tracker, ev } = setup();
  ev('s1', 'Notification', permNote({ tool_name: 'Bash' }));
  ev('s1', 'PermissionRequest', { tool_name: 'Bash', tool_input: { command: 'rm -rf build' } });
  const [s] = tracker.sessions();
  assert.deepEqual([s.st, s.tool, s.det], ['perm', 'Bash', 'rm -rf build']);
  assert.equal(tracker.alerts().length, 1);
});

test('Stop and UserPromptSubmit leave a permission_prompt perm', () => {
  for (const [name, st] of [['Stop', 'done'], ['UserPromptSubmit', 'running']]) {
    const { tracker, ev } = setup();
    ev('s1', 'Notification', permNote());
    ev('s1', name);
    assert.equal(tracker.sessions()[0].st, st, name);
  }
});
