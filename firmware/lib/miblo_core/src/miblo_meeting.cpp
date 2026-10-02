#include "miblo_meeting.h"

namespace miblo {

void MeetingMode::start(uint16_t minutes, uint32_t nowMs) {
  on_ = true;
  startMs_ = nowMs;
  lenMs_ = (uint32_t)minutes * 60000u;
}

bool MeetingMode::update(uint32_t nowMs) {
  if (on_ && nowMs - startMs_ >= lenMs_) on_ = false;  // wrap-safe; a long stall just ends it
  return on_;
}

uint32_t MeetingMode::leftMs(uint32_t nowMs) const {
  if (!on_) return 0;
  const uint32_t elapsed = nowMs - startMs_;
  return elapsed >= lenMs_ ? 0 : lenMs_ - elapsed;
}

// {"off":true} wins over "min" (turning it off is never a mistake worth a 400).
int meetingRequest(MeetingMode& m, JsonObjectConst body, uint32_t nowMs, const char** bad) {
  JsonVariantConst off = body["off"];
  if (!off.isNull()) {
    if (!off.is<bool>() || !off.as<bool>()) {
      if (bad) *bad = "off";
      return 400;
    }
    m.stop();
    return 200;
  }
  uint16_t minutes = kMeetingDefaultMin;
  if (body.containsKey("min")) {
    JsonVariantConst v = body["min"];
    // is<int>() is false for floats, strings and null.
    if (!v.is<int>() || v.as<int>() < 1 || v.as<int>() > kMeetingMaxMin) {
      if (bad) *bad = "min";
      return 400;
    }
    minutes = (uint16_t)v.as<int>();
  }
  m.start(minutes, nowMs);
  return 200;
}

}  // namespace miblo
