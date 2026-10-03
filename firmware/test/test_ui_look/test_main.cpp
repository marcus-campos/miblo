// The mascot's daily-life look: special-day accessories, the meeting tie, focus headphones, tired
// eye bags, and Friday the 13th's black cat.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

#include "../support/fake_canvas.h"
#include "miblo_mood.h"
#include "miblo_occasions.h"
#include "ui_internal.h"
#include "ui_pet.h"
#include "ui_screens.h"

using namespace miblo;
using screens::Eyes;
using screens::MascotLook;
using screens::Paws;

void setUp() {
  screens::setMascotPaint(screens::MascotPaint{});
  screens::setMascotStyle(0);
  screens::setMascotAccessory(0);
  screens::setMascotTie(false);
  screens::setCatMood(0);
}
void tearDown() {}

static const ui::ScreenSpec kSpecs[] = {{240, 240}, {320, 240}, {480, 320}, {170, 320}};

// Every accessory (the hats and the new ones), with and without the tie, headphones and eye bags,
// in poses that hop, shiver and look around, the box touching each corner: nothing outside it.
static void test_every_look_stays_in_its_box() {
  const MascotLook looks[] = {
      {0, 0, 0, 0, Eyes::Open, Paws::Down, 0},
      {0, -5, 3, 0, Eyes::Happy, Paws::Down, screens::kMouthO},
      {2, 0, -3, 2, Eyes::Wide, Paws::Down, screens::kSweat},  // a shiver
      {-2, 0, 0, 0, Eyes::Open, Paws::Down, screens::kFluffed},
      {0, 0, -3, 2, Eyes::Wide, Paws::ReachLeft, 0},
      {0, -3, 0, 0, Eyes::Closed, Paws::Up, screens::kMouthWide},
      {0, 0, 3, 0, Eyes::Sleepy, Paws::Cover, (uint16_t)(screens::kZ1 | screens::kZ2)},
      {0, -4, 0, 3, Eyes::Open, Paws::TapLeft, (uint16_t)(screens::kAlarm | screens::kStars | screens::kHeart)},
  };
  const uint8_t accessories[] = {
      (uint8_t)Accessory::None,      (uint8_t)Accessory::SantaHat, (uint8_t)Accessory::WitchHat,
      (uint8_t)Accessory::PartyHat,  (uint8_t)Accessory::BunnyEars, (uint8_t)Accessory::Glasses,
      (uint8_t)Accessory::Hearts};
  for (const auto& spec : kSpecs) {
    FakeCanvas fc(spec);
    screens::bind(fc);
    const int half = screens::Sz(36);
    for (uint8_t style = 0; style < 4; style++) {
      screens::setMascotStyle(style);
      for (uint8_t a : accessories) {
        screens::setMascotAccessory(a);
        for (int extra = 0; extra < 8; extra++) {
          screens::setMascotTie(extra & 1);
          for (const auto& base : looks) {
            MascotLook k = base;
            if (extra & 2) k.extras |= screens::kHeadphones;
            if (extra & 4) k.extras |= screens::kEyeBags;
            screens::deskMascot(half, half, k, 36, false, false);
            screens::deskMascot(spec.w - half, spec.h - half, k, 36, false, false);
            screens::deskMascot(half, spec.h - half, k, 36, true, true);
            screens::deskMascot(spec.w - half, half, k, 36, true, true);
          }
          screens::mascot(spec.w - screens::Sz(48), screens::Sz(48), 2, false);
          screens::mascot(screens::Sz(24), spec.h - screens::Sz(24), 2, true);
        }
      }
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fc.outOfBounds, "out of bounds");
  }
}

// Every pet, in every eye shape, preset and custom colours, with every accessory, in poses that
// hop (dy -5), sink (dy 1), shiver (dx +-3) and look around, the box touching two corners:
// nothing outside it (the cat's own reach: a pet may reach as far, never further).
static void test_every_pet_stays_in_its_box() {
  const MascotLook looks[] = {
      {0, 0, 0, 0, Eyes::Open, Paws::Down, 0},
      {0, -5, 3, 0, Eyes::Wide, Paws::Up, (uint16_t)(screens::kFluffed | screens::kAlarm | screens::kMouthWide)},
      {3, 0, -3, 3, Eyes::Sleepy, Paws::ReachLeft, (uint16_t)(screens::kSweat | screens::kZ1 | screens::kZ2)},
      {-3, 0, 3, -3, Eyes::Happy, Paws::ReachRight, (uint16_t)(screens::kHeart | screens::kStars | screens::kMouthO)},
      {0, 1, 0, 0, Eyes::Dizzy, Paws::Cover, screens::kCoffee},
      {0, -3, 0, 0, Eyes::Closed, Paws::Lick, (uint16_t)(screens::kTongue | screens::kGrumpy)},
      {0, 0, 0, 3, Eyes::Open, Paws::TapLeft, screens::kCrossEyed},
      {0, 0, 0, 3, Eyes::Wide, Paws::TapRight, 0},
      {0, -5, 0, 0, Eyes::Closed, Paws::ReachRight, (uint16_t)(screens::kGuitar | screens::kMouthWide)},
      {0, 1, 0, 0, Eyes::Happy, Paws::Down, screens::kGuitar},
  };
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  for (uint8_t pet = 0; pet < kPetIds; pet++) {
    if (!isPet(pet)) continue;
    for (uint8_t shape = 0; shape < kEyeShapes; shape++) {
      for (int custom = 0; custom < 2; custom++) {
        screens::MascotPaint p;
        p.pet = pet;
        p.style = (uint8_t)(pet % 4);
        p.eyeShape = shape;
        if (custom) {
          for (uint8_t i = 0; i < kPetSlots; i++) p.slots[i] = 0x102030u * (i + 1) + 1;
        }
        screens::setMascotPaint(p);
        for (uint8_t a = 0; a <= (uint8_t)Accessory::Hearts; a++) {
          screens::setMascotAccessory(a);
          screens::setMascotTie(a & 1);
          for (const auto& base : looks) {
            MascotLook k = base;
            if (a & 2) k.extras |= (uint16_t)(screens::kHeadphones | screens::kEyeBags);
            screens::deskMascot(36, 36, k, 36, false, false);
            screens::deskMascot(204, 204, k, 36, true, true);
          }
          screens::mascot(192, 48, 2, false);
          screens::mascot(24, 216, 1, true);
        }
        char msg[48];
        snprintf(msg, sizeof(msg), "pet %u, eyes %u, custom %d: out of bounds", pet, shape, custom);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, fc.outOfBounds, msg);
      }
    }
  }
}

// Pet mode's Tail antic: every pet but the cat draws its own (the cat's curl is a prop, drawn by
// the antic), and only then; it stays within the reach the cat's tail has beside it (x -64..64,
// y -48..47 design units), in the antic's looks (a turn: dx +-4), in every frame.
static void test_every_pet_wags_its_own_tail() {
  const MascotLook looks[] = {
      {0, 0, 3, -2, Eyes::Open, Paws::Down, 0},
      {-4, 0, 3, 0, Eyes::Wide, Paws::ReachLeft, 0},
      {4, 0, 3, 0, Eyes::Wide, Paws::ReachRight, 0},
      {0, 0, 0, 0, Eyes::Dizzy, Paws::Down, screens::kStars},
  };
  FakeCanvas fc({96, 72});  // half 36: 0.75 px a unit, the reach exactly
  screens::bind(fc);
  for (uint8_t pet = 0; pet < kPetIds; pet++) {
    if (!isPet(pet)) continue;
    screens::MascotPaint p;
    p.pet = pet;
    screens::setMascotPaint(p);
    for (const auto& k : looks) {
      fc.clearLog();
      screens::deskMascot(48, 36, k, 36, false, false);
      const int still = fc.calls;
      for (uint8_t f = 1; f <= 32; f++) {
        fc.clearLog();
        screens::deskMascot(48, 36, k, 36, false, false, f);
        if (pet == 0) TEST_ASSERT_EQUAL_INT(still, fc.calls);
        else TEST_ASSERT_TRUE_MESSAGE(fc.calls > still, "a pet's tail antic draws its own tail");
      }
    }
    char msg[32];
    snprintf(msg, sizeof(msg), "pet %u: tail out of reach", pet);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fc.outOfBounds, msg);
  }
}

// Riff's guitar (kGuitar: its fanfare and its solo) is Riff's alone; other pets ignore the flag.
static void test_only_riff_plays_guitar() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  const MascotLook plain{0, 0, 0, 0, Eyes::Happy, Paws::Down, 0};
  MascotLook rock = plain;
  rock.extras = screens::kGuitar;
  for (uint8_t pet = 0; pet < kPetIds; pet++) {
    if (!isPet(pet)) continue;
    screens::setMascotPet(pet);
    fc.clearLog();
    screens::deskMascot(120, 120, plain, 48, false, true);
    const int without = fc.calls;
    fc.clearLog();
    screens::deskMascot(120, 120, rock, 48, false, true);
    if (pet == (uint8_t)Pet::Riff) TEST_ASSERT_TRUE(fc.calls > without);
    else TEST_ASSERT_EQUAL_INT(without, fc.calls);
  }
}

// Glasses take light rims where the eyes sit on something dark (the robot's screen); the
// penguin's hats sit higher (a negative hatDy), on top of its taller head.
static void test_glasses_and_hats_fit_the_pet() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  const MascotLook k{0, 0, 0, 0, Eyes::Open, Paws::Down, 0};
  screens::setMascotAccessory((uint8_t)Accessory::Glasses);
  screens::setMascotPet((uint8_t)Pet::Robot);
  screens::deskMascot(120, 120, k, 48, false, true);
  TEST_ASSERT_EQUAL(ui::color::MUTED, fc.colorAt(120 - 12 - 10, 120 + 4 - 6 + 5));  // a lens' side rim
  screens::setMascotAccessory((uint8_t)Accessory::SantaHat);
  screens::setMascotPet((uint8_t)Pet::Penguin);
  screens::deskMascot(120, 120, k, 48, false, true);
  TEST_ASSERT_EQUAL(ui::color::WHITE, fc.colorAt(120, 120 - 25));  // the brim, above the cat's
}

// The pet's colours: Auto slots (every one by default) come from the preset, or from a custom
// body; a slot the user sets is drawn as it is; an unknown pet is the cat.
static void test_pet_colours() {
  using screens::MascotPaint;
  TEST_ASSERT_EQUAL_HEX16(0xF282, screens::mascotColorsFor(0xF55110).skin);  // the orange preset's
  // A dark body: light lines on the face, outlines lighter than the body (they show on the BG).
  const screens::MascotColors dark = screens::mascotColorsFor(0x101014);
  TEST_ASSERT_TRUE(screens::luma565(dark.lid) > screens::luma565(dark.skin) + 80);
  TEST_ASSERT_TRUE(screens::luma565(dark.line) > screens::luma565(dark.skin));
  // A light one: dark lines, darker outlines.
  const screens::MascotColors light = screens::mascotColorsFor(0xFFFFFF);
  TEST_ASSERT_EQUAL_HEX16(ui::color::PUPIL, light.lid);
  TEST_ASSERT_TRUE(screens::luma565(light.line) + 60 < screens::luma565(light.skin));

  FakeCanvas fc({240, 240});
  screens::bind(fc);
  const MascotLook k{0, 0, 0, 0, Eyes::Open, Paws::Down, 0};
  const int cx = 120, cy = 120;  // half 48: one design unit per pixel
  screens::deskMascot(cx, cy, k, 48, false, true);
  TEST_ASSERT_EQUAL_HEX16(ui::color::SKIN, fc.colorAt(cx, cy - 10));        // the forehead
  TEST_ASSERT_EQUAL_HEX16(ui::color::EYE_GREEN, screens::petColors(screens::kPetCat).eye);
  MascotPaint p;
  p.slots[kSlotBody] = 0x9BD84E + 1;  // the eyes' own green
  screens::setMascotPaint(p);
  screens::deskMascot(cx, cy, k, 48, false, true);
  TEST_ASSERT_EQUAL_HEX16(ui::color::EYE_GREEN, fc.colorAt(cx, cy - 10));
  const screens::PetColors pc = screens::petColors(screens::kPetCat);
  TEST_ASSERT_TRUE(abs((int)screens::luma565(pc.eye) - (int)screens::luma565(pc.skin)) >= 48);  // Auto eyes keep apart
  TEST_ASSERT_EQUAL_HEX16(ui::color::PUPIL, pc.lid);  // a light body: dark lines
  p.slots[kSlotEye] = 0x9BD84E + 1;  // the user's pick is respected
  p.slots[kSlotNose] = 0x0000FF + 1;
  screens::setMascotPaint(p);
  TEST_ASSERT_EQUAL_HEX16(ui::color::EYE_GREEN, screens::petColors(screens::kPetCat).eye);
  screens::deskMascot(cx, cy, k, 48, false, true);
  TEST_ASSERT_EQUAL_HEX16(0x001F, fc.colorAt(cx, cy + 19));  // the nose
  TEST_ASSERT_EQUAL_HEX16(ui::color::EYE_GREEN, screens::mascotSkin());
  // Every slot changes the paint hash (the desk mascot redraws).
  uint32_t seen = screens::mascotPaintHash();
  for (uint8_t i = 0; i < kPetSlots; i++) {
    p.slots[i] = 0x123456 + i;
    screens::setMascotPaint(p);
    TEST_ASSERT_TRUE(screens::mascotPaintHash() != seen);
    seen = screens::mascotPaintHash();
  }
  p.eyeShape = 1;
  screens::setMascotPaint(p);
  TEST_ASSERT_TRUE(screens::mascotPaintHash() != seen);
  screens::setMascotPet(kPetIds);
  TEST_ASSERT_EQUAL_UINT8(0, screens::mascotPet());
  screens::setMascotPet(14);
  TEST_ASSERT_EQUAL_UINT8(14, screens::mascotPet());
  screens::setMascotPet(13);  // never a pet: the cat
  TEST_ASSERT_EQUAL_UINT8(0, screens::mascotPet());
  screens::setMascotPet(12);
  TEST_ASSERT_EQUAL_UINT8(12, screens::mascotPet());
  p.pet = 13;
  screens::setMascotPaint(p);
  TEST_ASSERT_EQUAL_UINT8(0, screens::mascotPet());
  p.slots[kSlotBody] = kPetColorMax + 1;  // never set by the config: Auto
  p.eyeShape = kEyeShapes;
  screens::setMascotPaint(p);
  TEST_ASSERT_EQUAL_UINT32(kPetAuto, screens::mascotPaint().slots[kSlotBody]);
  TEST_ASSERT_EQUAL_UINT8(0, screens::mascotPaint().eyeShape);
}

// Each piece is actually drawn where it belongs (240 grid, half 48: one design unit per pixel).
static void test_pieces_are_drawn() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  const MascotLook k{0, 0, 0, 0, Eyes::Open, Paws::Down, 0};
  const int cx = 120, cy = 120;
  screens::deskMascot(cx, cy, k, 48, false, true);
  TEST_ASSERT_NOT_EQUAL(ui::color::VIOLET, fc.colorAt(cx, cy + 31));
  TEST_ASSERT_NOT_EQUAL(ui::color::WRINKLE, fc.colorAt(cx - 14, cy + 17));
  screens::setMascotTie(true);
  screens::deskMascot(cx, cy, k, 48, false, true);
  TEST_ASSERT_EQUAL(ui::color::VIOLET, fc.colorAt(cx, cy + 31));  // the knot under the chin
  screens::setMascotTie(false);
  screens::setMascotAccessory((uint8_t)Accessory::Glasses);
  screens::deskMascot(cx, cy, k, 48, false, true);
  TEST_ASSERT_EQUAL(ui::color::PUPIL, fc.colorAt(cx, cy + 2));  // the bridge
  screens::setMascotAccessory((uint8_t)Accessory::BunnyEars);
  screens::deskMascot(cx, cy, k, 48, false, true);
  TEST_ASSERT_EQUAL(ui::color::WHITE, fc.colorAt(cx - 18, cy - 33));  // the left ear
  screens::setMascotAccessory(0);
  MascotLook focused = k;
  focused.extras = screens::kHeadphones;
  screens::deskMascot(cx, cy, focused, 48, false, true);
  TEST_ASSERT_EQUAL(ui::color::DIM, fc.colorAt(cx, cy - 22));  // the band over the head
  MascotLook tired = k;
  tired.extras = screens::kEyeBags;
  screens::deskMascot(cx, cy, tired, 48, false, true);
  TEST_ASSERT_EQUAL(ui::color::WRINKLE, fc.colorAt(cx - 14, cy + 17));
  // A tired day draws them on any look.
  screens::deskMascot(cx, cy, k, 48, false, true);
  TEST_ASSERT_NOT_EQUAL(ui::color::WRINKLE, fc.colorAt(cx - 14, cy + 17));
  screens::setCatMood((uint8_t)CatMood::Tired);
  screens::deskMascot(cx, cy, k, 48, false, true);
  TEST_ASSERT_EQUAL(ui::color::WRINKLE, fc.colorAt(cx - 14, cy + 17));
}

// The desk cat only redraws when its look hash changes: the tie and the mood must be in it.
static void test_tie_and_mood_redraw_the_cat() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  screens::reset();
  const MascotLook k{0, 0, 0, 0, Eyes::Open, Paws::Down, 0};
  screens::deskCat(screens::R_BODY, 120, 96, 48, k);
  fc.clearLog();
  screens::deskCat(screens::R_BODY, 120, 96, 48, k);
  TEST_ASSERT_EQUAL_INT(0, fc.calls);
  screens::setMascotTie(true);
  screens::deskCat(screens::R_BODY, 120, 96, 48, k);
  TEST_ASSERT_TRUE(fc.calls > 0);
  fc.clearLog();
  screens::setCatMood((uint8_t)CatMood::Tired);
  screens::deskCat(screens::R_BODY, 120, 96, 48, k);
  TEST_ASSERT_TRUE(fc.calls > 0);
}

// Friday the 13th: the stranger (a dark one of our own pet's kind) crosses the screen in
// kPasserbyMs, never outside it, composed in layers, and leaves our own look as it was.
static void test_black_cat_crosses_inside_the_screen() {
  for (uint8_t pet = 0; pet < kPetKinds; pet++) {
  for (const auto& spec : kSpecs) {
    FakeCanvas fc(spec);
    screens::bind(fc);
    screens::reset();
    screens::MascotPaint paint;
    paint.style = 1;
    paint.pet = pet;  // the stranger is a dark one of the same kind
    paint.slots[kSlotBody] = 0x336699 + 1;
    screens::setMascotPaint(paint);
    screens::setMascotAccessory((uint8_t)Accessory::PartyHat);
    screens::setMascotTie(true);
    screens::setCatMood((uint8_t)CatMood::Tired);
    Snapshot s;
    memset(&s, 0, sizeof(s));
    screens::Clock clk{};
    strcpy(clk.hhmm, "14:32");
    int first = -1, last = -1;
    for (uint32_t ms = 0; ms <= kPasserbyMs; ms += 40) {
      fc.clearLog();
      screens::passerby(Lang::En, s, clk, ms);
      if (fc.layerBegins == 0) continue;
      TEST_ASSERT_TRUE(fc.panelFills == 0);  // no clear straight on the panel: no blink
      if (first < 0) first = (int)ms;
      last = (int)ms;
    }
    char what[48];
    snprintf(what, sizeof(what), "out of bounds: pet %u, %dx%d", (unsigned)pet, spec.w, spec.h);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fc.outOfBounds, what);
    TEST_ASSERT_EQUAL_INT(0, first);
    TEST_ASSERT_TRUE(last >= (int)kPasserbyMs - 200);
    TEST_ASSERT_EQUAL_UINT8(1, screens::mascotStyle());
    TEST_ASSERT_EQUAL_UINT8(pet, screens::mascotPet());
    TEST_ASSERT_EQUAL_UINT32(0x336699 + 1, screens::mascotPaint().slots[kSlotBody]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Accessory::PartyHat, screens::mascotAccessory());
    TEST_ASSERT_TRUE(screens::mascotTie());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)CatMood::Tired, screens::catMood());
  }
  }
}

// A guest from another Miblo is drawn in exactly the colours it draws itself with: its preset,
// pet, eye shape and custom slots (through the wire's RGB565), the Auto ones derived from them as
// on its own screen; it wears our holiday hat.
static void test_guest_looks_as_on_its_own_miblo() {
  screens::MascotPaint own;
  own.style = 1;
  own.pet = (uint8_t)Pet::Dog;
  own.eyeShape = (uint8_t)EyeShape::Sleepy;
  own.slots[kSlotBody] = 0x123457 + 1;  // not a multiple of the RGB565 steps
  own.slots[kSlotAccent] = 0xFFFFFF + 1;
  screens::setMascotPaint(own);
  const screens::PetColors theirs = screens::petColors(screens::kPetDog);
  // On our screen: we are a grey cat in a party hat, today is Christmas.
  screens::MascotPaint mine;
  mine.style = 3;
  screens::setMascotPaint(mine);
  screens::setMascotAccessory((uint8_t)Accessory::PartyHat);
  screens::setGuestAccessory((uint8_t)Accessory::SantaHat);
  screens::dressGuest(own.style, own.pet, friendLook(own.slots, own.eyeShape, 3, 12, 21));
  const screens::PetColors here = screens::petColors(screens::kPetDog);
  TEST_ASSERT_EQUAL_MEMORY(&theirs, &here, sizeof(here));
  TEST_ASSERT_EQUAL_UINT8((uint8_t)Pet::Dog, screens::mascotPet());
  TEST_ASSERT_EQUAL_UINT8(1, screens::mascotStyle());
  TEST_ASSERT_EQUAL_UINT8((uint8_t)Accessory::SantaHat, screens::mascotAccessory());
  // A guest in its preset (an older firmware's, no look): every slot Auto.
  screens::dressGuest(2, (uint8_t)Pet::Cat, FriendLook());
  for (uint32_t v : screens::mascotPaint().slots) TEST_ASSERT_EQUAL_UINT32(kPetAuto, v);
  TEST_ASSERT_EQUAL_UINT8(0, screens::mascotPaint().eyeShape);
  screens::setGuestAccessory(0);
}

// Friday the 13th's stranger: our own kind of pet in the black preset (none of our colours, eye
// shape or accessories), readable on the dark background whatever the pet.
static void test_stranger_is_a_dark_pet_of_our_kind() {
  for (uint8_t pet = 0; pet < kPetKinds; pet++) {
    screens::MascotPaint own;
    own.style = 1;
    own.pet = pet;
    own.eyeShape = 1;
    own.slots[kSlotBody] = 0xFFFFFF + 1;
    const screens::MascotPaint st = screens::strangerPaint(own);
    TEST_ASSERT_EQUAL_UINT8(2, st.style);
    TEST_ASSERT_EQUAL_UINT8(pet, st.pet);
    TEST_ASSERT_EQUAL_UINT8(0, st.eyeShape);
    for (uint32_t v : st.slots) TEST_ASSERT_EQUAL_UINT32(kPetAuto, v);
    screens::setMascotPaint(st);
    TEST_ASSERT_TRUE(screens::luma565(screens::mascotSkin()) >= screens::luma565(ui::color::BG) + 40);
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_every_look_stays_in_its_box);
  RUN_TEST(test_every_pet_stays_in_its_box);
  RUN_TEST(test_pet_colours);
  RUN_TEST(test_every_pet_wags_its_own_tail);
  RUN_TEST(test_only_riff_plays_guitar);
  RUN_TEST(test_glasses_and_hats_fit_the_pet);
  RUN_TEST(test_pieces_are_drawn);
  RUN_TEST(test_tie_and_mood_redraw_the_cat);
  RUN_TEST(test_black_cat_crosses_inside_the_screen);
  RUN_TEST(test_guest_looks_as_on_its_own_miblo);
  RUN_TEST(test_stranger_is_a_dark_pet_of_our_kind);
  return UNITY_END();
}
