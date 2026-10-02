// Live time zone offsets for the gadgets. A gadget turns its zone (an IANA name) into local time
// with a table frozen into its firmware; the computer's time zone data is kept current by its
// updates, so the bridge works out, for each zone a gadget shows, the offset now, the next change
// within ~400 days and the offset after it, and sends them in the snapshot (`tz`, see
// withZones). The gadget uses them while they are fresh and falls back to its table otherwise.
//
// Two sources, whichever has the newer tz database: Node's own (Intl, ICU's copy, as old as the
// Node release) or the operating system's compiled zone files (macOS and Linux keep them current;
// Windows has none).
import fs from 'node:fs';
import path from 'node:path';

const DAY_S = 86400;
export const HORIZON_DAYS = 400;
export const MAX_ZONES = 2;  // the gadget's own zone and its second clock (Config::tz, tz2)
const CACHE_MS = 3600_000;
// An IANA name: never a path that could leave the zone directory (the name comes from a gadget).
const ZONE_RE = /^[A-Za-z][A-Za-z0-9_+-]*(\/[A-Za-z0-9_+-]+){0,2}$/;

export const isZoneName = (z) => typeof z === 'string' && z.length <= 47 && ZONE_RE.test(z) && !z.includes('..');

// --- Intl (Node's ICU data) ---------------------------------------------------------------

const fmts = new Map();
// Minutes east of UTC in `zone` at `ms` (epoch ms), from Intl; throws on an unknown zone.
export function intlOffset(zone, ms) {
  let f = fmts.get(zone);
  if (!f) {
    f = new Intl.DateTimeFormat('en-US', { timeZone: zone, timeZoneName: 'longOffset' });
    fmts.set(zone, f);
  }
  const name = f.formatToParts(new Date(ms)).find((p) => p.type === 'timeZoneName')?.value ?? '';
  const m = /^GMT(?:([+-])(\d{1,2})(?::(\d{2}))?(?::(\d{2}))?)?$/.exec(name);
  if (!m) throw new RangeError(`unexpected offset "${name}" for ${zone}`);
  if (!m[1]) return 0;
  const min = Number(m[2]) * 60 + Number(m[3] ?? 0) + Math.round(Number(m[4] ?? 0) / 60);
  return m[1] === '-' ? -min : min;
}

// --- POSIX TZ rules (the footer of a TZif file) --------------------------------------------

const secs = (s) => {
  const sign = s.startsWith('-') ? -1 : 1;
  const [h, m = 0, x = 0] = s.replace(/^[+-]/, '').split(':').map(Number);
  return sign * (h * 3600 + m * 60 + x);
};
const NAME = '(?:<[^>]+>|[A-Za-z]{3,})';
const OFF = '[+-]?\\d+(?::\\d+){0,2}';
const RULE_RE = new RegExp(`^${NAME}(${OFF})(?:${NAME}(${OFF})?(?:,([^,]+),([^,]+))?)?$`);
const daysFromCivil = (y, m, d) => Math.floor(Date.UTC(y, m - 1, d) / 86400_000);

// A transition spec in `year` ("Mm.w.d", "Jn" or "n", each with an optional "/time"):
// [day since 1970, local seconds of the day].
function specDay(spec, year) {
  const [date, at] = spec.split('/');
  const t = at === undefined ? 7200 : secs(at);
  if (date.startsWith('M')) {
    const [m, w, d] = date.slice(1).split('.').map(Number);
    const first = daysFromCivil(year, m, 1);
    const next = daysFromCivil(year + (m === 12 ? 1 : 0), (m % 12) + 1, 1);
    let day = first + ((d + 7 - ((first + 4) % 7)) % 7) + 7 * (w - 1);
    while (day >= next) day -= 7;
    return [day, t];
  }
  const jan1 = daysFromCivil(year, 1, 1);
  if (date.startsWith('J')) {  // 1..365, February 29 never counted
    const n = Number(date.slice(1));
    const leap = (year % 4 === 0 && year % 100 !== 0) || year % 400 === 0;
    return [jan1 + n - 1 + (leap && n >= 60 ? 1 : 0), t];
  }
  return [jan1 + Number(date), t];  // 0..365
}

// Minutes-east-of-UTC function (of epoch seconds) for a POSIX TZ rule; null when unreadable.
export function posixOffset(rule) {
  const m = RULE_RE.exec(rule ?? '');
  if (!m) return null;
  const std = secs(m[1]);  // west of Greenwich
  if (!m[3]) return () => -std / 60;
  const dst = m[2] ? secs(m[2]) : std - 3600;
  return (utc) => {
    const year = new Date(Math.floor((utc - std) / DAY_S) * 86400_000).getUTCFullYear();
    const [d0, t0] = specDay(m[3], year);
    const [d1, t1] = specDay(m[4], year);
    const start = d0 * DAY_S + t0 + std;  // written in standard time
    const end = d1 * DAY_S + t1 + dst;    // written in daylight time
    const on = start < end ? utc >= start && utc < end : utc >= start || utc < end;
    return -(on ? dst : std) / 60;
  };
}

// --- TZif files (the operating system's zone data) -----------------------------------------

// Minutes-east-of-UTC function (of epoch seconds) for a TZif v2+ file; null when unreadable.
export function tzifOffset(buf) {
  if (buf.length < 44 || buf.toString('latin1', 0, 4) !== 'TZif' || buf[4] < 0x32) return null;
  const counts = (at) => [0, 1, 2, 3, 4, 5].map((i) => buf.readUInt32BE(at + 20 + 4 * i));
  let [isut, isstd, leap, timecnt, typecnt, charcnt] = counts(0);
  let p = 44 + timecnt * 5 + typecnt * 6 + charcnt + leap * 8 + isstd + isut;  // skip the v1 block
  if (buf.length < p + 44 || buf.toString('latin1', p, p + 4) !== 'TZif') return null;
  [isut, isstd, leap, timecnt, typecnt, charcnt] = counts(p);
  p += 44;
  const end = p + timecnt * 9 + typecnt * 6 + charcnt + leap * 12 + isstd + isut;
  if (!typecnt || buf.length < end) return null;
  const times = [];
  for (let i = 0; i < timecnt; i++) times.push(Number(buf.readBigInt64BE(p + 8 * i)));
  const idx = buf.subarray(p + 8 * timecnt, p + 9 * timecnt);
  const typeAt = p + 9 * timecnt;
  const utoff = (k) => buf.readInt32BE(typeAt + 6 * k) / 60;
  const tail = buf.toString('latin1', end).split('\n')[1] ?? '';
  const after = tail ? posixOffset(tail) : null;
  if (tail && !after) return null;
  return (utc) => {
    if (!timecnt) return after ? after(utc) : utoff(0);  // RFC 8536 3.2
    if (utc < times[0]) return utoff(0);
    if (utc >= times[timecnt - 1] && after) return after(utc);
    let lo = 0, hi = timecnt - 1;  // the last transition at or before utc
    while (lo < hi) {
      const mid = (lo + hi + 1) >> 1;
      if (times[mid] <= utc) lo = mid; else hi = mid - 1;
    }
    return utoff(idx[lo]);
  };
}

// The operating system's zone directory and its tz database version ("2026c"), or null.
export function osZoneInfo(dirs = [process.env.TZDIR, '/usr/share/zoneinfo', '/usr/lib/zoneinfo']) {
  for (const dir of dirs.filter(Boolean)) {
    let version = null;
    try { version = fs.readFileSync(path.join(dir, '+VERSION'), 'latin1').trim(); } catch {}
    if (!version) {
      try {
        const head = fs.readFileSync(path.join(dir, 'tzdata.zi'), 'latin1').slice(0, 64);
        version = /^# version (\d{4}[a-z]+)/.exec(head)?.[1] ?? null;
      } catch {}
    }
    version = /^\d{4}[a-z]+/.exec(version ?? '')?.[0] ?? null;
    if (version) return { dir, version };
  }
  return null;
}

// "2026c" > "2024b": the year, then the letter(s) (a release after "z" is "za", longer).
export const newerTz = (a, b) => {
  const pa = /^(\d{4})([a-z]+)$/.exec(a ?? ''), pb = /^(\d{4})([a-z]+)$/.exec(b ?? '');
  if (!pa || !pb) return !!pa && !pb;
  if (pa[1] !== pb[1]) return pa[1] > pb[1];
  return pa[2].length !== pb[2].length ? pa[2].length > pb[2].length : pa[2] > pb[2];
};

// The offset function (minutes east, of epoch seconds) for `zone` from the newest source, or null
// when the zone is unknown to both.
export function zoneSource(zone, { os = osZoneInfo(), nodeTz = process.versions.tz } = {}) {
  if (!isZoneName(zone)) return null;
  if (os && newerTz(os.version, nodeTz)) {
    try {
      const fn = tzifOffset(fs.readFileSync(path.join(os.dir, ...zone.split('/'))));
      if (fn) return fn;
    } catch {}
  }
  try {
    intlOffset(zone, 0);
  } catch {
    return null;
  }
  return (utc) => intlOffset(zone, utc * 1000);
}

// { z, off, next, noff } for `zone` at `nowMs`: minutes east of UTC now, the first second (epoch
// s) of the next change within HORIZON_DAYS (0 = none) and the offset from then on (= off without
// a change). `offsetAt(epochSeconds)` -> minutes east. Days are stepped, the change is bisected
// down to the second (no zone changes twice within a day).
export function nextChange(zone, nowMs, offsetAt) {
  const now = Math.floor(nowMs / 1000);
  const off = offsetAt(now);
  for (let d = 1; d <= HORIZON_DAYS; d++) {
    const t = now + d * DAY_S;
    if (offsetAt(t) === off) continue;
    let lo = t - DAY_S, hi = t;  // offsetAt(lo) === off, offsetAt(hi) !== off
    while (hi - lo > 1) {
      const mid = Math.floor((lo + hi) / 2);
      if (offsetAt(mid) === off) lo = mid; else hi = mid;
    }
    return { z: zone, off, next: hi, noff: offsetAt(hi) };
  }
  return { z: zone, off, next: 0, noff: off };
}

// Remembers each zone's answer for an hour, or until its change comes (whichever is first).
export class ZoneOffsets {
  #cache = new Map();

  constructor({ source = zoneSource } = {}) {
    this.source = source;
  }

  // The entry for `zone` at `nowMs`, or null for a name no source knows.
  get(zone, nowMs) {
    const hit = this.#cache.get(zone);
    if (hit && nowMs >= hit.from && nowMs < hit.until) return hit.entry;
    const fn = this.source(zone);
    const entry = fn ? nextChange(zone, nowMs, fn) : null;
    const until = Math.min(nowMs + CACHE_MS, entry?.next ? entry.next * 1000 : Infinity);
    this.#cache.set(zone, { entry, from: nowMs, until });
    return entry;
  }

  // `snapshot` with `tz`: the live offsets of `zones` (a gadget's tz and tz2; unknown names are
  // left out). Unchanged when there are none.
  withZones(snapshot, zones, nowMs) {
    const list = [...new Set((zones ?? []).filter(isZoneName))].slice(0, MAX_ZONES)
      .map((z) => this.get(z, nowMs)).filter(Boolean);
    return list.length ? { ...snapshot, tz: list } : snapshot;
  }
}
