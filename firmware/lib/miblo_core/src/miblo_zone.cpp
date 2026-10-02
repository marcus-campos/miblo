#include "miblo_zone.h"

#include <stdio.h>
#include <string.h>

#include "miblo_format.h"
#include "miblo_tz.h"

namespace miblo {

namespace {

// Days since 1970-01-01 of a civil date (proleptic Gregorian; H. Hinnant's algorithm).
int32_t daysFromCivil(int32_t y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int32_t)doe - 719468;
}

// The year a day count (since 1970-01-01) falls in: the inverse of daysFromCivil, year only.
int32_t yearOfDays(int32_t z) {
  z += 719468;
  const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = (unsigned)(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  return (int32_t)yoe + era * 400 + (mp >= 10);  // March-based year: Jan and Feb belong to the next
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }

unsigned number(const char*& p) {
  unsigned v = 0;
  while (isDigit(*p)) v = v * 10 + (unsigned)(*p++ - '0');
  return v;
}

// "[+-]h[:mm[:ss]]" in seconds. False without a digit.
bool clockValue(const char*& p, int32_t& out) {
  int32_t sign = 1;
  if (*p == '+' || *p == '-') sign = *p++ == '-' ? -1 : 1;
  if (!isDigit(*p)) return false;
  int32_t v = (int32_t)number(p) * 3600;
  for (int32_t unit = 60; unit && *p == ':'; unit /= 60) {
    p++;
    v += (int32_t)number(p) * unit;
  }
  out = sign * v;
  return true;
}

// A zone abbreviation: "<+0330>" or 3+ letters ("WET").
bool skipName(const char*& p) {
  if (*p == '<') {
    const char* close = strchr(p, '>');
    if (!close) return false;
    p = close + 1;
    return true;
  }
  const char* s = p;
  while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) p++;
  return p - s >= 3;
}

// ",Mm.w.d[/time]" in `year`: the day it falls on (days since 1970) and the local time of day (s).
bool transition(const char*& p, int32_t year, int32_t& day, int32_t& at) {
  if (*p++ != ',' || *p++ != 'M') return false;
  const unsigned m = number(p);
  if (*p++ != '.') return false;
  const unsigned w = number(p);
  if (*p++ != '.') return false;
  const unsigned d = number(p);
  if (m < 1 || m > 12 || w < 1 || w > 5 || d > 6) return false;
  at = 2 * 3600;
  if (*p == '/' && !clockValue(++p, at)) return false;
  const int32_t first = daysFromCivil(year, m, 1);
  const int32_t next = m == 12 ? daysFromCivil(year + 1, 1, 1) : daysFromCivil(year, m + 1, 1);
  const unsigned firstWday = (unsigned)(((first % 7) + 7 + 4) % 7);  // 1970-01-01 was a Thursday
  day = first + (int32_t)((d + 7 - firstWday) % 7) + 7 * (int32_t)(w - 1);
  while (day >= next) day -= 7;  // week 5 = the last one in the month
  return true;
}

// Seconds east of UTC at `utc` for a POSIX TZ rule ("WET0WEST,M3.5.0/1,M10.5.0"; the table only
// has Mm.w.d transitions). False when the rule cannot be read.
bool utcOffset(const char* tz, int64_t utc, int32_t& east) {
  const char* p = tz;
  int32_t stdOff;  // POSIX offsets count west of Greenwich
  if (!skipName(p) || !clockValue(p, stdOff)) return false;
  east = -stdOff;
  if (!*p) return true;
  if (!skipName(p)) return false;
  int32_t dstOff = stdOff - 3600;
  if (*p && *p != ',' && !clockValue(p, dstOff)) return false;
  if (!*p) return true;  // a daylight name without rules: standard time all year
  const int32_t year = yearOfDays((int32_t)((utc - stdOff) / 86400));
  int32_t d0, t0, d1, t1;
  if (!transition(p, year, d0, t0) || !transition(p, year, d1, t1) || *p) return false;
  // The start is written in standard time, the end in daylight time.
  const int64_t start = (int64_t)d0 * 86400 + t0 + stdOff;
  const int64_t end = (int64_t)d1 * 86400 + t1 + dstOff;
  const bool dst = start < end ? (utc >= start && utc < end) : (utc >= start || utc < end);
  east = -(dst ? dstOff : stdOff);
  return true;
}

}  // namespace

bool zoneHHMM(const char* iana, uint32_t epoch, char* out, size_t cap, const LiveTz* live) {
  if (cap) out[0] = 0;
  char rule[48];
  int32_t east;
  if (!iana || !iana[0] || !epoch || !tzLookup(iana, rule, sizeof(rule))) return false;
  if (!(live && live->offset(iana, epoch, east)) && !utcOffset(rule, epoch, east)) return false;
  const int64_t local = (int64_t)epoch + east;
  const int32_t secOfDay = (int32_t)(((local % 86400) + 86400) % 86400);
  formatHHMM(secOfDay / 3600, secOfDay / 60 % 60, out, cap);
  return true;
}

void zoneLabel(const Config& cfg, char* out, size_t cap) {
  if (!cap) return;
  if (cfg.tz2Label[0]) {
    snprintf(out, cap, "%s", cfg.tz2Label);
    return;
  }
  const char* city = strrchr(cfg.tz2, '/');
  snprintf(out, cap, "%s", city ? city + 1 : cfg.tz2);
  for (char* c = out; *c; c++) {
    if (*c == '_') *c = ' ';
  }
}

}  // namespace miblo
