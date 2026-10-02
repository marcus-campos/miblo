// Today's work for the gadget's daily summary: how many responses finished (a session going from
// running to done) and how long at least one session was working. Kept in the data dir, so the
// bridge exiting after a quiet spell (or a new Claude Code window starting another one) doesn't
// lose the day's count; both reset at local midnight. Past days are kept too (the last 14, with
// the day's cost) for Monday's summary of last week, and today's work per session name for
// /miblo:today.
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
const keyOf = (y, m, d) => dayKey(new Date(y, m, d).getTime());
const DAYS_KEPT = 14;
const count = (v) => (Number.isInteger(v) && v >= 0 ? v : 0);
const amount = (v) => (Number.isFinite(v) && v >= 0 ? v : 0);
// A past day as stored: { day: 'YYYY-M-D', turns, workMs, usd }, or null if unusable.
const pastDay = (d) => (typeof d?.day === 'string' && d.day
  ? { day: d.day, turns: count(d.turns), workMs: amount(d.workMs), usd: amount(d.usd) } : null);
// Longest gap counted as work between two observations: the bridge pushes a heartbeat every 10 s,
// so a longer gap means it was stopped (asleep laptop...), not that work went on.
const MAX_STEP_MS = 60_000;

export class DayStats {
  #file;
  #day;
  #turns = 0;
  #workMs = 0;
  #usd = 0;
  #byName = new Map();  // session name -> ms worked today
  #days = [];           // past days, oldest first (at most DAYS_KEPT)
  #states = new Map();  // session id -> last seen state
  #running = [];        // names of the sessions running at the last observation
  #lastMs = null;       // last observation while something was running
  #dirty = false;

  constructor({ dataDir, now = () => Date.now() } = {}) {
    this.now = now;
    this.#file = dataDir ? path.join(dataDir, 'day-stats.json') : null;
    this.#day = dayKey(now());
    this.#load();
  }

  // Called with the tracker's sessions on every push (events and heartbeat). `usd` is today's
  // cost so far (MetricsStore.today().usd), kept with the day for last week's summary.
  observe(sessions, { usd } = {}) {
    const t = this.now();
    this.#roll(t);
    if (Number.isFinite(usd) && usd >= 0 && usd !== this.#usd) {
      this.#usd = usd;
      this.#dirty = true;
    }
    const running = [];
    const seen = new Set();
    for (const s of sessions) {
      seen.add(s.id);
      if (s.st === 'done' && this.#states.get(s.id) === 'running') {
        this.#turns += 1;
        this.#dirty = true;
      }
      this.#states.set(s.id, s.st);
      if (s.st === 'running') running.push(String(s.name || s.id));
    }
    for (const id of this.#states.keys()) if (!seen.has(id)) this.#states.delete(id);
    if (this.#lastMs !== null) {
      const step = t - this.#lastMs;
      if (step > 0 && step <= MAX_STEP_MS) {
        this.#workMs += step;
        // The step goes to the sessions that were running through it, like the day's total.
        for (const name of this.#running) this.#byName.set(name, (this.#byName.get(name) ?? 0) + step);
        this.#dirty = true;
      }
    }
    this.#lastMs = running.length ? t : null;
    this.#running = running;
    this.#save();
  }

  // { turns, work } where work is in whole seconds.
  today() {
    this.#roll(this.now());
    return { turns: this.#turns, work: Math.floor(this.#workMs / 1000) };
  }

  // Today's sessions that worked the longest: [{ name, work }] (work in whole seconds), longest first.
  topSessions(n = 3) {
    this.#roll(this.now());
    return [...this.#byName]
      .map(([name, ms]) => ({ name, work: Math.floor(ms / 1000) }))
      .filter((s) => s.work > 0)
      .sort((a, b) => b.work - a.work || a.name.localeCompare(b.name))
      .slice(0, n);
  }

  // Last week's totals (Monday to Sunday, local time, before the current week):
  // { work (s), turns, usd, top } where top is the weekday (0 = Sunday .. 6) with the most work,
  // or null when no work was recorded. null when nothing at all was recorded that week.
  week() {
    this.#roll(this.now());
    const d = new Date(this.now());
    const monday = d.getDate() - ((d.getDay() + 6) % 7) - 7;
    const keys = new Map();  // day key -> weekday
    for (let i = 0; i < 7; i++) {
      const k = keyOf(d.getFullYear(), d.getMonth(), monday + i);
      keys.set(k, (i + 1) % 7);
    }
    let workMs = 0, turns = 0, usd = 0, top = null, topMs = 0, any = false;
    for (const p of this.#days) {
      if (!keys.has(p.day)) continue;
      if (!p.turns && !p.workMs && !p.usd) continue;
      any = true;
      workMs += p.workMs;
      turns += p.turns;
      usd += p.usd;
      if (p.workMs > topMs) {
        topMs = p.workMs;
        top = keys.get(p.day);
      }
    }
    if (!any) return null;
    return { work: Math.floor(workMs / 1000), turns, usd: Math.round(usd * 100) / 100, top };
  }

  #roll(t) {
    const key = dayKey(t);
    if (key === this.#day) return;
    this.#keep({ day: this.#day, turns: this.#turns, workMs: this.#workMs, usd: this.#usd });
    this.#day = key;
    this.#turns = 0;
    this.#workMs = 0;
    this.#usd = 0;
    this.#byName.clear();
    // Work that straddles midnight only counts from 00:00 on the new day.
    if (this.#lastMs !== null) this.#lastMs = Math.max(this.#lastMs, startOfDay(t));
    this.#dirty = true;
  }

  // Adds a finished day to the history (replacing one with the same key) and keeps the last 14.
  #keep(entry) {
    const p = pastDay(entry);
    if (!p) return;
    this.#days = this.#days.filter((x) => x.day !== p.day);
    this.#days.push(p);
    if (this.#days.length > DAYS_KEPT) this.#days.splice(0, this.#days.length - DAYS_KEPT);
  }

  // Reads { day, turns, workMs, usd, sessions: {name: ms}, days: [...] }, or the single-day
  // { day, turns, workMs } of older versions.
  #load() {
    if (!this.#file) return;
    try {
      const d = JSON.parse(fs.readFileSync(this.#file, 'utf8'));
      if (Array.isArray(d?.days)) for (const x of d.days) if (x?.day !== this.#day) this.#keep(x);
      if (d?.day === this.#day) {
        this.#turns = count(d.turns);
        this.#workMs = amount(d.workMs);
        this.#usd = amount(d.usd);
        if (d.sessions && typeof d.sessions === 'object') {
          for (const [name, ms] of Object.entries(d.sessions)) if (amount(ms) > 0) this.#byName.set(name, ms);
        }
      } else {
        // The file is from an earlier day (the bridge was not running at midnight).
        this.#keep(d);
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
      fs.writeFileSync(tmp, JSON.stringify({
        day: this.#day, turns: this.#turns, workMs: this.#workMs, usd: this.#usd,
        sessions: Object.fromEntries(this.#byName), days: this.#days,
      }));
      fs.renameSync(tmp, this.#file);
    } catch {
      // the summary is a nicety: never let it break a push
    }
  }
}
