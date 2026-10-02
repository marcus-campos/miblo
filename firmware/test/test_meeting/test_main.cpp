// Meeting mode (MeetingMode, POST /api/meeting) and its screens: the anonymous flash and
// hero, the meeting badge and the long task fanfare.
#include <ArduinoJson.h>
#include <string.h>
#include <unity.h>

#include "../support/fake_canvas.h"
#include "miblo_alerts.h"
#include "miblo_meeting.h"
#include "ui_screens.h"

using namespace miblo;

void setUp() {}
void tearDown() {}

static const uint32_t NOW = 1790616720;
static const ui::ScreenSpec kSpecs[] = {{240, 240}, {320, 240}, {480, 320}, {170, 320}};

static void test_meeting_mode_times_out_and_handles_requests() {
  MeetingMode m;
  const char* bad = nullptr;
  StaticJsonDocument<64> d;
  deserializeJson(d, "{}");
  const uint32_t t0 = 0xFFFFFFFFu - 1000;
  TEST_ASSERT_EQUAL_INT(200, meetingRequest(m, d.as<JsonObjectConst>(), t0, &bad));
  TEST_ASSERT_TRUE(m.on());
  TEST_ASSERT_EQUAL_UINT32(60u * 60000, m.leftMs(t0));
  TEST_ASSERT_TRUE(m.update(t0 + 60u * 60000 - 1));  // across the wrap
  TEST_ASSERT_EQUAL_UINT32(1, m.leftMs(t0 + 60u * 60000 - 1));
  TEST_ASSERT_FALSE(m.update(t0 + 60u * 60000));
  TEST_ASSERT_FALSE(m.on());
  TEST_ASSERT_EQUAL_UINT32(0, m.leftMs(t0 + 60u * 60000));
  deserializeJson(d, "{\"min\":30}");
  TEST_ASSERT_EQUAL_INT(200, meetingRequest(m, d.as<JsonObjectConst>(), 0, &bad));
  TEST_ASSERT_EQUAL_UINT32(30u * 60000, m.leftMs(0));
  deserializeJson(d, "{\"min\":480}");
  TEST_ASSERT_EQUAL_INT(200, meetingRequest(m, d.as<JsonObjectConst>(), 0, &bad));
  TEST_ASSERT_EQUAL_UINT32(480u * 60000, m.leftMs(0));
  for (const char* b : {"{\"min\":0}", "{\"min\":481}", "{\"min\":-5}", "{\"min\":\"5\"}", "{\"min\":2.5}",
                        "{\"min\":null}", "{\"off\":false}", "{\"off\":1}"}) {
    bad = nullptr;
    deserializeJson(d, b);
    TEST_ASSERT_EQUAL_INT_MESSAGE(400, meetingRequest(m, d.as<JsonObjectConst>(), 0, &bad), b);
    TEST_ASSERT_NOT_NULL_MESSAGE(bad, b);
    TEST_ASSERT_TRUE_MESSAGE(m.on(), b);  // a rejected request changes nothing
  }
  deserializeJson(d, "{\"min\":0}");
  meetingRequest(m, d.as<JsonObjectConst>(), 0, &bad);
  TEST_ASSERT_EQUAL_STRING("min", bad);
  deserializeJson(d, "{\"off\":false}");
  meetingRequest(m, d.as<JsonObjectConst>(), 0, &bad);
  TEST_ASSERT_EQUAL_STRING("off", bad);
  deserializeJson(d, "{\"off\":true}");
  TEST_ASSERT_EQUAL_INT(200, meetingRequest(m, d.as<JsonObjectConst>(), 0, &bad));
  TEST_ASSERT_FALSE(m.on());
  TEST_ASSERT_FALSE(m.update(1));
  deserializeJson(d, "{\"off\":true}");  // already off: still fine
  TEST_ASSERT_EQUAL_INT(200, meetingRequest(m, d.as<JsonObjectConst>(), 0, &bad));
}

// A loop stalled for longer than the whole meeting (OTA) just ends it.
static void test_meeting_survives_a_long_gap() {
  MeetingMode m;
  m.start(1, 5000);
  TEST_ASSERT_TRUE(m.update(5000 + 59999));
  TEST_ASSERT_FALSE(m.update(5000 + 3600000));
  TEST_ASSERT_EQUAL_UINT32(0, m.leftMs(5000 + 3600000));
}

static Snapshot snap;

static void session(const char* id, const char* name, SessionState st, const char* tool, const char* det) {
  SessionRow& r = snap.sessions[snap.count++];
  memset(&r, 0, sizeof(r));
  strcpy(r.id, id);
  strcpy(r.name, name);
  r.st = st;
  strcpy(r.tool, tool);
  strcpy(r.det, det);
  r.since = NOW - 42;
  strcpy(r.model, "Opus");
  r.ctx = 71;
  r.tok = 412000;
}

static void fillWaiting(const char* name, const char* tool, const char* det) {
  memset(&snap, 0, sizeof(snap));
  snap.now = NOW;
  session("11111111", name, SessionState::Perm, tool, det);
  session("22222222", "other-repo", SessionState::Done, "", "");
}

static screens::Clock testClock() {
  screens::Clock c{};
  c.valid = true;
  strcpy(c.hhmm, "14:32");
  c.epoch = NOW;
  return c;
}

static bool drewAny(const FakeCanvas& fc, const char* needle) { return fc.drew(needle); }

// Meeting mode: no session name, tool or command reaches the screen; the alert still does.
static void test_anonymous_alerts_hide_names() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  fillWaiting("secret-client", "Bash", "deploy prod");
  RunTracker runs;
  const screens::Clock clk = testClock();
  screens::reset();
  screens::flash(Lang::En, AlertKind::Perm, "secret-client", 0, 0, true);
  screens::reset();
  screens::hero(Lang::En, snap, 0, AlertKind::Perm, false, clk, runs, true);
  TEST_ASSERT_FALSE(drewAny(fc, "secret-client"));
  TEST_ASSERT_FALSE(drewAny(fc, "deploy"));
  TEST_ASSERT_FALSE(drewAny(fc, "Bash"));
  TEST_ASSERT_TRUE(drewAny(fc, "A session needs you"));
  // "finished", anonymous: no name either, and no other session's name in the footer.
  fc.clearLog();
  screens::reset();
  screens::flash(Lang::En, AlertKind::Done, "other-repo", 0, 0, true);
  screens::reset();
  screens::hero(Lang::En, snap, 1, AlertKind::Done, false, clk, runs, true);
  TEST_ASSERT_FALSE(drewAny(fc, "other-repo"));
  TEST_ASSERT_TRUE(drewAny(fc, "FINISHED"));
  // With the response timed: "Finished after 7:07", still no name.
  RunTracker timed;
  snap.sessions[1].st = SessionState::Running;
  snap.sessions[1].since = NOW - 427;
  timed.observe(snap);
  snap.sessions[1].st = SessionState::Done;
  snap.sessions[1].since = NOW;
  timed.observe(snap);
  uint32_t dur = 0;
  TEST_ASSERT_TRUE(timed.stats("22222222", dur));
  fc.clearLog();
  screens::reset();
  screens::hero(Lang::En, snap, 1, AlertKind::Done, false, clk, timed, true);
  TEST_ASSERT_FALSE(drewAny(fc, "other-repo"));
  TEST_ASSERT_TRUE(drewAny(fc, "Finished after "));
  // Not anonymous: the name is back (the same calls, so the test above means something).
  fc.clearLog();
  screens::reset();
  screens::flash(Lang::En, AlertKind::Perm, "secret-client", 0);
  screens::reset();
  screens::hero(Lang::En, snap, 0, AlertKind::Perm, false, clk, runs);
  TEST_ASSERT_TRUE(drewAny(fc, "secret-client"));
  TEST_ASSERT_TRUE(drewAny(fc, "deploy prod"));
}

// The alerted session missing from the snapshot on screen (another paired computer's snapshot, or
// an alerts-only one), nothing cached: the flash and hero still draw, anonymously.
static void test_needs_you_without_its_session_row_draws_anonymously() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  fillWaiting("secret-client", "Bash", "deploy prod");
  RunTracker runs;
  const screens::Clock clk = testClock();
  screens::reset();
  fc.clearLog();
  screens::flash(Lang::En, AlertKind::Perm, "", 0, 0, false);
  TEST_ASSERT_TRUE(drewAny(fc, "A session needs you"));
  screens::reset();
  fc.clearLog();
  screens::hero(Lang::En, snap, -1, AlertKind::Question, false, clk, runs, false);
  TEST_ASSERT_TRUE(drewAny(fc, "A session needs you"));
  TEST_ASSERT_TRUE(drewAny(fc, "Asked a question"));
  TEST_ASSERT_FALSE(drewAny(fc, "secret-client"));
  TEST_ASSERT_FALSE(drewAny(fc, "waiting "));  // unknown: no made-up duration
  screens::reset();
  fc.clearLog();
  // A "finished" from another computer survives its snapshot too: anonymous, no row details.
  screens::hero(Lang::En, snap, -1, AlertKind::Done, false, clk, runs, false);
  TEST_ASSERT_TRUE(drewAny(fc, "FINISHED"));
  TEST_ASSERT_FALSE(drewAny(fc, "other-repo"));
  TEST_ASSERT_FALSE(drewAny(fc, "Opus"));
}

// Two computers' snapshots alternate during a hero: once drawn, its header stays as it started
// (name, tool) while the row is missing; drawn first without the row, it uses the name cached at
// the alert's start; anonymous only without one.
static void test_hero_header_stays_put_while_its_row_is_missing() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  fillWaiting("secret-client", "Bash", "deploy prod");
  Snapshot other = snap;
  other.count = 1;
  strcpy(other.sessions[0].id, "99999999");
  strcpy(other.sessions[0].name, "elsewhere");
  RunTracker runs;
  const screens::Clock clk = testClock();
  screens::reset();
  screens::hero(Lang::En, snap, 0, AlertKind::Perm, false, clk, runs, false, "secret-client");
  TEST_ASSERT_TRUE(drewAny(fc, "deploy prod"));
  fc.clearLog();
  screens::hero(Lang::En, other, -1, AlertKind::Perm, false, clk, runs, false, "secret-client");
  TEST_ASSERT_FALSE(drewAny(fc, "A session needs you"));
  TEST_ASSERT_FALSE(drewAny(fc, "secret-client"));  // not redrawn: still on screen
  fc.clearLog();
  screens::hero(Lang::En, snap, 0, AlertKind::Perm, false, clk, runs, false, "secret-client");
  TEST_ASSERT_FALSE(drewAny(fc, "secret-client"));  // back: same header, nothing to redraw
  // The hero starts on the other computer's snapshot: the cached name, no tool.
  screens::reset();
  fc.clearLog();
  screens::hero(Lang::En, other, -1, AlertKind::Perm, false, clk, runs, false, "secret-client");
  TEST_ASSERT_TRUE(drewAny(fc, "secret-client"));
  TEST_ASSERT_TRUE(drewAny(fc, "Asked permission"));
  TEST_ASSERT_FALSE(drewAny(fc, "A session needs you"));
  // Meeting mode: anonymous whatever is cached.
  screens::reset();
  fc.clearLog();
  screens::hero(Lang::En, other, -1, AlertKind::Perm, false, clk, runs, true, "secret-client");
  TEST_ASSERT_FALSE(drewAny(fc, "secret-client"));
  TEST_ASSERT_TRUE(drewAny(fc, "A session needs you"));
}

// From the 5th reminder the flash blinks red (white text), whatever the kind.
static void test_level_two_flash_is_red() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  screens::reset();
  screens::flash(Lang::En, AlertKind::Perm, "api-server", 0, 2);
  TEST_ASSERT_EQUAL_INT(ui::color::RED, fc.bgOf("api-server"));
  fc.clearLog();
  screens::reset();
  screens::flash(Lang::En, AlertKind::Perm, "api-server", 0, 1);
  TEST_ASSERT_EQUAL_INT(ui::color::AMBER, fc.bgOf("api-server"));
  fc.clearLog();
  screens::reset();
  screens::flash(Lang::En, AlertKind::Perm, "api-server", 0, 2, true);
  TEST_ASSERT_EQUAL_INT(ui::color::RED, fc.bgOf("A session"));
}

// The anonymous flash and hero and the badge fit in every language and resolution.
static void test_meeting_screens_fit_every_language_and_resolution() {
  fillWaiting("WWWWWWWWWWWWWWWWWWWW", "Bash", "deploy prod");
  RunTracker runs;
  const screens::Clock clk = testClock();
  for (const auto& sp : kSpecs) {
    for (uint8_t l = 0; l < (uint8_t)Lang::Count; l++) {
      const Lang lang = (Lang)l;
      FakeCanvas fc(sp);
      screens::bind(fc);
      for (uint8_t level = 0; level < 3; level++) {
        screens::reset();
        screens::flash(lang, AlertKind::Question, "x", 0, level, true);
        screens::reset();
        screens::flash(lang, AlertKind::Done, "x", 750, level, true);
      }
      screens::reset();
      screens::hero(lang, snap, 0, AlertKind::Perm, true, clk, runs, true);
      screens::reset();
      screens::hero(lang, snap, 1, AlertKind::Done, true, clk, runs, true);
      fc.bandArmed = true;  // the badge stays in its corner, inside the margins
      fc.bandMinX = screens::X(120);
      fc.bandMaxX = screens::X(236);
      fc.bandTop = screens::Y(200);
      fc.bandBottom = screens::Y(236);
      screens::meetingBadge(lang);
      fc.bandArmed = false;
      TEST_ASSERT_TRUE(fc.calls > 10);
      TEST_ASSERT_EQUAL_INT(0, fc.outOfBounds);
      TEST_ASSERT_EQUAL_INT(0, fc.bandOut);
    }
  }
}

// The badge draws every frame (an overlay over a screen that may have redrawn under it).
static void test_badge_draws_every_frame() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  screens::reset();
  fc.clearLog();
  screens::meetingBadge(Lang::En);
  const int first = fc.calls;
  TEST_ASSERT_TRUE(first > 0);
  screens::meetingBadge(Lang::En);
  TEST_ASSERT_EQUAL_INT(2 * first, fc.calls);
  TEST_ASSERT_EQUAL_INT(0, fc.outOfBounds);
}

// The fanfare: a long name, a long duration, every language and resolution; confetti, the cat
// and the line all drawn.
static void test_fanfare_fits_every_language_and_resolution() {
  const char* names[] = {"WWWWWWWWWWWWWWWWWWWW", "app-mobile", ""};
  const uint32_t durs[] = {23 * 60, 9 * 3600 + 59 * 60};
  for (const auto& sp : kSpecs) {
    for (uint8_t l = 0; l < (uint8_t)Lang::Count; l++) {
      for (const char* name : names) {
        for (uint32_t dur : durs) {
          FakeCanvas fc(sp);
          screens::bind(fc);
          screens::reset();
          for (uint32_t ms = 0; ms < kFanfareMs; ms += 299) screens::fanfare((Lang)l, name, dur, ms);
          TEST_ASSERT_TRUE(fc.calls > 50);
          TEST_ASSERT_EQUAL_INT(0, fc.outOfBounds);
        }
      }
    }
  }
}

static void test_fanfare_says_who_and_how_long() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  screens::reset();
  screens::fanfare(Lang::En, "app-mobile", 23 * 60, 0);
  TEST_ASSERT_TRUE(fc.drew("app-mobile"));
  TEST_ASSERT_TRUE(fc.drew("23min"));
  fc.clearLog();
  screens::reset();
  screens::fanfare(Lang::En, "", 23 * 60, 0);  // meeting mode: no name
  TEST_ASSERT_TRUE(fc.drew("Finished after 23min"));
  // The cat hops: up and down frames draw it again; the text does not change.
  fc.clearLog();
  screens::fanfare(Lang::En, "", 23 * 60, 300);
  TEST_ASSERT_FALSE(fc.drew("Finished"));
  TEST_ASSERT_TRUE(fc.calls > 0);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_meeting_mode_times_out_and_handles_requests);
  RUN_TEST(test_meeting_survives_a_long_gap);
  RUN_TEST(test_anonymous_alerts_hide_names);
  RUN_TEST(test_level_two_flash_is_red);
  RUN_TEST(test_needs_you_without_its_session_row_draws_anonymously);
  RUN_TEST(test_hero_header_stays_put_while_its_row_is_missing);
  RUN_TEST(test_meeting_screens_fit_every_language_and_resolution);
  RUN_TEST(test_badge_draws_every_frame);
  RUN_TEST(test_fanfare_fits_every_language_and_resolution);
  RUN_TEST(test_fanfare_says_who_and_how_long);
  return UNITY_END();
}
