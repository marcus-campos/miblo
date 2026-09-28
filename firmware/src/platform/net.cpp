#include "net.h"

#include <DNSServer.h>
#include <time.h>

#include "../context.h"
#include "platform.h"

namespace net {

static miblo::NetPolicy policy;
static DNSServer dns;
static bool apOn = false;
static bool apHadStations = false;
static bool wasConnected = false;
static uint32_t connId = 0;
static uint32_t lastRetryMs = 0;
static bool pendingCreds = false;
static uint32_t pendingAtMs = 0;
static char pendingSsid[33];
static char pendingPass[65];
// Credentials already known to work (mirrors what the SDK has saved to flash). Restored after a
// failed portal submission so a wrong password never evicts a network that already worked.
static char savedSsid[33] = "";
static char savedPass[65] = "";
static bool trialCreds = false;
static uint32_t trialStartMs = 0;

static miblo::LinkStatus link() {
  switch (WiFi.status()) {
    case WL_CONNECTED: return miblo::LinkStatus::Connected;
#if defined(ESP8266)
    case WL_WRONG_PASSWORD: return miblo::LinkStatus::WrongPassword;
#endif
    default: return miblo::LinkStatus::Down;
  }
}

static void startAp() {
  WiFi.persistent(false);  // the AP flips on/off often; never write that to flash
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(ctx.ident.apSsid);
  WiFi.persistent(true);
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());
  apOn = true;
}

static void stopAp() {
  dns.stop();
  WiFi.persistent(false);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.persistent(true);
  apOn = false;
}

void applyTimezone() { configTime(ctx.cfg.tz, "pool.ntp.org", "time.google.com"); }

void begin(uint32_t nowMs) {
  uint32_t chip = chipId() & 0xFFFF;
  snprintf(ctx.ident.id, sizeof(ctx.ident.id), "miblo-%04x", (unsigned)chip);
  snprintf(ctx.ident.defaultName, sizeof(ctx.ident.defaultName), "Miblo-%04X", (unsigned)chip);
  snprintf(ctx.ident.apSsid, sizeof(ctx.ident.apSsid), "Miblo-Setup-%04X", (unsigned)chip);

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.persistent(true);
#if defined(ESP8266)
  WiFi.hostname(ctx.ident.id);
#else
  WiFi.setHostname(ctx.ident.id);
#endif
  WiFi.setAutoReconnect(true);
  bool hasCreds = WiFi.SSID().length() > 0;  // saved by the SDK (previous firmware or our own portal)
  strlcpy(savedSsid, WiFi.SSID().c_str(), sizeof(savedSsid));
  strlcpy(savedPass, WiFi.psk().c_str(), sizeof(savedPass));
  if (hasCreds) WiFi.begin();
  policy.begin(hasCreds, nowMs);
  lastRetryMs = nowMs;
  applyTimezone();
}

void loop(uint32_t nowMs) {
  if (pendingCreds && nowMs - pendingAtMs >= 500) {
    pendingCreds = false;
    // Try the submitted network without touching the SDK's saved credentials yet: a mistyped
    // password or an out-of-range network must never evict a network that already worked.
    WiFi.persistent(false);
    WiFi.begin(pendingSsid, pendingPass);
    trialCreds = true;
    trialStartMs = nowMs;
    policy.credentialsSubmitted(nowMs);
    lastRetryMs = nowMs;
  }

  miblo::LinkStatus curLink = link();
  miblo::NetState st = policy.update(curLink, nowMs);

  if (trialCreds) {
    if (curLink == miblo::LinkStatus::Connected) {
      // It works: let the SDK persist it now.
      trialCreds = false;
      strlcpy(savedSsid, pendingSsid, sizeof(savedSsid));
      strlcpy(savedPass, pendingPass, sizeof(savedPass));
      WiFi.persistent(true);
      WiFi.begin(pendingSsid, pendingPass);
    } else if (curLink == miblo::LinkStatus::WrongPassword ||
               nowMs - trialStartMs >= miblo::NetPolicy::kFallbackMs) {
      // Wrong password or timed out: drop the attempt and go back to the network that was
      // already saved, still without ever writing the failed attempt to flash.
      trialCreds = false;
      WiFi.persistent(false);
      WiFi.begin(savedSsid, savedPass);
      lastRetryMs = nowMs;
    }
  }

  if (policy.apWanted() && !apOn) startAp();
  if (!policy.apWanted() && apOn) stopAp();
  if (apOn) dns.processNextRequest();

  bool isConnected = st == miblo::NetState::Connected;
  if (isConnected && !wasConnected) {
    connId++;
    applyTimezone();
  }
  wasConnected = isConnected;

  // Auto-reconnect competes with the setup portal for airtime: pause it while a phone or
  // laptop is attached to the AP, resume once the AP is alone again.
  bool apHasStations = apOn && WiFi.softAPgetStationNum() > 0;
  if (apHasStations != apHadStations) {
    WiFi.setAutoReconnect(!apHasStations);
    apHadStations = apHasStations;
  }

  // While the setup network is up, keep retrying the saved network every 60 s — but not while
  // someone is on the AP, and not in the middle of testing a freshly submitted network.
  if (!isConnected && apOn && !apHasStations && !trialCreds && savedSsid[0] && nowMs - lastRetryMs >= 60000) {
    lastRetryMs = nowMs;
    WiFi.begin();
  }
}

miblo::NetState state() { return policy.state(); }
bool apActive() { return apOn; }
bool connected() { return policy.state() == miblo::NetState::Connected; }
uint32_t connectionId() { return connId; }

String ip() {
  if (connected()) return WiFi.localIP().toString();
  if (apOn) return WiFi.softAPIP().toString();
  return String("0.0.0.0");
}

void submitCredentials(const char* ssid, const char* pass, uint32_t nowMs) {
  strlcpy(pendingSsid, ssid, sizeof(pendingSsid));
  strlcpy(pendingPass, pass, sizeof(pendingPass));
  pendingCreds = true;
  pendingAtMs = nowMs;
}

}  // namespace net
