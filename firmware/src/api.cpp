#include "api.h"

#include <ArduinoJson.h>
#include <time.h>

#include "app.h"
#include "board.h"
#include "platform/platform.h"
#include "context.h"
#include "miblo_desknotes.h"
#include "miblo_focus.h"
#include "miblo_info.h"
#include "miblo_meeting.h"
#include "miblo_snapshot.h"
#include "miblo_utf8.h"
#include "miblo_version.h"
#include "platform/crashlog.h"
#include "platform/net.h"
#include "platform/routes.h"
#include "platform/storage.h"
#include "web.h"

namespace api {

static WebServerT* srv = nullptr;

static void json(int code, const char* s) { web::sendJson(*srv, code, s); }
static void json(int code, const __FlashStringHelper* s) { web::sendJson(*srv, code, s); }

// The paired computer behind this request's bearer token: its place in ctx.tokens, -1 if none.
static int caller() {
  char token[40];
  const String auth = web::requestHeader(*srv, F("Authorization"));  // never the previous request's
  if (!miblo::bearerToken(auth.c_str(), token, sizeof(token))) return -1;
  const int i = ctx.tokens.find(token);
  if (i >= 0) ctx.tokens.seen((uint8_t)i, millis());  // the settings page's "active now"
  return i;
}

static bool authorized() { return caller() >= 0; }

// /api/info "focus.phase".
static const __FlashStringHelper* focusPhaseName(miblo::FocusPhase p) {
  switch (p) {
    case miblo::FocusPhase::Focus: return F("focus");
    case miblo::FocusPhase::Break: return F("break");
    case miblo::FocusPhase::Back: return F("back");
    case miblo::FocusPhase::LongBreak: return F("long");
    case miblo::FocusPhase::Off: break;
  }
  return F("off");
}

static void handleInfo() {
  // A paired gadget tells someone without its token only who it is (miblo::infoView): its name,
  // version, settings and diagnostics are for the computers paired with it.
  const String auth = web::requestHeader(*srv, F("Authorization"));  // never the previous request's
  // Flood guard for everything not authenticated with a valid token (an unpaired gadget answers
  // in full too): a paired computer's own reads neither consume nor wait on this bucket.
  if (!authorized() && !ctx.publicReqs.allow(millis())) {
    json(429, F("{\"error\":\"slow down\"}"));
    return;
  }
  if (miblo::infoView(ctx.tokens, auth.c_str()) == miblo::InfoView::Public) {
    StaticJsonDocument<96> pub;
    miblo::writePublicInfo(pub.to<JsonObject>(), ctx.ident.id, true, MIBLO_PROTO);
    String out;
    serializeJson(pub, out);
    json(200, out.c_str());
    return;
  }
  // 42 top-level members + screen{2} + focus{4} + caps + copied strings (flash, reset, the
  // phase, the countdown date): ~830 B on the ESP8266, plus ~440 B for the keys, which are copied
  // in from flash (F()) so they never sit in RAM for good, plus "crash" (~300 B) after a crash:
  // ~1.57 KB at worst. On the heap, not the stack: on the stack it took the HTTP path to ~4 KB,
  // the whole of the 4 KB loop() stack. The reply (~1.2 KB) is built on the heap after it.
  constexpr size_t kInfoDoc = 1792;
  if (heapLowForRequest(kInfoDoc + 1280)) {
    json(503, F("{\"error\":\"busy\"}"));
    return;
  }
  DynamicJsonDocument doc(kInfoDoc);
  if (!doc.capacity()) {  // the allocation failed anyway
    json(503, F("{\"error\":\"busy\"}"));
    return;
  }
  doc[F("id")] = ctx.ident.id;
  doc[F("name")] = deviceName();
  doc[F("fw")] = MIBLO_FW_VERSION;
  doc[F("build")] = MIBLO_BUILD;
  doc[F("proto")] = MIBLO_PROTO;
  doc[F("paired")] = ctx.tokens.count() > 0;
  doc[F("board")] = board::kName;
  doc[F("lang")] = miblo::langCode(uiLang());  // the language the screen is drawn in
  doc[F("langSet")] = ctx.cfg.langSet;          // false = automatic (follows the last browser)
  JsonObject screen = doc.createNestedObject(F("screen"));
  screen[F("w")] = board::kScreen.w;
  screen[F("h")] = board::kScreen.h;
  JsonArray caps = doc.createNestedArray(F("caps"));  // future: "buttons", "touch", "buzzer", "led"
  for (uint8_t i = 0; i < board::kCapCount; i++) caps.add(board::cap(i));
  char flash[12];
  snprintf_P(flash, sizeof(flash), PSTR("%06x"), (unsigned)flashChipId());
  doc[F("flash")] = flash;
  // Diagnostics (field reports): free heap, largest allocatable block, last reset, uptime (s).
  doc[F("heap")] = freeHeap();
  doc[F("maxBlock")] = maxFreeBlock();
  doc[F("reset")] = resetReason();
  doc[F("uptime")] = millis() / 1000;
  doc[F("minHeapParse")] = app::minHeapDuringParse();  // worst-case free heap during a snapshot parse
  doc[F("maxSessions")] = miblo::kMaxSessions;  // how many sessions this firmware can show/parse
  doc[F("maxBytes")] = miblo::kSnapshotMaxBytes;
  // Wi-Fi join diagnostics: last station disconnect reason (WIFI_DISCONNECT_REASON_*, 0 = none)
  // and the current WiFi.status() (wl_status_t).
  doc[F("wifiReason")] = net::lastDisconnectReason();
  doc[F("wifiStatus")] = net::wifiStatus();
  // Overview/Limits rotation settings (read back by `/miblo:rotate`).
  doc[F("rotate")] = ctx.cfg.rotate;
  doc[F("rotateEverySec")] = ctx.cfg.rotateEverySec;
  doc[F("rotateShowSec")] = ctx.cfg.rotateShowSec;
  // Night mode (read back by `/miblo:night`).
  doc[F("night")] = ctx.cfg.night;
  doc[F("nightFrom")] = ctx.cfg.nightFrom;
  doc[F("nightTo")] = ctx.cfg.nightTo;
  doc[F("nightBrightness")] = ctx.cfg.nightBrightness;
  // Blue light filter (0 off, 1 always, 2 scheduled; strength 1..3; its own window).
  doc[F("blueFilter")] = ctx.cfg.blueFilter;
  doc[F("blueLevel")] = ctx.cfg.blueLevel;
  doc[F("blueFrom")] = ctx.cfg.blueFrom;
  doc[F("blueTo")] = ctx.cfg.blueTo;
  doc[F("mascot")] = ctx.cfg.mascot;
  doc[F("sleepMin")] = ctx.cfg.sleepMin;
  doc[F("petMin")] = ctx.cfg.petMin;
  doc[F("flashBlinks")] = ctx.cfg.flashBlinks;
  doc[F("friends")] = ctx.cfg.friends;  // (the owner's name and birthday never leave through here)
  // Daily life (read back by /miblo:focus, meeting, timer, countdown; "daily" = these routes exist).
  const uint32_t nowMs = millis();
  JsonObject focus = doc.createNestedObject(F("focus"));
  focus[F("phase")] = focusPhaseName(ctx.focus.phase());
  focus[F("round")] = ctx.focus.round();
  focus[F("rounds")] = ctx.focus.plan().rounds;
  focus[F("left")] = ctx.focus.leftMs(nowMs) / 1000;  // seconds left in the phase
  doc[F("meetingLeft")] = ctx.meeting.on() ? ctx.meeting.leftMs(nowMs) / 1000 : 0;
  doc[F("timerLeft")] = ctx.notes.timerRunning() ? ctx.notes.timerLeftMs(nowMs) / 1000 : 0;
  const miblo::Countdown& cd = ctx.notes.countdown();
  doc[F("countdown")] = (const char*)cd.label;  // "" = none (stable memory: not copied)
  char date[11] = "";
  if (cd.label[0]) {
    snprintf_P(date, sizeof(date), PSTR("%04u-%02u-%02u"), (unsigned)cd.date.year, (unsigned)cd.date.month,
               (unsigned)cd.date.day);
  }
  doc[F("countdownDate")] = date;  // char[]: copied
  doc[F("daily")] = 1;
  crashlog::report(doc.as<JsonObject>());  // after a crash: where it happened
  // ~1.57 KB in the worst case (keys copied, a crash record): a field that didn't fit would be
  // dropped silently, so a document that overflowed is an error, never a partial answer.
  if (doc.overflowed()) {
    json(500, "{\"error\":\"info too large\"}");
    return;
  }
  String out;
  serializeJson(doc, out);
  json(200, out.c_str());
}

static void handlePair() {
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, srv->arg(F("plain")))) {
    json(400, F("{\"error\":\"bad json\"}"));
    return;
  }
  char code[8];
  JsonVariantConst c = doc["code"];
  if (c.is<const char*>()) strlcpy(code, c.as<const char*>(), sizeof(code));
  else if (c.is<int>()) snprintf_P(code, sizeof(code), PSTR("%04d"), c.as<int>());
  else code[0] = 0;
  uint32_t now = millis();
  switch (ctx.pairing.check(code, now)) {
    case miblo::PairingGuard::Result::Locked:
      web::sendLocked(*srv, ctx.pairing.lockRemainingMs(now));  // 429 {retryAfter}
      return;
    case miblo::PairingGuard::Result::BadCode:
      json(403, F("{\"error\":\"bad code\"}"));
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
  const int from = caller();
  if (from < 0) {
    json(401, F("{\"error\":\"unauthorized\"}"));
    return;
  }
  const String& plain = srv->arg(F("plain"));
  if (plain.length() > miblo::kSnapshotMaxBytes) {
    json(400, F("{\"error\":\"too large\"}"));
    return;
  }
  if (heapLowForRequest(plain.length())) {  // low heap: refuse (the plugin resends an alerts-only snapshot)
    json(503, F("{\"error\":\"busy\"}"));
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
  // The settings page's list names each computer by its host name, unless the user named it
  // (stored at most once a minute by the app loop: TokenStore::saveDue).
  ctx.tokens.autoLabel((uint8_t)from, ctx.snap.host);
  if (time(nullptr) < 1600000000 && ctx.snap.now > 1600000000) {
    timeval tv{(time_t)ctx.snap.now, 0};  // clock before NTP: use the computer's time
    settimeofday(&tv, nullptr);
  }
  json(200, F("{\"ok\":true}"));
}

static void handleConfig() {
  if (!authorized()) {
    json(401, F("{\"error\":\"unauthorized\"}"));
    return;
  }
  if (heapLowForRequest(miblo::kConfigJsonCapacity)) {
    json(503, F("{\"error\":\"busy\"}"));
    return;
  }
  DynamicJsonDocument doc(miblo::kConfigJsonCapacity);
  if (deserializeJson(doc, srv->arg(F("plain"))) || !doc.is<JsonObject>()) {
    json(400, F("{\"error\":\"bad json\"}"));
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
  json(200, F("{\"ok\":true}"));
}

// POST /api/demo {"minutes": 1..30} (default 10; 0 stops): pet mode right away, for showing it
// off or testing visits between Miblos. Alerts still come first.
static void handleDemo() {
  if (!authorized()) {
    json(401, F("{\"error\":\"unauthorized\"}"));
    return;
  }
  StaticJsonDocument<64> doc;
  const String& body = srv->arg(F("plain"));
  if (body.length() && (deserializeJson(doc, body) || !doc.is<JsonObject>())) {
    json(400, F("{\"error\":\"bad json\"}"));
    return;
  }
  JsonVariantConst m = doc["minutes"];
  const int minutes = m.isNull() ? 10 : m.is<int>() ? m.as<int>() : -1;
  if (minutes < 0 || minutes > 30) {
    json(400, F("{\"error\":\"invalid\",\"field\":\"minutes\"}"));
    return;
  }
  const uint32_t now = millis();
  ctx.demo = minutes > 0;
  ctx.demoKick = true;
  ctx.demoUntilMs = now + (uint32_t)minutes * 60000;
  ctx.lastInteractionMs = now;  // wake the screen
  json(200, F("{\"ok\":true}"));
}

static void handleReset() {
  if (!authorized()) {
    json(401, F("{\"error\":\"unauthorized\"}"));
    return;
  }
  json(200, F("{\"ok\":true}"));
  ctx.factoryResetRequested = true;
}

// ---- Daily life: focus, meeting, notes on the desk ----

// The local clock, like app.cpp: known once NTP (or a snapshot) set it.
static bool localNow(struct tm& lt) {
  const time_t now = time(nullptr);
  if (now <= 1600000000) return false;
  localtime_r(&now, &lt);
  return true;
}
// Local minute of the day, or -1 while the time is unknown.
static int minuteNow() {
  struct tm lt;
  return localNow(lt) ? lt.tm_hour * 60 + lt.tm_min : -1;
}
// Today's local date in `d`, or null while the time is unknown.
static const miblo::Date* todayOrNull(miblo::Date& d) {
  struct tm lt;
  if (!localNow(lt)) return nullptr;
  d = miblo::Date{(uint16_t)(lt.tm_year + 1900), (uint8_t)(lt.tm_mon + 1), (uint8_t)lt.tm_mday};
  return &d;
}
// Now in Unix seconds, or 0 while the time is unknown.
static uint32_t epochNow() {
  const time_t now = time(nullptr);
  return now > 1600000000 ? (uint32_t)now : 0;
}

// A module's request handler: fills `out` (200 only) and returns the HTTP status; on 400/409
// *bad names the field or the reason ("clock", "full", ...): always a string literal, since it
// goes into the reply raw (no JSON escaping) and after the handler returned.
using DailyHandler = int (*)(JsonObjectConst body, JsonObject out, const char** bad);

// Daily-life routes: token, a small JSON body (absent = {}), then the module's own handler. One
// function for every route (each route is a captureless lambda), so the parsing exists once.
static void dailyRoute(DailyHandler handle) {
  if (!authorized()) {
    json(401, F("{\"error\":\"unauthorized\"}"));
    return;
  }
  // Both documents on the heap (~1.2 KB on the stack took this path past 3 KB of the 4 KB
  // loop() stack), with the same headroom check as the other handlers that allocate.
  constexpr size_t kBodyDoc = 384, kReplyDoc = 768;
  if (heapLowForRequest(kBodyDoc + kReplyDoc + 768)) {
    json(503, F("{\"error\":\"busy\"}"));
    return;
  }
  DynamicJsonDocument doc(kBodyDoc);  // texts are <= 47 bytes: a body over 300 bytes is not ours
  DynamicJsonDocument reply(kReplyDoc);
  if (!doc.capacity() || !reply.capacity()) {  // an allocation failed anyway
    json(503, F("{\"error\":\"busy\"}"));
    return;
  }
  const String& body = srv->arg(F("plain"));
  if (body.length() > 300 || (body.length() && (deserializeJson(doc, body) || !doc.is<JsonObject>()))) {
    json(400, F("{\"error\":\"bad json\"}"));
    return;
  }
  if (!body.length()) doc.to<JsonObject>();
  // The biggest reply is GET /api/remind: 8 items of 4 members (~690 B on the ESP8266; texts are
  // not copied).
  JsonObject out = reply.to<JsonObject>();
  const char* bad = nullptr;
  const int code = handle(doc.as<JsonObjectConst>(), out, &bad);
  // Someone at the desk: wake the screen. Only a POST: listing the reminders changes nothing.
  if (srv->method() == HTTP_POST) ctx.lastInteractionMs = millis();
  if (code == 200) {
    out[F("ok")] = true;
    if (reply.overflowed()) {  // never a partial answer
      json(500, F("{\"error\":\"reply too large\"}"));
      return;
    }
    String s;
    serializeJson(reply, s);
    json(200, s.c_str());
    return;
  }
  String s = String(F("{\"error\":\"")) + (code == 409 ? F("conflict") : F("invalid")) + F("\",\"field\":\"") +
             (bad ? bad : "") + F("\"}");
  json(code, s.c_str());
}

// POST /api/focus {"focusMin","breakMin","rounds"} | {"stop":true}
static void handleFocus() {
  dailyRoute([](JsonObjectConst b, JsonObject, const char** bad) {
    return miblo::focusRequest(ctx.focus, b, millis(), bad);
  });
}

// POST /api/meeting {"min":1..480} | {"off":true}
static void handleMeeting() {
  dailyRoute([](JsonObjectConst b, JsonObject, const char** bad) {
    return miblo::meetingRequest(ctx.meeting, b, millis(), bad);
  });
}

// POST /api/say {"text","min"} | {"off":true}
static void handleSay() {
  dailyRoute([](JsonObjectConst b, JsonObject, const char** bad) {
    return miblo::sayRequest(ctx.notes, b, millis(), bad);
  });
}

// POST /api/remind {"in"|"at"[,"days"],"text"} | {"dismiss":true} | {"delete":N}
static void handleRemind() {
  dailyRoute([](JsonObjectConst b, JsonObject out, const char** bad) {
    return miblo::remindRequest(ctx.notes, b, millis(), minuteNow(), out, bad);
  });
}

// GET /api/remind: {"items":[{"id":1,"in":840,"text":...},{"id":5,"at":"09:45","days":62,...}]}
static void handleReminders() {
  dailyRoute([](JsonObjectConst, JsonObject out, const char**) {
    ctx.notes.listJson(out.createNestedArray(F("items")), millis(), epochNow());
    return 200;
  });
}

// POST /api/timer {"min":1..180} | {"stop":true}
static void handleTimer() {
  dailyRoute([](JsonObjectConst b, JsonObject, const char** bad) {
    return miblo::timerRequest(ctx.notes, b, millis(), bad);
  });
}

// POST /api/countdown {"label","date":"YYYY-MM-DD"} | {"label","md":"MM-DD"} | {"off":true}
static void handleCountdown() {
  dailyRoute([](JsonObjectConst b, JsonObject, const char** bad) {
    miblo::Date d;
    return miblo::countdownRequest(ctx.notes, b, todayOrNull(d), bad);
  });
}

// POST /api/find: the cat waves with the settings QR for a few seconds.
static void handleFind() {
  dailyRoute([](JsonObjectConst, JsonObject, const char**) {
    ctx.notes.find(millis());
    return 200;
  });
}

static const routes::Route kRoutes[] PROGMEM = {
    {"/api/info", HTTP_GET, handleInfo},        {"/api/pair", HTTP_POST, handlePair},
    {"/api/state", HTTP_POST, handleState},     {"/api/config", HTTP_POST, handleConfig},
    {"/api/reset", HTTP_POST, handleReset},     {"/api/demo", HTTP_POST, handleDemo},
    {"/api/focus", HTTP_POST, handleFocus},     {"/api/meeting", HTTP_POST, handleMeeting},
    {"/api/say", HTTP_POST, handleSay},         {"/api/remind", HTTP_POST, handleRemind},
    {"/api/remind", HTTP_GET, handleReminders}, {"/api/timer", HTTP_POST, handleTimer},
    {"/api/countdown", HTTP_POST, handleCountdown}, {"/api/find", HTTP_POST, handleFind},
};

void begin(WebServerT& server) {
  srv = &server;
  routes::add(server, kRoutes);
}

}  // namespace api
