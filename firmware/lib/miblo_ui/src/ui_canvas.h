#pragma once
#include <stdint.h>

// Board-independent drawing interface. Each board implements a Canvas (e.g. TFT_eSPI +
// u8g2 fonts on the GeekMagic Ultra); the layouts in ui_screens.h only talk to this interface.
namespace ui {

struct ScreenSpec {
  int16_t w;
  int16_t h;
};

// Text styles; the board maps each one to real fonts (with Latin/Cyrillic/CJK fallback).
// Brand: the "miblo" wordmark (only its letters exist in that font).
enum class Font : uint8_t { Small, SmallBold, Body, BodyBold, Title, Hero, NumL, NumM, Brand, Count };
enum class Align : uint8_t { Left, Center, Right };

// Mockup palette (RGB565).
namespace color {
constexpr uint16_t BG = 0x0841;          // #0b0b0d
constexpr uint16_t TEXT = 0xEF7D;        // #eeeeee
constexpr uint16_t MUTED = 0xAD55;       // #aaaaaa
constexpr uint16_t DIM = 0x73AE;         // #777777
constexpr uint16_t FAINT = 0x52AA;       // #555555
constexpr uint16_t AMBER = 0xF524;       // #f5a524 needs you
constexpr uint16_t GREEN = 0x4EF0;       // #4ade80 running
constexpr uint16_t BLUE = 0x653F;        // #60a5fa finished
constexpr uint16_t FLASH_BLUE = 0x3C1E;  // #3b82f6 blue flash
constexpr uint16_t CORAL = 0xDBAA;       // #d97757 5h window
constexpr uint16_t VIOLET = 0x8C5E;      // #8b8bf5 week
constexpr uint16_t RED = 0xEA28;         // #ef4444 limit >= 95%
constexpr uint16_t TRACK = 0x2125;       // #262629 bar background
constexpr uint16_t DIVIDER = 0x2104;     // #222222
constexpr uint16_t CARD = 0x10A3;        // #16161a
constexpr uint16_t CARD_AMBER = 0x18A1;  // #1c160a
constexpr uint16_t CMD_BG = 0x18C3;      // #1a1a1e
// Sphynx mascot.
constexpr uint16_t SKIN = 0xF5D5;        // #f2b8a8 warm peach skin
constexpr uint16_t WRINKLE = 0xBB8F;     // #b8707c forehead wrinkles, mouth
constexpr uint16_t EAR_IN = 0xE473;      // #e48f9c inner ears
constexpr uint16_t NOSE = 0xCB2F;        // #cf6479
constexpr uint16_t PUPIL = 0x1882;       // #1a1014 pupils, closed eyes
constexpr uint16_t EYE_GREEN = 0x9EC9;   // #9bd84e iris
constexpr uint16_t BLACK = 0x0000;
constexpr uint16_t WHITE = 0xFFFF;
}  // namespace color

class Canvas {
 public:
  virtual ~Canvas() = default;
  virtual ScreenSpec spec() const = 0;
  virtual void fillRect(int x, int y, int w, int h, uint16_t c) = 0;
  virtual void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) = 0;
  virtual void drawRect(int x, int y, int w, int h, uint16_t c) = 0;
  virtual void fillCircle(int cx, int cy, int r, uint16_t c) = 0;
  virtual void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) = 0;
  virtual void wideLine(int x0, int y0, int x1, int y1, int width, uint16_t c, uint16_t bg) = 0;
  // Anti-aliased arc: angles in degrees, 0 = 6 o'clock, clockwise (TFT_eSPI convention).
  virtual void arc(int cx, int cy, int r, int ir, int a0, int a1, uint16_t fg, uint16_t bg) = 0;
  // UTF-8 text with baseline at y; truncates with "..." if it exceeds maxW. Returns the width.
  // Missing glyph in the fonts → rectangle, never crashes.
  virtual int text(int x, int y, const char* s, Font f, uint16_t fg, Align a, int maxW) = 0;
  virtual int textWidth(const char* s, Font f) = 0;
  // Text on a solid background, for values that change in place (timers, clock, %): paints the
  // box (boxW wide, anchored at x like the text, from the font's top to its bottom) in `bg`
  // together with the text, without clearing it first, so the old value is replaced with no
  // flash and a shorter string still covers the old glyphs. Truncates like text().
  virtual int textBox(int x, int y, const char* s, Font f, uint16_t fg, uint16_t bg, Align a, int boxW) = 0;

  // Optional off-screen layer, used to redraw an area without flicker. beginLayer() asks the
  // board to redirect the drawing primitives (shapes, wide lines and text — not arcs) inside
  // the screen rectangle (x, y, w, h) to an off-screen buffer; endLayer() pushes that buffer to
  // the panel in one go. Coordinates stay screen-absolute. The layer's previous content is
  // undefined: the caller repaints all of it. Returns false when the board has no layer (or no
  // memory for it): the caller then just draws directly.
  // Layers may reduce colours to a small palette (16 per layer) and drop anti-aliasing.
  virtual bool beginLayer(int x, int y, int w, int h) {
    (void)x, (void)y, (void)w, (void)h;
    return false;
  }
  virtual void endLayer() {}
  // Frees any memory held for layers (called on every screen switch).
  virtual void releaseLayer() {}
  // Paints the whole physical screen (screen switches), whatever offset a wrapper applies.
  virtual void clear(uint16_t c) { fillRect(0, 0, spec().w, spec().h, c); }
};

// Blue light filter: `c` as it looks under a warmer white point, the colour temperature of each
// level being 4500 K, 3500 K and 2700 K (black-body RGB multipliers after Tanner Helland's
// approximation: green 219/196/169, blue 186/137/87 out of 255; red stays). Level 0 returns `c`
// unchanged; levels above 3 are treated as 3. A switch, not a table: on the ESP8266 a const
// table would sit in RAM.
inline uint16_t warmColor(uint16_t c, uint8_t level) {
  uint16_t gm, bm;
  switch (level) {
    case 0: return c;
    case 1: gm = 219, bm = 186; break;
    case 2: gm = 196, bm = 137; break;
    default: gm = 169, bm = 87; break;
  }
  const uint16_t g = (uint16_t)((((c >> 5) & 63) * gm + 127) / 255);
  const uint16_t b = (uint16_t)(((c & 31) * bm + 127) / 255);
  return (uint16_t)((c & 0xF800) | g << 5 | b);
}

// Draws through another canvas with everything moved by (dx, dy) pixels. Shifting the whole
// picture a pixel or two every few minutes keeps static edges (headers, rings, text) from
// sitting on the same pixels for hours, which is what leaves ghost images on an LCD.
// Content moved past an edge is simply clipped by the panel.
// It is also where the blue light filter is applied (setWarmth): every colour it forwards goes
// through warmColor, so all screens, the mascot and the props are filtered alike. Changing the
// warmth does not repaint what is already on the panel: the caller redraws everything.
class ShiftCanvas : public Canvas {
 public:
  explicit ShiftCanvas(Canvas& inner) : in_(inner) {}
  void setShift(int dx, int dy) { dx_ = dx, dy_ = dy; }
  int dx() const { return dx_; }
  int dy() const { return dy_; }
  void setWarmth(uint8_t level) { warm_ = level; }
  uint8_t warmth() const { return warm_; }

  ScreenSpec spec() const override { return in_.spec(); }
  void fillRect(int x, int y, int w, int h, uint16_t c) override { in_.fillRect(x + dx_, y + dy_, w, h, f(c)); }
  void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) override {
    in_.fillRoundRect(x + dx_, y + dy_, w, h, r, f(c));
  }
  void drawRect(int x, int y, int w, int h, uint16_t c) override { in_.drawRect(x + dx_, y + dy_, w, h, f(c)); }
  void fillCircle(int cx, int cy, int r, uint16_t c) override { in_.fillCircle(cx + dx_, cy + dy_, r, f(c)); }
  void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) override {
    in_.fillTriangle(x0 + dx_, y0 + dy_, x1 + dx_, y1 + dy_, x2 + dx_, y2 + dy_, f(c));
  }
  void wideLine(int x0, int y0, int x1, int y1, int width, uint16_t c, uint16_t bg) override {
    in_.wideLine(x0 + dx_, y0 + dy_, x1 + dx_, y1 + dy_, width, f(c), f(bg));
  }
  void arc(int cx, int cy, int r, int ir, int a0, int a1, uint16_t fg, uint16_t bg) override {
    in_.arc(cx + dx_, cy + dy_, r, ir, a0, a1, f(fg), f(bg));
  }
  int text(int x, int y, const char* s, Font font, uint16_t fg, Align a, int maxW) override {
    return in_.text(x + dx_, y + dy_, s, font, f(fg), a, maxW);
  }
  int textWidth(const char* s, Font font) override { return in_.textWidth(s, font); }
  int textBox(int x, int y, const char* s, Font font, uint16_t fg, uint16_t bg, Align a, int boxW) override {
    return in_.textBox(x + dx_, y + dy_, s, font, f(fg), f(bg), a, boxW);
  }
  bool beginLayer(int x, int y, int w, int h) override { return in_.beginLayer(x + dx_, y + dy_, w, h); }
  void endLayer() override { in_.endLayer(); }
  void releaseLayer() override { in_.releaseLayer(); }
  void clear(uint16_t c) override { in_.clear(f(c)); }  // the whole panel, not the shifted area

 private:
  uint16_t f(uint16_t c) const { return warmColor(c, warm_); }

  Canvas& in_;
  int dx_ = 0;
  int dy_ = 0;
  uint8_t warm_ = 0;  // blue light filter strength, 0 = off
};

}  // namespace ui
