// The penguin pet (miblo::Pet::Penguin): an original chubby penguin chick. A tall egg (skin) with a
// little tuft of feathers on top, the eyes on the dark of its head, a pale bib from the beak down
// (the accent, white by default), a short pointed beak (nose), flippers for paws and
// two feet (nose) on the desk. See ui_pet.h for the contract.
#include "miblo_rom.h"
#include "ui_pet.h"

namespace screens {

namespace color = ui::color;

void penguinHead(const PetCtx& c) {
  MascotPen& d = c.d;
  const MascotLook& k = c.k;
  const PetColors& mc = c.mc;
  const int x = c.x, b = c.b;
  // The tuft: three short feathers, the middle one tallest.
  d.tri(-9 + x, -24 + b, -8 + x, -34 + b, -1 + x, -26 + b, mc.skin);
  d.tri(-4 + x, -26 + b, 1 + x, -39 + b, 5 + x, -26 + b, mc.skin);
  d.tri(2 + x, -26 + b, 9 + x, -33 + b, 9 + x, -24 + b, mc.skin);
  if (c.desk) {  // the feet, on the table under the body
    d.rrect(-21 + x, 34 + b, 15, 6, 3, mc.nose);
    d.rrect(6 + x, 34 + b, 15, 6, 3, mc.nose);
  }
  // The egg: one tall shape, a little wider at the bottom.
  d.rrect(-32 + x, 4 + b, 64, 33, 18, mc.skin);
  d.rrect(-29 + x, -29 + b, 58, 58, 28, mc.skin);
  if (c.desk && (k.extras & kFluffed)) {  // feathers standing up on the sides
    for (int s = -1; s <= 1; s += 2) {
      d.tri(s * 27 + x, -18 + b, s * 37 + x, -14 + b, s * 29 + x, -8 + b, mc.skin);
      d.tri(s * 29 + x, -4 + b, s * 40 + x, 1 + b, s * 31 + x, 6 + b, mc.skin);
      d.tri(s * 31 + x, 10 + b, s * 42 + x, 16 + b, s * 31 + x, 22 + b, mc.skin);
    }
  }
  // The belly: a tall pale oval from under the beak to the feet.
  d.rrect(-18 + x, 8 + b, 36, 30, 16, mc.accent);
  petEyes(c);
  if (petTired(c)) {
    petEyeBag(c, -14, 6);
    petEyeBag(c, 14, 6);
  }
  // The beak, between the eyes: a short pointed wedge.
  d.rrect(-6 + x, 8 + b, 12, 5, 2, mc.nose);
  d.tri(-6 + x, 11 + b, 6 + x, 11 + b, x, 18 + b, mc.nose);
  if (c.detail) d.rect(-5 + x, 11 + b, 10, 1, mc.line);
}

// A flipper: a skin paddle with a darker outline (it reads as a flipper over the body), its outer
// bottom corner pointed (no further out than the cat's paw).
static void penguinFlipper(const PetCtx& c, int px, int py, int pw, int ph, int r) {
  const int x = c.x, b = c.b;
  const int tip = px + pw / 2 < 0 ? px - 1 : px + pw + 1;  // the outer side
  const int edge = px + pw / 2 < 0 ? px + 2 : px + pw - 2;
  c.d.tri(edge + x, py + b, edge + x, py + ph + 1 + b, tip + x, py + ph + 1 + b, c.mc.line);
  c.d.rrect(px + x - 1, py + b - 1, pw + 2, ph + 2, r + 1, c.mc.line);
  c.d.rrect(px + x, py + b, pw, ph, r + 2 > ph / 2 ? ph / 2 : r + 2, c.mc.skin);
  c.d.tri(edge + x, py + 1 + b, edge + x, py + ph - 1 + b, tip - (tip > px ? 1 : -1) + x, py + ph + b,
          c.mc.skin);
}

void penguinFront(const PetCtx& c) {
  MascotPen& d = c.d;
  const PetColors& mc = c.mc;
  const int x = c.x, b = c.b;
  // The mouths open under the beak.
  if (c.k.extras & kMouthO) d.circle(x, 21 + b, 3, mc.lid);
  if (c.k.extras & kMouthWide) {  // a yawn or a sneeze: the beak wide open
    d.rrect(-6 + x, 11 + b, 12, 11, 4, mc.lid);
    d.rect(-3 + x, 17 + b, 6, 4, mc.earIn);
    d.rrect(-6 + x, 8 + b, 12, 5, 2, mc.nose);
  }
  petPaws(c, penguinFlipper);
  if (c.k.extras & kTongue) d.rrect(-3 + x, 16 + b, 6, 5, 2, mc.earIn);
}

// The Tail antic: a short stubby wedge low behind the feet, waddling up and down.
static void penguinTail(const PetCtx& c) {
  const int x = c.x, b = c.b, ty = 33 + 2 * petSwing(c) + b;
  c.d.tri(24 + x, 26 + b, 24 + x, 39 + b, 42 + x, ty, c.mc.line);
  c.d.tri(25 + x, 28 + b, 25 + x, 37 + b, 40 + x, ty, c.mc.skin);
}

// Its head is taller than the cat's: the hats a little higher.
const PetDef kPetPenguin MIBLO_ROM = {penguinHead, penguinFront, {-4, 0, 0, 0, 6, 14}, 0x3171, color::WHITE,
                                      penguinTail};

}  // namespace screens
