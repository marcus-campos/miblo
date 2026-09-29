// Today's work for the gadget's daily summary: how many responses finished (a session going from
// running to done) and how long at least one session was working. Kept in the data dir, so the
// bridge exiting after a quiet spell (or a new Claude Code window starting another one) doesn't
// lose the day's count; both reset at local midnight.
import fs from 'node:fs';
import path from 'node:path';

const dayKey = (ms) => {
  const d = new Date(ms);
  return `${d.getFullYear()}-${d.getMonth() + 1}-${d.getDate()}`;
};
const startOfDay = (ms) => {
  const d = new Date(ms);
  return new Date(d.getFullYear(), d.getMonth(), d.getDate()).getTime();
};
// Longest gap counted as work between two observations: the bridge pushes a heartbeat every 10 s,
// so a longer gap means it was stopped (asleep laptop...), not that work went on.
const MAX_STEP_MS = 60_000;

export class DayStats {
  #file;
  #day;
  #turns = 0;
  #workMs = 0;
  #states = new Map();  // session id -> last seen state
  #lastMs = null;       // last observation while something was running
  #dirty = false;

  constructor({ dataDir, now = () => Date.now() } = {}) {
    this.now = now;
    this.#file = dataDir ? path.join(dataDir, 'day-stats.json') : null;
    this.#day = dayKey(now());
    this.#load();
  }

  // Called with the tracker's sessions on every push (events and heartbeat).
  observe(sessions) {
    const t = this.now();
    this.#roll(t);
    let running = false;
    const seen = new Set();
    for (const s of sessions) {
      seen.add(s.id);
      if (s.st === 'done' && this.#states.get(s.id) === 'running') {
        this.#turns += 1;
        this.#dirty = true;
      }
      this.#states.set(s.id, s.st);
      if (s.st === 'running') running = true;
    }
    for (const id of this.#states.keys()) if (!seen.has(id)) this.#states.delete(id);
    if (this.#lastMs !== null) {
      const step = t - this.#lastMs;
      if (step > 0 && step <= MAX_STEP_MS) {
        this.#workMs += step;
        this.#dirty = true;
      }
    }
    this.#lastMs = running ? t : null;
    this.#save();
  }

  // { turns, work } where work is in whole seconds.
  today() {
    this.#roll(this.now());
    return { turns: this.#turns, work: Math.floor(this.#workMs / 1000) };
  }

  #roll(t) {
    const key = dayKey(t);
    if (key === this.#day) return;
    this.#day = key;
    this.#turns = 0;
    this.#workMs = 0;
    // Work that straddles midnight only counts from 00:00 on the new day.
    if (this.#lastMs !== null) this.#lastMs = Math.max(this.#lastMs, startOfDay(t));
    this.#dirty = true;
  }

  #load() {
    if (!this.#file) return;
    try {
      const d = JSON.parse(fs.readFileSync(this.#file, 'utf8'));
      if (d?.day === this.#day) {
        this.#turns = Number.isInteger(d.turns) && d.turns >= 0 ? d.turns : 0;
        this.#workMs = Number.isFinite(d.workMs) && d.workMs >= 0 ? d.workMs : 0;
      }
    } catch {
      // first run of the day (or an unreadable file): start from zero
    }
  }

  #save() {
    if (!this.#file || !this.#dirty) return;
    this.#dirty = false;
    try {
      fs.mkdirSync(path.dirname(this.#file), { recursive: true });
      const tmp = `${this.#file}.${process.pid}.tmp`;
      fs.writeFileSync(tmp, JSON.stringify({ day: this.#day, turns: this.#turns, workMs: this.#workMs }));
      fs.renameSync(tmp, this.#file);
    } catch {
      // the summary is a nicety: never let it break a push
    }
  }
}
