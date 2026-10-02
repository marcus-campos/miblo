// The owl pet (miblo::Pet::Owl): a night-coder owl, all round body with ear tufts, a big facial
// disc around big eyes, a small beak, wings for paws and little feet on the table. See ui_pet.h
// for the contract.
#include "miblo_rom.h"
#include "ui_pet.h"

namespace screens {

namespace {

// The eyes: the shared round eyes a little bigger (9/8) and higher, their lids in the facial
// disc's colour. They land at (+-16, 3).
constexpr int kEyeNum = 9, kEyeDen = 8, kEyeDy = -4;

// A wing: a skin feather pad with an outline and two lines of coverts.
void owlWing(const PetCtx& c, int px, int py, int pw, int ph, int r) {
  MascotPen& d = c.d;
  const int x = px + c.x, y = py + c.b;
  d.rrect(x - 1, y - 1, pw + 2, ph + 2, r + 1, c.mc.line);
  d.rrect(x, y, pw, ph, r, c.mc.skin);
  d.rect(x + 2, y + ph / 3, pw - 4, 1, c.mc.line);
  d.rect(x + 4, y + ph * 2 / 3, pw - 8, 1, c.mc.line);
}

void owlHead(const PetCtx& c) {
  MascotPen& d = c.d;
  const PetColors& mc = c.mc;
  const int x = c.x, b = c.b;
  for (int s = -1; s <= 1; s += 2) {  // ear tufts: two feathers a side, swept up and out
    d.tri(s * 14 + x, -20 + b, s * 34 + x, -22 + b, s * 40 + x, -42 + b, mc.skin);
    d.tri(s * 22 + x, -20 + b, s * 34 + x, -14 + b, s * 44 + x, -32 + b, mc.skin);
    if (c.detail) d.tri(s * 26 + x, -22 + b, s * 33 + x, -23 + b, s * 38 + x, -36 + b, mc.line);
  }
  d.rrect(-36 + x, -24 + b, 72, 63, 28, mc.skin);  // the round body, sitting on the table
  if (c.desk && (c.k.extras & kFluffed)) {  // feathers standing up on both sides
    for (int s = -1; s <= 1; s += 2) {
      d.tri(s * 34 + x, -10 + b, s * 43 + x, -4 + b, s * 34 + x, 2 + b, mc.skin);
      d.tri(s * 35 + x, 4 + b, s * 44 + x, 10 + b, s * 35 + x, 16 + b, mc.skin);
    }
  }
  for (int s = -1; s <= 1; s += 2) d.circle(s * 16 + x, 3 + b, 17, mc.line);  // the facial disc,
  for (int s = -1; s <= 1; s += 2) d.circle(s * 16 + x, 3 + b, 16, mc.earIn);  // one round per eye
  if (c.detail) {
    d.tri(-6 + x, -14 + b, 6 + x, -14 + b, x, -6 + b, mc.skin);  // the brow's dip between them
    for (int i = 0; i < 3; i++) {  // chest feathers: little "v"s
      const int fx = (i - 1) * 7, fy = i == 1 ? 33 : 28;
      d.rect(fx - 3 + x, fy + b, 2, 2, mc.line);
      d.rect(fx - 1 + x, fy + 1 + b, 2, 2, mc.line);
      d.rect(fx + 1 + x, fy + b, 2, 2, mc.line);
    }
  }
  MascotPen ep{d.g, d.cx + d.s(x), d.cy + d.s(b + kEyeDy), d.num * kEyeNum, d.den * kEyeDen};
  PetColors ec = mc;
  ec.skin = mc.earIn;  // the lids are the disc's
  const PetCtx e{ep, c.k, ec, 0, 0, c.detail, c.desk};
  petEyes(e);
  if (petTired(c)) {
    petEyeBag(e, -14, 6);
    petEyeBag(e, 14, 6);
  }
  d.tri(-5 + x, 11 + b, 5 + x, 11 + b, x, 21 + b, mc.nose);  // the beak
}

void owlFront(const PetCtx& c) {
  MascotPen& d = c.d;
  const PetColors& mc = c.mc;
  const int x = c.x, b = c.b;
  for (int s = -1; s <= 1; s += 2) {  // little feet on the table edge, three toes each
    for (int t = -1; t <= 1; t++) d.rrect(s * 6 + 2 * t - 1 + x, 36 + b, 2, 4, 1, mc.accent);
  }
  if (c.k.extras & kMouthO) d.circle(x, 25 + b, 3, mc.lid);
  if (c.k.extras & kMouthWide) {  // the beak wide open
    d.rrect(-7 + x, 20 + b, 14, 11, 5, mc.lid);
    d.rect(-4 + x, 26 + b, 8, 4, mc.earIn);
  }
  petPaws(c, owlWing);
  if (c.k.extras & kTongue) d.rrect(-3 + x, 20 + b, 6, 6, 2, mc.earIn);
}

}  // namespace

// Bigger eyes, a little wider apart (glasses follow), headphones out on the wider body; amber
// eyes, coral feet.
const PetDef kPetOwl MIBLO_ROM = {owlHead, owlFront, {0, 0, 4, 0, 3, 16}, ui::color::AMBER, ui::color::CORAL};

}  // namespace screens
