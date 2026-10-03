#pragma once
#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

#include "miblo_alerts.h"
#include "miblo_i18n.h"
#include "miblo_pet.h"
#include "miblo_policy.h"

namespace miblo {

enum class Mode : uint8_t { Overview, Limits, Sessions };
const char* modeCode(Mode m);  // "overview" | "limits" | "sessions"
bool modeFromCode(const char* s, Mode& out);

struct Config {
  // Defined in miblo_config.cpp: on the ESP8266 the defaults below become a constant image that
  // sits in RAM (.rodata); out of line there is one image for the whole firmware, not one per
  // file that makes a Config.
  Config();
  Mode mode = Mode::Overview;
  uint8_t brightness = 80;  // %, 5..100
  bool alerts = true;
  uint8_t heroPermSec = 10;  // 3..60
  uint8_t heroDoneSec = 5;   // 2..60
  uint8_t flashBlinks = 2;   // 2..5 blinks when an alert comes in
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
  // Blue light filter (warmer colours), on its own schedule, separate from night dimming:
  // 0 off, 1 always, 2 between blueFrom and blueTo. Strength 1..100 % (ui::warmGains); the
  // default is the old "medium" level. Saved with its nearest old level too ("blueLevel" 1..3,
  // what firmware before the slider reads); a config with only blueLevel loads at that level's
  // strength (blueStrengthForLevel).
  uint8_t blueFilter = 0;
  uint8_t blueStrength = 63;
  uint16_t blueFrom = 21 * 60;   // local minute of the day, 0..1439
  uint16_t blueTo = 7 * 60;      // 0..1439, != blueFrom; may be earlier than blueFrom (overnight)
  uint8_t mascot = 0;            // mascot colours: 0 sphynx, 1 orange, 2 black, 3 grey (kMascotStyles)
  uint8_t pet = 0;               // which animal the mascot is (miblo::Pet: isPet(), never 13); 0 the cat.
                                 // Every pet wears the colour above on its body.
  uint32_t petColors[kPetSlots] = {};  // per part (PetSlot): kPetAuto or 0xRRGGBB + 1
  uint8_t petEyes = 0;           // eye shape (miblo::EyeShape): 0 round, 1 big and shiny, 2 sleepy
  uint16_t sleepMin = 60;        // screen off after this many idle minutes, 0..240 (0 = never: pet mode on)
  uint8_t petMin = 15;           // pet mode after this many idle minutes, 1..60 (the page keeps sleepMin later)
  char owner[64] = "";           // the owner's name, <= 20 characters (greetings); empty = unknown
  char birthday[6] = "";         // the owner's birthday, "MM-DD"; empty = unknown
  char born[11] = "";            // the gadget's own birthday, "YYYY-MM-DD" (set on the first day it is used)
  bool friends = true;           // pet mode: play with other Miblos on the network (miblo_friends.h)
  uint8_t friendsSide = 0;       // where the other Miblos stand: 0 right, 1 left, 2 above, 3 below
                                 // (our cat leaves that way to visit; a guest comes in from there)
  // ---- Daily life (focus, wellness, end of day...): everything that nags is off by default ----
  bool focusQuiet = true;        // during focus only "needs you" alerts; "finished" waits for the break
  bool insist = true;            // a long wait makes the reminder stronger (miblo_alerts.h)
  // Wellness timings, free values (the page steps them by 5). Before 1.12 breakAfterMin was only
  // 0/60/90/120 and waterMin 0/60/90: flash keeps those for older firmware (configToStored).
  uint8_t breakAfterMin = 0;     // suggest a break after this much continuous work: 0 (off) or 15..240
  uint8_t waterMin = 0;          // drink water every N minutes of the work hours: 0 (off) or 15..240
  bool eyes = false;             // 20-20-20 eye rest
  uint8_t breakLenMin = 5;       // the break the nudge suggests ("How about a 5 min break?"), 1..30
  uint8_t eyesEveryMin = 20;     // eye rest every N minutes of continuous work, 10..60
  uint8_t eyesSec = 20;          // how long the eye rest nudge stays (looking far away), 10..60 s
  bool endOfDay = false;         // the day's summary at workTo on work days
  uint16_t workFrom = 9 * 60;    // work hours (local minute of the day), workFrom < workTo
  uint16_t workTo = 18 * 60;
  uint8_t workDays = 0x3E;       // bit 0 = Sunday .. bit 6 = Saturday; Mon..Fri by default; never 0
  uint8_t fanfareMin = 5;        // a response that took this long ends with a party: 0 (off), 3, 5, 10
  bool frame = false;            // thin status-coloured frame around the screen
  char tz2[32] = "";             // second time zone, an IANA name from the table ("" = off; kTzNameMax)
  char tz2Label[37] = "";        // its name on screen, <= 12 characters ("" = the city of tz2)
  bool deskQr = false;           // settings QR on the Desk screen (always on /miblo:find)
  bool weekly = true;            // Monday: last week's summary
};

constexpr uint8_t kMascotStyles = 4;

// Capacity of every JSON document that holds the whole config: the saved file (storage.cpp), a
// settings POST from the page or the plugin, and the page's copy. Too small and the saved config
// fails to load, which resets every setting: the worst case (every string at its byte limit)
// must stay within 2/3 of it (test_stored_config_fits_on_the_gadget). A transient allocation.
// Older firmware reads this firmware's saved config into its own (2304 before 1.12): the worst
// case (~1.6 KB) must stay under that too, or a downgrade would lose every setting.
constexpr size_t kConfigJsonCapacity = 2560;  // 2304 before the wellness timings (1536 before daily life)

// Validates all present fields and only then applies them. Unknown fields are ignored.
// On error, `cfg` is left unchanged and `*badField` (if not null) points to the invalid field's name.
bool applyConfigPatch(Config& cfg, JsonObjectConst patch, const char** badField);
// includePrivate=false omits owner and birthday (the private fields), so the settings page can
// be served to anyone on the LAN without leaking them; they are fetched only after the on-screen
// code unlocks the page.
void configToJson(const Config& cfg, JsonObject out, bool includePrivate = true);
// What goes to flash: configToJson plus, in automatic language mode, the negotiated language
// ("langAuto"), so the screen keeps speaking it after a reboot (configToJson's "lang" is "" then).
// A break or water interval firmware before 1.12 rejects (anything but 0/60/90/120 and 0/60/90;
// a rejected field there resets every setting) is stored as the nearest of those, and exactly
// under "breakAfterExact" / "waterExact", which older firmware ignores.
void configToStored(const Config& cfg, JsonObject out);
// After applyConfigPatch on a stored config: restores the automatic-mode language from
// "langAuto" and the exact break and water intervals (when they still match the stored choice).
void restoreStored(Config& cfg, JsonObjectConst stored);
// The nearest interval firmware before 1.12 accepts (0 stays 0).
uint8_t legacyBreakAfterMin(uint8_t minutes);
uint8_t legacyWaterMin(uint8_t minutes);
AlertTiming alertTiming(const Config& cfg);
// "MM-DD" (a real day of the year, 02-29 included) -> month 1..12 and day 1..31.
bool parseMonthDay(const char* s, uint8_t& month, uint8_t& day);
// "YYYY-MM-DD" (year 2020..2199) -> year, month, day.
bool parseDate(const char* s, uint16_t& year, uint8_t& month, uint8_t& day);
// Enabled only when rotation is on and the device is in Overview mode.
RotationTiming rotationTiming(const Config& cfg);
// Backlight % for the local time. `minuteOfDay`: 0..1439, or -1 when the time is unknown (no
// night mode then). In the night window it is min(nightBrightness, brightness).
bool nightActive(const Config& cfg, int minuteOfDay);
uint8_t brightnessAt(const Config& cfg, int minuteOfDay);
// The blue light filter's old three strengths (blueLevel 1..3, a select before the slider) as the
// 1..100 % strengths with exactly their colours (31, 63, 100), and back: the nearest old level.
uint8_t blueStrengthForLevel(uint8_t level);
uint8_t blueLevelForStrength(uint8_t strength);
// Blue light filter strength for the local time (0 = none): blueStrength when the filter is always
// on, or scheduled and inside [blueFrom, blueTo). An unknown time (-1) is never inside a schedule.
uint8_t warmthAt(const Config& cfg, int minuteOfDay);

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
