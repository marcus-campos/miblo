// The crab pet (miblo::Pet::Crab): a round-shelled crab with its eyes up on short stalks, two
// claws with coloured tips and little legs, original (not any project's mascot). See ui_pet.h for
// the contract.
#include "miblo_rom.h"
#include "ui_pet.h"

namespace screens {

namespace color = ui::color;

namespace {

constexpr int kEyeUp = 16;  // the eyes sit this much higher than the cat's (at y -10)

// A claw: a skin pincer with an outline, taller than the cat's paw, its two tips in the accent
// split by a notch cut down into it.
void crabClaw(const PetCtx& c, int px, int py, int pw, int ph, int r) {
  MascotPen& d = c.d;
  const int x = px + c.x, y = py - 3 + c.b, h = ph + 3;
  d.rrect(x - 1, y - 1, pw + 2, h + 2, r + 1, c.mc.line);
  d.rrect(x, y, pw, h, r, c.mc.accent);
  d.rrect(x, y + h * 3 / 5, pw, h - h * 3 / 5, r < 4 ? r : 4, c.mc.skin);
  const int n = x + pw * 3 / 5;  // the notch between the two pincers
  d.tri(n - 3, y - 1, n + 2, y - 1, n, y + h / 2, color::BG);
}

void crabHead(const PetCtx& c) {
  MascotPen& d = c.d;
  const PetColors& mc = c.mc;
  const int x = c.x, b = c.b;
  for (int s = -1; s <= 1; s += 2) {  // three little legs a side, under the shell's rim
    const int o = s < 0 ? -1 : 0;     // mirror a w-unit-wide piece: left edge s * a + o * w
    for (int i = 0; i < 3; i++) {
      const int ly = 16 + 7 * i;
      d.rrect(s * 30 + o * 13 + x, ly + b, 13, 5, 2, mc.line);
      d.rrect(s * 41 + o * 4 + x, ly + 2 + b, 4, 6, 2, mc.line);  // the tip, bent down
      if (c.detail) {
        d.rect(s * 31 + o * 10 + x, ly + 1 + b, 10, 3, mc.skin);
        d.rect(s * 42 + o * 2 + x, ly + 3 + b, 2, 4, mc.skin);
      }
    }
  }
  for (int s = -1; s <= 1; s += 2) {  // the stalks and the eyeballs on them
    d.rect(s * 14 - 3 + x, -4 + b, 6, 14, mc.line);
    d.rect(s * 14 - 2 + x, -4 + b, 4, 14, mc.skin);
    d.circle(s * 14 + x, 6 - kEyeUp + b, 13, mc.line);
    d.circle(s * 14 + x, 6 - kEyeUp + b, 12, mc.skin);
  }
  d.rrect(-37 + x, 7 + b, 74, 31, 15, mc.line);  // the shell: a wide dome
  d.rrect(-36 + x, 8 + b, 72, 29, 14, mc.skin);
  if (c.desk && (c.k.extras & kFluffed)) {  // spikes up along the rim
    for (int s = -1; s <= 1; s += 2) {
      d.tri(s * 22 + x, 10 + b, s * 28 + x, 1 + b, s * 31 + x, 12 + b, mc.skin);
      d.tri(s * 31 + x, 14 + b, s * 41 + x, 8 + b, s * 36 + x, 22 + b, mc.skin);
    }
  }
  if (c.detail) {  // the shell's pattern: a row of spots under the rim
    d.rrect(-5 + x, 11 + b, 10, 4, 2, mc.earIn);
    d.rrect(-21 + x, 13 + b, 9, 4, 2, mc.earIn);
    d.rrect(12 + x, 13 + b, 9, 4, 2, mc.earIn);
    d.circle(-28 + x, 20 + b, 3, mc.nose);  // blush
    d.circle(28 + x, 20 + b, 3, mc.nose);
  }
  const PetCtx e{c.d, c.k, c.mc, c.x, c.b - kEyeUp, c.detail, c.desk};
  petEyes(e);
  if (petTired(c)) {
    petEyeBag(e, -14, 6);
    petEyeBag(e, 14, 6);
  }
  if (!(c.k.extras & (kMouthO | kMouthWide))) {  // a small smile
    d.rect(-4 + x, 25 + b, 8, 2, mc.lid);
    d.rect(-6 + x, 23 + b, 2, 2, mc.lid);
    d.rect(4 + x, 23 + b, 2, 2, mc.lid);
  }
}

void crabFront(const PetCtx& c) {
  MascotPen& d = c.d;
  const PetColors& mc = c.mc;
  const int x = c.x, b = c.b;
  if (c.k.extras & kMouthO) d.circle(x, 24 + b, 3, mc.lid);
  if (c.k.extras & kMouthWide) {
    d.rrect(-7 + x, 19 + b, 14, 11, 5, mc.lid);
    d.rect(-4 + x, 25 + b, 8, 4, mc.earIn);
  }
  if (c.k.paws == Paws::Cover) {  // its eyes are up on the stalks: the claws go up there
    crabClaw(c, -28, -21, 24, 20, 6);
    crabClaw(c, 4, -21, 24, 20, 6);
  } else {
    petPaws(c, crabClaw);
  }
  if (c.k.extras & kTongue) d.rrect(-3 + x, 26 + b, 6, 5, 2, mc.earIn);
}

}  // namespace

// Eyes up on the stalks (glasses there too); blue eyes, red claw tips.
const PetDef kPetCrab MIBLO_ROM = {crabHead, crabFront, {0, 0, 0, 0, 6 - kEyeUp, 14}, ui::color::BLUE,
                                   ui::color::RED};

}  // namespace screens
