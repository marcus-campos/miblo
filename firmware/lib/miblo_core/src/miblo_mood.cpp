#include "miblo_mood.h"

namespace miblo {

// A window's percentage as the Desk shows it (screens::deskPct): one whose reset time has passed
// is back to 0.
static uint8_t livePct(const UsageWindow& w, uint32_t nowEpoch) {
  if (!w.present) return 0;
  if (w.reset && nowEpoch && nowEpoch >= w.reset) return 0;
  return w.pct;
}

CatMood catMoodFor(const Snapshot& s, uint32_t nowEpoch) {
  if (s.todayWorkSec >= kTiredWorkSec) return CatMood::Tired;
  if (s.todayWorkSec < kLightWorkSec && livePct(s.h5, nowEpoch) <= kLightMaxPct &&
      livePct(s.d7, nowEpoch) <= kLightMaxPct) {
    return CatMood::Playful;
  }
  return CatMood::Normal;
}

}  // namespace miblo
