#pragma once
#include <stdint.h>

// The brute-force lockouts (pairing and each presence purpose, miblo::EscalatingLockout) kept in
// RTC user memory, so a reset, a crash or a watchdog never hands out fresh guesses (M2). A power
// cut clears RTC memory: the counters start over then (unplugging is a physical act anyway).
// A factory reset clears them too (clear()).
namespace lockouts {
void restore(uint32_t nowMs);  // once at boot, after the context is set up
void persist(uint32_t nowMs);  // regularly from the loop: writes only when something changed
// Forgets the stored counters: a factory reset (which needs a code, an authorised session or six
// power-ons in a row) starts the unit over, its lockouts included. Reboots keep them.
void clear();
}  // namespace lockouts
