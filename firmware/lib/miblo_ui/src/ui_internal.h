#pragma once
// Shared by the miblo_ui files (ui_main.cpp and the daily-life screens: ui_alerts, ui_focus,
// ui_dayrhythm, ui_notes, ui_cues, ui_occasions). Internal to miblo_ui: the regions of the main
// screens and the drawing helpers that used to be static in ui_main.cpp.
#include <stddef.h>
#include <stdint.h>

#include "miblo_snapshot.h"
#include "ui_screens.h"
#include "ui_visit_kit.h"  // C(), Align, Font, color

namespace screens {

// Regions (RegionCache ids) of the main screens.
// Values that change in place (clock, timers, countdowns, page) have their own field regions
// (see field()), so a tick never clears the block they sit in.
enum : uint8_t {
  R_HEADER = 0, R_LIMITS = 1, R_WEEK = 2, R_DIVIDER = 3, R_ROW0 = 4, R_BODY = 9, R_FOOT = 10, R_TIME0 = 11,
  R_CLOCK = 14, R_RESET5 = 15, R_RESET7 = 16, R_PAGE = 17, R_BURN = 18
};

constexpr const char kDot[] = " \xC2\xB7 ";  // " · "

// Shapes at (x + Sz(dx), y + Sz(dy)) in 240-grid units, out of line: each one costs a call rather
// than its own scaling code. (Sz is odd, Sz(-v) == -Sz(v), so x - Sz(v) is x + Sz(-v).)
void szRect(int x, int y, int dx, int dy, int w, int h, uint16_t c);
void szRound(int x, int y, int dx, int dy, int w, int h, int r, uint16_t c);
void szDisc(int x, int y, int dx, int dy, int r, uint16_t c);
void szTri(int x, int y, int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c);

// The mascot in its box (`half240` on the 240 grid) in region `id`, only redrawn when the
// expression (or the hat, colour...) changes; composed in strips, never one big layer.
void deskCat(uint8_t id, int cx, int cy, int half240, const MascotLook& k);
// Clock at the right of a header (baseline y), on the header's colour. `parent` = the header
// region's hash, so a redrawn header always gets its clock back.
void clockRight(uint32_t parent, const Clock& clk, int y, uint16_t fg, uint16_t bg);
// Compact limits strip (one row, baseline y) in region `id`: "5h ▓▓░ 30%   7d ▓░ 13%" (or
// today's cost without usage data).
void compactLimits(uint8_t id, Lang lang, const miblo::Snapshot& s, int top, int h, int y, uint16_t bg);
// "Opus · ctx 71% · 412k tok" (missing parts are omitted).
void metaLine(const miblo::SessionRow& r, char* out, size_t cap);
// Seconds the session has been in its state (0 when the time is unknown).
uint32_t sessionSince(const miblo::SessionRow& r, const Clock& clk);
// A session state's colour: amber waiting, green running, blue done, faint idle.
uint16_t stateColor(miblo::SessionState st);
// Confetti along the top and the bottom bands (R_HEADER, R_FOOT), reshuffled when `frame` changes.
void confettiBands(uint32_t frame);

}  // namespace screens
