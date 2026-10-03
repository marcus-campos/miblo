#pragma once
// The pets (config "pet", miblo::Pet): which animal the mascot is. Internal to miblo_ui.
//
// Every mascot on every screen is drawn by drawMascot() (ui_base.cpp) inside a 96x96-unit box
// centred on its anchor (x and y from -48 to 47 design units, scaled by MascotPen). drawMascot
// draws what all pets share, in this order:
//   1. the box's background (unless the caller asked for none) and the desk's table edge (y 40)
//   2. the pet's head()                       <- the pet
//   3. focus headphones (kHeadphones), the owner's neck item or the meeting tie, the owner's face
//      item, the special day's hat or glasses or hearts, the owner's head item (setMascotOutfit;
//      all placed with the pet's PetAnchors: hats on hatDy, faces on eyeY / eyeDx, the headset on
//      phonesDy / phonesDx, neck items on neckDy)
//   4. (desk mascot only) the pet's front()    <- the pet
//   5. (desk mascot only) the props held or floating by it: coffee cup (kCoffee), heart (kHeart),
//      sweat drop (kSweat), alarm marks (kAlarm), zzz (kZ1, kZ2), dizzy stars (kStars)
//
// A pet is one file, ui_pet_<name>.cpp, that defines its PetDef (kPet<Name>) in ROM. Contract:
//  - Reach no further than the cat: inside the box x, y in [-48, 47] for every look with the pet
//    moved by k.dx in -3..3 (a shiver) and k.dy in -5..1 (a hop), every eye shape, colour and
//    accessory (test_ui_look's test_every_pet_stays_in_its_box). The cat: ears up to y -42, paws
//    out to x +-47, down to y 40 (the table edge), the tie to y 46. Add c.x to every x and c.b to
//    every y that moves with the pet.
//  - Flat shapes only (MascotPen: rect, rrect, circle, tri; canvas wideLine through c.d.g if you
//    must): a frame is redrawn often and composed on small 16-colour layers.
//  - Colours: ONLY from c.mc (PetColors), one per part, each of which the user can set (a slot,
//    miblo::PetSlot) or leave on Auto: skin = the body (default Miblo's peach); line = outlines
//    and creases; earIn = the light secondary detail (inner ears, tongue, inside of a mouth);
//    nose = nose / beak / snout; lid = the lines drawn ON the face (closed eyes, mouth); eye =
//    the iris (pupil: c.mc.pupil); accent = the pet's own extra (antenna tips, patches, claw
//    tips, lights...), whose Auto value is the pet's PetDef::accent (its eye's: PetDef::eye).
//    Besides these only the fixed UI colours for what is not the pet (white of an eye, a glint,
//    color::BG to cut a shape). Check every pet on the four presets (peach, orange, black, grey)
//    and on the black background: the 70-pet-* screenshots.
//  - Eye shape c.mc.eyeShape (miblo::EyeShape): Round (the default), Big (big and shiny),
//    Sleepy (half-lidded): every pet draws all three, each in every Eyes state. petEyes() does
//    all of it for two round eyes at (+-14, 6).
//  - head(c): everything up to the face, every Eyes state (Open, Closed = blink, Wide, Sleepy,
//    Happy "^ ^", Dizzy rings), the gaze (k.gx, k.gy: -3..3, the pupils look there), kCrossEyed,
//    kGrumpy (frowning lids), kEyeBags or the Tired mood (petTired(): faint bags under the eyes),
//    and, with c.desk, kFluffed (bristling). c.detail is false on the small 48 px boot mascot:
//    leave out fine details there.
//    head() alone, eyes Open, c.detail false, is also the brand row's logo (screens::logo, ~22 px,
//    the box 96 * size / 90 px): keep it legible there (the 71-logo-pets-* screenshots) and no
//    lower than y 42 (test_ui_look's test_logo_is_the_current_pet).
//  - front(c), desk mascot only: the mouth states (kMouthO "o", kMouthWide yawn/sneeze, kTongue),
//    and the front paws (or wings, claws, hands) in every Paws pose: Down (resting on the table
//    edge at y 40), ReachLeft/ReachRight (batting at a gauge beside it), Cover (over the eyes),
//    Up (stretching, beside the head), Lick (one raised to the mouth), TapLeft/TapRight (one
//    lifted: typing). petPaws() places them where the cat's go; props in antics and visits are
//    held at those spots.
//  - PetAnchors: where the shared accessories sit on it (see below; the cat's are all 0).
//  - tail(c), pet mode's Tail antic only (optional: none = the cat's curl, a prop of the antic):
//    the pet's own tail, or what it plays with instead, its frame c.wag - 1 (4 a second; it stops
//    at 2 while it is dizzy). Drawn before head() (behind the pet); head() and front() may move
//    their own parts with c.wag (0 everywhere else). It may reach beside the box as far as the
//    cat's tail does: x -64..64, y -48..47, with the antic turning it by k.dx -4..4
//    (test_ui_look's test_every_pet_wags_its_own_tail). Keep it cheap: a few shapes.
//
// To add a pet: the kind in miblo_pet.h (at the end, never renumbered, never 13; kPetIds), its
// file ui_pet_<name>.cpp, its row in kPets (ui_base.cpp), its name (S::WebPet<Name>, miblo_i18n.h)
// in all 9 languages (miblo_strings.cpp, then scripts/pack_strings.py) and in petName()'s table
// (miblo_i18n.cpp); the screenshots (tools/screenshots/shots_pets.cpp) render every isPet() value.
#include <stdint.h>

#include "ui_canvas.h"
#include "ui_screens.h"

namespace screens {

// Mascot drawing helper: design units (a 96x96 box centred on the anchor) scaled by
// u = num / den with round-half-up.
struct MascotPen {
  ui::Canvas& g;
  int cx, cy, num, den;
  int s(int v) const {
    const long n = 2L * v * num + den;  // floor(v * u + 0.5)
    const long d = 2L * den;
    return (int)(n >= 0 ? n / d : -((-n + d - 1) / d));
  }
  int w(int v) const { return s(v) < 1 ? 1 : s(v); }
  void rect(int x, int y, int ww, int h, uint16_t c) { g.fillRect(cx + s(x), cy + s(y), w(ww), w(h), c); }
  void rrect(int x, int y, int ww, int h, int r, uint16_t c) {
    g.fillRoundRect(cx + s(x), cy + s(y), w(ww), w(h), s(r), c);
  }
  void circle(int x, int y, int r, uint16_t c) { g.fillCircle(cx + s(x), cy + s(y), w(r), c); }
  void tri(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) {
    g.fillTriangle(cx + s(x0), cy + s(y0), cx + s(x1), cy + s(y1), cx + s(x2), cy + s(y2), c);
  }
};

// Mascot colours per preset (setMascotStyle): skin, inner ears, outlines/wrinkles, nose, and
// the dark lines drawn on the skin (closed eyes, mouth), which must contrast with it.
struct MascotColors {
  uint16_t skin, earIn, line, nose, lid;
};
// What a pet draws with (RGB565), every slot resolved (Auto or the user's colour).
struct PetColors {
  uint16_t skin, earIn, line, nose, lid;  // as MascotColors
  uint16_t eye, pupil, accent;
  uint16_t bag;      // the Tired look's eye bags (petEyeBag)
  uint8_t eyeShape;  // miblo::EyeShape
};
struct PetDef;
// The current paint (setMascotPaint) for `pet`.
PetColors petColors(const PetDef& pet);
// The colours a custom body colour (0xRRGGBB) gives the Auto slots: integer only.
MascotColors mascotColorsFor(uint32_t rgb);
// Brightness (0..255) of an RGB565 colour.
uint8_t luma565(uint16_t c);

// What a pet's drawing gets.
struct PetCtx {
  MascotPen& d;
  const MascotLook& k;
  PetColors mc;
  int x, b;     // k.dx, k.dy: added to everything that moves with the pet
  bool detail;  // false: the small 48 px mascot (boot), fine details left out
  bool desk;    // the desk mascot (front() is drawn, kFluffed shows)
  uint8_t wag;  // pet mode's Tail antic: its frame + 1 (0: not playing; see PetDef::tail)
};

// Where the shared accessories go on a pet, in design units relative to where they sit on the
// cat (whose head is the rounded box x -32..32, y -20..32, eyes at (+-14, 6), chin at y 29).
struct PetAnchors {
  int8_t hatDy;     // hats, bunny ears, hearts: down by this; up when < 0 (-4 at most: a hop
                    // then takes them no higher than the box top, they sink into it a little)
  int8_t phonesDy;  // headphones: down by this
  int8_t phonesDx;  // headphones' cups: outwards by this (the cat's at x +-30..39; <= 8)
  int8_t neckDy;    // the tie: down by this (the cat's knot at y 30)
  int8_t eyeY;      // glasses: the eyes' centre y (the cat's 6)
  int8_t eyeDx;     // glasses: the eyes at x +-eyeDx (the cat's 14); 0: one eye in the middle
  int8_t darkEyes;  // 1: the eyes sit on something dark (the robot's screen): light glasses rims
  int8_t ownGlasses;  // 1: the pet wears glasses of its own: no glasses, sunglasses or monocle
                      // over them (the owner's face item is skipped; a moustache still shows)
};
#define MIBLO_CAT_ANCHORS {0, 0, 0, 0, 6, 14}
// The cat's PetDef fields after its anchors (for pets not drawn yet): green eyes, no accent.
#define MIBLO_CAT_COLORS ui::color::EYE_GREEN, ui::color::AMBER

struct PetDef {
  void (*head)(const PetCtx& c);
  void (*front)(const PetCtx& c);  // desk mascot only
  PetAnchors at;
  uint16_t eye;     // Auto eye colour (the cat's green)
  uint16_t accent;  // Auto accent colour (the pet's own extra; unused by the cat)
  void (*tail)(const PetCtx& c);  // pet mode's Tail antic (nullptr: the cat's curl), see above
};

// ---- Shared pieces a pet may reuse (the cat's own) ----
// The Tired look (kEyeBags, or the Tired mood): faint bags under the eyes.
bool petTired(const PetCtx& c);
// A bag under an eye centred at (ex, ey) (design units, before c.x / c.b), lower for Wide eyes.
void petEyeBag(const PetCtx& c, int ex, int ey);
// Two round eyes at (+-14, 6) in c.mc.eye / c.mc.pupil, in every eye shape and Eyes state, the
// gaze, kCrossEyed, kGrumpy and the lids (in c.mc.skin, lines in c.mc.lid).
void petEyes(const PetCtx& c);
// A paw at (px, py) (before c.x / c.b), pw x ph, corner r.
typedef void (*PetPawFn)(const PetCtx& c, int px, int py, int pw, int ph, int r);
// The cat's paw: a skin pad with a darker outline and two toe lines.
void petPadPaw(const PetCtx& c, int px, int py, int pw, int ph, int r);
// Both front paws in k.paws' pose, where the cat's go, drawn with `paw`.
void petPaws(const PetCtx& c, PetPawFn paw);
// The Tail antic's swing, -2..2 and back with c.wag (0 when it is not playing), like the cat's.
int petSwing(const PetCtx& c);

// The cat (ui_pet_cat.cpp): the default and the stand-in for a pet not drawn yet.
void catHead(const PetCtx& c);
void catFront(const PetCtx& c);

// One per miblo::Pet (ui_pet_<name>.cpp).
extern const PetDef kPetCat;
extern const PetDef kPetDuck;
extern const PetDef kPetBug;
extern const PetDef kPetDaemon;
extern const PetDef kPetRobot;
extern const PetDef kPetMug;
extern const PetDef kPetPenguin;
extern const PetDef kPetCrab;
extern const PetDef kPetOwl;
extern const PetDef kPetDog;
extern const PetDef kPetAlien;
extern const PetDef kPetRiff;
extern const PetDef kPetDev;
extern const PetDef kPetDino;
extern const PetDef kPetDevChan;

}  // namespace screens
