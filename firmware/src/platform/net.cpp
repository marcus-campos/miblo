#include "net.h"

#include <DNSServer.h>
#include <time.h>

#include "../context.h"
#include "platform.h"

namespace net {

static miblo::NetPolicy policy;
static DNSServer dns;
static bool apOn = false;
static bool wasConnected = false;
static uint32_t connId = 0;
static uint32_t lastRetryMs = 0;
static bool pendingCreds = false;
static uint32_t pendingAtMs = 0;
static char pendingSsid[33];
static char pendingPass[65];

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
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(ctx.ident.apSsid);
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());
  apOn = true;
}

static void stopAp() {
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apOn = false;
}

void applyTimezone() { configTime(ctx.cfg.tz, "pool.ntp.org", "time.google.com"); }

void begin(uint32_t nowMs) {
  uint32_t chip = chipId() & 0xFFFF;
  snprintf(ctx.ident.id, sizeof(ctx.ident.id), "miblo-%04x", (unsigned)chip);
  snprintf(ctx.ident.defaultName, sizeof(ctx.ident.defaultName), "Miblo-%04X", (unsigned)chip);
  snprintf(ctx.ident.apSsid, sizeof(ctx.ident.apSsid), "Miblo-Setup-%04X", (unsigned)chip);

  WiFi.persistent(true);
  WiFi.mode(WIFI_STA);
#if defined(ESP8266)
  WiFi.hostname(ctx.ident.id);
#else
  WiFi.setHostname(ctx.ident.id);
#endif
  WiFi.setAutoReconnect(true);
  bool hasCreds = WiFi.SSID().length() > 0;  // salvo no SDK (firmware anterior ou nosso portal)
  if (hasCreds) WiFi.begin();
  policy.begin(hasCreds, nowMs);
  lastRetryMs = nowMs;
  applyTimezone();
}

void loop(uint32_t nowMs) {
  if (pendingCreds && nowMs - pendingAtMs >= 500) {
    pendingCreds = false;
    WiFi.begin(pendingSsid, pendingPass);  // persistent(true): o SDK salva
    policy.credentialsSubmitted(nowMs);
    lastRetryMs = nowMs;
  }

  miblo::NetState st = policy.update(link(), nowMs);
  if (policy.apWanted() && !apOn) startAp();
  if (!policy.apWanted() && apOn) stopAp();
  if (apOn) dns.processNextRequest();

  bool isConnected = st == miblo::NetState::Connected;
  if (isConnected && !wasConnected) {
    connId++;
    applyTimezone();
  }
  wasConnected = isConnected;

  // Com a rede de setup no ar, continua tentando a rede salva a cada 60 s.
  if (!isConnected && apOn && WiFi.SSID().length() > 0 && nowMs - lastRetryMs >= 60000) {
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
