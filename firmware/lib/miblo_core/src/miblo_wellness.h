#pragma once
#include <stdint.h>

#include "miblo_config.h"

// Healthy breaks (spec 2), all off by default: a break after long continuous work, water every
// N minutes of the work hours, eye rest (20-20-20 by default: every cfg.eyesEveryMin minutes of
// continuous work, for cfg.eyesSec seconds). Never during an alert, focus, a meeting or pet mode:
// the caller says so with `allowed`.
namespace miblo {

enum class Nudge : uint8_t { None, Break, Water, Eyes };
constexpr uint32_t kWorkGapMs = 600000;     // up to 10 min without work still counts as continuous
constexpr uint32_t kBreakNudgeMs = 60000, kWaterNudgeMs = 20000;
constexpr uint32_t kEyesNudgeMs = 20000;    // the default eye rest (Config::eyesSec); at most 60 s
constexpr uint32_t kNudgeWaitMs = 300000;   // a nudge due while blocked waits this long, then is skipped
constexpr uint32_t kActiveMs = 60000;       // break and eye rest come due only within a minute of work

class WellnessClock {
 public:
  // Every frame. `working`: a session is running. `workHours`: inside the work hours (water).
  // `allowed`: nothing more important is on (no alert, focus, meeting, pet mode, daily screen).
  void update(uint32_t nowMs, const Config& cfg, bool working, bool workHours, bool allowed);
  Nudge showing(uint32_t nowMs) const;
  uint32_t elapsed(uint32_t nowMs) const { return nowMs - shownMs_; }
  void reset();

 private:
  uint32_t showMs() const;
  // The 1-byte members first: no padding between them (32 B on the ESP8266).
  Nudge shown_ = Nudge::None;
  Nudge due_ = Nudge::None;
  bool working_ = false;
  bool inHours_ = false;
  uint8_t eyesSec_ = 20;  // the eye rest showing keeps the length it started with
  uint32_t shownMs_ = 0;
  uint32_t workStartMs_ = 0, lastWorkMs_ = 0, eyesFromMs_ = 0;
  uint32_t waterFromMs_ = 0;
  uint32_t dueMs_ = 0;
};

}  // namespace miblo
