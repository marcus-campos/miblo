// Screenshots of the mascot's looks (see shots.h): the special days' accessories, the meeting tie,
// focus headphones, tired eye bags and Friday the 13th's black cat.
#include "miblo_mood.h"
#include "miblo_occasions.h"
#include "shots.h"

namespace shots {

namespace {
using miblo::Accessory;
using screens::Eyes;
using screens::MascotLook;
using screens::Paws;

// Puts the mascot back as it is everywhere else.
void plainMascot() {
  screens::setMascotStyle(0);
  screens::setMascotAccessory(0);
  screens::setMascotTie(false);
  screens::setCatMood(0);
}

// One mascot per cell, the four colours across, one row per `pieces` entry: to check every piece
// on every colour at once (the @4x PNG is the one to look at).
struct Piece {
  uint8_t accessory;
  bool tie;
  uint16_t extras;
};
void sheet(const Piece* pieces, int rows, const std::string& name) {
  Shot s;
  const int cell = 228 / 4, half = cell / 2 - 1;
  for (int r = 0; r < rows; r++) {
    for (uint8_t style = 0; style < 4; style++) {
      screens::setMascotStyle(style);
      screens::setMascotAccessory(pieces[r].accessory);
      screens::setMascotTie(pieces[r].tie);
      const MascotLook k{0, 0, 0, 0, Eyes::Open, Paws::Down, pieces[r].extras};
      screens::deskMascot(6 + style * cell + cell / 2, 6 + r * cell + cell / 2, k, half, false, false);
    }
  }
  plainMascot();
  save(s, name);
}
}  // namespace

void renderLook(miblo::Lang L) {
  const screens::Clock clk = shots::clock();
  usage(34, 21);
  // The new special days' accessories, on the desk mascot (like 27-hat-*).
  const struct {
    Accessory a;
    const char* name;
  } kDays[] = {{Accessory::BunnyEars, "bunny"}, {Accessory::Glasses, "glasses"}, {Accessory::Hearts, "hearts"}};
  for (const auto& d : kDays) {
    screens::setMascotAccessory((uint8_t)d.a);
    Shot s;
    screens::desk(L, snap, clk, 0);
    save(s, std::string("58-look-") + d.name);
  }
  plainMascot();
  // Meeting mode's tie, and a tired day (8 h of Claude working): bags under the eyes.
  {
    screens::setMascotTie(true);
    Shot s;
    screens::desk(L, snap, clk, 0);
    save(s, "58-look-tie");
    plainMascot();
  }
  {
    screens::setCatMood((uint8_t)miblo::CatMood::Tired);
    Shot s;
    screens::desk(L, snap, clk, 0);
    save(s, "58-look-tired");
    plainMascot();
  }
  // Focus headphones (the focus screen's cat, here on its own).
  {
    Shot s;
    const MascotLook k{0, 0, 0, 2, Eyes::Open, Paws::TapLeft, screens::kHeadphones};
    screens::deskMascot(screens::X(120), screens::Y(120), k, 64, true, true);
    save(s, "58-look-headphones");
  }
  // Every piece on every colour, and with the hats.
  const Piece kNew[] = {{(uint8_t)Accessory::BunnyEars, false, 0},
                        {(uint8_t)Accessory::Glasses, false, 0},
                        {(uint8_t)Accessory::Hearts, false, 0},
                        {0, true, 0}};
  sheet(kNew, 4, "58-look-sheet-days");
  const Piece kDaily[] = {{0, false, screens::kHeadphones},
                          {0, false, screens::kEyeBags},
                          {(uint8_t)Accessory::Glasses, true, (uint16_t)(screens::kHeadphones | screens::kEyeBags)},
                          {(uint8_t)Accessory::BunnyEars, true, screens::kHeadphones}};
  sheet(kDaily, 4, "58-look-sheet-daily");
  const Piece kHats[] = {{(uint8_t)Accessory::SantaHat, true, screens::kHeadphones},
                         {(uint8_t)Accessory::WitchHat, true, screens::kEyeBags},
                         {(uint8_t)Accessory::PartyHat, true, (uint16_t)(screens::kHeadphones | screens::kEyeBags)},
                         {(uint8_t)Accessory::Hearts, true, screens::kHeadphones}};
  sheet(kHats, 4, "58-look-sheet-hats");
  // Friday the 13th: the black cat on its way across pet mode, and stopping to look at you.
  miblo::Snapshot none{};
  {
    Shot s;
    screens::passerby(L, none, clk, 1500);
    save(s, "58-black-cat");
  }
  {
    Shot s;
    screens::passerby(L, none, clk, 3400 + 300);
    save(s, "58-black-cat-look");
  }
}

}  // namespace shots
