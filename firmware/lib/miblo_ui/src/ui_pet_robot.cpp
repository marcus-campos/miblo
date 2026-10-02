// The robot pet (miblo::Pet::Robot): a friendly little bot. A rounded-square head (skin) with an
// antenna whose light is the accent, bolt "ears" (earIn), a dark screen for a face where the eyes
// and the smile are LEDs (eye colour), and claw-ish hands. See ui_pet.h for the contract.
#include "miblo_mood.h"
#include "miblo_rom.h"
#include "ui_pet.h"

namespace screens {

namespace color = ui::color;

namespace {

// The screen (x -25..25, y -11..21) and the LED eyes' centres on it.
constexpr int kScrX = -25, kScrY = -11, kScrW = 50, kScrH = 32;
constexpr int kEyeX = 12, kEyeY = 4;

// One LED eye centred at (ex, ey) (c.x / c.b already added), in the current eye shape.
void ledEye(const PetCtx& c, int ex, int ey, bool sleepyLid) {
  MascotPen& d = c.d;
  const PetColors& mc = c.mc;
  const bool big = mc.eyeShape == (uint8_t)miblo::EyeShape::Big;
  const int hw = big ? 7 : 5, hh = big ? 8 : 6;
  d.rrect(ex - hw, ey - hh, 2 * hw, 2 * hh, big ? 4 : 3, mc.eye);
  if (big) {  // shiny: a glint in the top corner
    d.rect(ex - hw + 2, ey - hh + 2, 3, 3, color::WHITE);
  }
  if (sleepyLid) {  // the top part switched off, a lit lid line
    d.rect(ex - hw - 1, ey - hh - 1, 2 * hw + 2, hh, mc.pupil);
    d.rect(ex - hw, ey - 1, 2 * hw, 1, mc.eye);
  }
}

}  // namespace

void robotHead(const PetCtx& c) {
  MascotPen& d = c.d;
  const MascotLook& k = c.k;
  const PetColors& mc = c.mc;
  const int x = c.x, b = c.b;
  // The antenna: a stem and its light.
  d.rect(-2 + x, -34 + b, 4, 14, mc.line);
  d.circle(x, -37 + b, 5, mc.accent);
  if (c.detail) d.circle(-2 + x, -39 + b, 1, color::WHITE);
  // The bolts on each side.
  for (int s = -1; s <= 1; s += 2) {
    const int o = s < 0 ? -1 : 0;  // mirror a w-unit-wide piece: left edge s * a + o * w
    d.rrect(s * 32 + o * 9 + x, -2 + b, 9, 16, 3, mc.earIn);
    if (c.detail) d.rect(s * 39 + o * 2 + x, 2 + b, 2, 8, mc.line);
  }
  d.rrect(-34 + x, -22 + b, 68, 54, 13, mc.skin);  // the head
  if (c.desk && (k.extras & kFluffed)) {  // sparks flying off both sides
    for (int s = -1; s <= 1; s += 2) {
      d.tri(s * 34 + x, -14 + b, s * 44 + x, -18 + b, s * 38 + x, -10 + b, mc.accent);
      d.tri(s * 35 + x, 20 + b, s * 44 + x, 24 + b, s * 38 + x, 16 + b, mc.accent);
    }
  }
  if (c.detail) {  // two rivets on the forehead
    d.circle(-26 + x, -15 + b, 2, mc.line);
    d.circle(26 + x, -15 + b, 2, mc.line);
  }
  // The screen, with a bezel in the line colour.
  d.rrect(kScrX - 2 + x, kScrY - 2 + b, kScrW + 4, kScrH + 4, 9, mc.line);
  d.rrect(kScrX + x, kScrY + b, kScrW, kScrH, 7, mc.pupil);

  const int ey = kEyeY + b;
  switch (k.eyes) {
    case Eyes::Closed:  // a lit line
      d.rect(-kEyeX - 6 + x, ey, 12, 2, mc.eye);
      d.rect(kEyeX - 6 + x, ey, 12, 2, mc.eye);
      break;
    case Eyes::Happy:  // "^ ^"
      for (int s = -1; s <= 1; s += 2) {
        const int ex = s * kEyeX + x;
        d.tri(ex - 7, ey + 4, ex, ey - 4, ex + 7, ey + 4, mc.eye);
        d.tri(ex - 4, ey + 5, ex, ey, ex + 4, ey + 5, mc.pupil);
      }
      break;
    case Eyes::Wide:  // big rings, a dot looking around
      for (int s = -1; s <= 1; s += 2) {
        const int ex = s * kEyeX + x;
        d.circle(ex, ey, 8, mc.eye);
        d.circle(ex, ey, 6, mc.pupil);
        d.circle(ex + k.gx, ey + k.gy, 3, mc.eye);
      }
      break;
    case Eyes::Open:
    case Eyes::Sleepy: {
      const int cross = (k.extras & kCrossEyed) ? 4 : 0;
      const bool lid = k.eyes == Eyes::Sleepy || mc.eyeShape == (uint8_t)miblo::EyeShape::Sleepy;
      for (int s = -1; s <= 1; s += 2) {
        const int ex = s * (kEyeX - cross) + x + k.gx;
        ledEye(c, ex, ey + k.gy, lid);
        if (k.eyes == Eyes::Sleepy) d.rect(ex - 8, ey + k.gy - 9, 16, 6, mc.pupil);  // heavier
      }
      if (k.extras & kGrumpy) {  // the inner top corners switched off: a frown
        d.tri(-21 + x, -10 + b, -3 + x, -10 + b, -3 + x, 3 + b, mc.pupil);
        d.tri(21 + x, -10 + b, 3 + x, -10 + b, 3 + x, 3 + b, mc.pupil);
      }
      break;
    }
    case Eyes::Dizzy:  // spinning rings
      for (int s = -1; s <= 1; s += 2) {
        const int ex = s * kEyeX + x;
        d.circle(ex, ey, 7, mc.eye);
        d.circle(ex, ey, 5, mc.pupil);
        d.circle(ex, ey, 3, mc.eye);
        d.circle(ex, ey, 1, mc.pupil);
      }
      break;
  }
  if (petTired(c)) {
    petEyeBag(c, -kEyeX, kEyeY - 1);
    petEyeBag(c, kEyeX, kEyeY - 1);
  }
  // The LED smile (front() swaps it for the other mouths).
  d.rect(-6 + x, 16 + b, 12, 2, mc.eye);
  d.rect(-8 + x, 14 + b, 2, 2, mc.eye);
  d.rect(6 + x, 14 + b, 2, 2, mc.eye);
}

// A claw: a skin mitten in an outline, split at the top into two pincers.
static void robotClaw(const PetCtx& c, int px, int py, int pw, int ph, int r) {
  const int x = c.x, b = c.b;
  c.d.rrect(px + x - 1, py + b - 1, pw + 2, ph + 2, r + 1, c.mc.line);
  c.d.rrect(px + x, py + b, pw, ph, r, c.mc.skin);
  c.d.tri(px + x + pw / 2 - 3, py + b - 1, px + x + pw / 2 + 3, py + b - 1, px + x + pw / 2, py + b + ph / 2,
          c.mc.line);
}

void robotFront(const PetCtx& c) {
  MascotPen& d = c.d;
  const PetColors& mc = c.mc;
  const int x = c.x, b = c.b;
  const uint16_t mouths = kMouthO | kMouthWide | kTongue;
  if (c.k.extras & mouths) d.rect(-9 + x, 12 + b, 18, 8, mc.pupil);  // the smile off
  if (c.k.extras & kMouthO) {
    d.circle(x, 16 + b, 4, mc.eye);
    d.circle(x, 16 + b, 2, mc.pupil);
  }
  if (c.k.extras & kMouthWide) {  // a yawn or a sneeze: a wide lit oval
    d.rrect(-7 + x, 11 + b, 14, 9, 4, mc.eye);
    d.rrect(-5 + x, 13 + b, 10, 5, 2, mc.pupil);
  }
  petPaws(c, robotClaw);
  if (c.k.extras & kTongue) {  // a flat smile and a little tongue under it
    d.rect(-6 + x, 14 + b, 12, 2, mc.eye);
    d.rrect(-3 + x, 16 + b, 6, 4, 2, mc.earIn);
  }
}

const PetDef kPetRobot MIBLO_ROM = {robotHead, robotFront, {0, 0, 4, 0, 4, 12}, 0x339F, color::RED};

}  // namespace screens
