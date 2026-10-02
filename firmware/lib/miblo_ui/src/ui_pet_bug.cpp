// The bug pet (miblo::Pet::Bug): a small cute beetle: a round head with big friendly eyes and two
// short antennae, on a domed shell split by its centre seam, six little legs. See ui_pet.h for the
// contract.
#include "miblo_rom.h"
#include "ui_pet.h"

namespace screens {

namespace {

// The eyes sit this much higher than the cat's (y 6 - 10).
constexpr int kEyeLift = 10;

// A 2-unit stroke from (x0, y0) to (x1, y1) (mostly vertical), in `col`.
void stroke(const PetCtx& c, int x0, int y0, int x1, int y1, uint16_t col) {
  const int x = c.x, b = c.b;
  c.d.tri(x0 - 1 + x, y0 + b, x0 + 1 + x, y0 + b, x1 + 1 + x, y1 + b, col);
  c.d.tri(x0 - 1 + x, y0 + b, x1 + 1 + x, y1 + b, x1 - 1 + x, y1 + b, col);
}

// A front leg's foot: a small rounded skin pad, outlined, with a joint line across it.
void bugFoot(const PetCtx& c, int px, int py, int pw, int ph, int r) {
  const int x = c.x, b = c.b;
  c.d.rrect(px + x - 1, py + b - 1, pw + 2, ph + 2, r + 1, c.mc.line);
  c.d.rrect(px + x, py + b, pw, ph, r, c.mc.skin);
  c.d.rect(px + x + 2, py + b + ph / 2, pw - 4, 1, c.mc.line);
}

void bugHead(const PetCtx& c) {
  MascotPen& d = c.d;
  const PetColors& mc = c.mc;
  const int x = c.x, b = c.b;
  const bool fluffed = c.desk && (c.k.extras & kFluffed);
  // The antennae (straight up when bristling), round tips in the accent.
  for (int s = -1; s <= 1; s += 2) {
    const int tx = fluffed ? s * 13 : s * 19;
    stroke(c, s * 9, -20, tx, -37, mc.line);
    d.circle(tx + x, -37 + b, 3, mc.accent);
  }
  // The shell: a dome behind the head, split by its seam.
  d.rrect(-39 + x, 1 + b, 78, 39, 20, mc.line);
  d.rrect(-38 + x, 2 + b, 76, 37, 19, mc.skin);
  d.rect(-1 + x, 12 + b, 2, 27, mc.line);
  if (c.detail) {  // a sheen on each half of the shell
    d.rrect(-30 + x, 20 + b, 3, 9, 1, mc.earIn);
    d.rrect(27 + x, 20 + b, 3, 9, 1, mc.earIn);
  }
  // Four little legs out of the shell's sides (the front two are front()'s feet).
  for (int s = -1; s <= 1; s += 2) {
    const int o = s < 0 ? -1 : 0;  // mirror a w-unit-wide piece: left edge s * a + o * w
    for (int ly = 14; ly <= 26; ly += 12) {
      d.rect(s * 38 + o * 5 + x, ly + b, 5, 2, mc.line);
      d.rect(s * 42 + o * 2 + x, ly + b, 2, 5, mc.line);
    }
  }
  // The head.
  d.rrect(-31 + x, -25 + b, 62, 42, 20, mc.line);
  d.rrect(-30 + x, -24 + b, 60, 40, 19, mc.skin);
  if (fluffed) {  // bristling: spikes on both sides of the head
    for (int s = -1; s <= 1; s += 2) {
      d.tri(s * 29 + x, -14 + b, s * 38 + x, -9 + b, s * 29 + x, -4 + b, mc.skin);
      d.tri(s * 29 + x, -2 + b, s * 39 + x, 3 + b, s * 29 + x, 8 + b, mc.skin);
    }
  }
  const PetCtx e{c.d, c.k, c.mc, c.x, c.b - kEyeLift, c.detail, c.desk};
  petEyes(e);
  if (petTired(c)) {
    petEyeBag(e, -14, 6);
    petEyeBag(e, 14, 6);
  }
  // Rosy cheeks (the nose slot) and a small smile.
  d.circle(-23 + x, 8 + b, 3, mc.nose);
  d.circle(23 + x, 8 + b, 3, mc.nose);
  d.rect(-4 + x, 8 + b, 2, 2, mc.lid);
  d.rect(-2 + x, 10 + b, 4, 2, mc.lid);
  d.rect(2 + x, 8 + b, 2, 2, mc.lid);
}

void bugFront(const PetCtx& c) {
  MascotPen& d = c.d;
  const PetColors& mc = c.mc;
  const int x = c.x, b = c.b;
  if (c.k.extras & (kMouthO | kMouthWide)) d.rect(-5 + x, 7 + b, 10, 6, mc.skin);  // no smile
  if (c.k.extras & kMouthO) d.circle(x, 10 + b, 3, mc.lid);
  if (c.k.extras & kMouthWide) {  // a yawn or a sneeze
    d.rrect(-6 + x, 6 + b, 12, 10, 5, mc.lid);
    d.rect(-3 + x, 12 + b, 6, 3, mc.earIn);
  }
  if (c.k.paws == Paws::Cover) {  // the feet over the eyes, which sit higher than the cat's
    const PetCtx e{c.d, c.k, c.mc, c.x, c.b - kEyeLift, c.detail, c.desk};
    petPaws(e, bugFoot);
  } else {
    petPaws(c, bugFoot);
  }
  if (c.k.extras & kTongue) d.rrect(-2 + x, 11 + b, 4, 5, 2, mc.earIn);
}

}  // namespace

const PetDef kPetBug MIBLO_ROM = {bugHead, bugFront, {0, -8, 0, -12, -4, 14}, 0x2514, ui::color::RED};

}  // namespace screens
