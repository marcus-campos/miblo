#include "web.h"

#include <ArduinoJson.h>

#include "context.h"
#include "miblo_tz_table.h"
#include "miblo_version.h"
#include "platform/net.h"

namespace web {

using miblo::Lang;
using miblo::S;

static WebServerT* srv = nullptr;
static constexpr uint32_t kMaxPostBody = 4096;  // any POST except /update (the snapshot is ≤ 3072 B)

static const char kCss[] PROGMEM =
    "body{font-family:system-ui,sans-serif;background:#0b0b0d;color:#eee;margin:0;padding:16px;max-width:480px}"
    "h1{font-size:20px;margin:4px 0 12px}h2{font-size:16px;margin:24px 0 8px;color:#f5a524}"
    "label{display:block;margin:12px 0 4px;color:#aaa}input,select{width:100%;box-sizing:border-box;padding:10px;"
    "background:#1a1a1e;color:#eee;border:1px solid #333;border-radius:6px;font-size:16px}"
    "input[type=checkbox]{width:auto;margin-right:8px}button{margin-top:16px;padding:12px;width:100%;border:0;"
    "border-radius:8px;background:#f5a524;color:#111;font-weight:700;font-size:16px}"
    "button.s{background:#333;color:#eee}button.d{background:#ef4444;color:#fff}.m{color:#888}.w{color:#f5a524}"
    "a{color:#60a5fa}";

// Fills the time zone <select> with the device's IANA list (GET /api/zones, one name per line).
// `cur` is the stored value: an IANA name → selected; "UTC0" (never set) or a legacy POSIX rule →
// the browser's zone is preselected, and `done(true)` is called only for "UTC0" so the page can
// save it. Browsers still report a few legacy aliases; A maps them to the names in the table.
static const char kTzJs[] PROGMEM =
    "function tzFill(sel,cur,done){"
    "const A={'UTC':'Etc/UTC','Etc/Universal':'Etc/UTC','Asia/Calcutta':'Asia/Kolkata',"
    "'Europe/Kyiv':'Europe/Kiev','Asia/Saigon':'Asia/Ho_Chi_Minh','Asia/Katmandu':'Asia/Kathmandu',"
    "'Asia/Rangoon':'Asia/Yangon','America/Buenos_Aires':'America/Argentina/Buenos_Aires'};"
    "let b='';try{b=Intl.DateTimeFormat().resolvedOptions().timeZone||'';}catch(e){}"
    "const add=z=>{const o=document.createElement('option');o.value=z;o.textContent=z.replace(/_/g,' ');"
    "sel.appendChild(o);};"
    "const fill=(L,S)=>{sel.textContent='';const bz=S.has(b)?b:(S.has(A[b])?A[b]:'');"
    "const set=S.has(cur),unset=!cur||cur==='UTC0';"
    "if(!set&&!bz&&!unset)add(cur);"  // legacy rule and no usable browser zone: keep it as-is
    "for(const z of L)add(z);"
    "sel.value=set?cur:(bz||(unset?'Etc/UTC':cur));"
    "if(done)done(!set&&unset&&!!bz);};"
    "fetch('/api/zones').then(r=>r.ok?r.text():Promise.reject())"
    ".then(t=>{const L=t.split('\\n').filter(Boolean);fill(L,new Set(L));})"
    ".catch(()=>{const L=[...new Set([cur,b].filter(z=>z&&z!=='UTC0'))];fill(L,new Set(L));});}";

String tr(Lang lang, S id) {
  char b[160];
  miblo::tr(lang, id, b, sizeof(b));
  return String(b);
}

void appendEscaped(String& out, const char* s) {
  for (; *s; s++) {
    switch (*s) {
      case '&': out += F("&amp;"); break;
      case '<': out += F("&lt;"); break;
      case '>': out += F("&gt;"); break;
      case '"': out += F("&quot;"); break;
      case '\'': out += F("&#39;"); break;
      default: out += *s;
    }
  }
}

void sendJson(WebServerT& server, int code, const char* json) {
  server.send(code, F("application/json"), json);
}

void sendLocked(WebServerT& server, uint32_t remainingMs) {
  char out[48];
  snprintf(out, sizeof(out), "{\"error\":\"locked\",\"retryAfter\":%u}", (unsigned)((remainingMs + 999) / 1000));
  sendJson(server, 429, out);
}

// Second layer for the human pages. The server hook installed in begin() refuses most POST
// bodies over kMaxPostBody before ESP8266WebServer buffers them, but only when Content-Length
// arrived in the first TCP segment; by the time a handler runs the body is already in RAM, so
// this check enforces the pages' tighter 1 KiB limit and covers what the hook could not see.
static bool bodyTooLarge() {
  String cl = srv->header(F("Content-Length"));
  return cl.length() > 0 && (uint32_t)cl.toInt() > 1024;
}

bool requireJson(WebServerT& server) {
  String ct = server.header(F("Content-Type"));
  ct.trim();
  ct.toLowerCase();
  if (ct.startsWith(F("application/json")) &&
      (ct.length() == 16 || ct[16] == ';' || ct[16] == ' ' || ct[16] == '\t')) {
    return true;
  }
  sendJson(server, 415, "{\"error\":\"json required\"}");
  return false;
}

Lang pageLang(WebServerT& server) {
  if (ctx.cfg.langSet) return ctx.cfg.lang;
  Lang l = miblo::negotiateLang(server.header(F("Accept-Language")).c_str());
  if (l != ctx.cfg.lang) {  // automatic mode: the screen follows the last browser's language
    ctx.cfg.lang = l;
    ctx.configChanged = true;
  }
  return l;
}

void pageStart(String& out, Lang lang, const char* title, size_t reserveHint) {
  out.reserve(reserveHint);
  out += F("<!doctype html><html lang=\"");
  out += miblo::langCode(lang);
  out += F("\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
           "<title>");
  appendEscaped(out, title);
  out += F("</title><style>");
  out += FPSTR(kCss);
  out += F("</style></head><body>");
}

void pageEnd(String& out) { out += F("</body></html>"); }

static void langOptions(String& out, Lang selected, bool withAuto) {
  if (withAuto) {
    out += F("<option value=\"\">Auto</option>");
  }
  for (int i = 0; i < (int)Lang::Count; i++) {
    out += F("<option value=\"");
    out += miblo::langCode((Lang)i);
    out += '"';
    if (!withAuto && (Lang)i == selected) out += F(" selected");
    out += '>';
    out += miblo::langName((Lang)i);
    out += F("</option>");
  }
}

// ---------- captive portal (setup network) ----------

static void portalPage() {
  Lang lang = pageLang(*srv);
  String out;
  pageStart(out, lang, tr(lang, S::WebSetupTitle).c_str());
  out += F("<h1>");
  appendEscaped(out, tr(lang, S::WebSetupTitle).c_str());
  out += F("</h1><form method=\"post\" action=\"/wifi\"><label>");
  appendEscaped(out, tr(lang, S::WebChooseNetwork).c_str());
  out += F("</label><select name=\"ssid\" id=\"ssid\" onchange=\"o()\">");
  int n = WiFi.scanNetworks();
  if (n > 32) n = 32;
  int order[32];
  for (int i = 0; i < n; i++) order[i] = i;
  for (int i = 1; i < n; i++) {  // sort by signal strength (insertion sort; n is small)
    int cur = order[i];
    int j = i - 1;
    while (j >= 0 && WiFi.RSSI(order[j]) < WiFi.RSSI(cur)) {
      order[j + 1] = order[j];
      j--;
    }
    order[j + 1] = cur;
  }
  int shown = 0;
  for (int i = 0; i < n && shown < 15; i++) {
    String ssid = WiFi.SSID(order[i]);
    bool dup = ssid.length() == 0;
    for (int k = 0; k < i && !dup; k++) {
      if (WiFi.SSID(order[k]) == ssid) dup = true;
    }
    if (dup) continue;
    out += F("<option value=\"");
    appendEscaped(out, ssid.c_str());
    out += F("\">");
    appendEscaped(out, ssid.c_str());
    out += F("</option>");
    shown++;
  }
  WiFi.scanDelete();
  out += F("<option value=\"\">");
  appendEscaped(out, tr(lang, S::WebOtherNetwork).c_str());
  out += F("</option></select><div id=\"other\" hidden><label>");
  appendEscaped(out, tr(lang, S::WebNetworkName).c_str());
  out += F("</label><input name=\"ssid_other\" maxlength=\"32\"></div><label>");
  appendEscaped(out, tr(lang, S::WebPassword).c_str());
  out += F("</label><input name=\"pass\" type=\"password\" maxlength=\"64\"><label>");
  appendEscaped(out, tr(lang, S::WebTimezone).c_str());
  out += F("</label><select name=\"tz\" id=\"tz\" data-cur=\"");
  appendEscaped(out, ctx.cfg.tz);
  out += F("\"></select><label>");
  appendEscaped(out, tr(lang, S::WebLanguage).c_str());
  out += F("</label><select name=\"lang\">");
  langOptions(out, lang, false);
  out += F("</select><button>");
  appendEscaped(out, tr(lang, S::WebConnect).c_str());
  out += F("</button></form><script>");
  out += FPSTR(kTzJs);
  out += F("{const t=document.getElementById('tz');tzFill(t,t.dataset.cur,null);}"
           "function o(){document.getElementById('other').hidden=document.getElementById('ssid').value!==''}o();"
           "</script>");
  pageEnd(out);
  srv->send(200, F("text/html; charset=utf-8"), out);
}

static void handleWifi() {
  // Only from a client of the setup network, while it is up: never from the home LAN.
  if (!net::apActive() || srv->client().localIP() != WiFi.softAPIP()) {
    srv->send(403, F("text/plain"), F("forbidden"));
    return;
  }
  if (bodyTooLarge()) {
    srv->send(413, F("text/plain"), F("payload too large"));
    return;
  }
  String ssid = srv->arg(F("ssid"));
  if (ssid.length() == 0) ssid = srv->arg(F("ssid_other"));
  String pass = srv->arg(F("pass"));
  if (ssid.length() == 0 || ssid.length() > 32 || pass.length() > 64) {
    srv->send(400, F("text/plain"), F("bad ssid"));
    return;
  }
  StaticJsonDocument<256> patch;
  if (srv->arg(F("tz")).length()) patch["tz"] = srv->arg(F("tz"));
  if (srv->arg(F("lang")).length()) patch["lang"] = srv->arg(F("lang"));
  if (miblo::applyConfigPatch(ctx.cfg, patch.as<JsonObjectConst>(), nullptr)) ctx.configChanged = true;

  Lang lang = ctx.cfg.lang;
  String out;
  pageStart(out, lang, tr(lang, S::WebSetupTitle).c_str());
  out += F("<h1>");
  appendEscaped(out, tr(lang, S::WebConnecting).c_str());
  out += F("</h1>");
  pageEnd(out);
  srv->send(200, F("text/html; charset=utf-8"), out);
  net::submitCredentials(ssid.c_str(), pass.c_str(), millis());
}

// ---------- settings page (home network) ----------

// Serializes `doc` into `out`, escaping "<" so a JSON string value (a device name, a paired
// host…) can never close the surrounding <script> tag early (stored XSS).
static void appendJsonForScript(String& out, const JsonDocument& doc) {
  String tmp;
  serializeJson(doc, tmp);
  tmp.replace("<", "\\u003c");
  out += tmp;
}

static void settingsPage() {
  Lang lang = pageLang(*srv);
  String out;
  pageStart(out, lang, deviceName(), 7000);
  out += F("<h1>");
  appendEscaped(out, deviceName());
  out += F("</h1><p class=\"m\">");
  appendEscaped(out, tr(lang, S::WebVersion).c_str());
  out += F(" " MIBLO_FW_VERSION " &middot; ");
  char line[96];
  snprintf(line, sizeof(line), tr(lang, S::WebPairedCount).c_str(), (unsigned)ctx.tokens.count());
  appendEscaped(out, line);
  out += F("</p>");
  if (!ctx.usageEverSeen) {
    out += F("<p class=\"w\">");
    appendEscaped(out, tr(lang, S::WebLimitsHint).c_str());
    out += F("</p>");
  }
  out += F("<h2>");
  appendEscaped(out, tr(lang, S::WebSettings).c_str());
  out += F("</h2><label>");
  appendEscaped(out, tr(lang, S::WebMode).c_str());
  out += F("</label><select id=\"mode\"><option value=\"overview\">");
  appendEscaped(out, tr(lang, S::ModeOverview).c_str());
  out += F("</option><option value=\"limits\">");
  appendEscaped(out, tr(lang, S::ModeLimits).c_str());
  out += F("</option><option value=\"sessions\">");
  appendEscaped(out, tr(lang, S::ModeSessions).c_str());
  out += F("</option></select><label>");
  appendEscaped(out, tr(lang, S::WebBrightness).c_str());
  out += F("</label><input id=\"brightness\" type=\"range\" min=\"5\" max=\"100\"><label><input id=\"alerts\" "
           "type=\"checkbox\">");
  appendEscaped(out, tr(lang, S::WebAlerts).c_str());
  out += F("</label><label>");
  appendEscaped(out, tr(lang, S::WebHeroPerm).c_str());
  out += F("</label><input id=\"heroPermSec\" type=\"number\" min=\"3\" max=\"60\"><label>");
  appendEscaped(out, tr(lang, S::WebHeroDone).c_str());
  out += F("</label><input id=\"heroDoneSec\" type=\"number\" min=\"2\" max=\"60\"><label>");
  appendEscaped(out, tr(lang, S::WebReminder).c_str());
  out += F("</label><input id=\"reminderMin\" type=\"number\" min=\"0\" max=\"30\"><label><input id=\"discreet\" "
           "type=\"checkbox\">");
  appendEscaped(out, tr(lang, S::WebDiscreet).c_str());
  out += F("</label><label>");
  appendEscaped(out, tr(lang, S::WebDeviceName).c_str());
  out += F("</label><input id=\"name\" maxlength=\"20\" placeholder=\"");
  appendEscaped(out, ctx.ident.defaultName);
  out += F("\"><label>");
  appendEscaped(out, tr(lang, S::WebTimezone).c_str());
  out += F("</label><select id=\"tz\"></select><label>");
  appendEscaped(out, tr(lang, S::WebLanguage).c_str());
  out += F("</label><select id=\"lang\">");
  langOptions(out, lang, true);
  out += F("</select><button onclick=\"save()\">");
  appendEscaped(out, tr(lang, S::WebSave).c_str());
  out += F("</button><p id=\"st\" class=\"m\"></p><h2>");
  appendEscaped(out, tr(lang, S::WebFirmware).c_str());
  out += F("</h2><p><a href=\"/update\">");
  appendEscaped(out, tr(lang, S::WebFirmware).c_str());
  out += F("</a></p><button class=\"s\" onclick=\"post('/pair-code')\">");
  appendEscaped(out, tr(lang, S::WebShowPairCode).c_str());
  out += F("</button><h2>");
  appendEscaped(out, tr(lang, S::WebFactoryReset).c_str());
  out += F("</h2><p class=\"m\">");
  appendEscaped(out, tr(lang, S::WebResetConfirm).c_str());
  out += F("</p><button class=\"d\" onclick=\"rst()\">");
  appendEscaped(out, tr(lang, S::WebFactoryReset).c_str());
  out += F("</button><script>const C=");

  DynamicJsonDocument cfg(768);
  miblo::configToJson(ctx.cfg, cfg.to<JsonObject>());
  appendJsonForScript(out, cfg);
  out += F(";const T=");
  DynamicJsonDocument txt(768);
  txt["saved"] = tr(lang, S::WebSaved);
  txt["failed"] = tr(lang, S::WebFailed);
  txt["hint"] = tr(lang, S::WebCodeHint);
  txt["bad"] = tr(lang, S::WebBadCode);
  appendJsonForScript(out, txt);
  out += F(";");
  out += FPSTR(kTzJs);
  out += F(
      "const $=k=>document.getElementById(k),J={'Content-Type':'application/json'};"
      "for(const k in C){const e=$(k);if(!e)continue;if(e.type==='checkbox')e.checked=C[k];else e.value=C[k];}"
      "function val(k){const e=$(k);return e.type==='checkbox'?e.checked:"
      "(e.type==='number'||e.type==='range')?Number(e.value):e.value;}"
      "function save(){const b={};for(const k of ['mode','brightness','alerts','heroPermSec','heroDoneSec',"
      "'reminderMin','discreet','name','tz','lang']){const v=val(k);if(k==='tz'&&!v)continue;b[k]=v;}"
      "fetch('/settings',{method:'POST',headers:J,body:JSON.stringify(b)})"
      ".then(r=>{$('st').textContent=r.ok?T.saved:T.failed;}).catch(()=>{$('st').textContent=T.failed;});}"
      "function post(u){return fetch(u,{method:'POST',headers:J,body:'{}'});}"
      "function rst(){post('/reset-code').then(()=>{const c=prompt(T.hint);if(!c)return;"
      "post('/factory-reset?code='+encodeURIComponent(c)).then(r=>{if(!r.ok)alert(T.bad);});});}"
      "tzFill($('tz'),C.tz,ch=>{if(ch)save();});"
      "</script>");
  pageEnd(out);
  srv->send(200, F("text/html; charset=utf-8"), out);
}

static void handleSettings() {
  if (!requireJson(*srv)) return;
  if (bodyTooLarge() || srv->arg(F("plain")).length() > 1024) {
    sendJson(*srv, 413, "{\"error\":\"too large\"}");
    return;
  }
  DynamicJsonDocument doc(1024);
  if (deserializeJson(doc, srv->arg(F("plain"))) || !doc.is<JsonObject>()) {
    sendJson(*srv, 400, "{\"error\":\"bad json\"}");
    return;
  }
  const char* bad = nullptr;
  if (!miblo::applyConfigPatch(ctx.cfg, doc.as<JsonObjectConst>(), &bad)) {
    String err = String(F("{\"error\":\"invalid\",\"field\":\"")) + bad + F("\"}");
    sendJson(*srv, 400, err.c_str());
    return;
  }
  ctx.configChanged = true;
  sendJson(*srv, 200, "{\"ok\":true}");
}

// The IANA zone names the device can resolve, one per line. Streamed straight from flash
// (send_P writes the PROGMEM blob in small chunks): no ~7 KB String on a ~30 KB heap.
static void handleZones() {
  srv->sendHeader(F("Cache-Control"), F("max-age=86400"));
  srv->send_P(200, PSTR("text/plain; charset=utf-8"), miblo::kTzNames, miblo::kTzNamesLen);
}

static void handleRoot() {
  if (net::apActive() && !net::connected()) portalPage();
  else settingsPage();
}

static void handlePairCode() {
  if (!requireJson(*srv)) return;
  ctx.showPairCode = true;
  ctx.pairCodeAtMs = millis();
  sendJson(*srv, 200, "{\"ok\":true}");
}

static void handleResetCode() {
  if (!requireJson(*srv)) return;
  const uint32_t now = millis();
  char code[5];
  miblo::formatCode(hwRandom(), code);
  if (!ctx.presence.open(miblo::PresenceGate::Purpose::Reset, code, now)) {
    sendLocked(*srv, ctx.presence.lockRemainingMs(now));
    return;
  }
  sendJson(*srv, 200, "{\"ok\":true}");
}

static void handleFactoryReset() {
  if (!requireJson(*srv)) return;
  if (ctx.presence.locked(millis())) {
    sendLocked(*srv, ctx.presence.lockRemainingMs(millis()));
    return;
  }
  if (!ctx.presence.check(miblo::PresenceGate::Purpose::Reset, srv->arg(F("code")).c_str(), millis())) {
    sendJson(*srv, 403, "{\"error\":\"bad code\"}");
    return;
  }
  sendJson(*srv, 200, "{\"ok\":true}");
  ctx.factoryResetRequested = true;
}

static bool captiveRedirect() {
  if (!net::apActive() || net::connected()) return false;
  String host = srv->hostHeader();
  if (host == WiFi.softAPIP().toString()) return false;
  srv->sendHeader(F("Location"), String(F("http://")) + WiFi.softAPIP().toString() + F("/"), true);
  srv->send(302, F("text/plain"), "");
  return true;
}

#if defined(ESP8266)
// Best-effort first layer. Runs right after the request line, before ESP8266WebServer reads (and
// buffers in RAM) a non-multipart POST body. Peeks at the header bytes already received — without
// consuming them — and refuses bodies over kMaxPostBody (largest Content-Length if duplicated).
// Only a multipart POST /update (streamed to flash by the upload handler) is exempt.
// Headers that did not arrive in the first TCP segment are not seen here; the handler checks
// remain as a second layer.
static ESP8266WebServer::ClientFuture limitPostBody(const String& method, const String& url, WiFiClient* client,
                                                     ESP8266WebServer::ContentTypeFunction) {
  if (method != F("POST")) return ESP8266WebServer::CLIENT_REQUEST_CAN_CONTINUE;
  if (url == F("/update") && miblo::contentTypeIsMultipart(client->peekBuffer(), client->peekAvailable())) {
    return ESP8266WebServer::CLIENT_REQUEST_CAN_CONTINUE;
  }
  uint32_t len = 0;
  if (!miblo::findContentLength(client->peekBuffer(), client->peekAvailable(), len) || len <= kMaxPostBody) {
    return ESP8266WebServer::CLIENT_REQUEST_CAN_CONTINUE;
  }
  static const char kReply[] PROGMEM =
      "HTTP/1.1 413 Payload Too Large\r\nContent-Type: application/json\r\nConnection: close\r\n"
      "Content-Length: 21\r\n\r\n{\"error\":\"too large\"}";
  client->print(FPSTR(kReply));
  return ESP8266WebServer::CLIENT_MUST_STOP;
}
#endif

void begin(WebServerT& server) {
  srv = &server;
#if defined(ESP8266)
  server.addHook(limitPostBody);
#endif
  // Content-Length: OTA progress (ota.cpp) and the body-size checks above; Authorization: the API
  // (api.cpp); Content-Type: the CSRF check on the pages' state-changing POSTs (requireJson).
  server.collectHeaders("Accept-Language", "Authorization", "Content-Length", "Content-Type");
  server.on(F("/"), HTTP_GET, handleRoot);
  server.on(F("/wifi"), HTTP_POST, handleWifi);
  server.on(F("/settings"), HTTP_POST, handleSettings);
  server.on(F("/api/zones"), HTTP_GET, handleZones);
  server.on(F("/pair-code"), HTTP_POST, handlePairCode);
  server.on(F("/reset-code"), HTTP_POST, handleResetCode);
  server.on(F("/factory-reset"), HTTP_POST, handleFactoryReset);
  // Captive-portal detection (Android, iOS/macOS, Windows): everything goes to the setup page.
  for (const char* path : {"/generate_204", "/gen_204", "/hotspot-detect.html", "/library/test/success.html",
                           "/ncsi.txt", "/connecttest.txt", "/redirect", "/fwlink"}) {
    server.on(path, HTTP_GET, [] {
      if (!captiveRedirect()) handleRoot();
    });
  }
  server.onNotFound([] {
    if (captiveRedirect()) return;
    sendJson(*srv, 404, "{\"error\":\"not found\"}");
  });
}

}  // namespace web
