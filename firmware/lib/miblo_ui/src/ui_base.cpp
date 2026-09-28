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

uint8_t mascotPose(uint8_t frame) {
  // Idle-heavy 8-frame loop (~3.2 s at 400 ms/frame), as in docs/mascot/options.html.
  static const uint8_t kSeq[8] = {0, 0, 1, 0, 2, 0, 3, 0};
  return kSeq[frame % 8];
}

namespace {
// Mascot drawing helper: design units (a 96x96 box centred on the anchor) scaled by
// u = num / den with round-half-up, exactly like the JS scaler in docs/mascot/options.html.
struct MascotPen {
  ui::Canvas& g;
  int cx, cy, num, den;
  int s(int v) const {
    const long n = 2L * v * num + den;  // floor(v * u + 0.5)
    const long d = 2L * den;
    return (int)(n >= 0 ? n / d : -((-n + d - 1) / d));
  }
  int w(int v) const { return s(v) < 1 ? 1 : s(v); }
  void rect(int x, int y, int ww, int h, uint16_t c) { g.fillRect(cx + s(x), cy + s(y), w(ww), w(h), c); }
  void rrect(int x, int y, int ww, int h, int r, uint16_t c) {
    g.fillRoundRect(cx + s(x), cy + s(y), w(ww), w(h), s(r), c);
  }
  void circle(int x, int y, int r, uint16_t c) { g.fillCircle(cx + s(x), cy + s(y), w(r), c); }
  void line(int x0, int y0, int x1, int y1, int ww, uint16_t c, uint16_t bg) {
    g.wideLine(cx + s(x0), cy + s(y0), cx + s(x1), cy + s(y1), w(ww), c, bg);
  }
  void arc(int x, int y, int r, int ir, int a0, int a1, uint16_t c) {
    const int sr = s(r), sir = s(ir) < sr - 1 ? s(ir) : sr - 1;
    g.arc(cx + s(x), cy + s(y), sr, sir, a0, a1, c, color::SKIN);
  }
  // Triangular ear: three fanned round-capped lines from the tip to the base, plus the inner ear.
  void ear(int tx, int ty, int b0x, int b0y, int b1x, int b1y, int ix0, int iy0, int ix1, int iy1) {
    line(tx, ty, b0x, b0y, 10, color::SKIN, color::BG);
    line(tx, ty, b1x, b1y, 10, color::SKIN, color::BG);
    line(tx, ty, (b0x + b1x) / 2, (b0y + b1y) / 2, 10, color::SKIN, color::BG);
    line(ix0, iy0, ix1, iy1, 5, color::EAR_IN, color::SKIN);
  }
};
}  // namespace

void mascot(int cx, int cy, uint8_t frame, bool small) {
  // Option "A+B" of docs/mascot/options.html: A's geometric Sphynx head (big fanned ears,
  // forehead wrinkles, nose, "w" mouth) with B's round green eyes and happy ^^ blink.
  // Poses: 0 idle, 1 blink, 2 ear twitch, 3 glance. `small` = 48 px variant (no wrinkles,
  // mouth or eye highlights).
  const int m = g_w < g_h ? g_w : g_h;
  MascotPen d{*g_canvas, cx, cy, m, small ? 480 : 240};
  const uint8_t pose = mascotPose(frame);
  d.rect(-48, -48, 96, 96, color::BG);
  if (pose == 2) d.ear(-42, -32, -28, 0, -8, -14, -36, -26, -18, -8);
  else d.ear(-34, -40, -28, 0, -8, -14, -30, -32, -18, -8);
  d.ear(34, -40, 28, 0, 8, -14, 30, -32, 18, -8);
  d.rrect(-30, -18, 60, 52, 22, color::SKIN);
  if (!small) {
    d.arc(0, 24, 36, 34, 167, 193, color::WRINKLE);  // forehead wrinkles, concentric
    d.arc(0, 24, 32, 30, 164, 196, color::WRINKLE);
    d.arc(0, 24, 28, 26, 160, 200, color::WRINKLE);
  }
  if (pose == 1) {
    d.arc(-14, 10, 6, 4, 120, 240, color::PUPIL);  // happy closed eyes
    d.arc(14, 10, 6, 4, 120, 240, color::PUPIL);
  } else {
    const int gx = pose == 3 ? 3 : 0;
    d.circle(-14, 8, 7, color::EYE_GREEN);
    d.circle(14, 8, 7, color::EYE_GREEN);
    d.rrect(-16 + gx, 3, 4, 10, 2, color::PUPIL);  // slit pupils
    d.rrect(12 + gx, 3, 4, 10, 2, color::PUPIL);
    if (!small) {
      d.circle(-11, 5, 2, color::WHITE);
      d.circle(17, 5, 2, color::WHITE);
    }
  }
  d.rrect(-4, 18, 8, 5, 2, color::NOSE);
  if (!small) {
    d.arc(-3, 23, 4, 2, 300, 60, color::WRINKLE);  // "w" mouth
    d.arc(3, 23, 4, 2, 300, 60, color::WRINKLE);
  }
}

void qr(const char* payload, int x, int y, int scale) {
  QRCode code;
  uint8_t data[(29 * 29 + 7) / 8];  // = qrcode_getBufferSize(3): version 3, 29x29 modules
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
