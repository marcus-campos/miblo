#include "miblo_wellness.h"

namespace miblo {

// Stub (daily-life foundation): track C implements it.
void WellnessClock::update(uint32_t nowMs, const Config& cfg, bool working, bool workHours, bool allowed) {
  (void)nowMs;
  (void)cfg;
  (void)working;
  (void)workHours;
  (void)allowed;
}

// Stub (daily-life foundation): track C implements it.
Nudge WellnessClock::showing(uint32_t nowMs) const {
  (void)nowMs;
  return Nudge::None;
}

// Stub (daily-life foundation): track C implements it.
void WellnessClock::reset() {}

}  // namespace miblo
