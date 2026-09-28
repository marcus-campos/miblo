#pragma once
#include <stddef.h>
#include <stdint.h>

#include "miblo_i18n.h"
#include "miblo_overview.h"
#include "miblo_snapshot.h"
#include "ui_canvas.h"

// Layouts for all screens. Coordinates are designed on a 240x240 grid and scaled by the
// Canvas's ScreenSpec (X/Y/Sz in ui_base.cpp). Each function is called every frame and only
// redraws the regions whose content changed (RegionCache) — no framebuffer.
namespace screens {

using miblo::Lang;

// Binds the layouts to a Canvas (once, at boot).
void bind(ui::Canvas& canvas);
ui::Canvas& canvas();
// Screen switch: clears everything and invalidates the region cache.
void reset();
// If the region's hash changed, clears the rectangle (coordinates already scaled) and returns true.
bool region(uint8_t id, uint32_t hash, int x, int y, int w, int h, uint16_t bg = ui::color::BG);
// Translated text (4 rotating buffers).
const char* t(Lang lang, miblo::S id);
// Scale from the 240 grid: X for horizontal widths/positions, Y for vertical, Sz for sizes.
int X(int v);
int Y(int v);
int Sz(int v);

// Composite primitives.
void bar(int x, int y, int w, int h, uint8_t pct, uint16_t fg);
void check(int cx, int cy, int size, uint16_t c);
void mascot(int cx, int cy, uint8_t frame);  // Miblo mascot placeholder (3 frames)
void qr(const char* payload, int x, int y, int scale);

// ---- system screens (§4.5) ----
void boot(Lang lang, uint8_t frame);
// Why the setup screen is shown again after an attempt: a red title + a hint over the QR.
enum class SetupNote : uint8_t { None, WrongPassword, NotFound, Refused, Failed };
// `code`: disconnect reason shown with SetupNote::Failed (0 = none).
void setup(Lang lang, const char* apSsid, SetupNote note = SetupNote::None, unsigned code = 0);
void welcome(Lang lang, const char* pairCode, const char* ip);
void paired(Lang lang, const char* host, const char* modeName, const char* mdnsHost);
void code(Lang lang, miblo::S title, const char* code, uint32_t remainingSec);
void updating(Lang lang, uint8_t pct);
// Quick-boot hard reset countdown: big amber N + "Quick restarts left to reset: N".
void hardResetCountdown(Lang lang, uint8_t remaining);
void disconnected(Lang lang, bool timeValid, int hour, int minute, int wday, int mday, const char* ip,
                  const char* mdnsHost, const char* pairCode);

// ---- main screens (§4.1–4.4) ----
struct Clock {
  bool valid;      // local time known
  char hhmm[6];    // "14:32" or "--:--"
  uint32_t epoch;  // now, in Unix seconds (0 = unknown)
};

// "16:42" (same day) or "Thu 09:00" (another day), in local time (system TZ).
void formatWhen(Lang lang, uint32_t epoch, uint32_t now, char* out, size_t cap);

void flash(Lang lang, miblo::AlertKind kind, const char* name, uint32_t elapsedMs);
void hero(Lang lang, const miblo::Snapshot& s, int idx, miblo::AlertKind kind, bool discreet, const Clock& clk,
          const miblo::RunTracker& runs);
void overview(Lang lang, const miblo::Snapshot& s, miblo::Pager& pager, uint32_t nowMs, const Clock& clk,
              bool discreet);
void limits(Lang lang, const miblo::Snapshot& s, const Clock& clk);
void sessions(Lang lang, const miblo::Snapshot& s, miblo::Pager& pager, uint32_t nowMs, const Clock& clk,
              bool discreet);

}  // namespace screens
