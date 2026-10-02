// When the 5-hour limit runs out at the current pace: a straight line fitted to the last hour of
// readings from the status line. The gadget shows it on the Limits screen ("at this pace, ends at
// 15:40") and the CLI in /miblo:limits; with no forecast (pace unknown, flat, or the window resets
// first) the field is simply left out.

const FIT_WINDOW_SEC = 3600;     // only the last hour sets the pace
const MIN_SPAN_SEC = 600;        // less than 10 min of data is not a pace yet
const MIN_DISTINCT = 3;          // two values make a line out of a single step
const KEEP_MS = 2 * 3600_000;    // readings kept in memory
const SAMPLE_EVERY_MS = 60_000;  // an unchanged percentage is kept at most once a minute
const RESET_DROP = 5;            // a fall of more than this many points means a new window
const RESET_MOVE_SEC = 60;       // a reset time that moves more than this means a new window

// Pure: least-squares pace over the last hour of samples [{t (epoch s), pct}] -> epoch s or null.
// null when fewer than 10 min of data, fewer than 3 distinct values, not rising, already at 100,
// or the window resets first.
export function forecastEta(samples, nowSec, resetSec) {
  const s = (samples ?? []).filter((x) => x.t >= nowSec - FIT_WINDOW_SEC && x.t <= nowSec);
  if (s.length < 2) return null;
  const last = s[s.length - 1];
  if (last.t - s[0].t < MIN_SPAN_SEC) return null;
  if (new Set(s.map((x) => x.pct)).size < MIN_DISTINCT) return null;
  if (last.pct >= 100) return null;
  // Times relative to the first sample keep the sums small and exact.
  const t0 = s[0].t;
  const n = s.length;
  let st = 0, sp = 0, stt = 0, stp = 0;
  for (const x of s) {
    const t = x.t - t0;
    st += t;
    sp += x.pct;
    stt += t * t;
    stp += t * x.pct;
  }
  const den = n * stt - st * st;
  if (den <= 0) return null;
  const slope = (n * stp - st * sp) / den;  // points per second
  if (!(slope > 0)) return null;
  const eta = Math.round(nowSec + (100 - last.pct) / slope);
  if (resetSec && eta >= resetSec) return null;
  return eta;
}

export class LimitForecast {
  #samples = [];  // [{ t (epoch s), pct }], oldest first
  #lastMs = null; // when the newest sample was kept
  #reset = null;  // the window's reset time (epoch s) the samples belong to

  constructor({ now = () => Date.now() } = {}) {
    this.now = now;
  }

  // h5 = metrics.usage()?.h5 ({ pct, reset }) or undefined. Called on every status line reading.
  observe(h5) {
    const ms = this.now();
    this.#prune(ms);
    if (!h5 || !Number.isFinite(h5.pct)) return;
    const reset = Number.isFinite(h5.reset) ? h5.reset : null;
    const prev = this.#samples.at(-1);
    const moved = reset !== null && this.#reset !== null && Math.abs(reset - this.#reset) > RESET_MOVE_SEC;
    if (moved || (prev && prev.pct - h5.pct > RESET_DROP)) {
      this.#samples = [];
      this.#lastMs = null;
    }
    if (reset !== null) this.#reset = reset;
    const last = this.#samples.at(-1);
    if (last && last.pct === h5.pct && ms - this.#lastMs < SAMPLE_EVERY_MS) return;
    this.#samples.push({ t: Math.floor(ms / 1000), pct: h5.pct });
    this.#lastMs = ms;
  }

  // Epoch s when the 5-hour window reaches 100% at the current pace, or null.
  eta() {
    const ms = this.now();
    this.#prune(ms);
    return forecastEta(this.#samples, Math.floor(ms / 1000), this.#reset);
  }

  #prune(ms) {
    const cutoff = Math.floor((ms - KEEP_MS) / 1000);
    let i = 0;
    while (i < this.#samples.length && this.#samples[i].t < cutoff) i++;
    if (i) this.#samples.splice(0, i);
  }
}
