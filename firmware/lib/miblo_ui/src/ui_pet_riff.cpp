// The Riff pet (miblo::Pet::Riff): an original little rocker, a round creature (not a human) with
// a tall spiky mohawk (the accent colour), a studded collar, a guitar-pick earring, one raised
// eyebrow and a crooked grin. See ui_pet.h for the contract.
#include "miblo_rom.h"
#include "ui_pet.h"

namespace screens {

namespace color = ui::color;

namespace {

constexpr uint16_t kHotPink = 0xE8F6;  // #e81cb0: the mohawk's (and the pick's) Auto colour

// The mohawk's spikes: base from x0 to x1 (at y -12) and the tip, centre one the tallest.
const int8_t kSpikes[][3] MIBLO_ROM = {{-14, -6, -30}, {-10, 0, -38}, {-5, 5, -42}, {0, 10, -38}, {6, 14, -30}};

// The brows: the left one level, the right one cocked up (its default sceptical look); frowning
// down towards the middle when grumpy.
void brows(const PetCtx& c) {
  MascotPen& d = c.d;
  const int x = c.x, b = c.b;
  const uint16_t col = c.mc.lid;
  if (c.k.extras & kGrumpy) {
    d.tri(-23 + x, -9 + b, -7 + x, -4 + b, -7 + x, -1 + b, col);
    d.tri(23 + x, -9 + b, 7 + x, -4 + b, 7 + x, -1 + b, col);
    return;
  }
  const int lift = c.k.eyes == Eyes::Wide ? 3 : 0;  // both up in surprise
  d.rrect(-21 + x, -7 - lift + b, 14, 3, 1, col);
  d.tri(6 + x, -9 - lift + b, 21 + x, -15 - lift + b, 22 + x, -12 - lift + b, col);
  d.tri(6 + x, -9 - lift + b, 7 + x, -6 - lift + b, 22 + x, -12 - lift + b, col);
}

}  // namespace

void riffHead(const PetCtx& c) {
  MascotPen& d = c.d;
  const PetColors& mc = c.mc;
  const int x = c.x, b = c.b;
  for (size_t i = 0; i < sizeof(kSpikes) / sizeof(kSpikes[0]); i++) {  // the mohawk
    int8_t s[3];
    mibloRomCopy(s, kSpikes[i], sizeof(s));
    d.tri(s[0] + x, -10 + b, s[1] + x, -10 + b, (s[0] + s[1]) / 2 + 3 + x, s[2] + b, mc.accent);
  }
  d.circle(-31 + x, 4 + b, 6, mc.skin);  // small round ears
  d.circle(31 + x, 4 + b, 6, mc.skin);
  if (c.detail) {
    d.circle(-31 + x, 4 + b, 3, mc.earIn);
    d.circle(31 + x, 4 + b, 3, mc.earIn);
  }
  d.rrect(-30 + x, -16 + b, 60, 48, 23, mc.skin);  // the round body
  if (c.desk && (c.k.extras & kFluffed)) {  // bristling: spikes on both cheeks like the mohawk's
    for (int s = -1; s <= 1; s += 2) {
      d.tri(s * 29 + x, 8 + b, s * 39 + x, 13 + b, s * 29 + x, 19 + b, mc.skin);
      d.tri(s * 28 + x, 18 + b, s * 37 + x, 24 + b, s * 27 + x, 27 + b, mc.skin);
    }
  }
  if (c.detail) {  // the studded collar where it meets the table
    d.rrect(-26 + x, 25 + b, 52, 6, 2, mc.line);
    for (int sx = -20; sx <= 20; sx += 8) d.circle(sx + x, 28 + b, 1, mc.earIn);
  }
  petEyes(c);
  if (petTired(c)) {
    petEyeBag(c, -14, 6);
    petEyeBag(c, 14, 6);
  }
  brows(c);
  d.rrect(-3 + x, 14 + b, 6, 3, 1, mc.nose);
  if (!c.desk || !(c.k.extras & (kMouthO | kMouthWide))) {  // the crooked grin, up on the right
    d.rect(-8 + x, 19 + b, 3, 2, mc.lid);
    d.rect(-6 + x, 20 + b, 9, 2, mc.lid);
    d.rect(2 + x, 19 + b, 5, 2, mc.lid);
    d.rect(6 + x, 17 + b, 3, 3, mc.lid);
  }
  if (c.detail) {  // the guitar-pick earring, hanging from the left ear
    d.rect(-35 + x, 9 + b, 2, 4, mc.line);
    d.rrect(-39 + x, 13 + b, 10, 5, 2, mc.accent);
    d.tri(-39 + x, 16 + b, -29 + x, 16 + b, -34 + x, 23 + b, mc.accent);
  }
}

void riffFront(const PetCtx& c) {
  MascotPen& d = c.d;
  const PetColors& mc = c.mc;
  const int x = c.x, b = c.b;
  if (c.k.extras & kMouthO) d.circle(x, 21 + b, 3, mc.lid);
  if (c.k.extras & kMouthWide) {  // a yawn or a sneeze (or a scream)
    d.rrect(-7 + x, 18 + b, 14, 10, 5, mc.lid);
    d.rect(-4 + x, 23 + b, 8, 4, mc.earIn);
  }
  petPaws(c, petPadPaw);
  if (c.k.extras & kTongue) d.rrect(1 + x, 20 + b, 6, 6, 2, mc.earIn);  // out of the grin's high side
}

// Its head a little lower than the cat's (the mohawk above it): hats and headphones down by 4.
const PetDef kPetRiff MIBLO_ROM = {riffHead, riffFront, {4, 4, 0, 0, 6, 14}, color::FLASH_BLUE, kHotPink};

}  // namespace screens
