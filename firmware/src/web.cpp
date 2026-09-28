#include "web.h"

#include <ArduinoJson.h>

#include "context.h"
#include "miblo_version.h"
#include "platform/net.h"

namespace web {

using miblo::Lang;
using miblo::S;

static WebServerT* srv = nullptr;

static const char kCss[] PROGMEM =
    "body{font-family:system-ui,sans-serif;background:#0b0b0d;color:#eee;margin:0;padding:16px;max-width:480px}"
    "h1{font-size:20px;margin:4px 0 12px}h2{font-size:16px;margin:24px 0 8px;color:#f5a524}"
    "label{display:block;margin:12px 0 4px;color:#aaa}input,select{width:100%;box-sizing:border-box;padding:10px;"
    "background:#1a1a1e;color:#eee;border:1px solid #333;border-radius:6px;font-size:16px}"
    "input[type=checkbox]{width:auto;margin-right:8px}button{margin-top:16px;padding:12px;width:100%;border:0;"
    "border-radius:8px;background:#f5a524;color:#111;font-weight:700;font-size:16px}"
    "button.s{background:#333;color:#eee}button.d{background:#ef4444;color:#fff}.m{color:#888}.w{color:#f5a524}"
    "a{color:#60a5fa}";

// Monta um POSIX TZ a partir do fuso do navegador (offset de janeiro/julho + regra de horário de
// verão por região). Sem horário de verão → offset fixo, ex. "<-03>3".
static const char kTzJs[] PROGMEM =
    "function posixTz(){const y=new Date().getFullYear();"
    "const jan=-new Date(y,0,1).getTimezoneOffset(),jul=-new Date(y,6,1).getTimezoneOffset();"
    "const std=Math.min(jan,jul),dst=Math.max(jan,jul);"
    "const p=n=>String(n).padStart(2,'0');"
    "const nm=m=>'<'+(m<0?'-':'+')+p(Math.floor(Math.abs(m)/60))+(Math.abs(m)%60?p(Math.abs(m)%60):'')+'>';"
    "const off=m=>{const a=Math.abs(m);return (m>0?'-':'')+Math.floor(a/60)+(a%60?':'+p(a%60):'')};"
    "let tz=nm(std)+off(std);if(jan===jul)return tz;"
    "const zone=(Intl.DateTimeFormat().resolvedOptions().timeZone||'');let rule;"
    "if(zone.startsWith('Europe/')){const h=1+std/60;rule=',M3.5.0/'+h+',M10.5.0/'+(h+1);}"
    "else if(jul>jan){rule=',M3.2.0,M11.1.0';}else{rule=',M10.1.0,M4.1.0/3';}"
    "return tz+nm(dst)+(dst-std!==60?off(dst):'')+rule;}";

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

Lang pageLang(WebServerT& server) {
  if (ctx.cfg.langSet) return ctx.cfg.lang;
  Lang l = miblo::negotiateLang(server.header(F("Accept-Language")).c_str());
  if (l != ctx.cfg.lang) {  // modo automático: a tela acompanha o idioma do último navegador
    ctx.cfg.lang = l;
    ctx.configChanged = true;
  }
  return l;
}

void pageStart(String& out, Lang lang, const char* title) {
  out.reserve(7000);
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

// ---------- portal cativo (rede de setup) ----------

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
  for (int i = 1; i < n; i++) {  // ordena por sinal (inserção; n é pequeno)
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
  out += F("</label><input name=\"tz\" id=\"tz\" maxlength=\"47\" value=\"");
  appendEscaped(out, ctx.cfg.tz);
  out += F("\"><label>");
  appendEscaped(out, tr(lang, S::WebLanguage).c_str());
  out += F("</label><select name=\"lang\">");
  langOptions(out, lang, false);
  out += F("</select><button>");
  appendEscaped(out, tr(lang, S::WebConnect).c_str());
  out += F("</button></form><script>");
  out += FPSTR(kTzJs);
  out += F("document.getElementById('tz').value=posixTz();"
           "function o(){document.getElementById('other').hidden=document.getElementById('ssid').value!==''}o();"
           "</script>");
  pageEnd(out);
  srv->send(200, F("text/html; charset=utf-8"), out);
}

static void handleWifi() {
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

// ---------- página de configuração (rede de casa) ----------

static void settingsPage() {
  Lang lang = pageLang(*srv);
  String out;
  pageStart(out, lang, deviceName());
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
  out += F("</label><input id=\"tz\" maxlength=\"47\"><label>");
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
  serializeJson(cfg, out);
  out += F(";const T=");
  DynamicJsonDocument txt(768);
  txt["saved"] = tr(lang, S::WebSaved);
  txt["failed"] = tr(lang, S::WebFailed);
  txt["hint"] = tr(lang, S::WebCodeHint);
  txt["bad"] = tr(lang, S::WebBadCode);
  serializeJson(txt, out);
  out += F(";");
  out += FPSTR(kTzJs);
  out += F(
      "const $=k=>document.getElementById(k);"
      "for(const k in C){const e=$(k);if(!e)continue;if(e.type==='checkbox')e.checked=C[k];else e.value=C[k];}"
      "function val(k){const e=$(k);return e.type==='checkbox'?e.checked:"
      "(e.type==='number'||e.type==='range')?Number(e.value):e.value;}"
      "function save(){const b={};for(const k of ['mode','brightness','alerts','heroPermSec','heroDoneSec',"
      "'reminderMin','discreet','name','tz','lang'])b[k]=val(k);"
      "fetch('/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)})"
      ".then(r=>{$('st').textContent=r.ok?T.saved:T.failed;}).catch(()=>{$('st').textContent=T.failed;});}"
      "function post(u){return fetch(u,{method:'POST'});}"
      "function rst(){post('/reset-code').then(()=>{const c=prompt(T.hint);if(!c)return;"
      "fetch('/factory-reset?code='+encodeURIComponent(c),{method:'POST'}).then(r=>{if(!r.ok)alert(T.bad);});});}"
      "if(C.tz==='UTC0'){const z=posixTz();if(z!=='UTC0'){$('tz').value=z;save();}}"
      "</script>");
  pageEnd(out);
  srv->send(200, F("text/html; charset=utf-8"), out);
}

static void handleSettings() {
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

static void handleRoot() {
  if (net::apActive() && !net::connected()) portalPage();
  else settingsPage();
}

static void handlePairCode() {
  ctx.showPairCode = true;
  ctx.pairCodeAtMs = millis();
  sendJson(*srv, 200, "{\"ok\":true}");
}

static void handleResetCode() {
  char code[5];
  miblo::formatCode(hwRandom(), code);
  ctx.presence.open(miblo::PresenceGate::Purpose::Reset, code, millis());
  sendJson(*srv, 200, "{\"ok\":true}");
}

static void handleFactoryReset() {
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

void begin(WebServerT& server) {
  srv = &server;
  // Content-Length: progresso do OTA (ota.cpp); Authorization: API (api.cpp).
  server.collectHeaders("Accept-Language", "Authorization", "Content-Length");
  server.on(F("/"), HTTP_GET, handleRoot);
  server.on(F("/wifi"), HTTP_POST, handleWifi);
  server.on(F("/settings"), HTTP_POST, handleSettings);
  server.on(F("/pair-code"), HTTP_POST, handlePairCode);
  server.on(F("/reset-code"), HTTP_POST, handleResetCode);
  server.on(F("/factory-reset"), HTTP_POST, handleFactoryReset);
  // Detecção de portal cativo (Android, iOS/macOS, Windows): tudo vai para a página de setup.
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
