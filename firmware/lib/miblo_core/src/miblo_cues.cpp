#include "miblo_cues.h"

namespace miblo {

// Stub (daily-life foundation): track E implements it.
uint8_t cuePulses(CueKind k) {
  (void)k;
  return 3;
}

// Stub (daily-life foundation): track E implements it.
void StrongCue::fire(CueKind k, uint32_t nowMs) {
  (void)k;
  (void)nowMs;
}

// Stub (daily-life foundation): track E implements it.
CueKind StrongCue::active(uint32_t nowMs) const {
  (void)nowMs;
  return CueKind::None;
}

// Stub (daily-life foundation): track E implements it.
uint8_t StrongCue::intensity(uint32_t elapsedMs, uint8_t pulses) {
  (void)elapsedMs;
  (void)pulses;
  return 0;
}

// Stub (daily-life foundation): track E implements it.
uint8_t cueBrightness(const Config& cfg, int minuteOfDay) { return brightnessAt(cfg, minuteOfDay); }

// Stub (daily-life foundation): track E implements it.
FrameColor frameColorFor(const Snapshot& s, uint32_t nowEpoch) {
  (void)s;
  (void)nowEpoch;
  return FrameColor::None;
}

}  // namespace miblo
