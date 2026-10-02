#include <unity.h>

#include "../support/fake_canvas.h"
#include "miblo_focus.h"
#include "ui_screens.h"

using namespace miblo;

void setUp() {}
void tearDown() {}

static constexpr uint32_t M = 60000;

static void test_defaults_and_derived_breaks() {
  TEST_ASSERT_EQUAL_UINT8(5, defaultBreakFor(25));
  TEST_ASSERT_EQUAL_UINT8(10, defaultBreakFor(50));
  TEST_ASSERT_EQUAL_UINT8(1, defaultBreakFor(5));
  TEST_ASSERT_EQUAL_UINT8(15, longBreakFor(5));
  TEST_ASSERT_EQUAL_UINT8(30, longBreakFor(10));
  TEST_ASSERT_EQUAL_UINT8(30, longBreakFor(60));
}

// 25/5/2: focus, break, back (1 min), focus, long break (15), off.
static void test_full_cycle() {
  FocusTimer t;
  FocusPlan p;
  p.rounds = 2;
  t.start(p, 1000);
  TEST_ASSERT_EQUAL(FocusPhase::Focus, t.phase());
  TEST_ASSERT_EQUAL_UINT8(1, t.round());
  TEST_ASSERT_EQUAL_UINT32(25 * M, t.leftMs(1000));
  TEST_ASSERT_EQUAL(FocusEvent::None, t.update(1000 + 25 * M - 1));
  TEST_ASSERT_EQUAL(FocusEvent::BreakStarted, t.update(1000 + 25 * M));
  TEST_ASSERT_EQUAL(FocusPhase::Break, t.phase());
  TEST_ASSERT_EQUAL(FocusEvent::BackPrompt, t.update(1000 + 30 * M));
  TEST_ASSERT_EQUAL(FocusPhase::Back, t.phase());
  TEST_ASSERT_EQUAL(FocusEvent::FocusStarted, t.update(1000 + 31 * M));
  TEST_ASSERT_EQUAL_UINT8(2, t.round());
  TEST_ASSERT_EQUAL(FocusEvent::BreakStarted, t.update(1000 + 56 * M));
  TEST_ASSERT_EQUAL(FocusPhase::LongBreak, t.phase());
  TEST_ASSERT_EQUAL_UINT32(15 * M, t.phaseLenMs());
  TEST_ASSERT_EQUAL(FocusEvent::Finished, t.update(1000 + 71 * M));
  TEST_ASSERT_EQUAL(FocusPhase::Off, t.phase());
  TEST_ASSERT_EQUAL_UINT32(0, t.leftMs(1000 + 72 * M));
}

// The loop stalled for 40 min and millis() wrapped meanwhile: the phases still end where the
// schedule says, one event is reported, nothing is skipped twice.
static void test_focus_survives_wrap_and_long_gap() {
  FocusTimer t;
  FocusPlan p;
  const uint32_t start = 0xFFFFFFFFu - 10 * M;
  t.start(p, start);
  TEST_ASSERT_EQUAL(FocusEvent::BackPrompt, t.update(start + 30 * M + 10));  // crossed Focus->Break->Back
  TEST_ASSERT_EQUAL(FocusPhase::Back, t.phase());
  TEST_ASSERT_EQUAL_UINT32(M - 10, t.leftMs(start + 30 * M + 10));
  TEST_ASSERT_EQUAL(FocusEvent::FocusStarted, t.update(start + 31 * M));
  TEST_ASSERT_EQUAL_UINT8(2, t.round());
}

static void test_stop_and_restart() {
  FocusTimer t;
  t.start(FocusPlan{}, 0);
  t.stop();
  TEST_ASSERT_EQUAL(FocusPhase::Off, t.phase());
  TEST_ASSERT_EQUAL(FocusEvent::None, t.update(100 * M));
  t.start(FocusPlan{}, 100 * M);
  TEST_ASSERT_EQUAL_UINT8(1, t.round());
}

static void test_focus_request() {
  FocusTimer t;
  const char* bad = nullptr;
  StaticJsonDocument<128> d;
  deserializeJson(d, "{\"focusMin\":50}");
  TEST_ASSERT_EQUAL_INT(200, focusRequest(t, d.as<JsonObjectConst>(), 0, &bad));
  TEST_ASSERT_EQUAL_UINT8(10, t.plan().breakMin);
  TEST_ASSERT_EQUAL_UINT8(4, t.plan().rounds);
  const char* const bads[][2] = {{"{\"focusMin\":4}", "focusMin"}, {"{\"focusMin\":121}", "focusMin"},
                                 {"{\"breakMin\":0}", "breakMin"}, {"{\"rounds\":13}", "rounds"},
                                 {"{\"focusMin\":\"25\"}", "focusMin"}, {"{\"stop\":1}", "stop"}};
  for (const auto& b : bads) {
    deserializeJson(d, b[0]);
    TEST_ASSERT_EQUAL_INT_MESSAGE(400, focusRequest(t, d.as<JsonObjectConst>(), 0, &bad), b[0]);
    TEST_ASSERT_EQUAL_STRING(b[1], bad);
    TEST_ASSERT_EQUAL_UINT8(50, t.plan().focusMin);  // unchanged
  }
  deserializeJson(d, "{\"stop\":true}");
  TEST_ASSERT_EQUAL_INT(200, focusRequest(t, d.as<JsonObjectConst>(), 0, &bad));
  TEST_ASSERT_EQUAL(FocusPhase::Off, t.phase());
}

// ---- the focus screen ----

static const screens::Clock kClk{true, "14:32", 1790605920};

static void test_focus_screen_stays_on_screen() {
  const ui::ScreenSpec specs[] = {{240, 240}, {320, 240}, {480, 320}, {170, 320}};
  for (const auto& sp : specs) {
    FakeCanvas fc(sp);
    screens::bind(fc);
    for (int l = 0; l < (int)miblo::Lang::Count; l++)
      for (int ph = 1; ph <= 4; ph++) {
        screens::reset();
        for (uint32_t ms = 0; ms < 8000; ms += 500)
          screens::focus((miblo::Lang)l, kClk, (miblo::FocusPhase)ph, 3, 4, 120u * 60000 - ms, 120u * 60000,
                         1790613120, ms);
        // the most rounds, the last one
        screens::focus((miblo::Lang)l, kClk, (miblo::FocusPhase)ph, kRoundsMax, kRoundsMax, 1000, 60000, 1790613120,
                       9000);
      }
    TEST_ASSERT_EQUAL_INT(0, fc.outOfBounds);
    TEST_ASSERT_TRUE(fc.texts.size() > 0 && fc.arcs.size() > 0);  // fails with the stub (reset() alone draws)
  }
}

static void drawOnce(FakeCanvas& fc, FocusPhase ph, uint32_t leftMs, uint32_t untilEpoch) {
  screens::bind(fc);
  screens::reset();
  fc.clearLog();
  screens::focus(Lang::En, kClk, ph, 2, 4, leftMs, 25 * M, untilEpoch, 10000);
}

static void test_focus_screen_texts() {
  FakeCanvas fc({240, 240});
  drawOnce(fc, FocusPhase::Focus, (18 * 60 + 42) * 1000u, 1790607042);
  TEST_ASSERT_TRUE(fc.drew("18:42"));
  TEST_ASSERT_EQUAL(ui::Font::NumL, fc.fontOf("18:42"));
  TEST_ASSERT_TRUE(fc.drew("focus until "));
  TEST_ASSERT_TRUE(fc.arcs.size() >= 2);  // the ring: track and progress
  drawOnce(fc, FocusPhase::Focus, 60000, 0);  // time unknown: no "until" line
  TEST_ASSERT_FALSE(fc.drew("focus until"));
  drawOnce(fc, FocusPhase::Break, 4 * M, 0);
  TEST_ASSERT_TRUE(fc.drew("Break time"));
  TEST_ASSERT_TRUE(fc.drew("4:00"));
  drawOnce(fc, FocusPhase::LongBreak, 15 * M, 0);
  TEST_ASSERT_TRUE(fc.drew("Long break"));
  drawOnce(fc, FocusPhase::Back, 42000, 0);
  TEST_ASSERT_TRUE(fc.drew("Back to focus?"));
  TEST_ASSERT_EQUAL(ui::Font::Title, fc.fontOf("Back to focus?"));
  TEST_ASSERT_TRUE(fc.drew("0:42"));
  TEST_ASSERT_NOT_EQUAL(ui::Font::NumL, fc.fontOf("0:42"));  // small seconds, no big count
}

// Between ticks nothing is redrawn; a new second only rewrites the time in place (no clear).
static void test_focus_screen_ticks_in_place() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  screens::reset();
  const uint32_t len = 25 * M, left = 18 * M;
  screens::focus(Lang::En, kClk, FocusPhase::Focus, 2, 4, left, len, 1790607042, 1000);
  fc.clearLog();
  screens::focus(Lang::En, kClk, FocusPhase::Focus, 2, 4, left - 300, len, 1790607042, 1300);
  TEST_ASSERT_EQUAL_INT(0, fc.calls);
  screens::focus(Lang::En, kClk, FocusPhase::Focus, 2, 4, left - 1000, len, 1790607042, 2000);
  TEST_ASSERT_EQUAL_INT(0, fc.panelFills);
  TEST_ASSERT_EQUAL_INT(1, fc.boxTexts);
  TEST_ASSERT_TRUE(fc.drew("17:59"));
  TEST_ASSERT_EQUAL_INT(0, (int)fc.arcs.size());  // the ring moves only every 2 degrees
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_and_derived_breaks);
  RUN_TEST(test_full_cycle);
  RUN_TEST(test_focus_survives_wrap_and_long_gap);
  RUN_TEST(test_stop_and_restart);
  RUN_TEST(test_focus_request);
  RUN_TEST(test_focus_screen_stays_on_screen);
  RUN_TEST(test_focus_screen_texts);
  RUN_TEST(test_focus_screen_ticks_in_place);
  return UNITY_END();
}
