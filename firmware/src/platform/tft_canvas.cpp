#include "tft_canvas.h"

#include "miblo_utf8.h"

namespace {
constexpr uint32_t kEllipsis = 0x2026;  // "…" doesn't exist in the fonts: becomes "..."
}

void TftCanvas::begin() {
  u8_.begin(tft_);
  u8_.setFontMode(1);  // transparent: the region is cleared before drawing
  u8_.setFontDirection(0);
}

const uint8_t* TftCanvas::fontFor(ui::Font f, uint32_t cp) {
  if (cp > 0xFFFF) return nullptr;
  for (FontStack p = stacks_[(int)f]; *p; p++) {
    u8_.setFont(*p);
    if (u8g2_IsGlyph(&u8_.u8g2, (uint16_t)cp)) return *p;
  }
  return nullptr;
}

int TftCanvas::ascent(ui::Font f) {
  u8_.setFont(stacks_[(int)f][0]);
  return u8_.getFontAscent();
}

int TftCanvas::glyphAdvance(ui::Font f, uint32_t cp, const uint8_t** font) {
  const uint8_t* fnt = fontFor(f, cp);
  if (font) *font = fnt;
  if (!fnt) return ascent(f) * 2 / 3 + 3;  // width of the missing-glyph rectangle
  u8_.setFont(fnt);
  return u8g2_GetGlyphWidth(&u8_.u8g2, (uint16_t)cp);
}

// Width of `s`. If it exceeds maxW, *end points to where to cut so that prefix + "..."
// fits, and the return value is the width of prefix + "...". If not cut, *end = nullptr.
int TftCanvas::layout(const char* s, ui::Font f, int maxW, const char** end) {
  const int dots = glyphAdvance(f, '.', nullptr) * 3;
  int w = 0;
  int fitW = 0;
  const char* fit = s;
  const char* p = s;
  *end = nullptr;
  while (*p) {
    uint32_t cp = miblo::utf8Next(p);
    w += cp == kEllipsis ? dots : glyphAdvance(f, cp, nullptr);
    if (w + dots <= maxW) {
      fit = p;
      fitW = w;
    }
  }
  if (w <= maxW) return w;
  *end = fit;
  return fitW + dots;
}

int TftCanvas::drawRun(int x, int y, const char* s, const char* end, ui::Font f, uint16_t fg) {
  const int x0 = x;
  const char* p = s;
  while (*p && (!end || p < end)) {
    uint32_t cp = miblo::utf8Next(p);
    if (cp == kEllipsis) {
      x += drawRun(x, y, "...", nullptr, f, fg);
      continue;
    }
    const uint8_t* fnt;
    const int adv = glyphAdvance(f, cp, &fnt);
    if (fnt) {
      u8_.setFont(fnt);
      // u8g2_SetFont() resets the font mode to opaque on every font change, which painted each
      // glyph's box in the (black) background colour: keep it transparent so the text sits on
      // whatever colour the region was cleared with.
      u8_.setFontMode(1);
      u8_.setForegroundColor(fg);
      u8_.drawGlyph(x, y, (uint16_t)cp);
    } else {
      const int h = ascent(f);
      tft_.drawRect(x + 1, y - h, adv - 2, h, fg);  // missing glyph: rectangle, never crashes
    }
    x += adv;
  }
  return x - x0;
}

int TftCanvas::textWidth(const char* s, ui::Font f) {
  const char* end;
  return layout(s ? s : "", f, 30000, &end);
}

int TftCanvas::text(int x, int y, const char* s, ui::Font f, uint16_t fg, ui::Align a, int maxW) {
  if (!s) s = "";
  const char* end;
  const int w = layout(s, f, maxW, &end);
  const int left = a == ui::Align::Left ? x : (a == ui::Align::Center ? x - w / 2 : x - w);
  int drawn = drawRun(left, y, s, end, f, fg);
  if (end) drawn += drawRun(left + drawn, y, "...", nullptr, f, fg);
  return drawn;
}
