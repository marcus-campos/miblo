// End to end, without hardware: the real plugin programs (bin/hook.js as Claude Code runs it, with
// the event on stdin; bin/bridge.js; bin/miblo.js as the slash commands run it) talk real HTTP on
// localhost to the fake gadget (fakes/fake-device.js, the firmware's HTTP API contract).
// The fixed-clock scenarios also write contract fixtures (UPDATE_FIXTURES=1) that the firmware's
// native snapshot parser reads (firmware/test/test_snapshot).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import net from 'node:net';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { startFakeDevice } from './fakes/fake-device.js';
import { fakeImage } from './fakes/fake-github.js';
import { DeviceClient } from '../lib/device-client.js';
import { DeviceStore } from '../lib/device-store.js';
import { ZoneOffsets, intlOffset } from '../lib/tz-offsets.js';
import { pluginVersion } from '../lib/constants.js';
import { createBridge } from '../bin/bridge.js';
import { run } from '../bin/miblo.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const BIN = path.resolve(here, '../bin');
const FIXTURES = path.resolve(here, '../../fixtures/snapshots');
const VERSION = pluginVersion();
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// Text that must never leave the computer: prompts, notification and elicitation messages, tool
// output, transcripts. Every hook payload below carries it somewhere.
const SECRET = 'SECRET-d41d8c';

// The bridge key the hooks and the bridge share (lib/bridge-auth.js).
const bridgeKey = (dataDir) => {
  try { return fs.readFileSync(path.join(dataDir, 'bridge.key'), 'utf8').trim(); } catch { return ''; }
};

function tmpRoot() {
  return fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-e2e-'));
}

function freePort() {
  return new Promise((resolve) => {
    const s = net.createServer();
    s.listen(0, '127.0.0.1', () => {
      const { port } = s.address();
      s.close(() => resolve(port));
    });
  });
}

// Runs a plugin program like Claude Code does: node <script>, stdin, a clean Miblo environment.
function node(script, args, { env, input = '' }) {
  const e = { ...process.env };
  for (const k of ['MIBLO_NO_SPAWN', 'MIBLO_PORT', 'CLAUDE_PLUGIN_DATA', 'CLAUDE_CONFIG_DIR']) delete e[k];
  return new Promise((resolve, reject) => {
    const child = spawn(process.execPath, [path.join(BIN, script), ...args], { env: { ...e, ...env } });
    let out = '';
    let err = '';
    child.stdout.on('data', (d) => { out += d; });
    child.stderr.on('data', (d) => { err += d; });
    child.on('error', reject);
    child.on('close', (code) => resolve({ code, out, err }));
    child.stdin.end(input);
  });
}

async function waitFor(what, check, ms = 4000) {
  const until = Date.now() + ms;
  for (;;) {
    const v = check();
    if (v) return v;
    if (Date.now() > until) assert.fail(`timed out waiting for ${what}`);
    await sleep(20);
  }
}

// A Claude Code hook payload: the fields every event carries, plus the event's own.
const payload = (sid, cwd, name, extra = {}) => ({
  session_id: sid,
  transcript_path: `/Users/me/.claude/projects/${SECRET}/${sid}.jsonl`,
  cwd,
  permission_mode: 'default',
  hook_event_name: name,
  ...extra,
});

const sessionOf = (snap, short) => snap?.sessions?.find((s) => s.id === short);
const alertsOf = (snap, short) => (snap?.alerts ?? []).filter((a) => a.sid === short).map((a) => a.kind);

// ---- 1. Real processes: the CLI pairs, a hook starts the bridge, the gadget follows along ----

test('processes: /miblo:pair, hooks start the bridge and every prompt state reaches the gadget', async () => {
  const root = tmpRoot();
  const dataDir = path.join(root, 'data');
  const port = await freePort();
  const env = { MIBLO_PORT: String(port), CLAUDE_PLUGIN_DATA: dataDir, CLAUDE_CONFIG_DIR: path.join(root, 'claude') };
  // The release check is fresh: the bridge never asks GitHub (the test runs offline).
  fs.mkdirSync(dataDir, { recursive: true });
  fs.writeFileSync(path.join(dataDir, 'update-check.json'), JSON.stringify({ checkedAt: Date.now(), latest: '9.9.9' }));
  // Busy (low on memory) for the first pairing request: the CLI retries it.
  const dev = await startFakeDevice({ busy: 1, busyPath: '/api/pair' });
  const cli = (...a) => node('miblo.js', ['--data', dataDir, ...a], { env });
  const hook = async (evt) => {
    const r = await node('hook.js', [], { env, input: JSON.stringify(evt) });
    assert.equal(r.code, 0);
    assert.equal(r.out, '', 'a hook never writes to stdout');
  };
  const last = () => dev.state.snapshots.at(-1);
  const sid = '7c0ffee0-1111-4aaa-8bbb-000000000001';
  const S = '7c0ffee0';
  const cwd = '/Users/me/work/api-server';
  const until = (what, pred) => waitFor(what, () => { const s = last(); return s && pred(s) ? s : null; });
  try {
    let r = await cli('pair', dev.addr, '0000');
    assert.equal(r.code, 2);
    assert.match(r.out, /Wrong pairing code/);
    r = await cli('pair', dev.addr, '4827');
    assert.equal(r.code, 0, r.out);
    assert.match(r.out, /^Paired with Miblo-4F2A \(miblo-4f2a\)/);
    assert.equal(dev.state.busyHits, 1);
    assert.equal(new DeviceStore(dataDir).list()[0].token, dev.state.token);

    // No bridge yet: the SessionStart hook starts it, and it pushes the session.
    await hook(payload(sid, cwd, 'SessionStart', { source: 'startup', model: 'claude-opus' }));
    let snap = await until('the new session', (s) => sessionOf(s, S)?.st === 'idle');
    assert.equal(snap.v, 1);
    assert.equal(snap.latest, '9.9.9');
    assert.equal(sessionOf(snap, S).name, 'api-server');

    await hook(payload(sid, cwd, 'UserPromptSubmit', { prompt: `fix the tests ${SECRET}` }));
    await until('working', (s) => sessionOf(s, S)?.st === 'running');

    await hook(payload(sid, cwd, 'PreToolUse', { tool_name: 'Bash', tool_use_id: 't1', tool_input: { command: 'npm test', description: 'Run the tests' } }));
    snap = await until('the command', (s) => sessionOf(s, S)?.det === 'npm test');
    assert.equal(sessionOf(snap, S).tool, 'Bash');
    assert.ok(sessionOf(snap, S).ts > 0, 'a running command carries its start');

    // A permission prompt (auto mode: only the notification reaches the hooks).
    await hook(payload(sid, cwd, 'Notification', { notification_type: 'permission_prompt', message: 'Claude needs your permission to use Bash', title: SECRET }));
    snap = await until('the permission prompt', (s) => sessionOf(s, S)?.st === 'perm');
    assert.deepEqual(alertsOf(snap, S), ['perm']);
    assert.equal(sessionOf(snap, S).tool, 'Bash');

    await hook(payload(sid, cwd, 'PostToolUse', { tool_name: 'Bash', tool_input: { command: 'npm test' }, tool_response: { stdout: SECRET } }));
    snap = await until('the prompt answered', (s) => sessionOf(s, S)?.st === 'running');
    assert.deepEqual(alertsOf(snap, S), []);

    // A subagent (worker) asks for permission: the main session shows it, with the tool.
    await hook(payload(sid, cwd, 'SubagentStart', { agent_id: 'a-1', agent_type: 'researcher' }));
    await hook(payload(sid, cwd, 'Notification', { agent_id: 'a-1', notification_type: 'worker_permission_prompt', message: 'researcher needs permission for WebFetch' }));
    snap = await until('the worker prompt', (s) => sessionOf(s, S)?.st === 'perm' && sessionOf(s, S)?.tool === 'WebFetch');
    assert.deepEqual(alertsOf(snap, S), ['perm']);
    await hook(payload(sid, cwd, 'PostToolUse', { agent_id: 'a-1', agent_type: 'researcher', tool_name: 'WebFetch', tool_input: { url: 'https://example.com' } }));
    await until('the worker prompt answered', (s) => sessionOf(s, S)?.st === 'running');
    await hook(payload(sid, cwd, 'SubagentStop', { agent_id: 'a-1', agent_type: 'researcher' }));

    // A notification type a later Claude Code adds is read by its name.
    await hook(payload(sid, cwd, 'Notification', { notification_type: 'sandbox_permission_prompt', message: SECRET }));
    snap = await until('an unknown permission type', (s) => sessionOf(s, S)?.st === 'perm');
    assert.deepEqual(alertsOf(snap, S), ['perm']);
    await hook(payload(sid, cwd, 'PostToolUse', { tool_name: 'Bash', tool_input: { command: 'ls' } }));
    await until('back to work', (s) => sessionOf(s, S)?.st === 'running');
    const pushes = dev.state.snapshots.length;
    // Settled or uninteresting notifications change nothing (no push at all).
    await hook(payload(sid, cwd, 'Notification', { notification_type: 'permission_granted', message: SECRET }));
    await hook(payload(sid, cwd, 'Notification', { notification_type: 'idle_prompt', message: 'Claude is waiting for your input' }));
    await hook(payload(sid, cwd, 'TeammateIdle', { message: SECRET }));
    await sleep(300);
    assert.equal(dev.state.snapshots.length, pushes);
    assert.equal(sessionOf(last(), S).st, 'running');

    await hook(payload(sid, cwd, 'Notification', { notification_type: 'plan_approval_dialog', message: SECRET }));
    snap = await until('an unknown question type', (s) => sessionOf(s, S)?.st === 'question');
    assert.deepEqual(alertsOf(snap, S), ['question']);
    await hook(payload(sid, cwd, 'UserPromptSubmit', { prompt: SECRET }));
    await until('working again', (s) => sessionOf(s, S)?.st === 'running');

    // Questions: Claude's own (AskUserQuestion) and an MCP server's (elicitation).
    await hook(payload(sid, cwd, 'PreToolUse', { tool_name: 'AskUserQuestion', tool_input: { questions: [{ question: SECRET }] } }));
    snap = await until('the question', (s) => sessionOf(s, S)?.st === 'question');
    assert.deepEqual(alertsOf(snap, S), ['question']);
    await hook(payload(sid, cwd, 'PostToolUse', { tool_name: 'AskUserQuestion', tool_response: { answers: SECRET } }));
    await until('the question answered', (s) => sessionOf(s, S)?.st === 'running');
    await hook(payload(sid, cwd, 'Elicitation', { mcp_server_name: 'jira', message: SECRET, requested_schema: { type: 'object' } }));
    await until('the elicitation', (s) => sessionOf(s, S)?.st === 'question');
    await hook(payload(sid, cwd, 'ElicitationResult', { mcp_server_name: 'jira', action: 'accept', content: { v: SECRET } }));
    await until('the elicitation answered', (s) => sessionOf(s, S)?.st === 'running');

    // Finished, and a second session whose turn an API error ended.
    await hook(payload(sid, cwd, 'Stop', { stop_hook_active: false, last_assistant_message: SECRET }));
    snap = await until('finished', (s) => sessionOf(s, S)?.st === 'done');
    assert.deepEqual(alertsOf(snap, S), ['done']);
    const sid2 = '8d0ffee0-2222-4aaa-8bbb-000000000002';
    await hook(payload(sid2, '/Users/me/work/front', 'UserPromptSubmit', { prompt: SECRET }));
    await until('the second session', (s) => sessionOf(s, '8d0ffee0')?.st === 'running');
    await hook(payload(sid2, '/Users/me/work/front', 'StopFailure', { error: 'rate_limit', error_details: SECRET }));
    snap = await until('the failed turn', (s) => s.sessions.filter((x) => x.st === 'done').length === 2);
    assert.equal(snap.alerts.filter((a) => a.kind === 'done').length, 2);

    // The status line reading reaches the gadget and the CLI reads the running bridge.
    const reset = Math.floor(Date.now() / 1000) + 3600;
    await fetch(`http://127.0.0.1:${port}/statusline`, {
      method: 'POST', headers: { 'content-type': 'application/json', 'x-miblo-key': bridgeKey(dataDir) },
      body: JSON.stringify({ session_id: sid, model: { display_name: 'Opus' }, cost: { total_cost_usd: 1.25 },
        context_window: { used_percentage: 40, total_input_tokens: 9000, total_output_tokens: 1000 },
        rate_limits: { five_hour: { used_percentage: 62, resets_at: reset }, seven_day: { used_percentage: 20, resets_at: reset + 86400 } } }),
    });
    snap = await until('the limits', (s) => s.usage?.h5?.pct === 62);
    assert.equal(sessionOf(snap, S).model, 'Opus');
    assert.equal(sessionOf(snap, S).ctx, 40);

    r = await cli('status');
    assert.equal(r.code, 0);
    const st = JSON.parse(r.out);
    assert.equal(st.bridge, 'running');
    assert.equal(st.devices[0].online, true);
    assert.equal(st.sessions.length, 2);
    assert.ok(!r.out.includes(dev.state.token), 'status never prints the token');
    r = await cli('today');
    assert.match(r.out, /^Today: 2 responses, \d+ min with Claude working, US\$ 1\.25\.\n5h 62% \(resets [^)]+\) · week 20% \(resets [^)]+\)\n/);
    r = await cli('limits');
    assert.match(r.out, /^5h 62% /);

    // The session ends: it leaves the gadget.
    await hook(payload(sid, cwd, 'SessionEnd', { reason: 'prompt_input_exit' }));
    snap = await until('the session gone', (s) => !sessionOf(s, S) || s.sessions.length === 1);
    assert.equal(snap.sessions.length, 1);

    const all = JSON.stringify(dev.state.snapshots);
    assert.ok(!all.includes(SECRET), 'no prompt, message or output text ever reaches the gadget');
    assert.ok(!all.includes('needs your permission') && !all.includes('needs permission for'));
    for (const s of dev.state.snapshots) assert.ok(Buffer.byteLength(JSON.stringify(s)) <= 3072, 'within the gadget caps');
  } finally {
    await fetch(`http://127.0.0.1:${port}/shutdown`, { method: 'POST', headers: { 'content-type': 'application/json', 'x-miblo-key': bridgeKey(dataDir) }, body: '{}' }).catch(() => {});
    await dev.close();
  }
});

test('processes: two Claude Code windows starting at once share one bridge, and neither event is lost', async () => {
  const root = tmpRoot();
  const dataDir = path.join(root, 'data');
  const port = await freePort();
  const env = { MIBLO_PORT: String(port), CLAUDE_PLUGIN_DATA: dataDir, CLAUDE_CONFIG_DIR: path.join(root, 'claude') };
  fs.mkdirSync(dataDir, { recursive: true });
  fs.writeFileSync(path.join(dataDir, 'update-check.json'), JSON.stringify({ checkedAt: Date.now(), latest: null }));
  const dev = await startFakeDevice();
  try {
    assert.equal((await node('miblo.js', ['--data', dataDir, 'pair', dev.addr, '4827'], { env })).code, 0);
    const hook = (sid, cwd) => node('hook.js', [], { env, input: JSON.stringify(payload(sid, cwd, 'SessionStart', { source: 'startup' })) });
    const rs = await Promise.all([hook('11111111-aaaa', '/w/one'), hook('22222222-bbbb', '/w/two'), hook('33333333-cccc', '/w/three')]);
    assert.deepEqual(rs.map((r) => r.code), [0, 0, 0]);
    const snap = await waitFor('all three sessions', () => {
      const s = dev.state.snapshots.at(-1);
      return s?.sessions?.length === 3 ? s : null;
    });
    assert.deepEqual(snap.sessions.map((x) => x.name).sort(), ['one', 'three', 'two']);
    assert.equal(snap.latest, undefined);  // no release known: the field is left out
    const health = await (await fetch(`http://127.0.0.1:${port}/health`)).json();
    assert.deepEqual([health.app, health.version], ['miblo-bridge', VERSION]);
  } finally {
    await fetch(`http://127.0.0.1:${port}/shutdown`, { method: 'POST', headers: { 'content-type': 'application/json', 'x-miblo-key': bridgeKey(dataDir) }, body: '{}' }).catch(() => {});
    await dev.close();
  }
});

test('processes: a Desktop agent session (no cwd, no SessionStart, Claude in Chrome) reaches the gadget without its text', async () => {
  const root = tmpRoot();
  const dataDir = path.join(root, 'data');
  const port = await freePort();
  // Desktop's agent sessions (Cowork) run the CLI with this entrypoint; hooks inherit it.
  const env = { MIBLO_PORT: String(port), CLAUDE_PLUGIN_DATA: dataDir, CLAUDE_CONFIG_DIR: path.join(root, 'claude'), CLAUDE_CODE_ENTRYPOINT: 'local-agent' };
  fs.mkdirSync(dataDir, { recursive: true });
  fs.writeFileSync(path.join(dataDir, 'update-check.json'), JSON.stringify({ checkedAt: Date.now(), latest: null }));
  const dev = await startFakeDevice();
  const sid = 'dd5e55e0-2222-4aaa-8bbb-000000000002';
  const S = 'dd5e55e0';
  // No cwd at all: the documented fields are required in Claude Code, but nothing guarantees them here.
  const evt = (name, extra = {}) => {
    const p = payload(sid, undefined, name, extra);
    delete p.cwd;
    return JSON.stringify(p);
  };
  const hook = async (input) => assert.equal((await node('hook.js', [], { env, input })).code, 0);
  const until = (what, pred) => waitFor(what, () => { const s = dev.state.snapshots.at(-1); return s && pred(s) ? s : null; });
  try {
    assert.equal((await node('miblo.js', ['--data', dataDir, 'pair', dev.addr, '4827'], { env })).code, 0);
    await hook(evt('UserPromptSubmit', { prompt: `book a table ${SECRET}` }));
    let snap = await until('the session', (s) => sessionOf(s, S)?.st === 'running');
    assert.equal(sessionOf(snap, S).name, 'session');
    await hook(evt('PreToolUse', { tool_name: 'mcp__claude-in-chrome__navigate', tool_input: { url: `https://example.com/${SECRET}`, text: SECRET } }));
    snap = await until('browsing', (s) => sessionOf(s, S)?.det === 'browsing');
    assert.equal(sessionOf(snap, S).tool, 'navigate');
    await hook(evt('Stop', { last_assistant_message: SECRET }));
    snap = await until('done', (s) => sessionOf(s, S)?.st === 'done');
    assert.ok(!JSON.stringify(dev.state.snapshots).includes(SECRET), 'no text reaches the gadget');
  } finally {
    await fetch(`http://127.0.0.1:${port}/shutdown`, { method: 'POST', headers: { 'content-type': 'application/json', 'x-miblo-key': bridgeKey(dataDir) }, body: '{}' }).catch(() => {});
    await dev.close();
  }
});

// ---- 2. Daily commands as the slash commands run them ----

test('processes: the daily commands reach the gadget (say, remind, timer, countdown, focus, meeting, find, blue, update)', async () => {
  const root = tmpRoot();
  const dataDir = path.join(root, 'data');
  // Nothing listens on this port: today/limits say the bridge is not running.
  const env = { MIBLO_PORT: String(await freePort()), CLAUDE_PLUGIN_DATA: dataDir, CLAUDE_CONFIG_DIR: path.join(root, 'claude') };
  const dev = await startFakeDevice({ fw: '1.11.0', otaCode: '5309', rebootMs: 50 });
  const cli = (...a) => node('miblo.js', ['--data', dataDir, ...a], { env });
  const ok = async (...a) => {
    const r = await cli(...a);
    assert.equal(r.code, 0, `${a.join(' ')}: ${r.out}`);
    assert.ok(!r.out.includes(dev.state.token ?? '\u0000'), 'never prints the token');
    return r.out;
  };
  try {
    await ok('pair', dev.addr, '4827');

    assert.match(await ok('say', 'Back', 'at', '3', '--min', '20'), /Miblo-4F2A/);
    assert.deepEqual(dev.state.say, { text: 'Back at 3', min: 20 });
    await ok('say', 'off');
    assert.deepEqual(dev.state.say, { off: true });

    await ok('remind', '10', 'stretch');
    await ok('remind', 'weekdays', '09:45', 'stand-up');
    await ok('remind', 'every', 'day', '8h30', 'water', 'the', 'plants');
    assert.deepEqual(dev.state.reminders.map(({ id, at, days, text }) => ({ id, at, days, text })), [
      { id: 1, at: undefined, days: undefined, text: 'stretch' },
      { id: 5, at: '09:45', days: 62, text: 'stand-up' },
      { id: 6, at: '08:30', days: 127, text: 'water the plants' },
    ]);
    const list = await ok('remind');
    assert.match(list, /stretch/);
    assert.match(list, /09:45.*stand-up|stand-up.*09:45/);
    await ok('remind', 'off', '5');
    assert.deepEqual(dev.state.reminders.map((x) => x.id), [1, 6]);

    await ok('timer', '25');
    assert.ok(dev.state.timerUntil > Date.now() + 24 * 60_000);
    await ok('timer', 'stop');
    assert.equal(dev.state.timerUntil, 0);

    assert.match(await ok('countdown', 'launch', '31/12'), /Countdown on Miblo-4F2A: "launch" on 31\/12\./);
    assert.deepEqual(dev.state.countdown, { label: 'launch', md: '12-31' });
    assert.match(await ok('countdown'), /Miblo-4F2A: "launch" on 31\/12\/\d{4}/);

    await ok('focus', '50', '10', '2');
    assert.deepEqual(dev.state.focus, { focusMin: 50, breakMin: 10, rounds: 2 });
    assert.match(await ok('focus', 'status'), /Miblo-4F2A/);
    await ok('focus', 'stop');
    assert.deepEqual(dev.state.focus, { stop: true });

    await ok('meeting', '30');
    assert.deepEqual(dev.state.meeting, { min: 30 });
    await ok('meeting', 'off');
    await ok('find');
    assert.equal(dev.state.finds, 1);

    await ok('blue', '60%');
    assert.equal(dev.state.config.blueStrength, 60);
    await ok('blue', '21:00', '07:00');
    assert.deepEqual([dev.state.config.blueFilter, dev.state.config.blueFrom, dev.state.config.blueTo], [2, 1260, 420]);
    assert.match(await ok('blue', 'status'), /on from 21:00 to 07:00 \(strength 60%\)/);

    assert.match(await ok('today'), /bridge isn't running/);
    assert.match(await ok('limits'), /bridge isn't running/);

    // A firmware update from a local image: open shows a code on the gadget, send uploads it.
    const file = path.join(root, 'miblo-geekmagic_ultra-1.12.0.bin');
    fs.writeFileSync(file, fakeImage('e2e'));
    const opened = JSON.parse(await ok('update', 'open', '--file', file));
    assert.deepEqual([opened.from, opened.to, opened.codeRequired], ['1.11.0', '1.12.0', true]);
    let r = await cli('update', 'send', '0000');
    assert.equal(r.code, 2);
    assert.match(r.out, /Wrong code/);
    assert.match(await ok('update', 'send', '5309'), /Miblo-4F2A updated from 1\.11\.0 to 1\.12\.0\./);
    assert.equal(dev.state.fw, '1.12.0');
    assert.equal(dev.state.uploads.length, 1);
    assert.deepEqual(dev.state.uploads[0].bytes, fs.readFileSync(file));

    // The gadget dropped this computer (reset on its screen): the commands say so, pairing again fixes it.
    dev.state.tokens = [];
    r = await cli('find');
    assert.equal(r.code, 1);
    assert.match(r.out, /pair/i);
    await ok('pair', dev.addr, '4827');
    await ok('find');
    assert.equal(dev.state.finds, 2);
  } finally {
    await dev.close();
  }
});

// ---- 3. Fixed clock: real hook processes into the bridge, deterministic snapshots ----

const T0 = Date.UTC(2026, 9, 20, 12, 20, 0);  // a Tuesday, two weeks before New York leaves DST
const intlZones = () => new ZoneOffsets({ source: (z) => (utc) => intlOffset(z, utc * 1000) });

// A bridge on a fixed clock, listening on a free port, pushing to `dev`; `hook(evt)` runs
// bin/hook.js against it and lets the debounced push land before the clock moves on.
async function fixedBridge(dev, { tz, tz2, discoverFn = async () => [], extraDevs = [], addrOk = null } = {}) {
  const dataDir = path.join(tmpRoot(), 'data');
  const store = new DeviceStore(dataDir);
  for (const [i, d] of [dev, ...extraDevs].entries()) {
    const token = `tok-${i}`;
    d.state.tokens.push(token);
    store.upsert({ id: d.id ?? 'miblo-4f2a', name: d.name ?? 'Miblo-4F2A', addr: d.addr, token });
  }
  if (tz !== undefined) dev.state.config.tz = tz;
  if (tz2 !== undefined) dev.state.config.tz2 = tz2;
  let t = T0;
  const clock = { now: () => t, advance: (ms) => { t += ms; }, set: (ms) => { t = ms; } };
  const bridge = createBridge({
    dataDir, now: clock.now, client: new DeviceClient({ busyRetryMs: [] }), discoverFn, host: 'MacBook-Marcus',
    version: VERSION, release: { get: () => '1.12.0', refreshIfStale: async () => {} }, zones: intlZones(), addrOk,
  });
  // Pushes still on their way to the gadget: settled before the clock moves on or a test reads.
  const inflight = new Set();
  const pushAll = bridge.devices.pushAll.bind(bridge.devices);
  bridge.devices.pushAll = (snap) => {
    const p = pushAll(snap);
    inflight.add(p);
    p.finally(() => inflight.delete(p)).catch(() => {});
    return p;
  };
  const settle = () => Promise.all([...inflight]);
  await new Promise((r) => bridge.server.listen(0, '127.0.0.1', r));
  const env = { MIBLO_PORT: String(bridge.server.address().port), MIBLO_NO_SPAWN: '1', CLAUDE_PLUGIN_DATA: dataDir };
  const hook = async (evt, stepMs = 2000) => {
    const r = await node('hook.js', [], { env, input: JSON.stringify(evt) });
    assert.equal(r.code, 0);
    await sleep(220);  // DEBOUNCE_MS: the push for this event goes out at this clock reading
    await settle();
    clock.advance(stepMs);
  };
  return {
    bridge, clock, hook, dataDir, store, settle,
    statusline: (sl) => fetch(`http://127.0.0.1:${env.MIBLO_PORT}/statusline`, { method: 'POST', headers: { 'content-type': 'application/json', 'x-miblo-key': bridge.key }, body: JSON.stringify(sl) }),
    stop: () => new Promise((r) => bridge.server.close(r)),
  };
}

// The last snapshot the gadget received as a contract fixture: written with UPDATE_FIXTURES=1,
// compared otherwise. firmware/test/test_snapshot parses the same files.
function fixture(name, actual) {
  const file = path.join(FIXTURES, `${name}.json`);
  if (process.env.UPDATE_FIXTURES === '1') {
    fs.mkdirSync(FIXTURES, { recursive: true });
    fs.writeFileSync(file, JSON.stringify(actual, null, 2) + '\n');
  }
  assert.deepEqual(actual, JSON.parse(fs.readFileSync(file, 'utf8')));
}

const S5 = Math.floor(T0 / 1000);
const BERLIN = { z: 'Europe/Berlin', off: 120, next: Date.UTC(2026, 9, 25, 1) / 1000, noff: 60 };
const NEW_YORK = { z: 'America/New_York', off: -240, next: Date.UTC(2026, 10, 1, 6) / 1000, noff: -300 };

test('fixed clock: every kind of prompt, through the hook, as the gadget receives it (e2e-prompts)', async () => {
  const dev = await startFakeDevice();
  const b = await fixedBridge(dev, { tz: 'Europe/Berlin', tz2: 'America/New_York' });
  try {
    // A: Claude asks to run a command (PermissionRequest after its PreToolUse).
    await b.hook(payload('aaaaaaaa-0001', '/w/api-server', 'SessionStart', { source: 'startup' }));
    await b.hook(payload('aaaaaaaa-0001', '/w/api-server', 'UserPromptSubmit', { prompt: SECRET }));
    await b.hook(payload('aaaaaaaa-0001', '/w/api-server', 'PreToolUse', { tool_name: 'Bash', tool_input: { command: 'git push origin main' } }));
    await b.hook(payload('aaaaaaaa-0001', '/w/api-server', 'PermissionRequest', { tool_name: 'Bash', tool_input: { command: 'git push origin main' } }));
    await b.hook(payload('aaaaaaaa-0001', '/w/api-server', 'Notification', { notification_type: 'permission_prompt', message: 'Claude needs your permission to use Bash' }));
    // B: a subagent (worker) asks, by notification only.
    await b.hook(payload('bbbbbbbb-0002', '/w/infra', 'UserPromptSubmit', { prompt: SECRET }));
    await b.hook(payload('bbbbbbbb-0002', '/w/infra', 'SubagentStart', { agent_id: 'w1', agent_type: 'researcher' }));
    await b.hook(payload('bbbbbbbb-0002', '/w/infra', 'Notification', { agent_id: 'w1', notification_type: 'worker_permission_prompt', message: 'researcher needs permission for WebFetch' }));
    // C: a notification type a later Claude Code adds ("approval": a question).
    await b.hook(payload('cccccccc-0003', '/w/front-app', 'UserPromptSubmit', { prompt: SECRET }));
    await b.hook(payload('cccccccc-0003', '/w/front-app', 'Notification', { notification_type: 'plan_approval_dialog', message: SECRET }));
    // D: an MCP server asks (elicitation).
    await b.hook(payload('dddddddd-0004', '/w/docs', 'UserPromptSubmit', { prompt: SECRET }));
    await b.hook(payload('dddddddd-0004', '/w/docs', 'Elicitation', { mcp_server_name: 'jira', message: SECRET }));
    // E: Claude asks a question; F: still working on a long command.
    await b.hook(payload('eeeeeeee-0005', '/w/mobile', 'PreToolUse', { tool_name: 'AskUserQuestion', tool_input: { questions: [{ question: SECRET }] } }));
    await b.hook(payload('ffffffff-0006', '/w/worker', 'PreToolUse', { tool_name: 'Bash', tool_input: { command: 'npm run e2e' } }), 0);

    const snap = dev.state.snapshots.at(-1);
    assert.equal(snap.now, S5 + 13 * 2);
    const st = Object.fromEntries(snap.sessions.map((s) => [s.id, [s.st, s.tool, s.det]]));
    assert.deepEqual(st, {
      aaaaaaaa: ['perm', 'Bash', 'git push origin main'],
      bbbbbbbb: ['perm', 'WebFetch', ''],
      cccccccc: ['question', '', ''],
      dddddddd: ['question', '', ''],
      eeeeeeee: ['question', '', ''],
      ffffffff: ['running', 'Bash', 'npm run e2e'],
    });
    assert.deepEqual(snap.alerts.map((a) => [a.kind, a.sid]), [
      ['perm', 'aaaaaaaa'], ['perm', 'bbbbbbbb'], ['question', 'cccccccc'], ['question', 'dddddddd'], ['question', 'eeeeeeee'],
    ]);
    assert.equal(snap.sessions.find((s) => s.id === 'ffffffff').ts, snap.now);
    assert.deepEqual(snap.tz, [BERLIN, NEW_YORK]);
    assert.ok(!JSON.stringify(dev.state.snapshots).includes(SECRET));
    fixture('e2e-prompts', snap);
  } finally {
    await b.stop();
    await dev.close();
  }
});

test('fixed clock: finished, failed, waiting, compacting and ended sessions (e2e-finished)', async () => {
  const dev = await startFakeDevice();
  const b = await fixedBridge(dev, { tz: 'America/Sao_Paulo' });
  try {
    const sid = (c) => `${c.repeat(8)}-0001`;
    for (const c of ['a', 'b', 'c', 'd', 'e']) await b.hook(payload(sid(c), `/w/p-${c}`, 'UserPromptSubmit', { prompt: SECRET }));
    await b.hook(payload(sid('a'), '/w/p-a', 'Stop', { stop_hook_active: false, last_assistant_message: SECRET }));
    await b.hook(payload(sid('b'), '/w/p-b', 'StopFailure', { error: 'server_error', error_details: SECRET }));
    // The turn ends but two background agents will wake it again: still working, not finished.
    await b.hook(payload(sid('c'), '/w/p-c', 'Stop', { background_tasks: [{ type: 'subagent', status: 'running', description: SECRET }, { type: 'subagent', status: 'pending' }] }));
    await b.hook(payload(sid('d'), '/w/p-d', 'PreCompact', { trigger: 'auto', custom_instructions: SECRET }));
    await b.hook(payload(sid('e'), '/w/p-e', 'SessionEnd', { reason: 'logout' }));
    await b.statusline({ session_id: sid('a'), model: { display_name: 'Sonnet' }, cost: { total_cost_usd: 0.5 },
      context_window: { used_percentage: 21, total_input_tokens: 40000, total_output_tokens: 2000 },
      rate_limits: { five_hour: { used_percentage: 30, resets_at: S5 + 7200 }, seven_day: { used_percentage: 10, resets_at: S5 + 300000 } } });
    await sleep(220);
    await b.settle();

    const snap = dev.state.snapshots.at(-1);
    const st = Object.fromEntries(snap.sessions.map((s) => [s.id, [s.st, s.tool, s.det]]));
    assert.deepEqual(st, {
      aaaaaaaa: ['done', '', ''],
      bbbbbbbb: ['done', '', ''],
      cccccccc: ['running', '_wait_agents', '2'],
      dddddddd: ['running', '_compact', ''],
    });
    assert.deepEqual(snap.alerts.map((a) => [a.kind, a.sid]), [['done', 'aaaaaaaa'], ['done', 'bbbbbbbb']]);
    assert.equal(snap.today.turns, 2);
    assert.ok(snap.today.work > 0);
    assert.equal(snap.usage.h5.pct, 30);
    assert.deepEqual(snap.tz, [{ z: 'America/Sao_Paulo', off: -180, next: 0, noff: -180 }]);
    assert.ok(!JSON.stringify(dev.state.snapshots).includes(SECRET));
    fixture('e2e-finished', snap);
  } finally {
    await b.stop();
    await dev.close();
  }
});

// ---- 4. Live time zones: what the gadget reports in /api/info, sent back with live offsets ----

test('fixed clock: the zones the gadget reports get live offsets, and a change of zone follows within the hour', async () => {
  const dev = await startFakeDevice();
  const b = await fixedBridge(dev, { tz: 'Australia/Lord_Howe', tz2: 'Asia/Kolkata' });
  try {
    await b.bridge.push();
    // Lord Howe: +11:00 in summer, half an hour back on 2027-04-04 at 02:00 local (15:00 UTC the day before).
    assert.deepEqual(dev.state.snapshots.at(-1).tz, [
      { z: 'Australia/Lord_Howe', off: 660, next: Date.UTC(2027, 3, 3, 15) / 1000, noff: 630 },
      { z: 'Asia/Kolkata', off: 330, next: 0, noff: 330 },
    ]);
    // The owner picks other zones on the settings page; the bridge reads /api/info again hourly.
    dev.state.config.tz = 'America/New_York';
    dev.state.config.tz2 = '';
    b.clock.advance(10 * 60_000);
    await b.bridge.push();
    assert.equal(dev.state.snapshots.at(-1).tz[0].z, 'Australia/Lord_Howe');
    b.clock.advance(3600_000);
    await b.bridge.push();
    assert.deepEqual(dev.state.snapshots.at(-1).tz, [NEW_YORK]);
    // Past New York's change: the new offset now, the next change in March.
    b.clock.set(Date.UTC(2026, 10, 2, 12));
    await b.bridge.push();
    assert.deepEqual(dev.state.snapshots.at(-1).tz, [{ z: 'America/New_York', off: -300, next: Date.UTC(2027, 2, 14, 7) / 1000, noff: -240 }]);
    // A name no tz database knows (or a hostile one) is left out; the gadget uses its own table.
    dev.state.config.tz = '../../etc/passwd';
    b.clock.advance(2 * 3600_000);
    await b.bridge.push();
    assert.equal(dev.state.snapshots.at(-1).tz, undefined);
  } finally {
    await b.stop();
    await dev.close();
  }
});

// ---- 5. Trouble on the way: unpaired (401), busy (503), moved (new address) ----

test('fixed clock: a gadget that dropped the pairing shows unauthorized until /miblo:pair, then gets the snapshots again', async () => {
  const dev = await startFakeDevice();
  const b = await fixedBridge(dev);
  const status = async () => JSON.parse((await run(['status'], { dataDir: b.dataDir, settingsPath: path.join(b.dataDir, 'none.json'),
    fetchStatus: async () => (await fetch(`http://127.0.0.1:${b.bridge.server.address().port}/status`, { headers: { 'x-miblo-key': b.bridge.key } })).json() })).out);
  try {
    await b.hook(payload('aaaaaaaa-0001', '/w/api', 'UserPromptSubmit', { prompt: 'x' }));
    assert.equal(dev.state.snapshots.length, 1);
    assert.deepEqual((await status()).devices.map((d) => [d.online, d.unauthorized]), [[true, false]]);

    dev.state.tokens = [];  // reset on the gadget
    await b.hook(payload('aaaaaaaa-0001', '/w/api', 'Stop', {}));
    assert.equal(dev.state.snapshots.length, 1);
    assert.deepEqual((await status()).devices.map((d) => [d.online, d.unauthorized]), [[false, true]]);

    const r = await run(['pair', dev.addr, '4827'], { dataDir: b.dataDir, client: new DeviceClient(), hostname: 'test-host' });
    assert.equal(r.code, 0, r.out);
    b.clock.advance(60_000);  // past the backoff
    await b.bridge.push();
    assert.equal(dev.state.snapshots.at(-1).sessions[0].st, 'done');
    assert.deepEqual((await status()).devices.map((d) => [d.online, d.unauthorized]), [[true, false]]);
  } finally {
    await b.stop();
    await dev.close();
  }
});

test('fixed clock: a busy gadget (503) still gets the alert; with nothing to alert it retries later', async () => {
  const dev = await startFakeDevice();
  const b = await fixedBridge(dev);
  try {
    dev.state.busyPath = '/api/state';
    dev.state.busyLeft = 1;
    await b.hook(payload('aaaaaaaa-0001', '/w/api', 'PermissionRequest', { tool_name: 'Bash', tool_input: { command: 'rm -rf build' } }));
    assert.equal(dev.state.busyHits, 1);
    const snap = dev.state.snapshots.at(-1);
    assert.deepEqual(snap.alerts.map((a) => a.kind), ['perm']);
    assert.deepEqual(snap.sessions, [{ id: 'aaaaaaaa', name: 'api', st: 'perm', tool: 'Bash', det: 'rm -rf build', since: S5 }]);
    assert.equal(snap.usage, null);

    dev.state.busyLeft = 1;
    b.clock.advance(60_000);  // the alert expired: nothing small to send instead
    const n = dev.state.snapshots.length;
    await b.bridge.push();
    assert.equal(dev.state.snapshots.length, n);
    b.clock.advance(1000);
    await b.bridge.push();
    assert.equal(dev.state.snapshots.length, n + 1);
    assert.equal(dev.state.snapshots.at(-1).sessions[0].since, S5);
  } finally {
    await b.stop();
    await dev.close();
  }
});

test('fixed clock: a gadget that moved to another address is found again (mDNS) and keeps getting snapshots', async () => {
  const old = await startFakeDevice();
  let moved = null;
  // The fakes listen on 127.0.0.1 with random ports; a real gadget must be on the LAN on port 80.
  const b = await fixedBridge(old, { addrOk: () => true, discoverFn: async () => (moved ? [{ id: 'miblo-4f2a', name: 'Miblo-4F2A', addr: moved.addr }] : []) });
  try {
    await b.bridge.push();
    assert.equal(old.state.snapshots.length, 1);
    await old.close();
    moved = await startFakeDevice({ tokens: ['tok-0'] });  // same gadget, new DHCP lease
    for (let i = 0; i < 3; i++) {
      b.clock.advance(5000);
      await b.bridge.push();
    }
    assert.equal(b.store.list()[0].addr, moved.addr);
    await b.bridge.push();
    assert.equal(moved.state.snapshots.length, 1);
    assert.equal(b.bridge.devices.status()[0].online, true);
  } finally {
    await b.stop();
    await moved?.close();
  }
});
