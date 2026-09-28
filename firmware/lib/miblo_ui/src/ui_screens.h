#pragma once
#include <stddef.h>
#include <stdint.h>

#include "miblo_i18n.h"
#include "miblo_overview.h"
#include "miblo_snapshot.h"
#include "ui_canvas.h"

// Layouts de todas as telas. Coordenadas são pensadas numa grade de 240×240 e escaladas pelo
// ScreenSpec do Canvas (X/Y/Sz em ui_base.cpp). Cada função é chamada a cada quadro e só
// redesenha as regiões cujo conteúdo mudou (RegionCache) — sem framebuffer.
namespace screens {

using miblo::Lang;

// Liga os layouts a um Canvas (uma vez, no boot).
void bind(ui::Canvas& canvas);
ui::Canvas& canvas();
// Troca de tela: limpa tudo e invalida o cache de regiões.
void reset();
// Se o hash da região mudou, limpa o retângulo (coordenadas já escaladas) e retorna true.
bool region(uint8_t id, uint32_t hash, int x, int y, int w, int h, uint16_t bg = ui::color::BG);
// Texto traduzido (4 buffers rotativos).
const char* t(Lang lang, miblo::S id);
// Escala da grade de 240: X para larguras/posições horizontais, Y para verticais, Sz para tamanhos.
int X(int v);
int Y(int v);
int Sz(int v);

// Primitivas compostas.
void bar(int x, int y, int w, int h, uint8_t pct, uint16_t fg);
void check(int cx, int cy, int size, uint16_t c);
void mascot(int cx, int cy, uint8_t frame);  // placeholder do mascote Miblo (3 quadros)
void qr(const char* payload, int x, int y, int scale);

// ---- telas de sistema (§4.5) ----
void boot(Lang lang, uint8_t frame);
void setup(Lang lang, const char* apSsid, bool wrongPassword);
void welcome(Lang lang, const char* pairCode, const char* ip);
void paired(Lang lang, const char* host, const char* modeName, const char* mdnsHost);
void code(Lang lang, miblo::S title, const char* code, uint32_t remainingSec);
void updating(Lang lang, uint8_t pct);
// Quick-boot hard reset countdown: big amber N + "N more quick restarts to reset".
void hardResetCountdown(Lang lang, uint8_t remaining);
void disconnected(Lang lang, bool timeValid, int hour, int minute, int wday, int mday, const char* ip,
                  const char* mdnsHost, const char* pairCode);

// ---- telas principais (§4.1–4.4) ----
struct Clock {
  bool valid;      // hora local conhecida
  char hhmm[6];    // "14:32" ou "--:--"
  uint32_t epoch;  // agora, em segundos Unix (0 = desconhecido)
};

// "16:42" (mesmo dia) ou "qui 09:00" (outro dia), no fuso local (TZ do sistema).
void formatWhen(Lang lang, uint32_t epoch, uint32_t now, char* out, size_t cap);

void flash(Lang lang, miblo::AlertKind kind, const char* name, uint32_t elapsedMs);
void hero(Lang lang, const miblo::Snapshot& s, int idx, miblo::AlertKind kind, bool discreet, const Clock& clk,
          const miblo::RunTracker& runs);
void overview(Lang lang, const miblo::Snapshot& s, miblo::Pager& pager, uint32_t nowMs, const Clock& clk,
              bool discreet);
void limits(Lang lang, const miblo::Snapshot& s, const Clock& clk);
void sessions(Lang lang, const miblo::Snapshot& s, miblo::Pager& pager, uint32_t nowMs, const Clock& clk,
              bool discreet);

}  // namespace screens
