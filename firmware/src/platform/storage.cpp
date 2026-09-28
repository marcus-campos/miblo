#include "storage.h"

#include <ArduinoJson.h>

#include "platform.h"

namespace storage {

static const char* kConfig = "/miblo/config.json";
static const char* kTokens = "/miblo/pairs.json";
static const char* kBoot = "/miblo/boot.cnt";

bool begin() {
#if defined(ESP32)
  if (!LittleFS.begin(true)) {  // true = formata se não montar
#else
  if (!LittleFS.begin()) {
#endif
    LittleFS.format();
    if (!LittleFS.begin()) return false;
  }
  if (!LittleFS.exists("/miblo")) {
    // First Miblo boot over another firmware's LittleFS: start from a clean filesystem so no
    // foreign files eat the 1 MB partition (and nothing stale is ever read as ours).
    LittleFS.format();
    if (!LittleFS.begin()) return false;
    LittleFS.mkdir("/miblo");
  }
  return true;
}

bool loadConfig(miblo::Config& cfg) {
  File f = LittleFS.open(kConfig, "r");
  if (!f) return false;
  DynamicJsonDocument doc(1024);
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;
  miblo::Config loaded;
  if (!miblo::applyConfigPatch(loaded, doc.as<JsonObjectConst>(), nullptr)) return false;
  cfg = loaded;
  return true;
}

bool saveConfig(const miblo::Config& cfg) {
  DynamicJsonDocument doc(1024);
  miblo::configToJson(cfg, doc.to<JsonObject>());
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
  LittleFS.remove(kConfig);
  LittleFS.remove(kTokens);
  LittleFS.remove(kBoot);
  WiFi.persistent(true);
#if defined(ESP8266)
  WiFi.disconnect(true);  // com persistent(true), apaga SSID/senha salvos no SDK
  ESP.eraseConfig();
#else
  WiFi.disconnect(true, true);
#endif
  delay(200);
  ESP.restart();
  while (true) delay(100);
}

}  // namespace storage
