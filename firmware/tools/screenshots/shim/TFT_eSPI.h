#pragma once
// Host stand-in for TFT_eSPI (and the bits of Arduino it drags in), so the real TftCanvas and the
// vendored u8g2 font renderer run on a computer and draw into an RGB565 framebuffer. Used only by
// tools/screenshots. Shapes follow TFT_eSPI's conventions (fillCircle is 2r+1 wide, arcs start at
// 6 o'clock and go clockwise, anti-aliased arcs and wide lines blend into what is underneath).
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <vector>

class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t* buffer, size_t size) {
    size_t n = 0;
    while (size--) n += write(*buffer++);
    return n;
  }
};

// Free heap TftCanvas sees (main.cpp sets plenty): with it, layers (4-bit sprites) as on a healthy
// gadget; with 0, everything drawn straight to the panel, as when memory is short (then a text
// field that changes keeps bits of the old text: opaque glyphs only repaint their own box).
struct HostEsp {
  uint32_t heap = 0;
  uint32_t getFreeHeap() const { return heap; }
  uint32_t getMaxFreeBlockSize() const { return heap; }
};
extern HostEsp ESP;

class TFT_eSPI {
 public:
  TFT_eSPI(int16_t w = 240, int16_t h = 240) : w_(w), h_(h), px_((size_t)w * h, 0) {}
  virtual ~TFT_eSPI() = default;

  int16_t width() const { return w_; }
  int16_t height() const { return h_; }
  const std::vector<uint16_t>& pixels() const { return px_; }

  void drawPixel(int32_t x, int32_t y, uint32_t c);
  void drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t c) { fillRect(x, y, w, 1, c); }
  void drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t c) { fillRect(x, y, 1, h, c); }
  void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c);
  void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c);
  void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c);
  void fillCircle(int32_t x, int32_t y, int32_t r, uint32_t c);
  void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t c);
  void drawWideLine(float ax, float ay, float bx, float by, float wd, uint32_t fg, uint32_t bg = 0x00FFFFFF);
  void drawSmoothArc(int32_t x, int32_t y, int32_t r, int32_t ir, uint32_t startAngle, uint32_t endAngle,
                     uint32_t fg, uint32_t bg, bool roundEnds = false);

 protected:
  void blend(int32_t x, int32_t y, uint16_t c, float alpha);
  int16_t w_, h_;
  std::vector<uint16_t> px_;
};

// 4-bit sprite, only the calls TftCanvas makes: pixels hold palette indices (TftCanvas draws
// into it with indices), pushSprite() writes their colours to the panel.
class TFT_eSprite : public TFT_eSPI {
 public:
  explicit TFT_eSprite(TFT_eSPI* parent) : TFT_eSPI(0, 0), parent_(parent) {}
  void* getPointer() { return px_.empty() ? nullptr : px_.data(); }
  void* createSprite(int16_t w, int16_t h) {
    w_ = w;
    h_ = h;
    px_.assign((size_t)w * h, 0);
    return px_.data();
  }
  void deleteSprite() {
    w_ = h_ = 0;
    px_.clear();
  }
  void setColorDepth(int8_t) {}
  void setPaletteColor(uint8_t i, uint16_t c) { pal_[i & 15] = c; }
  void pushSprite(int32_t x, int32_t y) {
    for (int32_t j = 0; j < h_; j++) {
      for (int32_t i = 0; i < w_; i++) parent_->drawPixel(x + i, y + j, pal_[px_[(size_t)j * w_ + i] & 15]);
    }
  }

 private:
  TFT_eSPI* parent_;
  uint16_t pal_[16] = {};
};
