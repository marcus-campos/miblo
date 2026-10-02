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

static void test_amber_before_blue_and_perm_before_question() {
  AlertSequencer q;
  reset();
  session("done", SessionState::Done, 10);
  session("ask", SessionState::Question, 20);
  session("perm", SessionState::Perm, 30);
  alert(1, AlertKind::Done, "done");
  alert(2, AlertKind::Question, "ask");
  alert(3, AlertKind::Perm, "perm");
  q.ingest(snap, 0);
  TEST_ASSERT_EQUAL_STRING("perm", q.update(snap, 0).sid);
  q.update(snap, 1500);
  TEST_ASSERT_EQUAL_STRING("ask", q.update(snap, 11500).sid);
  q.update(snap, 13000);
  TEST_ASSERT_EQUAL_STRING("done", q.update(snap, 23000).sid);
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

static void test_reminder_for_pending_seen_without_alert_and_can_be_disabled() {
  AlertSequencer q;
  reset();
  session("x", SessionState::Question);
  q.ingest(snap, 0);  // the gadget booted with the pending item already in progress (alert expired on the bridge)
  TEST_ASSERT_EQUAL(AlertPhase::None, q.update(snap, 1000).phase);
  TEST_ASSERT_EQUAL(AlertKind::Question, q.update(snap, 121000).kind);

  AlertSequencer off;
  AlertTiming t;
  t.reminderMs = 0;
  off.setTiming(t);
  off.ingest(snap, 0);
  off.update(snap, 1000);
  TEST_ASSERT_EQUAL(AlertPhase::None, off.update(snap, 999999).phase);
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

static void removeSession(const char* id) {
  for (int i = 0; i < snap.count; i++) {
    if (strcmp(snap.sessions[i].id, id) != 0) continue;
    for (int j = i + 1; j < snap.count; j++) snap.sessions[j - 1] = snap.sessions[j];
    snap.count--;
    return;
  }
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
  uint32_t now = 0;
  q.update(snap, now);
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
  now += 1 + t.reminderMs + 1;
  q.update(snap, now);
  const AlertView& v = q.update(snap, now + t.reminderMs);
  TEST_ASSERT_EQUAL(AlertPhase::Flash, v.phase);
  TEST_ASSERT_EQUAL_UINT8(0, v.level);
}

// With insistence off every reminder stays plain.
static void test_insistence_off_keeps_level_zero() {
  AlertSequencer q;
  q.setModifiers({false, false, false});
  reset();
  session("a", SessionState::Question);
  uint32_t now = 0;
  q.update(snap, now);
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
  uint32_t now = 0;
  q.update(snap, now);
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
  uint32_t now = 0;
  q.update(snap, now);
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

// N sessions ask one at a time, faster than the alerts can play, with no reminders: the queue
// (8) overflows, yet every one of them gets its flash and hero, once.
static void allWaitingAreShown(int n) {
  AlertSequencer q;
  AlertTiming t;
  t.reminderMs = 0;
  q.setTiming(t);
  reset();
  char ids[kMaxSessions][4];
  int shown[kMaxSessions] = {};
  char last[9] = "";
  AlertPhase lastPhase = AlertPhase::None;
  auto step = [&](uint32_t now) {
    const AlertView& v = q.update(snap, now);
    if (v.phase == AlertPhase::Flash && (lastPhase != AlertPhase::Flash || strcmp(last, v.sid) != 0)) {
      for (int i = 0; i < n; i++) {
        if (strcmp(ids[i], v.sid) == 0) shown[i]++;
      }
    }
    lastPhase = v.phase;
    strcpy(last, v.sid);
  };
  for (int i = 0; i < n; i++) {
    snprintf(ids[i], sizeof(ids[i]), "p%d", i);
    session(ids[i], SessionState::Perm, 1000 + (uint32_t)i);
    snap.alertCount = 0;
    alert((uint32_t)i + 1, AlertKind::Perm, ids[i]);
    snap.seq++;
    q.ingest(snap, (uint32_t)i * 100);
    step((uint32_t)i * 100);
  }
  for (uint32_t now = (uint32_t)n * 100; now < 400u * 1000u; now += 100) step(now);
  for (int i = 0; i < n; i++) {
    char msg[48];
    snprintf(msg, sizeof(msg), "%d waiting: %s", n, ids[i]);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, shown[i], msg);
  }
}

static void test_ten_and_twenty_waiting_are_all_shown() {
  allWaitingAreShown(10);
  allWaitingAreShown(kMaxSessions);
}

// Property: random finishes, permissions and questions over 20 sessions (up to all 20 waiting at
// once), the focus hold going on and off, no reminders (they would hide a lost alert), alerts
// delivered like the bridge does (each one resent in every snapshot for 30 s, several per
// snapshot, the newest 8). Every permission/question gets its flash and hero while its session
// still waits, within one alert cycle per needs-you alert that goes before it (plus what is on
// screen when it comes in): the sequencer never idles or plays a "finished" while it waits.
static void test_every_needs_you_alert_is_shown() {
  const int kSessions = kMaxSessions;
  for (uint32_t seed = 1; seed <= 500; seed++) {
    uint32_t r = seed * 2654435761u;
    auto rnd = [&](uint32_t n) {
      r = r * 1103515245u + 12345u;
      return (r >> 16) % n;
    };
    AlertSequencer q;
    AlertTiming t;
    t.reminderMs = 0;
    q.setTiming(t);
    reset();
    char ids[kSessions][4];
    for (int i = 0; i < kSessions; i++) {
      snprintf(ids[i], sizeof(ids[i]), "s%d", i);
      session(ids[i], SessionState::Running, 100);
    }
    struct Sent {
      uint32_t id, atMs;
      AlertKind kind;
      int k;
    };
    Sent sent[64];
    int nSent = 0;
    uint32_t pendingId[kSessions] = {};  // the needs-you alert still to be shown
    uint32_t deadline[kSessions] = {};
    bool sawFlash[kSessions] = {};
    const uint32_t cycle = t.flashMs + t.heroPermMs;
    const uint32_t doneCycle = t.flashMs + t.heroDoneMs;
    uint32_t now = 0, nextId = 1;
    bool hold = false;
    AlertPhase prevPhase = AlertPhase::None;
    char prevSid[9] = "";
    auto waits = [&](int k) {
      return snap.sessions[k].st == SessionState::Perm || snap.sessions[k].st == SessionState::Question;
    };
    for (int step = 0; step < 150; step++) {
      const uint32_t events = 1 + rnd(5);  // several changes in one snapshot
      for (uint32_t e = 0; e < events; e++) {
        const uint32_t ev = rnd(10);
        const int k = (int)rnd(kSessions);
        if (ev < 3) {  // a session finishes
          if (!waits(k)) {
            snap.sessions[k].st = SessionState::Done;
            snap.sessions[k].since = 100 + now / 1000;
            if (nSent < 64) sent[nSent++] = {nextId, now, AlertKind::Done, k};
            nextId++;
          }
        } else if (ev < 7) {  // a session asks
          if (!waits(k)) {
            const bool perm = rnd(2);
            snap.sessions[k].st = perm ? SessionState::Perm : SessionState::Question;
            snap.sessions[k].since = 100 + now / 1000;
            pendingId[k] = nextId;
            sawFlash[k] = false;
            // What is on screen (a "finished" at most) and its own cycle; every other needs-you
            // alert that starts while it waits adds one cycle (see below).
            deadline[k] = now + doneCycle + cycle + 1000;
            if (nSent < 64) sent[nSent++] = {nextId, now, perm ? AlertKind::Perm : AlertKind::Question, k};
            nextId++;
          }
        } else if (ev < 8) {  // an answer, once it was shown
          if (waits(k) && !pendingId[k]) snap.sessions[k].st = SessionState::Running;
        } else if (ev < 9) {
          hold = !hold;
        } else if (snap.sessions[k].st == SessionState::Done || snap.sessions[k].st == SessionState::Idle) {
          snap.sessions[k].st = SessionState::Running;  // a new prompt
        }
      }
      // The bridge's alert list: what was raised in the last 30 s, the newest 8.
      int keep = 0;
      for (int i = 0; i < nSent; i++) {
        if (now - sent[i].atMs <= 30000) sent[keep++] = sent[i];
      }
      nSent = keep;
      snap.alertCount = 0;
      for (int i = nSent > kMaxAlerts ? nSent - kMaxAlerts : 0; i < nSent; i++) {
        alert(sent[i].id, sent[i].kind, ids[sent[i].k]);
      }
      snap.seq++;
      q.setModifiers({true, false, hold});
      q.ingest(snap, now);
      const uint32_t until = now + 100 + rnd(12000);
      for (; now < until; now += 100) {
        const AlertView& v = q.update(snap, now);
        const bool started = v.phase == AlertPhase::Flash && (prevPhase != AlertPhase::Flash || strcmp(prevSid, v.sid));
        prevPhase = v.phase;
        strcpy(prevSid, v.sid);
        if (started && v.kind != AlertKind::Done) {  // another needs-you ahead: one more cycle
          for (int i = 0; i < kSessions; i++) {
            if (pendingId[i] && strcmp(v.sid, ids[i]) != 0) deadline[i] += cycle;
          }
        }
        for (int i = 0; i < kSessions; i++) {
          if (!pendingId[i] || strcmp(v.sid, ids[i]) != 0 || v.kind == AlertKind::Done) continue;
          if (v.phase == AlertPhase::Flash) sawFlash[i] = true;
          if (v.phase == AlertPhase::Hero && sawFlash[i]) pendingId[i] = 0;
        }
        for (int i = 0; i < kSessions; i++) {
          if (pendingId[i] && (int32_t)(now - deadline[i]) > 0) {
            char msg[64];
            snprintf(msg, sizeof(msg), "seed %u session %d alert %u", (unsigned)seed, i, (unsigned)pendingId[i]);
            TEST_FAIL_MESSAGE(msg);
          }
        }
      }
    }
    // Let everything play out: nothing raised is ever lost.
    for (uint32_t end = now + 30u * 60000u; now < end; now += 100) {
      const AlertView& v = q.update(snap, now);
      for (int i = 0; i < kSessions; i++) {
        if (!pendingId[i] || strcmp(v.sid, ids[i]) != 0 || v.kind == AlertKind::Done) continue;
        if (v.phase == AlertPhase::Flash) sawFlash[i] = true;
        if (v.phase == AlertPhase::Hero && sawFlash[i]) pendingId[i] = 0;
      }
    }
    for (int i = 0; i < kSessions; i++) {
      char msg[64];
      snprintf(msg, sizeof(msg), "seed %u session %d never shown", (unsigned)seed, i);
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, pendingId[i], msg);
    }
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_flash_then_hero_then_summary_for_permission);
  RUN_TEST(test_done_hero_is_shorter);
  RUN_TEST(test_dedupe_by_id);
  RUN_TEST(test_amber_before_blue_and_perm_before_question);
  RUN_TEST(test_answered_alert_ends_early_and_stale_queue_is_dropped);
  RUN_TEST(test_reminder_every_interval_while_pending);
  RUN_TEST(test_reminder_for_pending_seen_without_alert_and_can_be_disabled);
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
  RUN_TEST(test_ten_and_twenty_waiting_are_all_shown);
  RUN_TEST(test_every_needs_you_alert_is_shown);
  return UNITY_END();
}
