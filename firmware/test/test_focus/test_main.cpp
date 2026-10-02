#include <unity.h>

#include "miblo_focus.h"

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

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_and_derived_breaks);
  RUN_TEST(test_full_cycle);
  RUN_TEST(test_focus_survives_wrap_and_long_gap);
  RUN_TEST(test_stop_and_restart);
  RUN_TEST(test_focus_request);
  return UNITY_END();
}
