#pragma once
#include <stdint.h>

// Anuncia e responde `_miblo._tcp.local` e `miblo-xxxx.local` (respondedor próprio: ver miblo_mdns).
namespace mdns {

void loop(uint32_t nowMs);
// Reanuncia (ex.: o nome do aparelho mudou).
void announce();

}  // namespace mdns
