#include <string.h>
#include <unity.h>

#include "../support/fake_canvas.h"
#include <time.h>

#include "miblo_config.h"
#include "miblo_mood.h"
#include "miblo_zone.h"
#include "ui_screens.h"
#include "ui_visit_kit.h"

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
  screens::reset();
  screens::desk(Lang::En, snap, clk, 0);
  screens::reset();
  screens::disconnected(Lang::En, clk, "192.168.0.42", "miblo-4f2a", "4827", 0, 0);
  screens::reset();
  screens::limitReset(Lang::En, snap, clk, 0);
  screens::reset();
  screens::summary(Lang::En, snap, clk);
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

// A long session name never gets cut because of the "N WAITING" word beside the clock: the
// header falls back to a smaller font, then to "2 · name"; the name stays whole.
static void test_attention_header_keeps_the_name() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  attention();
  strlcpy(snap.sessions[0].name, "checkout-service", sizeof(snap.sessions[0].name));
  strlcpy(snap.sessions[1].name, "billing-service", sizeof(snap.sessions[1].name));  // both waiting
  Pager pager(3, 5000);
  for (uint8_t l = 0; l < (uint8_t)Lang::Count; l++) {
    screens::reset();
    fc.clearLog();
    screens::overview((Lang)l, snap, pager, 0, testClock(), false);
    bool whole = false;  // the header line ends with the full name and wasn't cut
    for (size_t i = 0; i < fc.texts.size(); i++) {
      const std::string& s = fc.texts[i];
      const bool named = s.size() > 16 && (s.compare(s.size() - 16, 16, "checkout-service") == 0 ||
                                           s.compare(s.size() - 15, 15, "billing-service") == 0);
      if (named && s.find("\xC2\xB7") != std::string::npos && !fc.cut[i]) whole = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(whole, miblo::langCode((Lang)l));
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
  TEST_ASSERT_TRUE(fc.drew("<1m"));  // time waiting, on the card (minute granularity)
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
  Pager pager(3, 5000);
  screens::reset();
  fc.clearLog();
  screens::sessions(Lang::En, snap, pager, 0, testClock(), false);
  TEST_ASSERT_TRUE(fc.drew("SESSIONS · 8"));
  TEST_ASSERT_TRUE(fc.drew("1/2"));
  TEST_ASSERT_TRUE(fc.drew("api-server"));
  TEST_ASSERT_TRUE(fc.drew("infra"));
  TEST_ASSERT_TRUE(fc.drew("front-app"));
  TEST_ASSERT_FALSE(fc.drew("docs"));  // 3 per page
  TEST_ASSERT_FALSE(fc.drew("ctx"));   // no model · ctx · tokens line in this mode
  TEST_ASSERT_FALSE(fc.drew("412k"));
  fc.clearLog();
  screens::sessions(Lang::En, snap, pager, 5000, testClock(), false);
  TEST_ASSERT_TRUE(fc.drew("2/2"));
  TEST_ASSERT_TRUE(fc.drew("docs"));
  TEST_ASSERT_TRUE(fc.drew("worker"));
  TEST_ASSERT_FALSE(fc.drew("api-server"));

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
    TEST_ASSERT_TRUE(fc.drew("3m"));
    // compact limits strip instead of the big limits
    TEST_ASSERT_TRUE(fc.drew("5h"));
    TEST_ASSERT_TRUE(fc.drew("30%"));
    TEST_ASSERT_TRUE(fc.drew("7d"));
    TEST_ASSERT_TRUE(fc.drew("13%"));
    TEST_ASSERT_FALSE(fc.drew("5h session"));
    TEST_ASSERT_FALSE(fc.drew("resets"));
    // brand row (logo + name + clock) and footer (count + page)
    TEST_ASSERT_TRUE(fc.drew("miblo"));
    TEST_ASSERT_TRUE(fc.drew("14:32"));
    TEST_ASSERT_TRUE(fc.drew("3 RUNNING · 1/2"));
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
  // one second later nothing changes on screen (times have minute granularity)
  screens::Clock later = testClock();
  later.epoch += 1;
  screens::overview(Lang::En, snap, pager, 200, later, false);
  TEST_ASSERT_EQUAL_INT(0, fc.calls);
  // a minute later only the times change, in place, never the cards' text
  later.epoch += 60;
  screens::overview(Lang::En, snap, pager, 300, later, false);
  TEST_ASSERT_TRUE(fc.drew("4m"));
  TEST_ASSERT_FALSE(fc.drew("front-app"));
  TEST_ASSERT_EQUAL_INT(0, fc.panelFills);
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
  TEST_ASSERT_EQUAL_INT(ui::color::CARD, fc.bgOf("3m"));  // time box painted with the card colour
  idle();
  screens::reset();
  fc.clearLog();
  screens::overview(Lang::En, snap, pager, 0, testClock(), false);
  TEST_ASSERT_EQUAL_INT(ui::color::BG, fc.bgOf("62%"));  // NumL
  TEST_ASSERT_EQUAL_INT(ui::color::BG, fc.bgOf("38%"));  // NumM
}

// Sessions mode (S1): at most 3 big cards per page on every resolution — name in BodyBold,
// activity in Small, time in state; no model/ctx/tokens line; pending cards stay amber.
static void test_sessions_mode_is_legible() {
  const ui::ScreenSpec specs[] = {{240, 240}, {320, 240}, {480, 320}, {170, 320}};
  for (const auto& sp : specs) {
    FakeCanvas fc(sp);
    screens::bind(fc);
    attention();
    session("55555555", "worker", SessionState::Running, "Bash", "npm test", 18);
    Pager pager(3, 5000);
    screens::reset();
    fc.clearLog();
    screens::sessions(Lang::En, snap, pager, 0, testClock(), false);
    TEST_ASSERT_EQUAL_INT(0, fc.outOfBounds);
    TEST_ASSERT_TRUE(fc.drew("1/2"));
    TEST_ASSERT_EQUAL_INT((int)ui::Font::BodyBold, (int)fc.fontOf("api-server"));
    TEST_ASSERT_EQUAL_INT((int)ui::Font::Small, (int)fc.fontOf("permission · Bash"));
    TEST_ASSERT_EQUAL_INT((int)ui::Font::Small, (int)fc.fontOf("Editing Header.tsx"));
    TEST_ASSERT_TRUE(fc.drew("<1m"));
    TEST_ASSERT_TRUE(fc.drew("3m"));
    TEST_ASSERT_FALSE(fc.drew("docs"));
    TEST_ASSERT_FALSE(fc.drew("Opus"));
    TEST_ASSERT_EQUAL_INT(ui::color::CARD_AMBER, fc.bgOf("permission · Bash"));
    TEST_ASSERT_EQUAL_INT(ui::color::CARD, fc.bgOf("Editing Header.tsx"));
    // page 2: the other two sessions, the third slot empty
    fc.clearLog();
    screens::sessions(Lang::En, snap, pager, 5000, testClock(), false);
    TEST_ASSERT_EQUAL_INT(0, fc.outOfBounds);
    TEST_ASSERT_TRUE(fc.drew("2/2"));
    TEST_ASSERT_TRUE(fc.drew("docs"));
    TEST_ASSERT_TRUE(fc.drew("worker"));
    TEST_ASSERT_FALSE(fc.drew("front-app"));
  }
  // no sessions, then sessions again: every row comes back
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  Pager pager(3, 5000);
  attention();
  const uint8_t n = snap.count;
  snap.count = 0;
  screens::reset();
  screens::sessions(Lang::En, snap, pager, 0, testClock(), false);
  TEST_ASSERT_TRUE(fc.drew("No active sessions"));
  snap.count = n;
  fc.clearLog();
  screens::sessions(Lang::En, snap, pager, 100, testClock(), false);
  TEST_ASSERT_TRUE(fc.drew("api-server"));
  TEST_ASSERT_TRUE(fc.drew("infra"));
  TEST_ASSERT_TRUE(fc.drew("front-app"));
  TEST_ASSERT_TRUE(fc.drew("<1m"));
}

// Ticks never clear anything: an unchanged snapshot one second later draws nothing; a 10 s
// heartbeat (new seq/now) redraws at most the values that changed, in place (textBox), with no
// clear and no session name redrawn; a minute later only the time boxes change.
static void renderScreen(int which, Pager& pager, uint32_t ms, const screens::Clock& clk) {
  switch (which) {
    case 0: working(); break;
    case 1: attention(); break;
    case 2: idle(); break;
    default: attention(); break;
  }
  snap.h5.reset += 30;  // countdown "2h10" not on a minute boundary at the start
  snap.seq = (uint32_t)(clk.epoch - NOW);  // volatile fields follow the clock
  snap.now = clk.epoch;
  RunTracker runs;
  switch (which) {
    case 3: screens::limits(Lang::En, snap, clk); break;
    case 4: screens::sessions(Lang::En, snap, pager, ms, clk, false); break;
    case 5: screens::hero(Lang::En, snap, 0, AlertKind::Perm, false, clk, runs); break;
    default: screens::overview(Lang::En, snap, pager, ms, clk, false); break;
  }
}

static void test_ticks_update_in_place() {
  const bool layers[] = {true, false};
  for (bool withLayer : layers) {
    for (int which = 0; which < 6; which++) {
      FakeCanvas fc({240, 240});
      fc.layerSupported = withLayer;
      screens::bind(fc);
      Pager pager(3, 5000);  // no page flip within this test
      screens::Clock clk = testClock();
      screens::reset();
      renderScreen(which, pager, 0, clk);
      TEST_ASSERT_TRUE(fc.texts.size() > 2);
      fc.clearLog();
      clk.epoch += 1;
      renderScreen(which, pager, 100, clk);
      TEST_ASSERT_EQUAL_INT(0, fc.calls);  // +1 s: nothing at all
      clk.epoch += 10;
      renderScreen(which, pager, 200, clk);  // heartbeat
      TEST_ASSERT_EQUAL_INT(0, fc.panelFills);
      TEST_ASSERT_EQUAL_INT((int)fc.texts.size(), fc.boxTexts);
      TEST_ASSERT_FALSE(fc.drew("api-server"));
      TEST_ASSERT_FALSE(fc.drew("front-app"));
      fc.clearLog();
      clk.epoch += 60;
      renderScreen(which, pager, 300, clk);  // a minute later: timers move, in place
      TEST_ASSERT_EQUAL_INT(0, fc.panelFills);
      TEST_ASSERT_EQUAL_INT((int)fc.texts.size(), fc.boxTexts);
      TEST_ASSERT_FALSE(fc.drew("front-app"));
      if (which == 0) TEST_ASSERT_TRUE(fc.drew("4m"));
      if (which == 5) TEST_ASSERT_TRUE(fc.drew("waiting 1m"));
    }
  }
}

// Paging recomposes whole cards off-screen when the canvas has a layer, one push per card,
// and frees the layer right after; without a layer it still draws everything directly.
static void test_paging_uses_layer() {
  const bool layers[] = {true, false};
  for (bool withLayer : layers) {
    FakeCanvas fc({240, 240});
    fc.layerSupported = withLayer;
    screens::bind(fc);
    attention();
    session("55555555", "worker", SessionState::Running, "Bash", "npm test", 18);
    Pager pager(3, 5000);
    screens::reset();
    screens::sessions(Lang::En, snap, pager, 0, testClock(), false);
    fc.clearLog();
    screens::sessions(Lang::En, snap, pager, 5000, testClock(), false);
    TEST_ASSERT_TRUE(fc.drew("docs"));
    TEST_ASSERT_TRUE(fc.drew("worker"));
    if (withLayer) {
      TEST_ASSERT_EQUAL_INT(3, fc.layerEnds);  // the three cards
      TEST_ASSERT_EQUAL_INT(3, fc.layerReleases);
      TEST_ASSERT_EQUAL_INT(0, fc.panelFills);
      TEST_ASSERT_FALSE(fc.inLayer);
    } else {
      TEST_ASSERT_EQUAL_INT(0, fc.layerEnds);
      TEST_ASSERT_TRUE(fc.panelFills > 0);
    }
  }
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

// English words that must never reach a pt-BR screen (whole words, ASCII-case-insensitive).
// Legit Latin text on the screens: tool names (Bash), file names, "Miblo", "Wi-Fi", "Opus",
// "ctx"/"tok" (Claude Code jargon), the time units "m"/"h"/"d" and the URL.
// ("no" is not in the list: it is Portuguese too, "no ritmo atual".)
static const char* const kEnglishOnly[] = {
    "running", "sessions", "session", "waiting", "wait", "agent", "agents", "task", "tasks", "finished",
    "idle", "week", "limits", "limit", "today", "done", "needs", "you", "all", "asked", "permission",
    "question", "resets", "reset", "in", "editing", "reading", "searching", "fetching", "working",
    "cost", "unavailable", "took", "ago", "connecting", "connected", "disconnected", "updating",
    "unplug", "pairing", "paired", "code", "expires", "scan", "phone", "join", "network", "wrong",
    "password", "for", "the", "computer", "overview", "left", "restarts", "leave", "cancel", "error",
    "could", "not", "found", "refused", "and", "on", "of", "to",
};

static std::string lower(const std::string& w) {
  std::string o = w;
  for (auto& c : o) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
  return o;
}

// First denylisted word in `text`, or "". Words are runs of ASCII letters/digits; a run touching
// a non-ASCII byte is part of a localized word ("SESSÕES" → "SESS", skipped).
static std::string englishWord(const std::string& text) {
  size_t i = 0;
  while (i < text.size()) {
    auto alnum = [&](size_t k) {
      const char c = text[k];
      return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    };
    if (!alnum(i)) {
      i++;
      continue;
    }
    size_t j = i;
    while (j < text.size() && alnum(j)) j++;
    const bool glued = (i > 0 && (unsigned char)text[i - 1] >= 0x80) ||
                       (j < text.size() && (unsigned char)text[j] >= 0x80);
    const std::string w = lower(text.substr(i, j - i));
    if (!glued) {
      for (const char* bad : kEnglishOnly) {
        if (w == bad) return text.substr(i, j - i);
      }
    }
    i = j;
  }
  return "";
}

// Every screen, in pt-BR, with states that exercise every string (background waits from both
// plugin generations, "+N more", no usage data, pages, alerts): no English word may be drawn.
static void test_pt_br_screens_have_no_english() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  const Lang L = Lang::PtBR;
  const screens::Clock clk = testClock();
  RunTracker runs;
  auto check = [&](const char* what) {
    for (const auto& t : fc.texts) {
      const std::string w = englishWord(t);
      if (!w.empty()) {
        char msg[256];
        snprintf(msg, sizeof(msg), "%s: English \"%s\" in \"%s\"", what, w.c_str(), t.c_str());
        TEST_FAIL_MESSAGE(msg);
      }
    }
    fc.clearLog();
  };
  // sanity: the checker catches the bug this test guards against
  TEST_ASSERT_EQUAL_STRING("waiting", englishWord("Agente waiting 5 tasks").c_str());
  TEST_ASSERT_EQUAL_STRING("", englishWord("SESSÕES · 3").c_str());

  auto withWaits = [&]() {
    working();
    session("77777777", "bg-new", SessionState::Running, "_wait_tasks", "5", 30);
    session("88888888", "bg-old", SessionState::Running, "Agent", "waiting 2 agents", 40);
    snap.more = 3;
    // background waits on the first page
    SessionRow tmp = snap.sessions[0];
    snap.sessions[0] = snap.sessions[4];
    snap.sessions[4] = tmp;
    tmp = snap.sessions[1];
    snap.sessions[1] = snap.sessions[5];
    snap.sessions[5] = tmp;
  };
  for (int page = 0; page < 3; page++) {
    Pager pager(3, 5000);
    withWaits();
    screens::reset();
    screens::overview(L, snap, pager, (uint32_t)page * 5000, clk, false);
    if (page == 0) {
      TEST_ASSERT_TRUE(fc.drew("Aguardando 5 tarefas"));
      TEST_ASSERT_TRUE(fc.drew("Aguardando 2 agentes"));
    }
    check("overview working");
    screens::reset();
    screens::sessions(L, snap, pager, (uint32_t)page * 5000, clk, false);
    check("sessions");
    screens::reset();
    screens::sessions(L, snap, pager, (uint32_t)page * 5000, clk, true);
    check("sessions discreet");
    attention();
    snap.more = 2;
    screens::reset();
    screens::overview(L, snap, pager, (uint32_t)page * 5000, clk, false);
    check("overview attention");
  }
  Pager pager(3, 5000);
  idle();
  screens::reset();
  screens::overview(L, snap, pager, 0, clk, false);
  check("overview idle");
  screens::reset();
  screens::limits(L, snap, clk);
  check("limits");
  for (float usd : {0.0f, 2.5f}) {
    idle();
    snap.hasUsage = false;
    snap.todayUsd = usd;
    screens::reset();
    screens::overview(L, snap, pager, 0, clk, false);
    check("overview idle, no usage");
    screens::reset();
    screens::limits(L, snap, clk);
    check("limits, no usage");
    withWaits();
    snap.hasUsage = false;
    snap.todayUsd = usd;
    screens::reset();
    screens::overview(L, snap, pager, 0, clk, false);
    check("overview working, no usage");
  }
  memset(&snap, 0, sizeof(snap));
  screens::reset();
  screens::sessions(L, snap, pager, 0, clk, false);
  check("sessions empty");

  attention();
  for (AlertKind k : {AlertKind::Perm, AlertKind::Question, AlertKind::Done}) {
    screens::reset();
    screens::flash(L, k, "api-server", 0);
    check("flash");
  }
  screens::reset();
  screens::hero(L, snap, 0, AlertKind::Perm, false, clk, runs);
  check("hero perm");
  screens::reset();
  screens::hero(L, snap, 1, AlertKind::Question, false, clk, runs);
  check("hero question");
  runs.observe(snap);
  idle();
  runs.observe(snap);
  screens::reset();
  screens::hero(L, snap, 0, AlertKind::Done, false, clk, runs);
  check("hero done");

  screens::reset();
  screens::boot(L, 1);
  check("boot");
  for (auto note : {screens::SetupNote::None, screens::SetupNote::WrongPassword, screens::SetupNote::NotFound,
                    screens::SetupNote::Refused, screens::SetupNote::Failed}) {
    screens::reset();
    screens::setup(L, "Miblo-Setup-4F2A", note, 204);
    check("setup");
  }
  screens::reset();
  screens::welcome(L, "4827", "192.168.0.42");
  check("welcome");
  screens::reset();
  screens::paired(L, "MacBook", screens::t(L, S::ModeOverview), "miblo-4f2a");
  check("paired");
  for (S title : {S::PairingCode, S::CodeUpdate, S::CodeReset}) {
    screens::reset();
    screens::code(L, title, "1234", 299);
    check("code");
  }
  screens::reset();
  screens::updating(L, 42);
  check("updating");
  screens::reset();
  screens::hardResetCountdown(L, 3);
  check("hard reset");
  screens::reset();
  screens::disconnected(L, clk, "192.168.0.42", "miblo-4f2a", "4827", 0, 0);
  check("disconnected");
  screens::reset();
  screens::desk(L, snap, clk, 0, NOW + 3600);
  check("desk");
  screens::reset();
  screens::limits(L, snap, clk, NOW + 3600);
  check("limits burning");
  screens::reset();
  screens::limitReset(L, snap, clk, 0);
  check("limit reset");
  screens::reset();
  screens::summary(L, snap, clk);
  check("summary");
}

// Desk: the mood follows the fuller window, a window past its reset counts as 0%, and the
// mascot looks at (and bats at) the gauge that worries it.
static void test_desk_mood_and_gauges() {
  TEST_ASSERT_EQUAL_INT((int)screens::DeskMood::Calm, (int)screens::deskMood(49));
  TEST_ASSERT_EQUAL_INT((int)screens::DeskMood::Watchful, (int)screens::deskMood(50));
  TEST_ASSERT_EQUAL_INT((int)screens::DeskMood::Worried, (int)screens::deskMood(80));
  TEST_ASSERT_EQUAL_INT((int)screens::DeskMood::Scared, (int)screens::deskMood(95));
  const UsageWindow w{true, 88, NOW + 60, 0};
  TEST_ASSERT_EQUAL_UINT8(88, screens::deskPct(w, NOW));
  TEST_ASSERT_EQUAL_UINT8(0, screens::deskPct(w, NOW + 60));  // reset since: back to 0
  TEST_ASSERT_EQUAL_UINT8(88, screens::deskPct({true, 88, 0, 0}, NOW));  // unknown reset: kept

  // Scared of the week gauge (right): it gazes right at some point, never left while focused.
  bool gazedRight = false, alarm = false, covered = false;
  for (uint32_t ms = 0; ms < 20000; ms += 50) {
    const screens::MascotLook k = screens::deskLook(screens::DeskMood::Scared, false, ms);
    gazedRight |= k.gx > 0;
    alarm |= (k.extras & screens::kAlarm) != 0;
    covered |= k.paws == screens::Paws::Cover;
  }
  TEST_ASSERT_TRUE(gazedRight && alarm && covered);
  bool reachLeft = false;
  for (uint32_t ms = 0; ms < 20000; ms += 50) {
    reachLeft |= screens::deskLook(screens::DeskMood::Watchful, true, ms).paws == screens::Paws::ReachLeft;
  }
  TEST_ASSERT_TRUE(reachLeft);
  bool sleeps = false;
  for (uint32_t ms = 0; ms < 30000; ms += 50) {
    const screens::MascotLook k = screens::deskLook(screens::DeskMood::Calm, true, ms);
    sleeps |= k.eyes == screens::Eyes::Closed && (k.extras & screens::kZ1);
  }
  TEST_ASSERT_TRUE(sleeps);

  FakeCanvas fc({240, 240});
  screens::bind(fc);
  idle();  // 62% / 38%
  screens::reset();
  fc.clearLog();
  screens::desk(Lang::En, snap, testClock(), 0);
  TEST_ASSERT_TRUE(fc.drew("62%"));
  TEST_ASSERT_TRUE(fc.drew("38%"));
  TEST_ASSERT_TRUE(fc.drew("14:32"));
  TEST_ASSERT_EQUAL_INT(4, (int)fc.arcs.size());  // two tracks + two values
  TEST_ASSERT_EQUAL_INT(0, fc.outOfBounds);
  // Same expression and limits: nothing redrawn.
  fc.clearLog();
  screens::desk(Lang::En, snap, testClock(), 0);
  TEST_ASSERT_EQUAL_INT(0, fc.calls);
  // Limits past their reset show 0%.
  screens::Clock later = testClock();
  later.epoch = NOW + 250000;
  screens::reset();
  fc.clearLog();
  screens::desk(Lang::En, snap, later, 0);
  TEST_ASSERT_TRUE(fc.drew("0%"));
  TEST_ASSERT_FALSE(fc.drew("62%"));
}

// Disconnected: says so, keeps the address and pairing code, animates the waiting dots, and
// shows no limits.
static void test_disconnected_has_mascot_and_info() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  screens::reset();
  fc.clearLog();
  screens::disconnected(Lang::En, testClock(), "192.168.0.42", "miblo-4f2a", "4827", 0, 0);
  TEST_ASSERT_TRUE(fc.drew("Disconnected"));
  TEST_ASSERT_TRUE(fc.drew("Waiting for the computer"));
  TEST_ASSERT_TRUE(fc.drew("192.168.0.42"));
  TEST_ASSERT_TRUE(fc.drew("4827"));
  TEST_ASSERT_FALSE(fc.drew("%"));
  TEST_ASSERT_EQUAL_INT(0, (int)fc.arcs.size());
  TEST_ASSERT_EQUAL_INT(0, fc.outOfBounds);
  fc.clearLog();
  screens::disconnected(Lang::En, testClock(), "192.168.0.42", "miblo-4f2a", "4827", 1200, 1200);
  TEST_ASSERT_TRUE(fc.drew("Waiting for the computer.."));  // the dots move
  TEST_ASSERT_EQUAL_INT(0, fc.outOfBounds);
}

// The mascot is composed in 8 small strips (one 96x12 buffer on a 240 screen) and never
// clears anything on the panel itself: no flash when its expression changes, even with a
// fragmented heap (a whole-box 8 KB layer used to fail and fall back to direct drawing).
static void test_desk_mascot_redraws_in_strips_without_flashing() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  idle();
  screens::reset();
  screens::desk(Lang::En, snap, testClock(), 0);
  // Find the next expression change in the watchful loop and redraw only that.
  const screens::MascotLook first = screens::deskLook(screens::DeskMood::Watchful, true, 0);
  uint32_t ms = 0;
  while (screens::deskLook(screens::DeskMood::Watchful, true, ms) == first) ms += 50;
  fc.clearLog();
  screens::desk(Lang::En, snap, testClock(), ms);  // 62%: watchful
  TEST_ASSERT_EQUAL_INT(8, fc.layerBegins);
  TEST_ASSERT_EQUAL_INT(8, fc.layerEnds);
  TEST_ASSERT_EQUAL_INT(1, fc.layerReleases);
  TEST_ASSERT_EQUAL_INT(96, fc.lastLayer[2]);  // the desk mascot's 96 px box
  TEST_ASSERT_EQUAL_INT(12, fc.lastLayer[3]);
  TEST_ASSERT_EQUAL_INT(0, fc.panelFills);  // nothing painted straight on the panel
  TEST_ASSERT_EQUAL_INT(0, fc.outOfBounds);
  // Same expression: nothing at all.
  fc.clearLog();
  screens::desk(Lang::En, snap, testClock(), ms);
  TEST_ASSERT_EQUAL_INT(0, fc.calls);
}

// Burn rate: when the recent pace runs the 5h window out before it resets, the arc and the desk
// say when, in amber; otherwise the plain reset countdown.
static void test_burn_rate_lines() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  idle();  // 62%, resets in 2h10
  screens::reset();
  fc.clearLog();
  screens::limits(Lang::En, snap, testClock(), NOW + 80 * 60);
  TEST_ASSERT_TRUE(fc.drew("at this pace, runs out at "));
  screens::reset();
  fc.clearLog();
  screens::limits(Lang::En, snap, testClock(), 0);
  TEST_ASSERT_FALSE(fc.drew("runs out"));
  screens::reset();
  fc.clearLog();
  screens::desk(Lang::En, snap, testClock(), 0, NOW + 80 * 60);
  TEST_ASSERT_TRUE(fc.drew("runs out in 1h20"));
  TEST_ASSERT_FALSE(fc.drew("in 2h10"));
  screens::reset();
  fc.clearLog();
  screens::desk(Lang::En, snap, testClock(), 0, 0);
  TEST_ASSERT_TRUE(fc.drew("in 2h10"));
  TEST_ASSERT_EQUAL_INT(0, fc.outOfBounds);
}

// "Limit freed": green band, the new usage, the next reset; today's summary: responses, time
// worked, cost and the limits strip.
static void test_limit_reset_and_summary_content() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  idle();
  snap.h5 = {true, 2, NOW + 5 * 3600};
  screens::reset();
  fc.clearLog();
  screens::limitReset(Lang::En, snap, testClock(), 0);
  TEST_ASSERT_TRUE(fc.drew("LIMIT FREED"));
  TEST_ASSERT_TRUE(fc.drew("2%"));
  TEST_ASSERT_TRUE(fc.drew("5h session"));
  char when[32];  // local time: depends on the machine's time zone
  screens::formatWhen(Lang::En, NOW + 5 * 3600, NOW, when, sizeof(when));
  TEST_ASSERT_TRUE(fc.drew(std::string("resets ") + when));
  TEST_ASSERT_EQUAL_INT(0, fc.outOfBounds);

  idle();
  snap.todayTurns = 14;
  snap.todayWorkSec = 3 * 3600 + 12 * 60;
  screens::reset();
  fc.clearLog();
  screens::summary(Lang::En, snap, testClock());
  TEST_ASSERT_TRUE(fc.drew("TODAY"));
  TEST_ASSERT_TRUE(fc.drew("14"));
  TEST_ASSERT_TRUE(fc.drew("responses"));
  TEST_ASSERT_TRUE(fc.drew("3h12"));
  TEST_ASSERT_TRUE(fc.drew("$3.50"));
  TEST_ASSERT_TRUE(fc.drew("62%"));
  TEST_ASSERT_EQUAL_INT(0, fc.outOfBounds);
  snap.todayWorkSec = 0;
  screens::reset();
  fc.clearLog();
  screens::summary(Lang::En, snap, testClock());
  TEST_ASSERT_TRUE(fc.drew("0min"));
}

// Pet mode: the wandering card carries the clock, the limits, the next reset (or "limit freed"
// once the 5h window has reset) and the last finished task, and stays on screen all along its path.
static void test_pet_mode_card_and_path() {
  const ui::ScreenSpec specs[] = {{240, 240}, {320, 240}, {480, 320}, {170, 320}};
  for (const auto& sp : specs) {
    FakeCanvas fc(sp);
    screens::bind(fc);
    idle();
    screens::reset();
    for (uint32_t ms = 0; ms < 400000; ms += 997) screens::roam(Lang::En, snap, testClock(), ms, screens::DeskMood::Calm);
    TEST_ASSERT_EQUAL_INT(0, fc.outOfBounds);
  }
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  idle();
  screens::reset();
  fc.clearLog();
  screens::roam(Lang::En, snap, testClock(), 0, screens::DeskMood::Calm);
  TEST_ASSERT_TRUE(fc.drew("14:32"));
  // each window marked by an icon, not a label: the percentages are drawn on their own (bold)
  TEST_ASSERT_TRUE(fc.drew("62%"));
  TEST_ASSERT_TRUE(fc.drew("38%"));
  TEST_ASSERT_FALSE(fc.drew("5h 62%"));
  TEST_ASSERT_TRUE(fc.drew("resets "));
  TEST_ASSERT_TRUE(fc.drew("infra"));             // the most recently finished session
  TEST_ASSERT_TRUE(fc.drew("finished 2m ago"));
  // The 5h window has reset since the last snapshot: 0% and "limit freed".
  screens::Clock later = testClock();
  later.epoch = NOW + 3 * 3600;
  screens::reset();
  fc.clearLog();
  screens::roam(Lang::En, snap, later, 0, screens::DeskMood::Calm);
  TEST_ASSERT_TRUE(fc.drew("0%"));
  TEST_ASSERT_TRUE(fc.drew("LIMIT FREED"));
  // It moves: later on it is somewhere else.
  int x0, y0, x1, y1;
  screens::roamPosition(0, x0, y0);
  screens::roamPosition(60000, x1, y1);
  TEST_ASSERT_TRUE(x0 != x1 || y0 != y1);
}

// Pet mode with the computer away: a crossed-out laptop sits on the sign, only then, and inside
// the screen all along the path.
static void test_pet_mode_shows_computer_away() {
  int cx, cy, ix, iy, iw, ih;
  screens::roamPosition(0, cx, cy);
  screens::roamAwayIcon(cx, cy, ix, iy, iw, ih);
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  idle();
  screens::reset();
  screens::roam(Lang::En, snap, testClock(), 0, screens::DeskMood::Searching);
  const int plain = fc.colorAt(ix + iw / 2, iy);  // the top of the laptop's lid
  TEST_ASSERT_NOT_EQUAL(ui::color::MUTED, plain);
  screens::reset();
  screens::roam(Lang::En, snap, testClock(), 0, screens::DeskMood::Searching, nullptr, UINT32_MAX, true);
  TEST_ASSERT_EQUAL_INT(ui::color::MUTED, fc.colorAt(ix + iw / 2, iy));  // readable, not DIM
  for (uint32_t ms = 0; ms < 400000; ms += 997)
    screens::roam(Lang::En, snap, testClock(), ms, screens::DeskMood::Searching, nullptr, UINT32_MAX, true);
  TEST_ASSERT_EQUAL_INT(0, fc.outOfBounds);
}

static void test_update_available_screen() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  screens::reset();
  fc.clearLog();
  screens::updateAvailable(Lang::En, "1.0.1", "1.1.0", 0);
  TEST_ASSERT_TRUE(fc.drew("Update available"));
  TEST_ASSERT_TRUE(fc.drew("v1.1.0 (you have v1.0.1)"));
  TEST_ASSERT_TRUE(fc.drew("/miblo:update"));
  TEST_ASSERT_EQUAL_INT(0, fc.outOfBounds);
}

// The new poses (stretching, licking, playing keys, dizzy, grumpy, cross-eyed, fluffed up) stay
// inside the cat's 96-unit box: drawn with the box touching the top-left and bottom-right corners,
// nothing may fall off the screen.
static void test_mascot_new_poses_stay_in_box() {
  using screens::Eyes;
  using screens::Paws;
  const screens::MascotLook looks[] = {
      {0, -3, 0, 0, Eyes::Closed, Paws::Up, screens::kMouthWide},
      {0, 0, 0, 0, Eyes::Closed, Paws::Lick, screens::kTongue},
      {0, 0, 0, 3, Eyes::Happy, Paws::TapLeft, 0},
      {0, 0, 0, 3, Eyes::Happy, Paws::TapRight, 0},
      {0, 0, 0, 0, Eyes::Dizzy, Paws::Down, screens::kStars},
      {0, 0, 3, 0, Eyes::Open, Paws::Down, (uint16_t)(screens::kGrumpy | screens::kCrossEyed)},
      {0, -6, 3, 0, Eyes::Wide, Paws::Down, (uint16_t)(screens::kFluffed | screens::kAlarm)},
  };
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  for (const auto& k : looks) {
    screens::deskMascot(36, 36, k, 36, false, false);
    screens::deskMascot(204, 204, k, 36, false, false);
  }
  TEST_ASSERT_EQUAL_INT(0, fc.outOfBounds);
}

// Pet mode antics: one every kAnticEveryMs from the second cycle, all 30 once per round in a
// shuffled order, never the same one twice in a row (across rounds too), always the same for the
// same time; each lasts its length (held: kAnticMs; on the floor: kAnticFloorMs).
static void test_pet_antics_order() {
  for (int run = 0; run < 2; run++) {  // deterministic: a second run sees the same order
    static screens::RoamAntic first[91];
    uint8_t seen[3][screens::kAnticCount + 1] = {};
    screens::RoamAntic prev = screens::RoamAntic::None;
    for (uint32_t c = 1; c <= 90; c++) {
      uint32_t at = 1;
      const screens::RoamAntic a = screens::roamAntic(c * screens::kAnticEveryMs, &at);
      TEST_ASSERT_EQUAL_UINT32(0, at);
      TEST_ASSERT_TRUE(a != screens::RoamAntic::None);
      TEST_ASSERT_TRUE(a != prev);
      if (run == 0) first[c] = a;
      TEST_ASSERT_TRUE(first[c] == a);
      seen[(c - 1) / screens::kAnticCount][(uint8_t)a]++;
      prev = a;
    }
    for (int r = 0; r < 3; r++)
      for (int a = 1; a <= screens::kAnticCount; a++) TEST_ASSERT_EQUAL_UINT8(1, seen[r][a]);
  }
  TEST_ASSERT_TRUE(screens::roamAntic(1000, nullptr) == screens::RoamAntic::None);  // first cycle: calm
  for (uint32_t c = 1; c <= screens::kAnticCount; c++) {
    const uint32_t t0 = c * screens::kAnticEveryMs;
    const screens::RoamAntic a = screens::roamAntic(t0, nullptr);
    const uint32_t len = screens::anticLength(a);
    TEST_ASSERT_EQUAL_UINT32(screens::anticOnSign(a) ? screens::kAnticMs : screens::kAnticFloorMs, len);
    TEST_ASSERT_TRUE(screens::roamAntic(t0 + len - 1, nullptr) == a);
    TEST_ASSERT_TRUE(screens::roamAntic(t0 + len, nullptr) == screens::RoamAntic::None);
  }
  // The nine with the sign in its paws
  int held = 0;
  for (int a = 1; a <= screens::kAnticCount; a++) held += screens::anticOnSign((screens::RoamAntic)a);
  TEST_ASSERT_EQUAL_INT(9, held);
}

// Off the sign: the sign goes down on the floor with all its lines (and the away icon), the
// whole screen is redrawn; back to a held sign, the whole screen once more (no floor leftovers,
// no blink). The computer away and Calm: what pet mode shows while nobody's computer is there.
static void test_floor_sign_keeps_lines_and_icon() {
  uint32_t c = 1;
  while (screens::anticOnSign(screens::roamAntic(c * screens::kAnticEveryMs, nullptr))) c++;
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  idle();
  screens::reset();
  fc.clearLog();
  const uint32_t play = c * screens::kAnticEveryMs + screens::kAnticPutMs + 4000;
  screens::roam(Lang::En, snap, testClock(), play, screens::DeskMood::Calm, nullptr, UINT32_MAX, true);
  TEST_ASSERT_TRUE(fc.drew("14:32"));
  TEST_ASSERT_TRUE(fc.drew("62%"));
  TEST_ASSERT_TRUE(fc.drew("finished 2m ago"));
  // the crossed-out laptop's outline (MUTED, like the labels) is somewhere in the bottom half:
  // more MUTED there than on the same frame with the computer present
  auto mutedBelow = [&] {
    int n = 0;
    for (int y = 120; y < 240; y++)
      for (int x = 0; x < 240; x++) n += fc.colorAt(x, y) == ui::color::MUTED;
    return n;
  };
  const int withIcon = mutedBelow();
  TEST_ASSERT_EQUAL_INT(0, fc.outOfBounds);
  screens::reset();
  screens::roam(Lang::En, snap, testClock(), play, screens::DeskMood::Calm);
  TEST_ASSERT_TRUE(withIcon > mutedBelow());
  screens::reset();
  screens::roam(Lang::En, snap, testClock(), play, screens::DeskMood::Calm, nullptr, UINT32_MAX, true);
  // The held sign comes back without blinking: no clear straight on the panel, the whole screen
  // recomposed in strips with the sign in its paws again.
  fc.clearLog();
  screens::roam(Lang::En, snap, testClock(), (c + 1) * screens::kAnticEveryMs - 1, screens::DeskMood::Calm);
  TEST_ASSERT_EQUAL_INT(0, fc.panelFills);
  TEST_ASSERT_TRUE(fc.drew("14:32"));
  // Nor on the held frames around it, the box moving a pixel at a time.
  for (uint32_t step = 1; step <= 3; step++)
    screens::roam(Lang::En, snap, testClock(), (c + 1) * screens::kAnticEveryMs - 1 - step * 420,
                  screens::DeskMood::Calm);
  TEST_ASSERT_EQUAL_INT(0, fc.panelFills);
}

// Every prop stays within 70 px of its anchor at the 240 grid (the antics lay them out with that
// much room from the screen edges).
static void test_props_stay_near_their_anchor() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  for (uint8_t kind = 1; kind <= 27; kind++)
    for (uint8_t f = 0; f < 24; f++) screens::drawPropForTest(kind, 120, 120, f, kind == 9 ? 60 : 0);
  TEST_ASSERT_EQUAL_INT(0, fc.outOfBounds);
  FakeCanvas small({140, 140});  // anchors at the centre of a 140 px canvas: 70 px of room
  screens::bind(small);
  for (uint8_t kind = 1; kind <= 27; kind++)
    for (uint8_t f = 0; f < 24; f++) screens::drawPropForTest(kind, 70, 70, f, 0);
  TEST_ASSERT_EQUAL_INT(0, small.outOfBounds);
}

// Not playful (a friend's note, or not calm): no antic, the sign stays in its paws.
static void test_pet_scene_quiet_when_not_playful() {
  uint32_t c = 1;
  while (screens::anticOnSign(screens::roamAntic(c * screens::kAnticEveryMs, nullptr))) c++;
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  idle();
  screens::reset();
  const uint32_t ms = c * screens::kAnticEveryMs + screens::kAnticPutMs + 4000;
  screens::roam(Lang::En, snap, testClock(), ms, screens::DeskMood::Calm, "Hi, Nina!");
  int x, y, w, h, cx, cy;
  screens::roamPosition(ms, cx, cy);
  screens::roamAwayIcon(cx, cy, x, y, w, h);
  TEST_ASSERT_TRUE(fc.drew("Hi, Nina!"));
  TEST_ASSERT_EQUAL_INT(0x2125, fc.colorAt(x - 4, y + h));  // the held sign's fill at the pet's spot
}

// Every antic, all along it (putting the sign down, playing, picking it up), stays on screen at
// every supported resolution.
static void test_pet_antics_stay_on_screen() {
  const ui::ScreenSpec specs[] = {{240, 240}, {320, 240}, {480, 320}, {170, 320}};
  for (const auto& sp : specs) {
    FakeCanvas fc(sp);
    screens::bind(fc);
    idle();
    screens::reset();
    for (uint32_t c = 1; c <= screens::kAnticCount; c++)
      for (uint32_t at = 0; at < screens::kAnticFloorMs; at += 100)
        screens::roam(Lang::En, snap, testClock(), c * screens::kAnticEveryMs + at, screens::DeskMood::Calm);
    TEST_ASSERT_EQUAL_INT(0, fc.outOfBounds);
  }
}

// Riff alone has a 31st antic, a guitar solo: in its rounds all 31 once each, never twice in a
// row; no other pet ever plays it, and the others' order is the same as ever.
static void test_riff_plays_a_solo() {
  screens::RoamAntic catOrder[61];
  for (uint32_t c = 1; c <= 60; c++) catOrder[c] = screens::roamAntic(c * screens::kAnticEveryMs, nullptr);
  screens::setMascotPet((uint8_t)miblo::Pet::Riff);
  const uint8_t n = screens::kAnticCount + 1;
  uint8_t seen[3][screens::kAnticCount + 2] = {};
  screens::RoamAntic prev = screens::RoamAntic::None;
  for (uint32_t c = 1; c <= 3u * n; c++) {
    const screens::RoamAntic a = screens::roamAntic(c * screens::kAnticEveryMs, nullptr);
    TEST_ASSERT_TRUE(a != screens::RoamAntic::None);
    TEST_ASSERT_TRUE(a != prev);
    seen[(c - 1) / n][(uint8_t)a]++;
    prev = a;
  }
  for (int r = 0; r < 3; r++)
    for (int a = 1; a <= n; a++) TEST_ASSERT_EQUAL_UINT8(1, seen[r][a]);
  TEST_ASSERT_FALSE(screens::anticOnSign(screens::RoamAntic::Solo));
  for (uint8_t pet = 0; pet < miblo::kPetKinds; pet++) {
    if (pet == (uint8_t)miblo::Pet::Riff) continue;
    screens::setMascotPet(pet);
    for (uint32_t c = 1; c <= 60; c++)
      TEST_ASSERT_TRUE(screens::roamAntic(c * screens::kAnticEveryMs, nullptr) == catOrder[c]);
  }
  screens::setMascotPet(0);
}

// Every pet's own tail antic (and Riff's solo), all along it, stays on screen at every resolution.
static void test_pet_tails_stay_on_screen() {
  const ui::ScreenSpec specs[] = {{240, 240}, {320, 240}, {480, 320}, {170, 320}};
  for (uint8_t pet = 0; pet < miblo::kPetKinds; pet++) {
    screens::setMascotPet(pet);
    for (const auto& sp : specs) {
      FakeCanvas fc(sp);
      screens::bind(fc);
      idle();
      screens::reset();
      for (uint32_t c = 1; c <= 2u * (screens::kAnticCount + 1); c++) {
        const screens::RoamAntic a = screens::roamAntic(c * screens::kAnticEveryMs, nullptr);
        if (a != screens::RoamAntic::Tail && a != screens::RoamAntic::Solo) continue;
        for (uint32_t at = 0; at < screens::kAnticFloorMs; at += 50)
          screens::roam(Lang::En, snap, testClock(), c * screens::kAnticEveryMs + at, screens::DeskMood::Calm);
      }
      TEST_ASSERT_EQUAL_INT(0, fc.outOfBounds);
    }
  }
  screens::setMascotPet(0);
}

// The canvas whose band check visit() arms around each prop it draws.
static FakeCanvas* gBandCanvas = nullptr;
static void armBand(bool drawing) {
  if (gBandCanvas) gBandCanvas->bandArmed = drawing;
}

// Every activity (the 7 old and the 30 new), host and visitor, 1 to 3 guests, every side, every
// supported resolution, all along the stay and the first step of leaving (the tall area clears
// back to the band): nothing off screen (walking in and out, the cats leave the screen on purpose:
// the panel clips them), and every prop inside its band: x X(3)..X(237), y Y(30)..the cats'
// bottom. And a frame that differs only in an item's animation redraws.
static void test_visits_stay_on_screen_all_activities() {
  screens::visitItemHookForTest = armBand;
  const ui::ScreenSpec specs[] = {{240, 240}, {320, 240}, {480, 320}, {170, 320}};
  for (const auto& sp : specs) {
    FakeCanvas fc(sp);
    gBandCanvas = &fc;
    screens::bind(fc);
    idle();
    screens::reset();
    fc.bandMinX = screens::X(3);
    fc.bandMaxX = screens::X(237);
    fc.bandTop = screens::Y(30);
    for (int g = 0; g < (int)miblo::Gift::Count; g++)
      for (int role = 0; role < 2; role++)
        for (uint8_t extra = 0; extra < 3; extra++)
          for (uint8_t side = 0; side < 4; side++) {
            if (role == 1 && extra) continue;  // a visitor sees only itself
            miblo::VisitView v;
            strcpy(v.name, "Nina");
            v.mascot = 1;
            v.extraMascot[0] = 2;
            v.extraMascot[1] = 3;
            v.role = role ? miblo::VisitRole::Visitor : miblo::VisitRole::Host;
            v.gift = (miblo::Gift)g;
            v.extra = extra;
            const int cats = role ? 2 : 2 + extra;  // the cats' size, as visit() picks it
            fc.bandBottom = screens::Y(104) + screens::Sz(cats <= 2 ? 40 : cats == 3 ? 33 : 27);
            for (uint32_t ms = miblo::kVisitArriveMs; ms <= miblo::kVisitPartMs; ms += 250) {
              v.ms = ms;
              screens::visit(Lang::En, snap, testClock(), v, side);
            }
          }
    TEST_ASSERT_EQUAL_INT(0, fc.outOfBounds);
    TEST_ASSERT_EQUAL_INT(0, fc.bandOut);
  }
  screens::visitItemHookForTest = nullptr;
  gBandCanvas = nullptr;
  // Pair programming at t = 590 and 610 ms: same looks (paws switch every 250 ms), only the
  // laptop's code frame (every 300 ms) differs, and that alone redraws the cats.
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  idle();
  screens::reset();
  miblo::VisitView v;
  strcpy(v.name, "Nina");
  v.role = miblo::VisitRole::Host;
  v.gift = miblo::Gift::Pair;
  v.ms = miblo::kVisitArriveMs + 590;
  screens::visit(Lang::En, snap, testClock(), v);
  const int before = fc.calls;
  v.ms = miblo::kVisitArriveMs + 610;
  screens::visit(Lang::En, snap, testClock(), v);
  TEST_ASSERT_GREATER_THAN_INT(before, fc.calls);
}

// addItem keeps up to four props and drops kinds nobody draws.
static void test_visit_add_item_drops_bad_kinds() {
  screens::VisitFrame f{};
  screens::addItem(f, screens::vprop::None, 1, 1);
  screens::addItem(f, screens::vprop::Count, 1, 1);
  screens::addItem(f, screens::kItemsA - 1, 1, 1);
  screens::addItem(f, screens::kItemsEnd, 1, 1);
  TEST_ASSERT_EQUAL_UINT8(0, f.n);
  const uint8_t ok[] = {screens::vprop::Duck, screens::vprop::Drop, screens::kItemsA, screens::kItemsEnd - 1,
                        screens::kItemsB};
  for (uint8_t k : ok) screens::addItem(f, k, 2, 3, 4);
  TEST_ASSERT_EQUAL_UINT8(4, f.n);
  TEST_ASSERT_EQUAL_UINT8(screens::kItemsEnd - 1, f.items[3].kind);
}

// Blue light filter: the colour under a warmer white point, strength 1..100 %. Off changes
// nothing, black stays black, red is kept, and the white gets warmer with the strength (blue drops
// faster than green). The old three levels' strengths give exactly their old colours. ShiftCanvas
// applies it to every colour it forwards, and switching it off restores the colours.
static void test_warm_color() {
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, ui::warmColor(0xFFFF, ui::warmGains(0)));
  TEST_ASSERT_EQUAL_HEX16(0x1234, ui::warmColor(0x1234, ui::warmGains(0)));
  TEST_ASSERT_EQUAL_HEX16(0x0000, ui::warmColor(0x0000, ui::warmGains(100)));
  TEST_ASSERT_EQUAL_HEX16(0xF800, ui::warmColor(0xF800, ui::warmGains(100)));  // pure red is untouched
  TEST_ASSERT_EQUAL_HEX16(ui::warmColor(0xFFFF, ui::warmGains(100)), ui::warmColor(0xFFFF, ui::warmGains(200)));  // clamped

  // The old levels (1, 2, 3 = 4541 K, 3489 K, 2732 K): green and blue out of 255.
  static const uint8_t kLegacy[3][2] = {{222, 188}, {199, 139}, {173, 89}};
  for (uint8_t level = 1; level <= 3; level++) {
    const ui::WarmGains g = ui::warmGains(miblo::blueStrengthForLevel(level));
    TEST_ASSERT_EQUAL_UINT8(kLegacy[level - 1][0], g.g);
    TEST_ASSERT_EQUAL_UINT8(kLegacy[level - 1][1], g.b);
  }

  // Monotonic: each step is at least as warm as the one before, blue never above green, 1 % is
  // almost neutral and 100 % is the old strongest.
  ui::WarmGains prev = ui::warmGains(0);
  TEST_ASSERT_EQUAL_UINT8(255, prev.g);
  TEST_ASSERT_EQUAL_UINT8(255, prev.b);
  for (int s = 1; s <= 100; s++) {
    const ui::WarmGains g = ui::warmGains((uint8_t)s);
    TEST_ASSERT_TRUE(g.g <= prev.g && g.b <= prev.b);
    TEST_ASSERT_TRUE(g.g + g.b < prev.g + prev.b);  // strictly warmer at every step
    TEST_ASSERT_TRUE(g.b <= g.g);
    prev = g;
  }
  TEST_ASSERT_TRUE(ui::warmGains(1).g >= 253 && ui::warmGains(1).b >= 252);

  // Every colour at every strength is exactly round(v * m / 255) per channel (warmColor computes
  // it with a multiply and a shift): green and blue scaled by the strength's multipliers, red kept.
  for (int s = 1; s <= 100; s++) {
    const ui::WarmGains gn = ui::warmGains((uint8_t)s);
    for (uint32_t c = 0; c <= 0xFFFF; c++) {
      const uint32_t g = (((c >> 5) & 63) * gn.g + 127) / 255, b = ((c & 31) * gn.b + 127) / 255;
      const uint16_t want = (uint16_t)((c & 0xF800) | g << 5 | b);
      if (ui::warmColor((uint16_t)c, gn) != want) TEST_ASSERT_EQUAL_HEX16(want, ui::warmColor((uint16_t)c, gn));
    }
  }

  FakeCanvas fc({240, 240});
  ui::ShiftCanvas sc(fc);
  sc.setWarmth(63);
  TEST_ASSERT_EQUAL_UINT8(63, sc.warmth());
  sc.fillRect(0, 0, 10, 10, 0xFFFF);
  TEST_ASSERT_EQUAL_INT(ui::warmColor(0xFFFF, ui::warmGains(63)), fc.colorAt(5, 5));
  sc.fillRoundRect(20, 0, 10, 10, 2, ui::color::BLUE);
  TEST_ASSERT_EQUAL_INT(ui::warmColor(ui::color::BLUE, ui::warmGains(63)), fc.colorAt(25, 5));
  sc.setWarmth(0);
  sc.fillRect(40, 0, 10, 10, 0xFFFF);
  TEST_ASSERT_EQUAL_INT(0xFFFF, fc.colorAt(45, 5));
}

// Every colour ShiftCanvas forwards is filtered (foregrounds and backgrounds of every primitive,
// and the whole-panel clear), so nothing on the screen keeps its cold colour.
struct ColorLog : ui::Canvas {
  std::vector<uint16_t> colors;
  ui::ScreenSpec spec() const override { return {240, 240}; }
  void fillRect(int, int, int, int, uint16_t c) override { colors.push_back(c); }
  void fillRoundRect(int, int, int, int, int, uint16_t c) override { colors.push_back(c); }
  void drawRect(int, int, int, int, uint16_t c) override { colors.push_back(c); }
  void fillCircle(int, int, int, uint16_t c) override { colors.push_back(c); }
  void fillTriangle(int, int, int, int, int, int, uint16_t c) override { colors.push_back(c); }
  void wideLine(int, int, int, int, int, uint16_t c, uint16_t bg) override { colors.push_back(c), colors.push_back(bg); }
  void arc(int, int, int, int, int, int, uint16_t fg, uint16_t bg) override { colors.push_back(fg), colors.push_back(bg); }
  int text(int, int, const char*, ui::Font, uint16_t fg, ui::Align, int) override { return colors.push_back(fg), 7; }
  int textWidth(const char*, ui::Font) override { return 7; }
  int textBox(int, int, const char*, ui::Font, uint16_t fg, uint16_t bg, ui::Align, int) override {
    return colors.push_back(fg), colors.push_back(bg), 7;
  }
  void clear(uint16_t c) override { colors.push_back(c); }
};

static void test_shift_canvas_warms_every_color() {
  for (uint8_t level : {0, 1, 31, 63, 100}) {
    ColorLog log;
    ui::ShiftCanvas sc(log);
    sc.setShift(2, -1);
    sc.setWarmth(level);
    uint16_t c = 0x1111;  // a different colour for each argument
    const uint16_t first = c;
    sc.fillRect(0, 0, 1, 1, c++);
    sc.fillRoundRect(0, 0, 1, 1, 1, c++);
    sc.drawRect(0, 0, 1, 1, c++);
    sc.fillCircle(0, 0, 1, c++);
    sc.fillTriangle(0, 0, 1, 1, 2, 0, c++);
    sc.wideLine(0, 0, 9, 9, 3, c, (uint16_t)(c + 1)), c += 2;
    sc.arc(50, 50, 20, 15, 0, 90, c, (uint16_t)(c + 1)), c += 2;
    TEST_ASSERT_EQUAL_INT(7, sc.text(0, 0, "a", ui::Font::Small, c++, ui::Align::Left, 99));
    TEST_ASSERT_EQUAL_INT(7, sc.textBox(0, 0, "a", ui::Font::Small, c, (uint16_t)(c + 1), ui::Align::Left, 99));
    c += 2;
    sc.clear(c++);
    TEST_ASSERT_EQUAL_INT(c - first, (int)log.colors.size());
    for (size_t i = 0; i < log.colors.size(); i++) {
      TEST_ASSERT_EQUAL_HEX16(ui::warmColor((uint16_t)(first + i), ui::warmGains(level)), log.colors[i]);
    }
  }
}

// ---------------- daily life: forecast, long commands, second clock ----------------

// A FakeCanvas that also keeps each text's colour.
class ColorCanvas : public FakeCanvas {
 public:
  explicit ColorCanvas(ui::ScreenSpec s) : FakeCanvas(s) {}
  int text(int x, int y, const char* s, ui::Font f, uint16_t fg, ui::Align a, int maxW) override {
    fgs.push_back(fg);
    return FakeCanvas::text(x, y, s, f, fg, a, maxW);
  }
  // Colour of the first drawn text containing `needle` (-1 if not drawn).
  int fgOf(const std::string& needle) const {
    for (size_t i = 0; i < texts.size(); i++) {
      if (texts[i].find(needle) != std::string::npos) return fgs[i];
    }
    return -1;
  }
  void clearAll() {
    clearLog();
    fgs.clear();
  }
  std::vector<uint16_t> fgs;
};

static screens::Clock clockAt(uint32_t epoch) {
  screens::Clock c = testClock();
  c.epoch = epoch;
  return c;
}

static void test_second_zone_clock_keeps_tz() {
  setenv("TZ", "<-03>3", 1);
  tzset();
  char b[8];
  TEST_ASSERT_TRUE(miblo::zoneHHMM("Europe/Lisbon", 1790605920, b, sizeof(b)));  // 2026-09-28 14:32 UTC
  TEST_ASSERT_EQUAL_STRING("15:32", b);  // WEST, UTC+1
  TEST_ASSERT_EQUAL_STRING("<-03>3", getenv("TZ"));
  const time_t probe = 1790605920;
  struct tm lt;
  localtime_r(&probe, &lt);
  TEST_ASSERT_EQUAL_INT(11, lt.tm_hour);  // the process zone still rules localtime
  TEST_ASSERT_TRUE(miblo::zoneHHMM("Europe/Lisbon", 1768833120, b, sizeof(b)));  // 2026-01-19 14:32 UTC
  TEST_ASSERT_EQUAL_STRING("14:32", b);                                       // WET, UTC+0
  TEST_ASSERT_TRUE(miblo::zoneHHMM("Australia/Sydney", 1768833120, b, sizeof(b)));  // summer there: +11
  TEST_ASSERT_EQUAL_STRING("01:32", b);
  TEST_ASSERT_TRUE(miblo::zoneHHMM("Australia/Sydney", 1790605920, b, sizeof(b)));  // before its DST: +10
  TEST_ASSERT_EQUAL_STRING("00:32", b);
  TEST_ASSERT_TRUE(miblo::zoneHHMM("Asia/Kolkata", 1790605920, b, sizeof(b)));  // +05:30
  TEST_ASSERT_EQUAL_STRING("20:02", b);
  TEST_ASSERT_TRUE(miblo::zoneHHMM("America/Sao_Paulo", 1790605920, b, sizeof(b)));
  TEST_ASSERT_EQUAL_STRING("11:32", b);
  TEST_ASSERT_TRUE(miblo::zoneHHMM("America/New_York", 1790605920, b, sizeof(b)));  // EDT
  TEST_ASSERT_EQUAL_STRING("10:32", b);
  // The DST edge: Lisbon changes at 01:00 UTC on the last Sunday of March (2026-03-29).
  TEST_ASSERT_TRUE(miblo::zoneHHMM("Europe/Lisbon", 1774745940, b, sizeof(b)));  // 00:59 UTC
  TEST_ASSERT_EQUAL_STRING("00:59", b);
  TEST_ASSERT_TRUE(miblo::zoneHHMM("Europe/Lisbon", 1774746000, b, sizeof(b)));  // 01:00 UTC
  TEST_ASSERT_EQUAL_STRING("02:00", b);
  TEST_ASSERT_FALSE(miblo::zoneHHMM("Mars/Base", 1790605920, b, sizeof(b)));
  TEST_ASSERT_FALSE(miblo::zoneHHMM("Europe/Lisbon", 0, b, sizeof(b)));  // time unknown
  miblo::Config c;
  strcpy(c.tz2, "America/Sao_Paulo");
  char big[40];
  miblo::zoneLabel(c, big, sizeof(big));
  TEST_ASSERT_EQUAL_STRING("Sao Paulo", big);
  strcpy(c.tz2, "America/Argentina/Buenos_Aires");
  miblo::zoneLabel(c, big, sizeof(big));
  TEST_ASSERT_EQUAL_STRING("Buenos Aires", big);
  strcpy(c.tz2Label, "Lisboa");
  miblo::zoneLabel(c, big, sizeof(big));
  TEST_ASSERT_EQUAL_STRING("Lisboa", big);
}

// A Bash command past 30 s shows "M:SS" on its card, next to the command; the card itself is not
// redrawn every second.
static void test_long_command_on_overview() {
  ColorCanvas fc({240, 240});
  screens::bind(fc);
  Pager pager(3, 5000);
  working();  // worker: Bash "npm test"
  snap.sessions[1].ts = NOW - 102;
  screens::reset();
  fc.clearAll();
  screens::overview(Lang::En, snap, pager, 0, clockAt(NOW), false, 0);
  TEST_ASSERT_TRUE(fc.drew("1:42"));
  TEST_ASSERT_EQUAL_INT(ui::color::GREEN, fc.fgOf("1:42"));
  TEST_ASSERT_TRUE(fc.fontOf("1:42") == Font::SmallBold);
  TEST_ASSERT_TRUE(fc.drew("npm test"));
  TEST_ASSERT_FALSE(fc.drew("Bash"));  // the command alone, its time beside it
  fc.clearAll();
  screens::overview(Lang::En, snap, pager, 0, clockAt(NOW + 1), false, 0);
  TEST_ASSERT_TRUE(fc.drew("1:43"));
  TEST_ASSERT_TRUE(fc.calls < 20);  // only the time field
  TEST_ASSERT_FALSE(fc.drew("npm test"));
  // Discreet: the time still shows, the command does not.
  screens::reset();
  fc.clearAll();
  screens::overview(Lang::En, snap, pager, 0, clockAt(NOW), true, 0);
  TEST_ASSERT_TRUE(fc.drew("1:42"));
  TEST_ASSERT_FALSE(fc.drew("npm test"));
  // Under 30 s, or a tool other than Bash: no time.
  snap.sessions[1].ts = NOW - 29;
  screens::reset();
  fc.clearAll();
  screens::overview(Lang::En, snap, pager, 0, clockAt(NOW), false, 0);
  TEST_ASSERT_FALSE(fc.drew("0:29"));
  TEST_ASSERT_TRUE(fc.drew("Bash"));
  snap.sessions[0].ts = NOW - 300;  // Edit: never
  screens::reset();
  fc.clearAll();
  screens::overview(Lang::En, snap, pager, 0, clockAt(NOW), false, 0);
  TEST_ASSERT_FALSE(fc.drew("5:00"));
}

// Forecast under 30 min: the 5h number turns amber and "runs out ~HH:MM" replaces the reset time.
static void test_overview_forecast_line() {
  setenv("TZ", "<-03>3", 1);
  tzset();
  ColorCanvas fc({240, 240});
  screens::bind(fc);
  Pager pager(3, 5000);
  idle();  // 62%
  screens::reset();
  fc.clearAll();
  screens::overview(Lang::En, snap, pager, 0, clockAt(NOW), false, NOW + 20 * 60);
  TEST_ASSERT_TRUE(fc.drew("runs out ~14:52"));
  TEST_ASSERT_FALSE(fc.drew("resets 16:42"));
  TEST_ASSERT_EQUAL_INT(ui::color::AMBER, fc.fgOf("62%"));
  screens::reset();
  fc.clearAll();
  screens::overview(Lang::En, snap, pager, 0, clockAt(NOW), false, NOW + 40 * 60);
  TEST_ASSERT_FALSE(fc.drew("runs out ~"));
  TEST_ASSERT_TRUE(fc.drew("resets 16:42"));
  TEST_ASSERT_EQUAL_INT(ui::color::TEXT, fc.fgOf("62%"));
  // The forecast coming closer redraws the number in amber (no stale colour).
  fc.clearAll();
  screens::overview(Lang::En, snap, pager, 0, clockAt(NOW), false, NOW + 20 * 60);
  TEST_ASSERT_EQUAL_INT(ui::color::AMBER, fc.fgOf("62%"));
  TEST_ASSERT_TRUE(fc.drew("runs out ~14:52"));
  // Compact strip (Working): the 5h number in amber.
  working();  // 30%
  screens::reset();
  fc.clearAll();
  screens::overview(Lang::En, snap, pager, 0, clockAt(NOW), false, NOW + 20 * 60);
  TEST_ASSERT_EQUAL_INT(ui::color::AMBER, fc.fgOf("30%"));
  screens::reset();
  fc.clearAll();
  screens::overview(Lang::En, snap, pager, 0, clockAt(NOW), false, 0);
  TEST_ASSERT_EQUAL_INT(ui::color::TEXT, fc.fgOf("30%"));
}

// Limits: "at this pace, runs out at HH:MM" in amber under the arc.
static void test_limits_forecast_at() {
  setenv("TZ", "<-03>3", 1);
  tzset();
  ColorCanvas fc({240, 240});
  screens::bind(fc);
  idle();
  screens::reset();
  fc.clearAll();
  screens::limits(Lang::En, snap, clockAt(NOW), NOW + 80 * 60);
  TEST_ASSERT_TRUE(fc.drew("at this pace, runs out at 15:52"));
  TEST_ASSERT_EQUAL_INT(ui::color::AMBER, fc.fgOf("at this pace"));
  TEST_ASSERT_EQUAL_INT(0, fc.outOfBounds);
}

// Second clock: Overview Idle shows "<label> HH:MM" under its footer, as a field (a new minute
// redraws only it); the Desk shows it in the top-right corner.
static void test_second_clock_on_overview_and_desk() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  Pager pager(3, 5000);
  idle();
  screens::setSecondClock("Lisboa", "19:32");
  screens::reset();
  fc.clearLog();
  screens::overview(Lang::En, snap, pager, 0, clockAt(NOW), false, 0);
  TEST_ASSERT_TRUE(fc.drew("Lisboa 19:32"));
  fc.clearLog();
  screens::setSecondClock("Lisboa", "19:33");
  screens::overview(Lang::En, snap, pager, 0, clockAt(NOW), false, 0);
  TEST_ASSERT_TRUE(fc.drew("Lisboa 19:33"));
  TEST_ASSERT_TRUE(fc.calls < 6);
  screens::setSecondClock("", "");
  screens::reset();
  fc.clearLog();
  screens::overview(Lang::En, snap, pager, 0, clockAt(NOW), false, 0);
  TEST_ASSERT_FALSE(fc.drew("19:33"));
  // Desk: label and time, the time a field of its own.
  screens::setSecondClock("Lisboa", "19:32");
  screens::reset();
  fc.clearLog();
  screens::desk(Lang::En, snap, clockAt(NOW), 0);
  TEST_ASSERT_TRUE(fc.drew("Lisboa"));
  TEST_ASSERT_TRUE(fc.drew("19:32"));
  fc.clearLog();
  screens::setSecondClock("Lisboa", "19:33");
  screens::desk(Lang::En, snap, clockAt(NOW), 0);
  TEST_ASSERT_TRUE(fc.drew("19:33"));
  TEST_ASSERT_FALSE(fc.drew("Lisboa"));
  TEST_ASSERT_TRUE(fc.calls < 6);
  screens::setSecondClock("", "");
  screens::reset();
  fc.clearLog();
  screens::desk(Lang::En, snap, clockAt(NOW), 0);
  TEST_ASSERT_FALSE(fc.drew("Lisboa"));
}

// The new lines at their longest, in every language and resolution: second clock "WWWWWWWWWWWW
// 23:59", a 9:59:59 command, the forecasts.
static void test_daily_lines_fit_any_resolution() {
  const ui::ScreenSpec specs[] = {{240, 240}, {320, 240}, {480, 320}, {170, 320}};
  screens::setSecondClock("WWWWWWWWWWWW", "23:59");
  for (const auto& sp : specs) {
    for (uint8_t l = 0; l < (uint8_t)Lang::Count; l++) {
      const Lang L = (Lang)l;
      FakeCanvas fc(sp);
      screens::bind(fc);
      Pager pager(3, 5000);
      idle();
      screens::reset();
      screens::overview(L, snap, pager, 0, clockAt(NOW), false, NOW + 20 * 60);
      screens::reset();
      screens::limits(L, snap, clockAt(NOW), NOW + 26 * 3600);
      screens::reset();
      screens::desk(L, snap, clockAt(NOW), 0, NOW + 20 * 60);
      working();
      snap.sessions[1].ts = NOW - 35999;
      strcpy(snap.sessions[1].det, "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW");
      screens::reset();
      screens::overview(L, snap, pager, 0, clockAt(NOW), false, NOW + 20 * 60);
      TEST_ASSERT_TRUE(fc.drew("9h59"));
      screens::reset();
      screens::sessions(L, snap, pager, 0, clockAt(NOW), false);
      attention();
      screens::reset();
      screens::overview(L, snap, pager, 0, clockAt(NOW), false, NOW + 20 * 60);
      TEST_ASSERT_EQUAL_INT(0, fc.outOfBounds);
    }
  }
  screens::setSecondClock("", "");
}

// ---------------- daily life: the Desk's extras and the cat's mood ----------------

// Playful (a light day): an antic every 20 s instead of 30, still the same antic for the same ms.
static void test_playful_cat_plays_more_often() {
  screens::setCatMood((uint8_t)miblo::CatMood::Playful);
  uint32_t at = 1;
  const screens::RoamAntic a = screens::roamAntic(20000, &at);
  TEST_ASSERT_TRUE(a != screens::RoamAntic::None);
  TEST_ASSERT_EQUAL_UINT32(0, at);
  TEST_ASSERT_TRUE(screens::roamAntic(20000, nullptr) == a);
  TEST_ASSERT_TRUE(screens::roamAntic(40000, nullptr) != screens::RoamAntic::None);
  screens::setCatMood((uint8_t)miblo::CatMood::Normal);
  TEST_ASSERT_TRUE(screens::roamAntic(20000, nullptr) == screens::RoamAntic::None);
  TEST_ASSERT_TRUE(screens::roamAntic(30000, nullptr) != screens::RoamAntic::None);
}

// Tired (8 h of work today): slow, sleepy blinks and a yawn now and then; never on a normal day.
static void test_tired_cat_yawns() {
  for (uint8_t mood : {(uint8_t)miblo::CatMood::Tired, (uint8_t)miblo::CatMood::Normal}) {
    screens::setCatMood(mood);
    bool yawn = false, sleepy = false;
    for (uint32_t ms = 0; ms < 45000; ms += 50) {
      const screens::MascotLook k = screens::deskLook(screens::DeskMood::Calm, true, ms);
      yawn |= (k.extras & screens::kMouthWide) != 0;
      sleepy |= k.eyes == screens::Eyes::Sleepy && ms < 11000;  // kCalm's own doze starts at 11.75 s
    }
    const bool tired = mood == (uint8_t)miblo::CatMood::Tired;
    TEST_ASSERT_EQUAL(tired, yawn);
    TEST_ASSERT_EQUAL(tired, sleepy);
  }
  screens::setCatMood((uint8_t)miblo::CatMood::Normal);
}

// Desk: the countdown line, the settings QR in the corner (in place of the second clock) and
// confetti on the day itself; everything inside the screen at every size, in every language.
static void test_desk_countdown_and_qr() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  idle();
  screens::setSecondClock("Lisboa", "19:32");
  screens::setDeskExtras("release in 3 days", "");
  screens::reset();
  fc.clearLog();
  screens::desk(Lang::En, snap, clockAt(NOW), 0);
  TEST_ASSERT_TRUE(fc.drew("release in 3 days"));
  TEST_ASSERT_TRUE(fc.drew("Lisboa"));
  screens::setDeskExtras("release in 3 days", "http://192.168.100.200/");
  screens::reset();
  fc.clearLog();
  screens::desk(Lang::En, snap, clockAt(NOW), 0);
  TEST_ASSERT_FALSE(fc.drew("Lisboa"));  // the QR wins the corner
  TEST_ASSERT_EQUAL_INT(ui::color::WHITE, fc.colorAt(232, 6));
  // Same frame again: nothing redrawn (the QR is not repainted every frame).
  fc.clearLog();
  screens::desk(Lang::En, snap, clockAt(NOW), 0);
  TEST_ASSERT_EQUAL_INT(0, fc.calls);

  const ui::ScreenSpec specs[] = {{240, 240}, {320, 240}, {480, 320}, {170, 320}};
  for (const auto& sp : specs) {
    for (uint8_t l = 0; l < (uint8_t)Lang::Count; l++) {
      FakeCanvas f(sp);
      screens::bind(f);
      for (const char* qr : {"", "http://192.168.100.200/"}) {
        screens::setSecondClock("WWWWWWWWWWWW", "23:59");
        screens::setDeskExtras("WWWWWWWWWWWWWWWWWWWW in 999 days", qr);
        screens::reset();
        for (uint32_t ms = 0; ms < 60000; ms += 997) screens::desk((Lang)l, snap, clockAt(NOW), ms, NOW + 600);
        screens::setDeskExtras("WWWWWWWWWWWWWWWWWWWW is today!", qr);
        screens::reset();
        for (uint32_t ms = 0; ms < 5000; ms += 97) screens::desk((Lang)l, snap, clockAt(NOW), ms);
      }
      TEST_ASSERT_EQUAL_INT(0, f.outOfBounds);
    }
  }
  screens::setSecondClock("", "");
  screens::setDeskExtras("", "");
}

// The pet's sign: the countdown takes the last task's place, unless a note (a friend, a say)
// is up; long ones wrap onto the second line.
static void test_pet_sign_countdown() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  idle();
  screens::setDeskExtras("release in 3 days", "");
  screens::reset();
  fc.clearLog();
  screens::roam(Lang::En, snap, clockAt(NOW), 0, screens::DeskMood::Calm);
  TEST_ASSERT_TRUE(fc.drew("release in 3 days"));
  TEST_ASSERT_FALSE(fc.drew("infra"));
  screens::reset();
  fc.clearLog();
  screens::roam(Lang::En, snap, clockAt(NOW), 0, screens::DeskMood::Calm, "Hi, Nina!");
  TEST_ASSERT_TRUE(fc.drew("Hi, Nina!"));
  TEST_ASSERT_FALSE(fc.drew("release"));
  screens::setDeskExtras("Trip to Lisbon with the team in 12 days", "");
  screens::reset();
  fc.clearLog();
  screens::roam(Lang::En, snap, clockAt(NOW), 0, screens::DeskMood::Calm);
  TEST_ASSERT_TRUE(fc.drew("in 12 days"));
  const ui::ScreenSpec specs[] = {{240, 240}, {320, 240}, {480, 320}, {170, 320}};
  for (const auto& sp : specs) {
    FakeCanvas f(sp);
    screens::bind(f);
    screens::setDeskExtras("WWWWWWWWWWWWWWWWWWWW in 999 days", "");
    screens::reset();
    for (uint32_t ms = 0; ms < 400000; ms += 997) screens::roam(Lang::En, snap, clockAt(NOW), ms, screens::DeskMood::Calm);
    TEST_ASSERT_EQUAL_INT(0, f.outOfBounds);
  }
  screens::setDeskExtras("", "");
}

// ---------------- integration fixes ----------------

// Meeting mode (screens::setAnonymous): no project name, tool or command on any screen, whatever
// the discreet flag says; the state lines stay ("1 WAITING", "permission", "finished 2m ago").
static void test_anonymous_hides_names_on_every_screen() {
  static const char* const kSecret[] = {"api-server", "infra", "front-app", "docs", "worker", "search",
                                        "Bash", "npm", "Header.tsx", "Grep", "migrate"};
  const ui::ScreenSpec specs[] = {{240, 240}, {170, 320}};
  screens::setAnonymous(true);
  for (const auto& sp : specs) {
    for (uint8_t l = 0; l < (uint8_t)Lang::Count; l++) {
      const Lang L = (Lang)l;
      for (int screen = 0; screen < 9; screen++) {
        FakeCanvas fc(sp);
        screens::bind(fc);
        Pager pager(3, 5000);
        screens::reset();
        switch (screen) {
          case 0: attention(); screens::overview(L, snap, pager, 0, clockAt(NOW), false); break;
          case 1:
            working();
            snap.sessions[1].ts = NOW - 4000;  // a long Bash command
            screens::overview(L, snap, pager, 0, clockAt(NOW), false);
            break;
          case 2: idle(); screens::overview(L, snap, pager, 0, clockAt(NOW), false); break;
          case 3: attention(); screens::sessions(L, snap, pager, 0, clockAt(NOW), false); break;
          case 4: working(); screens::sessions(L, snap, pager, 0, clockAt(NOW), false); break;
          case 5: idle(); screens::summary(L, snap, clockAt(NOW)); break;
          case 6: idle(); screens::limits(L, snap, clockAt(NOW), NOW + 600); break;
          case 7: idle(); screens::desk(L, snap, clockAt(NOW), 0, NOW + 600); break;
          case 8: idle(); screens::roam(L, snap, clockAt(NOW), 0, screens::DeskMood::Calm); break;
        }
        TEST_ASSERT_TRUE(fc.texts.size() > 0);
        for (const char* secret : kSecret) {
          if (fc.drew(secret)) {
            char msg[64];
            snprintf(msg, sizeof(msg), "screen %d lang %d drew %s", screen, (int)l, secret);
            TEST_FAIL_MESSAGE(msg);
          }
        }
        if (screen == 0 && L == Lang::En) TEST_ASSERT_TRUE(fc.drew("2 WAITING"));
        if (screen == 2 && L == Lang::En) TEST_ASSERT_TRUE(fc.drew("finished 2m ago"));
        if (screen == 8 && L == Lang::En) TEST_ASSERT_TRUE(fc.drew("finished 2m ago"));
      }
    }
  }
  screens::setAnonymous(false);
  // Off again: the names are back.
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  Pager pager(3, 5000);
  attention();
  screens::reset();
  screens::overview(Lang::En, snap, pager, 0, clockAt(NOW), false);
  TEST_ASSERT_TRUE(fc.drew("api-server"));
  TEST_ASSERT_TRUE(fc.drew("permission · Bash"));
}

// The Desk's settings QR only when it gets at least 2 px a module (a 1 px QR can't be scanned):
// on the 170x320 panel the second clock keeps the corner.
static void test_desk_qr_needs_two_pixel_modules() {
  idle();
  screens::setSecondClock("Lisboa", "19:32");
  screens::setDeskExtras("", "http://192.168.100.200/");
  {
    FakeCanvas fc({170, 320});
    screens::bind(fc);
    screens::reset();
    screens::desk(Lang::En, snap, clockAt(NOW), 0);
    TEST_ASSERT_TRUE(fc.drew("Lisboa"));
    TEST_ASSERT_TRUE(fc.drew("19:32"));
    TEST_ASSERT_TRUE(fc.colorAt(165, 6) != ui::color::WHITE);  // no quiet zone in the corner
  }
  {
    FakeCanvas fc({240, 240});
    screens::bind(fc);
    screens::reset();
    screens::desk(Lang::En, snap, clockAt(NOW), 0);
    TEST_ASSERT_FALSE(fc.drew("Lisboa"));
    TEST_ASSERT_EQUAL_INT(ui::color::WHITE, fc.colorAt(232, 6));
  }
  screens::setSecondClock("", "");
  screens::setDeskExtras("", "");
}

// The Limits forecast is never cut (the time would go): when "at this pace, runs out at Fri 15:40"
// does not fit, the short "runs out ~Fri 15:40" takes its place. Every language, every resolution,
// today and another day.
static void test_limits_forecast_never_cut() {
  setenv("TZ", "<-03>3", 1);
  tzset();
  const ui::ScreenSpec specs[] = {{240, 240}, {320, 240}, {480, 320}, {170, 320}};
  for (const auto& sp : specs) {
    for (uint8_t l = 0; l < (uint8_t)Lang::Count; l++) {
      for (uint32_t ahead : {80u * 60u, 26u * 3600u}) {
        FakeCanvas fc(sp);
        screens::bind(fc);
        idle();
        screens::reset();
        screens::limits((Lang)l, snap, clockAt(NOW), NOW + ahead);
        const char* hhmm = ahead < 3600 * 2 ? "15:52" : "16:32";
        bool found = false;
        for (size_t i = 0; i < fc.texts.size(); i++) {
          const std::string& tx = fc.texts[i];
          // The forecast: the line with its time (at the end, but in Chinese "按当前速度 15:52 用完").
          if (tx.size() <= 5 || tx.find(hhmm) == std::string::npos) continue;
          found = true;
          char msg[96];
          snprintf(msg, sizeof(msg), "lang %d %dx%d: %s", (int)l, sp.w, sp.h, tx.c_str());
          TEST_ASSERT_FALSE_MESSAGE(fc.cut[i], msg);
        }
        TEST_ASSERT_TRUE(found);
      }
    }
  }
}

// A long command over an hour: "1h02" (the house format for hours), changing once a minute.
static void test_long_command_over_an_hour() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  Pager pager(3, 5000);
  working();
  snap.sessions[1].ts = NOW - 3725;
  screens::reset();
  screens::overview(Lang::En, snap, pager, 0, clockAt(NOW), false, 0);
  TEST_ASSERT_TRUE(fc.drew("1h02"));
  TEST_ASSERT_FALSE(fc.drew("1:02:05"));
  fc.clearLog();
  screens::overview(Lang::En, snap, pager, 0, clockAt(NOW + 1), false, 0);
  TEST_ASSERT_EQUAL_INT(0, (int)fc.texts.size());  // same minute: nothing redrawn
  fc.clearLog();
  screens::overview(Lang::En, snap, pager, 0, clockAt(NOW + 60), false, 0);
  TEST_ASSERT_TRUE(fc.drew("1h03"));
}

// Calls a frame of a visit makes: our cat alone (walking out to a friend's), or host and guest.
static int visitCalls(bool both) {
  FakeCanvas fc({240, 240});
  fc.layerSupported = false;  // one pass, no strips
  screens::bind(fc);
  idle();
  miblo::VisitView v{};
  v.role = both ? miblo::VisitRole::Host : miblo::VisitRole::Visitor;
  strcpy(v.name, "Nina");
  v.mascot = 1;
  v.gift = miblo::Gift::Coffee;
  v.ms = both ? miblo::kVisitArriveMs + 100 : 100;
  screens::reset();
  fc.clearLog();
  screens::visit(Lang::En, snap, clockAt(NOW), v, 0);
  return fc.calls;
}

// Friends' cats don't wear our meeting tie or our tired eye bags (they are ours, not theirs);
// ours come back after the guests are drawn.
static void test_guests_wear_no_tie_or_eye_bags() {
  const int alone = visitCalls(false), both = visitCalls(true);
  screens::setMascotTie(true);
  screens::setCatMood((uint8_t)miblo::CatMood::Tired);
  const int aloneDressed = visitCalls(false), bothDressed = visitCalls(true);
  TEST_ASSERT_TRUE(aloneDressed > alone);
  // Only our cat changed (dressed guests would add as much again, each).
  TEST_ASSERT_TRUE(bothDressed - both <= aloneDressed - alone);
  TEST_ASSERT_TRUE(screens::mascotTie());
  TEST_ASSERT_EQUAL_UINT8((uint8_t)miblo::CatMood::Tired, screens::catMood());
  screens::setMascotTie(false);
  screens::setCatMood((uint8_t)miblo::CatMood::Normal);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_anonymous_hides_names_on_every_screen);
  RUN_TEST(test_desk_qr_needs_two_pixel_modules);
  RUN_TEST(test_limits_forecast_never_cut);
  RUN_TEST(test_long_command_over_an_hour);
  RUN_TEST(test_guests_wear_no_tie_or_eye_bags);
  RUN_TEST(test_playful_cat_plays_more_often);
  RUN_TEST(test_tired_cat_yawns);
  RUN_TEST(test_desk_countdown_and_qr);
  RUN_TEST(test_pet_sign_countdown);
  RUN_TEST(test_second_zone_clock_keeps_tz);
  RUN_TEST(test_long_command_on_overview);
  RUN_TEST(test_overview_forecast_line);
  RUN_TEST(test_limits_forecast_at);
  RUN_TEST(test_second_clock_on_overview_and_desk);
  RUN_TEST(test_daily_lines_fit_any_resolution);
  RUN_TEST(test_warm_color);
  RUN_TEST(test_shift_canvas_warms_every_color);
  RUN_TEST(test_pet_antics_order);
  RUN_TEST(test_mascot_new_poses_stay_in_box);
  RUN_TEST(test_main_screens_fit_any_resolution);
  RUN_TEST(test_overview_attention_content);
  RUN_TEST(test_attention_header_keeps_the_name);
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
  RUN_TEST(test_sessions_mode_is_legible);
  RUN_TEST(test_ticks_update_in_place);
  RUN_TEST(test_paging_uses_layer);
  RUN_TEST(test_pt_br_screens_have_no_english);
  RUN_TEST(test_desk_mood_and_gauges);
  RUN_TEST(test_disconnected_has_mascot_and_info);
  RUN_TEST(test_desk_mascot_redraws_in_strips_without_flashing);
  RUN_TEST(test_burn_rate_lines);
  RUN_TEST(test_limit_reset_and_summary_content);
  RUN_TEST(test_pet_mode_card_and_path);
  RUN_TEST(test_pet_mode_shows_computer_away);
  RUN_TEST(test_floor_sign_keeps_lines_and_icon);
  RUN_TEST(test_pet_scene_quiet_when_not_playful);
  RUN_TEST(test_props_stay_near_their_anchor);
  RUN_TEST(test_pet_antics_stay_on_screen);
  RUN_TEST(test_riff_plays_a_solo);
  RUN_TEST(test_pet_tails_stay_on_screen);
  RUN_TEST(test_visits_stay_on_screen_all_activities);
  RUN_TEST(test_visit_add_item_drops_bad_kinds);
  RUN_TEST(test_update_available_screen);
  return UNITY_END();
}
