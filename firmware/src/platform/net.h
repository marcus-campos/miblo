#pragma once
#include <Arduino.h>

#include "miblo_policy.h"

// Wi-Fi: usa as credenciais salvas no SDK (inclusive as do firmware anterior); sem credenciais,
// com senha errada ou após 2 min sem conexão, abre a rede de setup "Miblo-Setup-XXXX" com DNS
// cativo — e continua tentando a rede salva.
namespace net {

void begin(uint32_t nowMs);
void loop(uint32_t nowMs);
miblo::NetState state();
bool apActive();
bool connected();
// Número que muda a cada nova conexão (para reanunciar o mDNS).
uint32_t connectionId();
String ip();
// Chamado pelo portal: conecta na nova rede logo depois de a resposta HTTP sair.
void submitCredentials(const char* ssid, const char* pass, uint32_t nowMs);
// Reaplica o fuso (TZ POSIX de ctx.cfg.tz) e o NTP.
void applyTimezone();

}  // namespace net
