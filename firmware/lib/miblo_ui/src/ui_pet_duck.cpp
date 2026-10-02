// The duck pet (miblo::Pet::Duck): a rubber duck (rubber duck debugging): a round head with a
// curl on top and a wide flat beak, sitting on a bath-toy body with its tail tip up and small
// wings for paws, a ripple of bath water at the desk. See ui_pet.h for the contract.
#include "miblo_rom.h"
#include "ui_pet.h"

namespace screens {

namespace {

namespace color = ui::color;

// The head's circle: centre (0, kHeadY), radius kHeadR.
constexpr int kHeadY = 4;
constexpr int kHeadR = 28;

// A wing: a rounded skin blade with an outline and two feather tips at its lower edge.
void duckWing(const PetCtx& c, int px, int py, int pw, int ph, int r) {
  const int x = c.x, b = c.b;
  c.d.rrect(px + x - 1, py + b - 1, pw + 2, ph + 2, r + 1, c.mc.line);
  c.d.rrect(px + x, py + b, pw, ph, r, c.mc.skin);
  const int fy = py + b + ph - ph / 3;
  for (int t = 1; t <= 2; t++) c.d.rect(px + x + pw * t / 3, fy, 1, ph / 3, c.mc.line);
}

void duckHead(const PetCtx& c) {
  MascotPen& d = c.d;
  const PetColors& mc = c.mc;
  const int x = c.x, b = c.b;
  // The bath-toy body behind the head, its tail tip up on the right.
  d.tri(24 + x, 24 + b, 43 + x, 4 + b, 41 + x, 30 + b, mc.line);
  d.tri(26 + x, 24 + b, 41 + x, 7 + b, 39 + x, 29 + b, mc.skin);
  d.rrect(-41 + x, 13 + b, 82, 27, 13, mc.line);
  d.rrect(-40 + x, 14 + b, 80, 25, 12, mc.skin);
  // The curl on top of the head.
  d.tri(-2 + x, -22 + b, 4 + x, -22 + b, 5 + x, -31 + b, mc.skin);
  d.circle(7 + x, -30 + b, 3, mc.skin);
  if (c.detail) d.circle(7 + x, -30 + b, 1, mc.line);
  // The head, outlined where it meets the body.
  d.circle(x, kHeadY + b, kHeadR + 1, mc.line);
  d.circle(x, kHeadY + b, kHeadR, mc.skin);
  if (c.desk && (c.k.extras & kFluffed)) {  // feathers ruffled up on both cheeks
    for (int s = -1; s <= 1; s += 2) {
      d.tri(s * 27 + x, -10 + b, s * 37 + x, -6 + b, s * 28 + x, 0 + b, mc.skin);
      d.tri(s * 28 + x, 2 + b, s * 38 + x, 7 + b, s * 28 + x, 12 + b, mc.skin);
      d.tri(s * 29 + x, -27 + b, s * 25 + x, -16 + b, s * 20 + x, -24 + b, mc.skin);
    }
  }
  petEyes(c);
  if (petTired(c)) {
    petEyeBag(c, -14, 6);
    petEyeBag(c, 14, 6);
  }
  // The beak: a wide flat upper bill over a smaller lower one.
  d.rrect(-11 + x, 20 + b, 22, 7, 3, mc.line);
  d.rrect(-10 + x, 20 + b, 20, 6, 3, mc.nose);
  d.rrect(-18 + x, 13 + b, 36, 9, 4, mc.line);
  d.rrect(-17 + x, 14 + b, 34, 7, 3, mc.nose);
  if (c.detail) {  // the nostrils
    d.rect(-6 + x, 16 + b, 2, 1, mc.line);
    d.rect(4 + x, 16 + b, 2, 1, mc.line);
  }
}

void duckFront(const PetCtx& c) {
  MascotPen& d = c.d;
  const PetColors& mc = c.mc;
  const int x = c.x, b = c.b;
  // A ripple of bath water around the body.
  d.rrect(-44 + x, 36 + b, 10, 3, 1, mc.accent);
  d.rrect(34 + x, 36 + b, 10, 3, 1, mc.accent);
  if (c.k.extras & kMouthWide) {  // a yawn or a quack: the lower bill drops
    d.rrect(-10 + x, 20 + b, 20, 10, 4, mc.lid);
    d.rect(-5 + x, 25 + b, 10, 3, mc.earIn);
    d.rrect(-11 + x, 28 + b, 22, 5, 2, mc.line);
    d.rrect(-10 + x, 28 + b, 20, 4, 2, mc.nose);
    d.rrect(-18 + x, 13 + b, 36, 9, 4, mc.line);
    d.rrect(-17 + x, 14 + b, 34, 7, 3, mc.nose);
    d.rect(-6 + x, 16 + b, 2, 1, mc.line);
    d.rect(4 + x, 16 + b, 2, 1, mc.line);
  } else if (c.k.extras & kMouthO) {  // "o": the bill parted
    d.rrect(-5 + x, 21 + b, 10, 5, 2, mc.lid);
  }
  petPaws(c, duckWing);
  if (c.k.extras & kTongue) d.rrect(-3 + x, 24 + b, 6, 6, 2, mc.earIn);
}

}  // namespace

const PetDef kPetDuck MIBLO_ROM = {duckHead, duckFront, {0, 0, 0, 0, 6, 14}, 0x435F, ui::color::FLASH_BLUE};

}  // namespace screens
