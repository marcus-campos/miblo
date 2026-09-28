import { describeTool } from './describe-tool.js';
import { ALERT_TTL_MS, SESSION_TTL_MS, WORKER_TTL_MS } from './constants.js';

const PRIORITY = { perm: 0, question: 1, done: 2, running: 3, idle: 4 };
const ALERTING = new Set(['perm', 'question', 'done']);
// background_tasks entries are documented as in flight; drop any finished one defensively.
const FINISHED = new Set(['completed', 'failed', 'killed', 'stopped', 'cancelled']);

const plural = (n, word) => `${n} ${word}${n === 1 ? '' : 's'}`;

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
  // session id -> the "waiting N agents" detail shown while its Stop waits.
  #waitDet = new Map();

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
    if (evt.pid !== undefined && evt.pid !== null) s.pid = evt.pid;

    const agentId = typeof evt.agent_id === 'string' && evt.agent_id ? evt.agent_id : null;
    if (name === 'SubagentStart' || name === 'SubagentStop' || agentId) {
      this.#subagentEvent(s, name, agentId, evt);
      return created || JSON.stringify(s) !== before;
    }
    // Any main-thread event other than Stop means the main agent is working again.
    if (name !== 'Stop') s.waiting = false;
    s.permBy = null;

    switch (name) {
      case 'SessionStart':
        this.#started.add(id);
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
      case 'Stop': {
        // The main agent may end its turn only to wait for background work that
        // will wake it up again: that is not "finished".
        const wait = this.#pending(s.id, evt);
        if (wait) {
          s.waiting = true;
          this.#enter(s, 'running');
          s.tool = 'Agent';
          s.det = wait;
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
    return created || JSON.stringify(s) !== before;
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
      s = { id, name, st: 'idle', since: this.now(), tool: '', det: '', pid: null, waiting: false, permBy: null };
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
    } else if (s.st === 'perm' && s.permBy === agentId) {
      s.permBy = null;
      this.#enter(s, 'running');
      if (s.waiting) Object.assign(s, { tool: 'Agent', det: this.#waitDet.get(s.id) ?? '' });
    }
  }

  // Plain-English description of the background work a Stop waits on, or '' if none.
  // Stop's background_tasks (the task registry) is authoritative when present;
  // older Claude Code versions lack it, so fall back to the tracked subagents.
  #pending(sid, evt) {
    if (Array.isArray(evt.background_tasks)) {
      const tasks = evt.background_tasks.filter((t) => !FINISHED.has(t?.status));
      if (tasks.length === 0) return '';
      const agentsOnly = tasks.every((t) => t?.type === 'subagent');
      return `waiting ${plural(tasks.length, agentsOnly ? 'agent' : 'task')}`;
    }
    const n = this.#liveWorkers(sid);
    return n ? `waiting ${plural(n, 'agent')}` : '';
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
    this.#lastSeen.delete(id);
    this.#alerts = this.#alerts.filter((a) => a.sid !== id);
    return this.#sessions.delete(id);
  }
}
