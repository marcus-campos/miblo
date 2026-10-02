#pragma once
#include <stddef.h>
#include <stdint.h>

#include "miblo_cues.h"
#include "miblo_desknotes.h"
#include "miblo_focus.h"
#include "miblo_friends.h"
#include "miblo_i18n.h"
#include "miblo_overview.h"
#include "miblo_snapshot.h"
#include "miblo_wellness.h"
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
// true if the region was drawn since the last screen switch (reset()).
bool drawn(uint8_t id);
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
enum class Eyes : uint8_t { Open, Closed, Wide, Sleepy, Happy, Dizzy };  // Happy: "^ ^"; Dizzy: rings
// Cover: paws over the eyes; Up: stretching; Lick: one paw at the mouth; TapLeft/TapRight: one
// paw lifted (typing, playing keys).
enum class Paws : uint8_t { Down, ReachLeft, ReachRight, Cover, Up, Lick, TapLeft, TapRight };
// MascotLook::extras. kCoffee: a cup in hand (a friend's visit); kHeart: a heart beside the ear;
// kMouthWide: a yawn or a sneeze; kTongue: the tip of the tongue; kGrumpy: frowning lids;
// kCrossEyed: pupils to the middle; kFluffed: fur standing up; kStars: dizzy stars over the head.
enum : uint16_t {
  kSweat = 1, kAlarm = 2, kZ1 = 4, kZ2 = 8, kMouthO = 16, kCoffee = 32, kHeart = 64,
  kMouthWide = 128, kTongue = 256, kGrumpy = 512, kCrossEyed = 1024, kFluffed = 2048, kStars = 4096
};
struct MascotLook {
  int8_t dx;   // whole cat sideways (shiver)
  int8_t dy;   // whole cat up/down (hop < 0)
  int8_t gx;   // pupils: gaze offset
  int8_t gy;
  Eyes eyes;
  Paws paws;
  uint16_t extras;  // the k* flags above
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
uint8_t mascotStyle();
// A hat for special days (miblo::Accessory: 0 none, 1 Santa, 2 witch, 3 party), on every mascot
// drawn from then on.
void setMascotAccessory(uint8_t accessory);
uint8_t mascotAccessory();
// The current mascot colour's skin (props drawn in the cat's colour, like its tail).
uint16_t mascotSkin();
// Desk mascot with front paws: flat primitives only, inside the square
// (cx - Sz(half), cy - Sz(half), 2 * Sz(half)), background included (half on the 240 grid).
// `table`: the table edge under the paws (left out when the mascot moves around the screen).
// `box` false: no background square, so the cat can sit over something drawn first (the pet mode's
// sign, with the paws on its edge); the caller then clears what the cat leaves behind.
void deskMascot(int cx, int cy, const MascotLook& look, int half = 64, bool table = true, bool box = true);
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
constexpr uint32_t kFlashPhaseMs = 375;  // half of miblo::kBlinkMs: config flashBlinks x 750 ms
// Both in ui_alerts.cpp. `level`: insistence (miblo_alerts.h); `anonymous`: meeting mode, no
// session names, tools or commands.
void flash(Lang lang, miblo::AlertKind kind, const char* name, uint32_t elapsedMs, uint8_t level = 0,
           bool anonymous = false);
// `idx` < 0: the alerted session is not in `s` (another paired computer's snapshot): what the
// hero already drew stays as it is; drawn first like this, it shows `name` (the name the alert
// started with, miblo::AlertSequencer::alertName()), anonymous if "".
void hero(Lang lang, const miblo::Snapshot& s, int idx, miblo::AlertKind kind, bool discreet, const Clock& clk,
          const miblo::RunTracker& runs, bool anonymous = false, const char* name = "");
// `exhaustAt` (miblo::etaFor): when the 5h window runs out at the current pace (0 = no forecast).
void overview(Lang lang, const miblo::Snapshot& s, miblo::Pager& pager, uint32_t nowMs, const Clock& clk,
              bool discreet, uint32_t exhaustAt = 0);
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
// how long ago), so nothing stays on the same pixels. `ms`: time in the mode. `note`: a line
// that replaces the last task for a while ("Hi, Nina!", "Napping with Nina"). `lookMs`: the
// expression's clock when it must differ from `ms` (napping in step with a friend).
// `computerAway`: the computer is not connected; a small crossed-out laptop sits in the sign's
// top-right corner.
void roam(Lang lang, const miblo::Snapshot& s, const Clock& clk, uint32_t ms, DeskMood mood,
          const char* note = nullptr, uint32_t lookMs = UINT32_MAX, bool computerAway = false);
// Where roam() draws the crossed-out laptop for a pet centred at (cx, cy): its box.
void roamAwayIcon(int cx, int cy, int& x, int& y, int& w, int& h);
#ifdef PIO_UNIT_TESTING
void drawPropForTest(uint8_t kind, int x, int y, uint8_t f, int x2);  // tests: one prop, by PropKind value
// Tests: called with true just before a visit prop is drawn and with false right after.
extern void (*visitItemHookForTest)(bool drawing);
#endif
// A visit between two Miblos in pet mode (miblo_friends.h). Visitor: our mascot walks off to the
// right, the screen says who it is visiting, and it walks back in. Host: the friend's mascot (in
// its own colours) walks in from the left, they play, and it leaves. The limits stay at the bottom.
// `side` (config friendsSide): where the other Miblos stand, 0 right, 1 left, 2 above, 3 below; our
// cat leaves that way and a guest comes in from it.
void visit(Lang lang, const miblo::Snapshot& s, const Clock& clk, const miblo::VisitView& v, uint8_t side = 0);
// A greeting ("Hi! I'm" / "Tofu", "Good morning" / "Ana", "Happy birthday" / "Ana"): the mascot
// cheering, `line1` small (may be empty) over `line2` big. `party`: confetti.
void hello(const char* line1, const char* line2, bool party, uint32_t ms);
// Where the pet is at `ms` (centre of its box: cat + card), bouncing inside the screen.
void roamPosition(uint32_t ms, int& cx, int& cy);
// Pet mode antics, now and then while it is calm. With the sign in its paws: batting at it,
// spilling a coffee on it, chasing the mouse cursor across it, a nap on it, a sneeze, peekaboo
// behind it, a heart, sunglasses, a wave. Away from the sign (put down on the floor
// first, picked up again after): a laptop and a bug, its tail, a stretch, licking a paw, a fly, a
// ball of yarn, a mug pushed off the edge, a cardboard box, a little keyboard, a laser dot, soap
// bubbles, a fish snack, a rubber duck, a coffee, a butterfly, a balloon, a paper plane, a fish
// bowl, a deploy button and its rocket, a cucumber, a blanket.
enum class RoamAntic : uint8_t {
  None, Bat, Spill, Cursor, Nap, Sneeze, Peek, Heart, Glasses, Wave,
  Laptop, Tail, Stretch, Lick, Fly, Yarn, Mug, Box, Keys,
  Laser, Bubbles, Fish, Duck, Coffee, Butterfly, Balloon, Plane, Bowl, Deploy, Cucumber, Blanket
};
constexpr uint8_t kAnticCount = 30;
constexpr uint32_t kAnticEveryMs = 30000;  // one antic at the start of every 30 s (from the second)
constexpr uint32_t kAnticMs = 9000;        // with the sign in its paws, or playing away from it
constexpr uint32_t kAnticPutMs = 1500;     // putting the sign down on the floor (or picking it up)
constexpr uint32_t kAnticFloorMs = kAnticMs + 2 * kAnticPutMs;
// Played holding the sign (else the sign goes down on the floor first).
bool anticOnSign(RoamAntic a);
uint32_t anticLength(RoamAntic a);
// The antic playing at `ms` into pet mode, and how far into its cycle (*atMs).
RoamAntic roamAntic(uint32_t ms, uint32_t* atMs);
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

// ---- Daily life ----
// Focus (ui_focus.cpp): the cat with headphones, a progress ring, the time left big, "focus until
// 15:30" and the rounds as dots; Break/LongBreak: stretching, "Break time" and its countdown;
// Back: "Back to focus?". `leftMs`/`lenMs`: the phase; `untilEpoch`: when it ends (0 = unknown).
void focus(Lang lang, const Clock& clk, miblo::FocusPhase phase, uint8_t round, uint8_t rounds, uint32_t leftMs,
           uint32_t lenMs, uint32_t untilEpoch, uint32_t ms);
// Wellness nudges and the day/week summaries (ui_dayrhythm.cpp).
void nudge(Lang lang, miblo::Nudge kind, uint32_t ms);
void dayEnd(Lang lang, const miblo::Snapshot& s, const char* owner, uint32_t ms);
void weekRecap(Lang lang, const miblo::Snapshot& s, uint32_t ms);
// Alerts (ui_alerts.cpp; flash() and hero() are declared with the main screens above).
void fanfare(Lang lang, const char* name, uint32_t durSec, uint32_t ms);  // name "" in meeting mode
void meetingBadge(Lang lang);  // overlay, every frame while meeting mode is on
// Notes (ui_notes.cpp): the cat holding a text (say / reminder / alarm / "Time's up!"), the
// timer with an hourglass, and find (waving + the settings QR).
void note(Lang lang, miblo::NoteKind kind, const char* text, const Clock& clk, uint32_t ms);
void timer(Lang lang, const Clock& clk, uint32_t leftMs, uint32_t lenMs, uint32_t ms);
void findMe(Lang lang, const char* settingsUrl, uint32_t ms);
// Cues (ui_cues.cpp): the full-screen slow pulse; the status frame overlay (None clears nothing:
// the caller redraws the screen when it goes away).
void cue(miblo::CueKind kind, uint32_t elapsedMs);
void stateFrame(miblo::FrameColor c);
// Friday the 13th (ui_occasions.cpp): a black cat crossing the pet mode screen.
void passerby(Lang lang, const miblo::Snapshot& s, const Clock& clk, uint32_t ms);
// State for every mascot drawn from now on (ui_daily_state.cpp), like setMascotAccessory().
void setMascotTie(bool on);
bool mascotTie();
// Meeting mode: the main screens draw no session name, tool or command (the alerts take their
// own `anonymous` argument). Changing it needs a full redraw (app.cpp sets firstFrame).
void setAnonymous(bool on);
bool anonymous();
void setCatMood(uint8_t mood);  // miblo::CatMood
uint8_t catMood();
void setSecondClock(const char* label, const char* hhmm);  // "" = none
const char* secondClockLabel();
const char* secondClockTime();
void setDeskExtras(const char* countdownLine, const char* qrUrl);  // "" = none
const char* deskCountdown();
const char* deskQrUrl();
// A session waiting for you, over a daily full screen (miblo::waitingMarkOn; ui_daily_state.cpp):
// an amber band across the top with a "!" and the waiting session's name (`name` "" in meeting
// mode: "NEEDS YOU"), "+N" when more wait. Drawn after the screen, every frame; it repaints only
// when it changes, after a screen switch, or when the screen drew over its band (the canvas from
// waitingGuard() notices). Without that canvas it repaints on every call. Daily screens keep
// their own content out of the top Y(30) when they can: it sits under the band.
void waitingMark(Lang lang, const char* name, uint8_t pending);
// Wraps `inner` (app.cpp binds the result) so the waiting mark knows when anything was drawn under
// its band. One wrapper for the whole firmware; calling it again rewraps another canvas.
ui::Canvas& waitingGuard(ui::Canvas& inner);
// Call after the overlays that follow the mark in the same frame (state frame, meeting badge):
// what they drew over the band is not the screen covering it, so it must not trigger a repaint.
void waitingOverlaysDrawn();
// Look extras used by the new screens (MascotLook::extras; drawn by ui_base.cpp).
enum : uint16_t { kHeadphones = 8192, kEyeBags = 16384 };

}  // namespace screens
