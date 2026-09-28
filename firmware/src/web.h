#pragma once
#include "platform/platform.h"

#include "miblo_i18n.h"

// Pages for humans (localized): the Wi-Fi captive portal and the settings page.
namespace web {

void begin(WebServerT& server);

// Helpers reused by the update page (ota.cpp).
miblo::Lang pageLang(WebServerT& server);
// reserveHint: expected page size, so the String doesn't reallocate mid-build (~1500 for the
// small pages, 7000 for the settings page).
void pageStart(String& out, miblo::Lang lang, const char* title, size_t reserveHint = 1500);
void pageEnd(String& out);
void appendEscaped(String& out, const char* s);
String tr(miblo::Lang lang, miblo::S id);
void sendJson(WebServerT& server, int code, const char* json);
// 429 {"error":"locked","retryAfter":<s>} for a locked presence gate or pairing guard.
void sendLocked(WebServerT& server, uint32_t remainingMs);

}  // namespace web
