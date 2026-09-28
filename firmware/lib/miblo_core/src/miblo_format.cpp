#include "miblo_format.h"

#include <stdio.h>

namespace miblo {

void formatElapsed(uint32_t secs, char* out, size_t cap) {
  uint32_t h = secs / 3600;
  uint32_t m = (secs / 60) % 60;
  uint32_t s = secs % 60;
  if (h > 0) {
    snprintf(out, cap, "%u:%02u:%02u", (unsigned)h, (unsigned)m, (unsigned)s);
  } else {
    snprintf(out, cap, "%u:%02u", (unsigned)m, (unsigned)s);
  }
}

void formatAgo(uint32_t secs, char* out, size_t cap) {
  if (secs < 60) {
    snprintf(out, cap, "%us", (unsigned)secs);
  } else if (secs < 3600) {
    snprintf(out, cap, "%um", (unsigned)(secs / 60));
  } else if (secs < 86400) {
    snprintf(out, cap, "%uh", (unsigned)(secs / 3600));
  } else {
    snprintf(out, cap, "%ud", (unsigned)(secs / 86400));
  }
}

void formatCountdown(uint32_t secs, char* out, size_t cap) {
  if (secs < 60) {
    snprintf(out, cap, "<1min");
  } else if (secs < 3600) {
    snprintf(out, cap, "%umin", (unsigned)(secs / 60));
  } else if (secs < 86400) {
    snprintf(out, cap, "%uh%02u", (unsigned)(secs / 3600), (unsigned)((secs / 60) % 60));
  } else {
    snprintf(out, cap, "%ud%uh", (unsigned)(secs / 86400), (unsigned)((secs / 3600) % 24));
  }
}

void formatInState(uint32_t secs, char* out, size_t cap) {
  if (secs < 60) {
    snprintf(out, cap, "<1m");
  } else if (secs < 3600) {
    snprintf(out, cap, "%um", (unsigned)(secs / 60));
  } else if (secs < 86400) {
    snprintf(out, cap, "%uh%02u", (unsigned)(secs / 3600), (unsigned)((secs / 60) % 60));
  } else {
    snprintf(out, cap, "%ud%uh", (unsigned)(secs / 86400), (unsigned)((secs / 3600) % 24));
  }
}

static void withTenths(uint64_t tenths, char suffix, char* out, size_t cap) {
  unsigned whole = (unsigned)(tenths / 10);
  unsigned frac = (unsigned)(tenths % 10);
  if (whole >= 100 || frac == 0) {
    snprintf(out, cap, "%u%c", whole, suffix);
  } else {
    snprintf(out, cap, "%u.%u%c", whole, frac, suffix);
  }
}

void formatTokens(uint64_t tok, char* out, size_t cap) {
  if (tok < 1000) {
    snprintf(out, cap, "%u", (unsigned)tok);
  } else if (tok < 1000000) {
    withTenths(tok / 100, 'k', out, cap);
  } else {
    withTenths(tok / 100000, 'M', out, cap);
  }
}

void formatHHMM(int hour, int minute, char* out, size_t cap) {
  snprintf(out, cap, "%02d:%02d", hour, minute);
}

void formatUsd(float usd, char* out, size_t cap) {
  if (usd < 0) usd = 0;
  unsigned cents = (unsigned)(usd * 100.0f + 0.5f);
  snprintf(out, cap, "$%u.%02u", cents / 100, cents % 100);
}

}  // namespace miblo
