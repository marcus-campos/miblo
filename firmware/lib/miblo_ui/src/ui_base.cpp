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
  g_canvas->releaseLayer();
  g_canvas->fillRect(0, 0, g_w, g_h, color::BG);
  g_cache.invalidate();
}

bool dirty(uint8_t id, uint32_t hash) { return g_cache.changed(id, hash); }

bool region(uint8_t id, uint32_t hash, int x, int y, int w, int h, uint16_t bg) {
  if (!dirty(id, hash)) return false;
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
  void tri(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) {
    g.fillTriangle(cx + s(x0), cy + s(y0), cx + s(x1), cy + s(y1), cx + s(x2), cy + s(y2), c);
  }
};
}  // namespace

void mascot(int cx, int cy, uint8_t frame, bool small) {
  // Simplified Sphynx (docs/mascot/options.html, "shipped"): flat shapes only, so a frame is
  // cheap and renders the same on an off-screen 16-colour layer. Big triangular ears with pink
  // insides, a round peach head, round green eyes with dark pupils and a small pink nose.
  // Poses: 0 idle, 1 blink (eyes become thin lines), 2 hop (whole cat up a bit), 3 glance
  // (pupils to the side). `small` = 48 px variant (no inner ears).
  const int m = g_w < g_h ? g_w : g_h;
  MascotPen d{*g_canvas, cx, cy, m, small ? 480 : 240};
  const uint8_t pose = mascotPose(frame);
  const int b = pose == 2 ? -4 : 0;  // hop
  d.rect(-48, -48, 96, 96, color::BG);
  d.tri(-36, -42 + b, -32, -4 + b, -8, -18 + b, color::SKIN);  // ears
  d.tri(36, -42 + b, 32, -4 + b, 8, -18 + b, color::SKIN);
  if (!small) {
    d.tri(-31, -33 + b, -28, -10 + b, -14, -17 + b, color::EAR_IN);
    d.tri(31, -33 + b, 28, -10 + b, 14, -17 + b, color::EAR_IN);
  }
  d.rrect(-32, -20 + b, 64, 52, 24, color::SKIN);  // head
  if (pose == 1) {
    d.rect(-22, 5 + b, 16, 3, color::PUPIL);  // closed eyes
    d.rect(6, 5 + b, 16, 3, color::PUPIL);
  } else {
    const int gx = pose == 3 ? 3 : 0;
    d.circle(-14, 6 + b, 8, color::EYE_GREEN);
    d.circle(14, 6 + b, 8, color::EYE_GREEN);
    d.circle(-14 + gx, 6 + b, 4, color::PUPIL);
    d.circle(14 + gx, 6 + b, 4, color::PUPIL);
  }
  d.rrect(-4, 17 + b, 8, 5, 2, color::NOSE);
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
