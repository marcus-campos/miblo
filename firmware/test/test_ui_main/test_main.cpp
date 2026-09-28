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
static const char* const kEnglishOnly[] = {
    "running", "sessions", "session", "waiting", "wait", "agent", "agents", "task", "tasks", "finished",
    "idle", "week", "limits", "limit", "today", "done", "needs", "you", "all", "asked", "permission",
    "question", "resets", "reset", "in", "editing", "reading", "searching", "fetching", "working",
    "cost", "unavailable", "took", "ago", "no", "connecting", "connected", "disconnected", "updating",
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
  screens::disconnected(L, true, 14, 32, 1, 28, "192.168.0.42", "miblo-4f2a", "4827");
  check("disconnected");
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
  RUN_TEST(test_sessions_mode_is_legible);
  RUN_TEST(test_ticks_update_in_place);
  RUN_TEST(test_paging_uses_layer);
  RUN_TEST(test_pt_br_screens_have_no_english);
  return UNITY_END();
}
