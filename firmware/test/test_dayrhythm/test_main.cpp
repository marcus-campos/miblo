// The day's rhythm — wellness nudges, the end of the work day, Monday's recap, the cat's mood.
#include <string.h>
#include <unity.h>

#include "../support/fake_canvas.h"
#include "miblo_config.h"
#include "miblo_dayend.h"
#include "miblo_mood.h"
#include "miblo_wellness.h"
#include "ui_screens.h"

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
  e.update(0, c, k, 5, 17 * 60 + 59, false, true, true);
  TEST_ASSERT_FALSE(e.showing(0));
  e.update(1000, c, k, 5, 18 * 60, true, true, true);  // still running: waits
  TEST_ASSERT_FALSE(e.showing(1000));
  e.update(2000, c, k, 5, 18 * 60 + 20, false, true, true);
  TEST_ASSERT_TRUE(e.showing(2000));
  TEST_ASSERT_FALSE(e.showing(2000 + miblo::EndOfDay::kShowMs));
  e.update(5000000, c, k, 5, 19 * 60, false, true, true);
  TEST_ASSERT_FALSE(e.showing(5000000));  // once per day
  TEST_ASSERT_EQUAL_UINT8(5, e.petMinutes(c, k));
  TEST_ASSERT_EQUAL_UINT8(c.petMin, e.petMinutes(c, key(2026, 10, 3)));
}

static void test_end_of_day_rules() {
  miblo::Config c;
  miblo::EndOfDay e;
  e.update(0, c, key(2026, 10, 2), 5, 19 * 60, false, true, true);
  TEST_ASSERT_FALSE(e.showing(0));  // off by default
  c.endOfDay = true;
  e.update(0, c, key(2026, 10, 3), 6, 19 * 60, false, true, true);
  TEST_ASSERT_FALSE(e.showing(0));  // Saturday is not a work day
  e.update(0, c, 0, 5, 19 * 60, false, true, true);
  TEST_ASSERT_FALSE(e.showing(0));  // time unknown
  miblo::EndOfDay w;
  w.update(0, c, key(2026, 10, 2), 5, 18 * 60, true, true, true);
  w.update(miblo::EndOfDay::kMaxWaitMs, c, key(2026, 10, 2), 5, 19 * 60, true, true, true);
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
  e.update(t, c, k, 5, 18 * 60, false, true, false);
  TEST_ASSERT_FALSE(e.showing(t));
  e.update(t + 500, c, 0, 5, -1, false, true, true);  // the clock went away: stays quiet
  TEST_ASSERT_FALSE(e.showing(t + 500));
  e.update(t + 600, c, k, 5, 18 * 60 + 1, false, true, true);
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

// Claude stopped (the person may already be resting): a break or eye rest never comes due in
// the gap; it comes when work resumes.
static void test_break_not_due_in_a_gap() {
  miblo::WellnessClock w;
  const miblo::Config c = wellCfg(60, 0, true);
  uint32_t t = 0;
  for (int i = 0; i <= 55; i++, t += M) w.update(t, c, true, false, true);
  for (int i = 0; i < 7; i++, t += M) {  // 56..62 min: no session running
    w.update(t, c, false, false, true);
    TEST_ASSERT_EQUAL(miblo::Nudge::None, w.showing(t));
  }
  w.update(t, c, true, false, true);  // 63 min, the gap was under 10 min: still continuous
  TEST_ASSERT_EQUAL(miblo::Nudge::Break, w.showing(t));
}

// A higher nudge coming due does not restart a lower one's wait: nothing waits over 5 min.
static void test_blocked_wait_never_restarts() {
  miblo::WellnessClock w;
  const miblo::Config c = wellCfg(60, 60, false);
  uint32_t t = 0;
  w.update(t, c, false, true, false);  // work hours from 0: water due at 60 min
  for (t = 2 * M; t <= 65 * M; t += M) w.update(t, c, true, true, false);  // break due at 62 min
  w.update(66 * M, c, true, true, false);  // water has waited 6 min: both skipped
  w.update(66 * M + 1, c, true, true, true);
  TEST_ASSERT_EQUAL(miblo::Nudge::None, w.showing(66 * M + 1));
  w.update(67 * M, c, true, true, true);
  TEST_ASSERT_EQUAL(miblo::Nudge::None, w.showing(67 * M));
}

// The hour of waiting for a running session starts when the summary may first take the screen,
// not while something else (focus, alerts) held it.
static void test_end_of_day_wait_starts_when_allowed() {
  miblo::Config c;
  c.endOfDay = true;
  miblo::EndOfDay e;
  const uint32_t k = key(2026, 10, 2);
  const uint32_t H = 3600000;
  const uint32_t half = H / 2;
  e.update(0, c, k, 5, 18 * 60, true, true, false);
  e.update(half, c, k, 5, 18 * 60 + 30, true, true, true);  // allowed for the first time, still running
  TEST_ASSERT_FALSE(e.showing(half));
  e.update(half + H - 1, c, k, 5, 19 * 60 + 29, true, true, true);
  TEST_ASSERT_FALSE(e.showing(half + H - 1));
  e.update(half + H, c, k, 5, 19 * 60 + 30, true, true, true);
  TEST_ASSERT_TRUE(e.showing(half + H));
}

// Only in the two hours after workTo: a restart later that evening (an update, a power blip) or
// plugging the gadget in at night does not bring the day's summary back.
static void test_end_of_day_only_shortly_after_work() {
  miblo::Config c;
  c.endOfDay = true;
  const uint32_t k = key(2026, 10, 2);
  miblo::EndOfDay late;
  late.update(0, c, k, 5, 20 * 60, false, true, true);  // 18:00 + 2 h: too late
  TEST_ASSERT_FALSE(late.showing(0));
  late.update(1000, c, k, 5, 23 * 60, false, true, true);
  TEST_ASSERT_FALSE(late.showing(1000));
  miblo::EndOfDay edge;
  edge.update(0, c, k, 5, 20 * 60 - 1, false, true, true);
  TEST_ASSERT_TRUE(edge.showing(0));
  // A running session still holding it when the window closes: the summary is skipped.
  miblo::EndOfDay held;
  held.update(0, c, k, 5, 19 * 60 + 30, true, true, true);
  held.update(1800000, c, k, 5, 20 * 60, true, true, true);
  held.update(1800001, c, k, 5, 20 * 60, false, true, true);
  TEST_ASSERT_FALSE(held.showing(1800001));
  // workTo late in the evening: the window ends at midnight.
  c.workTo = 23 * 60;
  miblo::EndOfDay night;
  night.update(0, c, k, 5, 23 * 60 + 59, false, true, true);
  TEST_ASSERT_TRUE(night.showing(0));
}

// Without the day's stats (no snapshot since the gadget started) it waits for them; it never
// shows zeros.
static void test_end_of_day_needs_the_days_stats() {
  miblo::Config c;
  c.endOfDay = true;
  const uint32_t k = key(2026, 10, 2);
  miblo::EndOfDay e;
  e.update(0, c, k, 5, 18 * 60, false, false, true);
  TEST_ASSERT_FALSE(e.showing(0));
  e.update(1000, c, k, 5, 18 * 60 + 5, false, true, true);  // the first snapshot arrived
  TEST_ASSERT_TRUE(e.showing(1000));
}

// Monday morning only: a restart (or the first snapshot) after noon does not show last week.
static void test_weekly_recap_only_in_the_morning() {
  const uint32_t k = key(2026, 10, 5);
  miblo::WeeklyRecap r;
  r.update(0, true, k, 1, miblo::WeeklyRecap::kUntil, true, true, true);
  TEST_ASSERT_FALSE(r.showing(0));
  r.update(10, true, k, 1, 15 * 60, false, true, true);
  TEST_ASSERT_FALSE(r.showing(10));
  miblo::WeeklyRecap q;
  q.update(0, true, k, 1, miblo::WeeklyRecap::kUntil - 1, false, true, true);
  TEST_ASSERT_TRUE(q.showing(0));
}

static void test_weekly_recap_across_the_wrap() {
  miblo::WeeklyRecap r;
  const uint32_t t = 0xFFFFFFFFu - 1000;
  r.update(t, true, key(2026, 10, 5), 1, 9 * 60, false, true, true);
  TEST_ASSERT_TRUE(r.showing(t));
  TEST_ASSERT_TRUE(r.showing(t + miblo::WeeklyRecap::kShowMs - 1));
  r.update(t + miblo::WeeklyRecap::kShowMs, true, key(2026, 10, 5), 1, 9 * 60 + 1, false, true, true);
  TEST_ASSERT_FALSE(r.showing(t + miblo::WeeklyRecap::kShowMs));
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

// ---- Screens ----

static const ui::ScreenSpec kSpecs[] = {{240, 240}, {320, 240}, {480, 320}, {170, 320}};
static const char kWideOwner[] = "WWWWWWWWWWWWWWWWWWWW";  // 20 wide characters

static miblo::Snapshot busySnap() {
  miblo::Snapshot s{};
  s.now = 1790616720;
  s.hasUsage = true;
  s.todayTurns = 999;
  s.todayWorkSec = 23 * 3600 + 59 * 60;
  s.todayUsd = 9999.99f;
  s.week = {true, 3, 999, 99 * 3600 + 59 * 60, 9999.99f};  // present, busiest, turns, workSec, usd
  return s;
}

// Every frame of the nudges, the day end and the week recap fits, in every language and size.
static void test_rhythm_screens_fit_everywhere() {
  const miblo::Snapshot s = busySnap();
  for (const auto& sp : kSpecs) {
    for (int l = 0; l < (int)miblo::Lang::Count; l++) {
      const miblo::Lang lang = (miblo::Lang)l;
      FakeCanvas fc(sp);
      screens::bind(fc);
      const miblo::Nudge kinds[] = {miblo::Nudge::Break, miblo::Nudge::Water, miblo::Nudge::Eyes};
      for (miblo::Nudge k : kinds) {
        screens::reset();
        for (uint32_t ms = 0; ms < 20000; ms += 700) screens::nudge(lang, k, ms);
      }
      screens::reset();
      for (uint32_t ms = 0; ms < 9000; ms += 700) screens::dayEnd(lang, s, kWideOwner, ms);
      screens::reset();
      screens::dayEnd(lang, s, "", 0);
      screens::reset();
      for (uint32_t ms = 0; ms < 9000; ms += 700) screens::weekRecap(lang, s, ms);
      TEST_ASSERT_TRUE(fc.calls > 0);
      TEST_ASSERT_TRUE(fc.texts.size() >= 3);
      TEST_ASSERT_EQUAL_INT(0, fc.outOfBounds);
    }
  }
}

static void test_nudge_says_what_to_do() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  screens::reset();
  screens::nudge(miblo::Lang::En, miblo::Nudge::Water, 0);
  TEST_ASSERT_TRUE(fc.drew("drink water"));
  screens::reset();
  fc.clearLog();
  screens::nudge(miblo::Lang::En, miblo::Nudge::Eyes, 0);
  TEST_ASSERT_TRUE(fc.drew("Look far away"));
  screens::reset();
  fc.clearLog();
  screens::nudge(miblo::Lang::En, miblo::Nudge::Break, 0);
  TEST_ASSERT_TRUE(fc.drew("5 min"));
}

static void test_day_end_content() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  miblo::Snapshot s{};
  s.todayTurns = 47;
  s.todayWorkSec = 3 * 3600 + 12 * 60;
  s.todayUsd = 4.2f;
  screens::reset();
  screens::dayEnd(miblo::Lang::En, s, "Marcus", 0);
  TEST_ASSERT_TRUE(fc.drew("TODAY"));
  TEST_ASSERT_TRUE(fc.drew("47"));
  TEST_ASSERT_TRUE(fc.drew("3h12"));
  TEST_ASSERT_TRUE(fc.drew("$4.20"));
  TEST_ASSERT_TRUE(fc.drew("Have a good rest, Marcus!"));
  screens::reset();
  fc.clearLog();
  screens::dayEnd(miblo::Lang::En, s, "", 0);
  TEST_ASSERT_TRUE(fc.drew("Have a good rest!"));
}

static void test_week_recap_content() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  miblo::Snapshot s{};
  s.week = {true, 3, 212, 31 * 3600 + 20 * 60, 38.5f};  // present, busiest, turns, workSec, usd
  screens::reset();
  screens::weekRecap(miblo::Lang::En, s, 0);
  TEST_ASSERT_TRUE(fc.drew("LAST WEEK"));
  TEST_ASSERT_TRUE(fc.drew("212"));
  TEST_ASSERT_TRUE(fc.drew("31h20"));  // hours, never "1d7h"
  TEST_ASSERT_TRUE(fc.drew("$38.50"));
  TEST_ASSERT_TRUE(fc.drew("busiest day: Wed"));
  s.week.busiest = 255;
  screens::reset();
  fc.clearLog();
  screens::weekRecap(miblo::Lang::En, s, 0);
  TEST_ASSERT_FALSE(fc.drew("busiest"));
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
  RUN_TEST(test_break_not_due_in_a_gap);
  RUN_TEST(test_blocked_wait_never_restarts);
  RUN_TEST(test_end_of_day_wait_starts_when_allowed);
  RUN_TEST(test_end_of_day_only_shortly_after_work);
  RUN_TEST(test_end_of_day_needs_the_days_stats);
  RUN_TEST(test_weekly_recap_only_in_the_morning);
  RUN_TEST(test_weekly_recap_across_the_wrap);
  RUN_TEST(test_rhythm_screens_fit_everywhere);
  RUN_TEST(test_nudge_says_what_to_do);
  RUN_TEST(test_day_end_content);
  RUN_TEST(test_week_recap_content);
  return UNITY_END();
}
