#pragma once
#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

#include "miblo_i18n.h"
#include "miblo_occasions.h"

// Notes on the desk (spec 9 and 11): a message for passers-by (/miblo:say), reminders in N
// minutes or at HH:MM, recurring alarms (saved), a visible timer, a countdown to a date (saved),
// and /miblo:find. Texts: <= kNoteChars characters and < kNoteBytes bytes of UTF-8.
namespace miblo {

constexpr uint8_t kNoteChars = 40;
constexpr size_t kNoteBytes = 48;
constexpr uint8_t kLabelChars = 20;
constexpr size_t kLabelBytes = 41;
constexpr uint8_t kMaxReminders = 4;  // one-off, ids 1..4
constexpr uint8_t kMaxAlarms = 4;     // recurring, ids 5..8
constexpr uint32_t kHeldMs = 300000;  // the cat holds a reminder for 5 min (or until dismissed)
constexpr uint32_t kFindMs = 10000;
constexpr uint16_t kSayDefaultMin = 30, kSayMaxMin = 480;
constexpr uint16_t kTimerMaxMin = 180;

enum class NoteKind : uint8_t { None, Say, Reminder, Alarm, Timer };

struct Countdown {
  char label[kLabelBytes];  // "" = none
  Date date;
};

class DeskNotes {
 public:
  // ---- say ----
  void say(const char* text, uint16_t minutes, uint32_t nowMs);
  void sayOff();
  const char* saying(uint32_t nowMs) const;  // nullptr when none
  // ---- reminders and alarms (ids: 1..4 one-off, 5..8 recurring; 0 = full / invalid) ----
  uint8_t remindIn(uint16_t minutes, const char* text, uint32_t nowMs);
  // At a local minute: today if still ahead, else tomorrow. `nowMinute` must be known (>= 0).
  uint8_t remindAt(uint16_t minute, int nowMinute, const char* text, uint32_t nowMs);
  uint8_t addAlarm(uint16_t minute, uint8_t days /* bit 0 = Sunday */, const char* text);
  bool remove(uint8_t id);
  bool dismiss();  // the reminder/alarm/timer text the cat is holding
  // ---- timer ----
  void timerStart(uint16_t minutes, uint32_t nowMs);
  void timerStop();
  bool timerRunning() const;
  uint32_t timerLeftMs(uint32_t nowMs) const;
  uint32_t timerLenMs() const;
  // ---- countdown ----
  void setCountdown(const char* label, const Date& d);
  void clearCountdown();
  const Countdown& countdown() const;
  // ---- find ----
  void find(uint32_t nowMs);
  bool finding(uint32_t nowMs) const;
  uint32_t findElapsed(uint32_t nowMs) const;
  // ---- every frame ----
  // `dayKey` 0 = time unknown (alarms wait). Returns what fired on this call (for the strong
  // cue): Reminder, Alarm, Timer or None; at most one per call, the others on the next calls.
  NoteKind update(uint32_t nowMs, uint32_t dayKey, uint8_t weekday, int minute);
  NoteKind held(uint32_t nowMs) const;       // what the cat is holding now
  const char* heldText(uint32_t nowMs) const;  // its text ("" for the timer: the screen says TimesUp)
  // ---- persistence (recurring alarms + countdown) and listing ----
  bool takeDirty();  // the saved part changed since the last call
  void toJson(JsonObject out) const;
  bool fromJson(JsonObjectConst in);
  void listJson(JsonArray out, uint32_t nowMs, uint32_t nowEpoch) const;  // GET /api/remind

 private:
  // Track D adds the state here.
};

// "release in 3 days" / "release tomorrow" / "release is today!" ("" when none or past).
void countdownLine(Lang lang, const Countdown& c, const Date& today, char* out, size_t cap);

// Request handlers: return the HTTP status (200; 400 with *bad = the field; 409 with *bad =
// "clock" (time unknown) or "full"). *bad must be a string literal (api.cpp puts it in the JSON
// reply as is, unescaped, after the handler returned).
int sayRequest(DeskNotes& n, JsonObjectConst body, uint32_t nowMs, const char** bad);
int remindRequest(DeskNotes& n, JsonObjectConst body, uint32_t nowMs, int nowMinute, JsonObject reply,
                  const char** bad);
int timerRequest(DeskNotes& n, JsonObjectConst body, uint32_t nowMs, const char** bad);
// `today` null = time unknown (a date without a year then can't be placed: 409 "clock").
int countdownRequest(DeskNotes& n, JsonObjectConst body, const Date* today, const char** bad);

}  // namespace miblo
