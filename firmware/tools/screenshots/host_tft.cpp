// Framebuffer implementation of the host TFT_eSPI stand-in (shim/TFT_eSPI.h).
#include <math.h>

#include <algorithm>
#include <cstdlib>

#include "TFT_eSPI.h"

HostEsp ESP;

namespace {
constexpr int kSub = 4;  // anti-aliasing: kSub x kSub samples per pixel

uint16_t mix565(uint16_t fg, uint16_t bg, float a) {
  const int fr = fg >> 11, fg6 = (fg >> 5) & 63, fb = fg & 31;
  const int br = bg >> 11, bg6 = (bg >> 5) & 63, bb = bg & 31;
  const int r = (int)lroundf(br + (fr - br) * a);
  const int g = (int)lroundf(bg6 + (fg6 - bg6) * a);
  const int b = (int)lroundf(bb + (fb - bb) * a);
  return (uint16_t)((r << 11) | (g << 5) | b);
}
}  // namespace

void TFT_eSPI::drawPixel(int32_t x, int32_t y, uint32_t c) {
  if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
  px_[(size_t)y * w_ + x] = (uint16_t)c;
}

void TFT_eSPI::blend(int32_t x, int32_t y, uint16_t c, float alpha) {
  if (x < 0 || y < 0 || x >= w_ || y >= h_ || alpha <= 0.0f) return;
  uint16_t& p = px_[(size_t)y * w_ + x];
  p = alpha >= 1.0f ? c : mix565(c, p, alpha);
}

void TFT_eSPI::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) {
  for (int32_t j = 0; j < h; j++) {
    for (int32_t i = 0; i < w; i++) drawPixel(x + i, y + j, c);
  }
}

void TFT_eSPI::drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) {
  if (w <= 0 || h <= 0) return;
  fillRect(x, y, w, 1, c);
  fillRect(x, y + h - 1, w, 1, c);
  fillRect(x, y, 1, h, c);
  fillRect(x + w - 1, y, 1, h, c);
}

// A pixel belongs to a filled circle of radius r (diameter 2r+1) like TFT_eSPI's midpoint fill.
static bool inCircle(int32_t dx, int32_t dy, int32_t r) { return dx * dx + dy * dy <= r * r + r; }

void TFT_eSPI::fillCircle(int32_t x, int32_t y, int32_t r, uint32_t c) {
  for (int32_t dy = -r; dy <= r; dy++) {
    for (int32_t dx = -r; dx <= r; dx++) {
      if (inCircle(dx, dy, r)) drawPixel(x + dx, y + dy, c);
    }
  }
}

void TFT_eSPI::fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c) {
  if (w <= 0 || h <= 0) return;
  r = std::max<int32_t>(0, std::min(r, std::min(w, h) / 2));
  const int32_t l = x + r, rt = x + w - 1 - r, t = y + r, b = y + h - 1 - r;
  for (int32_t py = y; py < y + h; py++) {
    for (int32_t px = x; px < x + w; px++) {
      const int32_t cx = px < l ? l : (px > rt ? rt : px);
      const int32_t cy = py < t ? t : (py > b ? b : py);
      if (inCircle(px - cx, py - cy, r)) drawPixel(px, py, c);
    }
  }
}

void TFT_eSPI::fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t c) {
  const int32_t minX = std::min({x0, x1, x2}), maxX = std::max({x0, x1, x2});
  const int32_t minY = std::min({y0, y1, y2}), maxY = std::max({y0, y1, y2});
  const float area = (float)(x1 - x0) * (y2 - y0) - (float)(x2 - x0) * (y1 - y0);
  if (area == 0.0f) {  // degenerate (a line or a point): TFT_eSPI's own scanline spans
    if (y0 > y1) std::swap(y0, y1), std::swap(x0, x1);
    if (y1 > y2) std::swap(y1, y2), std::swap(x1, x2);
    if (y0 > y1) std::swap(y0, y1), std::swap(x0, x1);
    if (y0 == y2) {
      fillRect(minX, y0, maxX - minX + 1, 1, c);
      return;
    }
    const int32_t dx01 = x1 - x0, dy01 = y1 - y0, dx02 = x2 - x0, dy02 = y2 - y0, dx12 = x2 - x1, dy12 = y2 - y1;
    int32_t sa = 0, sb = 0, y = y0;
    const int32_t last = y1 == y2 ? y1 : y1 - 1;
    for (; y <= last; y++) {
      int32_t a = x0 + sa / dy01, b = x0 + sb / dy02;
      sa += dx01;
      sb += dx02;
      if (a > b) std::swap(a, b);
      fillRect(a, y, b - a + 1, 1, c);
    }
    sa = dx12 * (y - y1);
    sb = dx02 * (y - y0);
    for (; y <= y2; y++) {
      int32_t a = x1 + sa / dy12, b = x0 + sb / dy02;
      sa += dx12;
      sb += dx02;
      if (a > b) std::swap(a, b);
      fillRect(a, y, b - a + 1, 1, c);
    }
    return;
  }
  auto edge = [](float ax, float ay, float bx, float by, float px, float py) {
    // signed distance of p to the line a->b
    const float len = sqrtf((bx - ax) * (bx - ax) + (by - ay) * (by - ay));
    return ((bx - ax) * (py - ay) - (by - ay) * (px - ax)) / (len > 0 ? len : 1);
  };
  const float s = area > 0 ? 1.0f : -1.0f;
  for (int32_t py = minY; py <= maxY; py++) {
    for (int32_t px = minX; px <= maxX; px++) {
      // pixel centres within half a pixel of the triangle: the edges are included, as on the panel
      if (s * edge(x0, y0, x1, y1, px, py) >= -0.5f && s * edge(x1, y1, x2, y2, px, py) >= -0.5f &&
          s * edge(x2, y2, x0, y0, px, py) >= -0.5f) {
        drawPixel(px, py, c);
      }
    }
  }
}

// TFT_eSprite::drawLine's Bresenham, pixel for pixel.
void TFT_eSPI::drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t c) {
  const bool steep = std::abs(y1 - y0) > std::abs(x1 - x0);
  if (steep) std::swap(x0, y0), std::swap(x1, y1);
  if (x0 > x1) std::swap(x0, x1), std::swap(y0, y1);
  const int32_t dx = x1 - x0, dy = std::abs(y1 - y0), ystep = y0 < y1 ? 1 : -1;
  int32_t err = dx >> 1, xs = x0, dlen = 0;
  for (; x0 <= x1; x0++) {
    dlen++;
    err -= dy;
    if (err < 0) {
      err += dx;
      if (steep) fillRect(y0, xs, 1, dlen, c);
      else fillRect(xs, y0, dlen, 1, c);
      dlen = 0;
      y0 += ystep;
      xs = x0 + 1;
    }
  }
  if (dlen) {
    if (steep) fillRect(y0, xs, 1, dlen, c);
    else fillRect(xs, y0, dlen, 1, c);
  }
}

void TFT_eSPI::drawWideLine(float ax, float ay, float bx, float by, float wd, uint32_t fg, uint32_t) {
  const float r = wd / 2.0f;
  const int32_t minX = (int32_t)floorf(std::min(ax, bx) - r - 1), maxX = (int32_t)ceilf(std::max(ax, bx) + r + 1);
  const int32_t minY = (int32_t)floorf(std::min(ay, by) - r - 1), maxY = (int32_t)ceilf(std::max(ay, by) + r + 1);
  const float dx = bx - ax, dy = by - ay, len2 = dx * dx + dy * dy;
  for (int32_t py = minY; py <= maxY; py++) {
    for (int32_t px = minX; px <= maxX; px++) {
      int hits = 0;
      for (int sy = 0; sy < kSub; sy++) {
        for (int sx = 0; sx < kSub; sx++) {
          const float qx = px + (sx + 0.5f) / kSub - 0.5f, qy = py + (sy + 0.5f) / kSub - 0.5f;
          float t = len2 > 0 ? ((qx - ax) * dx + (qy - ay) * dy) / len2 : 0.0f;
          t = std::max(0.0f, std::min(1.0f, t));
          const float ex = qx - (ax + t * dx), ey = qy - (ay + t * dy);
          if (ex * ex + ey * ey <= r * r) hits++;
        }
      }
      blend(px, py, (uint16_t)fg, (float)hits / (kSub * kSub));
    }
  }
}

void TFT_eSPI::drawSmoothArc(int32_t x, int32_t y, int32_t r, int32_t ir, uint32_t startAngle, uint32_t endAngle,
                             uint32_t fg, uint32_t, bool roundEnds) {
  if (endAngle <= startAngle) return;
  const float a0 = (float)startAngle, a1 = (float)endAngle;
  const float midR = (r + ir) / 2.0f, capR = (r - ir) / 2.0f;
  auto capAt = [&](float deg, float& cx, float& cy) {  // 0 degrees = 6 o'clock, clockwise
    const float t = deg * (float)M_PI / 180.0f;
    cx = x - midR * sinf(t);
    cy = y + midR * cosf(t);
  };
  float c0x, c0y, c1x, c1y;
  capAt(a0, c0x, c0y);
  capAt(a1, c1x, c1y);
  for (int32_t py = y - r - 1; py <= y + r + 1; py++) {
    for (int32_t px = x - r - 1; px <= x + r + 1; px++) {
      int hits = 0;
      for (int sy = 0; sy < kSub; sy++) {
        for (int sx = 0; sx < kSub; sx++) {
          const float qx = px + (sx + 0.5f) / kSub - 0.5f - x, qy = py + (sy + 0.5f) / kSub - 0.5f - y;
          const float d = sqrtf(qx * qx + qy * qy);
          bool in = false;
          if (d >= ir && d <= r) {
            float deg = atan2f(-qx, qy) * 180.0f / (float)M_PI;
            if (deg < 0) deg += 360.0f;
            in = deg >= a0 && deg <= a1;
          }
          if (!in && roundEnds) {
            const float ux = qx + x, uy = qy + y;
            in = (ux - c0x) * (ux - c0x) + (uy - c0y) * (uy - c0y) <= capR * capR ||
                 (ux - c1x) * (ux - c1x) + (uy - c1y) * (uy - c1y) <= capR * capR;
          }
          if (in) hits++;
        }
      }
      blend(px, py, (uint16_t)fg, (float)hits / (kSub * kSub));
    }
  }
}
