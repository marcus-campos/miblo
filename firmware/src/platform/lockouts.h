#pragma once
#include <stdint.h>

// The brute-force lockouts (pairing and each presence purpose, miblo::EscalatingLockout) kept in
// RTC user memory, so a reset, a crash or a watchdog never hands out fresh guesses (M2). A power
// cut clears RTC memory: the counters start over then (unplugging is a physical act anyway).
namespace lockouts {
void restore(uint32_t nowMs);  // once at boot, after the context is set up
void persist(uint32_t nowMs);  // regularly from the loop: writes only when something changed
}  // namespace lockouts
