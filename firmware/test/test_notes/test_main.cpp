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

// ---- Task 12: reminders, recurring alarms, persistence ----

static int remind(miblo::DeskNotes& n, const char* json, uint32_t nowMs, int nowMinute, const char** bad,
                  int* id = nullptr) {
  StaticJsonDocument<256> d, r;
  deserializeJson(d, json);
  const int code = miblo::remindRequest(n, d.as<JsonObjectConst>(), nowMs, nowMinute, r.to<JsonObject>(), bad);
  if (id) *id = r["id"] | 0;
  return code;
}

static void test_one_off_reminders() {
  miblo::DeskNotes n;
  const char* bad = nullptr;
  StaticJsonDocument<256> d, r;
  deserializeJson(d, "{\"in\":15,\"text\":\"ligar pro cliente\"}");
  TEST_ASSERT_EQUAL_INT(200, miblo::remindRequest(n, d.as<JsonObjectConst>(), 0, 600, r.to<JsonObject>(), &bad));
  TEST_ASSERT_EQUAL_INT(1, r["id"].as<int>());
  TEST_ASSERT_EQUAL(miblo::NoteKind::Reminder, n.update(15 * M, 0, 0, -1));
  TEST_ASSERT_EQUAL_STRING("ligar pro cliente", n.heldText(15 * M));
  TEST_ASSERT_EQUAL(miblo::NoteKind::None, n.held(15 * M + miblo::kHeldMs));
  for (int i = 0; i < 4; i++) {
    deserializeJson(d, "{\"in\":60,\"text\":\"x\"}");
    TEST_ASSERT_EQUAL_INT(200, miblo::remindRequest(n, d.as<JsonObjectConst>(), 0, 600, r.to<JsonObject>(), &bad));
  }
  TEST_ASSERT_EQUAL_INT(409, miblo::remindRequest(n, d.as<JsonObjectConst>(), 0, 600, r.to<JsonObject>(), &bad));
  TEST_ASSERT_EQUAL_STRING("full", bad);
}

static void test_alarm_at_needs_the_clock_and_rolls_to_tomorrow() {
  miblo::DeskNotes n;
  const char* bad = nullptr;
  StaticJsonDocument<256> d, r;
  deserializeJson(d, "{\"at\":\"16:30\",\"text\":\"daily\"}");
  TEST_ASSERT_EQUAL_INT(409, miblo::remindRequest(n, d.as<JsonObjectConst>(), 0, -1, r.to<JsonObject>(), &bad));
  TEST_ASSERT_EQUAL_STRING("clock", bad);
  TEST_ASSERT_EQUAL_INT(200, miblo::remindRequest(n, d.as<JsonObjectConst>(), 0, 17 * 60, r.to<JsonObject>(), &bad));
  TEST_ASSERT_EQUAL(miblo::NoteKind::None, n.update(23 * 60 * M, 0, 0, -1));
  TEST_ASSERT_EQUAL(miblo::NoteKind::Reminder, n.update((24 * 60 - 30) * M, 0, 0, -1));  // 16:30 tomorrow
}

static void test_recurring_alarm_weekdays_and_unknown_time() {
  miblo::DeskNotes n;
  TEST_ASSERT_EQUAL_UINT8(5, n.addAlarm(9 * 60 + 45, 0x3E, "daily"));
  const uint32_t mon = 2026 * 400 + 10 * 32 + 5, sat = 2026 * 400 + 10 * 32 + 10;
  TEST_ASSERT_EQUAL(miblo::NoteKind::None, n.update(1000, 0, 1, 9 * 60 + 45));      // time unknown
  TEST_ASSERT_EQUAL(miblo::NoteKind::None, n.update(2000, sat, 6, 9 * 60 + 45));    // Saturday
  TEST_ASSERT_EQUAL(miblo::NoteKind::Alarm, n.update(3000, mon, 1, 9 * 60 + 46));   // a minute late: still fires
  TEST_ASSERT_EQUAL(miblo::NoteKind::None, n.update(4000, mon, 1, 9 * 60 + 46));    // once a day
  TEST_ASSERT_TRUE(n.takeDirty());
  StaticJsonDocument<768> doc;
  n.toJson(doc.to<JsonObject>());
  miblo::DeskNotes back;
  TEST_ASSERT_TRUE(back.fromJson(doc.as<JsonObjectConst>()));
  StaticJsonDocument<512> list;
  back.listJson(list.to<JsonArray>(), 0, 0);
  TEST_ASSERT_EQUAL_STRING("09:45", list[0]["at"]);
  TEST_ASSERT_EQUAL_INT(62, list[0]["days"].as<int>());
  TEST_ASSERT_TRUE(back.remove(5));
  TEST_ASSERT_FALSE(back.remove(5));
}

static void test_bad_saved_notes_are_skipped() {
  StaticJsonDocument<512> doc;
  deserializeJson(doc, "{\"v\":1,\"alarms\":[{\"m\":5000,\"d\":62,\"t\":\"x\"},{\"m\":600,\"d\":0,\"t\":\"y\"},"
                       "{\"m\":600,\"d\":2,\"t\":\"ok\"}],\"cd\":{\"l\":\"\",\"y\":1,\"mo\":1,\"d\":1}}");
  miblo::DeskNotes n;
  TEST_ASSERT_TRUE(n.fromJson(doc.as<JsonObjectConst>()));
  StaticJsonDocument<512> list;
  n.listJson(list.to<JsonArray>(), 0, 0);
  TEST_ASSERT_EQUAL_INT(1, list.size());
  TEST_ASSERT_EQUAL_STRING("", n.countdown().label);
}

// The CLI's contract (plugin/test/fakes/fake-device.js): every field name and status.
static void test_remind_fields_match_the_cli() {
  miblo::DeskNotes n;
  const char* bad = nullptr;
  const char* const bads[][2] = {
      {"{}", "in"},
      {"{\"text\":\"x\"}", "in"},
      {"{\"in\":0,\"text\":\"x\"}", "in"},
      {"{\"in\":1441,\"text\":\"x\"}", "in"},
      {"{\"in\":\"5\",\"text\":\"x\"}", "in"},
      {"{\"in\":5}", "text"},
      {"{\"in\":5,\"text\":\"a\\u001bb\"}", "text"},
      {"{\"at\":\"9:45\",\"text\":\"x\"}", "at"},
      {"{\"at\":\"24:00\",\"text\":\"x\"}", "at"},
      {"{\"at\":\"12:60\",\"text\":\"x\"}", "at"},
      {"{\"at\":\"12:3a\",\"text\":\"x\"}", "at"},
      {"{\"at\":1230,\"text\":\"x\"}", "at"},
      {"{\"at\":\"09:45\",\"days\":0,\"text\":\"x\"}", "days"},
      {"{\"at\":\"09:45\",\"days\":128,\"text\":\"x\"}", "days"},
      {"{\"at\":\"09:45\",\"days\":62}", "text"},
      {"{\"dismiss\":false}", "dismiss"},
      {"{\"delete\":1}", "delete"},
      {"{\"delete\":0}", "delete"},
      {"{\"delete\":9}", "delete"},
      {"{\"delete\":\"1\"}", "delete"},
  };
  for (const auto& b : bads) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(400, remind(n, b[0], 0, 600, &bad), b[0]);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(b[1], bad, b[0]);
  }
  TEST_ASSERT_EQUAL_INT(409, remind(n, "{\"dismiss\":true}", 0, 600, &bad));
  TEST_ASSERT_EQUAL_STRING("none", bad);
  // Recurring alarms need no clock to be set (they wait for it to fire); 4 of them at most.
  int id = 0;
  for (int i = 0; i < 4; i++) {
    TEST_ASSERT_EQUAL_INT(200, remind(n, "{\"at\":\"09:45\",\"days\":127,\"text\":\"a\"}", 0, -1, &bad, &id));
    TEST_ASSERT_EQUAL_INT(5 + i, id);
  }
  TEST_ASSERT_EQUAL_INT(409, remind(n, "{\"at\":\"09:45\",\"days\":127,\"text\":\"a\"}", 0, -1, &bad));
  TEST_ASSERT_EQUAL_STRING("full", bad);
  TEST_ASSERT_EQUAL_INT(200, remind(n, "{\"delete\":6}", 0, -1, &bad));
  TEST_ASSERT_EQUAL_INT(200, remind(n, "{\"at\":\"23:59\",\"days\":1,\"text\":\"b\"}", 0, -1, &bad, &id));
  TEST_ASSERT_EQUAL_INT(6, id);  // the freed id
  // At the current minute: tomorrow, not now.
  TEST_ASSERT_EQUAL_INT(200, remind(n, "{\"at\":\"10:00\",\"text\":\"c\"}", 0, 600, &bad, &id));
  TEST_ASSERT_EQUAL_INT(1, id);
  TEST_ASSERT_EQUAL(miblo::NoteKind::None, n.update(24 * 60 * M - 1, 0, 0, -1));
  TEST_ASSERT_EQUAL(miblo::NoteKind::Reminder, n.update(24 * 60 * M, 0, 0, -1));
  TEST_ASSERT_EQUAL_INT(200, remind(n, "{\"dismiss\":true}", 24 * 60 * M, 600, &bad));
  TEST_ASSERT_EQUAL(miblo::NoteKind::None, n.held(24 * 60 * M));
}

// GET /api/remind: one-offs by id with the seconds left, then the alarms; a reminder the cat holds
// is no longer pending.
static void test_list_reminders() {
  miblo::DeskNotes n;
  const char* bad = nullptr;
  TEST_ASSERT_EQUAL_INT(200, remind(n, "{\"in\":15,\"text\":\"one\"}", 1000, 600, &bad));
  TEST_ASSERT_EQUAL_INT(200, remind(n, "{\"in\":1,\"text\":\"two\"}", 1000, 600, &bad));
  TEST_ASSERT_EQUAL_UINT8(5, n.addAlarm(0, 0x41, "weekend"));
  StaticJsonDocument<768> list;
  n.listJson(list.to<JsonArray>(), 1000 + 60 * 1000 + 500, 0);  // 1 min 0.5 s later
  TEST_ASSERT_EQUAL_INT(3, list.size());
  TEST_ASSERT_EQUAL_INT(1, list[0]["id"].as<int>());
  TEST_ASSERT_EQUAL_INT(14 * 60, list[0]["in"].as<int>());  // 13 min 59.5 s, rounded up
  TEST_ASSERT_EQUAL_STRING("one", list[0]["text"]);
  TEST_ASSERT_EQUAL_INT(2, list[1]["id"].as<int>());
  TEST_ASSERT_EQUAL_INT(0, list[1]["in"].as<int>());  // due, waiting to be shown
  TEST_ASSERT_EQUAL_INT(5, list[2]["id"].as<int>());
  TEST_ASSERT_EQUAL_STRING("00:00", list[2]["at"]);
  TEST_ASSERT_EQUAL_INT(0x41, list[2]["days"].as<int>());
  TEST_ASSERT_FALSE(list[2].containsKey("in"));
  TEST_ASSERT_EQUAL(miblo::NoteKind::Reminder, n.update(1000 + 60 * 1000 + 500, 0, 0, -1));
  n.listJson(list.to<JsonArray>(), 1000 + 60 * 1000 + 500, 0);
  TEST_ASSERT_EQUAL_INT(2, list.size());
  TEST_ASSERT_EQUAL_INT(1, list[0]["id"].as<int>());
  // Eight items fit the API's reply document (768 B on the ESP8266, more here: 64-bit slots).
  miblo::DeskNotes full;
  const char* w40 = "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW";
  for (int i = 0; i < 4; i++) {
    TEST_ASSERT_NOT_EQUAL(0, full.remindIn(60, w40, 0));
    TEST_ASSERT_NOT_EQUAL(0, full.addAlarm(600, 0x7F, w40));
  }
  DynamicJsonDocument big(1536);
  full.listJson(big.createNestedArray("items"), 0, 0);
  TEST_ASSERT_FALSE(big.overflowed());
  TEST_ASSERT_EQUAL_INT(8, big["items"].size());
}

// Two things due together: one at a time, the next once the first goes (dismissed or after 5 min).
static void test_one_at_a_time() {
  miblo::DeskNotes n;
  const uint32_t mon = 2026 * 400 + 10 * 32 + 5;
  TEST_ASSERT_EQUAL_UINT8(1, n.remindIn(1, "first", 0));
  TEST_ASSERT_EQUAL_UINT8(2, n.remindIn(1, "second", 0));
  TEST_ASSERT_EQUAL_UINT8(5, n.addAlarm(600, 0x7F, "standup"));
  n.timerStart(1, 0);
  TEST_ASSERT_EQUAL(miblo::NoteKind::Timer, n.update(M, mon, 1, 600));  // the timer first
  TEST_ASSERT_EQUAL(miblo::NoteKind::None, n.update(M + 10, mon, 1, 600));
  TEST_ASSERT_TRUE(n.dismiss());
  TEST_ASSERT_EQUAL(miblo::NoteKind::Reminder, n.update(M + 20, mon, 1, 601));
  TEST_ASSERT_EQUAL_STRING("first", n.heldText(M + 20));
  // The next shows by itself once the hold runs out.
  TEST_ASSERT_EQUAL(miblo::NoteKind::Reminder, n.update(M + 20 + miblo::kHeldMs, mon, 1, 606));
  TEST_ASSERT_EQUAL_STRING("second", n.heldText(M + 20 + miblo::kHeldMs));
  TEST_ASSERT_TRUE(n.dismiss());
  // The alarm's minute went by long ago while it waited: it still shows, once.
  TEST_ASSERT_EQUAL(miblo::NoteKind::Alarm, n.update(M + 30 + miblo::kHeldMs, mon, 1, 606));
  TEST_ASSERT_EQUAL_STRING("standup", n.heldText(M + 30 + miblo::kHeldMs));
  // Deleting what the cat holds takes it away.
  TEST_ASSERT_TRUE(n.remove(5));
  TEST_ASSERT_EQUAL(miblo::NoteKind::None, n.held(M + 40 + miblo::kHeldMs));
  TEST_ASSERT_EQUAL(miblo::NoteKind::None, n.update(M + 50 + miblo::kHeldMs, mon, 1, 606));
  TEST_ASSERT_FALSE(n.dismiss());
}

// A held reminder keeps its slot (RAM: its text lives there) but never makes the list "full":
// a new reminder takes the held one's place and the cat puts it down.
static void test_held_reminder_gives_way() {
  miblo::DeskNotes n;
  for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL_UINT8(i + 1, n.remindIn(i == 0 ? 1 : 60, "r", 0));
  TEST_ASSERT_EQUAL(miblo::NoteKind::Reminder, n.update(M, 0, 0, -1));
  TEST_ASSERT_EQUAL_UINT8(1, n.remindIn(30, "new", M));
  TEST_ASSERT_EQUAL(miblo::NoteKind::None, n.held(M));
  TEST_ASSERT_EQUAL_UINT8(0, n.remindIn(30, "more", M));
  // An invalid text never costs the held one its place.
  miblo::DeskNotes m;
  m.remindIn(1, "keep", 0);
  m.update(M, 0, 0, -1);
  TEST_ASSERT_EQUAL_UINT8(0, m.remindIn(30, "", M));
  TEST_ASSERT_EQUAL_STRING("keep", m.heldText(M));
}

// Reminders across the millis() wrap and a long stall: each fires once.
static void test_reminder_wrap_and_gap() {
  miblo::DeskNotes n;
  const uint32_t t0 = 0xFFFFFFFFu - M;
  TEST_ASSERT_EQUAL_UINT8(1, n.remindIn(5, "wrap", t0));
  TEST_ASSERT_EQUAL(miblo::NoteKind::None, n.update(t0 + 5 * M - 1, 0, 0, -1));
  TEST_ASSERT_EQUAL(miblo::NoteKind::Reminder, n.update(t0 + 9 * M, 0, 0, -1));
  TEST_ASSERT_EQUAL(miblo::NoteKind::None, n.update(t0 + 9 * M + 1, 0, 0, -1));
  TEST_ASSERT_EQUAL(miblo::NoteKind::None, n.update(t0 + 9 * M + miblo::kHeldMs, 0, 0, -1));
  TEST_ASSERT_EQUAL(miblo::NoteKind::None, n.update(t0 + 9 * M + miblo::kHeldMs + 1, 0, 0, -1));
}

// Alarms: the right weekday, the minute (or the one after, when the loop skipped it), the time
// known; never on another day's key; a new day fires again.
static void test_alarm_days_and_minutes() {
  miblo::DeskNotes n;
  TEST_ASSERT_EQUAL_UINT8(5, n.addAlarm(23 * 60 + 59, 0x01, "sunday"));
  TEST_ASSERT_EQUAL_UINT8(0, n.addAlarm(1440, 0x01, "x"));
  TEST_ASSERT_EQUAL_UINT8(0, n.addAlarm(0, 0, "x"));
  TEST_ASSERT_EQUAL_UINT8(0, n.addAlarm(0, 0x80, "x"));
  TEST_ASSERT_EQUAL_UINT8(0, n.addAlarm(0, 1, ""));
  const uint32_t sun = 2026 * 400 + 10 * 32 + 4, sun2 = 2026 * 400 + 10 * 32 + 11;
  TEST_ASSERT_EQUAL(miblo::NoteKind::None, n.update(0, sun, 0, 23 * 60 + 57));
  TEST_ASSERT_EQUAL(miblo::NoteKind::None, n.update(0, sun, 1, 23 * 60 + 59));  // Monday
  TEST_ASSERT_EQUAL(miblo::NoteKind::Alarm, n.update(0, sun, 0, 23 * 60 + 59));
  TEST_ASSERT_TRUE(n.dismiss());
  TEST_ASSERT_EQUAL(miblo::NoteKind::None, n.update(1, sun, 0, 23 * 60 + 59));
  TEST_ASSERT_EQUAL(miblo::NoteKind::Alarm, n.update(2, sun2, 0, 23 * 60 + 59));  // a week later
  TEST_ASSERT_TRUE(n.dismiss());
  // Two minutes late is too late (the loop never stalls that long without a reboot).
  TEST_ASSERT_EQUAL_UINT8(6, n.addAlarm(600, 0x7F, "late"));
  TEST_ASSERT_EQUAL(miblo::NoteKind::None, n.update(3, sun2, 0, 602));
  TEST_ASSERT_EQUAL(miblo::NoteKind::None, n.update(4, sun2, 0, 599));
}

// /notes.json: what is saved, and that anything odd in it never breaks the rest.
static void test_saved_notes_round_trip_and_garbage() {
  miblo::DeskNotes n;
  n.addAlarm(585, 62, "daily");
  n.addAlarm(1439, 127, "\xE4\xB8\x80 late");
  n.setCountdown("release", miblo::Date{2026, 10, 15});
  n.remindIn(5, "not saved", 0);
  n.say("not saved", 5, 0);
  StaticJsonDocument<768> doc;
  n.toJson(doc.to<JsonObject>());
  std::string json;
  serializeJson(doc, json);
  TEST_ASSERT_EQUAL_STRING(
      "{\"v\":1,\"alarms\":[{\"m\":585,\"d\":62,\"t\":\"daily\"},{\"m\":1439,\"d\":127,\"t\":\"\xE4\xB8\x80 late\"}],"
      "\"cd\":{\"l\":\"release\",\"y\":2026,\"mo\":10,\"d\":15}}",
      json.c_str());
  miblo::DeskNotes back;
  StaticJsonDocument<768> in;
  deserializeJson(in, json);
  TEST_ASSERT_TRUE(back.fromJson(in.as<JsonObjectConst>()));
  TEST_ASSERT_FALSE(back.takeDirty());
  TEST_ASSERT_EQUAL_STRING("release", back.countdown().label);
  TEST_ASSERT_EQUAL_UINT8(15, back.countdown().date.day);
  StaticJsonDocument<768> list;
  back.listJson(list.to<JsonArray>(), 0, 0);
  TEST_ASSERT_EQUAL_INT(2, list.size());
  TEST_ASSERT_EQUAL_STRING("23:59", list[1]["at"]);
  TEST_ASSERT_NULL(back.saying(0));

  const char* const garbage[] = {
      "{}", "{\"v\":2,\"alarms\":[{\"m\":1,\"d\":1,\"t\":\"x\"}]}", "{\"v\":\"1\"}",
  };
  for (const char* g : garbage) {
    miblo::DeskNotes x;
    deserializeJson(in, g);
    TEST_ASSERT_FALSE_MESSAGE(x.fromJson(in.as<JsonObjectConst>()), g);
  }
  TEST_ASSERT_FALSE(back.fromJson(JsonObjectConst()));  // no file / not an object
  // Odd entries are skipped one by one; more than 4 alarms are cut.
  DynamicJsonDocument odd(4096);
  deserializeJson(odd,
                  "{\"v\":1,\"alarms\":[1,\"x\",{\"m\":-1,\"d\":1,\"t\":\"a\"},{\"m\":10,\"d\":200,\"t\":\"a\"},"
                  "{\"m\":10,\"d\":1,\"t\":5},{\"m\":10,\"d\":1,\"t\":\"a\\nb\"},"
                  "{\"m\":1,\"d\":1,\"t\":\"1\"},{\"m\":2,\"d\":1,\"t\":\"2\"},{\"m\":3,\"d\":1,\"t\":\"3\"},"
                  "{\"m\":4,\"d\":1,\"t\":\"4\"},{\"m\":5,\"d\":1,\"t\":\"5\"}],"
                  "\"cd\":{\"l\":\"x\",\"y\":2026,\"mo\":2,\"d\":30}}");
  miblo::DeskNotes y;
  TEST_ASSERT_FALSE(odd.overflowed());
  TEST_ASSERT_TRUE(y.fromJson(odd.as<JsonObjectConst>()));
  y.listJson(list.to<JsonArray>(), 0, 0);
  TEST_ASSERT_EQUAL_INT(4, list.size());
  TEST_ASSERT_EQUAL_STRING("1", list[0]["text"]);
  TEST_ASSERT_EQUAL_STRING("4", list[3]["text"]);
  TEST_ASSERT_EQUAL_STRING("", y.countdown().label);  // 30 February
  deserializeJson(in, "{\"v\":1,\"alarms\":{\"m\":1},\"cd\":[1]}");
  TEST_ASSERT_TRUE(y.fromJson(in.as<JsonObjectConst>()));  // replaces what was there
  y.listJson(list.to<JsonArray>(), 0, 0);
  TEST_ASSERT_EQUAL_INT(0, list.size());
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
  RUN_TEST(test_one_off_reminders);
  RUN_TEST(test_alarm_at_needs_the_clock_and_rolls_to_tomorrow);
  RUN_TEST(test_recurring_alarm_weekdays_and_unknown_time);
  RUN_TEST(test_bad_saved_notes_are_skipped);
  RUN_TEST(test_remind_fields_match_the_cli);
  RUN_TEST(test_list_reminders);
  RUN_TEST(test_one_at_a_time);
  RUN_TEST(test_held_reminder_gives_way);
  RUN_TEST(test_reminder_wrap_and_gap);
  RUN_TEST(test_alarm_days_and_minutes);
  RUN_TEST(test_saved_notes_round_trip_and_garbage);
  RUN_TEST(test_the_state_stays_small);
  return UNITY_END();
}
