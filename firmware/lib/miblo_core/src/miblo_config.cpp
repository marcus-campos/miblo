#include "miblo_config.h"

#include <string.h>

#include "miblo_tz.h"
#include "miblo_utf8.h"

namespace miblo {

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
  static const uint8_t kDays[12] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return month >= 1 && month <= 12 ? kDays[month - 1] : 0;
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

// A name typed by a person: <= 20 characters, no control characters.
static bool personName(const char* s, size_t cap) {
  if (!s || strlen(s) >= cap || utf8Length(s) > 20) return false;
  for (const char* p = s; *p; p++) {
    if ((uint8_t)*p < 0x20 || *p == 0x7F) return false;
  }
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
    } else if (strcmp(k, "mascot") == 0) {
      ok = intIn(v, 0, kMascotStyles - 1, next.mascot);
    } else if (strcmp(k, "sleepMin") == 0) {
      ok = intIn16(v, 0, 240, next.sleepMin);
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
  if (bad) {
    if (badField) *badField = bad;
    return false;
  }
  cfg = next;
  return true;
}

void configToJson(const Config& cfg, JsonObject out) {
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
  out["mascot"] = cfg.mascot;
  out["sleepMin"] = cfg.sleepMin;
  out["owner"] = cfg.owner;
  out["birthday"] = cfg.birthday;
  out["born"] = cfg.born;
  out["friends"] = cfg.friends;
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

bool nightActive(const Config& cfg, int minuteOfDay) {
  if (!cfg.night || minuteOfDay < 0 || cfg.nightFrom == cfg.nightTo) return false;
  const int m = minuteOfDay % 1440;
  if (cfg.nightFrom < cfg.nightTo) return m >= cfg.nightFrom && m < cfg.nightTo;
  return m >= cfg.nightFrom || m < cfg.nightTo;  // crosses midnight (22:00 -> 07:00)
}

uint8_t brightnessAt(const Config& cfg, int minuteOfDay) {
  if (!nightActive(cfg, minuteOfDay)) return cfg.brightness;
  return cfg.nightBrightness < cfg.brightness ? cfg.nightBrightness : cfg.brightness;
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
