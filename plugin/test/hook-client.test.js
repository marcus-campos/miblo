import { test } from 'node:test';
import assert from 'node:assert/strict';
import { pickEvent, wantsPid, deliver, isConnError } from '../lib/hook-client.js';

test('pickEvent forwards only the whitelisted fields, tool_input cut to 200 chars', () => {
  const raw = {
    session_id: 's1', hook_event_name: 'PreToolUse', cwd: '/w/x', tool_name: 'Write', notification_type: 'n',
    transcript_path: '/secret/transcript.jsonl', permission_mode: 'default', prompt: 'my secret prompt',
    tool_input: { file_path: '/w/x/a.js', content: 'SECRET FILE BODY', command: 'y'.repeat(500), old_string: 'x', limit: 5 },
    tool_response: { ok: true },
  };
  assert.deepEqual(pickEvent(raw), {
    session_id: 's1', hook_event_name: 'PreToolUse', cwd: '/w/x', tool_name: 'Write', notification_type: 'n',
    tool_input: { file_path: '/w/x/a.js', command: 'y'.repeat(200) },
  });
  assert.deepEqual(pickEvent(null), {});
  assert.deepEqual(pickEvent({ session_id: 5 }), {});
});

test('pickEvent forwards agent_id/agent_type and only type/status of background_tasks', () => {
  const raw = {
    session_id: 's1', hook_event_name: 'Stop', agent_id: 'a1', agent_type: 'Explore',
    last_assistant_message: 'secret',
    background_tasks: [{ id: 't1', type: 'shell', status: 'running', command: 'secret cmd', description: 'x' }, null],
    session_crons: [{ prompt: 'secret' }],
  };
  assert.deepEqual(pickEvent(raw), {
    session_id: 's1', hook_event_name: 'Stop', agent_id: 'a1', agent_type: 'Explore',
    background_tasks: [{ type: 'shell', status: 'running' }, {}],
  });
});

test('pid is attached on SessionStart and UserPromptSubmit only', () => {
  assert.equal(wantsPid({ hook_event_name: 'SessionStart' }), true);
  assert.equal(wantsPid({ hook_event_name: 'UserPromptSubmit' }), true);
  assert.equal(wantsPid({ hook_event_name: 'PreToolUse' }), false);
});

test('isConnError distinguishes connection errors from HTTP errors and timeouts', () => {
  assert.equal(isConnError(new TypeError('fetch failed')), true);
  assert.equal(isConnError(Object.assign(new Error('x'), { code: 'ECONNREFUSED' })), true);
  assert.equal(isConnError(Object.assign(new Error('bridge 500'), { status: 500 })), false);
  assert.equal(isConnError(new DOMException('timeout', 'TimeoutError')), false);
});

function fakeIo({ postResults = [], healthResults = [] } = {}) {
  const calls = [];
  let slept = 0;
  const io = {
    calls,
    slept: () => slept,
    async post(body) {
      calls.push(['post', body]);
      const r = postResults.shift();
      if (r instanceof Error) throw r;
      return r ?? { ok: true };
    },
    async health() {
      calls.push(['health']);
      return healthResults.length > 1 ? healthResults.shift() : healthResults[0] ?? null;
    },
    async shutdown(withKey) { calls.push(['shutdown', withKey]); },
    startBridge() { calls.push(['spawn']); },
    foreign() { calls.push(['foreign']); },
    async sleep(ms) { slept += ms; },
  };
  return io;
}
const names = (io) => io.calls.map((c) => c[0]);
// health() answers: our bridge proves it knows the key (bridge-auth.js); anything else does not.
const ours = (version = '2') => ({ ok: true, app: 'miblo-bridge', version, proven: true });
const unproven = (version = '2') => ({ ok: true, app: 'miblo-bridge', version, proven: false });

test('our bridge proves itself on /health, then gets the event', async () => {
  const io = fakeIo({ healthResults: [ours()] });
  assert.equal(await deliver('{}', io, { version: '2' }), 'sent');
  assert.deepEqual(names(io), ['health', 'post']);
});

test('an HTTP error on the post never spawns a bridge', async () => {
  const io = fakeIo({ healthResults: [ours()], postResults: [Object.assign(new Error('bridge 401'), { status: 401 })] });
  assert.equal(await deliver('{}', io), 'dropped');
  assert.deepEqual(names(io), ['health', 'post']);
});

test('nothing on the port: spawn, poll /health every 100 ms until it proves itself, then post once', async () => {
  const io = fakeIo({ healthResults: [null, null, null, ours('1')] });
  assert.equal(await deliver('B', io), 'spawned');
  assert.deepEqual(names(io), ['health', 'spawn', 'health', 'health', 'health', 'post']);
  assert.equal(io.slept(), 300);
});

test('gives up after 1500 ms of polling without posting', async () => {
  const io = fakeIo({ healthResults: [null] });
  assert.equal(await deliver('B', io), 'timeout');
  assert.equal(io.slept(), 1500);
  assert.ok(!names(io).includes('post'));
});

// F5: another program (or another user) holds the port.
test('another app on the port gets nothing: no post, no spawn, noted once', async () => {
  const io = fakeIo({ healthResults: [{ hello: 'world', proven: false }] });
  assert.equal(await deliver('B', io, { version: '2' }), 'foreign');
  assert.deepEqual(names(io), ['health', 'foreign']);
});

test('a listener that says it is the bridge but cannot prove it never gets the event nor the key', async () => {
  // Asked to shut down without the key (an older bridge, before the key, does so); it stays.
  const io = fakeIo({ healthResults: [unproven()] });
  assert.equal(await deliver('B', io, { version: '2' }), 'foreign');
  assert.ok(!names(io).includes('post'));
  assert.deepEqual(io.calls.filter((c) => c[0] === 'shutdown'), [['shutdown', false]]);
  assert.equal(names(io).at(-1), 'foreign');
});

test('an older bridge (before the key) is shut down without the key and replaced; the new one gets the event', async () => {
  const io = fakeIo({ healthResults: [unproven('1'), null, ours('2')] });
  assert.equal(await deliver('B', io, { version: '2' }), 'spawned');
  assert.deepEqual(io.calls.map((c) => c.join(':')), ['health', 'shutdown:false', 'health', 'spawn', 'health', 'post:B']);
});

test('MIBLO_NO_SPAWN: never spawns, never shuts anything down', async () => {
  let io = fakeIo({ healthResults: [null] });
  assert.equal(await deliver('B', io, { allowSpawn: false, version: '2' }), 'dropped');
  assert.deepEqual(names(io), ['health']);
  io = fakeIo({ healthResults: [ours('1')] });
  assert.equal(await deliver('B', io, { allowSpawn: false, version: '2' }), 'sent');
  assert.deepEqual(names(io), ['health', 'post']);
  io = fakeIo({ healthResults: [unproven('1')] });
  assert.equal(await deliver('B', io, { allowSpawn: false, version: '2' }), 'foreign');
  assert.deepEqual(names(io), ['health', 'foreign']);
});

// After /reload-plugins the hooks run the new plugin while the bridge started by the old one keeps
// running: any event hands the bridge over to the hook's version (asked with the key: it is ours).
test('our bridge of another version is shut down with the key and replaced by the hook\'s version', async () => {
  const io = fakeIo({ healthResults: [ours('1'), null, ours('2')] });
  assert.equal(await deliver('B', io, { version: '2' }), 'spawned');
  assert.deepEqual(io.calls.map((c) => c.join(':')), ['health', 'shutdown:true', 'health', 'spawn', 'health', 'post:B']);
});

test('same version: no restart; no version known: never restarts', async () => {
  let io = fakeIo({ healthResults: [ours('2')] });
  assert.equal(await deliver('B', io, { version: '2' }), 'sent');
  assert.deepEqual(names(io), ['health', 'post']);
  io = fakeIo({ healthResults: [ours('1')] });
  assert.equal(await deliver('B', io), 'sent');
  assert.deepEqual(names(io), ['health', 'post']);
});

test('pickEvent forwards the compaction trigger and the SessionStart source', () => {
  assert.deepEqual(
    pickEvent({ session_id: 's', hook_event_name: 'PreCompact', trigger: 'manual', custom_instructions: 'secret' }),
    { session_id: 's', hook_event_name: 'PreCompact', trigger: 'manual' });
  assert.deepEqual(pickEvent({ session_id: 's', hook_event_name: 'SessionStart', source: 'compact' }),
    { session_id: 's', hook_event_name: 'SessionStart', source: 'compact' });
});

test('pickEvent turns a permission_prompt message into a tool name only, never the text', () => {
  const note = (message, extra = {}) => pickEvent({
    session_id: 's', hook_event_name: 'Notification', notification_type: 'permission_prompt', message, title: 'secret', ...extra,
  });
  const base = { session_id: 's', hook_event_name: 'Notification', notification_type: 'permission_prompt' };
  assert.deepEqual(note('Claude needs your permission to use Bash'), { ...base, tool_name: 'Bash' });
  assert.deepEqual(note('Claude needs your permission to use mcp__github__create_issue'),
    { ...base, tool_name: 'mcp__github__create_issue' });
  assert.deepEqual(note('Claude needs your permission to use Bash', { agent_id: 'a1' }), { ...base, agent_id: 'a1', tool_name: 'Bash' });
  // Anything else in the message stays local.
  assert.deepEqual(note('Run `curl -H "Authorization: abc"`?'), base);
  assert.deepEqual(note(42), base);
  assert.deepEqual(note('Claude needs your permission to use ' + 'x'.repeat(100)), base);
  // Other notifications never get a tool from their message.
  assert.deepEqual(
    pickEvent({ session_id: 's', hook_event_name: 'Notification', notification_type: 'idle_prompt', message: 'Claude needs your permission to use Bash' }),
    { session_id: 's', hook_event_name: 'Notification', notification_type: 'idle_prompt' });
});

test('pickEvent takes only the tool from a worker_permission_prompt message', () => {
  const base = { session_id: 's', hook_event_name: 'Notification', notification_type: 'worker_permission_prompt' };
  assert.deepEqual(pickEvent({ ...base, message: 'researcher needs permission for Bash' }), { ...base, tool_name: 'Bash' });
  assert.deepEqual(pickEvent({ ...base, message: 'researcher needs permission for rm -rf secret' }), base);
});

test('pickEvent forwards an unknown notification_type but never its message', () => {
  const base = { session_id: 's', hook_event_name: 'Notification', notification_type: 'future_permission_prompt' };
  assert.deepEqual(pickEvent({ ...base, message: 'Claude needs your permission to use Bash', title: 'secret' }), base);
  // Only a short identifier-like type is forwarded.
  for (const bad of ['x'.repeat(65), 'has spaces in it', 'a\nb', '']) {
    assert.deepEqual(pickEvent({ ...base, notification_type: bad }), { session_id: 's', hook_event_name: 'Notification' }, bad);
  }
});
