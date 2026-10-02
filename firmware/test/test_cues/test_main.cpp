#include <stdlib.h>
#include <unity.h>

#include "miblo_config.h"
#include "miblo_cues.h"
#include "miblo_snapshot.h"
#include "../support/fake_canvas.h"
#include "ui_screens.h"

void setUp() {}
void tearDown() {}

static void test_cue_pulses() {
  miblo::StrongCue c;
  TEST_ASSERT_EQUAL(miblo::CueKind::None, c.active(0));
  const uint32_t t0 = 0xFFFFFFFFu - 1000;
  c.fire(miblo::CueKind::Timer, t0);
  TEST_ASSERT_EQUAL(miblo::CueKind::Timer, c.active(t0 + 3 * miblo::kCuePulseMs - 1));
  TEST_ASSERT_EQUAL(miblo::CueKind::None, c.active(t0 + 3 * miblo::kCuePulseMs));
  c.fire(miblo::CueKind::BreakEnd, 0);
  TEST_ASSERT_EQUAL(miblo::CueKind::None, c.active(miblo::kCuePulseMs));  // a single soft pulse
  // smooth: 0 at the start and end of a pulse, top in the middle, never jumps by more than 40
  TEST_ASSERT_EQUAL_UINT8(0, miblo::StrongCue::intensity(0, 3));
  TEST_ASSERT_TRUE(miblo::StrongCue::intensity(miblo::kCuePulseMs / 2, 3) > 200);
  uint8_t prev = 0;
  for (uint32_t t = 0; t < 3 * miblo::kCuePulseMs; t += 50) {
    const uint8_t v = miblo::StrongCue::intensity(t, 3);
    TEST_ASSERT_TRUE(abs((int)v - (int)prev) <= 40);
    prev = v;
  }
  TEST_ASSERT_EQUAL_UINT8(0, miblo::StrongCue::intensity(3 * miblo::kCuePulseMs, 3));
}

// Each kind's pulse count, the gaps between pulses, and a cue that replaces another.
static void test_cue_kinds_and_refire() {
  TEST_ASSERT_EQUAL_UINT8(0, miblo::cuePulses(miblo::CueKind::None));
  TEST_ASSERT_EQUAL_UINT8(1, miblo::cuePulses(miblo::CueKind::BreakEnd));
  TEST_ASSERT_EQUAL_UINT8(3, miblo::cuePulses(miblo::CueKind::FocusEnd));
  TEST_ASSERT_EQUAL_UINT8(3, miblo::cuePulses(miblo::CueKind::Timer));
  TEST_ASSERT_EQUAL_UINT8(3, miblo::cuePulses(miblo::CueKind::Alarm));
  TEST_ASSERT_EQUAL_UINT8(3, miblo::cuePulses(miblo::CueKind::Reminder));
  // Each pulse falls back to 0 before the next one rises.
  TEST_ASSERT_EQUAL_UINT8(0, miblo::StrongCue::intensity(miblo::kCuePulseMs, 3));
  TEST_ASSERT_TRUE(miblo::StrongCue::intensity(miblo::kCuePulseMs + miblo::kCuePulseMs / 2, 3) > 200);
  TEST_ASSERT_EQUAL_UINT8(0, miblo::StrongCue::intensity(miblo::kCuePulseMs, 1));  // one pulse only
  TEST_ASSERT_EQUAL_UINT8(0, miblo::StrongCue::intensity(miblo::kCuePulseMs / 2, 0));

  miblo::StrongCue c;
  c.fire(miblo::CueKind::Reminder, 1000);
  TEST_ASSERT_EQUAL(miblo::CueKind::Reminder, c.active(1000));
  TEST_ASSERT_EQUAL_UINT32(700, c.elapsed(1700));
  c.fire(miblo::CueKind::Alarm, 2000);  // a newer cue starts over
  TEST_ASSERT_EQUAL(miblo::CueKind::Alarm, c.active(2000 + 3 * miblo::kCuePulseMs - 1));
  TEST_ASSERT_EQUAL_UINT32(0, c.elapsed(2000));
  c.fire(miblo::CueKind::None, 9000);  // None clears it
  TEST_ASSERT_EQUAL(miblo::CueKind::None, c.active(9000));
  // A loop that stalled for much longer than the cue does not bring it back.
  c.fire(miblo::CueKind::Timer, 0);
  TEST_ASSERT_EQUAL(miblo::CueKind::None, c.active(0x80000000u));
  // Once over, it stays over: millis() wrapping back near the start (2^32 + 100) brings no ghost.
  c.fire(miblo::CueKind::Timer, 0);
  TEST_ASSERT_EQUAL(miblo::CueKind::None, c.active(3 * miblo::kCuePulseMs));
  TEST_ASSERT_EQUAL(miblo::CueKind::None, c.active(100));
}

static void test_cue_brightness_respects_night_mode() {
  miblo::Config c;
  c.brightness = 60;
  TEST_ASSERT_EQUAL_UINT8(100, miblo::cueBrightness(c, 12 * 60));
  c.night = true;
  c.nightBrightness = 10;
  TEST_ASSERT_EQUAL_UINT8(20, miblo::cueBrightness(c, 23 * 60));
  c.nightBrightness = 70;  // twice the night value, at most 100
  c.brightness = 100;
  TEST_ASSERT_EQUAL_UINT8(100, miblo::cueBrightness(c, 23 * 60));
  TEST_ASSERT_EQUAL_UINT8(100, miblo::cueBrightness(c, -1));  // time unknown: no night
}

static void test_frame_colour() {
  miblo::Snapshot s{};
  s.count = 1;
  s.sessions[0].st = miblo::SessionState::Done;
  s.sessions[0].since = 1000;
  TEST_ASSERT_EQUAL(miblo::FrameColor::Green, miblo::frameColorFor(s, 1059));
  TEST_ASSERT_EQUAL(miblo::FrameColor::None, miblo::frameColorFor(s, 1060));
  s.count = 2;
  s.sessions[1].st = miblo::SessionState::Question;
  TEST_ASSERT_EQUAL(miblo::FrameColor::Amber, miblo::frameColorFor(s, 1010));
}

// Permission waits too; nothing to show without sessions or while the time is unknown.
static void test_frame_colour_edges() {
  miblo::Snapshot s{};
  TEST_ASSERT_EQUAL(miblo::FrameColor::None, miblo::frameColorFor(s, 5000));
  s.count = 1;
  s.sessions[0].st = miblo::SessionState::Perm;
  s.sessions[0].since = 1000;
  TEST_ASSERT_EQUAL(miblo::FrameColor::Amber, miblo::frameColorFor(s, 0));  // waiting needs no clock
  s.sessions[0].st = miblo::SessionState::Done;
  TEST_ASSERT_EQUAL(miblo::FrameColor::None, miblo::frameColorFor(s, 0));     // time unknown
  TEST_ASSERT_EQUAL(miblo::FrameColor::Green, miblo::frameColorFor(s, 990));  // computer clock ahead
  s.sessions[0].st = miblo::SessionState::Running;
  TEST_ASSERT_EQUAL(miblo::FrameColor::None, miblo::frameColorFor(s, 1010));
  s.sessions[0].st = miblo::SessionState::Idle;
  TEST_ASSERT_EQUAL(miblo::FrameColor::None, miblo::frameColorFor(s, 1010));
}

// Counts the cue's full-screen redraws (a fill across the top edge, the whole width) and the
// fills straight on the panel over the icon (what would make it blink).
class CueCanvas : public FakeCanvas {
 public:
  using FakeCanvas::FakeCanvas;
  void fillRect(int x, int y, int w, int h, uint16_t c) override {
    if (x <= 0 && y <= 0 && x + w >= spec_.w) redraws++;
    const int cx = spec_.w / 2, cy = spec_.h / 2;
    if (!inLayer && x <= cx && cx < x + w && y <= cy && cy < y + h) iconFills++;
    FakeCanvas::fillRect(x, y, w, h, c);
  }
  int redraws = 0;
  int iconFills = 0;
};

static const ui::ScreenSpec kSpecs[] = {{240, 240}, {320, 240}, {480, 320}, {170, 320}};
static const miblo::CueKind kKinds[] = {miblo::CueKind::FocusEnd, miblo::CueKind::BreakEnd, miblo::CueKind::Timer,
                                        miblo::CueKind::Alarm, miblo::CueKind::Reminder};

static void test_cue_fits_and_redraws_by_step() {
  for (const ui::ScreenSpec& sp : kSpecs) {
    for (miblo::CueKind k : kKinds) {
      CueCanvas fc(sp);
      screens::bind(fc);
      screens::reset();
      fc.redraws = fc.iconFills = 0;
      // Every 20 ms for 4.5 s: only a change of step (8 per rise or fall) repaints the screen.
      for (uint32_t t = 0; t <= 4500; t += 20) screens::cue(k, t);
      TEST_ASSERT_EQUAL_INT(0, fc.outOfBounds);
      TEST_ASSERT_TRUE(fc.redraws <= 3 * 2 * 8);
      TEST_ASSERT_TRUE(fc.redraws >= miblo::cuePulses(k) * 2 * 6);  // it really pulses
      TEST_ASSERT_EQUAL_INT(0, fc.iconFills);  // the icon is composed off-screen: no blink
      // At the top of a pulse the screen is mostly the cue's colour.
      screens::reset();
      screens::cue(k, miblo::kCuePulseMs / 2);
      const uint16_t want = k == miblo::CueKind::Timer ? ui::color::AMBER
                            : (k == miblo::CueKind::FocusEnd || k == miblo::CueKind::BreakEnd) ? ui::color::GREEN
                                                                                                 : ui::color::VIOLET;
      TEST_ASSERT_EQUAL_INT(want, fc.colorAt(sp.w / 2, 2));
      fc.clearLog();
      screens::cue(k, miblo::kCuePulseMs / 2 + 10);  // same step: nothing drawn
      TEST_ASSERT_EQUAL_INT(0, fc.calls);
    }
  }
  // Without memory for a layer it still draws, inside the screen.
  CueCanvas fc({240, 240});
  fc.layerSupported = false;
  screens::bind(fc);
  screens::reset();
  screens::cue(miblo::CueKind::Timer, 300);
  TEST_ASSERT_TRUE(fc.calls > 4);
  TEST_ASSERT_EQUAL_INT(0, fc.outOfBounds);
  screens::reset();
  fc.clearLog();
  screens::cue(miblo::CueKind::None, 300);
  TEST_ASSERT_EQUAL_INT(0, fc.calls);
}

static void test_state_frame() {
  for (const ui::ScreenSpec& sp : kSpecs) {
    FakeCanvas fc(sp);
    screens::bind(fc);
    screens::reset();
    fc.clearLog();
    screens::stateFrame(miblo::FrameColor::None);
    TEST_ASSERT_EQUAL_INT(0, fc.calls);
    screens::stateFrame(miblo::FrameColor::Amber);
    TEST_ASSERT_EQUAL_INT(0, fc.outOfBounds);
    TEST_ASSERT_EQUAL_INT(ui::color::AMBER, fc.colorAt(2, sp.h / 2));            // left
    TEST_ASSERT_EQUAL_INT(ui::color::AMBER, fc.colorAt(sp.w - 3, sp.h / 2));     // right
    TEST_ASSERT_EQUAL_INT(ui::color::AMBER, fc.colorAt(sp.w / 2, 3));            // top
    TEST_ASSERT_EQUAL_INT(ui::color::AMBER, fc.colorAt(sp.w / 2, sp.h - 4));     // bottom
    TEST_ASSERT_EQUAL_INT(ui::color::BG, fc.colorAt(sp.w / 2, sp.h / 2));        // the screen is left alone
    TEST_ASSERT_EQUAL_INT(ui::color::BG, fc.colorAt(1, sp.h / 2));               // 2 px in from the edge
    TEST_ASSERT_EQUAL_INT(ui::color::BG, fc.colorAt(4, sp.h / 2));               // 2 px wide
    screens::stateFrame(miblo::FrameColor::Green);
    TEST_ASSERT_EQUAL_INT(ui::color::GREEN, fc.colorAt(sp.w / 2, 2));
    TEST_ASSERT_EQUAL_INT(0, fc.outOfBounds);
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_cue_pulses);
  RUN_TEST(test_cue_kinds_and_refire);
  RUN_TEST(test_cue_brightness_respects_night_mode);
  RUN_TEST(test_frame_colour);
  RUN_TEST(test_frame_colour_edges);
  RUN_TEST(test_cue_fits_and_redraws_by_step);
  RUN_TEST(test_state_frame);
  return UNITY_END();
}
