#include <ArduinoJson.h>
#include <string.h>

#include <string>
#include <unity.h>

#include "miblo_desknotes.h"

void setUp() {}
void tearDown() {}

static constexpr uint32_t M = 60000;

// ---- Task 11: say, timer, countdown, find ----

static void test_say_note_and_its_limits() {
  miblo::DeskNotes n;
  const char* bad = nullptr;
  StaticJsonDocument<256> d;
  deserializeJson(d, "{\"text\":\"volto em 10 min\"}");
  TEST_ASSERT_EQUAL_INT(200, miblo::sayRequest(n, d.as<JsonObjectConst>(), 0, &bad));
  TEST_ASSERT_EQUAL_STRING("volto em 10 min", n.saying(30 * M - 1));
  TEST_ASSERT_NULL(n.saying(30 * M));
  const char* const bads[][2] = {
      {"{\"text\":\"\"}", "text"},
      {"{\"text\":\"   \"}", "text"},
      {"{\"text\":\"0123456789012345678901234567890123456789X\"}", "text"},  // 41 characters
      {"{\"text\":\"\\u4e00\\u4e00\\u4e00\\u4e00\\u4e00\\u4e00\\u4e00\\u4e00\\u4e00\\u4e00\\u4e00\\u4e00"
       "\\u4e00\\u4e00\\u4e00\\u4e00\"}", "text"},  // 16 hanzi = 48 bytes: too many bytes
      {"{\"text\":\"a\\u0007b\"}", "text"},
      {"{\"text\":\"ok\",\"min\":481}", "min"},
  };
  for (const auto& b : bads) {
    deserializeJson(d, b[0]);
    TEST_ASSERT_EQUAL_INT_MESSAGE(400, miblo::sayRequest(n, d.as<JsonObjectConst>(), 0, &bad), b[0]);
    TEST_ASSERT_EQUAL_STRING(b[1], bad);
  }
  deserializeJson(d, "{\"off\":true}");
  TEST_ASSERT_EQUAL_INT(200, miblo::sayRequest(n, d.as<JsonObjectConst>(), 0, &bad));
  TEST_ASSERT_NULL(n.saying(0));
}

// What the CLI's FakeDevice (plugin/test/fakes/fake-device.js) answers, field by field.
static void test_say_and_timer_fields_match_the_cli() {
  miblo::DeskNotes n;
  const char* bad = nullptr;
  StaticJsonDocument<256> d;
  const char* const sayBads[][2] = {
      {"{\"off\":1}", "off"},
      {"{}", "text"},
      {"{\"text\":5}", "text"},
      {"{\"text\":\"ok\",\"min\":0}", "min"},
      {"{\"text\":\"ok\",\"min\":\"5\"}", "min"},
      {"{\"text\":\"ok\",\"min\":2.5}", "min"},
  };
  for (const auto& b : sayBads) {
    deserializeJson(d, b[0]);
    TEST_ASSERT_EQUAL_INT_MESSAGE(400, miblo::sayRequest(n, d.as<JsonObjectConst>(), 0, &bad), b[0]);
    TEST_ASSERT_EQUAL_STRING(b[1], bad);
  }
  // Spaces around the text are trimmed; the minutes are honoured.
  deserializeJson(d, "{\"text\":\"  back soon  \",\"min\":480}");
  TEST_ASSERT_EQUAL_INT(200, miblo::sayRequest(n, d.as<JsonObjectConst>(), 0, &bad));
  TEST_ASSERT_EQUAL_STRING("back soon", n.saying(480 * M - 1));
  TEST_ASSERT_NULL(n.saying(480 * M));
  // A request that fails leaves the note as it was.
  deserializeJson(d, "{\"text\":\"\"}");
  miblo::sayRequest(n, d.as<JsonObjectConst>(), 0, &bad);
  TEST_ASSERT_EQUAL_STRING("back soon", n.saying(M));

  const char* const timerBads[][2] = {
      {"{\"stop\":false}", "stop"}, {"{}", "min"}, {"{\"min\":0}", "min"}, {"{\"min\":181}", "min"},
      {"{\"min\":\"10\"}", "min"},
  };
  for (const auto& b : timerBads) {
    deserializeJson(d, b[0]);
    TEST_ASSERT_EQUAL_INT_MESSAGE(400, miblo::timerRequest(n, d.as<JsonObjectConst>(), 0, &bad), b[0]);
    TEST_ASSERT_EQUAL_STRING(b[1], bad);
  }
  TEST_ASSERT_FALSE(n.timerRunning());
  deserializeJson(d, "{\"min\":180}");
  TEST_ASSERT_EQUAL_INT(200, miblo::timerRequest(n, d.as<JsonObjectConst>(), 1000, &bad));
  TEST_ASSERT_TRUE(n.timerRunning());
  TEST_ASSERT_EQUAL_UINT32(180 * M, n.timerLenMs());
  TEST_ASSERT_EQUAL_UINT32(180 * M - 500, n.timerLeftMs(1500));
  deserializeJson(d, "{\"stop\":true}");
  TEST_ASSERT_EQUAL_INT(200, miblo::timerRequest(n, d.as<JsonObjectConst>(), 2000, &bad));
  TEST_ASSERT_FALSE(n.timerRunning());
  TEST_ASSERT_EQUAL(miblo::NoteKind::None, n.update(200 * M, 0, 0, -1));  // a stopped timer never fires
}

// Untrusted text: invalid UTF-8, overlong forms, surrogates, C1 controls and DEL are refused;
// accents, CJK and emoji within the limits are kept byte for byte.
static void test_text_is_sanitized() {
  miblo::DeskNotes n;
  const char* const refused[] = {
      "a\xFF" "b",          // not UTF-8
      "a\xC3",              // truncated sequence
      "\xC0\xAF",           // overlong '/'
      "\xED\xA0\x80",       // UTF-16 surrogate
      "\xF4\x90\x80\x80",   // beyond U+10FFFF
      "a\xC2\x85" "b",      // C1 control (NEL)
      "a\x7F",              // DEL
      "line\nbreak",        // control
  };
  for (const char* s : refused) {
    n.say(s, 10, 0);
    TEST_ASSERT_NULL_MESSAGE(n.saying(0), s);
  }
  const char* const kept[] = {
      "caf\xC3\xA9 \xC3\xA0 l\xC3\xA1",                  // accents
      "\xE4\xB8\x80\xE4\xBA\x8C\xE4\xB8\x89",             // CJK
      "\xF0\x9F\x98\x80 ok",                              // emoji
      "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",         // 40 wide characters
  };
  for (const char* s : kept) {
    n.say(s, 10, 0);
    TEST_ASSERT_EQUAL_STRING(s, n.saying(0));
  }
  // 15 hanzi = 45 bytes: fits; 47 bytes is the most.
  std::string h;
  for (int i = 0; i < 15; i++) h += "\xE4\xB8\x80";
  h += "ab";  // 47 bytes
  n.say(h.c_str(), 10, 0);
  TEST_ASSERT_EQUAL_STRING(h.c_str(), n.saying(0));
  h += "c";  // 48 bytes
  n.say(h.c_str(), 10, 0);
  TEST_ASSERT_NULL(n.saying(0));
  n.say(nullptr, 10, 0);
  TEST_ASSERT_NULL(n.saying(0));
}

// The timer across the millis() wrap and a long stall: it fires once, then the cat holds
// "Time's up!" for 5 min (or until dismissed).
static void test_timer_wrap_and_gap() {
  miblo::DeskNotes n;
  const uint32_t t0 = 0xFFFFFFFFu - 2 * M;
  n.timerStart(10, t0);
  TEST_ASSERT_EQUAL_UINT32(10 * M, n.timerLeftMs(t0));
  TEST_ASSERT_EQUAL(miblo::NoteKind::None, n.update(t0 + 10 * M - 1, 0, 0, -1));
  TEST_ASSERT_EQUAL(miblo::NoteKind::Timer, n.update(t0 + 25 * M, 0, 0, -1));  // stalled 15 min past it
  TEST_ASSERT_EQUAL(miblo::NoteKind::None, n.update(t0 + 25 * M + 100, 0, 0, -1));  // not twice
  TEST_ASSERT_FALSE(n.timerRunning());
  TEST_ASSERT_EQUAL(miblo::NoteKind::Timer, n.held(t0 + 25 * M + 100));
  TEST_ASSERT_TRUE(n.dismiss());
  TEST_ASSERT_EQUAL(miblo::NoteKind::None, n.held(t0 + 25 * M + 200));
}

static void test_timer_hold_runs_out() {
  miblo::DeskNotes n;
  n.timerStart(1, 0);
  TEST_ASSERT_EQUAL_UINT32(30000, n.timerLeftMs(30000));
  TEST_ASSERT_EQUAL(miblo::NoteKind::Timer, n.update(M, 0, 0, -1));
  TEST_ASSERT_EQUAL_STRING("", n.heldText(M));
  TEST_ASSERT_EQUAL_UINT32(0, n.timerLeftMs(M));
  TEST_ASSERT_EQUAL(miblo::NoteKind::Timer, n.held(M + miblo::kHeldMs - 1));
  TEST_ASSERT_EQUAL(miblo::NoteKind::None, n.held(M + miblo::kHeldMs));
  TEST_ASSERT_EQUAL(miblo::NoteKind::None, n.update(M + miblo::kHeldMs, 0, 0, -1));
  TEST_ASSERT_FALSE(n.dismiss());
}

static void test_countdown_lines() {
  miblo::DeskNotes n;
  n.setCountdown("release", miblo::Date{2026, 10, 15});
  char b[64];
  miblo::countdownLine(miblo::Lang::En, n.countdown(), miblo::Date{2026, 10, 12}, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("release in 3 days", b);
  miblo::countdownLine(miblo::Lang::En, n.countdown(), miblo::Date{2026, 10, 14}, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("release tomorrow", b);
  miblo::countdownLine(miblo::Lang::PtBR, n.countdown(), miblo::Date{2026, 10, 15}, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("release é hoje!", b);
  miblo::countdownLine(miblo::Lang::En, n.countdown(), miblo::Date{2026, 10, 16}, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("", b);
  n.setCountdown("ano novo", miblo::Date{2027, 1, 1});
  miblo::countdownLine(miblo::Lang::En, n.countdown(), miblo::Date{2026, 12, 30}, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("ano novo in 2 days", b);  // across the year
  miblo::countdownLine(miblo::Lang::En, n.countdown(), miblo::Date{2024, 2, 28}, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("", b);  // over 999 days away: nothing
  n.clearCountdown();
  miblo::countdownLine(miblo::Lang::En, n.countdown(), miblo::Date{2026, 12, 30}, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("", b);
  // A leap day counts.
  n.setCountdown("x", miblo::Date{2028, 3, 1});
  miblo::countdownLine(miblo::Lang::En, n.countdown(), miblo::Date{2028, 2, 28}, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("x in 2 days", b);
  // A tiny buffer is cut, never overrun.
  char tiny[4];
  miblo::countdownLine(miblo::Lang::En, n.countdown(), miblo::Date{2028, 2, 28}, tiny, sizeof(tiny));
  TEST_ASSERT_EQUAL_STRING("x i", tiny);
}

static void test_countdown_request() {
  miblo::DeskNotes n;
  const char* bad = nullptr;
  StaticJsonDocument<128> d;
  const miblo::Date today{2026, 10, 20};
  deserializeJson(d, "{\"label\":\"release\",\"md\":\"10-15\"}");
  TEST_ASSERT_EQUAL_INT(200, miblo::countdownRequest(n, d.as<JsonObjectConst>(), &today, &bad));
  TEST_ASSERT_EQUAL_UINT16(2027, n.countdown().date.year);  // next 15/10
  TEST_ASSERT_EQUAL_INT(409, miblo::countdownRequest(n, d.as<JsonObjectConst>(), nullptr, &bad));
  TEST_ASSERT_EQUAL_STRING("clock", bad);
  deserializeJson(d, "{\"label\":\"x\",\"date\":\"2026-10-19\"}");
  TEST_ASSERT_EQUAL_INT(400, miblo::countdownRequest(n, d.as<JsonObjectConst>(), &today, &bad));
  TEST_ASSERT_EQUAL_STRING("date", bad);
  TEST_ASSERT_TRUE(n.takeDirty());
  TEST_ASSERT_FALSE(n.takeDirty());
}

// The CLI's contract: md includes today, 02-29 waits for a leap year, the field names.
static void test_countdown_fields_match_the_cli() {
  miblo::DeskNotes n;
  const char* bad = nullptr;
  StaticJsonDocument<128> d;
  const miblo::Date today{2026, 10, 20};
  deserializeJson(d, "{\"label\":\"today\",\"md\":\"10-20\"}");
  TEST_ASSERT_EQUAL_INT(200, miblo::countdownRequest(n, d.as<JsonObjectConst>(), &today, &bad));
  TEST_ASSERT_EQUAL_UINT16(2026, n.countdown().date.year);
  TEST_ASSERT_EQUAL_STRING("today", n.countdown().label);
  deserializeJson(d, "{\"label\":\"leap\",\"md\":\"02-29\"}");
  TEST_ASSERT_EQUAL_INT(200, miblo::countdownRequest(n, d.as<JsonObjectConst>(), &today, &bad));
  TEST_ASSERT_EQUAL_UINT16(2028, n.countdown().date.year);
  deserializeJson(d, "{\"label\":\"x\",\"date\":\"2026-10-20\"}");
  TEST_ASSERT_EQUAL_INT(200, miblo::countdownRequest(n, d.as<JsonObjectConst>(), &today, &bad));
  // Without the time a full date is accepted (it can't be checked against today).
  deserializeJson(d, "{\"label\":\"x\",\"date\":\"2020-01-01\"}");
  TEST_ASSERT_EQUAL_INT(200, miblo::countdownRequest(n, d.as<JsonObjectConst>(), nullptr, &bad));
  const char* const bads[][2] = {
      {"{\"off\":\"yes\"}", "off"},
      {"{\"date\":\"2027-01-01\"}", "label"},
      {"{\"label\":\"\",\"date\":\"2027-01-01\"}", "label"},
      {"{\"label\":\"123456789012345678901\",\"date\":\"2027-01-01\"}", "label"},  // 21 characters
      {"{\"label\":\"a\\tb\",\"date\":\"2027-01-01\"}", "label"},
      {"{\"label\":\"x\"}", "date"},
      {"{\"label\":\"x\",\"date\":\"2027-02-29\"}", "date"},
      {"{\"label\":\"x\",\"date\":\"2027-13-01\"}", "date"},
      {"{\"label\":\"x\",\"date\":\"27-01-01\"}", "date"},
      {"{\"label\":\"x\",\"date\":5}", "date"},
      {"{\"label\":\"x\",\"md\":\"02-30\"}", "md"},
      {"{\"label\":\"x\",\"md\":\"2-3\"}", "md"},
      {"{\"label\":\"x\",\"md\":true}", "md"},
  };
  for (const auto& b : bads) {
    deserializeJson(d, b[0]);
    TEST_ASSERT_EQUAL_INT_MESSAGE(400, miblo::countdownRequest(n, d.as<JsonObjectConst>(), &today, &bad), b[0]);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(b[1], bad, b[0]);
  }
  TEST_ASSERT_EQUAL_STRING("x", n.countdown().label);  // untouched by the failures
  // 20 characters in 40 bytes: the most a label holds.
  deserializeJson(d, "{\"label\":\"\\u00e9\\u00e9\\u00e9\\u00e9\\u00e9\\u00e9\\u00e9\\u00e9\\u00e9\\u00e9\\u00e9\\u00e9"
                     "\\u00e9\\u00e9\\u00e9\\u00e9\\u00e9\\u00e9\\u00e9\\u00e9\",\"date\":\"2027-01-01\"}");
  TEST_ASSERT_EQUAL_INT(200, miblo::countdownRequest(n, d.as<JsonObjectConst>(), &today, &bad));
  TEST_ASSERT_EQUAL_size_t(40, strlen(n.countdown().label));
  deserializeJson(d, "{\"off\":true}");
  TEST_ASSERT_TRUE(n.takeDirty());
  TEST_ASSERT_EQUAL_INT(200, miblo::countdownRequest(n, d.as<JsonObjectConst>(), &today, &bad));
  TEST_ASSERT_EQUAL_STRING("", n.countdown().label);
  TEST_ASSERT_TRUE(n.takeDirty());
}

static void test_find() {
  miblo::DeskNotes n;
  TEST_ASSERT_FALSE(n.finding(0));
  n.find(0xFFFFFFFFu - 10);
  TEST_ASSERT_TRUE(n.finding(5));
  TEST_ASSERT_EQUAL_UINT32(16, n.findElapsed(5));
  TEST_ASSERT_FALSE(n.finding(0xFFFFFFFFu - 10 + miblo::kFindMs));
}

// RAM is tight on the ESP8266 (the notes live in the global context): every text once, nothing more.
static void test_the_state_stays_small() { TEST_ASSERT_LESS_OR_EQUAL_size_t(552, sizeof(miblo::DeskNotes)); }

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_say_note_and_its_limits);
  RUN_TEST(test_say_and_timer_fields_match_the_cli);
  RUN_TEST(test_text_is_sanitized);
  RUN_TEST(test_timer_wrap_and_gap);
  RUN_TEST(test_timer_hold_runs_out);
  RUN_TEST(test_countdown_lines);
  RUN_TEST(test_countdown_request);
  RUN_TEST(test_countdown_fields_match_the_cli);
  RUN_TEST(test_find);
  RUN_TEST(test_the_state_stays_small);
  return UNITY_END();
}
