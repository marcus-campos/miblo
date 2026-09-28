#pragma once
#include <ArduinoJson.h>
#include <stdint.h>

#include "miblo_alerts.h"
#include "miblo_i18n.h"
#include "miblo_policy.h"

namespace miblo {

enum class Mode : uint8_t { Overview, Limits, Sessions };
const char* modeCode(Mode m);  // "overview" | "limits" | "sessions"
bool modeFromCode(const char* s, Mode& out);

struct Config {
  Mode mode = Mode::Overview;
  uint8_t brightness = 80;  // %, 5..100
  bool alerts = true;
  uint8_t heroPermSec = 10;  // 3..60
  uint8_t heroDoneSec = 5;   // 2..60
  uint8_t reminderMin = 2;   // 0..30 (0 = no reminder)
  bool discreet = false;
  char tz[48] = "UTC0";      // IANA name ("America/Sao_Paulo"); legacy: POSIX rule ("<-03>3"). See miblo_tz.h
  char name[64] = "";        // <= 20 characters; empty = default name "Miblo-XXXX"
  Lang lang = Lang::En;
  bool langSet = false;      // false = automatic language (Accept-Language)
  bool rotate = false;           // Overview mode: alternate with Limits now and then
  uint16_t rotateEverySec = 60;  // 10..3600: period between two Limits slots
  uint16_t rotateShowSec = 10;   // 3..300 and < rotateEverySec: how long Limits stays up
};

// Validates all present fields and only then applies them. Unknown fields are ignored.
// On error, `cfg` is left unchanged and `*badField` (if not null) points to the invalid field's name.
bool applyConfigPatch(Config& cfg, JsonObjectConst patch, const char** badField);
void configToJson(const Config& cfg, JsonObject out);
AlertTiming alertTiming(const Config& cfg);
// Enabled only when rotation is on and the device is in Overview mode.
RotationTiming rotationTiming(const Config& cfg);

// ---- AirTag-style hard reset by quick power cycles (spec §6) ----
// Each power-on with less than 10 s of uptime counts; the 6th in a row erases everything.
// From the 3rd quick boot on, the screen shows how many are left ("Leave it on to cancel").
constexpr uint8_t kPowerCyclesForReset = 6;
constexpr uint8_t kPowerCycleCountdownFrom = 3;
constexpr uint32_t kPowerCycleWindowMs = 10000;
struct BootDecision {
  uint8_t nextCount;  // value to persist right now
  bool factoryReset;
  uint8_t remaining;  // quick restarts still needed for the reset (0 = show nothing)
};
// storedCount: counter persisted by the previous boot (erased flash 0xFF, or any value out of
// range, counts as 0). powerOn: false for crash/watchdog/OTA/software restarts, which keep the
// stored count as-is (no increment, no reset, nothing shown). After 10 s of uptime the firmware
// persists 0.
BootDecision decideBoot(uint8_t storedCount, bool powerOn);

}  // namespace miblo
