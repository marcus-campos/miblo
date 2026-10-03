// The penguin pet (miblo::Pet::Penguin): an original chubby, sitting, pear-shaped penguin. A round
// head on a fat body (the body colour: black on the black preset), a big white belly and a white
// face patch around the eyes, a wide beak and big flat feet (the accent, yellow-orange by
// default), small flippers for paws and a content, slightly smug little smile. See ui_pet.h for
// the contract.
#include "miblo_rom.h"
#include "ui_pet.h"

namespace screens {

namespace color = ui::color;

namespace {

constexpr uint16_t kBeakYellow = 0xF400;  // #f08000: the beak's and feet's Auto colour (dark enough
                                          // to stay apart from the peach and the black bodies)
// The eyes' Auto colour: white, the irises one with the face patch (only the pupils show).
constexpr uint16_t kEyeWhite = color::WHITE;
constexpr int kLift = -10;                // its eyes sit this much higher than the cat's (y -4)
// The white of its face and belly: like the whites of an eye, not a part the user paints.
constexpr uint16_t kFront = color::WHITE;

// What the eyes are drawn with: on the white face patch, their lids cut in white and drawn in the
// darkest of the face's line colours (the black preset's lid is light).
PetCtx eyeCtx(const PetCtx& c) {
  PetCtx e = c;
  e.b = c.b + kLift;
  e.mc.skin = kFront;
  if (luma565(c.mc.lid) >= 110) e.mc.lid = luma565(c.mc.line) < luma565(c.mc.skin) ? c.mc.line : c.mc.skin;
  return e;
}

}  // namespace

void penguinHead(const PetCtx& c) {
  MascotPen& d = c.d;
  const PetColors& mc = c.mc;
  const int x = c.x, b = c.b;
  // The pear: a fat body sitting on the table, the round head on it.
  d.rrect(-37 + x, 6 + b, 74, 35, 20, mc.skin);
  d.rrect(-33 + x, -8 + b, 66, 40, 26, mc.skin);
  d.rrect(-28 + x, -33 + b, 56, 50, 26, mc.skin);
  if (c.desk && (c.k.extras & kFluffed)) {  // feathers standing up on its sides
    for (int s = -1; s <= 1; s += 2) {
      d.tri(s * 30 + x, -16 + b, s * 39 + x, -12 + b, s * 31 + x, -6 + b, mc.skin);
      d.tri(s * 33 + x, 0 + b, s * 43 + x, 5 + b, s * 35 + x, 11 + b, mc.skin);
      d.tri(s * 36 + x, 16 + b, s * 44 + x, 22 + b, s * 36 + x, 28 + b, mc.skin);
    }
  }
  // The white front: the belly, and the face patch around the eyes running down into it.
  d.rrect(-26 + x, 4 + b, 52, 37, 22, kFront);
  d.circle(-12 + x, -4 + b, 13, kFront);
  d.circle(12 + x, -4 + b, 13, kFront);
  d.rrect(-17 + x, -6 + b, 34, 16, 6, kFront);
  const PetCtx e = eyeCtx(c);
  petEyes(e);
  if (petTired(c)) {
    petEyeBag(e, -14, 6);
    petEyeBag(e, 14, 6);
  }
  // The beak: wide and flat, the upper half over the lower, a crease between them; its corners
  // curl up a little (a content smile) unless the mouth is open.
  const bool open = c.desk && (c.k.extras & kMouthWide);
  d.rrect(-11 + x, 10 + b, 22, 7, 3, mc.accent);
  d.rrect(-14 + x, 6 + b, 28, 7, 3, mc.accent);
  if (!open) {
    d.rect(-12 + x, 12 + b, 24, 1, e.mc.lid);
    if (c.detail) {
      d.rect(-14 + x, 11 + b, 2, 1, e.mc.lid);
      d.rect(12 + x, 11 + b, 2, 1, e.mc.lid);
    }
  }
  if (c.detail) d.rect(-3 + x, 8 + b, 2, 1, mc.line);  // a nostril
}

// A flipper: a body-coloured paddle with a darker outline (it reads as a flipper over the white
// belly), its outer bottom corner pointed (no further out than the cat's paw).
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
  const PetCtx e = eyeCtx(c);
  if (c.k.extras & kMouthO) d.circle(x, 13 + b, 3, e.mc.lid);  // the beak a little open
  if (c.k.extras & kMouthWide) {  // a yawn or a sneeze: the beak wide open
    d.rrect(-9 + x, 11 + b, 18, 12, 5, e.mc.lid);
    d.rect(-5 + x, 17 + b, 10, 4, mc.earIn);
    d.rrect(-11 + x, 20 + b, 22, 5, 2, mc.accent);
    d.rrect(-14 + x, 6 + b, 28, 7, 3, mc.accent);
  }
  // The big flat feet on the table, three toes each, the flippers resting on them.
  for (int s = -1; s <= 1; s += 2) {
    const int o = s < 0 ? -26 : 0;  // mirror a 26-unit-wide foot: left edge s * 4 + o
    d.rrect(s * 4 + o + x, 34 + b, 26, 7, 3, mc.accent);
    if (c.detail) {
      d.rect(s * 4 + o + 8 + x, 37 + b, 1, 4, mc.line);
      d.rect(s * 4 + o + 17 + x, 37 + b, 1, 4, mc.line);
    }
  }
  petPaws(c, penguinFlipper);
  if (c.k.extras & kTongue) d.rrect(-3 + x, 13 + b, 6, 5, 2, mc.earIn);
}

// The Tail antic: a short stubby wedge low behind it, waddling up and down.
static void penguinTail(const PetCtx& c) {
  const int x = c.x, b = c.b, ty = 31 + 2 * petSwing(c) + b;
  c.d.tri(28 + x, 22 + b, 28 + x, 38 + b, 47 + x, ty, c.mc.line);
  c.d.tri(29 + x, 24 + b, 29 + x, 36 + b, 45 + x, ty, c.mc.skin);
}

// Its head is taller than the cat's: the hats a little higher; the eyes (glasses) and the neck
// (tie, under the beak) higher too.
const PetDef kPetPenguin MIBLO_ROM = {penguinHead, penguinFront, {-4, -6, 0, -10, 6 + kLift, 14}, kEyeWhite,
                                      kBeakYellow, penguinTail};

}  // namespace screens
