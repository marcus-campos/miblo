// Screens shared by all of daily life: the waiting mark that keeps "needs you" visible over
// every daily full screen.
#include <unity.h>

#include "../support/fake_canvas.h"
#include "miblo_i18n.h"
#include "ui_screens.h"

void setUp() {}
void tearDown() {}

static const ui::ScreenSpec kSpecs[] = {{240, 240}, {320, 240}, {480, 320}, {170, 320}};

// An amber band across the top with the waiting session's name (or "NEEDS YOU"), inside the
// screen in every resolution and language, even with a long wide name and several waiting.
static void test_waiting_mark_is_amber_and_on_screen() {
  for (const auto& sp : kSpecs) {
    FakeCanvas fc(sp);
    screens::bind(fc);
    for (int l = 0; l < (int)miblo::Lang::Count; l++) {
      screens::reset();
      screens::waitingMark((miblo::Lang)l, "WWWWWWWWWWWWWWWWWWWW", 3);
      screens::waitingMark((miblo::Lang)l, "", 1);
    }
    TEST_ASSERT_EQUAL_INT(0, fc.outOfBounds);
    TEST_ASSERT_TRUE(fc.calls > 0);
    TEST_ASSERT_EQUAL_HEX16(ui::color::AMBER, fc.colorAt(sp.w / 2, 2));
  }
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  screens::reset();
  screens::waitingMark(miblo::Lang::En, "checkout", 2);
  TEST_ASSERT_TRUE(fc.drew("checkout"));
  TEST_ASSERT_TRUE(fc.drew("+1"));  // two waiting: the name and one more
  FakeCanvas meeting({240, 240});
  screens::bind(meeting);
  screens::reset();
  screens::waitingMark(miblo::Lang::En, "", 1);  // meeting mode: no name
  TEST_ASSERT_TRUE(meeting.drew("NEEDS YOU"));
  TEST_ASSERT_FALSE(meeting.drew("checkout"));
}

// Through the guard canvas (as on the gadget) the mark is drawn once and then only when it
// changes, when the screen under it painted over its band, or after a screen switch: never every
// frame, never cleared first (no flicker).
static void test_waiting_mark_redraws_only_when_needed() {
  FakeCanvas fc({240, 240});
  screens::bind(screens::waitingGuard(fc));
  screens::reset();
  screens::waitingMark(miblo::Lang::En, "checkout", 1);
  int calls = fc.calls;
  TEST_ASSERT_TRUE(calls > 0);
  screens::waitingMark(miblo::Lang::En, "checkout", 1);
  TEST_ASSERT_EQUAL_INT(calls, fc.calls);  // nothing changed: nothing drawn
  screens::canvas().fillRect(0, 200, 240, 20, ui::color::BG);  // the screen drew below the band
  calls = fc.calls;
  screens::waitingMark(miblo::Lang::En, "checkout", 1);
  TEST_ASSERT_EQUAL_INT(calls, fc.calls);
  screens::canvas().text(120, 12, "14:32", ui::Font::Body, ui::color::TEXT, ui::Align::Center, 60);  // over it
  calls = fc.calls;
  screens::waitingMark(miblo::Lang::En, "checkout", 1);
  TEST_ASSERT_TRUE(fc.calls > calls);  // painted back on top
  calls = fc.calls;
  screens::waitingMark(miblo::Lang::En, "checkout", 2);  // one more waiting
  TEST_ASSERT_TRUE(fc.calls > calls);
  calls = fc.calls;
  screens::reset();  // a screen switch clears the panel
  screens::waitingMark(miblo::Lang::En, "checkout", 2);
  TEST_ASSERT_TRUE(fc.calls > calls + 1);
  screens::bind(fc);
}

// The overlays drawn after the mark every frame (the state frame lines, the meeting badge) reach
// the band too; they are part of the same frame, so they must not make the mark repaint forever.
static void test_overlays_after_the_mark_do_not_repaint_it() {
  FakeCanvas fc({240, 240});
  screens::bind(screens::waitingGuard(fc));
  screens::reset();
  for (int frame = 0; frame < 5; frame++) {
    const int before = fc.calls;
    screens::waitingMark(miblo::Lang::En, "checkout", 1);
    const bool painted = fc.calls > before;
    TEST_ASSERT_TRUE(frame == 0 ? painted : !painted);
    screens::canvas().fillRect(0, 2, 240, 2, ui::color::AMBER);  // a frame line across the top
    screens::waitingOverlaysDrawn();
  }
  // The screen itself drawing over the band still brings it back.
  screens::canvas().text(120, 12, "14:32", ui::Font::Body, ui::color::TEXT, ui::Align::Center, 60);
  const int before = fc.calls;
  screens::waitingMark(miblo::Lang::En, "checkout", 1);
  TEST_ASSERT_TRUE(fc.calls > before);
  screens::bind(fc);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_waiting_mark_is_amber_and_on_screen);
  RUN_TEST(test_waiting_mark_redraws_only_when_needed);
  RUN_TEST(test_overlays_after_the_mark_do_not_repaint_it);
  return UNITY_END();
}
