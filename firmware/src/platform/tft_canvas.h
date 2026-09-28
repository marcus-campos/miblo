#pragma once
#include <TFT_eSPI.h>
#include <U8g2_for_TFT_eSPI.h>

#include "ui_canvas.h"

// Canvas sobre TFT_eSPI + fontes u8g2 (UTF-8). Serve para qualquer placa com TFT_eSPI; a placa
// informa o tamanho da tela e, para cada ui::Font, uma pilha de fontes u8g2 (terminada em
// nullptr): o primeiro que tiver o glyph desenha o caractere; nenhum → retângulo.
class TftCanvas : public ui::Canvas {
 public:
  using FontStack = const uint8_t* const*;
  TftCanvas(TFT_eSPI& tft, ui::ScreenSpec spec, const FontStack* stacks) : tft_(tft), spec_(spec), stacks_(stacks) {}
  void begin();

  ui::ScreenSpec spec() const override { return spec_; }
  void fillRect(int x, int y, int w, int h, uint16_t c) override { tft_.fillRect(x, y, w, h, c); }
  void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) override { tft_.fillRoundRect(x, y, w, h, r, c); }
  void drawRect(int x, int y, int w, int h, uint16_t c) override { tft_.drawRect(x, y, w, h, c); }
  void fillCircle(int cx, int cy, int r, uint16_t c) override { tft_.fillCircle(cx, cy, r, c); }
  void wideLine(int x0, int y0, int x1, int y1, int width, uint16_t c, uint16_t bg) override {
    tft_.drawWideLine(x0, y0, x1, y1, width, c, bg);
  }
  void arc(int cx, int cy, int r, int ir, int a0, int a1, uint16_t fg, uint16_t bg) override {
    tft_.drawSmoothArc(cx, cy, r, ir, a0, a1, fg, bg, true);
  }
  int text(int x, int y, const char* s, ui::Font f, uint16_t fg, ui::Align a, int maxW) override;
  int textWidth(const char* s, ui::Font f) override;

 private:
  TFT_eSPI& tft_;
  ui::ScreenSpec spec_;
  const FontStack* stacks_;
  U8g2_for_TFT_eSPI u8_;

  const uint8_t* fontFor(ui::Font f, uint32_t cp);
  int glyphAdvance(ui::Font f, uint32_t cp, const uint8_t** font);
  int ascent(ui::Font f);
  int layout(const char* s, ui::Font f, int maxW, const char** end);
  int drawRun(int x, int y, const char* s, const char* end, ui::Font f, uint16_t fg);
};
