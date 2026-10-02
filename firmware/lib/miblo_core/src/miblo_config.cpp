#include "miblo_config.h"

#include <string.h>

#include "miblo_tz.h"
#include "miblo_utf8.h"
#include "miblo_rom.h"

namespace miblo {

Config::Config() = default;

const char* modeCode(Mode m) {
  switch (m) {
    case Mode::Overview: return "overview";
    case Mode::Limits: return "limits";
    case Mode::Sessions: return "sessions";
  }
  return "overview";
}

bool modeFromCode(const char* s, Mode& out) {
  if (!s) return false;
  if (strcmp(s, "overview") == 0) out = Mode::Overview;
  else if (strcmp(s, "limits") == 0) out = Mode::Limits;
  else if (strcmp(s, "sessions") == 0) out = Mode::Sessions;
  else return false;
  return true;
}

static bool intIn(JsonVariantConst v, int lo, int hi, uint8_t& out) {
  if (!v.is<int>()) return false;
  int x = v.as<int>();
  if (x < lo || x > hi) return false;
  out = (uint8_t)x;
  return true;
}

static uint8_t daysIn(uint8_t month) {
  static const uint8_t kDays[12] MIBLO_ROM = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return month >= 1 && month <= 12 ? mibloRomByte((const char*)&kDays[month - 1]) : 0;
}

static bool digits(const char* s, int n, int& out) {
  out = 0;
  for (int i = 0; i < n; i++) {
    if (s[i] < '0' || s[i] > '9') return false;
    out = out * 10 + (s[i] - '0');
  }
  return true;
}

bool parseMonthDay(const char* s, uint8_t& month, uint8_t& day) {
  int m, d;
  if (!s || strlen(s) != 5 || s[2] != '-' || !digits(s, 2, m) || !digits(s + 3, 2, d)) return false;
  if (m < 1 || m > 12 || d < 1 || d > daysIn((uint8_t)m)) return false;
  month = (uint8_t)m;
  day = (uint8_t)d;
  return true;
}

bool parseDate(const char* s, uint16_t& year, uint8_t& month, uint8_t& day) {
  int y;
  if (!s || strlen(s) != 10 || s[4] != '-' || !digits(s, 4, y) || y < 2020 || y > 2199) return false;
  if (!parseMonthDay(s + 5, month, day)) return false;
  if (month == 2 && day == 29 && !(y % 4 == 0 && (y % 100 != 0 || y % 400 == 0))) return false;
  year = (uint16_t)y;
  return true;
}

// No control characters (typed by a person).
static bool printableUtf8(const char* s) {
  for (const char* p = s; *p; p++) {
    if ((uint8_t)*p < 0x20 || *p == 0x7F) return false;
  }
  return true;
}

// A name typed by a person: <= 20 characters, no control characters.
static bool personName(const char* s, size_t cap) {
  return s && strlen(s) < cap && utf8Length(s) <= 20 && printableUtf8(s);
}

// An integer from a fixed set of choices (the settings page's selects). Unused slots are -1. No
// table: a const array would sit in the ESP8266's RAM.
static bool oneOf(JsonVariantConst v, uint8_t& out, int a, int b, int c, int d = -1) {
  if (!v.is<int>()) return false;
  const int x = v.as<int>();
  if (x < 0 || (x != a && x != b && x != c && x != d)) return false;
  out = (uint8_t)x;
  return true;
}

// A plain bool field.
static bool boolField(JsonVariantConst v, bool& out) {
  if (!v.is<bool>()) return false;
  out = v.as<bool>();
  return true;
}

static bool intIn16(JsonVariantConst v, int lo, int hi, uint16_t& out) {
  if (!v.is<int>()) return false;
  int x = v.as<int>();
  if (x < lo || x > hi) return false;
  out = (uint16_t)x;
  return true;
}

bool applyConfigPatch(Config& cfg, JsonObjectConst patch, const char** badField) {
  Config next = cfg;
  const char* bad = nullptr;
  for (JsonPairConst kv : patch) {
    const char* k = kv.key().c_str();
    JsonVariantConst v = kv.value();
    bool ok = true;
    if (strcmp(k, "mode") == 0) {
      ok = modeFromCode(v.as<const char*>(), next.mode);
    } else if (strcmp(k, "brightness") == 0) {
      ok = intIn(v, 5, 100, next.brightness);
    } else if (strcmp(k, "alerts") == 0) {
      ok = v.is<bool>();
      if (ok) next.alerts = v.as<bool>();
    } else if (strcmp(k, "heroPermSec") == 0) {
      ok = intIn(v, 3, 60, next.heroPermSec);
    } else if (strcmp(k, "heroDoneSec") == 0) {
      ok = intIn(v, 2, 60, next.heroDoneSec);
    } else if (strcmp(k, "flashBlinks") == 0) {
      ok = intIn(v, 2, 5, next.flashBlinks);
    } else if (strcmp(k, "reminderMin") == 0) {
      ok = intIn(v, 0, 30, next.reminderMin);
    } else if (strcmp(k, "discreet") == 0) {
      ok = v.is<bool>();
      if (ok) next.discreet = v.as<bool>();
    } else if (strcmp(k, "tz") == 0) {
      const char* s = v.as<const char*>();
      // An IANA name from the table, or (configs from before the table) a POSIX rule.
      ok = s && strlen(s) < sizeof(next.tz) && (tzIsKnown(s) || tzLooksPosix(s));
      if (ok) strcpy(next.tz, s);
    } else if (strcmp(k, "name") == 0) {
      const char* s = v.as<const char*>();
      ok = personName(s, sizeof(next.name));
      if (ok) strcpy(next.name, s);
    } else if (strcmp(k, "owner") == 0) {
      const char* s = v.as<const char*>();
      ok = personName(s, sizeof(next.owner));
      if (ok) strcpy(next.owner, s);
    } else if (strcmp(k, "birthday") == 0) {
      const char* s = v.as<const char*>();
      uint8_t m, d;
      ok = s && (s[0] == 0 || parseMonthDay(s, m, d));
      if (ok) strcpy(next.birthday, s);
    } else if (strcmp(k, "born") == 0) {
      const char* s = v.as<const char*>();
      uint16_t y;
      uint8_t m, d;
      ok = s && (s[0] == 0 || parseDate(s, y, m, d));
      if (ok) strcpy(next.born, s);
    } else if (strcmp(k, "friends") == 0) {
      ok = v.is<bool>();
      if (ok) next.friends = v.as<bool>();
    } else if (strcmp(k, "friendsSide") == 0) {
      ok = intIn(v, 0, 3, next.friendsSide);
    } else if (strcmp(k, "lang") == 0) {
      const char* s = v.as<const char*>();
      if (s && s[0] == 0) {
        next.langSet = false;
      } else {
        ok = langFromCode(s, next.lang);
        if (ok) next.langSet = true;
      }
    } else if (strcmp(k, "rotate") == 0) {
      ok = v.is<bool>();
      if (ok) next.rotate = v.as<bool>();
    } else if (strcmp(k, "rotateEverySec") == 0) {
      ok = intIn16(v, 10, 3600, next.rotateEverySec);
    } else if (strcmp(k, "rotateShowSec") == 0) {
      ok = intIn16(v, 3, 300, next.rotateShowSec);
    } else if (strcmp(k, "night") == 0) {
      ok = v.is<bool>();
      if (ok) next.night = v.as<bool>();
    } else if (strcmp(k, "nightFrom") == 0) {
      ok = intIn16(v, 0, 1439, next.nightFrom);
    } else if (strcmp(k, "nightTo") == 0) {
      ok = intIn16(v, 0, 1439, next.nightTo);
    } else if (strcmp(k, "nightBrightness") == 0) {
      ok = intIn(v, 1, 100, next.nightBrightness);
    } else if (strcmp(k, "blueFilter") == 0) {
      ok = intIn(v, 0, 2, next.blueFilter);
    } else if (strcmp(k, "blueLevel") == 0) {
      ok = intIn(v, 1, 3, next.blueLevel);
    } else if (strcmp(k, "blueFrom") == 0) {
      ok = intIn16(v, 0, 1439, next.blueFrom);
    } else if (strcmp(k, "blueTo") == 0) {
      ok = intIn16(v, 0, 1439, next.blueTo);
    } else if (strcmp(k, "mascot") == 0) {
      ok = intIn(v, 0, kMascotStyles - 1, next.mascot);
    } else if (strcmp(k, "sleepMin") == 0) {
      ok = intIn16(v, 0, 240, next.sleepMin);
    } else if (strcmp(k, "petMin") == 0) {
      ok = intIn(v, 1, 60, next.petMin);
    } else if (strcmp(k, "focusQuiet") == 0) {
      ok = boolField(v, next.focusQuiet);
    } else if (strcmp(k, "insist") == 0) {
      ok = boolField(v, next.insist);
    } else if (strcmp(k, "eyes") == 0) {
      ok = boolField(v, next.eyes);
    } else if (strcmp(k, "endOfDay") == 0) {
      ok = boolField(v, next.endOfDay);
    } else if (strcmp(k, "frame") == 0) {
      ok = boolField(v, next.frame);
    } else if (strcmp(k, "deskQr") == 0) {
      ok = boolField(v, next.deskQr);
    } else if (strcmp(k, "weekly") == 0) {
      ok = boolField(v, next.weekly);
    } else if (strcmp(k, "breakAfterMin") == 0) {
      ok = oneOf(v, next.breakAfterMin, 0, 60, 90, 120);
    } else if (strcmp(k, "waterMin") == 0) {
      ok = oneOf(v, next.waterMin, 0, 60, 90);
    } else if (strcmp(k, "fanfareMin") == 0) {
      ok = oneOf(v, next.fanfareMin, 0, 3, 5, 10);
    } else if (strcmp(k, "workFrom") == 0) {
      ok = intIn16(v, 0, 1439, next.workFrom);
    } else if (strcmp(k, "workTo") == 0) {
      ok = intIn16(v, 0, 1439, next.workTo);
    } else if (strcmp(k, "workDays") == 0) {
      ok = intIn(v, 1, 127, next.workDays);
    } else if (strcmp(k, "tz2") == 0) {
      const char* s = v.as<const char*>();
      // Off ("") or an IANA name from the table (no POSIX rules: the page only offers the table).
      ok = s && strlen(s) < sizeof(next.tz2) && (s[0] == 0 || tzIsKnown(s));
      if (ok) strcpy(next.tz2, s);
    } else if (strcmp(k, "tz2Label") == 0) {
      const char* s = v.as<const char*>();
      ok = s && strlen(s) < sizeof(next.tz2Label) && utf8Length(s) <= 12 && printableUtf8(s);
      if (ok) strcpy(next.tz2Label, s);
    }
    if (!ok) {
      bad = k;
      break;
    }
  }
  // Cross-field rule, checked on the merged result: the Limits slot must be shorter than the period.
  if (!bad && next.rotateShowSec >= next.rotateEverySec) {
    bad = patch["rotateShowSec"].isNull() ? "rotateEverySec" : "rotateShowSec";
  }
  // An empty night window (start == end) is meaningless: reject whichever end the patch moved.
  if (!bad && next.nightFrom == next.nightTo) bad = patch["nightTo"].isNull() ? "nightFrom" : "nightTo";
  if (!bad && next.blueFrom == next.blueTo) bad = patch["blueTo"].isNull() ? "blueFrom" : "blueTo";
  // Work hours stay within one day (no overnight shifts): the start comes first.
  if (!bad && next.workFrom >= next.workTo) bad = patch["workTo"].isNull() ? "workFrom" : "workTo";
  if (bad) {
    if (badField) *badField = bad;
    return false;
  }
  cfg = next;
  return true;
}

void configToJson(const Config& cfg, JsonObject out, bool includePrivate) {
  out["mode"] = modeCode(cfg.mode);
  out["brightness"] = cfg.brightness;
  out["alerts"] = cfg.alerts;
  out["heroPermSec"] = cfg.heroPermSec;
  out["heroDoneSec"] = cfg.heroDoneSec;
  out["flashBlinks"] = cfg.flashBlinks;
  out["reminderMin"] = cfg.reminderMin;
  out["discreet"] = cfg.discreet;
  out["tz"] = cfg.tz;
  out["name"] = cfg.name;
  out["lang"] = cfg.langSet ? langCode(cfg.lang) : "";
  out["rotate"] = cfg.rotate;
  out["rotateEverySec"] = cfg.rotateEverySec;
  out["rotateShowSec"] = cfg.rotateShowSec;
  out["night"] = cfg.night;
  out["nightFrom"] = cfg.nightFrom;
  out["nightTo"] = cfg.nightTo;
  out["nightBrightness"] = cfg.nightBrightness;
  out["blueFilter"] = cfg.blueFilter;
  out["blueLevel"] = cfg.blueLevel;
  out["blueFrom"] = cfg.blueFrom;
  out["blueTo"] = cfg.blueTo;
  out["mascot"] = cfg.mascot;
  out["sleepMin"] = cfg.sleepMin;
  out["petMin"] = cfg.petMin;
  if (includePrivate) {
    out["owner"] = cfg.owner;
    out["birthday"] = cfg.birthday;
  }
  out["born"] = cfg.born;
  out["friends"] = cfg.friends;
  out["friendsSide"] = cfg.friendsSide;
  out["focusQuiet"] = cfg.focusQuiet;
  out["insist"] = cfg.insist;
  out["breakAfterMin"] = cfg.breakAfterMin;
  out["waterMin"] = cfg.waterMin;
  out["eyes"] = cfg.eyes;
  out["endOfDay"] = cfg.endOfDay;
  out["workFrom"] = cfg.workFrom;
  out["workTo"] = cfg.workTo;
  out["workDays"] = cfg.workDays;
  out["fanfareMin"] = cfg.fanfareMin;
  out["frame"] = cfg.frame;
  out["tz2"] = cfg.tz2;
  out["tz2Label"] = cfg.tz2Label;
  out["deskQr"] = cfg.deskQr;
  out["weekly"] = cfg.weekly;
}

void configToStored(const Config& cfg, JsonObject out) {
  configToJson(cfg, out);
  if (!cfg.langSet) out["langAuto"] = langCode(cfg.lang);
}

void restoreStoredLang(Config& cfg, JsonObjectConst stored) {
  if (cfg.langSet) return;
  Lang l;
  if (langFromCode(stored["langAuto"] | "", l)) cfg.lang = l;
}

AlertTiming alertTiming(const Config& cfg) {
  AlertTiming t;
  t.enabled = cfg.alerts;
  t.heroPermMs = (uint32_t)cfg.heroPermSec * 1000;
  t.heroDoneMs = (uint32_t)cfg.heroDoneSec * 1000;
  t.flashMs = (uint32_t)cfg.flashBlinks * kBlinkMs;
  t.reminderMs = (uint32_t)cfg.reminderMin * 60000;
  return t;
}

RotationTiming rotationTiming(const Config& cfg) {
  RotationTiming t;
  t.enabled = cfg.rotate && cfg.mode == Mode::Overview;
  t.everyMs = (uint32_t)cfg.rotateEverySec * 1000;
  t.showMs = (uint32_t)cfg.rotateShowSec * 1000;
  return t;
}

// Inside the daily window [from, to) (local minutes; it may cross midnight, 22:00 -> 07:00). An
// unknown time (-1) or an empty window is never inside.
static bool inWindow(uint16_t from, uint16_t to, int minuteOfDay) {
  if (minuteOfDay < 0 || from == to) return false;
  const int m = minuteOfDay % 1440;
  if (from < to) return m >= from && m < to;
  return m >= from || m < to;
}

bool nightActive(const Config& cfg, int minuteOfDay) {
  return cfg.night && inWindow(cfg.nightFrom, cfg.nightTo, minuteOfDay);
}

uint8_t brightnessAt(const Config& cfg, int minuteOfDay) {
  if (!nightActive(cfg, minuteOfDay)) return cfg.brightness;
  return cfg.nightBrightness < cfg.brightness ? cfg.nightBrightness : cfg.brightness;
}

uint8_t warmthAt(const Config& cfg, int minuteOfDay) {
  if (cfg.blueFilter == 1) return cfg.blueLevel;
  if (cfg.blueFilter == 2 && inWindow(cfg.blueFrom, cfg.blueTo, minuteOfDay)) return cfg.blueLevel;
  return 0;
}

BootDecision decideBoot(uint8_t storedCount, bool powerOn) {
  const uint8_t prev = storedCount < kPowerCyclesForReset ? storedCount : 0;
  // Crash/watchdog/OTA/software restart: neither counts nor breaks the sequence (a crash between
  // two quick power-ons must not lose the progress), and never triggers the reset by itself.
  if (!powerOn) return BootDecision{prev, false, 0};
  const uint8_t count = (uint8_t)(prev + 1);
  if (count >= kPowerCyclesForReset) return BootDecision{0, true, 0};
  const uint8_t remaining = count >= kPowerCycleCountdownFrom ? (uint8_t)(kPowerCyclesForReset - count) : 0;
  return BootDecision{count, false, remaining};
}

}  // namespace miblo
