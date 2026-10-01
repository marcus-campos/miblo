// ui_visit_c.cpp — visits between Miblos: ship it, sprint, origami, nostalgia, kernel panic,
// picnic, fishing, umbrella, tin can phone, kite (and their props, kinds 128..159).
#include "miblo_rom.h"
#include "ui_visit_kit.h"

namespace screens {

namespace {

// Props of this file (VisitItem::kind). Anchors: see drawVisitItemC.
constexpr uint8_t kBoat = kItemsC;  // paper boat
constexpr uint8_t kFlag = 129;      // checkered finish flag
constexpr uint8_t kSheet = 130;     // a sheet of paper being folded
constexpr uint8_t kWindow = 131;    // an old, dusty Q&A page in a browser window
constexpr uint8_t kDust = 132;      // a cloud of dust
constexpr uint8_t kSparkle = 133;   // a four-point sparkle
constexpr uint8_t kSiren = 134;     // a red alarm light
constexpr uint8_t kCloth = 135;     // checkered picnic cloth
constexpr uint8_t kSandwich = 136;  // a sandwich, then the halves being eaten
constexpr uint8_t kPond = 137;      // a little pond
constexpr uint8_t kBobber = 138;    // a fishing float
constexpr uint8_t kLine = 139;      // a string, rod, pole, umbrella stick or stream between two points
constexpr uint8_t kRain = 140;      // rain streaks over a rectangle
constexpr uint8_t kUmbrella = 141;  // an open umbrella's canopy
constexpr uint8_t kFurled = 142;    // a closed umbrella, held by its handle
constexpr uint8_t kCan = 143;       // a tin can (of the tin can phone)
constexpr uint8_t kWaves = 144;     // sound waves "))" travelling along the string
constexpr uint8_t kKite = 145;      // a kite with its tail
constexpr uint8_t kCatch = 146;     // a fish hanging from the line

// kLine styles (VisitItem::f).
enum : uint8_t { kString, kRod, kPole, kSagging, kStream, kStickHookRight, kStickHookLeft };

// Colours of the props, by index (the shape tables below hold indices). The old window's greys
// are the UI's own: a layer holds only 16 colours.
enum : uint8_t {
  cWhite, cPaper, cWater, cDeep, cRain, cWood, cString, cDim, cFaint, cMuted, cText, cCheck, cRed, cSirenOff,
  cGrey, cCloth, cBread, cLettuce, cOrange, cTin, cTinRim, cCanopyDark, cAmber, cViolet, cPupil
};
static const uint16_t kColors[] MIBLO_ROM = {
    color::WHITE, 0xCE79 /* #cccccc folded paper */, 0x5DBC /* #5ab4e6 water */, 0x2B53 /* #2a6a9a */,
    0x8E5E /* #8cc8f0 rain */, 0x8AC6 /* #8a5a34 wood */, 0xD69A /* #d0d0d0 string */, color::DIM, color::FAINT,
    color::MUTED, color::TEXT, 0x6C4B /* #6a8a5a faded green check */, color::RED, 0x6104 /* #602020 */,
    0x8410 /* #808080 */, 0xD208 /* #d04040 */, 0xE58E /* #e0b070 bread */, 0x5DCB /* #5cb85c lettuce */,
    0xFC00 /* #ff8000 */, 0xBDF8 /* #b8bcc4 tin */, 0x8452 /* #808890 */, 0xA145 /* #a02828 umbrella ribs */,
    color::AMBER, color::VIOLET, color::PUPIL};

uint16_t col(uint8_t i) {
  uint16_t c;
  mibloRomCopy(&c, &kColors[i], sizeof(c));
  return c;
}

// A prop as a list of shapes in design units around its anchor (scaled by Sz), kept in flash.
// R: rect (a, b, w = c, h = d); O: rounded (+ radius e); D: disc (a, b, r = c); T: triangle; L: line.
enum : uint8_t { R, O, D, T, L };
struct Shape {
  uint8_t op, col;
  int8_t a, b, c, d, e, g;
};

#define SHAPES(name) static const Shape name[] MIBLO_ROM
#define COUNT(arr) (uint8_t)(sizeof(arr) / sizeof(arr[0]))

SHAPES(kBoatShape) = {{T, cWhite, -15, -2, 15, -2, 9, 5},   {T, cWhite, -15, -2, -9, 5, 9, 5},
                      {T, cWhite, 0, -17, -10, -2, 0, -2},  {T, cPaper, 0, -17, 0, -2, 10, -2}};
SHAPES(kRipples) = {{R, cWhite, -15, 6, 6, 1, 0, 0}, {R, cWhite, 5, 6, 6, 1, 0, 0}};
SHAPES(kSheetFlat) = {{R, cWhite, -9, -6, 18, 12, 0, 0}, {L, cPaper, -9, -6, 0, 5, 0, 0}, {L, cPaper, 9, -6, 0, 5, 0, 0}};
SHAPES(kSheetHouse) = {{R, cWhite, -9, -1, 18, 7, 0, 0}, {T, cWhite, -9, -1, 9, -1, 0, -8}, {T, cPaper, -9, -1, 0, -8, 0, -1}};
SHAPES(kSheetDart) = {{T, cWhite, -12, 0, 12, -5, 12, 0}, {T, cPaper, -12, 0, 12, 0, 12, 5}};
// The page (anchor: centre of its top edge, 56 x 30): title bar and its three dots, the votes (up,
// count, down), a big "?" and the question's lines.
SHAPES(kPage) = {{R, cDim, -28, 0, 56, 30, 0, 0},     {R, cFaint, -27, 1, 54, 5, 0, 0},
                 {D, cMuted, -24, 3, 1, 0, 0, 0},     {D, cMuted, -20, 3, 1, 0, 0, 0},
                 {D, cMuted, -16, 3, 1, 0, 0, 0},     {R, cMuted, -27, 6, 54, 23, 0, 0},
                 {T, cFaint, -21, 9, -24, 13, -18, 13}, {R, cFaint, -23, 15, 4, 1, 0, 0},
                 {T, cFaint, -24, 17, -18, 17, -21, 21}, {D, cFaint, -8, 13, 5, 0, 0, 0},
                 {D, cMuted, -8, 13, 3, 0, 0, 0},     {R, cMuted, -13, 13, 5, 6, 0, 0},
                 {R, cFaint, -9, 16, 2, 4, 0, 0},     {R, cFaint, -9, 23, 2, 2, 0, 0},
                 {R, cDim, 1, 11, 20, 2, 0, 0},       {R, cDim, 1, 16, 15, 2, 0, 0},
                 {R, cDim, 1, 21, 10, 2, 0, 0}};
SHAPES(kCobweb) = {{L, cWhite, 27, 6, 12, 6, 0, 0},  {L, cWhite, 27, 6, 15, 13, 0, 0}, {L, cWhite, 27, 6, 21, 18, 0, 0},
                   {L, cWhite, 27, 6, 27, 21, 0, 0}, {L, cWhite, 22, 6, 23, 8, 0, 0},  {L, cWhite, 23, 8, 25, 10, 0, 0},
                   {L, cWhite, 25, 10, 27, 11, 0, 0}, {L, cWhite, 17, 6, 19, 11, 0, 0}, {L, cWhite, 19, 11, 23, 14, 0, 0},
                   {L, cWhite, 23, 14, 27, 16, 0, 0}};
SHAPES(kSpecks) = {{R, cText, -24, 9, 2, 1, 0, 0}, {R, cText, -15, 16, 2, 1, 0, 0}, {R, cText, -6, 27, 2, 1, 0, 0},
                   {R, cText, 3, 8, 2, 1, 0, 0},   {R, cText, 11, 24, 2, 1, 0, 0},  {R, cText, 20, 20, 2, 1, 0, 0},
                   {R, cText, -20, 23, 2, 1, 0, 0}, {R, cText, 7, 15, 2, 1, 0, 0},  {R, cText, -9, 9, 2, 1, 0, 0},
                   {R, cText, 16, 27, 2, 1, 0, 0}};
SHAPES(kRays) = {{R, cRed, -20, -11, 7, 2, 0, 0}, {R, cRed, 13, -11, 7, 2, 0, 0}, {R, cRed, -1, -24, 2, 5, 0, 0},
                 {T, cRed, -16, -22, -13, -23, -10, -18}, {T, cRed, 16, -22, 13, -23, 10, -18}};
// A sandwich (bread, tomato, lettuce, bread) whole, then a half, then a bitten half; then crumbs.
SHAPES(kSandwiches) = {
    {O, cBread, -10, 2, 20, 4, 1, 0}, {R, cRed, -9, 0, 18, 2, 0, 0}, {R, cLettuce, -11, -2, 22, 2, 0, 0}, {O, cBread, -10, -7, 20, 5, 2, 0},
    {O, cBread, -6, 2, 12, 4, 1, 0},  {R, cRed, -5, 0, 10, 2, 0, 0}, {R, cLettuce, -7, -2, 14, 2, 0, 0},  {O, cBread, -6, -7, 12, 5, 2, 0},
    {O, cBread, -4, 2, 8, 4, 1, 0},   {R, cRed, -3, 0, 6, 2, 0, 0},  {R, cLettuce, -5, -2, 10, 2, 0, 0},  {O, cBread, -4, -7, 8, 5, 2, 0},
    {R, cBread, -5, 3, 2, 2, 0, 0},   {R, cBread, 1, 4, 2, 1, 0, 0}, {R, cBread, 5, 2, 1, 1, 0, 0}};
SHAPES(kPondShape) = {{O, cWater, -22, -4, 44, 8, 4, 0}, {O, cDeep, -15, -2, 30, 4, 2, 0}};
SHAPES(kFloat) = {{D, cWhite, 0, -3, 3, 0, 0, 0}, {R, cRed, -3, -6, 6, 3, 0, 0}};
SHAPES(kFurledShape) = {{R, cWood, -1, -30, 2, 30, 0, 0}, {T, cRed, 0, -34, -4, -10, 4, -10}, {R, cCanopyDark, -3, -19, 6, 2, 0, 0}};
SHAPES(kCanShape) = {{R, cTin, -4, -5, 8, 10, 0, 0}, {R, cTinRim, -4, -5, 8, 1, 0, 0}, {R, cTinRim, -4, 0, 8, 1, 0, 0},
                     {R, cTinRim, -4, 4, 8, 1, 0, 0}};
SHAPES(kKiteShape) = {{T, cRed, 0, -11, -8, -2, 0, 9}, {T, cAmber, 0, -11, 8, -2, 0, 9}, {L, cWood, 0, -11, 0, 9, 0, 0},
                      {L, cWood, -8, -2, 8, -2, 0, 0}};
SHAPES(kBow) = {{T, cViolet, 0, 0, -3, -2, -3, 2}, {T, cViolet, 0, 0, 3, -2, 3, 2}};
SHAPES(kFishBody) = {{O, cOrange, -4, 0, 8, 13, 4, 0}, {D, cPupil, -2, 4, 1, 0, 0, 0}};
SHAPES(kFishTail) = {{T, cOrange, 0, 11, -5, 17, 5, 17}};

// Sz, but never 0 for a size (at 170 px wide, Sz(1) is 0).
int sz(int v) {
  const int p = Sz(v);
  return v > 0 && p < 1 ? 1 : p;
}

// A one-pixel line (scaled) made of small rectangles: the canvas's wideLine is anti-aliased off a
// layer but a quad on one, and a quad one pixel wide vanishes (or fills its box) on the layers.
void thin(int x0, int y0, int x1, int y1, uint16_t c) {
  const int u = sz(1);
  const int adx = x1 > x0 ? x1 - x0 : x0 - x1, ady = y1 > y0 ? y1 - y0 : y0 - y1;
  const int n = (adx < ady ? adx : ady) + 1;  // runs: one per step of the shorter axis
  for (int i = 0; i < n; i++) {
    const int xa = x0 + (x1 - x0) * i / n, xb = x0 + (x1 - x0) * (i + 1) / n;
    const int ya = y0 + (y1 - y0) * i / n, yb = y0 + (y1 - y0) * (i + 1) / n;
    const int w = xa < xb ? xb - xa : xa - xb, h = ya < yb ? yb - ya : ya - yb;
    C().fillRect(xa < xb ? xa : xb, ya < yb ? ya : yb, w > u ? w : u, h > u ? h : u, c);
  }
}

void draw(const Shape* list, uint8_t n, int x, int y) {
  for (uint8_t i = 0; i < n; i++) {
    Shape p;
    mibloRomCopy(&p, &list[i], sizeof(p));
    const uint16_t c = col(p.col);
    const int ax = x + Sz(p.a), ay = y + Sz(p.b);
    switch (p.op) {
      case R: C().fillRect(ax, ay, sz(p.c), sz(p.d), c); break;
      case O: C().fillRoundRect(ax, ay, sz(p.c), sz(p.d), Sz(p.e), c); break;
      case D: C().fillCircle(ax, ay, sz(p.c), c); break;
      case T: C().fillTriangle(ax, ay, x + Sz(p.c), y + Sz(p.d), x + Sz(p.e), y + Sz(p.g), c); break;
      default: thin(ax, ay, x + Sz(p.c), y + Sz(p.d), c); break;
    }
  }
}
#define DRAW(arr, x, y) draw(arr, COUNT(arr), x, y)

// Offsets that shrink with the cats (groups): `v` on the scale of a pair (half = Sz(40)).
int P(const VisitStage& s, int v) { return s.half * v / 40; }

// Keeps a prop `m` px wide each side of x inside the screen's margins.
int clampX(int x, int m) {
  const int lo = X(3) + m, hi = X(237) - m;
  return x < lo ? lo : x > hi ? hi : x;
}

// A cat at catX looking at (x, y), still (no hop).
MascotLook at(const VisitStage& s, int catX, int x, int y, Eyes e, uint16_t extras = 0) {
  const int dx = x - catX, near = s.half / 4;
  const int8_t gx = (int8_t)(dx > near ? 3 : dx < -near ? -3 : 0);
  const int8_t gy = (int8_t)(y < s.cy - s.half / 3 ? -3 : y > s.cy + s.half / 3 ? 3 : 0);
  return MascotLook{0, 0, gx, gy, e, Paws::Down, extras};
}

// Both looking at (x, y), still.
void look(VisitFrame& f, const VisitStage& s, int x, int y, Eyes e = Eyes::Open, uint16_t extras = 0) {
  f.them = at(s, s.guestX, x, y, e, extras);
  f.me = at(s, s.hostX, x, y, e, extras);
}

// Still, eyes on each other (the default gaze), with these eyes and extras.
void still(VisitFrame& f, Eyes e, uint16_t extras = 0) {
  f.them.dy = f.me.dy = 0;
  f.them.eyes = f.me.eyes = e;
  f.them.extras = f.me.extras = extras;
}

Paws guestReach(const VisitStage& s) { return s.toHost > 0 ? Paws::ReachRight : Paws::ReachLeft; }
Paws hostReach(const VisitStage& s) { return s.toHost > 0 ? Paws::ReachLeft : Paws::ReachRight; }

// Where the guest's paw reaching towards the host is (holding a rod, a string, an umbrella).
int pawX(const VisitStage& s) { return s.guestX + s.toHost * P(s, 32); }
int pawY(const VisitStage& s) { return s.cy + P(s, 24); }

// A line from (x0, y0) to (x1, y1): both ends packed in the item (screens are under 1024 px).
void addLine(VisitFrame& f, int x0, int y0, int x1, int y1, uint8_t style) {
  addItem(f, kLine, x0 + 1024 * x1, y0 + 1024 * y1, style);
}

// Cheering at the end: a heart each (they hop, as they came in).
void cheer(VisitFrame& f, bool pawsUp) {
  f.me.extras |= kHeart;
  f.them.extras |= kHeart;
  if (pawsUp) f.me.paws = f.them.paws = Paws::Up;
}

int isqrt(int v) {
  int r = 0;
  while ((r + 1) * (r + 1) <= v) r++;
  return r;
}

}  // namespace

bool visitActivityC(miblo::Gift g, const VisitStage& s, VisitFrame& f) {
  const uint32_t t = s.t;
  const int toHost = s.toHost;
  switch (g) {
    case miblo::Gift::ShipIt: {  // a paper boat: the guest pushes it over to the host; both wave
      const int y = s.bottom - Sz(10);
      const int from = s.guestX + toHost * P(s, 12), to = s.hostX - toHost * P(s, 12);
      const int lo = from < to ? from : to, hi = from < to ? to : from;
      addLine(f, clampX(lo - Sz(18), 0), s.bottom - Sz(4), clampX(hi + Sz(18), 0), s.bottom - Sz(4), kStream);
      const int bx = t < 3000 ? from : lerpTo(from, to, t - 3000, 6000);
      if (t < 9000) {  // pushed off, it drifts across
        look(f, s, bx, y, t < 6000 ? Eyes::Open : Eyes::Wide);
        if (t >= 1800 && t < 4200) f.them.paws = guestReach(s);
      } else if (t < 12000) {  // the host catches it
        look(f, s, bx, y, Eyes::Happy);
        f.me.paws = hostReach(s);
      } else {  // both wave
        const bool up = (t / 300) % 2;
        f.me.paws = up ? Paws::Up : Paws::Down;
        f.them.paws = up ? Paws::Down : Paws::Up;
        if (t >= 15000) cheer(f, false);
      }
      addItem(f, kBoat, bx, y - ((t / 400) % 2 ? Sz(1) : 0), (uint8_t)(t / 250));
      return true;
    }
    case miblo::Gift::Sprint: {  // a finish flag; running in place, side by side; a tie
      const int flagY = s.top + Sz(4);
      addLine(f, s.mid, flagY, s.mid, s.bottom - Sz(2), kPole);
      addItem(f, kFlag, s.mid, flagY, (uint8_t)(t / (t < 13000 ? 400 : 150)));
      if (t < 2500) {  // on your marks: crouched, eyes on each other
        still(f, Eyes::Open, kGrumpy);
        f.them.dy = f.me.dy = 3;
      } else if (t < 13000) {  // go! running in place
        const bool step = (t / 150) % 2;
        still(f, Eyes::Open, t >= 8000 ? kSweat : 0);
        f.them.dy = (int8_t)(step ? -5 : 0);
        f.me.dy = (int8_t)(step ? 0 : -5);
        f.them.paws = step ? Paws::TapLeft : Paws::TapRight;
        f.me.paws = step ? Paws::TapRight : Paws::TapLeft;
        if (t < 3000) addItem(f, vprop::Burst, s.mid, flagY + Sz(8));
        else  // dust behind their feet
          addItem(f, vprop::Puff, clampX(step ? s.guestX - toHost * P(s, 30) : s.hostX + toHost * P(s, 30), Sz(6)),
                  s.bottom - Sz(5), 1);
      } else if (t < 15000) {  // the finish: both lean in at once
        still(f, Eyes::Wide);
        f.them.dx = f.them.gx;
        f.me.dx = f.me.gx;
        f.them.paws = guestReach(s);
        f.me.paws = hostReach(s);
        if (t < 14000) addItem(f, vprop::Burst, s.mid, flagY + Sz(8));
      } else {  // a tie: both win
        cheer(f, true);
      }
      return true;
    }
    case miblo::Gift::Origami: {  // a sheet folded in turns into a paper plane that flies off
      const int y = s.bottom - Sz(8);
      const int dir = s.mid < X(120) ? 1 : -1;  // the plane heads for the open side of the screen
      if (t < 9000) {  // folding, one fold each
        look(f, s, s.mid, y);
        if (t >= 2000) {
          if (((t - 2000) / 1200) % 2 == 0) {
            f.them.paws = guestReach(s);
          } else {
            f.me.paws = hostReach(s);
            f.them.extras = kTongue;  // concentrating
          }
        }
        addItem(f, kSheet, s.mid, y, (uint8_t)(t < 4500 ? 0 : t < 7000 ? 1 : 2));
      } else if (t < 10500) {  // it's a plane!
        look(f, s, s.mid, y, Eyes::Wide);
        addItem(f, vprop::Plane, s.mid + dir * Sz(9), y, dir > 0 ? 0 : 1);
        if (t < 9600) addItem(f, vprop::Burst, s.mid, y - Sz(10));
      } else {  // up it goes, then loops around over their heads
        const int hi = s.top + Sz(9), span = P(s, 24);
        int px, py, heading = dir;
        if (t < 14000) {
          px = lerpTo(s.mid + dir * Sz(9), s.mid + dir * span, t - 10500, 3500);
          py = lerpTo(y, hi, t - 10500, 3500);
        } else {
          const uint32_t p = t - 14000;
          px = s.mid + dir * span - dir * shuttle(p, 3000, 2 * span);
          py = hi + shuttle(p, 1500, Sz(4));
          if (p % 3000 < 1500) heading = -dir;
        }
        px = clampX(px, Sz(18));
        if (t < 15500) look(f, s, px, py, Eyes::Wide);
        else cheer(f, t >= 16500);
        addItem(f, vprop::Plane, px, py, heading > 0 ? 0 : 1);
      }
      return true;
    }
    case miblo::Gift::Nostalgia: {  // an old dusty Q&A page; blown off, laughed at; a laptop sparkles
      const int wy = s.top + Sz(6), ly = s.bottom - Sz(4);
      addItem(f, kWindow, s.mid, wy, (uint8_t)(t < 4500 ? 3 : 2));  // dusty (bit 0), a cobweb (bit 1)
      if (t < 2500) {  // there it is, untouched for years
        look(f, s, s.mid, wy, t < 1200 ? Eyes::Open : Eyes::Wide);
      } else if (t < 6500) {  // the guest blows the dust off; both cough
        const bool blowing = t < 4500;
        look(f, s, s.mid, wy, blowing ? Eyes::Open : Eyes::Closed);
        if (blowing) {
          f.them.eyes = Eyes::Closed;
          f.them.extras = kMouthO;
        } else {
          f.me.dx = f.them.dx = (int8_t)((t / 120) % 2 ? 1 : -1);
        }
        if (t >= 3000) addItem(f, kDust, s.mid, wy + Sz(22), (uint8_t)((t - 3000) / 700));
      } else if (t < 10500) {  // they look at each other and laugh
        if ((t / 350) % 2) f.me.extras = f.them.extras = kMouthWide;
      } else {  // a sparkle on a laptop: both turn to it, leaving the old page behind
        addItem(f, vprop::Laptop, s.mid, ly, (uint8_t)(t / 300));
        addItem(f, kSparkle, s.mid + Sz(12), ly - Sz(20), (uint8_t)(t / 250));
        if (t < 15500) {
          look(f, s, s.mid, ly, t < 11500 ? Eyes::Wide : Eyes::Open);
          if (t >= 11500) {
            if ((t / 250) % 2) f.them.paws = guestReach(s);
            else f.me.paws = hostReach(s);
          }
        } else {
          cheer(f, false);
        }
      }
      return true;
    }
    case miblo::Gift::Panic: {  // kernel panic: an alarm flashing, both shaking; it stops; phew
      const int sy = s.top + Sz(25);
      const bool on = t >= 1500 && t < 11000;
      addItem(f, kSiren, s.mid, sy, (uint8_t)(on ? 1 + (t / 250) % 2 : 0));
      if (t < 1500) {
        // all calm (the default happy looks)
      } else if (t < 13000) {  // the alarm; then silence
        look(f, s, s.mid, sy, Eyes::Wide, (uint16_t)((on ? kFluffed | kAlarm : 0) | (t >= 6000 ? kSweat : 0)));
        if (on) {
          const int8_t shake = (int8_t)((t / 70) % 2 ? 2 : -2);
          f.them.dx = shake;
          f.me.dx = (int8_t)-shake;
          if ((t / 1500) % 3 == 2) f.them.paws = f.me.paws = Paws::Cover;
        }
      } else if (t < 15500) {  // a sigh of relief
        still(f, Eyes::Closed, kSweat);
        f.them.gx = f.me.gx = 0;
        f.them.dy = f.me.dy = 2;
        const uint8_t puff = (uint8_t)((t - 13000) / 500);
        for (int k = 0; k < 2; k++)
          addItem(f, vprop::Puff, clampX(k ? s.hostX + toHost * P(s, 22) : s.guestX - toHost * P(s, 22), Sz(8)),
                  s.cy + P(s, 14), puff > 3 ? 3 : puff);
      } else {
        cheer(f, false);
      }
      return true;
    }
    case miblo::Gift::Picnic: {  // a checkered cloth and a sandwich: half each, eaten happily
      addItem(f, kCloth, s.mid, s.bottom);
      const int sy = s.bottom - Sz(14);
      if (t < 4000) {  // a sandwich! one half each
        look(f, s, s.mid, sy, t < 1000 ? Eyes::Open : Eyes::Wide, t < 2500 ? kTongue : 0);
        if (t >= 2500) {
          if (t < 3200) f.them.paws = guestReach(s);
          else f.me.paws = hostReach(s);
        }
        addItem(f, kSandwich, s.mid, sy, 0);
      } else if (t < 12000) {  // munching, a half each at the mouth
        const uint8_t left = (uint8_t)(t < 6500 ? 1 : t < 9500 ? 2 : 3);
        still(f, Eyes::Happy);
        f.them.paws = f.me.paws = Paws::Lick;
        ((t / 300) % 2 ? f.them : f.me).extras = kMouthO;  // chewing in turns
        addItem(f, kSandwich, s.guestX - P(s, 8), s.cy + P(s, 20), left);
        addItem(f, kSandwich, s.hostX - P(s, 8), s.cy + P(s, 20), left);
      } else {  // full and happy, crumbs on the cloth
        addItem(f, kSandwich, s.mid, sy + Sz(4), 3);
        f.me.extras = f.them.extras = kTongue;
        if (t >= 15000) cheer(f, false);
      }
      return true;
    }
    case miblo::Gift::Fishing: {  // a rod over a pond: a bite, pulled in together, a fish!
      // The rod leans from the guest's paw out over the pond (towards the host); the float hangs
      // under its tip.
      const int py = s.bottom - Sz(5), hx = pawX(s), hy = pawY(s);
      const int floatX = s.mid + toHost * P(s, 6);
      int tipX = floatX, tipY = s.cy - P(s, 16);
      addItem(f, kPond, s.mid, py, (uint8_t)(t / 400));
      if (t < 12500) {
        const bool bite = t >= 8000, pull = t >= 10000;
        if (pull) {  // the rod comes up and back, the line taut
          tipX = s.mid - toHost * P(s, 2);
          tipY = s.cy - P(s, 24);
        }
        addLine(f, hx, hy, tipX, tipY, kRod);
        addLine(f, tipX, tipY, floatX, py - (bite ? Sz(1) : Sz(5)), kString);
        addItem(f, kBobber, floatX, py - (!bite && (t / 700) % 2 ? Sz(1) : 0), (uint8_t)(bite ? 1 + (t / 200) % 2 : 0));
        if (!bite) {  // waiting...
          look(f, s, floatX, py);
          if ((t / 2000) % 3 == 2) f.them.eyes = Eyes::Sleepy;
          if ((t / 2600) % 3 == 2) f.me.eyes = Eyes::Sleepy;
        } else {  // a bite!
          look(f, s, floatX, py, Eyes::Wide, kMouthO);
          if (pull) {  // pulling together: leaning back, straining
            f.me.paws = hostReach(s);
            f.them.dx = (int8_t)(-3 * toHost);
            f.me.dx = (int8_t)(3 * toHost);
            f.them.extras = f.me.extras = kSweat;
            f.them.eyes = f.me.eyes = (t / 400) % 2 ? Eyes::Closed : Eyes::Wide;
          }
        }
      } else {  // out it comes, wriggling on the line
        tipX = s.mid;
        tipY = s.cy - P(s, 30);
        addLine(f, hx, hy, tipX, tipY, kRod);
        addLine(f, tipX, tipY, tipX, tipY + Sz(10), kString);
        addItem(f, kCatch, tipX, tipY + Sz(10), (uint8_t)(t / 180));
        if (t < 15000) {
          look(f, s, tipX, tipY, Eyes::Wide, kMouthO);
        } else {
          cheer(f, false);
          f.me.paws = Paws::Up;
        }
      }
      f.them.paws = guestReach(s);  // holding the rod throughout
      return true;
    }
    case miblo::Gift::Umbrella: {  // rain; the guest opens an umbrella over both; dry and happy
      const int left = X(4), right = X(236), rainY = s.top + 1024 * s.bottom;
      const uint8_t drops = (uint8_t)(t / 90);
      int w = (s.hostX > s.guestX ? s.hostX - s.guestX : s.guestX - s.hostX) / 2 + P(s, 34);
      if (w > s.mid - left) w = s.mid - left;
      if (w > right - s.mid) w = right - s.mid;
      if (t < 1500) {
        // a nice day... for now
      } else if (t < 6500) {
        addItem(f, kRain, left + 1024 * right, rainY, drops);
        if (t < 3000) {  // the first drops
          f.them = at(s, s.guestX, s.guestX, s.top, Eyes::Wide);
          f.me = at(s, s.hostX, s.hostX, s.top, Eyes::Wide);
        } else if (t < 5000) {  // soaked
          still(f, Eyes::Sleepy, kGrumpy | kSweat);
          f.them.gx = f.me.gx = 0;
          f.them.gy = f.me.gy = 2;
          f.them.dx = (int8_t)((t / 90) % 2 ? 1 : -1);
          f.me.dx = (int8_t)-f.them.dx;
        } else {  // the guest has an umbrella!
          still(f, Eyes::Wide, kSweat);
          f.them.eyes = Eyes::Open;
          f.them.paws = guestReach(s);
          addItem(f, kFurled, pawX(s), pawY(s));
        }
      } else {  // under the umbrella, the rain falls beside them
        const int apex = s.top + Sz(4);
        if (s.mid - w - Sz(3) > left) addItem(f, kRain, left + 1024 * (s.mid - w - Sz(3)), rainY, drops);
        if (s.mid + w + Sz(3) < right) addItem(f, kRain, s.mid + w + Sz(3) + 1024 * right, rainY, drops);
        addLine(f, s.mid, apex + Sz(18), s.mid, pawY(s) - Sz(2), toHost > 0 ? kStickHookLeft : kStickHookRight);
        addItem(f, kUmbrella, s.mid, apex, (uint8_t)(w / 2 > 255 ? 255 : w / 2));
        if (t < 7500) look(f, s, s.mid, apex, Eyes::Wide);
        if (t >= 12000) f.me.extras |= kHeart;
        if (t >= 15000) f.them.extras |= kHeart;
        f.them.paws = guestReach(s);
      }
      return true;
    }
    case miblo::Gift::CanPhone: {  // a tin can each and a string: one talks, the other listens and laughs
      // The guest talks first, then the host; each laughs at what it heard. The talker holds its can
      // to the mouth, the listener to the side of its head.
      const bool guestTalks = t < 9000;
      const int talkerX = guestTalks ? s.guestX : s.hostX, listenerX = guestTalks ? s.hostX : s.guestX;
      const int ax = talkerX, ay = s.cy + P(s, 22);
      const int bx = listenerX + (talkerX > listenerX ? P(s, 30) : -P(s, 30)), by = s.cy;
      addLine(f, ax, ay, bx, by, kSagging);
      addItem(f, kCan, ax, ay);
      addItem(f, kCan, bx, by);
      MascotLook& talker = guestTalks ? f.them : f.me;
      MascotLook& listener = guestTalks ? f.me : f.them;
      const uint32_t p = guestTalks ? t : t - 9000;  // 0..9000 of this turn
      if (p < 6500) {
        talker = at(s, talkerX, listenerX, s.cy, (p / 900) % 3 == 2 ? Eyes::Happy : Eyes::Open);
        listener = at(s, listenerX, talkerX, s.cy, p < 1500 ? Eyes::Open : Eyes::Wide);
        if (p >= 1500) {  // talking: waves go along the string
          const int32_t q = (int32_t)(p % 1000);
          addItem(f, kWaves, ax + (bx - ax) * q / 1000, ay + (by - ay) * q / 1000 - Sz(7), bx > ax ? 0 : 1);
        }
      } else if ((p / 300) % 2) {  // the listener bursts out laughing
        listener.extras = kMouthWide;
      }
      talker.paws = Paws::Lick;
      if (t >= 16000) cheer(f, false);
      return true;
    }
    case miblo::Gift::Kite: {  // a kite goes up on a string, sways high between them; both look up
      const int hx = pawX(s), hy = pawY(s), skyY = s.top + Sz(12), sway = P(s, 18);
      const int lowX = clampX(hx, Sz(10)), lowY = s.cy - Sz(4);
      int kx = lowX, ky = lowY;  // held up, ready
      if (t >= 6000) {  // swaying in the wind
        kx = clampX(s.mid - sway + shuttle(t - 6000, 3000, 2 * sway), Sz(10));
        ky = skyY + shuttle(t, 2200, Sz(3));
      } else if (t >= 2500) {  // up it goes
        kx = lerpTo(lowX, s.mid, t - 2500, 3500);
        ky = lerpTo(lowY, skyY, t - 2500, 3500);
      }
      addLine(f, kx, ky + Sz(9), hx, hy, kString);
      addItem(f, kKite, kx, ky, (uint8_t)(t / 200));
      if (t < 15000) look(f, s, kx, ky, t >= 6000 && (t / 2000) % 2 ? Eyes::Happy : Eyes::Open);
      else cheer(f, false);
      if (t >= 6000) f.them.gy = f.me.gy = -3;
      f.them.paws = guestReach(s);
      return true;
    }
    default:
      return false;
  }
}

void drawVisitItemC(const VisitItem& it) {
  ui::Canvas& c = C();
  const int x = it.x, y = it.y, u = sz(1);
  switch (it.kind) {
    case kBoat:  // (centre of the hull's top edge) a paper boat; f moves the ripples under it
      DRAW(kBoatShape, x, y);
      DRAW(kRipples, x + (int)(it.f % 3) * Sz(2), y);
      break;
    case kFlag:  // (top of the pole) a checkered flag flying to the right; f flutters it
      for (int i = 0; i < 3; i++) {
        const int fx = x + u + i * Sz(6), fy = y + ((i + it.f) % 2 ? u : 0);
        c.fillRect(fx, fy, Sz(6), Sz(12), color::WHITE);
        c.fillRect(fx, fy + (i % 2 ? Sz(6) : 0), Sz(6), Sz(6), color::PUPIL);
      }
      c.fillCircle(x, y, Sz(2), color::AMBER);
      break;
    case kSheet:  // (centre) a sheet of paper; f 0 flat, 1 corners folded, 2 folded into a dart
      if (it.f == 0) DRAW(kSheetFlat, x, y);
      else if (it.f == 1) DRAW(kSheetHouse, x, y);
      else DRAW(kSheetDart, x, y);
      break;
    case kWindow:  // (centre of its top edge) an old Q&A page, all grey; f bit 0: dusty, bit 1: cobweb
      DRAW(kPage, x, y);
      check(x - Sz(21), y + Sz(25), Sz(8), col(cCheck));  // the accepted answer, faded
      if (it.f & 2) DRAW(kCobweb, x, y);
      if (it.f & 1) DRAW(kSpecks, x, y);
      break;
    case kDust: {  // (centre) dust puffs rising and spreading with f (0..4)
      const int k = it.f > 4 ? 4 : it.f;
      for (int i = 0; i < 4; i++) {
        const int dx = (i < 2 ? -14 + 9 * i : -5 + 10 * i) * (4 + k) / 4;
        c.fillCircle(x + Sz(dx), y - Sz(i % 2 ? 3 : 1) - k * Sz(3), Sz(3 + i % 2) + k * u / 2, color::DIM);
      }
      break;
    }
    case kSparkle: {  // (centre) a four-point sparkle pulsing with f
      const int ph = it.f % 4;
      const int r = Sz(6 + 2 * (ph < 2 ? ph : 4 - ph)), q = r / 4 > u ? r / 4 : u;
      c.fillTriangle(x, y - r, x - q, y, x + q, y, color::AMBER);
      c.fillTriangle(x, y + r, x - q, y, x + q, y, color::AMBER);
      c.fillTriangle(x - r, y, x, y - q, x, y + q, color::AMBER);
      c.fillTriangle(x + r, y, x, y - q, x, y + q, color::AMBER);
      c.fillCircle(x, y, q, color::WHITE);
      break;
    }
    case kSiren:  // (bottom centre of its base) an alarm light; f 0 off, 1 on, 2 on with rays
      c.fillRoundRect(x - Sz(8), y - Sz(17), Sz(16), Sz(15), Sz(6), col(it.f ? cRed : cSirenOff));
      c.fillRect(x - Sz(4), y - Sz(14), sz(2), Sz(5), col(it.f ? cWhite : cGrey));
      c.fillRect(x - Sz(11), y - Sz(4), Sz(22), Sz(4), col(cGrey));
      if (it.f == 2) DRAW(kRays, x, y);
      break;
    case kCloth: {  // (centre of its bottom edge) a red and white checkered cloth
      const int cw = Sz(5), ch = Sz(4), l = x - 8 * cw;
      c.fillRect(l, y - 2 * ch, 16 * cw, 2 * ch, color::WHITE);
      for (int i = 0; i < 16; i++) c.fillRect(l + i * cw, y - (i % 2 ? 1 : 2) * ch, cw, ch, col(cCloth));
      break;
    }
    case kSandwich:  // (centre) f 0 a whole sandwich, 1 a half, 2 a half bitten, 3 crumbs
      draw(kSandwiches + 4 * (it.f > 3 ? 3 : it.f), it.f >= 3 ? 3 : 4, x, y);
      break;
    case kPond:  // (centre) a little pond; f moves the glint
      DRAW(kPondShape, x, y);
      c.fillRect(x - Sz(14) + (int)(it.f % 3) * Sz(5), y - Sz(3), Sz(6), u, color::WHITE);
      break;
    case kBobber:  // (on the water line) a float; f 0 floating, 1-2 pulled under with a splash
      if (it.f == 0) {
        DRAW(kFloat, x, y);
      } else {
        c.fillCircle(x, y - u, sz(2), color::RED);
        const int sp = it.f == 1 ? Sz(5) : Sz(7);
        drawPropKind(vprop::Drop, x - sp, y - Sz(2), 0);
        drawPropKind(vprop::Drop, x + sp, y - Sz(2), 0);
      }
      break;
    case kLine: {  // x = x0 + 1024 * x1, y = y0 + 1024 * y1; f: the style
      const int x0 = x % 1024, x1 = x / 1024, y0 = y % 1024, y1 = y / 1024;
      const int thick = Sz(2) > 2 ? Sz(2) : 2;
      if (it.f == kString) {
        thin(x0, y0, x1, y1, col(cString));
      } else if (it.f == kSagging) {  // dipping a little in the middle
        const int mx = (x0 + x1) / 2, my = (y0 > y1 ? y0 : y1) + Sz(4);
        thin(x0, y0, mx, my, col(cString));
        thin(mx, my, x1, y1, col(cString));
      } else if (it.f == kStream) {  // a strip of water from x0 to x1, its top at y0
        c.fillRect(x0 < x1 ? x0 : x1, y0, x0 < x1 ? x1 - x0 : x0 - x1, Sz(3), col(cWater));
      } else {  // a rod, a pole, or an umbrella stick with its hook at (x1, y1)
        c.wideLine(x0, y0, x1, y1, thick, col(it.f == kPole ? cMuted : cWood), color::BG);
        if (it.f >= kStickHookRight) {
          const int d = it.f == kStickHookRight ? 1 : -1;
          c.fillRect(d > 0 ? x1 : x1 - Sz(6), y1, Sz(6) + u, thick, col(cWood));
          c.fillRect(x1 + d * Sz(6) - (d > 0 ? 0 : thick - u), y1 - Sz(3), thick, Sz(3), col(cWood));
        }
      }
      break;
    }
    case kRain: {  // x = left + 1024 * right, y = top + 1024 * bottom: streaks falling with f
      const int l = x % 1024, r = x / 1024, top = y % 1024, bot = y / 1024;
      const int gap = Sz(12) > 4 ? Sz(12) : 4, period = Sz(18) > 6 ? Sz(18) : 6, len = Sz(5) > 2 ? Sz(5) : 2;
      for (int cx = (l / gap + 1) * gap; cx < r; cx += gap) {
        const int off = (it.f * Sz(5) + (cx / gap) * Sz(7)) % period;
        for (int sy = top - period + off; sy < bot; sy += period) {
          const int a = sy < top ? top : sy, b = sy + len > bot ? bot : sy + len;
          if (b > a) c.fillRect(cx, a, u, b - a, col(cRain));
        }
      }
      break;
    }
    case kUmbrella: {  // (top of the canopy) a red canopy f * 2 px wide each side, scalloped edge
      const int w = it.f * 2, h = Sz(18), step = u + u;
      c.fillRect(x - u, y - Sz(4), sz(2), Sz(4), col(cCanopyDark));
      for (int r = 0; r < h; r += step) {  // half an ellipse, row by row
        const int half = w * isqrt(h * h - (h - r) * (h - r)) / h;
        c.fillRect(x - half, y + r, 2 * half, step, color::RED);
      }
      const int n = w / Sz(8) > 1 ? w / Sz(8) : 1;
      for (int i = 0; i <= 2 * n; i++) c.fillCircle(x - w + i * w / n, y + h, sz(3), color::RED);
      for (int i = -1; i <= 1; i++) thin(x, y, x + i * w / 2, y + h, col(cCanopyDark));
      break;
    }
    case kFurled:  // (the handle) a closed umbrella, pointing up
      DRAW(kFurledShape, x, y);
      break;
    case kCan:  // (centre) a tin can
      DRAW(kCanShape, x, y);
      break;
    case kWaves: {  // (centre) "))" going right (f 0) or left (f 1), two pixels thick
      const int d = it.f ? -1 : 1;
      for (int i = 0; i < 4; i++) {
        const int wx = x + d * ((i / 2) * Sz(4) - Sz(2)) + (i % 2) * u, r = Sz(3 + 2 * (i / 2));
        thin(wx, y - r, wx + d * Sz(2), y, color::TEXT);
        thin(wx + d * Sz(2), y, wx, y + r, color::TEXT);
      }
      break;
    }
    case kKite:  // (centre of the cross) a diamond kite, its tail swinging with f
      DRAW(kKiteShape, x, y);
      for (int i = 0; i < 3; i++) DRAW(kBow, x + ((it.f + i) % 2 ? Sz(2) : -Sz(2)), y + Sz(13 + 5 * i));
      thin(x, y + Sz(9), x + (it.f % 2 ? Sz(2) : -Sz(2)), y + Sz(23), col(cString));
      break;
    case kCatch:  // (its mouth, at the end of the line) an orange fish, wriggling with f
      DRAW(kFishBody, x, y);
      DRAW(kFishTail, x + (it.f % 2 ? u : -u), y);
      break;
    default: break;
  }
}

}  // namespace screens
