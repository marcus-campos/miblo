#pragma once
#include <ArduinoJson.h>
#include <stdint.h>

#include "miblo_security.h"

namespace miblo {

// How much GET /api/info tells this request. Full while the gadget is not paired yet (the plugin
// and the fleet tool need the name, version and board to set it up) or for a paired computer's
// "Bearer <token>"; anyone else on the LAN of a paired gadget only learns who it is (Public).
enum class InfoView : uint8_t { Full, Public };
InfoView infoView(const TokenStore& tokens, const char* authHeader);

// The Public document: exactly id, paired and proto.
void writePublicInfo(JsonObject doc, const char* id, bool paired, int proto);

}  // namespace miblo
