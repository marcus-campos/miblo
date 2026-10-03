#include "app.h"

#include <time.h>

#include "api.h"
#include "board.h"
#include "context.h"
#include "miblo_cues.h"
#include "miblo_daily.h"
#include "miblo_dayend.h"
#include "miblo_format.h"
#include "miblo_mood.h"
#include "miblo_overview.h"
#include "miblo_policy.h"
#include "miblo_version.h"
#include "miblo_occasions.h"
#include "miblo_wellness.h"
#include "miblo_zone.h"
#include "platform/crashlog.h"
#include "platform/friends_net.h"
#include "platform/lockouts.h"
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
static bool cueScreen = false;  // this frame's screen is the cue's pulse (set before drawing)
static bool firstFrame = true;
static Lang drawnLang = Lang::En;
static uint32_t lastFrameMs = 0;
static miblo::Pager listPager(3, 5000);  // Overview: 3 session cards per page
static miblo::Pager sessionPager(3, 5000);  // Sessions mode: 3 big cards per page
static miblo::RotationClock rotation;  // optional Overview/Limits alternation
static miblo::QuietClock quiet;        // All done -> Desk while nothing happens
static bool mainLimits = false;        // Main shows the Limits arc instead of the mode's screen
static miblo::DemoBreak demoBreak;
static uint32_t awaySinceMs = 0;       // when the Disconnected screen came up
static uint32_t limitResetMs = 0;      // when the "limit freed" screen came up
// Daily life, timed here (focus, meeting and the notes live in ctx: the API changes them).
static miblo::StrongCue cue;            // slow full-screen pulses: end of focus, timer, alarms
static miblo::WellnessClock wellness;   // break, water, eye rest nudges
static miblo::EndOfDay dayEnd;          // the day's summary at the end of the work hours
static miblo::WeeklyRecap weekly;       // Monday: last week's summary
static miblo::FrameColor frameShown = miblo::FrameColor::None;  // the status frame last drawn
static bool markShown = false;  // the "needs you" mark is over a daily screen
// Saves to flash: at once on a change and, when one fails (no heap for its document at that
// moment, a flash error), again after 1, 2, 4... minutes, at most an hour apart (miblo::SaveRetry).
// The pairings' schedule is ctx.tokensSave (the API and the settings page change them).
static miblo::SaveRetry notesSave;   // notes.json: the alarms/countdown in RAM
static miblo::SaveRetry configSave;  // config.json

// One save attempt's outcome on its schedule.
static void saved(miblo::SaveRetry& r, bool ok, uint32_t now, const __FlashStringHelper* what) {
  if (ok) {
    r.succeeded(now);  // the next write waits SaveRetry::kMinGapMs (flash wear)
    return;
  }
  Serial.print(what);
  Serial.println(F(": save failed, will retry"));
  r.failed(now);
}

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
    case miblo::PresenceGate::Purpose::Settings:
    case miblo::PresenceGate::Purpose::Wifi: return miblo::S::CodeSettings;  // joining a network is a setting
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
static bool displayOff = false;  // the panel is asleep (nobody using it: see miblo::PetLatch)
// Everything is drawn through it: the picture moves a pixel or two every few minutes so nothing
// sits still for hours (LCD ghosting), and it applies the blue light filter. It only keeps a
// reference to the board's canvas, so it can be built before board::begin().
static ui::ShiftCanvas shifted(board::canvas());
static uint8_t shiftStep = 0;    // current pixel-shift position (shifted)
static uint32_t shiftAtMs = 0;
static uint32_t roamSinceMs = 0;  // when pet mode came up
static miblo::PetLatch petLatch;  // pet mode, and when the panel sleeps
// Only compared, never shown: kept as 32-bit hashes (FNV-1a) instead of copies of the text.
static uint32_t shownName = 0;   // the name the screen last knew (a change greets with it)
static uint32_t knownOwner = 0;  // owner name + birthday last applied (a change re-arms today's greeting)
static uint32_t nameHash() { return miblo::hashStr(miblo::kHashSeed, deviceName()); }
static uint32_t ownerHash() {
  return miblo::hashStr(miblo::hashStr(miblo::kHashSeed, ctx.cfg.owner), ctx.cfg.birthday);
}
static uint8_t accessory = 0;     // today's hat (miblo::Accessory), worn if the outfit lets it
static uint8_t holidayHat = 0;    // today's hat for visitors (the holiday's, never our birthday's)
static miblo::Occasion occasion = miblo::Occasion::None;  // today's special day
static uint32_t occasionAtMs = 0;

// Local date, minute of the day and weekday (0 = Sunday); false while the time is unknown.
static bool today(miblo::Date& d, int& minute, uint8_t* weekday = nullptr) {
  const time_t now = time(nullptr);
  if (now <= 1600000000) return false;
  struct tm lt;
  localtime_r(&now, &lt);
  d = miblo::Date{(uint16_t)(lt.tm_year + 1900), (uint8_t)(lt.tm_mon + 1), (uint8_t)lt.tm_mday};
  minute = lt.tm_hour * 60 + lt.tm_min;
  if (weekday) *weekday = (uint8_t)lt.tm_wday;
  return true;
}

// One number per local day (miblo_dayend.h, miblo_desknotes.h); 0 = the time is unknown.
static uint32_t dayKeyOf(const miblo::Date& d) { return d.year * 400u + d.month * 32u + d.day; }

// What the pet wears: today's special accessory and the owner's items (miblo::outfitFor).
static void applyOutfit() {
  const miblo::Outfit o = miblo::outfitFor(ctx.cfg, (miblo::Accessory)accessory);
  screens::setMascotAccessory((uint8_t)o.occasion);
  screens::setMascotOutfit(screens::MascotOutfit{o.head, o.face, o.neck});
  // Guests wear the holiday's hat over their own items only if we let special days dress pets.
  screens::setGuestAccessory(ctx.cfg.occasionHats ? holidayHat : 0);
}

// Once a minute: today's hat, and the gadget's own birthday noted on the first day it is used.
static void updateOccasion(uint32_t now) {
  if (occasionAtMs && now - occasionAtMs < 60000) return;
  occasionAtMs = now;
  miblo::Date d;
  int minute;
  uint8_t want = 0, guests = 0;
  occasion = miblo::Occasion::None;
  if (today(d, minute)) {
    occasion = miblo::occasionOn(ctx.cfg, d);
    want = (uint8_t)miblo::accessoryFor(occasion);
    guests = (uint8_t)miblo::accessoryFor(miblo::holidayOn(d));  // visitors: the holiday, not our birthday
    if (!ctx.cfg.born[0] && ctx.tokens.count() > 0) {
      snprintf_P(ctx.cfg.born, sizeof(ctx.cfg.born), PSTR("%04u-%02u-%02u"), (unsigned)d.year, (unsigned)d.month,
               (unsigned)d.day);
      ctx.configChanged = true;
    }
  }
  if (want != accessory || guests != holidayHat) {
    accessory = want;
    holidayHat = guests;
    applyOutfit();
    firstFrame = true;
  }
}

// Once a minute (when the local minute changes, or after a settings change): the cat's mood, the
// second clock and the Desk's extras (countdown line, settings QR). A changed mood, label,
// countdown or QR redraws everything; the second clock's time alone does not (the screens draw it
// as a field).
// While the time is unknown it still runs once a minute of uptime (the mood, the QR).
static int dailyLookMinute = -2;  // -2: recompute now
static bool lookConnected = false;  // the Wi-Fi state the QR was decided with
static void updateDailyLook(int minuteNow, const miblo::Date& day, bool timeKnown, uint32_t epoch, uint32_t nowMs) {
  if (net::connected() != lookConnected) {  // the QR's address appeared or went away
    lookConnected = net::connected();
    dailyLookMinute = -2;
  }
  const int key = minuteNow >= 0 ? minuteNow : -3 - (int)((nowMs / 60000) & 0xFFFF);
  if (key == dailyLookMinute) return;
  dailyLookMinute = key;
  bool changed = false;
  const uint8_t mood = (uint8_t)miblo::catMoodFor(ctx.snap, epoch ? epoch : ctx.snap.now);
  if (mood != screens::catMood()) {
    screens::setCatMood(mood);
    changed = true;
  }
  char label[37] = "", hhmm[6] = "";
  if (ctx.cfg.tz2[0] && miblo::zoneHHMM(ctx.cfg.tz2, epoch, hhmm, sizeof(hhmm), &ctx.liveTz)) {
    miblo::zoneLabel(ctx.cfg, label, sizeof(label));
  } else {
    hhmm[0] = 0;
  }
  if (strcmp(label, screens::secondClockLabel()) != 0) changed = true;
  screens::setSecondClock(label, hhmm);
  char line[64] = "", url[32] = "";
  if (timeKnown) miblo::countdownLine(uiLang(), ctx.notes.countdown(), day, line, sizeof(line));
  if (ctx.cfg.deskQr && net::connected()) snprintf_P(url, sizeof(url), PSTR("http://%s/"), net::ip().c_str());
  if (strcmp(line, screens::deskCountdown()) != 0 || strcmp(url, screens::deskQrUrl()) != 0) changed = true;
  screens::setDeskExtras(line, url);
  if (changed) firstFrame = true;
}

// Local minute of the day, or -1 while the time is unknown.
static int minuteOfDay() {
  miblo::Date d;
  int minute;
  return today(d, minute) ? minute : -1;
}

// Brightness at `minute` (local minute of the day, -1 = unknown; night mode); only touches the
// board when it changes.
static void updateBacklight(int minute) {
  if (displayOff) return;  // stays dark until the panel wakes
  // A strong cue lights the screen up (never more than twice the night brightness at night), only
  // while its pulses are on screen: an alert that interrupts it shows at the normal brightness.
  const bool cueOn = cue.active(millis()) != miblo::CueKind::None && cueScreen;
  const uint8_t want = cueOn ? miblo::cueBrightness(ctx.cfg, minute) : miblo::brightnessAt(ctx.cfg, minute);
  if (want == backlight) return;
  backlight = want;
  board::setBacklight(want);
}

// Blue light filter at `minute` (as updateBacklight); when its strength changes, everything is
// redrawn in the new colours (what is on the panel was drawn with the old ones).
static void updateWarmth(int minute) {
  const uint8_t want = miblo::warmthAt(ctx.cfg, minute);
  if (want == shifted.warmth()) return;
  shifted.setWarmth(want);
  firstFrame = true;
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
  const int minute = minuteOfDay();
  updateBacklight(minute);
  updateWarmth(minute);
  screens::MascotPaint paint{ctx.cfg.mascot, ctx.cfg.pet, ctx.cfg.petEyes};
  memcpy(paint.slots, ctx.cfg.petColors, sizeof(paint.slots));
  screens::setMascotPaint(paint);
  applyOutfit();
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
static miblo::CpuMeter cpuMeter;  // the settings page's processing graph
uint8_t cpuLoad() { return cpuMeter.percent(); }

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
  // Through the waiting mark's guard: it notices drawing under its band (screens::waitingMark).
  screens::bind(screens::waitingGuard(shifted));
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
  storage::loadNotes(ctx.notes);  // recurring alarms and the countdown (the rest never survives a reboot)
  storage::loadTokens(ctx.tokens);
  applyConfig();
  shownName = nameHash();
  knownOwner = ownerHash();
  char code[5];
  miblo::formatCode(hwRandom(), code);
  ctx.pairing.setCode(code);
  lockouts::restore(millis());  // a reset never hands out fresh code guesses (M2)

  bootMs = millis();
  ctx.heap.begin(crashlog::heapRestartStreak());
  net::begin(bootMs);
  web::begin(server);
  api::begin(server);
  ota::begin(server, onOtaProgress);
  server.begin();
}

static void frame(uint32_t now);

void loop() {
  const uint32_t now = millis();
  cpuMeter.update(micros());
  // Low-memory guard: while the heap is low, held and new requests are answered 503 busy at once,
  // the setup network is not opened, and pet visits and mDNS replies wait (miblo::HeapGuard).
  const bool heapLow = ctx.heap.update(freeHeap(), maxFreeBlock(), now);
  // Low for a whole minute (longer after restarts in a row): it is not coming back (a fragmented heap). Restart through the
  // reboot path below, which flushes pending saves first. Here no request is in flight; not during
  // an update or while a submitted network is tried.
  // The wait grows after a few such restarts in a row (HeapGuard::restartWaitMs); a healthy
  // stretch ends the streak.
  if (ctx.heap.streakEnded(now)) crashlog::endHeapRestartStreak();
  if (!ctx.rebootRequested && ctx.heap.restartDue(now, ctx.updating || net::trialBusy())) {
    crashlog::noteHeapRestart();  // /api/info "heapRestarts"
    ctx.rebootRequested = true;
    ctx.rebootAtMs = now;
  }
#if defined(ESP8266)
  LookaheadClient::shed(heapLow);
#endif
  server.handleClient();
  net::loop(now, heapLow);
  net::syncTimezone(now);
  mdns::loop(now, heapLow);
  friendsnet::loop(now, heapLow);

  if (!bootCountCleared && now - bootMs >= miblo::kPowerCycleWindowMs) {
    storage::writeBootCount(0);
    bootCountCleared = true;
  }
  if (ctx.tokens.saveDue(now)) ctx.tokensSave.request(now);  // a computer's new host name (its automatic label)
  if (ctx.tokensSave.due(now)) {
    saved(ctx.tokensSave, storage::saveTokens(ctx.tokens), now, F("pairs"));
    ctx.tokens.saved(now);  // any automatic label is in this save (or its retry); the next waits a minute
  }
  if (ctx.configChanged) {
    ctx.configChanged = false;
    configSave.request(now);
    // The time zone first: night dimming and the blue light filter go by the local time, so a new
    // zone must not light the old zone's schedule for a frame (and redraw everything twice).
    net::applyTimezone();  // (no new mDNS announcement: it carries only the id, never the name)
    applyConfig();
    if (nameHash() != shownName) {  // renamed: say hello with the new name
      shownName = nameHash();
      ctx.greeter.named(now);
    }
    if (ownerHash() != knownOwner) {  // told who we are: greet (again) today
      knownOwner = ownerHash();
      ctx.greeter.rearm();
    }
    occasionAtMs = 0;  // a birthday may have been set
    dailyLookMinute = -2;  // the second clock, the QR...
    firstFrame = true;  // language/mode may have changed: redraw everything
  }
  if (configSave.due(now)) saved(configSave, storage::saveConfig(ctx.cfg), now, F("config"));
  if (ctx.factoryResetRequested) {
    ctx.webSession.clear();
    delay(300);  // let the HTTP response go out
    storage::factoryReset();
  }
  if (ctx.rebootRequested && (int32_t)(now - ctx.rebootAtMs) >= 0) {
    // A change still waiting for its spaced write (SaveRetry::kMinGapMs) is not lost.
    if (configSave.pending()) storage::saveConfig(ctx.cfg);
    if (notesSave.pending()) storage::saveNotes(ctx.notes);
    if (ctx.tokensSave.pending()) storage::saveTokens(ctx.tokens);  // a pairing or a rename
    ESP.restart();
  }
  ctx.presence.update(now);  // expire old brute-force lockouts before the clock can wrap
  ctx.pairing.update(now);
  lockouts::persist(now);    // and keep them across a reset (RTC memory)
  if (ctx.showPairCode && now - ctx.pairCodeAtMs >= miblo::kPairCodeScreenMs) ctx.showPairCode = false;

  if (now - lastFrameMs < 100) {  // ~10 frames/s; in between, a pause (the Wi-Fi stack runs in it)
    const uint32_t t0 = micros();
    delay(1);
    cpuMeter.idle(micros() - t0);
    return;
  }
  lastFrameMs = now;
  frame(now);
}

// One frame: the screen's state machine and its drawing. Out of loop() and never inlined, so
// the frame's own buffers and the renderers' stack are not under the HTTP handlers (which run
// from server.handleClient() above, on the same 4 KB loop() stack).
static void __attribute__((noinline)) frame(uint32_t now) {
  if (!bootAnimDone && now - bootMs >= 2400) bootAnimDone = true;
  // The local date and time, read once per frame (night dimming, the filter, greetings).
  miblo::Date day{};
  int minute = 0;
  uint8_t weekday = 0;
  const bool timeKnown = today(day, minute, &weekday);
  const int minuteNow = timeKnown ? minute : -1;
  const uint32_t dayKey = timeKnown ? dayKeyOf(day) : 0;

  // Daily life: the timers move first, so a phase that ended this frame shows its cue now.
  ctx.meeting.update(now);
  const miblo::FocusEvent fe = ctx.focus.update(now);
  if (fe == miblo::FocusEvent::BreakStarted || fe == miblo::FocusEvent::Finished) {
    cue.fire(miblo::CueKind::FocusEnd, now);
  } else if (fe == miblo::FocusEvent::BackPrompt) {
    cue.fire(miblo::CueKind::BreakEnd, now);
  }
  const miblo::NoteKind fired = ctx.notes.update(now, dayKey, weekday, minuteNow);
  if (fired == miblo::NoteKind::Timer) cue.fire(miblo::CueKind::Timer, now);
  else if (fired == miblo::NoteKind::Alarm) cue.fire(miblo::CueKind::Alarm, now);
  else if (fired == miblo::NoteKind::Reminder) cue.fire(miblo::CueKind::Reminder, now);
  if (ctx.notes.takeDirty()) {
    notesSave.request(now);
    dailyLookMinute = -2;  // the countdown may have changed: the Desk's line too
  }
  // A save that failed is tried again later, so an alarm or countdown set then still survives a
  // reboot (a flash that keeps failing is not worn out by it).
  if (notesSave.due(now)) saved(notesSave, storage::saveNotes(ctx.notes), now, F("notes"));
  // Alerts: insistence, a single blink in meetings, "finished" waits out a focus round.
  ctx.alerts.setModifiers({ctx.cfg.insist, ctx.meeting.on(),
                           ctx.focus.phase() == miblo::FocusPhase::Focus && ctx.cfg.focusQuiet});
  const bool discreet = ctx.cfg.discreet || ctx.meeting.on();  // meeting mode hides commands too
  // The tie goes on/off every mascot, and the names off/on every screen: everything is redrawn
  // (the regions that carry a name hash it, so none keeps a stale one).
  if (ctx.meeting.on() != screens::mascotTie() || ctx.meeting.on() != screens::anonymous()) {
    screens::setMascotTie(ctx.meeting.on());
    screens::setAnonymous(ctx.meeting.on());
    firstFrame = true;
  }

  updateWarmth(minuteNow);
  // Before the first snapshot ctx.snap is all zeros (parseSnapshot only writes it on success).
  const miblo::AlertView& alert = ctx.alerts.update(ctx.snap, now);
  // A response that took long ends with a party: its hero becomes the fanfare, and stays longer.
  bool fanfare = false;
  uint32_t fanDur = 0;
  if (alert.phase == miblo::AlertPhase::Hero && alert.kind == miblo::AlertKind::Done && ctx.cfg.fanfareMin &&
      ctx.runs.stats(alert.sid, fanDur) && fanDur >= ctx.cfg.fanfareMin * 60u) {
    fanfare = true;
    ctx.alerts.extendHero(miblo::kFanfareMs);
  }

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
  // Once pet mode starts it stays until real activity (see miblo::PetLatch): a session running or
  // waiting (not a stale one while the computer is away), an alert, setup, pairing or an update.
  // A limit reset, an update notice or the computer dropping out for a moment don't count.
  const bool ordinaryScreen = idleScreen || screen == ScreenId::Main || screen == ScreenId::LimitReset ||
                              screen == ScreenId::UpdateAvailable;
  // Daily life: what is on (the screen it lands on is decided further down). Focus, a timer, a
  // held text, a cue or find count as someone at the desk.
  miblo::DailyInputs di;
  di.fanfare = fanfare;
  di.cue = cue.active(now);
  di.find = ctx.notes.finding(now);
  di.held = ctx.notes.held(now);
  di.focus = ctx.focus.phase();
  di.timer = ctx.notes.timerRunning();
  di.say = ctx.notes.saying(now) != nullptr;
  const bool activity = !ordinaryScreen || (!away && (counts.running > 0 || counts.pending > 0)) ||
                        miblo::dailyActivity(di);
  const uint8_t petMin = dayEnd.petMinutes(ctx.cfg, dayKey);  // sooner once the work day ended
  // Demo (/miblo:demo): pet mode now, over any ordinary screen (alerts and setup still win), until
  // its minutes are up or real activity starts.
  if (demoBreak.update(ctx.demo, activity) || (ctx.demo && (int32_t)(now - ctx.demoUntilMs) >= 0)) {
    if (ctx.demo) ctx.demoKick = true;
    ctx.demo = false;
  }
  const bool demo = ctx.demo && (screen == ScreenId::Main || screen == ScreenId::Desk ||
                                 screen == ScreenId::Summary || screen == ScreenId::Disconnected);
  const bool petOn = petLatch.update(activity, idleMs, sinceSeen, petMin, now);
  const bool pet = petOn || demo;
  if (pet) screen = ScreenId::Roam;
  if (screen == ScreenId::Roam && current != ScreenId::Roam && current != ScreenId::Visit &&
      current != ScreenId::Passerby)  // the black cat passes through pet mode, it does not end it
    roamSinceMs = now;

  // Other Miblos on the network: what we tell them (in pet mode, napping, limits past 80%), and
  // a visit takes over the pet mode screen.
  const uint32_t nowEpoch = clockNow().epoch;
  const bool napping = pet && away && now - awaySinceMs >= miblo::kAwayNapMs;
  const screens::DeskMood limitsMood = screens::deskMoodFor(ctx.snap, nowEpoch ? nowEpoch : ctx.snap.now);
  const bool tired = ctx.usageEverSeen &&
                     (limitsMood == screens::DeskMood::Worried || limitsMood == screens::DeskMood::Scared);
  // Our look as pet mode draws it, with the owner's accessories (never today's special hat: the
  // host dresses its guests for the holiday itself).
  ctx.friends.setSelf(ctx.ident.id, deviceName(), ctx.cfg.mascot, ctx.cfg.pet,
                      miblo::friendLook(ctx.cfg.petColors, ctx.cfg.petEyes, ctx.cfg.accHead, ctx.cfg.accFace,
                                        ctx.cfg.accNeck));
  // Not roaming while the panel sleeps (displayOff is still last frame's): nobody visits a dark
  // screen, and a visit in progress ends the way it does when our human comes back.
  ctx.friends.update(now, ctx.cfg.friends && net::connected(),
                     ((pet && !displayOff) ? miblo::kFriendRoaming : 0) | (napping ? miblo::kFriendNapping : 0) |
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
  ctx.greeter.update(now, screen == ScreenId::Main && counts.running > 0, timeKnown, day, minute, ctx.cfg);
  const miblo::Greeting greeting = ctx.greeter.showing(now);
  if (greeting != miblo::Greeting::None &&
      (screen == ScreenId::Main || screen == ScreenId::Desk || screen == ScreenId::Summary ||
       screen == ScreenId::Disconnected || screen == ScreenId::Roam || screen == ScreenId::Visit ||
       screen == ScreenId::Paired)) {
    screen = ScreenId::Hello;
  }

  // Wellness, the end of the day and Monday's recap only take a plain screen: no alert, focus,
  // meeting, pet mode, timer, note or cue.
  const bool plainScreen = screen == ScreenId::Main || screen == ScreenId::Desk || screen == ScreenId::Summary ||
                           screen == ScreenId::Disconnected;
  // A session waiting keeps the Overview's attention view (a stale one while away does not count).
  const bool quietDesk = plainScreen && (away || counts.pending == 0) && alert.phase == miblo::AlertPhase::None &&
                         di.focus == miblo::FocusPhase::Off && !ctx.meeting.on() && !di.timer &&
                         di.held == miblo::NoteKind::None && !di.say && di.cue == miblo::CueKind::None && !di.find;
  dayEnd.update(now, ctx.cfg, dayKey, weekday, minuteNow, counts.running > 0, ctx.hasSnapshot, quietDesk);
  weekly.update(now, ctx.cfg.weekly, dayKey, weekday, minuteNow, counts.running > 0, ctx.snap.week.present,
                quietDesk);
  di.dayEnd = dayEnd.showing(now);
  di.weekRecap = weekly.showing(now);
  wellness.update(now, ctx.cfg, counts.running > 0, miblo::inWorkHours(ctx.cfg, weekday, minuteNow),
                  quietDesk && !di.dayEnd && !di.weekRecap);
  di.nudge = wellness.showing(now);
  // Friday the 13th: now and then a black cat crosses pet mode.
  uint32_t passAt = 0;
  di.passerby = occasion == miblo::Occasion::Friday13 && screen == ScreenId::Roam &&
                miblo::passerbyAt(now - roamSinceMs, &passAt);
  di.screen = screen;
  screen = miblo::dailyScreen(di);
  cueScreen = screen == ScreenId::Cue;
  updateBacklight(minuteNow);  // after the screen is known: the cue's boost starts and ends with it
  updateDailyLook(minuteNow, day, timeKnown, clockNow().epoch, now);

  // The panel stays on while a /miblo:say note is up (it is meant for passers-by).
  const bool asleep = !ctx.demo && petLatch.asleep(ctx.cfg.sleepMin, petMin, now) && !di.say;
  if (asleep != displayOff) {
    displayOff = asleep;
    board::setDisplay(!asleep);
    if (!asleep) {
      backlight = 0;  // relight at the current brightness
      updateBacklight(minuteNow);
      firstFrame = true;
    }
  }
  if (displayOff) return;
  if (now - shiftAtMs >= miblo::kShiftEveryMs) {
    shiftAtMs = now;
    int8_t dx, dy;
    miblo::pixelShift(++shiftStep, dx, dy);
    shifted.setShift(dx, dy);
    firstFrame = true;  // redraw everything at the new offset
  }
  enter(screen);

  const Lang lang = uiLang();
  const screens::Clock clk = clockNow();
  const uint32_t eta = miblo::etaFor(ctx.snap, ctx.limits);  // the bridge's forecast, else ours
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
      screens::desk(lang, ctx.snap, clk, now, eta);
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
      // The computer away: calm, so the antics play (the crossed-out laptop on the sign says it).
      screens::DeskMood mood =
          away ? screens::DeskMood::Calm : screens::deskMoodFor(ctx.snap, clk.epoch ? clk.epoch : ctx.snap.now);
      char note[96] = "";
      uint32_t lookMs = UINT32_MAX;
      if (const char* hi = ctx.friends.greeting(now)) {
        snprintf(note, sizeof(note), screens::t(lang, S::FriendHi), hi);
        mood = screens::DeskMood::Celebrate;
      } else if (const char* buddy = ctx.friends.napBuddy()) {
        // Napping together: the same wall-clock phase on both screens, so the zzz go in step.
        snprintf(note, sizeof(note), screens::t(lang, S::FriendNap), buddy);
        mood = screens::DeskMood::Asleep;
        if (clk.valid) lookMs = (clk.epoch % 86400) * 1000;
      } else if (const char* say = ctx.notes.saying(now)) {
        snprintf(note, sizeof(note), "%s", say);  // /miblo:say rides on the pet's sign
      }
      screens::roam(lang, ctx.snap, clk, now - roamSinceMs, mood, note, lookMs, away);
      break;
    }
    case ScreenId::Visit:
      screens::visit(lang, ctx.snap, clk, visit, ctx.cfg.friendsSide);
      break;
    case ScreenId::Hello: {
      char l1[64], l2[64];
      miblo::greetingLines(lang, greeting, ctx.cfg.owner, deviceName(), l1, sizeof(l1), l2, sizeof(l2));
      screens::hello(l1, l2, miblo::greetingIsParty(greeting), ctx.greeter.elapsed(now));
      break;
    }
    case ScreenId::AlertFlash: {
      // The name the alert started with: another computer's snapshot (without that row) doesn't
      // change it.
      screens::flash(lang, alert.kind, ctx.alerts.alertName(), now - alert.phaseStartMs, alert.level,
                     ctx.meeting.on());
      break;
    }
    case ScreenId::AlertHero:
      screens::hero(lang, ctx.snap, miblo::findSession(ctx.snap, alert.sid), alert.kind, discreet, clk, ctx.runs,
                    ctx.meeting.on(), ctx.alerts.alertName());
      break;
    case ScreenId::Fanfare: {
      const char* name = ctx.meeting.on() ? "" : ctx.alerts.alertName();
      screens::fanfare(lang, name, fanDur, now - alert.phaseStartMs);
      break;
    }
    case ScreenId::Focus: {
      const uint32_t left = ctx.focus.leftMs(now);
      screens::focus(lang, clk, ctx.focus.phase(), ctx.focus.round(), ctx.focus.plan().rounds, left,
                     ctx.focus.phaseLenMs(), clk.epoch ? clk.epoch + left / 1000 : 0, now);
      break;
    }
    case ScreenId::Timer:
      screens::timer(lang, clk, ctx.notes.timerLeftMs(now), ctx.notes.timerLenMs(), now);
      break;
    case ScreenId::Note: {
      const bool held = di.held != miblo::NoteKind::None;
      const char* text = held ? ctx.notes.heldText(now) : ctx.notes.saying(now);
      screens::note(lang, held ? di.held : miblo::NoteKind::Say, text ? text : "", clk, now);
      break;
    }
    case ScreenId::Cue:
      screens::cue(di.cue, cue.elapsed(now));
      break;
    case ScreenId::Find: {
      char url[32];
      snprintf_P(url, sizeof(url), PSTR("http://%s/"), net::ip().c_str());
      screens::findMe(lang, url, ctx.notes.findElapsed(now));
      break;
    }
    case ScreenId::Nudge:
      screens::nudge(lang, di.nudge, wellness.elapsed(now), ctx.cfg.breakLenMin);
      break;
    case ScreenId::DayEnd:
      screens::dayEnd(lang, ctx.snap, ctx.cfg.owner, dayEnd.elapsed(now));
      break;
    case ScreenId::WeekRecap:
      screens::weekRecap(lang, ctx.snap, weekly.elapsed(now));
      break;
    case ScreenId::Passerby:
      screens::passerby(lang, ctx.snap, clk, passAt);
      break;
    case ScreenId::Main:
      if (mainLimits) {  // a rotation slot, or the arc's turn in the Desk cycle
        screens::limits(lang, ctx.snap, clk, eta);
        break;
      }
      switch (ctx.cfg.mode) {
        case miblo::Mode::Overview:
          screens::overview(lang, ctx.snap, listPager, now, clk, discreet, eta);
          break;
        case miblo::Mode::Limits:
          screens::limits(lang, ctx.snap, clk, eta);
          break;
        case miblo::Mode::Sessions:
          screens::sessions(lang, ctx.snap, sessionPager, now, clk, discreet);
          break;
      }
      break;
  }

  // Overlays, every frame, only over the ordinary and daily-life screens and the alert hero:
  // never over setup, codes, updates, the alert flash or the full-screen pulse. The status frame
  // stays off while the computer is away: its snapshot (a prompt left open) is stale.
  const bool overlays = (miblo::dailyMayReplace(screen) || screen >= ScreenId::Focus || screen == ScreenId::AlertHero) &&
                        screen != ScreenId::Cue;
  const miblo::FrameColor fc = overlays && ctx.cfg.frame && !away
                                   ? miblo::frameColorFor(ctx.snap, clk.epoch ? clk.epoch : ctx.snap.now)
                                   : miblo::FrameColor::None;
  if (overlays && fc == miblo::FrameColor::None && frameShown != miblo::FrameColor::None) {
    firstFrame = true;  // the frame went away: redraw the screen under it next frame
  }
  frameShown = fc;
  // A session waiting for you is never hidden by daily life: an amber mark on the daily screens.
  // Drawn first: the frame and the badge go over it, and what they draw on its band is ignored
  // (waitingOverlaysDrawn), so the band repaints only when the screen itself covers it.
  const bool mark = miblo::waitingMarkOn(screen, counts.pending);
  if (mark) {
    const char* name = "";
    if (!ctx.meeting.on()) {  // meeting mode: no names
      for (uint8_t i = 0; i < ctx.snap.count; i++) {
        const miblo::SessionState st = ctx.snap.sessions[i].st;
        if (st == miblo::SessionState::Perm || st == miblo::SessionState::Question) {
          name = ctx.snap.sessions[i].name;
          break;
        }
      }
    }
    screens::waitingMark(lang, name, counts.pending);
  } else if (markShown && miblo::dailyFullScreen(screen)) {
    firstFrame = true;  // nobody waits any more: redraw the screen under the band
  }
  markShown = mark;
  if (fc != miblo::FrameColor::None) screens::stateFrame(fc);
  if (overlays && ctx.meeting.on()) screens::meetingBadge(lang);
  screens::waitingOverlaysDrawn();
}

}  // namespace app
