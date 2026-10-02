#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "miblo_activity.h"
#include "miblo_format.h"
#include "miblo_policy.h"
#include "miblo_rom.h"
#include "ui_internal.h"
#include "ui_screens.h"
#include "ui_visit_kit.h"

namespace screens {

using miblo::AlertKind;
using miblo::hashInt;
using miblo::hashStr;
using miblo::kHashSeed;
using miblo::OverviewKind;
using miblo::S;
using miblo::SessionRow;
using miblo::SessionState;
using miblo::Snapshot;
using ui::Align;
using ui::Font;
namespace color = ui::color;

// Session cards per page, in the Overview (Working / Needs you) and in the Sessions mode.
constexpr uint8_t kRows = 3;

// Regions of this file's daily-life additions (past ui_internal.h's R_*; RegionCache holds 24).
enum : uint8_t {
  R_ZONE = 19,  // the second clock's time (Overview Idle header, Desk corner)
  R_CMD0 = 20,  // a long Bash command's running time, one per card slot (20..22)
  R_QR = 23,    // the Desk's settings QR
};

// The 5h forecast counts as close when the window runs out within this (Overview: amber).
constexpr uint32_t kForecastSoonSec = 30 * 60;
static bool forecastSoon(uint32_t exhaustAt, uint32_t now) {
  return now && exhaustAt > now && exhaustAt - now < kForecastSoonSec;
}

// Shapes (ui_internal.h), out of line on purpose.
__attribute__((noinline)) void szRect(int x, int y, int dx, int dy, int w, int h, uint16_t c) {
  C().fillRect(x + Sz(dx), y + Sz(dy), Sz(w), Sz(h), c);
}
__attribute__((noinline)) void szRound(int x, int y, int dx, int dy, int w, int h, int r, uint16_t c) {
  C().fillRoundRect(x + Sz(dx), y + Sz(dy), Sz(w), Sz(h), Sz(r), c);
}
__attribute__((noinline)) void szDisc(int x, int y, int dx, int dy, int r, uint16_t c) {
  C().fillCircle(x + Sz(dx), y + Sz(dy), Sz(r), c);
}
__attribute__((noinline)) void szTri(int x, int y, int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) {
  C().fillTriangle(x + Sz(x0), y + Sz(y0), x + Sz(x1), y + Sz(y1), x + Sz(x2), y + Sz(y2), c);
}

static uint16_t levelColor(uint8_t pct, uint16_t base) {
  if (pct >= 95) return color::RED;
  if (pct >= 80) return color::AMBER;
  return base;
}

uint16_t stateColor(SessionState st) {
  switch (st) {
    case SessionState::Perm:
    case SessionState::Question: return color::AMBER;
    case SessionState::Running: return color::GREEN;
    case SessionState::Done: return color::BLUE;
    case SessionState::Idle: return color::FAINT;
  }
  return color::FAINT;
}

static bool isPending(SessionState st) { return st == SessionState::Perm || st == SessionState::Question; }

uint32_t sessionSince(const SessionRow& r, const Clock& clk) {
  return (clk.epoch && clk.epoch > r.since) ? clk.epoch - r.since : 0;
}

// Clock at the right of a header (baseline y), on the header's colour. `parent` = the header
// region's hash, so a redrawn header always gets its clock back.
void clockRight(uint32_t parent, const Clock& clk, int y, uint16_t fg, uint16_t bg) {
  field(R_CLOCK, parent, X(228), y, clk.hhmm, Font::Small, fg, bg, Align::Right, X(44));
}

// "Opus · ctx 71% · 412k tok" (missing parts are omitted; tok = the session's context tokens).
void metaLine(const SessionRow& r, char* out, size_t cap) {
  char tmp[48];
  out[0] = 0;
  if (r.model[0]) snprintf(out, cap, "%s", r.model);
  if (r.ctx >= 0) {
    snprintf(tmp, sizeof(tmp), "%sctx %d%%", out[0] ? kDot : "", r.ctx);
    strncat(out, tmp, cap - strlen(out) - 1);
  }
  if (r.tok >= 0) {
    char tk[16];
    miblo::formatTokens((uint64_t)r.tok, tk, sizeof(tk));
    snprintf(tmp, sizeof(tmp), "%s%s tok", out[0] ? kDot : "", tk);
    strncat(out, tmp, cap - strlen(out) - 1);
  }
}

void formatWhen(Lang lang, uint32_t epoch, uint32_t now, char* out, size_t cap) {
  time_t te = (time_t)epoch;
  time_t tn = (time_t)now;
  struct tm a;
  struct tm b;
  localtime_r(&te, &a);
  localtime_r(&tn, &b);
  char hhmm[8];
  miblo::formatHHMM(a.tm_hour, a.tm_min, hhmm, sizeof(hhmm));
  if (a.tm_yday == b.tm_yday && a.tm_year == b.tm_year) {
    snprintf(out, cap, "%s", hhmm);
  } else {
    snprintf(out, cap, "%s %s", t(lang, (S)((int)S::WdSun + a.tm_wday)), hhmm);
  }
}

// ---------------- shared blocks ----------------

// "resets 16:42 · in 2h10"
static void resetLine(Lang lang, const miblo::UsageWindow& w, const Clock& clk, uint32_t fallbackNow, bool withCountdown,
                      char* out, size_t cap) {
  char when[32];
  char a[48];
  const uint32_t now = clk.epoch ? clk.epoch : fallbackNow;
  formatWhen(lang, w.reset, now, when, sizeof(when));
  snprintf(a, sizeof(a), t(lang, S::ResetsAt), when);
  if (!withCountdown) {
    snprintf(out, cap, "%s", a);
    return;
  }
  char left[16];
  char b[32];
  miblo::formatCountdown(w.reset > now ? w.reset - now : 0, left, sizeof(left));
  snprintf(b, sizeof(b), t(lang, S::InTime), left);
  snprintf(out, cap, "%s%s%s", a, kDot, b);
}

// Today's cost when there are no limits (account without a subscription), or "limits unavailable".
static void noLimits(Lang lang, const Snapshot& s, int cy) {
  if (s.todayUsd > 0.0f) {
    char usd[16];
    char buf[64];
    miblo::formatUsd(s.todayUsd, usd, sizeof(usd));
    snprintf(buf, sizeof(buf), t(lang, S::CostToday), usd);
    C().text(X(120), cy, buf, Font::Title, color::TEXT, Align::Center, X(232));
    C().text(X(120), cy + Y(26), t(lang, S::LimitsUnavailable), Font::Small, color::DIM, Align::Center, X(232));
  } else {
    C().text(X(120), cy, t(lang, S::LimitsUnavailable), Font::Body, color::MUTED, Align::Center, X(232));
  }
}

// Big limits in the Overview: y 26..146 of the grid.
// The reset lines ("resets 16:42 · in 2h10") are fields: the countdown ticks once a minute
// without redrawing the numbers and bars.
// `soon`: the 5h window runs out within kForecastSoonSec at the current pace (exhaustAt): its
// number turns amber and its reset line becomes "runs out ~15:40".
static void limitsBlock(Lang lang, const Snapshot& s, const Clock& clk, uint32_t exhaustAt) {
  const bool soon = s.hasUsage && s.h5.present && forecastSoon(exhaustAt, clk.epoch);
  uint32_t h = hashInt(hashInt(hashInt(kHashSeed, (uint32_t)lang), s.hasUsage), soon);
  h = hashInt(hashInt(h, s.h5.present ? s.h5.pct : 255), s.h5.reset != 0);
  h = hashInt(hashInt(hashInt(h, s.d7.present ? s.d7.pct : 255), s.d7.reset != 0), (uint32_t)(s.todayUsd * 100));
  char buf[96];
  Compose block;
  if (block.begin(R_LIMITS, h, 0, Y(26), X(240), Y(122))) {
    if (!s.hasUsage) {
      noLimits(lang, s, Y(84));
    } else {
      C().text(X(12), Y(52), t(lang, S::Session5h), Font::Body, color::MUTED, Align::Left, X(140));
      if (s.h5.present) {
        snprintf(buf, sizeof(buf), "%u%%", s.h5.pct);
        C().text(X(228), Y(58), buf, Font::NumL, soon ? color::AMBER : color::TEXT, Align::Right, X(100));
        bar(X(12), Y(64), X(216), Y(10), s.h5.pct, levelColor(s.h5.pct, color::CORAL));
      } else {
        C().text(X(228), Y(58), "--", Font::NumM, color::DIM, Align::Right, X(100));
      }
      C().text(X(12), Y(116), t(lang, S::Week), Font::Body, color::MUTED, Align::Left, X(140));
      if (s.d7.present) {
        snprintf(buf, sizeof(buf), "%u%%", s.d7.pct);
        C().text(X(228), Y(118), buf, Font::NumM, color::TEXT, Align::Right, X(100));
        bar(X(12), Y(124), X(216), Y(6), s.d7.pct, levelColor(s.d7.pct, color::VIOLET));
      } else {
        C().text(X(228), Y(118), "--", Font::NumM, color::DIM, Align::Right, X(100));
      }
    }
  }
  block.end();
  if (!s.hasUsage) return;
  if (soon) {
    char when[32];
    formatWhen(lang, exhaustAt, clk.epoch, when, sizeof(when));
    snprintf(buf, sizeof(buf), t(lang, S::RunsOutShort), when);
    field(R_RESET5, h, X(12), Y(90), buf, Font::Small, color::AMBER, color::BG, Align::Left, X(216));
  } else if (s.h5.present && s.h5.reset) {  // 0 = unknown (the plugin sent reset:null): no line
    resetLine(lang, s.h5, clk, s.now, true, buf, sizeof(buf));
    field(R_RESET5, h, X(12), Y(90), buf, Font::Small, color::DIM, color::BG, Align::Left, X(216));
  }
  if (s.d7.present && s.d7.reset) {
    resetLine(lang, s.d7, clk, s.now, false, buf, sizeof(buf));
    field(R_RESET7, h, X(12), Y(144), buf, Font::Small, color::DIM, color::BG, Align::Left, X(216));
  }
}

// Compact limits strip (one row, baseline y): "5h ▓▓░ 30%   7d ▓░ 13%". Without usage data
// (account without a subscription) it shows today's cost instead. Drawn in region `id`.
// `soon5h`: the 5h window runs out soon at the current pace: its number in amber.
static void limitsStrip(uint8_t id, Lang lang, const Snapshot& s, int top, int h, int y, uint16_t bg, bool soon5h) {
  uint32_t hs = hashInt(hashInt(hashInt(kHashSeed + 3, (uint32_t)lang), s.hasUsage), soon5h);
  hs = hashInt(hashInt(hs, s.h5.present ? s.h5.pct : 255), s.d7.present ? s.d7.pct : 255);
  hs = hashInt(hashInt(hs, (uint32_t)(s.todayUsd * 100)), (uint32_t)top);
  Compose strip;
  if (!strip.begin(id, hs, 0, top, X(240), h, bg)) return;
  char buf[64];
  if (!s.hasUsage) {
    if (s.todayUsd > 0.0f) {
      char usd[16];
      miblo::formatUsd(s.todayUsd, usd, sizeof(usd));
      snprintf(buf, sizeof(buf), t(lang, S::CostToday), usd);
      C().text(X(10), y, buf, Font::SmallBold, color::TEXT, Align::Left, X(220));
    } else {
      C().text(X(10), y, t(lang, S::LimitsUnavailable), Font::Small, color::DIM, Align::Left, X(220));
    }
    return;
  }
  struct Win {
    const char* label;
    const miblo::UsageWindow* w;
    uint16_t base;
    int x;
    bool soon;
  };
  const Win wins[2] = {{t(lang, S::Short5h), &s.h5, color::CORAL, X(10), soon5h},
                       {t(lang, S::Short7d), &s.d7, color::VIOLET, X(124), false}};
  const int barH = Y(5) < 2 ? 2 : Y(5);
  for (const Win& win : wins) {
    C().text(win.x, y, win.label, Font::Small, color::MUTED, Align::Left, X(20));
    const bool present = win.w->present;
    const uint8_t pct = present ? win.w->pct : 0;
    bar(win.x + X(20), y - Y(8), X(48), barH, pct, levelColor(pct, win.base));
    if (present) snprintf(buf, sizeof(buf), "%u%%", pct);
    else snprintf(buf, sizeof(buf), "--");
    const uint16_t fg = !present ? color::DIM : win.soon && pct < 95 ? color::AMBER : levelColor(pct, color::TEXT);
    C().text(win.x + X(72), y, buf, Font::SmallBold, fg, Align::Left, X(34));
  }
}

void compactLimits(uint8_t id, Lang lang, const Snapshot& s, int top, int h, int y, uint16_t bg) {
  limitsStrip(id, lang, s, top, h, y, bg, false);
}

// Geometry of the session rows (Overview Working / Needs you, Sessions mode), in pixels.
struct RowGeom {
  int top;    // y of the first row
  int pitch;  // row height (card + gap)
  int l1;     // baseline of line 1, relative to the row top
  int l2;     // baseline of line 2
};

// One session card: line 1 = state dot + name (BodyBold) + time in state (right);
// line 2 = what it is doing (sessionLine: "Editing Header.tsx", "Bash · npm test", "finished"),
// Small, cut with "..." by the canvas. Pending cards are amber.
// A changed card (paging, new state/activity) is composed off-screen and pushed in one go; the
// time in state ("3m", "1h12") changes at most once a minute and is updated in place.
// A Bash command running for over 30 s: line 2 is the command and its running time beside it,
// big enough to read from afar ("npm test · 1:42", the time bold and green); the time is a field
// of its own, so the card is not redrawn every second.
static void sessionRow(uint8_t slot, Lang lang, const SessionRow* r, const Clock& clk, bool discreet,
                       const RowGeom& g) {
  const int y0 = g.top + slot * g.pitch;
  char line[160];
  char timeStr[16];
  char cmdStr[16];
  line[0] = 0;
  timeStr[0] = 0;
  cmdStr[0] = 0;
  if (r) {
    miblo::sessionLine(lang, *r, discreet, line, sizeof(line));
    miblo::formatInState(sessionSince(*r, clk), timeStr, sizeof(timeStr));
    const uint32_t cmdSec = miblo::longCommandSec(*r, clk.epoch);
    if (cmdSec) {
      miblo::formatMinSec(cmdSec, cmdStr, sizeof(cmdStr));
      if (!discreet && r->det[0]) snprintf(line, sizeof(line), "%s", r->det);  // the time stands for "Bash"
    }
  }
  const bool pending = r && isPending(r->st);
  const uint16_t cardBg = r ? (pending ? color::CARD_AMBER : color::CARD) : color::BG;
  const uint16_t timeFg = pending ? color::AMBER : color::DIM;
  const int timeX = X(226);
  const int timeW = X(62);
  // The command's time: right after the command (cut first so the time always fits) and a dot.
  const int cmdW = X(48);
  int lineW = X(208), cmdX = 0;
  if (cmdStr[0]) {
    const int dotW = C().textWidth(kDot, Font::Small);
    lineW = X(208) - cmdW - dotW;
    const int w = C().textWidth(line, Font::Small);
    cmdX = X(16) + (w < lineW ? w : lineW) + dotW;
  }
  uint32_t h = hashInt(hashInt(kHashSeed + 5, (uint32_t)g.top), (uint32_t)g.pitch);
  if (r) h = hashInt(hashStr(hashStr(hashInt(h, (uint32_t)r->st), r->name), line), (uint32_t)cmdX);
  Compose row;
  if (row.begin(R_ROW0 + slot, h, 0, y0, X(240), g.pitch)) {
    (void)dirty(R_TIME0 + slot, hashStr(h, timeStr));  // the times are drawn with the card
    (void)dirty(R_CMD0 + slot, hashStr(h, cmdStr));
    if (r) {
      const uint16_t sc = stateColor(r->st);
      const uint16_t lineFg = r->st == SessionState::Running ? color::MUTED : sc;
      C().fillRect(X(8), y0 + Y(2), X(224), g.pitch - Y(4), cardBg);
      C().fillRect(X(8), y0 + Y(2), Sz(3), g.pitch - Y(4), sc);
      C().fillCircle(X(20), y0 + g.l1 - Y(5), Sz(4), sc);
      C().text(X(30), y0 + g.l1, r->name, Font::BodyBold, pending ? color::AMBER : color::TEXT, Align::Left,
               X(130));
      const int w = C().text(X(16), y0 + g.l2, line, Font::Small, lineFg, Align::Left, lineW);
      if (cmdStr[0]) {
        C().text(X(16) + w, y0 + g.l2, kDot, Font::Small, lineFg, Align::Left, cmdX - X(16) - w);
        C().textBox(cmdX, y0 + g.l2, cmdStr, Font::SmallBold, color::GREEN, cardBg, Align::Left, cmdW);
      }
      C().textBox(timeX, y0 + g.l1, timeStr, Font::Small, timeFg, cardBg, Align::Right, timeW);
    }
    return;
  }
  if (r) field(R_TIME0 + slot, h, timeX, y0 + g.l1, timeStr, Font::Small, timeFg, cardBg, Align::Right, timeW);
  if (cmdStr[0]) field(R_CMD0 + slot, h, cmdX, y0 + g.l2, cmdStr, Font::SmallBold, color::GREEN, cardBg, Align::Left, cmdW);
}

// Rows of one page: slot i shows session page * per + i (per <= kRows).
static void sessionRows(Lang lang, const Snapshot& s, uint8_t page, uint8_t per, const Clock& clk, bool discreet,
                        const RowGeom& g) {
  for (uint8_t i = 0; i < kRows; i++) {
    const int idx = page * per + i;
    sessionRow(i, lang, i < per && idx < s.count ? &s.sessions[idx] : nullptr, clk, discreet, g);
  }
}

// ---------------- Adaptive overview ----------------
//   Needs you: amber band + compact limits strip + session cards (pending first).
//   Working:   compact limits strip + session cards + footer (N running, page, clock).
//   Idle:      big 5h/week limits + last finished session + today's cost.

static void overviewIdle(Lang lang, const Snapshot& s, const Clock& clk, uint32_t exhaustAt) {
  char buf[128];
  char tmp[48];
  const uint32_t hh = hashInt(kHashSeed + 7, (uint32_t)lang);
  if (region(R_HEADER, hh, 0, 0, X(240), Y(26))) {
    check(X(17), Y(12), Sz(12), color::BLUE);
    C().text(X(28), Y(18), t(lang, S::AllDone), Font::SmallBold, color::BLUE, Align::Left, X(150));
  }
  clockRight(hh, clk, Y(18), color::DIM, color::BG);
  limitsBlock(lang, s, clk, exhaustAt);
  if (region(R_DIVIDER, 1, 0, Y(150), X(240), 2)) C().fillRect(X(12), Y(150), X(216), 1, color::DIVIDER);

  // footer: most recently finished session + today's cost (updated in place)
  const int last = miblo::lastFinished(s);
  buf[0] = 0;
  if (last >= 0) {
    miblo::formatInState(sessionSince(s.sessions[last], clk), tmp, sizeof(tmp));
    snprintf(buf, sizeof(buf), t(lang, S::FinishedAgo), s.sessions[last].name, tmp);
  }
  field(R_ROW0, kHashSeed, X(12), Y(168), buf, Font::Small, color::MUTED, color::BG, Align::Left, X(216));
  buf[0] = 0;
  if (s.todayUsd > 0.0f) {
    miblo::formatUsd(s.todayUsd, tmp, sizeof(tmp));
    snprintf(buf, sizeof(buf), t(lang, S::CostToday), tmp);
  }
  field(R_ROW0 + 1, kHashSeed, X(12), Y(186), buf, Font::Small, color::DIM, color::BG, Align::Left, X(216));
  // The second clock ("Lisboa 19:32"), on a line of its own under the footer: the header has no
  // room for it next to the title in most languages.
  buf[0] = 0;
  if (secondClockLabel()[0] && secondClockTime()[0]) {
    snprintf(buf, sizeof(buf), "%s %s", secondClockLabel(), secondClockTime());
  }
  field(R_ZONE, kHashSeed, X(12), Y(204), buf, Font::Small, color::DIM, color::BG, Align::Left, X(216));
}

void overview(Lang lang, const Snapshot& s, miblo::Pager& pager, uint32_t nowMs, const Clock& clk, bool discreet,
              uint32_t exhaustAt) {
  const OverviewKind kind = miblo::classifyOverview(s);
  // The three layouts tile the screen differently: switching layout clears everything.
  static bool haveKind = false;
  static OverviewKind lastKind = OverviewKind::Idle;
  if (haveKind && kind != lastKind) reset();
  haveKind = true;
  lastKind = kind;
  if (kind == OverviewKind::Idle) {
    overviewIdle(lang, s, clk, exhaustAt);
    return;
  }

  const miblo::StateCounts c = miblo::countStates(s);
  const bool soon = s.hasUsage && s.h5.present && forecastSoon(exhaustAt, clk.epoch);
  char buf[128];
  char tmp[48];
  RowGeom g;
  if (kind == OverviewKind::Attention) {
    const int heroIdx = miblo::selectHero(s, false);
    const char* heroName = heroIdx >= 0 ? s.sessions[heroIdx].name : "";
    const uint32_t h = hashStr(hashInt(hashInt(kHashSeed + 11, c.pending), (uint32_t)lang), heroName);
    Compose head;
    if (head.begin(R_HEADER, h, 0, 0, X(240), Y(24))) {
      // fixed amber band: "1 WAITING · api-server"
      C().fillRect(0, 0, X(240), Y(22), color::AMBER);
      snprintf(tmp, sizeof(tmp), t(lang, S::NWaiting), (unsigned)c.pending);
      snprintf(buf, sizeof(buf), "%s%s%s", tmp, kDot, heroName);
      C().text(X(10), Y(16), buf, Font::SmallBold, color::BLACK, Align::Left, X(170));
    }
    head.end();
    clockRight(h, clk, Y(16), color::BLACK, color::AMBER);
    limitsStrip(R_LIMITS, lang, s, Y(24), Y(22), Y(40), color::BG, soon);
    g = {Y(46), Y(52), Y(20), Y(40)};
  } else {
    // Brand row (logo, "miblo", clock), then the limits strip, then the cards.
    const uint32_t hb = hashInt(kHashSeed + 41, 1);
    if (region(R_HEADER, hb, 0, 0, X(240), Y(24))) {
      logo(X(19), Y(12), Sz(22));
      C().text(X(35), Y(21), "miblo", Font::Brand, color::TEXT, Align::Left, X(120));
    }
    clockRight(hb, clk, Y(18), color::DIM, color::BG);
    limitsStrip(R_LIMITS, lang, s, Y(24), Y(22), Y(40), color::BG, soon);
    g = {Y(46), Y(52), Y(20), Y(40)};
  }

  const uint8_t per = pager.perPage() < kRows ? pager.perPage() : kRows;
  const uint8_t page = pager.update(s.count, nowMs);
  const uint8_t pages = pager.pageCount(s.count);
  sessionRows(lang, s, page, per, clk, discreet, g);

  // footer: "2 RUNNING · 1/2 · +3"
  const bool working = kind == OverviewKind::Working;
  const int footTop = g.top + kRows * g.pitch;
  const int fy = footTop + (Y(240) - footTop) / 2 + Y(5);
  buf[0] = 0;
  if (working) snprintf(buf, sizeof(buf), t(lang, S::NRunning), (unsigned)c.running);
  if (pages > 1) {
    snprintf(tmp, sizeof(tmp), "%s%u/%u", buf[0] ? kDot : "", (unsigned)page + 1, (unsigned)pages);
    strncat(buf, tmp, sizeof(buf) - strlen(buf) - 1);
  }
  if (s.more) {
    snprintf(tmp, sizeof(tmp), "%s+%u", buf[0] ? kDot : "", (unsigned)s.more);
    strncat(buf, tmp, sizeof(buf) - strlen(buf) - 1);
  }
  const uint32_t hf = hashStr(hashInt(kHashSeed + 13, working), buf);
  Compose foot;
  if (foot.begin(R_FOOT, hf, 0, footTop, X(240), Y(240) - footTop)) {
    int x = X(12);
    if (working) {
      C().fillCircle(X(16), fy - Y(5), Sz(4), color::GREEN);
      x = X(26);
    }
    C().text(x, fy, buf, Font::SmallBold, working ? color::GREEN : color::DIM, Align::Left, X(150));
  }
  foot.end();
  // (the clock is in the brand row while working, in the amber band while something waits)
}

// ---------------- Limits mode ----------------

void limits(Lang lang, const Snapshot& s, const Clock& clk, uint32_t exhaustAt) {
  char buf[96];
  const uint32_t hh = hashInt(kHashSeed, (uint32_t)lang);
  if (region(R_HEADER, hh, 0, 0, X(240), Y(24))) {
    C().text(X(12), Y(18), t(lang, S::LimitsTitle), Font::SmallBold, color::DIM, Align::Left, X(150));
  }
  clockRight(hh, clk, Y(18), color::DIM, color::BG);
  // The arc is anti-aliased (no layer): it is redrawn only when the percentage changes; the
  // countdown under it is a field.
  uint32_t h = hashInt(hashInt(hashInt(kHashSeed, s.hasUsage), s.h5.present ? s.h5.pct : 255), s.h5.reset != 0);
  h = hashInt(hashInt(h, (uint32_t)lang), (uint32_t)(s.todayUsd * 100));
  const int cx = X(120);
  const int ir = Sz(64);
  if (region(R_LIMITS, h, 0, Y(24), X(240), Y(150))) {
    if (!s.hasUsage || !s.h5.present) {
      noLimits(lang, s, Y(104));
    } else {
      const uint8_t pct = s.h5.pct;
      const int cy = Y(104);
      const int r = Sz(78);
      // 270-degree arc with the gap at the bottom
      C().arc(cx, cy, r, ir, 45, 315, color::TRACK, color::BG);
      if (pct > 0) {
        int end = 45 + 270 * pct / 100;
        if (end <= 45) end = 46;
        C().arc(cx, cy, r, ir, 45, end, levelColor(pct, color::CORAL), color::BG);
      }
      snprintf(buf, sizeof(buf), "%u%%", pct);
      C().text(cx, Y(112), buf, Font::NumL, color::TEXT, Align::Center, 2 * ir);
      C().text(cx, Y(134), t(lang, S::Session5h), Font::Small, color::MUTED, Align::Center, 2 * ir);
    }
  }
  if (s.hasUsage && s.h5.present && s.h5.reset) {  // 0 = unknown: no countdown
    char left[16];
    miblo::formatCountdown(s.h5.reset > clk.epoch ? s.h5.reset - clk.epoch : 0, left, sizeof(left));
    snprintf(buf, sizeof(buf), t(lang, S::InTime), left);
    // Narrower than the ring: the box is painted and must stay clear of the arc's round ends.
    field(R_RESET5, h, cx, Y(152), buf, Font::Small, color::DIM, color::BG, Align::Center, Sz(84));
  }
  // At the current pace it runs out before it resets: "at this pace, runs out at 15:40", in amber,
  // under the arc's ends (clear of them: the whole width).
  buf[0] = 0;
  if (s.hasUsage && s.h5.present && exhaustAt > clk.epoch && clk.epoch) {
    char when[32];
    formatWhen(lang, exhaustAt, clk.epoch, when, sizeof(when));
    snprintf(buf, sizeof(buf), t(lang, S::RunsOutAt), when);
  }
  field(R_BURN, h, cx, Y(174), buf, Font::Small, color::AMBER, color::BG, Align::Center, X(228));
  h = hashInt(hashInt(kHashSeed, s.d7.present ? s.d7.pct : 255), (uint32_t)lang);
  const bool week = s.hasUsage && s.d7.present;
  Compose wk;
  if (wk.begin(R_WEEK, hashInt(h, week), 0, Y(176), X(240), Y(40)) && week) {
    C().text(X(12), Y(192), t(lang, S::Week), Font::Small, color::MUTED, Align::Left, X(100));
    bar(X(12), Y(200), X(216), Y(6), s.d7.pct, levelColor(s.d7.pct, color::VIOLET));
  }
  wk.end();
  if (week) {  // "38% · Thu 09:00" (the day word depends on today): a field
    if (s.d7.reset) {
      char when[32];
      formatWhen(lang, s.d7.reset, clk.epoch ? clk.epoch : s.now, when, sizeof(when));
      snprintf(buf, sizeof(buf), "%u%%%s%s", s.d7.pct, kDot, when);
    } else {
      snprintf(buf, sizeof(buf), "%u%%", s.d7.pct);  // reset unknown: percentage only
    }
    field(R_RESET7, hashInt(h, week), X(228), Y(192), buf, Font::Small, color::MUTED, color::BG, Align::Right,
          X(116));
  }
}

// ---------------- Sessions mode ----------------
// Up to three big cards per page (the same card as the Overview, with more air), paging every
// 5 s; the header shows the total and the page ("1/2").

void sessions(Lang lang, const Snapshot& s, miblo::Pager& pager, uint32_t nowMs, const Clock& clk, bool discreet) {
  char buf[160];
  char tmp[48];
  const uint8_t per = pager.perPage() < kRows ? pager.perPage() : kRows;
  const uint8_t page = pager.update(s.count, nowMs);
  const uint8_t pages = pager.pageCount(s.count);
  const unsigned total = (unsigned)s.count + s.more;
  const uint32_t hh = hashInt(hashInt(kHashSeed, total), (uint32_t)lang);
  Compose head;
  if (head.begin(R_HEADER, hh, 0, 0, X(240), Y(24))) {
    snprintf(buf, sizeof(buf), t(lang, S::SessionsTitle), total);
    C().text(X(10), Y(18), buf, Font::SmallBold, color::DIM, Align::Left, X(170));
  }
  head.end();
  snprintf(tmp, sizeof(tmp), "%u/%u", (unsigned)page + 1, (unsigned)pages);
  field(R_PAGE, hh, X(230), Y(18), tmp, Font::Small, color::DIM, color::BG, Align::Right, X(50));
  if (s.count == 0) {
    if (region(R_ROW0, hashInt(kHashSeed + 9, (uint32_t)lang), 0, Y(24), X(240), Y(216))) {
      C().text(X(120), Y(130), t(lang, S::NoSessions), Font::Body, color::MUTED, Align::Center, X(232));
    }
    for (uint8_t i = 1; i < kRows; i++) (void)dirty(R_ROW0 + i, 0xFFFFFFFFu);  // force a redraw later
    return;
  }
  sessionRows(lang, s, page, per, clk, discreet, {Y(26), Y(70), Y(28), Y(52)});
}

// ---------------- Desk and Disconnected (the mascot) ----------------
// The mascot plays a short looped choreography picked by its mood; each step is one
// expression held for `ms`. Gaze is symbolic: Focus = towards the gauge that worries it.

namespace {
enum class Gaze : uint8_t { Front, Focus, Other, Up };
enum : uint8_t { P_DOWN = 0, P_REACH = 1, P_COVER = 2 };
struct Step {
  uint16_t ms;
  int8_t dx;
  int8_t dy;
  Gaze gaze;
  Eyes eyes;
  uint8_t paws;
  uint16_t extras;  // MascotLook::extras' flags (some past 8 bits)
};
constexpr Gaze F = Gaze::Front, FO = Gaze::Focus, OT = Gaze::Other, UP = Gaze::Up;
constexpr Eyes O = Eyes::Open, CL = Eyes::Closed, W = Eyes::Wide, SL = Eyes::Sleepy, HA = Eyes::Happy;

// Under half of the limits used: idles, checks both gauges, hops, dozes off for a bit.
const Step kCalm[] MIBLO_ROM = {
    {2200, 0, 0, F, O, P_DOWN, 0},   {150, 0, 0, F, CL, P_DOWN, 0},    {1400, 0, 0, F, O, P_DOWN, 0},
    {1600, 0, 0, FO, O, P_DOWN, 0},  {1600, 0, 0, OT, O, P_DOWN, 0},   {600, 0, 0, F, O, P_DOWN, 0},
    {250, 0, -4, F, O, P_DOWN, 0},   {250, 0, 0, F, O, P_DOWN, 0},     {250, 0, -4, F, O, P_DOWN, 0},
    {1800, 0, 0, F, O, P_DOWN, 0},   {150, 0, 0, F, CL, P_DOWN, 0},    {1500, 0, 0, UP, O, P_DOWN, 0},
    {2500, 0, 0, F, SL, P_DOWN, 0},  {1200, 0, 0, F, CL, P_DOWN, kZ1}, {1200, 0, 0, F, CL, P_DOWN, kZ1 | kZ2},
    {1200, 0, 0, F, CL, P_DOWN, kZ1}, {1200, 0, 0, F, CL, P_DOWN, kZ1 | kZ2}, {800, 0, 0, F, SL, P_DOWN, 0},
    {150, 0, 0, F, CL, P_DOWN, 0},   {1500, 0, 0, F, O, P_DOWN, 0},
};
// 50-79%: keeps an eye on the fuller gauge and bats at it.
const Step kWatchful[] MIBLO_ROM = {
    {1500, 0, 0, F, O, P_DOWN, 0},   {2000, 0, 0, FO, O, P_DOWN, 0},  {150, 0, 0, FO, CL, P_DOWN, 0},
    {1200, 0, 0, FO, O, P_DOWN, 0},  {400, 0, 0, FO, O, P_REACH, 0},  {300, 0, 0, FO, O, P_DOWN, 0},
    {400, 0, 0, FO, O, P_REACH, 0},  {300, 0, 0, FO, O, P_DOWN, 0},   {1500, 0, 0, OT, O, P_DOWN, 0},
    {1800, 0, 0, FO, O, P_DOWN, 0},  {150, 0, 0, F, CL, P_DOWN, 0},   {1500, 0, 0, F, O, P_DOWN, 0},
    {250, 0, -4, F, O, P_DOWN, 0},   {250, 0, 0, F, O, P_DOWN, 0},
};
// 80-94%: wide eyes on the gauge, sweating, a nervous shiver.
const Step kWorried[] MIBLO_ROM = {
    {1200, 0, 0, FO, W, P_DOWN, 0},      {1500, 0, 0, FO, W, P_DOWN, kSweat}, {120, 0, 0, F, CL, P_DOWN, kSweat},
    {800, 0, 0, F, O, P_DOWN, kSweat},   {1400, 0, 0, FO, W, P_DOWN, kSweat}, {120, -2, 0, FO, W, P_DOWN, kSweat},
    {120, 2, 0, FO, W, P_DOWN, kSweat},  {120, -2, 0, FO, W, P_DOWN, kSweat}, {120, 2, 0, FO, W, P_DOWN, kSweat},
    {1200, 0, 0, OT, O, P_DOWN, 0},      {1500, 0, 0, FO, W, P_DOWN, kSweat}, {150, 0, 0, F, CL, P_DOWN, 0},
    {1000, 0, 0, F, O, P_DOWN, 0},
};
// 95% and up: alarmed, jumps, shivers, covers its eyes and peeks.
const Step kScared[] MIBLO_ROM = {
    {800, 0, 0, FO, W, P_DOWN, kAlarm | kMouthO},   {200, 0, -5, FO, W, P_DOWN, kAlarm | kMouthO},
    {200, 0, 0, FO, W, P_DOWN, kAlarm | kMouthO},   {200, 0, -5, FO, W, P_DOWN, kAlarm | kMouthO},
    {200, 0, 0, FO, W, P_DOWN, kAlarm | kMouthO},   {100, -2, 0, FO, W, P_DOWN, kAlarm | kSweat},
    {100, 2, 0, FO, W, P_DOWN, kAlarm | kSweat},    {100, -2, 0, FO, W, P_DOWN, kAlarm | kSweat},
    {100, 2, 0, FO, W, P_DOWN, kAlarm | kSweat},    {100, -2, 0, FO, W, P_DOWN, kAlarm | kSweat},
    {100, 2, 0, FO, W, P_DOWN, kAlarm | kSweat},    {1800, 0, 0, F, O, P_COVER, kSweat},
    {400, 0, 0, FO, W, P_DOWN, kSweat},             {1200, 0, 0, F, O, P_COVER, kSweat},
    {1000, 0, 0, FO, W, P_DOWN, kSweat | kMouthO},  {150, 0, 0, F, CL, P_DOWN, kSweat},
};
// Disconnected: looks left and right for the computer, up, sighs.
const Step kSearching[] MIBLO_ROM = {
    {1500, 0, 0, F, O, P_DOWN, 0},   {1200, 0, 0, FO, O, P_DOWN, 0},  {1200, 0, 0, OT, O, P_DOWN, 0},
    {150, 0, 0, F, CL, P_DOWN, 0},   {1000, 0, 0, UP, O, P_DOWN, 0},  {1200, 0, 0, F, O, P_DOWN, 0},
    {800, 0, 0, FO, O, P_DOWN, 0},   {800, 0, 0, OT, O, P_DOWN, 0},   {250, 0, -4, F, O, P_DOWN, 0},
    {250, 0, 0, F, O, P_DOWN, 0},    {2000, 0, 0, F, SL, P_DOWN, 0},  {150, 0, 0, F, CL, P_DOWN, 0},
    {1200, 0, 0, F, O, P_DOWN, 0},
};
// Disconnected for long: asleep, now and then half-opening an eye.
const Step kAsleep[] MIBLO_ROM = {
    {1400, 0, 0, F, CL, P_DOWN, kZ1}, {1400, 0, 0, F, CL, P_DOWN, kZ1 | kZ2}, {1400, 0, 0, F, CL, P_DOWN, kZ1},
    {1400, 0, 0, F, CL, P_DOWN, kZ1 | kZ2}, {1400, 0, 0, F, CL, P_DOWN, kZ1}, {1400, 0, 0, F, CL, P_DOWN, kZ1 | kZ2},
    {1200, 0, 0, UP, SL, P_DOWN, 0},  {1400, 0, 0, F, CL, P_DOWN, 0},
};

// The tables live in flash (MIBLO_ROM): each step is copied out before use.
// The 5h window just reset: happy hops, a look up, a cheer.
const Step kCelebrate[] MIBLO_ROM = {
    {250, 0, -5, F, HA, P_DOWN, 0},  {250, 0, 0, F, HA, P_DOWN, 0},  {250, 0, -5, F, HA, P_DOWN, 0},
    {250, 0, 0, F, HA, P_DOWN, 0},   {600, 0, 0, UP, O, P_DOWN, 0},  {250, 0, -5, F, HA, P_DOWN, kMouthO},
    {250, 0, 0, F, HA, P_DOWN, kMouthO}, {250, 0, -5, F, HA, P_DOWN, kMouthO}, {700, 0, 0, F, HA, P_DOWN, 0},
};

template <size_t N>
Step stepAt(const Step (&seq)[N], uint32_t ms) {
  Step st;
  uint32_t total = 0;
  for (size_t i = 0; i < N; i++) {
    mibloRomCopy(&st, &seq[i], sizeof(st));
    total += st.ms;
  }
  uint32_t t = ms % total;
  for (size_t i = 0; i < N; i++) {
    mibloRomCopy(&st, &seq[i], sizeof(st));
    if (t < st.ms) return st;
    t -= st.ms;
  }
  mibloRomCopy(&st, &seq[0], sizeof(st));
  return st;
}
}  // namespace

DeskMood deskMood(uint8_t pct) {
  if (pct >= 95) return DeskMood::Scared;
  if (pct >= 80) return DeskMood::Worried;
  if (pct >= 50) return DeskMood::Watchful;
  return DeskMood::Calm;
}

uint8_t deskPct(const miblo::UsageWindow& w, uint32_t nowEpoch) {
  if (!w.present) return 0;
  if (w.reset && nowEpoch && nowEpoch >= w.reset) return 0;  // the window has reset since
  return w.pct;
}

MascotLook deskLook(DeskMood mood, bool focusLeft, uint32_t ms) {
  Step step;
  switch (mood) {
    case DeskMood::Calm: step = stepAt(kCalm, ms); break;
    case DeskMood::Watchful: step = stepAt(kWatchful, ms); break;
    case DeskMood::Worried: step = stepAt(kWorried, ms); break;
    case DeskMood::Scared: step = stepAt(kScared, ms); break;
    case DeskMood::Searching: step = stepAt(kSearching, ms); break;
    case DeskMood::Celebrate: step = stepAt(kCelebrate, ms); break;
    case DeskMood::Asleep:
    default: step = stepAt(kAsleep, ms); break;
  }
  const Step* st = &step;
  MascotLook k{st->dx, st->dy, 0, 0, st->eyes, Paws::Down, st->extras};
  const int8_t side = focusLeft ? -3 : 3;  // the gauges sit below the cat: gaze down and sideways
  switch (st->gaze) {
    case Gaze::Front: break;
    case Gaze::Focus: k.gx = side, k.gy = 3; break;
    case Gaze::Other: k.gx = (int8_t)-side, k.gy = 3; break;
    case Gaze::Up: k.gy = -3; break;
  }
  if (st->paws == P_REACH) k.paws = focusLeft ? Paws::ReachLeft : Paws::ReachRight;
  if (st->paws == P_COVER) k.paws = Paws::Cover;
  return k;
}

static uint32_t lookHash(uint32_t salt, const MascotLook& k) {
  uint32_t h = hashInt(salt, (uint32_t)(uint8_t)k.dx | (uint32_t)(uint8_t)k.dy << 8 | (uint32_t)(uint8_t)k.gx << 16 |
                                 (uint32_t)(uint8_t)k.gy << 24);
  h = hashInt(h, (uint32_t)k.eyes | (uint32_t)k.paws << 8 | (uint32_t)k.extras << 16);
  return hashInt(h, (uint32_t)mascotAccessory() | (uint32_t)mascotStyle() << 8);  // a hat or colour change redraws
}

// The mascot in its box (`half` on the 240 grid), only redrawn when the expression changes. It
// is composed in horizontal strips (kCatStrips small layers of one reused buffer, each clipping
// the whole drawing) and pushed strip by strip, so it never needs one big block of heap: a
// whole-box layer (8 KB) often could not be allocated once the heap was fragmented, and the
// direct fallback flashed the background before each frame.
constexpr int kCatStrips = 8;

void deskCat(uint8_t id, int cx, int cy, int half240, const MascotLook& k) {
  if (!dirty(id, lookHash(kHashSeed + 17, k))) return;
  const int half = Sz(half240);
  const int stripH = (2 * half + kCatStrips - 1) / kCatStrips;
  for (int y = cy - half; y < cy + half; y += stripH) {
    const int h = y + stripH <= cy + half ? stripH : cy + half - y;
    if (!C().beginLayer(cx - half, y, 2 * half, h)) {  // no memory even for a strip: draw directly
      deskMascot(cx, cy, k, half240);
      break;
    }
    deskMascot(cx, cy, k, half240);
    C().endLayer();
  }
  C().releaseLayer();
}

// Ring gauge (270 degrees, gap at the bottom): the percentage big inside, the label under it.
static void ring(int cx, int cy, const char* label, bool present, uint8_t pct, uint16_t base) {
  const int r = Sz(44);
  const int ir = Sz(35);
  C().arc(cx, cy, r, ir, 45, 315, color::TRACK, color::BG);
  if (present && pct > 0) {
    int end = 45 + 270 * pct / 100;
    if (end <= 45) end = 46;
    C().arc(cx, cy, r, ir, 45, end, levelColor(pct, base), color::BG);
  }
  char buf[8];
  if (present) snprintf(buf, sizeof(buf), "%u%%", pct);
  else snprintf(buf, sizeof(buf), "--");
  C().text(cx, cy + Y(6), buf, Font::NumM, present ? color::TEXT : color::DIM, Align::Center, 2 * ir + Sz(10));
  C().text(cx, cy + Y(24), label, Font::Small, color::MUTED, Align::Center, 2 * ir);
}

DeskMood deskMoodFor(const Snapshot& s, uint32_t now, bool* focusLeft) {
  const uint8_t p5 = deskPct(s.h5, now);
  const uint8_t p7 = deskPct(s.d7, now);
  const bool usage = s.hasUsage && (s.h5.present || s.d7.present);
  const uint8_t worst = !usage ? 0 : (s.h5.present && (!s.d7.present || p5 >= p7) ? p5 : p7);
  if (focusLeft) *focusLeft = !usage || !s.d7.present || (s.h5.present && p5 >= p7);
  return deskMood(worst);
}

// Props of the programmer activities (miblo::Gift), drawn over the band after the cats.
enum class PropKind : uint8_t {
  None, Duck, Laptop, Lgtm, Bug, Rocket, Burst,
  Tail, Fly, Yarn, Mug, Box, Keys, Note, Puff, Glasses, Bubble, Fish, Butterfly, Balloon, Plane,
  Bowl, Button, Cucumber, Blanket, Laser, Dots, Drop,
  Count  // keep last: ui_visit_kit.h's vprop names every kind before it
};
// The visit activity files (ui_visit_kit.h) name these by number.
static_assert((uint8_t)PropKind::Duck == vprop::Duck, "ui_visit_kit.h's vprop::Duck");
static_assert((uint8_t)PropKind::Laptop == vprop::Laptop, "ui_visit_kit.h's vprop::Laptop");
static_assert((uint8_t)PropKind::Lgtm == vprop::Lgtm, "ui_visit_kit.h's vprop::Lgtm");
static_assert((uint8_t)PropKind::Bug == vprop::Bug, "ui_visit_kit.h's vprop::Bug");
static_assert((uint8_t)PropKind::Rocket == vprop::Rocket, "ui_visit_kit.h's vprop::Rocket");
static_assert((uint8_t)PropKind::Burst == vprop::Burst, "ui_visit_kit.h's vprop::Burst");
static_assert((uint8_t)PropKind::Tail == vprop::Tail, "ui_visit_kit.h's vprop::Tail");
static_assert((uint8_t)PropKind::Fly == vprop::Fly, "ui_visit_kit.h's vprop::Fly");
static_assert((uint8_t)PropKind::Yarn == vprop::Yarn, "ui_visit_kit.h's vprop::Yarn");
static_assert((uint8_t)PropKind::Mug == vprop::Mug, "ui_visit_kit.h's vprop::Mug");
static_assert((uint8_t)PropKind::Box == vprop::Box, "ui_visit_kit.h's vprop::Box");
static_assert((uint8_t)PropKind::Keys == vprop::Keys, "ui_visit_kit.h's vprop::Keys");
static_assert((uint8_t)PropKind::Note == vprop::Note, "ui_visit_kit.h's vprop::Note");
static_assert((uint8_t)PropKind::Puff == vprop::Puff, "ui_visit_kit.h's vprop::Puff");
static_assert((uint8_t)PropKind::Glasses == vprop::Glasses, "ui_visit_kit.h's vprop::Glasses");
static_assert((uint8_t)PropKind::Bubble == vprop::Bubble, "ui_visit_kit.h's vprop::Bubble");
static_assert((uint8_t)PropKind::Fish == vprop::Fish, "ui_visit_kit.h's vprop::Fish");
static_assert((uint8_t)PropKind::Butterfly == vprop::Butterfly, "ui_visit_kit.h's vprop::Butterfly");
static_assert((uint8_t)PropKind::Balloon == vprop::Balloon, "ui_visit_kit.h's vprop::Balloon");
static_assert((uint8_t)PropKind::Plane == vprop::Plane, "ui_visit_kit.h's vprop::Plane");
static_assert((uint8_t)PropKind::Bowl == vprop::Bowl, "ui_visit_kit.h's vprop::Bowl");
static_assert((uint8_t)PropKind::Button == vprop::Button, "ui_visit_kit.h's vprop::Button");
static_assert((uint8_t)PropKind::Cucumber == vprop::Cucumber, "ui_visit_kit.h's vprop::Cucumber");
static_assert((uint8_t)PropKind::Blanket == vprop::Blanket, "ui_visit_kit.h's vprop::Blanket");
static_assert((uint8_t)PropKind::Laser == vprop::Laser, "ui_visit_kit.h's vprop::Laser");
static_assert((uint8_t)PropKind::Dots == vprop::Dots, "ui_visit_kit.h's vprop::Dots");
static_assert((uint8_t)PropKind::Drop == vprop::Drop, "ui_visit_kit.h's vprop::Drop");
static_assert((uint8_t)PropKind::Count == vprop::Count, "a PropKind was added: name it in ui_visit_kit.h's vprop");
static_assert((uint8_t)PropKind::Count <= kItemsA, "PropKind fits below the visit files' kinds");
struct Prop {
  PropKind kind;
  int x, y;   // screen coordinates (see drawProp for the anchor of each)
  uint8_t f;  // animation frame: code lines, rocket flame, wing beat...
  int x2;     // Yarn: where its thread starts (0 = none)
};
constexpr uint16_t kDuckYellow = 0xFFE0;
constexpr uint16_t kOrange = 0xFC00;
constexpr uint16_t kGrey = 0x8410;
constexpr uint16_t kDarkGreen = 0x0400;
constexpr uint16_t kCoffeeBrown = 0x6A20;
constexpr uint16_t kBoxBrown = 0xB3C9;      // #b07848 cardboard
constexpr uint16_t kBoxDark = 0x8AC6;       // #8a5a34 its flaps
constexpr uint16_t kYarnDark = 0x5AD6;      // #5a5ab0 yarn lines, blanket stripes
constexpr uint16_t kWater = 0x5DBC;         // #5ab4e6
constexpr uint16_t kFishBlue = 0x7D3A;      // #7aa6d0
constexpr uint16_t kCucumber = 0x3CC7;      // #3a9a3a
constexpr uint16_t kCucumberDark = 0x2B45;  // #2a6a2a
constexpr uint16_t kBubble = 0x9DFF;        // #9cbcff
constexpr uint16_t kLaserGlow = 0x7800;     // #780000

static void drawProp(const Prop& p) {
  const int x = p.x, y = p.y, u = Sz(1) < 1 ? 1 : Sz(1);
  switch (p.kind) {
    case PropKind::Duck:  // rubber duck (centre of the body)
      szDisc(x, y, 0, 0, 6, kDuckYellow);
      szDisc(x, y, 5, -6, 4, kDuckYellow);
      szTri(x, y, 8, -7, 13, -5, 8, -4, kOrange);
      C().fillRect(x + Sz(6), y - Sz(8), u + u, u + u, color::PUPIL);
      break;
    case PropKind::Laptop:  // (centre of the keyboard) with code scrolling on the screen
      szRect(x, y, -15, 0, 30, 3, kGrey);
      szRect(x, y, -12, -17, 24, 17, kGrey);
      szRect(x, y, -11, -16, 22, 15, color::BLACK);
      for (int i = 0; i < 4; i++) {
        const int len = 4 + (p.f * 5 + i * 7) % 13;
        C().fillRect(x - Sz(9) + (i % 2) * Sz(3), y - Sz(14) + i * Sz(3), Sz(len), u, i % 3 ? color::GREEN : color::BLUE);
      }
      break;
    case PropKind::Lgtm:  // code review sign (top-left corner)
      szRound(x, y, 0, 0, 44, 16, 3, color::WHITE);
      C().text(x + Sz(22), y + Sz(12), "LGTM", Font::SmallBold, kDarkGreen, Align::Center, Sz(42));
      break;
    case PropKind::Bug:  // a little bug (centre of the body), legs going
      for (int s = -1; s <= 1; s += 2) {
        for (int l = -1; l <= 1; l++) C().fillRect(x + l * Sz(2), y + s * Sz(3) + (p.f % 2 ? s : 0), u, Sz(2), color::PUPIL);
      }
      szDisc(x, y, 0, 0, 3, color::RED);
      szDisc(x, y, 3, 0, 2, color::PUPIL);
      break;
    case PropKind::Rocket:  // (tip of the nose), flame when f > 0
      szTri(x, y, 0, 0, -4, 6, 4, 6, color::RED);
      szRect(x, y, -4, 6, 8, 12, color::WHITE);
      szDisc(x, y, 0, 10, 2, color::BLUE);
      szTri(x, y, -4, 12, -8, 19, -4, 18, color::RED);
      szTri(x, y, 4, 12, 8, 19, 4, 18, color::RED);
      if (p.f) szTri(x, y, -3, 18, 3, 18, 0, (21 + (p.f % 2) * 3), color::AMBER);
      break;
    case PropKind::Burst:  // the bug is fixed: a spark
      C().fillRect(x - Sz(6), y - u, Sz(12), u + u, color::AMBER);
      C().fillRect(x - u, y - Sz(6), u + u, Sz(12), color::AMBER);
      szDisc(x, y, 0, 0, 2, color::WHITE);
      break;
    case PropKind::Tail: {  // (its base) a curl in the cat's colour, swinging with f
      const int sw = (int)(p.f % 8 < 4 ? p.f % 8 : 8 - p.f % 8) - 2;  // -2..2
      for (int i = 0; i < 6; i++) C().fillCircle(x + sw * i * Sz(1), y - i * Sz(4), Sz(3), mascotSkin());
      C().fillCircle(x + sw * Sz(6) + (sw >= 0 ? Sz(3) : -Sz(3)), y - Sz(22), Sz(3), mascotSkin());
      break;
    }
    case PropKind::Fly:  // (body) wings flicker with f; grey, so it shows on the black background
      szDisc(x, y, 0, 0, 2, color::DIM);
      if (p.f % 2) {
        szDisc(x, y, -2, -3, 2, color::TEXT);
        szDisc(x, y, 2, -3, 2, color::TEXT);
      }
      break;
    case PropKind::Yarn:  // (centre of the ball) rolling lines; x2: where its thread starts
      if (p.x2) {
        const int a = p.x2 < x ? p.x2 : x, b = p.x2 < x ? x : p.x2;
        C().fillRect(a, y + Sz(6), b - a, u, kYarnDark);
      }
      szDisc(x, y, 0, 0, 8, color::VIOLET);
      for (int i = -1; i <= 1; i++) C().fillRect(x - Sz(6), y + i * Sz(3) + (int)(p.f % 3) - 1, Sz(12), u, kYarnDark);
      break;
    case PropKind::Mug:  // (centre) a blue mug of coffee
      szRect(x, y, -6, -7, 12, 14, color::BLUE);
      szDisc(x, y, 8, 0, 4, color::BLUE);
      szDisc(x, y, 8, 0, 2, color::BG);
      szRect(x, y, -5, -6, 10, 2, kCoffeeBrown);
      break;
    case PropKind::Box:  // (centre of the front) open cardboard box, flaps out
      szRect(x, y, -32, -14, 64, 28, kBoxBrown);
      szTri(x, y, -32, -14, -40, -22, -14, -14, kBoxDark);
      szTri(x, y, 32, -14, 40, -22, 14, -14, kBoxDark);
      C().fillRect(x - Sz(32), y - Sz(14), Sz(64), u + u, kBoxDark);
      break;
    case PropKind::Keys:  // (centre) a little piano keyboard
      szRect(x, y, -30, -7, 60, 14, color::WHITE);
      for (int i = 0; i < 9; i++)
        if (i % 3 != 2) C().fillRect(x - Sz(26) + i * Sz(6), y - Sz(7), Sz(4), Sz(8), color::BLACK);
      break;
    case PropKind::Note:  // (the note's head) an eighth note
      szDisc(x, y, 0, 0, 3, color::AMBER);
      C().fillRect(x + Sz(2), y - Sz(10), u + u, Sz(10), color::AMBER);
      C().fillRect(x + Sz(2), y - Sz(10), Sz(5), u + u, color::AMBER);
      break;
    case PropKind::Puff: {  // (centre) a sneeze cloud growing with f (0..4)
      const int r = Sz(2 + (p.f > 4 ? 4 : p.f));
      C().fillCircle(x, y, r, color::WHITE);
      C().fillCircle(x + r, y - r / 2, r * 3 / 4, color::WHITE);
      C().fillCircle(x - r / 2, y + r / 2, r * 2 / 3, color::WHITE);
      break;
    }
    case PropKind::Glasses:  // (between the lenses) sunglasses with a glint
      for (int i = 0; i < 2; i++)  // a faint rim: they show on the black background too
        C().fillRoundRect(x + Sz(i ? 3 : -19) - u, y - Sz(5) - u, Sz(16) + u + u, Sz(10) + u + u, Sz(3) + u,
                          color::FAINT);
      szRound(x, y, -19, -5, 16, 10, 3, color::BLACK);
      szRound(x, y, 3, -5, 16, 10, 3, color::BLACK);
      C().fillRect(x - Sz(3), y - Sz(3), Sz(6), u + u, color::BLACK);
      C().fillRect(x - Sz(16), y - Sz(3), Sz(4), u + u, color::WHITE);
      break;
    case PropKind::Bubble:  // (centre) a soap bubble
      C().arc(x, y, Sz(6), Sz(5), 0, 360, kBubble, color::BG);  // a ring: the face shows through
      C().fillCircle(x - Sz(2), y - Sz(2), u, color::WHITE);
      break;
    case PropKind::Fish: {  // (centre) a fish snack; f = bites taken (3: only the tail is left)
      const uint8_t bites = p.f > 3 ? 3 : p.f;
      szTri(x, y, 8, 0, 14, -5, 14, 5, kFishBlue);
      if (bites < 3) szDisc(x, y, 0, 0, 7 - 2 * bites, kFishBlue);
      if (bites == 0) C().fillCircle(x - Sz(4), y - Sz(1), u, color::PUPIL);
      break;
    }
    case PropKind::Butterfly: {  // (body) wings beating with f
      const int w = p.f % 2 ? Sz(3) : Sz(7);
      C().fillTriangle(x, y, x - w, y - Sz(6), x - w, y + Sz(2), color::VIOLET);
      C().fillTriangle(x, y, x + w, y - Sz(6), x + w, y + Sz(2), color::VIOLET);
      C().fillTriangle(x, y, x - w + u, y + Sz(6), x - u, y + Sz(6), color::AMBER);
      C().fillTriangle(x, y, x + w - u, y + Sz(6), x + u, y + Sz(6), color::AMBER);
      C().fillRect(x - u, y - Sz(4), u + u, Sz(9), color::PUPIL);
      break;
    }
    case PropKind::Balloon:  // (centre) a red balloon on a string
      C().fillRect(x, y + Sz(12), u, Sz(16), color::MUTED);
      szDisc(x, y, 0, 0, 11, color::RED);
      szTri(x, y, -2, 13, 2, 13, 0, 10, color::RED);
      szDisc(x, y, -4, -4, 2, color::WHITE);
      break;
    case PropKind::Plane: {  // (nose) a paper plane; f 0: flying right, 1: flying left
      const int d = p.f ? 1 : -1;  // the tail is behind the nose
      szTri(x, y, 0, 0, d * 18, -7, d * 12, 0, color::WHITE);
      szTri(x, y, 0, 0, d * 18, 3, d * 12, 0, color::MUTED);
      break;
    }
    case PropKind::Bowl: {  // (centre of the water) a fish bowl; f moves the fish
      szDisc(x, y, 0, 0, 14, kWater);
      szRect(x, y, -14, -14, 28, 6, color::BG);
      C().fillRect(x - Sz(10), y - Sz(9), Sz(20), u, color::WHITE);
      const int fx = x - Sz(8) + (int)(p.f % 20 < 10 ? p.f % 20 : 20 - p.f % 20) * Sz(16) / 10;
      szDisc(fx, y, 0, 3, 3, kOrange);
      szTri(fx, y, -3, 3, -6, 0, -6, 6, kOrange);
      break;
    }
    case PropKind::Button:  // (centre of the top) a big red button; f 1: pressed
      szRect(x, y, -10, 2, 20, 6, kGrey);
      C().fillRoundRect(x - Sz(7), y - (p.f ? 0 : Sz(3)), Sz(14), Sz(5) + (p.f ? 0 : Sz(3)), Sz(2), color::RED);
      break;
    case PropKind::Cucumber:  // (centre) standing up
      szRound(x, y, -6, -16, 12, 32, 6, kCucumber);
      for (int i = 0; i < 3; i++)
        C().fillRect(x - Sz(2) + (i % 2) * Sz(3), y - Sz(10) + i * Sz(8), u + u, u + u, kCucumberDark);
      break;
    case PropKind::Blanket:  // (centre of its top edge) a striped blanket
      szRound(x, y, -36, 0, 72, 24, 6, color::VIOLET);
      for (int i = 0; i < 3; i++) C().fillRect(x - Sz(36), y + Sz(5) + i * Sz(7), Sz(72), u + u, kYarnDark);
      break;
    case PropKind::Laser:  // (centre) a laser dot with its glow
      szDisc(x, y, 0, 0, 4, kLaserGlow);
      szDisc(x, y, 0, 0, 2, color::RED);
      break;
    case PropKind::Dots:  // (left dot) "..." while talking; f = how many (1..3)
      for (int i = 0; i < p.f && i < 3; i++) C().fillCircle(x + i * Sz(6), y, Sz(2), color::WHITE);
      break;
    case PropKind::Drop:  // (centre) a splash of water
      szDisc(x, y, 0, 0, 2, kWater);
      szTri(x, y, -2, 0, 2, 0, 0, -4, kWater);
      break;
    case PropKind::None:
    case PropKind::Count: break;
  }
}

// ---- pet mode ----
// The box moves at most a pixel a frame and carries a margin of background around its content,
// so each redraw also wipes where it was; a bigger jump (a stalled frame) clears the screen first.
constexpr int kRoamHalfW = 74;    // box half-width on the 240 grid (card lines + margin)
constexpr int kRoamCatHalf = 36;  // the cat: 72 px
constexpr int kRoamCardH = 88;    // clock, limits, reset, last task (name + when), under the cat
constexpr int kRoamMargin = 4;
constexpr uint32_t kRoamVxMs = 300;  // ms per pixel, sideways
constexpr uint32_t kRoamVyMs = 420;  // ms per pixel, up/down (different: the path covers the screen)

static int bounce(uint32_t steps, int span) {  // 0..span..0..
  if (span <= 0) return 0;
  const uint32_t p = steps % (2 * (uint32_t)span);
  return p <= (uint32_t)span ? (int)p : (int)(2 * span - p);
}

// 0..span..0 over `period` ms (a back-and-forth run).
int shuttle(uint32_t t, uint32_t period, int span) {
  const uint32_t p = t % period;
  const uint32_t half = period / 2;
  return (int)((int64_t)span * (p < half ? p : period - p) / half);
}

static int roamW() { return 2 * X(kRoamHalfW); }
static int roamH() { return 2 * Sz(kRoamCatHalf) + Y(kRoamCardH) + 2 * Y(kRoamMargin); }

void roamPosition(uint32_t ms, int& cx, int& cy) {
  cx = roamW() / 2 + bounce(ms / kRoamVxMs, X(240) - roamW());
  cy = roamH() / 2 + bounce(ms / kRoamVyMs, Y(240) - roamH());
}

bool anticOnSign(RoamAntic a) { return a >= RoamAntic::Bat && a <= RoamAntic::Wave; }

uint32_t anticLength(RoamAntic a) {
  return a == RoamAntic::None ? 0 : anticOnSign(a) ? kAnticMs : kAnticFloorMs;
}

// Round `round`'s order of the 30 antics: a seeded shuffle, the same on every run.
static void anticOrder(uint32_t round, uint8_t* out) {
  for (uint8_t i = 0; i < kAnticCount; i++) out[i] = (uint8_t)(i + 1);
  uint32_t s = round * 2654435761u + 0x9E3779B9u;
  for (uint8_t i = kAnticCount - 1; i > 0; i--) {
    s = s * 1664525u + 1013904223u;
    const uint8_t j = (uint8_t)((s >> 8) % (uint32_t)(i + 1));
    const uint8_t t = out[i];
    out[i] = out[j];
    out[j] = t;
  }
}

static RoamAntic anticOfCycle(uint32_t cycle) {  // cycle >= 1
  const uint32_t idx = cycle - 1, round = idx / kAnticCount, pos = idx % kAnticCount;
  uint8_t order[kAnticCount];
  anticOrder(round, order);
  if (round > 0) {  // never the same one twice in a row, across rounds too
    uint8_t prev[kAnticCount];
    anticOrder(round - 1, prev);
    if (order[0] == prev[kAnticCount - 1]) {
      const uint8_t t = order[0];
      order[0] = order[1];
      order[1] = t;
    }
  }
  return (RoamAntic)order[pos];
}

RoamAntic roamAntic(uint32_t ms, uint32_t* atMs) {
  const uint32_t cycle = ms / kAnticEveryMs, at = ms % kAnticEveryMs;
  if (atMs) *atMs = at;
  if (cycle == 0) return RoamAntic::None;
  const RoamAntic a = anticOfCycle(cycle);
  return at < anticLength(a) ? a : RoamAntic::None;
}

// The pet's sign: visible (not the screen's black), with a lighter edge.
constexpr uint16_t kSignFill = 0x2125;  // #26262c
constexpr uint16_t kSignEdge = 0x5ACC;  // #5a5a66

// The crossed-out laptop (computer away), in the sign's top-right corner.
constexpr int kAwayIconW = 22, kAwayIconH = 15;

static void signAwayIcon(int sx, int sy, int sw, int& x, int& y, int& w, int& h) {
  w = Sz(kAwayIconW), h = Sz(kAwayIconH);
  x = sx + sw - Sz(8) - w;
  y = sy + Sz(6);
}

// What pet mode shows at a given moment: the cat, its sign (held, or down on the floor), and the
// props of the antic playing.
struct RoamScene {
  int catX, catY;      // the cat's centre
  int sx, sy, sw, sh;  // the sign
  bool floor;          // the sign is (going) down on the floor: the whole screen is redrawn
  bool catBehind;      // peekaboo: the cat is drawn before the sign, which hides it
  MascotLook k;
  int signDx;          // the sign wiggles (batted, a sneeze)
  bool blot;           // coffee spilled on the sign
  int drops;           // coffee drops falling (0..3)
  int curX, curY;      // the mouse cursor on the sign, 0..100 (curX -1: none)
  Prop props[5];
  uint8_t nProps;
};

static void addProp(RoamScene& sc, PropKind kind, int x, int y, uint8_t f = 0, int x2 = 0) {
  if (sc.nProps < sizeof(sc.props) / sizeof(sc.props[0])) sc.props[sc.nProps++] = Prop{kind, x, y, f, x2};
}

static int lerp(int a, int b, uint32_t t, uint32_t len) {
  if (t >= len) return b;
  return a + (int)((int64_t)(b - a) * (int32_t)t / (int32_t)len);
}
int lerpTo(int a, int b, uint32_t t, uint32_t len) { return lerp(a, b, t, len); }

// The cat roaming with the sign in its paws, the box centred on (rx, ry).
static void heldLayout(int rx, int ry, RoamScene& sc) {
  const int top = ry - roamH() / 2, left = rx - roamW() / 2;
  sc.catX = rx;
  sc.catY = top + Y(kRoamMargin) + Sz(kRoamCatHalf);
  sc.sx = left + X(kRoamMargin);
  sc.sw = roamW() - 2 * X(kRoamMargin);
  sc.sy = sc.catY + Sz(kRoamCatHalf) - Sz(10);
  sc.sh = top + roamH() - Y(kRoamMargin) - sc.sy;
}

// The sign on the floor (bottom of the screen) and the cat above it, both centred: the antics
// lay their props out around this spot. `shift` (-1..1) moves the sign a pixel from one antic to
// the next, so it never sits on exactly the same pixels.
constexpr int kPlayCatY = 68;
static void floorLayout(int shift, RoamScene& sc) {
  sc.catX = X(120);
  sc.catY = Y(kPlayCatY);
  sc.sx = X(120) - sc.sw / 2 + shift;
  sc.sy = Y(240) - Y(3) - sc.sh - (shift < 0 ? -shift : shift);
}

void roamAwayIcon(int cx, int cy, int& x, int& y, int& w, int& h) {
  RoamScene sc{};
  heldLayout(cx, cy, sc);
  signAwayIcon(sc.sx, sc.sy, sc.sw, x, y, w, h);
}

static void signAntic(RoamAntic a, uint32_t at, RoamScene& sc);   // Task 3 (existing four) + Task 4
static void floorAntic(RoamAntic a, uint32_t p, RoamScene& sc);  // Tasks 5 and 6

static RoamScene roamScene(uint32_t ms, const MascotLook& base, bool playful) {
  RoamScene sc{};
  sc.curX = -1;
  sc.k = base;
  int rx, ry;
  roamPosition(ms, rx, ry);
  heldLayout(rx, ry, sc);
  uint32_t at = 0;
  const RoamAntic a = playful ? roamAntic(ms, &at) : RoamAntic::None;
  if (a == RoamAntic::None) return sc;
  if (anticOnSign(a)) {
    signAntic(a, at, sc);
    return sc;
  }
  // Away from the sign: put it down on the floor, play above it, pick it up again (back where the
  // roaming has got to by then).
  const RoamScene held = sc;
  floorLayout((int)((ms / kAnticEveryMs) % 3) - 1, sc);
  sc.floor = true;
  if (at < kAnticPutMs || at >= kAnticPutMs + kAnticMs) {
    const uint32_t t = at < kAnticPutMs ? at : kAnticFloorMs - at;  // 0 = held .. kAnticPutMs = down
    sc.catX = lerp(held.catX, sc.catX, t, kAnticPutMs);
    sc.catY = lerp(held.catY, sc.catY, t, kAnticPutMs);
    sc.sx = lerp(held.sx, sc.sx, t, kAnticPutMs);
    sc.sy = lerp(held.sy, sc.sy, t, kAnticPutMs);
    sc.k = MascotLook{0, 0, 0, 3, Eyes::Open, Paws::Down, 0};  // eyes on the sign it carries
    return sc;
  }
  floorAntic(a, at - kAnticPutMs, sc);
  return sc;
}

static void signAntic(RoamAntic a, uint32_t at, RoamScene& sc) {
  MascotLook& k = sc.k;
  switch (a) {
    case RoamAntic::Bat: {  // bats at the sign with one paw, then the other
      const bool left = (at / 400) % 2;
      k = MascotLook{0, 0, (int8_t)(left ? -3 : 3), 3, Eyes::Open, left ? Paws::ReachLeft : Paws::ReachRight, 0};
      if ((at / 200) % 2) sc.signDx = left ? -Sz(2) : Sz(2);
      if (at >= kAnticMs - 1500) k = MascotLook{0, 0, 0, 0, Eyes::Happy, Paws::Down, 0};
      break;
    }
    case RoamAntic::Spill:  // coffee in paw, it tips over the sign: scared, then hides its eyes
      if (at < 2000) {
        k = MascotLook{0, 0, 0, 0, Eyes::Happy, Paws::Down, kCoffee};
      } else if (at < 3000) {
        k = MascotLook{2, -2, 3, 3, Eyes::Open, Paws::Down, kCoffee};
        sc.drops = 1 + (int)((at - 2000) / 350);
      } else if (at < 6000) {
        k = MascotLook{(int8_t)((at / 120) % 2 ? -1 : 1), 0, 0, 3, Eyes::Wide, Paws::Down, kSweat | kMouthO};
        sc.blot = true;
      } else {
        k = MascotLook{0, 0, 0, 0, Eyes::Open, Paws::Cover, kSweat};
        sc.blot = true;
      }
      break;
    case RoamAntic::Cursor: {  // a mouse cursor runs over the sign; eyes on it, then a pounce
      const uint32_t pounce = kAnticMs - 1500;
      if (at < pounce) {
        sc.curX = shuttle(at, 2600, 100);
        sc.curY = shuttle(at + 700, 1900, 100);
        k = MascotLook{0, 0, (int8_t)(sc.curX < 35 ? -3 : sc.curX > 65 ? 3 : 0), 3, Eyes::Wide, Paws::Down, 0};
      } else {
        k = MascotLook{0, (int8_t)(at < pounce + 500 ? -4 : 0), 0, 3, Eyes::Happy,
                       at < pounce + 500 ? Paws::ReachLeft : Paws::Down, 0};
      }
      break;
    }
    case RoamAntic::Nap:  // dozes off on the sign
      if (at < 1200) k = MascotLook{0, 0, 0, 0, Eyes::Sleepy, Paws::Down, 0};
      else if (at < kAnticMs - 1000)
        k = MascotLook{0, 3, 0, 0, Eyes::Closed, Paws::Down, (uint16_t)((at / 900) % 2 ? kZ1 : kZ1 | kZ2)};
      else k = MascotLook{0, 0, 0, -3, Eyes::Open, Paws::Down, 0};
      break;
    case RoamAntic::Sneeze:  // wrinkles its nose, sneezes ("achoo" cloud), the sign shakes
      if (at < 1500) {
        k = MascotLook{0, 0, 0, 0, Eyes::Sleepy, Paws::Down, 0};
      } else if (at < 2500) {
        k = MascotLook{0, -3, 0, 0, Eyes::Closed, Paws::Down, kMouthWide};
        sc.signDx = (at / 80) % 2 ? Sz(2) : -Sz(2);
        addProp(sc, PropKind::Puff, sc.catX + Sz(46), sc.catY - Sz(4), (uint8_t)((at - 1500) / 250));
      } else if (at < 3500) {
        k = MascotLook{0, 0, 0, 0, Eyes::Open, Paws::Down, 0};
        addProp(sc, PropKind::Puff, sc.catX + Sz(46), sc.catY - Sz(4), 4);
      } else {
        k = MascotLook{0, 0, 0, 0, Eyes::Happy, Paws::Down, 0};
      }
      break;
    case RoamAntic::Peek: {  // ducks behind the sign (ears only), peeks out on one side, the other
      sc.catBehind = true;
      int dy = 56, dx = 0;
      Eyes e = Eyes::Open;
      // Slides up to peek out (16 px down) at 3000 on the left and at 6000 on the right, and back.
      auto peek = [&](uint32_t t0, int side) {
        if (at < t0 || at >= t0 + 1500) return;
        const uint32_t t = at - t0;
        dy = t < 400 ? 56 - (int)(t * 40 / 400) : t >= 1100 ? 16 + (int)((t - 1100) * 40 / 400) : 16;
        dx = side * 30, e = Eyes::Wide;
      };
      if (at < 1500) dy = (int)(at * 56 / 1500);
      peek(3000, -1);
      peek(6000, 1);
      if (at >= 7500) dy = (int)((kAnticMs - at) * 56 / 1500), e = Eyes::Happy;
      k = MascotLook{(int8_t)dx, (int8_t)dy, 0, 0, e, Paws::Down, 0};
      break;
    }
    case RoamAntic::Heart:  // looks at you, "^ ^", a little heart beating beside its ear
      k = MascotLook{0, (int8_t)(at >= 2000 && at < 2400 ? -3 : 0), 0, 0, Eyes::Happy, Paws::Down,
                     (uint16_t)(at < 6000 && (at / 600) % 2 == 0 ? kHeart : 0)};
      break;
    case RoamAntic::Glasses: {  // sunglasses come down onto its face ("deal with it"), then go up
      const int seat = sc.catY + Sz(4), from = sc.catY - Sz(32);
      const int gy = at < 2000 ? lerp(from, seat, at, 2000) : at < 7000 ? seat : lerp(seat, from, at - 7000, 2000);
      k = MascotLook{0, (int8_t)(at >= 2000 && at < 2600 ? -2 : 0), 0, 0, at < 2000 ? Eyes::Wide : Eyes::Open,
                     Paws::Down, 0};
      addProp(sc, PropKind::Glasses, sc.catX, gy);
      break;
    }
    case RoamAntic::Wave:  // looks at you and waves a paw, head tilted, happy
      if (at < 1000 || at >= kAnticMs - 1000) {
        k = MascotLook{0, 0, 0, 0, Eyes::Open, Paws::Down, 0};
      } else {
        const bool up = (at / 400) % 2 == 0;
        k = MascotLook{0, 0, 0, 3, Eyes::Happy, up ? Paws::ReachRight : Paws::TapRight, 0};
      }
      break;
    default: break;
  }
}

static void floorAntic(RoamAntic a, uint32_t p, RoamScene& sc) {
  MascotLook& k = sc.k;
  const int cx = sc.catX, cy = sc.catY;
  switch (a) {
    case RoamAntic::Laptop: {  // types on a laptop; a bug crawls out of it and gets squashed
      const int lx = cx + Sz(52), ly = cy + Sz(30);  // the keyboard's centre
      if (p < 5000) {
        k = MascotLook{0, 0, 3, 3, Eyes::Open, (p / 200) % 2 ? Paws::ReachRight : Paws::Down, 0};
        addProp(sc, PropKind::Laptop, lx, ly, (uint8_t)(p / 150));
      } else if (p < 6500) {
        addProp(sc, PropKind::Laptop, lx, ly, 33);
        addProp(sc, PropKind::Bug, lerp(lx - Sz(8), cx + Sz(22), p - 5000, 1500), ly - Sz(4), (uint8_t)(p / 120));
        k = MascotLook{0, 0, 3, 3, Eyes::Wide, Paws::Down, 0};
      } else if (p < 7500) {
        addProp(sc, PropKind::Laptop, lx, ly, 33);
        addProp(sc, PropKind::Burst, cx + Sz(22), ly - Sz(4));
        k = MascotLook{0, -3, 3, 3, Eyes::Wide, Paws::ReachRight, 0};
      } else {
        addProp(sc, PropKind::Laptop, lx, ly, 33);
        k = MascotLook{0, 0, 0, 0, Eyes::Happy, Paws::Down, 0};
      }
      break;
    }
    case RoamAntic::Tail: {  // its tail swings; it watches, turns after it, ends up dizzy
      const uint8_t sw = (uint8_t)(p / 250);
      addProp(sc, PropKind::Tail, cx + Sz(40), cy + Sz(28), p < 6500 ? sw : 2);
      if (p < 3000) k = MascotLook{0, 0, 3, (int8_t)(sw % 8 < 4 ? -2 : 0), Eyes::Open, Paws::Down, 0};
      else if (p < 6500)
        k = MascotLook{(int8_t)((p / 300) % 2 ? -4 : 4), 0, 3, 0, Eyes::Wide,
                       (p / 300) % 2 ? Paws::ReachLeft : Paws::ReachRight, 0};
      else k = MascotLook{0, 0, 0, 0, Eyes::Dizzy, Paws::Down, kStars};
      break;
    }
    case RoamAntic::Stretch:  // paws up, a big yawn, then content
      if (p < 1500) k = MascotLook{0, 0, 0, 0, Eyes::Closed, Paws::Up, 0};
      else if (p < 4500) k = MascotLook{0, -3, 0, 0, Eyes::Closed, Paws::Up, kMouthWide};
      else if (p < 6000) k = MascotLook{0, 0, 0, 0, Eyes::Sleepy, Paws::Down, 0};
      else k = MascotLook{0, 0, 0, 0, Eyes::Happy, Paws::Down, 0};
      break;
    case RoamAntic::Lick:  // licks a paw, eyes closed, then content
      if (p < 6000) k = MascotLook{0, 0, 0, 0, Eyes::Closed, Paws::Lick, (uint16_t)((p / 400) % 2 ? kTongue : 0)};
      else k = MascotLook{0, 0, 0, 0, Eyes::Happy, Paws::Down, 0};
      break;
    case RoamAntic::Fly:  // a fly zigzags around its head; two swipes; it gets away; grumpy
      if (p < 7000) {
        const int fx = cx - Sz(50) + shuttle(p, 2200, Sz(100)), fy = cy - Sz(44) + shuttle(p + 500, 1300, Sz(30));
        addProp(sc, PropKind::Fly, fx, fy, (uint8_t)(p / 60));
        const Paws pw = p >= 3500 && p < 4000 ? Paws::ReachLeft : p >= 5500 && p < 6000 ? Paws::ReachRight : Paws::Down;
        k = MascotLook{0, (int8_t)(pw != Paws::Down ? -3 : 0), (int8_t)(fx < cx - Sz(10) ? -3 : fx > cx + Sz(10) ? 3 : 0),
                       -3, Eyes::Wide, pw, 0};
      } else if (p < 8000) {
        addProp(sc, PropKind::Fly, lerp(cx + Sz(40), X(240) - X(8), p - 7000, 1000),
                lerp(cy - Sz(40), Y(10), p - 7000, 1000), (uint8_t)(p / 60));
        k = MascotLook{0, 0, 3, -3, Eyes::Open, Paws::Down, kGrumpy};
      } else {
        k = MascotLook{0, 0, 0, 0, Eyes::Open, Paws::Down, kGrumpy};
      }
      break;
    case RoamAntic::Yarn: {  // a ball of yarn rolls in, a push, it rolls away unwinding, comes back
      const int px = cx - Sz(30), yy = cy + Sz(34), far = X(14);
      int bx = px, thread = 0;
      if (p < 1500) bx = lerp(far, px, p, 1500);
      else if (p >= 2500 && p < 5000) bx = lerp(px, far, p - 2500, 2500), thread = px;
      else if (p >= 5000 && p < 7000) bx = lerp(far, px, p - 5000, 2000), thread = px;
      addProp(sc, PropKind::Yarn, bx, yy, (uint8_t)(bx / (Sz(3) > 0 ? Sz(3) : 1)), thread);
      if (p >= 1500 && p < 2500) k = MascotLook{0, -3, -3, 3, Eyes::Wide, Paws::ReachLeft, 0};
      else if (p < 7000) k = MascotLook{0, 0, -3, 3, Eyes::Wide, Paws::Down, 0};
      else k = MascotLook{0, 0, 0, 0, Eyes::Happy, Paws::Down, 0};
      break;
    }
    case RoamAntic::Mug: {  // looks at you, pushes a mug slowly off the edge; it falls; innocent
      const int my0 = cy + Sz(24), mx0 = cx + Sz(46), edge = X(240) - Sz(16);
      if (p < 2000) {
        addProp(sc, PropKind::Mug, mx0, my0);
        k = MascotLook{0, 0, 0, 0, Eyes::Open, Paws::Down, 0};
      } else if (p < 4000) {
        addProp(sc, PropKind::Mug, lerp(mx0, edge, p - 2000, 2000), my0);
        k = MascotLook{0, 0, 0, 0, Eyes::Open, Paws::ReachRight, 0};
      } else if (p < 5500) {
        addProp(sc, PropKind::Mug, edge, lerp(my0, Y(240) - Sz(12), p - 4000, 1500));
        k = MascotLook{0, 0, 3, 3, Eyes::Wide, Paws::Down, kMouthO};
      } else {
        k = MascotLook{0, 0, 0, 0, Eyes::Happy, Paws::Down, 0};
      }
      break;
    }
    case RoamAntic::Box:  // a box shows up; it hops in (ears and eyes peeking out), hops out
      addProp(sc, PropKind::Box, cx, cy + Sz(38));
      if (p < 1500) k = MascotLook{0, 0, 0, 3, Eyes::Open, Paws::Down, 0};
      else if (p < 2500) k = MascotLook{0, (int8_t)(p < 1900 ? -5 : 16), 0, 0, Eyes::Wide, Paws::Down, 0};
      else if (p < 6500)
        k = MascotLook{0, 16, 0, 0, (p % 1500) < 200 ? Eyes::Closed : Eyes::Open, Paws::Down, 0};
      else if (p < 7500) k = MascotLook{0, (int8_t)(p < 6900 ? -5 : 0), 0, 0, Eyes::Happy, Paws::Down, 0};
      else k = MascotLook{0, 0, 0, 0, Eyes::Happy, Paws::Down, 0};
      break;
    case RoamAntic::Keys:  // plays a little keyboard; notes float up
      addProp(sc, PropKind::Keys, cx, cy + Sz(36));
      for (int i = 0; i < 3; i++) {
        const uint32_t ph = (p + (uint32_t)i * 700) % 2100;
        const int nx = i == 1 ? 44 : i ? -56 : -44;  // beside the head, never over its face
        addProp(sc, PropKind::Note, cx + Sz(nx), cy + Sz(26) - (int)(ph * (uint32_t)Sz(56) / 2100));
      }
      k = MascotLook{0, 0, 0, 3, Eyes::Happy, (p / 250) % 2 ? Paws::TapLeft : Paws::TapRight, 0};
      break;
    case RoamAntic::Laser: {  // a red dot races around; eyes on it; a pounce; gone
      if (p < 6600) {
        const int lx = X(20) + shuttle(p, 3000, X(200));
        const int ly = Y(14) + shuttle(p + 900, 2300, sc.sy - Sz(8) - Y(14));
        addProp(sc, PropKind::Laser, lx, ly);
        const int8_t gx = (int8_t)(lx < cx - Sz(10) ? -3 : lx > cx + Sz(10) ? 3 : 0);
        const int8_t gy = (int8_t)(ly < cy - Sz(10) ? -3 : ly > cy + Sz(10) ? 3 : 0);
        if (p >= 6000) k = MascotLook{0, -5, gx, gy, Eyes::Wide, gx < 0 ? Paws::ReachLeft : Paws::ReachRight, 0};
        else k = MascotLook{0, 0, gx, gy, Eyes::Wide, Paws::Down, 0};
      } else {
        k = MascotLook{0, 0, (int8_t)((p / 400) % 2 ? -3 : 3), 0, Eyes::Open, Paws::Down, 0};
      }
      break;
    }
    case RoamAntic::Bubbles: {  // bubbles float up; swipes; one pops on its nose
      const int top = Y(10), bottom = sc.sy - Sz(10);
      for (int i = 0; i < 3; i++) {
        if (p >= 6000 && i == 1) continue;  // this one popped on its nose
        const uint32_t ph = (p + (uint32_t)i * 1200) % 3600;
        addProp(sc, PropKind::Bubble, cx - Sz(40) + i * Sz(40) + (int)((ph / 300) % 2) * Sz(2),
                bottom - (int)((int64_t)ph * (bottom - top) / 3600));
      }
      if (p >= 6000 && p < 6600) addProp(sc, PropKind::Burst, cx, cy + Sz(14));
      if (p < 6000)
        k = MascotLook{0, -2, 0, -3, Eyes::Wide, (p / 700) % 2 ? Paws::ReachLeft : Paws::ReachRight, 0};
      else if (p < 7000) k = MascotLook{0, 0, 0, 0, Eyes::Closed, Paws::Down, kMouthO};
      else k = MascotLook{0, 0, 0, 0, Eyes::Happy, Paws::Down, 0};
      break;
    }
    case RoamAntic::Fish:  // a fish snack, three bites, licks its lips
      if (p < 2000) {
        addProp(sc, PropKind::Fish, cx, cy + Sz(32), 0);
        k = MascotLook{0, 0, 0, 3, Eyes::Wide, Paws::Down, 0};
      } else if (p < 6000) {
        addProp(sc, PropKind::Fish, cx, cy + Sz(32), (uint8_t)((p - 2000) / 1000));
        const bool bite = (p / 250) % 2;
        k = MascotLook{0, (int8_t)(bite ? 2 : 0), 0, 3, Eyes::Closed, Paws::Down, (uint16_t)(bite ? kMouthO : 0)};
      } else {
        k = MascotLook{0, 0, 0, 0, Eyes::Happy, Paws::Down, (uint16_t)((p / 400) % 2 ? kTongue : 0)};
      }
      break;
    case RoamAntic::Duck: {  // explains the bug to a rubber duck... then gets it
      addProp(sc, PropKind::Duck, cx + Sz(54), cy + Sz(24));
      if (p < 5000) {
        addProp(sc, PropKind::Dots, cx + Sz(34), cy - Sz(14), (uint8_t)(1 + (p / 500) % 3));
        k = MascotLook{0, 0, 3, 0, Eyes::Open, Paws::Down, (uint16_t)((p / 300) % 2 ? kMouthO : 0)};
      } else if (p < 6500) {
        k = MascotLook{0, -3, 3, 0, Eyes::Wide, Paws::Down, kAlarm};
      } else {
        k = MascotLook{0, 0, 0, 0, Eyes::Happy, Paws::Down, 0};
      }
      break;
    }
    case RoamAntic::Coffee:  // a slow coffee, eyes closed on the sip
      if (p < 3000) k = MascotLook{0, 0, 0, 0, Eyes::Open, Paws::Down, kCoffee};
      else if (p < 6000) k = MascotLook{0, -1, 0, 0, Eyes::Closed, Paws::Down, kCoffee};
      else k = MascotLook{0, 0, 0, 0, Eyes::Happy, Paws::Down, kCoffee};
      break;
    case RoamAntic::Butterfly:  // a butterfly flutters around and lands on its nose: cross-eyed
      if (p < 5000) {
        const int bx = cx - Sz(50) + shuttle(p, 2600, Sz(100)), by = cy - Sz(40) + shuttle(p + 700, 1700, Sz(20));
        addProp(sc, PropKind::Butterfly, bx, by, (uint8_t)(p / 150));
        k = MascotLook{0, 0, (int8_t)(bx < cx - Sz(10) ? -3 : bx > cx + Sz(10) ? 3 : 0), -3, Eyes::Open, Paws::Down, 0};
      } else if (p < 8000) {
        addProp(sc, PropKind::Butterfly, cx, cy + Sz(14), (uint8_t)(p / 600));
        k = MascotLook{0, 0, 0, 0, Eyes::Open, Paws::Down, kCrossEyed};
      } else {
        addProp(sc, PropKind::Butterfly, lerp(cx, cx + Sz(50), p - 8000, 1000), lerp(cy + Sz(14), Y(12), p - 8000, 1000),
                (uint8_t)(p / 150));
        k = MascotLook{0, 0, 3, -3, Eyes::Happy, Paws::Down, 0};
      }
      break;
    case RoamAntic::Balloon: {  // pokes a balloon, it pops: fur up, shaking, sweating
      const int bx = cx + Sz(50), by = cy - Sz(16) + (int)((p / 500) % 2) * Sz(2);
      if (p < 3800) {
        addProp(sc, PropKind::Balloon, bx, by);
        k = MascotLook{0, (int8_t)(p >= 3000 ? -3 : 0), 3, -3, Eyes::Open, p >= 3000 ? Paws::ReachRight : Paws::Down, 0};
      } else if (p < 4400) {
        addProp(sc, PropKind::Burst, bx, by);
        k = MascotLook{0, -4, 0, 0, Eyes::Wide, Paws::Down, (uint16_t)(kFluffed | kMouthO)};
      } else if (p < 6500) {
        k = MascotLook{(int8_t)((p / 100) % 2 ? -1 : 1), 0, 0, 0, Eyes::Wide, Paws::Down, kFluffed};
      } else {
        k = MascotLook{0, 0, 0, 0, Eyes::Open, Paws::Down, kSweat};
      }
      break;
    }
    case RoamAntic::Plane:  // a paper plane crosses the screen; it swipes at it on the way back
      if (p < 4000) {
        const int x = lerp(X(28), X(230), p, 4000);
        addProp(sc, PropKind::Plane, x, Y(22), 0);
        k = MascotLook{0, 0, (int8_t)(x < cx - Sz(10) ? -3 : x > cx + Sz(10) ? 3 : 0), -3, Eyes::Open, Paws::Down, 0};
      } else if (p >= 4500 && p < 8500) {
        const int x = lerp(X(212), X(10), p - 4500, 4000);
        addProp(sc, PropKind::Plane, x, cy + Sz(4), 1);
        const bool swipe = p >= 6000 && p < 6800;
        k = MascotLook{0, (int8_t)(swipe ? -3 : 0), (int8_t)(x < cx - Sz(10) ? -3 : x > cx + Sz(10) ? 3 : 0), 0,
                       Eyes::Wide, swipe ? Paws::ReachLeft : Paws::Down, 0};
      } else {
        k = MascotLook{0, 0, 0, 0, Eyes::Open, Paws::Down, 0};
      }
      break;
    case RoamAntic::Bowl: {  // watches a fish bowl, dips a paw in (splash), wet paw: grumpy
      const int bx = cx + Sz(54), by = cy + Sz(26);
      addProp(sc, PropKind::Bowl, bx, by, (uint8_t)(p / 150));
      if (p < 4000) {
        k = MascotLook{0, 0, 3, 3, Eyes::Wide, Paws::Down, 0};
      } else if (p < 6000) {
        k = MascotLook{0, -2, 3, 3, Eyes::Wide, Paws::ReachRight, 0};
        if ((p / 300) % 2) {
          addProp(sc, PropKind::Drop, bx - Sz(8), by - Sz(18));
          addProp(sc, PropKind::Drop, bx + Sz(6), by - Sz(20));
        }
      } else {
        k = MascotLook{0, 0, 0, 3, Eyes::Open, Paws::Down, kGrumpy};
      }
      break;
    }
    case RoamAntic::Deploy: {  // presses the big red button; a little rocket takes off
      const int bx = cx - Sz(42), by = cy + Sz(28);
      addProp(sc, PropKind::Button, bx, by, (uint8_t)(p >= 1500 && p < 2500 ? 1 : 0));
      if (p < 1500) {
        k = MascotLook{0, 0, -3, 3, Eyes::Open, Paws::Down, 0};
      } else if (p < 2500) {
        k = MascotLook{0, -2, -3, 3, Eyes::Open, Paws::ReachLeft, 0};
      } else if (p < 6000) {
        addProp(sc, PropKind::Rocket, cx + Sz(52), lerp(cy + Sz(10), Y(4), p - 2500, 3500), (uint8_t)(1 + p / 100));
        k = MascotLook{0, 0, 3, -3, Eyes::Wide, Paws::Down, 0};
      } else {
        k = MascotLook{0, 0, 0, 0, Eyes::Happy, Paws::Down, kHeart};
      }
      break;
    }
    case RoamAntic::Cucumber:  // a cucumber appears behind it; it turns round and jumps
      if (p >= 1500) addProp(sc, PropKind::Cucumber, cx + Sz(50), cy + Sz(20));
      if (p < 3000) k = MascotLook{0, 0, -3, 0, Eyes::Open, Paws::Down, 0};
      else if (p < 3200) k = MascotLook{0, 0, 3, 0, Eyes::Open, Paws::Down, 0};
      else if (p < 5000)
        k = MascotLook{0, (int8_t)(p < 3800 ? -6 : 0), 3, 0, Eyes::Wide, Paws::Down, (uint16_t)(kFluffed | kAlarm)};
      else k = MascotLook{0, 0, 3, 0, Eyes::Open, Paws::Down, kSweat};
      break;
    case RoamAntic::Blanket: {  // pulls up a blanket and dozes off
      addProp(sc, PropKind::Blanket, cx, p < 1500 ? lerp(cy + Sz(40), cy + Sz(14), p, 1500) : cy + Sz(14));
      k = MascotLook{0, 0, 0, 0, p < 3000 ? Eyes::Sleepy : Eyes::Closed, Paws::Down,
                     (uint16_t)(p < 3000 ? 0 : (p / 900) % 2 ? kZ1 : kZ1 | kZ2)};
      break;
    }
    default: break;
  }
}

// The sign's face: clock, limits, next reset (or "limit freed"), the last task, the away icon,
// and what an antic did to it (coffee, cursor).
// The sign's limits on one line, each window marked by an icon instead of a label ("5h 82%" read
// as one number, and 5h was easy to mix up with 7d): a clock for the 5-hour session, a calendar
// for the week, each followed by its percentage in bold, coloured by level.
struct SignLimits {
  char v5[8], v7[8];  // "82%", or "--" when unknown
  uint16_t c5, c7;
  bool shown;
};

constexpr int kSignIconW = 12;  // both icons, at screen scale 1: big enough to read from afar

static void signClockIcon(int x, int y) {  // (top-left) a round clock, hands at three o'clock
  szDisc(x, y, 6, 6, 6, color::MUTED);
  szDisc(x, y, 6, 6, 4, kSignFill);
  szRect(x, y, 5, 3, 2, 4, color::MUTED);  // the minute hand, up
  szRect(x, y, 5, 5, 4, 2, color::MUTED);  // the hour hand, right
}

static void signCalendarIcon(int x, int y) {  // (top-left) a wall calendar: rings, a header, a page
  szRect(x, y, 0, 2, 12, 10, color::MUTED);
  szRect(x, y, 2, 6, 8, 4, kSignFill);
  szRect(x, y, 3, 0, 2, 3, color::MUTED);  // the rings
  szRect(x, y, 7, 0, 2, 3, color::MUTED);
  szRect(x, y, 3, 7, 3, 2, color::MUTED);  // today
}

static void drawSignLimits(int cx, int y, const SignLimits& L) {
  const int icon = Sz(kSignIconW), pad = Sz(4), gap = Sz(14);
  const int b = C().textWidth(L.v5, Font::SmallBold), d = C().textWidth(L.v7, Font::SmallBold);
  int x = cx - (icon + pad + b + gap + icon + pad + d) / 2;
  const int iy = y - Sz(11);  // the icons stand on the text's baseline, as tall as the digits and a bit more
  signClockIcon(x, iy);
  x += icon + pad;
  C().text(x, y, L.v5, Font::SmallBold, L.c5, Align::Left, b + 1);
  x += b + gap;
  signCalendarIcon(x, iy);
  x += icon + pad;
  C().text(x, y, L.v7, Font::SmallBold, L.c7, Align::Left, d + 1);
}

static void drawSign(const RoamScene& sc, const char* hhmm, const SignLimits& lim, const char* reset, uint16_t resetFg,
                     const char* lastName, uint16_t nameFg, const char* lastWhen, bool computerAway) {
  const int sx = sc.sx + sc.signDx, sy = sc.sy, sw = sc.sw, sh = sc.sh;
  C().fillRoundRect(sx, sy, sw, sh, Sz(6), kSignEdge);
  C().fillRoundRect(sx + 1, sy + 1, sw - 2, sh - 2, Sz(5), kSignFill);
  const int w = sw - 2 * X(kRoamMargin), tx = sx + sw / 2, y0 = sy + Sz(10);
  C().text(tx, y0 + Y(16), hhmm, Font::BodyBold, color::TEXT, Align::Center, w);
  if (lim.shown) drawSignLimits(tx, y0 + Y(33), lim);
  if (reset[0]) C().text(tx, y0 + Y(49), reset, Font::Small, resetFg, Align::Center, w);
  if (lastName[0]) C().text(tx, y0 + Y(66), lastName, Font::SmallBold, nameFg, Align::Center, w);
  if (lastWhen[0]) C().text(tx, y0 + Y(81), lastWhen, Font::Small, color::MUTED, Align::Center, w);
  if (sc.blot) {  // a coffee stain across the top of the sign
    szDisc(tx, sy, -14, 9, 6, kCoffeeBrown);
    szDisc(tx, sy, -3, 12, 8, kCoffeeBrown);
    szDisc(tx, sy, 11, 8, 5, kCoffeeBrown);
    szDisc(tx, sy, 22, 14, 2, kCoffeeBrown);
  }
  for (int i = 0; i < sc.drops; i++) C().fillCircle(sc.catX + Sz(22), sy - Sz(4) + i * Sz(5), Sz(2), kCoffeeBrown);
  if (computerAway) {  // a small laptop, crossed out: discreet, and the same in every language
    int ix, iy, iw, ih;
    signAwayIcon(sx, sy, sw, ix, iy, iw, ih);
    const int t1 = Sz(1) > 0 ? Sz(1) : 1;
    const int lw = iw - Sz(4), lh = ih - Sz(4), lx = ix + Sz(2);  // the lid, above the base
    C().fillRect(lx, iy, lw, t1, color::MUTED);
    C().fillRect(lx, iy + lh - t1, lw, t1, color::MUTED);
    C().fillRect(lx, iy, t1, lh, color::MUTED);
    C().fillRect(lx + lw - t1, iy, t1, lh, color::MUTED);
    C().fillRect(ix, iy + lh + t1, iw, Sz(2), color::MUTED);  // the base
    // The slash, set off from the outline by a gap in the sign's colour.
    const int inset = Sz(3);
    C().wideLine(ix + inset, iy - Sz(1), ix + iw - inset, iy + ih, Sz(4), kSignFill, kSignFill);
    C().wideLine(ix + inset, iy - Sz(1), ix + iw - inset, iy + ih, Sz(2), color::AMBER, kSignFill);
  }
  if (sc.curX >= 0) {  // the mouse cursor: a white arrow, over the away icon
    const int ax = sx + Sz(10) + (sw - Sz(24)) * sc.curX / 100, ay = sy + Sz(12) + (sh - Sz(28)) * sc.curY / 100;
    szTri(ax, ay, 0, 0, 0, 11, 8, 8, color::WHITE);
    szRect(ax, ay, 3, 8, 2, 5, color::WHITE);
  }
}

void roam(Lang lang, const Snapshot& s, const Clock& clk, uint32_t ms, DeskMood mood, const char* note,
          uint32_t lookMs, bool computerAway) {
  // Antics while it is calm and nothing else is being said (a friend's hi, a nap together).
  const bool playful = (mood == DeskMood::Calm || mood == DeskMood::Watchful) && !(note && note[0]);
  const RoamScene sc = roamScene(ms, deskLook(mood, true, lookMs == UINT32_MAX ? ms : lookMs), playful);
  const uint32_t now = clk.epoch ? clk.epoch : s.now;

  // The card's lines (empty when unknown).
  SignLimits lim{"--", "--", color::TEXT, color::TEXT, false};
  char reset[48] = "", lastName[48] = "", lastWhen[64] = "";
  uint16_t resetFg = color::MUTED;
  const bool usage = s.hasUsage && (s.h5.present || s.d7.present);
  if (usage) {
    lim.shown = true;
    if (s.h5.present) {
      const unsigned p = deskPct(s.h5, now);
      snprintf(lim.v5, sizeof(lim.v5), "%u%%", p);
      lim.c5 = levelColor(p, color::TEXT);
    }
    if (s.d7.present) {
      const unsigned p = deskPct(s.d7, now);
      snprintf(lim.v7, sizeof(lim.v7), "%u%%", p);
      lim.c7 = levelColor(p, color::TEXT);
    }
    if (s.h5.present && s.h5.reset && now >= s.h5.reset) {  // the 5h window has reset since
      snprintf(reset, sizeof(reset), "%s", t(lang, S::LimitFreed));
      resetFg = color::GREEN;
    } else if (s.h5.present && s.h5.reset) {
      char when[32];
      formatWhen(lang, s.h5.reset, now, when, sizeof(when));
      snprintf(reset, sizeof(reset), t(lang, S::ResetsAt), when);
    }
  }
  const int lf = miblo::lastFinished(s);
  if (lf >= 0) {
    char ago[16];
    miblo::formatInState(sessionSince(s.sessions[lf], clk), ago, sizeof(ago));
    snprintf(lastName, sizeof(lastName), "%s", s.sessions[lf].name);
    // "finished 12m ago" without the name (it has its own line): the phrase with an empty name.
    snprintf(lastWhen, sizeof(lastWhen), t(lang, S::FinishedAgo), "", ago);
    char* w = lastWhen;
    while (*w == ' ') w++;
    memmove(lastWhen, w, strlen(w) + 1);
  }
  uint16_t nameFg = color::BLUE;
  if (note && note[0]) {  // a friend's "hi" or a nap together, in place of the last task
    snprintf(lastName, sizeof(lastName), "%s", note);
    lastWhen[0] = 0;
    nameFg = color::AMBER;
  }

  uint32_t h = lookHash(hashInt(kHashSeed + 43, (uint32_t)(sc.catX * 1000 + sc.catY)), sc.k);
  h = hashInt(h, (uint32_t)(sc.sx * 1000 + sc.sy));
  h = hashStr(hashStr(hashStr(hashStr(hashStr(hashStr(h, clk.hhmm), lim.v5), lim.v7), reset), lastName), lastWhen);
  h = hashInt(h, mascotAccessory());
  h = hashInt(h, (uint32_t)lim.c5 << 16 | lim.c7);
  h = hashInt(h, (uint32_t)sc.floor | (uint32_t)sc.catBehind << 1 | (uint32_t)sc.blot << 2 |
                     (uint32_t)sc.drops << 4 | (uint32_t)(sc.signDx + 16) << 8 | (uint32_t)computerAway << 16);
  h = hashInt(h, (uint32_t)((sc.curX + 1) * 1000 + sc.curY));
  for (uint8_t i = 0; i < sc.nProps; i++) {
    const Prop& p = sc.props[i];
    h = hashInt(hashInt(h, (uint32_t)p.kind | (uint32_t)p.f << 8), (uint32_t)(p.x * 1000 + p.y) ^ ((uint32_t)p.x2 << 20));
  }
  if (!dirty(R_BODY, h)) return;
  // The held sign travels in a box around the cat (cleared as it moves); away from the sign, and on
  // the first held frame after it (the hash has the floor bit, so that frame is dirty), the whole
  // screen is recomposed in strips: no clear straight on the panel, no blink.
  static int lastX = -1000, lastY = -1000;
  static bool wasFloor = false;
  const bool full = sc.floor || wasFloor;
  const int heldLeft = sc.catX - roamW() / 2, heldTop = sc.catY - Sz(kRoamCatHalf) - Y(kRoamMargin);
  const int bw = full ? X(240) : roamW(), bh = full ? Y(240) : roamH();
  const int left = full ? 0 : heldLeft, top = full ? 0 : heldTop;
  if (!full && (abs(left - lastX) > X(kRoamMargin) || abs(top - lastY) > Y(kRoamMargin))) C().clear(color::BG);
  lastX = heldLeft, lastY = heldTop, wasFloor = sc.floor;  // the held box's spot, even on a full frame
  auto draw = [&] {
    C().fillRect(left, top, bw, bh, color::BG);
    if (sc.catBehind) deskMascot(sc.catX, sc.catY, sc.k, kRoamCatHalf, false, false);
    drawSign(sc, clk.hhmm, lim, reset, resetFg, lastName, nameFg, lastWhen, computerAway);
    if (!sc.catBehind) deskMascot(sc.catX, sc.catY, sc.k, kRoamCatHalf, false, false);  // over the sign
    for (uint8_t i = 0; i < sc.nProps; i++) drawProp(sc.props[i]);
  };
  // In strips, like the desk mascot: no big heap block, no flash.
  const int stripH = (bh + kCatStrips - 1) / kCatStrips;
  for (int y = top; y < top + bh; y += stripH) {
    const int hh = y + stripH <= top + bh ? stripH : top + bh - y;
    if (!C().beginLayer(left, y, bw, hh)) {
      draw();
      break;
    }
    draw();
    C().endLayer();
  }
  C().releaseLayer();
}

#ifdef PIO_UNIT_TESTING
void drawPropForTest(uint8_t kind, int x, int y, uint8_t f, int x2) { drawProp(Prop{(PropKind)kind, x, y, f, x2}); }
#endif

// ---- visits between Miblos (miblo_friends.h) ----
void drawPropKind(uint8_t kind, int x, int y, uint8_t f) { drawProp(Prop{(PropKind)kind, x, y, f, 0}); }

void addItem(VisitFrame& f, uint8_t kind, int x, int y, uint8_t fr) {
  // Not a kind anyone draws (none, past the PropKinds, past the visit files' ranges): dropped.
  if (kind == vprop::None || (kind >= vprop::Count && kind < kItemsA) || kind >= kItemsEnd) return;
  if (f.n < sizeof(f.items) / sizeof(f.items[0])) f.items[f.n++] = VisitItem{kind, x, y, fr};
}

#ifdef PIO_UNIT_TESTING
void (*visitItemHookForTest)(bool drawing) = nullptr;
#endif

static void drawVisitItem(const VisitItem& it) {
#ifdef PIO_UNIT_TESTING
  if (visitItemHookForTest) visitItemHookForTest(true);
#endif
  if (it.kind < kItemsA) drawPropKind(it.kind, it.x, it.y, it.f);
  else if (it.kind < kItemsB) drawVisitItemA(it);
  else if (it.kind < kItemsC) drawVisitItemB(it);
  else drawVisitItemC(it);
#ifdef PIO_UNIT_TESTING
  if (visitItemHookForTest) visitItemHookForTest(false);
#endif
}

// Both cats walk on one horizontal band, recomposed in strips on every change (no trail, no flash).
constexpr int kVisitCatY = 104;

// Position along a walk: from `a` to `b` over [t0, t0 + kVisitWalkMs).
static int walk(uint32_t ms, uint32_t t0, int a, int b) {
  if (ms <= t0) return a;
  if (ms >= t0 + miblo::kVisitWalkMs) return b;
  return a + (int)((int64_t)(b - a) * (int32_t)(ms - t0) / (int32_t)miblo::kVisitWalkMs);
}

// A walking cat: bobbing, eyes towards where it goes (dx, dy: -1, 0 or 1).
static MascotLook walking(uint32_t ms, int dx, int dy = 0) {
  MascotLook k{0, (int8_t)((ms / 180) % 2 ? -2 : 0), (int8_t)(3 * dx), (int8_t)(3 * dy), Eyes::Open, Paws::Down, 0};
  return k;
}

void visit(Lang lang, const Snapshot& s, const Clock& clk, const miblo::VisitView& v, uint8_t side) {
  using miblo::VisitRole;
  const uint32_t ms = v.ms;
  // A host may have a group (1:2, 1:3): up to four cats side by side, smaller the more there are.
  const uint8_t guests = v.role == VisitRole::Host ? (uint8_t)(1 + v.extra) : 1;
  const uint8_t cats = (uint8_t)(1 + guests);
  const int catHalf = cats <= 2 ? kVisitHalfPair : cats == 3 ? kVisitHalf3 : kVisitHalf4;
  const int half = Sz(catHalf);
  const int cy = Y(kVisitCatY);
  const bool coffee = v.gift == miblo::Gift::Coffee;
  VisitFrame frame{};  // the activity's props (up to four), drawn over the cats

  // Which way the friends are (config friendsSide): our cat leaves that way, a guest comes from it.
  const bool horiz = side < 2;
  const int dir = side == 1 ? -1 : 1;   // sideways: +1 right, -1 left
  const int dirY = side == 2 ? -1 : 1;  // up or down: -1 above, +1 below
  const int offH = dir > 0 ? X(240) + half + X(2) : -half - X(2);
  const int offV = dirY < 0 ? -half - Y(2) : Y(240) + half + Y(2);
  // Slots across the band: the host at the far end, the guests from next to it towards the side they
  // came from (the first guest, who does the activity, right next to the host).
  auto slot = [&](int i) { return cats == 2 ? X(120) + (i ? X(40) : -X(40)) : X(240) * (2 * i + 1) / (2 * cats); };
  const int hostIdx = horiz && dir > 0 ? 0 : cats - 1;
  const int step = hostIdx == 0 ? 1 : -1;
  const int guestSlot = slot(hostIdx + step);
  const int hostSlot = slot(hostIdx);
  const int mid = (hostSlot + guestSlot) / 2;  // between the host and the first guest: the props go there
  const int toHost = hostSlot > guestSlot ? 1 : -1;            // from the guest towards the host
  const Paws guestReach = toHost > 0 ? Paws::ReachRight : Paws::ReachLeft;
  const Paws hostReach = toHost > 0 ? Paws::ReachLeft : Paws::ReachRight;

  // Where each cat is and how it looks.
  bool mine = true, guest = false;
  bool together = false;  // host: the guests are in, doing the activity
  int myX = X(120), myY = cy, guestX = offH, guestY = cy;
  MascotLook me = deskLook(DeskMood::Calm, true, ms), them{};
  if (v.role == VisitRole::Visitor) {
    const uint32_t back = miblo::kVisitMs - miblo::kVisitWalkMs;
    if (ms < miblo::kVisitWalkMs || ms >= back) {  // walking out, or back home
      const bool out = ms < miblo::kVisitWalkMs;
      if (horiz) myX = out ? walk(ms, 0, X(120), offH) : walk(ms, back, offH, X(120));
      else myY = out ? walk(ms, 0, cy, offV) : walk(ms, back, offV, cy);
      me = horiz ? walking(ms, out ? dir : -dir) : walking(ms, 0, out ? dirY : -dirY);
      if (!out && v.turnedAway) {  // the host's human got back to work: home, sulking
        me.eyes = Eyes::Sleepy;
        me.gy = 3;
        me.extras |= kSweat;
      }
    } else {
      mine = false;
    }
  } else {
    myX = hostSlot;
    const uint32_t in0 = miblo::kVisitWalkMs, out0 = miblo::kVisitPartMs;
    guest = ms >= in0 && ms < out0 + miblo::kVisitWalkMs;
    guestX = guestSlot;
    if (ms < miblo::kVisitArriveMs || ms >= out0) {  // the guest walking in, or leaving
      const bool in = ms < miblo::kVisitArriveMs;
      if (horiz) guestX = in ? walk(ms, in0, offH, guestSlot) : walk(ms, out0, guestSlot, offH);
      else guestY = in ? walk(ms, in0, offV, cy) : walk(ms, out0, cy, offV);
      them = horiz ? walking(ms, in ? -dir : dir) : walking(ms, 0, in ? -dirY : dirY);
      if (coffee && in) them.extras |= kCoffee;
      me.gx = (int8_t)(horiz ? -3 * toHost : 0);  // looking at the door
      me.gy = (int8_t)(horiz ? 0 : 3 * dirY);
    } else {
      // Together: the visit's activity (miblo::Gift), happy hops and a heart around it.
      together = true;
      const uint32_t t = ms - miblo::kVisitArriveMs;
      const uint32_t stay = miblo::kVisitStayMs;
      const bool firstHalf = t < stay / 2;
      them = MascotLook{0, (int8_t)((t / 400) % 3 == 0 ? -4 : 0), (int8_t)(3 * toHost), 0, Eyes::Happy, Paws::Down, 0};
      me = MascotLook{0, (int8_t)((t / 400) % 3 == 1 ? -4 : 0), (int8_t)(-3 * toHost), 0, Eyes::Happy, Paws::Down, 0};
      // The props stand on the cats' floor (their bottom edge), smaller cats in a group or not.
      const int ground = cy + half;
      const MascotLook watchL{0, 0, (int8_t)(3 * toHost), 3, Eyes::Open, Paws::Down, 0};   // the guest, at the middle
      const MascotLook watchR{0, 0, (int8_t)(-3 * toHost), 3, Eyes::Open, Paws::Down, 0};  // the host, at the middle
      switch (v.gift) {
        case miblo::Gift::Coffee:  // a coffee changes hands halfway, then a heart
          if (firstHalf) them.extras |= kCoffee;
          else me.extras |= kCoffee | kHeart;
          break;
        case miblo::Gift::Duck:  // rubber duck debugging: held out, then set down and explained to
          if (firstHalf) {
            them.paws = guestReach;
            addItem(frame, vprop::Duck, guestX + toHost * Sz(28), ground - Sz(18), 0);
          } else {
            them = watchL;
            me = watchR;
            me.eyes = (t / 1200) % 3 == 2 ? Eyes::Happy : Eyes::Open;  // the "aha!" moments
            addItem(frame, vprop::Duck, mid, ground - Sz(12) - ((t / 500) % 2 ? Sz(2) : 0), 0);
          }
          break;
        case miblo::Gift::Pair:  // pair programming: both typing on a tiny laptop, then it compiles
          if (t < stay - 2500) {
            them = watchL;
            me = watchR;
            if ((t / 250) % 2) them.paws = guestReach;
            else me.paws = hostReach;
          } else {
            me.extras |= kHeart;
          }
          addItem(frame, vprop::Laptop, mid, ground - Sz(6), (uint8_t)(t / 300));
          break;
        case miblo::Gift::Review:  // code review: the guest holds up "LGTM", the host reads it
          if (firstHalf) {
            them.paws = guestReach;
            me = watchR;
            addItem(frame, vprop::Lgtm, toHost > 0 ? guestX + Sz(4) : guestX - Sz(48), ground - Sz(24), 0);
          } else {
            me.extras |= kHeart;
          }
          break;
        case miblo::Gift::Bug: {  // a bug runs back and forth, both follow it, the host catches it
          const uint32_t caught = 15000;
          const int bugX = mid - Sz(26) + shuttle(t, 3000, Sz(52));
          if (t < caught) {
            them = watchL;
            me = watchR;
            them.eyes = me.eyes = Eyes::Wide;
            them.gx = (int8_t)((bugX - guestX) * toHost > Sz(20) ? 3 * toHost : 0);
            me.gx = (int8_t)((myX - bugX) * toHost > Sz(20) ? -3 * toHost : 0);
            if (t >= caught - 1500) {
              me.paws = hostReach;  // pounce
              me.dy = -3;
            }
            addItem(frame, vprop::Bug, bugX, ground - Sz(7), (uint8_t)(t / 120));
          } else if (t < caught + 900) {
            addItem(frame, vprop::Burst, mid, ground - Sz(10), 0);
          } else {
            me.extras |= kHeart;
          }
          break;
        }
        case miblo::Gift::Deploy: {  // a rocket on the pad, lift-off (both look up), then cheers
          const uint32_t launch = 3000, gone = 9000;
          if (t < launch) {
            them = watchL;
            me = watchR;
            addItem(frame, vprop::Rocket, mid, ground - Sz(28), 0);
          } else if (t < gone) {
            them = MascotLook{0, 0, (int8_t)(2 * toHost), -3, Eyes::Wide, Paws::Down, 0};
            me = MascotLook{0, 0, (int8_t)(-2 * toHost), -3, Eyes::Wide, Paws::Down, 0};
            const int y = ground - Sz(28) - (int)((int64_t)Sz(48) * (int32_t)(t - launch) / (int32_t)(gone - launch));
            addItem(frame, vprop::Rocket, mid, y, (uint8_t)(1 + (t / 120) % 2));
          } else {
            me.extras |= kHeart;
          }
          break;
        }
        default: {  // the 30 newer activities, scripted in ui_visit_a/b/c.cpp
          const VisitStage st{t, hostSlot, guestX, mid, cy, half, toHost, Y(30), cy + half};
          frame.me = me;
          frame.them = them;
          if (visitActivityA(v.gift, st, frame) || visitActivityB(v.gift, st, frame) ||
              visitActivityC(v.gift, st, frame)) {
            me = frame.me;
            them = frame.them;
            break;
          }
          frame.n = 0;
          if ((t / 1500) % 4 == 3) me.extras |= kHeart;  // none (or not scripted yet): just hearts
          break;
        }
      }
    }
  }
  // The rest of a group: they walk in and out with the first guest and cheer along.
  int exX[miblo::kMaxGuests - 1] = {}, exY[miblo::kMaxGuests - 1] = {};
  MascotLook exLook[miblo::kMaxGuests - 1] = {};
  const uint8_t extras = guest ? v.extra : 0;
  for (uint8_t e = 0; e < extras; e++) {
    const int sx = slot(hostIdx + step * (2 + e));
    exX[e] = sx;
    exY[e] = cy;
    const uint32_t in0 = miblo::kVisitWalkMs, out0 = miblo::kVisitPartMs;
    if (ms < miblo::kVisitArriveMs || ms >= out0) {
      const bool in = ms < miblo::kVisitArriveMs;
      if (horiz) exX[e] = in ? walk(ms, in0, offH, sx) : walk(ms, out0, sx, offH);
      else exY[e] = in ? walk(ms, in0, offV, cy) : walk(ms, out0, cy, offV);
      exLook[e] = horiz ? walking(ms + 90 * (e + 1), in ? -dir : dir) : walking(ms + 90 * (e + 1), 0, in ? -dirY : dirY);
    } else {
      const uint32_t t = ms - miblo::kVisitArriveMs;
      exLook[e] = MascotLook{0, (int8_t)((t / 400 + e + 2) % 3 == 0 ? -4 : 0), (int8_t)(3 * toHost), 0,
                             (t / 1600 + e) % 3 ? Eyes::Happy : Eyes::Open, Paws::Down, 0};
    }
  }

  // Walking up or down, a cat crosses the whole screen: then everything is composed (the clock,
  // the text and the limits come back once it is out of the way).
  static bool fullScreen = false;
  bool vertical = (mine && myY != cy) || (guest && guestY != cy);
  for (uint8_t e = 0; e < extras; e++) vertical |= exY[e] != cy;
  if (fullScreen && !vertical) {
    fullScreen = false;
    reset();  // clears and redraws everything below
  }
  fullScreen = vertical;
  // Together, the props may go up to Y(30) (just under the clock); walking, only the cats' band is
  // redrawn, so what a prop left above it is cleared once.
  static bool tall = false;
  const bool wasTall = tall;
  tall = together && !vertical;
  if (wasTall && !tall && !vertical) C().fillRect(0, Y(30), X(240), cy - half - Y(30), color::BG);
  if (!vertical) {
    field(R_CLOCK, kHashSeed + 59, X(120), Y(18), clk.hhmm, Font::Body, color::DIM, color::BG, Align::Center, X(80));
  }

  uint32_t h = hashInt(hashInt(kHashSeed + 61 + tall, (uint32_t)(mine ? myX * 1000 + myY + 1 : 0)),
                       (uint32_t)(guest ? guestX * 1000 + guestY + 1 : 0));
  h = hashInt(hashInt(h, lookHash(kHashSeed, me)), lookHash(kHashSeed + 1, them));
  for (uint8_t i = 0; i < frame.n; i++) {
    const VisitItem& it = frame.items[i];
    h = hashInt(hashInt(h, (uint32_t)it.kind | (uint32_t)it.f << 8), (uint32_t)(it.x * 1000 + it.y));
  }
  for (uint8_t e = 0; e < extras; e++) {
    h = hashInt(lookHash(hashInt(h, (uint32_t)(exX[e] * 1000 + exY[e])), exLook[e]), v.extraMascot[e]);
  }
  if (dirty(R_BODY, h)) {
    const int top = vertical ? 0 : tall ? Y(30) : cy - half;
    const int bh = vertical ? Y(240) : tall ? cy + half - Y(30) : 2 * half;
    const uint8_t myStyle = mascotStyle(), myHat = mascotAccessory();
    auto draw = [&] {
      C().fillRect(0, top, X(240), bh, color::BG);
      if (mine) deskMascot(myX, myY, me, catHalf, false);
      if (guest) {  // in their own colours, no hat (the special day is ours)
        setMascotAccessory(0);
        setMascotStyle(v.mascot);
        deskMascot(guestX, guestY, them, catHalf, false);
        for (uint8_t e = 0; e < extras; e++) {
          setMascotStyle(v.extraMascot[e]);
          deskMascot(exX[e], exY[e], exLook[e], catHalf, false);
        }
        setMascotStyle(myStyle);
        setMascotAccessory(myHat);
      }
      for (uint8_t i = 0; i < frame.n; i++) drawVisitItem(frame.items[i]);
    };
    const int stripH = (bh + kCatStrips - 1) / kCatStrips;
    for (int y = top; y < top + bh; y += stripH) {
      const int sh = y + stripH <= top + bh ? stripH : top + bh - y;
      if (!C().beginLayer(0, y, X(240), sh)) {
        draw();
        break;
      }
      draw();
      C().endLayer();
    }
    C().releaseLayer();
  }

  // What is going on, under the cats.
  char buf[96];
  if (v.role == VisitRole::Visitor) {
    snprintf(buf, sizeof(buf), t(lang, v.turnedAway ? S::FriendBusy : S::FriendAway), v.name);
    if (mine && !v.turnedAway) buf[0] = 0;  // still on screen (leaving or back home)
  } else {
    static const S kLine[] MIBLO_ROM = {S::FriendVisiting, S::FriendCoffee, S::FriendDuck, S::FriendPair,
                              S::FriendReview, S::FriendBug, S::FriendDeploy, S::FriendHighFive,
                              S::FriendPingPong, S::FriendDance, S::FriendPizza, S::FriendCake,
                              S::FriendMerge, S::FriendStandup, S::FriendHackathon, S::FriendSelfie,
                              S::FriendChess, S::FriendGame, S::FriendGossip, S::FriendToast, S::FriendMovie,
                              S::FriendBlocks, S::FriendBrainstorm, S::FriendPomodoro, S::FriendHotfix,
                              S::FriendTests, S::FriendNotFound, S::FriendShipIt, S::FriendSprint,
                              S::FriendOrigami, S::FriendNostalgia, S::FriendPanic, S::FriendPicnic,
                              S::FriendFishing, S::FriendUmbrella, S::FriendCanPhone, S::FriendKite};
    static_assert(sizeof(kLine) / sizeof(kLine[0]) == (size_t)miblo::Gift::Count, "one line per activity");
    const uint8_t g = (uint8_t)v.gift < (uint8_t)miblo::Gift::Count ? (uint8_t)v.gift : 0;
    char who[40];
    if (v.extra) snprintf(who, sizeof(who), "%s +%u", v.name, (unsigned)v.extra);  // a group
    else snprintf(who, sizeof(who), "%s", v.name);
    S line;
    mibloRomCopy(&line, &kLine[g], sizeof(S));  // S is 16 bits: copy it whole from flash
    snprintf(buf, sizeof(buf), t(lang, line), who);
    if (!guest) buf[0] = 0;
  }
  if (vertical) return;
  const uint32_t ht = hashStr(hashInt(kHashSeed + 67, (uint32_t)lang), buf);
  if (region(R_ROW0, ht, 0, Y(150), X(240), Y(40)) && buf[0]) {
    C().text(X(120), Y(176), buf, Font::BodyBold, color::AMBER, Align::Center, X(228));
  }
  compactLimits(R_LIMITS, lang, s, Y(206), Y(30), Y(226), color::BG);
}

// ---- greetings ----

// Confetti along the top and the bottom bands (R_HEADER, R_FOOT), reshuffled when `frame`
// changes (hello() passes ms / 250: a few times a second).
void confettiBands(uint32_t frame) {
  static const uint16_t kColors[] MIBLO_ROM = {color::AMBER, color::GREEN,  color::BLUE,
                                               color::CORAL, color::VIOLET, color::RED};
  for (uint8_t band = 0; band < 2; band++) {
    const int y0 = band ? Y(216) : Y(4), bandH = Y(20);
    if (!region(band ? R_FOOT : R_HEADER, hashInt(kHashSeed + 71 + band, frame), 0, y0, X(240), bandH)) continue;
    uint32_t r = frame * 2654435761u + band * 97u + 1;
    for (int i = 0; i < 14; i++) {
      r = r * 1103515245u + 12345u;
      const int x = X(6) + (int)((r >> 8) % (uint32_t)X(224));
      const int y = y0 + (int)((r >> 20) % (uint32_t)(bandH - Sz(5)));
      uint16_t c;
      mibloRomCopy(&c, &kColors[(r >> 4) % 6], sizeof(c));
      C().fillRect(x, y, Sz(5), Sz(3) + (int)(r % 3), c);
    }
  }
}

void hello(const char* line1, const char* line2, bool party, uint32_t ms) {
  deskCat(R_BODY, X(120), Y(90), 56, deskLook(DeskMood::Celebrate, true, ms));
  if (party) confettiBands(ms / 250);
  const uint32_t h = hashStr(hashStr(hashInt(kHashSeed + 73, party), line1), line2);
  if (region(R_LIMITS, h, 0, Y(152), X(240), Y(62))) {
    const bool two = line1 && line1[0];
    if (two) C().text(X(120), Y(172), line1, Font::Body, color::MUTED, Align::Center, X(228));
    // The big line in the title font when it fits, else a size down (long phrases, some languages).
    const Font big = C().textWidth(line2, Font::Title) <= X(228) ? Font::Title : Font::BodyBold;
    C().text(X(120), two ? Y(202) : Y(190), line2, big, party ? color::AMBER : color::TEXT, Align::Center, X(228));
  }
}

// The Desk's top-right corner, right of the cat's box (which starts at `left`): the second clock,
// its name over its time. Both fields: a new minute redraws only the time. Nothing when it is off
// (turning it on or off redraws the whole screen: app.cpp's updateDailyLook).
static void deskZone(int left) {
  const char* label = secondClockLabel();
  const char* hhmm = secondClockTime();
  if (!label[0] || !hhmm[0]) return;
  const int w = X(228) - left;
  field(R_ROW0, kHashSeed + 59, X(228), Y(18), label, Font::Small, color::DIM, color::BG, Align::Right, w);
  field(R_ZONE, kHashSeed + 59, X(228), Y(36), hhmm, Font::SmallBold, color::DIM, color::BG, Align::Right, w);
}

void desk(Lang lang, const Snapshot& s, const Clock& clk, uint32_t nowMs, uint32_t exhaustAt) {
  const uint32_t now = clk.epoch ? clk.epoch : s.now;
  const uint8_t p5 = deskPct(s.h5, now);
  const uint8_t p7 = deskPct(s.d7, now);
  const bool usage = s.hasUsage && (s.h5.present || s.d7.present);
  bool focusLeft;
  const DeskMood mood = deskMoodFor(s, now, &focusLeft);
  field(R_CLOCK, kHashSeed + 19, X(120), Y(18), clk.hhmm, Font::Body, color::DIM, color::BG, Align::Center, X(80));
  const int catHalf = 48;
  deskCat(R_BODY, X(120), Y(72), catHalf, deskLook(mood, focusLeft, nowMs));
  deskZone(X(120) + Sz(catHalf) + Sz(4));
  uint32_t h = hashInt(hashInt(kHashSeed + 23, (uint32_t)lang), usage);
  h = hashInt(hashInt(h, s.h5.present ? p5 : 255), s.d7.present ? p7 : 255);
  h = hashInt(h, (uint32_t)(s.todayUsd * 100));
  if (region(R_LIMITS, h, 0, Y(121), X(240), Y(100))) {
    if (!usage) {
      noLimits(lang, s, Y(176));
    } else {
      ring(X(62), Y(170), t(lang, S::Short5h), s.h5.present, p5, color::CORAL);
      ring(X(178), Y(170), t(lang, S::Short7d), s.d7.present, p7, color::VIOLET);
    }
  }
  // Under each ring, when its reset is known and still ahead: "in 2h10" (5h), "Fri 19:32" (week).
  // Fields: the countdown ticks once a minute without touching the rings.
  // The 5h line becomes "runs out in 1h20" (amber) when the recent pace ends it before the reset.
  char buf[48];
  buf[0] = 0;
  const bool burning = usage && s.h5.present && exhaustAt > now;
  if (burning) {
    char left[16];
    miblo::formatCountdown(exhaustAt - now, left, sizeof(left));
    snprintf(buf, sizeof(buf), t(lang, S::RunsOutIn), left);
  } else if (usage && s.h5.present && s.h5.reset > now) {
    char left[16];
    miblo::formatCountdown(s.h5.reset - now, left, sizeof(left));
    snprintf(buf, sizeof(buf), t(lang, S::InTime), left);
  }
  field(R_RESET5, h, X(62), Y(233), buf, burning ? Font::Small : Font::Body, burning ? color::AMBER : color::MUTED,
        color::BG, Align::Center, X(118));
  buf[0] = 0;
  if (usage && s.d7.present && s.d7.reset > now) formatWhen(lang, s.d7.reset, now, buf, sizeof(buf));
  field(R_RESET7, h, X(178), Y(233), buf, Font::Body, color::MUTED, color::BG, Align::Center, X(114));
}

void limitReset(Lang lang, const Snapshot& s, const Clock& clk, uint32_t ms) {
  const uint32_t hh = hashInt(kHashSeed + 31, (uint32_t)lang);
  if (region(R_HEADER, hh, 0, 0, X(240), Y(30), color::GREEN)) {
    C().text(X(120), Y(21), t(lang, S::LimitFreed), Font::BodyBold, color::BLACK, Align::Center, X(228));
  }
  deskCat(R_BODY, X(120), Y(90), 48, deskLook(DeskMood::Celebrate, true, ms));
  const uint32_t now = clk.epoch ? clk.epoch : s.now;
  uint32_t h = hashInt(hashInt(hh, s.h5.present ? s.h5.pct : 255), s.h5.reset);
  if (region(R_LIMITS, h, 0, Y(142), X(240), Y(98))) {
    char buf[48];
    snprintf(buf, sizeof(buf), "%u%%", s.h5.present ? (unsigned)s.h5.pct : 0u);
    C().text(X(120), Y(180), buf, Font::NumL, color::GREEN, Align::Center, X(200));
    C().text(X(120), Y(202), t(lang, S::Session5h), Font::Body, color::MUTED, Align::Center, X(228));
    if (s.h5.present && s.h5.reset) {
      char when[32];
      formatWhen(lang, s.h5.reset, now, when, sizeof(when));
      snprintf(buf, sizeof(buf), t(lang, S::ResetsAt), when);
      C().text(X(120), Y(228), buf, Font::Small, color::DIM, Align::Center, X(228));
    }
  }
}

void updateAvailable(Lang lang, const char* current, const char* latest, uint8_t frame) {
  if (dirty(R_BODY, hashInt(kHashSeed + 47, mascotPose(frame)))) {
    const int half = Sz(48);
    const bool layered = C().beginLayer(X(120) - half, Y(66) - half, 2 * half, 2 * half);
    mascot(X(120), Y(66), frame);
    if (layered) {
      C().endLayer();
      C().releaseLayer();
    }
  }
  const uint32_t h = hashStr(hashStr(hashInt(kHashSeed + 53, (uint32_t)lang), current), latest);
  if (region(R_LIMITS, h, 0, Y(118), X(240), Y(122))) {
    char buf[64];
    C().text(X(120), Y(142), t(lang, S::UpdateAvailable), Font::BodyBold, color::TEXT, Align::Center, X(232));
    snprintf(buf, sizeof(buf), t(lang, S::UpdateVersions), latest, current);
    C().text(X(120), Y(166), buf, Font::Body, color::AMBER, Align::Center, X(232));
    C().text(X(120), Y(192), t(lang, S::RunInClaude), Font::Small, color::MUTED, Align::Center, X(232));
    C().fillRoundRect(X(40), Y(200), X(160), Y(28), Sz(4), color::CMD_BG);
    C().text(X(120), Y(220), "/miblo:update", Font::BodyBold, color::TEXT, Align::Center, X(152));
  }
}

void summary(Lang lang, const Snapshot& s, const Clock& clk) {
  const uint32_t hh = hashInt(kHashSeed + 37, (uint32_t)lang);
  if (region(R_HEADER, hh, 0, 0, X(240), Y(26))) {
    C().text(X(12), Y(18), t(lang, S::TodayTitle), Font::SmallBold, color::DIM, Align::Left, X(150));
  }
  clockRight(hh, clk, Y(18), color::DIM, color::BG);
  uint32_t h = hashInt(hashInt(hashInt(hh, s.todayTurns), s.todayWorkSec / 60), (uint32_t)(s.todayUsd * 100));
  if (region(R_BODY, h, 0, Y(26), X(240), Y(172))) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%u", (unsigned)s.todayTurns);
    C().text(X(120), Y(84), buf, Font::NumL, color::TEXT, Align::Center, X(220));
    C().text(X(120), Y(108), t(lang, S::SumResponses), Font::Body, color::MUTED, Align::Center, X(228));
    C().fillRect(X(24), Y(126), X(192), 1, color::DIVIDER);
    if (s.todayWorkSec < 60) snprintf(buf, sizeof(buf), "0min");
    else miblo::formatCountdown(s.todayWorkSec, buf, sizeof(buf));
    C().text(X(64), Y(164), buf, Font::Title, color::GREEN, Align::Center, X(112));
    C().text(X(64), Y(186), t(lang, S::SumWorked), Font::Small, color::MUTED, Align::Center, X(112));
    miblo::formatUsd(s.todayUsd, buf, sizeof(buf));
    C().text(X(176), Y(164), buf, Font::Title, color::TEXT, Align::Center, X(112));
    C().text(X(176), Y(186), t(lang, S::SumSpent), Font::Small, color::MUTED, Align::Center, X(112));
  }
  compactLimits(R_LIMITS, lang, s, Y(206), Y(30), Y(226), color::BG);
}

void disconnected(Lang lang, const Clock& clk, const char* ip, const char* mdnsHost, const char* pairCode,
                  uint32_t nowMs, uint32_t awayMs) {
  const uint32_t hh = hashInt(kHashSeed + 29, (uint32_t)lang);
  if (region(R_HEADER, hh, 0, 0, X(240), Y(26))) {
    C().fillCircle(X(16), Y(13), Sz(4), color::RED);
    C().text(X(26), Y(18), t(lang, S::Disconnected), Font::SmallBold, color::DIM, Align::Left, X(150));
  }
  clockRight(hh, clk, Y(18), color::DIM, color::BG);
  const DeskMood mood = awayMs >= miblo::kAwayNapMs ? DeskMood::Asleep : DeskMood::Searching;
  deskCat(R_BODY, X(120), Y(96), 64, deskLook(mood, true, nowMs));
  // "Waiting for the computer" with dots that come and go.
  char buf[128];
  const unsigned dots = (unsigned)(nowMs / 600 % 4);
  snprintf(buf, sizeof(buf), "%s%s", t(lang, S::WaitingComputer), "..." + (3 - dots));
  field(R_ROW0, hh, X(120), Y(180), buf, Font::Body, color::MUTED, color::BG, Align::Center, X(232));
  const uint32_t hf = hashStr(hashStr(hashStr(hh, ip), mdnsHost), pairCode);
  if (region(R_FOOT, hf, 0, Y(206), X(240), Y(34))) {
    char line[64];
    snprintf(line, sizeof(line), "%s \xC2\xB7 %s.local", ip, mdnsHost);
    C().text(X(120), Y(215), line, Font::Small, color::FAINT, Align::Center, X(232));
    snprintf(line, sizeof(line), "%s %s", t(lang, S::PairingCode), pairCode);
    C().text(X(120), Y(232), line, Font::Small, color::FAINT, Align::Center, X(232));
  }
}

}  // namespace screens
