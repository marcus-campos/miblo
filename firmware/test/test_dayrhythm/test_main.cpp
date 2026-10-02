// Track C: the day's rhythm — wellness nudges, the end of the work day, Monday's recap, the cat's mood.
#include <unity.h>

#include "miblo_config.h"
#include "miblo_wellness.h"

void setUp() {}
void tearDown() {}

static miblo::Config wellCfg(uint8_t breakAfter, uint8_t water, bool eyes) {
  miblo::Config c;
  c.breakAfterMin = breakAfter;
  c.waterMin = water;
  c.eyes = eyes;
  return c;
}
static constexpr uint32_t M = 60000;

// ---- WellnessClock ----

static void test_break_after_continuous_work_with_short_gaps() {
  miblo::WellnessClock w;
  const miblo::Config c = wellCfg(60, 0, false);
  uint32_t t = 0xFFFFFFFFu - 30 * M;  // crosses the millis() wrap halfway
  for (int i = 0; i < 51; i++, t += M) w.update(t, c, true, true, true);
  for (int i = 0; i < 9; i++, t += M) w.update(t, c, false, true, true);  // a 10 min gap: still continuous
  w.update(t, c, true, true, true);                                       // 60 min since the start
  TEST_ASSERT_EQUAL(miblo::Nudge::Break, w.showing(t));
  TEST_ASSERT_EQUAL(miblo::Nudge::Break, w.showing(t + miblo::kBreakNudgeMs - 1));
  TEST_ASSERT_EQUAL(miblo::Nudge::None, w.showing(t + miblo::kBreakNudgeMs));
}

static void test_long_gap_resets_and_blocked_nudge_is_skipped() {
  miblo::WellnessClock w;
  const miblo::Config c = wellCfg(60, 0, false);
  uint32_t t = 0;
  for (int i = 0; i < 59; i++, t += M) w.update(t, c, true, true, true);
  for (int i = 0; i < 11; i++, t += M) w.update(t, c, false, true, true);  // > 10 min: reset
  for (int i = 0; i < 60; i++, t += M) w.update(t, c, true, true, true);
  TEST_ASSERT_EQUAL(miblo::Nudge::None, w.showing(t));
  w.update(t, c, true, true, false);  // due now, but blocked (an alert, focus...)
  t += miblo::kNudgeWaitMs + M;
  w.update(t, c, true, true, false);
  w.update(t + 1, c, true, true, true);
  TEST_ASSERT_EQUAL(miblo::Nudge::None, w.showing(t + 1));  // skipped, cycle restarted
}

static void test_eyes_every_20_min_and_water_only_in_work_hours() {
  miblo::WellnessClock w;
  const miblo::Config c = wellCfg(0, 60, true);
  uint32_t t = 0;
  for (int i = 0; i <= 20; i++, t += M) w.update(t, c, true, false, true);
  TEST_ASSERT_EQUAL(miblo::Nudge::Eyes, w.showing(t - M));
  for (int i = 0; i < 120; i++, t += M) w.update(t, c, false, false, true);  // outside work hours
  TEST_ASSERT_EQUAL(miblo::Nudge::None, w.showing(t));
  for (int i = 0; i <= 60; i++, t += M) w.update(t, c, false, true, true);
  TEST_ASSERT_EQUAL(miblo::Nudge::Water, w.showing(t - M));
}

static void test_everything_off_by_default() {
  miblo::WellnessClock w;
  miblo::Config c;
  for (uint32_t t = 0; t < 600 * M; t += M) {
    w.update(t, c, true, true, true);
    TEST_ASSERT_EQUAL(miblo::Nudge::None, w.showing(t));
  }
}

// A blocked nudge shows as soon as it is allowed (within the wait), once.
static void test_blocked_nudge_shows_when_allowed_once() {
  miblo::WellnessClock w;
  const miblo::Config c = wellCfg(0, 0, true);
  uint32_t t = 0;
  for (int i = 0; i <= 20; i++, t += M) w.update(t, c, true, false, false);
  TEST_ASSERT_EQUAL(miblo::Nudge::None, w.showing(t - M));
  w.update(t, c, true, false, true);  // 1 min later: allowed
  TEST_ASSERT_EQUAL(miblo::Nudge::Eyes, w.showing(t));
  w.update(t + miblo::kEyesNudgeMs, c, true, false, true);
  TEST_ASSERT_EQUAL(miblo::Nudge::None, w.showing(t + miblo::kEyesNudgeMs));
  // The next one comes 20 min after it showed, not right away.
  w.update(t + 10 * M, c, true, false, true);
  TEST_ASSERT_EQUAL(miblo::Nudge::None, w.showing(t + 10 * M));
  w.update(t + 20 * M, c, true, false, true);
  TEST_ASSERT_EQUAL(miblo::Nudge::Eyes, w.showing(t + 20 * M));
}

// Break and water due together: the break shows first, the water right after it; the break also
// restarts the eye cycle (it rests the eyes too).
static void test_priority_break_then_water_and_break_restarts_eyes() {
  miblo::WellnessClock w;
  const miblo::Config c = wellCfg(60, 60, true);
  uint32_t t = 0;
  for (int i = 0; i < 60; i++, t += M) w.update(t, c, true, true, false);  // blocked: eyes skipped
  w.update(t, c, true, true, true);
  TEST_ASSERT_EQUAL(miblo::Nudge::Break, w.showing(t));
  w.update(t + M / 2, c, true, true, true);
  TEST_ASSERT_EQUAL(miblo::Nudge::Break, w.showing(t + M / 2));  // never replaced while showing
  t += miblo::kBreakNudgeMs;
  w.update(t, c, true, true, true);
  TEST_ASSERT_EQUAL(miblo::Nudge::Water, w.showing(t));
  t += miblo::kWaterNudgeMs;
  w.update(t, c, true, true, true);
  TEST_ASSERT_EQUAL(miblo::Nudge::None, w.showing(t));  // no eyes right after the break
}

// The loop held for a long time (OTA, a big parse): one nudge, never two of the same.
static void test_long_stall_fires_once() {
  miblo::WellnessClock w;
  const miblo::Config c = wellCfg(0, 60, false);
  w.update(0, c, false, true, true);
  w.update(5 * 60 * M, c, false, true, true);  // 5 h later
  TEST_ASSERT_EQUAL(miblo::Nudge::Water, w.showing(5 * 60 * M));
  const uint32_t after = 5 * 60 * M + miblo::kWaterNudgeMs;
  w.update(after, c, false, true, true);
  TEST_ASSERT_EQUAL(miblo::Nudge::None, w.showing(after));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_break_after_continuous_work_with_short_gaps);
  RUN_TEST(test_long_gap_resets_and_blocked_nudge_is_skipped);
  RUN_TEST(test_eyes_every_20_min_and_water_only_in_work_hours);
  RUN_TEST(test_everything_off_by_default);
  RUN_TEST(test_blocked_nudge_shows_when_allowed_once);
  RUN_TEST(test_priority_break_then_water_and_break_restarts_eyes);
  RUN_TEST(test_long_stall_fires_once);
  return UNITY_END();
}
