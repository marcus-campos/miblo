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
// Erases config, pairings, and the Wi-Fi saved in the SDK, then reboots. Does not return.
void factoryReset();

}  // namespace storage
