#include "crashlog.h"

#include "platform.h"

#if defined(ESP8266)
#include <user_interface.h>

namespace {
constexpr uint32_t kMagic = 0x4D43524Cu;  // "MCRL"
constexpr uint32_t kBlock = 96;          // RTC user memory, in 4-byte blocks: past the OTA (eboot) command
constexpr uint8_t kAddrs = 10;
struct Record {
  uint32_t magic;
  uint32_t reason;
  uint32_t exccause;
  uint32_t epc1;
  uint32_t excvaddr;
  uint32_t addrs[kAddrs];
};

// Restarts by the low-memory guard: one RTC block, the free one between this record (96..110) and
// lockouts.cpp's (112..127). Magic in the top byte, the restarts in a row (the backoff's streak) in
// the next, the total since power-on in the low half.
constexpr uint32_t kHeapBlock = 111;
constexpr uint32_t kHeapMagic = 0x4D000000u;  // "M"
static_assert(kBlock + sizeof(Record) / 4 <= kHeapBlock, "crash record overlaps the restart count");

uint32_t heapWord() {
  uint32_t v = 0;
  if (!ESP.rtcUserMemoryRead(kHeapBlock, &v, sizeof(v)) || (v & 0xFF000000u) != kHeapMagic) return kHeapMagic;
  return v;
}
void writeHeapWord(uint32_t v) { ESP.rtcUserMemoryWrite(kHeapBlock, &v, sizeof(v)); }

// Code lives in IRAM (0x40100000..) or is mapped from flash (0x40200000..0x40300000).
bool isCode(uint32_t a) { return (a >= 0x40100000u && a < 0x40108000u) || (a >= 0x40201000u && a < 0x40300000u); }
}  // namespace

// Called by the core on an exception or a watchdog, just before the restart. Nothing here may
// allocate or print.
extern "C" void custom_crash_callback(struct rst_info* ri, uint32_t stack, uint32_t stackEnd) {
  Record r{};
  r.magic = kMagic;
  r.reason = ri->reason;
  r.exccause = ri->exccause;
  r.epc1 = ri->epc1;
  r.excvaddr = ri->excvaddr;
  uint8_t n = 0;
  for (uint32_t p = stack; p + 4 <= stackEnd && n < kAddrs; p += 4) {
    const uint32_t v = *reinterpret_cast<const uint32_t*>(p);
    if (isCode(v)) r.addrs[n++] = v;
  }
  ESP.rtcUserMemoryWrite(kBlock, reinterpret_cast<uint32_t*>(&r), sizeof(r));
}

namespace crashlog {

void noteHeapRestart() {
  const uint32_t v = heapWord();
  uint32_t streak = (v >> 16) & 0xFFu, total = v & 0xFFFFu;
  if (streak < 0xFFu) streak++;
  if (total < 0xFFFFu) total++;
  writeHeapWord(kHeapMagic | (streak << 16) | total);
}

uint8_t heapRestartStreak() { return (uint8_t)(heapWord() >> 16); }

void endHeapRestartStreak() { writeHeapWord(heapWord() & ~0x00FF0000u); }

void report(JsonObject info) {
  if (const uint32_t n = heapWord() & 0xFFFFu) info["heapRestarts"] = n;
  const uint32_t reason = ESP.getResetInfoPtr()->reason;
  if (reason != REASON_EXCEPTION_RST && reason != REASON_SOFT_WDT_RST && reason != REASON_WDT_RST) return;
  Record r{};
  if (!ESP.rtcUserMemoryRead(kBlock, reinterpret_cast<uint32_t*>(&r), sizeof(r)) || r.magic != kMagic) return;
  JsonObject c = info.createNestedObject("crash");
  char hex[12];
  c["reason"] = r.reason;
  c["exccause"] = r.exccause;
  snprintf_P(hex, sizeof(hex), PSTR("0x%08x"), (unsigned)r.epc1);
  c["epc1"] = hex;  // copied (a char array)
  snprintf_P(hex, sizeof(hex), PSTR("0x%08x"), (unsigned)r.excvaddr);
  c["excvaddr"] = hex;
  JsonArray a = c.createNestedArray("addrs");
  for (uint8_t i = 0; i < kAddrs && r.addrs[i]; i++) {
    snprintf_P(hex, sizeof(hex), PSTR("0x%08x"), (unsigned)r.addrs[i]);
    a.add(hex);
  }
}

}  // namespace crashlog

#else
namespace crashlog {
void report(JsonObject) {}
void noteHeapRestart() {}
uint8_t heapRestartStreak() { return 0; }
void endHeapRestartStreak() {}
}  // namespace crashlog
#endif
