// Property/fuzz tests (validation harness, firmware/tools/fuzz/README.md): random sequences of
// Claude Code hook events and status line readings, garbage included, through the same path as
// the bridge (hook-client's pickEvent, SessionTracker, MetricsStore, DayStats, buildSnapshot), and
// random raw HTTP requests at the bridge server. Deterministic (fixed seeds). The sizes are small
// for `npm test`; `make validate` (firmware/) runs them with MIBLO_FUZZ_SCALE=20.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import net from 'node:net';
import { SessionTracker } from '../lib/session-tracker.js';
import { MetricsStore } from '../lib/metrics-store.js';
import { DayStats } from '../lib/day-stats.js';
import { buildSnapshot, alertOnlySnapshot, trimSnapshot } from '../lib/snapshot-builder.js';
import { pickEvent } from '../lib/hook-client.js';
import { createBridgeServer } from '../lib/bridge-server.js';
import { authHeader } from '../lib/bridge-auth.js';
import { MAX_SESSIONS, SNAPSHOT_MAX_BYTES, NAME_LEN, DET_LEN, MODEL_LEN, SESSION_TTL_MS } from '../lib/constants.js';

const SCALE = Math.max(1, Number(process.env.MIBLO_FUZZ_SCALE) || 1);

// mulberry32: small, fast, reproducible.
function rng(seed) {
  let a = seed >>> 0;
  const next = () => {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
  const int = (n) => Math.floor(next() * n);
  const pick = (arr) => arr[int(arr.length)];
  const chance = (p) => next() < p;
  return { next, int, pick, chance };
}

// Every event and notification type in session-tracker.js's decision table, plus ignored ones.
const EVENTS = [
  'SessionStart', 'SessionEnd', 'UserPromptSubmit', 'PreToolUse', 'PostToolUse', 'PostToolUseFailure',
  'PermissionRequest', 'PermissionDenied', 'Notification', 'Stop', 'StopFailure', 'SubagentStart',
  'SubagentStop', 'PreCompact', 'PostCompact', 'Elicitation', 'ElicitationResult', 'PostToolBatch',
  'UserPromptExpansion', 'TeammateIdle', 'TaskCreated', 'TaskCompleted', 'ConfigChange', 'CwdChanged',
  'MessageDisplay', 'Setup', 'Nonsense', '',
];
const NOTES = [
  'permission_prompt', 'worker_permission_prompt', 'elicitation_dialog', 'elicitation_url_dialog',
  'agent_needs_input', 'idle_prompt', 'auth_success', 'agent_completed', 'push_notification',
  'computer_use_enter', 'quota_auto_resume_start', 'model_refusal_fallback', 'bogus',
  'permission_denied', 'permission_granted', 'worker_permission_resolved', 'elicitation_complete', 'elicitation_response', 'input_received', 'dialog_closed',
];
const TOOLS = ['Bash', 'Edit', 'Write', 'Read', 'NotebookEdit', 'Grep', 'Glob', 'WebFetch', 'WebSearch', 'Agent',
  'Task', 'AskUserQuestion', 'mcp__srv__do_it', 'mcp__', 'Monitor', '_compact', '_wait_agents', ''];
const TERMINAL = new Set(['Stop', 'StopFailure', 'UserPromptSubmit']);
// Planted in every field that is not shown on purpose: none of it may reach a snapshot.
const CANARY = 'CANARY-7f3a-secret';
const STATES = new Set(['idle', 'running', 'perm', 'question', 'done']);

function weird(r) {
  return r.pick([
    null, undefined, 0, -1, 1e308, NaN, Infinity, true, [], {}, [1, 2], { a: { b: 1 } }, '', ' ',
    'x'.repeat(r.int(5000)), '\u0000', '\ud800', '\u202e', 'é'.repeat(300), '项目'.repeat(100), '😀'.repeat(50),
    '-', '__proto__', 'constructor', 'toString', '../../etc/passwd', '\n\n\n', '\u0085',
  ]);
}

function string(r, pool) {
  if (r.chance(0.08)) return weird(r);
  return r.pick(pool);
}

function makeEvent(r, ids, agents) {
  const name = r.chance(0.03) ? weird(r) : r.pick(EVENTS);
  const evt = {
    session_id: r.chance(0.04) ? weird(r) : r.pick(ids),
    hook_event_name: name,
    cwd: r.chance(0.1) ? weird(r) : r.pick(['/w/api', '/w/web', '/', 'C:\\proj\\x', '/w/' + 'n'.repeat(r.int(80))]),
    // Fields that never go to the gadget.
    prompt: CANARY, message: `Claude needs your permission to use ${r.pick(TOOLS)} ${CANARY}`, title: CANARY,
    transcript_path: `/home/${CANARY}.jsonl`, last_assistant_message: CANARY, error: CANARY,
    tool_response: { output: CANARY, stdout: CANARY }, custom_instructions: CANARY,
  };
  if (r.chance(0.5)) evt.agent_id = r.chance(0.1) ? weird(r) : r.pick(agents);
  if (r.chance(0.3)) evt.agent_type = r.pick(['', 'general-purpose', 'Explore', weird(r)]);
  if (name === 'Notification' || r.chance(0.05)) evt.notification_type = r.chance(0.05) ? weird(r) : r.pick(NOTES);
  if (r.chance(0.7)) evt.tool_name = string(r, TOOLS);
  if (r.chance(0.7)) {
    evt.tool_input = r.chance(0.05) ? weird(r) : {
      command: r.chance(0.5) ? `npm test\n${CANARY}` : weird(r),
      file_path: r.chance(0.5) ? '/w/src/app.js' : weird(r), pattern: 'TODO', url: r.pick(['https://x.dev/a', 'nope', weird(r)]),
      query: 'how to', description: r.chance(0.5) ? 'Research the thing' : weird(r),
      content: CANARY, old_string: CANARY, new_string: CANARY, secret: CANARY,
    };
  }
  if (r.chance(0.15)) evt.source = r.pick(['startup', 'resume', 'compact', weird(r)]);
  if (r.chance(0.15)) evt.trigger = r.pick(['manual', 'auto', weird(r)]);
  if (r.chance(0.3)) {
    evt.background_tasks = r.chance(0.1) ? weird(r) : Array.from({ length: r.int(r.chance(0.05) ? 300 : 5) }, () => ({
      type: r.pick(['subagent', 'bash', 'monitor', weird(r)]), status: r.pick(['running', 'completed', 'failed', weird(r)]),
      description: CANARY,
    }));
  }
  if (r.chance(0.2)) evt.pid = r.pick([null, 0, 123, -5, 'abc', 1e20]);
  return evt;
}

function makeStatusline(r, ids) {
  const sl = {
    session_id: r.chance(0.05) ? weird(r) : r.pick(ids),
    model: r.chance(0.1) ? weird(r) : { display_name: r.chance(0.8) ? r.pick(['Opus 4.5', 'Sonnet', 'x'.repeat(200)]) : weird(r) },
    context_window: r.chance(0.1) ? weird(r) : {
      used_percentage: r.pick([12, 99.6, 0, -3, 1e9, 1e300, NaN, '50', null]),
      total_input_tokens: r.pick([1000, 1e15, -5, 'x', null]), total_output_tokens: r.pick([10, 1e300, null]),
    },
    cost: r.chance(0.1) ? weird(r) : { total_cost_usd: r.pick([0.5, 1.25, 1e9, -1, 1e300, null]) },
    transcript_path: CANARY, workspace: { current_dir: CANARY },
  };
  if (r.chance(0.5)) {
    sl.rate_limits = r.chance(0.1) ? weird(r) : {
      five_hour: { used_percentage: r.pick([40, 100, 250, -1, 1e300]), resets_at: r.pick([null, 1_790_003_600, 'soon', -1, 1e20, { x: 1 }, 'x'.repeat(r.int(9000))]) },
      seven_day: { used_percentage: r.pick([12, 99.9]), resets_at: r.pick([1_790_400_000, null]) },
    };
  }
  return sl;
}

const chars = (s) => Array.from(String(s)).length;

// What the firmware's parser expects (miblo_snapshot.cpp, kSnapshotMaxBytes, kMaxSessions).
function checkSnapshot(obj, ctx) {
  const json = JSON.stringify(obj);
  const snap = JSON.parse(json);  // what goes over the wire (NaN becomes null, undefined drops)
  assert.ok(Buffer.byteLength(json) <= SNAPSHOT_MAX_BYTES, `${ctx}: snapshot ${Buffer.byteLength(json)} B > ${SNAPSHOT_MAX_BYTES}`);
  assert.ok(!json.includes(CANARY), `${ctx}: private text in the snapshot: ${json.slice(json.indexOf(CANARY) - 80, json.indexOf(CANARY) + 40)}`);
  assert.ok(snap.sessions.length <= MAX_SESSIONS, `${ctx}: ${snap.sessions.length} sessions`);
  assert.ok(Number.isInteger(snap.more) && snap.more >= 0, `${ctx}: more ${snap.more}`);
  for (const s of snap.sessions) {
    assert.ok(typeof s.id === 'string' && s.id.length <= 8, `${ctx}: id ${s.id}`);
    assert.ok(chars(s.name) <= NAME_LEN && chars(s.tool) <= DET_LEN && chars(s.det) <= DET_LEN && chars(s.model) <= MODEL_LEN,
      `${ctx}: field too long ${JSON.stringify(s)}`);
    assert.ok(STATES.has(s.st), `${ctx}: state ${s.st}`);
    assert.ok(Number.isInteger(s.since) && s.since >= 0, `${ctx}: since ${s.since}`);
    assert.ok(s.ctx === null || (Number.isInteger(s.ctx) && s.ctx >= 0 && s.ctx <= 100), `${ctx}: ctx ${s.ctx}`);
    // Any finite number: the gadget turns a negative or past-int64 one into "unknown" itself.
    assert.ok(s.tok === null || Number.isFinite(s.tok), `${ctx}: tok ${s.tok}`);
  }
  for (const a of snap.alerts) {
    assert.ok(['perm', 'question', 'done'].includes(a.kind) && typeof a.sid === 'string' && a.sid.length <= 8, `${ctx}: alert ${JSON.stringify(a)}`);
  }
  assert.ok(snap.alerts.length <= 8, `${ctx}: ${snap.alerts.length} alerts (the gadget keeps 8)`);
  const alertOnly = alertOnlySnapshot(obj);
  if (alertOnly) assert.ok(Buffer.byteLength(JSON.stringify(alertOnly)) <= SNAPSHOT_MAX_BYTES, `${ctx}: alert-only snapshot too large`);
  const legacy = trimSnapshot(obj, 8, 3072);
  assert.ok(legacy.sessions.length <= 8 && Buffer.byteLength(JSON.stringify(legacy)) <= 3072, `${ctx}: legacy snapshot too large`);
}

function world(seed) {
  let t = 1_790_000_000_000;
  const clock = { now: () => t, advance: (ms) => { t += ms; } };
  const alive = new Set([123]);
  const tracker = new SessionTracker({ now: clock.now, isAlive: (pid) => alive.has(pid) });
  const metrics = new MetricsStore({ now: clock.now });
  const day = new DayStats({ now: clock.now });
  return { r: rng(seed), clock, tracker, metrics, day };
}

function snapshotOf(w, seq) {
  w.day.observe(w.tracker.sessions(), { usd: w.metrics.today().usd });
  return buildSnapshot({ seq, nowMs: w.clock.now(), host: 'MacBook-of-' + 'x'.repeat(seq % 40), tracker: w.tracker,
    metrics: w.metrics, day: w.day, latest: '1.10.1', eta: seq % 3 ? 1_790_009_999 : 0, week: seq % 5 ? null : w.day.week() });
}

test('random hook event sequences keep the tracker and the snapshot sane', (t) => {
  const runs = 40 * SCALE;
  let events = 0;
  for (let run = 0; run < runs; run++) {
    const w = world(1000 + run);
    const { r, tracker } = w;
    const ids = Array.from({ length: 1 + r.int(r.chance(0.2) ? 60 : 6) }, (_, i) => `sess-${run}-${i}-${'a'.repeat(r.int(40))}`);
    const agents = Array.from({ length: 1 + r.int(8) }, (_, i) => `agent-${i}`);
    const len = 50 + r.int(450);
    for (let i = 0; i < len; i++, events++) {
      const ctx = `seed ${1000 + run} event ${i}`;
      if (r.chance(0.12)) {
        const sl = makeStatusline(r, ids);
        assert.doesNotThrow(() => w.metrics.ingest(sl, { fresh: tracker.sawStart(sl?.session_id) }), ctx);
      } else {
        const raw = makeEvent(r, ids, agents);
        // Mostly what bin/hook.js forwards; sometimes the raw object (any local client may post).
        const evt = r.chance(0.8) ? pickEvent(raw) : raw;
        assert.doesNotThrow(() => tracker.handle(evt), ctx);
        const id = evt.session_id;
        const mainThread = !(typeof evt.agent_id === 'string' && evt.agent_id);
        if (id && typeof id === 'string') {
          const s = tracker.sessions().find((x) => x.id === id);
          if (evt.hook_event_name === 'SessionEnd') assert.equal(s, undefined, `${ctx}: session survives SessionEnd`);
          else if (TERMINAL.has(evt.hook_event_name) && mainThread && s) {
            assert.ok(s.st !== 'perm' && s.st !== 'question', `${ctx}: ${s.st} after a main-thread ${evt.hook_event_name}`);
          }
        }
      }
      if (r.chance(0.05)) w.clock.advance(r.pick([1, 1500, 60_000, 31 * 60_000, SESSION_TTL_MS + 1]));
      else w.clock.advance(r.int(3000));
      if (r.chance(0.05)) assert.doesNotThrow(() => tracker.sweep(), ctx);
      // One alert per session at most, and it is the session's current state.
      const sessions = tracker.sessions();
      const alerts = tracker.alerts();
      const sids = new Set();
      for (const a of alerts) {
        const s = sessions.find((x) => x.id === a.sid);
        assert.ok(s, `${ctx}: alert for a session that is gone`);
        assert.equal(a.kind, s.st, `${ctx}: alert ${a.kind} for a session in ${s.st}`);
        assert.ok(!sids.has(a.sid), `${ctx}: two alerts for one session`);
        sids.add(a.sid);
      }
      if (i % 7 === 0 || i === len - 1) {
        const snap = snapshotOf(w, i + 1);
        checkSnapshot(snap, ctx);
        assert.equal(snap.sessions.length + snap.more, sessions.length, `${ctx}: sessions + more != tracked`);
      }
    }
    // Everyone leaves: nothing is left behind.
    for (const s of tracker.sessions()) tracker.handle({ session_id: s.id, hook_event_name: 'SessionEnd' });
    assert.equal(tracker.hasActive(), false);
    assert.equal(tracker.alerts().length, 0);
  }
  assert.ok(events > 0);
  t.diagnostic(`${runs} sequences, ${events} events/readings`);
});

// Many sessions and agents over a long time: what the tracker keeps stays bounded by what is live.
test('long streams of new sessions and agents stay bounded', (t) => {
  const w = world(7);
  const { r, tracker, clock } = w;
  const n = 20_000 * SCALE;
  for (let i = 0; i < n; i++) {
    const id = `s${i % 5000}-${Math.floor(i / 5000)}`;
    tracker.handle({ session_id: id, hook_event_name: r.pick(['PreToolUse', 'Notification', 'Stop', 'PostToolUse']),
      agent_id: r.chance(0.5) ? `a${i}` : undefined, notification_type: 'permission_prompt', tool_name: 'Bash',
      tool_input: { command: 'ls' } });
    clock.advance(15_000);  // 12 h of session TTL = 2880 events
    if (i % 100 === 0) tracker.sweep();
    if (i % 997 === 0) {
      assert.ok(tracker.sessions().length <= 2880 + 100, `${tracker.sessions().length} sessions kept`);
      checkSnapshot(snapshotOf(w, i), `long stream ${i}`);
    }
  }
  t.diagnostic(`${n} events`);
});

test('alerting sessions never push the snapshot over the gadget limit', () => {
  const w = world(11);
  const { tracker } = w;
  for (let i = 0; i < 400 * SCALE; i++) {
    tracker.handle({ session_id: `session-with-a-long-id-${i}`, hook_event_name: 'PermissionRequest', cwd: `/w/p${i}`,
      tool_name: 'Bash', tool_input: { command: 'rm -rf build' } });
  }
  checkSnapshot(snapshotOf(w, 1), 'many alerts');
});

// ---- bridge HTTP input ----

function rawRequest(port, text, { timeoutMs = 2000 } = {}) {
  return new Promise((resolve) => {
    const sock = net.connect(port, '127.0.0.1');
    let out = '';
    const done = () => resolve(out);
    sock.setTimeout(timeoutMs, () => { sock.destroy(); done(); });
    sock.on('data', (d) => { out += d.toString('latin1'); });
    sock.on('error', done);
    sock.on('close', done);
    sock.on('connect', () => {
      sock.write(text, 'latin1', () => {});
      sock.end();
    });
  });
}

const KEY = 'ab'.repeat(32);
test('bridge server: random requests never crash it and the guard always holds', async (t) => {
  const seen = [];
  const server = createBridgeServer({
    onEvent: (e) => seen.push(['event', e]),
    onStatusline: (e) => seen.push(['statusline', e]),
    getStatus: async () => ({ ok: true }),
    version: 'test',
    key: KEY,
    challengesPerSecond: 10_000,  // one /health per fuzzed request
  });
  await new Promise((res) => server.listen(0, '127.0.0.1', res));
  const port = server.address().port;
  const r = rng(4242);
  const goodHosts = [`127.0.0.1:${port}`, `localhost:${port}`];
  const hosts = [...goodHosts, `127.0.0.1`, `evil.example:${port}`, `127.0.0.1:${port}.evil.example`, `LOCALHOST:${port}`,
    `127.0.0.1:${port + 1}`, `[::1]:${port}`, '', null];
  const types = ['application/json', 'application/json; charset=utf-8', 'application/jsonx', 'text/plain',
    'application/x-www-form-urlencoded', 'multipart/form-data; boundary=x', '', null];
  const bodies = () => r.pick([
    '{"session_id":"s1","hook_event_name":"Stop"}', '{', '}', 'null', '[]', '1', '"x"', '{"a":', '\u0000',
    '['.repeat(r.int(100000)), '{"x":"' + 'y'.repeat(r.int(300_000)) + '"}', 'é', '\xff\xfe',
    JSON.stringify({ session_id: 's2', hook_event_name: 'PreToolUse', tool_name: 'Bash', tool_input: { command: 'x' } }),
  ]);
  try {
    const requests = 120 * SCALE;
    for (let i = 0; i < requests; i++) {
      // A valid request (POST /event, a loopback Host, JSON, no Origin) with 0..3 parts changed.
      let method = 'POST', path = r.pick(['/event', '/statusline']), host = r.pick(goodHosts), type = 'application/json';
      let origin = null;
      // Signed over a fresh challenge, as the plugin's clients do (bridge-auth.js).
      const h = await fetch(`http://127.0.0.1:${port}/health`, { headers: { 'x-miblo-nonce': '0'.repeat(32) } });
      const challenge = h.headers.get('x-miblo-challenge');
      await h.text();
      let auth = 'sign';
      let body = r.pick(['{"session_id":"s1","hook_event_name":"Stop"}',
        JSON.stringify({ session_id: 's2', hook_event_name: 'PreToolUse', tool_name: 'Bash', tool_input: { command: 'x' } })]);
      for (let k = r.int(4); k > 0; k--) {
        switch (r.int(6)) {
          case 0: method = r.pick(['POST', 'GET', 'PUT', 'OPTIONS', 'DELETE']); break;
          case 1: path = r.pick(['/event', '/statusline', '/health', '/status', '/shutdown-not', '/', '/event?x=1', '//event', '/%2e%2e']); break;
          case 2: host = r.pick(hosts); break;
          case 3: type = r.pick(types); origin = r.chance(0.5) ? r.pick(['null', 'https://evil.example', `http://127.0.0.1:${port}`]) : origin; break;
          case 4: body = bodies(); break;
          case 5: auth = r.pick([null, '', KEY, `${challenge}:${'0'.repeat(64)}`, 'wrong-key', 'f'.repeat(32) + ':' + 'a'.repeat(64)]); break;
        }
      }
      const chunked = r.chance(0.15);
      const lines = [`${method} ${path} HTTP/1.1`];
      if (host !== null) lines.push(`Host: ${host}`);
      if (r.chance(0.05)) lines.push(`Host: evil.example`);  // a second Host header
      if (type !== null) lines.push(`Content-Type: ${type}`);
      if (origin !== null) lines.push(`Origin: ${origin}`);
      const bytes = Buffer.from(body, 'utf8');
      const authValue = auth === 'sign' ? authHeader(KEY, challenge, method, path, bytes)
        : auth === 'wrong-key' ? authHeader('cd'.repeat(32), challenge, method, path, bytes) : auth;
      if (authValue !== null) lines.push(`X-Miblo-Auth: ${authValue}`);
      if (chunked) lines.push('Transfer-Encoding: chunked');
      else lines.push(`Content-Length: ${r.chance(0.1) ? bytes.length + r.int(20) - 10 : bytes.length}`);
      lines.push('Connection: close');
      const payload = chunked
        ? `${lines.join('\r\n')}\r\n\r\n${bytes.length.toString(16)}\r\n${bytes.toString('latin1')}\r\n0\r\n\r\n`
        : `${lines.join('\r\n')}\r\n\r\n${bytes.toString('latin1')}`;
      const before = seen.length;
      const reply = await rawRequest(port, payload);
      const status = Number((reply.match(/^HTTP\/1\.1 (\d{3})/) || [])[1] || 0);
      const ctx = `request ${i}: ${method} ${path} host=${host} type=${type} origin=${origin} auth=${auth}`;
      const hostOk = goodHosts.includes(host);
      // Node itself answers 400 to a request it cannot parse (no Host, a bad length), else the guard 403s.
      if (status && !hostOk) assert.ok(status === 403 || status === 400, `${ctx}: status ${status}`);
      if (!hostOk || origin !== null) assert.equal(seen.length, before, `${ctx}: a refused request reached the handler`);
      if (method === 'POST' && !String(type ?? '').startsWith('application/json')) assert.equal(seen.length, before, `${ctx}: non-JSON body accepted`);
      if (auth !== 'sign') assert.equal(seen.length, before, `${ctx}: a request not signed over a challenge reached the handler`);
      if (seen.length > before) {
        const [, e] = seen.at(-1);
        assert.ok(e !== undefined, ctx);
      }
      // Still alive.
      if (i % 10 === 0) {
        const health = await rawRequest(port, `GET /health HTTP/1.1\r\nHost: 127.0.0.1:${port}\r\nConnection: close\r\n\r\n`);
        assert.match(health, /^HTTP\/1\.1 200/, `${ctx}: bridge stopped answering`);
      }
    }
    assert.ok(seen.length > 0, 'no request ever got through: the fuzzer is not exercising the handlers');
    t.diagnostic(`${requests} requests, ${seen.length} reached a handler`);
    // Whatever reached the handlers goes through the tracker without throwing.
    const tracker = new SessionTracker({ isAlive: () => true });
    for (const [kind, e] of seen) if (kind === 'event') assert.doesNotThrow(() => tracker.handle(e));
  } finally {
    await new Promise((res) => server.close(res));
  }
});
