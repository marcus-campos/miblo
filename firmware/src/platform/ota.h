#pragma once
#include "platform.h"

// /update is always available (including on the setup network). GET serves the page; the page
// then calls POST /update/open (JSON) so the screen shows a 4-digit code. The multipart POST
// (field "firmware") only flashes with
// ?code=<code on the screen>. The code is required even with a Bearer token (the token travels
// in plain text on the local network).
// Exception: an unconfigured unit (no saved Wi-Fi, no pairings) reached over its own setup AP
// flashes without a code; /update/open then answers {"ok":true,"codeRequired":false}.
namespace ota {

using ProgressHook = void (*)(uint8_t pct);
void begin(WebServerT& server, ProgressHook onProgress);
// Is an upload window open right now, for a request from `client`? True while the update code
// (POST /update/open) is active, or, on a unit that needs no code, for PresenceGate::kTtlMs after
// its POST /update/open. Outside a window the server refuses every multipart request before
// parsing it (web.cpp, limitPostBody): a multipart body is only ever the firmware upload.
bool uploadArmed(WiFiClient& client);

}  // namespace ota
