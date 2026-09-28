#include <stdio.h>
#include <string.h>
#include <time.h>

#include "miblo_activity.h"
#include "miblo_format.h"
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
  R_CLOCK = 14, R_RESET5 = 15, R_RESET7 = 16, R_PAGE = 17
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
    compactLimits(R_HEADER, lang, s, 0, Y(26), Y(17), color::BG);
    g = {Y(26), Y(58), Y(24), Y(46)};
  }

  const uint8_t per = pager.perPage() < kRows ? pager.perPage() : kRows;
  const uint8_t page = pager.update(s.count, nowMs);
  const uint8_t pages = pager.pageCount(s.count);
  sessionRows(lang, s, page, per, clk, discreet, g);

  // footer: "2 RUNNING · 1/2 · +3" + clock (the clock is in the amber band when something waits)
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
  if (working) clockRight(hf, clk, fy, color::DIM, color::BG);
}

// ---------------- Limits mode ----------------

void limits(Lang lang, const Snapshot& s, const Clock& clk) {
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
    field(R_RESET5, h, cx, Y(152), buf, Font::Small, color::DIM, color::BG, Align::Center, 2 * ir);
  }
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
    C().text(X(10), Y(16), buf, Font::SmallBold, color::DIM, Align::Left, X(170));
  }
  head.end();
  snprintf(tmp, sizeof(tmp), "%u/%u", (unsigned)page + 1, (unsigned)pages);
  field(R_PAGE, hh, X(230), Y(16), tmp, Font::Small, color::DIM, color::BG, Align::Right, X(50));
  if (s.count == 0) {
    if (region(R_ROW0, hashInt(kHashSeed + 9, (uint32_t)lang), 0, Y(24), X(240), Y(216))) {
      C().text(X(120), Y(130), t(lang, S::NoSessions), Font::Body, color::MUTED, Align::Center, X(232));
    }
    for (uint8_t i = 1; i < kRows; i++) (void)dirty(R_ROW0 + i, 0xFFFFFFFFu);  // force a redraw later
    return;
  }
  sessionRows(lang, s, page, per, clk, discreet, {Y(26), Y(70), Y(28), Y(52)});
}

}  // namespace screens
