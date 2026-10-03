// Special days on screen: Friday the 13th's stranger crossing pet mode.
#include "miblo_occasions.h"
#include "miblo_overview.h"
#include "miblo_rom.h"
#include "ui_internal.h"
#include "ui_screens.h"

namespace screens {

using miblo::hashInt;
using miblo::kHashSeed;

namespace {
constexpr int kPassHalf = 32;           // the black cat's half size (240 grid)
constexpr uint32_t kPassStopAt = 3400;  // it stops halfway ...
constexpr uint32_t kPassStopMs = 1600;  // ... looks at you (and blinks once), and walks on
constexpr uint32_t kPassStepMs = 180;   // one step: a bob and the other paw
constexpr int kPassStrips = 8;          // composed in strips, like the desk cat (no big layer)
// The cat's tail (the stranger is a cat), raised behind it: a curve leaning back with a hook at the tip (a "?"),
// swaying with `f`. (x, y) is its base; it goes up Sz(30) and back Sz(10).
void tail(int x, int y, uint8_t f) {
  static const int8_t kPath[][2] MIBLO_ROM = {{0, 0},    {-2, -4},  {-4, -8},  {-5, -12}, {-6, -16},
                                              {-6, -20}, {-5, -24}, {-3, -27}, {0, -28},  {2, -26}};
  const int sw = (int)(f % 8 < 4 ? f % 8 : 8 - f % 8) - 2;  // -2..2
  const uint16_t c = mascotSkin();
  for (size_t i = 0; i < sizeof(kPath) / sizeof(kPath[0]); i++) {
    int8_t p[2];
    mibloRomCopy(p, kPath[i], sizeof(p));
    szDisc(x, y, p[0] + sw * (int)i / 3, p[1], 3, c);  // the higher, the more it sways
  }
}
}  // namespace

// A stranger: a dark one of our own pet's kind (strangerPaint: the black preset) walks along the
// bottom of the screen from left to right in kPasserbyMs, stopping halfway to look at you, its
// tail swinging (a cat's curl, or the pet's own Tail antic). No sign, only the clock. Our colours,
// eye shape, tie and mood are not its own; it wears what guests wear (today's holiday hat).
void passerby(Lang lang, const miblo::Snapshot& s, const Clock& clk, uint32_t ms) {
  (void)lang;
  (void)s;
  field(R_CLOCK, kHashSeed + 71, X(120), Y(18), clk.hhmm, Font::Body, color::DIM, color::BG, Align::Center, X(80));

  const int half = Sz(kPassHalf);
  const bool cat = mascotPaint().pet == 0;  // the curl is the cat's; another pet swings its own tail
  const int back = Sz(42);  // the tail reaches this far left of the centre (a pet's own: either side)
  const int cy = Y(228) - half;
  const int x0 = X(6) + back, x2 = X(236) - (cat ? half : back);
  const int x1 = (x0 + x2) / 2;
  const uint32_t t = ms < miblo::kPasserbyMs ? ms : miblo::kPasserbyMs;
  const uint32_t walkIn = kPassStopAt, walkOut = miblo::kPasserbyMs - kPassStopAt - kPassStopMs;
  const bool stopped = t >= kPassStopAt && t < kPassStopAt + kPassStopMs;
  int cx;
  if (t < kPassStopAt) cx = x0 + (int)((int64_t)(x1 - x0) * t / walkIn);
  else if (stopped) cx = x1;
  else cx = x1 + (int)((int64_t)(x2 - x1) * (t - kPassStopAt - kPassStopMs) / walkOut);

  const bool step = (t / kPassStepMs) % 2;
  MascotLook k{0, (int8_t)(step ? -2 : 0), 3, 0, Eyes::Open, step ? Paws::TapLeft : Paws::TapRight, 0};
  if (stopped) {
    const uint32_t p = t - kPassStopAt;
    k = MascotLook{0, 0, 0, 0, p >= 700 && p < 900 ? Eyes::Closed : Eyes::Open, Paws::Down, 0};
  }
  const uint8_t swing = (uint8_t)(t / 250);

  uint32_t h = hashInt(hashInt(kHashSeed + 73, (uint32_t)cx), (uint32_t)swing);
  h = hashInt(hashInt(h, mascotPaintHash()), guestAccessory());
  h = hashInt(h, (uint32_t)(uint8_t)k.dy | (uint32_t)(uint8_t)k.gx << 8 | (uint32_t)k.eyes << 16 |
                     (uint32_t)k.paws << 24);
  if (!dirty(R_BODY, h)) return;
  const int top = cy - half, bh = 2 * half;
  const uint8_t hat = mascotAccessory(), mood = catMood();
  const MascotPaint paint = mascotPaint();
  const bool tie = mascotTie();
  const MascotOutfit outfit = mascotOutfit();
  auto draw = [&] {
    C().fillRect(0, top, X(240), bh, color::BG);
    C().fillRect(X(6), cy + Sz(27), X(228), Sz(2) > 0 ? Sz(2) : 1, color::DIVIDER);  // the floor it walks on
    if (cat) tail(cx - Sz(30), cy + Sz(24), swing);
    deskMascot(cx, cy, k, kPassHalf, false, false, cat ? 0 : (uint8_t)(swing + 1));
  };
  setMascotPaint(strangerPaint(paint));  // a dark one of our own kind
  setMascotAccessory(guestAccessory());
  setMascotOutfit(MascotOutfit{});  // a stranger: nothing of ours
  setMascotTie(false);
  setCatMood(0);
  const int stripH = (bh + kPassStrips - 1) / kPassStrips;
  for (int y = top; y < top + bh; y += stripH) {
    const int sh = y + stripH <= top + bh ? stripH : top + bh - y;
    if (!C().beginLayer(0, y, X(240), sh)) {  // no memory even for a strip: draw directly
      draw();
      break;
    }
    draw();
    C().endLayer();
  }
  C().releaseLayer();
  setMascotPaint(paint);
  setMascotAccessory(hat);
  setMascotOutfit(outfit);
  setMascotTie(tie);
  setCatMood(mood);
}

}  // namespace screens
