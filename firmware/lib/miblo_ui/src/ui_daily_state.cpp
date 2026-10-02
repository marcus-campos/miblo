// Daily-life state that changes how every screen draws (like setMascotAccessory in ui_base.cpp):
// set by app.cpp when it changes, read by the screens. Strings are copied (cut when too long).
#include <stdio.h>

#include "miblo_overview.h"
#include "ui_internal.h"
#include "ui_screens.h"

namespace screens {

static bool g_tie = false;
static uint8_t g_mood = 0;
static char g_label[37] = "";      // second clock: Config::tz2Label's size
static char g_hhmm[6] = "";        // "23:59"
static char g_countdown[64] = "";  // miblo::countdownLine
static char g_qr[32] = "";         // "http://192.168.100.200/"

void setMascotTie(bool on) { g_tie = on; }
bool mascotTie() { return g_tie; }

void setCatMood(uint8_t mood) { g_mood = mood; }
uint8_t catMood() { return g_mood; }

void setSecondClock(const char* label, const char* hhmm) {
  snprintf(g_label, sizeof(g_label), "%s", label ? label : "");
  snprintf(g_hhmm, sizeof(g_hhmm), "%s", hhmm ? hhmm : "");
}
const char* secondClockLabel() { return g_label; }
const char* secondClockTime() { return g_hhmm; }

void setDeskExtras(const char* countdownLine, const char* qrUrl) {
  snprintf(g_countdown, sizeof(g_countdown), "%s", countdownLine ? countdownLine : "");
  snprintf(g_qr, sizeof(g_qr), "%s", qrUrl ? qrUrl : "");
}
const char* deskCountdown() { return g_countdown; }
const char* deskQrUrl() { return g_qr; }

// ---- The waiting mark ----

// The band's height on the 240 grid.
constexpr int kMarkH = 30;

// Forwards everything to the canvas it wraps and notes when a drawing reached the waiting mark's
// band (the top of the screen) or the whole panel was cleared. Text extents are estimated
// generously (an over-estimate only costs a repaint of the band, never a flicker).
class GuardCanvas : public ui::Canvas {
 public:
  void wrap(ui::Canvas& inner) {
    in_ = &inner;
    touched_ = true;
  }
  void setBand(int bottom) { bottom_ = bottom; }
  // True once since the last call if something was drawn under the band.
  bool takeTouched() {
    const bool t = touched_;
    touched_ = false;
    return t;
  }

  ui::ScreenSpec spec() const override { return in_->spec(); }
  void fillRect(int x, int y, int w, int h, uint16_t c) override {
    hit(y, h);
    in_->fillRect(x, y, w, h, c);
  }
  void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) override {
    hit(y, h);
    in_->fillRoundRect(x, y, w, h, r, c);
  }
  void drawRect(int x, int y, int w, int h, uint16_t c) override {
    hit(y, h);
    in_->drawRect(x, y, w, h, c);
  }
  void fillCircle(int cx, int cy, int r, uint16_t c) override {
    hit(cy - r, 2 * r + 1);
    in_->fillCircle(cx, cy, r, c);
  }
  void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) override {
    const int top = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
    hit(top, 1);
    in_->fillTriangle(x0, y0, x1, y1, x2, y2, c);
  }
  void wideLine(int x0, int y0, int x1, int y1, int width, uint16_t c, uint16_t bg) override {
    hit((y0 < y1 ? y0 : y1) - width, 1);
    in_->wideLine(x0, y0, x1, y1, width, c, bg);
  }
  void arc(int cx, int cy, int r, int ir, int a0, int a1, uint16_t fg, uint16_t bg) override {
    hit(cy - r, 2 * r + 1);
    in_->arc(cx, cy, r, ir, a0, a1, fg, bg);
  }
  int text(int x, int y, const char* s, ui::Font f, uint16_t fg, ui::Align a, int maxW) override {
    hit(y - kTextUp, 1);
    return in_->text(x, y, s, f, fg, a, maxW);
  }
  int textWidth(const char* s, ui::Font f) override { return in_->textWidth(s, f); }
  int textBox(int x, int y, const char* s, ui::Font f, uint16_t fg, uint16_t bg, ui::Align a, int boxW) override {
    hit(y - kTextUp, 1);
    return in_->textBox(x, y, s, f, fg, bg, a, boxW);
  }
  bool beginLayer(int x, int y, int w, int h) override {
    hit(y, h);
    return in_->beginLayer(x, y, w, h);
  }
  void endLayer() override { in_->endLayer(); }
  void releaseLayer() override { in_->releaseLayer(); }
  void clear(uint16_t c) override {
    touched_ = true;
    in_->clear(c);
  }

 private:
  static constexpr int kTextUp = 64;  // the tallest glyphs above a baseline, generously
  void hit(int top, int h) {
    (void)h;
    if (top < bottom_) touched_ = true;
  }
  ui::Canvas* in_ = nullptr;
  int bottom_ = 0;
  bool touched_ = true;
};

static GuardCanvas g_guard;
static bool g_guarded = false;     // the bound canvas is g_guard
static uint32_t g_markHash = 0;    // what the band shows

ui::Canvas& waitingGuard(ui::Canvas& inner) {
  g_guard.wrap(inner);
  g_guarded = true;
  return g_guard;
}

void waitingMark(Lang lang, const char* name, uint8_t pending) {
  const bool named = name && name[0];
  uint32_t h = miblo::hashInt(miblo::hashInt(miblo::kHashSeed + 211, (uint32_t)lang), pending);
  h = miblo::hashStr(h, named ? name : "");
  const bool guarded = g_guarded && &canvas() == &g_guard;
  g_guard.setBand(Y(kMarkH));
  const bool touched = guarded && g_guard.takeTouched();
  if (guarded && !touched && h == g_markHash) return;
  g_markHash = h;
  // Painted over, never cleared first: repainting the same pixels shows no flicker.
  C().fillRect(0, 0, X(240), Y(kMarkH), color::AMBER);
  // "!" in a dark disc.
  szDisc(X(18), Y(15), 0, 0, 10, color::BLACK);
  szRect(X(18), Y(15), -2, -7, 4, 9, color::AMBER);
  szRect(X(18), Y(15), -2, 4, 4, 3, color::AMBER);
  char buf[96];
  if (!named) snprintf(buf, sizeof(buf), "%s", t(lang, miblo::S::NeedsYou));
  else if (pending > 1) snprintf(buf, sizeof(buf), "%s +%u", name, (unsigned)(pending - 1));
  else snprintf(buf, sizeof(buf), "%s", name);
  C().text(X(34), Y(22), buf, Font::BodyBold, color::BLACK, Align::Left, X(198));
  if (guarded) g_guard.takeTouched();  // its own drawing does not count
}

}  // namespace screens
