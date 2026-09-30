import { MAX_SESSIONS, NAME_LEN, DET_LEN, MODEL_LEN, PROTOCOL_VERSION, SNAPSHOT_MAX_BYTES } from './constants.js';

const cut = (s, n) => {
  const chars = Array.from(String(s ?? ''));
  return chars.length > n ? chars.slice(0, n - 1).join('') + '…' : chars.join('');
};
const shortId = (id) => String(id).replace(/-/g, '').slice(0, 8);
const toSec = (ms) => Math.floor(ms / 1000);

// `day` (a DayStats) is optional: without it `today` only carries the cost. `latest` is the
// newest released version ("X.Y.Z"); the field is omitted when unknown.
export function buildSnapshot({ seq, nowMs, host, tracker, metrics, day, latest }) {
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
    };
  });

  const snapshot = {
    v: PROTOCOL_VERSION,
    seq,
    now: toSec(nowMs),
    host: cut(host, NAME_LEN),
    usage: metrics.usage(),
    today: { ...metrics.today(), ...(day ? day.today() : {}) },
    sessions: rows.slice(0, MAX_SESSIONS),
    more: Math.max(0, rows.length - MAX_SESSIONS),
    alerts: tracker.alerts().map((a) => ({ id: a.id, kind: a.kind, sid: shortId(a.sid) })),
    ...(latest ? { latest } : {}),
  };

  while (snapshot.sessions.length > 0 && Buffer.byteLength(JSON.stringify(snapshot)) > SNAPSHOT_MAX_BYTES) {
    snapshot.sessions.pop();
    snapshot.more += 1;
  }
  return snapshot;
}
