#include "net.h"

#include <DNSServer.h>
#include <new>
#include <time.h>
#if defined(ESP8266)
#include <lwip/etharp.h>
#include <lwip/ip4_frag.h>
#include <lwip/netif.h>
#endif

#include "../context.h"
#include "miblo_link.h"
#include "miblo_livetz.h"
#include "miblo_tz.h"
#include "platform.h"
#include "storage.h"

namespace net {

static miblo::NetPolicy policy;
static miblo::LinkKeeper keeper;  // the safety net: probes, disconnect+begin cycles, a last restart
#if defined(ESP8266) && IP_REASSEMBLY
static miblo::ReassAger reassAger;  // incomplete fragmented datagrams leave the heap sooner
#endif
static DNSServer dns;
static bool apOn = false;
static bool scanStarted = false;  // a background scan for the setup page was started
static uint32_t scanAtMs = 0;
constexpr uint32_t kScanEveryMs = 30000;
constexpr uint8_t kMaxNets = 12;
constexpr uint8_t kChannels = 13;
// The setup page's list (strongest first), kept between sweeps so it is never empty mid-scan.
// Networks are scanned one channel at a time: in a dense area (hundreds of networks) a scan of
// every channel at once makes the SDK hold them all in RAM, more than the heap has.
struct Net {
  char ssid[33];
  int8_t rssi;
};
static Net nets[kMaxNets];
static uint8_t netCount = 0;
// The sweep in progress: on the heap only while one runs (the setup network is up), so the
// 400 B it needs are not held for good on a unit that is simply connected.
static Net* sweep = nullptr;
static uint8_t sweepCount = 0;
static uint8_t sweepChannel = 0;  // 0 = no sweep running; else the channel being scanned

// Adds a network to the sweep: strongest kMaxNets distinct names.
static void keepNet(const String& ssid, int8_t rssi) {
  if (!ssid.length() || ssid.length() >= sizeof(sweep[0].ssid)) return;
  for (uint8_t k = 0; k < sweepCount; k++) {
    if (strcmp(sweep[k].ssid, ssid.c_str()) != 0) continue;
    if (rssi > sweep[k].rssi) sweep[k].rssi = rssi;  // same name on another channel/AP: keep the best
    return;
  }
  uint8_t at = sweepCount;
  if (sweepCount == kMaxNets) {  // full: replace the weakest if this one is stronger
    at = 0;
    for (uint8_t k = 1; k < sweepCount; k++) if (sweep[k].rssi < sweep[at].rssi) at = k;
    if (rssi <= sweep[at].rssi) return;
  } else {
    sweepCount++;
  }
  strlcpy(sweep[at].ssid, ssid.c_str(), sizeof(sweep[at].ssid));
  sweep[at].rssi = rssi;
}

// One channel's scan finished: merge it; after the last channel, publish the sorted list.
static void keepScan(int n) {
  for (int i = 0; i < n; i++) keepNet(WiFi.SSID(i), (int8_t)WiFi.RSSI(i));
  WiFi.scanDelete();
  if (sweepChannel < kChannels) return;
  for (uint8_t i = 1; i < sweepCount; i++) {  // strongest first (insertion sort; a dozen entries)
    const Net cur = sweep[i];
    int j = i - 1;
    while (j >= 0 && sweep[j].rssi < cur.rssi) {
      sweep[j + 1] = sweep[j];
      j--;
    }
    sweep[j + 1] = cur;
  }
  memcpy(nets, sweep, sizeof(nets));
  netCount = sweepCount;
  sweepChannel = 0;
  delete[] sweep;
  sweep = nullptr;
}
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

// An AutoIP address (169.254.x.x, DHCP unanswered) is not a connection (miblo::stationLink).
static miblo::LinkStatus link() {
  const wl_status_t st = WiFi.status();
#if defined(ESP8266)
  const bool wrongPassword = st == WL_WRONG_PASSWORD;
#else
  const bool wrongPassword = false;
#endif
  const bool up = st == WL_CONNECTED;
  return miblo::stationLink(up, wrongPassword, up ? (uint32_t)WiFi.localIP() : 0);
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

// The live offset the clock runs on (seconds east), or kTableTz while it runs on the table's rule:
// syncTimezone() touches TZ only when this changes, never on every pass.
static constexpr int32_t kTableTz = INT32_MIN;
static int32_t appliedTz = kTableTz;
static uint32_t tzCheckedMs = 0;

static uint32_t epochNow() {
  const time_t now = time(nullptr);
  return now > 1600000000 ? (uint32_t)now : 0;
}

static int32_t wantedTz(uint32_t epoch) {
  int32_t east;
  return ctx.liveTz.offset(ctx.cfg.tz, epoch, east) ? east : kTableTz;
}

void applyTimezone() {
  char rule[48];
  const uint32_t epoch = epochNow();
  appliedTz = wantedTz(epoch);
  miblo::liveRule(ctx.cfg.tz, ctx.liveTz, epoch, rule, sizeof(rule));
  configTime(rule, "pool.ntp.org", "time.google.com");
}

void syncTimezone(uint32_t nowMs) {
  if (nowMs - tzCheckedMs < 1000) return;
  tzCheckedMs = nowMs;
  const uint32_t epoch = epochNow();
  if (wantedTz(epoch) == appliedTz) return;
  char rule[48];
  appliedTz = wantedTz(epoch);
  miblo::liveRule(ctx.cfg.tz, ctx.liveTz, epoch, rule, sizeof(rule));
  setenv("TZ", rule, 1);  // only the zone: NTP keeps running as configTime() set it up
  tzset();
}

void begin(uint32_t nowMs) {
  uint32_t chip = chipId() & 0xFFFF;
  snprintf_P(ctx.ident.id, sizeof(ctx.ident.id), PSTR("miblo-%04x"), (unsigned)chip);
  snprintf_P(ctx.ident.defaultName, sizeof(ctx.ident.defaultName), PSTR("Miblo-%04X"), (unsigned)chip);
  snprintf_P(ctx.ident.apSsid, sizeof(ctx.ident.apSsid), PSTR("Miblo-Setup-%04X"), (unsigned)chip);

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
  keeper.begin(nowMs);
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

// Runs the background sweep while the setup network is up: channel after channel, a new sweep
// every kScanEveryMs, paused while a submitted network is being tried.
static void scanStep(uint32_t nowMs) {
  if (!apOn) {
    if (scanStarted || scanAtMs) {
      WiFi.scanDelete();
      scanStarted = false;
      scanAtMs = 0;
      sweepChannel = 0;
    }
    delete[] sweep;
    sweep = nullptr;
    return;
  }
  const int done = WiFi.scanComplete();
  if (scanStarted && done != WIFI_SCAN_RUNNING) {  // this channel is done (or failed): merge it
    scanStarted = false;
    keepScan(done > 0 ? done : 0);
  }
  if (scanStarted || done == WIFI_SCAN_RUNNING || trialBusy()) return;
  if (!sweepChannel) {
    if (scanAtMs && nowMs - scanAtMs < kScanEveryMs) return;
    if (!sweep) sweep = new (std::nothrow) Net[kMaxNets];
    if (!sweep) return;  // no memory right now: try again on the next pass
    sweepCount = 0;  // a new sweep
    scanAtMs = nowMs ? nowMs : 1;
  }
  sweepChannel++;
  WiFi.scanNetworks(true, false, sweepChannel);
  scanStarted = true;
}

// What miblo::runLinkKeeper does, on the ESP8266.
struct Station {
  uint32_t nowMs;
#if defined(ESP8266)
  // The station's interface and the gateway, or false when there is nothing to probe.
  static bool target(netif*& n, ip4_addr_t& gw) {
    const uint32_t ip = (uint32_t)WiFi.localIP();
    ip4_addr_set_u32(&gw, (uint32_t)WiFi.gatewayIP());
    if (!ip || ip4_addr_isany_val(gw)) return false;
    for (n = netif_list; n; n = n->next) {
      if (netif_is_up(n) && ip4_addr_get_u32(netif_ip4_addr(n)) == ip) return true;
    }
    return false;
  }
  // Only a fresh reply may put the gateway in the ARP table, so a gateway already in it has to go
  // first. The core's lwIP2 cannot forget one entry (static entries are compiled out, the table and
  // its ages are private, and the glue calls ethernet_input directly, so there is no hook to watch
  // replies): the interface's table is flushed, only when the gateway is in it. Probes run only
  // when nothing is heard from the computer for a minute and no web request came in for 10 s
  // (miblo::linkQuiet), or at link-up, and with ARP_QUEUEING a packet to a forgotten neighbour
  // waits one ARP round trip instead of being lost.
  void sendProbe() {
    netif* n;
    ip4_addr_t gw;
    if (!target(n, gw)) return;
    struct eth_addr* mac;
    const ip4_addr_t* found;
    if (etharp_find_addr(n, &gw, &mac, &found) >= 0) etharp_cleanup_netif(n);
    etharp_request(n, &gw);
  }
  // Nothing to probe (no gateway): counted as answered, never as a dead link.
  bool probeAnswered() {
    netif* n;
    ip4_addr_t gw;
    if (!target(n, gw)) return true;
    struct eth_addr* mac;
    const ip4_addr_t* found;
    return etharp_find_addr(n, &gw, &mac, &found) >= 0;
  }
#else
  void sendProbe() {}
  bool probeAnswered() { return true; }
#endif
  void dropStation() {
#if defined(ESP8266)
    WiFi.disconnect(false, false);  // keeps the credentials (in RAM and flash)
#else
    WiFi.disconnect();
#endif
  }
  void joinSaved() {
    WiFi.persistent(false);
    WiFi.begin(savedSsid, savedPass);
    WiFi.persistent(true);
    lastRetryMs = nowMs;
  }
  void restart() {
    if (ctx.rebootRequested) return;
    ctx.rebootRequested = true;  // app.cpp's reboot path flushes pending saves first
    ctx.rebootAtMs = nowMs;
  }
};

void loop(uint32_t nowMs, bool heapLow) {
#if defined(ESP8266) && IP_REASSEMBLY
  // lwIP's own timeouts run in the SDK's task, which never runs during loop(): no overlap.
  if (reassAger.due(nowMs, heapLow)) ip_reass_tmr();
#endif
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
      keeper.networkChanged();    // what the old gateway did proves nothing about this one
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

  if (policy.apMayStart(heapLow) && !apOn) startAp();  // deferred while the heap is low
  if (!policy.apWanted() && apOn) stopAp();
  if (apOn) dns.processNextRequest();
  // Networks for the setup page, scanned in the background while the setup network is up. The
  // page only reads the last results: a scan inside the request (blocking for seconds, once per
  // captive-portal probe a phone sends) stalled the loop until the watchdog restarted the unit.
  scanStep(nowMs);

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

  // The safety net (miblo::LinkKeeper): the SDK's auto-reconnect and the policy above only react to
  // what WiFi.status() says. Held while a phone is on the setup network, a submitted network is
  // tried or an update runs.
  Station station{nowMs};
  const bool mayAct = savedSsid[0] && !trialBusy() && !apHasStations && !ctx.updating;
  const bool quiet = miblo::linkQuiet(nowMs, ctx.hasSnapshot, ctx.lastSnapshotMs, ctx.hasRequest, ctx.lastRequestMs);
  miblo::runLinkKeeper(keeper, station, nowMs, curLink == miblo::LinkStatus::Connected, mayAct, quiet);
}

miblo::NetState state() { return policy.state(); }
bool apActive() { return apOn; }
bool connected() { return policy.state() == miblo::NetState::Connected; }
bool hasSavedNetwork() { return savedSsid[0] != 0; }

bool trialBusy() { return pendingCreds || trialCreds; }
uint8_t scannedNetworks() { return netCount; }
const char* scannedNetwork(uint8_t i) { return i < netCount ? nets[i].ssid : ""; }
miblo::JoinFailure joinFailure() { return policy.failure(); }
uint8_t joinFailureCode() { return policy.failureCode(); }
uint8_t lastDisconnectReason() { return lastReason; }
uint32_t reconnectCycles() { return keeper.reconnects(); }
uint32_t deadLinks() { return keeper.deadLinks(); }
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
