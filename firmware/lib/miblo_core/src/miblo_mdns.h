#pragma once
#include <stddef.h>
#include <stdint.h>

namespace miblo {

// Respondedor mDNS/DNS-SD mínimo para `_miblo._tcp.local` (contrato: Plano 1, Task 7).
// Responde PTR do serviço (e de _services._dns-sd._udp), SRV/TXT da instância e A do host.
// Consulta de porta ≠ 5353 (legacy/"one-shot") ou com bit QU → resposta unicast para a origem.
struct MdnsInfo {
  const char* instance;  // "Miblo-4F2A"  → Miblo-4F2A._miblo._tcp.local
  const char* host;      // "miblo-4f2a"  → miblo-4f2a.local
  uint8_t ip[4];
  uint16_t port;
  const char* txt[4];    // "id=miblo-4f2a", "name=Miblo-4F2A", "fw=0.1.0"
  uint8_t txtCount;
};

struct MdnsReply {
  size_t len;    // 0 = não responder
  bool unicast;  // true = enviar para o IP/porta de origem
};

constexpr uint16_t kMdnsPort = 5353;

MdnsReply mdnsRespond(const uint8_t* pkt, size_t len, uint16_t srcPort, const MdnsInfo& info, uint8_t* out,
                      size_t cap);

// Anúncio não solicitado (PTR + SRV + TXT + A), enviado por multicast ao conectar.
size_t mdnsAnnounce(const MdnsInfo& info, uint8_t* out, size_t cap);

}  // namespace miblo
