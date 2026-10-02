// Notes on the desk: the cat holding a text (say / reminder / alarm / "Time's up!"), the timer
// with its hourglass, and find (waving, with the settings QR).
#include <stdio.h>
#include <string.h>

#include "miblo_format.h"
#include "miblo_utf8.h"
#include "ui_internal.h"
#include "ui_screens.h"

namespace screens {

using miblo::hashInt;
using miblo::hashStr;
using miblo::kHashSeed;
using miblo::NoteKind;
using miblo::S;

// A blink now and then, so a cat that stays up for minutes looks alive (the box is only
// redrawn when the look changes: twice every 4 s).
static bool blinking(uint32_t ms) { return ms % 4000 < 160; }

// ---- note: the cat holding a sign ----

namespace {
// Up to 4 lines of a text, as byte ranges of it.
struct Lines {
  uint8_t start[4], len[4];
  uint8_t n;
};
// One way to set the sign's text: a font, how many lines it may take, their spacing (240 grid).
struct Fit {
  Font f;
  uint8_t lines;
  uint8_t lineH;
};
// Biggest first. A switch, not a table: a const table would sit in RAM on the ESP8266.
Fit fitAt(int i) {
  switch (i) {
    case 0: return {Font::Title, 1, 26};
    case 1: return {Font::BodyBold, 3, 21};
    default: return {Font::Small, 4, 17};
  }
}
constexpr int kFits = 3;
}  // namespace

// Width of s[0..len) in font f.
static int widthOf(const char* s, size_t len, Font f) {
  char buf[64];
  if (len >= sizeof(buf)) len = sizeof(buf) - 1;
  memcpy(buf, s, len);
  buf[len] = 0;
  return C().textWidth(buf, f);
}

// Wraps `s` into at most `maxLines` lines no wider than `w`: at the last space that fits, or
// between two characters when a word alone is too wide (CJK has no spaces). False when the
// text needs more lines; `out` then holds the first `maxLines`, the last one with the rest of
// the text (the canvas cuts it with an ellipsis).
static bool wrap(const char* s, Font f, int w, uint8_t maxLines, Lines& out) {
  out.n = 0;
  size_t pos = 0;
  const size_t total = strlen(s) < 255 ? strlen(s) : 255;
  while (pos < total && out.n < maxLines) {
    while (pos < total && s[pos] == ' ') pos++;
    if (pos >= total) break;
    size_t end = pos, lastSpace = 0;
    const char* p = s + pos;
    while ((size_t)(p - s) < total) {
      const char* q = p;
      miblo::utf8Next(q);
      const size_t next = (size_t)(q - s);
      if (widthOf(s + pos, next - pos, f) > w) break;
      if (*p == ' ') lastSpace = (size_t)(p - s);
      end = next;
      p = q;
    }
    if ((size_t)(p - s) < total && lastSpace > pos) end = lastSpace;  // break at the last space that fits
    if (end == pos) {  // not even one character fits: take it anyway
      const char* q = s + pos;
      miblo::utf8Next(q);
      end = (size_t)(q - s);
    }
    size_t e = end;
    while (e > pos && s[e - 1] == ' ') e--;
    out.start[out.n] = (uint8_t)pos;
    out.len[out.n] = (uint8_t)(e - pos);
    out.n++;
    pos = end;
  }
  while (pos < total && s[pos] == ' ') pos++;
  if (pos < total && out.n) out.len[out.n - 1] = (uint8_t)(total - out.start[out.n - 1]);
  return pos >= total;
}

// The sign's text, as big as it fits: Title on one line, else BodyBold on up to 3 lines, else
// Small on up to 4 (the last one cut with an ellipsis if even that is not enough).
static void signText(const char* s, int cx, int top, int h, int w) {
  Lines l;
  Fit use = fitAt(kFits - 1);
  for (int i = 0; i < kFits; i++) {
    const Fit f = fitAt(i);
    if (wrap(s, f.f, w, f.lines, l)) {
      use = f;
      break;
    }
    if (i == kFits - 1) wrap(s, f.f, w, f.lines, l);  // the smallest, cut
  }
  const int lineH = Y(use.lineH);
  // Baselines: the block centred in the sign (a line's ink sits about 0.7 of lineH above it).
  int y = top + (h - l.n * lineH) / 2 + lineH * 7 / 10 + Y(1);
  char buf[64];
  for (uint8_t i = 0; i < l.n; i++, y += lineH) {
    const size_t len = l.len[i] < sizeof(buf) - 1 ? l.len[i] : sizeof(buf) - 1;
    memcpy(buf, s + l.start[i], len);
    buf[len] = 0;
    C().text(cx, y, buf, use.f, color::TEXT, Align::Center, w);
  }
}

static uint16_t signColor(NoteKind kind) {
  switch (kind) {
    case NoteKind::Reminder:
    case NoteKind::Alarm: return color::AMBER;
    case NoteKind::Timer: return color::GREEN;
    default: return color::VIOLET;
  }
}

void note(Lang lang, NoteKind kind, const char* text, const Clock& clk, uint32_t ms) {
  const bool urgent = kind == NoteKind::Reminder || kind == NoteKind::Alarm || kind == NoteKind::Timer;
  field(R_CLOCK, kHashSeed + 71, X(120), Y(18), clk.hhmm, Font::Small, color::DIM, color::BG, Align::Center, X(60));
  // The cat right above the sign, its paws on the sign's top edge; eyes wide with a "!" for
  // what came due, happy for a message.
  MascotLook look{0, 0, 0, 3, urgent ? Eyes::Wide : Eyes::Happy, Paws::Down, (uint16_t)(urgent ? kAlarm : 0)};
  if (blinking(ms)) look.eyes = Eyes::Closed;
  deskCat(R_BODY, X(120), Y(72), 40, look);
  const char* s = kind == NoteKind::Timer ? t(lang, S::TimesUp) : (text ? text : "");
  const uint32_t h = hashStr(hashInt(hashInt(kHashSeed + 73, (uint32_t)lang), (uint32_t)kind), s);
  const int x = X(12), top = Y(112), w = X(216), hh = Y(112);
  if (region(R_LIMITS, h, x, top, w, hh)) {
    const int b = Sz(3);
    C().fillRoundRect(x, top, w, hh, Sz(10), signColor(kind));
    C().fillRoundRect(x + b, top + b, w - 2 * b, hh - 2 * b, Sz(8), color::CARD);
    signText(s, X(120), top, hh, X(204));
  }
}

// ---- timer: the cat watching an hourglass ----

// The hourglass centred on (cx, cy), 240-grid units: 60 wide, 94 tall. `left` = sand still on
// top in 1/36ths; `drop` = the falling grains' phase (0..2), -1 when none fall.
static void hourglass(int cx, int cy, int left, int drop) {
  const int gone = 36 - left;
  szRound(cx, cy, -30, -47, 60, 7, 3, color::MUTED);
  szRound(cx, cy, -30, 40, 60, 7, 3, color::MUTED);
  szTri(cx, cy, -25, -40, 25, -40, 0, -1, color::TRACK);  // the glass, repainted over the old sand
  szTri(cx, cy, -25, 40, 25, 40, 0, 1, color::TRACK);
  if (left > 0) {  // the top sand: a triangle shrinking towards the neck
    const int sw = 25 * left / 36;
    szTri(cx, cy, -sw, -1 - left, sw, -1 - left, 0, -1, color::AMBER);
  }
  if (gone > 0) {  // the pile below, growing
    const int ph = 30 * gone / 36, pw = 24 * gone / 36 + 1;
    szTri(cx, cy, -pw, 39, pw, 39, 0, 39 - ph, color::AMBER);
  }
  if (drop >= 0 && left > 0) {  // grains falling through the neck
    for (int y = 2 + drop * 3; y < 36 - 30 * gone / 36; y += 9) szRect(cx, cy, -1, y, 2, 4, color::AMBER);
  }
}

void timer(Lang lang, const Clock& clk, uint32_t leftMs, uint32_t lenMs, uint32_t ms) {
  (void)lang;
  field(R_CLOCK, kHashSeed + 79, X(120), Y(18), clk.hhmm, Font::Small, color::DIM, color::BG, Align::Center, X(60));
  // The cat to the left, eyes on the hourglass.
  MascotLook look{0, 0, 3, 2, Eyes::Open, Paws::Down, 0};
  if (blinking(ms)) look.eyes = Eyes::Closed;
  deskCat(R_BODY, X(72), Y(86), 40, look);
  const uint32_t len = lenMs ? lenMs : 1;
  const uint32_t left = leftMs > len ? len : leftMs;
  uint32_t sand = left ? (uint32_t)((uint64_t)left * 36 / len) + 1 : 0;
  if (sand > 36) sand = 36;
  const int drop = left ? (int)(ms / 1000 % 3) : -1;
  // The hourglass: its shapes are repainted over themselves (no clear, no flash) when the sand
  // or the falling grains move; only the neck's column of grains is cleared first.
  if (dirty(R_LIMITS, hashInt(hashInt(kHashSeed + 83, sand), (uint32_t)(drop + 1)))) {
    szRect(X(170), Y(86), -2, 1, 4, 37, color::TRACK);
    hourglass(X(170), Y(86), (int)sand, drop);
  }
  char buf[16];
  miblo::formatMinSec((left + 999) / 1000, buf, sizeof(buf));
  field(R_RESET5, kHashSeed + 89, X(120), Y(184), buf, Font::NumL, color::TEXT, color::BG, Align::Center, X(200));
  const uint8_t pct = (uint8_t)((uint64_t)(len - left) * 100 / len);
  if (region(R_FOOT, hashInt(kHashSeed + 97, pct), X(40), Y(204), X(160), Y(8))) {
    bar(X(40), Y(205), X(160), Y(6) < 3 ? 3 : Y(6), pct, color::GREEN);
  }
}

// ---- find: waving, the settings QR, the address ----

void findMe(Lang lang, const char* settingsUrl, uint32_t ms) {
  const char* url = settingsUrl ? settingsUrl : "";
  // The bands at the top and the bottom pulse gently (BG <-> CARD every 500 ms), with the
  // title and the address on them.
  const bool lit = ms / 500 % 2 == 1;
  const uint16_t band = lit ? color::CARD : color::BG;
  const uint32_t hl = hashInt(hashInt(kHashSeed + 101, (uint32_t)lang), lit);
  if (region(R_HEADER, hl, 0, 0, X(240), Y(36), band)) {
    C().text(X(120), Y(27), t(lang, S::FindMe), Font::Title, color::TEXT, Align::Center, X(228));
  }
  if (region(R_FOOT, hashStr(hl, url), 0, Y(206), X(240), Y(240) - Y(206), band)) {
    C().text(X(120), Y(228), url, Font::Small, color::MUTED, Align::Center, X(228));
  }
  // Waving: the right paw up and down every 400 ms.
  const MascotLook look{0, 0, 3, 0, Eyes::Happy, ms / 400 % 2 ? Paws::ReachRight : Paws::Down, 0};
  deskCat(R_BODY, X(64), Y(121), 44, look);
  const int scale = Sz(3) < 2 ? 2 : Sz(3);
  const int size = (29 + 4) * scale;  // QR version 3 (29 modules) + the 2-module quiet zone
  if (dirty(R_LIMITS, hashStr(kHashSeed + 103, url)) && url[0]) {
    qr(url, X(176) - size / 2, Y(121) - size / 2, scale);
  }
}

}  // namespace screens
