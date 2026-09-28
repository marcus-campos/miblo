#include <stdio.h>
#include <string.h>

#include "miblo_format.h"
#include "miblo_version.h"
#include "ui_screens.h"

namespace screens {

using miblo::hashInt;
using miblo::hashStr;
using miblo::kHashSeed;
using miblo::S;
using ui::Align;
using ui::Font;
namespace color = ui::color;

static ui::Canvas& C() { return canvas(); }

void boot(Lang lang, uint8_t frame) {
  if (region(0, hashInt(kHashSeed, frame % 3), X(74), Y(54), Sz(92), Sz(92))) mascot(X(120), Y(100), frame);
  if (region(1, hashInt(kHashSeed, (uint32_t)lang), 0, Y(150), X(240), Y(70))) {
    C().text(X(120), Y(176), "Miblo", Font::Title, color::TEXT, Align::Center, X(240));
    C().text(X(120), Y(204), t(lang, S::Connecting), Font::Small, color::MUTED, Align::Center, X(232));
  }
  // Controller-requested: firmware version, small and muted, near the bottom.
  if (region(2, hashInt(kHashSeed, 2u), 0, Y(224), X(240), Y(16))) {
    char ver[16];
    snprintf(ver, sizeof(ver), "v%s", MIBLO_FW_VERSION);
    C().text(X(120), Y(236), ver, Font::Small, color::FAINT, Align::Center, X(232));
  }
}

void setup(Lang lang, const char* apSsid, bool wrongPassword) {
  uint32_t h = hashStr(hashInt(hashInt(kHashSeed, (uint32_t)lang), wrongPassword), apSsid);
  if (!region(0, h, 0, 0, X(240), Y(240))) return;
  if (wrongPassword) {
    C().text(X(120), Y(26), t(lang, S::WrongPassword), Font::BodyBold, color::RED, Align::Center, X(232));
  } else {
    C().text(X(120), Y(28), t(lang, S::Hello), Font::Title, color::TEXT, Align::Center, X(232));
  }
  C().text(X(120), Y(48), t(lang, S::ScanPhone), Font::Small, color::MUTED, Align::Center, X(232));
  char payload[64];
  snprintf(payload, sizeof(payload), "WIFI:S:%s;;", apSsid);
  const int scale = Sz(4) < 2 ? 2 : Sz(4);
  const int size = (29 + 4) * scale;  // QR versão 3 (29 módulos) + margem de 2 módulos
  qr(payload, (X(240) - size) / 2, Y(56), scale);
  C().text(X(120), Y(206), t(lang, S::OrJoin), Font::Small, color::MUTED, Align::Center, X(232));
  C().text(X(120), Y(228), apSsid, Font::BodyBold, color::AMBER, Align::Center, X(232));
}

void welcome(Lang lang, const char* pairCode, const char* ip) {
  uint32_t h = hashStr(hashStr(hashInt(kHashSeed, (uint32_t)lang), pairCode), ip);
  if (!region(0, h, 0, 0, X(240), Y(240))) return;
  const char* ok = t(lang, S::WifiConnected);
  const int w = C().textWidth(ok, Font::BodyBold);
  check(X(120) - w / 2 - Sz(4), Y(22), Sz(14), color::GREEN);
  C().text(X(120) + Sz(8), Y(28), ok, Font::BodyBold, color::GREEN, Align::Center, X(200));
  C().text(X(12), Y(58), t(lang, S::RunInClaude), Font::Small, color::MUTED, Align::Left, X(216));
  C().fillRoundRect(X(12), Y(66), X(216), Y(30), Sz(4), color::CMD_BG);
  C().text(X(20), Y(86), "/plugin install miblo@miblo", Font::Small, color::CORAL, Align::Left, X(200));
  C().text(X(120), Y(128), t(lang, S::PairingCode), Font::Small, color::MUTED, Align::Center, X(232));
  C().text(X(120), Y(176), pairCode, Font::NumL, color::TEXT, Align::Center, X(232));
  C().text(X(120), Y(228), ip, Font::Small, color::FAINT, Align::Center, X(232));
}

void paired(Lang lang, const char* host, const char* modeName, const char* mdnsHost) {
  uint32_t h = hashStr(hashStr(hashStr(hashInt(kHashSeed, (uint32_t)lang), host), modeName), mdnsHost);
  if (!region(0, h, 0, 0, X(240), Y(240))) return;
  check(X(120), Y(70), Sz(48), color::GREEN);
  C().text(X(120), Y(132), t(lang, S::PairedWith), Font::Title, color::TEXT, Align::Center, X(232));
  C().text(X(120), Y(158), host, Font::Body, color::MUTED, Align::Center, X(220));
  C().text(X(120), Y(208), modeName, Font::Small, color::FAINT, Align::Center, X(232));
  char url[48];
  snprintf(url, sizeof(url), "http://%s.local", mdnsHost);
  C().text(X(120), Y(226), url, Font::Small, color::FAINT, Align::Center, X(232));
}

void code(Lang lang, S title, const char* codeStr, uint32_t remainingSec) {
  uint32_t h = hashStr(hashInt(hashInt(kHashSeed, (uint32_t)lang), (uint32_t)title), codeStr);
  if (region(0, h, 0, Y(30), X(240), Y(120))) {
    C().text(X(120), Y(64), t(lang, title), Font::Body, color::TEXT, Align::Center, X(232));
    C().text(X(120), Y(132), codeStr, Font::NumL, color::AMBER, Align::Center, X(232));
  }
  if (region(1, hashInt(kHashSeed, remainingSec), 0, Y(150), X(240), Y(40))) {
    char left[16];
    char line[64];
    miblo::formatElapsed(remainingSec, left, sizeof(left));
    snprintf(line, sizeof(line), t(lang, S::ExpiresIn), left);
    C().text(X(120), Y(174), line, Font::Small, color::MUTED, Align::Center, X(232));
  }
}

void updating(Lang lang, uint8_t pct) {
  if (region(0, hashInt(kHashSeed, (uint32_t)lang), 0, Y(40), X(240), Y(60))) {
    C().text(X(120), Y(84), t(lang, S::Updating), Font::Title, color::TEXT, Align::Center, X(232));
  }
  if (region(1, hashInt(kHashSeed, pct), 0, Y(104), X(240), Y(60))) {
    bar(X(30), Y(110), X(180), Y(14), pct, color::CORAL);
    char b[8];
    snprintf(b, sizeof(b), "%u%%", (unsigned)pct);
    C().text(X(120), Y(154), b, Font::Body, color::TEXT, Align::Center, X(232));
  }
  if (region(2, hashInt(kHashSeed + 1, (uint32_t)lang), 0, Y(176), X(240), Y(30))) {
    C().text(X(120), Y(196), t(lang, S::DoNotUnplug), Font::Small, color::MUTED, Align::Center, X(232));
  }
}

// Draws `s` centered on at most two lines, breaking at the last space that still fits `maxW`
// (the second line is cut with "..." by the canvas if it is still too long).
static void twoLines(const char* s, Font f, uint16_t fg, int cx, int y1, int y2, int maxW) {
  if (C().textWidth(s, f) <= maxW) {
    C().text(cx, y1 + (y2 - y1) / 2, s, f, fg, Align::Center, maxW);
    return;
  }
  char first[128];
  size_t split = 0;
  for (size_t i = 0; s[i] && i < sizeof(first) - 1; i++) {
    if (s[i] != ' ') continue;
    memcpy(first, s, i);
    first[i] = 0;
    if (C().textWidth(first, f) > maxW) break;
    split = i;
  }
  if (split == 0) {
    C().text(cx, y1 + (y2 - y1) / 2, s, f, fg, Align::Center, maxW);
    return;
  }
  memcpy(first, s, split);
  first[split] = 0;
  C().text(cx, y1, first, f, fg, Align::Center, maxW);
  C().text(cx, y2, s + split + 1, f, fg, Align::Center, maxW);
}

void hardResetCountdown(Lang lang, uint8_t remaining) {
  uint32_t h = hashInt(hashInt(kHashSeed + 7, (uint32_t)lang), remaining);
  if (!region(0, h, 0, 0, X(240), Y(240))) return;
  const int cy = Y(84);
  const int r = Sz(46);
  C().arc(X(120), cy, r, r - Sz(5), 0, 360, color::AMBER, color::BG);
  char n[4];
  snprintf(n, sizeof(n), "%u", (unsigned)remaining);
  C().text(X(120), cy + Sz(15), n, Font::NumL, color::AMBER, Align::Center, X(232));
  char line[128];
  snprintf(line, sizeof(line), t(lang, S::HardResetCountdown), (unsigned)remaining);
  twoLines(line, Font::Body, color::TEXT, X(120), Y(162), Y(182), X(224));
  C().text(X(120), Y(218), t(lang, S::HardResetCancelHint), Font::Small, color::MUTED, Align::Center, X(232));
}

void disconnected(Lang lang, bool timeValid, int hour, int minute, int wday, int mday, const char* ip,
                  const char* mdnsHost, const char* pairCode) {
  if (region(0, hashInt(kHashSeed, (uint32_t)lang), 0, 0, X(240), Y(30))) {
    C().text(X(12), Y(20), t(lang, S::Disconnected), Font::SmallBold, color::DIM, Align::Left, X(216));
  }
  uint32_t ht = timeValid ? hashInt(hashInt(kHashSeed, (uint32_t)(hour * 60 + minute)), (uint32_t)(wday * 32 + mday))
                          : 1;
  if (region(1, hashInt(ht, (uint32_t)lang), 0, Y(60), X(240), Y(90))) {
    char hhmm[8];
    if (timeValid) miblo::formatHHMM(hour, minute, hhmm, sizeof(hhmm));
    else strcpy(hhmm, "--:--");
    C().text(X(120), Y(112), hhmm, Font::NumL, color::TEXT, Align::Center, X(232));
    if (timeValid && wday >= 0 && wday < 7) {
      char date[32];
      snprintf(date, sizeof(date), "%s %d", t(lang, (S)((int)S::WdSun + wday)), mday);
      C().text(X(120), Y(140), date, Font::Small, color::MUTED, Align::Center, X(232));
    }
  }
  uint32_t hf = hashStr(hashStr(hashStr(hashInt(kHashSeed, (uint32_t)lang), ip), mdnsHost), pairCode);
  if (region(2, hf, 0, Y(156), X(240), Y(84))) {
    C().text(X(120), Y(176), t(lang, S::WaitingComputer), Font::Small, color::MUTED, Align::Center, X(232));
    char line[64];
    snprintf(line, sizeof(line), "%s \xC2\xB7 %s.local", ip, mdnsHost);
    C().text(X(120), Y(208), line, Font::Small, color::FAINT, Align::Center, X(232));
    snprintf(line, sizeof(line), "%s %s", t(lang, S::PairingCode), pairCode);
    C().text(X(120), Y(228), line, Font::Small, color::FAINT, Align::Center, X(232));
  }
}

}  // namespace screens
