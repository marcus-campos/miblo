// Fuzz target over the alert queue (miblo_alerts): random snapshots from two paired computers
// (and an old plugin without `host`), with the clock, timings and modifiers moving, into one
// AlertSequencer, the way app.cpp drives it (ingest on every snapshot, update on every frame).
#include <map>
#include <string>

#include "fuzz.h"
#include "miblo_alerts.h"
#include "miblo_overview.h"
#include "miblo_snapshot.h"

using namespace miblo;
using fuzz::Reader;

namespace {

// A small pool of ids, so sessions come back, wait again and collide across computers.
const char* const kIds[] = {"0000000a", "0000000b", "0000000c", "deadbeef", "x", "0000000a1"};
constexpr uint8_t kIdCount = sizeof(kIds) / sizeof(kIds[0]);
const char* const kHosts[] = {"mac", "linux-box", ""};

struct HostState {
  uint32_t seq = 0, nextId = 1;
};

bool waiting(SessionState st) { return st == SessionState::Perm || st == SessionState::Question; }

void buildSnapshot(Reader& r, Snapshot& s, uint8_t host, HostState& hs) {
  memset(&s, 0, sizeof(s));
  snprintf(s.host, sizeof(s.host), "%s", kHosts[host]);
  const uint8_t flags = r.u8();
  if (flags & 0x80) hs.seq = r.u8() % 4, hs.nextId = 1 + r.u8() % 3;  // the bridge restarted
  s.seq = ++hs.seq;
  s.now = 1790000000u + r.u16();
  s.count = r.u8() % 8;
  for (uint8_t i = 0; i < s.count; i++) {
    SessionRow& row = s.sessions[i];
    snprintf(row.id, sizeof(row.id), "%s", kIds[r.u8() % kIdCount]);
    snprintf(row.name, sizeof(row.name), "p%u-%s", (unsigned)i, kHosts[host]);
    row.st = (SessionState)(r.u8() % 5);
    row.since = 1789990000u + r.u8() % 6;  // few distinct values: the same wait seen again
    row.ctx = -1;
    row.tok = -1;
  }
  // Duplicate ids within one snapshot are not something the bridge sends: keep them unique.
  for (uint8_t i = 0; i < s.count; i++) {
    for (uint8_t j = 0; j < i; j++) {
      if (strcmp(s.sessions[i].id, s.sessions[j].id) == 0) {
        s.sessions[i] = s.sessions[--s.count];
        i--;
        break;
      }
    }
  }
  s.alertCount = r.u8() % (kMaxAlerts + 1);
  for (uint8_t i = 0; i < s.alertCount; i++) {
    AlertItem& a = s.alerts[i];
    const uint8_t b = r.u8();
    a.id = (b & 0x80) ? (uint32_t)(b & 7) : hs.nextId++;  // old ids again now and then
    a.kind = (AlertKind)(r.u8() % 3);
    snprintf(a.sid, sizeof(a.sid), "%s", kIds[r.u8() % kIdCount]);
  }
}

void fuzzAlerts(const uint8_t* d, size_t n) {
  Reader r(d, n);
  AlertSequencer seq;
  AlertTiming timing;
  if (n && (d[0] & 1)) timing.reminderMs = 0;  // half the runs start without reminders
  seq.setTiming(timing);
  AlertModifiers mods;
  HostState hosts[3];
  static Snapshot snaps[3];
  static Snapshot cur;
  memset(&cur, 0, sizeof(cur));
  bool have[3] = {false, false, false};
  uint32_t nowMs = r.u32();
  uint32_t maxFlash = 2 * timing.flashMs, maxHero = 2 * (timing.heroPermMs > timing.heroDoneMs ? timing.heroPermMs : timing.heroDoneMs);
  uint32_t maxExtend = 0;
  // Waits already alerted: session -> since (a shadow of the sequencer's table).
  std::map<std::string, uint32_t> shown;
  bool remindersEver = timing.reminderMs != 0;  // the default timing reminds every 2 min
  bool flashOn = false;
  uint32_t flashStart = 0, flashSince = 0;
  AlertKind flashKind = AlertKind::Done;
  char flashSid[sizeof(AlertView::sid)] = {0};
  int steps = 0, deep = 0;
  while (r.left() && steps++ < 400) {
    const uint8_t op = r.u8();
    switch (op % 6) {
      case 0:
      case 1: {  // a snapshot from one of the computers
        const uint8_t h = (op >> 3) % 3;
        buildSnapshot(r, snaps[h], h, hosts[h]);
        have[h] = true;
        cur = snaps[h];
        seq.ingest(cur, nowMs);
        if (!deep++) fuzz::reached();
        break;
      }
      case 2: {  // the same computer's latest again (or another's): no new records
        const uint8_t h = (op >> 3) % 3;
        if (have[h]) cur = snaps[h];
        break;
      }
      case 3: {  // settings change
        AlertTiming t;
        const uint8_t b = r.u8();
        t.enabled = (b & 7) != 0;
        t.flashMs = kBlinkMs * (1 + r.u8() % 5);
        t.heroPermMs = 1000u * (1 + r.u8() % 30);
        t.heroDoneMs = 1000u * (1 + r.u8() % 30);
        t.reminderMs = (b & 0x18) ? 0 : 1000u * (1 + r.u8() % 120);
        if (t.reminderMs) remindersEver = true;
        timing = t;
        seq.setTiming(t);
        if (2 * t.flashMs > maxFlash) maxFlash = 2 * t.flashMs;
        if (2 * t.heroPermMs > maxHero) maxHero = 2 * t.heroPermMs;
        if (2 * t.heroDoneMs > maxHero) maxHero = 2 * t.heroDoneMs;
        break;
      }
      case 4: {  // focus / meeting / insistence
        const uint8_t b = r.u8();
        mods.insist = b & 1;
        mods.quietFlash = b & 2;
        mods.holdDone = b & 4;
        seq.setModifiers(mods);
        break;
      }
      case 5: {  // the fanfare
        const uint32_t ms = (uint32_t)r.u16() * 4;
        if (ms > maxExtend) maxExtend = ms;
        seq.extendHero(ms);
        break;
      }
    }
    // A frame: the clock moves (sometimes across the 32-bit wrap), then update().
    const uint8_t step = r.u8();
    nowMs += (step & 0xC0) == 0xC0 ? (uint32_t)r.u16() * 64 : (uint32_t)step * 40;
    // The sequencer's own table forgets a wait when its session shows up out of it.
    for (uint8_t i = 0; i < cur.count; i++) {
      auto it = shown.find(cur.sessions[i].id);
      if (it != shown.end() && (!waiting(cur.sessions[i].st) || it->second != cur.sessions[i].since)) shown.erase(it);
    }
    const AlertView& v = seq.update(cur, nowMs);
    FUZZ_CHECK(v.phase <= AlertPhase::Hero, "phase %u", (unsigned)v.phase);
    FUZZ_CHECK(v.level <= 2, "level %u", v.level);
    FUZZ_CHECK(strnlen(v.sid, sizeof(v.sid)) < sizeof(v.sid), "sid not terminated");
    FUZZ_CHECK(strnlen(seq.alertName(), sizeof(SessionRow::name)) < sizeof(SessionRow::name), "name not terminated");
    FUZZ_CHECK(seq.queued() <= kMaxAlerts, "queued %u", seq.queued());
    if (!timing.enabled) {
      FUZZ_CHECK(v.phase == AlertPhase::None, "an alert while alerts are off");
      // Off: the waits going on count as dealt with.
      for (uint8_t i = 0; i < cur.count; i++) {
        if (waiting(cur.sessions[i].st)) shown[cur.sessions[i].id] = cur.sessions[i].since;
      }
      flashOn = false;
      continue;
    }
    const bool amber = v.kind == AlertKind::Perm || v.kind == AlertKind::Question;
    if (v.phase != AlertPhase::None && v.level) FUZZ_CHECK(amber, "an insistent \"finished\"");
    // A new alert: a flash that was not on, or another one. The same session waiting again (a new
    // `since`) restarts the flash at the same millisecond: the old wait's alert ends, the new one
    // starts.
    const int row = findSession(cur, v.sid);
    const bool sameWait = !amber || row < 0 || cur.sessions[row].since == flashSince;
    const bool started = v.phase == AlertPhase::Flash && (!flashOn || v.phaseStartMs != flashStart ||
                                                           v.kind != flashKind || strcmp(v.sid, flashSid) != 0 ||
                                                           !sameWait);
    if (started) {
      flashStart = v.phaseStartMs;
      flashKind = v.kind;
      flashSince = row >= 0 ? cur.sessions[row].since : 0;
      memcpy(flashSid, v.sid, sizeof(flashSid));
      // The level is decided when the alert starts: only with insistence on.
      FUZZ_CHECK(v.level == 0 || mods.insist, "insistence level %u while insistence is off", v.level);
      FUZZ_CHECK(v.phaseStartMs == nowMs, "a new alert started in the past");
      const int i = findSession(cur, v.sid);
      if (amber) {
        // A needs-you alert starts from the latest snapshot's session list, for a waiting session.
        FUZZ_CHECK(i >= 0 && waiting(cur.sessions[i].st), "needs-you alert for %s, not waiting", v.sid);
        auto it = shown.find(v.sid);
        // Without reminders a wait is alerted once (a session id only: kMaxSessions >> the pool).
        FUZZ_CHECK(remindersEver || it == shown.end() || it->second != cur.sessions[i].since,
                   "the wait of %s alerted twice", v.sid);
        shown[v.sid] = cur.sessions[i].since;
      } else {
        FUZZ_CHECK(i < 0 || cur.sessions[i].st == SessionState::Done, "\"finished\" for %s, not done", v.sid);
      }
    }
    flashOn = v.phase == AlertPhase::Flash;
    // Nothing stays on screen past its length (the update after it ran out ends it).
    if (v.phase == AlertPhase::Flash) {
      FUZZ_CHECK(nowMs - v.phaseStartMs < maxFlash + kBlinkMs, "flash stuck for %u ms", nowMs - v.phaseStartMs);
    } else if (v.phase == AlertPhase::Hero) {
      const uint32_t bound = maxHero > maxExtend ? maxHero : maxExtend;
      FUZZ_CHECK(nowMs - v.phaseStartMs < bound, "hero stuck for %u ms", nowMs - v.phaseStartMs);
    }
    // Needs you is never left behind: with nothing on screen, every waiting session in the latest
    // snapshot has had its alert.
    if (v.phase == AlertPhase::None) {
      for (uint8_t i = 0; i < cur.count; i++) {
        if (!waiting(cur.sessions[i].st)) continue;
        auto it = shown.find(cur.sessions[i].id);
        FUZZ_CHECK(it != shown.end() && it->second == cur.sessions[i].since, "the wait of %s was never shown",
                   cur.sessions[i].id);
      }
    }
  }
}

const char* const kAlertSeeds[] = {
    // op 0 (snapshot host 0), flags, now(2), count 2, [id st since]x2, alerts 1, [b kind sid], step
    "hex:00000000" "00" "00" "1000" "02" "000100" "010200" "01" "00" "00" "00" "10",
    "hex:00000000" "08" "00" "1000" "03" "000100" "010300" "020400" "02" "00" "02" "02" "00" "01" "00" "20"
    "00" "00" "1000" "02" "000201" "010000" "00" "ff" "05ff" "05ff" "05ff" "05ff",
    "hex:01020304" "03" "01" "02" "03" "04" "05" "10" "04" "07" "10" "00" "00" "2000" "01" "030100" "00" "ff"
    "0000" "05" "ff" "0400" "02" "10" "05" "ffff" "10",
    nullptr};

}  // namespace

FUZZ_REGISTER(alerts, fuzzAlerts, kAlertSeeds, nullptr, 1500);
