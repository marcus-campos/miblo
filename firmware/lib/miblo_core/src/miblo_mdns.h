#pragma once
#include <stddef.h>
#include <stdint.h>

namespace miblo {

// Minimal mDNS/DNS-SD responder for `_miblo._tcp.local` (the plugin's lib/mdns.js queries it).
// Responds to service PTR (and _services._dns-sd._udp), instance SRV/TXT, and host A.
// Query on port != 5353 (legacy/"one-shot") or with the QU bit → unicast reply to the sender.
struct MdnsInfo {
  const char* instance;  // "Miblo-4F2A"  → Miblo-4F2A._miblo._tcp.local
  const char* host;      // "miblo-4f2a"  → miblo-4f2a.local
  uint8_t ip[4];
  uint16_t port;
  const char* txt[4];    // "id=miblo-4f2a", "name=Miblo-4F2A", "fw=0.1.0"
  uint8_t txtCount;
};

struct MdnsReply {
  size_t len;    // 0 = don't respond
  bool unicast;  // true = send to the sender's IP/port
};

constexpr uint16_t kMdnsPort = 5353;

MdnsReply mdnsRespond(const uint8_t* pkt, size_t len, uint16_t srcPort, const MdnsInfo& info, uint8_t* out,
                      size_t cap);

// Unsolicited announcement (PTR + SRV + TXT + A), sent by multicast on connect.
size_t mdnsAnnounce(const MdnsInfo& info, uint8_t* out, size_t cap);

}  // namespace miblo
