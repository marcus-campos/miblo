// Visits and pets from elsewhere, to review how a guest looks on the host (its own pet, colours,
// eye shape and the day's holiday hat), Friday the 13th's stranger (a dark one of our own kind)
// and pet mode on holidays: 45-guest-*, 58-stranger-*, 59-holiday-*.
#include <string.h>

#include "miblo_friends.h"
#include "miblo_occasions.h"
#include "shots.h"

namespace shots {

namespace {
using miblo::Accessory;
using miblo::Pet;
using screens::MascotPaint;

void dress(uint8_t style, Pet pet, uint32_t body = miblo::kPetAuto, uint8_t hat = 0) {
  MascotPaint p;
  p.style = style;
  p.pet = (uint8_t)pet;
  p.slots[miblo::kSlotBody] = body;
  screens::setMascotPaint(p);
  screens::setMascotAccessory(hat);
  screens::setGuestAccessory(hat);
  screens::setMascotTie(false);
  screens::setCatMood(0);
}

// A guest's look as its own Miblo sends it.
miblo::FriendLook look(uint32_t body, uint32_t accent, uint32_t eye, uint8_t eyes) {
  uint32_t slots[miblo::kPetSlots] = {};
  slots[miblo::kSlotBody] = body;
  slots[miblo::kSlotAccent] = accent;
  slots[miblo::kSlotEye] = eye;
  return miblo::friendLook(slots, eyes);
}
}  // namespace

void renderVisits(miblo::Lang L) {
  const screens::Clock clk = clock();
  usage(34, 21);
  // Guests in their own colours: a teal dog with big eyes visits our default cat; then a group of
  // three, each in its own look (a custom pink owl, a preset grey robot, a sleepy-eyed duck).
  const struct {
    uint8_t hat;
    const char* name;
  } days[] = {{0, ""}, {(uint8_t)Accessory::SantaHat, "-christmas"}, {(uint8_t)Accessory::WitchHat, "-halloween"},
              {(uint8_t)Accessory::Hearts, "-valentine"}};
  for (const auto& d : days) {
    dress(0, Pet::Cat, miblo::kPetAuto, d.hat);
    miblo::VisitView v;
    strcpy(v.name, "Nina");
    v.role = miblo::VisitRole::Host;
    v.gift = miblo::Gift::None;
    v.ms = miblo::kVisitArriveMs + 1000;
    v.mascot = 1;
    v.pet = (uint8_t)Pet::Dog;
    v.look = look(0x2BB3A4 + 1, 0xFFD23F + 1, miblo::kPetAuto, (uint8_t)miblo::EyeShape::Big);
    {
      Shot s;
      screens::visit(L, snap, clk, v);
      save(s, std::string("45-guest-custom") + d.name);
    }
    v.extra = 2;
    v.pet = (uint8_t)Pet::Owl;
    v.look = look(0xF06292 + 1, miblo::kPetAuto, 0x40C4FF + 1, 0);
    v.extraMascot[0] = 3;
    v.extraPet[0] = (uint8_t)Pet::Robot;
    v.extraLook[0] = miblo::FriendLook();
    v.extraMascot[1] = 0;
    v.extraPet[1] = (uint8_t)Pet::Duck;
    v.extraLook[1] = look(miblo::kPetAuto, miblo::kPetAuto, miblo::kPetAuto, (uint8_t)miblo::EyeShape::Sleepy);
    v.ms = miblo::kVisitArriveMs + 5000;
    Shot s;
    screens::visit(L, snap, clk, v);
    save(s, std::string("45-guest-group") + d.name);
  }
  // Friday the 13th: the stranger is a dark one of our own pet's kind, walking and looking at you.
  miblo::Snapshot none{};
  const struct {
    Pet pet;
    uint8_t style;
    uint32_t body;
    const char* name;
  } owners[] = {{Pet::Cat, 0, miblo::kPetAuto, "cat"},     {Pet::Dog, 1, miblo::kPetAuto, "dog"},
                {Pet::Owl, 0, 0xF06292 + 1, "owl"},       {Pet::Robot, 3, miblo::kPetAuto, "robot"},
                {Pet::Duck, 0, miblo::kPetAuto, "duck"},  {Pet::Penguin, 0, miblo::kPetAuto, "penguin"},
                {Pet::Riff, 0, miblo::kPetAuto, "riff"}, {Pet::Daemon, 0, miblo::kPetAuto, "daemon"}};
  for (const auto& o : owners) {
    dress(o.style, o.pet, o.body);
    {
      Shot s;
      screens::passerby(L, none, clk, 1500);
      save(s, std::string("58-stranger-") + o.name);
    }
    Shot s;
    screens::passerby(L, none, clk, 3400 + 300);
    save(s, std::string("58-stranger-") + o.name + "-look");
  }
  // Pet mode on holidays: the day's hat on a few pets.
  const Pet pets[] = {Pet::Cat, Pet::Dog, Pet::Owl, Pet::Robot, Pet::Crab, Pet::Mug};
  const char* const petNames[] = {"cat", "dog", "owl", "robot", "crab", "mug"};
  idle();
  for (size_t i = 0; i < sizeof(pets) / sizeof(pets[0]); i++) {
    for (size_t k = 1; k < sizeof(days) / sizeof(days[0]); k++) {
      dress(0, pets[i], miblo::kPetAuto, days[k].hat);
      Shot s;
      screens::roam(L, snap, clk, 20000, screens::DeskMood::Calm);
      save(s, std::string("59-holiday-") + petNames[i] + days[k].name);
    }
  }
  dress(0, Pet::Cat);
  screens::setGuestAccessory(0);
}

}  // namespace shots
