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

#include <string>
#include <vector>

#include "TFT_eSPI.h"
#include "fonts.h"
#include "miblo_overview.h"
#include "miblo_snapshot.h"
#include "platform/tft_canvas.h"
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

// ---------------- sample data ----------------

uint32_t gNow = 0;  // "now" in Unix seconds: 14:32 local time
Snapshot snap;

void session(const char* id, const char* name, SessionState st, const char* tool, const char* det, uint32_t ago,
             int ctx = 42, int64_t tok = 186000, const char* model = "Opus") {
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
  snap.h5 = {true, h5, gNow + 2 * 3600 + 10 * 60};
  snap.d7 = {true, d7, gNow + 3 * 86400 + 5 * 3600};
  snap.todayUsd = 12.40f;
}

void attention() {
  usage(62, 38);
  session("11111111", "api-pagamentos", SessionState::Perm, "Bash", "npm run migrate", 42);
  session("22222222", "app-mobile", SessionState::Question, "", "", 15);
  session("33333333", "landing-page", SessionState::Running, "Edit", "Hero.tsx", 192);
  session("44444444", "docs", SessionState::Done, "", "", 600);
}

void working() {
  usage(34, 21);
  session("33333333", "landing-page", SessionState::Running, "Edit", "Hero.tsx", 192);
  session("55555555", "worker", SessionState::Running, "Bash", "npm test", 18);
  session("66666666", "search-api", SessionState::Running, "Grep", "TODO", 65);
  session("11111111", "api-pagamentos", SessionState::Done, "", "", 300);
}

void idle() {
  usage(62, 38);
  session("11111111", "api-pagamentos", SessionState::Done, "", "", 300);
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

// ---------------- rendering ----------------

struct Shot {
  TFT_eSPI tft{240, 240};
  TftCanvas canvas{tft, {240, 240}, board::fonts::kStacks};
  Shot() {
    canvas.begin();
    screens::bind(canvas);
    screens::reset();
  }
};

std::string gDir;
int gCount = 0;

void save(Shot& s, const std::string& name) {
  const std::string base = gDir + "/" + name;
  if (!writePng(base + ".png", s.tft.pixels(), 240, 240, 1) ||
      !writePng(base + "@4x.png", s.tft.pixels(), 240, 240, 4)) {
    fprintf(stderr, "cannot write %s\n", base.c_str());
    exit(1);
  }
  gCount++;
}

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

  attention();
  { Shot s; screens::flash(L, AlertKind::Perm, "api-pagamentos", 0); save(s, "10-alert-flash-permission"); }
  { Shot s; screens::hero(L, snap, 0, AlertKind::Perm, false, clk, runs); save(s, "11-alert-permission"); }
  { Shot s; screens::hero(L, snap, 1, AlertKind::Question, false, clk, runs); save(s, "12-alert-question"); }
  { Shot s; screens::overview(L, snap, pager, 0, clk, false); save(s, "13-overview-needs-you"); }

  working();
  { Shot s; screens::overview(L, snap, pager, 0, clk, false); save(s, "14-overview-working"); }
  landingFinished(runs);
  { Shot s; screens::flash(L, AlertKind::Done, "landing-page", 0); save(s, "15-alert-flash-done"); }
  { Shot s; screens::hero(L, snap, 0, AlertKind::Done, false, clk, runs); save(s, "16-alert-finished"); }

  idle();
  { Shot s; screens::overview(L, snap, pager, 0, clk, false); save(s, "17-overview-all-done"); }
  attention();
  { Shot s; screens::limits(L, snap, clk); save(s, "18-limits"); }
  { Shot s; screens::sessions(L, snap, pager, 0, clk, false); save(s, "19-sessions"); }
  { Shot s; screens::overview(L, snap, pager, 0, clk, true); save(s, "20-overview-discreet"); }
  // At the recent pace the 5h window runs out in 1h20, before its reset in 2h10.
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
  screens::setMascotStyle(0);

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

void animateAll(Lang L) {
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
  miblo::RunTracker none;
  attention();
  alertClip(L, "alert-permission", AlertKind::Perm, "api-pagamentos", none);
  miblo::RunTracker runs;
  landingFinished(runs);
  alertClip(L, "alert-done", AlertKind::Done, "landing-page", runs);
}

}  // namespace

int main(int argc, char** argv) {
  int arg = 1;
  const bool animate = argc > 1 && strcmp(argv[1], "--animate") == 0;
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
  return 0;
}
