#pragma once
#include <stdint.h>

// Announces and responds to `_miblo._tcp.local` and `miblo-xxxx.local` (own responder: see miblo_mdns).
namespace mdns {

// lean: the heap is low (miblo::HeapGuard): queries are drained unanswered and announcements wait.
void loop(uint32_t nowMs, bool lean);
// Re-announces (e.g. the device name changed).
void announce();

}  // namespace mdns
