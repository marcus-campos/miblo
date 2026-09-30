#pragma once
#include <stdint.h>

// The network side of miblo_friends: broadcasts ctx.friends' packets on the local network and
// feeds it what other Miblos send (UDP port miblo::kFriendPort).
namespace friendsnet {

void loop(uint32_t nowMs);

}  // namespace friendsnet
