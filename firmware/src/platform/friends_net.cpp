#include "friends_net.h"

#include <WiFiUdp.h>

#include "../context.h"
#include "miblo_friends.h"
#include "net.h"
#include "platform.h"

namespace friendsnet {

static WiFiUDP udp;
static bool bound = false;
static uint32_t boundConn = 0;

void loop(uint32_t nowMs) {
  if (!net::connected() || !ctx.cfg.friends) {
    if (bound) {
      udp.stop();
      bound = false;
    }
    miblo::FriendPacket drop;
    while (ctx.friends.nextPacket(drop)) {
    }
    return;
  }
  if (!bound || boundConn != net::connectionId()) {
    udp.stop();
    bound = udp.begin(miblo::kFriendPort);
    boundConn = net::connectionId();
  }
  if (!bound) return;

  uint8_t buf[miblo::kFriendPacketMax];
  miblo::FriendPacket p;
  while (ctx.friends.nextPacket(p)) {
    const size_t n = miblo::encodeFriendPacket(p, buf, sizeof(buf));
    if (!n) continue;
    udp.beginPacket(WiFi.broadcastIP(), miblo::kFriendPort);
    udp.write(buf, n);
    udp.endPacket();
  }
  // A few packets per pass; anything bigger than a Miblo packet is skipped unread, and so is
  // anything that cannot come from another Miblo on this network: Miblos send from kFriendPort,
  // from an address of our own subnet that is neither ours (our broadcast coming back, or our
  // address spoofed) nor the subnet's network or broadcast address.
  const uint32_t self = (uint32_t)WiFi.localIP();
  const uint32_t mask = (uint32_t)WiFi.subnetMask();
  for (int k = 0; k < 4; k++) {
    const int len = udp.parsePacket();
    if (len <= 0) return;
    if (len > (int)sizeof(buf)) continue;
    const uint32_t from = (uint32_t)udp.remoteIP();
    const uint32_t host = from & ~mask;
    if (udp.remotePort() != miblo::kFriendPort || from == self || (from & mask) != (self & mask) || host == 0 ||
        host == ~mask) {
      continue;
    }
    udp.read(buf, len);
    if (miblo::decodeFriendPacket(buf, (size_t)len, p)) ctx.friends.receive(p, nowMs, from);
  }
}

}  // namespace friendsnet
