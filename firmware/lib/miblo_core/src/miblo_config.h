#pragma once
#include <ArduinoJson.h>
#include <stdint.h>

#include "miblo_alerts.h"
#include "miblo_i18n.h"

namespace miblo {

enum class Mode : uint8_t { Overview, Limits, Sessions };
const char* modeCode(Mode m);  // "overview" | "limits" | "sessions"
bool modeFromCode(const char* s, Mode& out);

struct Config {
  Mode mode = Mode::Overview;
  uint8_t brightness = 80;  // %, 5..100
  bool alerts = true;
  uint8_t heroPermSec = 10;  // 3..60
  uint8_t heroDoneSec = 5;   // 2..60
  uint8_t reminderMin = 2;   // 0..30 (0 = sem lembrete)
  bool discreet = false;
  char tz[48] = "UTC0";      // TZ POSIX, ex. "<-03>3"
  char name[64] = "";        // ≤ 20 caracteres; vazio = nome padrão "Miblo-XXXX"
  Lang lang = Lang::En;
  bool langSet = false;      // false = idioma automático (Accept-Language)
};

// Valida todos os campos presentes e só então aplica. Campos desconhecidos são ignorados.
// Em erro, `cfg` não muda e `*badField` (se não nulo) aponta para o nome do campo inválido.
bool applyConfigPatch(Config& cfg, JsonObjectConst patch, const char** badField);
void configToJson(const Config& cfg, JsonObject out);
AlertTiming alertTiming(const Config& cfg);

// ---- Reset de fábrica por liga/desliga (spec §6) ----
constexpr uint8_t kPowerCyclesForReset = 3;
constexpr uint32_t kPowerCycleWindowMs = 10000;
struct BootDecision {
  uint8_t storeCount;  // valor a gravar na flash agora
  bool factoryReset;
};
// No boot: incrementa o contador salvo; no 3º boot seguido (cada um com < 10 s de uptime) → reset.
// Depois de 10 s de uptime o firmware grava 0.
BootDecision decideBoot(uint8_t storedCount);

}  // namespace miblo
