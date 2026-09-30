#include "app.h"

#include <time.h>

#include "api.h"
#include "board.h"
#include "context.h"
#include "miblo_format.h"
#include "miblo_overview.h"
#include "miblo_policy.h"
#include "miblo_version.h"
#include "miblo_occasions.h"
#include "platform/friends_net.h"
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
static miblo::QuietClock quiet;        // All done -> Desk while nothing happens
static bool mainLimits = false;        // Main shows the Limits arc instead of the mode's screen
static uint32_t awaySinceMs = 0;       // when the Disconnected screen came up
static uint32_t limitResetMs = 0;      // when the "limit freed" screen came up

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

static miblo::S presenceCodeTitle(miblo::PresenceGate::Purpose p) {
  switch (p) {
    case miblo::PresenceGate::Purpose::Update: return miblo::S::CodeUpdate;
    case miblo::PresenceGate::Purpose::Settings: return miblo::S::CodeSettings;
    case miblo::PresenceGate::Purpose::Reset: break;
  }
  return miblo::S::CodeReset;
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
static bool displayOff = false;  // the panel is asleep (nobody using it: see miblo::screenAsleep)
static uint8_t shiftStep = 0;    // current pixel-shift position (ui::ShiftCanvas)
static uint32_t shiftAtMs = 0;
static uint32_t roamSinceMs = 0;  // when pet mode came up
static char shownName[64] = "";   // the name the screen last knew (a change greets with it)
static char knownOwner[64 + 6] = "";  // owner name + birthday last applied (a change re-arms today's greeting)
static uint8_t accessory = 0;     // today's hat (miblo::Accessory)
static uint32_t occasionAtMs = 0;

// Local date and minute of the day; false while the time is unknown.
static bool today(miblo::Date& d, int& minute) {
  const time_t now = time(nullptr);
  if (now <= 1600000000) return false;
  struct tm lt;
  localtime_r(&now, &lt);
  d = miblo::Date{(uint16_t)(lt.tm_year + 1900), (uint8_t)(lt.tm_mon + 1), (uint8_t)lt.tm_mday};
  minute = lt.tm_hour * 60 + lt.tm_min;
  return true;
}

// Once a minute: today's hat, and the gadget's own birthday noted on the first day it is used.
static void updateOccasion(uint32_t now) {
  if (occasionAtMs && now - occasionAtMs < 60000) return;
  occasionAtMs = now;
  miblo::Date d;
  int minute;
  uint8_t want = 0;
  if (today(d, minute)) {
    want = (uint8_t)miblo::accessoryFor(miblo::occasionOn(ctx.cfg, d));
    if (!ctx.cfg.born[0] && ctx.tokens.count() > 0) {
      snprintf(ctx.cfg.born, sizeof(ctx.cfg.born), "%04u-%02u-%02u", (unsigned)d.year, (unsigned)d.month,
               (unsigned)d.day);
      ctx.configChanged = true;
    }
  }
  if (want != accessory) {
    accessory = want;
    screens::setMascotAccessory(want);
    firstFrame = true;
  }
}

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
  if (displayOff) return;  // stays dark until the panel wakes
  const uint8_t want = miblo::brightnessAt(ctx.cfg, minuteOfDay());
  if (want == backlight) return;
  backlight = want;
  board::setBacklight(want);
}

// A session finished moments ago (kFreshFinishSec): only then is "All done" worth showing. A new
// idle session (or the computer coming back to idle sessions) has nothing done to report.
static constexpr uint32_t kFreshFinishSec = 120;
static bool justFinished() {
  const int i = miblo::lastFinished(ctx.snap);
  if (i < 0) return false;
  const time_t t = time(nullptr);
  const uint32_t now = t > 1600000000 ? (uint32_t)t : ctx.snap.now;
  const uint32_t since = ctx.snap.sessions[i].since;
  return since && now >= since && now - since < kFreshFinishSec;
}

static void applyConfig() {
  updateBacklight();
  screens::setMascotStyle(ctx.cfg.mascot);
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

static uint32_t g_minHeapParse = 0xFFFFFFFFu;  // least free heap seen during a snapshot parse
static void parseHeapProbe() {
  const uint32_t h = freeHeap();
  if (h < g_minHeapParse) g_minHeapParse = h;
}
uint32_t minHeapDuringParse() { return g_minHeapParse == 0xFFFFFFFFu ? 0 : g_minHeapParse; }

void setup() {
  Serial.begin(115200);
  miblo::setParseProbe(parseHeapProbe);
  storage::begin();
  // Hard reset without a button: 6 power-ons in a row, each with less than 10 s of
  // uptime. Persist the counter before anything slow so a quick unplug still counts.
  const miblo::BootDecision boot = miblo::decideBoot(storage::readBootCount(), poweredOn());
  storage::writeBootCount(boot.nextCount);
  hardResetRemaining = boot.remaining;

  board::begin();
  // Everything is drawn through a shifting canvas: the picture moves a pixel or two every few
  // minutes so nothing sits still for hours (LCD ghosting).
  static ui::ShiftCanvas shifted(board::canvas());
  screens::bind(shifted);
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
  strlcpy(shownName, deviceName(), sizeof(shownName));
  snprintf(knownOwner, sizeof(knownOwner), "%s|%s", ctx.cfg.owner, ctx.cfg.birthday);
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
  friendsnet::loop(now);

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
    if (strcmp(shownName, deviceName()) != 0) {  // renamed: say hello with the new name
      strlcpy(shownName, deviceName(), sizeof(shownName));
      ctx.greeter.named(now);
    }
    char owner[sizeof(knownOwner)];
    snprintf(owner, sizeof(owner), "%s|%s", ctx.cfg.owner, ctx.cfg.birthday);
    if (strcmp(owner, knownOwner) != 0) {  // told who we are: greet (again) today
      strlcpy(knownOwner, owner, sizeof(knownOwner));
      ctx.greeter.rearm();
    }
    occasionAtMs = 0;  // a birthday may have been set
    firstFrame = true;  // language/mode may have changed: redraw everything
  }
  if (ctx.factoryResetRequested) {
    ctx.webSession.clear();
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
  // Before the first snapshot ctx.snap is all zeros (parseSnapshot only writes it on success).
  const miblo::AlertView& alert = ctx.alerts.update(ctx.snap, now);

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
  in.limitReset = ctx.limits.celebrating(now);
  in.updateNotice = ctx.update.showing(now);
  ScreenId screen = miblo::selectScreen(in);
  const miblo::StateCounts counts = miblo::countStates(ctx.snap);
  // Nothing running or waiting: "All done" gives way to the Desk mascot with the limits, which
  // takes turns with the Limits arc.
  const miblo::QuietPhase qp =
      quiet.update(screen == ScreenId::Main && counts.pending == 0 && counts.running == 0, now, justFinished());
  if (qp == miblo::QuietPhase::Desk) screen = ScreenId::Desk;
  if (qp == miblo::QuietPhase::Summary) screen = ScreenId::Summary;
  if (screen == ScreenId::LimitReset && current != ScreenId::LimitReset) limitResetMs = now;
  // Rotation never takes the screen away from an alert or a session waiting on the user.
  const bool rotBlocked = screen != ScreenId::Main || counts.pending > 0;
  const bool rotLimits = rotation.update(miblo::rotationTiming(ctx.cfg), rotBlocked, now);
  const bool wantLimits = screen == ScreenId::Main && ((ctx.cfg.mode == miblo::Mode::Overview && rotLimits) ||
                                                       qp == miblo::QuietPhase::Arc);
  if (wantLimits != mainLimits) firstFrame = true;  // Overview <-> Limits: redraw everything
  mainLimits = wantLimits;
  if (screen == ScreenId::Disconnected && current != ScreenId::Disconnected) awaySinceMs = now;

  // Screen care. Nobody using it (computer away, or all quiet): after a while the mascot wanders
  // around the screen (pet mode), and after sleepMin minutes the panel goes off; the first sign
  // of life brings everything back.
  const bool idleScreen = screen == ScreenId::Disconnected || screen == ScreenId::Desk ||
                          screen == ScreenId::Summary || (screen == ScreenId::Main && qp != miblo::QuietPhase::Busy);
  const uint32_t idleMs = !idleScreen ? 0 : screen == ScreenId::Disconnected ? now - awaySinceMs : quiet.quietMs(now);
  const uint32_t sinceSeen = now - ctx.lastInteractionMs;
  const bool away = screen == ScreenId::Disconnected;
  // Demo (/miblo:demo): pet mode now, over any ordinary screen (alerts and setup still win).
  if (ctx.demo && (int32_t)(now - ctx.demoUntilMs) >= 0) {
    ctx.demo = false;
    ctx.demoKick = true;
  }
  const bool demo = ctx.demo && (screen == ScreenId::Main || screen == ScreenId::Desk ||
                                 screen == ScreenId::Summary || screen == ScreenId::Disconnected);
  const bool pet = miblo::petMode(idleMs, sinceSeen) || demo;
  if (pet) screen = ScreenId::Roam;
  if (screen == ScreenId::Roam && current != ScreenId::Roam && current != ScreenId::Visit) roamSinceMs = now;

  // Other Miblos on the network: what we tell them (in pet mode, napping, limits past 80%), and
  // a visit takes over the pet mode screen.
  const uint32_t nowEpoch = clockNow().epoch;
  const bool napping = pet && away && now - awaySinceMs >= miblo::kAwayNapMs;
  const screens::DeskMood limitsMood = screens::deskMoodFor(ctx.snap, nowEpoch ? nowEpoch : ctx.snap.now);
  const bool tired = ctx.usageEverSeen &&
                     (limitsMood == screens::DeskMood::Worried || limitsMood == screens::DeskMood::Scared);
  ctx.friends.setSelf(ctx.ident.id, deviceName(), ctx.cfg.mascot);
  ctx.friends.update(now, ctx.cfg.friends && net::connected(),
                     (pet ? miblo::kFriendRoaming : 0) | (napping ? miblo::kFriendNapping : 0) |
                         (tired ? miblo::kFriendTired : 0),
                     hwRandom());
  if (ctx.demoKick) {  // after update(): it has seen pet mode start
    ctx.demoKick = false;
    ctx.friends.demo(now, ctx.demo ? ctx.demoUntilMs : now);
  }
  const miblo::VisitView visit = ctx.friends.visit(now);
  if (screen == ScreenId::Roam && visit.role != miblo::VisitRole::None) screen = ScreenId::Visit;

  // Greetings: the first activity of the day, or a new name. Only over ordinary screens.
  updateOccasion(now);
  miblo::Date day{};
  int minute = 0;
  const bool timeKnown = today(day, minute);
  ctx.greeter.update(now, screen == ScreenId::Main && counts.running > 0, timeKnown, day, minute, ctx.cfg);
  const miblo::Greeting greeting = ctx.greeter.showing(now);
  if (greeting != miblo::Greeting::None &&
      (screen == ScreenId::Main || screen == ScreenId::Desk || screen == ScreenId::Summary ||
       screen == ScreenId::Disconnected || screen == ScreenId::Roam || screen == ScreenId::Visit ||
       screen == ScreenId::Paired)) {
    screen = ScreenId::Hello;
  }
  const bool asleep = !ctx.demo && miblo::screenAsleep(idleMs, sinceSeen, ctx.cfg.sleepMin);
  if (asleep != displayOff) {
    displayOff = asleep;
    board::setDisplay(!asleep);
    if (!asleep) {
      backlight = 0;  // relight at the current brightness
      updateBacklight();
      firstFrame = true;
    }
  }
  if (displayOff) return;
  if (now - shiftAtMs >= miblo::kShiftEveryMs) {
    shiftAtMs = now;
    int8_t dx, dy;
    miblo::pixelShift(++shiftStep, dx, dy);
    static_cast<ui::ShiftCanvas&>(screens::canvas()).setShift(dx, dy);
    firstFrame = true;  // redraw everything at the new offset
  }
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
      screens::code(lang, presenceCodeTitle(ctx.presence.purpose()), ctx.presence.code(),
                    ctx.presence.remainingMs(now) / 1000);
      break;
    case ScreenId::Updating:
      screens::updating(lang, ctx.updatePct);
      break;
    case ScreenId::Disconnected:
      screens::disconnected(lang, clk, net::ip().c_str(), ctx.ident.id, ctx.pairing.code(), now, now - awaySinceMs);
      break;
    case ScreenId::Desk:
      screens::desk(lang, ctx.snap, clk, now, ctx.limits.exhaustAt());
      break;
    case ScreenId::LimitReset:
      screens::limitReset(lang, ctx.snap, clk, now - limitResetMs);
      break;
    case ScreenId::Summary:
      screens::summary(lang, ctx.snap, clk);
      break;
    case ScreenId::UpdateAvailable:
      screens::updateAvailable(lang, MIBLO_FW_VERSION, ctx.snap.latest, (uint8_t)(now / 400));
      break;
    case ScreenId::Roam: {
      screens::DeskMood mood =
          away ? (now - awaySinceMs >= miblo::kAwayNapMs ? screens::DeskMood::Asleep : screens::DeskMood::Searching)
               : screens::deskMoodFor(ctx.snap, clk.epoch ? clk.epoch : ctx.snap.now);
      char note[96] = "";
      uint32_t lookMs = UINT32_MAX;
      if (const char* hi = ctx.friends.greeting(now)) {
        snprintf(note, sizeof(note), screens::t(lang, S::FriendHi), hi);
        mood = screens::DeskMood::Celebrate;
      } else if (const char* buddy = ctx.friends.napBuddy()) {
        // Napping together: the same wall-clock phase on both screens, so the zzz go in step.
        snprintf(note, sizeof(note), screens::t(lang, S::FriendNap), buddy);
        if (clk.valid) lookMs = (clk.epoch % 86400) * 1000;
      }
      screens::roam(lang, ctx.snap, clk, now - roamSinceMs, mood, note, lookMs);
      break;
    }
    case ScreenId::Visit:
      screens::visit(lang, ctx.snap, clk, visit);
      break;
    case ScreenId::Hello: {
      char l1[64], l2[64];
      miblo::greetingLines(lang, greeting, ctx.cfg.owner, deviceName(), l1, sizeof(l1), l2, sizeof(l2));
      screens::hello(l1, l2, miblo::greetingIsParty(greeting), ctx.greeter.elapsed(now));
      break;
    }
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
      if (mainLimits) {  // a rotation slot, or the arc's turn in the Desk cycle
        screens::limits(lang, ctx.snap, clk, ctx.limits.exhaustAt());
        break;
      }
      switch (ctx.cfg.mode) {
        case miblo::Mode::Overview:
          screens::overview(lang, ctx.snap, listPager, now, clk, ctx.cfg.discreet);
          break;
        case miblo::Mode::Limits:
          screens::limits(lang, ctx.snap, clk, ctx.limits.exhaustAt());
          break;
        case miblo::Mode::Sessions:
          screens::sessions(lang, ctx.snap, sessionPager, now, clk, ctx.cfg.discreet);
          break;
      }
      break;
  }
}

}  // namespace app
