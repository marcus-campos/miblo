// The cat (miblo::Pet::Cat): Miblo's own simplified Sphynx, and the shared pieces other pets may
// reuse (eyes, eye bags, paws). See ui_pet.h for the contract.
#include "miblo_mood.h"
#include "miblo_rom.h"
#include "ui_pet.h"

namespace screens {

namespace color = ui::color;

bool petTired(const PetCtx& c) {
  return (c.k.extras & kEyeBags) || catMood() == (uint8_t)miblo::CatMood::Tired;
}

void petEyeBag(const PetCtx& c, int ex, int ey) {
  const int low = c.k.eyes == Eyes::Wide ? 2 : 0;
  const uint16_t bag = c.mc.bag;
  const int x = c.x, b = c.b;
  c.d.rect(ex - 6 + x, ey + 10 + low + b, 2, 1, bag);
  c.d.rect(ex - 4 + x, ey + 11 + low + b, 8, 1, bag);
  c.d.rect(ex + 4 + x, ey + 10 + low + b, 2, 1, bag);
}

void petEyes(const PetCtx& c) {
  MascotPen& d = c.d;
  const MascotLook& k = c.k;
  const PetColors& mc = c.mc;
  const int x = c.x, b = c.b;
  const bool big = mc.eyeShape == (uint8_t)miblo::EyeShape::Big;
  switch (k.eyes) {
    case Eyes::Closed:
      d.rect(-22 + x, 5 + b, 16, 3, mc.lid);
      d.rect(6 + x, 5 + b, 16, 3, mc.lid);
      break;
    case Eyes::Happy:  // "^ ^": a chevron cut out of a lid-coloured triangle
      for (int ex = -14; ex <= 14; ex += 28) {
        d.tri(ex - 9 + x, 10 + b, ex + x, 1 + b, ex + 9 + x, 10 + b, mc.lid);
        d.tri(ex - 5 + x, 10 + b, ex + x, 5 + b, ex + 5 + x, 10 + b, mc.skin);
      }
      break;
    case Eyes::Wide:  // big irises, tiny pupils
      for (int ex = -14; ex <= 14; ex += 28) {
        d.circle(ex + x, 6 + b, big ? 11 : 10, mc.eye);
      }
      d.circle(-14 + x + k.gx, 6 + b + k.gy, 3, mc.pupil);
      d.circle(14 + x + k.gx, 6 + b + k.gy, 3, mc.pupil);
      if (big) {
        for (int ex = -14; ex <= 14; ex += 28) d.circle(ex - 4 + x + k.gx, 2 + b + k.gy, 2, color::WHITE);
      }
      break;
    case Eyes::Open:
    case Eyes::Sleepy: {
      const int cross = (k.extras & kCrossEyed) ? 3 : 0;
      d.circle(-14 + x, 6 + b, big ? 10 : 8, mc.eye);
      d.circle(14 + x, 6 + b, big ? 10 : 8, mc.eye);
      d.circle(-14 + x + k.gx + cross, 6 + b + k.gy, big ? 5 : 4, mc.pupil);
      d.circle(14 + x + k.gx - cross, 6 + b + k.gy, big ? 5 : 4, mc.pupil);
      if (big) {  // shiny: a big and a small highlight
        for (int ex = -14; ex <= 14; ex += 28) {
          const int px = ex + x + k.gx + (ex < 0 ? cross : -cross);
          d.circle(px - 3, 3 + b + k.gy, 2, color::WHITE);
          d.circle(px + 3, 9 + b + k.gy, 1, color::WHITE);
        }
      }
      if (k.eyes == Eyes::Sleepy) {  // heavy lids over the top half
        d.rect(-23 + x, -3 + b, 18, 8, mc.skin);
        d.rect(5 + x, -3 + b, 18, 8, mc.skin);
        d.rect(-22 + x, 4 + b, 16, 2, mc.lid);
        d.rect(6 + x, 4 + b, 16, 2, mc.lid);
      } else if (mc.eyeShape == (uint8_t)miblo::EyeShape::Sleepy) {  // relaxed lids over the top third
        d.rect(-23 + x, -3 + b, 18, 5, mc.skin);
        d.rect(5 + x, -3 + b, 18, 5, mc.skin);
        d.rect(-22 + x, 1 + b, 16, 2, mc.lid);
        d.rect(6 + x, 1 + b, 16, 2, mc.lid);
      }
      if (k.extras & kGrumpy) {  // lids slanting down towards the nose
        d.tri(-23 + x, -3 + b, -5 + x, -3 + b, -5 + x, 5 + b, mc.skin);
        d.tri(23 + x, -3 + b, 5 + x, -3 + b, 5 + x, 5 + b, mc.skin);
      }
      break;
    }
    case Eyes::Dizzy:  // hypnotised: rings in the eyes
      for (int ex = -14; ex <= 14; ex += 28) {
        d.circle(ex + x, 6 + b, 8, mc.eye);
        d.circle(ex + x, 6 + b, 6, mc.pupil);
        d.circle(ex + x, 6 + b, 4, mc.eye);
        d.circle(ex + x, 6 + b, 2, mc.pupil);
      }
      break;
  }
}

// A paw: a skin pad with a darker outline and toe lines, so it reads as a paw even over the
// (skin) head.
void petPadPaw(const PetCtx& c, int px, int py, int pw, int ph, int r) {
  const int x = c.x, b = c.b;
  c.d.rrect(px + x - 1, py + b - 1, pw + 2, ph + 2, r + 1, c.mc.line);
  c.d.rrect(px + x, py + b, pw, ph, r, c.mc.skin);
  for (int t = 1; t <= 2; t++) c.d.rect(px + x + pw * t / 3, py + b + 1, 1, ph / 2, c.mc.line);
}

void petPaws(const PetCtx& c, PetPawFn paw) {
  switch (c.k.paws) {
    case Paws::Down:
      paw(c, -26, 28, 16, 11, 5);
      paw(c, 10, 28, 16, 11, 5);
      break;
    case Paws::ReachLeft:  // batting at the left gauge
      paw(c, -46, 24, 16, 11, 5);
      paw(c, 10, 28, 16, 11, 5);
      break;
    case Paws::ReachRight:
      paw(c, -26, 28, 16, 11, 5);
      paw(c, 30, 24, 16, 11, 5);
      break;
    case Paws::Cover:  // can't look: both paws over the eyes, all of them
      paw(c, -28, -5, 24, 21, 6);
      paw(c, 4, -5, 24, 21, 6);
      break;
    case Paws::Up:  // stretching: both paws up beside the ears
      paw(c, -46, -34, 16, 11, 5);
      paw(c, 30, -34, 16, 11, 5);
      break;
    case Paws::Lick:  // the left paw raised under the mouth, beside the tongue (over the mouth it read as a snout)
      paw(c, -17, 20, 14, 17, 5);
      paw(c, 10, 28, 16, 11, 5);
      break;
    case Paws::TapLeft:  // the left paw lifted (typing, playing keys)
      paw(c, -26, 23, 16, 11, 5);
      paw(c, 10, 28, 16, 11, 5);
      break;
    case Paws::TapRight:
      paw(c, -26, 28, 16, 11, 5);
      paw(c, 10, 23, 16, 11, 5);
      break;
  }
}

// Big triangular ears with pink insides, a round head, round green eyes with dark pupils and a
// small nose.
void catHead(const PetCtx& c) {
  MascotPen& d = c.d;
  const PetColors& mc = c.mc;
  const int x = c.x, b = c.b;
  d.tri(-36 + x, -42 + b, -32 + x, -4 + b, -8 + x, -18 + b, mc.skin);  // ears
  d.tri(36 + x, -42 + b, 32 + x, -4 + b, 8 + x, -18 + b, mc.skin);
  if (c.detail) {
    d.tri(-31 + x, -33 + b, -28 + x, -10 + b, -14 + x, -17 + b, mc.earIn);
    d.tri(31 + x, -33 + b, 28 + x, -10 + b, 14 + x, -17 + b, mc.earIn);
  }
  d.rrect(-32 + x, -20 + b, 64, 52, 24, mc.skin);  // head
  if (c.desk && (c.k.extras & kFluffed)) {  // fur standing up on both cheeks
    for (int s = -1; s <= 1; s += 2) {
      d.tri(s * 31 + x, -8 + b, s * 40 + x, -2 + b, s * 31 + x, 4 + b, mc.skin);
      d.tri(s * 31 + x, 6 + b, s * 41 + x, 12 + b, s * 31 + x, 18 + b, mc.skin);
    }
  }
  petEyes(c);
  if (petTired(c)) {  // tired (8 h of Claude working today, or kEyeBags): faint bags under the eyes
    petEyeBag(c, -14, 6);
    petEyeBag(c, 14, 6);
  }
  d.rrect(-4 + x, 17 + b, 8, 5, 2, mc.nose);
}

void catFront(const PetCtx& c) {
  MascotPen& d = c.d;
  const PetColors& mc = c.mc;
  const int x = c.x, b = c.b;
  if (c.k.extras & kMouthO) d.circle(x, 26 + b, 3, mc.lid);
  if (c.k.extras & kMouthWide) {  // a yawn or a sneeze
    d.rrect(-7 + x, 22 + b, 14, 11, 5, mc.lid);
    d.rect(-4 + x, 28 + b, 8, 4, mc.earIn);
  }
  petPaws(c, petPadPaw);
  if (c.k.extras & kTongue) d.rrect(-3 + x, 22 + b, 6, 6, 2, mc.earIn);
}

const PetDef kPetCat MIBLO_ROM = {catHead, catFront, MIBLO_CAT_ANCHORS, MIBLO_CAT_COLORS};

}  // namespace screens
