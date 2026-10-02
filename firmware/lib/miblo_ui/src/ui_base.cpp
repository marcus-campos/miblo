#include <qrcode.h>

#include "miblo_rom.h"
#include "ui_screens.h"

namespace screens {

namespace color = ui::color;

static ui::Canvas* g_canvas = nullptr;
static int16_t g_w = 240;
static int16_t g_h = 240;
static int16_t g_sz = 240;  // what Sz() scales by: the smaller side (less while scaleSz() is on)
static miblo::RegionCache g_cache;

void bind(ui::Canvas& c) {
  g_canvas = &c;
  g_w = c.spec().w;
  g_h = c.spec().h;
  g_sz = g_w < g_h ? g_w : g_h;
  g_cache.invalidate();
}

ui::Canvas& canvas() { return *g_canvas; }

int X(int v) { return v * g_w / 240; }
int Y(int v) { return v * g_h / 240; }
int Sz(int v) { return v * g_sz / 240; }
void scaleSz(int num, int den) { g_sz = (int16_t)((g_w < g_h ? g_w : g_h) * num / den); }

void reset() {
  g_canvas->releaseLayer();
  g_canvas->clear(color::BG);
  g_cache.invalidate();
}

bool dirty(uint8_t id, uint32_t hash) { return g_cache.changed(id, hash); }

bool region(uint8_t id, uint32_t hash, int x, int y, int w, int h, uint16_t bg) {
  if (!dirty(id, hash)) return false;
  g_canvas->fillRect(x, y, w, h, bg);
  return true;
}

bool Compose::begin(uint8_t id, uint32_t hash, int x, int y, int w, int h, uint16_t bg) {
  end();
  if (!dirty(id, hash)) return false;
  layered_ = g_canvas->beginLayer(x, y, w, h);
  g_canvas->fillRect(x, y, w, h, bg);
  return true;
}

void Compose::end() {
  if (!layered_) return;
  layered_ = false;
  g_canvas->endLayer();
  g_canvas->releaseLayer();  // rows are recomposed rarely: give the memory back right away
}

bool field(uint8_t id, uint32_t salt, int x, int y, const char* s, ui::Font f, uint16_t fg, uint16_t bg,
           ui::Align a, int boxW) {
  if (!dirty(id, miblo::hashStr(salt, s))) return false;
  g_canvas->textBox(x, y, s, f, fg, bg, a, boxW);
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
  // Idle-heavy 8-frame loop (~3.2 s at 400 ms/frame).
  static const uint8_t kSeq[8] MIBLO_ROM = {0, 0, 1, 0, 2, 0, 3, 0};
  return mibloRomByte((const char*)&kSeq[frame % 8]);
}

namespace {
// Mascot drawing helper: design units (a 96x96 box centred on the anchor) scaled by
// u = num / den with round-half-up.
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

// Mascot colours per style (see setMascotStyle): skin, inner ears, outlines/wrinkles, nose, and
// the dark lines drawn on the skin (closed eyes, mouth), which must contrast with it.
struct MascotColors {
  uint16_t skin, earIn, line, nose, lid;
};
const MascotColors kMascotColors[] MIBLO_ROM = {
    {color::SKIN, color::EAR_IN, color::WRINKLE, color::NOSE, color::PUPIL},  // sphynx #f2b8a8
    {0xF282, 0xFD2F, 0xB1C1, 0xFD2F, color::PUPIL},                         // orange #f55110 (the logo's)
    {0x4A4A, 0x7ACC, 0x2946, 0xD3D1, 0xCE5A},                               // black (#4a4a55: shows on the dark bg)
    {0x9D16, 0xCD15, 0x5B2E, 0xB3D1, color::PUPIL},                         // grey #9aa3b0
};
static uint8_t g_style = 0;
static uint8_t g_accessory = 0;

void setMascotStyle(uint8_t style) {
  g_style = style < sizeof(kMascotColors) / sizeof(kMascotColors[0]) ? style : 0;
}
uint8_t mascotStyle() { return g_style; }
void setMascotAccessory(uint8_t accessory) { g_accessory = accessory; }
uint8_t mascotAccessory() { return g_accessory; }
// The current style's colours (copied out of flash).
static MascotColors mascotColors() {
  MascotColors mc;
  mibloRomCopy(&mc, &kMascotColors[g_style], sizeof(mc));
  return mc;
}
uint16_t mascotSkin() { return mascotColors().skin; }

// Hats for special days (miblo::Accessory), on top of the head. They stay inside the 96-unit box
// even when the cat hops (dy >= -5): nothing may be drawn outside it (no trail).
static void drawHat(MascotPen& d, int x, int b) {
  switch (g_accessory) {
    case 1:  // Santa hat: red, white brim and pompom, tipped to the right
      d.tri(-15 + x, -18 + b, 15 + x, -18 + b, 11 + x, -40 + b, color::RED);
      d.rrect(-17 + x, -22 + b, 34, 7, 3, color::WHITE);
      d.circle(12 + x, -39 + b, 3, color::WHITE);
      break;
    case 2:  // witch hat: wide brim, pointy purple crown with an amber band
      d.rect(-22 + x, -21 + b, 44, 4, 0x3008);
      d.tri(-12 + x, -19 + b, 12 + x, -19 + b, 4 + x, -43 + b, 0x5011);
      d.rect(-11 + x, -24 + b, 22, 3, color::AMBER);
      break;
    case 3:  // party hat: striped cone with a pompom
      d.tri(-10 + x, -18 + b, 10 + x, -18 + b, x, -38 + b, color::VIOLET);
      d.rect(-7 + x, -24 + b, 14, 2, color::AMBER);
      d.rect(-4 + x, -31 + b, 8, 2, color::GREEN);
      d.circle(x, -39 + b, 3, color::AMBER);
      break;
    default: break;
  }
}

// The cat itself. `desk` adds what only the big Desk mascot has: a table edge, front paws and
// the extras (sweat, alarm, zzz, open mouth).
static void drawCat(MascotPen& d, const MascotLook& k, bool innerEars, bool desk, bool table = true,
                    bool box = true) {
  const MascotColors mc = mascotColors();
  const int x = k.dx;
  const int b = k.dy;
  if (box) d.rect(-48, -48, 96, 96, color::BG);
  if (desk && table) d.rect(-48, 40, 96, 2, color::DIVIDER);  // table edge (stays put when it hops)
  d.tri(-36 + x, -42 + b, -32 + x, -4 + b, -8 + x, -18 + b, mc.skin);  // ears
  d.tri(36 + x, -42 + b, 32 + x, -4 + b, 8 + x, -18 + b, mc.skin);
  if (innerEars) {
    d.tri(-31 + x, -33 + b, -28 + x, -10 + b, -14 + x, -17 + b, mc.earIn);
    d.tri(31 + x, -33 + b, 28 + x, -10 + b, 14 + x, -17 + b, mc.earIn);
  }
  d.rrect(-32 + x, -20 + b, 64, 52, 24, mc.skin);  // head
  if (desk && (k.extras & kFluffed)) {  // fur standing up on both cheeks
    for (int s = -1; s <= 1; s += 2) {
      d.tri(s * 31 + x, -8 + b, s * 40 + x, -2 + b, s * 31 + x, 4 + b, mc.skin);
      d.tri(s * 31 + x, 6 + b, s * 41 + x, 12 + b, s * 31 + x, 18 + b, mc.skin);
    }
  }
  switch (k.eyes) {
    case Eyes::Closed:
      d.rect(-22 + x, 5 + b, 16, 3, mc.lid);
      d.rect(6 + x, 5 + b, 16, 3, mc.lid);
      break;
    case Eyes::Happy:  // "^ ^": a chevron cut out of a lid-coloured triangle
      for (int e = -14; e <= 14; e += 28) {
        d.tri(e - 9 + x, 10 + b, e + x, 1 + b, e + 9 + x, 10 + b, mc.lid);
        d.tri(e - 5 + x, 10 + b, e + x, 5 + b, e + 5 + x, 10 + b, mc.skin);
      }
      break;
    case Eyes::Wide:  // big irises, tiny pupils
      d.circle(-14 + x, 6 + b, 10, color::EYE_GREEN);
      d.circle(14 + x, 6 + b, 10, color::EYE_GREEN);
      d.circle(-14 + x + k.gx, 6 + b + k.gy, 3, color::PUPIL);
      d.circle(14 + x + k.gx, 6 + b + k.gy, 3, color::PUPIL);
      break;
    case Eyes::Open:
    case Eyes::Sleepy: {
      const int cross = (k.extras & kCrossEyed) ? 3 : 0;
      d.circle(-14 + x, 6 + b, 8, color::EYE_GREEN);
      d.circle(14 + x, 6 + b, 8, color::EYE_GREEN);
      d.circle(-14 + x + k.gx + cross, 6 + b + k.gy, 4, color::PUPIL);
      d.circle(14 + x + k.gx - cross, 6 + b + k.gy, 4, color::PUPIL);
      if (k.eyes == Eyes::Sleepy) {  // heavy lids over the top half
        d.rect(-23 + x, -3 + b, 18, 8, mc.skin);
        d.rect(5 + x, -3 + b, 18, 8, mc.skin);
        d.rect(-22 + x, 4 + b, 16, 2, mc.lid);
        d.rect(6 + x, 4 + b, 16, 2, mc.lid);
      }
      if (k.extras & kGrumpy) {  // lids slanting down towards the nose
        d.tri(-23 + x, -3 + b, -5 + x, -3 + b, -5 + x, 5 + b, mc.skin);
        d.tri(23 + x, -3 + b, 5 + x, -3 + b, 5 + x, 5 + b, mc.skin);
      }
      break;
    }
    case Eyes::Dizzy:  // hypnotised: rings in the eyes
      for (int e = -14; e <= 14; e += 28) {
        d.circle(e + x, 6 + b, 8, color::EYE_GREEN);
        d.circle(e + x, 6 + b, 6, color::PUPIL);
        d.circle(e + x, 6 + b, 4, color::EYE_GREEN);
        d.circle(e + x, 6 + b, 2, color::PUPIL);
      }
      break;
  }
  d.rrect(-4 + x, 17 + b, 8, 5, 2, mc.nose);
  drawHat(d, x, b);
  if (!desk) return;
  if (k.extras & kMouthO) d.circle(x, 26 + b, 3, mc.lid);
  if (k.extras & kMouthWide) {  // a yawn or a sneeze
    d.rrect(-7 + x, 22 + b, 14, 11, 5, mc.lid);
    d.rect(-4 + x, 28 + b, 8, 4, mc.earIn);
  }
  // A paw: a skin pad with a darker outline and toe lines, so it reads as a paw even over the
  // (skin) head.
  auto paw = [&](int px, int py, int pw, int ph, int r) {
    d.rrect(px + x - 1, py + b - 1, pw + 2, ph + 2, r + 1, mc.line);
    d.rrect(px + x, py + b, pw, ph, r, mc.skin);
    for (int t = 1; t <= 2; t++) d.rect(px + x + pw * t / 3, py + b + 1, 1, ph / 2, mc.line);
  };
  switch (k.paws) {
    case Paws::Down:
      paw(-26, 28, 16, 11, 5);
      paw(10, 28, 16, 11, 5);
      break;
    case Paws::ReachLeft:  // batting at the left gauge
      paw(-46, 24, 16, 11, 5);
      paw(10, 28, 16, 11, 5);
      break;
    case Paws::ReachRight:
      paw(-26, 28, 16, 11, 5);
      paw(30, 24, 16, 11, 5);
      break;
    case Paws::Cover:  // can't look: both paws over the eyes, all of them
      paw(-28, -5, 24, 21, 6);
      paw(4, -5, 24, 21, 6);
      break;
    case Paws::Up:  // stretching: both paws up beside the ears
      paw(-46, -34, 16, 11, 5);
      paw(30, -34, 16, 11, 5);
      break;
    case Paws::Lick:  // the left paw raised under the mouth, beside the tongue (over the mouth it read as a snout)
      paw(-17, 20, 14, 17, 5);
      paw(10, 28, 16, 11, 5);
      break;
    case Paws::TapLeft:  // the left paw lifted (typing, playing keys)
      paw(-26, 23, 16, 11, 5);
      paw(10, 28, 16, 11, 5);
      break;
    case Paws::TapRight:
      paw(-26, 28, 16, 11, 5);
      paw(10, 23, 16, 11, 5);
      break;
  }
  if (k.extras & kTongue) d.rrect(-3 + x, 22 + b, 6, 6, 2, mc.earIn);
  if (k.extras & kCoffee) {  // a cup held up next to the right paw, steaming
    d.rect(29 + x, 6 + b, 2, 5, color::MUTED);
    d.rect(34 + x, 4 + b, 2, 6, color::MUTED);
    d.rect(26 + x, 13 + b, 13, 13, color::WHITE);
    d.rect(27 + x, 14 + b, 11, 3, 0x6A20);  // the coffee
    d.circle(41 + x, 19 + b, 3, color::WHITE);
    d.circle(41 + x, 19 + b, 1, color::BG);
  }
  if (k.extras & kHeart) {  // beside the left ear, fixed like the alarm marks
    d.circle(-44, -40, 3, color::RED);
    d.circle(-39, -40, 3, color::RED);
    d.tri(-47, -39, -36, -39, -41, -32, color::RED);
  }
  if (k.extras & kSweat) {
    d.tri(36 + x, -12 + b, 32 + x, -3 + b, 40 + x, -3 + b, color::BLUE);
    d.circle(36 + x, -2 + b, 4, color::BLUE);
  }
  // Marks beside the right ear: fixed, so a shiver or a hop never pushes them out of the box.
  if (k.extras & kAlarm) {
    d.rect(41, -46, 5, 13, color::RED);
    d.rect(41, -30, 5, 5, color::RED);
  }
  if (k.extras & kZ1) {
    d.rect(38, -22, 9, 2, color::MUTED);
    d.tri(44, -20, 47, -20, 38, -16, color::MUTED);
    d.tri(47, -20, 41, -16, 38, -16, color::MUTED);
    d.rect(38, -16, 9, 2, color::MUTED);
  }
  if (k.extras & kZ2) {
    d.rect(40, -34, 6, 2, color::DIM);
    d.tri(43, -32, 46, -32, 40, -29, color::DIM);
    d.tri(46, -32, 43, -29, 40, -29, color::DIM);
    d.rect(40, -29, 6, 2, color::DIM);
  }
  if (k.extras & kStars) {  // dizzy: little stars above the head, fixed like the zzz
    for (int sx = -32; sx <= 26; sx += 29) {
      d.rect(sx, -45, 7, 2, color::AMBER);
      d.rect(sx + 3, -48, 2, 8, color::AMBER);
    }
  }
}

// Miblo's logo: the mascot's head as a 22x20 silhouette (bit 21 = leftmost pixel); the eyes are
// holes, so they show the background. Drawn in the mascot's colour, one run of pixels at a time.
static const uint32_t kLogo[20] MIBLO_ROM = {0x0C000C, 0x0E001C, 0x1F003E, 0x1F807E, 0x1FDEFE, 0x1FFFFE, 0x1FFFFE, 0x1FFFFE, 0x1FFFFE, 0x1FFFFE, 0x1FFFFE, 0x1F3F3E, 0x1E3F1E, 0x3E3F1F, 0x3F3F3F, 0x3FFFFF, 0x3FFFFF, 0x3FFFFF, 0x03FFF0, 0x001E00};
constexpr int kLogoW = 22;
constexpr int kLogoH = 20;

void logo(int cx, int cy, int size) {
  const uint16_t c = mascotSkin();
  const int px = size / kLogoW < 1 ? 1 : size / kLogoW;  // whole pixels: crisp at any scale
  const int left = cx - kLogoW * px / 2;
  const int top = cy - kLogoH * px / 2;
  for (int y = 0; y < kLogoH; y++) {
    uint32_t row;
    mibloRomCopy(&row, &kLogo[y], sizeof(row));
    for (int x = 0; x < kLogoW;) {
      if (!(row & (1u << (kLogoW - 1 - x)))) {
        x++;
        continue;
      }
      const int x0 = x;
      while (x < kLogoW && (row & (1u << (kLogoW - 1 - x)))) x++;
      g_canvas->fillRect(left + x0 * px, top + y * px, (x - x0) * px, px, c);
    }
  }
}

void mascot(int cx, int cy, uint8_t frame, bool small) {
  // Simplified Sphynx: flat shapes only, so a frame is
  // cheap and renders the same on an off-screen 16-colour layer. Big triangular ears with pink
  // insides, a round peach head, round green eyes with dark pupils and a small pink nose.
  // Poses: 0 idle, 1 blink (eyes become thin lines), 2 hop (whole cat up a bit), 3 glance
  // (pupils to the side). `small` = 48 px variant (no inner ears).
  const int m = g_w < g_h ? g_w : g_h;
  MascotPen d{*g_canvas, cx, cy, m, small ? 480 : 240};
  const uint8_t pose = mascotPose(frame);
  MascotLook k{0, 0, 0, 0, Eyes::Open, Paws::Down, 0};
  if (pose == 1) k.eyes = Eyes::Closed;
  if (pose == 2) k.dy = -4;
  if (pose == 3) k.gx = 3;
  drawCat(d, k, !small, false);
}

void deskMascot(int cx, int cy, const MascotLook& look, int half, bool table, bool box) {
  MascotPen d{*g_canvas, cx, cy, Sz(half), 48};  // the 96-unit box drawn exactly 2 * Sz(half) wide
  drawCat(d, look, true, true, table, box);
}

void qr(const char* payload, int x, int y, int scale) {
  QRCode code;
  // Version 3 (29x29 modules), the one the firmware builds the QR library for (LOCK_VERSION=3).
  static_assert(LOCK_VERSION == 0 || LOCK_VERSION == 3, "the QR library is locked to another version");
  uint8_t data[(29 * 29 + 7) / 8];  // = qrcode_getBufferSize(3)
  // The library never checks capacity: a payload longer than version 3 holds (53 bytes in byte
  // mode, ECC_LOW) would overrun its buffers. Such a QR is not drawn at all.
  if (strlen(payload) > 53) return;
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
