#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "miblo_alerts.h"

using namespace miblo;

void setUp() {}
void tearDown() {}

static Snapshot snap;

static void reset(uint32_t seq = 1) {
  memset(&snap, 0, sizeof(snap));
  snap.seq = seq;
}

static void session(const char* id, SessionState st, uint32_t since = 100) {
  SessionRow& r = snap.sessions[snap.count++];
  memset(&r, 0, sizeof(r));
  strcpy(r.id, id);
  r.st = st;
  r.since = since;
  r.ctx = -1;
  r.tok = -1;
}

static void alert(uint32_t id, AlertKind kind, const char* sid) {
  AlertItem& a = snap.alerts[snap.alertCount++];
  a.id = id;
  a.kind = kind;
  strcpy(a.sid, sid);
}

static void removeSession(const char* id) {
  for (int i = 0; i < snap.count; i++) {
    if (strcmp(snap.sessions[i].id, id) != 0) continue;
    for (int j = i + 1; j < snap.count; j++) snap.sessions[j - 1] = snap.sessions[j];
    snap.count--;
    return;
  }
}

static void setState(const char* id, SessionState st) {
  for (int i = 0; i < snap.count; i++) {
    if (strcmp(snap.sessions[i].id, id) == 0) snap.sessions[i].st = st;
  }
}

// Runs the sequencer until nothing is on screen; returns the time.
static uint32_t drain(AlertSequencer& q, uint32_t now) {
  while (q.update(snap, now).phase != AlertPhase::None) now += 100;
  return now;
}

static void test_flash_then_hero_then_summary_for_permission() {
  AlertSequencer q;
  reset();
  session("a", SessionState::Perm);
  alert(1, AlertKind::Perm, "a");
  q.ingest(snap, 0);
  const AlertView* v = &q.update(snap, 0);
  TEST_ASSERT_EQUAL(AlertPhase::Flash, v->phase);
  TEST_ASSERT_EQUAL(AlertKind::Perm, v->kind);
  TEST_ASSERT_EQUAL_STRING("a", v->sid);
  TEST_ASSERT_EQUAL(AlertPhase::Flash, q.update(snap, 1499).phase);
  TEST_ASSERT_EQUAL(AlertPhase::Hero, q.update(snap, 1500).phase);
  TEST_ASSERT_EQUAL(AlertPhase::Hero, q.update(snap, 11499).phase);
  TEST_ASSERT_EQUAL(AlertPhase::None, q.update(snap, 11500).phase);
}

static void test_done_hero_is_shorter() {
  AlertSequencer q;
  reset();
  session("d", SessionState::Done);
  alert(1, AlertKind::Done, "d");
  q.ingest(snap, 0);
  q.update(snap, 0);
  TEST_ASSERT_EQUAL(AlertPhase::Hero, q.update(snap, 1500).phase);
  TEST_ASSERT_EQUAL(AlertPhase::None, q.update(snap, 6500).phase);
}

static void test_dedupe_by_id() {
  AlertSequencer q;
  reset();
  session("d", SessionState::Done);
  alert(7, AlertKind::Done, "d");
  q.ingest(snap, 0);
  q.ingest(snap, 10);  // the same alert arrives in consecutive snapshots
  TEST_ASSERT_EQUAL_UINT8(1, q.queued());
  TEST_ASSERT_EQUAL_UINT32(7, q.lastSeenId());
  q.update(snap, 0);
  q.update(snap, 1500);
  q.update(snap, 6500);
  q.ingest(snap, 7000);
  TEST_ASSERT_EQUAL(AlertPhase::None, q.update(snap, 7000).phase);
}

// Amber before blue; the oldest wait first, Perm before Question when they started in the same
// second (oldest first, so a stream of new permissions can't keep an older question waiting).
static void test_amber_before_blue_and_oldest_wait_first() {
  AlertSequencer q;
  reset();
  session("done", SessionState::Done, 10);
  session("ask2", SessionState::Question, 30);
  session("perm", SessionState::Perm, 30);
  session("ask", SessionState::Question, 20);
  alert(1, AlertKind::Done, "done");
  alert(2, AlertKind::Question, "ask");
  alert(3, AlertKind::Perm, "perm");
  alert(4, AlertKind::Question, "ask2");
  q.ingest(snap, 0);
  TEST_ASSERT_EQUAL_STRING("ask", q.update(snap, 0).sid);
  q.update(snap, 1500);
  TEST_ASSERT_EQUAL_STRING("perm", q.update(snap, 11500).sid);
  q.update(snap, 13000);
  TEST_ASSERT_EQUAL_STRING("ask2", q.update(snap, 23000).sid);
  q.update(snap, 24500);
  TEST_ASSERT_EQUAL_STRING("done", q.update(snap, 34500).sid);
}

static void test_answered_alert_ends_early_and_stale_queue_is_dropped() {
  AlertSequencer q;
  reset();
  session("a", SessionState::Perm);
  session("b", SessionState::Question);
  alert(1, AlertKind::Perm, "a");
  alert(2, AlertKind::Question, "b");
  q.ingest(snap, 0);
  q.update(snap, 0);
  q.update(snap, 1500);
  snap.sessions[0].st = SessionState::Running;  // user approved it
  snap.sessions[1].st = SessionState::Running;  // and answered the other one
  TEST_ASSERT_EQUAL(AlertPhase::None, q.update(snap, 2000).phase);
  TEST_ASSERT_EQUAL_UINT8(0, q.queued());
}

static void test_reminder_every_interval_while_pending() {
  AlertSequencer q;
  reset();
  session("a", SessionState::Perm);
  alert(1, AlertKind::Perm, "a");
  q.ingest(snap, 0);
  q.update(snap, 0);
  q.update(snap, 1500);
  q.update(snap, 11500);  // hero finished at 11.5 s
  TEST_ASSERT_EQUAL(AlertPhase::None, q.update(snap, 131499).phase);
  const AlertView& v = q.update(snap, 131500);
  TEST_ASSERT_EQUAL(AlertPhase::Flash, v.phase);
  TEST_ASSERT_EQUAL(AlertKind::Perm, v.kind);
  TEST_ASSERT_EQUAL_STRING("a", v.sid);
}

// The gadget booted (or the bridge's alert record expired / was cut) while a session already
// waits: the wait is shown once from the session list, then only reminders bring it back.
static void test_wait_seen_without_alert_record_is_shown_once_then_reminded() {
  AlertSequencer q;
  reset();
  session("x", SessionState::Question);
  q.ingest(snap, 0);
  const AlertView& v = q.update(snap, 1000);
  TEST_ASSERT_EQUAL(AlertPhase::Flash, v.phase);
  TEST_ASSERT_EQUAL(AlertKind::Question, v.kind);
  TEST_ASSERT_EQUAL_UINT8(0, v.level);
  const uint32_t end = drain(q, 1000);
  TEST_ASSERT_EQUAL(AlertPhase::None, q.update(snap, end + 119000).phase);
  TEST_ASSERT_EQUAL(AlertPhase::Flash, q.update(snap, end + 120000).phase);

  AlertSequencer off;
  AlertTiming t;
  t.reminderMs = 0;
  off.setTiming(t);
  off.ingest(snap, 0);
  TEST_ASSERT_EQUAL(AlertPhase::Flash, off.update(snap, 1000).phase);
  const uint32_t offEnd = drain(off, 1000);
  for (uint32_t now = offEnd; now < offEnd + 999999; now += 1000) {
    TEST_ASSERT_EQUAL(AlertPhase::None, off.update(snap, now).phase);
  }
}

static void test_bridge_restart_resets_dedupe() {
  AlertSequencer q;
  reset(500);
  session("a", SessionState::Done);
  alert(9, AlertKind::Done, "a");
  q.ingest(snap, 0);
  q.update(snap, 0);
  q.update(snap, 1500);
  q.update(snap, 6500);
  reset(1);  // seq went backwards: new bridge, ids start over
  session("b", SessionState::Done);
  alert(1, AlertKind::Done, "b");
  q.ingest(snap, 7000);
  TEST_ASSERT_EQUAL(AlertPhase::Flash, q.update(snap, 7000).phase);
}

static void test_disabled_alerts_show_nothing() {
  AlertSequencer q;
  AlertTiming t;
  t.enabled = false;
  q.setTiming(t);
  reset();
  session("a", SessionState::Perm);
  alert(1, AlertKind::Perm, "a");
  q.ingest(snap, 0);
  TEST_ASSERT_EQUAL(AlertPhase::None, q.update(snap, 0).phase);
  TEST_ASSERT_EQUAL(AlertPhase::None, q.update(snap, 500000).phase);
  TEST_ASSERT_EQUAL_UINT32(1, q.lastSeenId());
}

// Same wait, reminders every 2 min: 1st and 2nd normal, 3rd and 4th doubled, 5th red; back to
// normal once nobody waits.
static void test_insistence_steps() {
  AlertSequencer q;
  AlertTiming t;
  t.reminderMs = 120000;
  q.setTiming(t);
  reset();
  session("a", SessionState::Perm);
  uint32_t now = drain(q, 0);  // the wait itself: shown once, plain
  uint8_t levels[6];
  for (int i = 0; i < 6; i++) {
    now += t.reminderMs + 30000;  // well after the previous alert ended
    const AlertView& v = q.update(snap, now);
    TEST_ASSERT_EQUAL(AlertPhase::Flash, v.phase);
    levels[i] = v.level;
    const uint8_t level = v.level;
    const uint32_t flash = level ? 2 * t.flashMs : t.flashMs;
    TEST_ASSERT_EQUAL(AlertPhase::Flash, q.update(snap, now + flash - 1).phase);
    TEST_ASSERT_EQUAL(AlertPhase::Hero, q.update(snap, now + flash).phase);
    const uint32_t hero = (level ? 2 : 1) * t.heroPermMs;
    TEST_ASSERT_EQUAL(AlertPhase::Hero, q.update(snap, now + flash + hero - 1).phase);
    now += flash + hero;
    TEST_ASSERT_EQUAL(AlertPhase::None, q.update(snap, now).phase);
  }
  const uint8_t expected[] = {0, 0, 1, 1, 2, 2};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, levels, 6);
  snap.sessions[0].st = SessionState::Running;  // answered: the wait is over
  q.update(snap, now + 1);
  snap.sessions[0].st = SessionState::Perm;  // a new wait starts from scratch
  const AlertView& v = q.update(snap, now + 2);
  TEST_ASSERT_EQUAL(AlertPhase::Flash, v.phase);
  TEST_ASSERT_EQUAL_UINT8(0, v.level);
  now = drain(q, now + 2);
  const AlertView& r = q.update(snap, now + t.reminderMs);  // and its 1st reminder is plain too
  TEST_ASSERT_EQUAL(AlertPhase::Flash, r.phase);
  TEST_ASSERT_EQUAL_UINT8(0, r.level);
}

// With insistence off every reminder stays plain.
static void test_insistence_off_keeps_level_zero() {
  AlertSequencer q;
  q.setModifiers({false, false, false});
  reset();
  session("a", SessionState::Question);
  uint32_t now = drain(q, 0);
  for (int i = 0; i < 6; i++) {
    now += 200000;
    const AlertView& v = q.update(snap, now);
    TEST_ASSERT_EQUAL(AlertPhase::Flash, v.phase);
    TEST_ASSERT_EQUAL_UINT8(0, v.level);
    TEST_ASSERT_EQUAL(AlertPhase::Hero, q.update(snap, now + 1500).phase);
    now += 1500 + 10000;
    TEST_ASSERT_EQUAL(AlertPhase::None, q.update(snap, now).phase);
  }
}

static void test_insistence_off_and_meeting_single_blink() {
  AlertSequencer q;
  AlertTiming t;
  t.flashMs = 3 * kBlinkMs;
  q.setTiming(t);
  q.setModifiers({false, true, false});
  reset();
  session("a", SessionState::Perm);
  alert(1, AlertKind::Perm, "a");
  q.ingest(snap, 0);
  TEST_ASSERT_EQUAL(AlertPhase::Flash, q.update(snap, 0).phase);
  TEST_ASSERT_EQUAL(AlertPhase::Flash, q.update(snap, kBlinkMs - 1).phase);
  TEST_ASSERT_EQUAL(AlertPhase::Hero, q.update(snap, kBlinkMs).phase);
}

// In a meeting a 5th reminder still blinks once, but stays red (level 2) with the long hero.
static void test_meeting_single_blink_keeps_the_red_level() {
  AlertSequencer q;
  q.setModifiers({true, true, false});
  reset();
  session("a", SessionState::Perm);
  uint32_t now = drain(q, 0);
  for (int i = 0; i < 5; i++) {
    now += 200000;
    q.update(snap, now);
    q.update(snap, now + kBlinkMs);
    now += kBlinkMs + 20000;
    q.update(snap, now);
  }
  now += 200000;
  const AlertView& v = q.update(snap, now);
  TEST_ASSERT_EQUAL(AlertPhase::Flash, v.phase);
  TEST_ASSERT_EQUAL_UINT8(2, v.level);
  TEST_ASSERT_EQUAL(AlertPhase::Hero, q.update(snap, now + kBlinkMs).phase);
  TEST_ASSERT_EQUAL(AlertPhase::Hero, q.update(snap, now + kBlinkMs + 19999).phase);
  TEST_ASSERT_EQUAL(AlertPhase::None, q.update(snap, now + kBlinkMs + 20000).phase);
}

// Focus: "finished" waits in the queue; a permission still goes straight through; the held
// "finished" shows when the hold ends, if that session is still done.
static void test_hold_done_only_holds_finished() {
  AlertSequencer q;
  q.setTiming(AlertTiming{});
  q.setModifiers({true, false, true});
  reset();
  session("d", SessionState::Done);
  alert(1, AlertKind::Done, "d");
  q.ingest(snap, 0);
  TEST_ASSERT_EQUAL(AlertPhase::None, q.update(snap, 10).phase);
  TEST_ASSERT_EQUAL_UINT8(1, q.queued());
  session("p", SessionState::Perm);
  alert(2, AlertKind::Perm, "p");
  q.ingest(snap, 20);
  TEST_ASSERT_EQUAL(AlertKind::Perm, q.update(snap, 20).kind);
  uint32_t now = 20;
  while (q.update(snap, now).phase != AlertPhase::None) now += 100;  // the permission runs its course
  TEST_ASSERT_EQUAL_UINT8(1, q.queued());  // "finished" is still held
  q.setModifiers({true, false, false});    // the break starts
  removeSession("p");
  const AlertView& v = q.update(snap, now + 100);
  TEST_ASSERT_EQUAL(AlertPhase::Flash, v.phase);
  TEST_ASSERT_EQUAL(AlertKind::Done, v.kind);
  TEST_ASSERT_EQUAL_STRING("d", v.sid);
}

// A held "finished" whose session moved on is dropped, not shown late.
static void test_held_done_is_dropped_when_stale() {
  AlertSequencer q;
  q.setModifiers({true, false, true});
  reset();
  session("d", SessionState::Done);
  alert(1, AlertKind::Done, "d");
  q.ingest(snap, 0);
  q.update(snap, 0);
  snap.sessions[0].st = SessionState::Running;  // a new prompt during the focus round
  q.setModifiers({true, false, false});
  TEST_ASSERT_EQUAL(AlertPhase::None, q.update(snap, 1000).phase);
  TEST_ASSERT_EQUAL_UINT8(0, q.queued());
}

static void test_extend_hero_for_the_fanfare() {
  AlertSequencer q;
  AlertTiming t;
  q.setTiming(t);
  reset();
  session("d", SessionState::Done);
  alert(1, AlertKind::Done, "d");
  q.ingest(snap, 0);
  q.extendHero(kFanfareMs);  // no hero yet: nothing to extend
  q.update(snap, 0);
  q.update(snap, t.flashMs);  // hero starts
  q.extendHero(kFanfareMs);
  q.extendHero(kFanfareMs);  // called every frame: still 8 s in total, not more
  TEST_ASSERT_EQUAL(AlertPhase::Hero, q.update(snap, t.flashMs + kFanfareMs - 1).phase);
  TEST_ASSERT_EQUAL(AlertPhase::None, q.update(snap, t.flashMs + kFanfareMs).phase);
}

// Eight sessions finish during a focus round: the held "finished" alerts fill the queue. A
// permission that comes in then still flashes right away, with or without reminders.
static void test_held_finished_never_crowds_out_a_permission() {
  const uint32_t reminders[] = {120000u, 0u};
  for (uint32_t reminder : reminders) {
    AlertSequencer q;
    AlertTiming t;
    t.reminderMs = reminder;
    q.setTiming(t);
    q.setModifiers({true, false, true});
    reset();
    char id[4];
    for (int i = 0; i < kMaxAlerts; i++) {
      snprintf(id, sizeof(id), "d%d", i);
      session(id, SessionState::Done);
    }
    for (int i = 0; i < kMaxAlerts; i++) {
      snap.alertCount = 0;
      snprintf(id, sizeof(id), "d%d", i);
      alert((uint32_t)i + 1, AlertKind::Done, id);
      q.ingest(snap, 0);
    }
    TEST_ASSERT_EQUAL(AlertPhase::None, q.update(snap, 10).phase);
    TEST_ASSERT_EQUAL_UINT8(kMaxAlerts, q.queued());
    session("p", SessionState::Perm);
    snap.alertCount = 0;
    alert(100, AlertKind::Perm, "p");
    q.ingest(snap, 20);
    const AlertView& v = q.update(snap, 20);
    TEST_ASSERT_EQUAL(AlertPhase::Flash, v.phase);
    TEST_ASSERT_EQUAL(AlertKind::Perm, v.kind);
    TEST_ASSERT_EQUAL_STRING("p", v.sid);
  }
}

// The same session finishing twice during the hold (no Running seen in between) shows one
// "finished" at the break, not two.
static void test_held_finished_shows_once_per_session() {
  AlertSequencer q;
  q.setModifiers({true, false, true});
  reset();
  session("d", SessionState::Done);
  alert(1, AlertKind::Done, "d");
  q.ingest(snap, 0);
  snap.alertCount = 0;
  alert(2, AlertKind::Done, "d");
  q.ingest(snap, 10);
  TEST_ASSERT_EQUAL_UINT8(1, q.queued());
  q.setModifiers({true, false, false});
  TEST_ASSERT_EQUAL(AlertKind::Done, q.update(snap, 20).kind);
  const uint32_t now = drain(q, 20);
  TEST_ASSERT_EQUAL(AlertPhase::None, q.update(snap, now + 100).phase);
  TEST_ASSERT_EQUAL_UINT8(0, q.queued());
}

// Insistence follows the session being reminded: when it stops waiting, the next session's
// reminders start plain again; a brand-new permission always starts at level 0.
static void test_insistence_resets_per_session() {
  AlertSequencer q;
  reset();
  session("a", SessionState::Perm, 100);
  session("b", SessionState::Perm, 200);
  uint32_t now = drain(q, 0);  // each wait shown once, then reminders only
  char first[9] = "";
  uint8_t level = 0;
  for (int i = 0; i < 5; i++) {
    now += 200000;
    const AlertView& v = q.update(snap, now);
    TEST_ASSERT_EQUAL(AlertPhase::Flash, v.phase);
    strcpy(first, v.sid);
    level = v.level;
    now = drain(q, now);
  }
  TEST_ASSERT_EQUAL_UINT8(2, level);
  setState(first, SessionState::Running);  // answered; the other one still waits
  now += 200000;
  const AlertView& v = q.update(snap, now);
  TEST_ASSERT_EQUAL(AlertPhase::Flash, v.phase);
  TEST_ASSERT_TRUE(strcmp(first, v.sid) != 0);
  TEST_ASSERT_EQUAL_UINT8(0, v.level);
  now = drain(q, now);
  // Push the second wait to level 2, then a new permission arrives: it starts plain.
  for (int i = 0; i < 4; i++) {
    now += 200000;
    q.update(snap, now);
    now = drain(q, now);
  }
  session("c", SessionState::Perm, 300);
  alert(50, AlertKind::Perm, "c");
  q.ingest(snap, now + 1);
  const AlertView& c = q.update(snap, now + 1);
  TEST_ASSERT_EQUAL_STRING("c", c.sid);
  TEST_ASSERT_EQUAL_UINT8(0, c.level);
}

// Property: random finishes, permissions and questions over 12 sessions, the focus hold going
// on and off, reminders on or off: every permission/question flashes and reaches its hero
// while its session still waits, within one alert cycle per alert queued ahead of it.
static void test_every_needs_you_alert_is_shown() {
  const int kSessions = 12;
  for (uint32_t seed = 1; seed <= 60; seed++) {
    uint32_t r = seed * 2654435761u;
    auto rnd = [&](uint32_t n) {
      r = r * 1103515245u + 12345u;
      return (r >> 16) % n;
    };
    AlertSequencer q;
    AlertTiming t;
    t.reminderMs = (seed % 2) ? 120000 : 0;
    q.setTiming(t);
    reset();
    char ids[kSessions][4];
    for (int i = 0; i < kSessions; i++) {
      snprintf(ids[i], sizeof(ids[i]), "s%d", i);
      session(ids[i], SessionState::Running, 100 + i);
    }
    uint32_t pendingId[kSessions] = {};   // alert id still to be shown for that session
    uint32_t pendingAt[kSessions] = {};   // when it came in
    bool sawFlash[kSessions] = {};
    const uint32_t cycle = t.flashMs + t.heroPermMs;
    const uint32_t bound = (kMaxAlerts + 1) * (2 * cycle) + 1000;
    uint32_t now = 0, nextId = 1;
    bool hold = false;
    for (int step = 0; step < 120; step++) {
      const uint32_t ev = rnd(10);
      const int k = (int)rnd(kSessions);
      snap.alertCount = 0;
      int waiting = 0;
      for (int i = 0; i < kSessions; i++) waiting += snap.sessions[i].st == SessionState::Perm ||
                                                     snap.sessions[i].st == SessionState::Question;
      if (ev < 4) {  // a session finishes (not one that waits on the user)
        if (!pendingId[k] && snap.sessions[k].st != SessionState::Perm && snap.sessions[k].st != SessionState::Question) {
          snap.sessions[k].st = SessionState::Done;
          alert(nextId++, AlertKind::Done, ids[k]);
        }
      } else if (ev < 7) {  // a session asks (at most kMaxAlerts waiting at once)
        if (snap.sessions[k].st != SessionState::Perm && snap.sessions[k].st != SessionState::Question &&
            waiting < kMaxAlerts) {
          const bool perm = rnd(2);
          snap.sessions[k].st = perm ? SessionState::Perm : SessionState::Question;
          pendingId[k] = nextId;
          pendingAt[k] = now;
          sawFlash[k] = false;
          alert(nextId++, perm ? AlertKind::Perm : AlertKind::Question, ids[k]);
        }
      } else if (ev < 8) {  // a session that was shown gets its answer
        if (!pendingId[k] && (snap.sessions[k].st == SessionState::Perm || snap.sessions[k].st == SessionState::Question)) {
          snap.sessions[k].st = SessionState::Running;
        }
      } else if (ev < 9) {
        hold = !hold;
      } else if (snap.sessions[k].st == SessionState::Done || snap.sessions[k].st == SessionState::Idle) {
        snap.sessions[k].st = SessionState::Running;  // a new prompt
      }
      snap.seq++;
      q.setModifiers({true, false, hold});
      q.ingest(snap, now);
      const uint32_t until = now + rnd(30000);
      for (; now < until; now += 100) {
        const AlertView& v = q.update(snap, now);
        for (int i = 0; i < kSessions; i++) {
          if (!pendingId[i] || strcmp(v.sid, ids[i]) != 0 || v.kind == AlertKind::Done) continue;
          if (v.phase == AlertPhase::Flash) sawFlash[i] = true;
          if (v.phase == AlertPhase::Hero && sawFlash[i]) pendingId[i] = 0;
        }
        for (int i = 0; i < kSessions; i++) {
          if (pendingId[i] && now - pendingAt[i] > bound) {
            char msg[64];
            snprintf(msg, sizeof(msg), "seed %u session %d alert %u", (unsigned)seed, i, (unsigned)pendingId[i]);
            TEST_FAIL_MESSAGE(msg);
          }
        }
      }
    }
  }
}

// Counts the needs-you flashes each session gets (a new flash = a Flash phase with a new start).
struct FlashCounter {
  int flashes[kMaxSessions] = {};
  AlertPhase lastPhase = AlertPhase::None;
  uint32_t lastStart = 0;
  char lastSid[9] = "";
  void see(const AlertView& v) {
    const bool fresh = v.phase == AlertPhase::Flash &&
                       (lastPhase != AlertPhase::Flash || lastStart != v.phaseStartMs || strcmp(lastSid, v.sid) != 0);
    if (fresh && v.kind != AlertKind::Done) {
      const int i = findSession(snap, v.sid);
      if (i >= 0) flashes[i]++;
    }
    lastPhase = v.phase;
    lastStart = v.phaseStartMs;
    strcpy(lastSid, v.sid);
  }
};

// 10 and 20 sessions start waiting in the same second, reminders off. The bridge's snapshot
// carries only the first 8 alert records (the parser's cap) and they expire after 30 s; every
// snapshot is ingested once a second. Each wait gets exactly one flash and its hero.
static void test_many_sessions_waiting_at_once_each_flash_exactly_once() {
  const int counts[] = {10, 20};
  for (int n : counts) {
    AlertSequencer q;
    AlertTiming t;
    t.reminderMs = 0;
    q.setTiming(t);
    reset();
    char ids[kMaxSessions][9];
    for (int i = 0; i < n; i++) {
      snprintf(ids[i], sizeof(ids[i]), "%08x", 0xa0000000u + (unsigned)i * 7919u);
      session(ids[i], i % 2 ? SessionState::Question : SessionState::Perm, 500);
    }
    for (int i = 0; i < n && i < kMaxAlerts; i++) {
      alert((uint32_t)i + 1, i % 2 ? AlertKind::Question : AlertKind::Perm, ids[i]);
    }
    FlashCounter fc;
    uint32_t now = 0;
    for (int sec = 0; sec < 600; sec++) {
      if (sec == 30) snap.alertCount = 0;  // the records expired on the bridge
      snap.seq++;
      q.ingest(snap, now);
      for (int k = 0; k < 10; k++, now += 100) fc.see(q.update(snap, now));
    }
    for (int i = 0; i < n; i++) {
      char msg[48];
      snprintf(msg, sizeof(msg), "n=%d session %d", n, i);
      TEST_ASSERT_EQUAL_INT_MESSAGE(1, fc.flashes[i], msg);
    }
  }
}

// A wait shown from the session list, then its alert record arrives late (or again, out of
// order): no second flash.
static void test_late_or_repeated_alert_record_does_not_flash_again() {
  AlertTiming t;
  t.reminderMs = 0;
  AlertSequencer q;
  q.setTiming(t);
  reset();
  session("1234abcd", SessionState::Perm, 700);
  q.ingest(snap, 0);
  FlashCounter fc;
  uint32_t now = 0;
  fc.see(q.update(snap, now));
  now = drain(q, now);
  alert(41, AlertKind::Perm, "1234abcd");  // late
  q.ingest(snap, now);
  for (int k = 0; k < 200; k++, now += 100) fc.see(q.update(snap, now));
  snap.alertCount = 0;
  alert(40, AlertKind::Perm, "1234abcd");  // an older id, out of order
  alert(42, AlertKind::Perm, "1234abcd");  // a newer one for the same wait
  q.ingest(snap, now);
  for (int k = 0; k < 200; k++, now += 100) fc.see(q.update(snap, now));
  TEST_ASSERT_EQUAL_INT(1, fc.flashes[0]);
}

// The alert record never reached the gadget (cut by the 8-record cap, expired after 30 s, or the
// snapshot carrying it was lost): the wait is still shown, from the session list.
static void test_wait_without_any_alert_record_is_still_shown() {
  AlertTiming t;
  t.reminderMs = 0;
  AlertSequencer q;
  q.setTiming(t);
  reset();
  session("r1", SessionState::Running, 100);
  q.ingest(snap, 0);
  TEST_ASSERT_EQUAL(AlertPhase::None, q.update(snap, 0).phase);
  setState("r1", SessionState::Question);
  snap.sessions[0].since = 160;
  snap.seq++;
  q.ingest(snap, 60000);
  const AlertView& v = q.update(snap, 60000);
  TEST_ASSERT_EQUAL(AlertPhase::Flash, v.phase);
  TEST_ASSERT_EQUAL(AlertKind::Question, v.kind);
  TEST_ASSERT_EQUAL_STRING("r1", v.sid);
  TEST_ASSERT_EQUAL(AlertPhase::Hero, q.update(snap, 60000 + t.flashMs).phase);
}

// Focus round: 8 "finished" alerts held fill the queue, then 5 sessions wait (no records): every
// wait is shown during the focus round, and the held "finished" still wait for the break.
static void test_waits_are_shown_during_focus_with_the_queue_full_of_held_finished() {
  AlertTiming t;
  t.reminderMs = 0;
  AlertSequencer q;
  q.setTiming(t);
  q.setModifiers({true, false, true});
  reset();
  char id[4];
  for (int i = 0; i < kMaxAlerts; i++) {
    snprintf(id, sizeof(id), "d%d", i);
    session(id, SessionState::Done);
    snap.alertCount = 0;
    alert((uint32_t)i + 1, AlertKind::Done, id);
    q.ingest(snap, 0);
  }
  TEST_ASSERT_EQUAL_UINT8(kMaxAlerts, q.queued());
  snap.alertCount = 0;
  for (int i = 0; i < 5; i++) {
    snprintf(id, sizeof(id), "w%d", i);
    session(id, i % 2 ? SessionState::Question : SessionState::Perm, 200 + (uint32_t)i);
  }
  FlashCounter fc;
  uint32_t now = 10;
  for (int sec = 0; sec < 120; sec++) {
    snap.seq++;
    q.ingest(snap, now);
    for (int k = 0; k < 10; k++, now += 100) {
      const AlertView& v = q.update(snap, now);
      TEST_ASSERT_FALSE(v.phase != AlertPhase::None && v.kind == AlertKind::Done);  // still held
      fc.see(v);
    }
  }
  for (int i = kMaxAlerts; i < kMaxAlerts + 5; i++) TEST_ASSERT_EQUAL_INT(1, fc.flashes[i]);
  TEST_ASSERT_EQUAL_UINT8(kMaxAlerts, q.queued());
}

// A session that stops waiting and waits again (a new `since`) is a new wait: shown again. The
// same wait with its session briefly missing (an alerts-only snapshot, or another paired
// computer's snapshot) is not.
static void test_new_wait_is_shown_again_and_a_missing_session_is_not_a_new_wait() {
  AlertTiming t;
  t.reminderMs = 0;
  AlertSequencer q;
  q.setTiming(t);
  reset();
  session("aa", SessionState::Perm, 100);
  session("bb", SessionState::Running, 100);
  FlashCounter fc;
  q.ingest(snap, 0);
  uint32_t now = 0;
  for (int k = 0; k < 200; k++, now += 100) fc.see(q.update(snap, now));
  TEST_ASSERT_EQUAL_INT(1, fc.flashes[0]);
  // Another computer's snapshot: "aa" is not in it.
  Snapshot mine = snap;
  reset(1);
  session("cc", SessionState::Running, 100);
  q.ingest(snap, now);
  for (int k = 0; k < 50; k++, now += 100) q.update(snap, now);
  snap = mine;
  snap.seq++;
  q.ingest(snap, now);
  for (int k = 0; k < 200; k++, now += 100) fc.see(q.update(snap, now));
  TEST_ASSERT_EQUAL_INT(1, fc.flashes[0]);
  // Answered, then a new wait.
  setState("aa", SessionState::Running);
  snap.sessions[0].since = 150;
  q.ingest(snap, now);
  for (int k = 0; k < 20; k++, now += 100) fc.see(q.update(snap, now));
  setState("aa", SessionState::Question);
  snap.sessions[0].since = 160;
  q.ingest(snap, now);
  for (int k = 0; k < 200; k++, now += 100) fc.see(q.update(snap, now));
  TEST_ASSERT_EQUAL_INT(2, fc.flashes[0]);
}

// ---- Randomized bridge + gadget model (adapted from the round-2 review harness) ----
// The bridge side models the plugin: sessions change state (`since` = when), every entry into
// perm/question/done adds an alert record (ids increasing) that lives 30 s; the snapshot sorts
// waiting sessions first, carries the first 8 records (the parser's cap); 1 in 8 snapshots is
// lost. The gadget side runs the real sequencer at ~10 updates per second. Checked: no wait is
// lost (each wait still open at the end was flashed and reached its hero), no wait flashes twice
// with reminders off, no "finished" shows twice.
namespace sim {

struct BridgeSession {
  char id[9];
  SessionState st;
  uint64_t sinceMs;
  int epoch;      // waits so far
  int doneEpoch;  // finishes so far
};
struct BridgeAlert {
  uint32_t id;
  AlertKind kind;
  int sidx;
  uint64_t created;
};
struct Cfg {
  int n;
  bool reminders, focus, meeting, insist, sameSec, terminalAnswers;
};
struct Stats {
  long lost = 0, ampDup = 0, doneDup = 0, heroMissing = 0, longUnshown = 0, starts = 0;
};

constexpr int kMaxEpochs = 400;
static Snapshot simSnap;
static uint8_t starts[kMaxSessions][kMaxEpochs];
static uint8_t dstarts[kMaxSessions][kMaxEpochs];
static bool hero[kMaxSessions][kMaxEpochs];
static uint64_t wstart[kMaxSessions][kMaxEpochs];

static bool waiting(SessionState s) { return s == SessionState::Perm || s == SessionState::Question; }

static void run(uint32_t seed, const Cfg& c, Stats& st) {
  uint32_t rs = seed * 2654435761u + 1;
  auto R = [&](uint32_t n) {
    rs ^= rs << 13;
    rs ^= rs >> 17;
    rs ^= rs << 5;
    return (int)(rs % n);
  };
  const uint64_t kEpoch0 = 1790000000000ull;
  BridgeSession ss[kMaxSessions];
  BridgeAlert al[64];
  int nal = 0;
  for (int i = 0; i < c.n; i++) {
    snprintf(ss[i].id, sizeof(ss[i].id), "%08x", (unsigned)(seed * 40503u + (uint32_t)i * 2246822519u));
    ss[i].st = SessionState::Idle;
    ss[i].sinceMs = kEpoch0;
    ss[i].epoch = ss[i].doneEpoch = 0;
  }
  memset(starts, 0, sizeof(starts));
  memset(dstarts, 0, sizeof(dstarts));
  memset(hero, 0, sizeof(hero));
  uint32_t nextId = 1, seq = 0;
  AlertSequencer q;
  AlertTiming t;
  if (!c.reminders) t.reminderMs = 0;
  q.setTiming(t);
  AlertModifiers m{c.insist, false, false};
  uint64_t nowMs = kEpoch0;
  uint32_t dev = seed * 977u;  // the gadget's millis(), from any offset
  auto dropAlertsOf = [&](int i) {
    int k = 0;
    for (int j = 0; j < nal; j++) if (al[j].sidx != i) al[k++] = al[j];
    nal = k;
  };
  auto enter = [&](int i, SessionState s2) {
    BridgeSession& b = ss[i];
    if (b.st == s2) return;
    if (waiting(b.st) && starts[i][b.epoch] == 0 && nowMs - wstart[i][b.epoch] > 400000) st.longUnshown++;
    b.st = s2;
    b.sinceMs = c.sameSec ? (nowMs / 1000) * 1000 : nowMs;
    dropAlertsOf(i);
    if (waiting(s2) && b.epoch + 1 < kMaxEpochs) {
      b.epoch++;
      wstart[i][b.epoch] = nowMs;
      al[nal++] = {nextId++, s2 == SessionState::Perm ? AlertKind::Perm : AlertKind::Question, i, nowMs};
    } else if (s2 == SessionState::Done && b.doneEpoch + 1 < kMaxEpochs) {
      b.doneEpoch++;
      al[nal++] = {nextId++, AlertKind::Done, i, nowMs};
    }
  };
  auto rank = [](SessionState s) {
    switch (s) {
      case SessionState::Perm: return 0;
      case SessionState::Question: return 1;
      case SessionState::Running: return 2;
      case SessionState::Done: return 3;
      default: return 4;
    }
  };
  auto build = [&]() {
    int k = 0;
    for (int j = 0; j < nal; j++) if (al[j].created + 30000 > nowMs) al[k++] = al[j];
    nal = k;
    int ord[kMaxSessions];
    for (int i = 0; i < c.n; i++) ord[i] = i;
    for (int i = 1; i < c.n; i++) {  // stable insertion sort, like the bridge's
      const int cur = ord[i];
      int j = i - 1;
      while (j >= 0 && (rank(ss[ord[j]].st) > rank(ss[cur].st) ||
                        (rank(ss[ord[j]].st) == rank(ss[cur].st) && ss[ord[j]].sinceMs > ss[cur].sinceMs))) {
        ord[j + 1] = ord[j];
        j--;
      }
      ord[j + 1] = cur;
    }
    simSnap.seq = ++seq;
    simSnap.now = (uint32_t)(nowMs / 1000);
    simSnap.count = 0;
    for (int o = 0; o < c.n; o++) {
      SessionRow& r = simSnap.sessions[simSnap.count++];
      memset(&r, 0, sizeof(r));
      strcpy(r.id, ss[ord[o]].id);
      r.st = ss[ord[o]].st;
      r.since = (uint32_t)(ss[ord[o]].sinceMs / 1000);
      r.ctx = -1;
      r.tok = -1;
    }
    simSnap.more = 0;
    simSnap.alertCount = 0;
    for (int j = 0; j < nal && simSnap.alertCount < kMaxAlerts; j++) {
      AlertItem& a = simSnap.alerts[simSnap.alertCount++];
      a.id = al[j].id;
      a.kind = al[j].kind;
      strcpy(a.sid, ss[al[j].sidx].id);
    }
  };
  auto idx = [&](const char* sid) {
    for (int i = 0; i < c.n; i++) if (!strcmp(ss[i].id, sid)) return i;
    return -1;
  };
  AlertPhase lp = AlertPhase::None;
  char lsid[9] = "";
  uint32_t lstart = 0;
  int lidx = -1, lep = -1;
  bool lamber = false;
  auto tick = [&]() {
    const AlertView& v = q.update(simSnap, dev);
    const bool fresh = v.phase == AlertPhase::Flash && (lp != AlertPhase::Flash || strcmp(lsid, v.sid) || lstart != v.phaseStartMs);
    if (fresh) {
      st.starts++;
      const int i = idx(v.sid);
      lamber = v.kind != AlertKind::Done;
      lidx = -1;
      const int di = findSession(simSnap, v.sid);
      // A flash counts for the bridge's current wait only when the gadget's snapshot shows that
      // wait (same `since`); one shown from an older snapshot is about an earlier wait.
      const bool current = i >= 0 && di >= 0 && simSnap.sessions[di].since == (uint32_t)(ss[i].sinceMs / 1000);
      if (current && lamber && ss[i].st == (v.kind == AlertKind::Perm ? SessionState::Perm : SessionState::Question)) {
        if (++starts[i][ss[i].epoch] == 2 && !c.reminders) st.ampDup++;
        lidx = i;
        lep = ss[i].epoch;
      } else if (i >= 0 && !lamber && ss[i].st == SessionState::Done) {
        if (++dstarts[i][ss[i].doneEpoch] == 2) st.doneDup++;
      }
    }
    if (v.phase == AlertPhase::Hero && lamber && lidx >= 0 && ss[lidx].epoch == lep) hero[lidx][lep] = true;
    lp = v.phase;
    strcpy(lsid, v.sid);
    lstart = v.phaseStartMs;
  };
  const int steps = 400;
  for (int s = 0; s < steps + 700; s++) {
    if (s >= steps) {  // drain: nothing changes any more, the focus round is over
      m.holdDone = false;
    } else {
      if (c.focus && R(60) == 0) m.holdDone = !m.holdDone;
      if (c.meeting && R(40) == 0) m.quietFlash = !m.quietFlash;
      const int burst = R(10) == 0 ? R(c.n) + 1 : 1;  // several sessions change in the same second
      for (int b = 0; b < burst; b++) {
        const int i = R(c.n);
        BridgeSession& x = ss[i];
        if (waiting(x.st)) {
          // answered once its hero showed, or now and then in the terminal, unseen
          if ((hero[i][x.epoch] && R(3) == 0) || (c.terminalAnswers && R(30) == 0)) {
            enter(i, R(2) ? SessionState::Running : SessionState::Idle);
          } else if (R(40) == 0) {
            enter(i, x.st == SessionState::Perm ? SessionState::Question : SessionState::Perm);
          }
        } else {
          const int r = R(10);
          if (r < 4) enter(i, R(2) ? SessionState::Perm : SessionState::Question);
          else if (r < 6) enter(i, SessionState::Done);
          else if (r < 8) enter(i, SessionState::Running);
          else enter(i, SessionState::Idle);
        }
      }
    }
    q.setModifiers(m);
    if (R(8) != 0) {  // 1 in 8 snapshots never arrives
      build();
      q.ingest(simSnap, dev);
    }
    const int ticks = 8 + R(5);
    for (int k = 0; k < ticks; k++) {
      tick();
      dev += 100 + (uint32_t)R(30);
      nowMs += 100;
    }
    nowMs = (nowMs / 1000 + 1) * 1000;
  }
  for (int i = 0; i < c.n; i++) {
    if (!waiting(ss[i].st)) continue;
    const int e = ss[i].epoch;
    if (starts[i][e] == 0) st.lost++;
    else if (!hero[i][e]) st.heroMissing++;
  }
}

}  // namespace sim

static void test_randomized_bridge_model_loses_and_duplicates_nothing() {
  const int ns[] = {1, 3, 8, 9, 10, 12, 16, 20};
  sim::Stats total;
  for (uint32_t k = 0; k < 320; k++) {
    auto bit = [&](int b) { return ((k >> b) & 1) == 1; };
    sim::Cfg c{ns[k % 8], bit(3), bit(4), bit(5), !bit(6), bit(7) != bit(8), k % 3 == 0};
    sim::Stats s;
    sim::run(k + 1, c, s);
    if (s.lost || s.ampDup || s.doneDup || s.heroMissing || s.longUnshown) {
      char msg[128];
      snprintf(msg, sizeof(msg), "seed %u n=%d rem=%d: lost=%ld dup=%ld doneDup=%ld heroMissing=%ld longUnshown=%ld",
               (unsigned)(k + 1), c.n, c.reminders, s.lost, s.ampDup, s.doneDup, s.heroMissing, s.longUnshown);
      TEST_FAIL_MESSAGE(msg);
    }
    total.starts += s.starts;
  }
  TEST_ASSERT_TRUE(total.starts > 1000);  // the model really exercised the sequencer
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_flash_then_hero_then_summary_for_permission);
  RUN_TEST(test_done_hero_is_shorter);
  RUN_TEST(test_dedupe_by_id);
  RUN_TEST(test_amber_before_blue_and_oldest_wait_first);
  RUN_TEST(test_answered_alert_ends_early_and_stale_queue_is_dropped);
  RUN_TEST(test_reminder_every_interval_while_pending);
  RUN_TEST(test_wait_seen_without_alert_record_is_shown_once_then_reminded);
  RUN_TEST(test_bridge_restart_resets_dedupe);
  RUN_TEST(test_disabled_alerts_show_nothing);
  RUN_TEST(test_insistence_steps);
  RUN_TEST(test_insistence_off_keeps_level_zero);
  RUN_TEST(test_insistence_off_and_meeting_single_blink);
  RUN_TEST(test_meeting_single_blink_keeps_the_red_level);
  RUN_TEST(test_hold_done_only_holds_finished);
  RUN_TEST(test_held_done_is_dropped_when_stale);
  RUN_TEST(test_extend_hero_for_the_fanfare);
  RUN_TEST(test_held_finished_never_crowds_out_a_permission);
  RUN_TEST(test_held_finished_shows_once_per_session);
  RUN_TEST(test_insistence_resets_per_session);
  RUN_TEST(test_every_needs_you_alert_is_shown);
  RUN_TEST(test_many_sessions_waiting_at_once_each_flash_exactly_once);
  RUN_TEST(test_late_or_repeated_alert_record_does_not_flash_again);
  RUN_TEST(test_wait_without_any_alert_record_is_still_shown);
  RUN_TEST(test_waits_are_shown_during_focus_with_the_queue_full_of_held_finished);
  RUN_TEST(test_new_wait_is_shown_again_and_a_missing_session_is_not_a_new_wait);
  RUN_TEST(test_randomized_bridge_model_loses_and_duplicates_nothing);
  return UNITY_END();
}
