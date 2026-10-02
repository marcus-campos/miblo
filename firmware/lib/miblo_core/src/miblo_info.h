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

// GET /settings-system, the settings page's System panel: the load (%), the CPU clock, RAM in
// use of the total, and storage in two parts: the program (firmware bytes against the largest
// program the flash layout takes) and the data (the filesystem's used and total bytes).
struct SystemStats {
  uint8_t cpu;
  uint16_t mhz;
  uint32_t ramUsed, ram;
  uint32_t fw, fwMax;
  uint32_t fsUsed, fs;
};
void writeSystemInfo(JsonObject doc, const SystemStats& s);

}  // namespace miblo
