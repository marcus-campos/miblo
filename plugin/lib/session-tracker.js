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
