#include "miblo_desknotes.h"

#include <stdio.h>
#include <string.h>

#include "miblo_config.h"  // parseDate, parseMonthDay

namespace miblo {

static constexpr uint32_t kMinMs = 60000;

// ---- untrusted text ----

// A text typed by a person, copied to `out` without the spaces around it. Refused (false, `out`
// = ""): null, blank, not valid UTF-8 (truncated, overlong, a surrogate, beyond U+10FFFF), a
// control character (C0, DEL, C1), more than `maxChars` characters, or `cap` bytes or more.
static bool cleanText(const char* in, char* out, size_t cap, uint8_t maxChars) {
  if (cap) out[0] = 0;
  if (!in) return false;
  while (*in == ' ') in++;
  size_t len = strlen(in);
  while (len && in[len - 1] == ' ') len--;
  if (!len || len >= cap) return false;
  const uint8_t* p = reinterpret_cast<const uint8_t*>(in);
  const uint8_t* end = p + len;
  unsigned chars = 0;
  while (p < end) {
    const uint8_t b = *p;
    uint32_t cp, least;
    int n;
    if (b < 0x80) cp = b, n = 1, least = 0;
    else if ((b & 0xE0) == 0xC0) cp = b & 0x1F, n = 2, least = 0x80;
    else if ((b & 0xF0) == 0xE0) cp = b & 0x0F, n = 3, least = 0x800;
    else if ((b & 0xF8) == 0xF0) cp = b & 0x07, n = 4, least = 0x10000;
    else return false;
    if (end - p < n) return false;
    for (int i = 1; i < n; i++) {
      if ((p[i] & 0xC0) != 0x80) return false;
      cp = (cp << 6) | (p[i] & 0x3F);
    }
    if (cp < least || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;  // overlong, out of range
    if (cp < 0x20 || (cp >= 0x7F && cp <= 0x9F)) return false;                       // C0, DEL, C1
    p += n;
    if (++chars > maxChars) return false;
  }
  memcpy(out, in, len);
  out[len] = 0;
  return true;
}

// Days since 1970-01-01 (proleptic Gregorian), for whole-day differences.
static int32_t dayNumber(const Date& d) {
  const int y = d.year - (d.month <= 2);
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (d.month + (d.month > 2 ? -3 : 9)) + 2) / 5 + d.day - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int32_t)doe - 719468;
}

static bool before(const Date& a, const Date& b) { return dayNumber(a) < dayNumber(b); }

// ---- say ----

void DeskNotes::say(const char* text, uint16_t minutes, uint32_t nowMs) {
  if (!minutes || minutes > kSayMaxMin || !cleanText(text, say_, sizeof(say_), kNoteChars)) {
    say_[0] = 0;
    return;
  }
  sayStartMs_ = nowMs;
  sayLenMs_ = minutes * kMinMs;
}

void DeskNotes::sayOff() { say_[0] = 0; }

const char* DeskNotes::saying(uint32_t nowMs) const {
  return say_[0] && nowMs - sayStartMs_ < sayLenMs_ ? say_ : nullptr;
}

// ---- reminders and alarms ----

uint8_t DeskNotes::remindIn(uint16_t minutes, const char* text, uint32_t nowMs) {
  (void)minutes;
  (void)text;
  (void)nowMs;
  return 0;
}

uint8_t DeskNotes::remindAt(uint16_t minute, int nowMinute, const char* text, uint32_t nowMs) {
  (void)minute;
  (void)nowMinute;
  (void)text;
  (void)nowMs;
  return 0;
}

uint8_t DeskNotes::addAlarm(uint16_t minute, uint8_t days, const char* text) {
  (void)minute;
  (void)days;
  (void)text;
  return 0;
}

bool DeskNotes::remove(uint8_t id) {
  (void)id;
  return false;
}

void DeskNotes::hold(NoteKind kind, uint8_t id, uint32_t nowMs) {
  heldKind_ = kind;
  heldId_ = id;
  heldStartMs_ = nowMs;
}

void DeskNotes::release() {
  if (heldKind_ == NoteKind::Reminder && heldId_ >= 1 && heldId_ <= kMaxReminders) rem_[heldId_ - 1].text[0] = 0;
  heldKind_ = NoteKind::None;
  heldId_ = 0;
}

bool DeskNotes::dismiss() {
  if (heldKind_ == NoteKind::None) return false;
  release();
  return true;
}

// ---- timer ----

void DeskNotes::timerStart(uint16_t minutes, uint32_t nowMs) {
  if (!minutes || minutes > kTimerMaxMin) return;
  timerStartMs_ = nowMs;
  timerLenMs_ = minutes * kMinMs;
  due_ &= ~kDueTimer;
}

void DeskNotes::timerStop() {
  timerLenMs_ = 0;
  due_ &= ~kDueTimer;
}

bool DeskNotes::timerRunning() const { return timerLenMs_ != 0; }

uint32_t DeskNotes::timerLeftMs(uint32_t nowMs) const {
  const uint32_t gone = nowMs - timerStartMs_;
  return gone < timerLenMs_ ? timerLenMs_ - gone : 0;
}

uint32_t DeskNotes::timerLenMs() const { return timerLenMs_; }

// ---- countdown ----

void DeskNotes::setCountdown(const char* label, const Date& d) {
  if (!cleanText(label, countdown_.label, sizeof(countdown_.label), kLabelChars)) countdown_.label[0] = 0;
  countdown_.date = countdown_.label[0] ? d : Date{0, 0, 0};
  dirty_ = true;
}

void DeskNotes::clearCountdown() {
  countdown_ = Countdown{};
  dirty_ = true;
}

const Countdown& DeskNotes::countdown() const { return countdown_; }

void countdownLine(Lang lang, const Countdown& c, const Date& today, char* out, size_t cap) {
  if (!cap) return;
  out[0] = 0;
  if (!c.label[0]) return;
  const int32_t days = dayNumber(c.date) - dayNumber(today);
  if (days < 0 || days > 999) return;
  char fmt[48];
  tr(lang, days == 0 ? S::CountdownToday : days == 1 ? S::CountdownTomorrow : S::CountdownDays, fmt, sizeof(fmt));
  snprintf(out, cap, fmt, c.label, (unsigned)days);
}

// ---- find ----

void DeskNotes::find(uint32_t nowMs) {
  finding_ = true;
  findStartMs_ = nowMs;
}

bool DeskNotes::finding(uint32_t nowMs) const { return finding_ && nowMs - findStartMs_ < kFindMs; }

uint32_t DeskNotes::findElapsed(uint32_t nowMs) const { return nowMs - findStartMs_; }

// ---- every frame ----

NoteKind DeskNotes::update(uint32_t nowMs, uint32_t dayKey, uint8_t weekday, int minute) {
  (void)dayKey;
  (void)weekday;
  (void)minute;
  if (finding_ && nowMs - findStartMs_ >= kFindMs) finding_ = false;  // never "finding" again after a wrap
  if (heldKind_ != NoteKind::None && nowMs - heldStartMs_ >= kHeldMs) release();
  if (timerLenMs_ && nowMs - timerStartMs_ >= timerLenMs_) {
    timerLenMs_ = 0;
    due_ |= kDueTimer;
  }
  // One thing at a time: what came due while the cat held something waits for it to go.
  if (heldKind_ != NoteKind::None) return NoteKind::None;
  if (due_ & kDueTimer) {
    due_ &= ~kDueTimer;
    hold(NoteKind::Timer, 0, nowMs);
    return NoteKind::Timer;
  }
  return NoteKind::None;
}

NoteKind DeskNotes::held(uint32_t nowMs) const {
  return heldKind_ != NoteKind::None && nowMs - heldStartMs_ < kHeldMs ? heldKind_ : NoteKind::None;
}

const char* DeskNotes::heldText(uint32_t nowMs) const {
  switch (held(nowMs)) {
    case NoteKind::Reminder: return rem_[heldId_ - 1].text;
    case NoteKind::Alarm: return alarms_[heldId_ - 1 - kMaxReminders].text;
    default: return "";
  }
}

// ---- persistence and listing ----

bool DeskNotes::takeDirty() {
  const bool d = dirty_;
  dirty_ = false;
  return d;
}

void DeskNotes::toJson(JsonObject out) const { (void)out; }

bool DeskNotes::fromJson(JsonObjectConst in) {
  (void)in;
  return false;
}

void DeskNotes::listJson(JsonArray out, uint32_t nowMs, uint32_t nowEpoch) const {
  (void)out;
  (void)nowMs;
  (void)nowEpoch;
}

// ---- request handlers ----
// Every field present is checked before anything changes; *bad names the first bad one, with the
// same names as the CLI's FakeDevice (plugin/test/fakes/fake-device.js).

// A flag such as {"off":true}: absent (on = false), true (on = true), anything else (*bad = key).
static bool flag(JsonObjectConst body, const char* key, bool& on, const char** bad) {
  on = false;
  if (!body.containsKey(key)) return true;
  if (!body[key].is<bool>() || !body[key].as<bool>()) {
    *bad = key;
    return false;
  }
  on = true;
  return true;
}

// A whole number in [lo, hi] (not a float, a string or a boolean).
static bool intField(JsonObjectConst body, const char* key, long lo, long hi, long& out) {
  JsonVariantConst v = body[key];
  if (!v.is<long>()) return false;
  out = v.as<long>();
  return out >= lo && out <= hi;
}

int sayRequest(DeskNotes& n, JsonObjectConst body, uint32_t nowMs, const char** bad) {
  const char* dummy;
  if (!bad) bad = &dummy;
  bool off;
  if (!flag(body, "off", off, bad)) return 400;
  if (off) {
    n.sayOff();
    return 200;
  }
  char text[kNoteBytes];
  if (!cleanText(body["text"].as<const char*>(), text, sizeof(text), kNoteChars)) {
    *bad = "text";
    return 400;
  }
  long min = kSayDefaultMin;
  if (body.containsKey("min") && !intField(body, "min", 1, kSayMaxMin, min)) {
    *bad = "min";
    return 400;
  }
  n.say(text, (uint16_t)min, nowMs);
  return 200;
}

int remindRequest(DeskNotes& n, JsonObjectConst body, uint32_t nowMs, int nowMinute, JsonObject reply,
                  const char** bad) {
  (void)n;
  (void)body;
  (void)nowMs;
  (void)nowMinute;
  (void)reply;
  if (bad) *bad = "in";
  return 400;
}

int timerRequest(DeskNotes& n, JsonObjectConst body, uint32_t nowMs, const char** bad) {
  const char* dummy;
  if (!bad) bad = &dummy;
  bool stop;
  if (!flag(body, "stop", stop, bad)) return 400;
  if (stop) {
    n.timerStop();
    return 200;
  }
  long min;
  if (!intField(body, "min", 1, kTimerMaxMin, min)) {
    *bad = "min";
    return 400;
  }
  n.timerStart((uint16_t)min, nowMs);
  return 200;
}

int countdownRequest(DeskNotes& n, JsonObjectConst body, const Date* today, const char** bad) {
  const char* dummy;
  if (!bad) bad = &dummy;
  bool off;
  if (!flag(body, "off", off, bad)) return 400;
  if (off) {
    n.clearCountdown();
    return 200;
  }
  char label[kLabelBytes];
  if (!cleanText(body["label"].as<const char*>(), label, sizeof(label), kLabelChars)) {
    *bad = "label";
    return 400;
  }
  Date d{0, 0, 0};
  if (body.containsKey("date")) {
    if (!parseDate(body["date"].as<const char*>(), d.year, d.month, d.day)) {
      *bad = "date";
      return 400;
    }
  } else if (body.containsKey("md")) {
    if (!parseMonthDay(body["md"].as<const char*>(), d.month, d.day)) {
      *bad = "md";
      return 400;
    }
    if (!today) {
      *bad = "clock";
      return 409;
    }
    // The next occurrence, today included; 02-29 waits for a leap year (at most 8 years away).
    d.year = today->year;
    for (int i = 0; i < 9; i++, d.year++) {
      const bool leap = d.year % 4 == 0 && (d.year % 100 != 0 || d.year % 400 == 0);
      if ((d.month != 2 || d.day != 29 || leap) && !before(d, *today)) break;
    }
  } else {
    *bad = "date";
    return 400;
  }
  if (today && before(d, *today)) {
    *bad = "date";
    return 400;
  }
  n.setCountdown(label, d);
  return 200;
}

}  // namespace miblo
