// The alien pet (miblo::Pet::Alien): a soft rounded
// head, one big eye, two antennae with glowing tips (the accent), a tiny body and three-fingered
// hands. See ui_pet.h for the contract.
#include "miblo_mood.h"
#include "miblo_rom.h"
#include "ui_pet.h"

namespace screens {

namespace color = ui::color;

namespace {

constexpr int kEyeY = 2;  // the one eye's centre (x 0)
constexpr uint16_t kGlow = 0x262B;  // #22c55e: the antennae's Auto glow, apart from every preset body

// The one big eye at (0, kEyeY): a white with an outline, the iris and the pupil, in every Eyes
// state and eye shape. The gaze moves it twice as far as two small eyes would, so it reads.
void alienEye(const PetCtx& c) {
  MascotPen& d = c.d;
  const MascotLook& k = c.k;
  const PetColors& mc = c.mc;
  const int x = c.x, b = c.b, y = kEyeY;
  const bool big = mc.eyeShape == (uint8_t)miblo::EyeShape::Big;
  const int gx = 2 * k.gx, gy = 2 * k.gy;
  switch (k.eyes) {
    case Eyes::Closed:  // a lid line curving down a little
      d.rect(-14 + x, y - 1 + b, 4, 3, mc.lid);
      d.rect(-11 + x, y + 1 + b, 22, 3, mc.lid);
      d.rect(10 + x, y - 1 + b, 4, 3, mc.lid);
      break;
    case Eyes::Happy:  // one big "^"
      d.tri(-14 + x, y + 7 + b, x, y - 7 + b, 14 + x, y + 7 + b, mc.lid);
      d.tri(-8 + x, y + 7 + b, x, y - 1 + b, 8 + x, y + 7 + b, mc.skin);
      break;
    case Eyes::Dizzy:  // hypnotised: rings
      d.circle(x, y + b, 15, mc.line);
      d.circle(x, y + b, 14, color::WHITE);
      d.circle(x, y + b, 11, mc.eye);
      d.circle(x, y + b, 8, mc.pupil);
      d.circle(x, y + b, 5, mc.eye);
      d.circle(x, y + b, 2, mc.pupil);
      break;
    case Eyes::Wide:  // the white wider, the iris big, the pupil tiny
      d.circle(x, y + b, 17, mc.line);
      d.circle(x, y + b, 16, color::WHITE);
      d.circle(x + gx, y + b + gy, big ? 11 : 10, mc.eye);
      d.circle(x + gx, y + b + gy, 3, mc.pupil);
      if (big) d.circle(x + gx - 5, y - 4 + b + gy, 2, color::WHITE);
      break;
    case Eyes::Open:
    case Eyes::Sleepy: {
      d.circle(x, y + b, 15, mc.line);
      d.circle(x, y + b, 14, color::WHITE);
      if (k.extras & kCrossEyed) {  // one eye can't cross: it rolls down to stare at its own nose
        d.circle(x, y + 5 + b, big ? 9 : 8, mc.eye);
        d.circle(x, y + 8 + b, 3, mc.pupil);
      } else {
        d.circle(x + gx, y + b + gy, big ? 10 : 8, mc.eye);
        d.circle(x + gx, y + b + gy, big ? 5 : 4, mc.pupil);
        if (big) {  // shiny: a big and a small highlight
          d.circle(x + gx - 4, y - 4 + b + gy, 2, color::WHITE);
          d.circle(x + gx + 4, y + 4 + b + gy, 1, color::WHITE);
        }
      }
      int lid = 0;  // how far the lid comes down from the top of the eye
      if (k.eyes == Eyes::Sleepy) lid = 15;
      else if (mc.eyeShape == (uint8_t)miblo::EyeShape::Sleepy) lid = 10;
      if (lid) {
        d.rect(-16 + x, y - 16 + b, 32, lid + 1, mc.skin);
        d.rect(-14 + x, y - 16 + lid + b, 28, 2, mc.lid);
      }
      if (k.extras & kGrumpy) {  // the lid frowning down to the middle: a "V"
        d.tri(-17 + x, y - 16 + b, 17 + x, y - 16 + b, x, y - 3 + b, mc.skin);
        d.tri(-15 + x, y - 9 + b, x, y - 4 + b, x, y - 1 + b, mc.lid);
        d.tri(15 + x, y - 9 + b, x, y - 4 + b, x, y - 1 + b, mc.lid);
      }
      break;
    }
  }
}

// A stalk from (x0, y0) on the head to a glowing tip at (x1, y1).
void antenna(const PetCtx& c, int x0, int y0, int x1, int y1, bool spark) {
  MascotPen& d = c.d;
  const int x = c.x, b = c.b;
  d.tri(x0 - 2 + x, y0 + b, x0 + 2 + x, y0 + b, x1 + x, y1 + b, c.mc.skin);
  d.tri(x0 + x, y0 + b, x1 - 1 + x, y1 + b, x1 + 1 + x, y1 + b, c.mc.skin);
  d.circle(x1 + x, y1 + b, 4, c.mc.accent);
  if (spark) {  // bristling: the tips crackle (sparks beside and below, never above: the box)
    const int s = x1 < 0 ? -1 : 1;
    d.rect(x1 + s * 6 - (s < 0 ? 2 : 0) + x, y1 - 1 + b, 3, 2, c.mc.accent);
    d.rect(x1 - s * 8 - (s < 0 ? 0 : 2) + x, y1 + 1 + b, 3, 2, c.mc.accent);
    d.rect(x1 + s * 4 - 1 + x, y1 + 5 + b, 2, 3, c.mc.accent);
  }
}

}  // namespace

void alienHead(const PetCtx& c) {
  MascotPen& d = c.d;
  const PetColors& mc = c.mc;
  const int x = c.x, b = c.b;
  const bool fluffed = c.desk && (c.k.extras & kFluffed);
  // Antennae: leaning out, standing straight up when it bristles.
  if (fluffed) {
    antenna(c, -10, -18, -15, -38, true);
    antenna(c, 10, -18, 15, -38, true);
  } else {
    antenna(c, -10, -18, -23, -37, false);
    antenna(c, 10, -18, 23, -37, false);
  }
  if (c.desk) d.rrect(-14 + x, 26 + b, 28, 14, 6, mc.skin);  // the tiny body, under the head
  d.rrect(-31 + x, -22 + b, 62, 52, 25, mc.skin);           // the head: a soft dome
    if (c.detail) {  // freckle-like spots on the dome
    d.circle(-17 + x, -13 + b, 2, mc.earIn);
    d.circle(-11 + x, -16 + b, 1, mc.earIn);
    d.circle(18 + x, -12 + b, 1, mc.earIn);
  }
  alienEye(c);
  if (petTired(c)) {  // faint bags under the eye, lower for a wide one
    const int low = c.k.eyes == Eyes::Wide ? 2 : 0;
    d.rect(-12 + x, kEyeY + 16 + low + b, 2, 1, mc.bag);
    d.rect(-10 + x, kEyeY + 17 + low + b, 20, 1, mc.bag);
    d.rect(10 + x, kEyeY + 16 + low + b, 2, 1, mc.bag);
  }
  d.rect(-4 + x, 21 + b, 2, 2, mc.nose);  // two nostrils
  d.rect(2 + x, 21 + b, 2, 2, mc.nose);
  if (!c.desk || !(c.k.extras & (kMouthO | kMouthWide | kTongue))) {  // a small smile
    d.rect(-6 + x, 24 + b, 2, 1, mc.lid);
    d.rect(-4 + x, 25 + b, 8, 1, mc.lid);
    d.rect(4 + x, 24 + b, 2, 1, mc.lid);
  }
}

namespace {

// A three-fingered hand in the paw's box: a palm and three round fingertips, pointing up when it
// is raised (stretching, covering, licking) and down over the table edge otherwise.
void alienHand(const PetCtx& c, int px, int py, int pw, int ph, int r) {
  MascotPen& d = c.d;
  const int x = c.x, b = c.b;
  const Paws p = c.k.paws;
  const bool up = p == Paws::Up || p == Paws::Cover || (p == Paws::Lick && px < 0);
  const int f = ph >= 17 ? 3 : 2;      // fingertip radius
  const int fy = up ? py + f : py + ph - f - 1;  // the fingertips' row
  const int palmY = up ? py + f : py, palmH = ph - f;
  for (int pass = 0; pass < 2; pass++) {  // the outline, then the skin over it
    const uint16_t col = pass ? c.mc.skin : c.mc.line;
    const int o = pass ? 0 : 1;
    d.rrect(px + x - o, palmY + b - o, pw + 2 * o, palmH + 2 * o, r + o, col);
    for (int i = 0; i < 3; i++) d.circle(px + pw * (2 * i + 1) / 6 + x, fy + b, f + o, col);
  }
  // A crease between the fingers, so the hand still reads over the (skin) head.
  for (int t = 1; t <= 2; t++) d.rect(px + x + pw * t / 3, (up ? py + 1 : py + ph - 2 * f - 1) + b, 1, 2 * f, c.mc.line);
}

}  // namespace

void alienFront(const PetCtx& c) {
  MascotPen& d = c.d;
  const PetColors& mc = c.mc;
  const int x = c.x, b = c.b;
  if (c.k.extras & kMouthO) d.circle(x, 27 + b, 3, mc.lid);
  if (c.k.extras & kMouthWide) {  // a yawn or a sneeze
    d.rrect(-7 + x, 24 + b, 14, 10, 5, mc.lid);
    d.rect(-4 + x, 29 + b, 8, 4, mc.earIn);
  }
  petPaws(c, alienHand);
  if (c.k.extras & kTongue) d.rrect(-3 + x, 24 + b, 6, 6, 2, mc.earIn);
}

namespace {
// The Tail antic: no tail; signal dots float out from its right antenna, one after the other.
void alienTail(const PetCtx& c) {
  for (int i = 0; i < 4; i++) {
    if ((c.wag - 1 + 4 - i) % 4 == 0) continue;  // a gap travelling out along them
    c.d.circle(31 + 7 * i + c.x, -38 + 3 * i - (i % 2) * 3 + c.b, 2 - i / 2, c.mc.accent);
  }
}
}  // namespace

// Its one eye a little higher than the cat's two (one wide lens for the glasses).
const PetDef kPetAlien MIBLO_ROM = {alienHead, alienFront, {0, 0, 0, 0, kEyeY, 0}, color::VIOLET,
                                      kGlow, alienTail};

}  // namespace screens
