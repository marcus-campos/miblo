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
enum : uint8_t { R_HEADER = 0, R_LIMITS = 1, R_WEEK = 2, R_DIVIDER = 3, R_ROW0 = 4, R_BODY = 9, R_FOOT = 10 };

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

// ---------------- alerts (§4.2) ----------------

void flash(Lang lang, AlertKind kind, const char* name, uint32_t elapsedMs) {
  (void)lang;
  const bool amber = kind != AlertKind::Done;
  const bool on = ((elapsedMs / 250) % 2) == 0;  // blinks every 250 ms
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

  uint32_t h = hashStr(hashInt(hashInt(kHashSeed, (uint32_t)lang), (uint32_t)kind), r.id);
  h = hashStr(hashStr(hashStr(h, r.name), discreet ? "" : r.det), r.tool);
  h = hashInt(hashInt(h, (uint32_t)r.ctx), (uint32_t)r.tok);
  if (region(R_HEADER, h, 0, 0, X(240), Y(150))) {
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

  if (amber) {
    const uint32_t waited = since(r, clk);
    if (region(R_BODY, hashInt(hashInt(kHashSeed, waited), (uint32_t)lang), 0, Y(150), X(240), Y(24))) {
      miblo::formatElapsed(waited, tmp, sizeof(tmp));
      snprintf(buf, sizeof(buf), t(lang, S::WaitingFor), tmp);
      C().text(X(12), Y(166), buf, Font::Small, color::DIM, Align::Left, X(216));
    }
  }

  const miblo::StateCounts c = miblo::countStates(s);
  if (region(R_FOOT, hashInt(hashInt(hashInt(kHashSeed, c.running), c.idle), (uint32_t)lang), 0, Y(210), X(240),
             Y(30))) {
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

static void clockRight(const Clock& clk, int y, uint16_t c) {
  C().text(X(228), y, clk.hhmm, Font::Small, c, Align::Right, X(60));
}

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
static void limitsBlock(Lang lang, const Snapshot& s, const Clock& clk) {
  uint32_t h = hashInt(hashInt(kHashSeed, (uint32_t)lang), clk.epoch / 60);
  h = hashInt(hashInt(hashInt(h, s.hasUsage), s.h5.present ? s.h5.pct : 255), s.h5.reset);
  h = hashInt(hashInt(hashInt(h, s.d7.present ? s.d7.pct : 255), s.d7.reset), (uint32_t)(s.todayUsd * 100));
  if (!region(R_LIMITS, h, 0, Y(26), X(240), Y(122))) return;
  if (!s.hasUsage) {
    noLimits(lang, s, Y(84));
    return;
  }
  char buf[96];
  C().text(X(12), Y(52), t(lang, S::Session5h), Font::Body, color::MUTED, Align::Left, X(140));
  if (s.h5.present) {
    snprintf(buf, sizeof(buf), "%u%%", s.h5.pct);
    C().text(X(228), Y(58), buf, Font::NumL, color::TEXT, Align::Right, X(100));
    bar(X(12), Y(64), X(216), Y(10), s.h5.pct, levelColor(s.h5.pct, color::CORAL));
    if (s.h5.reset) {  // 0 = unknown (the plugin sent reset:null): no line
      resetLine(lang, s.h5, clk, s.now, true, buf, sizeof(buf));
      C().text(X(12), Y(90), buf, Font::Small, color::DIM, Align::Left, X(216));
    }
  } else {
    C().text(X(228), Y(58), "--", Font::NumM, color::DIM, Align::Right, X(100));
  }
  C().text(X(12), Y(116), t(lang, S::Week), Font::Body, color::MUTED, Align::Left, X(140));
  if (s.d7.present) {
    snprintf(buf, sizeof(buf), "%u%%", s.d7.pct);
    C().text(X(228), Y(118), buf, Font::NumM, color::TEXT, Align::Right, X(100));
    bar(X(12), Y(124), X(216), Y(6), s.d7.pct, levelColor(s.d7.pct, color::VIOLET));
    if (s.d7.reset) {
      resetLine(lang, s.d7, clk, s.now, false, buf, sizeof(buf));
      C().text(X(12), Y(144), buf, Font::Small, color::DIM, Align::Left, X(216));
    }
  } else {
    C().text(X(228), Y(118), "--", Font::NumM, color::DIM, Align::Right, X(100));
  }
}

// One row of the compact list (Overview), with baseline y.
static void listRow(uint8_t slot, Lang lang, const SessionRow* r, const Clock& clk, bool discreet, int y) {
  char line[160];
  line[0] = 0;
  if (r) {
    if (isPending(r->st)) {
      char tmp[24];
      miblo::formatElapsed(since(*r, clk), tmp, sizeof(tmp));
      snprintf(line, sizeof(line), t(lang, S::WaitingFor), tmp);
    } else {
      miblo::sessionLine(lang, *r, discreet, line, sizeof(line));
    }
  }
  const uint32_t h = r ? hashStr(hashStr(hashInt(kHashSeed, (uint32_t)r->st), r->name), line) : 7;
  if (!region(R_ROW0 + slot, h, 0, y - Y(13), X(240), Y(18))) return;
  if (!r) return;
  const bool pending = isPending(r->st);
  C().fillCircle(X(16), y - Y(4), Sz(3), stateColor(r->st));
  C().text(X(26), y, r->name, Font::SmallBold, pending ? color::AMBER : color::TEXT, Align::Left, X(92));
  C().text(X(124), y, line, Font::Small, pending ? color::AMBER : color::MUTED, Align::Left, X(104));
}

// ---------------- Adaptive overview (§4.1) ----------------

void overview(Lang lang, const Snapshot& s, miblo::Pager& pager, uint32_t nowMs, const Clock& clk, bool discreet) {
  const OverviewKind kind = miblo::classifyOverview(s);
  const miblo::StateCounts c = miblo::countStates(s);
  char buf[128];
  char tmp[48];

  const int heroIdx = miblo::selectHero(s, false);
  const char* heroName = heroIdx >= 0 ? s.sessions[heroIdx].name : "";
  uint32_t h = hashInt(hashInt(hashInt(kHashSeed, (uint32_t)kind), c.pending), c.running);
  h = hashStr(hashStr(hashInt(h, (uint32_t)lang), clk.hhmm), heroName);
  if (region(R_HEADER, h, 0, 0, X(240), Y(26))) {
    if (kind == OverviewKind::Attention) {  // fixed amber band: "1 WAITING · api-server"
      C().fillRect(0, 0, X(240), Y(22), color::AMBER);
      snprintf(tmp, sizeof(tmp), t(lang, S::NWaiting), (unsigned)c.pending);
      snprintf(buf, sizeof(buf), "%s%s%s", tmp, kDot, heroName);
      C().text(X(10), Y(16), buf, Font::SmallBold, color::BLACK, Align::Left, X(180));
      clockRight(clk, Y(16), color::BLACK);
    } else if (kind == OverviewKind::Working) {
      C().fillCircle(X(16), Y(13), Sz(4), color::GREEN);
      snprintf(buf, sizeof(buf), t(lang, S::NRunning), (unsigned)c.running);
      C().text(X(26), Y(18), buf, Font::SmallBold, color::GREEN, Align::Left, X(150));
      clockRight(clk, Y(18), color::DIM);
    } else {
      check(X(17), Y(12), Sz(12), color::BLUE);
      C().text(X(28), Y(18), t(lang, S::AllDone), Font::SmallBold, color::BLUE, Align::Left, X(150));
      clockRight(clk, Y(18), color::DIM);
    }
  }

  limitsBlock(lang, s, clk);
  if (region(R_DIVIDER, 1, 0, Y(150), X(240), 2)) C().fillRect(X(12), Y(150), X(216), 1, color::DIVIDER);

  const int ys[4] = {Y(168), Y(186), Y(204), Y(222)};
  if (kind == OverviewKind::Idle) {
    // footer: most recently finished session + today's cost
    const int last = miblo::lastFinished(s);
    buf[0] = 0;
    if (last >= 0) {
      miblo::formatAgo(since(s.sessions[last], clk), tmp, sizeof(tmp));
      snprintf(buf, sizeof(buf), t(lang, S::FinishedAgo), s.sessions[last].name, tmp);
    }
    if (region(R_ROW0, hashStr(kHashSeed, buf), 0, ys[0] - Y(13), X(240), Y(18))) {
      C().text(X(12), ys[0], buf, Font::Small, color::MUTED, Align::Left, X(216));
    }
    buf[0] = 0;
    if (s.todayUsd > 0.0f) {
      miblo::formatUsd(s.todayUsd, tmp, sizeof(tmp));
      snprintf(buf, sizeof(buf), t(lang, S::CostToday), tmp);
    }
    if (region(R_ROW0 + 1, hashStr(kHashSeed + 1, buf), 0, ys[1] - Y(13), X(240), Y(18))) {
      C().text(X(12), ys[1], buf, Font::Small, color::DIM, Align::Left, X(216));
    }
    (void)region(R_ROW0 + 2, 0, 0, ys[2] - Y(13), X(240), Y(18));  // clears leftover list rows
    (void)region(R_ROW0 + 3, 0, 0, ys[3] - Y(13), X(240), Y(18));
    return;
  }

  const uint8_t page = pager.update(s.count, nowMs);
  for (uint8_t i = 0; i < 4; i++) {
    const int idx = page * pager.perPage() + i;
    listRow(i, lang, idx < s.count ? &s.sessions[idx] : nullptr, clk, discreet, ys[i]);
  }
}

// ---------------- Limits mode — L1 (§4.3) ----------------

void limits(Lang lang, const Snapshot& s, const Clock& clk) {
  char buf[96];
  if (region(R_HEADER, hashStr(hashInt(kHashSeed, (uint32_t)lang), clk.hhmm), 0, 0, X(240), Y(24))) {
    C().text(X(12), Y(18), t(lang, S::LimitsTitle), Font::SmallBold, color::DIM, Align::Left, X(150));
    clockRight(clk, Y(18), color::DIM);
  }
  uint32_t h = hashInt(hashInt(hashInt(kHashSeed, s.hasUsage), s.h5.present ? s.h5.pct : 255), clk.epoch / 60);
  h = hashInt(hashInt(h, (uint32_t)lang), (uint32_t)(s.todayUsd * 100));
  if (region(R_LIMITS, h, 0, Y(24), X(240), Y(150))) {
    if (!s.hasUsage || !s.h5.present) {
      noLimits(lang, s, Y(104));
    } else {
      const uint8_t pct = s.h5.pct;
      const int cx = X(120);
      const int cy = Y(104);
      const int r = Sz(78);
      const int ir = Sz(64);
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
      if (s.h5.reset) {  // 0 = unknown: no countdown
        char left[16];
        miblo::formatCountdown(s.h5.reset > clk.epoch ? s.h5.reset - clk.epoch : 0, left, sizeof(left));
        snprintf(buf, sizeof(buf), t(lang, S::InTime), left);
        C().text(cx, Y(152), buf, Font::Small, color::DIM, Align::Center, 2 * ir);
      }
    }
  }
  h = hashInt(hashInt(hashInt(kHashSeed, s.d7.present ? s.d7.pct : 255), s.d7.reset), (uint32_t)lang);
  if (region(R_WEEK, hashInt(h, clk.epoch / 3600), 0, Y(176), X(240), Y(40))) {
    if (s.hasUsage && s.d7.present) {
      char when[32];
      C().text(X(12), Y(192), t(lang, S::Week), Font::Small, color::MUTED, Align::Left, X(100));
      if (s.d7.reset) {
        formatWhen(lang, s.d7.reset, clk.epoch ? clk.epoch : s.now, when, sizeof(when));
        snprintf(buf, sizeof(buf), "%u%%%s%s", s.d7.pct, kDot, when);
      } else {
        snprintf(buf, sizeof(buf), "%u%%", s.d7.pct);  // reset unknown: percentage only
      }
      C().text(X(228), Y(192), buf, Font::Small, color::MUTED, Align::Right, X(120));
      bar(X(12), Y(200), X(216), Y(6), s.d7.pct, levelColor(s.d7.pct, color::VIOLET));
    }
  }
}

// ---------------- Sessions mode — S1 (§4.4) ----------------

void sessions(Lang lang, const Snapshot& s, miblo::Pager& pager, uint32_t nowMs, const Clock& clk, bool discreet) {
  char buf[160];
  char tmp[48];
  const uint8_t page = pager.update(s.count, nowMs);
  const uint8_t pages = pager.pageCount(s.count);
  const unsigned total = (unsigned)s.count + s.more;
  if (region(R_HEADER, hashInt(hashInt(hashInt(kHashSeed, total), page * 16u + pages), (uint32_t)lang), 0, 0, X(240),
             Y(24))) {
    snprintf(buf, sizeof(buf), t(lang, S::SessionsTitle), total);
    C().text(X(10), Y(16), buf, Font::SmallBold, color::DIM, Align::Left, X(170));
    snprintf(tmp, sizeof(tmp), "%u/%u", (unsigned)page + 1, (unsigned)pages);
    C().text(X(230), Y(16), tmp, Font::Small, color::DIM, Align::Right, X(50));
  }
  if (s.count == 0) {
    if (region(R_ROW0, hashInt(kHashSeed + 9, (uint32_t)lang), 0, Y(24), X(240), Y(216))) {
      C().text(X(120), Y(130), t(lang, S::NoSessions), Font::Body, color::MUTED, Align::Center, X(232));
    }
    for (uint8_t i = 1; i < 4; i++) (void)region(R_ROW0 + i, 0xFFFFFFFFu, 0, 0, 0, 0);  // force a redraw later
    return;
  }
  for (uint8_t i = 0; i < 4; i++) {
    const int idx = page * pager.perPage() + i;
    const int y0 = Y(26 + i * 53);
    const SessionRow* r = idx < s.count ? &s.sessions[idx] : nullptr;
    char timeStr[16] = "";
    char state[160] = "";
    char meta[96] = "";
    if (r) {
      if (isPending(r->st) || r->st == SessionState::Running) miblo::formatElapsed(since(*r, clk), timeStr, sizeof(timeStr));
      else miblo::formatAgo(since(*r, clk), timeStr, sizeof(timeStr));
      miblo::sessionLine(lang, *r, discreet, state, sizeof(state));
      metaLine(*r, meta, sizeof(meta));
    }
    const uint32_t h =
        r ? hashStr(hashStr(hashStr(hashStr(hashInt(kHashSeed, (uint32_t)r->st), r->name), timeStr), state), meta) : 3;
    if (!region(R_ROW0 + i, h, 0, y0 - 1, X(240), Y(53))) continue;
    if (!r) continue;
    const bool pending = isPending(r->st);
    const uint16_t sc = stateColor(r->st);
    C().fillRect(X(8), y0, X(224), Y(50), pending ? color::CARD_AMBER : color::CARD);
    C().fillRect(X(8), y0, Sz(3), Y(50), sc);
    C().text(X(16), y0 + Y(16), r->name, Font::BodyBold, color::TEXT, Align::Left, X(150));
    C().text(X(226), y0 + Y(16), timeStr, Font::Small, pending ? color::AMBER : color::DIM, Align::Right, X(60));
    C().text(X(16), y0 + Y(31), state, Font::Small, sc, Align::Left, X(208));
    C().text(X(16), y0 + Y(45), meta, Font::Small, color::DIM, Align::Left, X(208));
  }
}

}  // namespace screens
