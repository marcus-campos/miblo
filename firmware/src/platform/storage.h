#pragma once
#include <stdint.h>

#include "miblo_config.h"
#include "miblo_security.h"

// Persistence on LittleFS (small JSON files). Wi-Fi credentials stay in the SDK.
namespace storage {

bool begin();
bool loadConfig(miblo::Config& cfg);
bool saveConfig(const miblo::Config& cfg);
bool loadTokens(miblo::TokenStore& tokens);
bool saveTokens(const miblo::TokenStore& tokens);
uint8_t readBootCount();
void writeBootCount(uint8_t n);
// "Ever configured" marker (/.configured at the LittleFS root, outside /miblo): set the first time
// the unit joins a network submitted through the portal or is first paired, and never cleared --
// it survives factory reset, so a reset unit keeps requiring the on-screen code for OTA.
bool everConfigured();
void markConfigured();  // idempotent; writes the marker only once
// Erases config, pairings, and the Wi-Fi saved in the SDK, then reboots. Does not return.
// Keeps the root markers (/.miblo_fs, /.configured).
void factoryReset();

}  // namespace storage
