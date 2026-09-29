#include "board.h"

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <U8g2_for_TFT_eSPI.h>

#include "fonts.h"
#include "platform/tft_canvas.h"

static_assert(TFT_MOSI == board::kPinMosi && TFT_SCLK == board::kPinSclk && TFT_CS == board::kPinCs &&
                  TFT_DC == board::kPinDc && TFT_RST == board::kPinRst && TFT_BL == board::kPinBacklight,
              "pins in platformio.ini differ from boards/geekmagic_ultra/board.h");
static_assert(TFT_WIDTH == board::kScreen.w && TFT_HEIGHT == board::kScreen.h, "screen size mismatch");

namespace board {

namespace {

TFT_eSPI tft;
TftCanvas tftCanvas(tft, kScreen, fonts::kStacks);

}  // namespace

void begin() {
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(0x0841);
  tftCanvas.begin();
  analogWriteRange(255);
  setBacklight(80);
}

ui::Canvas& canvas() { return tftCanvas; }

void setBacklight(uint8_t percent) {
  if (percent > 100) percent = 100;
  const uint32_t duty = (uint32_t)percent * 255 / 100;
  analogWrite(kPinBacklight, 255 - duty);  // active LOW
}

Inputs readInputs() { return Inputs{false, false, 0, 0}; }

}  // namespace board
