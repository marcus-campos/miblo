#include "board.h"

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <U8g2_for_TFT_eSPI.h>

#include "platform/tft_canvas.h"

// Subset of u8g2_font_fub20_tf (digits and "%+,-./: "), made by scripts/vendor_u8g2.py.
extern const uint8_t u8g2_font_fub20_miblo[] U8G2_FONT_SECTION("u8g2_font_fub20_miblo");

static_assert(TFT_MOSI == board::kPinMosi && TFT_SCLK == board::kPinSclk && TFT_CS == board::kPinCs &&
                  TFT_DC == board::kPinDc && TFT_RST == board::kPinRst && TFT_BL == board::kPinBacklight,
              "pins in platformio.ini differ from boards/geekmagic_ultra/board.h");
static_assert(TFT_WIDTH == board::kScreen.w && TFT_HEIGHT == board::kScreen.h, "screen size mismatch");

namespace board {

namespace {

// Font stacks per ui::Font (Latin -> Cyrillic -> CJK). Vendored in lib/U8g2TFT.
const uint8_t* const kSmall[] = {u8g2_font_helvR10_te, u8g2_font_6x13_t_cyrillic, u8g2_font_wqy14_t_gb2312a, nullptr};
const uint8_t* const kSmallBold[] = {u8g2_font_helvB10_te, u8g2_font_6x13B_t_cyrillic, u8g2_font_wqy14_t_gb2312a,
                                     nullptr};
const uint8_t* const kBody[] = {u8g2_font_helvR12_te, u8g2_font_8x13_t_cyrillic, u8g2_font_wqy14_t_gb2312a, nullptr};
const uint8_t* const kBodyBold[] = {u8g2_font_helvB12_te, u8g2_font_8x13_t_cyrillic, u8g2_font_wqy14_t_gb2312a,
                                    nullptr};
const uint8_t* const kTitle[] = {u8g2_font_helvB18_te, u8g2_font_10x20_t_cyrillic, u8g2_font_wqy14_t_gb2312a, nullptr};
const uint8_t* const kHero[] = {u8g2_font_helvB24_te, u8g2_font_inr24_t_cyrillic, u8g2_font_wqy14_t_gb2312a, nullptr};
const uint8_t* const kNumL[] = {u8g2_font_fub30_tn, u8g2_font_fub20_miblo, u8g2_font_helvB18_te, nullptr};
const uint8_t* const kNumM[] = {u8g2_font_fub20_miblo, u8g2_font_helvB12_te, nullptr};
const TftCanvas::FontStack kStacks[] = {kSmall, kSmallBold, kBody, kBodyBold, kTitle, kHero, kNumL, kNumM};
static_assert(sizeof(kStacks) / sizeof(kStacks[0]) == (size_t)ui::Font::Count, "one stack per ui::Font");

TFT_eSPI tft;
TftCanvas tftCanvas(tft, kScreen, kStacks);

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
