#pragma once
// Visits between Miblos: what an activity script sees and draws (ui_visit_a/b/c.cpp). Internal to
// miblo_ui.
#include "miblo_friends.h"
#include "ui_screens.h"

namespace screens {

using ui::Align;
using ui::Font;
namespace color = ui::color;

// The canvas everything is drawn on (C().fillRect, fillRoundRect, fillCircle, fillTriangle, text...).
inline ui::Canvas& C() { return canvas(); }

// Item kinds. 1..63 are ui_main.cpp's PropKind values (drawn by drawPropKind; anchors are
// documented in ui_main.cpp's drawProp), checked against the enum there:
namespace vprop {
enum : uint8_t {
  None = 0,
  Duck = 1,        // (centre of the body) rubber duck
  Laptop = 2,      // (centre of the keyboard) tiny laptop; f scrolls the code
  Lgtm = 3,        // (top-left corner) "LGTM" sign
  Bug = 4,         // (centre of the body) a little bug; f moves the legs
  Rocket = 5,      // (tip of the nose) flame when f > 0
  Burst = 6,       // (centre) a spark
  Tail = 7,        // (its base) a curl in the cat's colour, swinging with f
  Fly = 8,         // (body) wings flicker with f
  Yarn = 9,        // (centre of the ball) rolling lines (no thread from here)
  Mug = 10,        // (centre) a blue mug of coffee
  Box = 11,        // (centre of the front) open cardboard box
  Keys = 12,       // (centre) a little piano keyboard
  Note = 13,       // (the note's head) an eighth note
  Puff = 14,       // (centre) a cloud growing with f (0..4)
  Glasses = 15,    // (between the lenses) sunglasses with a glint
  Bubble = 16,     // (centre) a soap bubble
  Fish = 17,       // (centre) a fish snack; f = bites taken (3: only the tail)
  Butterfly = 18,  // (body) wings beating with f
  Balloon = 19,    // (centre) a red balloon on a string
  Plane = 20,      // (nose) a paper plane; f 0: flying right, 1: flying left
  Bowl = 21,       // (centre of the water) a fish bowl; f moves the fish
  Button = 22,     // (centre of the top) a big red button; f 1: pressed
  Cucumber = 23,   // (centre) standing up
  Blanket = 24,    // (centre of its top edge) a striped blanket
  Laser = 25,      // (centre) a laser dot with its glow
  Dots = 26,       // (left dot) "..." while talking; f = how many (1..3)
  Drop = 27,       // (centre) a splash of water
  Count = 28,      // not a kind: how many there are (checked against PropKind::Count)
};
}  // namespace vprop
// Each activity file owns a range of new kinds, drawn by its drawVisitItemX.
constexpr uint8_t kItemsA = 64;    // ui_visit_a.cpp: 64..95
constexpr uint8_t kItemsB = 96;    // ui_visit_b.cpp: 96..127
constexpr uint8_t kItemsC = 128;   // ui_visit_c.cpp: 128..159
constexpr uint8_t kItemsEnd = 160;

// One thing drawn during a visit. Kinds 1..63 are ui_main.cpp's PropKind values (laptop, bug,
// burst, duck, rocket, LGTM, note, puff, mug, balloon, plane, drop, …); 64..95 belong to
// ui_visit_a.cpp, 96..127 to ui_visit_b.cpp, 128..159 to ui_visit_c.cpp.
struct VisitItem {
  uint8_t kind;
  int x, y;
  uint8_t f;  // animation frame / variant
};

// Where everyone is while together, and how far into the stay.
struct VisitStage {
  uint32_t t;      // ms into the stay, 0..miblo::kVisitStayMs
  int hostX;       // the host's centre x
  int guestX;      // the first guest's centre x (the one doing the activity with the host)
  int mid;         // between the two: where shared props go
  int cy;          // the cats' centre y
  int half;        // the cats' half size in px (smaller in groups)
  int toHost;      // +1 when the host is to the right of the first guest, else -1
  int top;         // highest y anything may use (Y(30))
  int bottom;      // lowest y anything may use (cy + half)
};

struct VisitFrame {
  MascotLook me;    // the host
  MascotLook them;  // the first guest (the other guests cheer along, drawn by visit())
  VisitItem items[4];
  uint8_t n;
};

void addItem(VisitFrame& f, uint8_t kind, int x, int y, uint8_t fr = 0);
int shuttle(uint32_t t, uint32_t period, int span);              // 0..span..0 over period
int lerpTo(int a, int b, uint32_t t, uint32_t len);              // a→b over len ms, clamped
void drawPropKind(uint8_t kind, int x, int y, uint8_t f);        // a ui_main.cpp PropKind

// Each file scripts 10 activities: returns false for a gift that is not its own. `f` comes in
// with the default happy looks (hops, eyes on each other) for the script to change.
bool visitActivityA(miblo::Gift g, const VisitStage& s, VisitFrame& f);  // HighFive..Chess (#1-10)
bool visitActivityB(miblo::Gift g, const VisitStage& s, VisitFrame& f);  // Game..NotFound (#11-20)
bool visitActivityC(miblo::Gift g, const VisitStage& s, VisitFrame& f);  // ShipIt..Kite (#21-30)
void drawVisitItemA(const VisitItem& it);  // kinds 64..95
void drawVisitItemB(const VisitItem& it);  // kinds 96..127
void drawVisitItemC(const VisitItem& it);  // kinds 128..159

}  // namespace screens
