# Plano 1 — Bridge + plugin do Claude Code (Miblo) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Um plugin do Claude Code (`miblo`) que acompanha o estado de todas as sessões locais, coleta limites/métricas oficiais pela status line e envia snapshots JSON para gadgets Miblo pareados na rede local — totalmente testável sem hardware (com um gadget falso).

**Architecture:** Hooks assíncronos chamam `bin/hook.js`, que repassa cada evento para um serviço local (`bin/bridge.js`, HTTP em `127.0.0.1:47821`), subindo-o se necessário. Um tap de status line (`statusline-tap.mjs`), encadeado com consentimento no `~/.claude/settings.json`, repassa o JSON oficial da status line para o mesmo serviço e executa a status line original do usuário. O bridge mantém `SessionTracker` + `MetricsStore`, monta o snapshot (`buildSnapshot`) e o envia para cada gadget via `DeviceManager`. O comando `/miblo` usa `bin/miblo.js` para descobrir (mDNS), parear e configurar gadgets.

**Tech Stack:** Node.js ≥ 18 (ESM, `node:test`, `fetch`, `dgram`), zero dependências npm. Plugin do Claude Code (hooks, commands, marketplace).

**Spec:** `docs/superpowers/specs/2026-09-28-claude-gadget-design.md`

## Global Constraints

- Zero dependências npm; apenas módulos `node:*`. Node ≥ 18.
- ESM em todo o plugin (`"type": "module"` em `plugin/package.json`). O tap é copiado para fora do plugin, então usa extensão `.mjs` e **não importa nada de `lib/`**.
- `hook.js` e `statusline-tap.mjs` **nunca** podem quebrar ou atrasar o Claude Code: capturam todos os erros, `hook.js` sempre sai com código 0 e sem stdout; o tap devolve exatamente a saída e o código da status line original.
- O bridge escuta **somente** em `127.0.0.1`. Porta padrão `47821`, sobrescrita por `MIBLO_PORT`.
- Estado persistente em `${CLAUDE_PLUGIN_DATA}` (nunca em `${CLAUDE_PLUGIN_ROOT}`, que muda a cada atualização).
- Tokens de pareamento nunca aparecem em stdout/logs.
- Protocolo bridge → gadget (spec §5.3): `v:1`; ≤ 8 sessões (excedente em `more`); `name` ≤ 20 caracteres, `tool`/`det` ≤ 32, `model` ≤ 12; corpo ≤ 3072 bytes (se exceder, sessões do fim vão para `more`).
- Estados de sessão: `idle`, `running`, `perm`, `question`, `done`. Prioridade de ordenação: `perm` < `question` < `done` < `running` < `idle`, empate por `since` crescente.
- Identificadores e mensagens de código em inglês; o `/miblo` responde no idioma do usuário (instrução no comando).

## File Structure

```
.claude-plugin/marketplace.json        marketplace do repositório (aponta para ./plugin)
fixtures/snapshots/*.json              contrato bridge ↔ firmware (gerado pelos testes do bridge)
plugin/
  .claude-plugin/plugin.json           manifesto do plugin
  package.json                         "type": "module", script de teste
  hooks/hooks.json                     registro dos hooks
  commands/miblo.md                    comando /miblo
  bin/hook.js                          entrada dos hooks → bridge
  bin/onboard.js                       SessionStart síncrono: lembrete de pareamento
  bin/bridge.js                        serviço local (entrypoint + createBridge)
  bin/miblo.js                         CLI usado pelo /miblo
  bin/statusline-tap.mjs               tap autossuficiente da status line
  lib/constants.js                     porta, limites, tempos
  lib/describe-tool.js                 ferramenta + detalhe curto
  lib/session-tracker.js               máquina de estados das sessões + alertas
  lib/metrics-store.js                 métricas/limites a partir da status line
  lib/snapshot-builder.js              monta o JSON do protocolo
  lib/device-client.js                 HTTP para o gadget
  lib/device-store.js                  pareamentos em devices.json
  lib/device-manager.js                envio, backoff, relocalização
  lib/mdns.js                          consulta DNS-SD mínima
  lib/bridge-server.js                 HTTP local (/event, /statusline, /status, /health)
  lib/proc.js                          PID do processo do Claude Code
  lib/statusline-link.js               encadear/desencadear a status line
  test/*.test.js                       testes (node:test)
  test/fakes/fake-device.js            gadget falso (HTTP) para testes
```

---

### Task 1: Scaffold do plugin, constantes e `describeTool`

**Files:**
- Create: `plugin/package.json`
- Create: `plugin/lib/constants.js`
- Create: `plugin/lib/describe-tool.js`
- Test: `plugin/test/describe-tool.test.js`

**Interfaces:**
- Produces: `constants.js` exports `PORT, HOST, PROTOCOL_VERSION, MAX_SESSIONS, NAME_LEN, DET_LEN, MODEL_LEN, SNAPSHOT_MAX_BYTES, ALERT_TTL_MS, DEBOUNCE_MS, HEARTBEAT_MS, PID_CHECK_MS, IDLE_EXIT_MS, MDNS_SERVICE, claudeSettingsPath()`; `describeTool(toolName: string, toolInput?: object) → { tool: string, det: string }`.

- [ ] **Step 1: `plugin/package.json`**

```json
{
  "name": "miblo-plugin",
  "private": true,
  "type": "module",
  "engines": { "node": ">=18" },
  "scripts": { "test": "node --test" }
}
```

- [ ] **Step 2: `plugin/lib/constants.js`**

```js
import os from 'node:os';
import path from 'node:path';

export const PORT = Number(process.env.MIBLO_PORT || 47821);
export const HOST = '127.0.0.1';
export const PROTOCOL_VERSION = 1;
export const MAX_SESSIONS = 8;
export const NAME_LEN = 20;
export const DET_LEN = 32;
export const MODEL_LEN = 12;
export const SNAPSHOT_MAX_BYTES = 3072;
export const ALERT_TTL_MS = 30_000;
export const DEBOUNCE_MS = 150;
export const HEARTBEAT_MS = 10_000;
export const PID_CHECK_MS = 15_000;
export const IDLE_EXIT_MS = 30 * 60_000;
export const MDNS_SERVICE = '_miblo._tcp.local';

export function claudeSettingsPath() {
  const dir = process.env.CLAUDE_CONFIG_DIR || path.join(os.homedir(), '.claude');
  return path.join(dir, 'settings.json');
}
```

- [ ] **Step 3: Write the failing test** — `plugin/test/describe-tool.test.js`

```js
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { describeTool } from '../lib/describe-tool.js';

const cases = [
  ['Bash', { command: 'npm test\necho done' }, { tool: 'Bash', det: 'npm test' }],
  ['Edit', { file_path: '/repo/src/Header.tsx' }, { tool: 'Edit', det: 'Header.tsx' }],
  ['Write', { file_path: 'C:\\repo\\a.txt' }, { tool: 'Write', det: 'a.txt' }],
  ['Read', { file_path: '/x/y.md' }, { tool: 'Read', det: 'y.md' }],
  ['NotebookEdit', { notebook_path: '/n/b.ipynb' }, { tool: 'NotebookEdit', det: 'b.ipynb' }],
  ['Grep', { pattern: 'TODO' }, { tool: 'Grep', det: 'TODO' }],
  ['Glob', { pattern: '**/*.ts' }, { tool: 'Glob', det: '**/*.ts' }],
  ['WebFetch', { url: 'https://docs.example.com/a?b=1' }, { tool: 'WebFetch', det: 'docs.example.com' }],
  ['WebFetch', { url: 'not a url' }, { tool: 'WebFetch', det: '' }],
  ['WebSearch', { query: 'esp8266 st7789' }, { tool: 'WebSearch', det: 'esp8266 st7789' }],
  ['Agent', { description: 'Find usages' }, { tool: 'Agent', det: 'Find usages' }],
  ['Task', { description: 'Refactor' }, { tool: 'Task', det: 'Refactor' }],
  ['mcp__github__create_issue', { title: 'x' }, { tool: 'create_issue', det: '' }],
  ['SomethingNew', undefined, { tool: 'SomethingNew', det: '' }],
  [undefined, undefined, { tool: '', det: '' }],
];

for (const [name, input, expected] of cases) {
  test(`describeTool(${name})`, () => {
    assert.deepEqual(describeTool(name, input), expected);
  });
}
```

- [ ] **Step 4: Run test to verify it fails**

Run: `cd plugin && node --test test/describe-tool.test.js`
Expected: FAIL — `Cannot find module '.../lib/describe-tool.js'`

- [ ] **Step 5: Write minimal implementation** — `plugin/lib/describe-tool.js`

```js
const firstLine = (s) => String(s ?? '').split('\n')[0].trim();
const baseName = (p) => String(p ?? '').split(/[\\/]/).filter(Boolean).pop() ?? '';

export function describeTool(toolName, toolInput = {}) {
  const name = String(toolName ?? '');
  const input = toolInput ?? {};
  const tool = name.startsWith('mcp__') ? name.split('__').slice(2).join('__') || name : name;
  let det = '';
  switch (name) {
    case 'Bash':
      det = firstLine(input.command);
      break;
    case 'Edit':
    case 'Write':
    case 'Read':
    case 'NotebookEdit':
      det = baseName(input.file_path ?? input.notebook_path);
      break;
    case 'Grep':
    case 'Glob':
      det = firstLine(input.pattern);
      break;
    case 'WebFetch':
      try {
        det = new URL(input.url).host;
      } catch {
        det = '';
      }
      break;
    case 'WebSearch':
      det = firstLine(input.query);
      break;
    case 'Agent':
    case 'Task':
      det = firstLine(input.description);
      break;
  }
  return { tool, det };
}
```

- [ ] **Step 6: Run test to verify it passes**

Run: `cd plugin && node --test test/describe-tool.test.js`
Expected: PASS (15 tests)

- [ ] **Step 7: Commit**

```bash
git add plugin/package.json plugin/lib/constants.js plugin/lib/describe-tool.js plugin/test/describe-tool.test.js
git commit -m "feat(plugin): scaffold, constants and tool description"
```

---

### Task 2: `SessionTracker`

**Files:**
- Create: `plugin/lib/session-tracker.js`
- Test: `plugin/test/session-tracker.test.js`

**Interfaces:**
- Consumes: `describeTool` (Task 1), `ALERT_TTL_MS` (Task 1).
- Produces:
  - `pidAlive(pid: number|null) → boolean`
  - `class SessionTracker({ now?: () => ms, isAlive?: (pid) => boolean })`
    - `handle(evt: HookPayload & { pid?: number|null }) → boolean` (true se algo mudou)
    - `sweep() → boolean` (remove sessões com PID morto; true se removeu)
    - `sessions() → Array<{ id, name, st, since /*ms*/, tool, det, pid }>` ordenado por prioridade
    - `alerts() → Array<{ id: number, kind: 'perm'|'question'|'done', sid: string }>` (só os criados há < `ALERT_TTL_MS`)
    - `hasActive() → boolean`

- [ ] **Step 1: Write the failing test** — `plugin/test/session-tracker.test.js`

```js
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
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd plugin && node --test test/session-tracker.test.js`
Expected: FAIL — `Cannot find module '.../lib/session-tracker.js'`

- [ ] **Step 3: Write minimal implementation** — `plugin/lib/session-tracker.js`

```js
import { describeTool } from './describe-tool.js';
import { ALERT_TTL_MS } from './constants.js';

const PRIORITY = { perm: 0, question: 1, done: 2, running: 3, idle: 4 };
const ALERTING = new Set(['perm', 'question', 'done']);

export function pidAlive(pid) {
  if (!pid) return true;
  try {
    process.kill(pid, 0);
    return true;
  } catch (e) {
    return e.code === 'EPERM';
  }
}

const baseName = (cwd) => String(cwd ?? '').split(/[\\/]/).filter(Boolean).pop() || 'session';

export class SessionTracker {
  #sessions = new Map();
  #alerts = [];
  #nextAlertId = 1;

  constructor({ now = () => Date.now(), isAlive = pidAlive } = {}) {
    this.now = now;
    this.isAlive = isAlive;
  }

  handle(evt) {
    const id = evt?.session_id;
    if (!id) return false;
    const name = evt.hook_event_name;
    if (name === 'SessionEnd') return this.#remove(id);

    const created = !this.#sessions.has(id);
    const s = this.#ensure(id, evt.cwd);
    const before = JSON.stringify(s);
    if (evt.pid !== undefined && evt.pid !== null) s.pid = evt.pid;

    switch (name) {
      case 'SessionStart':
        break;
      case 'UserPromptSubmit':
        this.#enter(s, 'running');
        s.tool = '';
        s.det = '';
        break;
      case 'PreToolUse':
        if (evt.tool_name === 'AskUserQuestion') {
          this.#enter(s, 'question');
        } else {
          this.#enter(s, 'running');
          Object.assign(s, describeTool(evt.tool_name, evt.tool_input));
        }
        break;
      case 'PermissionRequest':
        Object.assign(s, describeTool(evt.tool_name, evt.tool_input));
        this.#enter(s, 'perm');
        break;
      case 'Notification':
        if (evt.notification_type !== 'elicitation_dialog') return created;
        this.#enter(s, 'question');
        break;
      case 'PostToolUse':
        this.#enter(s, 'running');
        break;
      case 'Stop':
        this.#enter(s, 'done');
        break;
      default:
        return created;
    }
    return created || JSON.stringify(s) !== before;
  }

  sweep() {
    let changed = false;
    for (const s of [...this.#sessions.values()]) {
      if (s.pid && !this.isAlive(s.pid)) changed = this.#remove(s.id) || changed;
    }
    return changed;
  }

  sessions() {
    return [...this.#sessions.values()]
      .map((s) => ({ ...s }))
      .sort((a, b) => PRIORITY[a.st] - PRIORITY[b.st] || a.since - b.since);
  }

  alerts() {
    const cutoff = this.now() - ALERT_TTL_MS;
    this.#alerts = this.#alerts.filter((a) => a.createdAt > cutoff);
    return this.#alerts.map(({ id, kind, sid }) => ({ id, kind, sid }));
  }

  hasActive() {
    return this.#sessions.size > 0;
  }

  #ensure(id, cwd) {
    let s = this.#sessions.get(id);
    if (!s) {
      const base = baseName(cwd);
      const taken = new Set([...this.#sessions.values()].map((x) => x.name));
      let name = base;
      for (let n = 2; taken.has(name); n++) name = `${base} ${n}`;
      s = { id, name, st: 'idle', since: this.now(), tool: '', det: '', pid: null };
      this.#sessions.set(id, s);
    }
    return s;
  }

  #enter(s, st) {
    if (s.st === st) return;
    s.st = st;
    s.since = this.now();
    this.#alerts = this.#alerts.filter((a) => a.sid !== s.id);
    if (ALERTING.has(st)) {
      this.#alerts.push({ id: this.#nextAlertId++, kind: st, sid: s.id, createdAt: this.now() });
    }
  }

  #remove(id) {
    this.#alerts = this.#alerts.filter((a) => a.sid !== id);
    return this.#sessions.delete(id);
  }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd plugin && node --test test/session-tracker.test.js`
Expected: PASS (all tests)

- [ ] **Step 5: Commit**

```bash
git add plugin/lib/session-tracker.js plugin/test/session-tracker.test.js
git commit -m "feat(plugin): session state machine with alert queue"
```

---

### Task 3: `MetricsStore`

**Files:**
- Create: `plugin/lib/metrics-store.js`
- Test: `plugin/test/metrics-store.test.js`

**Interfaces:**
- Produces: `class MetricsStore({ now?: () => ms })`
  - `ingest(statusline: object) → boolean` (false se não houver `session_id`)
  - `forSession(sid) → { model: string, ctx: number|null, tok: number } | undefined`
  - `usage() → { h5?: {pct, reset}, d7?: {pct, reset} } | null` (`reset` em segundos Unix; janelas vencidas são descartadas)
  - `today() → { tok: number, usd: number }`
  - `forget(sid)`; `hasReadings() → boolean`

- [ ] **Step 1: Write the failing test** — `plugin/test/metrics-store.test.js`

```js
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { MetricsStore } from '../lib/metrics-store.js';

const T0 = new Date(2026, 8, 28, 14, 0, 0).getTime(); // 28/09/2026 14:00 local
const sec = (ms) => Math.floor(ms / 1000);

function sl(sid, { inTok = 0, outTok = 0, usd = 0, ctx = 10, model = 'Opus', rl } = {}) {
  return {
    session_id: sid,
    model: { display_name: model },
    cost: { total_cost_usd: usd },
    context_window: { total_input_tokens: inTok, total_output_tokens: outTok, used_percentage: ctx },
    ...(rl ? { rate_limits: rl } : {}),
  };
}

function setup() {
  let t = T0;
  const store = new MetricsStore({ now: () => t });
  return { store, advance: (ms) => { t += ms; }, now: () => t };
}

test('ignores payloads without session_id', () => {
  const { store } = setup();
  assert.equal(store.ingest({}), false);
  assert.equal(store.hasReadings(), false);
});

test('per-session model, ctx and tokens', () => {
  const { store } = setup();
  store.ingest(sl('a', { inTok: 1000, outTok: 200, ctx: 41.6, model: 'Sonnet' }));
  assert.deepEqual(store.forSession('a'), { model: 'Sonnet', ctx: 42, tok: 1200 });
  assert.equal(store.forSession('zzz'), undefined);
});

test('null used_percentage becomes null ctx', () => {
  const { store } = setup();
  store.ingest({ session_id: 'a', context_window: { used_percentage: null } });
  assert.equal(store.forSession('a').ctx, null);
});

test('today sums positive deltas across sessions', () => {
  const { store } = setup();
  store.ingest(sl('a', { inTok: 1000, outTok: 100, usd: 0.5 }));
  store.ingest(sl('a', { inTok: 1500, outTok: 150, usd: 0.75 }));
  store.ingest(sl('b', { inTok: 10, outTok: 5, usd: 0.01 }));
  store.ingest(sl('a', { inTok: 1400, outTok: 150, usd: 0.75 })); // regressão ignorada
  assert.deepEqual(store.today(), { tok: 1665, usd: 0.76 });
});

test('today resets at local midnight but keeps per-session baselines', () => {
  const { store, advance } = setup();
  store.ingest(sl('a', { inTok: 1000 }));
  advance(11 * 3600_000); // 01:00 do dia seguinte
  assert.deepEqual(store.today(), { tok: 0, usd: 0 });
  store.ingest(sl('a', { inTok: 1300 }));
  assert.deepEqual(store.today(), { tok: 300, usd: 0 });
});

test('usage takes the latest rate_limits and keeps them across readings without it', () => {
  const { store, now } = setup();
  const reset5 = sec(now()) + 7200;
  const reset7 = sec(now()) + 86400;
  store.ingest(sl('a', { rl: { five_hour: { used_percentage: 61.7, resets_at: reset5 }, seven_day: { used_percentage: 38.2, resets_at: reset7 } } }));
  store.ingest(sl('b'));
  assert.deepEqual(store.usage(), { h5: { pct: 62, reset: reset5 }, d7: { pct: 38, reset: reset7 } });
});

test('each window may be absent independently', () => {
  const { store, now } = setup();
  store.ingest(sl('a', { rl: { seven_day: { used_percentage: 10, resets_at: sec(now()) + 100 } } }));
  assert.deepEqual(Object.keys(store.usage()), ['d7']);
});

test('expired windows are dropped; usage is null when nothing remains', () => {
  const { store, now, advance } = setup();
  store.ingest(sl('a', { rl: { five_hour: { used_percentage: 90, resets_at: sec(now()) + 60 } } }));
  advance(61_000);
  assert.equal(store.usage(), null);
});

test('forget removes a session', () => {
  const { store } = setup();
  store.ingest(sl('a'));
  store.forget('a');
  assert.equal(store.forSession('a'), undefined);
});
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd plugin && node --test test/metrics-store.test.js`
Expected: FAIL — `Cannot find module '.../lib/metrics-store.js'`

- [ ] **Step 3: Write minimal implementation** — `plugin/lib/metrics-store.js`

```js
const num = (v) => (typeof v === 'number' && Number.isFinite(v) ? v : 0);
const dayKey = (ms) => {
  const d = new Date(ms);
  return `${d.getFullYear()}-${d.getMonth() + 1}-${d.getDate()}`;
};
const WINDOWS = [
  ['five_hour', 'h5'],
  ['seven_day', 'd7'],
];

export class MetricsStore {
  #per = new Map();
  #limits = {};
  #day = null;
  #todayTok = 0;
  #todayUsd = 0;

  constructor({ now = () => Date.now() } = {}) {
    this.now = now;
  }

  ingest(sl) {
    const sid = sl?.session_id;
    if (!sid) return false;
    this.#rollDay();

    const cw = sl.context_window ?? {};
    const cur = {
      model: String(sl.model?.display_name ?? ''),
      ctx: typeof cw.used_percentage === 'number' ? Math.round(cw.used_percentage) : null,
      tokIn: num(cw.total_input_tokens),
      tokOut: num(cw.total_output_tokens),
      usd: num(sl.cost?.total_cost_usd),
    };
    const prev = this.#per.get(sid) ?? { tokIn: 0, tokOut: 0, usd: 0 };
    this.#todayTok += Math.max(0, cur.tokIn - prev.tokIn) + Math.max(0, cur.tokOut - prev.tokOut);
    this.#todayUsd += Math.max(0, cur.usd - prev.usd);
    this.#per.set(sid, cur);

    const rl = sl.rate_limits;
    if (rl) {
      for (const [src, dst] of WINDOWS) {
        const w = rl[src];
        if (w && typeof w.used_percentage === 'number') {
          this.#limits[dst] = { pct: Math.round(w.used_percentage), reset: w.resets_at ?? null };
        }
      }
    }
    return true;
  }

  forSession(sid) {
    const m = this.#per.get(sid);
    return m ? { model: m.model, ctx: m.ctx, tok: m.tokIn + m.tokOut } : undefined;
  }

  usage() {
    const nowSec = Math.floor(this.now() / 1000);
    const out = {};
    for (const [, key] of WINDOWS) {
      const w = this.#limits[key];
      if (w && (w.reset === null || w.reset > nowSec)) out[key] = { ...w };
      else delete this.#limits[key];
    }
    return Object.keys(out).length ? out : null;
  }

  today() {
    this.#rollDay();
    return { tok: this.#todayTok, usd: Math.round(this.#todayUsd * 100) / 100 };
  }

  forget(sid) {
    this.#per.delete(sid);
  }

  hasReadings() {
    return this.#per.size > 0 || Object.keys(this.#limits).length > 0;
  }

  #rollDay() {
    const key = dayKey(this.now());
    if (key !== this.#day) {
      this.#day = key;
      this.#todayTok = 0;
      this.#todayUsd = 0;
    }
  }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd plugin && node --test test/metrics-store.test.js`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add plugin/lib/metrics-store.js plugin/test/metrics-store.test.js
git commit -m "feat(plugin): metrics store fed by the official statusline JSON"
```

---

### Task 4: `buildSnapshot` + fixtures de contrato

**Files:**
- Create: `plugin/lib/snapshot-builder.js`
- Test: `plugin/test/snapshot-builder.test.js`
- Test: `plugin/test/contract.test.js`
- Create (gerados): `fixtures/snapshots/attention.json`, `fixtures/snapshots/working.json`, `fixtures/snapshots/idle.json`, `fixtures/snapshots/overflow.json`

**Interfaces:**
- Consumes: `SessionTracker`, `MetricsStore`, constantes.
- Produces: `buildSnapshot({ seq: number, nowMs: number, host: string, tracker: SessionTracker, metrics: MetricsStore }) → Snapshot` onde

```
Snapshot = { v: 1, seq, now /*s*/, host, usage: {h5?, d7?}|null, today: {tok, usd},
             sessions: [{ id /*8 chars*/, name, st, tool, det, since /*s*/, model, ctx|null, tok|null }],
             more: number, alerts: [{ id, kind, sid /*8 chars*/ }] }
```

- [ ] **Step 1: Write the failing unit test** — `plugin/test/snapshot-builder.test.js`

```js
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
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd plugin && node --test test/snapshot-builder.test.js`
Expected: FAIL — `Cannot find module '.../lib/snapshot-builder.js'`

- [ ] **Step 3: Write minimal implementation** — `plugin/lib/snapshot-builder.js`

```js
import { MAX_SESSIONS, NAME_LEN, DET_LEN, MODEL_LEN, PROTOCOL_VERSION, SNAPSHOT_MAX_BYTES } from './constants.js';

const cut = (s, n) => {
  const chars = Array.from(String(s ?? ''));
  return chars.length > n ? chars.slice(0, n - 1).join('') + '…' : chars.join('');
};
const shortId = (id) => String(id).replace(/-/g, '').slice(0, 8);
const toSec = (ms) => Math.floor(ms / 1000);

export function buildSnapshot({ seq, nowMs, host, tracker, metrics }) {
  const all = tracker.sessions();
  const rows = all.map((s) => {
    const m = metrics.forSession(s.id);
    return {
      id: shortId(s.id),
      name: cut(s.name, NAME_LEN),
      st: s.st,
      tool: cut(s.tool, DET_LEN),
      det: cut(s.det, DET_LEN),
      since: toSec(s.since),
      model: m ? cut(m.model, MODEL_LEN) : '',
      ctx: m?.ctx ?? null,
      tok: m?.tok ?? null,
    };
  });

  const snapshot = {
    v: PROTOCOL_VERSION,
    seq,
    now: toSec(nowMs),
    host: cut(host, NAME_LEN),
    usage: metrics.usage(),
    today: metrics.today(),
    sessions: rows.slice(0, MAX_SESSIONS),
    more: Math.max(0, rows.length - MAX_SESSIONS),
    alerts: tracker.alerts().map((a) => ({ id: a.id, kind: a.kind, sid: shortId(a.sid) })),
  };

  while (snapshot.sessions.length > 0 && Buffer.byteLength(JSON.stringify(snapshot)) > SNAPSHOT_MAX_BYTES) {
    snapshot.sessions.pop();
    snapshot.more += 1;
  }
  return snapshot;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd plugin && node --test test/snapshot-builder.test.js`
Expected: PASS

- [ ] **Step 5: Write the contract test** — `plugin/test/contract.test.js`

Os arquivos em `fixtures/snapshots/` são o contrato consumido pelos testes do firmware (Plano 2). Este teste os gera (`UPDATE_FIXTURES=1`) e, depois, garante que não mudam sem querer.

```js
import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { SessionTracker } from '../lib/session-tracker.js';
import { MetricsStore } from '../lib/metrics-store.js';
import { buildSnapshot } from '../lib/snapshot-builder.js';

const dir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../fixtures/snapshots');
const NOW = new Date(2026, 8, 28, 14, 32, 0).getTime();
const S = Math.floor(NOW / 1000);

function world() {
  let t = NOW - 10_000; // < ALERT_TTL_MS, para os alertas ainda constarem no snapshot
  const clock = { now: () => t, set: (ms) => { t = ms; } };
  const tracker = new SessionTracker({ now: clock.now, isAlive: () => true });
  const metrics = new MetricsStore({ now: clock.now });
  const ev = (sid, name, cwd, extra = {}) => tracker.handle({ session_id: sid, hook_event_name: name, cwd, ...extra });
  const sl = (sid, model, ctx, inTok, outTok, usd, rl) =>
    metrics.ingest({ session_id: sid, model: { display_name: model }, cost: { total_cost_usd: usd },
      context_window: { used_percentage: ctx, total_input_tokens: inTok, total_output_tokens: outTok }, ...(rl ? { rate_limits: rl } : {}) });
  const rl = { five_hour: { used_percentage: 62, resets_at: S + 7800 }, seven_day: { used_percentage: 38, resets_at: S + 240000 } };
  const finish = () => { clock.set(NOW); return buildSnapshot({ seq: 42, nowMs: NOW, host: 'MacBook-Marcus', tracker, metrics }); };
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
    const w = world();
    w.ev('33333333-c', 'PreToolUse', '/w/front-app', { tool_name: 'Edit', tool_input: { file_path: '/w/front-app/src/Header.tsx' } });
    w.ev('55555555-e', 'PreToolUse', '/w/worker', { tool_name: 'Bash', tool_input: { command: 'npm test' } });
    w.sl('33333333-c', 'Sonnet', 34, 90000, 8000, 0.4, w.rl);
    return w.finish();
  },
  idle() {
    const w = world();
    w.ev('66666666-f', 'Stop', '/w/docs');
    w.sl('66666666-f', 'Opus', 54, 170000, 12000, 1.2, w.rl);
    return w.finish();
  },
  overflow() {
    const w = world();
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
```

- [ ] **Step 6: Run to verify it fails (fixtures ainda não existem)**

Run: `cd plugin && node --test test/contract.test.js`
Expected: FAIL — `ENOENT: no such file or directory ... fixtures/snapshots/attention.json`

- [ ] **Step 7: Generate the fixtures and review them**

Run: `cd plugin && UPDATE_FIXTURES=1 node --test test/contract.test.js && cat ../fixtures/snapshots/attention.json`
Expected: PASS; `attention.json` tem `sessions[0].st == "perm"` (api-server), `sessions[1].st == "question"` (infra), `alerts` com `perm` e `question`, `usage.h5.pct == 62`.

- [ ] **Step 8: Run again without the flag**

Run: `cd plugin && node --test test/contract.test.js`
Expected: PASS (4 tests)

- [ ] **Step 9: Commit**

```bash
git add plugin/lib/snapshot-builder.js plugin/test/snapshot-builder.test.js plugin/test/contract.test.js fixtures/snapshots
git commit -m "feat(plugin): snapshot builder and bridge-firmware contract fixtures"
```

---

### Task 5: Gadget falso, `DeviceClient` e `DeviceStore`

**Files:**
- Create: `plugin/test/fakes/fake-device.js`
- Create: `plugin/lib/device-client.js`
- Create: `plugin/lib/device-store.js`
- Test: `plugin/test/device-client.test.js`
- Test: `plugin/test/device-store.test.js`

**Interfaces:**
- Produces:
  - API do gadget (contrato para o Plano 2): `GET /api/info → {id, name, fw, proto, paired}`; `POST /api/pair {code, host} → {token}` (403 se código errado); `POST /api/state` (Bearer) → `{ok:true}` (401 sem token válido); `POST /api/config` (Bearer) `{mode?, ...}` → `{ok:true}`; `POST /api/reset` (Bearer) → `{ok:true}`.
  - `startFakeDevice({ id?, name?, code? }) → Promise<{ addr, state: { token, snapshots, config, resets }, close() }>`
  - `class DeviceClient({ fetchImpl?, timeoutMs? })`: `info(addr)`, `pair(addr, code, host) → token`, `pushState(addr, token, snapshot)`, `setConfig(addr, token, cfg)`, `reset(addr, token)`. Erros HTTP lançam `Error` com `.status`.
  - `class DeviceStore(dataDir)`: `list() → Device[]`, `upsert(Device)`, `update(id, patch)`, `remove(id)`; `Device = { id, name, addr, token }`; arquivo `devices.json` com permissão `0600`.

- [ ] **Step 1: Fake device** — `plugin/test/fakes/fake-device.js`

```js
import http from 'node:http';
import crypto from 'node:crypto';

export function startFakeDevice({ id = 'miblo-4f2a', name = 'Miblo-4F2A', code = '4827' } = {}) {
  const state = { token: null, snapshots: [], config: {}, resets: 0 };
  const readBody = (req) =>
    new Promise((resolve) => {
      const chunks = [];
      req.on('data', (c) => chunks.push(c));
      req.on('end', () => {
        const text = Buffer.concat(chunks).toString('utf8');
        try { resolve(text ? JSON.parse(text) : {}); } catch { resolve(null); }
      });
    });
  const authed = (req) => state.token && req.headers.authorization === `Bearer ${state.token}`;

  const server = http.createServer(async (req, res) => {
    const send = (code, obj) => { res.writeHead(code, { 'content-type': 'application/json' }); res.end(JSON.stringify(obj)); };
    const body = req.method === 'POST' ? await readBody(req) : null;
    if (req.method === 'GET' && req.url === '/api/info') return send(200, { id, name, fw: '0.0.0-fake', proto: 1, paired: !!state.token });
    if (req.method === 'POST' && req.url === '/api/pair') {
      if (String(body?.code) !== code) return send(403, { error: 'bad code' });
      state.token = crypto.randomBytes(16).toString('hex');
      return send(200, { token: state.token });
    }
    if (!authed(req)) return send(401, { error: 'unauthorized' });
    if (req.method === 'POST' && req.url === '/api/state') { state.snapshots.push(body); return send(200, { ok: true }); }
    if (req.method === 'POST' && req.url === '/api/config') { Object.assign(state.config, body); return send(200, { ok: true }); }
    if (req.method === 'POST' && req.url === '/api/reset') { state.resets++; return send(200, { ok: true }); }
    send(404, { error: 'not found' });
  });

  return new Promise((resolve) =>
    server.listen(0, '127.0.0.1', () =>
      resolve({
        addr: `127.0.0.1:${server.address().port}`,
        state,
        close: () => new Promise((c) => server.close(c)),
      })));
}
```

- [ ] **Step 2: Write the failing tests**

`plugin/test/device-client.test.js`:

```js
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { startFakeDevice } from './fakes/fake-device.js';
import { DeviceClient } from '../lib/device-client.js';

test('info, pair, push, config and reset against the fake device', async () => {
  const dev = await startFakeDevice();
  const client = new DeviceClient();
  try {
    const info = await client.info(dev.addr);
    assert.equal(info.id, 'miblo-4f2a');
    assert.equal(info.paired, false);

    await assert.rejects(client.pair(dev.addr, '0000', 'host'), (e) => e.status === 403);
    const token = await client.pair(dev.addr, '4827', 'host');
    assert.equal(token, dev.state.token);

    await client.pushState(dev.addr, token, { v: 1, seq: 1 });
    assert.deepEqual(dev.state.snapshots, [{ v: 1, seq: 1 }]);

    await client.setConfig(dev.addr, token, { mode: 'limits' });
    assert.equal(dev.state.config.mode, 'limits');

    await client.reset(dev.addr, token);
    assert.equal(dev.state.resets, 1);

    await assert.rejects(client.pushState(dev.addr, 'wrong', {}), (e) => e.status === 401);
  } finally {
    await dev.close();
  }
});

test('unreachable device rejects quickly', async () => {
  const client = new DeviceClient({ timeoutMs: 300 });
  const t0 = Date.now();
  await assert.rejects(client.info('127.0.0.1:1'));
  assert.ok(Date.now() - t0 < 2000);
});
```

`plugin/test/device-store.test.js`:

```js
import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { DeviceStore } from '../lib/device-store.js';

const tmp = () => fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-store-'));

test('empty when the file does not exist or is corrupt', () => {
  const dir = tmp();
  const store = new DeviceStore(dir);
  assert.deepEqual(store.list(), []);
  fs.writeFileSync(path.join(dir, 'devices.json'), '{nope');
  assert.deepEqual(store.list(), []);
});

test('upsert replaces by id, update patches, remove deletes', () => {
  const store = new DeviceStore(tmp());
  store.upsert({ id: 'a', name: 'A', addr: '1.1.1.1:80', token: 't1' });
  store.upsert({ id: 'b', name: 'B', addr: '2.2.2.2:80', token: 't2' });
  store.upsert({ id: 'a', name: 'A', addr: '1.1.1.9:80', token: 't3' });
  store.update('b', { addr: '2.2.2.9:80' });
  assert.deepEqual(store.list().map((d) => [d.id, d.addr]).sort(), [['a', '1.1.1.9:80'], ['b', '2.2.2.9:80']]);
  store.remove('a');
  assert.deepEqual(store.list().map((d) => d.id), ['b']);
});

test('file is private to the user', { skip: process.platform === 'win32' }, () => {
  const dir = tmp();
  new DeviceStore(dir).upsert({ id: 'a', name: 'A', addr: 'x', token: 't' });
  const mode = fs.statSync(path.join(dir, 'devices.json')).mode & 0o777;
  assert.equal(mode, 0o600);
});
```

- [ ] **Step 3: Run tests to verify they fail**

Run: `cd plugin && node --test test/device-client.test.js test/device-store.test.js`
Expected: FAIL — modules not found

- [ ] **Step 4: Implement** — `plugin/lib/device-client.js`

```js
export class DeviceClient {
  constructor({ fetchImpl = globalThis.fetch, timeoutMs = 2500 } = {}) {
    this.fetch = fetchImpl;
    this.timeoutMs = timeoutMs;
  }

  async #req(addr, path, { method = 'GET', token, body } = {}) {
    const headers = {};
    if (body !== undefined) headers['content-type'] = 'application/json';
    if (token) headers.authorization = `Bearer ${token}`;
    const res = await this.fetch(`http://${addr}${path}`, {
      method,
      headers,
      body: body === undefined ? undefined : JSON.stringify(body),
      signal: AbortSignal.timeout(this.timeoutMs),
    });
    const text = await res.text();
    let data = null;
    try { data = text ? JSON.parse(text) : null; } catch { data = null; }
    if (!res.ok) {
      const err = new Error(`device ${path} -> HTTP ${res.status}`);
      err.status = res.status;
      throw err;
    }
    return data;
  }

  info(addr) {
    return this.#req(addr, '/api/info');
  }

  async pair(addr, code, host) {
    const data = await this.#req(addr, '/api/pair', { method: 'POST', body: { code: String(code), host } });
    if (!data?.token) throw new Error('device did not return a token');
    return data.token;
  }

  pushState(addr, token, snapshot) {
    return this.#req(addr, '/api/state', { method: 'POST', token, body: snapshot });
  }

  setConfig(addr, token, cfg) {
    return this.#req(addr, '/api/config', { method: 'POST', token, body: cfg });
  }

  reset(addr, token) {
    return this.#req(addr, '/api/reset', { method: 'POST', token, body: {} });
  }
}
```

- [ ] **Step 5: Implement** — `plugin/lib/device-store.js`

```js
import fs from 'node:fs';
import path from 'node:path';

export class DeviceStore {
  constructor(dataDir) {
    this.file = path.join(dataDir, 'devices.json');
  }

  list() {
    try {
      const data = JSON.parse(fs.readFileSync(this.file, 'utf8'));
      return Array.isArray(data.devices) ? data.devices : [];
    } catch {
      return [];
    }
  }

  upsert(device) {
    this.#write([...this.list().filter((d) => d.id !== device.id), device]);
  }

  update(id, patch) {
    this.#write(this.list().map((d) => (d.id === id ? { ...d, ...patch } : d)));
  }

  remove(id) {
    this.#write(this.list().filter((d) => d.id !== id));
  }

  #write(devices) {
    fs.mkdirSync(path.dirname(this.file), { recursive: true });
    const tmp = `${this.file}.tmp`;
    fs.writeFileSync(tmp, JSON.stringify({ devices }, null, 2), { mode: 0o600 });
    fs.renameSync(tmp, this.file);
    fs.chmodSync(this.file, 0o600);
  }
}
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `cd plugin && node --test test/device-client.test.js test/device-store.test.js`
Expected: PASS

- [ ] **Step 7: Commit**

```bash
git add plugin/test/fakes/fake-device.js plugin/lib/device-client.js plugin/lib/device-store.js plugin/test/device-client.test.js plugin/test/device-store.test.js
git commit -m "feat(plugin): device HTTP client, pairing store and fake device"
```

---

### Task 6: `DeviceManager` (envio, backoff, relocalização)

**Files:**
- Create: `plugin/lib/device-manager.js`
- Test: `plugin/test/device-manager.test.js`

**Interfaces:**
- Consumes: `DeviceClient`-like `{ pushState(addr, token, snapshot) }`, `DeviceStore`-like `{ list(), update(id, patch) }`, `discover() → Promise<Array<{id, name, addr}>>`.
- Produces: `class DeviceManager({ client, store, discover?, now? })`
  - `pushAll(snapshot) → Promise<void>`
  - `status() → Array<{ id, name, addr, online: boolean, unauthorized: boolean, lastOk: number|null }>`
  - Backoff após falha: `min(60s, 1s × 2^(falhas−1))`; a partir de 3 falhas seguidas chama `discover()` e, se achar o mesmo `id` em outro endereço, atualiza o store e zera a espera.

- [ ] **Step 1: Write the failing test** — `plugin/test/device-manager.test.js`

```js
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { DeviceManager } from '../lib/device-manager.js';

function memStore(devices) {
  let list = devices.map((d) => ({ ...d }));
  return { list: () => list.map((d) => ({ ...d })), update: (id, p) => { list = list.map((d) => (d.id === id ? { ...d, ...p } : d)); } };
}

function setup({ failAddrs = new Set(), found = [] } = {}) {
  let t = 0;
  const pushes = [];
  const client = {
    async pushState(addr, token, snap) {
      pushes.push(addr);
      if (failAddrs.has(addr)) { const e = new Error('down'); e.status = addr.endsWith(':401') ? 401 : undefined; throw e; }
    },
  };
  let discovers = 0;
  const store = memStore([{ id: 'g1', name: 'G1', addr: '10.0.0.5:80', token: 't' }]);
  const mgr = new DeviceManager({ client, store, now: () => t, discover: async () => { discovers++; return found; } });
  return { mgr, store, pushes, advance: (ms) => { t += ms; }, discovers: () => discovers };
}

test('pushes to every paired device and marks it online', async () => {
  const { mgr, pushes } = setup();
  await mgr.pushAll({ v: 1 });
  assert.deepEqual(pushes, ['10.0.0.5:80']);
  assert.equal(mgr.status()[0].online, true);
});

test('backs off after failures', async () => {
  const { mgr, pushes, advance } = setup({ failAddrs: new Set(['10.0.0.5:80']) });
  await mgr.pushAll({});          // falha 1 → espera 1s
  await mgr.pushAll({});          // ignorado (em espera)
  assert.equal(pushes.length, 1);
  advance(1000);
  await mgr.pushAll({});          // falha 2 → espera 2s
  advance(1999);
  await mgr.pushAll({});          // ainda em espera
  assert.equal(pushes.length, 2);
  assert.equal(mgr.status()[0].online, false);
});

test('relocates via discovery after 3 failures', async () => {
  const s = setup({ failAddrs: new Set(['10.0.0.5:80']), found: [{ id: 'g1', name: 'G1', addr: '10.0.0.9:80' }] });
  for (const wait of [0, 1000, 2000]) { s.advance(wait); await s.mgr.pushAll({}); }
  assert.equal(s.discovers(), 1);
  assert.equal(s.store.list()[0].addr, '10.0.0.9:80');
  await s.mgr.pushAll({});        // sem espera após relocalizar
  assert.equal(s.pushes.at(-1), '10.0.0.9:80');
  assert.equal(s.mgr.status()[0].online, true);
});

test('flags unauthorized devices', async () => {
  let t = 0;
  const store = memStore([{ id: 'g1', name: 'G1', addr: 'x:401', token: 't' }]);
  const client = { async pushState() { const e = new Error('401'); e.status = 401; throw e; } };
  const mgr = new DeviceManager({ client, store, now: () => t });
  await mgr.pushAll({});
  assert.equal(mgr.status()[0].unauthorized, true);
});
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd plugin && node --test test/device-manager.test.js`
Expected: FAIL — module not found

- [ ] **Step 3: Implement** — `plugin/lib/device-manager.js`

```js
export class DeviceManager {
  #health = new Map();

  constructor({ client, store, discover = null, now = () => Date.now() }) {
    this.client = client;
    this.store = store;
    this.discover = discover;
    this.now = now;
  }

  async pushAll(snapshot) {
    await Promise.all(this.store.list().map((d) => this.#pushOne(d, snapshot)));
  }

  status() {
    return this.store.list().map((d) => {
      const h = this.#health.get(d.id) ?? {};
      return { id: d.id, name: d.name, addr: d.addr, online: !!h.online, unauthorized: !!h.unauthorized, lastOk: h.lastOk ?? null };
    });
  }

  #h(id) {
    if (!this.#health.has(id)) this.#health.set(id, { fails: 0, nextTry: 0, online: false, unauthorized: false, lastOk: null });
    return this.#health.get(id);
  }

  async #pushOne(dev, snapshot) {
    const h = this.#h(dev.id);
    if (this.now() < h.nextTry) return;
    try {
      await this.client.pushState(dev.addr, dev.token, snapshot);
      Object.assign(h, { fails: 0, nextTry: 0, online: true, unauthorized: false, lastOk: this.now() });
    } catch (e) {
      h.fails += 1;
      h.online = false;
      h.unauthorized = e?.status === 401;
      h.nextTry = this.now() + Math.min(60_000, 1000 * 2 ** (h.fails - 1));
      if (h.fails >= 3 && this.discover) await this.#relocate(dev, h);
    }
  }

  async #relocate(dev, h) {
    try {
      const hit = (await this.discover()).find((f) => f.id === dev.id);
      if (hit && hit.addr !== dev.addr) {
        this.store.update(dev.id, { addr: hit.addr });
        h.nextTry = 0;
      }
    } catch {
      // descoberta falhou: mantém o backoff
    }
  }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd plugin && node --test test/device-manager.test.js`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add plugin/lib/device-manager.js plugin/test/device-manager.test.js
git commit -m "feat(plugin): device manager with backoff and mDNS relocation"
```

---

### Task 7: Descoberta mDNS mínima

**Files:**
- Create: `plugin/lib/mdns.js`
- Test: `plugin/test/mdns.test.js`

**Interfaces:**
- Consumes: `MDNS_SERVICE` (Task 1).
- Produces (contrato com o firmware, Plano 2): o gadget anuncia `_miblo._tcp.local` com registro TXT contendo `id=<id>` e `name=<nome>`, SRV com a porta HTTP e A com o IPv4.
  - `buildQuery(service) → Buffer` (PTR, bit QU ligado)
  - `parseMessage(buf) → Array<{ name, type, data }>`
  - `resolveDevices(records, service) → Array<{ id, name, addr }>`
  - `discover({ service?, timeoutMs?, socketFactory? }) → Promise<Array<{ id, name, addr }>>`

- [ ] **Step 1: Write the failing test** — `plugin/test/mdns.test.js`

```js
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { EventEmitter } from 'node:events';
import { buildQuery, parseMessage, resolveDevices, discover } from '../lib/mdns.js';

// Encoder usado só nos testes para montar respostas como as de um gadget.
const enc = (name) => Buffer.concat([...name.split('.').filter(Boolean).map((l) => Buffer.concat([Buffer.from([Buffer.byteLength(l)]), Buffer.from(l)])), Buffer.from([0])]);
function rr(name, type, rdata) {
  const head = Buffer.alloc(10);
  head.writeUInt16BE(type, 0);
  head.writeUInt16BE(1, 2);
  head.writeUInt32BE(120, 4);
  head.writeUInt16BE(rdata.length, 8);
  return Buffer.concat([enc(name), head, rdata]);
}
function response(records) {
  const h = Buffer.alloc(12);
  h.writeUInt16BE(0x8400, 2);
  h.writeUInt16BE(records.length, 6);
  return Buffer.concat([h, ...records]);
}
function gadgetResponse({ inst = 'Miblo-4F2A._miblo._tcp.local', host = 'miblo-4f2a.local', ip = [192, 168, 0, 42], port = 80, id = 'miblo-4f2a' } = {}) {
  const srv = Buffer.concat([Buffer.from([0, 0, 0, 0, port >> 8, port & 255]), enc(host)]);
  const txt = Buffer.concat([`id=${id}`, 'name=Miblo-4F2A', 'fw=0.1.0'].map((s) => Buffer.concat([Buffer.from([s.length]), Buffer.from(s)])));
  return response([
    rr('_miblo._tcp.local', 12, enc(inst)),
    rr(inst, 33, srv),
    rr(inst, 16, txt),
    rr(host, 1, Buffer.from(ip)),
  ]);
}

test('buildQuery asks for PTR with the QU bit', () => {
  const q = buildQuery('_miblo._tcp.local');
  assert.equal(q.readUInt16BE(4), 1);
  assert.deepEqual([...q.subarray(-4)], [0x00, 0x0c, 0x80, 0x01]);
});

test('parse + resolve a gadget announcement', () => {
  const records = parseMessage(gadgetResponse());
  assert.deepEqual(resolveDevices(records, '_miblo._tcp.local'), [{ id: 'miblo-4f2a', name: 'Miblo-4F2A', addr: '192.168.0.42:80' }]);
});

test('parse handles name compression pointers', () => {
  const h = Buffer.alloc(12);
  h.writeUInt16BE(1, 6);
  const name = enc('_miblo._tcp.local');
  const ptrTarget = Buffer.concat([Buffer.from([4]), Buffer.from('inst'), Buffer.from([0xc0, 12])]);
  const head = Buffer.alloc(10);
  head.writeUInt16BE(12, 0);
  head.writeUInt16BE(1, 2);
  head.writeUInt16BE(ptrTarget.length, 8);
  const [r] = parseMessage(Buffer.concat([h, name, head, ptrTarget]));
  assert.equal(r.data, 'inst._miblo._tcp.local');
});

test('resolve ignores incomplete announcements', () => {
  const records = parseMessage(response([rr('_miblo._tcp.local', 12, enc('x._miblo._tcp.local'))]));
  assert.deepEqual(resolveDevices(records, '_miblo._tcp.local'), []);
});

test('discover collects responses until the timeout and dedupes by id', async () => {
  const sock = new EventEmitter();
  sock.bind = (port, cb) => cb();
  sock.send = () => {
    setTimeout(() => sock.emit('message', gadgetResponse()), 5);
    setTimeout(() => sock.emit('message', gadgetResponse()), 10);
    setTimeout(() => sock.emit('message', Buffer.from('garbage')), 15);
  };
  sock.close = () => {};
  const found = await discover({ timeoutMs: 50, socketFactory: () => sock });
  assert.deepEqual(found, [{ id: 'miblo-4f2a', name: 'Miblo-4F2A', addr: '192.168.0.42:80' }]);
});
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd plugin && node --test test/mdns.test.js`
Expected: FAIL — module not found

- [ ] **Step 3: Implement** — `plugin/lib/mdns.js`

```js
import dgram from 'node:dgram';
import { MDNS_SERVICE } from './constants.js';

const T_A = 1;
const T_PTR = 12;
const T_TXT = 16;
const T_SRV = 33;

function encodeName(name) {
  const labels = name.split('.').filter(Boolean).map((l) => {
    const b = Buffer.from(l, 'utf8');
    return Buffer.concat([Buffer.from([b.length]), b]);
  });
  return Buffer.concat([...labels, Buffer.from([0])]);
}

export function buildQuery(service) {
  const header = Buffer.alloc(12);
  header.writeUInt16BE(1, 4);
  return Buffer.concat([header, encodeName(service), Buffer.from([0x00, T_PTR, 0x80, 0x01])]);
}

function readName(buf, offset) {
  const labels = [];
  let off = offset;
  let end = null;
  for (let guard = 0; guard < 128; guard++) {
    const len = buf[off];
    if (len === undefined) throw new Error('truncated name');
    if (len === 0) { off += 1; break; }
    if ((len & 0xc0) === 0xc0) {
      if (end === null) end = off + 2;
      off = ((len & 0x3f) << 8) | buf[off + 1];
      continue;
    }
    labels.push(buf.toString('utf8', off + 1, off + 1 + len));
    off += 1 + len;
  }
  return { name: labels.join('.'), next: end ?? off };
}

export function parseMessage(buf) {
  if (buf.length < 12) throw new Error('short packet');
  const qd = buf.readUInt16BE(4);
  const total = buf.readUInt16BE(6) + buf.readUInt16BE(8) + buf.readUInt16BE(10);
  let off = 12;
  for (let i = 0; i < qd; i++) off = readName(buf, off).next + 4;
  const records = [];
  for (let i = 0; i < total; i++) {
    const { name, next } = readName(buf, off);
    const type = buf.readUInt16BE(next);
    const rdlen = buf.readUInt16BE(next + 8);
    const rd = next + 10;
    let data = null;
    if (type === T_PTR) data = readName(buf, rd).name;
    else if (type === T_SRV) data = { port: buf.readUInt16BE(rd + 4), target: readName(buf, rd + 6).name };
    else if (type === T_A) data = [...buf.subarray(rd, rd + 4)].join('.');
    else if (type === T_TXT) {
      data = {};
      for (let p = rd; p < rd + rdlen;) {
        const s = buf.toString('utf8', p + 1, p + 1 + buf[p]);
        const eq = s.indexOf('=');
        if (eq > 0) data[s.slice(0, eq)] = s.slice(eq + 1);
        p += 1 + buf[p];
      }
    }
    records.push({ name, type, data });
    off = rd + rdlen;
  }
  return records;
}

export function resolveDevices(records, service) {
  const lc = (s) => String(s).toLowerCase();
  const out = [];
  for (const ptr of records.filter((r) => r.type === T_PTR && lc(r.name) === lc(service))) {
    const inst = ptr.data;
    const srv = records.find((r) => r.type === T_SRV && lc(r.name) === lc(inst));
    const txt = records.find((r) => r.type === T_TXT && lc(r.name) === lc(inst))?.data ?? {};
    const a = srv && records.find((r) => r.type === T_A && lc(r.name) === lc(srv.data.target));
    if (srv && a && txt.id) out.push({ id: txt.id, name: txt.name || inst.split('.')[0], addr: `${a.data}:${srv.data.port}` });
  }
  return out;
}

export function discover({ service = MDNS_SERVICE, timeoutMs = 2000, socketFactory } = {}) {
  return new Promise((resolve) => {
    const sock = (socketFactory ?? (() => dgram.createSocket({ type: 'udp4', reuseAddr: true })))();
    const records = [];
    let finished = false;
    const finish = () => {
      if (finished) return;
      finished = true;
      try { sock.close(); } catch { /* já fechado */ }
      const byId = new Map();
      for (const d of resolveDevices(records, service)) byId.set(d.id, d);
      resolve([...byId.values()]);
    };
    sock.on('message', (msg) => {
      try { records.push(...parseMessage(msg)); } catch { /* pacote inválido */ }
    });
    sock.on('error', finish);
    sock.bind(0, () => {
      try { sock.send(buildQuery(service), 5353, '224.0.0.251'); } catch { finish(); }
    });
    setTimeout(finish, timeoutMs);
  });
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd plugin && node --test test/mdns.test.js`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add plugin/lib/mdns.js plugin/test/mdns.test.js
git commit -m "feat(plugin): minimal DNS-SD discovery for _miblo._tcp"
```

---

### Task 8: Servidor local e `createBridge` (integração com o gadget falso)

**Files:**
- Create: `plugin/lib/bridge-server.js`
- Create: `plugin/bin/bridge.js`
- Test: `plugin/test/bridge.test.js`

**Interfaces:**
- Consumes: tudo das Tasks 1–7.
- Produces:
  - `createBridgeServer({ onEvent(evt), onStatusline(sl), getStatus() }) → http.Server` com rotas `GET /health → {ok:true, app:'miblo-bridge'}`, `GET /status`, `POST /event`, `POST /statusline`; corpo máx. 256 KB; JSON inválido → 400.
  - `createBridge({ dataDir, now?, client?, discoverFn?, host? }) → { tracker, metrics, devices, server, push(): Promise, schedule(), idleFor(): ms }`
  - `bin/bridge.js --data <dir>`: escuta em `HOST:PORT`; sai com 0 se a porta já estiver em uso (outra instância); heartbeat a cada `HEARTBEAT_MS`; `sweep` a cada `PID_CHECK_MS`; sai após `IDLE_EXIT_MS` sem sessões; reinstala o tap se a status line estiver encadeada.

- [ ] **Step 1: Write the failing test** — `plugin/test/bridge.test.js`

```js
import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { startFakeDevice } from './fakes/fake-device.js';
import { DeviceClient } from '../lib/device-client.js';
import { DeviceStore } from '../lib/device-store.js';
import { createBridge } from '../bin/bridge.js';

async function started(bridge) {
  await new Promise((r) => bridge.server.listen(0, '127.0.0.1', r));
  const base = `http://127.0.0.1:${bridge.server.address().port}`;
  const post = (p, body) => fetch(base + p, { method: 'POST', headers: { 'content-type': 'application/json' }, body: typeof body === 'string' ? body : JSON.stringify(body) });
  return { base, post, stop: () => new Promise((r) => bridge.server.close(r)) };
}

test('hook events and statusline data reach a paired device', async () => {
  const dev = await startFakeDevice();
  const dataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-bridge-'));
  const client = new DeviceClient();
  const token = await client.pair(dev.addr, '4827', 'test');
  new DeviceStore(dataDir).upsert({ id: 'miblo-4f2a', name: 'Miblo-4F2A', addr: dev.addr, token });

  const bridge = createBridge({ dataDir, client, discoverFn: async () => [], host: 'test-host' });
  const http = await started(bridge);
  try {
    assert.equal((await (await fetch(http.base + '/health')).json()).app, 'miblo-bridge');

    await http.post('/event', { session_id: 's1', hook_event_name: 'PermissionRequest', cwd: '/w/api', tool_name: 'Bash', tool_input: { command: 'ls' } });
    await http.post('/statusline', { session_id: 's1', model: { display_name: 'Opus' }, context_window: { used_percentage: 12 }, rate_limits: { five_hour: { used_percentage: 50, resets_at: Math.floor(Date.now() / 1000) + 3600 } } });
    await bridge.push();

    const snap = dev.state.snapshots.at(-1);
    assert.equal(snap.host, 'test-host');
    assert.equal(snap.sessions[0].st, 'perm');
    assert.equal(snap.sessions[0].ctx, 12);
    assert.equal(snap.usage.h5.pct, 50);
    assert.equal(snap.alerts[0].kind, 'perm');

    const status = await (await fetch(http.base + '/status')).json();
    assert.equal(status.devices[0].online, true);
    assert.equal(status.statuslineSeen, true);
    assert.equal(status.sessions.length, 1);
  } finally {
    await http.stop();
    await dev.close();
  }
});

test('seq increases on every push', async () => {
  const dev = await startFakeDevice();
  const dataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-bridge-'));
  const client = new DeviceClient();
  const token = await client.pair(dev.addr, '4827', 'test');
  new DeviceStore(dataDir).upsert({ id: 'x', name: 'X', addr: dev.addr, token });
  const bridge = createBridge({ dataDir, client, discoverFn: async () => [] });
  try {
    await bridge.push();
    await bridge.push();
    const [a, b] = dev.state.snapshots;
    assert.ok(b.seq > a.seq);
  } finally {
    await dev.close();
  }
});

test('SessionEnd forgets metrics and invalid JSON returns 400', async () => {
  const dataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-bridge-'));
  const bridge = createBridge({ dataDir, discoverFn: async () => [] });
  const http = await started(bridge);
  try {
    await http.post('/statusline', { session_id: 's1', context_window: { used_percentage: 5 } });
    await http.post('/event', { session_id: 's1', hook_event_name: 'SessionEnd' });
    assert.equal(bridge.metrics.forSession('s1'), undefined);
    assert.equal((await http.post('/event', '{bad')).status, 400);
    assert.equal((await fetch(http.base + '/nope')).status, 404);
  } finally {
    await http.stop();
  }
});
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd plugin && node --test test/bridge.test.js`
Expected: FAIL — module not found

- [ ] **Step 3: Implement** — `plugin/lib/bridge-server.js`

```js
import http from 'node:http';

const MAX_BODY = 256 * 1024;

function readJson(req) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    let size = 0;
    req.on('data', (c) => {
      size += c.length;
      if (size > MAX_BODY) {
        reject(new Error('body too large'));
        req.destroy();
      } else {
        chunks.push(c);
      }
    });
    req.on('end', () => {
      try {
        resolve(JSON.parse(Buffer.concat(chunks).toString('utf8') || '{}'));
      } catch (e) {
        reject(e);
      }
    });
    req.on('error', reject);
  });
}

export function createBridgeServer({ onEvent, onStatusline, getStatus }) {
  return http.createServer(async (req, res) => {
    const send = (code, obj) => {
      res.writeHead(code, { 'content-type': 'application/json' });
      res.end(JSON.stringify(obj));
    };
    try {
      if (req.method === 'GET' && req.url === '/health') return send(200, { ok: true, app: 'miblo-bridge' });
      if (req.method === 'GET' && req.url === '/status') return send(200, await getStatus());
      if (req.method === 'POST' && req.url === '/event') {
        onEvent(await readJson(req));
        return send(200, { ok: true });
      }
      if (req.method === 'POST' && req.url === '/statusline') {
        onStatusline(await readJson(req));
        return send(200, { ok: true });
      }
      return send(404, { error: 'not found' });
    } catch (e) {
      return send(400, { error: String(e?.message ?? e) });
    }
  });
}
```

- [ ] **Step 4: Implement** — `plugin/bin/bridge.js`

```js
#!/usr/bin/env node
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { PORT, HOST, DEBOUNCE_MS, HEARTBEAT_MS, PID_CHECK_MS, IDLE_EXIT_MS, claudeSettingsPath } from '../lib/constants.js';
import { SessionTracker } from '../lib/session-tracker.js';
import { MetricsStore } from '../lib/metrics-store.js';
import { buildSnapshot } from '../lib/snapshot-builder.js';
import { DeviceClient } from '../lib/device-client.js';
import { DeviceStore } from '../lib/device-store.js';
import { DeviceManager } from '../lib/device-manager.js';
import { discover } from '../lib/mdns.js';
import { createBridgeServer } from '../lib/bridge-server.js';
import { isLinked, installTap } from '../lib/statusline-link.js';

export function createBridge({ dataDir, now = () => Date.now(), client = new DeviceClient(), discoverFn = discover, host = os.hostname() }) {
  const tracker = new SessionTracker({ now });
  const metrics = new MetricsStore({ now });
  const devices = new DeviceManager({ client, store: new DeviceStore(dataDir), discover: discoverFn, now });
  let seq = 0;
  let timer = null;
  let lastActive = now();

  const push = async () => {
    timer = null;
    if (tracker.hasActive()) lastActive = now();
    const snapshot = buildSnapshot({ seq: ++seq, nowMs: now(), host, tracker, metrics });
    await devices.pushAll(snapshot);
  };
  const schedule = () => {
    if (!timer) timer = setTimeout(() => push().catch(() => {}), DEBOUNCE_MS);
  };

  const server = createBridgeServer({
    onEvent(evt) {
      if (evt?.hook_event_name === 'SessionEnd') metrics.forget(evt.session_id);
      if (tracker.handle(evt)) schedule();
    },
    onStatusline(sl) {
      if (metrics.ingest(sl)) schedule();
    },
    getStatus: () => ({
      sessions: tracker.sessions(),
      usage: metrics.usage(),
      today: metrics.today(),
      devices: devices.status(),
      statuslineSeen: metrics.hasReadings(),
    }),
  });

  return { tracker, metrics, devices, server, push, schedule, idleFor: () => now() - lastActive };
}

function argValue(name) {
  const i = process.argv.indexOf(name);
  return i > 0 ? process.argv[i + 1] : undefined;
}

function main() {
  const here = path.dirname(fileURLToPath(import.meta.url));
  const dataDir = argValue('--data') || process.env.CLAUDE_PLUGIN_DATA || path.join(os.homedir(), '.miblo');
  try {
    if (isLinked({ settingsPath: claudeSettingsPath(), dataDir })) installTap({ pluginRoot: path.resolve(here, '..'), dataDir });
  } catch {
    // a status line continua funcionando com a cópia anterior do tap
  }
  const bridge = createBridge({ dataDir });
  bridge.server.on('error', (e) => process.exit(e.code === 'EADDRINUSE' ? 0 : 1));
  bridge.server.listen(PORT, HOST);
  setInterval(() => bridge.push().catch(() => {}), HEARTBEAT_MS);
  setInterval(() => {
    if (bridge.tracker.sweep()) bridge.schedule();
    if (bridge.idleFor() > IDLE_EXIT_MS) process.exit(0);
  }, PID_CHECK_MS);
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) main();
```

> Nota: `bridge.js` importa `statusline-link.js`, criado na Task 10. Para rodar este teste agora, crie o stub abaixo; a Task 10 o substitui pela implementação real.

- [ ] **Step 5: Stub temporário** — `plugin/lib/statusline-link.js`

```js
export function isLinked() {
  return false;
}
export function installTap() {}
```

- [ ] **Step 6: Run test to verify it passes**

Run: `cd plugin && node --test test/bridge.test.js`
Expected: PASS (3 tests)

- [ ] **Step 7: Run the whole suite**

Run: `cd plugin && npm test`
Expected: PASS (todas as suítes até aqui)

- [ ] **Step 8: Commit**

```bash
git add plugin/lib/bridge-server.js plugin/bin/bridge.js plugin/lib/statusline-link.js plugin/test/bridge.test.js
git commit -m "feat(plugin): local bridge service wiring tracker, metrics and devices"
```

---

### Task 9: `proc.js` e `hook.js`

**Files:**
- Create: `plugin/lib/proc.js`
- Create: `plugin/bin/hook.js`
- Test: `plugin/test/proc.test.js`
- Test: `plugin/test/hook.test.js`

**Interfaces:**
- Produces:
  - `parsePs(text) → Map<pid, { ppid, comm }>`; `findAncestor(map, startPid) → pid|null` (primeiro ancestral cujo executável é `claude` ou `node`); `findClaudePid({ platform?, startPid?, runPs? }) → pid|null` (sempre `null` no Windows).
  - `bin/hook.js`: lê o JSON do stdin; em `SessionStart` acrescenta `pid`; faz `POST /event`; se falhar, sobe `bin/bridge.js --data $CLAUDE_PLUGIN_DATA` destacado (exceto com `MIBLO_NO_SPAWN=1`) e tenta de novo uma vez. Sempre `exit 0`, nunca escreve em stdout.

- [ ] **Step 1: Write the failing tests**

`plugin/test/proc.test.js`:

```js
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { parsePs, findAncestor, findClaudePid } from '../lib/proc.js';

const PS = `
    1     0 /sbin/launchd
34076 37899 /bin/zsh
34450 34076 claude
68429 34450 /bin/zsh
70000 68429 /usr/local/bin/node
`;

test('parsePs reads pid, ppid and command', () => {
  const m = parsePs(PS);
  assert.deepEqual(m.get(34450), { ppid: 34076, comm: 'claude' });
});

test('finds the nearest claude ancestor starting from the hook shell', () => {
  assert.equal(findAncestor(parsePs(PS), 68429), 34450);
});

test('accepts an npm-installed node Claude and full paths', () => {
  const ps = '10 1 /bin/zsh\n20 10 /opt/homebrew/bin/node\n30 20 /bin/sh\n';
  assert.equal(findAncestor(parsePs(ps), 30), 20);
});

test('returns null when there is no match or on Windows', () => {
  assert.equal(findAncestor(parsePs('10 1 /bin/zsh\n'), 10), null);
  assert.equal(findClaudePid({ platform: 'win32' }), null);
  assert.equal(findClaudePid({ platform: 'darwin', startPid: 68429, runPs: () => PS }), 34450);
  assert.equal(findClaudePid({ platform: 'linux', runPs: () => { throw new Error('no ps'); } }), null);
});
```

`plugin/test/hook.test.js`:

```js
import { test } from 'node:test';
import assert from 'node:assert/strict';
import http from 'node:http';
import { spawnSync, spawn } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const hook = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../bin/hook.js');

function runHook(input, env) {
  return new Promise((resolve) => {
    const child = spawn(process.execPath, [hook], { env: { ...process.env, ...env } });
    let out = '';
    child.stdout.on('data', (d) => { out += d; });
    child.on('close', (code) => resolve({ code, out }));
    child.stdin.end(input);
  });
}

test('forwards the event to the bridge, adding pid on SessionStart', async () => {
  const received = [];
  const server = http.createServer((req, res) => {
    let body = '';
    req.on('data', (c) => { body += c; });
    req.on('end', () => { received.push(JSON.parse(body)); res.end('{}'); });
  });
  await new Promise((r) => server.listen(0, '127.0.0.1', r));
  try {
    const r = await runHook(JSON.stringify({ session_id: 's1', hook_event_name: 'SessionStart' }), { MIBLO_PORT: String(server.address().port), MIBLO_NO_SPAWN: '1' });
    assert.equal(r.code, 0);
    assert.equal(r.out, '');
    assert.equal(received[0].session_id, 's1');
    assert.ok('pid' in received[0]);
  } finally {
    await new Promise((r) => server.close(r));
  }
});

test('exits 0 silently when the bridge is down', () => {
  const t0 = Date.now();
  const r = spawnSync(process.execPath, [hook], { input: '{"session_id":"s","hook_event_name":"Stop"}', env: { ...process.env, MIBLO_PORT: '1', MIBLO_NO_SPAWN: '1' } });
  assert.equal(r.status, 0);
  assert.equal(r.stdout.toString(), '');
  assert.ok(Date.now() - t0 < 3000);
});

test('exits 0 silently on invalid input', () => {
  const r = spawnSync(process.execPath, [hook], { input: 'not json', env: { ...process.env, MIBLO_PORT: '1', MIBLO_NO_SPAWN: '1' } });
  assert.equal(r.status, 0);
  assert.equal(r.stdout.toString(), '');
});
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd plugin && node --test test/proc.test.js test/hook.test.js`
Expected: FAIL — modules not found

- [ ] **Step 3: Implement** — `plugin/lib/proc.js`

```js
import { execFileSync } from 'node:child_process';

const CLAUDE_RE = /(^|[\\/])(claude|node)(\.exe)?$/i;

export function parsePs(text) {
  const map = new Map();
  for (const line of String(text).split('\n')) {
    const m = line.trim().match(/^(\d+)\s+(\d+)\s+(.+)$/);
    if (m) map.set(Number(m[1]), { ppid: Number(m[2]), comm: m[3].trim() });
  }
  return map;
}

export function findAncestor(map, startPid) {
  let pid = startPid;
  for (let i = 0; i < 32 && pid > 1; i++) {
    const p = map.get(pid);
    if (!p) return null;
    if (CLAUDE_RE.test(p.comm)) return pid;
    pid = p.ppid;
  }
  return null;
}

export function findClaudePid({ platform = process.platform, startPid = process.ppid, runPs } = {}) {
  if (platform === 'win32') return null;
  try {
    const read = runPs ?? (() => execFileSync('ps', ['-A', '-o', 'pid=,ppid=,comm='], { encoding: 'utf8', timeout: 500 }));
    return findAncestor(parsePs(read()), startPid);
  } catch {
    return null;
  }
}
```

- [ ] **Step 4: Implement** — `plugin/bin/hook.js`

```js
#!/usr/bin/env node
// Entrada de todos os hooks. Nunca pode atrasar nem quebrar o Claude Code:
// roda com "async": true, captura tudo, sai sempre com 0 e não escreve em stdout.
import { spawn } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { PORT, HOST } from '../lib/constants.js';
import { findClaudePid } from '../lib/proc.js';

const here = path.dirname(fileURLToPath(import.meta.url));

async function readStdin() {
  const chunks = [];
  for await (const c of process.stdin) chunks.push(c);
  return Buffer.concat(chunks).toString('utf8');
}

async function post(body) {
  const res = await fetch(`http://${HOST}:${PORT}/event`, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body,
    signal: AbortSignal.timeout(800),
  });
  if (!res.ok) throw new Error(`bridge ${res.status}`);
}

function startBridge() {
  const dataDir = process.env.CLAUDE_PLUGIN_DATA || path.join(here, '..', '.data');
  const child = spawn(process.execPath, [path.join(here, 'bridge.js'), '--data', dataDir], {
    detached: true,
    stdio: 'ignore',
    windowsHide: true,
  });
  child.unref();
}

async function main() {
  const evt = JSON.parse((await readStdin()) || '{}');
  if (evt.hook_event_name === 'SessionStart') evt.pid = findClaudePid();
  const body = JSON.stringify(evt);
  try {
    await post(body);
  } catch {
    if (process.env.MIBLO_NO_SPAWN === '1') return;
    startBridge();
    await new Promise((r) => setTimeout(r, 400));
    await post(body).catch(() => {});
  }
}

main()
  .catch(() => {})
  .finally(() => process.exit(0));
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cd plugin && node --test test/proc.test.js test/hook.test.js`
Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add plugin/lib/proc.js plugin/bin/hook.js plugin/test/proc.test.js plugin/test/hook.test.js
git commit -m "feat(plugin): hook entrypoint that forwards events and starts the bridge"
```

---

### Task 10: Tap da status line e encadeamento no `settings.json`

**Files:**
- Create: `plugin/bin/statusline-tap.mjs`
- Modify: `plugin/lib/statusline-link.js` (substitui o stub da Task 8)
- Test: `plugin/test/statusline-tap.test.js`
- Test: `plugin/test/statusline-link.test.js`

**Interfaces:**
- Produces:
  - `statusline-tap.mjs` (autossuficiente, vive em `${dataDir}`): encaminha o stdin para `POST /statusline` (timeout 200ms), executa o comando de `statusline-original.json` (ao lado do script) com o mesmo stdin e devolve stdout/stderr/código. Sem comando original: não imprime nada e sai com 0.
  - `statusline-link.js`: `tapCommand({ dataDir, nodePath }) → string`, `installTap({ pluginRoot, dataDir })`, `isLinked({ settingsPath, dataDir }) → boolean`, `link({ settingsPath, dataDir, pluginRoot, nodePath? }) → { changed, original }`, `unlink({ settingsPath, dataDir }) → { changed }`. `link` faz backup único em `settings.json.miblo-backup`, preserva as demais chaves e campos do `statusLine` (ex.: `padding`, `refreshInterval`) e é idempotente.

- [ ] **Step 1: Write the failing tests**

`plugin/test/statusline-link.test.js`:

```js
import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { link, unlink, isLinked } from '../lib/statusline-link.js';

const pluginRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

function setup(settings) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-link-'));
  const settingsPath = path.join(root, 'claude', 'settings.json');
  const dataDir = path.join(root, 'data');
  if (settings !== undefined) {
    fs.mkdirSync(path.dirname(settingsPath), { recursive: true });
    fs.writeFileSync(settingsPath, JSON.stringify(settings, null, 2));
  }
  const read = () => JSON.parse(fs.readFileSync(settingsPath, 'utf8'));
  return { settingsPath, dataDir, read, opts: { settingsPath, dataDir, pluginRoot, nodePath: '/usr/bin/node' } };
}

test('link wraps an existing statusLine and keeps other settings', () => {
  const s = setup({ theme: 'dark', statusLine: { type: 'command', command: 'bash ~/sl.sh', padding: 2 } });
  const r = link(s.opts);
  assert.equal(r.changed, true);
  const after = s.read();
  assert.equal(after.theme, 'dark');
  assert.equal(after.statusLine.padding, 2);
  assert.equal(after.statusLine.command, `"/usr/bin/node" "${path.join(s.dataDir, 'statusline-tap.mjs')}"`);
  assert.ok(fs.existsSync(path.join(s.dataDir, 'statusline-tap.mjs')));
  assert.deepEqual(JSON.parse(fs.readFileSync(path.join(s.dataDir, 'statusline-original.json'), 'utf8')).command, 'bash ~/sl.sh');
  assert.ok(fs.existsSync(s.settingsPath + '.miblo-backup'));
  assert.equal(isLinked(s), true);
});

test('link is idempotent', () => {
  const s = setup({ statusLine: { type: 'command', command: 'x' } });
  link(s.opts);
  assert.equal(link(s.opts).changed, false);
  assert.equal(JSON.parse(fs.readFileSync(path.join(s.dataDir, 'statusline-original.json'), 'utf8')).command, 'x');
});

test('link works without settings.json or statusLine', () => {
  const s = setup(undefined);
  link(s.opts);
  assert.equal(s.read().statusLine.type, 'command');
  assert.deepEqual(JSON.parse(fs.readFileSync(path.join(s.dataDir, 'statusline-original.json'), 'utf8')), {});
});

test('unlink restores the original, or removes statusLine when there was none', () => {
  const a = setup({ statusLine: { type: 'command', command: 'orig', refreshInterval: 5 } });
  link(a.opts);
  assert.equal(unlink(a.opts).changed, true);
  assert.deepEqual(a.read().statusLine, { type: 'command', command: 'orig', refreshInterval: 5 });

  const b = setup({ other: 1 });
  link(b.opts);
  unlink(b.opts);
  assert.deepEqual(b.read(), { other: 1 });
  assert.equal(unlink(b.opts).changed, false);
});

test('refuses to touch a corrupt settings.json', () => {
  const s = setup(undefined);
  fs.mkdirSync(path.dirname(s.settingsPath), { recursive: true });
  fs.writeFileSync(s.settingsPath, '{broken');
  assert.throws(() => link(s.opts));
  assert.equal(fs.readFileSync(s.settingsPath, 'utf8'), '{broken');
});
```

`plugin/test/statusline-tap.test.js`:

```js
import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import http from 'node:http';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const src = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../bin/statusline-tap.mjs');

function installed(original) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-tap-'));
  fs.copyFileSync(src, path.join(dir, 'statusline-tap.mjs'));
  fs.writeFileSync(path.join(dir, 'statusline-original.json'), JSON.stringify(original));
  return path.join(dir, 'statusline-tap.mjs');
}

function run(tap, input, env = {}) {
  return new Promise((resolve) => {
    const child = spawn(process.execPath, [tap], { env: { ...process.env, ...env } });
    let out = '';
    child.stdout.on('data', (d) => { out += d; });
    child.on('close', (code) => resolve({ code, out }));
    child.stdin.end(input);
  });
}

const INPUT = JSON.stringify({ session_id: 's1', model: { display_name: 'Opus' } });

test('passes stdin to the original command and returns its exact output and code', async () => {
  const tap = installed({ type: 'command', command: `node -e "let s='';process.stdin.on('data',d=>s+=d).on('end',()=>{process.stdout.write('[' + JSON.parse(s).model.display_name + ']');process.exit(3)})"` });
  const r = await run(tap, INPUT, { MIBLO_PORT: '1' });
  assert.equal(r.out, '[Opus]');
  assert.equal(r.code, 3);
});

test('prints nothing when there is no original command', async () => {
  const r = await run(installed({}), INPUT, { MIBLO_PORT: '1' });
  assert.equal(r.out, '');
  assert.equal(r.code, 0);
});

test('forwards the statusline JSON to the bridge', async () => {
  const got = [];
  const server = http.createServer((req, res) => {
    let b = '';
    req.on('data', (c) => { b += c; });
    req.on('end', () => { got.push({ url: req.url, body: JSON.parse(b) }); res.end('{}'); });
  });
  await new Promise((r) => server.listen(0, '127.0.0.1', r));
  try {
    await run(installed({}), INPUT, { MIBLO_PORT: String(server.address().port) });
    assert.deepEqual(got, [{ url: '/statusline', body: JSON.parse(INPUT) }]);
  } finally {
    await new Promise((r) => server.close(r));
  }
});
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd plugin && node --test test/statusline-link.test.js test/statusline-tap.test.js`
Expected: FAIL — `link is not a function` / tap file not found

- [ ] **Step 3: Implement** — `plugin/bin/statusline-tap.mjs`

```js
#!/usr/bin/env node
// Autossuficiente: é copiado para o diretório de dados do plugin e referenciado
// em ~/.claude/settings.json. Não importa nada de lib/.
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const port = Number(process.env.MIBLO_PORT || 47821);

async function readStdin() {
  const chunks = [];
  for await (const c of process.stdin) chunks.push(c);
  return Buffer.concat(chunks);
}

function loadOriginal() {
  try {
    return JSON.parse(fs.readFileSync(path.join(here, 'statusline-original.json'), 'utf8'));
  } catch {
    return null;
  }
}

async function main() {
  const input = await readStdin();
  const forward = fetch(`http://127.0.0.1:${port}/statusline`, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: input,
    signal: AbortSignal.timeout(200),
  }).catch(() => {});

  const original = loadOriginal();
  if (original?.command) {
    const r = spawnSync(original.command, { shell: true, input, maxBuffer: 1024 * 1024 });
    if (r.stdout?.length) process.stdout.write(r.stdout);
    if (r.stderr?.length) process.stderr.write(r.stderr);
    process.exitCode = r.status ?? 0;
  }
  await forward;
}

main().catch(() => {});
```

- [ ] **Step 4: Implement** — `plugin/lib/statusline-link.js`

```js
import fs from 'node:fs';
import path from 'node:path';

const TAP = 'statusline-tap.mjs';
const ORIGINAL = 'statusline-original.json';

function readSettings(settingsPath) {
  try {
    return JSON.parse(fs.readFileSync(settingsPath, 'utf8'));
  } catch (e) {
    if (e.code === 'ENOENT') return {};
    throw new Error(`cannot parse ${settingsPath}: ${e.message}`);
  }
}

function writeJson(file, obj) {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  const tmp = `${file}.miblo-tmp`;
  fs.writeFileSync(tmp, JSON.stringify(obj, null, 2) + '\n');
  fs.renameSync(tmp, file);
}

export function tapCommand({ dataDir, nodePath = process.execPath }) {
  return `"${nodePath}" "${path.join(dataDir, TAP)}"`;
}

export function installTap({ pluginRoot, dataDir }) {
  fs.mkdirSync(dataDir, { recursive: true });
  fs.copyFileSync(path.join(pluginRoot, 'bin', TAP), path.join(dataDir, TAP));
}

export function isLinked({ settingsPath, dataDir }) {
  const cmd = readSettings(settingsPath).statusLine?.command;
  return typeof cmd === 'string' && cmd.includes(path.join(dataDir, TAP));
}

export function link({ settingsPath, dataDir, pluginRoot, nodePath = process.execPath }) {
  const settings = readSettings(settingsPath);
  installTap({ pluginRoot, dataDir });
  if (isLinked({ settingsPath, dataDir })) return { changed: false, original: null };

  if (fs.existsSync(settingsPath) && !fs.existsSync(`${settingsPath}.miblo-backup`)) {
    fs.copyFileSync(settingsPath, `${settingsPath}.miblo-backup`);
  }
  const original = settings.statusLine ?? null;
  writeJson(path.join(dataDir, ORIGINAL), original ?? {});
  settings.statusLine = { ...(original ?? {}), type: 'command', command: tapCommand({ dataDir, nodePath }) };
  writeJson(settingsPath, settings);
  return { changed: true, original };
}

export function unlink({ settingsPath, dataDir }) {
  if (!isLinked({ settingsPath, dataDir })) return { changed: false };
  const settings = readSettings(settingsPath);
  let original = null;
  try {
    original = JSON.parse(fs.readFileSync(path.join(dataDir, ORIGINAL), 'utf8'));
  } catch {
    original = null;
  }
  if (original?.command) settings.statusLine = original;
  else delete settings.statusLine;
  writeJson(settingsPath, settings);
  return { changed: true };
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cd plugin && node --test test/statusline-link.test.js test/statusline-tap.test.js`
Expected: PASS

- [ ] **Step 6: Run the whole suite**

Run: `cd plugin && npm test`
Expected: PASS

- [ ] **Step 7: Commit**

```bash
git add plugin/bin/statusline-tap.mjs plugin/lib/statusline-link.js plugin/test/statusline-link.test.js plugin/test/statusline-tap.test.js
git commit -m "feat(plugin): consent-based statusline tap for official usage metrics"
```

---

### Task 11: CLI `miblo.js` e `onboard.js`

**Files:**
- Create: `plugin/bin/miblo.js`
- Create: `plugin/bin/onboard.js`
- Test: `plugin/test/miblo-cli.test.js`
- Test: `plugin/test/onboard.test.js`

**Interfaces:**
- Consumes: `DeviceClient`, `DeviceStore`, `discover`, `link`, `unlink`, `isLinked`, `claudeSettingsPath`, `PORT`, `HOST`.
- Produces:
  - `run(argv: string[], deps) → Promise<{ code: number, out: string }>` com `deps = { dataDir, pluginRoot, settingsPath, client, discoverFn, hostname, fetchStatus }`. Subcomandos:
    - `discover` → uma linha por gadget: `<id>\t<name>\t<addr>`; nenhum → `No Miblo gadgets found on this network.` (code 0)
    - `pair <addr|ip> <code>` → `Paired with <name> (<id>) at <addr>.`; código errado → code 2, `Wrong pairing code.`; IP sem porta recebe `:80`
    - `status` → JSON `{ bridge: 'running'|'stopped', statusline: 'linked'|'not linked', devices, sessions, usage }`
    - `mode <overview|limits|sessions> [id]` → `Mode set to <mode> on <n> gadget(s).`; modo inválido → code 2
    - `reset <id>` → `Factory reset sent to <name>.` e remove o pareamento
    - `link-statusline` / `unlink-statusline` → `Statusline linked.` / `Statusline already linked.` / `Statusline unlinked.` / `Statusline was not linked.`
    - desconhecido → code 2 com a lista de uso
  - `onboardMessage({ store, stampFile, now }) → string|null`: mensagem JSON `{"systemMessage": ...}` se não há gadgets pareados e o último lembrete foi há mais de 24h.

- [ ] **Step 1: Write the failing tests**

`plugin/test/miblo-cli.test.js`:

```js
import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { startFakeDevice } from './fakes/fake-device.js';
import { DeviceClient } from '../lib/device-client.js';
import { DeviceStore } from '../lib/device-store.js';
import { run } from '../bin/miblo.js';

const pluginRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

function deps(extra = {}) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-cli-'));
  return {
    dataDir: path.join(root, 'data'),
    pluginRoot,
    settingsPath: path.join(root, 'settings.json'),
    client: new DeviceClient(),
    discoverFn: async () => [],
    hostname: 'test-host',
    fetchStatus: async () => null,
    ...extra,
  };
}

test('discover lists gadgets or says none were found', async () => {
  assert.match((await run(['discover'], deps())).out, /No Miblo gadgets found/);
  const d = deps({ discoverFn: async () => [{ id: 'g1', name: 'Miblo-4F2A', addr: '10.0.0.5:80' }] });
  assert.equal((await run(['discover'], d)).out.trim(), 'g1\tMiblo-4F2A\t10.0.0.5:80');
});

test('pair stores the device; wrong code returns 2', async () => {
  const dev = await startFakeDevice();
  const d = deps();
  try {
    const bad = await run(['pair', dev.addr, '0000'], d);
    assert.equal(bad.code, 2);
    assert.match(bad.out, /Wrong pairing code/);

    const ok = await run(['pair', dev.addr, '4827'], d);
    assert.equal(ok.code, 0);
    assert.match(ok.out, /Paired with Miblo-4F2A \(miblo-4f2a\)/);
    assert.ok(!ok.out.includes(dev.state.token));
    const [saved] = new DeviceStore(d.dataDir).list();
    assert.equal(saved.token, dev.state.token);
  } finally {
    await dev.close();
  }
});

test('pair adds :80 to a bare IP', async () => {
  const seen = [];
  const client = { info: async (addr) => { seen.push(addr); return { id: 'g', name: 'G' }; }, pair: async () => 'tok' };
  await run(['pair', '192.168.0.42', '1234'], deps({ client }));
  assert.equal(seen[0], '192.168.0.42:80');
});

test('mode sends config to all or to one gadget; invalid mode returns 2', async () => {
  const dev = await startFakeDevice();
  const d = deps();
  try {
    await run(['pair', dev.addr, '4827'], d);
    assert.equal((await run(['mode', 'banana'], d)).code, 2);
    const r = await run(['mode', 'limits'], d);
    assert.match(r.out, /Mode set to limits on 1 gadget/);
    assert.equal(dev.state.config.mode, 'limits');
  } finally {
    await dev.close();
  }
});

test('reset sends the command and forgets the pairing', async () => {
  const dev = await startFakeDevice();
  const d = deps();
  try {
    await run(['pair', dev.addr, '4827'], d);
    const r = await run(['reset', 'miblo-4f2a'], d);
    assert.match(r.out, /Factory reset sent to Miblo-4F2A/);
    assert.equal(dev.state.resets, 1);
    assert.deepEqual(new DeviceStore(d.dataDir).list(), []);
  } finally {
    await dev.close();
  }
});

test('link and unlink the statusline', async () => {
  const d = deps();
  assert.equal((await run(['link-statusline'], d)).out.trim(), 'Statusline linked.');
  assert.equal((await run(['link-statusline'], d)).out.trim(), 'Statusline already linked.');
  assert.equal((await run(['unlink-statusline'], d)).out.trim(), 'Statusline unlinked.');
  assert.equal((await run(['unlink-statusline'], d)).out.trim(), 'Statusline was not linked.');
});

test('status reports bridge state, statusline and devices without tokens', async () => {
  const d = deps();
  new DeviceStore(d.dataDir).upsert({ id: 'g1', name: 'G1', addr: 'x:80', token: 'secret-token' });
  const r = await run(['status'], d);
  assert.ok(!r.out.includes('secret-token'));
  const s = JSON.parse(r.out);
  assert.equal(s.bridge, 'stopped');
  assert.equal(s.statusline, 'not linked');
  assert.equal(s.devices[0].id, 'g1');
});

test('unknown subcommand prints usage with code 2', async () => {
  const r = await run(['wat'], deps());
  assert.equal(r.code, 2);
  assert.match(r.out, /Usage/);
});
```

`plugin/test/onboard.test.js`:

```js
import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { onboardMessage } from '../bin/onboard.js';

function setup(devices = []) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-onb-'));
  return { store: { list: () => devices }, stampFile: path.join(dir, 'onboard.json') };
}

test('nags once per 24h while nothing is paired', () => {
  const s = setup();
  const first = onboardMessage({ ...s, now: () => 0 });
  assert.match(JSON.parse(first).systemMessage, /\/miblo pair/);
  assert.equal(onboardMessage({ ...s, now: () => 3600_000 }), null);
  assert.ok(onboardMessage({ ...s, now: () => 24 * 3600_000 + 1 }));
});

test('silent when a gadget is paired', () => {
  assert.equal(onboardMessage({ ...setup([{ id: 'g' }]), now: () => 0 }), null);
});
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd plugin && node --test test/miblo-cli.test.js test/onboard.test.js`
Expected: FAIL — modules not found

- [ ] **Step 3: Implement** — `plugin/bin/miblo.js`

```js
#!/usr/bin/env node
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { PORT, HOST, claudeSettingsPath } from '../lib/constants.js';
import { DeviceClient } from '../lib/device-client.js';
import { DeviceStore } from '../lib/device-store.js';
import { discover } from '../lib/mdns.js';
import { link, unlink, isLinked } from '../lib/statusline-link.js';

const MODES = ['overview', 'limits', 'sessions'];
const USAGE = [
  'Usage: miblo.js --data <dir> <command>',
  '  discover',
  '  pair <ip[:port]> <code>',
  '  status',
  '  mode <overview|limits|sessions> [id]',
  '  reset <id>',
  '  link-statusline | unlink-statusline',
].join('\n');

const withPort = (addr) => (String(addr).includes(':') ? String(addr) : `${addr}:80`);

async function defaultFetchStatus() {
  try {
    const res = await fetch(`http://${HOST}:${PORT}/status`, { signal: AbortSignal.timeout(800) });
    return res.ok ? await res.json() : null;
  } catch {
    return null;
  }
}

export async function run(argv, deps) {
  const { dataDir, pluginRoot, settingsPath, client, discoverFn, hostname, fetchStatus } = deps;
  const store = new DeviceStore(dataDir);
  const [cmd, ...args] = argv;
  const ok = (out) => ({ code: 0, out: out + '\n' });
  const fail = (code, out) => ({ code, out: out + '\n' });

  switch (cmd) {
    case 'discover': {
      const found = await discoverFn();
      if (!found.length) return ok('No Miblo gadgets found on this network.');
      return ok(found.map((d) => `${d.id}\t${d.name}\t${d.addr}`).join('\n'));
    }
    case 'pair': {
      const [rawAddr, code] = args;
      if (!rawAddr || !code) return fail(2, USAGE);
      const addr = withPort(rawAddr);
      try {
        const info = await client.info(addr);
        const token = await client.pair(addr, code, hostname);
        store.upsert({ id: info.id, name: info.name, addr, token });
        return ok(`Paired with ${info.name} (${info.id}) at ${addr}.`);
      } catch (e) {
        if (e.status === 403) return fail(2, 'Wrong pairing code.');
        return fail(1, `Could not reach a Miblo gadget at ${addr}.`);
      }
    }
    case 'status': {
      const live = await fetchStatus();
      const devices = live?.devices ?? store.list().map(({ id, name, addr }) => ({ id, name, addr, online: null }));
      return ok(JSON.stringify({
        bridge: live ? 'running' : 'stopped',
        statusline: isLinked({ settingsPath, dataDir }) ? 'linked' : 'not linked',
        statuslineSeen: live?.statuslineSeen ?? false,
        devices,
        sessions: live?.sessions ?? [],
        usage: live?.usage ?? null,
      }, null, 2));
    }
    case 'mode': {
      const [mode, id] = args;
      if (!MODES.includes(mode)) return fail(2, `Mode must be one of: ${MODES.join(', ')}.`);
      const targets = store.list().filter((d) => !id || d.id === id);
      let done = 0;
      for (const d of targets) {
        try {
          await client.setConfig(d.addr, d.token, { mode });
          done++;
        } catch {
          // gadget offline: reportado pela contagem
        }
      }
      return ok(`Mode set to ${mode} on ${done} gadget(s).`);
    }
    case 'reset': {
      const d = store.list().find((x) => x.id === args[0]);
      if (!d) return fail(2, `No paired gadget with id ${args[0]}.`);
      try {
        await client.reset(d.addr, d.token);
      } catch {
        return fail(1, `Could not reach ${d.name}.`);
      }
      store.remove(d.id);
      return ok(`Factory reset sent to ${d.name}.`);
    }
    case 'link-statusline': {
      const r = link({ settingsPath, dataDir, pluginRoot });
      return ok(r.changed ? 'Statusline linked.' : 'Statusline already linked.');
    }
    case 'unlink-statusline': {
      const r = unlink({ settingsPath, dataDir });
      return ok(r.changed ? 'Statusline unlinked.' : 'Statusline was not linked.');
    }
    default:
      return fail(2, USAGE);
  }
}

async function main() {
  const argv = process.argv.slice(2);
  let dataDir = process.env.CLAUDE_PLUGIN_DATA || path.join(os.homedir(), '.miblo');
  const i = argv.indexOf('--data');
  if (i >= 0) {
    dataDir = argv[i + 1];
    argv.splice(i, 2);
  }
  const here = path.dirname(fileURLToPath(import.meta.url));
  const r = await run(argv, {
    dataDir,
    pluginRoot: path.resolve(here, '..'),
    settingsPath: claudeSettingsPath(),
    client: new DeviceClient(),
    discoverFn: () => discover(),
    hostname: os.hostname(),
    fetchStatus: defaultFetchStatus,
  });
  process.stdout.write(r.out);
  process.exitCode = r.code;
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  main().catch((e) => {
    process.stdout.write(`Error: ${e.message}\n`);
    process.exitCode = 1;
  });
}
```

- [ ] **Step 4: Implement** — `plugin/bin/onboard.js`

```js
#!/usr/bin/env node
// SessionStart síncrono: lembra, no máximo 1x/24h, de parear um gadget.
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { DeviceStore } from '../lib/device-store.js';

const DAY_MS = 24 * 3600_000;

export function onboardMessage({ store, stampFile, now = () => Date.now() }) {
  if (store.list().length > 0) return null;
  let last = -Infinity;
  try {
    last = JSON.parse(fs.readFileSync(stampFile, 'utf8')).last;
  } catch {
    last = -Infinity;
  }
  if (now() - last <= DAY_MS) return null;
  fs.mkdirSync(path.dirname(stampFile), { recursive: true });
  fs.writeFileSync(stampFile, JSON.stringify({ last: now() }));
  return JSON.stringify({ systemMessage: 'Miblo: no desk gadget paired yet. Run /miblo pair to connect it.' });
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  try {
    const dataDir = process.env.CLAUDE_PLUGIN_DATA;
    if (dataDir) {
      const msg = onboardMessage({ store: new DeviceStore(dataDir), stampFile: path.join(dataDir, 'onboard.json') });
      if (msg) process.stdout.write(msg);
    }
  } catch {
    // nunca atrapalhar o início da sessão
  }
  process.exitCode = 0;
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cd plugin && node --test test/miblo-cli.test.js test/onboard.test.js`
Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add plugin/bin/miblo.js plugin/bin/onboard.js plugin/test/miblo-cli.test.js plugin/test/onboard.test.js
git commit -m "feat(plugin): miblo CLI (discover, pair, status, mode, reset, statusline) and onboarding"
```

---

### Task 12: Empacotamento do plugin e verificação real no Claude Code

**Files:**
- Create: `plugin/.claude-plugin/plugin.json`
- Create: `plugin/hooks/hooks.json`
- Create: `plugin/commands/miblo.md`
- Create: `.claude-plugin/marketplace.json`
- Create: `plugin/test/packaging.test.js`

**Interfaces:**
- Consumes: `bin/hook.js`, `bin/onboard.js`, `bin/miblo.js`.
- Produces: plugin `miblo` instalável por `/plugin marketplace add <owner>/<repo>` + `/plugin install miblo@miblo`.

- [ ] **Step 1: Write the failing test** — `plugin/test/packaging.test.js`

```js
import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const read = (p) => JSON.parse(fs.readFileSync(path.join(root, p), 'utf8'));
const EVENTS = ['SessionStart', 'UserPromptSubmit', 'PreToolUse', 'PermissionRequest', 'PostToolUse', 'Notification', 'Stop', 'SessionEnd'];

test('manifest and marketplace agree on the plugin name', () => {
  assert.equal(read('.claude-plugin/plugin.json').name, 'miblo');
  const mk = read('../.claude-plugin/marketplace.json');
  assert.equal(mk.plugins[0].name, 'miblo');
  assert.equal(mk.plugins[0].source, './plugin');
});

test('every tracked event runs hook.js asynchronously', () => {
  const { hooks } = read('hooks/hooks.json');
  for (const ev of EVENTS) {
    const cmds = hooks[ev].flatMap((m) => m.hooks);
    const h = cmds.find((c) => c.command.includes('bin/hook.js'));
    assert.ok(h, ev);
    assert.equal(h.async, true, ev);
  }
  assert.equal(hooks.Notification[0].matcher, 'elicitation_dialog');
  const onboard = hooks.SessionStart.flatMap((m) => m.hooks).find((c) => c.command.includes('bin/onboard.js'));
  assert.ok(onboard);
  assert.notEqual(onboard.async, true);
});

test('the /miblo command references the CLI with the data dir', () => {
  const md = fs.readFileSync(path.join(root, 'commands/miblo.md'), 'utf8');
  assert.match(md, /\$\{CLAUDE_PLUGIN_ROOT\}\/bin\/miblo\.js/);
  assert.match(md, /\$\{CLAUDE_PLUGIN_DATA\}/);
  assert.match(md, /\$ARGUMENTS/);
});
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd plugin && node --test test/packaging.test.js`
Expected: FAIL — `ENOENT ... plugin.json`

- [ ] **Step 3: `plugin/.claude-plugin/plugin.json`**

```json
{
  "name": "miblo",
  "version": "0.1.0",
  "displayName": "Miblo",
  "description": "Shows Claude Code session status, alerts and usage limits on a Miblo desk gadget.",
  "author": { "name": "Marcus Campos" }
}
```

- [ ] **Step 4: `.claude-plugin/marketplace.json`** (raiz do repositório)

```json
{
  "name": "miblo",
  "owner": { "name": "Marcus Campos" },
  "plugins": [
    {
      "name": "miblo",
      "source": "./plugin",
      "description": "Shows Claude Code session status, alerts and usage limits on a Miblo desk gadget."
    }
  ]
}
```

- [ ] **Step 5: `plugin/hooks/hooks.json`**

```json
{
  "hooks": {
    "SessionStart": [
      {
        "hooks": [
          { "type": "command", "command": "node \"${CLAUDE_PLUGIN_ROOT}/bin/hook.js\"", "async": true },
          { "type": "command", "command": "node \"${CLAUDE_PLUGIN_ROOT}/bin/onboard.js\"", "timeout": 5 }
        ]
      }
    ],
    "UserPromptSubmit": [
      { "hooks": [{ "type": "command", "command": "node \"${CLAUDE_PLUGIN_ROOT}/bin/hook.js\"", "async": true }] }
    ],
    "PreToolUse": [
      { "matcher": "*", "hooks": [{ "type": "command", "command": "node \"${CLAUDE_PLUGIN_ROOT}/bin/hook.js\"", "async": true }] }
    ],
    "PermissionRequest": [
      { "matcher": "*", "hooks": [{ "type": "command", "command": "node \"${CLAUDE_PLUGIN_ROOT}/bin/hook.js\"", "async": true }] }
    ],
    "PostToolUse": [
      { "matcher": "*", "hooks": [{ "type": "command", "command": "node \"${CLAUDE_PLUGIN_ROOT}/bin/hook.js\"", "async": true }] }
    ],
    "Notification": [
      { "matcher": "elicitation_dialog", "hooks": [{ "type": "command", "command": "node \"${CLAUDE_PLUGIN_ROOT}/bin/hook.js\"", "async": true }] }
    ],
    "Stop": [
      { "hooks": [{ "type": "command", "command": "node \"${CLAUDE_PLUGIN_ROOT}/bin/hook.js\"", "async": true }] }
    ],
    "SessionEnd": [
      { "hooks": [{ "type": "command", "command": "node \"${CLAUDE_PLUGIN_ROOT}/bin/hook.js\"", "async": true }] }
    ]
  }
}
```

- [ ] **Step 6: `plugin/commands/miblo.md`**

````markdown
---
description: Pair and manage Miblo desk gadgets (pair, status, mode, link-statusline, unlink-statusline, reset)
argument-hint: "[pair [ip] | status | mode <overview|limits|sessions> [id] | link-statusline | unlink-statusline | reset <id>]"
allowed-tools: Bash(node:*), AskUserQuestion
---

You manage Miblo desk gadgets with this CLI (call it `MIBLO` below):

```
node "${CLAUDE_PLUGIN_ROOT}/bin/miblo.js" --data "${CLAUDE_PLUGIN_DATA}"
```

Arguments: `$ARGUMENTS`

Rules: reply in the user's language; keep replies short; never show pairing tokens; run only the commands below.

## No arguments, or `pair [ip]`

1. If an IP was given, use it as the address. Otherwise run `MIBLO discover`.
   - No gadget found: tell the user to check that the gadget shows a 4-digit code on screen and is on the same network, and that on WSL2 or corporate networks they can run `/miblo pair <ip>` with the IP shown at the bottom of the gadget screen. Stop.
   - More than one: ask which one (AskUserQuestion, one option per gadget name).
2. Ask for the 4-digit code shown on the gadget screen.
3. Run `MIBLO pair <address> <code>`. If it prints `Wrong pairing code.`, ask again (max 3 attempts).
4. Ask for consent to link the status line, explaining in one sentence: "To show your 5h/weekly limits and token usage, Miblo reads the official status line data. Your current status line keeps working exactly the same; you can undo with /miblo unlink-statusline." If yes, run `MIBLO link-statusline`.
5. Confirm success and say the gadget will update on the next Claude Code activity.

## `status`

Run `MIBLO status` and summarize: bridge running or stopped, status line linked or not (if linked but `statuslineSeen` is false, say limits appear after the next response), each gadget online/offline, active sessions and limits.

## `mode <overview|limits|sessions> [id]`

Run `MIBLO mode <mode> [id]` and report the result.

## `link-statusline` / `unlink-statusline`

Run the matching command and report the result.

## `reset <id>`

Confirm with the user first (it erases the gadget's Wi-Fi and pairing). Then run `MIBLO reset <id>`.
````

- [ ] **Step 7: Run tests to verify they pass**

Run: `cd plugin && npm test`
Expected: PASS (toda a suíte)

- [ ] **Step 8: Validate the plugin with Claude Code**

Run: `claude plugin validate ./plugin`
Expected: nenhum erro. Se o validador rejeitar algum campo (ex.: `displayName`), remover o campo apontado e repetir.

- [ ] **Step 9: Manual end-to-end check with the fake device**

Em um terminal, suba um gadget falso numa porta fixa:

```bash
cd plugin && node -e "import('./test/fakes/fake-device.js').then(async m => { const d = await m.startFakeDevice(); console.log(d.addr); setInterval(() => { const s = d.state.snapshots.at(-1); if (s) console.log(JSON.stringify(s.sessions.map(x => [x.name, x.st, x.tool, x.det]))); }, 2000); })"
```

Em outro terminal, abra o Claude Code com o plugin local: `claude --plugin-dir ./plugin`. Nele:
1. `/miblo pair <addr impresso acima>` → código `4827` → aceitar o encadeamento da status line. Expected: "Paired with Miblo-4F2A…"; a status line original continua aparecendo igual.
2. Pedir algo que rode um comando Bash que exija permissão. Expected: o terminal do gadget falso imprime a sessão com `perm` e `Bash`.
3. Aprovar, esperar terminar. Expected: `done`.
4. `/miblo status`. Expected: bridge running, status line linked, gadget online, limites presentes (conta Pro/Max) após a primeira resposta.
5. `/miblo unlink-statusline`. Expected: `~/.claude/settings.json` volta ao `statusLine` original.

Se `${CLAUDE_PLUGIN_DATA}` não for substituído dentro do `commands/miblo.md` (o CLI imprimir caminhos com `${CLAUDE_PLUGIN_DATA}` literal), trocar a linha do CLI por `node "${CLAUDE_PLUGIN_ROOT}/bin/miblo.js" --data "$HOME/.claude/plugins/data/miblo-miblo"` e registrar o achado em `plugin/README.md`.

- [ ] **Step 10: Commit**

```bash
git add plugin/.claude-plugin/plugin.json plugin/hooks/hooks.json plugin/commands/miblo.md .claude-plugin/marketplace.json plugin/test/packaging.test.js
git commit -m "feat(plugin): package miblo plugin (hooks, /miblo command, marketplace)"
```

**Critério de conclusão do Plano 1:** `npm test` verde em `plugin/`; `claude plugin validate` sem erros; roteiro manual do Step 9 completo contra o gadget falso.
