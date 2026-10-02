#include "miblo_meeting.h"

namespace miblo {

// Stub (daily-life foundation): track B implements it.
void MeetingMode::start(uint16_t minutes, uint32_t nowMs) {
  (void)minutes;
  (void)nowMs;
}

// Stub (daily-life foundation): track B implements it.
bool MeetingMode::update(uint32_t nowMs) {
  (void)nowMs;
  return false;
}

// Stub (daily-life foundation): track B implements it.
uint32_t MeetingMode::leftMs(uint32_t nowMs) const {
  (void)nowMs;
  return 0;
}

// Stub (daily-life foundation): track B implements it.
int meetingRequest(MeetingMode& m, JsonObjectConst body, uint32_t nowMs, const char** bad) {
  (void)m;
  (void)body;
  (void)nowMs;
  if (bad) *bad = "meeting";
  return 400;
}

}  // namespace miblo
