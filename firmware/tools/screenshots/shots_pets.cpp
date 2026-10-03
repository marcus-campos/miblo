// Screenshots of every pet (miblo::Pet, see shots.h and lib/miblo_ui/src/ui_pet.h), to review a
// pet at a glance: 70-pet-<name>-<shot>. Sheets (the @4x PNG is the one to look at):
//   looks   every Eyes state, mood extra and paw pose
//   dress   the special days' hats, glasses and hearts, the tie, focus headphones, tired eyes
//   colors  the four presets across, by eye shape (round, big, sleepy) and two custom paints
// and the pet on the main screens: desk, focus, limit freed, pet mode, a few antics, a visit,
// the boot screen's small mascot.
#include <string.h>

#include "miblo_focus.h"
#include "miblo_mood.h"
#include "miblo_occasions.h"
#include "shots.h"

namespace shots {

namespace {
using miblo::Accessory;
using screens::Eyes;
using screens::MascotLook;
using screens::MascotPaint;
using screens::Paws;

const char* const kPetFiles[] = {"cat", "duck",   "bug", "daemon", "robot", "mug",
                                 "penguin", "crab", "owl", "dog", "alien", "riff",
                                 "dev", nullptr /* 13: never a pet */, "dino", "devchan"};
static_assert(sizeof(kPetFiles) / sizeof(kPetFiles[0]) == miblo::kPetIds, "one file name per pet value");

// The pet on its own, as everywhere else: the default colours, no hat, tie or mood.
void plain(uint8_t pet) {
  MascotPaint p;
  p.pet = pet;
  screens::setMascotPaint(p);
  screens::setMascotAccessory(0);
  screens::setMascotTie(false);
  screens::setCatMood(0);
}

// A grid of mascots, `cols` x `rows` cells over the screen (inside its 3 px margin).
struct Grid {
  int cols, rows;
  int cell() const { return 228 / (cols > rows ? cols : rows); }
  int half() const { return cell() / 2 - 1; }
  int cx(int c) const { return 6 + c * cell() + cell() / 2; }
  int cy(int r) const { return 6 + r * cell() + cell() / 2; }
  void draw(int c, int r, const MascotLook& k) const {
    screens::deskMascot(cx(c), cy(r), k, half(), false, false);
  }
};

void looks(uint8_t pet, const std::string& name) {
  plain(pet);
  const MascotLook kLooks[] = {
      {0, 0, 0, 0, Eyes::Open, Paws::Down, 0},
      {0, 0, 0, 0, Eyes::Closed, Paws::Down, 0},  // a blink
      {0, -4, 3, 0, Eyes::Happy, Paws::Down, screens::kMouthO},
      {2, 0, -3, 2, Eyes::Wide, Paws::Down, screens::kSweat},
      {0, 0, 0, 0, Eyes::Sleepy, Paws::Down, screens::kZ1},
      {0, 0, 0, 0, Eyes::Closed, Paws::Down, (uint16_t)(screens::kZ1 | screens::kZ2)},  // asleep
      {0, 0, 0, 0, Eyes::Dizzy, Paws::Down, screens::kStars},
      {0, -6, 3, 0, Eyes::Wide, Paws::Down, (uint16_t)(screens::kFluffed | screens::kAlarm)},
      {0, 0, 3, 0, Eyes::Open, Paws::Down, (uint16_t)(screens::kGrumpy | screens::kCrossEyed)},
      {0, 0, -3, 2, Eyes::Wide, Paws::ReachLeft, 0},
      {0, 0, 3, 2, Eyes::Wide, Paws::ReachRight, 0},
      {0, 0, 0, 0, Eyes::Closed, Paws::Cover, 0},
      {0, -3, 0, 0, Eyes::Closed, Paws::Up, screens::kMouthWide},
      {0, 0, 0, 0, Eyes::Closed, Paws::Lick, screens::kTongue},
      {0, 0, 0, 3, Eyes::Happy, Paws::TapLeft, screens::kHeart},
      {0, 0, 0, 0, Eyes::Open, Paws::TapRight, screens::kCoffee},
  };
  const Grid g{4, 4};
  Shot s;
  for (int i = 0; i < 16; i++) g.draw(i % 4, i / 4, kLooks[i]);
  save(s, name);
}

void dress(uint8_t pet, const std::string& name) {
  const struct {
    uint8_t hat;
    bool tie;
    uint16_t extras;
    uint8_t mood;
  } kDress[] = {
      {(uint8_t)Accessory::SantaHat, false, 0, 0},
      {(uint8_t)Accessory::WitchHat, false, 0, 0},
      {(uint8_t)Accessory::PartyHat, false, 0, 0},
      {(uint8_t)Accessory::BunnyEars, false, 0, 0},
      {(uint8_t)Accessory::Glasses, false, 0, 0},
      {(uint8_t)Accessory::Hearts, false, 0, 0},
      {0, true, 0, 0},
      {0, false, screens::kHeadphones, 0},
      {0, false, 0, (uint8_t)miblo::CatMood::Tired},
      {(uint8_t)Accessory::PartyHat, true, screens::kHeadphones, 0},
      {(uint8_t)Accessory::Glasses, true, (uint16_t)(screens::kHeadphones | screens::kEyeBags), 0},
      {(uint8_t)Accessory::SantaHat, true, screens::kHeadphones, 0},
  };
  const Grid g{4, 3};
  Shot s;
  for (int i = 0; i < 12; i++) {
    plain(pet);
    screens::setMascotAccessory(kDress[i].hat);
    screens::setMascotTie(kDress[i].tie);
    screens::setCatMood(kDress[i].mood);
    g.draw(i % 4, i / 4, MascotLook{0, 0, 0, 0, Eyes::Open, Paws::Down, kDress[i].extras});
  }
  plain(pet);
  save(s, name);
}

void colors(uint8_t pet, const std::string& name) {
  const Grid g{4, 4};
  Shot s;
  for (uint8_t shape = 0; shape < miblo::kEyeShapes; shape++) {
    for (uint8_t style = 0; style < 4; style++) {
      MascotPaint p;
      p.pet = pet;
      p.style = style;
      p.eyeShape = shape;
      screens::setMascotPaint(p);
      g.draw(style, shape, MascotLook{0, 0, 0, 0, Eyes::Open, Paws::Down, 0});
    }
  }
  // Custom paints: a light body (Auto details), a near-black one (Auto details), every part set,
  // and a dark body with big eyes.
  const uint32_t kPaints[4][miblo::kPetSlots] = {
      {0xFFF4D6 + 1},
      {0x101014 + 1},
      {0x3FA7F5 + 1, 0x1B4F80 + 1, 0xFFD23F + 1, 0xFF5C8A + 1, 0x0B1E33 + 1, 0xF5F5F5 + 1, 0xFF9F1C + 1},
      {0x5B2A86 + 1},
  };
  for (int i = 0; i < 4; i++) {
    MascotPaint p;
    p.pet = pet;
    p.eyeShape = i == 3 ? (uint8_t)miblo::EyeShape::Big : 0;
    memcpy(p.slots, kPaints[i], sizeof(p.slots));
    screens::setMascotPaint(p);
    g.draw(i, 3, MascotLook{0, 0, 0, 0, Eyes::Open, Paws::Down, 0});
  }
  plain(pet);
  save(s, name);
}

// The first antic of kind `a` from pet mode's start, `into` ms into it (0: none).
uint32_t anticAt(screens::RoamAntic a, uint32_t into) {
  for (uint32_t c = 1; c <= 2 * (screens::kAnticCount + 1); c++) {
    const uint32_t t0 = c * screens::kAnticEveryMs;
    if (screens::roamAntic(t0, nullptr) != a) continue;
    return t0 + (screens::anticOnSign(a) ? 0 : screens::kAnticPutMs) + into;
  }
  return 0;
}
}  // namespace

void renderPets(miblo::Lang L) {
  const screens::Clock clk = shots::clock();
  for (uint8_t pet = 0; pet < miblo::kPetIds; pet++) {
    if (!miblo::isPet(pet)) continue;
    const std::string n = std::string("70-pet-") + kPetFiles[pet] + "-";
    looks(pet, n + "looks");
    dress(pet, n + "dress");
    colors(pet, n + "colors");
    plain(pet);
    usage(34, 21);
    { Shot s; screens::desk(L, snap, clk, 0); save(s, n + "desk"); }
    usage(93, 40);
    { Shot s; screens::desk(L, snap, clk, 0); save(s, n + "desk-scared"); }
    usage(34, 21);
    {
      Shot s;
      screens::focus(L, clk, miblo::FocusPhase::Focus, 2, 4, (18 * 60 + 42) * 1000u, 25 * 60000u,
                     gNow + 18 * 60 + 42, 0);
      save(s, n + "focus");
    }
    { Shot s; screens::limitReset(L, snap, clk, 1000); save(s, n + "limit-freed"); }
    { Shot s; screens::roam(L, snap, clk, 20000, screens::DeskMood::Calm); save(s, n + "roam"); }
    const struct {
      screens::RoamAntic a;
      uint32_t into;
      const char* name;
    } kAntics[] = {{screens::RoamAntic::Stretch, 4000, "stretch"},
                   {screens::RoamAntic::Nap, 4000, "nap"},
                   {screens::RoamAntic::Coffee, 4000, "coffee"},
                   {screens::RoamAntic::Peek, 3750, "peek"},
                   {screens::RoamAntic::Tail, 1000, "tail"},
                   {screens::RoamAntic::Tail, 2000, "tail-2"},
                   {screens::RoamAntic::Tail, 4400, "tail-chase"},
                   {screens::RoamAntic::Tail, 7000, "tail-dizzy"},
                   {screens::RoamAntic::Solo, 2600, "solo"},
                   {screens::RoamAntic::Solo, 5000, "solo-2"}};
    for (const auto& a : kAntics) {
      const uint32_t at = anticAt(a.a, a.into);
      if (!at) continue;
      Shot s;
      screens::roam(L, snap, clk, at, screens::DeskMood::Calm);
      save(s, n + "antic-" + a.name);
    }
    {
      miblo::VisitView v;
      strcpy(v.name, "Nina");
      v.role = miblo::VisitRole::Host;
      v.ms = miblo::kVisitArriveMs + 1000;
      v.mascot = 3;
      v.pet = pet;  // a friend with the same pet, in grey, visits our default-coloured one
      Shot s;
      screens::visit(L, snap, clk, v);
      save(s, n + "visit");
    }
    { Shot s; screens::boot(L, 0); save(s, n + "boot"); }
    if (pet == (uint8_t)miblo::Pet::Riff) {  // its own long-task fanfare: air guitar
      for (uint32_t ms : {650u, 1500u}) {
        Shot s;
        screens::fanfare(L, "app-mobile", 23 * 60 + 7, ms);
        save(s, n + "fanfare" + (ms == 650 ? "" : "-2"));
      }
    }
  }
  plain(0);
}

}  // namespace shots
