// The Dev-chan pet (miblo::Pet::DevChan): an original chibi dev companion, a round creature (not a
// human) with big sparkly eyes, a fringe and two pigtails (the accent colour) tied with little
// bobbles, rosy cheeks, a small smile and a hoodie collar. See ui_pet.h for the contract.
#include "miblo_mood.h"
#include "miblo_rom.h"
#include "ui_pet.h"

namespace screens {

namespace color = ui::color;

namespace {

constexpr uint16_t kLavender = 0x9BFE;  // #9c7cf4: the hair's Auto colour
constexpr uint16_t kTeal = 0x2D59;      // #28a8c8: the eyes' Auto colour
constexpr int kEyeY = 8;                // the eyes' centre (a little lower than the cat's)

// The pigtails, tied on both sides of the head and hanging outwards, bobbing by `bob` and swung
// out by `out`.
void pigtails(const PetCtx& c, int bob, int out) {
  MascotPen& d = c.d;
  const int x = c.x, b = c.b;
  for (int s = -1; s <= 1; s += 2) {
    const int o = s < 0 ? -1 : 0;  // mirror a w-unit-wide piece: left edge s * a + o * w
    d.tri(s * 24 + x, -16 + b, s * 33 + x, -18 + b, s * (39 + out) + x, 2 + bob + b, c.mc.accent);
    d.circle(s * (36 + out) + x, 8 + bob + b, 7, c.mc.accent);
    d.tri(s * (30 + out) + x, 10 + bob + b, s * (43 + out) + x, 8 + bob + b, s * (40 + 2 * out) + x,
          27 + bob + b, c.mc.accent);
    if (c.detail) d.rect(s * (36 + out) + o * 2 + x, 6 + bob + b, 2, 7, c.mc.line);  // a strand
  }
}

// One big eye centred at (ex, kEyeY) in every Eyes state and eye shape: a tall oval iris with a
// big pupil and two sparkles, a dark upper lash line with a flick at the outer corner.
void eye(const PetCtx& c, int ex, int s) {
  MascotPen& d = c.d;
  const MascotLook& k = c.k;
  const PetColors& mc = c.mc;
  const int x = c.x, b = c.b, y = kEyeY;
  const bool big = mc.eyeShape == (uint8_t)miblo::EyeShape::Big;
  switch (k.eyes) {
    case Eyes::Closed:  // a gentle downward curve, its lashes out at the corner
      d.rect(ex - 9 + x, y - 1 + b, 3, 2, mc.lid);
      d.rect(ex - 7 + x, y + 1 + b, 14, 2, mc.lid);
      d.rect(ex + 6 + x, y - 1 + b, 3, 2, mc.lid);
      d.tri(ex + s * 8 + x, y - 1 + b, ex + s * 12 + x, y - 3 + b, ex + s * 9 + x, y + 1 + b, mc.lid);
      return;
    case Eyes::Happy:  // "^ ^"
      d.tri(ex - 10 + x, y + 5 + b, ex + x, y - 5 + b, ex + 10 + x, y + 5 + b, mc.lid);
      d.tri(ex - 6 + x, y + 5 + b, ex + x, y - 1 + b, ex + 6 + x, y + 5 + b, mc.skin);
      return;
    case Eyes::Dizzy:
      d.circle(ex + x, y + b, 9, mc.eye);
      d.circle(ex + x, y + b, 7, mc.pupil);
      d.circle(ex + x, y + b, 5, mc.eye);
      d.circle(ex + x, y + b, 3, mc.pupil);
      d.circle(ex + x, y + b, 1, mc.eye);
      return;
    case Eyes::Wide:  // round, tiny pupils, still sparkling
      d.circle(ex + x, y + b, big ? 12 : 11, mc.eye);
      d.circle(ex + x + k.gx, y + b + k.gy, 4, mc.pupil);
      d.circle(ex - 4 + x + k.gx, y - 4 + b + k.gy, 2, color::WHITE);
      d.rrect(ex - 10 + x, y - 15 + b, 20, 3, 1, mc.lid);  // the lashes, raised
      return;
    case Eyes::Open:
    case Eyes::Sleepy:
      break;
  }
  const int cross = (k.extras & kCrossEyed) ? -s * 3 : 0;
  const int w = big ? 20 : 18, h = big ? 24 : 22;
  const int px = ex + x + k.gx + cross, py = y + b + k.gy;
  d.rrect(ex - w / 2 + x, y - h / 2 + b, w, h, w / 2, mc.eye);
  d.rrect(px - 5, py - 5, 10, 13, 5, mc.pupil);
  d.circle(px - 3, py - 4, big ? 3 : 2, color::WHITE);  // the sparkles
  d.circle(px + 3, py + 5, 1, color::WHITE);
  if (big) d.circle(px + 4, py - 6, 1, color::WHITE);
  // The lids: none, the relaxed eye shape's (the top third), or the Sleepy look's (the top half).
  int lid = y - h / 2;
  if (k.eyes == Eyes::Sleepy) lid = y;
  else if (mc.eyeShape == (uint8_t)miblo::EyeShape::Sleepy) lid = y - h / 2 + 7;
  if (lid > y - h / 2) d.rect(ex - w / 2 - 1 + x, y - h / 2 - 1 + b, w + 2, lid - (y - h / 2) + 1, mc.skin);
  d.rrect(ex - w / 2 + x, lid - 2 + b, w, 3, 1, mc.lid);  // the lash line
  d.tri(ex + s * (w / 2 - 2) + x, lid - 2 + b, ex + s * (w / 2 + 3) + x, lid - 5 + b, ex + s * (w / 2) + x,
        lid + 1 + b, mc.lid);
  if (k.extras & kGrumpy) {  // the lids slanting down towards the nose, a frowning lash along them
    const int top = y - h / 2 - 1 + b;
    d.tri(ex + s * 12 + x, top, ex - s * 12 + x, top, ex - s * 12 + x, y + 1 + b, mc.skin);
    d.tri(ex + s * 12 + x, top, ex - s * 11 + x, y + 1 + b, ex - s * 11 + x, y - 3 + b, mc.lid);
  }
}

}  // namespace

void devChanHead(const PetCtx& c) {
  MascotPen& d = c.d;
  const PetColors& mc = c.mc;
  const int x = c.x, b = c.b;
  if (!c.wag) pigtails(c, 0, 0);  // (the Tail antic bounces them: devChanTail)
  d.rrect(-33 + x, -24 + b, 66, 46, 26, mc.accent);  // the hair behind the head
  d.rrect(-31 + x, -19 + b, 62, 51, 25, mc.skin);    // the round head
  if (c.desk && (c.k.extras & kFluffed)) {  // bristling: tufts on both cheeks
    for (int s = -1; s <= 1; s += 2) {
      d.tri(s * 30 + x, 12 + b, s * 40 + x, 17 + b, s * 30 + x, 22 + b, mc.skin);
      d.tri(s * 28 + x, 21 + b, s * 37 + x, 26 + b, s * 26 + x, 29 + b, mc.skin);
    }
  }
  d.rrect(-30 + x, -22 + b, 60, 14, 12, mc.accent);  // the fringe, in soft rounded locks
  for (int fx = -20; fx <= 20; fx += 13) d.circle(fx + x, -10 + b, 6, mc.accent);
  for (int s = -1; s <= 1; s += 2) {  // the bobbles tying the pigtails
    d.circle(s * 27 + x, -15 + b, 4, mc.earIn);
    if (c.detail) d.circle(s * 26 + x, -16 + b, 1, color::WHITE);
  }
  if (c.detail) {  // the hoodie's collar where it meets the table, its strings
    d.rrect(-25 + x, 27 + b, 50, 7, 3, mc.line);
    d.rect(-7 + x, 30 + b, 2, 8, mc.earIn);
    d.rect(5 + x, 30 + b, 2, 8, mc.earIn);
  }
  d.rrect(-28 + x, 18 + b, 9, 5, 2, mc.earIn);  // rosy cheeks
  d.rrect(19 + x, 18 + b, 9, 5, 2, mc.earIn);
  eye(c, -14, -1);
  eye(c, 14, 1);
  if (petTired(c)) {
    petEyeBag(c, -14, kEyeY + 2);
    petEyeBag(c, 14, kEyeY + 2);
  }
  d.rect(-1 + x, 20 + b, 2, 2, mc.nose);  // a dot of a nose
  if (!c.desk || !(c.k.extras & (kMouthO | kMouthWide))) {  // a small smile
    d.rect(-5 + x, 23 + b, 2, 2, mc.lid);
    d.rect(-3 + x, 24 + b, 6, 2, mc.lid);
    d.rect(3 + x, 23 + b, 2, 2, mc.lid);
  }
}

void devChanFront(const PetCtx& c) {
  MascotPen& d = c.d;
  const PetColors& mc = c.mc;
  const int x = c.x, b = c.b;
  if (c.k.extras & kMouthO) d.circle(x, 25 + b, 3, mc.lid);
  if (c.k.extras & kMouthWide) {  // a yawn or a sneeze
    d.rrect(-6 + x, 22 + b, 12, 9, 4, mc.lid);
    d.rect(-3 + x, 26 + b, 6, 4, mc.earIn);
  }
  petPaws(c, petPadPaw);
  if (c.k.extras & kTongue) d.rrect(-2 + x, 24 + b, 5, 5, 2, mc.earIn);
}

// The Tail antic: the pigtails bounce.
static void devChanTail(const PetCtx& c) {
  const int w = petSwing(c), aw = w < 0 ? -w : w;
  pigtails(c, 2 * w, aw);
  for (int s = -1; s <= 1; s += 2)  // little bounce marks under the pigtails' tips
    c.d.rect(s * (41 + 2 * aw) - 2 + c.x, 31 + 2 * w + c.b, 4, 1, c.mc.line);
}

// The eyes a little lower than the cat's: the glasses go there.
const PetDef kPetDevChan MIBLO_ROM = {devChanHead, devChanFront, {0, 0, 2, 0, kEyeY, 14}, kTeal, kLavender,
                                      devChanTail};

}  // namespace screens
