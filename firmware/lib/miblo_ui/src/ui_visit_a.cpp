// ui_visit_a.cpp — visits between Miblos: high five, ping-pong, dancing, pizza, release cake,
// merge conflict, daily standup, hackathon, selfie, chess (and their props, kinds 64..95).
//
// Flash is tight, so most of it is data: each activity's looks are a table of phases (packed
// looks, what moves on the beat) and the props' fixed parts are tables of shapes; the code only
// places the props.
#include "miblo_rom.h"
#include "ui_visit_kit.h"

namespace screens {

namespace {

// This file's item kinds (64..95). The anchor of each is in drawVisitItemA.
constexpr uint8_t kSpark = kItemsA + 0;       // a big eight-ray spark
constexpr uint8_t kBall = kItemsA + 1;        // a ping-pong ball
constexpr uint8_t kTable = kItemsA + 2;       // a ping-pong table with its net
constexpr uint8_t kPizza = kItemsA + 3;       // a pizza, slices taken
constexpr uint8_t kSlice = kItemsA + 4;       // a slice of pizza held up, bites taken
constexpr uint8_t kCake = kItemsA + 5;        // a cake with a candle
constexpr uint8_t kConfetti = kItemsA + 6;    // a shower of confetti
constexpr uint8_t kRope = kItemsA + 7;        // a tug-of-war rope
constexpr uint8_t kBoard = kItemsA + 8;       // a little board with sticky notes
constexpr uint8_t kMiniLaptop = kItemsA + 9;  // a small laptop for one
constexpr uint8_t kPhone = kItemsA + 10;      // a phone (back with the camera, or the photo)
constexpr uint8_t kFlash = kItemsA + 11;      // a camera flash
constexpr uint8_t kChess = kItemsA + 12;      // a little chess board with its pieces
constexpr uint8_t kPiece = kItemsA + 13;      // a chess piece in the air (being moved)
static_assert(kPiece < kItemsB, "ui_visit_a.cpp owns kinds 64..95");

constexpr uint16_t kCheese = 0xFECE;   // #f8d870 pizza cheese
constexpr uint16_t kCrust = 0xD46A;    // #d08c50 pizza crust
constexpr uint16_t kRopeTan = 0xCD0C;  // #c8a060 rope
constexpr uint16_t kSteel = 0x8410;    // #808080 laptop and phone bodies
constexpr uint16_t kWood = 0x8AC6;     // #8a5a34 chess board rim

// A point on the cat's own drawing, in the units of its 96-unit box (paws at y 24..39, the
// reaching paw at x 30..46, ear tips at y -42), scaled to the cats' size (smaller in groups).
__attribute__((noinline)) int K(const VisitStage& s, int v) { return s.half * v / 48; }

// ---- looks ----

// A look packed in one number, its directions relative to the other cat: gx > 0 and dx > 0 point
// at it, and the paws are named by side (kIn: reaching out on its side). A gx field of 0 (DEF)
// keeps the look visit() passed in (happy hops) and only adds the extras.
// Fields (bits): gx + 4 (0-2), gy + 4 (3-5), eyes (6-8), paws (9-11), dy + 4 (12-14), dx + 4
// (15-17), extras (18-31). So gx, gy, dx and dy are -4..3 (gx -4 would read as DEF), eyes and
// paws below 8, extras 14 bits. onBeat relies on this: a hop subtracts 4 from dy (so dy must be
// 0..3 there), mAltG/H flip bit 9 (paws in pairs), mSway mirrors gx and dx about 4.
enum : uint8_t { kDown, kUp, kIn, kTapIn, kTapOut, kLick };  // pairs (Down, Up), (In, TapIn) alternate
static_assert((int)Eyes::Dizzy < 8 && kLick < 8, "eyes and paws fit their 3 bits (Dizzy: the last Eyes)");
static_assert(kStars < (1u << 14), "the extras fit their 14 bits (kStars: the highest extra)");
constexpr uint32_t L(int gx, int gy, Eyes e, uint8_t paws = kDown, uint32_t extras = 0, int dy = 0, int dx = 0) {
  return (uint32_t)(gx + 4) | (uint32_t)(gy + 4) << 3 | (uint32_t)e << 6 | (uint32_t)paws << 9 | (uint32_t)(dy + 4) << 12 |
         (uint32_t)(dx + 4) << 15 | extras << 18;
}
constexpr uint32_t DEF(uint32_t extras = 0) { return extras << 18; }

// The paw on the side `dir` (+1 right), reaching out or lifted.
Paws reachTo(int dir) { return dir > 0 ? Paws::ReachRight : Paws::ReachLeft; }
Paws tapTo(int dir) { return dir > 0 ? Paws::TapRight : Paws::TapLeft; }

// Sets `k` from a packed look, for a cat whose other cat is towards `dir` (+1: to its right).
__attribute__((noinline)) void unpack(MascotLook& k, int dir, uint32_t c) {
  if (!(c & 7)) {
    k.extras |= (uint16_t)(c >> 18);
    return;
  }
  const uint8_t p = c >> 9 & 7;
  k.paws = p == kIn ? reachTo(dir) : p == kTapIn ? tapTo(dir) : p == kTapOut ? tapTo(-dir) : p == kUp ? Paws::Up
           : p == kLick ? Paws::Lick : Paws::Down;
  k.dx = (int8_t)(((int)(c >> 15 & 7) - 4) * dir);
  k.dy = (int8_t)((int)(c >> 12 & 7) - 4);
  k.gx = (int8_t)(((int)(c & 7) - 4) * dir);
  k.gy = (int8_t)((int)(c >> 3 & 7) - 4);
  k.eyes = (Eyes)(c >> 6 & 7);
  k.extras = (uint16_t)(c >> 18);
}
// The guest's and the host's looks.
__attribute__((noinline)) void looks(const VisitStage& s, VisitFrame& f, uint32_t guest, uint32_t host) {
  unpack(f.them, s.toHost, guest);
  unpack(f.me, -s.toHost, host);
}

// What moves on the beat in a phase (the host one beat later with mCounter): mHop: up on even
// beats; mAltG / mAltH: the guest's / host's paws switch to their pair on odd beats; mChew: the
// mouth opens on odd beats; mSway: gaze and lean flip every two beats; mHeart: a heart beside the
// host.
enum : uint8_t { mHop = 1, mAltG = 2, mAltH = 4, mChew = 8, mSway = 16, mCounter = 32, mHeart = 64 };
// A phase: until `end` (100 ms steps into the stay), the beat (50 ms steps), what moves on it, and
// the guest's and the host's looks. The last phase of a script ends at 180.
#define B4(v) (uint8_t)((v) & 255), (uint8_t)((v) >> 8 & 255), (uint8_t)((v) >> 16 & 255), (uint8_t)((v) >> 24)
#define PH(end, beat, mods, guest, host) end, beat, mods, B4(guest), B4(host)
constexpr int kPhaseBytes = 11;

uint32_t onBeat(uint32_t c, uint32_t beat, uint8_t mods, uint8_t alt) {
  if (!(c & 7)) return c;
  if ((mods & mHop) && beat % 2 == 0) c -= 4u << 12;
  if ((mods & alt) && beat % 2) c ^= 1u << 9;
  if ((mods & mChew) && beat % 2) c |= (uint32_t)kMouthO << 18;
  if ((mods & mSway) && beat / 2 % 2) c = (c & ~(7u | 7u << 15)) | (8 - (c & 7)) | (8 - (c >> 15 & 7)) << 15;
  return c;
}

// Plays a script: sets both looks for `s.t` and returns the phase it is in (*since: ms into it).
__attribute__((noinline)) int play(const VisitStage& s, VisitFrame& f, const uint8_t* script, uint32_t* since) {
  uint8_t ph[kPhaseBytes];
  uint32_t from = 0;
  int i = 0;
  for (;; i++) {
    mibloRomCopy(ph, script + i * kPhaseBytes, kPhaseBytes);
    if (s.t < ph[0] * 100u || ph[0] >= 180) break;
    from = ph[0] * 100u;
  }
  *since = s.t - from;
  const uint32_t beat = s.t / (ph[1] ? ph[1] * 50u : 1u), mods = ph[2];
  const uint32_t g = (uint32_t)ph[3] | (uint32_t)ph[4] << 8 | (uint32_t)ph[5] << 16 | (uint32_t)ph[6] << 24;
  const uint32_t h = (uint32_t)ph[7] | (uint32_t)ph[8] << 8 | (uint32_t)ph[9] << 16 | (uint32_t)ph[10] << 24;
  looks(s, f, onBeat(g, beat, mods, mAltG), onBeat(h, beat + (mods & mCounter ? 1 : 0), mods, mAltH));
  if (mods & mHeart) f.me.extras |= kHeart;
  return i;
}

// ---- the activities ----

// Eyes on each other, three jumps that end in high fives (a spark between the paws), "^ ^".
constexpr uint32_t kHop = L(3, 0, Eyes::Open), kCrouch = L(3, 2, Eyes::Wide, kTapIn, 0, 2),
                   kSlap = L(3, 0, Eyes::Happy, kIn, 0, -4, 2), kPleased = L(3, 0, Eyes::Happy);
#define HIGH_FIVE(at)                                                                                   \
  PH(at + 20, 7, mHop, kHop, kHop), PH(at + 28, 0, 0, kCrouch, kCrouch), PH(at + 38, 0, 0, kSlap, kSlap), \
      PH(at + 40, 0, 0, kPleased, kPleased)
const uint8_t kHighFive[] MIBLO_ROM = {
    PH(20, 3, mSway, L(3, 0, Eyes::Open, kDown, 0, 0, 1), L(3, 0, Eyes::Open, kDown, 0, 0, 1)),  // ready? a wiggle
    HIGH_FIVE(20), HIGH_FIVE(60), HIGH_FIVE(100),
    PH(155, 0, mHeart, DEF(), DEF()), PH(180, 0, mHeart, DEF(kHeart), DEF())};
void highFive(const VisitStage& s, VisitFrame& f) {
  uint32_t since;
  const int i = play(s, f, kHighFive, &since);
  if (i < 13 && i % 4 == 3) addItem(f, kSpark, s.mid, s.cy + K(s, 16), (uint8_t)(since / 150));
}

// A ball goes back and forth over a little table, each one hits it with a paw; at the end the
// host misses it and the guest cheers.
void pingPong(const VisitStage& s, VisitFrame& f) {
  const uint32_t t = s.t;
  const int th = s.toHost;
  const uint32_t P = 900, start = 1000, miss = start + 14 * P;  // ms per crossing, the serve, the miss
  const int gp = s.guestX + th * K(s, 36), hp = s.hostX - th * K(s, 36);  // the hitting paws
  const int pawY = s.cy + K(s, 26), floorY = s.bottom - Sz(4);
  addItem(f, kTable, s.mid, s.bottom - Sz(3));
  uint32_t g = L(3, -2, Eyes::Open, kIn), h = L(3, 2, Eyes::Open);  // about to serve
  int bx = gp, by = pawY - K(s, 8);
  if (t >= miss + P) {  // it lies behind the host; the guest won, then the host laughs too
    bx = hp + th * K(s, 26);
    by = floorY;
    g = L(3, 0, Eyes::Happy, kUp, t >= 16000 ? kHeart : 0, (t / 300) % 2 ? -4 : 0);
    h = t < 16000 ? L(0, 3, Eyes::Wide, kDown, kSweat) : L(3, 0, Eyes::Happy);
  } else if (t >= start) {  // a lob each way; the last one goes past the host's paw, down
    const uint32_t n = (t - start) / P, p = (t - start) % P;
    const bool fromGuest = n % 2 == 0, last = t >= miss;
    const int a = fromGuest ? gp : hp, b = last ? hp + th * K(s, 26) : fromGuest ? hp : gp;
    bx = a + (b - a) * (int)p / (int)P;
    by = last ? pawY + (floorY - pawY) * (int)p / (int)P : pawY - K(s, 32) * 4 * (int)p * (int)(P - p) / (int)(P * P);
    const bool early = p < 200, late = p > P - 200;  // just hit it, or about to
    const int gy = p > 250 && p < 650 ? -2 : 2;      // eyes on the ball
    g = L(3, gy, Eyes::Open, (fromGuest ? early : late) ? kIn : kDown);
    h = L(3, gy, last ? Eyes::Wide : Eyes::Open, (fromGuest ? late : early) ? kIn : kDown);
  }
  looks(s, f, g, h);
  addItem(f, kBall, bx, by);
}

// A little dance: hops on the beat, paws up and down, swaying, notes rising between them.
const uint8_t kDance[] MIBLO_ROM = {
    PH(15, 10, mAltG | mSway, L(3, 0, Eyes::Happy, kDown, 0, 0, 1), L(3, 0, Eyes::Open)),  // the guest starts
    PH(155, 10, mHop | mAltG | mAltH | mSway, L(3, 0, Eyes::Happy, kDown, 0, 0, 1), L(3, 0, Eyes::Happy, kDown, 0, 0, 1)),
    PH(180, 10, mHop | mHeart, L(3, 0, Eyes::Happy, kUp), L(3, 0, Eyes::Happy, kUp))};  // ta-da
void dance(const VisitStage& s, VisitFrame& f) {
  uint32_t since;
  play(s, f, kDance, &since);
  const uint32_t t = s.t;
  const int y0 = s.cy, y1 = s.top + Sz(11);
  for (uint32_t i = 0; i < 3; i++) {  // notes rising from between them, one after the other
    if (t < 1000 + i * 700) continue;
    const uint32_t q = t - 1000 - i * 700;
    addItem(f, vprop::Note, s.mid + Sz(i == 1 ? -1 : i == 2 ? -6 : -9) + shuttle(q, 1000, Sz(4)) - Sz(2),
            y0 - (y0 - y1) * (int)(q % 2100) / 2100);
  }
}

// A pizza shows up in the middle; each takes a slice (the guest first) and munches it (it gets
// smaller), then licks its lips.
constexpr uint32_t kWatch = L(3, 3, Eyes::Open), kReach = L(3, 3, Eyes::Open, kIn), kMunch = L(0, 0, Eyes::Happy, kLick),
                   kYum = L(3, 0, Eyes::Happy, kDown, kTongue);
const uint8_t kPizzaScript[] MIBLO_ROM = {
    PH(20, 0, 0, L(3, 3, Eyes::Wide, kDown, kMouthO), L(3, 3, Eyes::Wide, kDown, kMouthO)),  // wow, pizza
    PH(35, 0, 0, kReach, kWatch), PH(50, 6, mChew, kMunch, kReach), PH(150, 6, mChew | mCounter, kMunch, kMunch),
    PH(160, 8, mHop | mCounter, kYum, kYum), PH(180, 8, mHop | mCounter | mHeart, kYum, kYum)};
// A cat's slice: in the reaching paw from `grab`, at the mouth from half a second later, a bite
// every 3.2 s; once it is gone the cat licks its paw.
void slice(const VisitStage& s, VisitFrame& f, MascotLook& k, int cx, int dir, uint32_t grab) {
  const uint32_t t = s.t;
  if (t < grab || t >= 15000) return;
  if (t < grab + 500) addItem(f, kSlice, cx + dir * K(s, 38), s.cy + K(s, 32), 0);
  else if (t < grab + 500 + 3 * 3200) addItem(f, kSlice, cx - K(s, 10), s.cy + K(s, 32), (uint8_t)((t - grab - 500) / 3200));
  else k.extras |= kTongue;
}
void pizza(const VisitStage& s, VisitFrame& f) {
  uint32_t since;
  play(s, f, kPizzaScript, &since);
  const uint32_t t = s.t;
  addItem(f, kPizza, s.mid, s.bottom - Sz(12), t < 3000 ? 0 : t < 4500 ? 1 : 2);
  slice(s, f, f.them, s.guestX, s.toHost, 3000);
  slice(s, f, f.me, s.hostX, -s.toHost, 4500);
}

// Release cake: a cake with a candle, a song, both blow it out together, confetti.
const uint8_t kCakeScript[] MIBLO_ROM = {
    PH(25, 0, 0, L(3, 3, Eyes::Wide, kDown, kMouthO), L(3, 3, Eyes::Wide, kDown, kMouthO)),    // 0: a cake!
    PH(70, 6, mHop | mChew | mCounter, L(3, 2, Eyes::Happy), L(3, 2, Eyes::Happy)),           // 1: singing
    PH(80, 0, 0, L(3, 3, Eyes::Open, kDown, kFluffed), L(3, 3, Eyes::Open, kDown, kFluffed)),  // 2: a big breath
    PH(93, 0, 0, L(0, 0, Eyes::Closed, kDown, kMouthO, 0, 1), L(0, 0, Eyes::Closed, kDown, kMouthO, 0, 1)),  // 3: blow!
    PH(120, 6, mHop | mCounter, L(3, -3, Eyes::Happy, kUp), L(3, -3, Eyes::Happy, kUp)),  // 4: confetti, cheers
    PH(180, 0, mHeart, DEF(), DEF())};
void cake(const VisitStage& s, VisitFrame& f) {
  uint32_t since;
  const int i = play(s, f, kCakeScript, &since);
  const uint32_t t = s.t;
  uint8_t flame = (uint8_t)((t / (t < 8000 ? 200 : 100)) % 2);  // flickers, harder when blown at
  if (t >= 8800) flame = t < 11000 ? 2 : 3;                       // smoke, then just the candle
  addItem(f, kCake, s.mid, s.bottom - Sz(1), flame);
  if (i == 1) {  // a note rising as they sing
    const int y0 = s.cy - K(s, 6), y1 = s.top + Sz(11);
    addItem(f, vprop::Note, s.mid - Sz(3), y0 - (y0 - y1) * (int)(since % 1500) / 1500);
  } else if (i >= 4) {
    const uint8_t step = (uint8_t)(since / 90 + (i == 5 ? 30 : 0));
    addItem(f, kConfetti, s.mid - Sz(12), s.top + Sz(2), step);
    addItem(f, kConfetti, s.mid + Sz(12), s.top + Sz(2), (uint8_t)(step + 37));
  }
}

// Merge conflict: a tug of war over a rope, one way then the other; it snaps, both fall back with
// a fright (only a unit: the raised paws stay on screen), then laugh.
constexpr uint32_t kPull = L(0, 0, Eyes::Closed, kIn, kGrumpy | kSweat), kHold = L(3, 0, Eyes::Wide, kIn);
const uint8_t kMerge[] MIBLO_ROM = {
    PH(20, 0, 0, L(3, 3, Eyes::Open), L(3, 3, Eyes::Open)),  // a rope on the floor: who goes first?
    PH(30, 0, 0, L(3, 0, Eyes::Open, kIn, kGrumpy), L(3, 0, Eyes::Open, kIn, kGrumpy)),  // grab it
    PH(45, 0, 0, kPull, kHold), PH(60, 0, 0, kHold, kPull), PH(75, 0, 0, kPull, kHold),
    PH(90, 0, 0, kHold, kPull), PH(105, 0, 0, kPull, kHold), PH(120, 0, 0, kHold, kPull),
    PH(138, 0, 0, L(0, -3, Eyes::Wide, kUp, kFluffed, 3, -1), L(0, -3, Eyes::Wide, kUp, kFluffed, 3, -1)),  // snap!
    PH(155, 0, 0, L(3, 0, Eyes::Open, kDown, kMouthO, 2), L(3, 0, Eyes::Open, kDown, kMouthO, 2)),
    PH(180, 0, mHeart, DEF(kMouthWide), DEF(kMouthWide))};  // and laughing
void merge(const VisitStage& s, VisitFrame& f) {
  uint32_t since;
  const int i = play(s, f, kMerge, &since);
  const int th = s.toHost;
  int shift = 0;  // both cats (and the rope) sideways, + towards the host: the puller wins ground
  if (i >= 2 && i < 8) {
    const int a = shuttle(since, 1500, 3);
    shift = i % 2 ? a : -a;
    f.them.dx = f.me.dx = (int8_t)(th * shift);
  }
  // The rope runs from one cat to the other, through their paws while they hold it, else it lies
  // on the floor (snapped in two at the end); the ribbon in its middle shows who is winning.
  const bool held = s.t >= 2000 && s.t < 12300;
  const int grid = (s.hostX - s.guestX) * th / 2 * 100 / (Sz(100) > 0 ? Sz(100) : 1);  // half, in 240-grid units
  addItem(f, kRope, s.mid + (held ? th * K(s, shift) : 0), held ? s.cy + K(s, 29) : s.bottom - Sz(5),
          (uint8_t)((grid > 63 ? 63 : grid) | (i >= 8 ? 64 : 0)));
}

// Daily standup: a little board; each one talks in turn ("...", paws gesturing) and puts up a
// sticky note; the fourth turn drags on (the other gets sleepy).
constexpr uint32_t kTalk = L(3, 0, Eyes::Open, kIn), kListen = L(3, 0, Eyes::Open), kPosted = L(2, -3, Eyes::Happy),
                   kGlance = L(2, -3, Eyes::Open);
// A turn from `at` to `end` (100 ms steps): talking, then half a second to put the note up.
#define TURN(at, end, guestTalks, listen)                                                                \
  PH(end - 5, 6, guestTalks ? mAltG : mAltH, guestTalks ? kTalk : listen, guestTalks ? listen : kTalk), \
      PH(end, 0, 0, guestTalks ? kPosted : kGlance, guestTalks ? kGlance : kPosted)
const uint8_t kStandup[] MIBLO_ROM = {
    PH(20, 0, 0, kGlance, kGlance),  // the board, empty
    TURN(20, 38, true, kListen), TURN(38, 56, false, kListen), TURN(56, 74, true, kListen),
    TURN(74, 104, false, L(3, 0, Eyes::Sleepy)), TURN(104, 122, true, kListen), TURN(122, 140, false, kListen),
    PH(155, 0, 0, L(2, -3, Eyes::Wide), L(2, -3, Eyes::Wide)),  // the whole board
    PH(180, 0, mHeart, DEF(), DEF())};
void standup(const VisitStage& s, VisitFrame& f) {
  uint32_t since;
  const int i = play(s, f, kStandup, &since);
  if (i >= 1 && i <= 11 && i % 2)  // talking: "..." over the speaker's head
    addItem(f, vprop::Dots, (i % 4 == 1 ? s.guestX : s.hostX) - Sz(6), s.cy - K(s, 30), (uint8_t)(1 + (since / 250) % 3));
  addItem(f, kBoard, s.mid, s.top + Sz(2), (uint8_t)(i > 12 ? 6 : i / 2));
}

// Hackathon: a laptop each, typing flat out (smoke from the keyboards), then it works: a spark.
const uint8_t kHackathon[] MIBLO_ROM = {
    PH(15, 0, 0, L(3, 3, Eyes::Open), L(3, 3, Eyes::Open)),  // lids up
    PH(60, 3, mAltG | mAltH | mCounter, L(3, 3, Eyes::Open, kIn), L(3, 3, Eyes::Open, kIn)),  // typing
    PH(125, 3, mAltG | mAltH | mCounter, L(3, 3, Eyes::Wide, kIn, kSweat), L(3, 3, Eyes::Wide, kIn, kSweat)),
    PH(140, 0, 0, L(3, -3, Eyes::Wide, kDown, kMouthO), L(3, -3, Eyes::Wide, kDown, kMouthO)),  // it works!
    PH(155, 8, mHop | mCounter, L(3, 0, Eyes::Happy, kUp), L(3, 0, Eyes::Happy, kUp)),
    PH(180, 0, mHeart, DEF(), DEF())};
void hackathon(const VisitStage& s, VisitFrame& f) {
  uint32_t since;
  const int i = play(s, f, kHackathon, &since);
  const uint32_t t = s.t;
  const int th = s.toHost;
  const int gl = s.guestX + th * K(s, 28), hl = s.hostX - th * K(s, 28), ly = s.bottom - Sz(3);
  const uint8_t code = (uint8_t)(t / 100 % 128), ok = i >= 3 ? 128 : 0;
  addItem(f, kMiniLaptop, gl, ly, (uint8_t)(code | ok));
  addItem(f, kMiniLaptop, hl, ly, (uint8_t)((code + 5) % 128 | ok));
  if (t >= 4000 && i < 3) {  // smoke from the keyboards
    addItem(f, vprop::Puff, gl - th * Sz(4), ly - Sz(17), (uint8_t)((t / 200) % 2));
    addItem(f, vprop::Puff, hl + th * Sz(4), ly - Sz(17), (uint8_t)((t / 200 + 1) % 2));
  }
  if (i == 3) addItem(f, kSpark, s.mid, s.cy - K(s, 30), (uint8_t)(since / 250));
}

// Selfie: the guest holds up a phone, both pose ("^ ^"), flash; a silly one, flash; then they
// look at the photo together.
constexpr uint32_t kShut = L(0, 0, Eyes::Closed), kDazzled = L(0, 0, Eyes::Wide), kHoldUp = (uint32_t)kUp << 9;
const uint8_t kSelfie[] MIBLO_ROM = {
    PH(10, 0, 0, L(3, 0, Eyes::Open, kIn), L(3, 0, Eyes::Open)),    // 0: the phone comes out
    PH(25, 0, 0, L(2, -3, Eyes::Open, kUp), L(3, -3, Eyes::Open)),  // 1: up it goes
    PH(58, 0, 0, L(2, -2, Eyes::Happy, kUp, 0, 0, 2), L(2, -2, Eyes::Happy, kUp, 0, 0, 2)),  // 2: cheek to cheek
    PH(62, 0, 0, kShut | kHoldUp, kShut),                           // 3: flash!
    PH(70, 0, 0, kDazzled | kHoldUp, kDazzled),                     // 4
    PH(98, 0, 0, L(2, -2, Eyes::Happy, kUp, kTongue, 0, 2), L(2, -2, Eyes::Closed, kTapIn, kTongue, 0, 2)),  // 5: silly
    PH(102, 0, 0, kShut | kHoldUp, kShut),                          // 6: flash!
    PH(110, 0, 0, kDazzled | kHoldUp, kDazzled),                    // 7
    PH(130, 0, 0, L(3, 2, Eyes::Open, kIn), L(3, 2, Eyes::Open)),   // 8: the photo
    PH(155, 0, 0, L(3, 2, Eyes::Happy, kIn), L(3, 2, Eyes::Happy)),
    PH(180, 0, mHeart, DEF(kHeart), DEF())};
void selfie(const VisitStage& s, VisitFrame& f) {
  uint32_t since;
  const int i = play(s, f, kSelfie, &since);
  const int th = s.toHost;
  if (i == 0) {
    addItem(f, kPhone, s.guestX + th * K(s, 38), s.cy + K(s, 22), 0);
  } else if (i < 8) {  // held up (further in while they lean in for the photo)
    const int px = s.guestX + th * K(s, i == 2 || i == 5 ? 40 : 38), py = s.cy - K(s, 30);
    addItem(f, kPhone, px, py, 0);
    if (i == 3 || i == 6) addItem(f, kFlash, px, py);
  } else {  // the photo, held between them
    addItem(f, kPhone, s.mid, s.cy + K(s, 14), 1);
  }
}

// The chess board: 4 columns x 3 rows of 7 x 5 squares, its bottom edge centred on (x, y). `hc`: the
// column counted from the host's side; (sx, sy): the square's centre.
constexpr int kChessCols = 4, kChessRows = 3;
void chessSquare(int x, int y, bool hostRight, int hc, int row, int& sx, int& sy) {
  const int col = hostRight ? kChessCols - 1 - hc : hc;
  sx = x - Sz(14) + col * Sz(7) + Sz(3);
  sy = y - Sz(16) + row * Sz(5) + Sz(2);
}
// Piece k (0, 1 the host's white ones, 2, 3 the guest's gold ones) in a state of the game (kChess's
// f & 7: 0 set up, 1 the host's piece lifted, 2 it moved forward, 3 the guest's piece lifted, 4 the
// guest took it): false when it is not on the board, else its (host-side column, row).
bool chessPiece(int state, int k, int& col, int& row) {
  col = k < 2 ? 0 : 3;
  row = k % 2 ? 2 : 0;
  if (k == 0 && (state == 1 || state >= 4)) return false;
  if (k == 2 && state == 3) return false;
  if ((k == 0 && state >= 2) || (k == 2 && state >= 4)) col = 2, row = 1;
  return true;
}

// Chess: the host moves a piece; the guest scratches its head ("..."), then takes it; the host
// jumps.
const uint8_t kChessScript[] MIBLO_ROM = {
    PH(20, 0, 0, L(3, 3, Eyes::Open), L(3, 3, Eyes::Open)),         // 0: set up
    PH(30, 0, 0, L(3, 3, Eyes::Open), L(3, 3, Eyes::Sleepy)),       // 1: the host thinks
    PH(45, 0, 0, L(3, 3, Eyes::Open), L(3, 3, Eyes::Open, kIn)),    // 2: and moves
    PH(90, 10, mAltG, L(2, -3, Eyes::Open, kUp), L(3, 0, Eyes::Happy)),  // 3: the guest scratches its head
    PH(105, 0, 0, L(3, 3, Eyes::Open, kIn), L(3, 3, Eyes::Open)),   // 4: and takes the piece
    PH(110, 0, 0, L(3, 0, Eyes::Happy, kDown, kTongue), L(0, 3, Eyes::Wide, kDown, kFluffed | kAlarm, -4)),  // 5: !
    PH(120, 0, 0, L(3, 0, Eyes::Happy, kDown, kTongue), L(0, 3, Eyes::Wide, kDown, kFluffed | kAlarm)),
    PH(135, 0, 0, L(3, 0, Eyes::Happy, kDown, kTongue), L(0, 3, Eyes::Wide, kDown, kFluffed | kSweat)),
    PH(150, 0, 0, L(3, 0, Eyes::Happy), L(3, 3, Eyes::Open, kDown, kGrumpy)),  // hmm
    PH(180, 0, mHeart, DEF(), DEF())};  // good game
void chess(const VisitStage& s, VisitFrame& f) {
  uint32_t since;
  const int i = play(s, f, kChessScript, &since);
  const bool hostRight = s.toHost > 0;
  const int bx = s.mid, by = s.bottom - Sz(1);
  const int state = i < 2 ? 0 : i < 5 ? i - 1 : 4;
  if (i == 2 || i == 4) {  // a piece flying to the middle square, lifted along the way
    int x0, y0, x1, y1;
    chessSquare(bx, by, hostRight, i == 2 ? 0 : 3, 0, x0, y0);
    chessSquare(bx, by, hostRight, 2, 1, x1, y1);
    const int lift = Sz(8) * 4 * (int)since * (int)(1500 - since) / (1500 * 1500);
    addItem(f, kPiece, lerpTo(x0, x1, since, 1500), lerpTo(y0, y1, since, 1500) + Sz(1) - lift, i == 2 ? 0 : 1);
  }
  if (i == 3) addItem(f, vprop::Dots, s.guestX - Sz(6), s.cy - K(s, 30), (uint8_t)(1 + (s.t / 300) % 3));
  addItem(f, kChess, bx, by, (uint8_t)(state | (hostRight ? 128 : 0)));
}

// ---- drawing ----

// Shapes in 240-grid units from an item's anchor (x, y), out of line: each one costs a call rather
// than its own scaling code. A non-zero size never scales down to nothing.
int px(int v) {
  const int p = Sz(v);
  return p || !v ? p : 1;
}
__attribute__((noinline)) void box(int x, int y, int dx, int dy, int w, int h, uint16_t c) {
  C().fillRect(x + Sz(dx), y + Sz(dy), px(w), px(h), c);
}
__attribute__((noinline)) void dot(int x, int y, int dx, int dy, int r, uint16_t c) {
  C().fillCircle(x + Sz(dx), y + Sz(dy), px(r), c);
}
__attribute__((noinline)) void tri(int x, int y, int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) {
  C().fillTriangle(x + Sz(x0), y + Sz(y0), x + Sz(x1), y + Sz(y1), x + Sz(x2), y + Sz(y2), c);
}

// Fixed shapes in flash, drawn in order from an anchor: an op and a colour (an index into kInk),
// then its numbers in 240-grid units: a rect (dx, dy, w, h), a dot (dx, dy, r) or a triangle
// (three corners).
const uint16_t kInk[] MIBLO_ROM = {color::WHITE, color::AMBER, color::GREEN, color::BLUE,  color::CORAL, color::RED,
                                   color::MUTED, color::BLACK, color::SKIN,  kCheese,      kCrust,       kSteel};
// Confetti takes five in a row from iAmber.
enum : int8_t { iWhite, iAmber, iGreen, iBlue, iCoral, iRed, iMuted, iBlack, iSkin, iCheese, iCrust, iSteel };
#define RECT(c, dx, dy, w, h) (int8_t)(c), dx, dy, w, h
#define DOT(c, dx, dy, r) (int8_t)(64 | (c)), dx, dy, r
#define TRI(c, x0, y0, x1, y1, x2, y2) (int8_t)(128 | (c)), x0, y0, x1, y1, x2, y2
#define END (int8_t)192
uint16_t ink(int i) {
  uint16_t c;
  mibloRomCopy(&c, &kInk[i], sizeof(c));
  return c;
}
void shapes(int x, int y, const int8_t* p) {
  for (;;) {
    int8_t v[7];
    v[0] = (int8_t)mibloRomByte((const char*)p);
    const uint8_t op = (uint8_t)v[0] >> 6, n = op == 0 ? 4 : op == 1 ? 3 : 6;
    if (op == 3) return;
    for (int i = 1; i <= n; i++) v[i] = (int8_t)mibloRomByte((const char*)p + i);
    p += n + 1;
    const uint16_t c = ink(v[0] & 63);
    if (op == 0) box(x, y, v[1], v[2], v[3], v[4], c);
    else if (op == 1) dot(x, y, v[1], v[2], v[3], c);
    else tri(x, y, v[1], v[2], v[3], v[4], v[5], v[6], c);
  }
}
const int8_t kBallShape[] MIBLO_ROM = {DOT(iWhite, 0, 0, 3), END};
const int8_t kTableShape[] MIBLO_ROM = {RECT(iGreen, -26, 0, 52, 3), RECT(iWhite, -26, 0, 52, 1), RECT(iWhite, -1, -6, 2, 6), END};
const int8_t kPizzaShape[] MIBLO_ROM = {DOT(iCrust, 0, 0, 11), DOT(iCheese, 0, 0, 9), DOT(iRed, -4, -5, 2), DOT(iRed, 4, -5, 2),
                                        DOT(iRed, -5, 3, 2),   DOT(iRed, 4, 4, 2),    DOT(iRed, 0, 0, 2),   END};
const int8_t kCakeShape[] MIBLO_ROM = {RECT(iMuted, -14, -2, 28, 2), RECT(iCoral, -11, -12, 22, 10), RECT(iWhite, -11, -12, 22, 3),
                                       DOT(iWhite, -7, -9, 2),       DOT(iWhite, 0, -9, 2),           DOT(iWhite, 7, -9, 2),
                                       RECT(iBlue, -1, -19, 2, 7),   END};
const int8_t kBoardShape[] MIBLO_ROM = {RECT(iMuted, -25, -1, 50, 30), RECT(iWhite, -24, 0, 48, 28), END};
const int8_t kLaptopShape[] MIBLO_ROM = {RECT(iSteel, -10, 0, 20, 2), RECT(iSteel, -8, -13, 16, 13), RECT(iBlack, -7, -12, 14, 11), END};
const int8_t kPhoneBack[] MIBLO_ROM = {RECT(iSteel, -5, -8, 10, 16), DOT(iBlack, -2, -5, 2), DOT(iWhite, -2, -5, 1), END};
// The photo: the phone turned sideways, two cat heads (ears, a round face, eyes) side by side.
const int8_t kPhonePhoto[] MIBLO_ROM = {RECT(iSteel, -10, -7, 20, 14),     RECT(iBlue, -9, -6, 18, 12),
                                        TRI(iSkin, -8, -1, -7, -6, -5, -2), TRI(iSkin, -1, -1, -2, -6, -4, -2),
                                        DOT(iSkin, -4, 1, 3),               TRI(iSkin, 1, -1, 2, -6, 4, -2),
                                        TRI(iSkin, 8, -1, 7, -6, 5, -2),    DOT(iSkin, 4, 1, 3),
                                        DOT(iBlack, -5, 0, 0),              DOT(iBlack, -3, 0, 0),
                                        DOT(iBlack, 3, 0, 0),               DOT(iBlack, 5, 0, 0),
                                        END};
const int8_t kFlashShape[] MIBLO_ROM = {DOT(iWhite, 0, 0, 6),              RECT(iWhite, -13, -1, 26, 2),     RECT(iWhite, -1, -13, 2, 26),
                                        TRI(iWhite, -9, -9, 0, -3, -3, 0), TRI(iWhite, 9, -9, 0, -3, 3, 0),  TRI(iWhite, -9, 9, 0, 3, -3, 0),
                                        TRI(iWhite, 9, 9, 0, 3, 3, 0),     END};

// A little pawn standing on (x, y): white, or gold for the guest.
void pawn(int x, int y, bool white) {
  const uint16_t c = white ? color::WHITE : color::AMBER;
  tri(x, y, -3, 0, 3, 0, 0, -6, c);
  dot(x, y, 0, -6, 2, c);
}

}  // namespace

bool visitActivityA(miblo::Gift g, const VisitStage& s, VisitFrame& f) {
  switch (g) {
    case miblo::Gift::HighFive: highFive(s, f); return true;
    case miblo::Gift::PingPong: pingPong(s, f); return true;
    case miblo::Gift::Dance: dance(s, f); return true;
    case miblo::Gift::Pizza: pizza(s, f); return true;
    case miblo::Gift::Cake: cake(s, f); return true;
    case miblo::Gift::Merge: merge(s, f); return true;
    case miblo::Gift::Standup: standup(s, f); return true;
    case miblo::Gift::Hackathon: hackathon(s, f); return true;
    case miblo::Gift::Selfie: selfie(s, f); return true;
    case miblo::Gift::Chess: chess(s, f); return true;
    default: return false;
  }
}

void drawVisitItemA(const VisitItem& it) {
  const int x = it.x, y = it.y;
  switch (it.kind) {
    case kSpark: {  // (centre) four rays, four dots and a white core; f 0..3 grows (at most 12 out)
      const int l = 6 + 2 * (it.f > 3 ? 3 : it.f), d = l * 2 / 3;
      box(x, y, -l, -1, 2 * l, 2, color::AMBER);
      box(x, y, -1, -l, 2, 2 * l, color::AMBER);
      for (int i = 0; i < 4; i++) dot(x, y, i % 2 ? d : -d, i / 2 ? d : -d, 1, color::AMBER);
      dot(x, y, 0, 0, 3, color::WHITE);
      break;
    }
    case kBall:  // (centre)
      shapes(x, y, kBallShape);
      break;
    case kTable:  // (centre of the top) a green table top with a white edge, the net standing on it
      shapes(x, y, kTableShape);
      break;
    case kPizza:  // (centre) radius 11, pepperoni; f = slices taken from the top (0..2): 60-degree
      // wedges cut out, kept inside the pizza (a paw may be under its edge)
      shapes(x, y, kPizzaShape);
      if (it.f >= 1) tri(x, y, 0, 0, -9, -5, 0, -11, color::BG);
      if (it.f >= 2) tri(x, y, 0, 0, 9, -5, 0, -11, color::BG);
      break;
    case kSlice: {  // (middle of the crust) tip up; f = bites (0..2), shorter each time
      const int b = it.f > 2 ? 2 : it.f;
      tri(x, y, -6, 0, 6, 0, 0, 4 * b - 14, kCheese);
      if (b < 2) dot(x, y, 0, -4, 2, color::RED);
      box(x, y, -7, 0, 14, 3, kCrust);
      break;
    }
    case kCake:  // (centre of the bottom) on a plate; f 0/1: the flame flickers, 2: smoke, 3: out
      shapes(x, y, kCakeShape);  // plate, sponge, icing and its drips, the candle
      if (it.f < 2) {
        dot(x, y, it.f, -21, 2, color::AMBER);
        tri(x, y, it.f - 2, -21, it.f + 2, -21, it.f, -26, color::AMBER);
      } else if (it.f == 2) {
        dot(x, y, 1, -22, 2, color::DIM);
        dot(x, y, -1, -26, 2, color::DIM);
      }
      break;
    case kConfetti:  // (top centre) 8 pieces falling 60 from y, 26 either side of x; f = fall step
      for (int i = 0; i < 8; i++) {
        const uint32_t r = (uint32_t)(i + 1) * 2654435761u;
        const int py = (int)(((r >> 16) + (uint32_t)it.f * (uint32_t)(2 + i % 3)) % 60);
        box(x, y, (int)((r >> 8) % 52) - 26, py, 4, 2 + i % 2, ink(iAmber + (i + it.f / 16) % 5));
      }
      break;
    case kRope: {  // (centre) f & 63: half-length in 240-grid units; 64: snapped in the middle
      const int hl = it.f & 63;
      if (it.f & 64) {
        box(x, y, -hl, 0, hl - 4, 2, kRopeTan);
        box(x, y, 4, 0, hl - 4, 2, kRopeTan);
        box(x, y, -7, -1, 3, 4, color::RED);  // the ribbon, on one end
      } else {
        box(x, y, -hl, 0, 2 * hl, 2, kRopeTan);
        box(x, y, -1, -2, 3, 6, color::RED);  // the ribbon marks the middle
      }
      break;
    }
    case kBoard:  // (centre of the top) 48 x 28 in a grey frame; f = sticky notes on it (0..6)
      shapes(x, y, kBoardShape);
      for (int i = 0; i < it.f && i < 6; i++) {
        const int k = (i + i / 3) % 3;
        box(x, y, -20 + (i % 3) * 14, 4 + (i / 3) * 12, 12, 10, ink(k == 0 ? iAmber : k == 1 ? iGreen : iCoral));
      }
      break;
    case kMiniLaptop:  // (centre of the keyboard) 20 wide; f & 127 scrolls the code, 128: a green check
      shapes(x, y, kLaptopShape);
      if (it.f & 128) {
        check(x, y - Sz(7), Sz(9), color::GREEN);
      } else {
        for (int i = 0; i < 3; i++)  // inside the screen (x -7..6): indented lines are shorter
          box(x, y, -5 + (i % 2) * 2, -10 + i * 3, 3 + ((it.f & 127) * 5 + i * 7) % (i % 2 ? 7 : 9), 1,
              i % 2 ? color::BLUE : color::GREEN);
      }
      break;
    case kPhone:  // (centre) f 0: its back with the camera (10 x 16), 1: the photo (20 x 14, two cat heads)
      shapes(x, y, it.f ? kPhonePhoto : kPhoneBack);
      break;
    case kFlash:  // (centre) a white burst, 13 out
      shapes(x, y, kFlashShape);
      break;
    case kChess:  // (centre of the bottom edge) 30 x 17; f & 7: the game's state, 128: the host on the right
      box(x, y, -15, -17, 30, 17, kWood);
      for (int r = 0; r < kChessRows; r++)
        for (int c = 0; c < kChessCols; c++) box(x, y, -14 + c * 7, -16 + r * 5, 7, 5, (r + c) % 2 ? color::FAINT : color::MUTED);
      for (int k = 0; k < 4; k++) {
        int col, row, sx, sy;
        if (!chessPiece(it.f & 7, k, col, row)) continue;
        chessSquare(x, y, it.f & 128, col, row, sx, sy);
        pawn(sx, sy + Sz(1), k < 2);
      }
      break;
    case kPiece:  // (base) a pawn in the air; f 0: white, 1: gold
      pawn(x, y, it.f == 0);
      break;
    default: break;
  }
}

#undef B4
#undef PH
#undef HIGH_FIVE
#undef TURN
#undef RECT
#undef DOT
#undef TRI
#undef END

}  // namespace screens
