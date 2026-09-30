import fs from 'node:fs';
import path from 'node:path';

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
  #todayUsd = 0;

  #file = null;

  // With `dataDir`, the limits and today's cost survive a bridge restart (a plugin update, the
  // bridge exiting when idle): without it the gadget showed no limits and a cost back at $0 until
  // the next response.
  constructor({ now = () => Date.now(), dataDir = null } = {}) {
    this.now = now;
    this.#file = dataDir ? path.join(dataDir, 'metrics.json') : null;
    this.#load();
  }

  #load() {
    if (!this.#file) return;
    try {
      const d = JSON.parse(fs.readFileSync(this.#file, 'utf8'));
      for (const [, key] of WINDOWS) {
        const w = d?.limits?.[key];
        if (w && Number.isFinite(w.pct) && (w.reset === null || Number.isFinite(w.reset))) {
          this.#limits[key] = { pct: Math.max(0, Math.min(100, Math.round(w.pct))), reset: w.reset };
        }
      }
      if (typeof d?.day === 'string' && Number.isFinite(d.todayUsd) && d.todayUsd >= 0) {
        this.#day = d.day;
        this.#todayUsd = d.todayUsd;
      }
    } catch {
      // no file yet, or unreadable: start empty
    }
  }

  #save() {
    if (!this.#file) return;
    try {
      fs.mkdirSync(path.dirname(this.#file), { recursive: true });
      const tmp = `${this.#file}.${process.pid}.tmp`;
      fs.writeFileSync(tmp, JSON.stringify({ day: this.#day, todayUsd: this.#todayUsd, limits: this.#limits }));
      fs.renameSync(tmp, this.#file);
    } catch {
      // best effort: the next reading tries again
    }
  }

  // Per session, `tok` is the CURRENT context size (total_input_tokens +
  // total_output_tokens), not a running total. Only cost.total_cost_usd is a
  // running total, so "today" sums its positive deltas. A session's first
  // reading counts fully only when `fresh` (the bridge saw its SessionStart);
  // otherwise it only sets the baseline.
  ingest(sl, { fresh = false } = {}) {
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
    const prev = this.#per.get(sid);
    const baseUsd = prev ? prev.usd : fresh ? 0 : cur.usd;
    const addedUsd = Math.max(0, cur.usd - baseUsd);
    this.#todayUsd += addedUsd;
    this.#per.set(sid, cur);

    const before = JSON.stringify([this.#todayUsd, this.#limits]);
    const rl = sl.rate_limits;
    if (rl) {
      for (const [src, dst] of WINDOWS) {
        const w = rl[src];
        if (w && typeof w.used_percentage === 'number') {
          this.#limits[dst] = { pct: Math.round(w.used_percentage), reset: w.resets_at ?? null };
        }
      }
    }
    if (JSON.stringify([this.#todayUsd, this.#limits]) !== before || addedUsd) this.#save();
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
    return { usd: Math.round(this.#todayUsd * 100) / 100 };
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
      this.#todayUsd = 0;
    }
  }
}
