// Stub (daily-life foundation): track D implements every function in this file. Until then
// nothing is ever shown, fired or saved, and every request is refused with 400.
#include "miblo_desknotes.h"

namespace miblo {

void DeskNotes::say(const char* text, uint16_t minutes, uint32_t nowMs) {
  (void)text;
  (void)minutes;
  (void)nowMs;
}

void DeskNotes::sayOff() {}

const char* DeskNotes::saying(uint32_t nowMs) const {
  (void)nowMs;
  return nullptr;
}

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

bool DeskNotes::dismiss() { return false; }

void DeskNotes::timerStart(uint16_t minutes, uint32_t nowMs) {
  (void)minutes;
  (void)nowMs;
}

void DeskNotes::timerStop() {}

bool DeskNotes::timerRunning() const { return false; }

uint32_t DeskNotes::timerLeftMs(uint32_t nowMs) const {
  (void)nowMs;
  return 0;
}

uint32_t DeskNotes::timerLenMs() const { return 0; }

void DeskNotes::setCountdown(const char* label, const Date& d) {
  (void)label;
  (void)d;
}

void DeskNotes::clearCountdown() {}

const Countdown& DeskNotes::countdown() const {
  static const Countdown kNone{};
  return kNone;
}

void DeskNotes::find(uint32_t nowMs) { (void)nowMs; }

bool DeskNotes::finding(uint32_t nowMs) const {
  (void)nowMs;
  return false;
}

uint32_t DeskNotes::findElapsed(uint32_t nowMs) const {
  (void)nowMs;
  return 0;
}

NoteKind DeskNotes::update(uint32_t nowMs, uint32_t dayKey, uint8_t weekday, int minute) {
  (void)nowMs;
  (void)dayKey;
  (void)weekday;
  (void)minute;
  return NoteKind::None;
}

NoteKind DeskNotes::held(uint32_t nowMs) const {
  (void)nowMs;
  return NoteKind::None;
}

const char* DeskNotes::heldText(uint32_t nowMs) const {
  (void)nowMs;
  return "";
}

bool DeskNotes::takeDirty() { return false; }

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

void countdownLine(Lang lang, const Countdown& c, const Date& today, char* out, size_t cap) {
  (void)lang;
  (void)c;
  (void)today;
  if (cap) out[0] = 0;
}

int sayRequest(DeskNotes& n, JsonObjectConst body, uint32_t nowMs, const char** bad) {
  (void)n;
  (void)body;
  (void)nowMs;
  if (bad) *bad = "say";
  return 400;
}

int remindRequest(DeskNotes& n, JsonObjectConst body, uint32_t nowMs, int nowMinute, JsonObject reply,
                  const char** bad) {
  (void)n;
  (void)body;
  (void)nowMs;
  (void)nowMinute;
  (void)reply;
  if (bad) *bad = "remind";
  return 400;
}

int timerRequest(DeskNotes& n, JsonObjectConst body, uint32_t nowMs, const char** bad) {
  (void)n;
  (void)body;
  (void)nowMs;
  if (bad) *bad = "timer";
  return 400;
}

int countdownRequest(DeskNotes& n, JsonObjectConst body, const Date* today, const char** bad) {
  (void)n;
  (void)body;
  (void)today;
  if (bad) *bad = "countdown";
  return 400;
}

}  // namespace miblo
