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
  return UNITY_END();
}
