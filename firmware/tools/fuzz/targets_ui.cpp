// Fuzz targets over what draws untrusted text: the real TftCanvas with the gadget's u8g2 fonts
// (on the screenshot tool's framebuffer TFT_eSPI), and the screens fed a fuzzed snapshot.
#include <string>

#include "TFT_eSPI.h"
#include "fonts.h"
#include "fuzz.h"
#include "miblo_cues.h"
#include "miblo_desknotes.h"
#include "miblo_focus.h"
#include "miblo_wellness.h"
#include "miblo_overview.h"
#include "miblo_snapshot.h"
#include "platform/tft_canvas.h"
#include "ui_screens.h"

using namespace miblo;
using fuzz::CStr;
using fuzz::ExactBuf;
using fuzz::Reader;

namespace {

struct Screen {
  TFT_eSPI tft{240, 240};
  TftCanvas canvas{tft, {240, 240}, board::fonts::kStacks};
  Screen() {
    canvas.begin();
    screens::bind(canvas);
  }
};
Screen& screen() {
  static Screen s;
  screens::reset();
  return s;
}

// ============================== canvas text ==============================
// Text of any bytes, any font, any width (truncation with "..." and glyph lookup across stacks).

const char* const kTextSeeds[] = {
    "\x07\x50\x03" "Hello, world",
    "\x02\x20\x01" "h\xc3\xa9llo \xd0\xbf\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82 \xe9\xa1\xb9\xe7\x9b\xae",
    "\x05\x08\x02\xf0\x9f\x98\x80\xf0\x9f\x98\x80\xe2\x80\xa6...",
    "\x06\xff\x01" "0123456789:%+-./",
    "\x03\x01\x02\xff\xfe\xc3\xed\xa0\x80",
    nullptr};

void fuzzCanvasText(const uint8_t* d, size_t n) {
  Reader r(d, n);
  const ui::Font f = (ui::Font)(r.u8() % (uint8_t)ui::Font::Count);
  const int maxW = (int)(int8_t)r.u8() * 3;  // -384 .. 381
  const ui::Align a = (ui::Align)(r.u8() % 3);
  const std::string rest = r.rest();
  CStr s(rest.data(), rest.size());
  Screen& sc = screen();
  const int w = sc.canvas.textWidth(s.s, f);
  FUZZ_CHECK(w >= 0, "negative width");
  sc.canvas.text(120, 120, s.s, f, 0xFFFF, a, maxW);
  sc.canvas.textBox(120, 60, s.s, f, 0xFFFF, 0x0000, a, maxW);
  sc.canvas.text(-50, 300, s.s, f, 0xFFFF, a, 240);
  screens::field(0, 1, 120, 200, s.s, f, 0xFFFF, 0, a, maxW);
  fuzz::reached();
}
FUZZ_REGISTER(canvas_text, fuzzCanvasText, kTextSeeds, nullptr, 400);

// ============================== the cat's sign ==============================
// screens::note() wraps the text over up to 4 lines at the real glyph widths (ui_notes.cpp wrap()).

const char* const kNoteSeeds[] = {
    "\x01\x09" "Back in 5 minutes, getting coffee",
    "\x02\x01\xe9\xa1\xb9\xe7\x9b\xae\xe9\xa1\xb9\xe7\x9b\xae\xe9\xa1\xb9\xe7\x9b\xae\xe9\xa1\xb9\xe7\x9b\xae",
    "\x03\x08" "supercalifragilisticexpialidocious-and-then-some",
    "\x01\x02     spaces     everywhere     ",
    "\x04\x05" "W",
    nullptr};

void fuzzNote(const uint8_t* d, size_t n) {
  Reader r(d, n);
  const NoteKind kind = (NoteKind)(1 + r.u8() % 4);
  const Lang lang = (Lang)(r.u8() % (uint8_t)Lang::Count);
  const std::string rest = r.rest();
  CStr s(rest.data(), rest.size());
  screen();
  const screens::Clock clk{true, "14:32", 1790000000};
  screens::note(lang, kind, s.s, clk, 0);
  screens::note(lang, kind, s.s, clk, 2500);
  screens::fanfare(lang, s.s, 600, 1000);
  screens::flash(lang, AlertKind::Perm, s.s, 300, 1);
  screens::hello(s.s, s.s + strlen(s.s) / 2, true, 400);
  screens::updateAvailable(lang, "1.10.1", s.s, 3);
  screens::paired(lang, s.s, s.s, s.s);
  fuzz::reached();
}
FUZZ_REGISTER(ui_note, fuzzNote, kNoteSeeds, nullptr, 300);

// ============================== screens from a snapshot ==============================
// A fuzzed /api/state body, then every screen that draws a snapshot.

const char* const kSnapSeeds[] = {
    "\x09\x09" R"({"v":1,"seq":7,"now":1790000000,"host":"marcus-mbp","usage":{"h5":{"pct":42,"reset":1790003600,"eta":1790002000},"d7":{"pct":12.5,"reset":1790400000}},"today":{"usd":3.25,"turns":12,"work":5400},"latest":"1.10.1","week":{"work":90000,"turns":300,"usd":40.5,"top":2},"sessions":[{"id":"a1b2c3d4","name":"claude_gadget","st":"running","tool":"Bash","det":"npm test","since":1789999000,"ts":1789999900,"model":"opus","ctx":45,"tok":91000},{"id":"e5f6","name":"other","st":"perm","tool":"Edit","det":"x.js","since":1789999500,"model":"sonnet","ctx":-7,"tok":2147483647}],"more":3,"alerts":[{"id":9,"kind":"perm","sid":"e5f6"}]})",
    "\x08\x01" R"({"v":1,"now":1790000000,"usage":{"h5":{"pct":100,"reset":0},"d7":{"pct":99.9}},"sessions":[{"id":"q","name":"项目项目项目项目项目项目项目","st":"question","tool":"AskUserQuestion","det":"WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW","ctx":32767}]})",
    "\x03\x07" R"({"v":1,"now":4294967295,"today":{"usd":999999999,"turns":65535,"work":4294967295},"week":{"work":4294967295,"turns":65535,"usd":999999999,"top":6},"sessions":[{"id":"1","st":"done","since":4294967295},{"id":"2","st":"idle","since":0},{"id":"3","st":"running","tool":"Bash","ts":1}]})",
    nullptr};

void fuzzScreens(const uint8_t* d, size_t n) {
  Reader r(d, n);
  const Lang lang = (Lang)(r.u8() % (uint8_t)Lang::Count);
  const uint8_t knobs = r.u8();
  const std::string body = r.rest();
  ExactBuf buf(body.data(), body.size());
  static Snapshot s;
  memset(&s, 0, sizeof(s));
  if (parseSnapshot(buf.p, buf.n, s) != ParseResult::Ok) return;
  fuzz::reached();
  screen();
  const screens::Clock clk{(knobs & 1) != 0, "09:05", s.now};
  const bool discreet = (knobs & 2) != 0;
  const uint32_t ms = (uint32_t)knobs * 997u;
  Pager pager(4);
  RunTracker runs;
  runs.observe(s);
  screens::overview(lang, s, pager, ms, clk, discreet, s.h5.eta);
  screen();
  screens::sessions(lang, s, pager, ms, clk, discreet);
  screen();
  screens::limits(lang, s, clk, s.h5.eta);
  if (s.count) {
    screen();
    const int idx = selectHero(s, true);
    screens::hero(lang, s, idx >= 0 ? idx : 0, AlertKind::Question, discreet, clk, runs, (knobs & 4) != 0);
  }
  screen();
  screens::desk(lang, s, clk, ms, s.h5.eta);
  screen();
  screens::summary(lang, s, clk);
  screen();
  screens::limitReset(lang, s, clk, ms);
  screen();
  screens::roam(lang, s, clk, ms * 13, screens::deskMoodFor(s, s.now), (knobs & 8) ? s.host : nullptr);
  screen();
  screens::dayEnd(lang, s, s.host, ms);
  screen();
  screens::weekRecap(lang, s, ms);

  // Daily life: the state every screen reads (meeting mode, the second clock, the desk's
  // countdown and QR, the cat's mood and tie) set from the snapshot's text, then the main screens
  // again and the daily screens and overlays.
  const char* name = s.count ? s.sessions[0].name : s.host;
  screens::setAnonymous((knobs & 16) != 0);
  screens::setSecondClock(s.host, s.latest);
  screens::setDeskExtras(name, s.count > 1 ? s.sessions[1].det : s.latest);
  screens::setCatMood((knobs >> 5) % 3);
  screens::setMascotTie((knobs & 32) != 0);
  screen();
  screens::desk(lang, s, clk, ms, s.h5.eta);
  screen();
  screens::overview(lang, s, pager, ms, clk, discreet, s.h5.eta);
  if (s.count) {
    screen();
    screens::hero(lang, s, 0, AlertKind::Perm, discreet, clk, runs, true);
  }
  screen();
  screens::focus(lang, clk, (FocusPhase)(knobs % 5), (uint8_t)(s.more & 0xFF), (uint8_t)(s.more >> 8), s.todayWorkSec,
                 s.week.workSec, s.now, ms);
  screen();
  screens::nudge(lang, (Nudge)(knobs % 4), ms);
  screen();
  screens::timer(lang, clk, s.todayWorkSec, s.week.workSec, ms);
  screen();
  screens::findMe(lang, name, ms);
  screen();
  screens::cue((CueKind)(knobs % 6), ms);
  screens::stateFrame((FrameColor)(knobs % 3));
  screen();
  screens::fanfare(lang, name, s.todayWorkSec, ms);
  screens::waitingMark(lang, name, (uint8_t)s.more);
  screens::meetingBadge(lang);
  screens::waitingOverlaysDrawn();
  screen();
  screens::passerby(lang, s, clk, ms);
  screens::setAnonymous(false);
  screens::setSecondClock("", "");
  screens::setDeskExtras("", "");
  screens::setCatMood(0);
  screens::setMascotTie(false);
}
FUZZ_REGISTER(screens, fuzzScreens, kSnapSeeds, nullptr, 7000);

}  // namespace
