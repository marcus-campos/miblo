#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "miblo_activity.h"
#include "miblo_format.h"
#include "miblo_policy.h"
#include "miblo_rom.h"
#include "ui_screens.h"

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

// Regions (RegionCache ids) of the main screens.
// Values that change in place (clock, timers, countdowns, page) have their own field regions
// (see field()), so a tick never clears the block they sit in.
enum : uint8_t {
  R_HEADER = 0, R_LIMITS = 1, R_WEEK = 2, R_DIVIDER = 3, R_ROW0 = 4, R_BODY = 9, R_FOOT = 10, R_TIME0 = 11,
  R_CLOCK = 14, R_RESET5 = 15, R_RESET7 = 16, R_PAGE = 17, R_BURN = 18
};
// Session cards per page, in the Overview (Working / Needs you) and in the Sessions mode.
constexpr uint8_t kRows = 3;

static ui::Canvas& C() { return canvas(); }
static const char* const kDot = " \xC2\xB7 ";  // " · "

static uint16_t levelColor(uint8_t pct, uint16_t base) {
  if (pct >= 95) return color::RED;
  if (pct >= 80) return color::AMBER;
  return base;
}

static uint16_t stateColor(SessionState st) {
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

static uint32_t since(const SessionRow& r, const Clock& clk) {
  return (clk.epoch && clk.epoch > r.since) ? clk.epoch - r.since : 0;
}

// Clock at the right of a header (baseline y), on the header's colour. `parent` = the header
// region's hash, so a redrawn header always gets its clock back.
static void clockRight(uint32_t parent, const Clock& clk, int y, uint16_t fg, uint16_t bg) {
  field(R_CLOCK, parent, X(228), y, clk.hhmm, Font::Small, fg, bg, Align::Right, X(44));
}

// "Opus · ctx 71% · 412k tok" (missing parts are omitted; tok = the session's context tokens).
static void metaLine(const SessionRow& r, char* out, size_t cap) {
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

// ---------------- alerts ----------------

void flash(Lang lang, AlertKind kind, const char* name, uint32_t elapsedMs) {
  (void)lang;
  const bool amber = kind != AlertKind::Done;
  const bool on = ((elapsedMs / kFlashPhaseMs) % 2) == 0;
  const uint16_t bg = on ? (amber ? color::AMBER : color::FLASH_BLUE) : color::BG;
  const uint16_t fg = on ? (amber ? color::BLACK : color::WHITE) : (amber ? color::AMBER : color::BLUE);
  if (!region(R_BODY, hashStr(hashInt(hashInt(kHashSeed, amber), on), name), 0, 0, X(240), Y(240), bg)) return;
  if (amber) {  // "!" in a circle
    C().fillCircle(X(120), Y(90), Sz(30), fg);
    C().fillRect(X(116), Y(70), Sz(8), Y(26), bg);
    C().fillRect(X(116), Y(102), Sz(8), Sz(8), bg);
  } else {
    C().wideLine(X(96), Y(92), X(112), Y(108), Sz(8), fg, bg);
    C().wideLine(X(112), Y(108), X(146), Y(72), Sz(8), fg, bg);
  }
  C().text(X(120), Y(160), name, Font::Hero, fg, Align::Center, X(224));
}

void hero(Lang lang, const Snapshot& s, int idx, AlertKind kind, bool discreet, const Clock& clk,
          const miblo::RunTracker& runs) {
  if (idx < 0 || idx >= s.count) return;
  const SessionRow& r = s.sessions[idx];
  const bool amber = kind != AlertKind::Done;
  char buf[160];
  char tmp[48];

  // Only what is displayed goes into the hash (the model/ctx/tokens line is only on "Finished").
  uint32_t h = hashStr(hashInt(hashInt(kHashSeed, (uint32_t)lang), (uint32_t)kind), r.id);
  h = hashStr(hashStr(hashStr(h, r.name), discreet ? "" : r.det), r.tool);
  if (!amber) {
    uint32_t dur = 0;
    h = hashStr(hashInt(hashInt(hashInt(h, (uint32_t)r.ctx), (uint32_t)r.tok), runs.stats(r.id, dur) ? dur : 0), r.model);
  }
  Compose head;
  if (head.begin(R_HEADER, h, 0, 0, X(240), Y(150))) {
    if (amber) {
      C().fillRect(0, 0, X(240), Y(4), color::AMBER);
      C().fillCircle(X(16), Y(15), Sz(4), color::AMBER);
      C().text(X(26), Y(20), t(lang, S::NeedsYou), Font::SmallBold, color::AMBER, Align::Left, X(200));
    } else {
      check(X(18), Y(14), Sz(12), color::BLUE);
      C().text(X(30), Y(20), t(lang, S::Finished), Font::SmallBold, color::BLUE, Align::Left, X(200));
    }
    C().text(X(12), Y(62), r.name, Font::Hero, color::TEXT, Align::Left, X(216));
    if (amber) {
      C().text(X(12), Y(92), t(lang, kind == AlertKind::Perm ? S::AskedPermission : S::AskedQuestion), Font::Body,
               color::AMBER, Align::Left, X(216));
      if (r.tool[0]) {
        C().fillRoundRect(X(12), Y(104), X(216), Y(32), Sz(4), color::CMD_BG);
        if (discreet || !r.det[0]) snprintf(buf, sizeof(buf), "%s", r.tool);
        else snprintf(buf, sizeof(buf), "%s: %s", r.tool, r.det);
        C().text(X(20), Y(125), buf, Font::Body, color::TEXT, Align::Left, X(200));
      }
    } else {
      uint32_t dur;
      if (runs.stats(r.id, dur)) {
        miblo::formatElapsed(dur, tmp, sizeof(tmp));
        snprintf(buf, sizeof(buf), t(lang, S::Took), tmp);
        C().text(X(12), Y(92), buf, Font::Body, color::TEXT, Align::Left, X(216));
      }
      metaLine(r, buf, sizeof(buf));
      C().text(X(12), Y(118), buf, Font::Small, color::MUTED, Align::Left, X(216));
    }
  }

  head.end();

  if (amber) {  // "Waiting for 3m": minute granularity, updated in place
    miblo::formatInState(since(r, clk), tmp, sizeof(tmp));
    snprintf(buf, sizeof(buf), t(lang, S::WaitingFor), tmp);
    field(R_BODY, kHashSeed, X(12), Y(166), buf, Font::Small, color::DIM, color::BG, Align::Left, X(216));
  }

  const miblo::StateCounts c = miblo::countStates(s);
  Compose foot;
  if (foot.begin(R_FOOT, hashInt(hashInt(hashInt(kHashSeed, c.running), c.idle), (uint32_t)lang), 0, Y(210),
                 X(240), Y(30))) {
    buf[0] = 0;
    if (c.running) snprintf(buf, sizeof(buf), t(lang, S::PlusRunning), (unsigned)c.running);
    if (c.idle) {
      snprintf(tmp, sizeof(tmp), t(lang, S::NIdle), (unsigned)c.idle);
      if (buf[0]) strncat(buf, kDot, sizeof(buf) - strlen(buf) - 1);
      strncat(buf, tmp, sizeof(buf) - strlen(buf) - 1);
    }
    C().text(X(12), Y(228), buf, Font::Small, color::FAINT, Align::Left, X(216));
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
static void limitsBlock(Lang lang, const Snapshot& s, const Clock& clk) {
  uint32_t h = hashInt(hashInt(kHashSeed, (uint32_t)lang), s.hasUsage);
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
        C().text(X(228), Y(58), buf, Font::NumL, color::TEXT, Align::Right, X(100));
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
  if (s.h5.present && s.h5.reset) {  // 0 = unknown (the plugin sent reset:null): no line
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
static void compactLimits(uint8_t id, Lang lang, const Snapshot& s, int top, int h, int y, uint16_t bg) {
  uint32_t hs = hashInt(hashInt(kHashSeed + 3, (uint32_t)lang), s.hasUsage);
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
  };
  const Win wins[2] = {{t(lang, S::Short5h), &s.h5, color::CORAL, X(10)},
                       {t(lang, S::Short7d), &s.d7, color::VIOLET, X(124)}};
  const int barH = Y(5) < 2 ? 2 : Y(5);
  for (const Win& win : wins) {
    C().text(win.x, y, win.label, Font::Small, color::MUTED, Align::Left, X(20));
    const bool present = win.w->present;
    const uint8_t pct = present ? win.w->pct : 0;
    bar(win.x + X(20), y - Y(8), X(48), barH, pct, levelColor(pct, win.base));
    if (present) snprintf(buf, sizeof(buf), "%u%%", pct);
    else snprintf(buf, sizeof(buf), "--");
    C().text(win.x + X(72), y, buf, Font::SmallBold, present ? levelColor(pct, color::TEXT) : color::DIM, Align::Left,
             X(34));
  }
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
static void sessionRow(uint8_t slot, Lang lang, const SessionRow* r, const Clock& clk, bool discreet,
                       const RowGeom& g) {
  const int y0 = g.top + slot * g.pitch;
  char line[160];
  char timeStr[16];
  line[0] = 0;
  timeStr[0] = 0;
  if (r) {
    miblo::sessionLine(lang, *r, discreet, line, sizeof(line));
    miblo::formatInState(since(*r, clk), timeStr, sizeof(timeStr));
  }
  const bool pending = r && isPending(r->st);
  const uint16_t cardBg = r ? (pending ? color::CARD_AMBER : color::CARD) : color::BG;
  const uint16_t timeFg = pending ? color::AMBER : color::DIM;
  const int timeX = X(226);
  const int timeW = X(62);
  uint32_t h = hashInt(hashInt(kHashSeed + 5, (uint32_t)g.top), (uint32_t)g.pitch);
  if (r) h = hashStr(hashStr(hashInt(h, (uint32_t)r->st), r->name), line);
  Compose row;
  if (row.begin(R_ROW0 + slot, h, 0, y0, X(240), g.pitch)) {
    (void)dirty(R_TIME0 + slot, hashStr(h, timeStr));  // the time is drawn with the card
    if (r) {
      const uint16_t sc = stateColor(r->st);
      C().fillRect(X(8), y0 + Y(2), X(224), g.pitch - Y(4), cardBg);
      C().fillRect(X(8), y0 + Y(2), Sz(3), g.pitch - Y(4), sc);
      C().fillCircle(X(20), y0 + g.l1 - Y(5), Sz(4), sc);
      C().text(X(30), y0 + g.l1, r->name, Font::BodyBold, pending ? color::AMBER : color::TEXT, Align::Left,
               X(130));
      C().text(X(16), y0 + g.l2, line, Font::Small, r->st == SessionState::Running ? color::MUTED : sc,
               Align::Left, X(208));
      C().textBox(timeX, y0 + g.l1, timeStr, Font::Small, timeFg, cardBg, Align::Right, timeW);
    }
    return;
  }
  if (r) field(R_TIME0 + slot, h, timeX, y0 + g.l1, timeStr, Font::Small, timeFg, cardBg, Align::Right, timeW);
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

static void overviewIdle(Lang lang, const Snapshot& s, const Clock& clk) {
  char buf[128];
  char tmp[48];
  const uint32_t hh = hashInt(kHashSeed + 7, (uint32_t)lang);
  if (region(R_HEADER, hh, 0, 0, X(240), Y(26))) {
    check(X(17), Y(12), Sz(12), color::BLUE);
    C().text(X(28), Y(18), t(lang, S::AllDone), Font::SmallBold, color::BLUE, Align::Left, X(150));
  }
  clockRight(hh, clk, Y(18), color::DIM, color::BG);
  limitsBlock(lang, s, clk);
  if (region(R_DIVIDER, 1, 0, Y(150), X(240), 2)) C().fillRect(X(12), Y(150), X(216), 1, color::DIVIDER);

  // footer: most recently finished session + today's cost (updated in place)
  const int last = miblo::lastFinished(s);
  buf[0] = 0;
  if (last >= 0) {
    miblo::formatInState(since(s.sessions[last], clk), tmp, sizeof(tmp));
    snprintf(buf, sizeof(buf), t(lang, S::FinishedAgo), s.sessions[last].name, tmp);
  }
  field(R_ROW0, kHashSeed, X(12), Y(168), buf, Font::Small, color::MUTED, color::BG, Align::Left, X(216));
  buf[0] = 0;
  if (s.todayUsd > 0.0f) {
    miblo::formatUsd(s.todayUsd, tmp, sizeof(tmp));
    snprintf(buf, sizeof(buf), t(lang, S::CostToday), tmp);
  }
  field(R_ROW0 + 1, kHashSeed, X(12), Y(186), buf, Font::Small, color::DIM, color::BG, Align::Left, X(216));
}

void overview(Lang lang, const Snapshot& s, miblo::Pager& pager, uint32_t nowMs, const Clock& clk, bool discreet) {
  const OverviewKind kind = miblo::classifyOverview(s);
  // The three layouts tile the screen differently: switching layout clears everything.
  static bool haveKind = false;
  static OverviewKind lastKind = OverviewKind::Idle;
  if (haveKind && kind != lastKind) reset();
  haveKind = true;
  lastKind = kind;
  if (kind == OverviewKind::Idle) {
    overviewIdle(lang, s, clk);
    return;
  }

  const miblo::StateCounts c = miblo::countStates(s);
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
    compactLimits(R_LIMITS, lang, s, Y(24), Y(22), Y(40), color::BG);
    g = {Y(46), Y(52), Y(20), Y(40)};
  } else {
    // Brand row (logo, "miblo", clock), then the limits strip, then the cards.
    const uint32_t hb = hashInt(kHashSeed + 41, 1);
    if (region(R_HEADER, hb, 0, 0, X(240), Y(24))) {
      logo(X(19), Y(12), Sz(22));
      C().text(X(35), Y(21), "miblo", Font::Brand, color::TEXT, Align::Left, X(120));
    }
    clockRight(hb, clk, Y(18), color::DIM, color::BG);
    compactLimits(R_LIMITS, lang, s, Y(24), Y(22), Y(40), color::BG);
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
  // At the recent pace it runs out before it resets: "runs out in 1h20", in amber, in the arc's gap.
  buf[0] = 0;
  if (s.hasUsage && s.h5.present && exhaustAt > clk.epoch && clk.epoch) {
    char left[16];
    miblo::formatCountdown(exhaustAt - clk.epoch, left, sizeof(left));
    snprintf(buf, sizeof(buf), t(lang, S::RunsOutIn), left);
  }
  field(R_BURN, h, cx, Y(174), buf, Font::Small, color::AMBER, color::BG, Align::Center, Sz(110));
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
  uint8_t extras;
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

static void deskCat(uint8_t id, int cx, int cy, int half240, const MascotLook& k) {
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
static int shuttle(uint32_t t, uint32_t period, int span) {
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

bool anticOnSign(RoamAntic a) { return a >= RoamAntic::Bat && a <= RoamAntic::Stamp; }

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
constexpr uint16_t kCoffeeBrown = 0x6A20;

// The crossed-out laptop (computer away), in the sign's top-right corner, beside the cat's paws.
constexpr int kAwayIconW = 22, kAwayIconH = 15;

void roamAwayIcon(int cx, int cy, int& x, int& y, int& w, int& h) {
  const int top = cy - roamH() / 2;
  const int sy = top + Y(kRoamMargin) + 2 * Sz(kRoamCatHalf) - Sz(10);  // the sign's top edge
  w = Sz(kAwayIconW), h = Sz(kAwayIconH);
  x = cx + roamW() / 2 - X(kRoamMargin) - Sz(8) - w;
  y = sy + Sz(6);
}

void roam(Lang lang, const Snapshot& s, const Clock& clk, uint32_t ms, DeskMood mood, const char* note,
          uint32_t lookMs, bool computerAway) {
  int cx, cy;
  roamPosition(ms, cx, cy);
  MascotLook k = deskLook(mood, true, lookMs == UINT32_MAX ? ms : lookMs);
  // Antics while it is calm and nothing else is being said (a friend's hi, a nap together).
  uint32_t at = 0;
  const bool playful = (mood == DeskMood::Calm || mood == DeskMood::Watchful) && !(note && note[0]);
  const RoamAntic antic = playful ? roamAntic(ms, &at) : RoamAntic::None;
  int signDx = 0;          // the sign wiggles when batted
  bool blot = false;       // coffee spilled on the sign
  int drops = 0;           // coffee drops falling (0..3)
  int curX = -1, curY = 0; // the mouse cursor on the sign (-1 = none)
  switch (antic) {
    case RoamAntic::Bat: {  // bats at the sign with one paw, then the other
      const bool left = (at / 400) % 2;
      k = MascotLook{0, 0, (int8_t)(left ? -3 : 3), 3, Eyes::Open, left ? Paws::ReachLeft : Paws::ReachRight, 0};
      if ((at / 200) % 2) signDx = left ? -Sz(2) : Sz(2);
      if (at >= kAnticMs - 1500) k = MascotLook{0, 0, 0, 0, Eyes::Happy, Paws::Down, 0};
      break;
    }
    case RoamAntic::Spill:  // coffee in paw, it tips over the sign: scared, then hides its eyes
      if (at < 2000) {
        k = MascotLook{0, 0, 0, 0, Eyes::Happy, Paws::Down, kCoffee};
      } else if (at < 3000) {
        k = MascotLook{2, -2, 3, 3, Eyes::Open, Paws::Down, kCoffee};
        drops = 1 + (int)((at - 2000) / 350);
      } else if (at < 6000) {
        k = MascotLook{(int8_t)((at / 120) % 2 ? -1 : 1), 0, 0, 3, Eyes::Wide, Paws::Down, kSweat | kMouthO};
        blot = true;
      } else {
        k = MascotLook{0, 0, 0, 0, Eyes::Open, Paws::Cover, kSweat};
        blot = true;
      }
      break;
    case RoamAntic::Cursor: {  // a mouse cursor runs over the sign; eyes on it, then a pounce
      const uint32_t pounce = kAnticMs - 1500;
      if (at < pounce) {
        curX = shuttle(at, 2600, 100);  // 0..100 across the sign, placed below
        curY = shuttle(at + 700, 1900, 100);
        k = MascotLook{0, 0, (int8_t)(curX < 35 ? -3 : curX > 65 ? 3 : 0), 3, Eyes::Wide, Paws::Down, 0};
      } else {
        k = MascotLook{0, (int8_t)(at < pounce + 500 ? -4 : 0), 0, 3, Eyes::Happy,
                       at < pounce + 500 ? Paws::ReachLeft : Paws::Down, 0};
      }
      break;
    }
    case RoamAntic::Nap:  // dozes off on the sign
      if (at < 1200) k = MascotLook{0, 0, 0, 0, Eyes::Sleepy, Paws::Down, 0};
      else if (at < kAnticMs - 1000) k = MascotLook{0, 3, 0, 0, Eyes::Closed, Paws::Down, (uint8_t)((at / 900) % 2 ? kZ1 : kZ1 | kZ2)};
      else k = MascotLook{0, 0, 0, -3, Eyes::Open, Paws::Down, 0};
      break;
    case RoamAntic::None: break;
    default: break;  // the other antics are drawn later; until then it looks calm
  }
  const int bw = roamW(), bh = roamH();
  const int left = cx - bw / 2, top = cy - bh / 2;
  const uint32_t now = clk.epoch ? clk.epoch : s.now;

  // The card's lines (empty when unknown).
  char lim[48] = "", reset[48] = "", lastName[48] = "", lastWhen[64] = "";
  uint16_t resetFg = color::DIM;
  const bool usage = s.hasUsage && (s.h5.present || s.d7.present);
  if (usage) {
    char a[16] = "--", b[16] = "--";
    if (s.h5.present) snprintf(a, sizeof(a), "%u%%", deskPct(s.h5, now));
    if (s.d7.present) snprintf(b, sizeof(b), "%u%%", deskPct(s.d7, now));
    snprintf(lim, sizeof(lim), "%s %s%s%s %s", t(lang, S::Short5h), a, kDot, t(lang, S::Short7d), b);
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
    miblo::formatInState(since(s.sessions[lf], clk), ago, sizeof(ago));
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

  uint32_t h = lookHash(hashInt(kHashSeed + 43, (uint32_t)(cx * 1000 + cy)), k);
  h = hashStr(hashStr(hashStr(hashStr(hashStr(h, clk.hhmm), lim), reset), lastName), lastWhen);
  h = hashInt(h, mascotAccessory());
  h = hashInt(hashInt(h, (uint32_t)(signDx + 16) | (uint32_t)blot << 8 | (uint32_t)drops << 9),
              (uint32_t)((curX + 1) * 1000 + curY));
  h = hashInt(h, computerAway);
  if (!dirty(R_BODY, h)) return;
  static int lastX = -1000, lastY = -1000;
  if (abs(cx - lastX) > X(kRoamMargin) || abs(cy - lastY) > Y(kRoamMargin)) C().clear(color::BG);
  lastX = cx, lastY = cy;
  const int catY = top + Y(kRoamMargin) + Sz(kRoamCatHalf);
  const int y0 = catY + Sz(kRoamCatHalf);
  auto draw = [&] {
    C().fillRect(left, top, bw, bh, color::BG);
    // The sign, held up to the cat's paws (drawn first: the paws rest on its top edge).
    const int sx = left + X(kRoamMargin) + signDx, sw = bw - 2 * X(kRoamMargin);
    const int sy = y0 - Sz(10), sh = top + bh - Y(kRoamMargin) - sy;
    C().fillRoundRect(sx, sy, sw, sh, Sz(6), kSignEdge);
    C().fillRoundRect(sx + 1, sy + 1, sw - 2, sh - 2, Sz(5), kSignFill);
    const int w = sw - 2 * X(kRoamMargin);
    const int tx = cx + signDx;
    C().text(tx, y0 + Y(16), clk.hhmm, Font::BodyBold, color::TEXT, Align::Center, w);
    if (lim[0]) C().text(tx, y0 + Y(33), lim, Font::Small, color::MUTED, Align::Center, w);
    if (reset[0]) C().text(tx, y0 + Y(49), reset, Font::Small, resetFg, Align::Center, w);
    if (lastName[0]) C().text(tx, y0 + Y(66), lastName, Font::SmallBold, nameFg, Align::Center, w);
    if (lastWhen[0]) C().text(tx, y0 + Y(81), lastWhen, Font::Small, color::DIM, Align::Center, w);
    if (blot) {  // a coffee stain across the top of the sign
      C().fillCircle(cx - Sz(14), sy + Sz(9), Sz(6), kCoffeeBrown);
      C().fillCircle(cx - Sz(3), sy + Sz(12), Sz(8), kCoffeeBrown);
      C().fillCircle(cx + Sz(11), sy + Sz(8), Sz(5), kCoffeeBrown);
      C().fillCircle(cx + Sz(22), sy + Sz(14), Sz(2), kCoffeeBrown);
    }
    for (int i = 0; i < drops; i++) C().fillCircle(cx + Sz(22), sy - Sz(4) + i * Sz(5), Sz(2), kCoffeeBrown);
    if (computerAway) {  // a small laptop, crossed out: discreet, and the same in every language
      int ix, iy, iw, ih;
      roamAwayIcon(cx, cy, ix, iy, iw, ih);
      ix += signDx;
      const int t1 = Sz(1) > 0 ? Sz(1) : 1;
      const int lw = iw - Sz(4), lh = ih - Sz(4), lx = ix + Sz(2);  // the lid, above the base
      C().fillRect(lx, iy, lw, t1, color::DIM);
      C().fillRect(lx, iy + lh - t1, lw, t1, color::DIM);
      C().fillRect(lx, iy, t1, lh, color::DIM);
      C().fillRect(lx + lw - t1, iy, t1, lh, color::DIM);
      C().fillRect(ix, iy + lh + t1, iw, Sz(2), color::DIM);  // the base
      // The slash, set off from the outline by a gap in the sign's colour.
      const int inset = Sz(3);
      C().wideLine(ix + inset, iy - Sz(1), ix + iw - inset, iy + ih, Sz(4), kSignFill, kSignFill);
      C().wideLine(ix + inset, iy - Sz(1), ix + iw - inset, iy + ih, Sz(2), color::AMBER, kSignFill);
    }
    if (curX >= 0) {  // the mouse cursor: a white arrow
      const int ax = sx + Sz(10) + (sw - Sz(24)) * curX / 100, ay = sy + Sz(12) + (sh - Sz(28)) * curY / 100;
      C().fillTriangle(ax, ay, ax, ay + Sz(11), ax + Sz(8), ay + Sz(8), color::WHITE);
      C().fillRect(ax + Sz(3), ay + Sz(8), Sz(2), Sz(5), color::WHITE);
    }
    deskMascot(cx, catY, k, kRoamCatHalf, false, false);  // over the sign, no background square
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

// ---- visits between Miblos (miblo_friends.h) ----
// Both cats walk on one horizontal band, recomposed in strips on every change (no trail, no flash).
constexpr int kVisitCatHalf = 40;
constexpr int kVisitCatY = 104;

// Position along a walk: from `a` to `b` over [t0, t0 + kVisitWalkMs).
static int walk(uint32_t ms, uint32_t t0, int a, int b) {
  if (ms <= t0) return a;
  if (ms >= t0 + miblo::kVisitWalkMs) return b;
  return a + (int)((int64_t)(b - a) * (int32_t)(ms - t0) / (int32_t)miblo::kVisitWalkMs);
}

// Props of the programmer activities (miblo::Gift), drawn over the band after the cats.
enum class PropKind : uint8_t { None, Duck, Laptop, Lgtm, Bug, Rocket, Burst };
struct Prop {
  PropKind kind;
  int x, y;   // screen coordinates (see drawProp for the anchor of each)
  uint8_t f;  // animation frame: code lines, rocket flame
};
constexpr uint16_t kDuckYellow = 0xFFE0;
constexpr uint16_t kOrange = 0xFC00;
constexpr uint16_t kGrey = 0x8410;
constexpr uint16_t kDarkGreen = 0x0400;

static void drawProp(const Prop& p) {
  const int x = p.x, y = p.y, u = Sz(1) < 1 ? 1 : Sz(1);
  switch (p.kind) {
    case PropKind::Duck:  // rubber duck (centre of the body)
      C().fillCircle(x, y, Sz(6), kDuckYellow);
      C().fillCircle(x + Sz(5), y - Sz(6), Sz(4), kDuckYellow);
      C().fillTriangle(x + Sz(8), y - Sz(7), x + Sz(13), y - Sz(5), x + Sz(8), y - Sz(4), kOrange);
      C().fillRect(x + Sz(6), y - Sz(8), u + u, u + u, color::PUPIL);
      break;
    case PropKind::Laptop:  // (centre of the keyboard) with code scrolling on the screen
      C().fillRect(x - Sz(15), y, Sz(30), Sz(3), kGrey);
      C().fillRect(x - Sz(12), y - Sz(17), Sz(24), Sz(17), kGrey);
      C().fillRect(x - Sz(11), y - Sz(16), Sz(22), Sz(15), color::BLACK);
      for (int i = 0; i < 4; i++) {
        const int len = 4 + (p.f * 5 + i * 7) % 13;
        C().fillRect(x - Sz(9) + (i % 2) * Sz(3), y - Sz(14) + i * Sz(3), Sz(len), u, i % 3 ? color::GREEN : color::BLUE);
      }
      break;
    case PropKind::Lgtm:  // code review sign (top-left corner)
      C().fillRoundRect(x, y, Sz(44), Sz(16), Sz(3), color::WHITE);
      C().text(x + Sz(22), y + Sz(12), "LGTM", Font::SmallBold, kDarkGreen, Align::Center, Sz(42));
      break;
    case PropKind::Bug:  // a little bug (centre of the body), legs going
      for (int s = -1; s <= 1; s += 2) {
        for (int l = -1; l <= 1; l++) C().fillRect(x + l * Sz(2), y + s * Sz(3) + (p.f % 2 ? s : 0), u, Sz(2), color::PUPIL);
      }
      C().fillCircle(x, y, Sz(3), color::RED);
      C().fillCircle(x + Sz(3), y, Sz(2), color::PUPIL);
      break;
    case PropKind::Rocket:  // (tip of the nose), flame when f > 0
      C().fillTriangle(x, y, x - Sz(4), y + Sz(6), x + Sz(4), y + Sz(6), color::RED);
      C().fillRect(x - Sz(4), y + Sz(6), Sz(8), Sz(12), color::WHITE);
      C().fillCircle(x, y + Sz(10), Sz(2), color::BLUE);
      C().fillTriangle(x - Sz(4), y + Sz(12), x - Sz(8), y + Sz(19), x - Sz(4), y + Sz(18), color::RED);
      C().fillTriangle(x + Sz(4), y + Sz(12), x + Sz(8), y + Sz(19), x + Sz(4), y + Sz(18), color::RED);
      if (p.f) C().fillTriangle(x - Sz(3), y + Sz(18), x + Sz(3), y + Sz(18), x, y + Sz(21 + (p.f % 2) * 3), color::AMBER);
      break;
    case PropKind::Burst:  // the bug is fixed: a spark
      C().fillRect(x - Sz(6), y - u, Sz(12), u + u, color::AMBER);
      C().fillRect(x - u, y - Sz(6), u + u, Sz(12), color::AMBER);
      C().fillCircle(x, y, Sz(2), color::WHITE);
      break;
    case PropKind::None: break;
  }
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
  const int catHalf = cats <= 2 ? kVisitCatHalf : cats == 3 ? 33 : 27;
  const int half = Sz(catHalf);
  const int cy = Y(kVisitCatY);
  const bool coffee = v.gift == miblo::Gift::Coffee;
  Prop prop{PropKind::None, 0, 0, 0};

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
      const uint32_t t = ms - miblo::kVisitArriveMs;
      const uint32_t stay = miblo::kVisitStayMs;
      const bool firstHalf = t < stay / 2;
      them = MascotLook{0, (int8_t)((t / 400) % 3 == 0 ? -4 : 0), (int8_t)(3 * toHost), 0, Eyes::Happy, Paws::Down, 0};
      me = MascotLook{0, (int8_t)((t / 400) % 3 == 1 ? -4 : 0), (int8_t)(-3 * toHost), 0, Eyes::Happy, Paws::Down, 0};
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
            prop = {PropKind::Duck, guestX + toHost * Sz(28), cy + Sz(22), 0};
          } else {
            them = watchL;
            me = watchR;
            me.eyes = (t / 1200) % 3 == 2 ? Eyes::Happy : Eyes::Open;  // the "aha!" moments
            prop = {PropKind::Duck, mid, cy + Sz(28) - ((t / 500) % 2 ? Sz(2) : 0), 0};
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
          prop = {PropKind::Laptop, mid, cy + Sz(34), (uint8_t)(t / 300)};
          break;
        case miblo::Gift::Review:  // code review: the guest holds up "LGTM", the host reads it
          if (firstHalf) {
            them.paws = guestReach;
            me = watchR;
            prop = {PropKind::Lgtm, toHost > 0 ? guestX + Sz(4) : guestX - Sz(48), cy + Sz(16), 0};
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
            prop = {PropKind::Bug, bugX, cy + Sz(33), (uint8_t)(t / 120)};
          } else if (t < caught + 900) {
            prop = {PropKind::Burst, mid, cy + Sz(30), 0};
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
            prop = {PropKind::Rocket, mid, cy + Sz(12), 0};
          } else if (t < gone) {
            them = MascotLook{0, 0, (int8_t)(2 * toHost), -3, Eyes::Wide, Paws::Down, 0};
            me = MascotLook{0, 0, (int8_t)(-2 * toHost), -3, Eyes::Wide, Paws::Down, 0};
            const int y = cy + Sz(12) - (int)((int64_t)Sz(48) * (int32_t)(t - launch) / (int32_t)(gone - launch));
            prop = {PropKind::Rocket, mid, y, (uint8_t)(1 + (t / 120) % 2)};
          } else {
            me.extras |= kHeart;
          }
          break;
        }
        case miblo::Gift::None:
        case miblo::Gift::Count:
          if ((t / 1500) % 4 == 3) me.extras |= kHeart;
          break;
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
  if (!vertical) {
    field(R_CLOCK, kHashSeed + 59, X(120), Y(18), clk.hhmm, Font::Body, color::DIM, color::BG, Align::Center, X(80));
  }

  uint32_t h = hashInt(hashInt(kHashSeed + 61, (uint32_t)(mine ? myX * 1000 + myY + 1 : 0)),
                       (uint32_t)(guest ? guestX * 1000 + guestY + 1 : 0));
  h = hashInt(hashInt(h, lookHash(kHashSeed, me)), lookHash(kHashSeed + 1, them));
  h = hashInt(hashInt(h, (uint32_t)prop.kind | (uint32_t)prop.f << 8), (uint32_t)(prop.x * 1000 + prop.y));
  for (uint8_t e = 0; e < extras; e++) {
    h = hashInt(lookHash(hashInt(h, (uint32_t)(exX[e] * 1000 + exY[e])), exLook[e]), v.extraMascot[e]);
  }
  if (dirty(R_BODY, h)) {
    const int top = vertical ? 0 : cy - half, bh = vertical ? Y(240) : 2 * half;
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
      drawProp(prop);
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
    static const S kLine[] = {S::FriendVisiting, S::FriendCoffee, S::FriendDuck, S::FriendPair,
                              S::FriendReview, S::FriendBug, S::FriendDeploy};
    static_assert(sizeof(kLine) / sizeof(kLine[0]) == (size_t)miblo::Gift::Count, "one line per activity");
    const uint8_t g = (uint8_t)v.gift < (uint8_t)miblo::Gift::Count ? (uint8_t)v.gift : 0;
    char who[40];
    if (v.extra) snprintf(who, sizeof(who), "%s +%u", v.name, (unsigned)v.extra);  // a group
    else snprintf(who, sizeof(who), "%s", v.name);
    snprintf(buf, sizeof(buf), t(lang, kLine[g]), who);
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

void hello(const char* line1, const char* line2, bool party, uint32_t ms) {
  deskCat(R_BODY, X(120), Y(90), 56, deskLook(DeskMood::Celebrate, true, ms));
  if (party) {
    // Confetti along the top and the bottom, reshuffled a few times a second.
    static const uint16_t kColors[] = {color::AMBER, color::GREEN, color::BLUE, color::CORAL, color::VIOLET, color::RED};
    const uint32_t frame = ms / 250;
    for (uint8_t band = 0; band < 2; band++) {
      const int y0 = band ? Y(216) : Y(4), bandH = Y(20);
      if (!region(band ? R_FOOT : R_HEADER, hashInt(kHashSeed + 71 + band, frame), 0, y0, X(240), bandH)) continue;
      uint32_t r = frame * 2654435761u + band * 97u + 1;
      for (int i = 0; i < 14; i++) {
        r = r * 1103515245u + 12345u;
        const int x = X(6) + (int)((r >> 8) % (uint32_t)X(224));
        const int y = y0 + (int)((r >> 20) % (uint32_t)(bandH - Sz(5)));
        C().fillRect(x, y, Sz(5), Sz(3) + (int)(r % 3), kColors[(r >> 4) % 6]);
      }
    }
  }
  const uint32_t h = hashStr(hashStr(hashInt(kHashSeed + 73, party), line1), line2);
  if (region(R_LIMITS, h, 0, Y(152), X(240), Y(62))) {
    const bool two = line1 && line1[0];
    if (two) C().text(X(120), Y(172), line1, Font::Body, color::MUTED, Align::Center, X(228));
    // The big line in the title font when it fits, else a size down (long phrases, some languages).
    const Font big = C().textWidth(line2, Font::Title) <= X(228) ? Font::Title : Font::BodyBold;
    C().text(X(120), two ? Y(202) : Y(190), line2, big, party ? color::AMBER : color::TEXT, Align::Center, X(228));
  }
}

void desk(Lang lang, const Snapshot& s, const Clock& clk, uint32_t nowMs, uint32_t exhaustAt) {
  const uint32_t now = clk.epoch ? clk.epoch : s.now;
  const uint8_t p5 = deskPct(s.h5, now);
  const uint8_t p7 = deskPct(s.d7, now);
  const bool usage = s.hasUsage && (s.h5.present || s.d7.present);
  bool focusLeft;
  const DeskMood mood = deskMoodFor(s, now, &focusLeft);
  field(R_CLOCK, kHashSeed + 19, X(120), Y(18), clk.hhmm, Font::Body, color::DIM, color::BG, Align::Center, X(80));
  deskCat(R_BODY, X(120), Y(72), 48, deskLook(mood, focusLeft, nowMs));
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
