#pragma once
#include <stdint.h>

// The network side of miblo_friends: broadcasts ctx.friends' packets on the local network and
// feeds it what other Miblos send (UDP port miblo::kFriendPort).
namespace friendsnet {

// lean: the heap is low (miblo::HeapGuard): nothing is sent (packets wait in their fixed queue)
// and what arrives is dropped unread, so the network stack's buffers go back to the heap.
void loop(uint32_t nowMs, bool lean);

}  // namespace friendsnet
