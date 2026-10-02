#pragma once
#include <ArduinoJson.h>
#include <stdint.h>

// Meeting mode, /miblo:meeting: discreet + no names in alerts + 1-blink flash + a tie + a badge,
// for a while (ends by itself; a reboot ends it too). Never hides that a session needs you.
namespace miblo {

constexpr uint16_t kMeetingDefaultMin = 60, kMeetingMaxMin = 480;

class MeetingMode {
 public:
  void start(uint16_t minutes, uint32_t nowMs);
  void stop() { on_ = false; }
  // Every frame: ends it when the time is up. Returns on().
  bool update(uint32_t nowMs);
  bool on() const { return on_; }
  uint32_t leftMs(uint32_t nowMs) const;

 private:
  bool on_ = false;
  uint32_t startMs_ = 0, lenMs_ = 0;
};

// POST /api/meeting: {"min":1..480} (absent = 60) or {"off":true}. 200, or 400 with *bad.
int meetingRequest(MeetingMode& m, JsonObjectConst body, uint32_t nowMs, const char** bad);

}  // namespace miblo
