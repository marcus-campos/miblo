#include <qrcode.h>

#include "miblo_mood.h"
#include "miblo_occasions.h"
#include "miblo_rom.h"
#include "ui_pet.h"
#include "ui_screens.h"

namespace screens {

namespace color = ui::color;

static ui::Canvas* g_canvas = nullptr;
static int16_t g_w = 240;
static int16_t g_h = 240;
static int16_t g_sz = 240;  // what Sz() scales by: the smaller side (less while scaleSz() is on)
static miblo::RegionCache g_cache;

void bind(ui::Canvas& c) {
  g_canvas = &c;
  g_w = c.spec().w;
  g_h = c.spec().h;
  g_sz = g_w < g_h ? g_w : g_h;
  g_cache.invalidate();
}

ui::Canvas& canvas() { return *g_canvas; }

int X(int v) { return v * g_w / 240; }
int Y(int v) { return v * g_h / 240; }
int Sz(int v) { return v * g_sz / 240; }
void scaleSz(int num, int den) { g_sz = (int16_t)((g_w < g_h ? g_w : g_h) * num / den); }

void reset() {
  g_canvas->releaseLayer();
  g_canvas->clear(color::BG);
  g_cache.invalidate();
}

bool dirty(uint8_t id, uint32_t hash) { return g_cache.changed(id, hash); }

bool drawn(uint8_t id) { return g_cache.drawn(id); }

bool region(uint8_t id, uint32_t hash, int x, int y, int w, int h, uint16_t bg) {
  if (!dirty(id, hash)) return false;
  g_canvas->fillRect(x, y, w, h, bg);
  return true;
}

bool Compose::begin(uint8_t id, uint32_t hash, int x, int y, int w, int h, uint16_t bg) {
  end();
  if (!dirty(id, hash)) return false;
  layered_ = g_canvas->beginLayer(x, y, w, h);
  g_canvas->fillRect(x, y, w, h, bg);
  return true;
}

void Compose::end() {
  if (!layered_) return;
  layered_ = false;
  g_canvas->endLayer();
  g_canvas->releaseLayer();  // rows are recomposed rarely: give the memory back right away
}

bool field(uint8_t id, uint32_t salt, int x, int y, const char* s, ui::Font f, uint16_t fg, uint16_t bg,
           ui::Align a, int boxW) {
  if (!dirty(id, miblo::hashStr(salt, s))) return false;
  g_canvas->textBox(x, y, s, f, fg, bg, a, boxW);
  return true;
}

const char* t(Lang lang, miblo::S id) {
  static char buf[4][128];
  static uint8_t next = 0;
  char* b = buf[next];
  next = (uint8_t)((next + 1) % 4);
  miblo::tr(lang, id, b, sizeof(buf[0]));
  return b;
}

void bar(int x, int y, int w, int h, uint8_t pct, uint16_t fg) {
  const int r = h / 2;
  g_canvas->fillRoundRect(x, y, w, h, r, color::TRACK);
  const int fw = (int)((long)w * (pct > 100 ? 100 : pct) / 100);
  if (fw >= h) g_canvas->fillRoundRect(x, y, fw, h, r, fg);
  else if (fw > 0) g_canvas->fillRect(x, y, fw, h, fg);
}

void check(int cx, int cy, int size, uint16_t c) {
  const int w = size / 6 < 2 ? 2 : size / 6;
  g_canvas->wideLine(cx - size / 2, cy, cx - size / 6, cy + size / 3, w, c, color::BG);
  g_canvas->wideLine(cx - size / 6, cy + size / 3, cx + size / 2, cy - size / 3, w, c, color::BG);
}

uint8_t mascotPose(uint8_t frame) {
  // Idle-heavy 8-frame loop (~3.2 s at 400 ms/frame).
  static const uint8_t kSeq[8] MIBLO_ROM = {0, 0, 1, 0, 2, 0, 3, 0};
  return mibloRomByte((const char*)&kSeq[frame % 8]);
}

// Mascot colours per style (MascotColors, ui_pet.h).
const MascotColors kMascotColors[] MIBLO_ROM = {
    {color::SKIN, color::EAR_IN, color::WRINKLE, color::NOSE, color::PUPIL},  // sphynx #f2b8a8
    {0xF282, 0xFD2F, 0xB1C1, 0xFD2F, color::PUPIL},                         // orange #f55110 (the logo's)
    {0x4A4A, 0x7ACC, 0x2946, 0xD3D1, 0xCE5A},                               // black (#4a4a55: shows on the dark bg)
    {0x9D16, 0xCD15, 0x5B2E, 0xB3D1, color::PUPIL},                         // grey #9aa3b0
};
static uint8_t g_accessory = 0;
static MascotOutfit g_outfit;
static MascotPaint g_paint;

// One per miblo::Pet, in its order.
static const PetDef* const kPets[] MIBLO_ROM = {&kPetCat,   &kPetDuck,    &kPetBug,  &kPetDaemon,
                                                &kPetRobot, &kPetMug,     &kPetPenguin, &kPetCrab,
                                                &kPetOwl,   &kPetDog,     &kPetAlien, &kPetRiff,
                                                &kPetDev,   &kPetCat,     &kPetDino,  &kPetDevChan};
// (13 is never a pet: setMascotPet() never lets it through; its row is only a stand-in.)
static_assert(sizeof(kPets) / sizeof(kPets[0]) == miblo::kPetIds, "one drawing per miblo::Pet value");
constexpr uint8_t kStyles = sizeof(kMascotColors) / sizeof(kMascotColors[0]);

void setMascotPet(uint8_t pet) { g_paint.pet = miblo::knownPet(pet); }
uint8_t mascotPet() { return g_paint.pet; }

void setMascotStyle(uint8_t style) { g_paint.style = style < kStyles ? style : 0; }
uint8_t mascotStyle() { return g_paint.style; }

void setMascotPaint(const MascotPaint& p) {
  g_paint = p;
  setMascotStyle(p.style);
  setMascotPet(p.pet);
  if (g_paint.eyeShape >= miblo::kEyeShapes) g_paint.eyeShape = 0;
  for (uint32_t& v : g_paint.slots) {
    if (v > miblo::kPetColorMax) v = miblo::kPetAuto;
  }
}
MascotPaint mascotPaint() { return g_paint; }
uint32_t mascotPaintHash() {
  uint32_t h = miblo::hashInt(miblo::kHashSeed + 31, (uint32_t)g_paint.style | (uint32_t)g_paint.pet << 8 |
                                           (uint32_t)g_paint.eyeShape << 16);
  for (uint32_t v : g_paint.slots) h = miblo::hashInt(h, v);
  // What it wears too: every screen that redraws on a paint change redraws on an outfit change.
  return miblo::hashInt(h, (uint32_t)g_outfit.head | (uint32_t)g_outfit.face << 8 | (uint32_t)g_outfit.neck << 16);
}

void setMascotAccessory(uint8_t accessory) { g_accessory = accessory; }
uint8_t mascotAccessory() { return g_accessory; }

void setMascotOutfit(const MascotOutfit& o) {
  // Only what fits its slot (a stray id, 13 above all, is nothing).
  g_outfit.head = miblo::wearFits(miblo::WearSlot::Head, o.head) ? o.head : 0;
  g_outfit.face = miblo::wearFits(miblo::WearSlot::Face, o.face) ? o.face : 0;
  g_outfit.neck = miblo::wearFits(miblo::WearSlot::Neck, o.neck) ? o.neck : 0;
}
MascotOutfit mascotOutfit() { return g_outfit; }
static uint8_t g_guestAccessory = 0;
void setGuestAccessory(uint8_t accessory) { g_guestAccessory = accessory; }
uint8_t guestAccessory() { return g_guestAccessory; }

void dressGuest(uint8_t style, uint8_t pet, const miblo::FriendLook& look) {
  MascotPaint p;
  p.style = style;
  p.pet = pet;
  p.eyeShape = look.eyes;
  miblo::lookSlots(look, p.slots);
  setMascotPaint(p);
  // The guest's own accessories, today's holiday hat (if any) taking its slot as on our pet.
  const miblo::Outfit o = miblo::outfitWith((miblo::Accessory)g_guestAccessory, look.accHead, look.accFace,
                                            look.accNeck);
  setMascotAccessory((uint8_t)o.occasion);
  setMascotOutfit(MascotOutfit{o.head, o.face, o.neck});
}

MascotPaint strangerPaint(const MascotPaint& own) {
  MascotPaint p;
  p.style = 2;  // black
  p.pet = own.pet;
  return p;
}

// ---- Colours (integer only) ----
static uint16_t rgb565(uint32_t rgb) {
  return (uint16_t)(((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
}
static void channels(uint16_t c, int& r, int& g, int& b) {
  r = (c >> 11) << 3 | (c >> 13);
  g = ((c >> 5) & 63) << 2 | ((c >> 9) & 3);
  b = (c & 31) << 3 | ((c >> 2) & 7);
}
static uint16_t pack(int r, int g, int b) {
  return rgb565((uint32_t)(r < 0 ? 0 : r > 255 ? 255 : r) << 16 | (uint32_t)(g < 0 ? 0 : g > 255 ? 255 : g) << 8 |
                (uint32_t)(b < 0 ? 0 : b > 255 ? 255 : b));
}
uint8_t luma565(uint16_t c) {
  int r, g, b;
  channels(c, r, g, b);
  return (uint8_t)((77 * r + 150 * g + 29 * b) >> 8);
}
// `a` moved towards `b` by num/16.
static uint16_t mix(uint16_t a, uint16_t b, int num) {
  int r0, g0, b0, r1, g1, b1;
  channels(a, r0, g0, b0);
  channels(b, r1, g1, b1);
  return pack(r0 + (r1 - r0) * num / 16, g0 + (g1 - g0) * num / 16, b0 + (b1 - b0) * num / 16);
}
constexpr uint16_t kPink = 0xFC75;     // #ff8ca8: where inner ears and noses lean
constexpr uint16_t kLightLid = 0xCE5A;  // the lines on a dark face (the black preset's)
constexpr uint8_t kDarkSkin = 110;      // a skin darker than this gets light lines

MascotColors mascotColorsFor(uint32_t rgb) {
  MascotColors mc;
  mc.skin = rgb565(rgb);
  const uint8_t y = luma565(mc.skin);
  const bool dark = y < kDarkSkin;
  // Outlines: darker on a light body; lighter on a dark one, so its edge still shows on the BG.
  mc.line = dark ? mix(mc.skin, color::WHITE, 6) : mix(mc.skin, color::BLACK, y > 200 ? 8 : 6);
  mc.earIn = mix(mc.skin, kPink, 8);
  mc.nose = mix(mc.skin, kPink, 11);
  mc.lid = dark ? kLightLid : color::PUPIL;
  return mc;
}

// `c` moved away from `bg` until their brightness differs by 48 or more (an Auto colour that
// would vanish into the body).
static uint16_t apart(uint16_t c, uint16_t bg) {
  const int d = (int)luma565(c) - (int)luma565(bg);
  if (d >= 48 || d <= -48) return c;
  return luma565(bg) >= 128 ? mix(c, color::BLACK, 9) : mix(c, color::WHITE, 9);
}

PetColors petColors(const PetDef& pet) {
  MascotColors base;
  mibloRomCopy(&base, &kMascotColors[g_paint.style], sizeof(base));
  const uint32_t* slot = g_paint.slots;
  if (slot[miblo::kSlotBody] != miblo::kPetAuto) base = mascotColorsFor(slot[miblo::kSlotBody] - 1);
  PetColors pc;
  pc.skin = base.skin;
  pc.earIn = base.earIn;
  pc.line = base.line;
  pc.nose = base.nose;
  pc.lid = base.lid;
  pc.eye = pet.eye;
  pc.pupil = color::PUPIL;
  pc.accent = pet.accent;
  pc.eyeShape = g_paint.eyeShape;
  // The eye bags: subtle but visible on every colour (the presets' own; a custom body's lines).
  pc.bag = slot[miblo::kSlotBody] != miblo::kPetAuto ? base.line
           : g_paint.style == 1                      ? (uint16_t)0x9900
           : g_paint.style == 2                      ? (uint16_t)0x7BCF
                                                     : base.line;
  // Readability: an Auto eye, nose or accent that would vanish into a custom body or another
  // pet's body moves away from it (the cat on the presets stays exactly as it always was).
  if (slot[miblo::kSlotBody] != miblo::kPetAuto || g_paint.pet != 0) {
    if (slot[miblo::kSlotEye] == miblo::kPetAuto) pc.eye = apart(pc.eye, pc.skin);
    if (slot[miblo::kSlotNose] == miblo::kPetAuto) pc.nose = apart(pc.nose, pc.skin);
    if (slot[miblo::kSlotAccent] == miblo::kPetAuto) pc.accent = apart(pc.accent, pc.skin);
  }
  // The user's own picks, as they are.
  uint16_t* const target[miblo::kPetSlots] = {&pc.skin, &pc.line, &pc.earIn, &pc.nose, &pc.lid, &pc.eye, &pc.accent};
  for (uint8_t i = 0; i < miblo::kPetSlots; i++) {
    if (slot[i] != miblo::kPetAuto) *target[i] = rgb565(slot[i] - 1);
  }
  if (slot[miblo::kSlotLine] != miblo::kPetAuto) pc.bag = pc.line;
  return pc;
}

static const PetDef* currentPet() {
  const PetDef* pet;
  mibloRomCopy(&pet, &kPets[g_paint.pet], sizeof(pet));
  return pet;
}
uint16_t mascotSkin() {
  PetDef def;
  mibloRomCopy(&def, currentPet(), sizeof(def));
  return petColors(def).skin;
}

// Nerdy square glasses over the pet's eyes (`g`: their centre y - 6): 2-unit rims, a bridge and
// the temples; one wide lens on a one-eyed pet. `dark`: light rims on a dark face.
static void drawGlasses(MascotPen& d, int x, int g, const PetAnchors& at, bool dark) {
  const uint16_t rim = dark ? color::MUTED : color::PUPIL;
  if (at.eyeDx == 0) {  // one eye: one wide lens, no bridge
    d.rect(-14 + x, -5 + g, 28, 2, rim);
    d.rect(-14 + x, 15 + g, 28, 2, rim);
    d.rect(-16 + x, -3 + g, 2, 18, rim);
    d.rect(14 + x, -3 + g, 2, 18, rim);
    d.rect(-13 + x, -1 + g, 2, 4, color::WHITE);
    d.rect(-32 + x, 1 + g, 16, 2, rim);
    d.rect(16 + x, 1 + g, 16, 2, rim);
    return;
  }
  const int ex = at.eyeDx;
  for (int e = -ex; e <= ex; e += 2 * ex) {
    d.rect(e - 9 + x, -5 + g, 18, 2, rim);
    d.rect(e - 9 + x, 15 + g, 18, 2, rim);
    d.rect(e - 11 + x, -3 + g, 2, 18, rim);
    d.rect(e + 9 + x, -3 + g, 2, 18, rim);
    d.rect(e - 8 + x, -1 + g, 2, 4, color::WHITE);  // a glint on the lens
  }
  d.rect(-ex + 11 + x, 1 + g, 2 * ex - 22, 2, rim);  // the bridge
  d.rect(-32 + x, 1 + g, 21 - ex, 2, rim);  // the temples
  d.rect(ex + 11 + x, 1 + g, 21 - ex, 2, rim);
}

// Hats for special days (miblo::Accessory), on top of the head. They stay inside the 96-unit box
// even when the cat hops (dy >= -5): nothing may be drawn outside it (no trail).
// `phase` moves the Valentine's hearts (it changes with the look).
// `at`: where they sit on the pet (hats down by hatDy, glasses on its eyes).
// `dark`: the face is dark (light glasses).
static void drawHat(MascotPen& d, int x, int b0, uint8_t phase, const PetAnchors& at, bool dark) {
  int b = b0 + at.hatDy;
  if (at.hatDy < 0 && b < -5) b = -5;  // raised on this pet: a hop takes them no higher than the cat's
  switch (g_accessory) {
    case 1:  // Santa hat: red, white brim and pompom, tipped to the right
      d.tri(-15 + x, -18 + b, 15 + x, -18 + b, 11 + x, -40 + b, color::RED);
      d.rrect(-17 + x, -22 + b, 34, 7, 3, color::WHITE);
      d.circle(12 + x, -39 + b, 3, color::WHITE);
      break;
    case 2:  // witch hat: wide brim, pointy purple crown with an amber band
      d.rect(-22 + x, -21 + b, 44, 4, 0x3008);
      d.tri(-12 + x, -19 + b, 12 + x, -19 + b, 4 + x, -43 + b, 0x5011);
      d.rect(-11 + x, -24 + b, 22, 3, color::AMBER);
      break;
    case 3:  // party hat: striped cone with a pompom
      d.tri(-10 + x, -18 + b, 10 + x, -18 + b, x, -38 + b, color::VIOLET);
      d.rect(-7 + x, -24 + b, 14, 2, color::AMBER);
      d.rect(-4 + x, -31 + b, 8, 2, color::GREEN);
      d.circle(x, -39 + b, 3, color::AMBER);
      break;
    case 4:  // bunny ears between the cat's own, leaning out a little: white with pink insides
      for (int e = -1; e <= 1; e += 2) {
        const int o = e < 0 ? -1 : 0;  // mirror a w-unit-wide shape: left edge e * a + o * w
        d.rrect(e * 6 + o * 12 + x, -28 + b, 12, 12, 5, color::WHITE);   // the base, on the head
        d.rrect(e * 8 + o * 12 + x, -43 + b, 12, 20, 6, color::WHITE);   // the tip, leaning out
        d.rrect(e * 10 + o * 6 + x, -39 + b, 6, 14, 3, color::EAR_IN);
        d.rrect(e * 9 + o * 6 + x, -28 + b, 6, 8, 3, color::EAR_IN);
      }
      break;
    case 5:  // nerdy square glasses over the eyes
      if (!at.ownGlasses) drawGlasses(d, x, b0 + at.eyeY - 6, at, dark);
      break;
    case 6: {  // hearts floating around the head; they bob with each change of look
      const int hb = b > 0 ? b : 0;  // they follow the cat down, never up out of the box
      for (int i = 0; i < 3; i++) {  // above the head, by the left cheek, by the right cheek
        const int hx = i == 0 ? 0 : i == 1 ? -41 : 41;
        const int hy = (i == 0 ? -38 : i == 1 ? -2 : 10) + hb - 2 * ((phase + i) % 3);
        const uint16_t c = i == 1 ? color::EAR_IN : color::RED;
        d.circle(hx - 3, hy, 4, c);
        d.circle(hx + 3, hy, 4, c);
        d.tri(hx - 6, hy + 2, hx + 6, hy + 2, hx, hy + 9, c);
      }
      break;
    }
    default: break;
  }
}

// ---- The owner's accessories (setMascotOutfit, miblo::Wear) ----
// Fixed colours that read on the four presets, a custom body and the black background.
constexpr uint16_t kWearRed = 0xB000;    // #b40000: a knot, a darker red
constexpr uint16_t kWearBrown = 0xA285;  // #a05028: the cowboy hat
constexpr uint16_t kWearDark = 0x4A69;   // #4c4c4c: the top hat, the beret (light enough on the BG)

// The head item, sitting on the head like the special days' hats (`b`: their baseline, hatDy and
// the hop clamp applied). Everything between y -43 and -15 (the cat's head top is -20).
static void drawWearHead(MascotPen& d, int x, int b, uint8_t id) {
  switch ((miblo::Wear)id) {
    case miblo::Wear::Cap:  // a baseball cap, its peak to the right
      d.rrect(-15 + x, -32 + b, 30, 16, 8, color::BLUE);
      d.rect(-15 + x, -24 + b, 30, 6, color::BLUE);
      d.rrect(-15 + x, -20 + b, 36, 4, 2, color::FLASH_BLUE);
      d.circle(x, -32 + b, 2, color::FLASH_BLUE);
      break;
    case miblo::Wear::Beanie:  // a knitted beanie: dome, folded cuff, pompom
      d.rrect(-16 + x, -34 + b, 32, 18, 9, color::CORAL);
      d.rrect(-18 + x, -22 + b, 36, 7, 3, 0xA9A6);
      d.circle(x, -36 + b, 4, color::WHITE);
      break;
    case miblo::Wear::Beret:  // a puffy beret slouching to the left, with its little stalk
      d.rrect(-23 + x, -31 + b, 38, 13, 6, color::RED);
      d.rrect(-14 + x, -21 + b, 28, 4, 2, kWearRed);
      d.rect(-4 + x, -34 + b, 2, 4, kWearRed);
      break;
    case miblo::Wear::TopHat:  // brim, tall crown, red band
      d.rrect(-20 + x, -22 + b, 40, 5, 2, kWearDark);
      d.rect(-12 + x, -42 + b, 24, 21, kWearDark);
      d.rect(-12 + x, -27 + b, 24, 4, color::RED);
      d.rect(-9 + x, -39 + b, 2, 10, color::DIM);  // a sheen
      break;
    case miblo::Wear::Crown:  // gold, three points, a ruby
      d.rect(-14 + x, -26 + b, 28, 8, color::AMBER);
      d.tri(-14 + x, -26 + b, -6 + x, -26 + b, -14 + x, -37 + b, color::AMBER);
      d.tri(-6 + x, -26 + b, 6 + x, -26 + b, x, -39 + b, color::AMBER);
      d.tri(6 + x, -26 + b, 14 + x, -26 + b, 14 + x, -37 + b, color::AMBER);
      d.circle(x, -22 + b, 2, color::RED);
      break;
    case miblo::Wear::CowboyHat:  // wide brim curling up at the ends, dented crown, band
      d.rrect(-24 + x, -22 + b, 48, 5, 2, kWearBrown);
      d.tri(-24 + x, -21 + b, -24 + x, -27 + b, -16 + x, -21 + b, kWearBrown);
      d.tri(24 + x, -21 + b, 24 + x, -27 + b, 16 + x, -21 + b, kWearBrown);
      d.rrect(-13 + x, -36 + b, 26, 16, 5, kWearBrown);
      d.rect(-13 + x, -25 + b, 26, 3, 0x6A20);
      break;
    case miblo::Wear::ChefHat:  // three white puffs on a tall band
      d.circle(-8 + x, -33 + b, 7, color::WHITE);
      d.circle(8 + x, -33 + b, 7, color::WHITE);
      d.circle(x, -36 + b, 7, color::WHITE);
      d.rect(-12 + x, -32 + b, 24, 13, color::WHITE);
      d.rect(-12 + x, -22 + b, 24, 2, color::MUTED);
      break;
    case miblo::Wear::Bandana:  // a red bandana tied over the head, white dots, knot on the right
      d.rrect(-22 + x, -27 + b, 44, 12, 6, color::RED);
      d.tri(18 + x, -21 + b, 30 + x, -27 + b, 28 + x, -15 + b, color::RED);
      for (int i = -1; i <= 1; i += 2) d.rect(i * 8 - 1 + x, -23 + b, 2, 2, color::WHITE);
      break;
    case miblo::Wear::FlowerCrown:  // five flowers in a row, a dot in each
      for (int i = -2; i <= 2; i++) {
        d.circle(i * 9 + x, -22 + b + (i == 0 ? -1 : 0), 4, i & 1 ? color::AMBER : color::EAR_IN);
        d.circle(i * 9 + x, -22 + b + (i == 0 ? -1 : 0), 1, color::WHITE);
      }
      d.rect(-20 + x, -19 + b, 40, 2, color::GREEN);
      break;
    case miblo::Wear::Halo:  // a golden ring floating above the head
      d.rect(-10 + x, -40 + b, 20, 2, color::AMBER);
      d.rect(-10 + x, -32 + b, 20, 2, color::AMBER);
      d.rrect(-14 + x, -39 + b, 5, 8, 2, color::AMBER);
      d.rrect(9 + x, -39 + b, 5, 8, 2, color::AMBER);
      break;
    default: break;
  }
}

// The face item, on the pet's eyes (`g`: their centre y - 6, like the special day's glasses).
// The gamer headset is drawn by drawMascot (it sits where headphones go).
static void drawWearFace(MascotPen& d, int x, int g, uint8_t id, const PetAnchors& at, bool dark) {
  const int ex = at.eyeDx;
  switch ((miblo::Wear)id) {
    case miblo::Wear::Sunglasses: {  // dark lenses with a glint, a thin frame
      const uint16_t frame = dark ? color::MUTED : color::PUPIL;
      const int w = ex ? 22 : 30;
      for (int e = -ex; e <= ex; e += ex ? 2 * ex : 1) {
        d.rrect(e - w / 2 + x, -2 + g, w, 15, 5, dark ? color::DIM : 0x10A2);  // grey on a dark face
        d.rect(e - w / 2 + 3 + x, 1 + g, 5, 2, color::DIM);
      }
      if (ex) d.rect(-ex + 11 + x, 2 + g, 2 * ex - 22, 2, frame);
      d.rect(-32 + x, 2 + g, 32 - ex - w / 2, 2, frame);
      d.rect(ex + w / 2 + x, 2 + g, 32 - ex - w / 2, 2, frame);
      break;
    }
    case miblo::Wear::NerdGlasses:  // the square glasses, taped at the bridge
      drawGlasses(d, x, g, at, dark);
      d.rect(-2 + x, -1 + g, 4, 6, color::WHITE);
      break;
    case miblo::Wear::Monocle: {  // a rim round the right eye (wide enough for the big ones), a chain
      const int e = ex + x;
      const uint16_t rim = dark ? color::AMBER : color::PUPIL;  // gold on a dark face
      d.rect(e - 8, -7 + g, 16, 2, rim);
      d.rect(e - 8, 19 + g, 16, 2, rim);
      d.rect(e - 12, -3 + g, 2, 18, rim);
      d.rect(e + 10, -3 + g, 2, 18, rim);
      for (int c = -1; c <= 1; c += 2) {  // the corners, rounding it off
        d.rect(e + c * 9 - 1, -5 + g, 2, 2, rim);
        d.rect(e + c * 9 - 1, 17 + g, 2, 2, rim);
      }
      for (int i = 0; i < 4; i++) d.rect(e + 11 + i, 20 + 3 * i + g, 2, 2, color::AMBER);
      break;
    }
    case miblo::Wear::Moustache: {  // a curled moustache under the nose
      const uint16_t c = dark ? color::MUTED : 0x3186;
      for (int s = -1; s <= 1; s += 2) {
        const int o = s < 0 ? -1 : 0;  // mirror a w-unit-wide piece: left edge s * a + o * w
        d.rrect(s * 1 + o * 13 + x, 19 + g, 13, 7, 3, c);
        d.tri(s * 11 + x, 20 + g, s * 19 + x, 13 + g, s * 15 + x, 24 + g, c);
      }
      break;
    }
    default: break;
  }
}

// The neck item, under the chin (`n`: the tie's baseline, neckDy applied). Down to y 46 at most;
// what shows matters most between the front paws (x -10..10), which are drawn over it.
static void drawWearNeck(MascotPen& d, int x, int n, uint8_t id) {
  switch ((miblo::Wear)id) {
    case miblo::Wear::BowTie:
      d.tri(-2 + x, 33 + n, -13 + x, 27 + n, -13 + x, 39 + n, color::RED);
      d.tri(2 + x, 33 + n, 13 + x, 27 + n, 13 + x, 39 + n, color::RED);
      d.rrect(-3 + x, 30 + n, 6, 6, 2, kWearRed);
      break;
    case miblo::Wear::Scarf:  // green with white stripes, both ends hanging in front
      d.rrect(-24 + x, 27 + n, 48, 7, 3, color::GREEN);
      d.rrect(-8 + x, 30 + n, 8, 16, 2, color::GREEN);
      d.rrect(1 + x, 30 + n, 8, 13, 2, color::GREEN);
      d.rect(-8 + x, 40 + n, 8, 2, color::WHITE);
      d.rect(1 + x, 37 + n, 8, 2, color::WHITE);
      for (int i = -1; i <= 1; i += 2) d.rect(i * 15 - 1 + x, 27 + n, 3, 7, color::WHITE);
      break;
    case miblo::Wear::Neckerchief:  // a triangle of cloth knotted at the front
      d.rect(-18 + x, 28 + n, 36, 4, color::BLUE);
      d.tri(-14 + x, 30 + n, 14 + x, 30 + n, x, 45 + n, color::BLUE);
      d.circle(x, 32 + n, 3, color::FLASH_BLUE);
      break;
    case miblo::Wear::Beads:  // a string of beads hanging in an arc
      for (int i = -4; i <= 4; i++) {
        const uint16_t c = i % 3 == 0 ? color::WHITE : (i & 1) ? color::CORAL : color::AMBER;
        d.circle(i * 4 + x, 43 - i * i * 7 / 16 + n, 2, c);
      }
      break;
    case miblo::Wear::Medal: {  // "shipped to prod": a gold medal with a tick, on a ribbon
      d.tri(-9 + x, 28 + n, -3 + x, 28 + n, 1 + x, 38 + n, color::FLASH_BLUE);
      d.tri(9 + x, 28 + n, 3 + x, 28 + n, -1 + x, 38 + n, color::RED);
      d.circle(x, 40 + n, 6, color::AMBER);
      d.rect(-3 + x, 40 + n, 2, 2, color::WHITE);  // the tick
      d.tri(-1 + x, 42 + n, 3 + x, 37 + n, 3 + x, 39 + n, color::WHITE);
      break;
    }
    default: break;
  }
}

// Meeting mode's tie (setMascotTie): a knot and a diamond blade under the chin, on every mascot.
static void drawTie(MascotPen& d, int x, int b) {
  d.tri(-8 + x, 29 + b, -2 + x, 32 + b, -5 + x, 35 + b, color::WHITE);  // the collar's points
  d.tri(8 + x, 29 + b, 2 + x, 32 + b, 5 + x, 35 + b, color::WHITE);
  d.rect(-3 + x, 30 + b, 6, 4, color::VIOLET);  // the knot
  d.tri(-3 + x, 34 + b, 3 + x, 34 + b, -5 + x, 40 + b, color::VIOLET);  // the blade, widening ...
  d.tri(3 + x, 34 + b, 5 + x, 40 + b, -5 + x, 40 + b, color::VIOLET);
  d.tri(-5 + x, 40 + b, 5 + x, 40 + b, x, 46 + b, color::VIOLET);       // ... to a point
}

// Focus headphones (kHeadphones): a band hugging the top of the head (discs along a circle just
// outside the head's rounded corner) and a cup on each side. Under any hat.
static const int8_t kBand[][2] MIBLO_ROM = {{8, -22}, {13, -22}, {17, -20}, {21, -19},
                                            {25, -16}, {28, -13}, {31, -9}, {32, -5}};
// `wide`: the cups (and the band's ends with them) that much further out. `body`, `cushion`: their
// colours (the gamer headset's are its own).
static void drawHeadphones(MascotPen& d, int x, int b, int wide, uint16_t body = color::DIM,
                           uint16_t cushion = color::FAINT) {
  d.rect(-13 + x, -24 + b, 26, 5, body);
  for (size_t i = 0; i < sizeof(kBand) / sizeof(kBand[0]); i++) {
    int8_t p[2];
    mibloRomCopy(p, kBand[i], sizeof(p));
    const int px = p[0] + wide * (int)i / (int)(sizeof(kBand) / sizeof(kBand[0]) - 1);
    d.circle(-px + x, p[1] + b, 2, body);
    d.circle(px + x, p[1] + b, 2, body);
  }
  for (int s = -1; s <= 1; s += 2) {
    const int o = s < 0 ? -1 : 0;  // mirror a w-unit-wide piece: left edge s * a + o * w
    d.rrect(s * (30 + wide) + o * 9 + x, -7 + b, 9, 19, 4, body);
    d.rect(s * (30 + wide) + o * 2 + x, -4 + b, 2, 13, cushion);  // the cushion against the head
  }
}

// Every mascot: the pet (ui_pet.h) and what all pets share around it. `desk` adds what only the
// big Desk mascot has: a table edge, front paws and the extras (sweat, alarm, zzz, open mouth).
static void drawMascot(MascotPen& d, const MascotLook& k, bool detail, bool desk, bool table = true,
                       bool box = true, uint8_t wag = 0) {
  PetDef def;
  mibloRomCopy(&def, currentPet(), sizeof(def));
  if (!def.tail) wag = 0;  // the cat's tail is the antic's prop
  const PetCtx c{d, k, petColors(def), k.dx, k.dy, detail, desk, wag};
  const int x = k.dx;
  const int b = k.dy;
  if (box) d.rect(-48, -48, 96, 96, color::BG);
  if (desk && table) d.rect(-48, 40, 96, 2, color::DIVIDER);  // table edge (stays put when it hops)
  if (wag) def.tail(c);  // behind the pet
  def.head(c);
  const bool dark = luma565(c.mc.skin) < kDarkSkin || def.at.darkEyes;
  if (k.extras & kHeadphones) drawHeadphones(d, x, b + def.at.phonesDy, def.at.phonesDx);
  else if (g_outfit.face == (uint8_t)miblo::Wear::Headset) {  // focus headphones take its place
    const int hb = b + def.at.phonesDy, s = -30 - def.at.phonesDx;  // the left cup's outer edge
    drawHeadphones(d, x, hb, def.at.phonesDx, color::DIM, color::GREEN);  // lit cushions
    d.rect(s + 4 + x, 11 + hb, 2, 9, color::DIM);  // the mic boom, round to the mouth
    d.rect(s + 4 + x, 19 + hb, 14, 2, color::DIM);
    d.circle(s + 19 + x, 20 + hb, 2, color::GREEN);
  }
  if (mascotTie()) drawTie(d, x, b + def.at.neckDy);  // the meeting's tie over the owner's neck item
  else drawWearNeck(d, x, b + def.at.neckDy, g_outfit.neck);
  // A pet with glasses of its own wears no others (a moustache still goes).
  if (!def.at.ownGlasses || g_outfit.face == (uint8_t)miblo::Wear::Moustache)
    drawWearFace(d, x, b + def.at.eyeY - 6, g_outfit.face, def.at, dark);
  drawHat(d, x, b, (uint8_t)(k.gx + 2 * k.gy + 3 * (int)k.eyes + 4 * (int)k.paws + 64), def.at, dark);
  if (g_outfit.head) {
    int hb = b + def.at.hatDy;
    if (def.at.hatDy < 0 && hb < -5) hb = -5;  // as the special days' hats: never above the box
    drawWearHead(d, x, hb, g_outfit.head);
  }
  if (!desk) return;
  def.front(c);
  if (k.extras & kCoffee) {  // a cup held up next to the right paw, steaming
    d.rect(29 + x, 6 + b, 2, 5, color::MUTED);
    d.rect(34 + x, 4 + b, 2, 6, color::MUTED);
    d.rect(26 + x, 13 + b, 13, 13, color::WHITE);
    d.rect(27 + x, 14 + b, 11, 3, 0x6A20);  // the coffee
    d.circle(41 + x, 19 + b, 3, color::WHITE);
    d.circle(41 + x, 19 + b, 1, color::BG);
  }
  if (k.extras & kHeart) {  // beside the left ear, fixed like the alarm marks
    d.circle(-44, -40, 3, color::RED);
    d.circle(-39, -40, 3, color::RED);
    d.tri(-47, -39, -36, -39, -41, -32, color::RED);
  }
  if (k.extras & kSweat) {
    d.tri(36 + x, -12 + b, 32 + x, -3 + b, 40 + x, -3 + b, color::BLUE);
    d.circle(36 + x, -2 + b, 4, color::BLUE);
  }
  // Marks beside the right ear: fixed, so a shiver or a hop never pushes them out of the box.
  if (k.extras & kAlarm) {
    d.rect(41, -46, 5, 13, color::RED);
    d.rect(41, -30, 5, 5, color::RED);
  }
  if (k.extras & kZ1) {
    d.rect(38, -22, 9, 2, color::MUTED);
    d.tri(44, -20, 47, -20, 38, -16, color::MUTED);
    d.tri(47, -20, 41, -16, 38, -16, color::MUTED);
    d.rect(38, -16, 9, 2, color::MUTED);
  }
  if (k.extras & kZ2) {
    d.rect(40, -34, 6, 2, color::DIM);
    d.tri(43, -32, 46, -32, 40, -29, color::DIM);
    d.tri(46, -32, 43, -29, 40, -29, color::DIM);
    d.rect(40, -29, 6, 2, color::DIM);
  }
  if (k.extras & kStars) {  // dizzy: little stars above the head, fixed like the zzz
    for (int sx = -32; sx <= 26; sx += 29) {
      d.rect(sx, -45, 7, 2, color::AMBER);
      d.rect(sx + 3, -48, 2, 8, color::AMBER);
    }
  }
}

// Miblo's logo: the mascot's head as a 22x20 silhouette (bit 21 = leftmost pixel); the eyes are
// holes, so they show the background. Drawn in the mascot's colour, one run of pixels at a time.
static const uint32_t kLogo[20] MIBLO_ROM = {0x0C000C, 0x0E001C, 0x1F003E, 0x1F807E, 0x1FDEFE, 0x1FFFFE, 0x1FFFFE, 0x1FFFFE, 0x1FFFFE, 0x1FFFFE, 0x1FFFFE, 0x1F3F3E, 0x1E3F1E, 0x3E3F1F, 0x3F3F3F, 0x3FFFFF, 0x3FFFFF, 0x3FFFFF, 0x03FFF0, 0x001E00};
constexpr int kLogoW = 22;
constexpr int kLogoH = 20;

void logo(int cx, int cy, int size) {
  const uint16_t c = mascotSkin();  // Miblo's logo is always the cat, in the mascot's colour
  const int px = size / kLogoW < 1 ? 1 : size / kLogoW;  // whole pixels: crisp at any scale
  const int left = cx - kLogoW * px / 2;
  const int top = cy - kLogoH * px / 2;
  for (int y = 0; y < kLogoH; y++) {
    uint32_t row;
    mibloRomCopy(&row, &kLogo[y], sizeof(row));
    for (int x = 0; x < kLogoW;) {
      if (!(row & (1u << (kLogoW - 1 - x)))) {
        x++;
        continue;
      }
      const int x0 = x;
      while (x < kLogoW && (row & (1u << (kLogoW - 1 - x)))) x++;
      g_canvas->fillRect(left + x0 * px, top + y * px, (x - x0) * px, px, c);
    }
  }
}

void mascot(int cx, int cy, uint8_t frame, bool small) {
  // Simplified Sphynx: flat shapes only, so a frame is
  // cheap and renders the same on an off-screen 16-colour layer. Big triangular ears with pink
  // insides, a round peach head, round green eyes with dark pupils and a small pink nose.
  // Poses: 0 idle, 1 blink (eyes become thin lines), 2 hop (whole cat up a bit), 3 glance
  // (pupils to the side). `small` = 48 px variant (no inner ears).
  const int m = g_w < g_h ? g_w : g_h;
  MascotPen d{*g_canvas, cx, cy, m, small ? 480 : 240};
  const uint8_t pose = mascotPose(frame);
  MascotLook k{0, 0, 0, 0, Eyes::Open, Paws::Down, 0};
  if (pose == 1) k.eyes = Eyes::Closed;
  if (pose == 2) k.dy = -4;
  if (pose == 3) k.gx = 3;
  drawMascot(d, k, !small, false);
}

void deskMascot(int cx, int cy, const MascotLook& look, int half, bool table, bool box, uint8_t wag) {
  MascotPen d{*g_canvas, cx, cy, Sz(half), 48};  // the 96-unit box drawn exactly 2 * Sz(half) wide
  drawMascot(d, look, true, true, table, box, wag);
}

bool petWags() {
  PetDef def;
  mibloRomCopy(&def, currentPet(), sizeof(def));
  return def.tail != nullptr;
}

void qr(const char* payload, int x, int y, int scale) {
  QRCode code;
  // Version 3 (29x29 modules), the one the firmware builds the QR library for (LOCK_VERSION=3).
  static_assert(LOCK_VERSION == 0 || LOCK_VERSION == 3, "the QR library is locked to another version");
  uint8_t data[(29 * 29 + 7) / 8];  // = qrcode_getBufferSize(3)
  // The library never checks capacity: a payload longer than version 3 holds (53 bytes in byte
  // mode, ECC_LOW) would overrun its buffers. Such a QR is not drawn at all.
  if (strlen(payload) > 53) return;
  qrcode_initText(&code, data, 3, ECC_LOW, payload);
  const int quiet = 2;
  const int size = (code.size + quiet * 2) * scale;
  g_canvas->fillRect(x, y, size, size, color::WHITE);
  for (uint8_t my = 0; my < code.size; my++) {
    for (uint8_t mx = 0; mx < code.size; mx++) {
      if (qrcode_getModule(&code, mx, my)) {
        g_canvas->fillRect(x + (mx + quiet) * scale, y + (my + quiet) * scale, scale, scale, color::BLACK);
      }
    }
  }
}

}  // namespace screens
