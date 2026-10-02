#include <unity.h>

#include "miblo_daily.h"

using namespace miblo;

void setUp() {}
void tearDown() {}

// Alerts, setup and codes always win: no daily-life input may take their screen.
static void test_alerts_beat_every_daily_screen() {
  for (ScreenId s : {ScreenId::AlertFlash, ScreenId::AlertHero, ScreenId::Setup, ScreenId::PairCode,
                     ScreenId::PresenceCode, ScreenId::Updating, ScreenId::Welcome}) {
    DailyInputs in;
    in.screen = s;
    in.cue = CueKind::Timer;
    in.find = true;
    in.held = NoteKind::Alarm;
    in.focus = FocusPhase::Focus;
    in.timer = in.dayEnd = in.weekRecap = in.say = true;
    in.nudge = Nudge::Water;
    TEST_ASSERT_EQUAL((int)s, (int)dailyScreen(in));
  }
}

static void test_fanfare_replaces_only_the_hero() {
  DailyInputs in;
  in.fanfare = true;
  in.screen = ScreenId::AlertHero;
  TEST_ASSERT_EQUAL((int)ScreenId::Fanfare, (int)dailyScreen(in));
  in.screen = ScreenId::AlertFlash;
  TEST_ASSERT_EQUAL((int)ScreenId::AlertFlash, (int)dailyScreen(in));
}

static void test_daily_priority_order() {
  DailyInputs in;
  in.screen = ScreenId::Main;
  in.say = true;
  TEST_ASSERT_EQUAL((int)ScreenId::Note, (int)dailyScreen(in));
  in.nudge = Nudge::Eyes;
  TEST_ASSERT_EQUAL((int)ScreenId::Nudge, (int)dailyScreen(in));
  in.weekRecap = true;
  TEST_ASSERT_EQUAL((int)ScreenId::WeekRecap, (int)dailyScreen(in));
  in.dayEnd = true;
  TEST_ASSERT_EQUAL((int)ScreenId::DayEnd, (int)dailyScreen(in));
  in.timer = true;
  TEST_ASSERT_EQUAL((int)ScreenId::Timer, (int)dailyScreen(in));
  in.focus = FocusPhase::Break;
  TEST_ASSERT_EQUAL((int)ScreenId::Focus, (int)dailyScreen(in));
  in.held = NoteKind::Reminder;
  TEST_ASSERT_EQUAL((int)ScreenId::Note, (int)dailyScreen(in));
  in.screen = ScreenId::Hello;
  TEST_ASSERT_EQUAL((int)ScreenId::Hello, (int)dailyScreen(in));
  in.find = true;
  TEST_ASSERT_EQUAL((int)ScreenId::Find, (int)dailyScreen(in));
  in.cue = CueKind::FocusEnd;
  TEST_ASSERT_EQUAL((int)ScreenId::Cue, (int)dailyScreen(in));
}

// Pet mode: wellness never shows there; the say note goes on the sign (roam), not full screen;
// the black cat only crosses pet mode.
static void test_pet_mode_rules() {
  DailyInputs in;
  in.screen = ScreenId::Roam;
  in.nudge = Nudge::Break;
  in.say = true;
  TEST_ASSERT_EQUAL((int)ScreenId::Roam, (int)dailyScreen(in));
  in.passerby = true;
  TEST_ASSERT_EQUAL((int)ScreenId::Passerby, (int)dailyScreen(in));
  in.screen = ScreenId::Main;
  in.nudge = Nudge::None;
  TEST_ASSERT_EQUAL((int)ScreenId::Note, (int)dailyScreen(in));  // say on Main; no black cat off pet mode
}

static void test_daily_activity_keeps_pet_mode_away() {
  DailyInputs in;
  TEST_ASSERT_FALSE(dailyActivity(in));
  in.say = true;
  TEST_ASSERT_FALSE(dailyActivity(in));  // the say note rides on the pet's sign
  in.focus = FocusPhase::Back;
  TEST_ASSERT_TRUE(dailyActivity(in));
  in = DailyInputs{};
  in.timer = true;
  TEST_ASSERT_TRUE(dailyActivity(in));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_alerts_beat_every_daily_screen);
  RUN_TEST(test_fanfare_replaces_only_the_hero);
  RUN_TEST(test_daily_priority_order);
  RUN_TEST(test_pet_mode_rules);
  RUN_TEST(test_daily_activity_keeps_pet_mode_away);
  return UNITY_END();
}
