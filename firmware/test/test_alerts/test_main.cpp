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
  return UNITY_END();
}
