// Alerts: the flash when one comes in, the hero that follows, and (track B) the long task
// fanfare and the meeting badge.
#include <stdio.h>
#include <string.h>

#include "miblo_format.h"
#include "ui_internal.h"
#include "ui_screens.h"

namespace screens {

using miblo::AlertKind;
using miblo::hashInt;
using miblo::hashStr;
using miblo::kHashSeed;
using miblo::S;
using miblo::SessionRow;
using miblo::Snapshot;

void flash(Lang lang, AlertKind kind, const char* name, uint32_t elapsedMs, uint8_t level, bool anonymous) {
  (void)lang;
  (void)level;      // track B: red blinks from the 5th reminder
  (void)anonymous;  // track B: meeting mode, no session name
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
          const miblo::RunTracker& runs, bool anonymous) {
  (void)anonymous;  // track B: meeting mode, no name, tool or command
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
    miblo::formatInState(sessionSince(r, clk), tmp, sizeof(tmp));
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

// Stub (daily-life foundation): track B draws it.
void fanfare(Lang lang, const char* name, uint32_t durSec, uint32_t ms) {
  (void)lang;
  (void)name;
  (void)durSec;
  (void)ms;
}

// Stub (daily-life foundation): track B draws it.
void meetingBadge(Lang lang) { (void)lang; }

}  // namespace screens
