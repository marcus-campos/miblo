// The mascot's daily-life look: special-day accessories, the meeting tie, focus headphones, tired
// eye bags, and Friday the 13th's black cat.
#include <string.h>
#include <unity.h>

#include "../support/fake_canvas.h"
#include "miblo_mood.h"
#include "miblo_occasions.h"
#include "ui_internal.h"
#include "ui_screens.h"

using namespace miblo;
using screens::Eyes;
using screens::MascotLook;
using screens::Paws;

void setUp() {
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

// Friday the 13th: the black cat crosses the screen in kPasserbyMs, never outside it, composed in
// layers, and leaves our own look as it was.
static void test_black_cat_crosses_inside_the_screen() {
  for (const auto& spec : kSpecs) {
    FakeCanvas fc(spec);
    screens::bind(fc);
    screens::reset();
    screens::setMascotStyle(1);
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
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fc.outOfBounds, "out of bounds");
    TEST_ASSERT_EQUAL_INT(0, first);
    TEST_ASSERT_TRUE(last >= (int)kPasserbyMs - 200);
    TEST_ASSERT_EQUAL_UINT8(1, screens::mascotStyle());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Accessory::PartyHat, screens::mascotAccessory());
    TEST_ASSERT_TRUE(screens::mascotTie());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)CatMood::Tired, screens::catMood());
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_every_look_stays_in_its_box);
  RUN_TEST(test_pieces_are_drawn);
  RUN_TEST(test_tie_and_mood_redraw_the_cat);
  RUN_TEST(test_black_cat_crosses_inside_the_screen);
  return UNITY_END();
}
