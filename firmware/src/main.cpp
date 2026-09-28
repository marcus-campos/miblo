// Demo da Task 11: placa + Canvas + fontes reais. Mostra o mascote e passa pelas telas de
// sistema e principais com dados de exemplo. Mantém o /update do firmware seguro (Task 1).
// Substituído pelo aplicativo completo na Task 13.
#include <Arduino.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <string.h>

#include "board.h"
#include "miblo_version.h"
#include "ui_screens.h"

using miblo::Lang;
using ui::Align;
using ui::Font;

ESP8266WebServer server(80);
ESP8266HTTPUpdateServer updater;
static miblo::Snapshot demo;
static miblo::Pager pager(4, 5000);
static miblo::RunTracker runs;

static void fontSample() {
  ui::Canvas& c = board::canvas();
  c.text(12, 24, "Miblo " MIBLO_FW_VERSION, Font::Title, ui::color::TEXT, Align::Left, 216);
  c.text(12, 50, "Ação às 14h · déjà vu", Font::Body, ui::color::MUTED, Align::Left, 216);
  c.text(12, 72, "Übersicht · ¡Hola!", Font::Body, ui::color::MUTED, Align::Left, 216);
  c.text(12, 94, "Ожидание компьютера", Font::Body, ui::color::MUTED, Align::Left, 216);
  c.text(12, 116, "等待电脑连接", Font::Body, ui::color::MUTED, Align::Left, 216);
  c.text(12, 138, "sem glyph: \xF0\x9F\x98\x80 fim", Font::Body, ui::color::AMBER, Align::Left, 216);
  c.text(12, 160, "texto comprido demais que precisa ser cortado", Font::Small, ui::color::DIM, Align::Left, 216);
  c.text(228, 226, "62%", Font::NumL, ui::color::TEXT, Align::Right, 216);
}

static void makeDemo() {
  memset(&demo, 0, sizeof(demo));
  const uint32_t now = 1790616720;
  demo.now = now;
  demo.hasUsage = true;
  demo.h5 = {true, 62, now + 7800};
  demo.d7 = {true, 38, now + 240000};
  demo.todayUsd = 3.5f;
  const struct {
    const char* id;
    const char* name;
    miblo::SessionState st;
    const char* tool;
    const char* det;
  } rows[] = {{"1", "api-server", miblo::SessionState::Perm, "Bash", "npm run migrate"},
              {"2", "front-app", miblo::SessionState::Running, "Edit", "Header.tsx"},
              {"3", "docs", miblo::SessionState::Done, "", ""}};
  for (const auto& r : rows) {
    miblo::SessionRow& s = demo.sessions[demo.count++];
    strcpy(s.id, r.id);
    strcpy(s.name, r.name);
    s.st = r.st;
    strcpy(s.tool, r.tool);
    strcpy(s.det, r.det);
    s.since = now - 42;
    strcpy(s.model, "Opus");
    s.ctx = 71;
    s.tok = 412000;
  }
}

void setup() {
  board::begin();
  screens::bind(board::canvas());
  makeDemo();
  WiFi.mode(WIFI_STA);
  WiFi.begin();
  const uint32_t t0 = millis();
  uint8_t frame = 0;
  screens::reset();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    screens::boot(Lang::PtBR, frame++);
    delay(300);
  }
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("Miblo-Recovery");
  }
  updater.setup(&server, "/update");
  server.begin();
}

void loop() {
  server.handleClient();
  static uint32_t last = 0;
  static uint8_t step = 0;
  if (millis() - last < 4000) return;
  last = millis();
  const String ip = (WiFi.getMode() & WIFI_AP) ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  screens::Clock clk{true, "14:32", demo.now};
  screens::reset();
  switch (step++ % 11) {
    case 0: fontSample(); break;
    case 1: screens::setup(Lang::En, "Miblo-Setup-4F2A", false); break;
    case 2: screens::setup(Lang::Ru, "Miblo-Setup-4F2A", true); break;
    case 3: screens::welcome(Lang::PtBR, "4827", ip.c_str()); break;
    case 4: screens::paired(Lang::Zh, "MacBook-Marcus", "概览", "miblo-4f2a"); break;
    case 5: screens::code(Lang::De, miblo::S::CodeUpdate, "1234", 299); break;
    case 6: screens::disconnected(Lang::Fr, true, 14, 32, 1, 28, ip.c_str(), "miblo-4f2a", "4827"); break;
    case 7: screens::hero(Lang::PtBR, demo, 0, miblo::AlertKind::Perm, false, clk, runs); break;
    case 8: screens::overview(Lang::PtBR, demo, pager, millis(), clk, false); break;
    case 9: screens::limits(Lang::PtBR, demo, clk); break;
    case 10: screens::sessions(Lang::PtBR, demo, pager, millis(), clk, false); break;
  }
}
