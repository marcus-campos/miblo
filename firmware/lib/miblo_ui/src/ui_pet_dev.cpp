// The Dev pet (miblo::Pet::Dev): an original veteran developer, a round creature (not a human)
// with thin rectangular glasses, swept-back greying hair tied in a ponytail (the accent colour),
// a stubble shadow, calm eyebrows and a mild little smile. See ui_pet.h for the contract.
#include "miblo_rom.h"
#include "ui_pet.h"

namespace screens {

namespace color = ui::color;

namespace {

constexpr uint16_t kGreyHair = 0x7B8D;  // #78706c: the hair's Auto colour (salt and pepper)
constexpr uint16_t kHazel = 0x8B65;     // #8c6c28: the eyes' Auto colour

// The stubble: dots around the mouth and along the jaw (before c.x / c.b).
const int8_t kStubble[][2] MIBLO_ROM = {{-18, 20}, {-14, 24}, {-20, 25}, {-9, 27}, {-15, 29}, {-3, 29},
                                        {3, 28},   {10, 27},  {16, 29},  {14, 24}, {19, 21}, {21, 26},
                                        {-23, 21}, {24, 22}};

// The ponytail, tied at the back of the head on its left and hanging down, swung by `sway`.
void ponytail(const PetCtx& c, int sway) {
  MascotPen& d = c.d;
  const int x = c.x, b = c.b;
  d.circle(-30 + x, -6 + b, 6, c.mc.accent);  // the tuft where it is tied
  d.tri(-40 + x, -4 + b, -26 + x, -2 + b, -40 + sway + x, 34 + b, c.mc.accent);
  d.rrect(-45 + sway / 2 + x, 0 + b, 12, 22, 6, c.mc.accent);
  d.rect(-37 + x, -3 + b, 8, 3, c.mc.line);  // the hair tie
}

// The brows: calm and level; up in surprise, frowning towards the middle when grumpy.
void brows(const PetCtx& c) {
  MascotPen& d = c.d;
  const int x = c.x, b = c.b;
  const uint16_t col = c.mc.lid;
  if (c.k.extras & kGrumpy) {
    d.tri(-23 + x, -12 + b, -7 + x, -8 + b, -7 + x, -5 + b, col);
    d.tri(23 + x, -12 + b, 7 + x, -8 + b, 7 + x, -5 + b, col);
    return;
  }
  const int lift = c.k.eyes == Eyes::Wide ? 3 : 0;
  d.rrect(-22 + x, -10 - lift + b, 15, 3, 1, col);
  d.rrect(7 + x, -10 - lift + b, 15, 3, 1, col);
}

// Its own thin glasses: the eyes are behind the lenses (what of an eye reaches past a lens is cut
// off with skin), the frames where the shared "nerdy glasses" accessory's rims go, so those cover
// them exactly.
void glasses(const PetCtx& c) {
  MascotPen& d = c.d;
  const int x = c.x, b = c.b;
  const uint16_t rim = c.mc.lid;
  for (int e = -14; e <= 14; e += 28) {
    d.rect(e - 12 + x, -7 + b, 24, 3, c.mc.skin);  // above and below the lens
    d.rect(e - 12 + x, 17 + b, 24, 3, c.mc.skin);
    d.rect(e - 10 + x, -4 + b, 20, 1, rim);
    d.rect(e - 10 + x, 16 + b, 20, 1, rim);
    d.rect(e - 11 + x, -3 + b, 1, 19, rim);
    d.rect(e + 10 + x, -3 + b, 1, 19, rim);
    if (c.detail) d.rect(e - 8 + x, -2 + b, 1, 3, color::WHITE);  // a glint
  }
  d.rect(-4 + x, 1 + b, 8, 1, rim);  // the bridge
  d.rect(-29 + x, 1 + b, 4, 1, rim);  // the temples, to the ears
  d.rect(25 + x, 1 + b, 4, 1, rim);
}

}  // namespace

void devHead(const PetCtx& c) {
  MascotPen& d = c.d;
  const PetColors& mc = c.mc;
  const int x = c.x, b = c.b;
  if (!c.wag) ponytail(c, 0);  // (the Tail antic swings it: devTail)
  d.rrect(-24 + x, -24 + b, 48, 14, 7, mc.accent);  // the hair, peeking out above and at the temples
  d.rrect(-33 + x, -13 + b, 9, 14, 4, mc.accent);
  d.rrect(24 + x, -13 + b, 9, 14, 4, mc.accent);
  d.circle(-31 + x, 6 + b, 5, mc.skin);  // small round ears
  d.circle(31 + x, 6 + b, 5, mc.skin);
  if (c.detail) {
    d.circle(-31 + x, 6 + b, 2, mc.earIn);
    d.circle(31 + x, 6 + b, 2, mc.earIn);
  }
  d.rrect(-30 + x, -19 + b, 60, 51, 24, mc.skin);  // the round head
  if (c.detail) {  // the hair swept back over the top (a high forehead: a veteran)
    d.tri(-23 + x, -12 + b, -4 + x, -20 + b, -27 + x, -3 + b, mc.accent);
    d.tri(23 + x, -12 + b, 4 + x, -20 + b, 27 + x, -3 + b, mc.accent);
  }
  if (c.desk && (c.k.extras & kFluffed)) {  // bristling: tufts on both cheeks
    for (int s = -1; s <= 1; s += 2) {
      d.tri(s * 29 + x, 10 + b, s * 39 + x, 15 + b, s * 29 + x, 20 + b, mc.skin);
      d.tri(s * 27 + x, 19 + b, s * 37 + x, 25 + b, s * 25 + x, 28 + b, mc.skin);
    }
  }
  if (c.detail) {  // the stubble
    for (size_t i = 0; i < sizeof(kStubble) / sizeof(kStubble[0]); i++) {
      int8_t p[2];
      mibloRomCopy(p, kStubble[i], sizeof(p));
      d.rect(p[0] + x, p[1] + b, 1, 1, mc.line);
    }
  }
  petEyes(c);
  glasses(c);
  if (petTired(c)) {  // under the lenses
    petEyeBag(c, -14, 8);
    petEyeBag(c, 14, 8);
  }
  brows(c);
  d.rrect(-3 + x, 12 + b, 6, 4, 2, mc.nose);
  if (!c.desk || !(c.k.extras & (kMouthO | kMouthWide))) {  // a mild little smile
    d.rect(-6 + x, 20 + b, 2, 2, mc.lid);
    d.rect(-4 + x, 21 + b, 8, 2, mc.lid);
    d.rect(4 + x, 20 + b, 2, 2, mc.lid);
  }
}

void devFront(const PetCtx& c) {
  MascotPen& d = c.d;
  const PetColors& mc = c.mc;
  const int x = c.x, b = c.b;
  if (c.k.extras & kMouthO) d.circle(x, 22 + b, 3, mc.lid);
  if (c.k.extras & kMouthWide) {  // a yawn or a sneeze
    d.rrect(-7 + x, 19 + b, 14, 10, 5, mc.lid);
    d.rect(-4 + x, 24 + b, 8, 4, mc.earIn);
  }
  petPaws(c, petPadPaw);
  if (c.k.extras & kTongue) d.rrect(-3 + x, 21 + b, 6, 6, 2, mc.earIn);
}

// The Tail antic: the ponytail swings, its tip flicking out the way it is going.
static void devTail(const PetCtx& c) {
  const int w = petSwing(c), x = c.x, b = c.b, tip = -40 + 3 * w;
  ponytail(c, 3 * w);
  c.d.tri(tip - 3 + x, 28 + b, tip + 3 + x, 28 + b, tip + (w < 0 ? -6 : w > 0 ? 6 : 0) + x, 38 + b, c.mc.accent);
}

// The glasses accessory sits on its own glasses (and covers them): the cat's anchors.
const PetDef kPetDev MIBLO_ROM = {devHead, devFront, MIBLO_CAT_ANCHORS, kHazel, kGreyHair, devTail};

}  // namespace screens
