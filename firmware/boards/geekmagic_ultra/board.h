#pragma once
#include <stdint.h>

#include "ui_canvas.h"

// Board: GeekMagic "Ultra" — ESP8266 ESP-12E/F, ST7789 240x240, no buttons or touch.
// The pins are also passed as -D to TFT_eSPI in platformio.ini ([env:geekmagic_ultra]);
// board.cpp checks that both places match.
namespace board {

constexpr const char* kName = "geekmagic_ultra";
constexpr ui::ScreenSpec kScreen = {240, 240};

constexpr uint8_t kPinMosi = 13;
constexpr uint8_t kPinSclk = 14;
constexpr uint8_t kPinCs = 15;
constexpr uint8_t kPinDc = 0;
constexpr uint8_t kPinRst = 2;
constexpr uint8_t kPinBacklight = 5;  // active LOW

// Extra capabilities announced in /api/info ("buttons", "touch", "buzzer", "led"). Ultra: none.
constexpr uint8_t kCapCount = 0;
inline const char* cap(uint8_t) { return ""; }

// Physical inputs. Ultra has none: always empty (the seam exists for future boards).
struct Inputs {
  bool button;
  bool touched;
  int16_t x;
  int16_t y;
};

void begin();                        // screen + backlight
ui::Canvas& canvas();
void setBacklight(uint8_t percent);  // 0..100
// Panel on/off: off stops driving the LCD (ST7789 DISPOFF) and turns the backlight off, so an
// unattended picture can't leave a ghost; on restores both (the caller redraws and relights).
void setDisplay(bool on);
Inputs readInputs();

}  // namespace board
