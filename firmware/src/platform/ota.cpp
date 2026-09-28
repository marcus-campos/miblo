#include "ota.h"

#include <ArduinoJson.h>

#include "../context.h"
#include "../web.h"
#include "miblo_version.h"
#include "net.h"

namespace ota {

using miblo::Lang;
using miblo::PresenceGate;
using miblo::S;

static WebServerT* srv = nullptr;
static ProgressHook hook = nullptr;
// Per-request upload state. Safe defaults: nothing is accepted unless UPLOAD_FILE_START ran for
// the current request; everything is reset at the end of done() and on an aborted upload.
static bool uploadRan = false;
static bool rejected = true;
static bool started = false;
static bool endedOk = false;  // Update.end(true) succeeded (image written and verified)
static size_t expected = 0;
static uint8_t lastPct = 255;

// Evaluated per request: codeless only for an unconfigured unit (no saved Wi-Fi, no pairings)
// reached over its own setup AP (see miblo::otaCodeRequired).
static bool codeRequired() {
  const bool hasWifiCreds = WiFi.SSID().length() > 0;  // SDK station config, as in net::begin
  const bool viaSoftAp = net::apActive() && srv->client().localIP() == WiFi.softAPIP();
  return miblo::otaCodeRequired(hasWifiCreds, ctx.tokens.count(), viaSoftAp);
}

static void resetState() {
  uploadRan = false;
  rejected = true;
  started = false;
  endedOk = false;
}

// POST /update/open (JSON, body {}): opens the presence gate so the screen shows the code.
// A plain GET never changes state (a link or <img> from another site cannot light up the code).
static void openGate() {
  if (!web::requireJson(*srv)) return;  // 415: CSRF guard
  if (!codeRequired()) {
    // Unconfigured unit on its own AP: no gate, no code on screen.
    web::sendJson(*srv, 200, "{\"ok\":true,\"codeRequired\":false}");
    return;
  }
  const uint32_t now = millis();
  if (ctx.presence.locked(now)) {
    web::sendLocked(*srv, ctx.presence.lockRemainingMs(now));
    return;
  }
  if (!ctx.presence.active(now) || ctx.presence.purpose() != PresenceGate::Purpose::Update) {
    char code[5];
    miblo::formatCode(hwRandom(), code);
    ctx.presence.open(PresenceGate::Purpose::Update, code, now);  // the screen now shows the code
  }
  web::sendJson(*srv, 200, "{\"ok\":true,\"codeRequired\":true}");
}

// GET /update: only serves the page; its script opens the gate with POST /update/open.
static void page() {
  Lang lang = web::pageLang(*srv);
  String out;
  web::pageStart(out, lang, web::tr(lang, S::WebFirmware).c_str());
  out += F("<h1>");
  web::appendEscaped(out, web::tr(lang, S::WebFirmware).c_str());
  out += F("</h1><p class=\"m\">");
  web::appendEscaped(out, web::tr(lang, S::WebVersion).c_str());
  out += F(" " MIBLO_FW_VERSION "</p><label id=\"cl\">");
  web::appendEscaped(out, web::tr(lang, S::WebCodeHint).c_str());
  out += F("</label><input id=\"code\" inputmode=\"numeric\" maxlength=\"4\" autocomplete=\"off\"><label>.bin</label>"
           "<input id=\"f\" type=\"file\" accept=\".bin\"><button onclick=\"up()\">");
  web::appendEscaped(out, web::tr(lang, S::WebUpload).c_str());
  out += F("</button><p id=\"st\" class=\"m\"></p><script>const T=");
  StaticJsonDocument<512> t;
  t["ok"] = web::tr(lang, S::WebUpdateOk);
  t["bad"] = web::tr(lang, S::WebBadCode);
  t["failed"] = web::tr(lang, S::WebFailed);
  serializeJson(t, out);
  out += F(";let nc=0;const $=k=>document.getElementById(k);"
           "fetch('/update/open',{method:'POST',headers:{'Content-Type':'application/json'},body:'{}'})"
           ".then(r=>{if(r.status===429)return r.json().then(j=>{$('st').textContent=T.failed+' ('+j.retryAfter+' s)';});"
           "if(!r.ok){$('st').textContent=T.failed;return;}"
           "return r.json().then(j=>{if(j.codeRequired===false){nc=1;$('cl').style.display=$('code').style.display='none';}});})"
           ".catch(()=>{$('st').textContent=T.failed;});"
           "function up(){const f=$('f').files[0];if(!f)return;const d=new FormData();d.append('firmware',f);"
           "$('st').textContent='...';"
           "fetch(nc?'/update':'/update?code='+encodeURIComponent($('code').value),{method:'POST',body:d})"
           ".then(r=>r.text().then(x=>{$('st').textContent=r.ok?T.ok:(r.status===403?T.bad:T.failed+': '+x);}))"
           ".catch(()=>{$('st').textContent=T.failed;});}</script>");
  web::pageEnd(out);
  srv->send(200, F("text/html; charset=utf-8"), out);
}

static void upload() {
  HTTPUpload& up = srv->upload();
  if (up.status == UPLOAD_FILE_START) {
    uploadRan = true;
    // Codeless only for an unconfigured unit on its own AP; otherwise the code check (and its
    // escalating lockout) applies exactly as before.
    rejected = codeRequired() &&
               !ctx.presence.check(PresenceGate::Purpose::Update, srv->arg(F("code")).c_str(), millis());
    started = false;
    endedOk = false;
    if (rejected) return;
    expected = (size_t)srv->header(F("Content-Length")).toInt();  // includes the multipart envelope
#if defined(ESP8266)
    uint32_t maxSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    started = Update.begin(maxSpace, U_FLASH);
#else
    started = Update.begin(UPDATE_SIZE_UNKNOWN);
#endif
    ctx.updating = started;
    ctx.updatePct = 0;
    lastPct = 255;
    if (hook) hook(0);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (rejected || !started) return;
    if (Update.write(up.buf, up.currentSize) != up.currentSize) return;
    uint8_t pct = expected ? (uint8_t)min<size_t>(100, up.totalSize * 100 / expected) : 0;
    if (pct != lastPct) {
      lastPct = pct;
      ctx.updatePct = pct;
      if (hook) hook(pct);
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (rejected || !started) return;
    endedOk = Update.end(true);
    if (endedOk && hook) hook(100);  // never show 100% for an image that failed to verify
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (started) Update.end(false);
    ctx.updating = false;
    resetState();
  }
}

static void done() {
  const uint32_t now = millis();
  if (!uploadRan) {
    // No multipart file part in THIS request: the code was never checked, nothing was written.
    if (ctx.presence.locked(now)) web::sendLocked(*srv, ctx.presence.lockRemainingMs(now));
    else web::sendJson(*srv, 400, "{\"error\":\"no firmware file\"}");
  } else if (rejected) {
    if (ctx.presence.locked(now)) web::sendLocked(*srv, ctx.presence.lockRemainingMs(now));
    else web::sendJson(*srv, 403, "{\"error\":\"bad code\"}");
  } else if (!started || !endedOk || Update.hasError()) {
    ctx.updating = false;
    String err = Update.getErrorString();
    if (started && !endedOk && Update.isRunning()) Update.end(false);  // drop a half-written image
    srv->send(500, F("text/plain"), err.length() ? err : String(F("update failed")));
    // the previous image stays valid
  } else {
    // Success: keep ctx.updating set so the progress screen stays up until the reboot.
    ctx.presence.close();
    srv->send(200, F("text/plain"), F("OK"));
    ctx.rebootRequested = true;
    ctx.rebootAtMs = now + 800;
  }
  resetState();
}

void begin(WebServerT& server, ProgressHook onProgress) {
  srv = &server;
  hook = onProgress;
  server.on(F("/update"), HTTP_GET, page);
  server.on(F("/update/open"), HTTP_POST, openGate);
  server.on(F("/update"), HTTP_POST, done, upload);
}

}  // namespace ota
