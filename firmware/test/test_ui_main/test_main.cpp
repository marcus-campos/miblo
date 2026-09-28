#include <string.h>
#include <unity.h>

#include "../support/fake_canvas.h"
#include "ui_screens.h"

using namespace miblo;
using ui::Align;
using ui::Font;

void setUp() {}
void tearDown() {}

static Snapshot snap;
static const uint32_t NOW = 1790616720;

static void session(const char* id, const char* name, SessionState st, const char* tool, const char* det,
                    uint32_t ago) {
  SessionRow& r = snap.sessions[snap.count++];
  memset(&r, 0, sizeof(r));
  strcpy(r.id, id);
  strcpy(r.name, name);
  r.st = st;
  strcpy(r.tool, tool);
  strcpy(r.det, det);
  r.since = NOW - ago;
  strcpy(r.model, "Opus");
  r.ctx = 71;
  r.tok = 412000;
}

static void attention() {
  memset(&snap, 0, sizeof(snap));
  snap.now = NOW;
  snap.hasUsage = true;
  snap.h5 = {true, 62, NOW + 7800};
  snap.d7 = {true, 38, NOW + 240000};
  snap.todayUsd = 3.5f;
  session("11111111", "api-server", SessionState::Perm, "Bash", "npm run migrate", 42);
  session("22222222", "infra", SessionState::Question, "", "", 10);
  session("33333333", "front-app", SessionState::Running, "Edit", "Header.tsx", 192);
  session("44444444", "docs", SessionState::Idle, "", "", 600);
}

// Nothing running or waiting: two finished sessions and an idle one.
static void idle() {
  memset(&snap, 0, sizeof(snap));
  snap.now = NOW;
  snap.hasUsage = true;
  snap.h5 = {true, 62, NOW + 7800};
  snap.d7 = {true, 38, NOW + 240000};
  snap.todayUsd = 3.5f;
  session("11111111", "api-server", SessionState::Done, "", "", 300);
  session("22222222", "infra", SessionState::Done, "", "", 120);
  session("44444444", "docs", SessionState::Idle, "", "", 600);
}

// Three sessions running (none waiting) + one finished, with 30% / 13% usage.
static void working() {
  memset(&snap, 0, sizeof(snap));
  snap.now = NOW;
  snap.hasUsage = true;
  snap.h5 = {true, 30, NOW + 7800};
  snap.d7 = {true, 13, NOW + 240000};
  snap.todayUsd = 3.5f;
  session("33333333", "front-app", SessionState::Running, "Edit", "Header.tsx", 192);
  session("55555555", "worker", SessionState::Running, "Bash", "npm test", 18);
  session("66666666", "search", SessionState::Running, "Grep", "TODO", 65);
  session("11111111", "api-server", SessionState::Done, "", "", 300);
}

static int indexOf(const FakeCanvas& fc, const char* needle) {
  for (size_t i = 0; i < fc.texts.size(); i++) {
    if (fc.texts[i].find(needle) != std::string::npos) return (int)i;
  }
  return -1;
}

static screens::Clock testClock() {
  screens::Clock c{};
  c.valid = true;
  strcpy(c.hhmm, "14:32");
  c.epoch = NOW;
  return c;
}

static void renderMain(FakeCanvas& fc) {
  screens::bind(fc);
  Pager pager(3, 5000);
  RunTracker runs;
  const screens::Clock clk = testClock();
  attention();
  screens::reset();
  screens::flash(Lang::En, AlertKind::Perm, "api-server", 0);
  screens::reset();
  screens::hero(Lang::PtBR, snap, 0, AlertKind::Perm, false, clk, runs);
  screens::reset();
  screens::hero(Lang::En, snap, 3, AlertKind::Done, false, clk, runs);
  screens::reset();
  screens::overview(Lang::PtBR, snap, pager, 0, clk, false);
  working();
  screens::reset();
  screens::overview(Lang::En, snap, pager, 0, clk, false);
  idle();
  screens::reset();
  screens::overview(Lang::En, snap, pager, 0, clk, false);
  attention();
  screens::reset();
  screens::limits(Lang::En, snap, clk);
  screens::reset();
  screens::sessions(Lang::En, snap, pager, 0, clk, false);
}

static void test_main_screens_fit_any_resolution() {
  const ui::ScreenSpec specs[] = {{240, 240}, {320, 240}, {480, 320}, {170, 320}};
  for (const auto& sp : specs) {
    FakeCanvas fc(sp);
    renderMain(fc);
    TEST_ASSERT_TRUE(fc.calls > 50);
    TEST_ASSERT_EQUAL_INT(0, fc.outOfBounds);
  }
}

static void test_overview_attention_content() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  attention();
  Pager pager(3, 5000);
  screens::reset();
  fc.clearLog();
  screens::overview(Lang::PtBR, snap, pager, 0, testClock(), false);
  TEST_ASSERT_TRUE(fc.drew("2 AGUARDANDO · api-server"));
  TEST_ASSERT_TRUE(fc.drew("62%"));  // compact strip, not the big limits
  TEST_ASSERT_TRUE(fc.drew("38%"));
  TEST_ASSERT_FALSE(fc.drew("Sessão 5h"));
  TEST_ASSERT_TRUE(fc.drew("permissão · Bash"));
  TEST_ASSERT_TRUE(fc.drew("0:42"));  // time waiting, on the card
  TEST_ASSERT_TRUE(fc.drew("Editando Header.tsx"));
  // second call with the same data: nothing is redrawn
  fc.clearLog();
  screens::overview(Lang::PtBR, snap, pager, 100, testClock(), false);
  TEST_ASSERT_EQUAL_INT(0, fc.calls);
}

static void test_discreet_mode_hides_details() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  attention();
  Pager pager(4, 5000);
  RunTracker runs;
  screens::reset();
  fc.clearLog();
  screens::hero(Lang::En, snap, 0, AlertKind::Perm, true, testClock(), runs);
  TEST_ASSERT_TRUE(fc.drew("Asked permission"));
  TEST_ASSERT_FALSE(fc.drew("npm run migrate"));
  screens::reset();
  fc.clearLog();
  screens::overview(Lang::En, snap, pager, 0, testClock(), true);
  TEST_ASSERT_FALSE(fc.drew("Header.tsx"));
  TEST_ASSERT_TRUE(fc.drew("Editing"));
}

static void test_limits_arc_and_cost_fallback() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  attention();
  screens::reset();
  fc.clearLog();
  screens::limits(Lang::En, snap, testClock());
  TEST_ASSERT_EQUAL_INT(2, (int)fc.arcs.size());
  TEST_ASSERT_EQUAL_INT(270, fc.arcs[0]);
  TEST_ASSERT_EQUAL_INT(167, fc.arcs[1]);  // 62% of 270°
  snap.hasUsage = false;
  screens::reset();
  fc.clearLog();
  screens::limits(Lang::PtBR, snap, testClock());
  TEST_ASSERT_TRUE(fc.drew("hoje $3.50"));
  TEST_ASSERT_TRUE(fc.drew("limites indisponíveis"));
}

// The plugin sends reset:null when unknown (parsed as 0): no "resets …" / "in …" line at all.
static void test_unknown_reset_hides_reset_line() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  Pager pager(3, 5000);
  idle();
  screens::reset();
  fc.clearLog();
  screens::overview(Lang::En, snap, pager, 0, testClock(), false);
  TEST_ASSERT_TRUE(fc.drew("resets"));  // known resets: both lines drawn
  snap.h5.reset = 0;
  snap.d7.reset = 0;
  screens::reset();
  fc.clearLog();
  screens::overview(Lang::En, snap, pager, 0, testClock(), false);
  TEST_ASSERT_TRUE(fc.drew("62%"));
  TEST_ASSERT_FALSE(fc.drew("resets"));
  TEST_ASSERT_FALSE(fc.drew("in "));
  attention();
  snap.h5.reset = 0;
  snap.d7.reset = 0;
  screens::reset();
  fc.clearLog();
  screens::limits(Lang::En, snap, testClock());
  TEST_ASSERT_TRUE(fc.drew("38%"));
  TEST_ASSERT_FALSE(fc.drew("in "));
  TEST_ASSERT_FALSE(fc.drew("38% ·"));
}

static void test_sessions_pages_and_flash_blinks() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  attention();
  session("55555555", "worker", SessionState::Running, "Bash", "npm test", 18);
  snap.more = 3;
  Pager pager(4, 5000);
  screens::reset();
  fc.clearLog();
  screens::sessions(Lang::En, snap, pager, 0, testClock(), false);
  TEST_ASSERT_TRUE(fc.drew("SESSIONS · 8"));
  TEST_ASSERT_TRUE(fc.drew("1/2"));
  TEST_ASSERT_TRUE(fc.drew("Opus · ctx 71% · 412k tok"));
  fc.clearLog();
  screens::sessions(Lang::En, snap, pager, 5000, testClock(), false);
  TEST_ASSERT_TRUE(fc.drew("2/2"));
  TEST_ASSERT_TRUE(fc.drew("worker"));

  screens::reset();
  fc.clearLog();
  screens::flash(Lang::En, AlertKind::Done, "docs", 0);
  int first = fc.calls;
  screens::flash(Lang::En, AlertKind::Done, "docs", 100);  // same phase: nothing changes
  TEST_ASSERT_EQUAL_INT(first, fc.calls);
  screens::flash(Lang::En, AlertKind::Done, "docs", screens::kFlashPhaseMs + 10);  // next phase: redraws
  TEST_ASSERT_TRUE(fc.calls > first);
}

static void test_overview_working_is_sessions_first() {
  const ui::ScreenSpec specs[] = {{240, 240}, {320, 240}, {480, 320}, {170, 320}};
  for (const auto& sp : specs) {
    FakeCanvas fc(sp);
    screens::bind(fc);
    working();
    Pager pager(3, 5000);
    screens::reset();
    fc.clearLog();
    screens::overview(Lang::En, snap, pager, 0, testClock(), false);
    TEST_ASSERT_EQUAL_INT(0, fc.outOfBounds);
    // three session cards, each with its activity and time in state
    TEST_ASSERT_TRUE(fc.drew("front-app"));
    TEST_ASSERT_TRUE(fc.drew("Editing Header.tsx"));
    TEST_ASSERT_TRUE(fc.drew("worker"));
    TEST_ASSERT_TRUE(fc.drew("Bash · npm test"));
    TEST_ASSERT_TRUE(fc.drew("search"));
    TEST_ASSERT_TRUE(fc.drew("Searching TODO"));
    TEST_ASSERT_TRUE(fc.drew("3:12"));
    // compact limits strip instead of the big limits
    TEST_ASSERT_TRUE(fc.drew("5h"));
    TEST_ASSERT_TRUE(fc.drew("30%"));
    TEST_ASSERT_TRUE(fc.drew("7d"));
    TEST_ASSERT_TRUE(fc.drew("13%"));
    TEST_ASSERT_FALSE(fc.drew("5h session"));
    TEST_ASSERT_FALSE(fc.drew("resets"));
    // footer: count + page + clock
    TEST_ASSERT_TRUE(fc.drew("3 RUNNING · 1/2"));
    TEST_ASSERT_TRUE(fc.drew("14:32"));
  }
  // same data again: nothing is redrawn
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  working();
  Pager pager(3, 5000);
  screens::reset();
  screens::overview(Lang::En, snap, pager, 0, testClock(), false);
  fc.clearLog();
  screens::overview(Lang::En, snap, pager, 100, testClock(), false);
  TEST_ASSERT_EQUAL_INT(0, fc.calls);
  // one second later only the time regions change, never the cards' text
  screens::Clock later = testClock();
  later.epoch += 1;
  screens::overview(Lang::En, snap, pager, 200, later, false);
  TEST_ASSERT_TRUE(fc.drew("3:13"));
  TEST_ASSERT_FALSE(fc.drew("front-app"));
  // discreet mode: the verb stays, the detail goes
  screens::reset();
  fc.clearLog();
  screens::overview(Lang::En, snap, pager, 300, testClock(), true);
  TEST_ASSERT_TRUE(fc.drew("Editing"));
  TEST_ASSERT_FALSE(fc.drew("Header.tsx"));
  TEST_ASSERT_FALSE(fc.drew("npm test"));
}

static void test_overview_working_pages_and_cost_fallback() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  working();
  session("77777777", "late", SessionState::Idle, "", "", 900);
  snap.more = 2;
  Pager pager(3, 5000);
  screens::reset();
  fc.clearLog();
  screens::overview(Lang::En, snap, pager, 0, testClock(), false);
  TEST_ASSERT_TRUE(fc.drew("3 RUNNING · 1/2 · +2"));
  TEST_ASSERT_FALSE(fc.drew("api-server"));
  fc.clearLog();
  screens::overview(Lang::En, snap, pager, 5000, testClock(), false);
  TEST_ASSERT_TRUE(fc.drew("2/2"));
  TEST_ASSERT_TRUE(fc.drew("api-server"));
  TEST_ASSERT_TRUE(fc.drew("finished"));
  TEST_ASSERT_TRUE(fc.drew("late"));
  TEST_ASSERT_FALSE(fc.drew("front-app"));
  // no usage data (API account): today's cost replaces the strip
  working();
  snap.hasUsage = false;
  screens::reset();
  fc.clearLog();
  screens::overview(Lang::En, snap, pager, 10000, testClock(), false);
  TEST_ASSERT_TRUE(fc.drew("today $3.50"));
  TEST_ASSERT_FALSE(fc.drew("5h"));
}

static void test_overview_pending_first() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  attention();
  Pager pager(3, 5000);
  screens::reset();
  fc.clearLog();
  screens::overview(Lang::En, snap, pager, 0, testClock(), false);
  const int perm = indexOf(fc, "permission · Bash");
  const int question = indexOf(fc, "question");
  const int running = indexOf(fc, "Editing Header.tsx");
  TEST_ASSERT_TRUE(perm >= 0 && question > perm && running > question);
  TEST_ASSERT_TRUE(fc.drew("62%"));
  TEST_ASSERT_FALSE(fc.drew("5h session"));
  TEST_ASSERT_EQUAL_INT(ui::color::CARD_AMBER, fc.bgOf("permission · Bash"));
  TEST_ASSERT_EQUAL_INT(ui::color::CARD, fc.bgOf("Editing Header.tsx"));
  TEST_ASSERT_EQUAL_INT(ui::color::AMBER, fc.bgOf("2 WAITING"));
}

static void test_overview_idle_keeps_big_limits() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  idle();
  Pager pager(3, 5000);
  screens::reset();
  fc.clearLog();
  screens::overview(Lang::En, snap, pager, 0, testClock(), false);
  TEST_ASSERT_TRUE(fc.drew("ALL DONE"));
  TEST_ASSERT_TRUE(fc.drew("5h session"));
  TEST_ASSERT_TRUE(fc.drew("62%"));
  TEST_ASSERT_TRUE(fc.drew("resets"));
  TEST_ASSERT_TRUE(fc.drew("infra finished 2m ago"));
  TEST_ASSERT_TRUE(fc.drew("today $3.50"));
  // switching layout (idle -> working) clears the screen and redraws everything
  working();
  fc.clearLog();
  screens::overview(Lang::En, snap, pager, 100, testClock(), false);
  TEST_ASSERT_TRUE(fc.drew("front-app"));
  TEST_ASSERT_TRUE(fc.drew("30%"));
}

// Text never gets a box of its own: it sits on the colour its region was cleared with.
static void test_text_background_is_region_background() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  Pager pager(3, 5000);
  void (*fixtures[])() = {working, attention, idle};
  for (auto fx : fixtures) {
    fx();
    screens::reset();
    fc.clearLog();
    screens::overview(Lang::En, snap, pager, 0, testClock(), false);
    TEST_ASSERT_TRUE(fc.texts.size() > 3);
    for (size_t i = 0; i < fc.textBgs.size(); i++) {
      TEST_ASSERT_NOT_EQUAL(-1, fc.textBgs[i]);
      TEST_ASSERT_NOT_EQUAL(ui::color::BLACK, fc.textBgs[i]);
    }
  }
  working();
  screens::reset();
  fc.clearLog();
  screens::overview(Lang::En, snap, pager, 0, testClock(), false);
  TEST_ASSERT_EQUAL_INT(ui::color::BG, fc.bgOf("30%"));
  TEST_ASSERT_EQUAL_INT(ui::color::BG, fc.bgOf("13%"));
  TEST_ASSERT_EQUAL_INT(ui::color::CARD, fc.bgOf("3:12"));  // time region cleared with the card colour
  idle();
  screens::reset();
  fc.clearLog();
  screens::overview(Lang::En, snap, pager, 0, testClock(), false);
  TEST_ASSERT_EQUAL_INT(ui::color::BG, fc.bgOf("62%"));  // NumL
  TEST_ASSERT_EQUAL_INT(ui::color::BG, fc.bgOf("38%"));  // NumM
}

// Alert flash: every phase repaints the whole screen first, and the name sits on that phase's colour.
static void test_flash_text_on_phase_background() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  const AlertKind kinds[] = {AlertKind::Perm, AlertKind::Done};
  for (AlertKind k : kinds) {
    screens::reset();
    uint32_t ms = 0;
    for (int phase = 0; phase < 4; phase++, ms += screens::kFlashPhaseMs) {
      fc.clearLog();
      screens::flash(Lang::En, k, "api-server", ms);
      TEST_ASSERT_EQUAL_INT(1, (int)fc.texts.size());
      const bool on = (phase % 2) == 0;
      const uint16_t want = on ? (k == AlertKind::Done ? ui::color::FLASH_BLUE : ui::color::AMBER) : ui::color::BG;
      TEST_ASSERT_EQUAL_INT(want, fc.textBgs[0]);
      TEST_ASSERT_EQUAL_INT(want, fc.colorAt(0, 0));        // full-screen repaint
      TEST_ASSERT_EQUAL_INT(want, fc.colorAt(239, 239));
    }
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_main_screens_fit_any_resolution);
  RUN_TEST(test_overview_attention_content);
  RUN_TEST(test_discreet_mode_hides_details);
  RUN_TEST(test_limits_arc_and_cost_fallback);
  RUN_TEST(test_sessions_pages_and_flash_blinks);
  RUN_TEST(test_unknown_reset_hides_reset_line);
  RUN_TEST(test_overview_working_is_sessions_first);
  RUN_TEST(test_overview_working_pages_and_cost_fallback);
  RUN_TEST(test_overview_pending_first);
  RUN_TEST(test_overview_idle_keeps_big_limits);
  RUN_TEST(test_text_background_is_region_background);
  RUN_TEST(test_flash_text_on_phase_background);
  return UNITY_END();
}
