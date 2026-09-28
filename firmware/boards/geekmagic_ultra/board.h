#pragma once
#include <stdint.h>

#include "ui_canvas.h"

// Placa: GeekMagic "Ultra" — ESP8266 ESP-12E/F, ST7789 240x240, sem botões nem touch.
// Os pinos também vão como -D para o TFT_eSPI no platformio.ini ([env:geekmagic_ultra]);
// board.cpp confere que os dois lugares batem.
namespace board {

constexpr const char* kName = "geekmagic_ultra";
constexpr ui::ScreenSpec kScreen = {240, 240};

constexpr uint8_t kPinMosi = 13;
constexpr uint8_t kPinSclk = 14;
constexpr uint8_t kPinCs = 15;
constexpr uint8_t kPinDc = 0;
constexpr uint8_t kPinRst = 2;
constexpr uint8_t kPinBacklight = 5;  // ativo em nível BAIXO

// Capacidades extras anunciadas em /api/info ("buttons", "touch", "buzzer", "led"). Ultra: nenhuma.
constexpr uint8_t kCapCount = 0;
inline const char* cap(uint8_t) { return ""; }

// Entradas físicas. Ultra não tem: sempre vazio (o seam existe para placas futuras).
struct Inputs {
  bool button;
  bool touched;
  int16_t x;
  int16_t y;
};

void begin();                        // tela + luz de fundo
ui::Canvas& canvas();
void setBacklight(uint8_t percent);  // 0..100
Inputs readInputs();

}  // namespace board
