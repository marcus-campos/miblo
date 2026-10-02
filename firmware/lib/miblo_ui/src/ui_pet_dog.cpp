// The dog pet (miblo::Pet::Dog): a friendly dog with floppy ears, a light muzzle around a big
// nose, a collar with a tag, and paws like the cat's. See ui_pet.h for the contract.
#include "miblo_rom.h"
#include "ui_pet.h"

namespace screens {

namespace {

void dogHead(const PetCtx& c) {
  MascotPen& d = c.d;
  const PetColors& mc = c.mc;
  const int x = c.x, b = c.b;
  d.rrect(-32 + x, -20 + b, 64, 52, 22, mc.skin);  // head
  if (c.desk && (c.k.extras & kFluffed)) {  // fur standing up on the cheeks, under the ears
    for (int s = -1; s <= 1; s += 2) {
      d.tri(s * 31 + x, 12 + b, s * 42 + x, 18 + b, s * 31 + x, 24 + b, mc.skin);
      d.tri(s * 22 + x, -18 + b, s * 14 + x, -27 + b, s * 10 + x, -18 + b, mc.skin);
    }
  }
  for (int s = -1; s <= 1; s += 2) {  // floppy ears: folded over at the top, hanging outwards
    const int o = s < 0 ? -1 : 0;     // mirror a w-unit-wide piece: left edge s * a + o * w
    d.rrect(s * 13 + o * 26 + x, -25 + b, 26, 14, 7, mc.line);
    d.rrect(s * 27 + o * 16 + x, -18 + b, 16, 34, 8, mc.line);
    d.rrect(s * 14 + o * 24 + x, -24 + b, 24, 12, 6, mc.earIn);
    d.rrect(s * 28 + o * 14 + x, -17 + b, 14, 32, 7, mc.earIn);
  }
  d.rrect(-13 + x, 13 + b, 26, 18, 9, mc.earIn);  // the muzzle
  petEyes(c);
  if (petTired(c)) {
    petEyeBag(c, -14, 6);
    petEyeBag(c, 14, 6);
  }
  d.rrect(-6 + x, 15 + b, 12, 7, 3, mc.nose);  // the nose ...
  if (c.detail) d.rect(-3 + x, 16 + b, 3, 2, mc.earIn);  // ... and its shine
  if (!(c.k.extras & (kMouthO | kMouthWide))) {  // a "w" smile under it
    d.rect(-1 + x, 21 + b, 2, 4, mc.lid);
    d.rect(-7 + x, 25 + b, 7, 2, mc.lid);
    d.rect(x, 25 + b, 7, 2, mc.lid);
    d.rect(-9 + x, 23 + b, 2, 2, mc.lid);
    d.rect(7 + x, 23 + b, 2, 2, mc.lid);
  }
  d.rrect(-16 + x, 31 + b, 32, 4, 2, mc.accent);  // the collar and its tag
  if (c.detail) d.circle(x, 37 + b, 3, mc.accent);
}

void dogFront(const PetCtx& c) {
  MascotPen& d = c.d;
  const PetColors& mc = c.mc;
  const int x = c.x, b = c.b;
  if (c.k.extras & kMouthO) d.circle(x, 25 + b, 3, mc.lid);
  if (c.k.extras & kMouthWide) {  // a yawn or a sneeze
    d.rrect(-7 + x, 21 + b, 14, 11, 5, mc.lid);
    d.rect(-4 + x, 27 + b, 8, 4, mc.earIn);
  }
  petPaws(c, petPadPaw);
  if (c.k.extras & kTongue) {  // a dog's tongue: out and hanging
    d.rrect(-4 + x, 24 + b, 8, 10, 3, mc.line);
    d.rrect(-3 + x, 24 + b, 6, 9, 3, mc.earIn);
    d.rect(x, 26 + b, 1, 4, mc.line);
  }
}

}  // namespace

// Brown eyes, a red collar.
const PetDef kPetDog MIBLO_ROM = {dogHead, dogFront, MIBLO_CAT_ANCHORS, 0xA285, ui::color::RED};

}  // namespace screens
