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
  TEST_ASSERT_TRUE(fc.drew("2 more quick"));  // may wrap onto two lines
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
  TEST_ASSERT_TRUE(fc.calls > 100);  // módulos do QR
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
  screens::code(Lang::En, S::CodeUpdate, "1234", 298);  // só a contagem muda
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
  TEST_ASSERT_TRUE(fc.drew(MIBLO_FW_VERSION));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_system_screens_fit_any_resolution);
  RUN_TEST(test_setup_and_welcome_content);
  RUN_TEST(test_regions_only_redraw_on_change);
  RUN_TEST(test_boot_shows_firmware_version);
  RUN_TEST(test_hard_reset_countdown_content);
  return UNITY_END();
}
