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
// Screen switch: clears everything, invalidates the region cache and frees any canvas layer.
void reset();
// If the region's hash changed, clears the rectangle (coordinates already scaled) and returns true.
bool region(uint8_t id, uint32_t hash, int x, int y, int w, int h, uint16_t bg = ui::color::BG);
// Like region() but without clearing: for areas that repaint their own background.
bool dirty(uint8_t id, uint32_t hash);
// Like region(), but the redraw is composed off-screen when the canvas has a layer (memory
// permitting) and pushed in one go on end() / at the end of the scope, so the region never
// flashes its background colour; otherwise it is cleared and drawn directly. For whole-region
// changes (paging, state changes). Arcs don't go to layers: keep them out of these regions.
class Compose {
 public:
  Compose() = default;
  Compose(const Compose&) = delete;
  Compose& operator=(const Compose&) = delete;
  ~Compose() { end(); }
  // true if the region changed (and was started: filled with bg, off-screen if possible).
  bool begin(uint8_t id, uint32_t hash, int x, int y, int w, int h, uint16_t bg = ui::color::BG);
  // Pushes the layer (if any) and frees it.
  void end();

 private:
  bool layered_ = false;
};
// A value that changes in place (timer, clock, %, countdown): redrawn only when `s` (or the
// salt, e.g. its region's hash) changes, with Canvas::textBox — never cleared first.
bool field(uint8_t id, uint32_t salt, int x, int y, const char* s, ui::Font f, uint16_t fg, uint16_t bg,
           ui::Align a, int boxW);
// Translated text (4 rotating buffers).
const char* t(Lang lang, miblo::S id);
// Scale from the 240 grid: X for horizontal widths/positions, Y for vertical, Sz for sizes.
int X(int v);
int Y(int v);
int Sz(int v);

// Composite primitives.
void bar(int x, int y, int w, int h, uint8_t pct, uint16_t fg);
void check(int cx, int cy, int size, uint16_t c);
// Miblo mascot: simplified flat Sphynx cat, about ten
// flat primitives, drawn only inside the square (cx - Sz(48), cy - Sz(48), 2 * Sz(48)),
// background included; `small` draws the 48 px variant inside (cx - Sz(24), cy - Sz(24), 2 * Sz(24)).
void mascot(int cx, int cy, uint8_t frame, bool small = false);
// Pose of a frame (0 idle, 1 blink, 2 hop, 3 glance); frames with equal poses draw the same.
uint8_t mascotPose(uint8_t frame);

// Expression of the big Desk mascot (design units of the 96-unit box).
enum class Eyes : uint8_t { Open, Closed, Wide, Sleepy, Happy };  // Happy: "^ ^"
enum class Paws : uint8_t { Down, ReachLeft, ReachRight, Cover };  // Cover: paws over the eyes
enum : uint8_t { kSweat = 1, kAlarm = 2, kZ1 = 4, kZ2 = 8, kMouthO = 16 };  // MascotLook::extras
struct MascotLook {
  int8_t dx;   // whole cat sideways (shiver)
  int8_t dy;   // whole cat up/down (hop < 0)
  int8_t gx;   // pupils: gaze offset
  int8_t gy;
  Eyes eyes;
  Paws paws;
  uint8_t extras;  // kSweat | kAlarm | kZ1 | kZ2 | kMouthO
  bool operator==(const MascotLook& o) const {
    return dx == o.dx && dy == o.dy && gx == o.gx && gy == o.gy && eyes == o.eyes && paws == o.paws &&
           extras == o.extras;
  }
  bool operator!=(const MascotLook& o) const { return !(*this == o); }
};
// Miblo's logo: the mascot's head (a 22x20 silhouette, eyes as holes) centred on (cx, cy), in the
// current mascot colour, scaled by whole pixels to about `size` px wide.
void logo(int cx, int cy, int size);
// Mascot colours (config "mascot"): 0 sphynx (peach), 1 orange, 2 black, 3 grey. Applies to
// every mascot drawn from then on.
void setMascotStyle(uint8_t style);
// Desk mascot with front paws: flat primitives only, inside the square
// (cx - Sz(half), cy - Sz(half), 2 * Sz(half)), background included (half on the 240 grid).
// `table`: the table edge under the paws (left out when the mascot moves around the screen).
void deskMascot(int cx, int cy, const MascotLook& look, int half = 64, bool table = true);
void qr(const char* payload, int x, int y, int scale);

// ---- system screens ----
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

// ---- main screens ----
struct Clock {
  bool valid;      // local time known
  char hhmm[6];    // "14:32" or "--:--"
  uint32_t epoch;  // now, in Unix seconds (0 = unknown)
};

// "16:42" (same day) or "Thu 09:00" (another day), in local time (system TZ).
void formatWhen(Lang lang, uint32_t epoch, uint32_t now, char* out, size_t cap);

// Alert flash: alternates colour/dark every kFlashPhaseMs; each phase repaints the whole
// screen first, then draws the text (transparent) on that phase's colour.
constexpr uint32_t kFlashPhaseMs = 375;  // the default 1.5 s flash = 2 full blinks
void flash(Lang lang, miblo::AlertKind kind, const char* name, uint32_t elapsedMs);
void hero(Lang lang, const miblo::Snapshot& s, int idx, miblo::AlertKind kind, bool discreet, const Clock& clk,
          const miblo::RunTracker& runs);
void overview(Lang lang, const miblo::Snapshot& s, miblo::Pager& pager, uint32_t nowMs, const Clock& clk,
              bool discreet);
// `exhaustAt` (LimitWatch): when the 5h window runs out at the recent pace (0 = not before it resets).
void limits(Lang lang, const miblo::Snapshot& s, const Clock& clk, uint32_t exhaustAt = 0);
void sessions(Lang lang, const miblo::Snapshot& s, miblo::Pager& pager, uint32_t nowMs, const Clock& clk,
              bool discreet);

// ---- Desk (long quiet spell) ----
// How the mascot feels about the last known limits (the higher of 5h and week).
// Searching / Asleep: the Disconnected screen (looking around for the computer, then a nap).
// Celebrate: the "limit freed" screen.
enum class DeskMood : uint8_t { Calm, Watchful, Worried, Scared, Searching, Asleep, Celebrate };
DeskMood deskMood(uint8_t pct);  // < 50 calm, < 80 watchful, < 95 worried, else scared
// Percentage the Desk shows for a window: a window whose reset time has passed is back to 0.
uint8_t deskPct(const miblo::UsageWindow& w, uint32_t nowEpoch);
// Expression at `ms` into the mood's loop; `focusLeft`: the gauge that worries it is the left one (5h).
MascotLook deskLook(DeskMood mood, bool focusLeft, uint32_t ms);
// The mood the Desk shows for the last known limits (Calm when there are none).
DeskMood deskMoodFor(const miblo::Snapshot& s, uint32_t nowEpoch, bool* focusLeft = nullptr);
// Pet mode (long idle, screen left on): the mascot wanders around the whole screen carrying a
// small card (clock, 5h/week limits, the next reset or "limit freed", the last finished task and
// how long ago), so nothing stays on the same pixels. `ms`: time in the mode.
void roam(Lang lang, const miblo::Snapshot& s, const Clock& clk, uint32_t ms, DeskMood mood);
// Where the pet is at `ms` (centre of its box: cat + card), bouncing inside the screen.
void roamPosition(uint32_t ms, int& cx, int& cy);
// Full-screen mascot + two ring gauges (5h, week) + clock.
void desk(Lang lang, const miblo::Snapshot& s, const Clock& clk, uint32_t nowMs, uint32_t exhaustAt = 0);
// The 5h window just reset after real use: green "limit freed" band, the mascot celebrating, the
// new usage and the next reset. `ms`: time since the screen came up (animation).
void limitReset(Lang lang, const miblo::Snapshot& s, const Clock& clk, uint32_t ms);
// Right after boot, when a newer firmware exists: the mascot, "Update available", "v1.0.2 (you have
// v1.0.1)" and the command to run. `frame`: mascot animation frame (like boot()).
void updateAvailable(Lang lang, const char* current, const char* latest, uint8_t frame);
// Today's summary: responses finished, time worked, cost, and the compact limits.
void summary(Lang lang, const miblo::Snapshot& s, const Clock& clk);
// No snapshot for a while: "Disconnected" + clock, the mascot looking for the computer (asleep
// after kAwayNapMs away), "Waiting for the computer", and the address and pairing code.
void disconnected(Lang lang, const Clock& clk, const char* ip, const char* mdnsHost, const char* pairCode,
                  uint32_t nowMs, uint32_t awayMs);

}  // namespace screens
