#include "tft_canvas.h"

#include <math.h>

#include "miblo_utf8.h"

namespace {
constexpr uint32_t kEllipsis = 0x2026;  // "…" doesn't exist in the fonts: becomes "..."
// Free heap that must remain after allocating a layer (Wi-Fi, web server, JSON parsing).
constexpr uint32_t kLayerHeapReserve = 16 * 1024;
}  // namespace

uint8_t TftCanvas::idx(uint16_t c) {
  for (uint8_t i = 0; i < palN_; i++) {
    if (pal_[i] == c) return i;
  }
  if (palN_ < 16) {  // exact colour: take the next free palette slot
    pal_[palN_] = c;
    spr_.setPaletteColor(palN_, c);
    return palN_++;
  }
  uint8_t best = 0;  // palette full: nearest colour
  uint32_t bestD = UINT32_MAX;
  for (uint8_t i = 0; i < 16; i++) {
    const uint16_t p = pal_[i];
    const int dr = ((p >> 11) & 31) - ((c >> 11) & 31);
    const int dg = ((p >> 5) & 63) / 2 - ((c >> 5) & 63) / 2;
    const int db = (p & 31) - (c & 31);
    const uint32_t d = (uint32_t)(dr * dr + dg * dg + db * db);
    if (d < bestD) {
      bestD = d;
      best = i;
    }
  }
  return best;
}

bool TftCanvas::beginLayer(int x, int y, int w, int h) {
  if (w <= 0 || h <= 0) return false;
  if (!spr_.getPointer() || w != lw_ || h != lh_) {
    releaseLayer();
    const uint32_t bytes = (uint32_t)((w + 1) & ~1) * h / 2 + 64;  // 4 bpp + palette/overhead
    if (ESP.getFreeHeap() < bytes + kLayerHeapReserve || ESP.getMaxFreeBlockSize() < bytes) return false;
    spr_.setColorDepth(4);
    if (!spr_.createSprite(w, h)) return false;  // out of memory: caller draws directly
    lw_ = w;
    lh_ = h;
  }
  palN_ = 0;  // the caller repaints the whole layer: the palette starts over
  lx_ = x;
  ly_ = y;
  layer_ = true;
  return true;
}

void TftCanvas::endLayer() {
  if (!layer_) return;
  layer_ = false;
  spr_.pushSprite(lx_, ly_);
}

void TftCanvas::releaseLayer() {
  layer_ = false;
  if (spr_.getPointer()) spr_.deleteSprite();
  lw_ = lh_ = 0;
}

void TftCanvas::wideLine(int x0, int y0, int x1, int y1, int width, uint16_t c, uint16_t bg) {
  if (!layer_) {
    tft_.drawWideLine(x0, y0, x1, y1, width, c, bg);
    return;
  }
  // A 4-bit layer can't blend anti-aliased edges: flat quad with round ends instead.
  const float dx = (float)(x1 - x0);
  const float dy = (float)(y1 - y0);
  const float len = sqrtf(dx * dx + dy * dy);
  const float r = width / 2.0f;
  if (len > 0.0f) {
    const int ox = (int)lroundf(-dy / len * r);
    const int oy = (int)lroundf(dx / len * r);
    fillTriangle(x0 + ox, y0 + oy, x1 + ox, y1 + oy, x1 - ox, y1 - oy, c);
    fillTriangle(x0 + ox, y0 + oy, x1 - ox, y1 - oy, x0 - ox, y0 - oy, c);
  }
  fillCircle(x0, y0, (int)r, c);
  fillCircle(x1, y1, (int)r, c);
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

int TftCanvas::descent(ui::Font f) {
  u8_.setFont(stacks_[(int)f][0]);
  return -u8_.getFontDescent();
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

int TftCanvas::drawRun(int x, int y, const char* s, const char* end, ui::Font f, uint16_t fg, int32_t bg) {
  // In a layer, u8g2 draws into the sprite (palette indices, layer-relative coordinates).
  const int ox = layer_ ? lx_ : 0;
  const int oy = layer_ ? ly_ : 0;
  const uint16_t fgc = layer_ ? idx(fg) : fg;
  const uint16_t bgc = bg < 0 ? 0 : (layer_ ? idx((uint16_t)bg) : (uint16_t)bg);
  if (layer_) u8_.begin(spr_);
  else u8_.begin(tft_);
  const int x0 = x;
  const char* p = s;
  while (*p && (!end || p < end)) {
    uint32_t cp = miblo::utf8Next(p);
    if (cp == kEllipsis) {
      x += drawRun(x, y, "...", nullptr, f, fg, bg);
      continue;
    }
    const uint8_t* fnt;
    const int adv = glyphAdvance(f, cp, &fnt);
    if (fnt) {
      u8_.setFont(fnt);
      // u8g2_SetFont() resets the font mode to opaque on every font change: set it again.
      // Transparent (1) sits the text on whatever the region was cleared with; opaque (0)
      // paints the glyph box in bg, replacing the old glyph in place.
      u8_.setFontMode(bg < 0 ? 1 : 0);
      u8_.setBackgroundColor(bgc);
      u8_.setForegroundColor(fgc);
      u8_.drawGlyph(x - ox, y - oy, (uint16_t)cp);
    } else {
      const int h = ascent(f);
      drawRect(x + 1, y - h, adv - 2, h, fg);  // missing glyph: rectangle, never crashes
    }
    x += adv;
  }
  if (layer_) u8_.begin(tft_);
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
  int drawn = drawRun(left, y, s, end, f, fg, -1);
  if (end) drawn += drawRun(left + drawn, y, "...", nullptr, f, fg, -1);
  return drawn;
}

int TftCanvas::textBox(int x, int y, const char* s, ui::Font f, uint16_t fg, uint16_t bg, ui::Align a, int boxW) {
  if (!s) s = "";
  const int top = y - ascent(f) - 1;
  const int h = ascent(f) + descent(f) + 2;
  const int bx = a == ui::Align::Left ? x : (a == ui::Align::Center ? x - boxW / 2 : x - boxW);
  // Best: compose the box off-screen (a small layer, freed right after) and push it in one go.
  // Inside an open layer the box simply goes into that layer.
  const bool own = !layer_ && beginLayer(bx, top, boxW, h);
  if (layer_) {
    fillRect(bx, top, boxW, h, bg);
    const int w = text(x, y, s, f, fg, a, boxW);
    if (own) {
      endLayer();
      releaseLayer();
    }
    return w;
  }
  // No memory for a layer: clear the box, then draw the text. (Opaque glyphs over the old ones
  // only repaint each new glyph's own box, so pieces of the old text stayed on screen; a brief
  // clear of this one box is the lesser evil.)
  tft_.fillRect(bx, top, boxW, h, bg);
  return text(x, y, s, f, fg, a, boxW);
}
