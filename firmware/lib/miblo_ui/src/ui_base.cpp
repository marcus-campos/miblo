#include <qrcode.h>

#include "ui_screens.h"

namespace screens {

namespace color = ui::color;

static ui::Canvas* g_canvas = nullptr;
static int16_t g_w = 240;
static int16_t g_h = 240;
static miblo::RegionCache g_cache;

void bind(ui::Canvas& c) {
  g_canvas = &c;
  g_w = c.spec().w;
  g_h = c.spec().h;
  g_cache.invalidate();
}

ui::Canvas& canvas() { return *g_canvas; }

int X(int v) { return v * g_w / 240; }
int Y(int v) { return v * g_h / 240; }
int Sz(int v) { return v * (g_w < g_h ? g_w : g_h) / 240; }

void reset() {
  g_canvas->fillRect(0, 0, g_w, g_h, color::BG);
  g_cache.invalidate();
}

bool region(uint8_t id, uint32_t hash, int x, int y, int w, int h, uint16_t bg) {
  if (!g_cache.changed(id, hash)) return false;
  g_canvas->fillRect(x, y, w, h, bg);
  return true;
}

const char* t(Lang lang, miblo::S id) {
  static char buf[4][128];
  static uint8_t next = 0;
  char* b = buf[next];
  next = (uint8_t)((next + 1) % 4);
  miblo::tr(lang, id, b, sizeof(buf[0]));
  return b;
}

void bar(int x, int y, int w, int h, uint8_t pct, uint16_t fg) {
  const int r = h / 2;
  g_canvas->fillRoundRect(x, y, w, h, r, color::TRACK);
  const int fw = (int)((long)w * (pct > 100 ? 100 : pct) / 100);
  if (fw >= h) g_canvas->fillRoundRect(x, y, fw, h, r, fg);
  else if (fw > 0) g_canvas->fillRect(x, y, fw, h, fg);
}

void check(int cx, int cy, int size, uint16_t c) {
  const int w = size / 6 < 2 ? 2 : size / 6;
  g_canvas->wideLine(cx - size / 2, cy, cx - size / 6, cy + size / 3, w, c, color::BG);
  g_canvas->wideLine(cx - size / 6, cy + size / 3, cx + size / 2, cy - size / 3, w, c, color::BG);
}

void mascot(int cx, int cy, uint8_t frame) {
  // Placeholder do Miblo: bolha coral com olhos. 0 = normal, 1 = piscando, 2 = pulinho.
  const int u = Sz(1) < 1 ? 1 : Sz(1);
  const int bounce = (frame % 3 == 2) ? -6 * u : 0;
  g_canvas->fillRect(cx - 46 * u, cy - 46 * u, 92 * u, 92 * u, color::BG);
  const int top = cy - 32 * u + bounce;
  g_canvas->fillRoundRect(cx - 40 * u, top, 80 * u, 60 * u, 24 * u, color::CORAL);
  g_canvas->fillRect(cx - 26 * u, top + 58 * u, 12 * u, 8 * u, color::CORAL);  // pezinhos
  g_canvas->fillRect(cx + 14 * u, top + 58 * u, 12 * u, 8 * u, color::CORAL);
  const int eyeY = top + 26 * u;
  if (frame % 3 == 1) {
    g_canvas->fillRect(cx - 22 * u, eyeY - u, 14 * u, 3 * u, color::BG);
    g_canvas->fillRect(cx + 8 * u, eyeY - u, 14 * u, 3 * u, color::BG);
  } else {
    g_canvas->fillCircle(cx - 15 * u, eyeY, 7 * u, color::WHITE);
    g_canvas->fillCircle(cx + 15 * u, eyeY, 7 * u, color::WHITE);
    g_canvas->fillCircle(cx - 13 * u, eyeY + u, 3 * u, color::BG);
    g_canvas->fillCircle(cx + 17 * u, eyeY + u, 3 * u, color::BG);
  }
}

void qr(const char* payload, int x, int y, int scale) {
  QRCode code;
  uint8_t data[(29 * 29 + 7) / 8];  // = qrcode_getBufferSize(3): versão 3, 29×29 módulos
  qrcode_initText(&code, data, 3, ECC_LOW, payload);
  const int quiet = 2;
  const int size = (code.size + quiet * 2) * scale;
  g_canvas->fillRect(x, y, size, size, color::WHITE);
  for (uint8_t my = 0; my < code.size; my++) {
    for (uint8_t mx = 0; mx < code.size; mx++) {
      if (qrcode_getModule(&code, mx, my)) {
        g_canvas->fillRect(x + (mx + quiet) * scale, y + (my + quiet) * scale, scale, scale, color::BLACK);
      }
    }
  }
}

}  // namespace screens
