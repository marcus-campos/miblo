#pragma once
#include "platform/platform.h"

#include "miblo_i18n.h"
#include "miblo_security.h"

// Pages for humans (localized): the Wi-Fi captive portal and the settings page.
namespace web {

void begin(WebServerT& server);

// Helpers reused by the update page (ota.cpp).
miblo::Lang pageLang(WebServerT& server);
// Pages are streamed as a chunked response, so a page never sits whole in the heap:
// pageStart() sends the headers and the <head>; pageFlush() sends what `out` holds once it
// passes kPageChunk bytes (always with force); pageSendP() streams a PROGMEM blob straight from
// flash; pageEnd() sends the rest and ends the response. Nothing else may be sent after it.
constexpr size_t kPageChunk = 1024;
void pageStart(String& out, miblo::Lang lang, const char* title);
void pageFlush(String& out, bool force = false);
void pageSendP(String& out, PGM_P blob);
void pageEnd(String& out);
void appendEscaped(String& out, const char* s);
String tr(miblo::Lang lang, miblo::S id);
void sendJson(WebServerT& server, int code, const char* json);
// Same, for a fixed reply kept in flash: sendJson(server, 404, F("{\"error\":\"not found\"}")).
void sendJson(WebServerT& server, int code, const __FlashStringHelper* json);
// CSRF guard for the pages' state-changing POSTs: a cross-site <form> cannot send
// application/json without a CORS preflight (which this server never answers). false → 415 sent.
bool requireJson(WebServerT& server);
// A request header, safe to use as a C string. ESP8266WebServer "clears" the headers a request did
// not send with String::clear(), which only sets the length to 0: header(...).c_str() would still
// read the PREVIOUS request's value (the plugin's bearer token, say). A copy holds only `length()`
// bytes, NUL-terminated. Never call c_str()/toInt() on server.header() directly.
inline String requestHeader(WebServerT& server, const __FlashStringHelper* name) { return String(server.header(name)); }
// 429 {"error":"locked","retryAfter":<s>} for a locked presence gate or pairing guard.
void sendLocked(WebServerT& server, uint32_t remainingMs);
// Opens the presence gate for `p` with a fresh code on the screen (an active code for the same
// purpose is kept). `trusted`: the caller is authorised (requestAuthorized, or the setup AP) and
// replaces a code opened anonymously (F4). On a lockout answers 429 {"error":"locked",retryAfter};
// while another purpose's code is still on the screen, or an anonymous caller asks again too soon
// (PresenceGate::kAnonGapMs), 429 {"error":"busy",retryAfter}. true when the code for `p` is now on
// the screen.
bool openPresence(WebServerT& server, miblo::PresenceGate::Purpose p, uint32_t nowMs, bool trusted);
// A paired computer's bearer token, or a web session the browser earned with the on-screen code.
bool requestAuthorized();

}  // namespace web
