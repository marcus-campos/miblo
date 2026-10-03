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

// Exactly the ordinary screens may be taken over; never setup, codes, updates, alerts or the
// daily screens themselves.
static void test_daily_may_replace_exactly_the_ordinary_screens() {
  for (int i = 0; i <= (int)ScreenId::Preview; i++) {
    const ScreenId s = (ScreenId)i;
    const bool want = s == ScreenId::Main || s == ScreenId::Desk || s == ScreenId::Summary ||
                      s == ScreenId::Disconnected || s == ScreenId::Roam || s == ScreenId::Visit ||
                      s == ScreenId::Hello || s == ScreenId::LimitReset || s == ScreenId::UpdateAvailable;
    TEST_ASSERT_EQUAL_MESSAGE(want, dailyMayReplace(s), "screen id");
  }
}

// A session waiting for you is never hidden by a daily screen: every daily full screen carries
// the amber waiting mark while something is pending, and no other screen does (alerts, pet
// mode and the ordinary screens show it their own way).
static void test_waiting_mark_on_every_daily_screen() {
  const ScreenId daily[] = {ScreenId::Focus,  ScreenId::Timer,     ScreenId::Note,   ScreenId::Find,
                            ScreenId::Nudge,  ScreenId::DayEnd,    ScreenId::WeekRecap, ScreenId::Preview};
  for (int i = 0; i <= (int)ScreenId::Preview; i++) {
    const ScreenId s = (ScreenId)i;
    bool isDaily = false;
    for (ScreenId d : daily) isDaily = isDaily || d == s;
    TEST_ASSERT_EQUAL_MESSAGE(isDaily, dailyFullScreen(s), "daily full screen");
    TEST_ASSERT_EQUAL_MESSAGE(isDaily, waitingMarkOn(s, 1), "mark with one waiting");
    TEST_ASSERT_EQUAL_MESSAGE(isDaily, waitingMarkOn(s, 3), "mark with three waiting");
    TEST_ASSERT_FALSE_MESSAGE(waitingMarkOn(s, 0), "no mark when nobody waits");
  }
  // And through the arbiter: focus with a session waiting (no alert on screen right now).
  DailyInputs in;
  in.focus = FocusPhase::Focus;
  TEST_ASSERT_TRUE(waitingMarkOn(dailyScreen(in), 1));
  in = DailyInputs{};
  in.held = NoteKind::Reminder;
  TEST_ASSERT_TRUE(waitingMarkOn(dailyScreen(in), 1));
}

// The settings page's preview: over every ordinary screen and every other daily screen (the
// person asked for it just now), never over an alert, setup or an update; it counts as someone
// at the desk.
static void test_preview_goes_first_but_never_over_an_alert() {
  DailyInputs in;
  in.preview = true;
  in.focus = FocusPhase::Focus;
  in.cue = CueKind::FocusEnd;
  in.find = true;
  in.say = true;
  for (ScreenId s : {ScreenId::Main, ScreenId::Desk, ScreenId::Roam, ScreenId::Visit, ScreenId::Hello}) {
    in.screen = s;
    TEST_ASSERT_EQUAL((int)ScreenId::Preview, (int)dailyScreen(in));
  }
  for (ScreenId s : {ScreenId::AlertFlash, ScreenId::AlertHero, ScreenId::Setup, ScreenId::Updating}) {
    in.screen = s;
    TEST_ASSERT_EQUAL((int)s, (int)dailyScreen(in));
  }
  DailyInputs only;
  only.preview = true;
  TEST_ASSERT_TRUE(dailyActivity(only));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_preview_goes_first_but_never_over_an_alert);
  RUN_TEST(test_alerts_beat_every_daily_screen);
  RUN_TEST(test_fanfare_replaces_only_the_hero);
  RUN_TEST(test_daily_priority_order);
  RUN_TEST(test_pet_mode_rules);
  RUN_TEST(test_daily_activity_keeps_pet_mode_away);
  RUN_TEST(test_daily_may_replace_exactly_the_ordinary_screens);
  RUN_TEST(test_waiting_mark_on_every_daily_screen);
  return UNITY_END();
}
