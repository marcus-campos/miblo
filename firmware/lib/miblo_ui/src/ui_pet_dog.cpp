// The dog pet (miblo::Pet::Dog): an original little shiba-style dog. A round head with full
// cheeks, pointy upright ears, a cream mask (the accent) over the cheeks, the muzzle and the chest,
// cream eyebrow spots raised high, and by default a sideways, suspicious glance (its pupils off to
// one side while nothing else steers them); cream socks on its paws and a curled tail. See ui_pet.h
// for the contract.
#include "miblo_rom.h"
#include "ui_pet.h"

namespace screens {

namespace {

constexpr uint16_t kCream = 0xFFDD;      // #fff8ec: the mask's Auto colour (light enough to stay
                                         // apart from the peach body)
constexpr uint16_t kAmberBrown = 0xA325;  // #a0642c: the eyes' Auto colour (the pupils show on it)

// The lines drawn on the cream mask (mouth, the open mouths): the face's lid colour when it is dark
// enough to show on cream, else the outlines' (the black preset's lid is light).
uint16_t maskLine(const PetCtx& c) {
  return luma565(c.mc.lid) < 110 ? c.mc.lid : c.mc.line;
}

void dogHead(const PetCtx& c) {
  MascotPen& d = c.d;
  const MascotLook& k = c.k;
  const PetColors& mc = c.mc;
  const int x = c.x, b = c.b;
  for (int s = -1; s <= 1; s += 2) {  // pointy ears, standing up and a little apart
    d.tri(s * 33 + x, -12 + b, s * 27 + x, -42 + b, s * 7 + x, -20 + b, mc.skin);
    if (c.detail) d.tri(s * 28 + x, -17 + b, s * 25 + x, -34 + b, s * 14 + x, -21 + b, mc.earIn);
  }
  d.rrect(-31 + x, -24 + b, 62, 54, 26, mc.skin);  // the head ...
  d.rrect(-35 + x, 2 + b, 70, 28, 14, mc.skin);    // ... and its full cheeks
  if (c.desk && (k.extras & kFluffed)) {  // fur standing up on the cheeks and the crown
    for (int s = -1; s <= 1; s += 2) {
      d.tri(s * 34 + x, 6 + b, s * 44 + x, 12 + b, s * 34 + x, 18 + b, mc.skin);
      d.tri(s * 33 + x, 16 + b, s * 43 + x, 23 + b, s * 32 + x, 28 + b, mc.skin);
      d.tri(s * 6 + x, -22 + b, s * 2 + x, -30 + b, s * -2 + x, -22 + b, mc.skin);
    }
  }
  // The cream mask: both cheeks, the muzzle between them, and (on the desk) the chest under it.
  if (c.desk) d.rrect(-14 + x, 24 + b, 28, 17, 8, mc.accent);
  d.circle(-19 + x, 20 + b, 11, mc.accent);
  d.circle(19 + x, 20 + b, 11, mc.accent);
  d.rrect(-14 + x, 10 + b, 28, 21, 10, mc.accent);
  // The eyebrow spots, raised high (the eyebrows' "hm?").
  for (int s = -1; s <= 1 && c.detail; s += 2) {
    d.circle(s * 16 + x, -11 + b, 3, mc.accent);
    d.circle(s * 12 + x, -13 + b, 2, mc.accent);
  }
  // The side-eye: with nothing steering its gaze (and not cross-eyed), the pupils look aside.
  // Open round eyes then also get low lids over their tops: the suspicious "hm?" look.
  MascotLook side = k;
  const bool aside = k.gx == 0 && k.gy == 0 && !(k.extras & kCrossEyed);
  if (aside) side.gx = -3;
  const PetCtx e{c.d, side, c.mc, c.x, c.b, c.detail, c.desk, c.wag};
  petEyes(e);
  if (aside && k.eyes == Eyes::Open && !(k.extras & kGrumpy) &&
      mc.eyeShape == (uint8_t)miblo::EyeShape::Round) {
    for (int ex = -14; ex <= 14; ex += 28) {
      d.rect(ex - 9 + x, -3 + b, 18, 3, mc.skin);
      d.rect(ex - 8 + x, 0 + b, 16, 2, mc.lid);
    }
  }
  if (petTired(c)) {
    petEyeBag(c, -14, 6);
    petEyeBag(c, 14, 6);
  }
  d.rrect(-6 + x, 12 + b, 12, 7, 3, mc.nose);            // the nose ...
  if (c.detail) d.rect(-3 + x, 13 + b, 3, 2, mc.earIn);  // ... and its shine
  if (!c.desk || !(k.extras & (kMouthO | kMouthWide))) {  // a small, closed "w" mouth under it
    const uint16_t m = maskLine(c);
    d.rect(-1 + x, 19 + b, 2, 4, m);
    d.rect(-6 + x, 23 + b, 6, 2, m);
    d.rect(x, 23 + b, 6, 2, m);
    d.rect(-8 + x, 21 + b, 2, 2, m);
    d.rect(6 + x, 21 + b, 2, 2, m);
  }
}

// A paw in a cream sock: a darker outline and two toe lines.
void sockPaw(const PetCtx& c, int px, int py, int pw, int ph, int r) {
  const int x = c.x, b = c.b;
  c.d.rrect(px + x - 1, py + b - 1, pw + 2, ph + 2, r + 1, c.mc.line);
  c.d.rrect(px + x, py + b, pw, ph, r, c.mc.accent);
  for (int t = 1; t <= 2; t++) c.d.rect(px + x + pw * t / 3, py + b + 1, 1, ph / 2, c.mc.line);
}

void dogFront(const PetCtx& c) {
  MascotPen& d = c.d;
  const PetColors& mc = c.mc;
  const int x = c.x, b = c.b;
  const uint16_t m = maskLine(c);
  if (c.k.extras & kMouthO) d.circle(x, 24 + b, 3, m);
  if (c.k.extras & kMouthWide) {  // a yawn or a sneeze
    d.rrect(-7 + x, 20 + b, 14, 11, 5, m);
    d.rect(-4 + x, 26 + b, 8, 4, mc.earIn);
  }
  petPaws(c, sockPaw);
  if (c.k.extras & kTongue) {  // a dog's tongue: out and hanging
    d.rrect(-4 + x, 23 + b, 8, 10, 3, mc.line);
    d.rrect(-3 + x, 23 + b, 6, 9, 3, mc.earIn);
    d.rect(x, 25 + b, 1, 4, mc.line);
  }
}

// The Tail antic: its curled tail up behind it, a ring with a cream underside, wagging side to side.
void dogTail(const PetCtx& c) {
  const int x = c.x, b = c.b, tx = 39 + 3 * petSwing(c) + x;
  c.d.tri(24 + x, 14 + b, 28 + x, 34 + b, tx - 4, 14 + b, c.mc.line);  // its root, from the back
  c.d.circle(tx, 8 + b, 11, c.mc.line);  // the outline, then the curl over it
  c.d.tri(25 + x, 16 + b, 29 + x, 32 + b, tx - 4, 15 + b, c.mc.skin);
  c.d.circle(tx, 8 + b, 10, c.mc.skin);
  c.d.circle(tx + 1, 9 + b, 4, c.mc.accent);
}

}  // namespace

// Amber-brown eyes, the cream mask.
const PetDef kPetDog MIBLO_ROM = {dogHead, dogFront, MIBLO_CAT_ANCHORS, kAmberBrown, kCream, dogTail};

}  // namespace screens
