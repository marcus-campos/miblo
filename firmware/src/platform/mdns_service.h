#pragma once
#include <stdint.h>

// Announces and responds to `_miblo._tcp.local` and `miblo-xxxx.local` (own responder: see miblo_mdns).
namespace mdns {

void loop(uint32_t nowMs);
// Re-announces (e.g. the device name changed).
void announce();

}  // namespace mdns
