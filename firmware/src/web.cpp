#include "web.h"

#include <ArduinoJson.h>

#include "board.h"
#include "context.h"
#include "miblo_snapshot.h"
#include "platform/platform.h"
#include "miblo_tz_table.h"
#include "miblo_version.h"
#include "platform/net.h"

namespace web {

using miblo::Lang;
using miblo::S;

static WebServerT* srv = nullptr;
static constexpr uint32_t kMaxPostBody = miblo::kSnapshotMaxBytes + 512;  // any POST except /update; fits a full snapshot

// Shared by every page (portal, settings, firmware update): cards, toggles, a fixed save bar.
static const char kCss[] PROGMEM =
    "*{box-sizing:border-box}html{color-scheme:dark}"
    "body{font-family:system-ui,sans-serif;background:#0b0b0d;color:#eee;margin:0 auto;padding:16px 16px 96px;"
    "max-width:560px;line-height:1.35}"
    "h1{font-size:22px;margin:4px 0 2px}h2{font-size:13px;margin:0 0 2px;color:#f5a524;text-transform:uppercase;"
    "letter-spacing:.06em}label{display:block;margin:12px 0 4px;color:#aaa;font-size:14px}"
    "input,select{width:100%;padding:10px;background:#1a1a1e;color:#eee;border:1px solid #333;border-radius:8px;"
    "font-size:16px}input:focus,select:focus{outline:2px solid #f5a524;outline-offset:-1px}"
    "input[type=checkbox]{flex:none;width:22px;height:22px;margin:0;accent-color:#f5a524}"
    "input[type=range]{padding:0;border:0;background:none;accent-color:#f5a524;height:28px}"
    "button{margin-top:16px;padding:12px;width:100%;border:0;border-radius:8px;background:#f5a524;color:#111;"
    "font-weight:700;font-size:16px;cursor:pointer}"
    "button.s{background:#2a2a30;color:#eee}button.d{background:#ef4444;color:#fff}.m{color:#888;font-size:14px}"
    ".w{color:#f5a524}a{color:#60a5fa}.r{display:flex;gap:8px}[hidden]{display:none!important}"
    ".c{background:#141417;border:1px solid #26262c;border-radius:12px;padding:14px;margin:14px 0}"
    ".g{display:grid;grid-template-columns:1fr 1fr;gap:0 12px;align-items:end}"
    ".t{display:flex;align-items:center;justify-content:space-between;gap:12px;color:#eee;font-size:16px;"
    "margin:14px 0 2px;cursor:pointer}.c>.t:first-child{margin-top:0;font-weight:600}"
    ".v{float:right;color:#eee}.bad{outline:2px solid #ef4444}#tzr{flex:0 0 40%}"
    "summary{cursor:pointer;font-weight:600;color:#aaa}details[open] summary{margin-bottom:12px}"
    "details h2{margin-top:16px}.bar{position:fixed;left:0;right:0;bottom:0;background:#0b0b0df0;"
    "border-top:1px solid #26262c;padding:10px 16px calc(10px + env(safe-area-inset-bottom))}"
    ".bar>div{max-width:528px;margin:0 auto;display:flex;align-items:center;gap:12px}"
    ".bar button{margin:0;flex:0 0 45%}#st{flex:1}#st.ok{color:#22c55e}#st.no{color:#ef4444}";

// Time zone picker shared by the portal and the settings page: a region <select> (`reg`) and a
// city <select> (`sel`, the value that is submitted), filled from GET /api/zones (the device's
// IANA list, one name per line). `cur` is the stored value: an IANA name → selected; "UTC0"
// (never set) or a legacy POSIX rule → the browser's zone is preselected, and `done(true)` is
// called only for "UTC0" (and only with the device's list, so the name is known) so the page
// can save it. A legacy rule with no usable browser zone stays selectable as-is. Browsers still
// report a few legacy aliases; A maps them to the names in the table. The list is fetched
// twice at most: the single-client web server may still be busy with the page. #tzn (if
// present) shows the time in the selected zone, so a wrong pick is obvious.
static const char kTzJs[] PROGMEM =
    "function tzFill(reg,sel,cur,done){"
    "const A={'UTC':'Etc/UTC','Etc/Universal':'Etc/UTC','Asia/Calcutta':'Asia/Kolkata',"
    "'Europe/Kyiv':'Europe/Kiev','Asia/Saigon':'Asia/Ho_Chi_Minh','Asia/Katmandu':'Asia/Kathmandu',"
    "'Asia/Rangoon':'Asia/Yangon','America/Buenos_Aires':'America/Argentina/Buenos_Aires'};"
    "let b='',Z=[];try{b=Intl.DateTimeFormat().resolvedOptions().timeZone||'';}catch(e){}"
    "const rg=z=>/^[A-Za-z]+\\//.test(z)?z.split('/')[0]:z;"
    "const now=()=>{const n=document.getElementById('tzn');if(!n)return;try{n.textContent=sel.value?"
    "new Date().toLocaleTimeString(document.documentElement.lang||[],{timeZone:sel.value,hour:'2-digit',minute:'2-digit'}):'';}"
    "catch(e){n.textContent='';}};"
    "const city=r=>{sel.textContent='';for(const z of Z)if(rg(z)===r)"
    "sel.add(new Option(z===r?z:z.slice(r.length+1).replace(/_/g,' ').replace(/\\//g,' / '),z));};"
    "reg.onchange=()=>{city(reg.value);now();};sel.addEventListener('change',now);"
    "const fill=(L,ok)=>{const S=new Set(L),bz=S.has(b)?b:(S.has(A[b])?A[b]:''),"
    "set=S.has(cur),unset=!cur||cur==='UTC0',v=set?cur:(bz||(unset?'Etc/UTC':cur));"
    "Z=S.has(v)?L:[v].concat(L);reg.textContent='';"
    "for(const r of new Set(Z.map(rg)))reg.add(new Option(r.replace(/_/g,' '),r));"
    "reg.value=rg(v);city(reg.value);sel.value=v;now();"
    "if(done)done(ok&&!set&&unset&&!!bz);};"
    "const get=n=>fetch('/api/zones').then(r=>r.ok?r.text():Promise.reject())"
    ".catch(e=>n?new Promise(w=>setTimeout(w,1500)).then(()=>get(n-1)):Promise.reject(e));"
    "get(1).then(t=>[t.split('\\n').filter(Boolean),1],"
    "()=>[[...new Set([cur,A[b]||b].filter(z=>z&&z!=='UTC0'))],0]).then(([L,ok])=>fill(L,ok));}";

String tr(Lang lang, S id) {
  char b[256];  // longest entry: WebRefused in Russian (~250 B of UTF-8); test_i18n keeps them under this
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
  snprintf_P(out, sizeof(out), PSTR("{\"error\":\"locked\",\"retryAfter\":%u}"), (unsigned)((remainingMs + 999) / 1000));
  sendJson(server, 429, out);
}

bool openPresence(WebServerT& server, miblo::PresenceGate::Purpose p, uint32_t nowMs) {
  char code[5];
  miblo::formatCode(hwRandom(), code);
  if (ctx.presence.open(p, code, nowMs)) return true;
  if (ctx.presence.locked(nowMs)) {
    sendLocked(server, ctx.presence.lockRemainingMs(nowMs));
  } else {  // another purpose's code is on the screen: never replaced, retry once it is gone
    char out[48];
    snprintf_P(out, sizeof(out), PSTR("{\"error\":\"busy\",\"retryAfter\":%u}"),
             (unsigned)((ctx.presence.remainingMs(nowMs) + 999) / 1000));
    sendJson(server, 429, out);
  }
  return false;
}

// Second layer for the human pages. The server hook installed in begin() refuses most POST
// bodies over kMaxPostBody before ESP8266WebServer buffers them, but only when Content-Length
// arrived in the first TCP segment; by the time a handler runs the body is already in RAM, so
// this check enforces the pages' tighter 1 KiB limit and covers what the hook could not see.
static bool bodyTooLarge() {
  String cl = requestHeader(*srv, F("Content-Length"));
  return cl.length() > 0 && (uint32_t)cl.toInt() > 1024;
}

bool requireJson(WebServerT& server) {
  String ct = requestHeader(server, F("Content-Type"));
  ct.trim();
  ct.toLowerCase();
  if (ct.startsWith(F("application/json")) &&
      (ct.length() == 16 || ct[16] == ';' || ct[16] == ' ' || ct[16] == '\t')) {
    return true;
  }
  sendJson(server, 415, "{\"error\":\"json required\"}");
  return false;
}

// Before pairing (setup), automatic mode makes the screen follow the browser's language. A paired
// gadget draws the page in the browser's language and never changes or saves anything for a page
// view (nobody on the LAN can make it write its flash, or learn its language, by opening a page).
Lang pageLang(WebServerT& server) {
  const Lang browser = miblo::negotiateLang(requestHeader(server, F("Accept-Language")).c_str());
  bool store = false;
  const Lang l = miblo::pageLanguage(ctx.tokens.count() > 0, ctx.cfg.langSet, ctx.cfg.lang, browser, store);
  if (store) {
    ctx.cfg.lang = l;
    ctx.configChanged = true;
  }
  return l;
}

void pageStart(String& out, Lang lang, const char* title) {
  srv->setContentLength(CONTENT_LENGTH_UNKNOWN);
  srv->send(200, F("text/html; charset=utf-8"), "");
  out.reserve(kPageChunk + 256);
  out += F("<!doctype html><html lang=\"");
  out += miblo::langCode(lang);
  out += F("\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
           "<title>");
  appendEscaped(out, title);
  out += F("</title><style>");
  pageSendP(out, kCss);
  out += F("</style></head><body>");
}

void pageFlush(String& out, bool force) {
  if (!out.length() || (!force && out.length() < kPageChunk)) return;
  srv->sendContent(out);
  out.remove(0);  // keeps the reserved buffer for the next chunk
}

void pageSendP(String& out, PGM_P blob) {
  pageFlush(out, true);
  srv->sendContent_P(blob);
}

void pageEnd(String& out) {
  out += F("</body></html>");
  pageFlush(out, true);
  srv->sendContent("");  // zero-length chunk: end of the response
}

static void langOptions(String& out, Lang selected, bool withAuto) {
  if (withAuto) {
    out += F("<option value=\"\">");
    appendEscaped(out, tr(selected, S::WebAuto).c_str());
    out += F("</option>");
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

static void appendJsonForScript(String& out, const JsonDocument& doc);

// ---------- captive portal (setup network) ----------

// Why the last submitted network failed, for the portal ("" if it did not).
static String failureText(Lang lang) {
  switch (net::state()) {
    case miblo::NetState::WrongPassword: return tr(lang, S::WrongPassword);
    case miblo::NetState::JoinFailed: break;
    default: return String();
  }
  switch (net::joinFailure()) {
    case miblo::JoinFailure::NotFound: return tr(lang, S::WebNotFound);
    case miblo::JoinFailure::Refused: return tr(lang, S::WebRefused);
    case miblo::JoinFailure::Other: {
      char b[160];
      snprintf(b, sizeof(b), tr(lang, S::WebFailedCode).c_str(), (unsigned)net::joinFailureCode());
      return String(b);
    }
    case miblo::JoinFailure::Timeout:
    case miblo::JoinFailure::None: break;
  }
  return tr(lang, S::WebConnectFailed);
}

// Shown after a submission (and on "/" while it is being tried): polls /api/wifi-status and says
// how it ended. The decision (and saving the network) happens on the device; if the phone drops
// off the setup network (the AP follows the router's channel), the page says to look at the screen.
static void joinStatusPage(Lang lang) {
  String out;
  pageStart(out, lang, tr(lang, S::WebSetupTitle).c_str());
  out += F("<h1 id=\"h\">");
  appendEscaped(out, tr(lang, S::WebConnecting).c_str());
  out += F("</h1><p id=\"m\" class=\"w\"></p><p id=\"a\"></p><script>const T=");
  DynamicJsonDocument txt(2048);
  txt["ok"] = tr(lang, S::WebConnectedAt);
  txt["wrong_password"] = tr(lang, S::WrongPassword);
  txt["not_found"] = tr(lang, S::WebNotFound);
  txt["refused"] = tr(lang, S::WebRefused);
  txt["timeout"] = tr(lang, S::WebConnectFailed);
  txt["failed"] = tr(lang, S::WebFailedCode);
  txt["again"] = tr(lang, S::WebTryAgain);
  txt["noreply"] = tr(lang, S::WebNoReply);
  appendJsonForScript(out, txt);
  out += F(
      ";const $=k=>document.getElementById(k);let f=0;"
      "function link(u,t){const e=document.createElement('a');e.href=u;e.textContent=t||u;"
      "$('a').appendChild(e);$('a').appendChild(document.createElement('br'));}"
      "function show(j){const s=j.state;"
      "if(s==='connecting'){setTimeout(poll,2000);return;}"
      "if(s==='connected'){$('h').textContent=T.ok;link('http://'+j.ip+'/');link('http://'+j.host+'/');return;}"
      "$('h').textContent=T[s]||(s==='failed'?T.failed.replace('%u',j.code):T.timeout);"
      "link('/',T.again);}"
      "function poll(){fetch('/api/wifi-status',{cache:'no-store'}).then(r=>r.json())"
      ".then(j=>{f=0;show(j);}).catch(()=>{if(++f>=4)$('h').textContent=T.noreply;setTimeout(poll,2000);});}"
      "setTimeout(poll,1500);</script>");
  pageEnd(out);
}

// GET /api/wifi-status: progress of the submitted network, plus diagnostics (last station
// disconnect reason and WiFi.status()) for field reports.
static void handleWifiStatus() {
  if (!net::apActive()) {  // only the setup portal's join page needs it
    sendJson(*srv, 404, "{\"error\":\"not found\"}");
    return;
  }
  StaticJsonDocument<256> doc;
  doc["state"] = miblo::joinStatusName(net::state(), net::joinFailure(), net::trialBusy());
  if (net::connected()) {
    doc["ip"] = net::ip();
    String host = String(ctx.ident.id) + F(".local");
    doc["host"] = host;
  }
  doc["code"] = net::joinFailureCode();
  doc["reason"] = net::lastDisconnectReason();
  doc["status"] = net::wifiStatus();
  String out;
  serializeJson(doc, out);
  srv->sendHeader(F("Cache-Control"), F("no-store"));
  sendJson(*srv, 200, out.c_str());
}

// Joining a network from the portal needs the on-screen code unless the unit is fresh (no saved
// network, not paired; a factory-reset unit is fresh again): see miblo::wifiCodeRequired.
static bool wifiCodeNeeded() { return miblo::wifiCodeRequired(net::hasSavedNetwork(), ctx.tokens.count()); }

// Only from a client of the setup network, while it is up: never from the home LAN.
static bool fromSetupNetwork() { return net::apActive() && srv->client().localIP() == WiFi.softAPIP(); }

static void portalPage() {
  Lang lang = pageLang(*srv);
  const bool needCode = wifiCodeNeeded();
  String out;
  pageStart(out, lang, tr(lang, S::WebSetupTitle).c_str());
  out += F("<h1>");
  appendEscaped(out, tr(lang, S::WebSetupTitle).c_str());
  out += F("</h1><p class=\"w\">");
  // The plugin finds the gadget over mDNS on the local network: another network never works.
  appendEscaped(out, tr(lang, S::WebSameNetwork).c_str());
  out += F("</p>");
  String why = failureText(lang);
  if (why.length()) {  // the last attempt failed: say why above the form
    out += F("<p class=\"w\">");
    appendEscaped(out, why.c_str());
    out += F("</p>");
  }
  out += F("<form method=\"post\" action=\"/wifi\"><label>");
  appendEscaped(out, tr(lang, S::WebChooseNetwork).c_str());
  out += F("</label><select name=\"ssid\" id=\"ssid\" onchange=\"o()\">");
  // The networks come from the background sweep (net::scannedNetworks): no scan in the request.
  const uint8_t n = net::scannedNetworks();
  for (uint8_t i = 0; i < n; i++) {
    const char* ssid = net::scannedNetwork(i);
    out += F("<option value=\"");
    appendEscaped(out, ssid);
    out += F("\">");
    appendEscaped(out, ssid);
    out += F("</option>");
    pageFlush(out);
  }
  out += F("<option value=\"\">");
  appendEscaped(out, tr(lang, S::WebOtherNetwork).c_str());
  out += F("</option></select>");
  if (!n) {  // the first sweep is still running (a couple of seconds): look again soon
    out += F("<script>setTimeout(()=>{if(!document.querySelector('[name=pass]').value)location.reload()},3000)"
             "</script>");
  }
  out += F("<div id=\"other\" hidden><label>");
  appendEscaped(out, tr(lang, S::WebNetworkName).c_str());
  out += F("</label><input name=\"ssid_other\" maxlength=\"32\"></div><label>");
  appendEscaped(out, tr(lang, S::WebPassword).c_str());
  out += F("</label><input name=\"pass\" type=\"password\" maxlength=\"64\">");
  if (needCode) {  // a configured unit: the code its screen shows (requested by the script below)
    out += F("<label for=\"code\">");
    appendEscaped(out, tr(lang, S::WebCodeHint).c_str());
    out += F("</label><input id=\"code\" name=\"code\" inputmode=\"numeric\" maxlength=\"4\" "
             "autocomplete=\"off\" required><button type=\"button\" class=\"s\" onclick=\"sc()\">");
    appendEscaped(out, tr(lang, S::WebUnlockTitle).c_str());  // "Code on the screen"
    out += F("</button><p class=\"w\" id=\"cw\" hidden>");
    appendEscaped(out, tr(lang, S::WebFailed).c_str());
    out += F("</p>");
  }
  out += F("<label for=\"tz\">");
  appendEscaped(out, tr(lang, S::WebTimezone).c_str());
  out += F("<span class=\"v\" id=\"tzn\"></span></label><div class=\"r\"><select id=\"tzr\"></select>"
           "<select name=\"tz\" id=\"tz\" data-cur=\"");
  if (!needCode) appendEscaped(out, ctx.cfg.tz);  // a configured unit's zone is not for passers-by
  out += F("\"></select></div><label>");
  appendEscaped(out, tr(lang, S::WebLanguage).c_str());
  out += F("</label><select name=\"lang\">");
  langOptions(out, lang, false);
  out += F("</select><button>");
  appendEscaped(out, tr(lang, S::WebConnect).c_str());
  out += F("</button></form><script>");
  pageSendP(out, kTzJs);
  out += F("{const t=document.getElementById('tz');tzFill(document.getElementById('tzr'),t,t.dataset.cur,null);}"
           "function o(){document.getElementById('other').hidden=document.getElementById('ssid').value!==''}o();");
  if (needCode) {
    // The code goes on the screen only when the person asks for it (not when a passer-by merely
    // loads the page).
    out += F("function sc(){const w=document.getElementById('cw');w.hidden=true;"
             "fetch('/wifi-code',{method:'POST',headers:{'Content-Type':'application/json'},body:'{}'})"
             ".then(r=>{if(r.status===429)return r.json().then(j=>{"
             "w.textContent=w.dataset.t+' ('+(j.retryAfter||60)+' s)';w.hidden=false;});}).catch(()=>{});}"
             "{const w=document.getElementById('cw');w.dataset.t=w.textContent;}");
  }
  out += F("</script>");
  pageEnd(out);
}

// A short page with one message and a link back to the form.
static void messagePage(Lang lang, const String& msg) {
  String out;
  pageStart(out, lang, tr(lang, S::WebSetupTitle).c_str());
  out += F("<h1>");
  appendEscaped(out, msg.c_str());
  out += F("</h1><p><a href=\"/\">");
  appendEscaped(out, tr(lang, S::WebTryAgain).c_str());
  out += F("</a></p>");
  pageEnd(out);
}

// POST /wifi-code (JSON, from the setup network): a configured unit shows the code its portal
// form needs; a fresh unit answers codeRequired:false.
static void handleWifiCode() {
  if (!fromSetupNetwork()) {
    sendJson(*srv, 403, "{\"error\":\"forbidden\"}");
    return;
  }
  if (!ctx.publicReqs.allow(millis())) {
    sendJson(*srv, 429, "{\"error\":\"slow down\"}");
    return;
  }
  if (!requireJson(*srv)) return;
  if (!wifiCodeNeeded()) {
    sendJson(*srv, 200, "{\"ok\":true,\"codeRequired\":false}");
    return;
  }
  const uint32_t now = millis();
  if (!openPresence(*srv, miblo::PresenceGate::Purpose::Wifi, now)) return;
  ctx.lastInteractionMs = now;
  sendJson(*srv, 200, "{\"ok\":true,\"codeRequired\":true}");
}

static void handleWifi() {
  if (!fromSetupNetwork()) {
    srv->send(403, F("text/plain"), F("forbidden"));
    return;
  }
  if (bodyTooLarge()) {
    srv->send(413, F("text/plain"), F("payload too large"));
    return;
  }
  if (wifiCodeNeeded()) {
    // A configured unit's open setup AP: only someone who can read its screen moves it.
    const Lang lang = pageLang(*srv);
    const uint32_t now = millis();
    if (ctx.presence.locked(now)) {
      char b[16];
      snprintf_P(b, sizeof(b), PSTR(" (%u s)"), (unsigned)((ctx.presence.lockRemainingMs(now) + 999) / 1000));
      messagePage(lang, tr(lang, S::WebFailed) + b);
      return;
    }
    if (!ctx.presence.check(miblo::PresenceGate::Purpose::Wifi, srv->arg(F("code")).c_str(), now)) {
      messagePage(lang, tr(lang, S::WebBadCode));
      return;
    }
    ctx.presence.close();  // used: the code leaves the screen
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

  // Queue first: the page's first poll must already see "connecting".
  net::submitCredentials(ssid.c_str(), pass.c_str(), millis());
  joinStatusPage(ctx.cfg.lang);
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

// The settings page's script (after `const C=<config>;const V=<{fw,board}|null>;const T=<texts>;`
// and kTzJs). It fills the fields from C, shows or hides the fields that depend on a toggle, and
// posts every setting at once (POST /settings); a refused field is highlighted. V is null on a
// paired gadget's locked page: C is empty and #all hidden until the on-screen code unlocks it, then
// the settings, name and version come from /settings-secret.
static const char kSetJs[] PROGMEM =
    "const $=k=>document.getElementById(k),J={'Content-Type':'application/json'};"
    // To change anything the browser must prove presence with the code on the gadget screen; the
    // session token is kept so the code is asked once, not on every save.
    "let TOK=null;try{TOK=localStorage.getItem('miblo_tok')}catch(e){}"
    "function hdr(){return TOK?{...J,'X-Miblo-Web':TOK}:{...J};}"
    "async function unlock(){const q=await fetch('/settings-code',{method:'POST',headers:J,body:'{}'}).catch(()=>null);"
    // Refused (another code on the screen, a lockout, or no answer): say so, never prompt in vain.
    "if(!q||q.status===429){const j=q?await q.json().catch(()=>({})):{};"
    "alert(T.failed+(j.retryAfter?' ('+j.retryAfter+' s)':''));return false;}"
    "for(let i=0;i<3;i++){const c=prompt(T.unlock);if(!c)return false;"
    "const r=await fetch('/settings-unlock',{method:'POST',headers:J,body:JSON.stringify({code:c})}).catch(()=>null);"
    "if(!r){alert(T.failed);return false;}"
    "if(r.ok){TOK=(await r.json()).token;try{localStorage.setItem('miblo_tok',TOK)}catch(e){}await loadSecret();return true;}"
    "if(r.status===429){const j=await r.json().catch(()=>({}));alert(T.failed+' ('+(j.retryAfter||60)+' s)');return false;}"
    "alert(T.ubad);}return false;}"
    // A POST that carries the token and, on 401, asks for the code once and retries.
    "async function areq(u,b){let r=await fetch(u,{method:'POST',headers:hdr(),body:b});"
    "if(r.status===401){if(!await unlock())return r;r=await fetch(u,{method:'POST',headers:hdr(),body:b});}return r;}"
    // Night times travel as minutes of the day; the page shows them as HH:MM.
    "const p2=n=>String(n).padStart(2,'0');"
    // A delay set through the API that the page doesn't offer (sleepMin 45, petMin 3) shows as the
    // nearest option (the longer one on a tie; "never" only for 0), never as a blank select.
    "function fill(){for(const k in C){const e=$(k);if(!e)continue;if(e.type==='checkbox')e.checked=C[k];"
    "else if(e.type==='time')e.value=p2(Math.floor(C[k]/60))+':'+p2(C[k]%60);else e.value=C[k];}"
    "for(const k of['petMin','sleepMin']){if(!(k in C))continue;const e=$(k),v=Number(C[k]);let b=null;"
    "for(const o of e.options){const n=Number(o.value);if((n===0)!==(v===0))continue;"
    "if(!b||Math.abs(n-v)<=Math.abs(Number(b.value)-v))b=o;}if(b)e.value=b.value;}}fill();"
    // Birthday: "MM-DD" in the config, a day and a month select on the page ("--" = not set).
    "for(const[id,n]of[['bd',31],['bm',12]]){const e=$(id);e.add(new Option('--',''));"
    "for(let i=1;i<=n;i++)e.add(new Option(String(i),p2(i)));}"
    // Owner and birthday are private: fetched only once the on-screen code unlocks the page.
    "let SEC=false;"
    // Never throws: no answer, a 503 (busy) or a bad reply say "failed, try again" on a locked page.
    "function nf(){if(!V&&!SEC)$('m').textContent=T.failed+'. '+T.again;}"
    "async function loadSecret(){const r=await fetch('/settings-secret',{headers:hdr()}).catch(()=>null);"
    "if(r&&r.status===401){TOK=null;try{localStorage.removeItem('miblo_tok')}catch(e){}return;}"
    "if(!r||!r.ok){nf();return;}const s=await r.json().catch(()=>null);if(!s){nf();return;}"
    // Locked page: the first unlock brings everything a paired gadget keeps from the LAN.
    "if(!V&&!SEC&&s.cfg){Object.assign(C,s.cfg);fill();FW=s.fw||'';BD=s.board||'';"
    "$('h').textContent=C.name||$('name').placeholder;"
    "$('m').textContent=T.ver+' '+FW+' \\u00b7 '+T.pc.replace('%u',s.paired);"
    "$('ub').hidden=true;$('all').hidden=false;tz();dep();rv();pd();}"
    "$('owner').value=s.owner||'';"
    "if(s.birthday){$('bm').value=s.birthday.slice(0,2);$('bd').value=s.birthday.slice(3);}SEC=true;}"
    "const lk=()=>{if(!V&&!SEC)$('ub').hidden=false;};if(TOK)loadSecret().then(lk);else lk();"
    // data-if="id": shown while that checkbox is on; data-if="id:value": while that select has it.
    "function dep(){for(const e of document.querySelectorAll('[data-if]')){const[k,v]=e.dataset.if.split(':'),"
    "x=$(k);e.hidden=v?x.value!==v:!x.checked;}}"
    // Sliders show their value (#<id>V).
    "function rv(){for(const e of document.querySelectorAll('input[type=range]'))$(e.id+'V').textContent=e.value+'%';}"
    "function st(t,c){const e=$('st');e.textContent=t;e.className=c||'';}"
    "document.addEventListener('input',rv);document.addEventListener('change',()=>{dep();st('');});"
    "document.addEventListener('keydown',e=>{if(e.key==='Enter'&&e.target.tagName==='INPUT')save();});"
    // The screen never turns off before pet mode starts: only screen-off delays longer than the pet
    // delay are offered (a stale choice moves to the next longer one).
    "function pd(){const p=Number($('petMin').value),s=$('sleepMin');"
    "for(const o of s.options)o.disabled=o.value!=='0'&&Number(o.value)<=p;"
    "const c=s.selectedOptions[0];"
    "if(!c||c.disabled)s.value=[...s.options].find(o=>!o.disabled&&o.value!=='0')?.value||'0';}"
    "$('petMin').addEventListener('change',pd);"
    "dep();rv();pd();"
    "function val(k){const e=$(k);if(e.type==='time'){const t=e.value.split(':');return t.length<2?C[k]:Number(t[0])*60+Number(t[1]);}"
    "return e.type==='checkbox'?e.checked:(e.type==='number'||e.type==='range')?Number(e.value):e.value;}"
    "function save(){if(!V&&!SEC)return;for(const e of document.querySelectorAll('.bad'))e.classList.remove('bad');"
    "const b={};for(const k of ['mode','brightness','alerts','heroPermSec','heroDoneSec',"
    "'reminderMin','flashBlinks','discreet','rotate','rotateEverySec','rotateShowSec','night','nightFrom','nightTo',"
    "'nightBrightness','mascot','petMin','sleepMin','name','friends','friendsSide','tz','lang']){let v=val(k);if(k==='tz'&&!v)continue;"
    "if(k==='mascot'||k==='petMin'||k==='sleepMin'||k==='flashBlinks'||k==='friendsSide')v=Number(v);b[k]=v;}"
    "if(SEC){b.owner=val('owner');b.birthday=$('bd').value&&$('bm').value?$('bm').value+'-'+$('bd').value:'';}st('...');"
    "areq('/settings',JSON.stringify(b))"
    ".then(r=>r.json().catch(()=>({})).then(j=>{"
    "if(r.ok){const re=b.lang!==C.lang;Object.assign(C,b);$('h').textContent=b.name||$('name').placeholder;"
    "st(T.saved,'ok');if(re)location.reload();return;}"
    // Refused: show and highlight the field the device named (it may sit in a hidden block).
    "const e=$(j.field==='birthday'?'bd':j.field||'');if(!e){st(T.failed,'no');return;}"
    "for(let p=e.closest('[hidden]');p;p=p.parentElement.closest('[hidden]'))p.hidden=false;"
    "e.classList.add('bad');e.scrollIntoView({block:'center'});st(T.failed+'. '+T.chk,'no');"
    "})).catch(()=>st(T.failed,'no'));}"
    "function post(u){return areq(u,'{}');}"
    "function rst(){post('/reset-code').then(r=>{if(!r.ok)return;const c=prompt(T.hint);if(!c)return;"
    "fetch('/factory-reset?code='+encodeURIComponent(c),{method:'POST',headers:J,body:'{}'})"
    ".then(r=>{if(!r.ok)alert(T.bad);});});}"
    // Check for updates: the browser asks GitHub (the gadget has no HTTPS to spare) and
    // compares with this firmware; a newer one gets the how-to and a link to its .bin.
    "let FW=V?V.fw:'',BD=V?V.board:'';"
    "function vc(a,b){a=a.split('.').map(Number);b=b.split('.').map(Number);"
    "for(let i=0;i<3;i++){const d=(a[i]||0)-(b[i]||0);if(d)return d;}return 0;}"
    "function chk(){const st=$('up');st.textContent='...';"
    "fetch('https://api.github.com/repos/" MIBLO_REPO "/releases/latest',{cache:'no-store'})"
    ".then(r=>{if(!r.ok)throw 0;return r.json();}).then(j=>{"
    "const v=String(j.tag_name||'').replace(/^v/,'');if(!/^\\d+\\.\\d+\\.\\d+$/.test(v))throw 0;"
    "if(vc(v,FW)<=0){st.textContent=T.uptodate.replace('%s',FW);return;}"
    "st.textContent='';const b=document.createElement('b');b.textContent=T.newver.replace('%s',v);"
    "st.append(b,' '+T.how);"
    "const a=(j.assets||[]).find(x=>x.name==='miblo-'+BD+'-'+v+'.bin');"
    "if(a){const l=document.createElement('a');l.href=a.browser_download_url;l.textContent=T.dl;"
    "st.append(document.createElement('br'),l);}"
    "}).catch(()=>{st.textContent=T.chkfail;});}"
    "function tz(){tzFill($('tzr'),$('tz'),C.tz,ch=>{if(ch)save();});}if(V)tz();";

// Settings page builders; every text is escaped.
static void text(String& out, Lang lang, S id) { appendEscaped(out, tr(lang, id).c_str()); }

// <label for="id">text</label>
static void label(String& out, Lang lang, S id, const __FlashStringHelper* forId) {
  out += F("<label for=\"");
  out += forId;
  out += F("\">");
  text(out, lang, id);
  out += F("</label>");
}

// A switch row: the whole line toggles the checkbox.
static void toggle(String& out, Lang lang, S id, const __FlashStringHelper* key) {
  out += F("<label class=\"t\"><span>");
  text(out, lang, id);
  out += F("</span><input type=\"checkbox\" id=\"");
  out += key;
  out += F("\"></label>");
}

// A labelled number input (wrapped in a <div> so two of them sit side by side in a .g grid).
static void number(String& out, Lang lang, S id, const __FlashStringHelper* key, int lo, int hi) {
  out += F("<div>");
  label(out, lang, id, key);
  out += F("<input type=\"number\" inputmode=\"numeric\" id=\"");
  out += key;
  out += F("\" min=\"");
  out += lo;
  out += F("\" max=\"");
  out += hi;
  out += F("\"></div>");
}

// A labelled 1..100 % slider whose value shows at the label's right (#<key>V).
static void slider(String& out, Lang lang, S id, const __FlashStringHelper* key, int lo) {
  out += F("<label for=\"");
  out += key;
  out += F("\">");
  text(out, lang, id);
  out += F("<span class=\"v\" id=\"");
  out += key;
  out += F("V\"></span></label><input type=\"range\" id=\"");
  out += key;
  out += F("\" min=\"");
  out += lo;
  out += F("\" max=\"100\">");
}

static void option(String& out, Lang lang, const __FlashStringHelper* value, S id) {
  out += F("<option value=\"");
  out += value;
  out += F("\">");
  text(out, lang, id);
  out += F("</option>");
}

static void settingsPage() {
  Lang lang = pageLang(*srv);
  String out;
  // A paired gadget tells the LAN only its id: its name, version and settings reach a browser
  // only after the code on its screen unlocks the page (the script then reads /settings-secret).
  // Before pairing everything is shown, as setup needs it.
  const bool open = ctx.tokens.count() == 0;
  const char* title = open ? deviceName() : ctx.ident.defaultName;
  pageStart(out, lang, title);
  out += F("<h1 id=\"h\">");
  appendEscaped(out, title);
  out += F("</h1><p class=\"m\" id=\"m\">");
  if (open) {
    text(out, lang, S::WebVersion);
    out += F(" " MIBLO_FW_VERSION " &middot; ");
    char line[96];
    snprintf(line, sizeof(line), tr(lang, S::WebPairedCount).c_str(), (unsigned)ctx.tokens.count());
    appendEscaped(out, line);
  }
  out += F("</p>");
  if (open && !ctx.usageEverSeen) {
    out += F("<p class=\"w\">");
    text(out, lang, S::WebLimitsHint);
    out += F("</p>");
  }
  // Locked page: only this until the code is typed.
  out += F("<div id=\"ub\" hidden><p class=\"w\">");
  text(out, lang, S::WebUnlock);
  out += F("</p><button onclick=\"unlock()\">");
  text(out, lang, S::WebUnlockTitle);
  out += F("</button></div><div id=\"all\"");
  if (!open) out += F(" hidden");
  out += F(">");

  // Screen: what it shows and how.
  out += F("<div class=\"c\"><h2>");
  text(out, lang, S::WebSecScreen);
  out += F("</h2>");
  label(out, lang, S::WebMode, F("mode"));
  out += F("<select id=\"mode\">");
  option(out, lang, F("overview"), S::ModeOverview);
  option(out, lang, F("limits"), S::ModeLimits);
  option(out, lang, F("sessions"), S::ModeSessions);
  out += F("</select><div data-if=\"mode:overview\">");  // rotation only applies to Overview
  toggle(out, lang, S::WebRotate, F("rotate"));
  out += F("<div class=\"g\" data-if=\"rotate\">");
  number(out, lang, S::WebRotateEvery, F("rotateEverySec"), 10, 3600);
  number(out, lang, S::WebRotateShow, F("rotateShowSec"), 3, 300);
  out += F("</div></div>");
  pageFlush(out);
  slider(out, lang, S::WebBrightness, F("brightness"), 5);
  label(out, lang, S::WebMascot, F("mascot"));
  out += F("<select id=\"mascot\">");
  static const S kStyles[] = {S::WebMascotSphynx, S::WebMascotOrange, S::WebMascotBlack, S::WebMascotGrey};
  static_assert(sizeof(kStyles) / sizeof(kStyles[0]) == miblo::kMascotStyles, "one name per mascot style");
  for (uint8_t i = 0; i < miblo::kMascotStyles; i++) {
    out += F("<option value=\"");
    out += i;
    out += F("\">");
    text(out, lang, kStyles[i]);
    out += F("</option>");
  }
  out += F("</select>");
  label(out, lang, S::WebPetAfter, F("petMin"));
  out += F("<select id=\"petMin\"><option value=\"1\">1 min</option><option value=\"2\">2 min</option>"
           "<option value=\"5\">5 min</option><option value=\"10\">10 min</option>"
           "<option value=\"15\">15 min</option><option value=\"20\">20 min</option>"
           "<option value=\"25\">25 min</option><option value=\"30\">30 min</option>"
           "<option value=\"40\">40 min</option><option value=\"50\">50 min</option>"
           "<option value=\"60\">1 h</option></select>");
  label(out, lang, S::WebSleep, F("sleepMin"));
  out += F("<select id=\"sleepMin\">");
  option(out, lang, F("0"), S::WebSleepNever);
  out += F("<option value=\"15\">15 min</option><option value=\"30\">30 min</option>"
           "<option value=\"60\">1 h</option><option value=\"120\">2 h</option><option value=\"240\">4 h</option>"
           "</select>");
  toggle(out, lang, S::WebDiscreet, F("discreet"));
  out += F("</div>");
  pageFlush(out);

  // Alerts: the switch heads the card; its details only while it is on.
  out += F("<div class=\"c\">");
  toggle(out, lang, S::WebAlerts, F("alerts"));
  out += F("<div data-if=\"alerts\">");
  label(out, lang, S::WebFlashBlinks, F("flashBlinks"));
  out += F("<select id=\"flashBlinks\"><option>2</option><option>3</option><option>4</option>"
           "<option>5</option></select><div class=\"g\">");
  number(out, lang, S::WebHeroPerm, F("heroPermSec"), 3, 60);
  number(out, lang, S::WebHeroDone, F("heroDoneSec"), 2, 60);
  out += F("</div>");
  number(out, lang, S::WebReminder, F("reminderMin"), 0, 30);
  out += F("</div></div>");
  pageFlush(out);

  // Night mode, same pattern.
  out += F("<div class=\"c\">");
  toggle(out, lang, S::WebNight, F("night"));
  out += F("<div data-if=\"night\"><div class=\"g\"><div>");
  label(out, lang, S::WebNightFrom, F("nightFrom"));
  out += F("<input id=\"nightFrom\" type=\"time\" required></div><div>");
  label(out, lang, S::WebNightTo, F("nightTo"));
  out += F("<input id=\"nightTo\" type=\"time\" required></div></div>");
  slider(out, lang, S::WebNightBrightness, F("nightBrightness"), 1);
  out += F("</div></div>");
  pageFlush(out);

  // About you.
  out += F("<div class=\"c\"><h2>");
  text(out, lang, S::WebSecYou);
  out += F("</h2>");
  label(out, lang, S::WebOwner, F("owner"));
  out += F("<input id=\"owner\" maxlength=\"20\" autocomplete=\"given-name\">");
  // Day and month, two selects (the year is never asked); filled and read by the script.
  label(out, lang, S::WebBirthday, F("bd"));
  out += F("<div class=\"r\"><select id=\"bd\"></select><select id=\"bm\"></select></div></div>");
  pageFlush(out);

  // This device.
  out += F("<div class=\"c\"><h2>");
  text(out, lang, S::WebSecDevice);
  out += F("</h2>");
  label(out, lang, S::WebDeviceName, F("name"));
  out += F("<input id=\"name\" maxlength=\"20\" autocomplete=\"off\" placeholder=\"");
  appendEscaped(out, ctx.ident.defaultName);
  out += F("\"><label for=\"tz\">");
  text(out, lang, S::WebTimezone);
  out += F("<span class=\"v\" id=\"tzn\"></span></label><div class=\"r\"><select id=\"tzr\"></select>"
           "<select id=\"tz\"></select></div>");
  label(out, lang, S::WebLanguage, F("lang"));
  out += F("<select id=\"lang\">");
  langOptions(out, lang, true);
  out += F("</select>");
  toggle(out, lang, S::WebFriends, F("friends"));
  // Where the other Miblos stand: our cat leaves that way to visit them (only with visits on).
  out += F("<div data-if=\"friends\">");
  label(out, lang, S::WebFriendsSide, F("friendsSide"));
  out += F("<select id=\"friendsSide\">");
  static const S kSides[] = {S::WebSideRight, S::WebSideLeft, S::WebSideUp, S::WebSideDown};
  for (uint8_t i = 0; i < 4; i++) {
    out += F("<option value=\"");
    out += i;
    out += F("\">");
    text(out, lang, kSides[i]);
    out += F("</option>");
  }
  out += F("</select></div></div>");
  pageFlush(out);

  // Advanced (collapsed): firmware, pairing code, factory reset.
  out += F("<details class=\"c\"><summary>");
  text(out, lang, S::WebAdvanced);
  out += F("</summary><h2>");
  text(out, lang, S::WebFirmware);
  out += F("</h2><button class=\"s\" onclick=\"chk()\">");
  text(out, lang, S::WebCheckUpdates);
  out += F("</button><p id=\"up\"></p><p><a href=\"/update\">");
  text(out, lang, S::WebFirmware);
  out += F("</a></p><button class=\"s\" onclick=\"post('/pair-code')\">");
  text(out, lang, S::WebShowPairCode);
  out += F("</button><h2>");
  text(out, lang, S::WebFactoryReset);
  out += F("</h2><p class=\"m\">");
  text(out, lang, S::WebResetConfirm);
  out += F("</p><button class=\"d\" onclick=\"rst()\">");
  text(out, lang, S::WebFactoryReset);
  out += F("</button></details>");
  pageFlush(out);

  // Always-visible save bar.
  out += F("<div class=\"bar\"><div><span id=\"st\"></span><button onclick=\"save()\">");
  text(out, lang, S::WebSave);
  out += F("</button></div></div></div><script>const C=");
  pageFlush(out);

  DynamicJsonDocument cfg(1024);
  if (open) miblo::configToJson(ctx.cfg, cfg.to<JsonObject>(), false);  // no owner/birthday here
  else cfg.to<JsonObject>();                                             // locked: nothing
  appendJsonForScript(out, cfg);
  out += open ? F(";const V={\"fw\":\"" MIBLO_FW_VERSION "\",\"board\":\"" MIBLO_BOARD_NAME "\"};const T=")
              : F(";const V=null;const T=");
  DynamicJsonDocument txt(2048);
  txt["saved"] = tr(lang, S::WebSaved);
  txt["uptodate"] = tr(lang, S::WebUpToDate);
  txt["newver"] = tr(lang, S::WebNewVersion);
  txt["how"] = tr(lang, S::WebUpdateHow);
  txt["chkfail"] = tr(lang, S::WebCheckFailed);
  txt["dl"] = tr(lang, S::WebDownloadBin);
  txt["failed"] = tr(lang, S::WebFailed);
  txt["hint"] = tr(lang, S::WebCodeHint);
  txt["bad"] = tr(lang, S::WebBadCode);
  txt["chk"] = tr(lang, S::WebCheckField);
  txt["unlock"] = tr(lang, S::WebUnlock);
  txt["utitle"] = tr(lang, S::WebUnlockTitle);
  txt["ubad"] = tr(lang, S::WebUnlockBad);
  txt["ver"] = tr(lang, S::WebVersion);
  txt["pc"] = tr(lang, S::WebPairedCount);
  txt["again"] = tr(lang, S::WebTryAgain);
  appendJsonForScript(out, txt);
  out += F(";");
  pageSendP(out, kTzJs);
  pageSendP(out, kSetJs);
  out += F("</script>");
  pageEnd(out);
}

// A state-changing web request is authorized by a paired computer's bearer token (the plugin) or
// by a web session the browser earned with the on-screen code. Everyone else on the LAN is
// refused: an unpaired prankster cannot change anything.
static bool webAuthorized() {
  char token[40];
  const String auth = requestHeader(*srv, F("Authorization"));  // never the previous request's
  if (miblo::bearerToken(auth.c_str(), token, sizeof(token)) && ctx.tokens.matches(token)) return true;
  const String web = requestHeader(*srv, F("X-Miblo-Web"));
  return ctx.webSession.valid(web.c_str(), millis());
}

// POST /settings-code: show a code on the gadget screen so the person at the keyboard can prove
// they are the one in front of it. Unauthenticated by design (this bootstraps the web session),
// but rate-limited and protected by the gate's escalating lockout on wrong codes.
static void handleSettingsCode() {
  if (!ctx.publicReqs.allow(millis())) {
    sendJson(*srv, 429, "{\"error\":\"slow down\"}");
    return;
  }
  if (!requireJson(*srv)) return;
  const uint32_t now = millis();
  if (!openPresence(*srv, miblo::PresenceGate::Purpose::Settings, now)) return;
  ctx.lastInteractionMs = now;  // keep the screen on so the code is readable
  sendJson(*srv, 200, "{\"ok\":true}");
}

// POST /settings-unlock {code}: on the right code, issue a web session token the browser keeps.
static void handleSettingsUnlock() {
  if (!requireJson(*srv)) return;
  const uint32_t now = millis();
  if (ctx.presence.locked(now)) {
    sendLocked(*srv, ctx.presence.lockRemainingMs(now));
    return;
  }
  // The page sends {"code":"1234"} as JSON (a ?code= argument also works).
  char code[8] = "";
  StaticJsonDocument<96> doc;
  if (!deserializeJson(doc, srv->arg(F("plain")))) {
    JsonVariantConst c = doc["code"];
    if (c.is<const char*>()) strlcpy(code, c.as<const char*>(), sizeof(code));
    else if (c.is<int>()) snprintf_P(code, sizeof(code), PSTR("%04d"), c.as<int>());
  }
  if (!code[0]) strlcpy(code, srv->arg(F("code")).c_str(), sizeof(code));
  if (!ctx.presence.check(miblo::PresenceGate::Purpose::Settings, code, now)) {
    if (ctx.presence.locked(now)) sendLocked(*srv, ctx.presence.lockRemainingMs(now));
    else sendJson(*srv, 403, "{\"error\":\"bad code\"}");
    return;
  }
  ctx.presence.close();  // unlocked: the code leaves the screen at once
  uint8_t rnd[16];
  for (int i = 0; i < 16; i += 4) {
    uint32_t r = hwRandom();
    memcpy(rnd + i, &r, 4);
  }
  ctx.webSession.issue(rnd, now);
  char token[33];
  miblo::makeToken(rnd, token);
  String out = String(F("{\"token\":\"")) + token + F("\"}");
  sendJson(*srv, 200, out.c_str());
}

// GET /settings-secret, only for an unlocked session: the private fields (owner, birthday) and,
// for a paired gadget's locked page, the settings, version, board and number of paired computers.
static void handleSettingsSecret() {
  if (!webAuthorized()) {
    sendJson(*srv, 401, "{\"error\":\"unauthorized\"}");
    return;
  }
  if (heapLowForRequest(2048)) {
    sendJson(*srv, 503, "{\"error\":\"busy\"}");
    return;
  }
  DynamicJsonDocument doc(1536);
  doc["owner"] = ctx.cfg.owner;
  doc["birthday"] = ctx.cfg.birthday;
  miblo::configToJson(ctx.cfg, doc.createNestedObject("cfg"), false);
  doc["fw"] = MIBLO_FW_VERSION;
  doc["board"] = MIBLO_BOARD_NAME;
  doc["paired"] = ctx.tokens.count();
  String out;
  serializeJson(doc, out);
  sendJson(*srv, 200, out.c_str());
}

static void handleSettings() {
  if (!webAuthorized()) {
    sendJson(*srv, 401, "{\"error\":\"unauthorized\"}");
    return;
  }
  ctx.lastInteractionMs = millis();
  if (!requireJson(*srv)) return;
  if (bodyTooLarge() || srv->arg(F("plain")).length() > 1024) {
    sendJson(*srv, 413, "{\"error\":\"too large\"}");
    return;
  }
  if (heapLowForRequest(1024)) {
    sendJson(*srv, 503, "{\"error\":\"busy\"}");
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
  if (!ctx.publicReqs.allow(millis())) {  // flood guard (unauthenticated page render)
    sendJson(*srv, 429, "{\"error\":\"slow down\"}");
    return;
  }
  if (heapLowForRequest(9216)) {  // rendering the page needs several KB: never risk a crash
    sendJson(*srv, 503, "{\"error\":\"busy\"}");
    return;
  }
  ctx.lastInteractionMs = millis();  // someone is looking: wake the screen
  if (net::apActive() && !net::connected()) {
    // No network scan while a submitted network is being tried: a scan in the middle of the
    // station's join attempt can abort it. Phones re-probe the captive portal all the time.
    if (net::trialBusy()) joinStatusPage(pageLang(*srv));
    else portalPage();
  } else {
    settingsPage();
  }
}

static void handlePairCode() {
  if (!webAuthorized()) {
    sendJson(*srv, 401, "{\"error\":\"unauthorized\"}");
    return;
  }
  if (!requireJson(*srv)) return;
  ctx.showPairCode = true;
  ctx.pairCodeAtMs = millis();
  sendJson(*srv, 200, "{\"ok\":true}");
}

static void handleResetCode() {
  if (!webAuthorized()) {
    sendJson(*srv, 401, "{\"error\":\"unauthorized\"}");
    return;
  }
  if (!requireJson(*srv)) return;
  if (!openPresence(*srv, miblo::PresenceGate::Purpose::Reset, millis())) return;
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
  const bool haveLen = miblo::findContentLength(client->peekBuffer(), client->peekAvailable(), len);
  if (haveLen && len > kMaxPostBody) {
    static const char kReply[] PROGMEM =
        "HTTP/1.1 413 Payload Too Large\r\nContent-Type: application/json\r\nConnection: close\r\n"
        "Content-Length: 21\r\n\r\n{\"error\":\"too large\"}";
    client->print(FPSTR(kReply));
    return ESP8266WebServer::CLIENT_MUST_STOP;
  }
  // Before buffering the body in RAM: if that would leave the Wi-Fi stack short, refuse now (503)
  // so the buffering itself can never starve it. The plugin then resends a smaller (alerts-only)
  // snapshot, which fits.
  if (haveLen && len > 512 && heapLowForRequest(len)) {
    static const char kBusy[] PROGMEM =
        "HTTP/1.1 503 Service Unavailable\r\nContent-Type: application/json\r\nConnection: close\r\n"
        "Content-Length: 16\r\n\r\n{\"error\":\"busy\"}";
    client->print(FPSTR(kBusy));
    return ESP8266WebServer::CLIENT_MUST_STOP;
  }
  return ESP8266WebServer::CLIENT_REQUEST_CAN_CONTINUE;
}
#endif

void begin(WebServerT& server) {
  srv = &server;
#if defined(ESP8266)
  server.addHook(limitPostBody);
#endif
  // Content-Length: OTA progress (ota.cpp) and the body-size checks above; Authorization: the API
  // (api.cpp); Content-Type: the CSRF check on the pages' state-changing POSTs (requireJson).
  server.collectHeaders("Accept-Language", "Authorization", "Content-Length", "Content-Type", "X-Miblo-Web");
  server.on(F("/"), HTTP_GET, handleRoot);
  server.on(F("/wifi"), HTTP_POST, handleWifi);
  server.on(F("/wifi-code"), HTTP_POST, handleWifiCode);
  server.on(F("/settings"), HTTP_POST, handleSettings);
  server.on(F("/settings-code"), HTTP_POST, handleSettingsCode);
  server.on(F("/settings-unlock"), HTTP_POST, handleSettingsUnlock);
  server.on(F("/settings-secret"), HTTP_GET, handleSettingsSecret);
  server.on(F("/api/zones"), HTTP_GET, handleZones);
  server.on(F("/api/wifi-status"), HTTP_GET, handleWifiStatus);
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
