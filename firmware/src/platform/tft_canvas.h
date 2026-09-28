#pragma once
#include <TFT_eSPI.h>
#include <U8g2_for_TFT_eSPI.h>

#include "ui_canvas.h"

// Canvas on top of TFT_eSPI + u8g2 fonts (UTF-8). Works for any board with TFT_eSPI; the board
// supplies the screen size and, for each ui::Font, a stack of u8g2 fonts (nullptr-terminated):
// the first one that has the glyph draws the character; none → rectangle.
// Layers (ui::Canvas::beginLayer): a 4-bit TFT_eSprite (w * h / 2 bytes, 4.6 KB for the 96x96
// mascot, ~7.4 KB for a session card) whose 16-colour palette is filled with the exact colours
// as they are first used, created on demand (only if 16 KB of heap remain) and freed by
// releaseLayer(). Shapes and text go to the layer; wide lines become flat (no anti-aliasing);
// arcs always draw straight to the panel.
class TftCanvas : public ui::Canvas {
 public:
  using FontStack = const uint8_t* const*;
  TftCanvas(TFT_eSPI& tft, ui::ScreenSpec spec, const FontStack* stacks)
      : tft_(tft), spr_(&tft), spec_(spec), stacks_(stacks) {}
  void begin();

  ui::ScreenSpec spec() const override { return spec_; }
  void fillRect(int x, int y, int w, int h, uint16_t c) override {
    if (layer_) spr_.fillRect(x - lx_, y - ly_, w, h, idx(c));
    else tft_.fillRect(x, y, w, h, c);
  }
  void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) override {
    if (layer_) spr_.fillRoundRect(x - lx_, y - ly_, w, h, r, idx(c));
    else tft_.fillRoundRect(x, y, w, h, r, c);
  }
  void drawRect(int x, int y, int w, int h, uint16_t c) override {
    if (layer_) spr_.drawRect(x - lx_, y - ly_, w, h, idx(c));
    else tft_.drawRect(x, y, w, h, c);
  }
  void fillCircle(int cx, int cy, int r, uint16_t c) override {
    if (layer_) spr_.fillCircle(cx - lx_, cy - ly_, r, idx(c));
    else tft_.fillCircle(cx, cy, r, c);
  }
  void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) override {
    if (layer_) spr_.fillTriangle(x0 - lx_, y0 - ly_, x1 - lx_, y1 - ly_, x2 - lx_, y2 - ly_, idx(c));
    else tft_.fillTriangle(x0, y0, x1, y1, x2, y2, c);
  }
  void wideLine(int x0, int y0, int x1, int y1, int width, uint16_t c, uint16_t bg) override;
  void arc(int cx, int cy, int r, int ir, int a0, int a1, uint16_t fg, uint16_t bg) override {
    tft_.drawSmoothArc(cx, cy, r, ir, a0, a1, fg, bg, true);
  }
  int text(int x, int y, const char* s, ui::Font f, uint16_t fg, ui::Align a, int maxW) override;
  int textWidth(const char* s, ui::Font f) override;
  int textBox(int x, int y, const char* s, ui::Font f, uint16_t fg, uint16_t bg, ui::Align a, int boxW) override;
  bool beginLayer(int x, int y, int w, int h) override;
  void endLayer() override;
  void releaseLayer() override;

 private:
  TFT_eSPI& tft_;
  TFT_eSprite spr_;
  bool layer_ = false;  // drawing into spr_
  int lx_ = 0, ly_ = 0, lw_ = 0, lh_ = 0;
  ui::ScreenSpec spec_;
  const FontStack* stacks_;
  U8g2_for_TFT_eSPI u8_;

  uint16_t pal_[16] = {};  // the layer's palette, filled as colours are used
  uint8_t palN_ = 0;

  uint8_t idx(uint16_t c);  // RGB565 -> layer palette index (added if there is room, else nearest)
  const uint8_t* fontFor(ui::Font f, uint32_t cp);
  int glyphAdvance(ui::Font f, uint32_t cp, const uint8_t** font);
  int ascent(ui::Font f);
  int descent(ui::Font f);
  int layout(const char* s, ui::Font f, int maxW, const char** end);
  // Draws glyphs from s to end (nullptr = the whole string); bg < 0 = transparent.
  int drawRun(int x, int y, const char* s, const char* end, ui::Font f, uint16_t fg, int32_t bg);
};
