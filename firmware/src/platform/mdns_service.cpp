#include "mdns_service.h"

#include <WiFiUdp.h>
#if defined(ESP8266)
#include <lwip/igmp.h>
#include <lwip/netif.h>
#endif

#include "../context.h"
#include "miblo_mdns.h"
#include "net.h"
#include "platform.h"

namespace mdns {

static WiFiUDP udp;
static uint32_t boundConn = 0;
static bool bound = false;
static uint8_t announcesLeft = 0;
static uint32_t nextAnnounceMs = 0;
static uint8_t in[512];
static uint8_t out[512];
static char txtId[32];

static miblo::MdnsInfo info() {
  miblo::MdnsInfo i{};
  // Only the id goes out to the whole LAN (the name and version are for paired computers).
  miblo::mdnsPublicIdentity(i, ctx.ident.id, ctx.ident.defaultName, txtId, sizeof(txtId));
  IPAddress ip = WiFi.localIP();
  for (int k = 0; k < 4; k++) i.ip[k] = ip[k];
  i.port = 80;
  return i;
}

static void sendMulticast(const uint8_t* data, size_t len) {
#if defined(ESP8266)
  udp.beginPacketMulticast(IPAddress(224, 0, 0, 251), miblo::kMdnsPort, WiFi.localIP());
#else
  udp.beginPacket(IPAddress(224, 0, 0, 251), miblo::kMdnsPort);
#endif
  udp.write(data, len);
  udp.endPacket();
}

#if defined(ESP8266)
// The ESP8266 core's station netif comes up WITHOUT NETIF_FLAG_IGMP. lwIP's igmp_joingroup()
// (what WiFiUDP::beginMulticast calls) then finds no eligible netif and fails, and ip4_input()
// drops every packet sent to 224.0.0.251: the responder never bound, never announced and never
// heard a query. The core's own ESP8266mDNS works around it the same way: set the flag, start
// IGMP on the netif and join the group directly on it.
static bool joinGroup() {
  ip4_addr_t group;
  IP4_ADDR(&group, 224, 0, 0, 251);
  const uint32_t self = (uint32_t)WiFi.localIP();
  bool joined = false;
  for (struct netif* n = netif_list; n; n = n->next) {
    if (!netif_is_up(n) || ip4_addr_get_u32(netif_ip4_addr(n)) != self) continue;
    if (!(n->flags & NETIF_FLAG_IGMP)) {
      n->flags |= NETIF_FLAG_IGMP;
      igmp_start(n);
    }
    if (igmp_lookfor_group(n, &group) || igmp_joingroup_netif(n, &group) == ERR_OK) joined = true;
  }
  return joined;
}
#endif

void announce() {
  announcesLeft = 2;  // RFC 6762: at least two announcements, 1 s apart
  nextAnnounceMs = millis();
}

void loop(uint32_t nowMs) {
  if (!net::connected()) {
    if (bound) {
      udp.stop();
      bound = false;
    }
    return;
  }
  if (!bound || boundConn != net::connectionId()) {
    udp.stop();
#if defined(ESP8266)
    bound = joinGroup() && udp.begin(miblo::kMdnsPort);
#else
    bound = udp.beginMulticast(IPAddress(224, 0, 0, 251), miblo::kMdnsPort);
#endif
    boundConn = net::connectionId();
    announce();
  }
  if (!bound) return;

  if (announcesLeft > 0 && (int32_t)(nowMs - nextAnnounceMs) >= 0) {
    miblo::MdnsInfo i = info();
    size_t n = miblo::mdnsAnnounce(i, out, sizeof(out));
    if (n) sendMulticast(out, n);
    announcesLeft--;
    nextAnnounceMs = nowMs + 1000;
  }

  // Drain several packets per pass: a busy LAN (Macs, TVs, printers) sends plenty of mDNS and the
  // core keeps only a short receive queue, dropping newer packets (like our query) once it's full.
  for (int k = 0; k < 8; k++) {
    int len = udp.parsePacket();
    if (len <= 0) return;
    if (len > (int)sizeof(in)) continue;  // too big for us; the next parsePacket() skips it
    udp.read(in, len);
    IPAddress from = udp.remoteIP();
    uint16_t port = udp.remotePort();
    miblo::MdnsInfo i = info();
    miblo::MdnsReply r = miblo::mdnsRespond(in, (size_t)len, port, i, out, sizeof(out));
    if (!r.len) continue;
    // A query is not someone at the gadget: the plugin also searches the network on its own (to
    // find a unit that moved, every minute while one is unreachable), which would keep pet mode
    // and the screen's sleep from ever starting. /miblo:pair wakes the screen with its code request.
    if (r.unicast) {
      udp.beginPacket(from, port);
      udp.write(out, r.len);
      udp.endPacket();
    } else {
      sendMulticast(out, r.len);
    }
  }
}

}  // namespace mdns
