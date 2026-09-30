#pragma once
#include <Arduino.h>

#include "miblo_policy.h"

// Wi-Fi: uses the credentials saved in the SDK (including ones from a previous firmware); with no
// credentials, a wrong password, or after 2 min without a connection, opens the "Miblo-Setup-XXXX"
// setup network with a captive DNS — and keeps retrying the saved network.
namespace net {

void begin(uint32_t nowMs);
void loop(uint32_t nowMs);
miblo::NetState state();
bool apActive();
bool connected();
// Number that changes on every new connection (to re-announce mDNS).
uint32_t connectionId();
String ip();
// Called by the portal: connects to the new network right after the HTTP response goes out.
void submitCredentials(const char* ssid, const char* pass, uint32_t nowMs);
// A submitted network is queued or being tried (the portal shows "connecting").
bool trialBusy();
// Networks seen by the background scan that runs while the setup network is up, strongest first,
// without duplicates or hidden ones: how many (0 until the first scan ends), and the i-th name.
uint8_t scannedNetworks();
const char* scannedNetwork(uint8_t i);
// Why the last submitted network was given up on (with NetState::JoinFailed).
miblo::JoinFailure joinFailure();
uint8_t joinFailureCode();
// Diagnostics: last station disconnect reason (WIFI_DISCONNECT_REASON_*, 0 = none yet) and the
// current WiFi.status() (wl_status_t).
uint8_t lastDisconnectReason();
int wifiStatus();
// Reapplies the timezone (ctx.cfg.tz, IANA name resolved to POSIX via miblo_tz) and NTP.
void applyTimezone();

}  // namespace net
