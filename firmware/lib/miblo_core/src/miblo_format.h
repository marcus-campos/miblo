#pragma once
#include <stddef.h>
#include <stdint.h>

namespace miblo {

// Elapsed time: 42 → "0:42", 192 → "3:12", 3725 → "1:02:05".
void formatElapsed(uint32_t secs, char* out, size_t cap);

// The big timers (focus, the timer): 0 -> "0:00", 1122 -> "18:42", 3909 -> "1:05:09".
// Same shape as formatElapsed; its own name so the timer screens can change it alone.
inline void formatMinSec(uint32_t secs, char* out, size_t cap) { formatElapsed(secs, out, cap); }

// Short time (list/"X ago"): 45 → "45s", 150 → "2m", 7300 → "2h", 180000 → "2d".
void formatAgo(uint32_t secs, char* out, size_t cap);

// Countdown: 30 → "<1min", 2700 → "45min", 7800 → "2h10", 240000 → "2d18h".
void formatCountdown(uint32_t secs, char* out, size_t cap);

// Time in a state, minute granularity (changes at most once a minute, so the screen doesn't
// tick every second): 30 → "<1m", 192 → "3m", 4320 → "1h12", 180000 → "2d2h".
void formatInState(uint32_t secs, char* out, size_t cap);

// Tokens (always rounded down): 950 → "950", 1500 → "1.5k", 12300 → "12.3k",
// 98000 → "98k", 412000 → "412k", 1510000 → "1.5M", 2000000 → "2M", 123456789 → "123M".
void formatTokens(uint64_t tok, char* out, size_t cap);

// "HH:MM" with leading zeros.
void formatHHMM(int hour, int minute, char* out, size_t cap);

// Dollars with 2 decimal places: 4.8 → "$4.80".
void formatUsd(float usd, char* out, size_t cap);

// Compares "X.Y.Z" versions numerically: <0, 0 or >0 like strcmp. Missing parts count as 0 and
// anything after the digits of a part is ignored ("1.2.3-rc" == "1.2.3").
int compareVersions(const char* a, const char* b);

}  // namespace miblo
