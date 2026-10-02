// Focus (Pomodoro) screen: the cat with headphones inside a progress ring, the time left big under
// it, "focus until 15:30" and the rounds as dots. Readable from across the room: it is the
// office's "do not disturb".
#include <stdio.h>

#include "miblo_format.h"
#include "ui_internal.h"
#include "ui_screens.h"

namespace screens {

using miblo::FocusPhase;
using miblo::hashInt;
using miblo::hashStr;
using miblo::kHashSeed;
using miblo::S;

namespace {

// 240-grid geometry. The ring's inner radius clears the cat box's corners (44 * sqrt 2 = 62.2),
// so the cat's redraws never touch it; the gap at the bottom leaves room for the time.
constexpr int kCatY = 84, kCatHalf = 44, kRingR = 70, kRingIr = 63;
constexpr int kRingFrom = 45, kRingSpan = 270;  // degrees, 0 = 6 o'clock, clockwise
constexpr uint32_t kStretchMs = 3000;           // the break starts with a stretch...
constexpr uint32_t kTapMs = 1500;               // one paw, then the other: slow typing

// This screen's slots in the region cache.
enum : uint8_t { R_RING = R_WEEK, R_TEXT = R_LIMITS, R_BIG = R_TIME0, R_LINE = R_ROW0 };

// ...seen once the end-of-focus cue (full-screen pulses, drawn instead of this screen) is over.
uint32_t stretchUntilMs() {
  return miblo::cuePulses(miblo::CueKind::FocusEnd) * miblo::kCuePulseMs + kStretchMs;
}

bool isBreak(FocusPhase p) { return p == FocusPhase::Break || p == FocusPhase::LongBreak; }

uint16_t ringColor(FocusPhase p) { return isBreak(p) ? color::BLUE : color::GREEN; }

MascotLook catLook(FocusPhase p, uint32_t phaseMs, uint32_t ms) {
  MascotLook k{0, 0, 0, 0, Eyes::Open, Paws::Down, 0};
  if (p == FocusPhase::Focus) {  // headphones on, eyes on the keyboard, typing slowly
    k.gy = 2;
    k.paws = (ms / kTapMs) % 2 ? Paws::TapRight : Paws::TapLeft;
    k.extras = kHeadphones;
  } else if (p == FocusPhase::Back) {
    k.eyes = Eyes::Wide;
  } else if (phaseMs < stretchUntilMs()) {
    k.eyes = Eyes::Closed;
    k.paws = Paws::Up;
  } else {
    if (p == FocusPhase::LongBreak) k.eyes = Eyes::Happy;
    else if (ms % 4000 < 200) k.eyes = Eyes::Closed;  // an occasional blink
  }
  return k;
}

// The ring moves in 2-degree steps; the filled part and the track never overlap, so a step
// repaints nothing that was already right (no flicker).
void ring(FocusPhase p, uint32_t elapsedMs, uint32_t lenMs) {
  int deg = lenMs ? (int)((uint64_t)kRingSpan * elapsedMs / lenMs) : 0;
  if (deg > kRingSpan) deg = kRingSpan;
  deg &= ~1;
  if (!dirty(R_RING, hashInt(hashInt(kHashSeed + 211, (uint32_t)p), (uint32_t)deg))) return;
  const int cx = X(120), cy = Y(kCatY), r = Sz(kRingR), ir = Sz(kRingIr);
  const int end = kRingFrom + deg;
  if (deg > 0) C().arc(cx, cy, r, ir, kRingFrom, end, ringColor(p), color::BG);
  if (deg < kRingSpan) C().arc(cx, cy, r, ir, end, kRingFrom + kRingSpan, color::TRACK, color::BG);
}

// Rounds as dots: done and current filled (the current one in the phase colour), the rest hollow.
void dots(FocusPhase p, uint8_t round, uint8_t rounds, int y) {
  if (rounds == 0) return;
  const int step = Sz(rounds > 8 ? 13 : 16), rad = Sz(4);
  int x = X(120) - (rounds - 1) * step / 2;
  for (uint8_t i = 1; i <= rounds; i++, x += step) {
    if (i < round) C().fillCircle(x, y, rad, color::MUTED);
    else if (i == round) C().fillCircle(x, y, rad, ringColor(p));
    else C().arc(x, y, rad, rad - Sz(2) + 1, 0, 360, color::FAINT, color::BG);
  }
}

}  // namespace

void focus(Lang lang, const Clock& clk, FocusPhase phase, uint8_t round, uint8_t rounds, uint32_t leftMs,
           uint32_t lenMs, uint32_t untilEpoch, uint32_t ms) {
  if (phase == FocusPhase::Off) return;
  if (leftMs > lenMs) leftMs = lenMs;
  const uint32_t elapsed = lenMs - leftMs;
  const uint32_t hl = hashInt(kHashSeed + 209, (uint32_t)lang);
  clockRight(hl, clk, Y(18), color::DIM, color::BG);
  deskCat(R_BODY, X(120), Y(kCatY), kCatHalf, catLook(phase, elapsed, ms));
  ring(phase, elapsed, lenMs);

  // Everything under the ring changes with the phase: one cleared block, values in fields.
  const uint32_t ht = hashInt(hashInt(hashInt(hl, (uint32_t)phase), round), rounds);
  if (region(R_TEXT, ht, 0, Y(140), X(240), Y(96))) {
    if (isBreak(phase)) {
      C().text(X(120), Y(200), t(lang, phase == FocusPhase::Break ? S::FocusBreak : S::FocusLongBreak),
               Font::BodyBold, color::BLUE, Align::Center, X(228));
    } else if (phase == FocusPhase::Back) {
      const char* back = t(lang, S::FocusBack);
      const Font f = C().textWidth(back, Font::Title) <= X(228) ? Font::Title : Font::BodyBold;
      C().text(X(120), Y(172), back, f, color::GREEN, Align::Center, X(228));
    }
    dots(phase, round, rounds, Y(224));
  }
  char left[16];
  miblo::formatMinSec((leftMs + 999) / 1000, left, sizeof(left));
  if (phase == FocusPhase::Back) {  // the minute ticks away small; the question is the big thing
    field(R_LINE, ht, X(120), Y(200), left, Font::Body, color::MUTED, color::BG, Align::Center, X(120));
    return;
  }
  field(R_BIG, ht, X(120), Y(174), left, Font::NumL, color::TEXT, color::BG, Align::Center, X(200));
  if (phase == FocusPhase::Focus && untilEpoch) {
    char when[24], line[64];
    formatWhen(lang, untilEpoch + 30, clk.epoch ? clk.epoch : untilEpoch, when, sizeof(when));  // nearest minute
    snprintf(line, sizeof(line), t(lang, S::FocusUntil), when);
    field(R_LINE, ht, X(120), Y(200), line, Font::Body, color::TEXT, color::BG, Align::Center, X(228));
  }
}

}  // namespace screens
