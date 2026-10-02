#pragma once
#include <stddef.h>
#include <stdint.h>

#include "miblo_config.h"

// A second clock (spec 11, "Outro fuso").
namespace miblo {

// "HH:MM" at `epoch` in another zone (an IANA name from the table). Swaps the process TZ for
// the call and puts the previous one back (getenv("TZ") copied first). False when unknown.
bool zoneHHMM(const char* iana, uint32_t epoch, char* out, size_t cap);
// The name beside it: cfg.tz2Label, else the city of cfg.tz2 ("America/Sao_Paulo" -> "Sao Paulo").
void zoneLabel(const Config& cfg, char* out, size_t cap);

}  // namespace miblo
