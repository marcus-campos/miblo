#include "storage.h"

#include <ArduinoJson.h>
#include <string.h>

#include "platform.h"

namespace storage {

static const char* kConfig = "/miblo/config.json";
static const char* kTokens = "/miblo/pairs.json";
static const char* kBoot = "/miblo/boot.cnt";
// Root markers, outside /miblo so that factory reset (which empties /miblo) never touches them.
static const char* kFsMarker = "/.miblo_fs";      // this LittleFS was initialised by Miblo
static const char* kConfigured = "/.configured";  // the unit was configured at least once
static bool configured = false;

static void touch(const char* path) {
  File f = LittleFS.open(path, "w");
  if (!f) return;
  f.write((uint8_t)'1');
  f.close();
}

bool begin() {
#if defined(ESP32)
  if (!LittleFS.begin(true)) {  // true = format if it can't mount
#else
  if (!LittleFS.begin()) {
#endif
    LittleFS.format();
    if (!LittleFS.begin()) return false;
  }
  if (!LittleFS.exists(kFsMarker)) {
    if (LittleFS.exists("/miblo")) {
      // Filesystem of an older Miblo build (before the root markers): keep it. Conservatively
      // treat a unit that already holds pairings or a saved network as configured.
      if (LittleFS.exists(kTokens) || WiFi.SSID().length() > 0) touch(kConfigured);
    } else {
      // First Miblo boot over another firmware's LittleFS: start from a clean filesystem so no
      // foreign files eat the 1 MB partition (and nothing stale is ever read as ours). Keyed on
      // the root marker, not on /miblo, so a factory reset (which empties /miblo) never formats.
      LittleFS.format();
      if (!LittleFS.begin()) return false;
    }
    touch(kFsMarker);
  }
  if (!LittleFS.exists("/miblo")) LittleFS.mkdir("/miblo");
  configured = LittleFS.exists(kConfigured);
  return true;
}

bool everConfigured() { return configured; }

void markConfigured() {
  if (configured) return;
  touch(kConfigured);
  configured = LittleFS.exists(kConfigured);
}

bool loadConfig(miblo::Config& cfg) {
  File f = LittleFS.open(kConfig, "r");
  if (!f) return false;
  DynamicJsonDocument doc(miblo::kConfigJsonCapacity);
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;
  miblo::Config loaded;
  const char* bad = nullptr;
  if (!miblo::applyConfigPatch(loaded, doc.as<JsonObjectConst>(), &bad)) {
    // A time zone saved by an older firmware that today's rules reject must not cost the user
    // every other setting: drop just that field (back to UTC; the settings page re-detects it).
    // Same for the second clock's zone (back to off).
    if (!bad) return false;
    if (strcmp(bad, "tz") == 0) doc.remove("tz");
    else if (strcmp(bad, "tz2") == 0) doc.remove("tz2");
    else return false;
    loaded = miblo::Config();
    if (!miblo::applyConfigPatch(loaded, doc.as<JsonObjectConst>(), nullptr)) return false;
  }
  miblo::restoreStoredLang(loaded, doc.as<JsonObjectConst>());
  cfg = loaded;
  return true;
}

// Stub (daily-life foundation): track D implements it.
bool loadNotes(miblo::DeskNotes& n) {
  (void)n;
  return false;
}

// Stub (daily-life foundation): track D implements it.
bool saveNotes(const miblo::DeskNotes& n) {
  (void)n;
  return false;
}

bool saveConfig(const miblo::Config& cfg) {
  DynamicJsonDocument doc(miblo::kConfigJsonCapacity);
  miblo::configToStored(cfg, doc.to<JsonObject>());
  File f = LittleFS.open(kConfig, "w");
  if (!f) return false;
  bool ok = serializeJson(doc, f) > 0;
  f.close();
  return ok;
}

bool loadTokens(miblo::TokenStore& tokens) {
  File f = LittleFS.open(kTokens, "r");
  if (!f) return false;
  DynamicJsonDocument doc(1024);
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;
  miblo::TokenEntry entries[miblo::TokenStore::kMax];
  uint8_t n = 0;
  for (JsonObjectConst e : doc["pairs"].as<JsonArrayConst>()) {
    if (n >= miblo::TokenStore::kMax) break;
    const char* token = e["token"] | "";
    const char* host = e["host"] | "";
    if (strlen(token) != 32) continue;
    strlcpy(entries[n].token, token, sizeof(entries[n].token));
    strlcpy(entries[n].host, host, sizeof(entries[n].host));
    entries[n].order = e["order"] | 0;
    n++;
  }
  tokens.restore(entries, n);
  return true;
}

bool saveTokens(const miblo::TokenStore& tokens) {
  DynamicJsonDocument doc(1024);
  JsonArray arr = doc.createNestedArray("pairs");
  for (uint8_t i = 0; i < tokens.count(); i++) {
    JsonObject e = arr.createNestedObject();
    e["token"] = tokens.at(i).token;
    e["host"] = tokens.at(i).host;
    e["order"] = tokens.at(i).order;
  }
  File f = LittleFS.open(kTokens, "w");
  if (!f) return false;
  bool ok = serializeJson(doc, f) > 0;
  f.close();
  return ok;
}

uint8_t readBootCount() {
  File f = LittleFS.open(kBoot, "r");
  if (!f) return 0;
  int v = f.read();
  f.close();
  return v < 0 ? 0 : (uint8_t)v;
}

void writeBootCount(uint8_t n) {
  File f = LittleFS.open(kBoot, "w");
  if (!f) return;
  f.write(n);
  f.close();
}

void factoryReset() {
  // Only the files under /miblo: the root markers (kFsMarker, kConfigured) must survive.
  LittleFS.remove(kConfig);
  LittleFS.remove(kTokens);
  LittleFS.remove(kBoot);
  WiFi.persistent(true);
#if defined(ESP8266)
  WiFi.disconnect(true);  // with persistent(true), erases the SSID/password saved in the SDK
  ESP.eraseConfig();
#else
  WiFi.disconnect(true, true);
#endif
  delay(200);
  ESP.restart();
  while (true) delay(100);
}

}  // namespace storage
