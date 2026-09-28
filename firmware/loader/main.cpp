// Miblo installer ("loader", stage 1 of the two-stage install).
//
// The stock GeekMagic firmware reserves most of the flash for its image filesystem, so its
// /update page cannot fit the full Miblo image ("ERROR[4]: Not Enough Space"). This sketch is
// deliberately tiny: once it is running, the flash layout is Miblo's own (eagle.flash.4m1m.ld,
// ~1 MB sketch + ~2 MB OTA space) and the full image can be uploaded to its /update.
//
// Behaviour:
//   - screen: "Miblo installer", version, and where to upload (IP or AP name);
//   - Wi-Fi: SDK-saved credentials; after 20 s without a connection an open AP
//     "Miblo-Installer-XXXX" is started. From then on the station stops its automatic retries
//     (their channel scans disrupt clients of the AP): the saved network is retried every 3 min,
//     and only while nobody is connected to the AP;
//   - HTTP: a minimal update server at /update (no auth: the loader only lives for the install
//     window; same replies as ESP8266HTTPUpdateServer: "Update error: ..." on failure), GET /info
//     (JSON identity for install tooling) and GET / -> /update;
//   - after a SUCCESSFUL install (Update.end(true)) and before the restart, the station
//     credentials saved in the SDK (the bench/shop Wi-Fi the loader was installed over) are
//     erased, so the unit boots Miblo straight into its Miblo-Setup-XXXX portal. POST
//     /update?keepwifi=1 keeps them. A failed or aborted upload never erases anything;
//   - mDNS: miblo-installer-xxxx.local.
// It links nothing from src/ or lib/ (only TFT_eSPI with its built-in GLCD font).
#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <TFT_eSPI.h>

#include "miblo_version.h"

static TFT_eSPI tft;
static ESP8266WebServer server(80);

// Per-request upload state (reset at UPLOAD_FILE_START).
static bool upStarted = false;  // Update.begin() succeeded for this request
static bool upOk = false;       // Update.end(true) succeeded: image written and verified
static String upError;          // "ERROR[n]: message" (same text as Update.printError)

static char id[16];        // "miblo-4f2a" (same derivation as the full firmware)
static char host[32];      // "miblo-installer-4f2a"
static char apSsid[32];    // "Miblo-Installer-4F2A"
static bool apOn = false;
static bool mdnsOn = false;
static bool wasConnected = false;
static bool retrying = false;  // a station attempt started by the AP-mode retry timer
static uint32_t bootMs = 0;
static uint32_t lastRetryMs = 0;

static const uint32_t kApAfterMs = 20000;
static const uint32_t kRetryEveryMs = 180000;  // AP on: retry the saved network this often...
static const uint32_t kRetryWindowMs = 15000;  // ...for this long, then stop scanning again

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
               "(saved Wi-Fi retried every 3 min)");
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

static void setUpdateError() {
  char buf[16];
  snprintf(buf, sizeof(buf), "ERROR[%u]: ", (unsigned)Update.getError());
  upError = buf;
  upError += Update.getErrorString();
  if (!upError.length()) upError = F("update failed");
}

static void handleUpdatePage() {
  server.send_P(200, PSTR("text/html"),
                PSTR("<!DOCTYPE html><html lang='en'><head><meta charset='utf-8'>"
                     "<meta name='viewport' content='width=device-width,initial-scale=1'></head><body>"
                     "<h3>Miblo installer v" MIBLO_FW_VERSION "</h3>"
                     "<form method='POST' action='/update' enctype='multipart/form-data'>"
                     "Firmware:<br><input type='file' accept='.bin' name='firmware'> "
                     "<input type='submit' value='Update Firmware'></form></body></html>"));
}

// Multipart upload body: streams the image into the OTA area through Update.
static void handleUpdateUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    upStarted = false;
    upOk = false;
    upError = String();
    drawStatus("Installing Miblo...", "do not unplug");
    uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    upStarted = Update.begin(maxSketchSpace, U_FLASH);
    if (!upStarted) setUpdateError();
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!upStarted || upError.length()) return;
    if (Update.write(up.buf, up.currentSize) != up.currentSize) setUpdateError();
  } else if (up.status == UPLOAD_FILE_END) {
    if (!upStarted || upError.length()) return;
    upOk = Update.end(true);  // true: the image size is what was received
    if (!upOk) setUpdateError();
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (upStarted && !upOk) Update.end(false);
    upStarted = false;
    upError = F("upload aborted");
  }
  yield();
}

// Erases the SDK station credentials (SSID + password) from flash. In core 3.1.2,
// WiFi.disconnect(true) == disconnect(wifioff=true, eraseCredentials=true): it zeroes the SSID and
// password in the station config and, with persistent(true), stores that through
// wifi_station_set_config (the flash copy the next firmware reads); then it turns the station off.
// That is exactly what Miblo checks (WiFi.SSID() empty -> setup portal), so ESP.eraseConfig()
// (which also wipes RF calibration and the whole SDK area) is not needed.
static void eraseStationCredentials() {
  WiFi.persistent(true);
  WiFi.disconnect(true);
  WiFi.persistent(false);
}

// End of the POST /update request (after the whole body went through handleUpdateUpload).
static void handleUpdateDone() {
  if (!upOk) {
    if (Update.isRunning()) Update.end(false);  // drop a half-written image; the loader stays
    const String err = upError.length() ? upError : String(F("no firmware file"));
    server.send(200, F("text/html"), String(F("Update error: ")) + err);
    upStarted = false;
    showAddress();
    return;
  }
  const bool keepWifi = server.arg(F("keepwifi")) == F("1");
  server.client().setNoDelay(true);
  server.send(200, F("text/plain"), F("OK"));
  delay(100);
  server.client().stop();
  drawStatus("Installed. Restarting...", keepWifi ? "(Wi-Fi kept)" : "(Wi-Fi erased: setup mode)");
  if (!keepWifi) eraseStationCredentials();  // only after a verified image, never on failure
  delay(200);
  ESP.restart();
}

static void handleRoot() {
  server.sendHeader(F("Location"), F("/update"));
  server.send(302, F("text/plain"), F("/update"));
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

  server.on(F("/update"), HTTP_GET, handleUpdatePage);
  server.on(F("/update"), HTTP_POST, handleUpdateDone, handleUpdateUpload);
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
    retrying = false;
    lastRetryMs = millis();
    showAddress();
  }
  const uint32_t now = millis();
  if (!connected && !apOn && now - bootMs > kApAfterMs) {
    // Open AP alongside the STA. Stop the SDK's automatic reconnects: every attempt scans all
    // channels and knocks the phone/laptop off the AP mid-upload. Credentials are kept.
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false, false);
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(apSsid);
    apOn = true;
    lastRetryMs = now;
    startMdns();
    showAddress();
  }
  if (apOn && !connected) {
    if (retrying && now - lastRetryMs > kRetryWindowMs) {
      WiFi.disconnect(false, false);  // give up this attempt; keep the credentials
      retrying = false;
    } else if (!retrying && now - lastRetryMs >= kRetryEveryMs && WiFi.softAPgetStationNum() == 0) {
      WiFi.begin();  // one attempt at the saved network, only while nobody uses the AP
      retrying = true;
      lastRetryMs = now;
    }
  }
}
