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

function fakeIo({ postResults = [], healthResults = [], reply = { ok: true, app: 'miblo-bridge', version: '2' } } = {}) {
  const calls = [];
  let slept = 0;
  const io = {
    calls,
    slept: () => slept,
    async post(body) {
      calls.push(['post', body]);
      const r = postResults.shift();
      if (r instanceof Error) throw r;
      return r ?? reply;
    },
    async health() {
      calls.push(['health']);
      return healthResults.length > 1 ? healthResults.shift() : healthResults[0] ?? null;
    },
    async shutdown() { calls.push(['shutdown']); },
    startBridge() { calls.push(['spawn']); },
    async sleep(ms) { slept += ms; },
  };
  return io;
}
const refused = () => new TypeError('fetch failed');
const names = (io) => io.calls.map((c) => c[0]);

test('delivered on the first try: no spawn', async () => {
  const io = fakeIo();
  assert.equal(await deliver('{}', io), 'sent');
  assert.deepEqual(names(io), ['post']);
});

test('HTTP error status never spawns a bridge', async () => {
  const io = fakeIo({ postResults: [Object.assign(new Error('bridge 403'), { status: 403 })] });
  assert.equal(await deliver('{}', io), 'dropped');
  assert.deepEqual(names(io), ['post']);
});

test('connection refused: spawn, poll /health every 100 ms, then post once', async () => {
  const bridge = { ok: true, app: 'miblo-bridge', version: '1' };
  const io = fakeIo({ postResults: [refused()], healthResults: [null, null, null, bridge] });
  assert.equal(await deliver('B', io), 'spawned');
  assert.deepEqual(names(io), ['post', 'health', 'spawn', 'health', 'health', 'health', 'post']);
  assert.equal(io.slept(), 300);
});

test('gives up after 1500 ms of polling without posting again', async () => {
  const io = fakeIo({ postResults: [refused()], healthResults: [null] });
  assert.equal(await deliver('B', io), 'timeout');
  assert.equal(io.slept(), 1500);
  assert.equal(names(io).filter((n) => n === 'post').length, 1);
});

test('does not spawn when another app owns the port', async () => {
  const io = fakeIo({ postResults: [refused()], healthResults: [{ hello: 'world' }] });
  assert.equal(await deliver('B', io), 'foreign');
  assert.ok(!names(io).includes('spawn'));
});

test('MIBLO_NO_SPAWN: never spawns', async () => {
  const io = fakeIo({ postResults: [refused()] });
  assert.equal(await deliver('B', io, { allowSpawn: false, checkVersion: true, version: '2' }), 'dropped');
  assert.deepEqual(names(io), ['post']);
});

test('version mismatch on SessionStart: shutdown old bridge, spawn new one, post', async () => {
  const old = { ok: true, app: 'miblo-bridge', version: '1' };
  const fresh = { ok: true, app: 'miblo-bridge', version: '2' };
  const io = fakeIo({ healthResults: [old, null, fresh] });
  assert.equal(await deliver('B', io, { checkVersion: true, version: '2' }), 'spawned');
  assert.deepEqual(names(io), ['health', 'shutdown', 'health', 'spawn', 'health', 'post']);
});

test('matching version: plain delivery', async () => {
  const io = fakeIo({ healthResults: [{ ok: true, app: 'miblo-bridge', version: '2' }] });
  assert.equal(await deliver('B', io, { checkVersion: true, version: '2' }), 'sent');
  assert.deepEqual(names(io), ['health', 'post']);
});

// After /reload-plugins the hooks run the new plugin while the bridge started by the old one keeps
// running: any event, not only SessionStart, hands the bridge over to the hook's version.
test('a bridge of another version answering any event is replaced by the hook\'s version', () => {
  const fresh = { ok: true, app: 'miblo-bridge', version: '2' };
  const old = { ok: true, app: 'miblo-bridge', version: '1' };
  const table = [
    ['older bridge', old],
    ['bridge from before replies carried a version', { ok: true }],
  ];
  return Promise.all(table.map(async ([label, reply]) => {
    const io = fakeIo({ postResults: [reply], healthResults: [old, null, fresh] });
    assert.equal(await deliver('B', io, { version: '2' }), 'spawned', label);
    assert.deepEqual(names(io), ['post', 'health', 'shutdown', 'health', 'spawn', 'health', 'post'], label);
  }));
});

test('a reply without a version from another app on the port never shuts it down', async () => {
  const io = fakeIo({ reply: { ok: true }, healthResults: [{ hello: 'world' }] });
  assert.equal(await deliver('B', io, { version: '2' }), 'sent');
  assert.deepEqual(names(io), ['post', 'health']);
});

test('same version in the reply: no restart; no version known: never restarts', async () => {
  let io = fakeIo();
  assert.equal(await deliver('B', io, { version: '2' }), 'sent');
  assert.deepEqual(names(io), ['post']);
  io = fakeIo({ reply: { ok: true } });
  assert.equal(await deliver('B', io), 'sent');
  assert.deepEqual(names(io), ['post']);
});

test('MIBLO_NO_SPAWN: an older bridge is left alone', async () => {
  const io = fakeIo({ reply: { ok: true, version: '1' } });
  assert.equal(await deliver('B', io, { allowSpawn: false, version: '2' }), 'sent');
  assert.deepEqual(names(io), ['post']);
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
