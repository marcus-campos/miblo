#pragma once
#include <stddef.h>
#include <stdint.h>

#include "miblo_config.h"
#include "miblo_livetz.h"

// A second clock (spec 11, "Outro fuso").
namespace miblo {

// "HH:MM" at `epoch` in another zone (an IANA name from the table), worked out from the zone's
// POSIX rule (offset and daylight saving): the process TZ is never touched, so localtime_r keeps
// giving the gadget's own time everywhere else. The zone's live offset from the bridge (`live`)
// wins over the table while it is fresh. False when the zone is unknown or epoch is 0.
bool zoneHHMM(const char* iana, uint32_t epoch, char* out, size_t cap, const LiveTz* live = nullptr);
// The name beside it: cfg.tz2Label, else the city of cfg.tz2 ("America/Sao_Paulo" -> "Sao Paulo").
void zoneLabel(const Config& cfg, char* out, size_t cap);

}  // namespace miblo
