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
// An alarm (or an "at HH:MM" reminder) whose minute the device missed (the clock arrived late
// after a reboot, the loop stalled, DST skipped it) still fires up to this many minutes late, once.
// Later than that it is skipped for the day: a 07:00 alarm never goes off at 13:00 after an outage.
// The catch-up stays within the day (a 23:50 alarm missed until 00:05 waits for its next day). A
// minute the clock jumped over without time going by (spring forward: 02:30 doesn't exist) is not
// late at all: the alarm fires on the first frame after the jump (03:00).
constexpr uint16_t kAlarmCatchUpMin = 30;
// An "at HH:MM" reminder follows the local clock, within this much of the real-time delay it was
// given when set: a DST change (or a corrected clock) moves the local clock by an hour.
constexpr uint32_t kAtSlackMs = 90u * 60000u;

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
  // It goes off when the local clock reaches that minute (so a DST change in between does not
  // move it), within kAtSlackMs of the real-time delay it was given; without the clock, or if
  // the clock never shows that minute in time, at the delay itself (at the latest kAtSlackMs on).
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
  // `dayKey` 0 = time unknown (alarms wait; "at" reminders fall back to their real-time delay).
  // Alarms fire at their minute, or up to kAlarmCatchUpMin late (see there), once per day.
  // Returns what fired on this call (for the strong cue): Reminder, Alarm, Timer or None; at most
  // one per call, the others on the next calls.
  NoteKind update(uint32_t nowMs, uint32_t dayKey, uint8_t weekday, int minute);
  NoteKind held(uint32_t nowMs) const;       // what the cat is holding now
  const char* heldText(uint32_t nowMs) const;  // its text ("" for the timer: the screen says TimesUp)
  // ---- persistence (recurring alarms + countdown) and listing ----
  bool takeDirty();  // the saved part changed since the last call
  void toJson(JsonObject out) const;
  bool fromJson(JsonObjectConst in);
  void listJson(JsonArray out, uint32_t nowMs, uint32_t nowEpoch) const;  // GET /api/remind

 private:
  // Kept small (RAM, ESP8266): every text lives once, in its slot. A one-off reminder keeps its
  // slot while the cat holds it (heldId_); an alarm's text stays in the alarm. On the ESP8266
  // sizeof(DeskNotes) is 576 B (test_notes checks it on the host).
  struct Reminder {
    uint32_t dueMs;
    uint16_t atMinute;      // the local minute of an "at HH:MM" reminder; kNotAt for "in N min"
    char text[kNoteBytes];  // "" = free slot
  };
  static constexpr uint16_t kNotAt = 0xFFFF;
  struct Alarm {
    uint16_t minute;   // local minute of the day, 0..1439
    uint16_t lastDay;  // low 16 bits of the dayKey it last fired on (saved as "ld")
    uint8_t days;      // bit 0 = Sunday; 0 = free slot
    char text[kNoteBytes];
  };
  enum : uint8_t { kDueTimer = 0x10 };  // due_: bits 0..3 = alarms waiting to be shown, 4 = timer

  void hold(NoteKind kind, uint8_t id, uint32_t nowMs);
  void release();  // drops what the cat holds (frees a one-off reminder's slot)
  uint8_t addReminder(uint32_t dueMs, uint16_t atMinute, const char* text);
  // The local clock is at `target` (minute of the day) or went past it within the catch-up
  // window: late by <= kAlarmCatchUpMin, or jumped over it since the last frame of the same day.
  bool reached(uint16_t target, uint32_t nowMs, uint32_t dayKey, int minute) const;
  bool reminderDue(const Reminder& r, uint32_t nowMs, uint32_t dayKey, int minute) const;
  NoteKind next(uint32_t nowMs, uint32_t dayKey, int minute);  // `dayKey` 0: time unknown

  Reminder rem_[kMaxReminders] = {};
  Alarm alarms_[kMaxAlarms] = {};
  Countdown countdown_ = {};
  char say_[kNoteBytes] = "";
  uint32_t sayStartMs_ = 0, sayLenMs_ = 0;
  uint32_t timerStartMs_ = 0, timerLenMs_ = 0;  // length 0 = no timer running
  uint32_t heldStartMs_ = 0, findStartMs_ = 0;
  // The previous frame with the local time known (dayKey 0 = none yet): a jump of the clock
  // over an alarm's minute is told from a late frame by the real time between the two.
  uint32_t prevMs_ = 0, prevDay_ = 0;
  int16_t prevMinute_ = -1;
  NoteKind heldKind_ = NoteKind::None;
  uint8_t heldId_ = 0;  // the reminder (1..4) or alarm (5..8) held; 0 for the timer
  uint8_t due_ = 0;
  bool finding_ = false, dirty_ = false;
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
