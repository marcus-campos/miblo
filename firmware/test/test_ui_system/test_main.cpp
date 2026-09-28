#include <math.h>
#include <unity.h>

#include "../support/fake_canvas.h"
#include "miblo_version.h"
#include "ui_screens.h"

using miblo::Lang;
using miblo::S;

void setUp() {}
void tearDown() {}

static void renderSystem(FakeCanvas& fc) {
  screens::bind(fc);
  screens::reset();
  screens::boot(Lang::En, 1);
  screens::reset();
  screens::setup(Lang::Ru, "Miblo-Setup-4F2A", true);
  screens::reset();
  screens::welcome(Lang::PtBR, "4827", "192.168.0.42");
  screens::reset();
  screens::paired(Lang::Zh, "MacBook-Marcus", "Overview", "miblo-4f2a");
  screens::reset();
  screens::code(Lang::De, S::CodeUpdate, "1234", 299);
  screens::reset();
  screens::updating(Lang::Fr, 42);
  screens::reset();
  screens::disconnected(Lang::It, true, 14, 32, 1, 28, "192.168.0.42", "miblo-4f2a", "4827");
  for (int l = 0; l < (int)Lang::Count; l++) {
    screens::reset();
    screens::hardResetCountdown((Lang)l, 3);
  }
}

// Addendum A: quick-boot hard reset countdown.
static void test_hard_reset_countdown_content() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  screens::reset();
  fc.clearLog();
  screens::hardResetCountdown(Lang::En, 2);
  TEST_ASSERT_TRUE(fc.drew("2"));
  TEST_ASSERT_TRUE(fc.drew("reset: 2"));  // may wrap onto two lines
  TEST_ASSERT_TRUE(fc.drew("Leave it on to cancel"));
  TEST_ASSERT_EQUAL_INT(0, fc.outOfBounds);
  fc.clearLog();
  screens::hardResetCountdown(Lang::En, 2);  // unchanged: nothing redrawn
  TEST_ASSERT_EQUAL_INT(0, fc.calls);
  screens::hardResetCountdown(Lang::En, 1);
  TEST_ASSERT_TRUE(fc.drew("1"));
}

static void test_system_screens_fit_any_resolution() {
  const ui::ScreenSpec specs[] = {{240, 240}, {320, 240}, {480, 320}, {170, 320}};
  for (const auto& sp : specs) {
    FakeCanvas fc(sp);
    renderSystem(fc);
    TEST_ASSERT_TRUE(fc.calls > 20);
    TEST_ASSERT_EQUAL_INT(0, fc.outOfBounds);
  }
}

static void test_setup_and_welcome_content() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  screens::reset();
  fc.clearLog();
  screens::setup(Lang::PtBR, "Miblo-Setup-4F2A", false);
  TEST_ASSERT_TRUE(fc.drew("Olá!"));
  TEST_ASSERT_TRUE(fc.drew("Miblo-Setup-4F2A"));
  TEST_ASSERT_TRUE(fc.calls > 100);  // QR modules
  screens::reset();
  fc.clearLog();
  screens::setup(Lang::PtBR, "Miblo-Setup-4F2A", true);
  TEST_ASSERT_TRUE(fc.drew("Senha incorreta"));
  screens::reset();
  fc.clearLog();
  screens::welcome(Lang::En, "4827", "192.168.0.42");
  // Addendum D: a QR to the repository (both install commands live in its README).
  TEST_ASSERT_TRUE(fc.drew("github.com/marcus-campos/miblo"));
  TEST_ASSERT_FALSE(fc.drew("/plugin install miblo@miblo"));
  TEST_ASSERT_TRUE(fc.calls > 100);  // QR modules
  TEST_ASSERT_TRUE(fc.drew("4827"));
  TEST_ASSERT_TRUE(fc.drew("192.168.0.42"));
}

static void test_regions_only_redraw_on_change() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  screens::reset();
  screens::code(Lang::En, S::CodeUpdate, "1234", 299);
  fc.clearLog();
  screens::code(Lang::En, S::CodeUpdate, "1234", 299);
  TEST_ASSERT_EQUAL_INT(0, fc.calls);
  screens::code(Lang::En, S::CodeUpdate, "1234", 298);  // only the countdown changes
  TEST_ASSERT_TRUE(fc.drew("expires in 4:58"));
  TEST_ASSERT_FALSE(fc.drew("1234"));
}

// Controller-requested (human): boot() must draw the firmware version near the bottom.
static void test_boot_shows_firmware_version() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  screens::reset();
  fc.clearLog();
  screens::boot(Lang::En, 1);
  TEST_ASSERT_TRUE(fc.drew("v" MIBLO_FW_VERSION " (" MIBLO_BUILD ")"));
}

// Records every primitive's box and counts the ones escaping a given bounding box.
class BoxCanvas : public FakeCanvas {
 public:
  using FakeCanvas::FakeCanvas;
  int bx = 0, by = 0, bw = 0, bh = 0, escaped = 0, prims = 0;
  void fillRect(int x, int y, int w, int h, uint16_t c) override {
    check(x, y, w, h);
    FakeCanvas::fillRect(x, y, w, h, c);
  }
  void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) override {
    check(x, y, w, h);
    FakeCanvas::fillRoundRect(x, y, w, h, r, c);
  }
  void drawRect(int x, int y, int w, int h, uint16_t c) override {
    check(x, y, w, h);
    FakeCanvas::drawRect(x, y, w, h, c);
  }
  void fillCircle(int cx, int cy, int r, uint16_t c) override {
    check(cx - r, cy - r, 2 * r + 1, 2 * r + 1);
    FakeCanvas::fillCircle(cx, cy, r, c);
  }
  void wideLine(int x0, int y0, int x1, int y1, int w, uint16_t c, uint16_t bg) override {
    const int hw = (w + 1) / 2;
    check((x0 < x1 ? x0 : x1) - hw, (y0 < y1 ? y0 : y1) - hw, abs(x1 - x0) + 2 * hw, abs(y1 - y0) + 2 * hw);
    FakeCanvas::wideLine(x0, y0, x1, y1, w, c, bg);
  }
  void arc(int cx, int cy, int r, int ir, int a0, int a1, uint16_t fg, uint16_t bg) override {
    // Only the drawn span counts (TFT_eSPI angles: 0 = 6 o'clock, clockwise; a0 > a1 wraps).
    const int span = ((a1 - a0) % 360 + 360) % 360;
    int x0 = cx, y0 = cy, x1 = cx, y1 = cy;
    for (int i = 0; i <= (span ? span : 360); i++) {
      const double t = (a0 + i) * 3.14159265358979 / 180.0;
      const int px = (int)lround(cx - r * sin(t)), py = (int)lround(cy + r * cos(t));
      if (i == 0 || px < x0) x0 = px;
      if (i == 0 || py < y0) y0 = py;
      if (i == 0 || px > x1) x1 = px;
      if (i == 0 || py > y1) y1 = py;
    }
    check(x0 - 1, y0 - 1, x1 - x0 + 2, y1 - y0 + 2);
    FakeCanvas::arc(cx, cy, r, ir, a0, a1, fg, bg);
  }

 private:
  void check(int x, int y, int w, int h) {
    prims++;
    if (w <= 0 || h <= 0 || x < bx || y < by || x + w > bx + bw || y + h > by + bh) escaped++;
  }
};

// The Sphynx mascot must stay inside its box (Sz(96) square, or Sz(48) for the small variant)
// on every frame and tested resolution, with a small primitive budget (redrawn on pose changes).
static void test_mascot_stays_in_its_box() {
  const ui::ScreenSpec specs[] = {{240, 240}, {320, 240}, {480, 320}, {170, 320}};
  for (const auto& sp : specs) {
    for (int small = 0; small < 2; small++) {
      BoxCanvas bc(sp);
      screens::bind(bc);
      const int cx = screens::X(120), cy = screens::Y(100), half = screens::Sz(small ? 24 : 48);
      bc.bx = cx - half;
      bc.by = cy - half;
      bc.bw = bc.bh = 2 * half;
      for (int f = 0; f < 256; f++) {
        bc.prims = 0;
        screens::mascot(cx, cy, (uint8_t)f, small != 0);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, bc.escaped, "mascot escaped its bounding box");
        TEST_ASSERT_TRUE(bc.prims > 0 && bc.prims <= (small ? 16 : 22));
      }
      TEST_ASSERT_EQUAL_INT(0, bc.outOfBounds);
    }
  }
}

// It animates (idle, blink, ear twitch, glance); boot() redraws the mascot only on pose changes.
static void test_mascot_animates_and_boot_redraws_only_on_pose_change() {
  bool seen[4] = {false, false, false, false};
  for (int f = 0; f < 256; f++) seen[screens::mascotPose((uint8_t)f)] = true;
  TEST_ASSERT_TRUE(seen[0] && seen[1] && seen[2] && seen[3]);

  FakeCanvas fc({240, 240});
  screens::bind(fc);
  screens::reset();
  screens::boot(Lang::En, 0);
  fc.clearLog();
  screens::boot(Lang::En, 1);  // same idle pose: nothing to redraw
  TEST_ASSERT_EQUAL_INT(0, fc.calls);
  screens::boot(Lang::En, 2);  // blink
  TEST_ASSERT_TRUE(fc.calls > 0);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_system_screens_fit_any_resolution);
  RUN_TEST(test_setup_and_welcome_content);
  RUN_TEST(test_regions_only_redraw_on_change);
  RUN_TEST(test_boot_shows_firmware_version);
  RUN_TEST(test_mascot_stays_in_its_box);
  RUN_TEST(test_mascot_animates_and_boot_redraws_only_on_pose_change);
  RUN_TEST(test_hard_reset_countdown_content);
  return UNITY_END();
}
