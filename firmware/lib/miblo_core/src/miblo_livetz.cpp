#include "miblo_livetz.h"

#include <string.h>

#include "miblo_overview.h"
#include "miblo_tz.h"

namespace miblo {

// The gadget's clock may trail the computer's by a little (NTP against the computer's clock).
static constexpr uint32_t kSkewSec = 3600;

void LiveTz::observe(const Snapshot& s) {
  if (!s.zoneCount || !s.now) return;
  count_ = s.zoneCount < kMaxLiveZones ? s.zoneCount : kMaxLiveZones;
  memcpy(zones_, s.zones, count_ * sizeof(LiveZone));
  at_ = s.now;
}

bool LiveTz::offset(const char* zone, uint32_t epoch, int32_t& east) const {
  if (!count_ || !epoch || !zone || !zone[0]) return false;
  // Compared without overflow: at_ - kSkewSec <= epoch <= at_ + kLiveTzMaxAgeSec.
  if (epoch >= at_ ? epoch - at_ > kLiveTzMaxAgeSec : at_ - epoch > kSkewSec) return false;
  const uint32_t h = hashStr(kHashSeed, zone);
  for (uint8_t i = 0; i < count_; i++) {
    const LiveZone& z = zones_[i];
    if (z.zone != h) continue;
    east = (int32_t)(z.next && epoch >= z.next ? z.noff : z.off) * 60;
    return true;
  }
  return false;
}

// Written by hand, not with snprintf: its format strings would sit in RAM on the ESP8266.
void fixedRule(int32_t eastSec, char* out, size_t cap) {
  if (!cap) return;
  const int32_t m = (eastSec < 0 ? -eastSec : eastSec) / 60;
  const int h = (int)(m / 60) % 100, mm = (int)(m % 60);
  char buf[16];
  size_t n = 0;
  buf[n++] = '<';
  buf[n++] = eastSec < 0 ? '-' : '+';
  buf[n++] = (char)('0' + h / 10);
  buf[n++] = (char)('0' + h % 10);
  if (mm) {
    buf[n++] = (char)('0' + mm / 10);
    buf[n++] = (char)('0' + mm % 10);
  }
  buf[n++] = '>';
  if (eastSec > 0) buf[n++] = '-';  // POSIX offsets count west of Greenwich: the opposite sign
  if (h >= 10) buf[n++] = (char)('0' + h / 10);
  buf[n++] = (char)('0' + h % 10);
  if (mm) {
    buf[n++] = ':';
    buf[n++] = (char)('0' + mm / 10);
    buf[n++] = (char)('0' + mm % 10);
  }
  buf[n] = 0;
  const size_t len = n < cap ? n : cap - 1;
  memcpy(out, buf, len);
  out[len] = 0;
}

void liveRule(const char* stored, const LiveTz& live, uint32_t epoch, char* out, size_t cap) {
  int32_t east;
  if (live.offset(stored, epoch, east)) fixedRule(east, out, cap);
  else tzResolve(stored, out, cap);
}

}  // namespace miblo
