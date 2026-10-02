#pragma once
#include <stdint.h>

#include "miblo_config.h"
#include "miblo_snapshot.h"

// Cues for peripheral vision (spec 10): the strong visual cue (slow full-screen pulses with the
// backlight up), the optional status frame.
namespace miblo {

enum class CueKind : uint8_t { None, FocusEnd, BreakEnd, Timer, Alarm, Reminder };
constexpr uint32_t kCuePulseMs = 1500;  // one slow pulse
uint8_t cuePulses(CueKind k);           // BreakEnd 1, the others 3

class StrongCue {
 public:
  void fire(CueKind k, uint32_t nowMs);
  // None once its pulses are over (and from then on: the cue is cleared, so millis() wrapping
  // back near sinceMs_ ~49.7 days later never brings it back).
  CueKind active(uint32_t nowMs) const;
  uint32_t elapsed(uint32_t nowMs) const { return nowMs - sinceMs_; }
  // 0..255 at `elapsedMs` into `pulses` pulses (smooth rise and fall, 0 between pulses).
  static uint8_t intensity(uint32_t elapsedMs, uint8_t pulses);

 private:
  mutable CueKind kind_ = CueKind::None;  // cleared by active() when the pulses end
  uint32_t sinceMs_ = 0;
};

// Backlight during a cue: 100%; while night mode dims the screen, at most twice the night
// brightness (and never less than it).
uint8_t cueBrightness(const Config& cfg, int minuteOfDay);

enum class FrameColor : uint8_t { None, Amber, Green };
constexpr uint32_t kFrameGreenSec = 60;
// Amber while a session waits for you; green for a minute after one finished; else None.
FrameColor frameColorFor(const Snapshot& s, uint32_t nowEpoch);

}  // namespace miblo
