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

// ---- JSON fields (requests and /notes.json) ----

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

static bool leapYear(unsigned y) { return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0); }

// A real calendar day (no table: a static array would sit in RAM on the ESP8266).
static bool realDay(long y, long m, long d) {
  if (m < 1 || m > 12 || d < 1) return false;
  const long days = m == 2 ? 28 + leapYear((unsigned)y) : 30 + ((m + (m > 7)) & 1);
  return d <= days;
}

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

uint8_t DeskNotes::addReminder(uint32_t dueMs, const char* text) {
  char clean[kNoteBytes];
  if (!cleanText(text, clean, sizeof(clean), kNoteChars)) return 0;
  uint8_t slot = kMaxReminders;
  for (uint8_t i = 0; i < kMaxReminders && slot == kMaxReminders; i++) {
    if (!rem_[i].text[0]) slot = i;
  }
  // A reminder the cat is holding still occupies its slot (its text lives there): a new one
  // takes its place rather than being refused, and the cat puts the old one down.
  if (slot == kMaxReminders && heldKind_ == NoteKind::Reminder) {
    slot = heldId_ - 1;
    release();
  }
  if (slot == kMaxReminders) return 0;
  rem_[slot].dueMs = dueMs;
  memcpy(rem_[slot].text, clean, sizeof(clean));
  return slot + 1;
}

uint8_t DeskNotes::remindIn(uint16_t minutes, const char* text, uint32_t nowMs) {
  if (!minutes || minutes > 1440) return 0;
  return addReminder(nowMs + minutes * kMinMs, text);
}

uint8_t DeskNotes::remindAt(uint16_t minute, int nowMinute, const char* text, uint32_t nowMs) {
  if (minute >= 1440 || nowMinute < 0 || nowMinute >= 1440) return 0;
  int ahead = (int)minute - nowMinute;
  if (ahead <= 0) ahead += 1440;  // this minute or earlier: tomorrow
  return addReminder(nowMs + (uint32_t)ahead * kMinMs, text);
}

uint8_t DeskNotes::addAlarm(uint16_t minute, uint8_t days, const char* text) {
  char clean[kNoteBytes];
  if (minute >= 1440 || !days || days > 0x7F || !cleanText(text, clean, sizeof(clean), kNoteChars)) return 0;
  for (uint8_t i = 0; i < kMaxAlarms; i++) {
    Alarm& a = alarms_[i];
    if (a.days) continue;
    a.minute = minute;
    a.days = days;
    a.lastDay = 0;
    memcpy(a.text, clean, sizeof(clean));
    due_ &= ~(1u << i);
    dirty_ = true;
    return kMaxReminders + 1 + i;
  }
  return 0;
}

bool DeskNotes::remove(uint8_t id) {
  if (id >= 1 && id <= kMaxReminders) {
    if (!rem_[id - 1].text[0]) return false;
    if (heldKind_ == NoteKind::Reminder && heldId_ == id) release();
    rem_[id - 1].text[0] = 0;
    return true;
  }
  if (id > kMaxReminders && id <= kMaxReminders + kMaxAlarms) {
    const uint8_t i = id - kMaxReminders - 1;
    if (!alarms_[i].days) return false;
    if (heldKind_ == NoteKind::Alarm && heldId_ == id) release();
    alarms_[i] = Alarm{};
    due_ &= ~(1u << i);
    dirty_ = true;
    return true;
  }
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
  if (finding_ && nowMs - findStartMs_ >= kFindMs) finding_ = false;  // never "finding" again after a wrap
  if (heldKind_ != NoteKind::None && nowMs - heldStartMs_ >= kHeldMs) release();
  if (timerLenMs_ && nowMs - timerStartMs_ >= timerLenMs_) {
    timerLenMs_ = 0;
    due_ |= kDueTimer;
  }
  // Alarms only with the local time known: at their minute, or the one after it when the loop
  // missed it; once per day.
  if (dayKey && minute >= 0 && weekday < 7) {
    for (uint8_t i = 0; i < kMaxAlarms; i++) {
      Alarm& a = alarms_[i];
      const int late = minute - (int)a.minute;
      if (!(a.days >> weekday & 1) || late < 0 || late > 1 || a.lastDay == (uint16_t)dayKey) continue;
      a.lastDay = (uint16_t)dayKey;
      due_ |= 1u << i;
    }
  }
  // One thing at a time: what came due while the cat held something waits for it to go.
  if (heldKind_ != NoteKind::None) return NoteKind::None;
  if (due_ & kDueTimer) {
    due_ &= ~kDueTimer;
    hold(NoteKind::Timer, 0, nowMs);
    return NoteKind::Timer;
  }
  // The most overdue one-off reminder.
  uint8_t pick = kMaxReminders;
  for (uint8_t i = 0; i < kMaxReminders; i++) {
    if (!rem_[i].text[0] || (int32_t)(nowMs - rem_[i].dueMs) < 0) continue;
    if (pick == kMaxReminders || (int32_t)(rem_[pick].dueMs - rem_[i].dueMs) > 0) pick = i;
  }
  if (pick < kMaxReminders) {
    hold(NoteKind::Reminder, pick + 1, nowMs);
    return NoteKind::Reminder;
  }
  for (uint8_t i = 0; i < kMaxAlarms; i++) {
    if (!(due_ & (1u << i))) continue;
    due_ &= ~(1u << i);
    hold(NoteKind::Alarm, kMaxReminders + 1 + i, nowMs);
    return NoteKind::Alarm;
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

// {"v":1,"alarms":[{"m":585,"d":62,"t":"daily"}],"cd":{"l":"release","y":2026,"mo":10,"d":15}}
// Texts are not copied: the document must not outlive this object.
void DeskNotes::toJson(JsonObject out) const {
  out["v"] = 1;
  JsonArray arr = out.createNestedArray("alarms");
  for (const Alarm& a : alarms_) {
    if (!a.days) continue;
    JsonObject o = arr.createNestedObject();
    o["m"] = a.minute;
    o["d"] = a.days;
    o["t"] = (const char*)a.text;
  }
  if (countdown_.label[0]) {
    JsonObject cd = out.createNestedObject("cd");
    cd["l"] = (const char*)countdown_.label;
    cd["y"] = countdown_.date.year;
    cd["mo"] = countdown_.date.month;
    cd["d"] = countdown_.date.day;
  }
}

// Read back from /notes.json, which may be damaged: anything that is not exactly what toJson
// writes is skipped, entry by entry. False (nothing changed) when it is not a version-1 object.
bool DeskNotes::fromJson(JsonObjectConst in) {
  if (in.isNull() || !in["v"].is<int>() || in["v"].as<int>() != 1) return false;
  for (uint8_t i = 0; i < kMaxAlarms; i++) {
    if (heldKind_ == NoteKind::Alarm && heldId_ == kMaxReminders + 1 + i) release();
    alarms_[i] = Alarm{};
  }
  due_ &= kDueTimer;
  for (JsonObjectConst a : in["alarms"].as<JsonArrayConst>()) {
    long m, d;
    if (!intField(a, "m", 0, 1439, m) || !intField(a, "d", 1, 0x7F, d)) continue;
    if (!addAlarm((uint16_t)m, (uint8_t)d, a["t"].as<const char*>())) continue;
  }
  countdown_ = Countdown{};
  JsonObjectConst cd = in["cd"];
  long y, mo, d;
  if (intField(cd, "y", 2020, 2199, y) && intField(cd, "mo", 1, 12, mo) && intField(cd, "d", 1, 31, d) &&
      realDay(y, mo, d)) {
    setCountdown(cd["l"].as<const char*>(), Date{(uint16_t)y, (uint8_t)mo, (uint8_t)d});
  }
  dirty_ = false;
  return true;
}

// GET /api/remind: [{"id":1,"in":840,"text":"…"}, {"id":5,"at":"09:45","days":62,"text":"daily"}]
// (`in`: seconds left, rounded up; 0 = due, waiting for the cat). Texts are not copied.
void DeskNotes::listJson(JsonArray out, uint32_t nowMs, uint32_t nowEpoch) const {
  (void)nowEpoch;
  for (uint8_t i = 0; i < kMaxReminders; i++) {
    const Reminder& r = rem_[i];
    if (!r.text[0] || (heldKind_ == NoteKind::Reminder && heldId_ == i + 1)) continue;
    const int32_t left = (int32_t)(r.dueMs - nowMs);
    JsonObject o = out.createNestedObject();
    o["id"] = i + 1;
    o["in"] = left > 0 ? ((uint32_t)left + 999) / 1000 : 0;
    o["text"] = (const char*)r.text;
  }
  for (uint8_t i = 0; i < kMaxAlarms; i++) {
    const Alarm& a = alarms_[i];
    if (!a.days) continue;
    const unsigned h = a.minute / 60, m = a.minute % 60;
    const char at[6] = {(char)('0' + h / 10), (char)('0' + h % 10), ':', (char)('0' + m / 10), (char)('0' + m % 10), 0};
    JsonObject o = out.createNestedObject();
    o["id"] = kMaxReminders + 1 + i;
    o["at"] = at;  // char[]: copied
    o["days"] = a.days;
    o["text"] = (const char*)a.text;
  }
}

// ---- request handlers ----
// Every field present is checked before anything changes; *bad names the first bad one, with the
// same names as the CLI's FakeDevice (plugin/test/fakes/fake-device.js).

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

// "HH:MM" (two digits each) -> minute of the day.
static bool parseHHMM(const char* s, uint16_t& minute) {
  if (!s || strlen(s) != 5 || s[2] != ':') return false;
  for (int i = 0; i < 5; i++) {
    if (i != 2 && (s[i] < '0' || s[i] > '9')) return false;
  }
  const int h = (s[0] - '0') * 10 + (s[1] - '0'), m = (s[3] - '0') * 10 + (s[4] - '0');
  if (h > 23 || m > 59) return false;
  minute = (uint16_t)(h * 60 + m);
  return true;
}

int remindRequest(DeskNotes& n, JsonObjectConst body, uint32_t nowMs, int nowMinute, JsonObject reply,
                  const char** bad) {
  const char* dummy;
  if (!bad) bad = &dummy;
  if (body.containsKey("dismiss")) {
    bool on;
    if (!flag(body, "dismiss", on, bad)) return 400;
    if (n.held(nowMs) == NoteKind::None) {
      *bad = "none";
      return 409;
    }
    n.dismiss();
    return 200;
  }
  if (body.containsKey("delete")) {
    long id;
    if (!intField(body, "delete", 1, kMaxReminders + kMaxAlarms, id) || !n.remove((uint8_t)id)) {
      *bad = "delete";
      return 400;
    }
    return 200;
  }
  char text[kNoteBytes];
  const bool textOk = cleanText(body["text"].as<const char*>(), text, sizeof(text), kNoteChars);
  uint8_t id;
  if (body.containsKey("in")) {
    long min;
    if (!intField(body, "in", 1, 1440, min)) {
      *bad = "in";
      return 400;
    }
    if (!textOk) {
      *bad = "text";
      return 400;
    }
    id = n.remindIn((uint16_t)min, text, nowMs);
  } else if (body.containsKey("at")) {
    uint16_t minute;
    long days = 0;
    if (!parseHHMM(body["at"].as<const char*>(), minute)) {
      *bad = "at";
      return 400;
    }
    if (body.containsKey("days") && !intField(body, "days", 1, 0x7F, days)) {
      *bad = "days";
      return 400;
    }
    if (!textOk) {
      *bad = "text";
      return 400;
    }
    if (days) {
      id = n.addAlarm(minute, (uint8_t)days, text);
    } else {
      if (nowMinute < 0) {  // the time is unknown: "at 16:30" can't be placed
        *bad = "clock";
        return 409;
      }
      id = n.remindAt(minute, nowMinute, text, nowMs);
    }
  } else {
    *bad = "in";
    return 400;
  }
  if (!id) {
    *bad = "full";
    return 409;
  }
  reply["id"] = id;
  return 200;
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
      if (realDay(d.year, d.month, d.day) && !before(d, *today)) break;
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
