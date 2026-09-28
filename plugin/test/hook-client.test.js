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
