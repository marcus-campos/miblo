import { MAX_SESSIONS, NAME_LEN, DET_LEN, MODEL_LEN, PROTOCOL_VERSION, SNAPSHOT_MAX_BYTES } from './constants.js';

const cut = (s, n) => {
  const chars = Array.from(String(s ?? ''));
  return chars.length > n ? chars.slice(0, n - 1).join('') + '…' : chars.join('');
};
const shortId = (id) => String(id).replace(/-/g, '').slice(0, 8);
const toSec = (ms) => Math.floor(ms / 1000);

// `usage` (MetricsStore.usage()) with the 5-hour forecast `eta` (epoch s) in `h5`; unchanged when
// there is no forecast or no 5-hour window to put it in.
export function withEta(usage, eta) {
  return eta && usage?.h5 ? { ...usage, h5: { ...usage.h5, eta } } : usage;
}

// `day` (a DayStats) is optional: without it `today` only carries the cost. `latest` is the
// newest released version ("X.Y.Z"); the field is omitted when unknown. `eta` (epoch s, from
// LimitForecast) goes into `usage.h5` when there is a forecast and a 5-hour window to put it in.
// `week` (DayStats.week(): last week's totals) is sent as given; the bridge passes it on Mondays.
export function buildSnapshot({ seq, nowMs, host, tracker, metrics, day, latest, eta, week }) {
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
      // When a running shell command started, so the gadget can show how long it has been going.
      ...(s.st === 'running' && s.tool === 'Bash' && s.cmdLive && s.toolSince ? { ts: toSec(s.toolSince) } : {}),
    };
  });

  const snapshot = {
    v: PROTOCOL_VERSION,
    seq,
    now: toSec(nowMs),
    host: cut(host, NAME_LEN),
    usage: withEta(metrics.usage(), eta),
    today: { ...metrics.today(), ...(day ? day.today() : {}) },
    ...(week ? { week } : {}),
    sessions: rows.slice(0, MAX_SESSIONS),
    more: Math.max(0, rows.length - MAX_SESSIONS),
    alerts: tracker.alerts().map((a) => ({ id: a.id, kind: a.kind, sid: shortId(a.sid) })),
    ...(latest ? { latest } : {}),
  };

  return trimSnapshot(snapshot, MAX_SESSIONS, SNAPSHOT_MAX_BYTES);
}

// A copy of `snapshot` that fits a gadget's own caps (from its /api/info): at most `maxSessions`
// sessions and `maxBytes` when serialized; the dropped ones are counted in `more`. Mixed fleets
// (a 1.5.0 gadget beside an older one) each get as much as they can show.
export function trimSnapshot(snapshot, maxSessions, maxBytes) {
  if (!Array.isArray(snapshot.sessions)) return snapshot;
  const extra = Math.max(0, snapshot.sessions.length - maxSessions);
  if (!extra && Buffer.byteLength(JSON.stringify(snapshot)) <= maxBytes) return snapshot;
  const s = { ...snapshot, sessions: snapshot.sessions.slice(0, maxSessions), more: snapshot.more + extra };
  while (s.sessions.length > 0 && Buffer.byteLength(JSON.stringify(s)) > maxBytes) {
    s.sessions.pop();
    s.more += 1;
  }
  return s;
}

// A tiny snapshot with ONLY the alerts and the sessions they name (minimal fields), for when the
// gadget is momentarily low on memory and refused the full one (503): alerts are the most
// important thing to show, so this always fits and gets through.
export function alertOnlySnapshot(snapshot) {
  const alerts = snapshot.alerts ?? [];
  if (!alerts.length) return null;
  const sids = new Set(alerts.map((a) => a.sid));
  const kept = (snapshot.sessions ?? []).filter((s) => sids.has(s.id))
    .map((s) => ({ id: s.id, name: s.name, st: s.st, tool: s.tool, det: s.det, since: s.since }));
  const dropped = (snapshot.sessions ?? []).length - kept.length;
  return {
    v: snapshot.v, seq: snapshot.seq, now: snapshot.now, usage: snapshot.usage ?? null,
    sessions: kept, more: (snapshot.more ?? 0) + Math.max(0, dropped), alerts,
  };
}
