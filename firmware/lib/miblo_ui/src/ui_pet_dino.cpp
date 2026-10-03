// The Dino pet (miblo::Pet::Dino): a cute little dinosaur with a round head, a row of back plates
// (the accent colour) along the top, spots on its cheeks, nostrils on a rounded snout, a wide grin
// with two tiny teeth, short clawed arms and a chunky tail. See ui_pet.h for the contract.
#include "miblo_rom.h"
#include "ui_pet.h"

namespace screens {

namespace color = ui::color;

namespace {

constexpr uint16_t kDinoGreen = 0x3E2C;  // #3cc464: the plates' and spots' Auto colour

// The back plates on top of the head: base from x0 to x1 (at y -14) and the tip's y.
const int8_t kPlates[][3] MIBLO_ROM = {{-21, -7, -28}, {-8, 8, -37}, {7, 21, -28}};

// The tail, out behind it on its right: its tip at (tx, ty), outlined; with two little plates
// when `plates` (in the Tail antic, where all of it shows).
void tail(const PetCtx& c, int tx, int ty, bool plates) {
  MascotPen& d = c.d;
  const int x = c.x, b = c.b;
  d.tri(22 + x, 13 + b, 22 + x, 37 + b, tx + 2 + x, ty + b, c.mc.line);
  d.tri(23 + x, 15 + b, 23 + x, 35 + b, tx + x, ty + b, c.mc.skin);
  if (!plates) return;
  const int mx = (22 + tx) / 2, my = (13 + ty) / 2 + 2;  // halfway along its top edge
  d.tri(mx - 7 + x, my + 3 + b, mx - 1 + x, my + b, mx - 6 + x, my - 5 + b, c.mc.accent);
  d.tri(mx + 3 + x, my + (ty - 13) / 4 + 2 + b, mx + 8 + x, my + (ty - 13) / 4 + b, mx + 6 + x,
        my + (ty - 13) / 4 - 5 + b, c.mc.accent);
}

// A short arm: a skin pad with a darker outline and three little claws along its top.
void clawPaw(const PetCtx& c, int px, int py, int pw, int ph, int r) {
  const int x = c.x, b = c.b;
  c.d.rrect(px + x - 1, py + b - 1, pw + 2, ph + 2, r + 1, c.mc.line);
  c.d.rrect(px + x, py + b, pw, ph, r, c.mc.skin);
  for (int t = 1; t <= 3; t++) c.d.rect(px + x + pw * t / 4 - 1, py + b, 2, 2, c.mc.earIn);
}

}  // namespace

void dinoHead(const PetCtx& c) {
  MascotPen& d = c.d;
  const PetColors& mc = c.mc;
  const int x = c.x, b = c.b;
  if (!c.wag) tail(c, 41, 30, false);  // resting (the Tail antic thumps it: dinoTail)
  for (size_t i = 0; i < sizeof(kPlates) / sizeof(kPlates[0]); i++) {  // the back plates
    int8_t p[3];
    mibloRomCopy(p, kPlates[i], sizeof(p));
    const int mid = (p[0] + p[1]) / 2;
    d.tri(p[0] + x, -12 + b, p[1] + x, -12 + b, mid + x, p[2] + b, mc.accent);
    d.circle(mid + x, p[2] + 4 + b, 3, mc.accent);  // a rounded tip
  }
  d.rrect(-31 + x, -18 + b, 62, 50, 24, mc.skin);  // the round head
  d.rrect(-26 + x, 8 + b, 52, 24, 12, mc.skin);   // the snout, a little wider
  if (c.desk && (c.k.extras & kFluffed)) {  // bristling: little plates up on both cheeks
    for (int s = -1; s <= 1; s += 2) {
      d.tri(s * 29 + x, 0 + b, s * 39 + x, 4 + b, s * 29 + x, 9 + b, mc.accent);
      d.tri(s * 29 + x, 12 + b, s * 40 + x, 17 + b, s * 28 + x, 21 + b, mc.accent);
    }
  }
  if (c.detail) {  // two spots on its forehead
    d.circle(-4 + x, -11 + b, 2, mc.accent);
    d.circle(4 + x, -9 + b, 1, mc.accent);
  }
  petEyes(c);
  if (petTired(c)) {
    petEyeBag(c, -14, 6);
    petEyeBag(c, 14, 6);
  }
  d.rrect(-8 + x, 15 + b, 4, 3, 1, mc.nose);  // the nostrils
  d.rrect(4 + x, 15 + b, 4, 3, 1, mc.nose);
  if (!c.desk || !(c.k.extras & (kMouthO | kMouthWide))) {  // a wide grin with two tiny teeth
    d.rect(-15 + x, 20 + b, 3, 2, mc.lid);
    d.rect(-12 + x, 22 + b, 24, 2, mc.lid);
    d.rect(12 + x, 20 + b, 3, 2, mc.lid);
    d.tri(-9 + x, 24 + b, -4 + x, 24 + b, -6 + x, 28 + b, color::WHITE);
    d.tri(4 + x, 24 + b, 9 + x, 24 + b, 6 + x, 28 + b, color::WHITE);
  }
}

void dinoFront(const PetCtx& c) {
  MascotPen& d = c.d;
  const PetColors& mc = c.mc;
  const int x = c.x, b = c.b;
  if (c.k.extras & kMouthO) d.circle(x, 24 + b, 3, mc.lid);
  if (c.k.extras & kMouthWide) {  // a yawn or a sneeze (or a little roar), the teeth showing
    d.rrect(-9 + x, 20 + b, 18, 11, 5, mc.lid);
    d.rect(-5 + x, 26 + b, 10, 4, mc.earIn);
    d.tri(-7 + x, 21 + b, -3 + x, 21 + b, -5 + x, 24 + b, color::WHITE);
    d.tri(3 + x, 21 + b, 7 + x, 21 + b, 5 + x, 24 + b, color::WHITE);
  }
  petPaws(c, clawPaw);
  if (c.k.extras & kTongue) d.rrect(-3 + x, 23 + b, 6, 6, 2, mc.earIn);
}

// The Tail antic: the tail wags up and thumps down on the table.
static void dinoTail(const PetCtx& c) {
  const int w = petSwing(c), tx = 52 - (w < 0 ? -w : w), ty = 30 + 4 * w;
  tail(c, tx, ty, true);
  for (int i = -1; i <= 1; i += 2)  // motion marks off its tip, the way it is going
    c.d.rect(tx + 4 + c.x, ty + (w < 0 ? 4 : -4) + 3 * i + c.b, 4, 1, c.mc.line);
}

// Amber eyes, green plates.
const PetDef kPetDino MIBLO_ROM = {dinoHead, dinoFront, MIBLO_CAT_ANCHORS, color::AMBER, kDinoGreen, dinoTail};

}  // namespace screens
