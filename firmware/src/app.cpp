#include "app.h"

#include <time.h>

#include "api.h"
#include "board.h"
#include "context.h"
#include "miblo_format.h"
#include "miblo_overview.h"
#include "miblo_policy.h"
#include "platform/mdns_service.h"
#include "platform/net.h"
#include "platform/ota.h"
#include "platform/platform.h"
#include "platform/storage.h"
#include "ui_screens.h"
#include "web.h"

namespace app {

using miblo::Lang;
using miblo::S;
using miblo::ScreenId;

static WebServerT server(80);
static uint32_t bootMs = 0;
static bool bootCountCleared = false;
static uint8_t hardResetRemaining = 0;  // > 0: show the quick-boot countdown for the first 10 s
static bool bootAnimDone = false;
static ScreenId current = ScreenId::Boot;
static bool firstFrame = true;
static Lang drawnLang = Lang::En;
static uint32_t lastFrameMs = 0;
static miblo::Pager listPager(3, 5000);  // Overview: 3 session cards per page
static miblo::Pager sessionPager(3, 5000);  // Sessions mode: 3 big cards per page
static miblo::RotationClock rotation;  // optional Overview/Limits alternation
static miblo::QuietClock quiet;        // All done -> Limits -> Desk while nothing happens
static bool mainLimits = false;        // Main is showing the Limits screen in Overview mode
static uint32_t awaySinceMs = 0;       // when the Disconnected screen came up

static void enter(ScreenId s) {
  if (!firstFrame && s == current && drawnLang == uiLang()) return;
  firstFrame = false;
  current = s;
  drawnLang = uiLang();
  screens::reset();
}

// Called by the OTA handler during the upload (the loop is blocked while the file arrives).
static void onOtaProgress(uint8_t pct) {
  enter(ScreenId::Updating);
  screens::updating(uiLang(), pct);
}

static const char* modeName(Lang lang) {
  switch (ctx.cfg.mode) {
    case miblo::Mode::Limits: return screens::t(lang, S::ModeLimits);
    case miblo::Mode::Sessions: return screens::t(lang, S::ModeSessions);
    case miblo::Mode::Overview: break;
  }
  return screens::t(lang, S::ModeOverview);
}

static screens::SetupNote setupNote(miblo::JoinFailure f) {
  switch (f) {
    case miblo::JoinFailure::NotFound: return screens::SetupNote::NotFound;
    case miblo::JoinFailure::Refused: return screens::SetupNote::Refused;
    case miblo::JoinFailure::None:
    case miblo::JoinFailure::Other:
    case miblo::JoinFailure::Timeout: break;
  }
  return screens::SetupNote::Failed;
}

static screens::Clock clockNow() {
  screens::Clock c{};
  time_t now = time(nullptr);
  if (now > 1600000000) {
    struct tm lt;
    localtime_r(&now, &lt);
    c.valid = true;
    miblo::formatHHMM(lt.tm_hour, lt.tm_min, c.hhmm, sizeof(c.hhmm));
    c.epoch = (uint32_t)now;
  } else {
    strcpy(c.hhmm, "--:--");
    c.epoch = ctx.hasSnapshot ? ctx.snap.now + (millis() - ctx.lastSnapshotMs) / 1000 : 0;
  }
  return c;
}

static uint8_t backlight = 0;  // % last sent to the board (0 = not yet)

// Local minute of the day, or -1 while the time is unknown.
static int minuteOfDay() {
  time_t now = time(nullptr);
  if (now <= 1600000000) return -1;
  struct tm lt;
  localtime_r(&now, &lt);
  return lt.tm_hour * 60 + lt.tm_min;
}

// Brightness for the current time (night mode); only touches the board when it changes.
static void updateBacklight() {
  const uint8_t want = miblo::brightnessAt(ctx.cfg, minuteOfDay());
  if (want == backlight) return;
  backlight = want;
  board::setBacklight(want);
}

static void applyConfig() {
  updateBacklight();
  ctx.alerts.setTiming(miblo::alertTiming(ctx.cfg));
}

// True for a real power-on (or the reset pin); false after a crash, watchdog, OTA or software
// restart — those must never count towards the quick-boot hard reset.
static bool poweredOn() {
#if defined(ESP8266)
  const uint32_t reason = ESP.getResetInfoPtr()->reason;
  return reason == REASON_DEFAULT_RST || reason == REASON_EXT_SYS_RST;
#else
  const esp_reset_reason_t reason = esp_reset_reason();
  return reason == ESP_RST_POWERON || reason == ESP_RST_EXT;
#endif
}

void setup() {
  Serial.begin(115200);
  storage::begin();
  // Hard reset without a button: 6 power-ons in a row, each with less than 10 s of
  // uptime. Persist the counter before anything slow so a quick unplug still counts.
  const miblo::BootDecision boot = miblo::decideBoot(storage::readBootCount(), poweredOn());
  storage::writeBootCount(boot.nextCount);
  hardResetRemaining = boot.remaining;

  board::begin();
  screens::bind(board::canvas());
  if (boot.factoryReset) {
    // Escape hatch: touch as little as possible (a corrupt config must not block the reset), so
    // the message is always in English. Keep it readable for a moment, then wipe and restart.
    screens::reset();
    screens::canvas().text(screens::X(120), screens::Y(124), screens::t(Lang::En, S::WebFactoryReset),
                           ui::Font::Title, ui::color::RED, ui::Align::Center, screens::X(232));
    delay(1500);
    storage::factoryReset();  // erases config, pairings and SDK Wi-Fi, then restarts into setup
  }

  storage::loadConfig(ctx.cfg);
  storage::loadTokens(ctx.tokens);
  applyConfig();
  char code[5];
  miblo::formatCode(hwRandom(), code);
  ctx.pairing.setCode(code);

  bootMs = millis();
  net::begin(bootMs);
  web::begin(server);
  api::begin(server);
  ota::begin(server, onOtaProgress);
  server.begin();
}

void loop() {
  const uint32_t now = millis();
  server.handleClient();
  net::loop(now);
  mdns::loop(now);

  if (!bootCountCleared && now - bootMs >= miblo::kPowerCycleWindowMs) {
    storage::writeBootCount(0);
    bootCountCleared = true;
  }
  if (ctx.configChanged) {
    ctx.configChanged = false;
    storage::saveConfig(ctx.cfg);
    applyConfig();
    net::applyTimezone();
    mdns::announce();
    firstFrame = true;  // language/mode may have changed: redraw everything
  }
  if (ctx.factoryResetRequested) {
    delay(300);  // let the HTTP response go out
    storage::factoryReset();
  }
  if (ctx.rebootRequested && (int32_t)(now - ctx.rebootAtMs) >= 0) ESP.restart();
  ctx.presence.update(now);  // expire old brute-force lockouts before the clock can wrap
  ctx.pairing.update(now);
  if (ctx.showPairCode && now - ctx.pairCodeAtMs >= miblo::kPairCodeScreenMs) ctx.showPairCode = false;

  if (now - lastFrameMs < 100) return;  // ~10 frames/s
  lastFrameMs = now;

  if (!bootAnimDone && now - bootMs >= 2400) bootAnimDone = true;
  updateBacklight();
  static const miblo::Snapshot kEmpty{};
  const miblo::AlertView& alert = ctx.alerts.update(ctx.hasSnapshot ? ctx.snap : kEmpty, now);

  miblo::ScreenInputs in;
  in.nowMs = now;
  in.bootAnimDone = bootAnimDone;
  in.net = net::state();
  in.updating = ctx.updating;
  in.presenceActive = ctx.presence.active(now);
  in.hardResetCountdown = hardResetRemaining > 0 && !bootCountCleared;
  in.pairCodeRequested = ctx.showPairCode;
  in.paired = ctx.tokens.count() > 0;
  in.justPaired = ctx.justPaired;
  in.pairedAtMs = ctx.pairedAtMs;
  in.hasSnapshot = ctx.hasSnapshot;
  in.lastSnapshotMs = ctx.lastSnapshotMs;
  in.alert = alert.phase;
  ScreenId screen = miblo::selectScreen(in);
  const miblo::StateCounts counts = miblo::countStates(ctx.snap);
  // Nothing running or waiting: "All done" gives way to Limits, and later to the Desk mascot.
  const miblo::QuietPhase qp =
      quiet.update(screen == ScreenId::Main && counts.pending == 0 && counts.running == 0, now);
  if (qp == miblo::QuietPhase::Desk) screen = ScreenId::Desk;
  // Rotation never takes the screen away from an alert or a session waiting on the user.
  const bool rotBlocked = screen != ScreenId::Main || counts.pending > 0 || qp == miblo::QuietPhase::Settled;
  const bool rotLimits = rotation.update(miblo::rotationTiming(ctx.cfg), rotBlocked, now);
  const bool wantLimits = screen == ScreenId::Main && ctx.cfg.mode == miblo::Mode::Overview &&
                          (rotLimits || qp == miblo::QuietPhase::Settled);
  if (wantLimits != mainLimits) firstFrame = true;  // Overview <-> Limits: redraw everything
  mainLimits = wantLimits;
  if (screen == ScreenId::Disconnected && current != ScreenId::Disconnected) awaySinceMs = now;
  enter(screen);

  const Lang lang = uiLang();
  const screens::Clock clk = clockNow();
  switch (screen) {
    case ScreenId::Boot:
      screens::boot(lang, (uint8_t)((now - bootMs) / 400));
      break;
    case ScreenId::HardResetCountdown:
      screens::hardResetCountdown(lang, hardResetRemaining);
      break;
    case ScreenId::Setup:
      screens::setup(lang, ctx.ident.apSsid);
      break;
    case ScreenId::WrongPassword:
      screens::setup(lang, ctx.ident.apSsid, screens::SetupNote::WrongPassword);
      break;
    case ScreenId::JoinFailed:
      screens::setup(lang, ctx.ident.apSsid, setupNote(net::joinFailure()), net::joinFailureCode());
      break;
    case ScreenId::Welcome:
      screens::welcome(lang, ctx.pairing.code(), net::ip().c_str());
      break;
    case ScreenId::Paired:
      screens::paired(lang, ctx.pairedHost, modeName(lang), ctx.ident.id);
      break;
    case ScreenId::PairCode:
      screens::code(lang, S::PairingCode, ctx.pairing.code(),
                    (miblo::kPairCodeScreenMs - (now - ctx.pairCodeAtMs)) / 1000);
      break;
    case ScreenId::PresenceCode:
      screens::code(lang,
                    ctx.presence.purpose() == miblo::PresenceGate::Purpose::Update ? S::CodeUpdate : S::CodeReset,
                    ctx.presence.code(), ctx.presence.remainingMs(now) / 1000);
      break;
    case ScreenId::Updating:
      screens::updating(lang, ctx.updatePct);
      break;
    case ScreenId::Disconnected:
      screens::disconnected(lang, clk, net::ip().c_str(), ctx.ident.id, ctx.pairing.code(), now, now - awaySinceMs);
      break;
    case ScreenId::Desk:
      screens::desk(lang, ctx.snap, clk, now);
      break;
    case ScreenId::AlertFlash: {
      int idx = miblo::findSession(ctx.snap, alert.sid);
      screens::flash(lang, alert.kind, idx >= 0 ? ctx.snap.sessions[idx].name : "", now - alert.phaseStartMs);
      break;
    }
    case ScreenId::AlertHero:
      screens::hero(lang, ctx.snap, miblo::findSession(ctx.snap, alert.sid), alert.kind, ctx.cfg.discreet, clk,
                    ctx.runs);
      break;
    case ScreenId::Main:
      switch (ctx.cfg.mode) {
        case miblo::Mode::Overview:
          if (mainLimits) {
            screens::limits(lang, ctx.snap, clk);
            break;
          }
          screens::overview(lang, ctx.snap, listPager, now, clk, ctx.cfg.discreet);
          break;
        case miblo::Mode::Limits:
          screens::limits(lang, ctx.snap, clk);
          break;
        case miblo::Mode::Sessions:
          screens::sessions(lang, ctx.snap, sessionPager, now, clk, ctx.cfg.discreet);
          break;
      }
      break;
  }
}

}  // namespace app
