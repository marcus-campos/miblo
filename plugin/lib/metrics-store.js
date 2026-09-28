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
