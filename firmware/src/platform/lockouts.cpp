#include "lockouts.h"

#include <string.h>

#include "../context.h"
#include "platform.h"

namespace lockouts {

#if defined(ESP8266)
namespace {
constexpr uint32_t kMagic = 0x4D4C4B31u;  // "MLK1"
constexpr uint32_t kBlock = 112;          // RTC user memory blocks 112..127, after crashlog.cpp's 96..110
constexpr uint8_t kCount = 1 + miblo::PresenceGate::kPurposes;
struct Record {
  uint32_t magic;
  miblo::LockoutState s[kCount];
};
static_assert(sizeof(Record) <= (128 - kBlock) * 4, "RTC user memory ends at block 127");

Record last{};

void collect(Record& r, uint32_t nowMs) {
  r = Record{};
  r.magic = kMagic;
  r.s[0] = ctx.pairing.lockout().save(nowMs);
  for (uint8_t i = 0; i < miblo::PresenceGate::kPurposes; i++) {
    r.s[1 + i] = ctx.presence.lockout((miblo::PresenceGate::Purpose)i).save(nowMs);
  }
}

// Remaining times change every millisecond: only a change worth a write is one.
bool differs(const Record& a, const Record& b) {
  for (uint8_t i = 0; i < kCount; i++) {
    if (a.s[i].lockMs != b.s[i].lockMs || a.s[i].failures != b.s[i].failures) return true;
    if ((a.s[i].remainingMs == 0) != (b.s[i].remainingMs == 0)) return true;
    const uint32_t d = a.s[i].remainingMs > b.s[i].remainingMs ? a.s[i].remainingMs - b.s[i].remainingMs
                                                               : b.s[i].remainingMs - a.s[i].remainingMs;
    if (d >= 10000) return true;  // keep the stored remaining time within ~10 s
  }
  return false;
}
}  // namespace

void restore(uint32_t nowMs) {
  Record r{};
  if (!ESP.rtcUserMemoryRead(kBlock, reinterpret_cast<uint32_t*>(&r), sizeof(r)) || r.magic != kMagic) {
    collect(last, nowMs);
    return;
  }
  ctx.pairing.lockout().restore(r.s[0], nowMs);
  for (uint8_t i = 0; i < miblo::PresenceGate::kPurposes; i++) {
    ctx.presence.lockout((miblo::PresenceGate::Purpose)i).restore(r.s[1 + i], nowMs);
  }
  collect(last, nowMs);
}

void persist(uint32_t nowMs) {
  Record r;
  collect(r, nowMs);
  if (!differs(r, last)) return;
  ESP.rtcUserMemoryWrite(kBlock, reinterpret_cast<uint32_t*>(&r), sizeof(r));
  last = r;
}
#else
void restore(uint32_t) {}
void persist(uint32_t) {}
#endif

}  // namespace lockouts
