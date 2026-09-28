#include <string.h>
#include <unity.h>

#include "miblo_overview.h"

using namespace miblo;

void setUp() {}
void tearDown() {}

static Snapshot snap;

static void reset() { memset(&snap, 0, sizeof(snap)); }

static void add(const char* id, SessionState st, uint32_t since) {
  SessionRow& r = snap.sessions[snap.count++];
  memset(&r, 0, sizeof(r));
  strcpy(r.id, id);
  strcpy(r.name, id);
  r.st = st;
  r.since = since;
  r.ctx = -1;
  r.tok = -1;
}

static void test_classify_and_counts() {
  reset();
  TEST_ASSERT_EQUAL(OverviewKind::Idle, classifyOverview(snap));
  add("a", SessionState::Idle, 1);
  add("b", SessionState::Done, 2);
  TEST_ASSERT_EQUAL(OverviewKind::Idle, classifyOverview(snap));
  add("c", SessionState::Running, 3);
  TEST_ASSERT_EQUAL(OverviewKind::Working, classifyOverview(snap));
  add("d", SessionState::Question, 4);
  TEST_ASSERT_EQUAL(OverviewKind::Attention, classifyOverview(snap));
  StateCounts c = countStates(snap);
  TEST_ASSERT_EQUAL_UINT8(1, c.pending);
  TEST_ASSERT_EQUAL_UINT8(1, c.running);
  TEST_ASSERT_EQUAL_UINT8(1, c.done);
  TEST_ASSERT_EQUAL_UINT8(1, c.idle);
}

static void test_hero_priority_and_tie_break() {
  reset();
  add("run", SessionState::Running, 1);
  add("done", SessionState::Done, 2);
  TEST_ASSERT_EQUAL_INT(-1, selectHero(snap, false));
  TEST_ASSERT_EQUAL_INT(1, selectHero(snap, true));
  add("q", SessionState::Question, 3);
  TEST_ASSERT_EQUAL_INT(2, selectHero(snap, true));
  add("p-new", SessionState::Perm, 50);
  add("p-old", SessionState::Perm, 10);
  TEST_ASSERT_EQUAL_INT(4, selectHero(snap, false));  // quem espera há mais tempo
  TEST_ASSERT_EQUAL_INT(4, selectHero(snap, true));
}

static void test_last_finished() {
  reset();
  TEST_ASSERT_EQUAL_INT(-1, lastFinished(snap));
  add("a", SessionState::Done, 10);
  add("b", SessionState::Done, 30);
  add("c", SessionState::Running, 40);
  TEST_ASSERT_EQUAL_INT(1, lastFinished(snap));
}

static void test_run_tracker_measures_a_full_response() {
  RunTracker rt;
  uint32_t dur;
  reset();
  add("s1", SessionState::Idle, 100);
  rt.observe(snap);
  snap.sessions[0].st = SessionState::Running;
  snap.sessions[0].since = 200;
  rt.observe(snap);
  snap.sessions[0].st = SessionState::Perm;  // pendência no meio não reinicia a contagem
  snap.sessions[0].since = 230;
  rt.observe(snap);
  TEST_ASSERT_FALSE(rt.stats("s1", dur));
  snap.sessions[0].st = SessionState::Done;
  snap.sessions[0].since = 458;
  rt.observe(snap);
  TEST_ASSERT_TRUE(rt.stats("s1", dur));
  TEST_ASSERT_EQUAL_UINT32(258, dur);
  snap.sessions[0].st = SessionState::Idle;  // novo ciclo: estatística some
  rt.observe(snap);
  TEST_ASSERT_FALSE(rt.stats("s1", dur));
}

static void test_run_tracker_unknown_start_and_forgetting() {
  RunTracker rt;
  uint32_t dur;
  reset();
  add("late", SessionState::Done, 500);  // já chegou terminado: sem estatística
  add("run", SessionState::Running, 100);
  rt.observe(snap);
  TEST_ASSERT_FALSE(rt.stats("late", dur));
  snap.sessions[1].st = SessionState::Done;
  snap.sessions[1].since = 160;
  rt.observe(snap);
  TEST_ASSERT_TRUE(rt.stats("run", dur));
  TEST_ASSERT_EQUAL_UINT32(60, dur);
  reset();  // sessão sumiu → esquecida
  rt.observe(snap);
  TEST_ASSERT_FALSE(rt.stats("run", dur));
}

static void test_pager_rotates_every_period() {
  Pager p(4, 5000);
  TEST_ASSERT_EQUAL_UINT8(1, p.pageCount(0));
  TEST_ASSERT_EQUAL_UINT8(1, p.pageCount(4));
  TEST_ASSERT_EQUAL_UINT8(3, p.pageCount(9));
  TEST_ASSERT_EQUAL_UINT8(0, p.update(9, 1000));
  TEST_ASSERT_EQUAL_UINT8(0, p.update(9, 5999));
  TEST_ASSERT_EQUAL_UINT8(1, p.update(9, 6000));
  TEST_ASSERT_EQUAL_UINT8(2, p.update(9, 11000));
  TEST_ASSERT_EQUAL_UINT8(0, p.update(9, 16000));
  TEST_ASSERT_EQUAL_UINT8(1, p.update(9, 21000));
  TEST_ASSERT_EQUAL_UINT8(0, p.update(3, 21001));  // lista encolheu: volta para a página 0
  TEST_ASSERT_EQUAL_UINT8(0, p.update(3, 60000));
}

static void test_region_cache() {
  RegionCache rc;
  uint32_t h1 = hashStr(kHashSeed, "62%");
  uint32_t h2 = hashStr(kHashSeed, "63%");
  TEST_ASSERT_TRUE(rc.changed(0, h1));
  TEST_ASSERT_FALSE(rc.changed(0, h1));
  TEST_ASSERT_TRUE(rc.changed(0, h2));
  TEST_ASSERT_TRUE(rc.changed(1, h2));
  rc.invalidate();
  TEST_ASSERT_TRUE(rc.changed(0, h2));
  TEST_ASSERT_TRUE(rc.changed(200, h2));  // fora do intervalo: sempre desenha
  TEST_ASSERT_NOT_EQUAL(hashStr(hashStr(kHashSeed, "ab"), "c"), hashStr(hashStr(kHashSeed, "a"), "bc"));
  TEST_ASSERT_NOT_EQUAL(hashInt(kHashSeed, 1), hashInt(kHashSeed, 2));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_classify_and_counts);
  RUN_TEST(test_hero_priority_and_tie_break);
  RUN_TEST(test_last_finished);
  RUN_TEST(test_run_tracker_measures_a_full_response);
  RUN_TEST(test_run_tracker_unknown_start_and_forgetting);
  RUN_TEST(test_pager_rotates_every_period);
  RUN_TEST(test_region_cache);
  return UNITY_END();
}
