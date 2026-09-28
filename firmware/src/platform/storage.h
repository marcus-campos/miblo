#pragma once
#include <stdint.h>

#include "miblo_config.h"
#include "miblo_security.h"

// Persistência em LittleFS (arquivos JSON pequenos). As credenciais de Wi-Fi ficam no SDK.
namespace storage {

bool begin();
bool loadConfig(miblo::Config& cfg);
bool saveConfig(const miblo::Config& cfg);
bool loadTokens(miblo::TokenStore& tokens);
bool saveTokens(const miblo::TokenStore& tokens);
uint8_t readBootCount();
void writeBootCount(uint8_t n);
// Apaga configuração, pareamentos e o Wi-Fi salvo no SDK, e reinicia. Não retorna.
void factoryReset();

}  // namespace storage
