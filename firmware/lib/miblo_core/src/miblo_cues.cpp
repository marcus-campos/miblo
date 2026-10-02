#include "miblo_cues.h"

#include "miblo_rom.h"

namespace miblo {

uint8_t cuePulses(CueKind k) {
  switch (k) {
    case CueKind::None:
      return 0;
    case CueKind::BreakEnd:
      return 1;  // "back to focus?": one soft pulse
    default:
      return 3;
  }
}

void StrongCue::fire(CueKind k, uint32_t nowMs) {
  kind_ = k;
  sinceMs_ = nowMs;
}

CueKind StrongCue::active(uint32_t nowMs) const {
  if (kind_ == CueKind::None) return CueKind::None;
  // Unsigned difference: safe across the millis() wrap; a long stall simply ends the cue. Once
  // over it is cleared, so a later wrap of nowMs - sinceMs_ into the pulse window stays quiet.
  if (nowMs - sinceMs_ < (uint32_t)cuePulses(kind_) * kCuePulseMs) return kind_;
  kind_ = CueKind::None;
  return CueKind::None;
}

// One pulse, (1 - cos(2 pi k / 16)) / 2 * 255 for k = 0..15: a smooth rise and fall, no floats.
static const uint8_t kWave[16] MIBLO_ROM = {0, 10, 37, 79, 128, 176, 218, 245, 255, 245, 218, 176, 128, 79, 37, 10};

static int32_t wave(uint8_t k) { return mibloRomByte(reinterpret_cast<const char*>(&kWave[k & 15])); }

uint8_t StrongCue::intensity(uint32_t elapsedMs, uint8_t pulses) {
  if (elapsedMs >= (uint32_t)pulses * kCuePulseMs) return 0;
  const uint32_t pos = (elapsedMs % kCuePulseMs) * 16;  // 0 .. 16 * kCuePulseMs - 1
  const uint8_t k = (uint8_t)(pos / kCuePulseMs);
  const int32_t frac = (int32_t)(pos % kCuePulseMs);
  const int32_t a = wave(k);
  const int32_t b = wave(k + 1);  // k = 15 wraps to 0: the pulse ends dark
  return (uint8_t)(a + (b - a) * frac / (int32_t)kCuePulseMs);
}

uint8_t cueBrightness(const Config& cfg, int minuteOfDay) {
  if (!nightActive(cfg, minuteOfDay)) return 100;
  const uint16_t twice = 2 * (uint16_t)brightnessAt(cfg, minuteOfDay);
  return (uint8_t)(twice > 100 ? 100 : twice);
}

FrameColor frameColorFor(const Snapshot& s, uint32_t nowEpoch) {
  bool green = false;
  for (int i = 0; i < s.count; i++) {
    const SessionRow& r = s.sessions[i];
    if (r.st == SessionState::Perm || r.st == SessionState::Question) return FrameColor::Amber;
    if (r.st != SessionState::Done || nowEpoch == 0) continue;  // without the time, no telling
    // Green for a minute after the finish; a computer clock up to a minute ahead counts as now.
    const int32_t ago = (int32_t)(nowEpoch - r.since);
    if (ago < (int32_t)kFrameGreenSec && ago > -(int32_t)kFrameGreenSec) green = true;
  }
  return green ? FrameColor::Green : FrameColor::None;
}

}  // namespace miblo
