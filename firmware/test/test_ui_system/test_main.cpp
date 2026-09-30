#include <stdio.h>
#include <math.h>
#include <unity.h>

#include <string>

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
  screens::setup(Lang::Ru, "Miblo-Setup-4F2A", screens::SetupNote::WrongPassword);
  screens::reset();
  screens::welcome(Lang::PtBR, "4827", "192.168.0.42");
  screens::reset();
  screens::paired(Lang::Zh, "MacBook-Marcus", "Overview", "miblo-4f2a");
  screens::reset();
  screens::code(Lang::De, S::CodeUpdate, "1234", 299);
  screens::reset();
  screens::updating(Lang::Fr, 42);
  screens::reset();
  for (int l = 0; l < (int)Lang::Count; l++) {
    screens::reset();
    screens::hardResetCountdown((Lang)l, 3);
    for (auto note : {screens::SetupNote::NotFound, screens::SetupNote::Refused, screens::SetupNote::Failed}) {
      screens::reset();
      screens::setup((Lang)l, "Miblo-Setup-4F2A", note, 204);
    }
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
  screens::setup(Lang::PtBR, "Miblo-Setup-4F2A");
  TEST_ASSERT_TRUE(fc.drew("Olá!"));
  TEST_ASSERT_TRUE(fc.drew("Miblo-Setup-4F2A"));
  TEST_ASSERT_TRUE(fc.calls > 100);  // QR modules
  screens::reset();
  fc.clearLog();
  screens::setup(Lang::PtBR, "Miblo-Setup-4F2A", screens::SetupNote::WrongPassword);
  TEST_ASSERT_TRUE(fc.drew("Senha incorreta"));
  screens::reset();
  fc.clearLog();
  screens::setup(Lang::En, "Miblo-Setup-4F2A", screens::SetupNote::NotFound);
  TEST_ASSERT_TRUE(fc.drew("Network not found"));
  TEST_ASSERT_TRUE(fc.drew("Use a 2.4 GHz network"));
  TEST_ASSERT_TRUE(fc.drew("Miblo-Setup-4F2A"));  // the QR / setup network stay available
  TEST_ASSERT_TRUE(fc.calls > 100);
  screens::reset();
  fc.clearLog();
  screens::setup(Lang::En, "Miblo-Setup-4F2A", screens::SetupNote::Refused);
  TEST_ASSERT_TRUE(fc.drew("Connection refused"));
  TEST_ASSERT_TRUE(fc.drew("Check password or use WPA2"));
  screens::reset();
  fc.clearLog();
  screens::setup(Lang::En, "Miblo-Setup-4F2A", screens::SetupNote::Failed, 200);
  TEST_ASSERT_TRUE(fc.drew("Could not connect"));
  TEST_ASSERT_TRUE(fc.drew("Error code 200"));
  screens::reset();
  fc.clearLog();
  screens::setup(Lang::En, "Miblo-Setup-4F2A", screens::SetupNote::Failed, 0);  // timeout, no reason
  TEST_ASSERT_TRUE(fc.drew("Could not connect"));
  TEST_ASSERT_TRUE(fc.drew("Scan with your phone"));
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
  void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) override {
    const int lx = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
    const int hx = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
    const int ly = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
    const int hy = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
    check(lx, ly, hx - lx + 1, hy - ly + 1);
    FakeCanvas::fillTriangle(x0, y0, x1, y1, x2, y2, c);
  }
  void wideLine(int x0, int y0, int x1, int y1, int w, uint16_t c, uint16_t bg) override {
    lines++;
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
    arcs_++;
    FakeCanvas::arc(cx, cy, r, ir, a0, a1, fg, bg);
  }
  int lines = 0, arcs_ = 0;  // anti-aliased primitives (not allowed in the mascot)

 private:
  void check(int x, int y, int w, int h) {
    prims++;
    if (w <= 0 || h <= 0 || x < bx || y < by || x + w > bx + bw || y + h > by + bh) escaped++;
  }
};

// The simplified Sphynx mascot must stay inside its box (2 * Sz(48) square, or 2 * Sz(24) for the
// small variant) on every frame and tested resolution, with at most ~10 flat primitives (no
// anti-aliased lines or arcs, which a 16-colour off-screen layer can't blend).
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
        TEST_ASSERT_TRUE(bc.prims > 0 && bc.prims <= (small ? 9 : 11));
      }
      TEST_ASSERT_EQUAL_INT(0, bc.lines + bc.arcs_);
      TEST_ASSERT_EQUAL_INT(0, bc.outOfBounds);
    }
  }
}

// The Desk mascot (with paws and extras) stays inside its 2 * Sz(64) box for every expression
// its choreographies use, with flat primitives only.
static void test_desk_mascot_stays_in_its_box() {
  const ui::ScreenSpec specs[] = {{240, 240}, {320, 240}, {480, 320}, {170, 320}};
  for (const auto& sp : specs) {
    BoxCanvas bc(sp);
    screens::bind(bc);
    const int cx = screens::X(120), cy = screens::Y(98), half = screens::Sz(64);
    bc.bx = cx - half;
    bc.by = cy - half;
    bc.bw = bc.bh = 2 * half;
    for (int mood = 0; mood <= (int)screens::DeskMood::Celebrate; mood++) {
      for (int left = 0; left < 2; left++) {
        for (uint32_t ms = 0; ms < 60000; ms += 50) {
          screens::deskMascot(cx, cy, screens::deskLook((screens::DeskMood)mood, left != 0, ms));
          TEST_ASSERT_EQUAL_INT_MESSAGE(0, bc.escaped, "desk mascot escaped its bounding box");
        }
      }
    }
    TEST_ASSERT_EQUAL_INT(0, bc.lines + bc.arcs_);
    TEST_ASSERT_EQUAL_INT(0, bc.outOfBounds);
  }
}

// Records the primitive sequence (kind + geometry) of one mascot frame, to compare poses.
class TraceCanvas : public FakeCanvas {
 public:
  using FakeCanvas::FakeCanvas;
  std::string trace;
  void fillRect(int x, int y, int w, int h, uint16_t c) override { add('r', x, y, w, h, c); }
  void fillRoundRect(int x, int y, int w, int h, int, uint16_t c) override { add('R', x, y, w, h, c); }
  void fillCircle(int cx, int cy, int r, uint16_t c) override { add('c', cx, cy, r, 0, c); }
  void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) override {
    add('t', x0, y0, x1 + x2, y1 + y2, c);
  }

 private:
  void add(char k, int a, int b, int c, int d, uint16_t col) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%c%d,%d,%d,%d,%u;", k, a, b, c, d, (unsigned)col);
    trace += buf;
  }
};

static std::string traceOf(uint8_t frame) {
  TraceCanvas tc({240, 240});
  screens::bind(tc);
  screens::mascot(120, 100, frame);
  return tc.trace;
}

// Every mascot colour style draws the same shapes (only the colours change), and an unknown
// style falls back to the first one.
static std::string shapesOf(const std::string& trace) {  // the trace without each colour field
  std::string out;
  size_t start = 0;
  while (start < trace.size()) {
    const size_t semi = trace.find(';', start);
    const std::string prim = trace.substr(start, semi - start);
    out += prim.substr(0, prim.rfind(',')) + ";";
    start = semi + 1;
  }
  return out;
}

static void test_mascot_styles_change_only_colours() {
  screens::setMascotStyle(0);
  const std::string first = traceOf(0);
  for (int style = 1; style < 4; style++) {
    screens::setMascotStyle((uint8_t)style);
    const std::string t = traceOf(0);
    TEST_ASSERT_TRUE(t != first);
    TEST_ASSERT_TRUE(shapesOf(t) == shapesOf(first));
  }
  screens::setMascotStyle(200);
  TEST_ASSERT_TRUE(traceOf(0) == first);
  screens::setMascotStyle(0);
}

// It animates (idle, blink, hop, glance), each pose drawing differently; frames with the same
// pose draw the same thing.
static void test_mascot_poses_differ() {
  bool seen[4] = {false, false, false, false};
  uint8_t first[4] = {0, 0, 0, 0};
  for (int f = 0; f < 256; f++) {
    const uint8_t p = screens::mascotPose((uint8_t)f);
    TEST_ASSERT_TRUE(p < 4);
    if (!seen[p]) first[p] = (uint8_t)f;
    seen[p] = true;
  }
  TEST_ASSERT_TRUE(seen[0] && seen[1] && seen[2] && seen[3]);
  for (int a = 0; a < 4; a++) {
    for (int b = a + 1; b < 4; b++) TEST_ASSERT_TRUE(traceOf(first[a]) != traceOf(first[b]));
  }
  for (int f = 0; f < 32; f++) {
    TEST_ASSERT_TRUE(traceOf((uint8_t)f) == traceOf(first[screens::mascotPose((uint8_t)f)]));
  }
}

// boot() composes the mascot on the canvas's off-screen layer (one begin/end pair around all
// of its primitives, covering the mascot box) and only on pose changes; reset() frees it.
static void test_boot_draws_mascot_on_a_layer_only_on_pose_change() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  screens::reset();
  fc.clearLog();
  screens::boot(Lang::En, 0);
  TEST_ASSERT_EQUAL_INT(1, fc.layerBegins);
  TEST_ASSERT_EQUAL_INT(1, fc.layerEnds);
  TEST_ASSERT_FALSE(fc.inLayer);
  TEST_ASSERT_EQUAL_INT(120 - 48, fc.lastLayer[0]);
  TEST_ASSERT_EQUAL_INT(100 - 48, fc.lastLayer[1]);
  TEST_ASSERT_EQUAL_INT(96, fc.lastLayer[2]);
  TEST_ASSERT_EQUAL_INT(96, fc.lastLayer[3]);
  TEST_ASSERT_TRUE(fc.layerCalls > 0 && fc.layerCalls <= 11);
  TEST_ASSERT_EQUAL_INT(0, fc.outOfBounds);

  fc.clearLog();
  screens::boot(Lang::En, 1);  // same idle pose: nothing to redraw, no layer
  TEST_ASSERT_EQUAL_INT(0, fc.calls);
  TEST_ASSERT_EQUAL_INT(0, fc.layerBegins);
  screens::boot(Lang::En, 2);  // blink: only the mascot, all of it on the layer
  TEST_ASSERT_EQUAL_INT(1, fc.layerBegins);
  TEST_ASSERT_EQUAL_INT(1, fc.layerEnds);
  TEST_ASSERT_EQUAL_INT(fc.calls, fc.layerCalls);

  fc.clearLog();
  screens::reset();  // leaving the boot screen frees the layer
  TEST_ASSERT_EQUAL_INT(1, fc.layerReleases);

  // No layer available (allocation failed): the mascot is still drawn, directly.
  fc.layerSupported = false;
  fc.clearLog();
  screens::boot(Lang::En, 2);
  TEST_ASSERT_EQUAL_INT(1, fc.layerBegins);
  TEST_ASSERT_EQUAL_INT(0, fc.layerEnds);
  TEST_ASSERT_EQUAL_INT(0, fc.layerCalls);
  TEST_ASSERT_TRUE(fc.calls > 3);
}

// ShiftCanvas moves every primitive by the shift (layers included) but clears the whole panel.
static void test_shift_canvas() {
  TraceCanvas inner({240, 240});
  ui::ShiftCanvas sc(inner);
  sc.setShift(2, -2);
  sc.fillRect(10, 10, 5, 5, 1);
  sc.fillCircle(50, 60, 3, 2);
  TEST_ASSERT_EQUAL_STRING("r12,8,5,5,1;c52,58,3,0,2;", inner.trace.c_str());
  FakeCanvas fc({240, 240});
  ui::ShiftCanvas sf(fc);
  sf.setShift(-2, 2);
  screens::bind(sf);
  screens::reset();  // the screen switch clears the real panel, from (0, 0)
  TEST_ASSERT_EQUAL_INT(0, fc.outOfBounds);
  fc.clearLog();
  sf.beginLayer(20, 30, 40, 50);
  TEST_ASSERT_EQUAL_INT(18, fc.lastLayer[0]);
  TEST_ASSERT_EQUAL_INT(32, fc.lastLayer[1]);
  sf.endLayer();
  TEST_ASSERT_EQUAL_INT(0, sf.textWidth("abc", ui::Font::Small) - fc.textWidth("abc", ui::Font::Small));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_system_screens_fit_any_resolution);
  RUN_TEST(test_setup_and_welcome_content);
  RUN_TEST(test_regions_only_redraw_on_change);
  RUN_TEST(test_boot_shows_firmware_version);
  RUN_TEST(test_mascot_stays_in_its_box);
  RUN_TEST(test_mascot_poses_differ);
  RUN_TEST(test_desk_mascot_stays_in_its_box);
  RUN_TEST(test_mascot_styles_change_only_colours);
  RUN_TEST(test_shift_canvas);
  RUN_TEST(test_boot_draws_mascot_on_a_layer_only_on_pose_change);
  RUN_TEST(test_hard_reset_countdown_content);
  return UNITY_END();
}
