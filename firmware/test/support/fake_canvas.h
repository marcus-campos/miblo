#pragma once
// Fake canvas for testing layouts on the host: records texts/arcs and checks that everything fits on screen.
#include <stdlib.h>

#include <string>
#include <vector>

#include "miblo_utf8.h"
#include "ui_canvas.h"

class FakeCanvas : public ui::Canvas {
 public:
  explicit FakeCanvas(ui::ScreenSpec s) : spec_(s) {}
  ui::ScreenSpec spec() const override { return spec_; }
  void fillRect(int x, int y, int w, int h, uint16_t) override { box(x, y, w, h); }
  void fillRoundRect(int x, int y, int w, int h, int, uint16_t) override { box(x, y, w, h); }
  void drawRect(int x, int y, int w, int h, uint16_t) override { box(x, y, w, h); }
  void fillCircle(int cx, int cy, int r, uint16_t) override { box(cx - r, cy - r, 2 * r, 2 * r); }
  void wideLine(int x0, int y0, int x1, int y1, int, uint16_t, uint16_t) override {
    box(x0 < x1 ? x0 : x1, y0 < y1 ? y0 : y1, abs(x1 - x0), abs(y1 - y0));
  }
  void arc(int cx, int cy, int r, int, int a0, int a1, uint16_t, uint16_t) override {
    box(cx - r, cy - r, 2 * r, 2 * r);
    arcs.push_back(a1 - a0);
  }
  // Text: 6 px per character, 10 px tall above the baseline.
  int text(int x, int y, const char* s, ui::Font, uint16_t, ui::Align a, int maxW) override {
    int w = textWidth(s, ui::Font::Small);
    if (w > maxW) w = maxW;
    const int left = a == ui::Align::Left ? x : (a == ui::Align::Center ? x - w / 2 : x - w);
    box(left, y - 10, w, 10);
    texts.push_back(s ? s : "");
    return w;
  }
  int textWidth(const char* s, ui::Font) override { return 6 * (int)miblo::utf8Length(s ? s : ""); }

  bool drew(const std::string& needle) const {
    for (const auto& t : texts) {
      if (t.find(needle) != std::string::npos) return true;
    }
    return false;
  }
  void clearLog() {
    texts.clear();
    arcs.clear();
    calls = 0;
  }

  ui::ScreenSpec spec_;
  std::vector<std::string> texts;
  std::vector<int> arcs;
  int calls = 0;
  int outOfBounds = 0;

 private:
  void box(int x, int y, int w, int h) {
    calls++;
    if (x < 0 || y < 0 || x + w > spec_.w || y + h > spec_.h) outOfBounds++;
  }
};
