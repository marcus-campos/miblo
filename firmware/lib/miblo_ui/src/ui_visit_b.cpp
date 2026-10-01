// ui_visit_b.cpp — visits between Miblos: video game, gossip, a toast, movie time, stacking
// blocks, brainstorm, pomodoro, hotfix, tests passing, 404 (and their props, kinds 96..127).
#include "ui_visit_kit.h"

namespace screens {

namespace {

// Our props (VisitItem::kind). The anchor of each is written next to its drawing below. An
// item's top two bits of `f` carry the cats' size (see put()), so a prop shrinks with the cats
// in a group; the lower six are its frame / state.
constexpr uint8_t kGameTv = kItemsB;        // a tiny TV with two pixel players
constexpr uint8_t kPad = 97;            // a game controller
constexpr uint8_t kMovieScreen = 98;    // a cinema screen
constexpr uint8_t kPopcorn = 99;        // a bucket of popcorn
constexpr uint8_t kTower = 100;         // a tower of blocks (standing, wobbling or fallen)
constexpr uint8_t kBlock = 101;         // one block on its way to the tower
constexpr uint8_t kBulb = 102;          // a light bulb, off or lit
constexpr uint8_t kTomato = 103;        // a tomato kitchen timer
constexpr uint8_t kKeyboard = 104;      // a flat little keyboard
constexpr uint8_t kServer = 105;        // a server rack: fine, smoking, burning or foamed
constexpr uint8_t kExtinguisher = 106;  // a fire extinguisher, spraying or not
constexpr uint8_t kChecks = 107;        // eight test results: green checks and pending dots
constexpr uint8_t kLens = 108;          // a magnifying glass
constexpr uint8_t kQuestion = 109;      // a big "?"
constexpr uint8_t kSign404 = 110;       // a white card saying "404"
static_assert(kSign404 < kItemsC, "kinds 96..127 are ours");

constexpr uint16_t kYellow = 0xFFE0;     // #ffff00 a lit bulb
constexpr uint16_t kCream = 0xFF9A;      // #fff0d0 popcorn
constexpr uint16_t kGlass = 0xB71F;      // #b0e0ff the magnifier's lens
constexpr uint16_t kBrown = 0x8AC6;      // #8a5a34 its handle
constexpr uint16_t kDarkGreen = 0x0400;  // #008000 grass on the cinema screen
constexpr uint16_t kBlockColors[] = {color::RED, color::AMBER, color::GREEN, color::BLUE, color::VIOLET, color::CORAL};

// ---- scripts ----

// What a script works with: the stage, and lengths in the cats' units.
struct Scene {
  const VisitStage& s;
  VisitFrame& f;
  int d;  // from the guest towards the host: +1 or -1
  // The cats' design units (their 96-unit box, 2 * half px wide).
  int u(int v) const { return v * s.half / 48; }
  // Prop units: what drawVisitItemB draws as 1 for these cats (1 px for a pair on 240x240).
  int z(int v) const { return v * s.half / 40; }
  // Centre y of the free space over the cats' ears.
  int above() const { return (s.top + s.cy - u(44)) / 2; }
  // One of our props, sized like the cats: 0 a pair (half 40), 1 three cats (33), 2 four (27).
  void put(uint8_t kind, int x, int y, uint8_t fr = 0) {
    const uint8_t size = s.half >= Sz(40) ? 0 : s.half >= Sz(33) ? 1 : 2;
    addItem(f, kind, x, y, (uint8_t)((fr & 63) | size << 6));
  }
  // One of ours over the heads, at full size: in a group the cats are smaller, so there is more
  // room up there, not less.
  void putTop(uint8_t kind, int x, int y, uint8_t fr = 0) { addItem(f, kind, x, y, (uint8_t)(fr & 63)); }
  // A prop from ui_main.cpp (drawn at the screen's scale).
  void prop(uint8_t kind, int x, int y, uint8_t fr = 0) { addItem(f, kind, x, y, fr); }
  // The "..." of someone talking, from `x` out towards side `dir`.
  void dots(int x, int dir, int y, uint8_t n) { prop(vprop::Dots, dir > 0 ? x : x - Sz(12), y, n); }
};

MascotLook look(int gx, int gy, Eyes e, Paws p = Paws::Down, uint16_t extras = 0, int dy = 0) {
  return MascotLook{0, (int8_t)dy, (int8_t)gx, (int8_t)gy, e, p, extras};
}
Paws reach(int dir) { return dir > 0 ? Paws::ReachRight : Paws::ReachLeft; }
Paws typing(uint32_t t, uint32_t every) { return (t / every) % 2 ? Paws::TapLeft : Paws::TapRight; }
int hop(uint32_t t, uint32_t every, uint32_t shift) { return (t / every + shift) % 2 ? -4 : 0; }

// The happy ending most activities share: hopping in turns, eyes "^ ^", a heart for the host.
void cheer(Scene& c, Paws paws = Paws::Down) {
  const uint32_t t = c.s.t;
  c.f.them = look(3 * c.d, 0, Eyes::Happy, paws, 0, hop(t, 350, 0));
  c.f.me = look(-3 * c.d, 0, Eyes::Happy, paws, kHeart, hop(t, 350, 1));
}

// #11 Video game: a controller each, two pixels jumping on a tiny TV; the guest wins (stars), the
// host sulks, then they laugh.
void game(Scene& c) {
  const VisitStage& s = c.s;
  const uint32_t t = s.t;
  const int d = c.d;
  const bool playing = t >= 1500 && t < 13000;
  uint8_t tv = (uint8_t)((t / 150) % 16);
  if (t >= 13000) tv |= 16 | (d > 0 ? 32 : 0);  // game over: the guest's side of the screen won
  if (t < 1500) {
    // controllers in hand, a happy look at each other (the default)
  } else if (playing) {
    const uint16_t tense = t >= 10500 ? kSweat : 0;  // the last level
    const Eyes e = t >= 10500 ? Eyes::Wide : Eyes::Open;
    c.f.them = look(2 * d, -3, e, typing(t, 180), tense);
    c.f.me = look(-2 * d, -3, e, typing(t + 90, 180), tense);
  } else if (t < 16000) {
    c.f.them = look(3 * d, -1, Eyes::Happy, Paws::Up, kStars, hop(t, 300, 0));  // won
    c.f.me = look(0, 3, Eyes::Open, Paws::Down, kGrumpy);                      // sulking
  } else {
    cheer(c);
  }
  c.putTop(kGameTv, s.mid, c.above(), tv);
  const int py = s.cy + c.u(30);
  c.put(kPad, s.guestX, py, playing && (t / 180) % 2);
  c.put(kPad, s.hostX, py, playing && ((t + 90) / 180) % 2);
}

// #12 Gossip: the guest leans over and whispers ("..."), the host's eyes go wide and its mouth
// opens, then both laugh to tears.
void gossip(Scene& c) {
  const VisitStage& s = c.s;
  const uint32_t t = s.t;
  const int d = c.d;
  if (t < 1200) return;  // the default happy hello
  if (t < 7000) {
    c.f.them = look(3 * d, 0, Eyes::Open, Paws::Lick);  // a paw at the mouth, whispering
    c.f.them.dx = (int8_t)(5 * d);                      // leaning in
    c.f.me = look(-3 * d, 1, Eyes::Open);
    c.f.me.dx = (int8_t)(-2 * d);
    c.dots(s.guestX + d * c.u(36), d, s.cy + c.u(18), (uint8_t)(1 + (t / 350) % 3));
  } else if (t < 11000) {
    c.f.them = look(3 * d, 0, Eyes::Happy, Paws::Down, kTongue);
    c.f.them.dx = (int8_t)(2 * d);
    if (t < 9000) c.f.me = look(-3 * d, 0, Eyes::Wide, Paws::Down, kMouthO | kAlarm, t < 7600 ? -4 : 0);
    else c.f.me = look(-3 * d, 0, Eyes::Wide, Paws::Lick, kMouthWide);  // "no way!"
  } else {
    const int shake = (t / 150) % 2 ? -2 : 0;
    c.f.them = look(3 * d, 0, Eyes::Happy, Paws::Down, kMouthWide, shake);
    c.f.me = look(-3 * d, 0, Eyes::Happy, Paws::Down, kMouthWide | (t >= 15500 ? kHeart : 0), -2 - shake);
    // tears of laughter beside the outer eyes
    const int ty = s.cy + c.u(12) + ((t / 300) % 2 ? c.u(4) : 0);
    c.prop(vprop::Drop, s.guestX - d * c.u(26), ty);
    c.prop(vprop::Drop, s.hostX + d * c.u(26), ty);
  }
}

// #13 A toast: mugs up, a clink in the middle with a spark, a sip with eyes closed, another clink.
void toast(Scene& c) {
  const VisitStage& s = c.s;
  const uint32_t t = s.t;
  const int d = c.d;
  const int holdY = s.cy + c.u(18), upY = s.cy - c.u(28);
  const int gHold = s.guestX + d * c.u(38), hHold = s.hostX - d * c.u(38);
  // Clinking: the mugs (a body with the handle on its right) meet over the middle.
  const int left = s.mid - Sz(13), right = s.mid + Sz(7);
  const int gClink = d > 0 ? left : right, hClink = d > 0 ? right : left;
  int gx = gHold, hx = hHold, gy = holdY, hy = holdY;
  bool spark = false;
  // A raise-and-clink from `t0`: 1500 ms up, 800 ms clinking, 700 ms back.
  auto clink = [&](uint32_t t0) {
    const uint32_t p = t - t0;
    gy = hy = lerpTo(holdY, upY, p, 1500);
    if (p >= 1500 && p < 2300) {
      gx = gClink;
      hx = hClink;
      spark = true;
    }
    c.f.them = look(2 * d, -2, p >= 1500 ? Eyes::Happy : Eyes::Open, Paws::Up);
    c.f.me = look(-2 * d, -2, p >= 1500 ? Eyes::Happy : Eyes::Open, Paws::Up);
  };
  if (t < 2000) {
    c.f.them.paws = reach(d);
    c.f.me.paws = reach(-d);
  } else if (t < 5000) {
    clink(2000);
  } else if (t < 11000) {
    // A sip each, in turns: the mug at the mouth, eyes closed.
    const bool gSip = ((t - 5000) / 1500) % 2 == 0;
    const int mouth = s.cy + c.u(24);
    if (gSip) {
      gx = s.guestX + d * c.u(4), gy = mouth;
      c.f.them = look(0, 0, Eyes::Closed, Paws::Lick);
      c.f.me = look(-3 * d, 0, Eyes::Happy, reach(-d));
    } else {
      hx = s.hostX - d * c.u(4), hy = mouth;
      c.f.me = look(0, 0, Eyes::Closed, Paws::Lick);
      c.f.them = look(3 * d, 0, Eyes::Happy, reach(d));
    }
  } else if (t < 14000) {
    clink(11000);
  } else {
    cheer(c);
    c.f.them.paws = reach(d);
    c.f.me.paws = reach(-d);
  }
  c.prop(vprop::Mug, gx, gy);
  c.prop(vprop::Mug, hx, hy);
  if (spark) c.prop(vprop::Burst, s.mid, upY - Sz(12));
}

// #14 Movie time: a screen above flickering, a bucket of popcorn between them; they take turns
// eating, a scary scene makes them jump (and hide their eyes), then they laugh.
void movie(Scene& c) {
  const VisitStage& s = c.s;
  const uint32_t t = s.t;
  const int d = c.d;
  const bool scary = t >= 10000 && t < 13000;
  c.putTop(kMovieScreen, s.mid, c.above(), (uint8_t)((t / 250) % 16 | (scary ? 16 : 0)));
  const uint8_t left = (uint8_t)(t < 6000 ? 3 : t < 12000 ? 2 : 1);
  c.put(kPopcorn, s.mid, s.cy + c.u(46), (uint8_t)(left | (t >= 10000 && t < 11500 ? 4 : 0)));
  if (t < 1500) return;
  if (t < 10000) {
    // Watching (eyes up on the screen), each dipping into the bucket in turn.
    auto watcher = [&](uint32_t p, int toMid) {
      const uint32_t q = p % 3000;
      const Paws paws = q < 700 ? reach(toMid) : q < 1500 ? Paws::Lick : Paws::Down;
      return look(q < 700 ? 2 * toMid : toMid, q < 700 ? 3 : -3, Eyes::Open, paws);
    };
    c.f.them = watcher(t, d);
    c.f.me = watcher(t + 1500, -d);
  } else if (t < 11500) {
    const int shiver = (t / 100) % 2 ? 2 : -2;
    c.f.them = look(2 * d, -3, Eyes::Wide, Paws::Down, kFluffed, t < 10600 ? -5 : 0);
    c.f.them.dx = (int8_t)shiver;
    c.f.me = look(-2 * d, -3, Eyes::Wide, Paws::Down, kFluffed | kSweat, t < 10600 ? -5 : 0);
    c.f.me.dx = (int8_t)-shiver;
  } else if (t < 13000) {
    c.f.them = look(0, 0, Eyes::Closed, Paws::Cover, kSweat);
    c.f.me = look(0, 0, Eyes::Closed, Paws::Cover, kSweat);
  } else if (t < 15500) {
    c.f.them = look(3 * d, 0, Eyes::Happy, Paws::Down, kMouthWide, (t / 200) % 2 ? -2 : 0);
    c.f.me = look(-3 * d, 0, Eyes::Happy, Paws::Down, kMouthWide, (t / 200) % 2 ? 0 : -2);
  } else {
    c.f.them = look(2 * d, -3, Eyes::Happy);
    c.f.me = look(-2 * d, -3, Eyes::Happy, Paws::Down, kHeart);
  }
}

// #15 Stacking blocks: in turns they put six coloured blocks on a tower; it wobbles and falls;
// they jump, then laugh.
void blocks(Scene& c) {
  const VisitStage& s = c.s;
  const uint32_t t = s.t;
  const int d = c.d;
  const int base = s.cy + c.u(46), bh = c.z(8);
  constexpr uint32_t kTurn = 1800, kBuilt = 6 * kTurn, kFall = 13500, kLaugh = 15500;
  if (t < kBuilt) {
    const uint32_t k = t / kTurn, p = t % kTurn;
    const bool guestTurn = k % 2 == 0;
    const int catX = guestTurn ? s.guestX : s.hostX, toMid = guestTurn ? d : -d;
    const uint8_t n = (uint8_t)(k + (p >= 1000 ? 1 : 0));
    c.put(kTower, s.mid, base, n);
    MascotLook placer = look(2 * toMid, 1, Eyes::Open, Paws::Down);
    MascotLook other = look(-2 * toMid, 1, Eyes::Open, Paws::Down);
    if (p < 1000) {  // carrying the next block from the paw to the top of the tower
      placer.paws = reach(toMid);
      const int x = lerpTo(catX + toMid * c.u(38), s.mid, p, 1000);
      const int y = lerpTo(s.cy + c.u(26), base - (int)k * bh - bh / 2, p, 1000);
      c.put(kBlock, x, y, (uint8_t)k);
    } else if (p < 1400) {
      placer.eyes = other.eyes = Eyes::Happy;
    }
    c.f.them = guestTurn ? placer : other;
    c.f.me = guestTurn ? other : placer;
  } else if (t < kFall) {
    const uint8_t lean = (t / 220) % 2 ? 1 : 2;  // swaying left and right
    c.put(kTower, s.mid, base, (uint8_t)(6 | lean << 3));
    c.f.them = look(2 * d, -2, Eyes::Wide, Paws::Down, kSweat);
    c.f.me = look(-2 * d, -2, Eyes::Wide, Paws::Down, kSweat);
  } else {
    c.put(kTower, s.mid, base, 6 | 32);  // fallen
    if (t < kLaugh) {
      const int jump = t < kFall + 700 ? -5 : 0;
      c.f.them = look(2 * d, 2, Eyes::Wide, Paws::Up, kFluffed, jump);
      c.f.me = look(-2 * d, 2, Eyes::Wide, Paws::Up, kFluffed, jump);
    } else {
      cheer(c);
      c.f.them.extras |= kMouthWide;
      c.f.me.extras |= kMouthWide;
    }
  }
}

// #16 Brainstorm: both think (eyes up, "..."), a bulb lights over the guest, then over the host;
// they cheer under two bulbs.
void brainstorm(Scene& c) {
  const VisitStage& s = c.s;
  const uint32_t t = s.t;
  const int d = c.d;
  const int y = c.above();
  if (t < 1500) return;
  const bool gIdea = t >= 7000, hIdea = t >= 10000;
  auto thinking = [&](uint32_t p) {
    return look((p / 1100) % 2 ? 3 : -3, -3, Eyes::Open, Paws::Lick);  // a paw at the chin
  };
  auto idea = [&](uint32_t since, int toOther) {
    return since < 600 ? look(0, -3, Eyes::Wide, Paws::Up, 0, -4) : look(toOther, 0, Eyes::Happy, Paws::Up);
  };
  if (t >= 13000) {
    cheer(c, Paws::Up);
  } else {
    c.f.them = gIdea ? idea(t - 7000, 3 * d) : thinking(t);
    c.f.me = hIdea ? idea(t - 10000, -3 * d) : thinking(t + 550);
  }
  if (gIdea) c.putTop(kBulb, s.guestX, y, 1);
  else c.prop(vprop::Dots, s.guestX - Sz(6), y, (uint8_t)(1 + (t / 400) % 3));
  if (hIdea) c.putTop(kBulb, s.hostX, y, 1);
  else c.prop(vprop::Dots, s.hostX - Sz(6), y, (uint8_t)(1 + (t / 400 + 1) % 3));
}

// #17 Pomodoro: the host winds a tomato timer, both type while its hand goes round, it rings
// (red), they jump and cheer.
void pomodoro(Scene& c) {
  const VisitStage& s = c.s;
  const uint32_t t = s.t;
  const int d = c.d;
  constexpr uint32_t kWound = 1800, kRing = 14000, kDone = 15800;
  uint8_t timer = 0;
  if (t < kWound) {
    timer = (uint8_t)((t / 150) % 8);  // winding
    c.f.me = look(-2 * d, 2, Eyes::Open, reach(-d));
    c.f.them = look(2 * d, 2, Eyes::Open);
  } else if (t < kRing) {
    timer = (uint8_t)((t - kWound) * 8 / (kRing - kWound));
    const bool gGlance = (t / 3000) % 2 && t % 3000 < 600, hGlance = (t / 3000) % 2 == 0 && t % 3000 < 600;
    c.f.them = look(gGlance ? 2 * d : 0, gGlance ? 1 : 3, Eyes::Open, typing(t, 200));
    c.f.me = look(hGlance ? -2 * d : 0, hGlance ? 1 : 3, Eyes::Open, typing(t + 100, 200));
  } else if (t < kDone) {
    timer = (uint8_t)(8 | ((t / 120) % 2 ? 16 : 0));  // ringing
    const int jump = (t / 150) % 2 ? -5 : 0;
    c.f.them = look(2 * d, 1, Eyes::Wide, Paws::Up, kAlarm, jump);
    c.f.me = look(-2 * d, 1, Eyes::Wide, Paws::Up, kAlarm, -5 - jump);
  } else {
    cheer(c, Paws::Up);
  }
  c.put(kTomato, s.mid, s.cy + c.u(30), timer);
  const int ky = s.cy + c.u(34);
  const bool working = t >= kWound && t < kRing;
  c.put(kKeyboard, s.guestX, ky, working && (t / 200) % 2);
  c.put(kKeyboard, s.hostX, ky, working && ((t + 100) / 200) % 2);
}

// #18 Hotfix: the server between them starts smoking, then burns; the host grabs an extinguisher
// and puts it out; both sweaty and relieved.
void hotfix(Scene& c) {
  const VisitStage& s = c.s;
  const uint32_t t = s.t;
  const int d = c.d;
  constexpr uint32_t kSmoke = 2500, kFire = 5000, kGrab = 7500, kSpray = 8500, kOut = 10000, kCalm = 11500,
                     kRelief = 15000;
  const uint8_t state = t < kSmoke ? 0 : t < kFire ? 1 : t < kOut ? 2 : 3;
  c.put(kServer, s.mid, s.cy + c.u(46), (uint8_t)((t / 200) % 8 | state << 3));
  if (t >= kGrab) {
    const bool spraying = t >= kSpray && t < kCalm;
    const uint8_t e = (uint8_t)((-d > 0 ? 0 : 1) | (spraying ? 2 : 0) | ((t / 100) % 2 ? 4 : 0));
    c.put(kExtinguisher, s.hostX - d * c.u(30), s.cy + c.u(22), e);
  }
  const int shiver = (t / 100) % 2 ? 2 : -2;
  if (t < 1500) {
    // all fine: the default happy look
  } else if (t < kSmoke) {
    c.f.them = look(2 * d, 2, Eyes::Open);
    c.f.me = look(-2 * d, 2, Eyes::Open);
  } else if (t < kFire) {
    c.f.them = look(2 * d, 1, Eyes::Wide, Paws::Down, kSweat);
    c.f.me = look(-2 * d, 1, Eyes::Wide, Paws::Down, kSweat);
  } else if (t < kGrab) {
    c.f.them = look(2 * d, 1, Eyes::Wide, Paws::Cover, kFluffed);
    c.f.them.dx = (int8_t)shiver;
    c.f.me = look(-2 * d, 1, Eyes::Wide, Paws::Down, kFluffed | kAlarm);
  } else if (t < kCalm) {
    c.f.them = look(2 * d, 1, Eyes::Wide, Paws::Down, kSweat);
    c.f.me = look(-3 * d, 1, Eyes::Open, reach(-d), kGrumpy);  // determined
  } else if (t < kRelief) {
    c.f.them = look(2 * d, 2, Eyes::Sleepy, Paws::Down, kSweat | kMouthO);
    c.f.me = look(-2 * d, 2, Eyes::Sleepy, Paws::Down, kSweat | kMouthO);
  } else {
    cheer(c);
    c.f.them.extras |= kSweat;
    c.f.me.extras |= kSweat;
  }
}

// #19 Tests passing: green checks appear one by one over the middle; on the last both jump.
void tests(Scene& c) {
  const VisitStage& s = c.s;
  const uint32_t t = s.t;
  const int d = c.d;
  constexpr uint32_t kStart = 1500, kEach = 1600, kAll = kStart + 8 * kEach;  // 14300
  const uint8_t n = (uint8_t)(t < kStart ? 0 : t >= kAll ? 8 : (t - kStart) / kEach);
  c.putTop(kChecks, s.mid, c.above(), (uint8_t)(n | ((t / 300) % 2 ? 16 : 0)));
  if (t < kAll) {
    const bool fresh = t >= kStart + kEach && (t - kStart) % kEach < 500;  // a check just went green
    const bool last = n == 7;
    const Eyes e = last ? Eyes::Wide : fresh ? Eyes::Happy : Eyes::Open;
    const uint16_t ex = last ? kSweat : 0;
    c.f.them = look(2 * d, -3, e, Paws::Down, ex, fresh && n % 2 ? -3 : 0);
    c.f.me = look(-2 * d, -3, e, Paws::Down, ex, fresh && n % 2 == 0 ? -3 : 0);
  } else if (t < kAll + 1300) {
    const int jump = (t / 160) % 2 ? -5 : 0;
    c.f.them = look(2 * d, -2, Eyes::Happy, Paws::Up, 0, jump);
    c.f.me = look(-2 * d, -2, Eyes::Happy, Paws::Up, 0, jump);
  } else {
    cheer(c, Paws::Up);
  }
}

// #20 404: a "404" pops up; the guest searches with a magnifying glass while the host looks one
// way and the other; nothing found, a "?" over both, then a laugh.
void notFound(Scene& c) {
  const VisitStage& s = c.s;
  const uint32_t t = s.t;
  const int d = c.d;
  constexpr uint32_t kSearch = 3000, kGiveUp = 13000, kShrug = 15500;
  const uint8_t handle = -d > 0 ? 0 : 1;  // the handle points back at the guest
  if (t < kSearch) {
    c.put(kSign404, s.mid, c.above());
    const Eyes e = t < 800 ? Eyes::Open : Eyes::Wide;
    c.f.them = look(2 * d, -3, e, Paws::Down, t < 800 ? 0 : kMouthO);
    c.f.me = look(-2 * d, -3, e, Paws::Down, t < 800 ? 0 : kMouthO);
  } else if (t < kGiveUp) {
    // The lens sweeps from beside the guest to over the host, and back.
    const int from = s.guestX - d * c.u(14), to = s.hostX - d * c.u(6);
    const int span = (to - from) * d;
    const int lx = from + d * shuttle(t - kSearch, 4000, span);
    const int ly = s.cy + c.u(12) + shuttle(t, 700, c.u(4));
    c.put(kLens, lx, ly, handle);
    const int side = lx > s.guestX ? 1 : -1;
    c.f.them = look(3 * side, 2, Eyes::Open, reach(side));
    c.f.me = look((t / 1000) % 2 ? 3 : -3, 1, Eyes::Open, (t / 2000) % 2 ? Paws::Lick : Paws::Down);
  } else {
    c.put(kLens, s.guestX + d * c.u(34), s.cy + c.u(26), handle);  // put down
    const int qy = c.above() + Sz(2);
    c.putTop(kQuestion, s.guestX, qy);
    c.putTop(kQuestion, s.hostX, qy);
    if (t < kShrug) {
      c.f.them = look(3 * d, 0, Eyes::Open, reach(d));
      c.f.me = look(-3 * d, 0, Eyes::Open);
    } else {
      c.f.them = look(3 * d, 0, Eyes::Happy, Paws::Down, kMouthWide, hop(t, 350, 0));
      c.f.me = look(-3 * d, 0, Eyes::Happy, Paws::Down, kMouthWide, hop(t, 350, 1));
    }
  }
}

}  // namespace

bool visitActivityB(miblo::Gift g, const VisitStage& s, VisitFrame& f) {
  Scene c{s, f, s.toHost};
  switch (g) {
    case miblo::Gift::Game: game(c); return true;
    case miblo::Gift::Gossip: gossip(c); return true;
    case miblo::Gift::Toast: toast(c); return true;
    case miblo::Gift::Movie: movie(c); return true;
    case miblo::Gift::Blocks: blocks(c); return true;
    case miblo::Gift::Brainstorm: brainstorm(c); return true;
    case miblo::Gift::Pomodoro: pomodoro(c); return true;
    case miblo::Gift::Hotfix: hotfix(c); return true;
    case miblo::Gift::Tests: tests(c); return true;
    case miblo::Gift::NotFound: notFound(c); return true;
    default: return false;
  }
}

// ---- drawing ----

namespace {

// Prop units for an item: 1 is 1 px for a pair of cats on 240x240, less in a group (the size in
// the top two bits of `f`), and never 0 for a non-zero length.
struct Pen {
  int n;  // the cats' half size in px
  int operator()(int v) const {
    const int a = (v < 0 ? -v : v) * n / 40;
    const int r = v && a < 1 ? 1 : a;
    return v < 0 ? -r : r;
  }
};

void drawGameTv(const Pen& p, int x, int y, uint8_t f) {  // (centre) players on a black screen
  const int fr = f & 15;
  C().fillRoundRect(x - p(24), y - p(15), p(48), p(30), p(3), color::FAINT);
  C().fillRect(x - p(21), y - p(12), p(42), p(24), color::BLACK);
  const int ground = y + p(9);
  C().fillRect(x - p(21), ground, p(42), p(1), color::GREEN);
  const int lx = x - p(13), rx = x + p(13) - p(5);  // left edges of the two players
  if (f & 16) {  // game over: the winner jumps with a crown, the other lies flat
    const bool leftWon = f & 32;
    const int wx = leftWon ? lx : rx, ox = leftWon ? rx : lx;
    const int wy = ground - p(5) - (fr % 4 < 2 ? p(5) : 0);
    C().fillRect(wx, wy, p(5), p(5), leftWon ? color::CORAL : color::BLUE);
    C().fillTriangle(wx, wy - 1, wx + p(5), wy - 1, wx + p(2), wy - p(4), color::AMBER);
    C().fillRect(ox - p(1), ground - p(2), p(7), p(2), color::DIM);
    return;
  }
  auto jump = [&](int k) { return p(2 * (k % 8 < 4 ? k % 8 : 8 - k % 8)); };  // 0..8 up
  C().fillRect(lx, ground - p(5) - jump(fr), p(5), p(5), color::CORAL);
  C().fillRect(rx, ground - p(5) - jump(fr + 4), p(5), p(5), color::BLUE);
  const int bx = x - p(6) + p((fr < 8 ? fr : 15 - fr) * 10 / 7);  // the ball between them
  C().fillRect(bx, y - p(6), p(2), p(2), color::WHITE);
}

void drawPad(const Pen& p, int x, int y, uint8_t f) {  // (centre) a white controller; f 1: a button pressed
  C().fillRoundRect(x - p(11), y - p(5), p(22), p(10), p(4), color::WHITE);
  C().fillRect(x - p(9), y - p(1), p(6), p(2), color::DIM);  // the cross
  C().fillRect(x - p(7), y - p(3), p(2), p(6), color::DIM);
  C().fillCircle(x + p(4), y + p(1), p(2), f & 1 ? color::AMBER : color::RED);
  C().fillCircle(x + p(8), y - p(1), p(2), color::BLUE);
}

void drawMovieScreen(const Pen& p, int x, int y, uint8_t f) {  // (centre) a film; f & 16: the scary scene
  const int fr = f & 15;
  C().fillRoundRect(x - p(24), y - p(13), p(48), p(26), p(2), color::FAINT);
  const int ix = x - p(22), iy = y - p(11), iw = p(44), ih = p(22);
  if (f & 16) {
    C().fillRect(ix, iy, iw, ih, fr % 5 == 0 ? color::WHITE : color::BLACK);  // lightning now and then
    if (fr % 5) {
      C().fillCircle(x - p(5), y - p(2), p(2), color::RED);
      C().fillCircle(x + p(5), y - p(2), p(2), color::RED);
    }
    return;
  }
  C().fillRect(ix, iy, iw, ih, fr % 6 == 0 ? color::VIOLET : color::BLUE);  // the sky, flickering
  C().fillRect(ix, y + p(4), iw, ih - p(15), kDarkGreen);
  C().fillCircle(x + p(15), y - p(5), p(3), color::WHITE);
  C().fillCircle(x - p(15) + p(fr * 2), y + p(1) - (fr % 2 ? p(1) : 0), p(3), color::AMBER);  // the hero
}

void drawPopcorn(const Pen& p, int x, int y, uint8_t f) {  // (bottom centre) f & 3: how full; f & 4: jumping out
  static const int8_t kPuff[4][2] = {{0, -17}, {-5, -15}, {5, -15}, {-1, -19}};
  const int n = 1 + (f & 3);
  for (int i = 0; i < n; i++) C().fillCircle(x + p(kPuff[i][0]), y + p(kPuff[i][1]), p(3), kCream);
  if (f & 4) {
    static const int8_t kFly[4][2] = {{-10, -26}, {9, -28}, {-3, -31}, {4, -23}};
    for (const auto& k : kFly) C().fillCircle(x + p(k[0]), y + p(k[1]), p(2), kCream);
  }
  C().fillRect(x - p(6), y - p(14), p(12), p(14), color::WHITE);
  C().fillTriangle(x - p(8), y - p(14), x - p(6), y - p(14), x - p(6), y - 1, color::WHITE);
  C().fillTriangle(x + p(8), y - p(14), x + p(6), y - p(14), x + p(6), y - 1, color::WHITE);
  for (int i = -5; i <= 3; i += 4) C().fillRect(x + p(i), y - p(14), p(2), p(14), color::RED);
}

void drawBlock(const Pen& p, int cx, int bottom, int i) {  // (bottom centre) one block, colour i
  const int h = p(8);
  C().fillRect(cx - p(6), bottom - h, p(12), h > 4 ? h - 1 : h, kBlockColors[i % 6]);
}

void drawTower(const Pen& p, int x, int y, uint8_t f) {  // (bottom centre) f & 7 blocks; (f >> 3) & 3 lean; f & 32 fallen
  const int n = f & 7;
  if (f & 32) {  // scattered on the floor
    static const int8_t kFloor[7][2] = {{-18, 0}, {-6, 0}, {6, 0}, {18, 0}, {-12, -8}, {12, -8}, {0, -16}};
    for (int i = 0; i < n; i++) drawBlock(p, x + p(kFloor[i][0]), y + p(kFloor[i][1]), i);
    return;
  }
  const int lean = ((f >> 3) & 3) == 1 ? -1 : ((f >> 3) & 3) == 2 ? 1 : 0;
  for (int i = 0; i < n; i++) drawBlock(p, x + lean * i * p(1), y - i * p(8), i);
}

void drawBulb(const Pen& p, int x, int y, uint8_t f) {  // (centre of the glass) f 1: lit, with rays
  const bool lit = f & 1;
  if (lit) {
    C().fillRect(x - p(1), y - p(12), p(2), p(3), color::AMBER);
    C().fillRect(x - p(12), y - p(1), p(3), p(2), color::AMBER);
    C().fillRect(x + p(9), y - p(1), p(3), p(2), color::AMBER);
    C().fillRect(x - p(9), y - p(9), p(2), p(2), color::AMBER);
    C().fillRect(x + p(7), y - p(9), p(2), p(2), color::AMBER);
  }
  C().fillCircle(x, y, p(6), lit ? kYellow : color::FAINT);
  C().fillRect(x - p(3), y + p(4), p(6), p(5), color::MUTED);
  C().fillRect(x - p(3), y + p(6), p(6), p(1), color::DIM);
  if (lit) C().fillCircle(x - p(2), y - p(2), p(1), color::WHITE);
}

void drawTomato(const Pen& p, int x, int y, uint8_t f) {  // (centre) f & 7: the hand; f & 8: ringing (f & 16 shakes)
  const bool ring = f & 8;
  const int cx = x + (ring ? (f & 16 ? p(1) : -p(1)) : 0);
  if (ring) {
    C().fillRect(x - p(16), y - p(5), p(2), p(3), color::AMBER);
    C().fillRect(x - p(17), y + p(1), p(3), p(2), color::AMBER);
    C().fillRect(x + p(14), y - p(5), p(2), p(3), color::AMBER);
    C().fillRect(x + p(14), y + p(1), p(3), p(2), color::AMBER);
  }
  C().fillCircle(cx, y, p(10), color::RED);
  C().fillTriangle(cx - p(6), y - p(8), cx, y - p(10), cx - p(1), y - p(6), color::GREEN);
  C().fillTriangle(cx + p(6), y - p(8), cx, y - p(10), cx + p(1), y - p(6), color::GREEN);
  C().fillRect(cx - p(1), y - p(12), p(2), p(3), color::GREEN);
  C().fillCircle(cx, y + p(1), p(5), color::WHITE);
  static const int8_t kHand[8][2] = {{0, -4}, {3, -3}, {4, 0}, {3, 3}, {0, 4}, {-3, 3}, {-4, 0}, {-3, -3}};
  const int h = f & 7, w = p(2) < 2 ? 2 : p(2);
  C().wideLine(cx, y + p(1), cx + p(kHand[h][0]), y + p(1) + p(kHand[h][1]), w, ring ? color::RED : color::PUPIL,
               color::WHITE);
}

void drawKeyboard(const Pen& p, int x, int y, uint8_t f) {  // (centre) f 1: a key pressed on the left, 0 on the right
  C().fillRoundRect(x - p(13), y - p(4), p(26), p(8), p(2), color::DIM);
  const int pressed = f & 1 ? 1 : 4;
  for (int row = 0; row < 2; row++)
    for (int k = 0; k < 6; k++)
      C().fillRect(x + p(-11 + 4 * k), y + (row ? p(1) : -p(3)), p(3), p(2),
                   row == 0 && k == pressed ? color::AMBER : color::TEXT);
}

void drawServer(const Pen& p, int x, int y, uint8_t f) {  // (bottom centre) (f >> 3) & 3: 0 fine, 1 smoke, 2 fire, 3 foam
  const int fr = f & 7, state = (f >> 3) & 3, top = y - p(28);
  C().fillRoundRect(x - p(9), top, p(18), p(28), p(2), color::DIM);
  for (int i = 0; i < 3; i++) {
    const int sy = top + p(3) + i * p(8);
    C().fillRect(x - p(7), sy, p(14), p(5), color::BLACK);
    if ((fr + i) % 3 == 0) continue;  // a blinking LED (few colours: a strip's layer has 16)
    const uint16_t led = state == 1 ? color::AMBER : state == 2 ? color::RED : color::GREEN;
    C().fillRect(x + p(3), sy + p(1), p(3), p(3), led);
  }
  switch (state) {
    case 1: {  // smoke rising
      const int rise = p(fr % 4);
      C().fillCircle(x - p(3), top - p(4) - rise, p(3), color::MUTED);
      C().fillCircle(x + p(3), top - p(10) - rise, p(4), color::MUTED);
      break;
    }
    case 2: {  // flames
      const int flick = fr % 2 ? p(2) : -p(2);
      C().fillTriangle(x - p(11), top + p(2), x - p(3), top, x - p(9) - flick, top - p(10), color::RED);
      C().fillTriangle(x + p(3), top, x + p(11), top + p(2), x + p(9) - flick, top - p(10), color::RED);
      C().fillTriangle(x - p(9), top, x + p(9), top, x + flick, top - p(18) - (fr % 3 ? 0 : p(2)), color::RED);
      C().fillTriangle(x - p(6), top, x + p(6), top, x - flick, top - p(11), color::AMBER);
      C().fillTriangle(x - p(3), top, x + p(3), top, x, top - p(5), color::WHITE);
      break;
    }
    case 3:  // foam all over it
      C().fillCircle(x - p(5), top + p(1), p(4), color::WHITE);
      C().fillCircle(x + p(3), top - p(1), p(5), color::WHITE);
      C().fillCircle(x + p(8), top + p(4), p(3), color::WHITE);
      C().fillCircle(x - p(8), top + p(9), p(3), color::WHITE);
      break;
    default: break;
  }
}

void drawExtinguisher(const Pen& p, int x, int y, uint8_t f) {  // (centre of the cylinder) f & 1: nozzle left; f & 2 spraying
  const int dir = f & 1 ? -1 : 1, jit = f & 4 ? p(1) : 0;
  C().fillRect(x - p(2), y - p(11), p(4), p(3), color::DIM);  // the valve
  const int w = p(2) < 2 ? 2 : p(2);
  C().wideLine(x, y - p(10), x + dir * p(8), y - p(12), w, color::DIM, color::BG);  // the hose
  C().fillTriangle(x + dir * p(8), y - p(12), x + dir * p(12), y - p(14), x + dir * p(12), y - p(10), color::DIM);
  C().fillRoundRect(x - p(4), y - p(8), p(8), p(16), p(3), color::RED);
  C().fillRect(x - p(4), y - p(1), p(8), p(3), color::WHITE);
  if (f & 2) {
    C().fillCircle(x + dir * p(15), y - p(12) + jit, p(2), color::WHITE);
    C().fillCircle(x + dir * p(19), y - p(12) - jit, p(3), color::WHITE);
    C().fillCircle(x + dir * p(24), y - p(12) + jit, p(3), color::WHITE);
  }
}

void drawChecks(const Pen& p, int x, int y, uint8_t f) {  // (centre) f & 15 green of 8; f & 16: the next one blinks
  const int n = f & 15;
  for (int i = 0; i < 8; i++) {
    const int cx = x + p((2 * (i % 4) - 3) * 13) / 2, cy = y + p((2 * (i / 4) - 1) * 13) / 2;
    if (i < n) check(cx, cy, p(11), color::GREEN);
    else if (i == n && (f & 16)) C().fillCircle(cx, cy, p(2), color::AMBER);
    else C().fillCircle(cx, cy, p(1), color::FAINT);
  }
}

void drawLens(const Pen& p, int x, int y, uint8_t f) {  // (centre of the lens) f & 1: handle down-left, else down-right
  const int hd = f & 1 ? -1 : 1, w = p(3) < 2 ? 2 : p(3);
  C().wideLine(x + hd * p(6), y + p(6), x + hd * p(12), y + p(12), w, kBrown, color::BG);
  C().fillCircle(x, y, p(9), color::MUTED);
  C().fillCircle(x, y, p(7), kGlass);
  C().fillCircle(x - p(3), y - p(3), p(2), color::WHITE);
}

void drawQuestion(const Pen& p, int x, int y) {  // (centre) a big amber "?"
  const uint16_t c = color::AMBER;
  C().fillRect(x - p(6), y - p(14), p(12), p(4), c);  // the hook
  C().fillRect(x - p(6), y - p(14), p(4), p(6), c);
  C().fillRect(x + p(3), y - p(14), p(4), p(11), c);
  C().fillRect(x - p(1), y - p(6), p(8), p(4), c);
  C().fillRect(x - p(1), y - p(6), p(4), p(9), c);  // the stem
  C().fillRect(x - p(1), y + p(6), p(4), p(4), c);  // the dot
}

void drawSign404(int x, int y) {  // (centre) a white card, "404" in red (screen scale, like "LGTM")
  C().fillRoundRect(x - Sz(17), y - Sz(8), Sz(34), Sz(16), Sz(3), color::WHITE);
  C().text(x, y + Sz(5), "404", Font::SmallBold, color::RED, Align::Center, Sz(32));
}

}  // namespace

void drawVisitItemB(const VisitItem& it) {
  static const uint8_t kHalf[] = {40, 33, 27, 27};
  const Pen p{Sz(kHalf[it.f >> 6])};
  const uint8_t f = it.f & 63;
  const int x = it.x, y = it.y;
  switch (it.kind) {
    case kGameTv: drawGameTv(p, x, y, f); break;
    case kPad: drawPad(p, x, y, f); break;
    case kMovieScreen: drawMovieScreen(p, x, y, f); break;
    case kPopcorn: drawPopcorn(p, x, y, f); break;
    case kTower: drawTower(p, x, y, f); break;
    case kBlock: drawBlock(p, x, y + p(4), f); break;  // (centre)
    case kBulb: drawBulb(p, x, y, f); break;
    case kTomato: drawTomato(p, x, y, f); break;
    case kKeyboard: drawKeyboard(p, x, y, f); break;
    case kServer: drawServer(p, x, y, f); break;
    case kExtinguisher: drawExtinguisher(p, x, y, f); break;
    case kChecks: drawChecks(p, x, y, f); break;
    case kLens: drawLens(p, x, y, f); break;
    case kQuestion: drawQuestion(p, x, y); break;
    case kSign404: drawSign404(x, y); break;
    default: break;
  }
}

}  // namespace screens
