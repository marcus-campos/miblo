#include "net.h"

#include <DNSServer.h>
#include <time.h>

#include "../context.h"
#include "miblo_tz.h"
#include "platform.h"
#include "storage.h"

namespace net {

static miblo::NetPolicy policy;
static DNSServer dns;
static bool apOn = false;
static bool autoReconnectOn = true;
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

// Station events arrive from the SDK's event callback; loop() hands them to the policy.
#if defined(ESP8266)
static WiFiEventHandler onDisc;
static WiFiEventHandler onAssoc;
#endif
static volatile uint32_t discSeq = 0;
static volatile uint8_t discReason = 0;
static volatile uint32_t assocSeq = 0;
static uint32_t seenDiscSeq = 0;
static uint32_t seenAssocSeq = 0;
static uint8_t lastReason = 0;  // survives trials: reported by /api/info

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

void applyTimezone() {
  char rule[48];
  miblo::tzResolve(ctx.cfg.tz, rule, sizeof(rule));
  configTime(rule, "pool.ntp.org", "time.google.com");
}

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
#if defined(ESP8266)
  onDisc = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected& e) {
    // ASSOC_LEAVE is our own doing (WiFi.begin()/disconnect() while associated): not a failure.
    if (e.reason == WIFI_DISCONNECT_REASON_ASSOC_LEAVE) return;
    discReason = (uint8_t)e.reason;
    discSeq = discSeq + 1;
  });
  onAssoc = WiFi.onStationModeConnected([](const WiFiEventStationModeConnected&) { assocSeq = assocSeq + 1; });
#endif
  bool hasCreds = WiFi.SSID().length() > 0;  // saved by the SDK (previous firmware or our own portal)
  strlcpy(savedSsid, WiFi.SSID().c_str(), sizeof(savedSsid));
  strlcpy(savedPass, WiFi.psk().c_str(), sizeof(savedPass));
  if (hasCreds) WiFi.begin();
  policy.begin(hasCreds, nowMs);
  lastRetryMs = nowMs;
  applyTimezone();
}

static void syncAutoReconnect(bool apHasStations) {
  const bool want = miblo::NetPolicy::autoReconnectWanted(apHasStations, trialCreds);
  if (want != autoReconnectOn) {
    WiFi.setAutoReconnect(want);
    autoReconnectOn = want;
  }
}

// Tries the submitted network without touching the SDK's saved credentials: a mistyped
// password or an out-of-range network must never evict a network that already worked.
static void beginTrial() {
  WiFi.persistent(false);
  WiFi.begin(pendingSsid, pendingPass);
  WiFi.persistent(true);
}

void loop(uint32_t nowMs) {
  const bool apHasStations = apOn && WiFi.softAPgetStationNum() > 0;

  if (pendingCreds && nowMs - pendingAtMs >= 500) {
    pendingCreds = false;
    trialCreds = true;
    syncAutoReconnect(apHasStations);  // on before the first attempt, even with the phone attached
    beginTrial();
    policy.credentialsSubmitted(nowMs);
  }

  if (discSeq != seenDiscSeq) {
    seenDiscSeq = discSeq;
    lastReason = discReason;
    policy.disconnected(lastReason, nowMs);
  }
  if (assocSeq != seenAssocSeq) {
    seenAssocSeq = assocSeq;
    policy.associated(nowMs);
  }

  const miblo::LinkStatus curLink = link();
  const miblo::NetState st = policy.update(curLink, nowMs);

  if (trialCreds && !policy.trialActive()) {
    trialCreds = false;
    if (st == miblo::NetState::Connected) {
      // It works: let the SDK persist it now. connect=false: we are already associated, and a
      // reconnect would hop channels (and drop the phone) a second time.
      strlcpy(savedSsid, pendingSsid, sizeof(savedSsid));
      strlcpy(savedPass, pendingPass, sizeof(savedPass));
      WiFi.persistent(true);
      WiFi.begin(pendingSsid, pendingPass, 0, nullptr, false);
      storage::markConfigured();  // first network joined from the portal: never codeless OTA again
    } else {
      // Wrong password, failure or timeout: drop the attempt and go back to the network that was
      // already saved, still without ever writing the failed attempt to flash. With nothing saved,
      // stop the SDK from retrying the failed network in the background.
      WiFi.persistent(false);
      if (savedSsid[0]) {
        WiFi.begin(savedSsid, savedPass);
      } else {
#if defined(ESP8266)
        WiFi.disconnect(false, true);  // persistent(false): clears the RAM config only
#else
        WiFi.disconnect();
#endif
      }
      WiFi.persistent(true);
      lastRetryMs = nowMs;
    }
  } else if (trialCreds && policy.trialRetryDue(nowMs)) {
    beginTrial();  // nothing heard from the SDK for a while: kick the attempt again
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
  // laptop is attached to the AP (unless a submitted network is being tried), resume once the
  // AP is alone again.
  syncAutoReconnect(apHasStations);

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
bool trialBusy() { return pendingCreds || trialCreds; }
miblo::JoinFailure joinFailure() { return policy.failure(); }
uint8_t joinFailureCode() { return policy.failureCode(); }
uint8_t lastDisconnectReason() { return lastReason; }
int wifiStatus() { return (int)WiFi.status(); }
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
