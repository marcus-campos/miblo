import { describeTool } from './describe-tool.js';
import { ALERT_TTL_MS, SESSION_TTL_MS, WORKER_TTL_MS } from './constants.js';

// Display order: waiting on the user, then still working, then finished, then idle.
const PRIORITY = { perm: 0, question: 1, running: 2, done: 3, idle: 4 };
const ALERTING = new Set(['perm', 'question', 'done']);
// background_tasks entries are documented as in flight; drop any finished one defensively.
const FINISHED = new Set(['completed', 'failed', 'killed', 'stopped', 'cancelled']);
// Watchers that live on after the turn without the session doing anything (Claude Code's monitor
// of a published artifact, for one, runs for hours): never counted as work in flight. The payload
// cannot tell them from a monitor the agent started to wait for (a CI run): that one reads as done
// too, and the session turns back to running as soon as the monitor wakes it.
const PASSIVE = new Set(['monitor']);

// Reserved activity tools for a Stop that waits on background work (det = the count).
export const WAIT_AGENTS = '_wait_agents';
export const WAIT_TASKS = '_wait_tasks';
// Reserved activity tool while Claude Code compacts the conversation (det empty).
export const COMPACT = '_compact';
// permBy of a permission prompt whose owner is unknown (a permission_prompt Notification without
// agent_id while subagents run): the next event of the main thread or of any subagent clears it.
const ANY_AGENT = '*';

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
  #started = new Set();
  #lastSeen = new Map();
  // session id -> Map(agent_id -> last event time) of its running subagents.
  #workers = new Map();
  // session id -> the { tool, det } activity shown while its Stop waits (see #pending).
  #waitDet = new Map();
  // session id -> compaction trigger ('manual' | 'auto') while one is in progress.
  #compacting = new Map();

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
    this.#lastSeen.set(id, this.now());
    const before = JSON.stringify(s);
    const activity = [s.tool, s.det];
    if (evt.pid !== undefined && evt.pid !== null) s.pid = evt.pid;

    const agentId = typeof evt.agent_id === 'string' && evt.agent_id ? evt.agent_id : null;
    if (name === 'Notification' && evt.notification_type === 'permission_prompt') {
      this.#permPrompt(s, agentId, evt.tool_name);
      if (s.tool !== activity[0] || s.det !== activity[1]) s.cmdLive = false;
      this.#markTool(s, activity, false);
      return created || JSON.stringify(s) !== before;
    }
    if (name === 'SubagentStart' || name === 'SubagentStop' || agentId) {
      this.#subagentEvent(s, name, agentId, evt);
      // A subagent that changes the activity shown (its permission prompt) replaces the main
      // thread's command, and the gadget never sees when the subagent's command runs or ends.
      if (s.tool !== activity[0] || s.det !== activity[1]) s.cmdLive = false;
      this.#markTool(s, activity, false);
      return created || JSON.stringify(s) !== before;
    }
    // Any main-thread event other than Stop means the main agent is working again.
    if (name !== 'Stop') s.waiting = false;
    s.permBy = null;

    switch (name) {
      case 'SessionStart':
        this.#started.add(id);
        // A compacted session restarts with source "compact": the compaction is over.
        if (evt.source === 'compact') this.#compactDone(s);
        break;
      case 'PreCompact':
        this.#compacting.set(s.id, evt.trigger === 'manual' ? 'manual' : 'auto');
        this.#enter(s, 'running', { alert: false });
        s.tool = COMPACT;
        s.det = '';
        break;
      case 'PostCompact':
        this.#compactDone(s);
        break;
      case 'UserPromptSubmit':
        this.#compacting.delete(s.id);  // a compaction that never reported its end
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
      case 'PostToolUseFailure':  // the tool failed (a command exiting non-zero, an interrupt)
        this.#enter(s, 'running');
        break;
      case 'Stop': {
        // The main agent may end its turn only to wait for background work that
        // will wake it up again: that is not "finished".
        const wait = this.#pending(s.id, evt);
        if (wait) {
          s.waiting = true;
          this.#enter(s, 'running');
          Object.assign(s, wait);
          this.#waitDet.set(s.id, wait);
        } else {
          s.waiting = false;
          this.#workers.delete(s.id);
          this.#enter(s, 'done');
        }
        break;
      }
      default:
        return created;
    }
    // A main-thread PreToolUse is always a new tool call, even one repeating the last command;
    // its PermissionRequest (same call) only moves the mark if it names something else.
    this.#markTool(s, activity, name === 'PreToolUse');
    // cmdLive: a main-thread shell command is in flight (from its PreToolUse until the next
    // main-thread event, normally its PostToolUse or PostToolUseFailure), so the gadget's command
    // timer stops when the command does, not when Claude moves on.
    s.cmdLive = (name === 'PreToolUse' || name === 'PermissionRequest') && s.tool === 'Bash';
    return created || JSON.stringify(s) !== before;
  }

  // toolSince = when the activity shown (tool, det) started: the gadget times long commands from it.
  #markTool(s, [tool, det], fresh) {
    if (fresh || s.tool !== tool || s.det !== det) s.toolSince = this.now();
  }

  sweep() {
    let changed = false;
    for (const s of [...this.#sessions.values()]) {
      const idleFor = this.now() - (this.#lastSeen.get(s.id) ?? 0);
      if (idleFor > SESSION_TTL_MS || (s.pid && !this.isAlive(s.pid))) {
        changed = this.#remove(s.id) || changed;
        continue;
      }
      this.#liveWorkers(s.id);
      // Waiting on background work that never reported back: give up quietly.
      if (s.waiting && idleFor > WORKER_TTL_MS) {
        s.waiting = false;
        this.#workers.delete(s.id);
        this.#enter(s, 'done', { alert: false });
        changed = true;
      }
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

  // True iff a SessionStart was handled for this session (its cost starts at zero here).
  sawStart(sid) {
    return this.#started.has(sid);
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
      s = { id, name, st: 'idle', since: this.now(), tool: '', det: '', toolSince: this.now(), cmdLive: false, pid: null, waiting: false, permBy: null };
      this.#sessions.set(id, s);
    }
    return s;
  }

  // Events from inside a subagent (they carry agent_id) and the subagent
  // lifecycle only track background workers. They never move the main session
  // to question/done; a subagent's permission prompt is shown to the user, so
  // it does raise perm, and that subagent's next event clears it.
  #subagentEvent(s, name, agentId, evt) {
    if (!agentId) return;
    if (name === 'SubagentStop') {
      this.#workers.get(s.id)?.delete(agentId);
      return;
    }
    // Claude Code's own internal agents report an empty agent_type.
    if (name === 'SubagentStart' && evt.agent_type === '') return;
    let w = this.#workers.get(s.id);
    if (!w) this.#workers.set(s.id, (w = new Map()));
    w.set(agentId, this.now());
    if (name === 'PermissionRequest') {
      Object.assign(s, describeTool(evt.tool_name, evt.tool_input));
      s.permBy = agentId;
      this.#enter(s, 'perm');
    } else if (s.st === 'perm' && (s.permBy === agentId || s.permBy === ANY_AGENT)) {
      s.permBy = null;
      this.#enter(s, 'running');
      if (s.waiting) Object.assign(s, this.#waitDet.get(s.id) ?? { tool: WAIT_AGENTS, det: '' });
    }
  }

  // Claude Code shows a permission prompt (permission_prompt Notification). Auto mode's classifier
  // can escalate a call to the user without a PermissionRequest reaching the hooks, so this alone
  // raises perm; when the PermissionRequest does come too (before or after), the session is
  // already in perm and alerts once. The notification names no command: the activity shown stays
  // the last known one unless the message named another tool. It never touches s.waiting: the
  // prompt may well come from a subagent while the main agent waits.
  #permPrompt(s, agentId, tool) {
    if (agentId) {
      let w = this.#workers.get(s.id);
      if (!w) this.#workers.set(s.id, (w = new Map()));
      w.set(agentId, this.now());
    }
    if (s.st === 'perm') {
      if (agentId && s.permBy === ANY_AGENT) s.permBy = agentId;
      return;
    }
    if (typeof tool === 'string' && tool && tool !== s.tool) {
      s.tool = tool;
      s.det = '';
    } else if (!tool && s.tool.startsWith('_')) {
      // A reserved activity (waiting on agents, compacting) is not the tool asking.
      s.tool = '';
      s.det = '';
    }
    // Without agent_id the prompt is the main thread's unless subagents are running.
    s.permBy = agentId ?? (this.#liveWorkers(s.id) ? ANY_AGENT : null);
    this.#enter(s, 'perm');
  }

  // Structured activity for the background work a Stop waits on, or null if none:
  // { tool: '_wait_agents' | '_wait_tasks', det: '<count>' }. The gadget localizes it
  // ("aguardando 2 agentes"), so no English text goes over the wire.
  // Stop's background_tasks (the task registry) is authoritative when present;
  // older Claude Code versions lack it, so fall back to the tracked subagents.
  #pending(sid, evt) {
    if (Array.isArray(evt.background_tasks)) {
      const tasks = evt.background_tasks.filter((t) => !FINISHED.has(t?.status) && !PASSIVE.has(t?.type));
      if (tasks.length === 0) return null;
      const agentsOnly = tasks.every((t) => t?.type === 'subagent');
      return { tool: agentsOnly ? WAIT_AGENTS : WAIT_TASKS, det: String(tasks.length) };
    }
    const n = this.#liveWorkers(sid);
    return n ? { tool: WAIT_AGENTS, det: String(n) } : null;
  }

  // Ends a compaction: a manual /compact leaves the session waiting for the user (idle, no
  // "finished" alert); an automatic one happens mid-turn, so the agent keeps working.
  #compactDone(s) {
    const trigger = this.#compacting.get(s.id);
    if (!trigger) return;
    this.#compacting.delete(s.id);
    if (s.tool === COMPACT) {
      s.tool = '';
      s.det = '';
    }
    if (trigger === 'manual' && s.st === 'running') this.#enter(s, 'idle');
  }

  // Drops workers silent for WORKER_TTL_MS and returns how many are left.
  #liveWorkers(sid) {
    const w = this.#workers.get(sid);
    if (!w) return 0;
    for (const [id, at] of w) if (this.now() - at > WORKER_TTL_MS) w.delete(id);
    if (w.size === 0) this.#workers.delete(sid);
    return w.size;
  }

  #enter(s, st, { alert = true } = {}) {
    if (s.st === st) return;
    s.st = st;
    s.since = this.now();
    this.#alerts = this.#alerts.filter((a) => a.sid !== s.id);
    if (alert && ALERTING.has(st)) {
      this.#alerts.push({ id: this.#nextAlertId++, kind: st, sid: s.id, createdAt: this.now() });
    }
  }

  #remove(id) {
    this.#started.delete(id);
    this.#workers.delete(id);
    this.#waitDet.delete(id);
    this.#compacting.delete(id);
    this.#lastSeen.delete(id);
    this.#alerts = this.#alerts.filter((a) => a.sid !== id);
    return this.#sessions.delete(id);
  }
}
