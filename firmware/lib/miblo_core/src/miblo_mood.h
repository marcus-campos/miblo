#pragma once
#include <stdint.h>

#include "miblo_snapshot.h"

// The cat's mood of the day (spec 11): expression and rhythm only, never a message.
namespace miblo {

enum class CatMood : uint8_t { Normal, Tired, Playful };
constexpr uint32_t kTiredWorkSec = 8 * 3600;  // 8 h of Claude working today: slow blinks, yawns, dark circles
constexpr uint32_t kLightWorkSec = 2 * 3600;  // under 2 h ...
constexpr uint8_t kLightMaxPct = 50;          // ... and no limit above 50%: plays more in pet mode
CatMood catMoodFor(const Snapshot& s, uint32_t nowEpoch);

}  // namespace miblo
