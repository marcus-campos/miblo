// The settings page's "Preview on Miblo" (miblo_preview.h): the look the form has now, unsaved.
#include "miblo_overview.h"
#include "ui_internal.h"
#include "ui_screens.h"

namespace screens {

void preview(const MascotPaint& paint, const MascotOutfit& outfit, uint32_t ms, uint32_t showMs) {
  // Our own look goes back after: the preview changes nothing for the other screens.
  const MascotPaint ownPaint = mascotPaint();
  const MascotOutfit ownOutfit = mascotOutfit();
  const uint8_t ownHat = mascotAccessory(), ownMood = catMood();
  const bool ownTie = mascotTie();
  setMascotPaint(paint);
  setMascotOutfit(outfit);
  setMascotAccessory(0);  // the picks as they are (a special day would take its slot)
  setMascotTie(false);
  setCatMood(0);
  // A few expressions, 1.2 s each: looking ahead, to the side, happy, typing; a blink now and then.
  const uint8_t phase = (uint8_t)(ms / 1200 % 4);
  MascotLook k{0, 0, 0, 0, Eyes::Open, Paws::Down, 0};
  if (phase == 1) k.gx = -3;
  if (phase == 2) k.eyes = Eyes::Happy, k.dy = -3, k.extras = kMouthO;
  if (phase == 3) k.gx = 3, k.gy = 2, k.paws = Paws::TapRight;
  if (phase != 2 && ms % 4000 < 160) k.eyes = Eyes::Closed;
  deskCat(R_BODY, X(120), Y(110), 64, k);
  setMascotPaint(ownPaint);
  setMascotOutfit(ownOutfit);
  setMascotAccessory(ownHat);
  setMascotTie(ownTie);
  setCatMood(ownMood);
  // The time left, as a bar running out (in 48 steps: a redraw every ~0.3 s at most).
  const uint32_t left = ms < showMs ? showMs - ms : 0;
  const int step = showMs ? (int)(left * 48 / showMs) : 0;
  if (region(R_FOOT, miblo::hashInt(miblo::kHashSeed + 131, (uint32_t)step), 0, Y(206), X(240), Y(16))) {
    C().fillRect(X(48), Y(210), X(144), Y(6) > 0 ? Y(6) : 1, color::TRACK);
    C().fillRect(X(48), Y(210), X(144) * step / 48, Y(6) > 0 ? Y(6) : 1, color::BLUE);
  }
}

}  // namespace screens
