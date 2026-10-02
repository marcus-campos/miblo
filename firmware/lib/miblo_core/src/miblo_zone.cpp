#include "miblo_zone.h"

namespace miblo {

// Stub (daily-life foundation): track U implements it.
bool zoneHHMM(const char* iana, uint32_t epoch, char* out, size_t cap) {
  (void)iana;
  (void)epoch;
  if (cap) out[0] = 0;
  return false;
}

// Stub (daily-life foundation): track U implements it.
void zoneLabel(const Config& cfg, char* out, size_t cap) {
  (void)cfg;
  if (cap) out[0] = 0;
}

}  // namespace miblo
