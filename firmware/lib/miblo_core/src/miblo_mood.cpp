#include "miblo_mood.h"

namespace miblo {

// Stub (daily-life foundation): track C implements it.
CatMood catMoodFor(const Snapshot& s, uint32_t nowEpoch) {
  (void)s;
  (void)nowEpoch;
  return CatMood::Normal;
}

}  // namespace miblo
