#include "api.h"

#include <ArduinoJson.h>
#include <time.h>

#include "board.h"
#include "context.h"
#include "miblo_utf8.h"
#include "miblo_version.h"
#include "platform/net.h"
#include "platform/storage.h"
#include "web.h"

namespace api {

static WebServerT* srv = nullptr;

static void json(int code, const char* s) { web::sendJson(*srv, code, s); }

static bool authorized() {
  char token[40];
  if (!miblo::bearerToken(srv->header(F("Authorization")).c_str(), token, sizeof(token))) return false;
  return ctx.tokens.matches(token);
}

static void handleInfo() {
  // 27 top-level members + screen{2} + caps + copied strings (flash, reset): ~560 B on the
  // ESP8266; 1024 leaves room for future caps.
  StaticJsonDocument<1024> doc;
  doc["id"] = ctx.ident.id;
  doc["name"] = deviceName();
  doc["fw"] = MIBLO_FW_VERSION;
  doc["build"] = MIBLO_BUILD;
  doc["proto"] = MIBLO_PROTO;
  doc["paired"] = ctx.tokens.count() > 0;
  doc["board"] = board::kName;
  doc["lang"] = miblo::langCode(uiLang());  // the language the screen is drawn in
  doc["langSet"] = ctx.cfg.langSet;          // false = automatic (follows the last browser)
  JsonObject screen = doc.createNestedObject("screen");
  screen["w"] = board::kScreen.w;
  screen["h"] = board::kScreen.h;
  JsonArray caps = doc.createNestedArray("caps");  // future: "buttons", "touch", "buzzer", "led"
  for (uint8_t i = 0; i < board::kCapCount; i++) caps.add(board::cap(i));
  char flash[12];
  snprintf(flash, sizeof(flash), "%06x", (unsigned)flashChipId());
  doc["flash"] = flash;
  // Diagnostics (field reports): free heap, largest allocatable block, last reset, uptime (s).
  doc["heap"] = freeHeap();
  doc["maxBlock"] = maxFreeBlock();
  doc["reset"] = resetReason();
  doc["uptime"] = millis() / 1000;
  // Wi-Fi join diagnostics: last station disconnect reason (WIFI_DISCONNECT_REASON_*, 0 = none)
  // and the current WiFi.status() (wl_status_t).
  doc["wifiReason"] = net::lastDisconnectReason();
  doc["wifiStatus"] = net::wifiStatus();
  // Overview/Limits rotation settings (read back by `/miblo:rotate`).
  doc["rotate"] = ctx.cfg.rotate;
  doc["rotateEverySec"] = ctx.cfg.rotateEverySec;
  doc["rotateShowSec"] = ctx.cfg.rotateShowSec;
  // Night mode (read back by `/miblo:night`).
  doc["night"] = ctx.cfg.night;
  doc["nightFrom"] = ctx.cfg.nightFrom;
  doc["nightTo"] = ctx.cfg.nightTo;
  doc["nightBrightness"] = ctx.cfg.nightBrightness;
  doc["mascot"] = ctx.cfg.mascot;
  doc["sleepMin"] = ctx.cfg.sleepMin;
  doc["flashBlinks"] = ctx.cfg.flashBlinks;
  doc["friends"] = ctx.cfg.friends;  // (the owner's name and birthday never leave through here)
  String out;
  serializeJson(doc, out);
  json(200, out.c_str());
}

static void handlePair() {
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, srv->arg(F("plain")))) {
    json(400, "{\"error\":\"bad json\"}");
    return;
  }
  char code[8];
  JsonVariantConst c = doc["code"];
  if (c.is<const char*>()) strlcpy(code, c.as<const char*>(), sizeof(code));
  else if (c.is<int>()) snprintf(code, sizeof(code), "%04d", c.as<int>());
  else code[0] = 0;
  uint32_t now = millis();
  switch (ctx.pairing.check(code, now)) {
    case miblo::PairingGuard::Result::Locked:
      web::sendLocked(*srv, ctx.pairing.lockRemainingMs(now));  // 429 {retryAfter}
      return;
    case miblo::PairingGuard::Result::BadCode:
      json(403, "{\"error\":\"bad code\"}");
      return;
    case miblo::PairingGuard::Result::Ok:
      break;
  }
  uint8_t rnd[16];
  for (int i = 0; i < 16; i += 4) {
    uint32_t r = hwRandom();
    memcpy(rnd + i, &r, 4);
  }
  char token[33];
  miblo::makeToken(rnd, token);
  char host[33];
  miblo::utf8Copy(host, sizeof(host), doc["host"] | "computer", 20);
  ctx.tokens.add(token, host);
  storage::saveTokens(ctx.tokens);
  storage::markConfigured();  // first pairing: never codeless OTA again (survives factory reset)
  strlcpy(ctx.pairedHost, host, sizeof(ctx.pairedHost));
  ctx.justPaired = true;
  ctx.pairedAtMs = now;
  ctx.showPairCode = false;
  char next[5];
  miblo::formatCode(hwRandom(), next);  // rotate: a code seen once on screen pairs only one computer
  ctx.pairing.setCode(next);
  String out = String(F("{\"token\":\"")) + token + F("\"}");
  json(200, out.c_str());
}

static void handleState() {
  if (!authorized()) {
    json(401, "{\"error\":\"unauthorized\"}");
    return;
  }
  const String& plain = srv->arg(F("plain"));
  if (plain.length() > miblo::kSnapshotMaxBytes) {
    json(400, "{\"error\":\"too large\"}");
    return;
  }
  // Parsed in place, in the server's own copy of the body (zero-copy JSON: it gets modified,
  // and nothing reads it afterwards): no second 3 KB buffer.
  miblo::ParseResult r = plain.length()
                             ? miblo::parseSnapshot(const_cast<String&>(plain).begin(), plain.length(), ctx.snap)
                             : miblo::ParseResult::BadJson;
  if (r != miblo::ParseResult::Ok) {
    json(400, r == miblo::ParseResult::BadVersion ? "{\"error\":\"bad version\"}" : "{\"error\":\"bad json\"}");
    return;  // the last valid screen stays up
  }
  uint32_t now = millis();
  ctx.limits.observe(ctx.snap, now);
  ctx.update.observe(ctx.snap.latest, MIBLO_FW_VERSION, now);
  ctx.hasSnapshot = true;
  ctx.lastSnapshotMs = now;
  if (ctx.snap.hasUsage) ctx.usageEverSeen = true;
  ctx.alerts.ingest(ctx.snap, now);
  ctx.runs.observe(ctx.snap);
  if (time(nullptr) < 1600000000 && ctx.snap.now > 1600000000) {
    timeval tv{(time_t)ctx.snap.now, 0};  // clock before NTP: use the computer's time
    settimeofday(&tv, nullptr);
  }
  json(200, "{\"ok\":true}");
}

static void handleConfig() {
  if (!authorized()) {
    json(401, "{\"error\":\"unauthorized\"}");
    return;
  }
  DynamicJsonDocument doc(1024);
  if (deserializeJson(doc, srv->arg(F("plain"))) || !doc.is<JsonObject>()) {
    json(400, "{\"error\":\"bad json\"}");
    return;
  }
  const char* bad = nullptr;
  if (!miblo::applyConfigPatch(ctx.cfg, doc.as<JsonObjectConst>(), &bad)) {
    String out = String(F("{\"error\":\"invalid\",\"field\":\"")) + bad + F("\"}");
    json(400, out.c_str());
    return;
  }
  ctx.configChanged = true;
  ctx.lastInteractionMs = millis();  // someone is setting it up: wake the screen (a new name says hi)
  json(200, "{\"ok\":true}");
}

static void handleReset() {
  if (!authorized()) {
    json(401, "{\"error\":\"unauthorized\"}");
    return;
  }
  json(200, "{\"ok\":true}");
  ctx.factoryResetRequested = true;
}

void begin(WebServerT& server) {
  srv = &server;
  server.on(F("/api/info"), HTTP_GET, handleInfo);
  server.on(F("/api/pair"), HTTP_POST, handlePair);
  server.on(F("/api/state"), HTTP_POST, handleState);
  server.on(F("/api/config"), HTTP_POST, handleConfig);
  server.on(F("/api/reset"), HTTP_POST, handleReset);
}

}  // namespace api
