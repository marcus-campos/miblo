// Cues for peripheral vision: the full-screen slow pulse and the status frame.
#include "miblo_cues.h"
#include "ui_internal.h"
#include "ui_screens.h"

namespace screens {

using miblo::CueKind;

namespace {

constexpr uint8_t kSteps = 7;  // 8 levels (0..7) per rise and per fall: only a new level is drawn

// a -> b by step / kSteps, per RGB565 channel.
uint16_t mix(uint16_t a, uint16_t b, uint8_t step) {
  const int ra = a >> 11, ga = (a >> 5) & 63, ba = a & 31;
  const int rb = b >> 11, gb = (b >> 5) & 63, bb = b & 31;
  const int r = ra + (rb - ra) * step / kSteps;
  const int g = ga + (gb - ga) * step / kSteps;
  const int bl = ba + (bb - ba) * step / kSteps;
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

uint16_t cueColor(CueKind k) {
  switch (k) {
    case CueKind::FocusEnd:
    case CueKind::BreakEnd:
      return color::GREEN;  // calm green: the end of a focus round or a break
    case CueKind::Timer:
      return color::AMBER;
    default:
      return color::VIOLET;  // alarms and reminders
  }
}

// A clock face (timer, alarm, reminder), within 40 of (cx, cy) on the 240 grid.
void clockIcon(int cx, int cy, uint16_t fg, uint16_t bg) {
  szDisc(cx, cy, 0, 0, 40, fg);
  szDisc(cx, cy, 0, 0, 32, bg);
  C().wideLine(cx, cy, cx, cy - Sz(22), Sz(5), fg, bg);
  C().wideLine(cx, cy, cx + Sz(16), cy, Sz(5), fg, bg);
  szDisc(cx, cy, 0, 0, 5, fg);
}

// A leaf leaning right (the end of focus or of a break), within 36 of (cx, cy) on the 240 grid.
void leafIcon(int cx, int cy, uint16_t fg, uint16_t bg) {
  szDisc(cx, cy, -8, 8, 26, fg);
  szTri(cx, cy, 30, -30, 10, 26, -26, -10, fg);
  C().wideLine(cx - Sz(20), cy + Sz(20), cx - Sz(34), cy + Sz(34), Sz(4), fg, bg);  // stem
  C().wideLine(cx - Sz(20), cy + Sz(20), cx + Sz(18), cy - Sz(18), Sz(3), bg, fg);  // vein
}

}  // namespace

void cue(CueKind kind, uint32_t elapsedMs) {
  if (kind == CueKind::None) return;
  const uint8_t level = miblo::StrongCue::intensity(elapsedMs, miblo::cuePulses(kind));
  const uint8_t step = (uint8_t)(level >> 5);  // 0..7
  if (!dirty(R_BODY, miblo::hashInt(miblo::hashInt(miblo::kHashSeed, (uint32_t)kind), step))) return;

  const uint16_t c = cueColor(kind);
  const uint16_t fill = mix(color::BG, c, step);
  const uint16_t icon = step * 2 > kSteps ? color::BG : c;  // dark on a bright screen, bright on a dark one
  // The screen is painted around the icon's box, and the box is composed off-screen and pushed in
  // one go, so the icon never blinks while the colour changes (8 times per rise or fall).
  const int w = X(240), h = Y(240);
  const int cx = X(120), cy = Y(120);
  const int half = Sz(48);
  const int bx = cx - half, by = cy - half, side = 2 * half;
  C().fillRect(0, 0, w, by, fill);
  C().fillRect(0, by + side, w, h - by - side, fill);
  C().fillRect(0, by, bx, side, fill);
  C().fillRect(bx + side, by, w - bx - side, side, fill);
  // The layer stays allocated while the cue lasts (same size every time); reset() frees it.
  const bool layered = C().beginLayer(bx, by, side, side);
  C().fillRect(bx, by, side, side, fill);
  if (kind == CueKind::FocusEnd || kind == CueKind::BreakEnd) leafIcon(cx, cy, icon, fill);
  else clockIcon(cx, cy, icon, fill);
  if (layered) C().endLayer();
}

void stateFrame(miblo::FrameColor fc) {
  if (fc == miblo::FrameColor::None) return;
  const uint16_t c = fc == miblo::FrameColor::Amber ? color::AMBER : color::GREEN;
  // 2 px wide, 2 px in from the edges: still on the panel with the screen care shift (±2 px).
  const int w = X(240), h = Y(240);
  C().fillRect(2, 2, w - 4, 2, c);
  C().fillRect(2, h - 4, w - 4, 2, c);
  C().fillRect(2, 4, 2, h - 8, c);
  C().fillRect(w - 4, 4, 2, h - 8, c);
}

}  // namespace screens
