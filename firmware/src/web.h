#pragma once
#include "platform/platform.h"

#include "miblo_i18n.h"

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
// CSRF guard for the pages' state-changing POSTs: a cross-site <form> cannot send
// application/json without a CORS preflight (which this server never answers). false → 415 sent.
bool requireJson(WebServerT& server);
// 429 {"error":"locked","retryAfter":<s>} for a locked presence gate or pairing guard.
void sendLocked(WebServerT& server, uint32_t remainingMs);

}  // namespace web
