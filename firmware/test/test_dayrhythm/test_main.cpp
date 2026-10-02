// Track C: the day's rhythm — wellness nudges, the end of the work day, Monday's recap, the cat's mood.
#include <unity.h>

#include "miblo_config.h"
#include "miblo_dayend.h"
#include "miblo_mood.h"
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

// ---- EndOfDay, work hours, WeeklyRecap ----

static uint32_t key(uint16_t y, uint8_t m, uint8_t d) { return y * 400u + m * 32u + d; }

static void test_end_of_day_once_after_running_session() {
  miblo::Config c;
  c.endOfDay = true;
  miblo::EndOfDay e;
  const uint32_t k = key(2026, 10, 2);  // a Friday
  e.update(0, c, k, 5, 17 * 60 + 59, false, true);
  TEST_ASSERT_FALSE(e.showing(0));
  e.update(1000, c, k, 5, 18 * 60, true, true);  // still running: waits
  TEST_ASSERT_FALSE(e.showing(1000));
  e.update(2000, c, k, 5, 18 * 60 + 20, false, true);
  TEST_ASSERT_TRUE(e.showing(2000));
  TEST_ASSERT_FALSE(e.showing(2000 + miblo::EndOfDay::kShowMs));
  e.update(5000000, c, k, 5, 19 * 60, false, true);
  TEST_ASSERT_FALSE(e.showing(5000000));  // once per day
  TEST_ASSERT_EQUAL_UINT8(5, e.petMinutes(c, k));
  TEST_ASSERT_EQUAL_UINT8(c.petMin, e.petMinutes(c, key(2026, 10, 3)));
}

static void test_end_of_day_rules() {
  miblo::Config c;
  miblo::EndOfDay e;
  e.update(0, c, key(2026, 10, 2), 5, 19 * 60, false, true);
  TEST_ASSERT_FALSE(e.showing(0));  // off by default
  c.endOfDay = true;
  e.update(0, c, key(2026, 10, 3), 6, 19 * 60, false, true);
  TEST_ASSERT_FALSE(e.showing(0));  // Saturday is not a work day
  e.update(0, c, 0, 5, 19 * 60, false, true);
  TEST_ASSERT_FALSE(e.showing(0));  // time unknown
  miblo::EndOfDay w;
  w.update(0, c, key(2026, 10, 2), 5, 18 * 60, true, true);
  w.update(miblo::EndOfDay::kMaxWaitMs, c, key(2026, 10, 2), 5, 19 * 60, true, true);
  TEST_ASSERT_TRUE(w.showing(miblo::EndOfDay::kMaxWaitMs));  // waited an hour: shows anyway
}

// Blocked (focus, an alert...) it waits for a gap; it never shows with the time unknown, and a
// short pet delay (petMin below 5) is kept.
static void test_end_of_day_waits_for_allowed_and_wraps() {
  miblo::Config c;
  c.endOfDay = true;
  c.petMin = 3;
  miblo::EndOfDay e;
  const uint32_t k = key(2026, 10, 2);
  const uint32_t t = 0xFFFFFFFFu - 1000;  // shows across the millis() wrap
  e.update(t, c, k, 5, 18 * 60, false, false);
  TEST_ASSERT_FALSE(e.showing(t));
  e.update(t + 500, c, 0, 5, -1, false, true);  // the clock went away: stays quiet
  TEST_ASSERT_FALSE(e.showing(t + 500));
  e.update(t + 600, c, k, 5, 18 * 60 + 1, false, true);
  TEST_ASSERT_TRUE(e.showing(t + 600));
  TEST_ASSERT_TRUE(e.showing(t + 600 + miblo::EndOfDay::kShowMs - 1));
  TEST_ASSERT_FALSE(e.showing(t + 600 + miblo::EndOfDay::kShowMs));
  TEST_ASSERT_EQUAL_UINT8(3, e.petMinutes(c, k));
}

static void test_work_hours() {
  miblo::Config c;
  TEST_ASSERT_TRUE(miblo::inWorkHours(c, 1, 9 * 60));
  TEST_ASSERT_FALSE(miblo::inWorkHours(c, 1, 18 * 60));
  TEST_ASSERT_FALSE(miblo::inWorkHours(c, 0, 10 * 60));
  TEST_ASSERT_FALSE(miblo::inWorkHours(c, 1, -1));
  TEST_ASSERT_FALSE(miblo::inWorkHours(c, 1, 8 * 60 + 59));
  TEST_ASSERT_TRUE(miblo::workDay(c, 5));
  TEST_ASSERT_FALSE(miblo::workDay(c, 6));
  TEST_ASSERT_FALSE(miblo::workDay(c, 7));  // out of range
}

static void test_weekly_recap_on_monday() {
  miblo::WeeklyRecap r;
  r.update(0, true, key(2026, 10, 5), 1, 8 * 60, false, true, true);
  TEST_ASSERT_FALSE(r.showing(0));
  r.update(10, true, key(2026, 10, 5), 1, 8 * 60, true, true, true);  // first activity of Monday
  TEST_ASSERT_TRUE(r.showing(10));
  r.update(100000, true, key(2026, 10, 5), 1, 10 * 60, false, true, true);
  TEST_ASSERT_FALSE(r.showing(100000));
  miblo::WeeklyRecap q;
  q.update(0, true, key(2026, 10, 6), 2, 10 * 60, true, true, true);
  TEST_ASSERT_FALSE(q.showing(0));  // Tuesday
  q.update(0, true, key(2026, 10, 12), 1, 9 * 60, false, false, true);
  TEST_ASSERT_FALSE(q.showing(0));  // no `week` from the bridge
  q.update(0, false, key(2026, 10, 12), 1, 9 * 60, false, true, true);
  TEST_ASSERT_FALSE(q.showing(0));  // turned off
}

// Before 05:00 activity does not count; at 09:00 it shows anyway; blocked it waits; time unknown
// never shows.
static void test_weekly_recap_rules() {
  miblo::WeeklyRecap r;
  const uint32_t k = key(2026, 10, 5);
  r.update(0, true, k, 1, 4 * 60 + 59, true, true, true);
  TEST_ASSERT_FALSE(r.showing(0));
  r.update(1, true, 0, 1, -1, true, true, true);
  TEST_ASSERT_FALSE(r.showing(1));
  r.update(2, true, k, 1, 9 * 60, false, true, false);
  TEST_ASSERT_FALSE(r.showing(2));
  r.update(3, true, k, 1, 9 * 60 + 1, false, true, true);
  TEST_ASSERT_TRUE(r.showing(3));
  r.update(3 + miblo::WeeklyRecap::kShowMs, true, k, 1, 9 * 60 + 2, false, true, true);
  TEST_ASSERT_FALSE(r.showing(3 + miblo::WeeklyRecap::kShowMs));
  r.update(9000000, true, key(2026, 10, 12), 1, 9 * 60, false, true, true);  // next Monday
  TEST_ASSERT_TRUE(r.showing(9000000));
}

// ---- Cat mood ----

static void test_cat_mood() {
  miblo::Snapshot s{};
  s.hasUsage = true;
  s.h5 = {true, 30, 2000, 0};
  s.d7 = {true, 40, 9000, 0};
  s.todayWorkSec = 3600;
  TEST_ASSERT_EQUAL(miblo::CatMood::Playful, miblo::catMoodFor(s, 1000));
  s.d7.pct = 51;
  TEST_ASSERT_EQUAL(miblo::CatMood::Normal, miblo::catMoodFor(s, 1000));
  s.d7.reset = 900;  // the week reset already: counts as 0
  TEST_ASSERT_EQUAL(miblo::CatMood::Playful, miblo::catMoodFor(s, 1000));
  s.todayWorkSec = 8 * 3600;
  TEST_ASSERT_EQUAL(miblo::CatMood::Tired, miblo::catMoodFor(s, 1000));
  s.todayWorkSec = 2 * 3600;
  TEST_ASSERT_EQUAL(miblo::CatMood::Normal, miblo::catMoodFor(s, 1000));
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
  RUN_TEST(test_end_of_day_once_after_running_session);
  RUN_TEST(test_end_of_day_rules);
  RUN_TEST(test_end_of_day_waits_for_allowed_and_wraps);
  RUN_TEST(test_work_hours);
  RUN_TEST(test_weekly_recap_on_monday);
  RUN_TEST(test_weekly_recap_rules);
  RUN_TEST(test_cat_mood);
  return UNITY_END();
}
