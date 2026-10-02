// Renders every Miblo screen to PNG on the computer, with the gadget's own drawing code: the real
// TftCanvas and u8g2 fonts over a framebuffer (shim/TFT_eSPI.h), fed with sample sessions.
// Each image is saved at 240x240 (the panel's pixels) and 4x (960x960, nearest neighbour).
// --animate renders short looping clips instead: 240x240 frames at 20 fps, drawn like the
// firmware loop does (one screen, only changed regions redrawn), which ffmpeg turns into mp4/gif.
//
//   make screenshots            ->  firmware/dist/screenshots/<lang>/*.png
//   make animations             ->  firmware/dist/animations/<lang>/*.mp4, *.gif
//   .pio/build/screenshots/program <out-dir> [lang ...]     (langs: en pt-BR es ...; default en pt-BR)
//   .pio/build/screenshots/program --animate <out-dir> [lang ...]
//                               ->  <out-dir>/<lang>/frames/<clip>/0000.png ...
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <algorithm>
#include <string>
#include <vector>

#include "TFT_eSPI.h"
#include "fonts.h"
#include "miblo_config.h"
#include "miblo_cues.h"
#include "miblo_occasions.h"
#include "miblo_overview.h"
#include "miblo_snapshot.h"
#include "platform/tft_canvas.h"
#include "miblo_mood.h"
#include "shots.h"
#include "ui_screens.h"

using miblo::AlertKind;
using miblo::Lang;
using miblo::S;
using miblo::SessionState;
using miblo::Snapshot;

namespace {

// ---------------- PNG (RGB, stored deflate: no zlib needed) ----------------

uint32_t crc32(const uint8_t* p, size_t n, uint32_t c = 0) {
  static uint32_t table[256];
  if (!table[1]) {
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t v = i;
      for (int k = 0; k < 8; k++) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1;
      table[i] = v;
    }
  }
  c = ~c;
  while (n--) c = table[(c ^ *p++) & 0xFF] ^ (c >> 8);
  return ~c;
}

void be32(std::vector<uint8_t>& o, uint32_t v) {
  for (int s = 24; s >= 0; s -= 8) o.push_back((uint8_t)(v >> s));
}

void chunk(std::vector<uint8_t>& png, const char* type, const std::vector<uint8_t>& data) {
  be32(png, (uint32_t)data.size());
  std::vector<uint8_t> td(type, type + 4);
  td.insert(td.end(), data.begin(), data.end());
  png.insert(png.end(), td.begin(), td.end());
  be32(png, crc32(td.data(), td.size()));
}

bool writePng(const std::string& path, const std::vector<uint16_t>& px, int w, int h, int scale) {
  const int W = w * scale, H = h * scale;
  std::vector<uint8_t> raw;
  raw.reserve((size_t)H * (W * 3 + 1));
  for (int y = 0; y < H; y++) {
    raw.push_back(0);  // filter: none
    for (int x = 0; x < W; x++) {
      const uint16_t c = px[(size_t)(y / scale) * w + x / scale];
      const int r = c >> 11, g = (c >> 5) & 63, b = c & 31;
      raw.push_back((uint8_t)((r << 3) | (r >> 2)));
      raw.push_back((uint8_t)((g << 2) | (g >> 4)));
      raw.push_back((uint8_t)((b << 3) | (b >> 2)));
    }
  }
  std::vector<uint8_t> z = {0x78, 0x01};
  for (size_t off = 0; off < raw.size() || off == 0; off += 65535) {
    const size_t len = raw.size() - off < 65535 ? raw.size() - off : 65535;
    z.push_back(off + len >= raw.size() ? 1 : 0);
    z.push_back((uint8_t)len);
    z.push_back((uint8_t)(len >> 8));
    z.push_back((uint8_t)~len);
    z.push_back((uint8_t)(~len >> 8));
    z.insert(z.end(), raw.begin() + off, raw.begin() + off + len);
    if (off + len >= raw.size()) break;
  }
  uint32_t a = 1, b = 0;
  for (uint8_t v : raw) {
    a = (a + v) % 65521;
    b = (b + a) % 65521;
  }
  be32(z, (b << 16) | a);
  std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  std::vector<uint8_t> ihdr;
  be32(ihdr, (uint32_t)W);
  be32(ihdr, (uint32_t)H);
  ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});  // 8-bit RGB
  chunk(png, "IHDR", ihdr);
  chunk(png, "IDAT", z);
  chunk(png, "IEND", {});
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return false;
  const bool ok = fwrite(png.data(), 1, png.size(), f) == png.size();
  return fclose(f) == 0 && ok;
}

}  // namespace

namespace shots {

// ---------------- sample data ----------------

uint32_t gNow = 0;  // "now" in Unix seconds: 14:32 local time
Snapshot snap;

void session(const char* id, const char* name, SessionState st, const char* tool, const char* det, uint32_t ago,
             int ctx, int64_t tok, const char* model) {
  miblo::SessionRow& r = snap.sessions[snap.count++];
  memset(&r, 0, sizeof(r));
  snprintf(r.id, sizeof(r.id), "%s", id);
  snprintf(r.name, sizeof(r.name), "%s", name);
  r.st = st;
  snprintf(r.tool, sizeof(r.tool), "%s", tool);
  snprintf(r.det, sizeof(r.det), "%s", det);
  r.since = gNow - ago;
  snprintf(r.model, sizeof(r.model), "%s", model);
  r.ctx = (int16_t)ctx;
  r.tok = tok;
}

void usage(uint8_t h5, uint8_t d7) {
  memset(&snap, 0, sizeof(snap));
  snap.now = gNow;
  snap.hasUsage = true;
  snap.h5 = {true, h5, gNow + 2 * 3600 + 10 * 60, 0};
  snap.d7 = {true, d7, gNow + 3 * 86400 + 5 * 3600, 0};
  snap.todayUsd = 12.40f;
}

void attention() {
  usage(62, 38);
  session("11111111", "checkout", SessionState::Perm, "Bash", "npm run migrate", 42);
  session("22222222", "app-mobile", SessionState::Question, "", "", 15);
  session("33333333", "landing-page", SessionState::Running, "Edit", "Hero.tsx", 192);
  session("44444444", "docs", SessionState::Done, "", "", 600);
}

void working() {
  usage(34, 21);
  session("33333333", "landing-page", SessionState::Running, "Edit", "Hero.tsx", 192);
  session("55555555", "worker", SessionState::Running, "Bash", "npm test", 18);
  session("66666666", "search-api", SessionState::Running, "Grep", "TODO", 65);
  session("11111111", "checkout", SessionState::Done, "", "", 300);
}

void idle() {
  usage(62, 38);
  session("11111111", "checkout", SessionState::Done, "", "", 300);
  session("22222222", "app-mobile", SessionState::Done, "", "", 120);
  session("44444444", "docs", SessionState::Idle, "", "", 900);
}

screens::Clock clock() {
  screens::Clock c{};
  c.valid = true;
  snprintf(c.hhmm, sizeof(c.hhmm), "14:32");
  c.epoch = gNow;
  return c;
}

}  // namespace shots

namespace {

// What the per-feature files (shots.h) share, used here unqualified.
using shots::attention;
using shots::clock;
using shots::gNow;
using shots::idle;
using shots::save;
using shots::session;
using shots::Shot;
using shots::snap;
using shots::usage;
using shots::working;

// ---------------- rendering ----------------

std::string gDir;
int gCount = 0;

// Screen care shifts the whole picture by up to 2 px (ui::ShiftCanvas): nothing may sit closer to
// an edge than kMinMargin, or it would be clipped. Full-width/height fills (bands, cards) don't
// count. --check makes any violation fail the run (CI).
constexpr int kMinMargin = 3;
std::vector<std::string> gMarginErrors;

void checkMargins(const std::vector<uint16_t>& px, int w, int h, const std::string& name, uint8_t warmth) {
  const uint16_t bg = ui::warmColor(ui::color::BG, ui::warmGains(warmth));
  std::vector<int> rowFill(h, 0), colFill(w, 0);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++)
      if (px[(size_t)y * w + x] != bg) rowFill[y]++, colFill[x]++;
  int left = w, top = h, right = -1, bottom = -1;
  for (int y = 0; y < h; y++) {
    if (rowFill[y] * 10 > w * 6) continue;  // a band across the screen
    for (int x = 0; x < w; x++) {
      if (colFill[x] * 10 > h * 6 || px[(size_t)y * w + x] == bg) continue;
      left = std::min(left, x), right = std::max(right, x);
      top = std::min(top, y), bottom = std::max(bottom, y);
    }
  }
  if (right < 0) return;
  const int m = std::min(std::min(left, top), std::min(w - 1 - right, h - 1 - bottom));
  if (m < kMinMargin) gMarginErrors.push_back(name + ": content " + std::to_string(m) + " px from an edge");
}

}  // namespace

namespace shots {

void save(Shot& s, const std::string& name) {
  checkMargins(s.tft.pixels(), 240, 240, gDir + "/" + name, s.shifted.warmth());
  const std::string base = gDir + "/" + name;
  if (!writePng(base + ".png", s.tft.pixels(), 240, 240, 1) ||
      !writePng(base + "@4x.png", s.tft.pixels(), 240, 240, 4)) {
    fprintf(stderr, "cannot write %s\n", base.c_str());
    exit(1);
  }
  gCount++;
}

}  // namespace shots

namespace {

// Every distinct expression of a mood's loop, in order (desk or disconnected).
template <typename Draw>
void mascotFrames(const std::string& prefix, screens::DeskMood mood, Draw draw) {
  std::vector<screens::MascotLook> seen;
  for (uint32_t ms = 0; ms < 60000; ms += 50) {
    const screens::MascotLook k = screens::deskLook(mood, true, ms);
    bool dup = false;
    for (const auto& s : seen) dup = dup || s == k;
    if (dup) continue;
    seen.push_back(k);
    Shot s;
    draw(ms);
    char name[96];
    snprintf(name, sizeof(name), "%s-%02zu", prefix.c_str(), seen.size());
    save(s, name);
  }
}

struct Mood {
  const char* name;
  uint8_t h5, d7;
  screens::DeskMood mood;
};
const Mood kMoods[] = {{"calm", 28, 12, screens::DeskMood::Calm},
                       {"watchful", 62, 38, screens::DeskMood::Watchful},
                       {"worried", 86, 44, screens::DeskMood::Worried},
                       {"scared", 97, 71, screens::DeskMood::Scared}};

// working(), then landing-page runs for 7:07 and finishes: the "took" line on the blue alert.
void landingFinished(miblo::RunTracker& runs) {
  working();
  snap.sessions[0].since = gNow - 7 * 60 - 12;
  runs.observe(snap);
  snap.sessions[0].st = SessionState::Done;
  snap.sessions[0].since = gNow - 5;
  runs.observe(snap);
}

void renderAll(Lang L) {
  const screens::Clock clk = clock();
  miblo::Pager pager(3, 5000);
  miblo::RunTracker runs;

  { Shot s; screens::boot(L, 0); save(s, "01-boot"); }
  { Shot s; screens::setup(L, "Miblo-Setup-4F2A"); save(s, "02-setup-qr"); }
  { Shot s; screens::welcome(L, "4827", "192.168.0.42"); save(s, "03-welcome"); }
  { Shot s; screens::paired(L, "MacBook-Pro", screens::t(L, S::ModeOverview), "miblo-4f2a"); save(s, "04-paired"); }
  { Shot s; screens::code(L, S::CodeUpdate, "4827", 287); save(s, "05-update-code"); }
  { Shot s; screens::updating(L, 64); save(s, "06-updating"); }
  { Shot s; screens::hardResetCountdown(L, 3); save(s, "07-hard-reset-countdown"); }
  { Shot s; screens::updateAvailable(L, "1.0.1", "1.1.0", 0); save(s, "08-update-available"); }

  attention();
  { Shot s; screens::flash(L, AlertKind::Perm, "checkout", 0); save(s, "10-alert-flash-permission"); }
  { Shot s; screens::hero(L, snap, 0, AlertKind::Perm, false, clk, runs); save(s, "11-alert-permission"); }
  { Shot s; screens::hero(L, snap, 1, AlertKind::Question, false, clk, runs); save(s, "12-alert-question"); }
  { Shot s; screens::overview(L, snap, pager, 0, clk, false); save(s, "13-overview-needs-you"); }
  // Meeting mode: the same screen with no session names or tools (app.cpp also sets discreet).
  screens::setAnonymous(true);
  screens::setMascotTie(true);
  { Shot s; screens::overview(L, snap, pager, 0, clk, true); save(s, "13-overview-needs-you-meeting"); }
  screens::setAnonymous(false);
  screens::setMascotTie(false);

  working();
  { Shot s; screens::overview(L, snap, pager, 0, clk, false); save(s, "14-overview-working"); }
  // "worker" has been running `npm test` for 1:42: the time runs beside the command. It has been
  // working for a bit longer than that ("2m"), not the "<1m" of the plain shot.
  snap.sessions[1].ts = gNow - 102;
  snap.sessions[1].since = gNow - 150;
  { Shot s; screens::overview(L, snap, pager, 0, clk, false); save(s, "14-overview-working-long-command"); }
  snap.sessions[1].ts = 0;
  landingFinished(runs);
  { Shot s; screens::flash(L, AlertKind::Done, "landing-page", 0); save(s, "15-alert-flash-done"); }
  { Shot s; screens::hero(L, snap, 0, AlertKind::Done, false, clk, runs); save(s, "16-alert-finished"); }

  idle();
  { Shot s; screens::overview(L, snap, pager, 0, clk, false); save(s, "17-overview-all-done"); }
  // At the current pace the 5h window runs out in 20 min: amber, "runs out ~14:52".
  { Shot s; screens::overview(L, snap, pager, 0, clk, false, gNow + 20 * 60); save(s, "17-overview-all-done-forecast-soon"); }
  // A second clock (settings: "Other time zone", here Lisbon) between the title and the clock.
  screens::setSecondClock("Lisboa", "18:32");
  { Shot s; screens::overview(L, snap, pager, 0, clk, false); save(s, "17-overview-all-done-second-zone"); }
  { Shot s; screens::desk(L, snap, clk, 0); save(s, "22-desk-second-zone"); }
  // The Desk's extras: a countdown (/miblo:countdown), the settings QR (wins the corner over the
  // second clock), confetti on the day itself; the countdown on the pet's sign.
  {
    char line[64];
    snprintf(line, sizeof(line), screens::t(L, S::CountdownDays), "Release", 3u);
    screens::setDeskExtras(line, "");
    { Shot s; screens::desk(L, snap, clk, 0); save(s, "22-desk-countdown"); }
    { Shot s; screens::roam(L, snap, clk, 0, screens::DeskMood::Calm); save(s, "42-pet-countdown"); }
    screens::setDeskExtras("", "http://192.168.0.42/");
    { Shot s; screens::desk(L, snap, clk, 0); save(s, "22-desk-qr"); }
    snprintf(line, sizeof(line), screens::t(L, S::CountdownToday), "Release");
    screens::setDeskExtras(line, "http://192.168.0.42/");
    { Shot s; screens::desk(L, snap, clk, 0); save(s, "22-desk-countdown-today-qr"); }
    screens::setDeskExtras("", "");
  }
  screens::setSecondClock("", "");
  // A long day (8 h of work): the cat yawns now and then.
  screens::setCatMood((uint8_t)miblo::CatMood::Tired);
  { Shot s; screens::desk(L, snap, clk, 44500); save(s, "22-desk-tired"); }
  screens::setCatMood((uint8_t)miblo::CatMood::Normal);
  attention();
  { Shot s; screens::limits(L, snap, clk); save(s, "18-limits"); }
  { Shot s; screens::sessions(L, snap, pager, 0, clk, false); save(s, "19-sessions"); }
  { Shot s; screens::overview(L, snap, pager, 0, clk, true); save(s, "20-overview-discreet"); }
  // At the current pace the 5h window runs out in 1h20 (15:52), before its reset in 2h10.
  usage(78, 41);
  { Shot s; screens::limits(L, snap, clk, gNow + 80 * 60); save(s, "21-limits-runs-out"); }
  { Shot s; screens::desk(L, snap, clk, 0, gNow + 80 * 60); save(s, "22-desk-runs-out"); }
  // The 5h window just reset after heavy use.
  usage(2, 41);
  snap.h5.reset = gNow + 5 * 3600;
  { Shot s; screens::limitReset(L, snap, clk, 0); save(s, "23-limit-freed"); }
  { Shot s; screens::limitReset(L, snap, clk, 1000); save(s, "23-limit-freed-cheer"); }
  // Today's summary.
  idle();
  snap.todayTurns = 14;
  snap.todayWorkSec = 3 * 3600 + 12 * 60;
  { Shot s; screens::summary(L, snap, clk); save(s, "24-today-summary"); }
  // The mascot's colours (settings page), on the desk screen.
  const char* styles[] = {"sphynx", "orange", "black", "grey"};
  for (uint8_t i = 0; i < 4; i++) {
    screens::setMascotStyle(i);
    Shot s;
    screens::desk(L, snap, clk, 0);
    save(s, std::string("25-mascot-") + styles[i]);
  }
  // The brand row's logo follows the mascot's colour.
  working();
  for (uint8_t i = 0; i < 4; i++) {
    screens::setMascotStyle(i);
    miblo::Pager p(3, 5000);
    Shot s;
    screens::overview(L, snap, p, 0, clk, false);
    save(s, std::string("26-logo-") + styles[i]);
  }
  screens::setMascotStyle(0);
  // Hats for special days, on the desk mascot.
  const char* hats[] = {"santa", "witch", "party"};
  for (uint8_t i = 0; i < 3; i++) {
    screens::setMascotAccessory(i + 1);
    Shot s;
    screens::desk(L, snap, clk, 0);
    save(s, std::string("27-hat-") + hats[i]);
  }
  screens::setMascotAccessory(0);
  // Greetings.
  {
    miblo::Config c;
    strcpy(c.owner, "Ana");
    char l1[64], l2[64];
    const struct {
      miblo::Greeting g;
      const char* name;
      uint8_t hat;
    } greets[] = {{miblo::Greeting::Named, "named", 0},
                  {miblo::Greeting::Morning, "morning", 0},
                  {miblo::Greeting::OwnerBirthday, "birthday", 3},
                  {miblo::Greeting::MibloBirthday, "miblo-birthday", 3},
                  {miblo::Greeting::Christmas, "christmas", 1}};
    for (const auto& g : greets) {
      screens::setMascotAccessory(g.hat);
      miblo::greetingLines(L, g.g, c.owner, "Tofu", l1, sizeof(l1), l2, sizeof(l2));
      Shot s;
      screens::hello(l1, l2, miblo::greetingIsParty(g.g), 0);
      save(s, std::string("09-hello-") + g.name);
    }
    screens::setMascotAccessory(0);
  }
  // Pet mode with other Miblos around: a hi, a nap together, and visits.
  {
    char note[96];
    snprintf(note, sizeof(note), screens::t(L, S::FriendHi), "Nina");
    Shot a;
    screens::roam(L, snap, clk, 20000, screens::DeskMood::Celebrate, note);
    save(a, "43-pet-friend-hi");
    snprintf(note, sizeof(note), screens::t(L, S::FriendNap), "Nina");
    Shot b;
    screens::roam(L, snap, clk, 20000, screens::DeskMood::Asleep, note);
    save(b, "43-pet-nap-together");
    Shot d;
    screens::roam(L, snap, clk, 20000, screens::DeskMood::Searching, nullptr, UINT32_MAX, true);
    save(d, "43-pet-computer-away");
    usage(34, 21);
    miblo::VisitView v;
    strcpy(v.name, "Nina");
    v.mascot = 1;
    const struct {
      miblo::VisitRole role;
      miblo::Gift gift;
      uint32_t ms;
      const char* name;
    } visits[] = {{miblo::VisitRole::Host, miblo::Gift::None, miblo::kVisitArriveMs + 1000, "host"},
                  {miblo::VisitRole::Host, miblo::Gift::Coffee, miblo::kVisitArriveMs + 1000, "host-coffee"},
                  {miblo::VisitRole::Host, miblo::Gift::Coffee, miblo::kVisitArriveMs + miblo::kVisitStayMs - 1000,
                   "host-coffee-given"},
                  {miblo::VisitRole::Visitor, miblo::Gift::None, miblo::kVisitArriveMs + 1000, "away"},
                  {miblo::VisitRole::Host, miblo::Gift::Duck, miblo::kVisitArriveMs + 3000, "duck-held"},
                  {miblo::VisitRole::Host, miblo::Gift::Duck, miblo::kVisitArriveMs + 12000, "duck"},
                  {miblo::VisitRole::Host, miblo::Gift::Pair, miblo::kVisitArriveMs + 5250, "pair"},
                  {miblo::VisitRole::Host, miblo::Gift::Review, miblo::kVisitArriveMs + 4000, "review"},
                  {miblo::VisitRole::Host, miblo::Gift::Bug, miblo::kVisitArriveMs + 6000, "bug"},
                  {miblo::VisitRole::Host, miblo::Gift::Bug, miblo::kVisitArriveMs + 15300, "bug-caught"},
                  {miblo::VisitRole::Host, miblo::Gift::Deploy, miblo::kVisitArriveMs + 1000, "deploy-pad"},
                  {miblo::VisitRole::Host, miblo::Gift::Deploy, miblo::kVisitArriveMs + 6000, "deploy-launch"}};
    for (const auto& x : visits) {
      v.role = x.role;
      v.gift = x.gift;
      v.ms = x.ms;
      Shot s;
      screens::visit(L, snap, clk, v);
      save(s, std::string("44-visit-") + x.name);
    }
  }
  // Group visits (1:2, 1:3), guests from the right (the default) and from the left.
  {
    usage(34, 21);
    const struct {
      uint8_t extra, side;
      miblo::Gift gift;
      const char* name;
    } groups[] = {{1, 0, miblo::Gift::None, "44-visit-group-2"},
                  {2, 0, miblo::Gift::Pair, "44-visit-group-3"},
                  {2, 1, miblo::Gift::Deploy, "44-visit-group-3-left"}};
    for (const auto& g : groups) {
      miblo::VisitView v;
      strcpy(v.name, "Nina");
      v.mascot = 1;
      v.role = miblo::VisitRole::Host;
      v.gift = g.gift;
      v.extra = g.extra;
      v.extraMascot[0] = 2;
      v.extraMascot[1] = 3;
      v.ms = miblo::kVisitArriveMs + 5000;
      Shot sh;
      screens::visit(L, snap, clk, v, g.side);
      save(sh, g.name);
    }
  }
  // Every visit activity, as the host sees it, with 1 and 3 guests, at two moments of the stay:
  // 44-visit-<activity>-<1|3>-<a|b>.
  {
    usage(34, 21);
    static const char* const kGiftNames[] = {
        "none",     "coffee",  "duck",      "pair",     "review",     "bug",      "deploy",  "highfive",
        "pingpong", "dance",   "pizza",     "cake",     "merge",      "standup",  "hackathon", "selfie",
        "chess",    "game",    "gossip",    "toast",    "movie",      "blocks",   "brainstorm", "pomodoro",
        "hotfix",   "tests",   "notfound",  "shipit",   "sprint",     "origami",  "nostalgia", "panic",
        "picnic",   "fishing", "umbrella",  "canphone", "kite"};
    static_assert(sizeof(kGiftNames) / sizeof(kGiftNames[0]) == (size_t)miblo::Gift::Count, "one name per gift");
    for (int g = 0; g < (int)miblo::Gift::Count; g++) {
      for (const int guests : {1, 3}) {
        for (const uint32_t at : {5000u, 12000u}) {
          miblo::VisitView v;
          strcpy(v.name, "Nina");
          v.mascot = 1;
          v.role = miblo::VisitRole::Host;
          v.gift = (miblo::Gift)g;
          v.extra = (uint8_t)(guests - 1);
          v.extraMascot[0] = 2;
          v.extraMascot[1] = 3;
          v.ms = miblo::kVisitArriveMs + at;
          Shot sh;
          screens::visit(L, snap, clk, v);
          save(sh, std::string("44-visit-") + kGiftNames[g] + (guests == 1 ? "-1-" : "-3-") +
                       (at == 5000u ? "a" : "b"));
        }
      }
    }
  }
  // Pet mode antics: two moments of each, and the sign going down on the floor.
  {
    idle();
    static const char* const kAnticNames[] = {"",        "bat",      "spill",   "cursor",  "nap",     "sneeze",
                                              "peek",    "heart",    "glasses", "wave",    "laptop",  "tail",
                                              "stretch", "lick",     "fly",     "yarn",    "mug",     "box",
                                              "keys",    "laser",    "bubbles", "fish",    "duck",    "coffee",
                                              "butterfly", "balloon", "plane",  "bowl",    "deploy",  "cucumber",
                                              "blanket"};
    static_assert(sizeof(kAnticNames) / sizeof(kAnticNames[0]) == screens::kAnticCount + 1, "one name per antic");
    bool putDown = false;
    for (uint32_t c = 1; c <= screens::kAnticCount; c++) {
      const uint32_t t0 = c * screens::kAnticEveryMs;
      const screens::RoamAntic a = screens::roamAntic(t0, nullptr);
      const uint32_t play = screens::anticOnSign(a) ? 0 : screens::kAnticPutMs;
      // Two moments where the antic reads; Peek's are the middle of each peek (out at 3000 on the
      // left and at 6000 on the right, 1.5 s each), Heart's with the heart on (it beats until 6000).
      uint32_t moments[] = {play + 2500, play + 6000};
      if (a == screens::RoamAntic::Peek) moments[0] = 3750, moments[1] = 6750;
      if (a == screens::RoamAntic::Heart) moments[0] = 1500, moments[1] = 4900;
      for (int i = 0; i < 2; i++) {
        Shot sh;
        screens::roam(L, snap, clk, t0 + moments[i], screens::DeskMood::Calm);
        save(sh, std::string("45-pet-") + kAnticNames[(uint8_t)a] + "-" + std::to_string(i + 1));
      }
      if (!putDown && play) {
        putDown = true;
        Shot sh;
        screens::roam(L, snap, clk, t0 + screens::kAnticPutMs / 2, screens::DeskMood::Calm);
        save(sh, "45-pet-putting-sign-down");
      }
    }
  }
  // Pet mode (long idle, screen left on).
  for (uint32_t ms : {0u, 20000u, 60000u}) {
    Shot s;
    screens::roam(L, snap, clk, ms, screens::DeskMood::Calm);
    save(s, "42-pet-mode-" + std::to_string(ms / 1000));
  }
  // The blue light filter at the old three strengths (0: off, for comparison), on the Overview and
  // in pet mode, then at the slider's low end. The old levels' files keep their names: the
  // strengths they map to give exactly the old colours.
  for (uint8_t level = 0; level <= 3; level++) {
    const uint8_t strength = level ? miblo::blueStrengthForLevel(level) : 0;
    {
      Shot s(strength);
      screens::overview(L, snap, pager, 0, clk, false);
      save(s, "60-blue-filter-overview-" + std::to_string(level));
    }
    {
      Shot s(strength);
      screens::roam(L, snap, clk, 20000, screens::DeskMood::Calm);
      save(s, "60-blue-filter-pet-" + std::to_string(level));
    }
  }
  {
    Shot s(15);
    screens::overview(L, snap, pager, 0, clk, false);
    save(s, "60-blue-filter-overview-15pct");
  }

  for (const Mood& m : kMoods) {
    idle();
    usage(m.h5, m.d7);
    mascotFrames(std::string("30-desk-") + m.name, m.mood,
                 [&](uint32_t ms) { screens::desk(L, snap, clk, ms); });
  }
  mascotFrames("40-disconnected-searching", screens::DeskMood::Searching, [&](uint32_t ms) {
    screens::disconnected(L, clk, "192.168.0.42", "miblo-4f2a", "4827", ms, 0);
  });
  mascotFrames("41-disconnected-asleep", screens::DeskMood::Asleep, [&](uint32_t ms) {
    screens::disconnected(L, clk, "192.168.0.42", "miblo-4f2a", "4827", ms, 3600000);
  });
  // Daily life: the shared overlays, then one file per feature (shots.h).
  shots::renderDaily(L);
  shots::renderFocus(L);
  shots::renderAlerts(L);
  shots::renderDayRhythm(L);
  shots::renderNotes(L);
  shots::renderCues(L);
  shots::renderLook(L);
}

// ---------------- animations ----------------

constexpr uint32_t kFrameMs = 50;  // 20 fps: the firmware redraws at ~10 fps, expressions last >= 100 ms
int gClips = 0;

// A clip's frames, drawn on one screen the way the firmware loop does it: the screen function is
// called every frame and only redraws the regions that changed.
struct Clip {
  Shot shot;
  std::string dir;
  int frames = 0;
  explicit Clip(const std::string& name) : dir(gDir + "/frames/" + name) {
    mkdir((gDir + "/frames").c_str(), 0755);
    mkdir(dir.c_str(), 0755);
    gClips++;
  }
  void frame() {
    char path[64];
    snprintf(path, sizeof(path), "/%04d.png", frames++);
    if (!writePng(dir + path, shot.tft.pixels(), 240, 240, 1)) {
      fprintf(stderr, "cannot write %s%s\n", dir.c_str(), path);
      exit(1);
    }
  }
};

// Length of one loop of the mood's expressions (a multiple of kFrameMs, so the clip loops seamlessly).
uint32_t moodLoopMs(screens::DeskMood mood) {
  for (uint32_t p = kFrameMs; p <= 60000; p += kFrameMs) {
    bool same = true;
    for (uint32_t ms = 0; same && ms < 60000; ms += 10)
      same = screens::deskLook(mood, true, ms) == screens::deskLook(mood, true, ms + p);
    if (same) return p;
  }
  return 12000;
}

// Alert: the flash for the default 1.5 s, then ~4 s of the hero screen.
void alertClip(Lang L, const char* name, AlertKind kind, const char* who, const miblo::RunTracker& runs) {
  const screens::Clock clk = clock();
  Clip c(name);
  for (uint32_t ms = 0; ms < 4 * screens::kFlashPhaseMs; ms += kFrameMs) {
    screens::flash(L, kind, who, ms);
    c.frame();
  }
  screens::reset();  // a new screen, as the firmware does on every screen change
  for (uint32_t ms = 0; ms < 4000; ms += kFrameMs) {
    screens::hero(L, snap, 0, kind, false, clk, runs);
    c.frame();
  }
}

// Daily life (shots.h): a few seconds of each of the most visual new screens, for the README.
void animateDaily(Lang L) {
  const screens::Clock clk = clock();
  const bool pt = L == Lang::PtBR || L == Lang::PtPT;
  constexpr uint32_t kMin = 60000;
  {
    // Focus: the cat with headphones typing inside the ring, the time left ticking down.
    Clip c("focus");
    const uint32_t left = (18 * 60 + 42) * 1000u, len = 25 * kMin;
    for (uint32_t ms = 0; ms < 6000; ms += kFrameMs) {
      screens::focus(L, clk, miblo::FocusPhase::Focus, 2, 4, left - ms, len, gNow + left / 1000, 1000 + ms);
      c.frame();
    }
  }
  {
    // A session needs you during a focus round: the amber mark over the focus screen.
    attention();
    Clip c("waiting-mark");
    const uint32_t left = (12 * 60 + 5) * 1000u, len = 25 * kMin;
    for (uint32_t ms = 0; ms < 5000; ms += kFrameMs) {
      screens::focus(L, clk, miblo::FocusPhase::Focus, 3, 4, left - ms, len, gNow + left / 1000, 1000 + ms);
      screens::waitingMark(L, "checkout", 1);
      c.frame();
    }
  }
  {
    // A reminder comes due: three slow pulses, then the cat holds it up.
    Clip c("reminder");
    const uint32_t pulses = miblo::cuePulses(miblo::CueKind::Reminder) * miblo::kCuePulseMs;
    for (uint32_t ms = 0; ms < pulses; ms += kFrameMs) {
      screens::cue(miblo::CueKind::Reminder, ms);
      c.frame();
    }
    screens::reset();
    for (uint32_t ms = 0; ms < 4000; ms += kFrameMs) {
      screens::note(L, miblo::NoteKind::Reminder, pt ? "ligar pro cliente" : "call the client", clk, 1000 + ms);
      c.frame();
    }
  }
  {
    // A note for whoever walks by, held up by the cat.
    Clip c("say");
    for (uint32_t ms = 0; ms < 5000; ms += kFrameMs) {
      screens::note(L, miblo::NoteKind::Say, pt ? "volto em 10 min" : "back in 10 min", clk, 1000 + ms);
      c.frame();
    }
  }
  {
    // The timer: the big countdown with the hourglass.
    Clip c("timer");
    const uint32_t left = (6 * 60 + 42) * 1000u;
    for (uint32_t ms = 0; ms < 5000; ms += kFrameMs) {
      screens::timer(L, clk, left - ms, 10 * kMin, 1000 + ms);
      c.frame();
    }
  }
  {
    // A long task finished: the fanfare (6 s of its 8).
    Clip c("fanfare");
    for (uint32_t ms = 0; ms < 6000; ms += kFrameMs) {
      screens::fanfare(L, "app-mobile", 23 * 60 + 7, ms);
      c.frame();
    }
  }
  {
    // Meeting mode: the desk with the tie and the badge, then an anonymous alert.
    screens::setMascotTie(true);
    idle();
    usage(34, 21);
    Clip c("meeting");
    for (uint32_t ms = 0; ms < 3000; ms += kFrameMs) {
      screens::desk(L, snap, clk, ms);
      screens::meetingBadge(L);
      c.frame();
    }
    attention();
    screens::reset();
    for (uint32_t ms = 0; ms < 2 * screens::kFlashPhaseMs; ms += kFrameMs) {
      screens::flash(L, AlertKind::Perm, "checkout", ms, 0, true);
      c.frame();
    }
    screens::reset();
    miblo::RunTracker none;
    for (uint32_t ms = 0; ms < 3000; ms += kFrameMs) {
      screens::hero(L, snap, 0, AlertKind::Perm, true, clk, none, true);
      screens::meetingBadge(L);
      c.frame();
    }
    screens::setMascotTie(false);
  }
  {
    // Friday the 13th: the black cat crossing pet mode.
    miblo::Snapshot none{};
    Clip c("black-cat");
    for (uint32_t ms = 0; ms < miblo::kPasserbyMs; ms += kFrameMs) {
      screens::passerby(L, none, clk, ms);
      c.frame();
    }
  }
  {
    // The desk with a countdown over the gauges and a second clock in the corner.
    idle();
    usage(62, 38);
    char fmt[48], line[64];
    miblo::tr(L, S::CountdownDays, fmt, sizeof(fmt));
    snprintf(line, sizeof(line), fmt, pt ? "lan\xC3\xA7""amento" : "release", 3u);
    screens::setDeskExtras(line, "");
    screens::setSecondClock("Lisboa", "18:32");
    Clip c("desk-countdown");
    const uint32_t loop = moodLoopMs(screens::DeskMood::Watchful);
    for (uint32_t ms = 0; ms < loop; ms += kFrameMs) {
      screens::desk(L, snap, clk, ms);
      c.frame();
    }
    screens::setDeskExtras("", "");
    screens::setSecondClock("", "");
  }
}

void animateAll(Lang L) {
  animateDaily(L);
  const screens::Clock clk = clock();
  for (const Mood& m : kMoods) {
    idle();
    usage(m.h5, m.d7);
    Clip c(std::string("desk-") + m.name);
    const uint32_t loop = moodLoopMs(m.mood);
    for (uint32_t ms = 0; ms < loop; ms += kFrameMs) {
      screens::desk(L, snap, clk, ms);
      c.frame();
    }
  }
  {
    // The "Waiting..." dots (2.4 s cycle) restart with the mascot's loop.
    Clip c("disconnected");
    const uint32_t loop = moodLoopMs(screens::DeskMood::Searching);
    for (uint32_t ms = 0; ms < loop; ms += kFrameMs) {
      screens::disconnected(L, clk, "192.168.0.42", "miblo-4f2a", "4827", ms, 0);
      c.frame();
    }
  }
  {
    // "Limit freed": the mascot cheering over one loop.
    usage(2, 41);
    snap.h5.reset = gNow + 5 * 3600;
    Clip c("limit-freed");
    const uint32_t loop = moodLoopMs(screens::DeskMood::Celebrate);
    for (uint32_t ms = 0; ms < loop; ms += kFrameMs) {
      screens::limitReset(L, snap, clk, ms);
      c.frame();
    }
  }
  {
    // Pet mode: the mascot wandering (30 s, drawn frame by frame like the firmware: no trail).
    idle();
    Clip c("pet-mode");
    for (uint32_t ms = 0; ms < 30000; ms += kFrameMs) {
      screens::roam(L, snap, clk, ms, screens::DeskMood::Calm);
      c.frame();
    }
  }
  {
    // A visit, as the host sees it: the friend walks in, they play, it leaves (30 s).
    idle();
    usage(34, 21);
    miblo::VisitView v;
    strcpy(v.name, "Nina");
    v.mascot = 1;
    v.role = miblo::VisitRole::Host;
    v.gift = miblo::Gift::Coffee;
    Clip c("visit-host");
    for (uint32_t ms = 0; ms < miblo::kVisitMs; ms += kFrameMs) {
      v.ms = ms;
      screens::visit(L, snap, clk, v);
      c.frame();
    }
  }
  {
    // A visit, as the visitor's own screen shows it: out to the right, back from the right.
    miblo::VisitView v;
    strcpy(v.name, "Nina");
    v.mascot = 1;
    v.role = miblo::VisitRole::Visitor;
    Clip c("visit-away");
    for (uint32_t ms = 0; ms < miblo::kVisitMs; ms += kFrameMs) {
      v.ms = ms;
      screens::visit(L, snap, clk, v);
      c.frame();
    }
  }
  {
    // Pet mode: the coffee spilled on the sign (the whole antic, from its first cycle).
    idle();
    for (uint32_t c = 1; c < 64; c++) {
      uint32_t at;
      if (screens::roamAntic(c * screens::kAnticEveryMs, &at) != screens::RoamAntic::Spill) continue;
      Clip clip("pet-spill");
      for (uint32_t ms = 0; ms < screens::kAnticMs; ms += kFrameMs) {
        screens::roam(L, snap, clk, c * screens::kAnticEveryMs + ms, screens::DeskMood::Calm);
        clip.frame();
      }
      break;
    }
  }
  {
    // A visit about a Friday deploy, as the host sees it (the whole visit).
    usage(34, 21);
    miblo::VisitView v;
    strcpy(v.name, "Nina");
    v.mascot = 1;
    v.role = miblo::VisitRole::Host;
    v.gift = miblo::Gift::Deploy;
    Clip c("visit-deploy");
    for (uint32_t ms = 0; ms < miblo::kVisitMs; ms += kFrameMs) {
      v.ms = ms;
      screens::visit(L, snap, clk, v);
      c.frame();
    }
  }
  {
    // Visits from the other sides (config friendsSide): a guest coming in from the left, and from
    // above (it crosses the whole screen), as the host sees them.
    const struct {
      uint8_t side;
      const char* name;
    } sides[] = {{1, "visit-from-left"}, {2, "visit-from-above"}, {3, "visit-away-below"}};
    for (const auto& sd : sides) {
      usage(34, 21);
      miblo::VisitView v;
      strcpy(v.name, "Nina");
      v.mascot = 1;
      v.role = sd.side == 3 ? miblo::VisitRole::Visitor : miblo::VisitRole::Host;
      v.gift = miblo::Gift::Pair;
      screens::reset();
      Clip c(sd.name);
      for (uint32_t ms = 0; ms < miblo::kVisitMs; ms += kFrameMs) {
        v.ms = ms;
        screens::visit(L, snap, clk, v, sd.side);
        c.frame();
      }
    }
  }
  {
    // Happy birthday, with the party hat and confetti.
    miblo::Config cfg;
    strcpy(cfg.owner, "Ana");
    char l1[64], l2[64];
    miblo::greetingLines(L, miblo::Greeting::OwnerBirthday, cfg.owner, "Tofu", l1, sizeof(l1), l2, sizeof(l2));
    screens::setMascotAccessory(3);
    Clip c("hello-birthday");
    for (uint32_t ms = 0; ms < 6000; ms += kFrameMs) {
      screens::hello(l1, l2, true, ms);
      c.frame();
    }
    screens::setMascotAccessory(0);
  }
  miblo::RunTracker none;
  attention();
  alertClip(L, "alert-permission", AlertKind::Perm, "checkout", none);
  miblo::RunTracker runs;
  landingFinished(runs);
  alertClip(L, "alert-done", AlertKind::Done, "landing-page", runs);
}

}  // namespace

int main(int argc, char** argv) {
  int arg = 1;
  const bool check = argc > arg && strcmp(argv[arg], "--check") == 0;
  if (check) arg++;
  const bool animate = argc > arg && strcmp(argv[arg], "--animate") == 0;
  if (animate) arg++;
  // Layers, as on a healthy gadget (shim/TFT_eSPI.h); MIBLO_LOW_MEMORY=1 draws like a gadget with no
  // heap to spare (no layers), to check that path.
  const char* low = getenv("MIBLO_LOW_MEMORY");
  ESP.heap = low && low[0] == '1' ? 0 : 1u << 20;
  const std::string out = argc > arg ? argv[arg] : (animate ? "animations" : "screenshots");
  std::vector<std::string> langs;
  for (int i = arg + 1; i < argc; i++) langs.push_back(argv[i]);
  if (langs.empty()) langs = {"en", "pt-BR"};

  setenv("TZ", "America/Sao_Paulo", 1);
  tzset();
  struct tm t = {};
  t.tm_year = 2026 - 1900;
  t.tm_mon = 8;
  t.tm_mday = 29;
  t.tm_hour = 14;
  t.tm_min = 32;
  t.tm_isdst = -1;
  gNow = (uint32_t)mktime(&t);

  mkdir(out.c_str(), 0755);
  for (const std::string& code : langs) {
    Lang L;
    if (!miblo::langFromCode(code.c_str(), L)) {
      fprintf(stderr, "unknown language: %s\n", code.c_str());
      return 2;
    }
    gDir = out + "/" + code;
    mkdir(gDir.c_str(), 0755);
    if (animate) animateAll(L);
    else renderAll(L);
  }
  if (animate) printf("%d clips (240x240 frames, %u fps) in %s\n", gClips, 1000 / kFrameMs, out.c_str());
  else printf("%d screenshots (each at 240x240 and 960x960) in %s\n", gCount, out.c_str());
  for (const std::string& e : gMarginErrors) fprintf(stderr, "margin: %s\n", e.c_str());
  if (check && !gMarginErrors.empty()) {
    fprintf(stderr, "%zu screen(s) closer than %d px to an edge: the pixel shift would clip them\n",
            gMarginErrors.size(), kMinMargin);
    return 1;
  }
  return 0;
}
