// The mug pet (miblo::Pet::Mug): a coffee mug with a face. A cylinder (skin) standing on the desk,
// its rim and the coffee inside (line) on top, a handle on its right (earIn), steam wisps rising
// (accent), rosy cheeks (nose) and little hands. See ui_pet.h for the contract.
#include "miblo_rom.h"
#include "ui_pet.h"

namespace screens {

namespace color = ui::color;

void mugHead(const PetCtx& c) {
  MascotPen& d = c.d;
  const MascotLook& k = c.k;
  const PetColors& mc = c.mc;
  const int x = c.x, b = c.b;
  // Steam: two wavy wisps, swaying with the look.
  if (c.detail) {
    const int sway = ((int)k.eyes + (int)k.paws + k.gx) & 1 ? 2 : -2;
    for (int s = -1; s <= 1; s += 2) {
      const int sx = s * 11 + x;
      d.rrect(sx - 1 + sway, -42 + b, 3, 5, 1, mc.accent);
      d.rrect(sx - 1 - sway, -37 + b, 3, 5, 1, mc.accent);
      d.rrect(sx - 1 + sway, -32 + b, 3, 4, 1, mc.accent);
    }
  }
  // The handle on the right: a thick ring with the background through it.
  d.rrect(22 + x, -6 + b, 19, 30, 9, mc.earIn);
  d.rrect(27 + x, 0 + b, 9, 18, 4, color::BG);
  // The body: a cylinder down to the table, the rim and the coffee on top.
  d.rrect(-30 + x, -22 + b, 60, 60, 8, mc.skin);
  d.rrect(-31 + x, -28 + b, 62, 12, 6, mc.skin);   // the rim
  d.rrect(-27 + x, -26 + b, 54, 8, 4, mc.line);    // the coffee
  if (c.detail) d.rect(-18 + x, -24 + b, 8, 2, mc.earIn);  // a gleam on the coffee
  if (c.desk && (k.extras & kFluffed)) {  // the coffee boiling over: bubbles and drips
    d.circle(-12 + x, -29 + b, 4, mc.line);
    d.circle(-2 + x, -31 + b, 5, mc.line);
    d.circle(10 + x, -29 + b, 4, mc.line);
    d.rrect(-25 + x, -20 + b, 5, 12, 2, mc.line);
    d.rrect(-14 + x, -20 + b, 4, 7, 2, mc.line);
    d.rrect(16 + x, -20 + b, 5, 10, 2, mc.line);
  }
  if (c.detail) {  // rosy cheeks
    d.rrect(-28 + x, 15 + b, 8, 4, 2, mc.nose);
    d.rrect(20 + x, 15 + b, 8, 4, 2, mc.nose);
  }
  petEyes(c);
  if (petTired(c)) {
    petEyeBag(c, -14, 6);
    petEyeBag(c, 14, 6);
  }
  if (!c.desk || !(k.extras & (kMouthO | kMouthWide | kTongue))) {  // a small smile
    d.rect(-4 + x, 21 + b, 8, 2, mc.lid);
    d.rect(-6 + x, 19 + b, 2, 2, mc.lid);
    d.rect(4 + x, 19 + b, 2, 2, mc.lid);
  }
}

// A hand: a small rounded mitten in the body's colour, outlined.
static void mugHand(const PetCtx& c, int px, int py, int pw, int ph, int r) {
  const int x = c.x, b = c.b;
  c.d.rrect(px + x - 1, py + b - 1, pw + 2, ph + 2, r + 1, c.mc.line);
  c.d.rrect(px + x, py + b, pw, ph, r, c.mc.skin);
  c.d.rect(px + x + pw / 2, py + b + 1, 1, ph / 2, c.mc.line);
}

void mugFront(const PetCtx& c) {
  MascotPen& d = c.d;
  const PetColors& mc = c.mc;
  const int x = c.x, b = c.b;
  if (c.k.extras & kMouthO) d.circle(x, 22 + b, 3, mc.lid);
  if (c.k.extras & kMouthWide) {  // a yawn or a sneeze
    d.rrect(-7 + x, 17 + b, 14, 10, 5, mc.lid);
    d.rect(-4 + x, 23 + b, 8, 3, mc.earIn);
  }
  if (c.k.extras & kTongue) {
    d.rect(-5 + x, 19 + b, 10, 2, mc.lid);
    d.rrect(-3 + x, 20 + b, 6, 5, 2, mc.earIn);
  }
  petPaws(c, mugHand);
}

// The Tail antic: no tail, so the steam curls off to the side into a little heart, rising.
static void mugTail(const PetCtx& c) {
  MascotPen& d = c.d;
  const int f = (c.wag - 1) % 4, x = c.x + petSwing(c), b = c.b;
  d.circle(18 + x, -38 + b, 1, c.mc.accent);  // the curl off the right wisp
  d.circle(22 + x, -41 + b, 2, c.mc.accent);
  const int hy = -38 - f + b;
  d.circle(26 + x, hy, 3, c.mc.accent);
  d.circle(32 + x, hy, 3, c.mc.accent);
  d.tri(23 + x, hy + 1, 35 + x, hy + 1, 29 + x, hy + 7, c.mc.accent);
}

const PetDef kPetMug MIBLO_ROM = {mugHead, mugFront, {0, 0, 2, 0, 6, 14}, 0x1C90, color::WHITE, mugTail};

}  // namespace screens
