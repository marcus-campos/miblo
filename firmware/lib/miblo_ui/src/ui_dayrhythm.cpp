// The day's rhythm: wellness nudges, the end of the work day, Monday's recap of last week.
#include <stdio.h>
#include <string.h>

#include "miblo_format.h"
#include "ui_internal.h"
#include "ui_screens.h"

namespace screens {

using miblo::hashInt;
using miblo::hashStr;
using miblo::kHashSeed;
using miblo::Nudge;
using miblo::S;
using miblo::Snapshot;

// Where `s` breaks so its first line fits `maxW` in `f`: the last space that fits, else (text
// without spaces, like Chinese) the last character that fits. 0 = it does not break.
static size_t breakAt(const char* s, Font f, int maxW) {
  char first[128];
  size_t space = 0, glyph = 0;
  for (size_t i = 1; s[i] && i < sizeof(first) - 1; i++) {
    if ((s[i] & 0xC0) == 0x80) continue;  // inside a UTF-8 character
    memcpy(first, s, i);
    first[i] = 0;
    if (C().textWidth(first, f) > maxW) break;
    if (s[i] == ' ') space = i;
    glyph = i;
  }
  return space ? space : glyph;
}

// A phrase centred on one line (`y`) in `big`, or on two lines (`y1`, `y2`) in `big` when both
// fit, else in `small` (the second line is cut with "..." if it is still too long).
static void phrase(const char* s, Font big, Font small, uint16_t fg, int y, int y1, int y2, int maxW) {
  if (C().textWidth(s, big) <= maxW) {
    C().text(X(120), y, s, big, fg, Align::Center, maxW);
    return;
  }
  Font f = big;
  size_t at = breakAt(s, f, maxW);
  if (!at || C().textWidth(s + at + (s[at] == ' '), f) > maxW) {
    f = small;
    if (C().textWidth(s, f) <= maxW) {
      C().text(X(120), y, s, f, fg, Align::Center, maxW);
      return;
    }
    at = breakAt(s, f, maxW);
  }
  if (!at) {
    C().text(X(120), y, s, f, fg, Align::Center, maxW);
    return;
  }
  char first[128];
  memcpy(first, s, at);
  first[at] = 0;
  C().text(X(120), y1, first, f, fg, Align::Center, maxW);
  C().text(X(120), y2, s + at + (s[at] == ' '), f, fg, Align::Center, maxW);
}

// The cat's expression for each nudge, at `ms` into it. Never a flash: just the cat.
static MascotLook nudgeLook(Nudge kind, uint32_t ms) {
  MascotLook k{0, 0, 0, 0, Eyes::Open, Paws::Down, 0};
  switch (kind) {
    case Nudge::Break:  // a big stretch with a yawn, then content
      if (ms < 2000) {
        k.paws = Paws::Up;
        k.eyes = Eyes::Closed;
        k.extras = kMouthWide;
      } else {
        k.eyes = Eyes::Happy;
      }
      break;
    case Nudge::Water:  // a glass in the right paw, raised for a sip every other 2 s
      k.paws = Paws::ReachRight;
      if (ms / 2000 % 2) {
        k.paws = Paws::TapRight;
        k.gy = -2;
        k.eyes = Eyes::Happy;
      }
      break;
    case Nudge::Eyes:  // the gaze drifts to the edge and stays there, far away, without blinking
    default: {
      const uint32_t g = ms / 300;
      k.gx = (int8_t)(g < 4 ? g : 4);
      k.gy = -2;
      break;
    }
  }
  return k;
}

// The water nudge's glass (MascotLook's kCoffee mug, steaming and brown inside, reads as
// coffee), in the cat's 96-unit box at (cx, cy) drawn 2 * Sz(half) wide. Held in the raised right
// paw (drawn behind the cat, so the paw grips it), or lifted to the mouth while sipping (drawn
// over the cat). The water goes down a little each sip.
static void glass(int cx, int cy, int half, bool sip, uint8_t sips) {
  const int unit = Sz(half);
  auto s = [unit](int v) { return (v * unit + 24) / 48; };  // box units (all positive here) to px
  const int x = cx + s(sip ? 16 : 31), y = cy + s(sip ? 6 : 12);
  const int w = s(13), h = s(18);
  constexpr uint16_t kRim = 0xC618;    // #c0c0c0 the glass
  constexpr uint16_t kInside = 0x29A8;  // #2a3440 empty glass
  constexpr uint16_t kWater = 0x7E3E;   // #7cc4f5
  C().fillRect(x, y, w, h, kRim);
  C().fillRect(x + 1, y, w - 2, h - 1, kInside);
  const int level = s(4 + 2 * (sips < 4 ? sips : 4));  // from the rim down
  C().fillRect(x + 1, y + level, w - 2, h - 1 - level, kWater);
  C().fillRect(x + 2, y + level + 1, 1, h - 3 - level > 0 ? h - 3 - level : 0, color::WHITE);  // shine
}

// The cat in its box with the glass, composed together in strips like deskCat() (no big layer,
// no flash of the cat without its glass).
static void catWithGlass(int cx, int cy, int half, const MascotLook& k, uint32_t ms) {
  const bool sip = ms / 2000 % 2;
  const uint8_t sips = (uint8_t)(ms / 4000);
  // The look follows `sip`; a hat or colour change redraws too.
  const uint32_t h = hashInt(hashInt(kHashSeed + 229, mascotPaintHash() ^ mascotAccessory()),
                             (uint32_t)sip | (uint32_t)(sips < 4 ? sips : 4) << 1);
  if (!dirty(R_BODY, h)) return;
  const int px = Sz(half);
  const int stripH = (2 * px + 7) / 8;
  for (int y = cy - px; y < cy + px; y += stripH) {
    const int sh = y + stripH <= cy + px ? stripH : cy + px - y;
    const bool layered = C().beginLayer(cx - px, y, 2 * px, sh);
    if (sip) {
      deskMascot(cx, cy, k, half);
      glass(cx, cy, half, sip, sips);
    } else {  // the box's background, the glass, then the cat over it without its own background
      C().fillRect(cx - px, cy - px, 2 * px, 2 * px, color::BG);
      glass(cx, cy, half, sip, sips);
      deskMascot(cx, cy, k, half, true, false);
    }
    if (!layered) break;  // no memory even for a strip: drawn directly, once
    C().endLayer();
  }
  C().releaseLayer();
}

void nudge(Lang lang, Nudge kind, uint32_t ms, uint8_t breakMin) {
  if (kind == Nudge::Water) catWithGlass(X(120), Y(90), 52, nudgeLook(kind, ms), ms);
  else deskCat(R_BODY, X(120), Y(90), 52, nudgeLook(kind, ms));
  const S id = kind == Nudge::Break ? S::NudgeBreak : kind == Nudge::Water ? S::NudgeWater : S::NudgeEyes;
  const uint32_t h = hashInt(hashInt(hashInt(kHashSeed + 211, (uint32_t)lang), (uint32_t)kind), breakMin);
  if (region(R_ROW0, h, 0, Y(152), X(240), Y(74))) {
    char buf[96];  // "How about a %u min break?": the longest translation is well under this
    snprintf(buf, sizeof(buf), t(lang, id), (unsigned)breakMin);
    phrase(buf, Font::Title, Font::BodyBold, color::TEXT, Y(190), Y(180), Y(208), X(228));
  }
}

// Time worked: "45min", "3h12", and past a day still in hours ("31h20", never "1d7h").
static void workTime(uint32_t secs, char* out, size_t cap) {
  if (secs < 3600) miblo::formatCountdown(secs, out, cap);
  else snprintf(out, cap, "%luh%02u", (unsigned long)(secs / 3600), (unsigned)(secs / 60 % 60));
}

// A title at the top, the cat small in the middle and the three numbers of a summary (responses
// big, time worked and cost in two columns), laid out like summary().
static void totals(uint32_t salt, Lang lang, S title, uint32_t turns, uint32_t workSec, float usd) {
  if (region(R_HEADER, salt, 0, 0, X(240), Y(26))) {
    C().text(X(120), Y(19), t(lang, title), Font::SmallBold, color::DIM, Align::Center, X(220));
  }
  const uint32_t h = hashInt(hashInt(hashInt(salt, turns), workSec / 60), (uint32_t)(usd * 100));
  if (!region(R_LIMITS, h, 0, Y(96), X(240), Y(110))) return;
  char buf[24];
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)turns);
  C().text(X(120), Y(130), buf, Font::NumL, color::TEXT, Align::Center, X(220));
  C().text(X(120), Y(150), t(lang, S::SumResponses), Font::Body, color::MUTED, Align::Center, X(228));
  C().fillRect(X(24), Y(160), X(192), 1, color::DIVIDER);
  workTime(workSec, buf, sizeof(buf));
  C().text(X(64), Y(184), buf, Font::Title, color::GREEN, Align::Center, X(112));
  C().text(X(64), Y(201), t(lang, S::SumWorked), Font::Small, color::MUTED, Align::Center, X(112));
  miblo::formatUsd(usd, buf, sizeof(buf));
  C().text(X(176), Y(184), buf, Font::Title, color::TEXT, Align::Center, X(112));
  C().text(X(176), Y(201), t(lang, S::SumSpent), Font::Small, color::MUTED, Align::Center, X(112));
}

void dayEnd(Lang lang, const Snapshot& s, const char* owner, uint32_t ms) {
  // The cat yawns every 4 s, sleepy in between.
  const bool yawn = ms % 4000 < 1200;
  const MascotLook k{0, 0, 0, 0, yawn ? Eyes::Closed : Eyes::Sleepy, Paws::Down, (uint16_t)(yawn ? kMouthWide : 0)};
  deskCat(R_BODY, X(120), Y(60), 32, k);
  const uint32_t salt = hashInt(kHashSeed + 223, (uint32_t)lang);
  totals(salt, lang, S::TodayTitle, s.todayTurns, s.todayWorkSec, s.todayUsd);
  const bool named = owner && owner[0];
  if (region(R_FOOT, hashStr(salt, named ? owner : ""), 0, Y(208), X(240), Y(32))) {
    char line[96];
    if (named) snprintf(line, sizeof(line), t(lang, S::RestWellName), owner);
    else snprintf(line, sizeof(line), "%s", t(lang, S::RestWell));
    const Font f = C().textWidth(line, Font::BodyBold) <= X(228) ? Font::BodyBold : Font::Body;
    C().text(X(120), Y(229), line, f, color::AMBER, Align::Center, X(228));
  }
}

void weekRecap(Lang lang, const Snapshot& s, uint32_t ms) {
  // Content, with a slow blink.
  const MascotLook k{0, 0, 0, 0, ms % 5000 < 4700 ? Eyes::Happy : Eyes::Closed, Paws::Down, 0};
  deskCat(R_BODY, X(120), Y(60), 32, k);
  const uint32_t salt = hashInt(kHashSeed + 227, (uint32_t)lang);
  totals(salt, lang, S::LastWeekTitle, s.week.turns, s.week.workSec, s.week.usd);
  const uint8_t day = s.week.busiest;
  if (region(R_FOOT, hashInt(salt, day), 0, Y(208), X(240), Y(32)) && day < 7) {
    char line[96];
    snprintf(line, sizeof(line), t(lang, S::BusiestDay), t(lang, (S)((uint16_t)S::WdSun + day)));
    C().text(X(120), Y(229), line, Font::Body, color::VIOLET, Align::Center, X(228));
  }
}

}  // namespace screens
