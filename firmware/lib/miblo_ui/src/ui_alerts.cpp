// Alerts: the flash when one comes in, the hero that follows, and the long task
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

// Splits `s` into at most two lines of `maxW` in font `f`, at the space that makes the longer
// line shortest (balanced: "A session / needs you", not "A session needs / you"). The first line
// goes to `a`, `*b` points at the rest in `s` ("" when it all fits on one line). Returns false
// when no split fits: then `a` holds it all, for text() to cut with "...".
static bool split2(const char* s, Font f, int maxW, char* a, size_t cap, const char** b) {
  *b = "";
  snprintf(a, cap, "%s", s);
  if (C().textWidth(s, f) <= maxW) return true;
  const size_t n = strlen(a);
  int best = -1, bestW = maxW + 1;
  for (size_t i = 1; i + 1 < n; i++) {
    if (a[i] != ' ') continue;
    a[i] = 0;
    const int w1 = C().textWidth(a, f), w2 = C().textWidth(s + i + 1, f);
    a[i] = ' ';
    const int w = w1 > w2 ? w1 : w2;
    if (w < bestW) bestW = w, best = (int)i;
  }
  if (best < 0) return false;
  a[best] = 0;
  *b = s + best + 1;
  return true;
}

// One or two centred lines (baselines y1 / y1 + gap, or y1 + gap / 2 alone).
static void centred2(const char* s, Font f, uint16_t fg, int y1, int gap, int maxW) {
  char a[96];
  const char* b;
  split2(s, f, maxW, a, sizeof(a), &b);
  if (!b[0]) {
    C().text(X(120), y1 + gap / 2, a, f, fg, Align::Center, maxW);
    return;
  }
  C().text(X(120), y1, a, f, fg, Align::Center, maxW);
  C().text(X(120), y1 + gap, b, f, fg, Align::Center, maxW);
}

void flash(Lang lang, AlertKind kind, const char* name, uint32_t elapsedMs, uint8_t level, bool anonymous) {
  const bool amber = kind != AlertKind::Done;
  const bool red = level >= 2;  // insistence, from the 5th reminder of the same wait
  const bool on = ((elapsedMs / kFlashPhaseMs) % 2) == 0;
  const uint16_t lit = red ? color::RED : amber ? color::AMBER : color::FLASH_BLUE;
  const uint16_t bg = on ? lit : color::BG;
  const uint16_t fg = on ? (amber && !red ? color::BLACK : color::WHITE) : (red || amber ? lit : color::BLUE);
  // Meeting mode: what happened, never which session. No name (the session is not in the
  // snapshot on screen, e.g. another computer's): the same wording.
  if (!name || !name[0]) anonymous = true;
  const char* label = anonymous ? t(lang, amber ? S::ASessionNeedsYou : S::Finished) : name;
  const uint32_t h = hashStr(hashInt(hashInt(hashInt(kHashSeed, amber), on), (uint32_t)red | anonymous << 1), label);
  if (!region(R_BODY, h, 0, 0, X(240), Y(240), bg)) return;
  if (amber) {  // "!" in a circle
    C().fillCircle(X(120), Y(90), Sz(30), fg);
    C().fillRect(X(116), Y(70), Sz(8), Y(26), bg);
    C().fillRect(X(116), Y(102), Sz(8), Sz(8), bg);
  } else {
    C().wideLine(X(96), Y(92), X(112), Y(108), Sz(8), fg, bg);
    C().wideLine(X(112), Y(108), X(146), Y(72), Sz(8), fg, bg);
  }
  if (anonymous) centred2(label, Font::Title, fg, Y(160), Y(28), X(224));
  else C().text(X(120), Y(160), label, Font::Hero, fg, Align::Center, X(224));
}

void hero(Lang lang, const Snapshot& s, int idx, AlertKind kind, bool discreet, const Clock& clk,
          const miblo::RunTracker& runs, bool anonymous, const char* name) {
  const bool amber = kind != AlertKind::Done;
  // The alerted session is not in this snapshot (another paired computer's, or an alerts-only
  // one): the header already on screen stays as it is, so it doesn't flip with every snapshot;
  // drawn first like this, it shows the cached name and the kind, nothing from the row.
  const bool known = idx >= 0 && idx < s.count;
  if (!known && drawn(R_HEADER)) return;
  const SessionRow& r = s.sessions[known ? idx : 0];  // row fields are read only when `known`
  if (!known && !(name && name[0])) anonymous = true;
  const char* title = known ? r.name : name;
  char buf[160];
  char tmp[48];

  // Only what is displayed goes into the hash (the model/ctx/tokens line is only on "Finished").
  uint32_t h = hashStr(hashInt(hashInt(kHashSeed, (uint32_t)lang), (uint32_t)kind), known ? r.id : "");
  // Meeting mode (anonymous): no name, tool or command at all.
  if (anonymous) h = hashInt(h, 1);
  else if (!known) h = hashStr(hashInt(h, 2), title);
  else h = hashStr(hashStr(hashStr(h, r.name), discreet ? "" : r.det), r.tool);
  if (!amber && known) {
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
    int sub = Y(92);  // baseline of the line under the title
    uint32_t took = 0;
    const bool timed = !amber && known && runs.stats(r.id, took);
    if (anonymous) {  // "A session needs you" / "Finished after 7:07", one or two lines of the title font
      char a[96];
      const char* b;
      if (amber) {
        snprintf(buf, sizeof(buf), "%s", t(lang, S::ASessionNeedsYou));
      } else if (timed) {
        miblo::formatElapsed(took, tmp, sizeof(tmp));
        snprintf(buf, sizeof(buf), t(lang, S::FinishedAfterAnon), tmp);
      } else {
        snprintf(buf, sizeof(buf), "%s", t(lang, S::Finished));
      }
      split2(buf, Font::Title, X(216), a, sizeof(a), &b);
      C().text(X(12), b[0] ? Y(56) : Y(62), a, Font::Title, color::TEXT, Align::Left, X(216));
      if (b[0]) {
        C().text(X(12), Y(84), b, Font::Title, color::TEXT, Align::Left, X(216));
        sub = Y(114);
      }
    } else {
      C().text(X(12), Y(62), title, Font::Hero, color::TEXT, Align::Left, X(216));
    }
    if (amber) {
      C().text(X(12), sub, t(lang, kind == AlertKind::Perm ? S::AskedPermission : S::AskedQuestion), Font::Body,
               color::AMBER, Align::Left, X(216));
      if (known && !anonymous && r.tool[0]) {
        C().fillRoundRect(X(12), Y(104), X(216), Y(32), Sz(4), color::CMD_BG);
        if (discreet || !r.det[0]) snprintf(buf, sizeof(buf), "%s", r.tool);
        else snprintf(buf, sizeof(buf), "%s: %s", r.tool, r.det);
        C().text(X(20), Y(125), buf, Font::Body, color::TEXT, Align::Left, X(200));
      }
    } else {
      if (timed && !anonymous) {  // anonymous: the title already says how long it took
        miblo::formatElapsed(took, tmp, sizeof(tmp));
        snprintf(buf, sizeof(buf), t(lang, S::Took), tmp);
        C().text(X(12), sub, buf, Font::Body, color::TEXT, Align::Left, X(216));
      }
      if (known) {
        metaLine(r, buf, sizeof(buf));
        C().text(X(12), sub + Y(26), buf, Font::Small, color::MUTED, Align::Left, X(216));
      }
    }
  }

  head.end();

  if (amber && known) {  // "Waiting for 3m": minute granularity, updated in place
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

// Long task fanfare (kFanfareMs, in place of the "finished" hero): confetti along the top and the
// bottom, the mascot hopping, and "app-mobile finished after 23min" in green, big enough to read
// from across the room. `name` "" (meeting mode): "Finished after 23min" (minutes, as the spec
// shows them: easier to read from afar than "23:07").
void fanfare(Lang lang, const char* name, uint32_t durSec, uint32_t ms) {
  confettiBands(ms / 250);
  MascotLook k = deskLook(DeskMood::Celebrate, true, ms);
  k.dy = (ms % 600) < 300 ? -6 : 0;  // a hop every 600 ms (two looks: the cat redraws 3x a second)
  if (mascotPet() == (uint8_t)miblo::Pet::Riff) {  // Riff rocks out instead: air guitar, headbanging
    const bool down = (ms % 600) < 300;
    k = MascotLook{0, (int8_t)(down ? 1 : -4), 0, 0, (ms / 1200) % 2 ? Eyes::Happy : Eyes::Closed,
                   down ? Paws::Down : Paws::ReachRight, (uint16_t)(kGuitar | kMouthWide)};
  }
  deskCat(R_BODY, X(120), Y(76), 38, k);

  char took[24], line[128];
  miblo::formatCountdown(durSec, took, sizeof(took));
  if (name && name[0]) snprintf(line, sizeof(line), t(lang, S::FinishedAfter), name, took);
  else snprintf(line, sizeof(line), t(lang, S::FinishedAfterAnon), took);
  if (!region(R_LIMITS, hashStr(hashInt(kHashSeed + 91, (uint32_t)lang), line), 0, Y(120), X(240), Y(94))) return;
  // The title font on up to three lines (balanced two when they fit), else two lines a size down.
  const int maxW = X(228);
  char a[96], first[96];
  const char* b;
  if (split2(line, Font::Title, maxW, a, sizeof(a), &b)) {
    centred2(line, Font::Title, color::GREEN, Y(160), Y(28), maxW);
    return;
  }
  // Three lines: the longest first line that fits, then the rest balanced on two.
  int cut = -1;
  for (int i = 0; line[i]; i++) {
    if (line[i] != ' ') continue;
    snprintf(first, sizeof(first), "%.*s", i, line);
    if (C().textWidth(first, Font::Title) <= maxW) cut = i;
    else break;
  }
  if (cut > 0 && split2(line + cut + 1, Font::Title, maxW, a, sizeof(a), &b) && b[0]) {
    snprintf(first, sizeof(first), "%.*s", cut, line);
    C().text(X(120), Y(146), first, Font::Title, color::GREEN, Align::Center, maxW);
    C().text(X(120), Y(174), a, Font::Title, color::GREEN, Align::Center, maxW);
    C().text(X(120), Y(202), b, Font::Title, color::GREEN, Align::Center, maxW);
    return;
  }
  centred2(line, Font::BodyBold, color::GREEN, Y(160), Y(28), maxW);
}

// A tie on a shirt collar, the collar's top at (cx, top), 240-grid units: two light collar
// wings, a small knot, then the blade widening down to its point (without the collar and the
// narrow neck it read as a down arrow).
static void tie(int cx, int top) {
  szTri(cx, top, -1, 0, -6, 0, -4, 4, color::MUTED);  // collar, left wing
  szTri(cx, top, 1, 0, 6, 0, 4, 4, color::MUTED);     // collar, right wing
  szTri(cx, top, -2, 1, 2, 1, 0, 4, color::VIOLET);   // knot
  szTri(cx, top, -1, 4, 1, 4, 3, 12, color::VIOLET);  // blade, widening
  szTri(cx, top, -1, 4, 3, 12, -3, 12, color::VIOLET);
  szTri(cx, top, -3, 12, 3, 12, 0, 15, color::VIOLET);  // point
}

// Meeting badge, an overlay drawn every frame over whatever screen is up (it has no region of
// its own: the screen under it may have just redrawn that corner). Small and cheap: a dark pill
// with a tie in the bottom-right corner. No words: "in a meeting" is wider than the corner that
// is free on every screen (the Desk's week reset and the Summary's week % end just left of it).
void meetingBadge(Lang lang) {
  (void)lang;
  const int w = Sz(18), h = Sz(20);
  const int x = X(236) - w, y = Y(236) - h;
  C().fillRoundRect(x, y, w, h, Sz(6), color::CMD_BG);
  tie(x + w / 2, y + Sz(2));
}

}  // namespace screens
