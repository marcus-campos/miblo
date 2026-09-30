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
  bool night = false;            // night mode: dim the screen between nightFrom and nightTo
  uint16_t nightFrom = 22 * 60;  // local minute of the day, 0..1439
  uint16_t nightTo = 7 * 60;     // 0..1439, != nightFrom; may be earlier than nightFrom (overnight)
  uint8_t nightBrightness = 10;  // %, 1..100 (never brighter than `brightness`)
  uint8_t mascot = 0;            // mascot colours: 0 sphynx, 1 orange, 2 black, 3 grey (kMascotStyles)
  uint16_t sleepMin = 60;        // screen off after this many idle minutes, 0..240 (0 = never: pet mode on)
};

constexpr uint8_t kMascotStyles = 4;

// Validates all present fields and only then applies them. Unknown fields are ignored.
// On error, `cfg` is left unchanged and `*badField` (if not null) points to the invalid field's name.
bool applyConfigPatch(Config& cfg, JsonObjectConst patch, const char** badField);
void configToJson(const Config& cfg, JsonObject out);
// What goes to flash: configToJson plus, in automatic language mode, the negotiated language
// ("langAuto"), so the screen keeps speaking it after a reboot (configToJson's "lang" is "" then).
void configToStored(const Config& cfg, JsonObject out);
// After applyConfigPatch on a stored config: restores the automatic-mode language from "langAuto".
void restoreStoredLang(Config& cfg, JsonObjectConst stored);
AlertTiming alertTiming(const Config& cfg);
// Enabled only when rotation is on and the device is in Overview mode.
RotationTiming rotationTiming(const Config& cfg);
// Backlight % for the local time. `minuteOfDay`: 0..1439, or -1 when the time is unknown (no
// night mode then). In the night window it is min(nightBrightness, brightness).
bool nightActive(const Config& cfg, int minuteOfDay);
uint8_t brightnessAt(const Config& cfg, int minuteOfDay);

// ---- Hard reset by quick power cycles ----
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
