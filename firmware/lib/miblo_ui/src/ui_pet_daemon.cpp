// The daemon pet (miblo::Pet::Daemon): a little ghost (a background process): a tall rounded top
// on a wavy hem, floating just above the desk, rimmed with a glow (the earIn slot), tiny arm nubs
// for paws and a small wisp by its head, the accent's "still running" light. See ui_pet.h for the
// contract.
#include "miblo_rom.h"
#include "ui_pet.h"

namespace screens {

namespace {

// The eyes sit this much higher than the cat's (y 6 - 4).
constexpr int kEyeLift = 4;
// The hem's four scallops: radius 7, centred at y kHemY and x -21, -7, 7, 21.
constexpr int kHemY = 29;

// An arm nub: a soft rounded skin blob a little smaller than a cat's paw, rimmed with the glow.
void daemonNub(const PetCtx& c, int px, int py, int pw, int ph, int r) {
  const int x = c.x, b = c.b;
  c.d.rrect(px + x + 1, py + b, pw - 2, ph, r, c.mc.earIn);
  c.d.rrect(px + x + 2, py + b + 1, pw - 4, ph - 2, r - 1, c.mc.skin);
}

void daemonHead(const PetCtx& c) {
  MascotPen& d = c.d;
  const PetColors& mc = c.mc;
  const int x = c.x, b = c.b;
  // The glow around the ghost, then its body over it: a tall dome on a wavy hem.
  d.rrect(-30 + x, -29 + b, 60, 58, 30, mc.earIn);
  for (int hx = -21; hx <= 21; hx += 14) d.circle(hx + x, kHemY + b, 9, mc.earIn);
  d.rrect(-28 + x, -27 + b, 56, 56, 28, mc.skin);
  for (int hx = -21; hx <= 21; hx += 14) d.circle(hx + x, kHemY + b, 7, mc.skin);
  if (c.detail) {
    // The wisp: a background process, still running.
    d.circle(36 + x, -31 + b, 3, mc.accent);
    d.circle(41 + x, -23 + b, 1, mc.accent);
  }
  if (c.desk && (c.k.extras & kFluffed)) {  // spooked: the sides flare out in points
    for (int s = -1; s <= 1; s += 2) {
      d.tri(s * 27 + x, -14 + b, s * 38 + x, -10 + b, s * 28 + x, -4 + b, mc.skin);
      d.tri(s * 28 + x, 2 + b, s * 39 + x, 7 + b, s * 28 + x, 12 + b, mc.skin);
    }
  }
  const PetCtx e{c.d, c.k, c.mc, c.x, c.b - kEyeLift, c.detail, c.desk};
  petEyes(e);
  if (petTired(c)) {
    petEyeBag(e, -14, 6);
    petEyeBag(e, 14, 6);
  }
  // Blushing cheeks (the nose slot) and a small smile.
  d.circle(-21 + x, 14 + b, 3, mc.nose);
  d.circle(21 + x, 14 + b, 3, mc.nose);
  d.rect(-4 + x, 14 + b, 2, 2, mc.lid);
  d.rect(-2 + x, 16 + b, 4, 2, mc.lid);
  d.rect(2 + x, 14 + b, 2, 2, mc.lid);
}

void daemonFront(const PetCtx& c) {
  MascotPen& d = c.d;
  const PetColors& mc = c.mc;
  const int x = c.x, b = c.b;
  if (c.k.extras & (kMouthO | kMouthWide)) d.rect(-5 + x, 13 + b, 10, 6, mc.skin);  // no smile
  if (c.k.extras & kMouthO) d.circle(x, 16 + b, 3, mc.lid);
  if (c.k.extras & kMouthWide) {  // a yawn or a boo
    d.rrect(-6 + x, 11 + b, 12, 11, 5, mc.lid);
    d.rect(-3 + x, 17 + b, 6, 4, mc.earIn);
  }
  if (c.k.paws == Paws::Down) {  // at rest the arms stay by its sides: it floats, nothing to lean on
    daemonNub(c, -40, 10, 14, 10, 5);
    daemonNub(c, 26, 10, 14, 10, 5);
  } else {
    petPaws(c, daemonNub);
  }
  if (c.k.extras & kTongue) d.rrect(-2 + x, 17 + b, 4, 5, 2, mc.earIn);
}

}  // namespace

const PetDef kPetDaemon MIBLO_ROM = {daemonHead, daemonFront, {0, -4, 0, -6, 2, 14}, ui::color::VIOLET, 0x262B};

}  // namespace screens
