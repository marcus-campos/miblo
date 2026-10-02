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
  void fillRect(int x, int y, int w, int h, uint16_t c) override { paint(x, y, w, h, c); }
  void fillRoundRect(int x, int y, int w, int h, int, uint16_t c) override { paint(x, y, w, h, c); }
  void drawRect(int x, int y, int w, int h, uint16_t) override { box(x, y, w, h); }
  void fillCircle(int cx, int cy, int r, uint16_t) override { box(cx - r, cy - r, 2 * r, 2 * r); }
  void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t) override {
    const int lx = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
    const int hx = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
    const int ly = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
    const int hy = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
    box(lx, ly, hx - lx + 1, hy - ly + 1);
  }
  void wideLine(int x0, int y0, int x1, int y1, int, uint16_t, uint16_t) override {
    box(x0 < x1 ? x0 : x1, y0 < y1 ? y0 : y1, abs(x1 - x0), abs(y1 - y0));
  }
  void arc(int cx, int cy, int r, int, int a0, int a1, uint16_t, uint16_t) override {
    box(cx - r, cy - r, 2 * r, 2 * r);
    arcs.push_back(a1 - a0);
  }
  // Text: 6 px per character, 10 px tall above the baseline.
  int text(int x, int y, const char* s, ui::Font f, uint16_t, ui::Align a, int maxW) override {
    int w = textWidth(s, ui::Font::Small);
    cut.push_back(w > maxW);
    if (w > maxW) w = maxW;
    const int left = a == ui::Align::Left ? x : (a == ui::Align::Center ? x - w / 2 : x - w);
    box(left, y - 10, w, 10);
    texts.push_back(s ? s : "");
    fonts.push_back(f);
    textBgs.push_back(colorAt(left + (w > 0 ? w / 2 : 0), y - 5));
    return w;
  }
  int textWidth(const char* s, ui::Font) override { return 6 * (int)miblo::utf8Length(s ? s : ""); }
  // Text box: 10 px above the baseline, 3 below, boxW wide; painted with bg (never a clear).
  int textBox(int x, int y, const char* s, ui::Font f, uint16_t fg, uint16_t bg, ui::Align a, int boxW) override {
    const int bx = a == ui::Align::Left ? x : (a == ui::Align::Center ? x - boxW / 2 : x - boxW);
    fill(bx, y - 10, boxW, 13, bg);
    boxTexts++;
    const int w = text(x, y, s, f, fg, a, boxW);
    textBgs.back() = bg;
    return w;
  }
  // Layers: recorded (a layer is "available" unless layerSupported = false); primitives drawn
  // while a layer is open are counted in layerCalls.
  bool beginLayer(int x, int y, int w, int h) override {
    layerBegins++;
    lastLayer[0] = x, lastLayer[1] = y, lastLayer[2] = w, lastLayer[3] = h;
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > spec_.w || y + h > spec_.h) outOfBounds++;
    inLayer = layerSupported;
    return layerSupported;
  }
  void endLayer() override {
    if (inLayer) layerEnds++;
    inLayer = false;
  }
  void releaseLayer() override { layerReleases++; }

  bool drew(const std::string& needle) const {
    for (const auto& t : texts) {
      if (t.find(needle) != std::string::npos) return true;
    }
    return false;
  }
  // Font of the first drawn text containing `needle` (Font::Count if not drawn).
  ui::Font fontOf(const std::string& needle) const {
    for (size_t i = 0; i < texts.size(); i++) {
      if (texts[i].find(needle) != std::string::npos) return fonts[i];
    }
    return ui::Font::Count;
  }
  // Background colour under the first drawn text containing `needle` (-1 if not drawn).
  int bgOf(const std::string& needle) const {
    for (size_t i = 0; i < texts.size(); i++) {
      if (texts[i].find(needle) != std::string::npos) return textBgs[i];
    }
    return -1;
  }
  // Colour of the topmost filled rectangle covering (x, y); -1 if nothing was filled there.
  int colorAt(int x, int y) const {
    for (size_t i = fills.size(); i-- > 0;) {
      const Fill& f = fills[i];
      if (x >= f.x && x < f.x + f.w && y >= f.y && y < f.y + f.h) return f.c;
    }
    return -1;
  }
  void clearLog() {
    texts.clear();
    fonts.clear();
    textBgs.clear();
    cut.clear();
    arcs.clear();
    calls = 0;
    layerCalls = 0;
    panelFills = 0;
    boxTexts = 0;
    layerBegins = layerEnds = layerReleases = 0;
  }

  ui::ScreenSpec spec_;
  std::vector<std::string> texts;
  std::vector<ui::Font> fonts;
  std::vector<int> textBgs;  // colour under each text (what a transparent font shows around it)
  std::vector<bool> cut;     // each text was wider than its box (the real canvas ends it with "...")
  std::vector<int> arcs;
  int calls = 0;
  int outOfBounds = 0;
  bool layerSupported = true;
  bool inLayer = false;
  int layerCalls = 0;
  int panelFills = 0;  // fillRect/fillRoundRect straight on the panel (outside a layer): a visible clear
  int boxTexts = 0;    // textBox() calls (in-place value updates)
  int layerBegins = 0, layerEnds = 0, layerReleases = 0;
  int lastLayer[4] = {0, 0, 0, 0};
  // Band check: while bandArmed, every primitive must stay inside [bandMinX, bandMaxX] x
  // [bandTop, bandBottom]; each one that does not counts in bandOut.
  bool bandArmed = false;
  int bandMinX = 0, bandMaxX = 0, bandTop = 0, bandBottom = 0;
  int bandOut = 0;

 private:
  struct Fill {
    int x, y, w, h;
    uint16_t c;
  };
  std::vector<Fill> fills;
  void paint(int x, int y, int w, int h, uint16_t c) {
    if (!inLayer) panelFills++;
    fill(x, y, w, h, c);
  }
  void fill(int x, int y, int w, int h, uint16_t c) {
    box(x, y, w, h);
    if (x <= 0 && y <= 0 && x + w >= spec_.w && y + h >= spec_.h) fills.clear();  // full clear: older fills are hidden
    fills.push_back({x, y, w, h, c});
  }
  void box(int x, int y, int w, int h) {
    calls++;
    if (inLayer) layerCalls++;
    if (x < 0 || y < 0 || x + w > spec_.w || y + h > spec_.h) outOfBounds++;
    if (bandArmed && (x < bandMinX || y < bandTop || x + w > bandMaxX || y + h > bandBottom)) bandOut++;
  }
};
