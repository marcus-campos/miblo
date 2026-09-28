#pragma once
#include "platform/platform.h"

#include "miblo_i18n.h"

// Páginas para humanos (localizadas): portal cativo de Wi-Fi e página de configuração.
namespace web {

void begin(WebServerT& server);

// Helpers reutilizados pela página de update (ota.cpp).
miblo::Lang pageLang(WebServerT& server);
void pageStart(String& out, miblo::Lang lang, const char* title);
void pageEnd(String& out);
void appendEscaped(String& out, const char* s);
String tr(miblo::Lang lang, miblo::S id);
void sendJson(WebServerT& server, int code, const char* json);

}  // namespace web
