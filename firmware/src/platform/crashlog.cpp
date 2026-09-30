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

void report(JsonObject info) {
  const uint32_t reason = ESP.getResetInfoPtr()->reason;
  if (reason != REASON_EXCEPTION_RST && reason != REASON_SOFT_WDT_RST && reason != REASON_WDT_RST) return;
  Record r{};
  if (!ESP.rtcUserMemoryRead(kBlock, reinterpret_cast<uint32_t*>(&r), sizeof(r)) || r.magic != kMagic) return;
  JsonObject c = info.createNestedObject("crash");
  char hex[12];
  c["reason"] = r.reason;
  c["exccause"] = r.exccause;
  snprintf(hex, sizeof(hex), "0x%08x", (unsigned)r.epc1);
  c["epc1"] = hex;  // copied (a char array)
  snprintf(hex, sizeof(hex), "0x%08x", (unsigned)r.excvaddr);
  c["excvaddr"] = hex;
  JsonArray a = c.createNestedArray("addrs");
  for (uint8_t i = 0; i < kAddrs && r.addrs[i]; i++) {
    snprintf(hex, sizeof(hex), "0x%08x", (unsigned)r.addrs[i]);
    a.add(hex);
  }
}

}  // namespace crashlog

#else
namespace crashlog {
void report(JsonObject) {}
}  // namespace crashlog
#endif
