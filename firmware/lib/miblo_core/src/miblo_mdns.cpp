#include "miblo_mdns.h"

#include <ctype.h>
#include <string.h>

namespace miblo {

namespace {

constexpr uint16_t T_A = 1;
constexpr uint16_t T_PTR = 12;
constexpr uint16_t T_TXT = 16;
constexpr uint16_t T_SRV = 33;
constexpr uint16_t T_ANY = 255;
constexpr uint16_t CLASS_IN = 1;
constexpr uint16_t CACHE_FLUSH = 0x8000;
constexpr const char* kService = "_miblo._tcp.local";
constexpr const char* kEnum = "_services._dns-sd._udp.local";

class Writer {
 public:
  Writer(uint8_t* out, size_t cap) : out_(out), cap_(cap) {}
  bool ok() const { return ok_; }
  size_t size() const { return n_; }
  void u8(uint8_t v) {
    if (n_ + 1 > cap_) {
      ok_ = false;
      return;
    }
    out_[n_++] = v;
  }
  void u16(uint16_t v) {
    u8((uint8_t)(v >> 8));
    u8((uint8_t)v);
  }
  void u32(uint32_t v) {
    u16((uint16_t)(v >> 16));
    u16((uint16_t)v);
  }
  void bytes(const void* p, size_t len) {
    for (size_t i = 0; i < len; i++) u8(((const uint8_t*)p)[i]);
  }
  // Nome em labels separados por '.', sem compressão. `first` é um label único (pode ter '.').
  void name(const char* first, const char* rest) {
    if (first) label(first, strlen(first));
    const char* p = rest;
    while (*p) {
      const char* dot = strchr(p, '.');
      size_t len = dot ? (size_t)(dot - p) : strlen(p);
      label(p, len);
      p += len;
      if (*p == '.') p++;
    }
    u8(0);
  }
  size_t mark() const { return n_; }
  void patch16(size_t at, uint16_t v) {
    if (at + 2 <= n_) {
      out_[at] = (uint8_t)(v >> 8);
      out_[at + 1] = (uint8_t)v;
    }
  }

 private:
  void label(const char* s, size_t len) {
    if (len > 63) len = 63;
    u8((uint8_t)len);
    bytes(s, len);
  }
  uint8_t* out_;
  size_t cap_;
  size_t n_ = 0;
  bool ok_ = true;
};

// Lê um nome (com ponteiros de compressão) para `dst` como "a.b.c". Retorna o offset após o nome.
bool readName(const uint8_t* pkt, size_t len, size_t off, char* dst, size_t cap, size_t& next) {
  size_t used = 0;
  bool jumped = false;
  int guard = 0;
  while (true) {
    if (off >= len || ++guard > 64) return false;
    uint8_t l = pkt[off];
    if (l == 0) {
      if (!jumped) next = off + 1;
      break;
    }
    if ((l & 0xC0) == 0xC0) {
      if (off + 1 >= len) return false;
      if (!jumped) next = off + 2;
      jumped = true;
      off = ((size_t)(l & 0x3F) << 8) | pkt[off + 1];
      continue;
    }
    if (off + 1 + l > len) return false;
    if (used && used + 1 < cap) dst[used++] = '.';
    for (uint8_t i = 0; i < l && used + 1 < cap; i++) dst[used++] = (char)pkt[off + 1 + i];
    off += 1 + l;
  }
  dst[used < cap ? used : cap - 1] = 0;
  return true;
}

bool sameName(const char* a, const char* b) {
  while (*a && *b) {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    a++;
    b++;
  }
  return *a == 0 && *b == 0;
}

// "<instance>._miblo._tcp.local" e "<host>.local" para comparação.
void join(char* dst, size_t cap, const char* a, const char* b) {
  size_t la = strlen(a);
  size_t lb = strlen(b);
  if (la + 1 + lb + 1 > cap) {
    dst[0] = 0;
    return;
  }
  memcpy(dst, a, la);
  dst[la] = '.';
  memcpy(dst + la + 1, b, lb + 1);
}

void rrHeader(Writer& w, uint16_t type, uint16_t cls, uint32_t ttl) {
  w.u16(type);
  w.u16(cls);
  w.u32(ttl);
}

void writePtr(Writer& w, const MdnsInfo& info, uint32_t ttl) {
  w.name(nullptr, kService);
  rrHeader(w, T_PTR, CLASS_IN, ttl);
  size_t at = w.mark();
  w.u16(0);
  w.name(info.instance, kService);
  w.patch16(at, (uint16_t)(w.mark() - at - 2));
}

void writeSrv(Writer& w, const MdnsInfo& info, uint32_t ttl, uint16_t flush) {
  w.name(info.instance, kService);
  rrHeader(w, T_SRV, CLASS_IN | flush, ttl);
  size_t at = w.mark();
  w.u16(0);
  w.u16(0);  // prioridade
  w.u16(0);  // peso
  w.u16(info.port);
  w.name(info.host, "local");
  w.patch16(at, (uint16_t)(w.mark() - at - 2));
}

void writeTxt(Writer& w, const MdnsInfo& info, uint32_t ttl, uint16_t flush) {
  w.name(info.instance, kService);
  rrHeader(w, T_TXT, CLASS_IN | flush, ttl);
  size_t at = w.mark();
  w.u16(0);
  for (uint8_t i = 0; i < info.txtCount; i++) {
    size_t l = strlen(info.txt[i]);
    if (l > 255) l = 255;
    w.u8((uint8_t)l);
    w.bytes(info.txt[i], l);
  }
  w.patch16(at, (uint16_t)(w.mark() - at - 2));
}

void writeA(Writer& w, const MdnsInfo& info, uint32_t ttl, uint16_t flush) {
  w.name(info.host, "local");
  rrHeader(w, T_A, CLASS_IN | flush, ttl);
  w.u16(4);
  w.bytes(info.ip, 4);
}

void writeEnum(Writer& w, uint32_t ttl) {
  w.name(nullptr, kEnum);
  rrHeader(w, T_PTR, CLASS_IN, ttl);
  size_t at = w.mark();
  w.u16(0);
  w.name(nullptr, kService);
  w.patch16(at, (uint16_t)(w.mark() - at - 2));
}

enum Want : uint8_t { W_PTR = 1, W_SRV = 2, W_TXT = 4, W_A = 8, W_ENUM = 16 };

}  // namespace

MdnsReply mdnsRespond(const uint8_t* pkt, size_t len, uint16_t srcPort, const MdnsInfo& info, uint8_t* out,
                      size_t cap) {
  MdnsReply none{0, false};
  if (len < 12) return none;
  uint16_t flags = (uint16_t)((pkt[2] << 8) | pkt[3]);
  if (flags & 0x8000) return none;  // é resposta, não consulta
  uint16_t qd = (uint16_t)((pkt[4] << 8) | pkt[5]);
  if (qd == 0 || qd > 16) return none;

  char instFull[128];
  char hostFull[80];
  join(instFull, sizeof(instFull), info.instance, kService);
  join(hostFull, sizeof(hostFull), info.host, "local");

  const bool legacy = srcPort != kMdnsPort;
  bool unicast = legacy;
  uint8_t want = 0;
  size_t off = 12;
  size_t qStart = off;
  for (uint16_t i = 0; i < qd; i++) {
    char name[128];
    size_t next;
    if (!readName(pkt, len, off, name, sizeof(name), next)) return none;
    if (next + 4 > len) return none;
    uint16_t qtype = (uint16_t)((pkt[next] << 8) | pkt[next + 1]);
    uint16_t qclass = (uint16_t)((pkt[next + 2] << 8) | pkt[next + 3]);
    off = next + 4;
    uint8_t hit = 0;
    if (sameName(name, kService) && (qtype == T_PTR || qtype == T_ANY)) hit = W_PTR | W_SRV | W_TXT | W_A;
    else if (sameName(name, kEnum) && (qtype == T_PTR || qtype == T_ANY)) hit = W_ENUM;
    else if (sameName(name, instFull) && (qtype == T_SRV || qtype == T_ANY)) hit = W_SRV | W_TXT | W_A;
    else if (sameName(name, instFull) && qtype == T_TXT) hit = W_TXT;
    else if (sameName(name, hostFull) && (qtype == T_A || qtype == T_ANY)) hit = W_A;
    if (hit && (qclass & 0x8000)) unicast = true;
    want |= hit;
  }
  if (!want) return none;
  size_t qEnd = off;

  const uint32_t ttl = legacy ? 10 : 120;
  const uint16_t flush = legacy ? 0 : CACHE_FLUSH;
  Writer w(out, cap);
  w.u16(legacy ? (uint16_t)((pkt[0] << 8) | pkt[1]) : 0);  // id
  w.u16(0x8400);                                             // resposta autoritativa
  w.u16(legacy ? qd : 0);
  size_t anAt = w.mark();
  w.u16(0);
  w.u16(0);
  size_t arAt = w.mark();
  w.u16(0);
  if (legacy) w.bytes(pkt + qStart, qEnd - qStart);  // ecoa as perguntas

  uint16_t an = 0;
  uint16_t ar = 0;
  if (want & W_ENUM) {
    writeEnum(w, ttl);
    an++;
  }
  if (want & W_PTR) {
    writePtr(w, info, ttl);
    an++;
    writeSrv(w, info, ttl, flush);
    writeTxt(w, info, ttl, flush);
    writeA(w, info, ttl, flush);
    ar += 3;
  } else {
    if (want & W_SRV) {
      writeSrv(w, info, ttl, flush);
      an++;
    }
    if (want & W_TXT) {
      writeTxt(w, info, ttl, flush);
      an++;
    }
    if (want & W_A) {
      writeA(w, info, ttl, flush);
      if (want & W_SRV) ar++;
      else an++;
    }
  }
  w.patch16(anAt, an);
  w.patch16(arAt, ar);
  if (!w.ok()) return none;
  return MdnsReply{w.size(), unicast};
}

size_t mdnsAnnounce(const MdnsInfo& info, uint8_t* out, size_t cap) {
  Writer w(out, cap);
  w.u16(0);
  w.u16(0x8400);
  w.u16(0);
  w.u16(4);
  w.u16(0);
  w.u16(0);
  writePtr(w, info, 120);
  writeSrv(w, info, 120, CACHE_FLUSH);
  writeTxt(w, info, 120, CACHE_FLUSH);
  writeA(w, info, 120, CACHE_FLUSH);
  return w.ok() ? w.size() : 0;
}

}  // namespace miblo
