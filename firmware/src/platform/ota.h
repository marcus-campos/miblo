#pragma once
#include "platform.h"

// /update is always available (including on the setup network). GET serves the page; the page
// then calls POST /update/open (JSON) so the screen shows a 4-digit code. The multipart POST
// (field "firmware") only flashes with
// ?code=<code on the screen>. The code is required even with a Bearer token (the token travels
// in plain text on the local network).
namespace ota {

using ProgressHook = void (*)(uint8_t pct);
void begin(WebServerT& server, ProgressHook onProgress);

}  // namespace ota
