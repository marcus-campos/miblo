#include "miblo_config.h"

#include <string.h>

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

static bool printableAscii(const char* s) {
  for (; *s; s++) {
    if (*s < 0x21 || *s > 0x7E) return false;
  }
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
    } else if (strcmp(k, "reminderMin") == 0) {
      ok = intIn(v, 0, 30, next.reminderMin);
    } else if (strcmp(k, "discreet") == 0) {
      ok = v.is<bool>();
      if (ok) next.discreet = v.as<bool>();
    } else if (strcmp(k, "tz") == 0) {
      const char* s = v.as<const char*>();
      ok = s && s[0] && strlen(s) < sizeof(next.tz) && printableAscii(s);
      if (ok) strcpy(next.tz, s);
    } else if (strcmp(k, "name") == 0) {
      const char* s = v.as<const char*>();
      ok = s && strlen(s) < sizeof(next.name) && utf8Length(s) <= 20;
      if (ok) strcpy(next.name, s);
    } else if (strcmp(k, "lang") == 0) {
      const char* s = v.as<const char*>();
      if (s && s[0] == 0) {
        next.langSet = false;
      } else {
        ok = langFromCode(s, next.lang);
        if (ok) next.langSet = true;
      }
    }
    if (!ok) {
      bad = k;
      break;
    }
  }
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
  out["reminderMin"] = cfg.reminderMin;
  out["discreet"] = cfg.discreet;
  out["tz"] = cfg.tz;
  out["name"] = cfg.name;
  out["lang"] = cfg.langSet ? langCode(cfg.lang) : "";
}

AlertTiming alertTiming(const Config& cfg) {
  AlertTiming t;
  t.enabled = cfg.alerts;
  t.heroPermMs = (uint32_t)cfg.heroPermSec * 1000;
  t.heroDoneMs = (uint32_t)cfg.heroDoneSec * 1000;
  t.reminderMs = (uint32_t)cfg.reminderMin * 60000;
  return t;
}

BootDecision decideBoot(uint8_t storedCount, bool powerOn) {
  if (!powerOn) return BootDecision{0, false, 0};
  const uint8_t prev = storedCount < kPowerCyclesForReset ? storedCount : 0;
  const uint8_t count = (uint8_t)(prev + 1);
  if (count >= kPowerCyclesForReset) return BootDecision{0, true, 0};
  const uint8_t remaining = count >= kPowerCycleCountdownFrom ? (uint8_t)(kPowerCyclesForReset - count) : 0;
  return BootDecision{count, false, remaining};
}

}  // namespace miblo
