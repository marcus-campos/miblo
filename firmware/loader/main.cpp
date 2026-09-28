// Miblo installer ("loader", stage 1 of the two-stage install).
//
// The stock GeekMagic firmware reserves most of the flash for its image filesystem, so its
// /update page cannot fit the full Miblo image ("ERROR[4]: Not Enough Space"). This sketch is
// deliberately tiny: once it is running, the flash layout is Miblo's own (eagle.flash.4m1m.ld,
// ~1 MB sketch + ~2 MB OTA space) and the full image can be uploaded to its /update.
//
// Behaviour:
//   - screen: "Miblo installer", version, and where to upload (IP or AP name);
//   - Wi-Fi: SDK-saved credentials (never erased); after 20 s without a connection an open AP
//     "Miblo-Installer-XXXX" is started while the STA keeps retrying;
//   - HTTP: ESP8266HTTPUpdateServer at /update (no auth: the loader only lives for the install
//     window), GET /info (JSON identity for install tooling) and GET / -> /update;
//   - mDNS: miblo-installer-xxxx.local.
// It links nothing from src/ or lib/ (only TFT_eSPI with its built-in GLCD font).
#include <Arduino.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <TFT_eSPI.h>

#include "miblo_version.h"

static TFT_eSPI tft;
static ESP8266WebServer server(80);
static ESP8266HTTPUpdateServer updater;

static char id[16];        // "miblo-4f2a" (same derivation as the full firmware)
static char host[32];      // "miblo-installer-4f2a"
static char apSsid[32];    // "Miblo-Installer-4F2A"
static bool apOn = false;
static bool mdnsOn = false;
static bool wasConnected = false;
static uint32_t bootMs = 0;
static int lastPct = -1;

static const uint32_t kApAfterMs = 20000;

static void drawHeader() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("Miblo installer", 120, 50, 1);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setTextSize(1);
  tft.drawString("v" MIBLO_FW_VERSION " (" MIBLO_BUILD ")", 120, 75, 1);
}

// Clears the status area and draws up to three centred lines.
static void drawStatus(const String& a, const String& b = String(), const String& c = String()) {
  tft.fillRect(0, 100, 240, 100, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.drawString(a, 120, 115, 1);
  if (b.length()) tft.drawString(b, 120, 140, 1);
  if (c.length()) tft.drawString(c, 120, 165, 1);
}

static void showAddress() {
  if (WiFi.status() == WL_CONNECTED) {
    drawStatus("Upload Miblo firmware to", "http://" + WiFi.localIP().toString() + "/update",
               String(host) + ".local");
  } else if (apOn) {
    drawStatus("Join Wi-Fi " + String(apSsid), "http://" + WiFi.softAPIP().toString() + "/update",
               "(still retrying saved Wi-Fi)");
  } else {
    drawStatus("Connecting to saved Wi-Fi...");
  }
}

static void startMdns() {
  if (mdnsOn) {
    MDNS.notifyAPChange();
    return;
  }
  mdnsOn = MDNS.begin(host);
  if (mdnsOn) MDNS.addService("http", "tcp", 80);
}

static void handleInfo() {
  String out = F("{\"app\":\"miblo-loader\",\"fw\":\"" MIBLO_FW_VERSION "\",\"board\":\"geekmagic_ultra\",\"id\":\"");
  out += id;
  out += F("\"}");
  server.send(200, F("application/json"), out);
}

static void handleRoot() {
  server.sendHeader(F("Location"), F("/update"));
  server.send(302, F("text/plain"), F("/update"));
}

// Upload progress on screen (the Updater calls this from inside server.handleClient()).
static void onProgress(size_t done, size_t total) {
  int pct = total ? (int)(done * 100 / total) : 0;
  if (pct == lastPct) return;
  if (lastPct < 0) drawStatus("Installing Miblo...", "do not unplug");
  lastPct = pct;
  tft.drawRect(20, 180, 200, 14, TFT_WHITE);
  tft.fillRect(22, 182, 196 * pct / 100, 10, TFT_ORANGE);
}

void setup() {
  bootMs = millis();
  uint32_t chip = ESP.getChipId() & 0xFFFF;
  snprintf(id, sizeof(id), "miblo-%04x", (unsigned)chip);
  snprintf(host, sizeof(host), "miblo-installer-%04x", (unsigned)chip);
  snprintf(apSsid, sizeof(apSsid), "Miblo-Installer-%04X", (unsigned)chip);

  tft.init();
  tft.setRotation(0);
  drawHeader();
  drawStatus("Connecting to saved Wi-Fi...");

  // Mode switches are not persisted, so the saved STA credentials and settings stay untouched.
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.hostname(host);
  WiFi.setAutoReconnect(true);
  WiFi.begin();  // credentials saved in the SDK by the previous firmware

  updater.setup(&server, "/update");
  Update.onProgress(onProgress);
  server.on("/info", HTTP_GET, handleInfo);
  server.on("/", HTTP_GET, handleRoot);
  server.begin();
}

void loop() {
  server.handleClient();
  if (mdnsOn) MDNS.update();

  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected != wasConnected) {
    wasConnected = connected;
    if (connected) startMdns();
    if (lastPct < 0) showAddress();
  }
  if (!connected && !apOn && millis() - bootMs > kApAfterMs) {
    // Open AP alongside the STA: the saved network keeps being retried in the background.
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(apSsid);
    apOn = true;
    startMdns();
    if (lastPct < 0) showAddress();
  }
}
