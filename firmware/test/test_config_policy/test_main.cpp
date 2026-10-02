#include <ArduinoJson.h>
#include <string.h>
#include <unity.h>

#include "miblo_config.h"
#include "miblo_format.h"
#include "miblo_policy.h"

using namespace miblo;

void setUp() {}
void tearDown() {}

static bool patch(Config& cfg, const char* json, const char** bad = nullptr) {
  StaticJsonDocument<1024> doc;
  TEST_ASSERT_FALSE(deserializeJson(doc, json));
  return applyConfigPatch(cfg, doc.as<JsonObjectConst>(), bad);
}

static void test_defaults_match_spec() {
  Config c;
  TEST_ASSERT_EQUAL(Mode::Overview, c.mode);
  TEST_ASSERT_TRUE(c.alerts);
  AlertTiming t = alertTiming(c);
  TEST_ASSERT_TRUE(t.enabled);
  TEST_ASSERT_EQUAL_UINT32(1500, t.flashMs);
  TEST_ASSERT_EQUAL_UINT32(10000, t.heroPermMs);
  TEST_ASSERT_EQUAL_UINT32(5000, t.heroDoneMs);
  TEST_ASSERT_EQUAL_UINT32(120000, t.reminderMs);
}

static void test_patch_applies_valid_fields_and_ignores_unknown() {
  Config c;
  TEST_ASSERT_TRUE(patch(c, "{\"mode\":\"limits\",\"brightness\":40,\"alerts\":false,\"heroPermSec\":20,"
                            "\"heroDoneSec\":3,\"reminderMin\":0,\"discreet\":true,\"tz\":\"<-03>3\","
                            "\"name\":\"Mesa\",\"lang\":\"pt-BR\",\"future\":123}"));
  TEST_ASSERT_EQUAL(Mode::Limits, c.mode);
  TEST_ASSERT_EQUAL_UINT8(40, c.brightness);
  TEST_ASSERT_FALSE(c.alerts);
  TEST_ASSERT_EQUAL_UINT8(20, c.heroPermSec);
  TEST_ASSERT_EQUAL_UINT8(0, c.reminderMin);
  TEST_ASSERT_TRUE(c.discreet);
  TEST_ASSERT_EQUAL_STRING("<-03>3", c.tz);
  TEST_ASSERT_EQUAL_STRING("Mesa", c.name);
  TEST_ASSERT_TRUE(c.langSet);
  TEST_ASSERT_EQUAL(Lang::PtBR, c.lang);
  TEST_ASSERT_EQUAL_UINT32(0, alertTiming(c).reminderMs);
  TEST_ASSERT_TRUE(patch(c, "{\"lang\":\"\"}"));
  TEST_ASSERT_FALSE(c.langSet);
}

static void test_invalid_patch_changes_nothing() {
  Config c;
  const char* bad = nullptr;
  TEST_ASSERT_FALSE(patch(c, "{\"mode\":\"sessions\",\"brightness\":101}", &bad));
  TEST_ASSERT_EQUAL_STRING("brightness", bad);
  TEST_ASSERT_EQUAL(Mode::Overview, c.mode);  // not even the valid field was applied
  TEST_ASSERT_FALSE(patch(c, "{\"mode\":\"grid\"}", &bad));
  TEST_ASSERT_EQUAL_STRING("mode", bad);
  TEST_ASSERT_FALSE(patch(c, "{\"alerts\":1}", &bad));
  TEST_ASSERT_FALSE(patch(c, "{\"tz\":\"America/Sao Paulo\"}", &bad));
  TEST_ASSERT_FALSE(patch(c, "{\"lang\":\"ja\"}", &bad));
  TEST_ASSERT_FALSE(patch(c, "{\"name\":\"123456789012345678901\"}", &bad));
  TEST_ASSERT_TRUE(patch(c, "{\"name\":\"项目项目项目项目项目项目项目项目项目项目\"}"));  // 20 characters
}

static void test_config_json_roundtrip() {
  Config a;
  TEST_ASSERT_TRUE(patch(a, "{\"mode\":\"sessions\",\"lang\":\"zh\",\"name\":\"X\"}"));
  StaticJsonDocument<1024> doc;
  configToJson(a, doc.to<JsonObject>());
  TEST_ASSERT_EQUAL_STRING("sessions", doc["mode"]);
  TEST_ASSERT_EQUAL_STRING("zh", doc["lang"]);
  Config b;
  TEST_ASSERT_TRUE(applyConfigPatch(b, doc.as<JsonObjectConst>(), nullptr));
  TEST_ASSERT_EQUAL(Mode::Sessions, b.mode);
  TEST_ASSERT_EQUAL(Lang::Zh, b.lang);
  TEST_ASSERT_EQUAL_STRING("X", b.name);
}

// What loadConfig/saveConfig do (src/platform/storage.cpp): the flash round-trip must keep the
// screen language, explicit or automatic (the negotiated one), across a reboot.
static Config storedRoundTrip(const Config& a) {
  StaticJsonDocument<1024> doc;
  configToStored(a, doc.to<JsonObject>());
  char text[1024];
  serializeJson(doc, text, sizeof(text));
  StaticJsonDocument<1024> in;
  TEST_ASSERT_FALSE(deserializeJson(in, text));
  Config b;
  TEST_ASSERT_TRUE(applyConfigPatch(b, in.as<JsonObjectConst>(), nullptr));
  restoreStoredLang(b, in.as<JsonObjectConst>());
  return b;
}

static void test_language_survives_reboot() {
  Config explicitLang;
  TEST_ASSERT_TRUE(patch(explicitLang, "{\"lang\":\"pt-BR\"}"));
  Config b = storedRoundTrip(explicitLang);
  TEST_ASSERT_TRUE(b.langSet);
  TEST_ASSERT_EQUAL(Lang::PtBR, b.lang);

  // Automatic mode: the language negotiated from the browser (web.cpp pageLang) was lost on
  // reboot (configToJson writes "lang":"" then) and the screen fell back to English.
  Config autoLang;
  autoLang.lang = Lang::PtBR;
  b = storedRoundTrip(autoLang);
  TEST_ASSERT_FALSE(b.langSet);
  TEST_ASSERT_EQUAL(Lang::PtBR, b.lang);

  // The API/page view is unchanged: automatic mode still reads as "".
  StaticJsonDocument<512> doc;
  configToJson(autoLang, doc.to<JsonObject>());
  TEST_ASSERT_EQUAL_STRING("", doc["lang"]);
  TEST_ASSERT_TRUE(doc["langAuto"].isNull());

  // Explicit wins over a stale langAuto; a bad langAuto is ignored.
  StaticJsonDocument<256> in;
  deserializeJson(in, "{\"lang\":\"de\",\"langAuto\":\"fr\"}");
  Config c;
  TEST_ASSERT_TRUE(applyConfigPatch(c, in.as<JsonObjectConst>(), nullptr));
  restoreStoredLang(c, in.as<JsonObjectConst>());
  TEST_ASSERT_EQUAL(Lang::De, c.lang);
  deserializeJson(in, "{\"lang\":\"\",\"langAuto\":\"xx\"}");
  Config d;
  TEST_ASSERT_TRUE(applyConfigPatch(d, in.as<JsonObjectConst>(), nullptr));
  restoreStoredLang(d, in.as<JsonObjectConst>());
  TEST_ASSERT_EQUAL(Lang::En, d.lang);
}

static void test_power_cycle_reset_counter() {
  // Five quick power-on boots: no reset; countdown 3, 2, 1 on boots 3, 4 and 5.
  const uint8_t expectedRemaining[] = {0, 0, 3, 2, 1};
  uint8_t stored = 0;
  for (uint8_t boot = 1; boot <= 5; boot++) {
    BootDecision d = decideBoot(stored, true);
    TEST_ASSERT_FALSE(d.factoryReset);
    TEST_ASSERT_EQUAL_UINT8(boot, d.nextCount);
    TEST_ASSERT_EQUAL_UINT8(expectedRemaining[boot - 1], d.remaining);
    stored = d.nextCount;
  }
  // Sixth quick power-on boot: hard reset, counter back to 0, nothing to show.
  BootDecision d = decideBoot(stored, true);
  TEST_ASSERT_TRUE(d.factoryReset);
  TEST_ASSERT_EQUAL_UINT8(0, d.nextCount);
  TEST_ASSERT_EQUAL_UINT8(0, d.remaining);
  TEST_ASSERT_EQUAL_UINT8(6, kPowerCyclesForReset);
}

static void test_non_power_on_boot_keeps_sequence() {
  // A crash/watchdog/OTA restart in the middle keeps the stored count: no increment, no countdown.
  BootDecision d = decideBoot(4, false);
  TEST_ASSERT_FALSE(d.factoryReset);
  TEST_ASSERT_EQUAL_UINT8(4, d.nextCount);
  TEST_ASSERT_EQUAL_UINT8(0, d.remaining);
  d = decideBoot(d.nextCount, true);  // the sequence continues where it was
  TEST_ASSERT_EQUAL_UINT8(5, d.nextCount);
  TEST_ASSERT_FALSE(d.factoryReset);
  TEST_ASSERT_EQUAL_UINT8(1, d.remaining);
  // Out-of-range stored values are clamped (count as 0) on non-power-on boots too.
  d = decideBoot(0xFF, false);
  TEST_ASSERT_EQUAL_UINT8(0, d.nextCount);
  TEST_ASSERT_FALSE(d.factoryReset);
}

static void test_crash_between_quick_power_ons_does_not_break_sequence() {
  uint8_t stored = 0;
  uint8_t powerOns = 0;
  bool reset = false;
  // Power-on, crash, power-on, crash, ... : only the 6th power-on triggers the reset.
  for (int i = 0; i < 20 && !reset; i++) {
    const bool powerOn = (i % 2) == 0;
    BootDecision d = decideBoot(stored, powerOn);
    if (!powerOn) TEST_ASSERT_FALSE(d.factoryReset);  // a crash never triggers the reset
    if (powerOn) powerOns++;
    reset = d.factoryReset;
    stored = d.nextCount;
  }
  TEST_ASSERT_TRUE(reset);
  TEST_ASSERT_EQUAL_UINT8(kPowerCyclesForReset, powerOns);
}

static void test_crash_never_triggers_reset() {
  for (int stored = 0; stored < 256; stored++) {
    BootDecision d = decideBoot((uint8_t)stored, false);
    TEST_ASSERT_FALSE(d.factoryReset);
    TEST_ASSERT_TRUE(d.nextCount < kPowerCyclesForReset);
    TEST_ASSERT_EQUAL_UINT8(0, d.remaining);
  }
}

static void test_erased_flash_counts_as_first_boot() {
  BootDecision d = decideBoot(0xFF, true);
  TEST_ASSERT_EQUAL_UINT8(1, d.nextCount);
  TEST_ASSERT_FALSE(d.factoryReset);
  TEST_ASSERT_EQUAL_UINT8(0, d.remaining);
}

static void test_net_policy_saved_credentials_then_router_down() {
  NetPolicy p;
  p.begin(true, 0);
  TEST_ASSERT_EQUAL(NetState::Connecting, p.state());
  TEST_ASSERT_FALSE(p.apWanted());
  TEST_ASSERT_EQUAL(NetState::Connected, p.update(LinkStatus::Connected, 3000));
  TEST_ASSERT_EQUAL(NetState::Connecting, p.update(LinkStatus::Down, 10000));  // router dropped
  TEST_ASSERT_EQUAL(NetState::Connecting, p.update(LinkStatus::Down, 129999));
  TEST_ASSERT_FALSE(p.apWanted());
  TEST_ASSERT_EQUAL(NetState::Portal, p.update(LinkStatus::Down, 130000));  // 2 min -> setup network
  TEST_ASSERT_TRUE(p.apWanted());
  TEST_ASSERT_EQUAL(NetState::Connected, p.update(LinkStatus::Connected, 200000));  // came back on its own
  TEST_ASSERT_FALSE(p.apWanted());
}

static void test_net_policy_first_boot_and_wrong_password() {
  NetPolicy p;
  p.begin(false, 0);
  TEST_ASSERT_EQUAL(NetState::Portal, p.state());
  TEST_ASSERT_TRUE(p.apWanted());
  p.credentialsSubmitted(5000);
  TEST_ASSERT_EQUAL(NetState::Connecting, p.state());
  TEST_ASSERT_TRUE(p.apWanted());
  TEST_ASSERT_EQUAL(NetState::WrongPassword, p.update(LinkStatus::WrongPassword, 9000));
  TEST_ASSERT_TRUE(p.apWanted());
  TEST_ASSERT_EQUAL(NetState::WrongPassword, p.update(LinkStatus::Down, 20000));
  p.credentialsSubmitted(30000);
  TEST_ASSERT_EQUAL(NetState::Connected, p.update(LinkStatus::Connected, 34000));
  TEST_ASSERT_FALSE(p.trialActive());
  // the setup network lingers so the phone can read the result, then drops
  TEST_ASSERT_TRUE(p.apWanted());
  p.update(LinkStatus::Connected, 34000 + NetPolicy::kApLingerMs - 1);
  TEST_ASSERT_TRUE(p.apWanted());
  p.update(LinkStatus::Connected, 34000 + NetPolicy::kApLingerMs);
  TEST_ASSERT_FALSE(p.apWanted());
}

static void test_classify_disconnect_reasons() {
  TEST_ASSERT_EQUAL(JoinFailure::None, classifyDisconnect(0));
  TEST_ASSERT_EQUAL(JoinFailure::NotFound, classifyDisconnect(201));
  for (uint8_t r : {2, 13, 14, 15, 20, 24, 202, 203, 204}) TEST_ASSERT_EQUAL(JoinFailure::Refused, classifyDisconnect(r));
  for (uint8_t r : {1, 3, 4, 7, 8, 12, 25, 200, 205}) TEST_ASSERT_EQUAL(JoinFailure::Other, classifyDisconnect(r));
}

// Real unit, first setup: the phone stays on the AP, the first attempt fails. The trial must keep
// retrying (auto-reconnect on + a re-issued join every kTrialRetryMs of silence) and then connect.
static void test_trial_retries_until_connected() {
  NetPolicy p;
  p.begin(false, 0);
  TEST_ASSERT_FALSE(NetPolicy::autoReconnectWanted(true, false));  // background: paused with a phone
  TEST_ASSERT_TRUE(NetPolicy::autoReconnectWanted(true, true));    // trial: never paused
  TEST_ASSERT_TRUE(NetPolicy::autoReconnectWanted(false, false));
  p.credentialsSubmitted(1000);
  TEST_ASSERT_TRUE(p.trialActive());
  TEST_ASSERT_FALSE(p.trialRetryDue(10999));
  TEST_ASSERT_TRUE(p.trialRetryDue(11000));  // nothing heard for 10 s: kick it
  TEST_ASSERT_FALSE(p.trialRetryDue(12000));
  p.disconnected(1, 15000);  // one-off failures do not end the trial
  TEST_ASSERT_EQUAL(NetState::Connecting, p.update(LinkStatus::Down, 15000));
  TEST_ASSERT_FALSE(p.trialRetryDue(24999));  // the SDK is still busy retrying
  TEST_ASSERT_TRUE(p.trialRetryDue(25000));
  p.associated(30000);
  TEST_ASSERT_FALSE(p.trialRetryDue(39999));  // waiting for DHCP
  TEST_ASSERT_EQUAL(NetState::Connected, p.update(LinkStatus::Connected, 33000));
  TEST_ASSERT_FALSE(p.trialActive());
  TEST_ASSERT_FALSE(p.trialRetryDue(60000));
}

static void test_trial_network_not_found_fails_after_20s() {
  NetPolicy p;
  p.begin(false, 0);
  p.credentialsSubmitted(1000);
  uint32_t t = 3000;
  for (; t < 1000 + 22000; t += 2500) {
    p.disconnected(201, t);  // NO_AP_FOUND on every SDK retry (5 GHz only / out of range)
    if (t - 3000 < NetPolicy::kSameReasonMs) {
      TEST_ASSERT_EQUAL(NetState::Connecting, p.update(LinkStatus::Down, t));
    }
  }
  TEST_ASSERT_EQUAL(NetState::JoinFailed, p.update(LinkStatus::Down, 3000 + NetPolicy::kSameReasonMs));
  TEST_ASSERT_EQUAL(JoinFailure::NotFound, p.failure());
  TEST_ASSERT_EQUAL_UINT8(201, p.failureCode());
  TEST_ASSERT_TRUE(p.apWanted());
  TEST_ASSERT_FALSE(p.trialActive());
  TEST_ASSERT_EQUAL(ScreenId::JoinFailed, [&] {
    ScreenInputs in;
    in.bootAnimDone = true;
    in.net = p.state();
    return selectScreen(in);
  }());
  // sticky (the setup screen keeps the reason) until the next submission
  TEST_ASSERT_EQUAL(NetState::JoinFailed, p.update(LinkStatus::Down, 500000));
  p.credentialsSubmitted(600000);
  TEST_ASSERT_EQUAL(NetState::Connecting, p.state());
  TEST_ASSERT_EQUAL(JoinFailure::None, p.failure());
}

static void test_trial_refused_fails_fast() {
  NetPolicy p;
  p.begin(false, 0);
  p.credentialsSubmitted(0);
  p.disconnected(15, 2000);  // 4-way handshake timeout: wrong password or WPA3/PMF-only router
  p.disconnected(15, 4000);
  TEST_ASSERT_EQUAL(NetState::Connecting, p.update(LinkStatus::Down, 4000));
  p.disconnected(15, 6000);
  TEST_ASSERT_EQUAL(NetState::JoinFailed, p.update(LinkStatus::Down, 6000));
  TEST_ASSERT_EQUAL(JoinFailure::Refused, p.failure());
  TEST_ASSERT_EQUAL_UINT8(15, p.failureCode());
}

static void test_trial_mixed_reasons_do_not_fail_early_then_time_out_with_last_reason() {
  NetPolicy p;
  p.begin(false, 0);
  p.credentialsSubmitted(0);
  for (uint32_t t = 1000; t < NetPolicy::kFallbackMs; t += 1000) {
    p.disconnected(t % 2000 ? 200 : 1, t);  // alternating reasons never count as "the same"
    TEST_ASSERT_EQUAL(NetState::Connecting, p.update(LinkStatus::Down, t));
  }
  TEST_ASSERT_EQUAL(NetState::JoinFailed, p.update(LinkStatus::Down, NetPolicy::kFallbackMs));
  TEST_ASSERT_EQUAL(JoinFailure::Other, p.failure());
  TEST_ASSERT_EQUAL_UINT8(200, p.failureCode());  // the last one seen (t = 119 s)
}

static void test_trial_silent_timeout() {
  NetPolicy p;
  p.begin(false, 0);
  p.credentialsSubmitted(0);
  TEST_ASSERT_EQUAL(NetState::JoinFailed, p.update(LinkStatus::Down, NetPolicy::kFallbackMs));
  TEST_ASSERT_EQUAL(JoinFailure::Timeout, p.failure());
  TEST_ASSERT_EQUAL_STRING("timeout", joinStatusName(p.state(), p.failure(), false));
}

static void test_background_reasons_never_fail_a_saved_network() {
  NetPolicy p;
  p.begin(true, 0);
  for (uint32_t t = 1000; t < 100000; t += 1000) p.disconnected(201, t);
  TEST_ASSERT_EQUAL(NetState::Connecting, p.update(LinkStatus::Down, 100000));
  TEST_ASSERT_EQUAL(NetState::Portal, p.update(LinkStatus::Down, NetPolicy::kFallbackMs));  // as before
  TEST_ASSERT_FALSE(p.trialRetryDue(200000));
}

static void test_join_status_names() {
  TEST_ASSERT_EQUAL_STRING("connecting", joinStatusName(NetState::Portal, JoinFailure::None, true));
  TEST_ASSERT_EQUAL_STRING("connecting", joinStatusName(NetState::Connecting, JoinFailure::None, false));
  TEST_ASSERT_EQUAL_STRING("connected", joinStatusName(NetState::Connected, JoinFailure::None, false));
  TEST_ASSERT_EQUAL_STRING("wrong_password", joinStatusName(NetState::WrongPassword, JoinFailure::None, false));
  TEST_ASSERT_EQUAL_STRING("idle", joinStatusName(NetState::Portal, JoinFailure::None, false));
  TEST_ASSERT_EQUAL_STRING("not_found", joinStatusName(NetState::JoinFailed, JoinFailure::NotFound, false));
  TEST_ASSERT_EQUAL_STRING("refused", joinStatusName(NetState::JoinFailed, JoinFailure::Refused, false));
  TEST_ASSERT_EQUAL_STRING("failed", joinStatusName(NetState::JoinFailed, JoinFailure::Other, false));
}

static void test_screen_selection_order() {
  ScreenInputs in;
  in.nowMs = 100000;
  TEST_ASSERT_EQUAL(ScreenId::Boot, selectScreen(in));
  in.hardResetCountdown = true;
  TEST_ASSERT_EQUAL(ScreenId::HardResetCountdown, selectScreen(in));  // replaces the boot animation
  in.net = NetState::Portal;
  in.bootAnimDone = true;
  TEST_ASSERT_EQUAL(ScreenId::HardResetCountdown, selectScreen(in));  // and the setup screen
  in.hardResetCountdown = false;
  in.net = NetState::Connecting;
  in.bootAnimDone = true;
  TEST_ASSERT_EQUAL(ScreenId::Boot, selectScreen(in));  // connecting
  in.net = NetState::Portal;
  TEST_ASSERT_EQUAL(ScreenId::Setup, selectScreen(in));
  in.net = NetState::WrongPassword;
  TEST_ASSERT_EQUAL(ScreenId::WrongPassword, selectScreen(in));
  in.net = NetState::JoinFailed;
  TEST_ASSERT_EQUAL(ScreenId::JoinFailed, selectScreen(in));
  in.net = NetState::Connected;
  TEST_ASSERT_EQUAL(ScreenId::Welcome, selectScreen(in));
  in.paired = true;
  in.justPaired = true;
  in.pairedAtMs = 97000;
  TEST_ASSERT_EQUAL(ScreenId::Paired, selectScreen(in));
  in.pairedAtMs = 95000;
  TEST_ASSERT_EQUAL(ScreenId::Disconnected, selectScreen(in));  // paired, no snapshot yet
  in.hasSnapshot = true;
  in.lastSnapshotMs = 71000;
  TEST_ASSERT_EQUAL(ScreenId::Main, selectScreen(in));
  in.lastSnapshotMs = 70000;
  TEST_ASSERT_EQUAL(ScreenId::Disconnected, selectScreen(in));  // 30 s with no snapshot
  in.lastSnapshotMs = 99000;
  in.limitReset = true;  // "limit freed": over the main screen, under alerts
  TEST_ASSERT_EQUAL(ScreenId::LimitReset, selectScreen(in));
  in.alert = AlertPhase::Flash;
  TEST_ASSERT_EQUAL(ScreenId::AlertFlash, selectScreen(in));
  in.alert = AlertPhase::Hero;
  TEST_ASSERT_EQUAL(ScreenId::AlertHero, selectScreen(in));
  in.pairCodeRequested = true;
  TEST_ASSERT_EQUAL(ScreenId::PairCode, selectScreen(in));
  in.presenceActive = true;
  TEST_ASSERT_EQUAL(ScreenId::PresenceCode, selectScreen(in));
  in.updating = true;
  TEST_ASSERT_EQUAL(ScreenId::Updating, selectScreen(in));
}

static void test_rotation_config_defaults_validation_and_json() {
  Config c;
  TEST_ASSERT_FALSE(c.rotate);
  TEST_ASSERT_EQUAL_UINT16(60, c.rotateEverySec);
  TEST_ASSERT_EQUAL_UINT16(10, c.rotateShowSec);
  TEST_ASSERT_FALSE(rotationTiming(c).enabled);
  const char* bad = nullptr;
  TEST_ASSERT_TRUE(patch(c, "{\"rotate\":true,\"rotateEverySec\":3600,\"rotateShowSec\":300}"));
  TEST_ASSERT_TRUE(c.rotate);
  TEST_ASSERT_EQUAL_UINT16(3600, c.rotateEverySec);
  TEST_ASSERT_EQUAL_UINT16(300, c.rotateShowSec);
  RotationTiming t = rotationTiming(c);
  TEST_ASSERT_TRUE(t.enabled);
  TEST_ASSERT_EQUAL_UINT32(3600000, t.everyMs);
  TEST_ASSERT_EQUAL_UINT32(300000, t.showMs);
  // Only in Overview mode.
  TEST_ASSERT_TRUE(patch(c, "{\"mode\":\"limits\"}"));
  TEST_ASSERT_FALSE(rotationTiming(c).enabled);
  // Ranges and types.
  TEST_ASSERT_FALSE(patch(c, "{\"rotateEverySec\":9}", &bad));
  TEST_ASSERT_EQUAL_STRING("rotateEverySec", bad);
  TEST_ASSERT_FALSE(patch(c, "{\"rotateEverySec\":3601}", &bad));
  TEST_ASSERT_FALSE(patch(c, "{\"rotateShowSec\":2}", &bad));
  TEST_ASSERT_EQUAL_STRING("rotateShowSec", bad);
  TEST_ASSERT_FALSE(patch(c, "{\"rotateShowSec\":301}", &bad));
  TEST_ASSERT_FALSE(patch(c, "{\"rotate\":1}", &bad));
  TEST_ASSERT_EQUAL_STRING("rotate", bad);
  // show < every, on the merged result; all-or-nothing.
  TEST_ASSERT_FALSE(patch(c, "{\"rotate\":false,\"rotateEverySec\":20,\"rotateShowSec\":20}", &bad));
  TEST_ASSERT_EQUAL_STRING("rotateShowSec", bad);
  TEST_ASSERT_TRUE(c.rotate);
  TEST_ASSERT_EQUAL_UINT16(3600, c.rotateEverySec);
  TEST_ASSERT_FALSE(patch(c, "{\"rotateEverySec\":200}", &bad));  // current show is 300
  TEST_ASSERT_EQUAL_STRING("rotateEverySec", bad);
  TEST_ASSERT_TRUE(patch(c, "{\"rotateEverySec\":20,\"rotateShowSec\":5}"));
  StaticJsonDocument<1024> doc;
  configToJson(c, doc.to<JsonObject>());
  TEST_ASSERT_TRUE(doc["rotate"].as<bool>());
  TEST_ASSERT_EQUAL(20, doc["rotateEverySec"].as<int>());
  TEST_ASSERT_EQUAL(5, doc["rotateShowSec"].as<int>());
  Config b;
  TEST_ASSERT_TRUE(applyConfigPatch(b, doc.as<JsonObjectConst>(), nullptr));
  TEST_ASSERT_TRUE(b.rotate);
  TEST_ASSERT_EQUAL_UINT16(20, b.rotateEverySec);
  TEST_ASSERT_EQUAL_UINT16(5, b.rotateShowSec);
}

// Night mode: off by default (22:00-07:00, 10%), validated all-or-nothing, dims only inside the
// window (overnight or same-day), never above the day brightness, and not while the time is unknown.
static void test_night_mode_config_and_brightness() {
  Config c;
  TEST_ASSERT_FALSE(c.night);
  TEST_ASSERT_EQUAL_UINT16(1320, c.nightFrom);
  TEST_ASSERT_EQUAL_UINT16(420, c.nightTo);
  TEST_ASSERT_EQUAL_UINT8(10, c.nightBrightness);
  TEST_ASSERT_EQUAL_UINT8(80, brightnessAt(c, 23 * 60));  // off: day brightness
  const char* bad = nullptr;
  TEST_ASSERT_TRUE(patch(c, "{\"night\":true}"));
  TEST_ASSERT_EQUAL_UINT8(10, brightnessAt(c, 23 * 60));
  TEST_ASSERT_EQUAL_UINT8(10, brightnessAt(c, 0));
  TEST_ASSERT_EQUAL_UINT8(10, brightnessAt(c, 6 * 60 + 59));
  TEST_ASSERT_EQUAL_UINT8(80, brightnessAt(c, 7 * 60));      // end is exclusive
  TEST_ASSERT_EQUAL_UINT8(80, brightnessAt(c, 21 * 60 + 59));
  TEST_ASSERT_EQUAL_UINT8(10, brightnessAt(c, 22 * 60));     // start is inclusive
  TEST_ASSERT_EQUAL_UINT8(80, brightnessAt(c, -1));          // time unknown
  // Same-day window, and never brighter than the day setting.
  TEST_ASSERT_TRUE(patch(c, "{\"nightFrom\":780,\"nightTo\":840,\"nightBrightness\":50,\"brightness\":30}"));
  TEST_ASSERT_EQUAL_UINT8(30, brightnessAt(c, 800));
  TEST_ASSERT_EQUAL_UINT8(30, brightnessAt(c, 900));
  TEST_ASSERT_FALSE(nightActive(c, 900));
  TEST_ASSERT_TRUE(nightActive(c, 780));
  // Ranges, types, empty window; all-or-nothing.
  TEST_ASSERT_FALSE(patch(c, "{\"nightFrom\":1440}", &bad));
  TEST_ASSERT_EQUAL_STRING("nightFrom", bad);
  TEST_ASSERT_FALSE(patch(c, "{\"nightTo\":-1}", &bad));
  TEST_ASSERT_EQUAL_STRING("nightTo", bad);
  TEST_ASSERT_FALSE(patch(c, "{\"nightBrightness\":0}", &bad));
  TEST_ASSERT_EQUAL_STRING("nightBrightness", bad);
  TEST_ASSERT_FALSE(patch(c, "{\"night\":\"yes\"}", &bad));
  TEST_ASSERT_EQUAL_STRING("night", bad);
  TEST_ASSERT_FALSE(patch(c, "{\"night\":false,\"nightTo\":780}", &bad));
  TEST_ASSERT_EQUAL_STRING("nightTo", bad);
  TEST_ASSERT_TRUE(c.night);
  TEST_ASSERT_FALSE(patch(c, "{\"nightFrom\":840}", &bad));
  TEST_ASSERT_EQUAL_STRING("nightFrom", bad);
  // JSON round trip.
  StaticJsonDocument<1024> doc;
  configToJson(c, doc.to<JsonObject>());
  TEST_ASSERT_TRUE(doc["night"].as<bool>());
  TEST_ASSERT_EQUAL(780, doc["nightFrom"].as<int>());
  TEST_ASSERT_EQUAL(840, doc["nightTo"].as<int>());
  TEST_ASSERT_EQUAL(50, doc["nightBrightness"].as<int>());
  Config b;
  TEST_ASSERT_TRUE(applyConfigPatch(b, doc.as<JsonObjectConst>(), nullptr));
  TEST_ASSERT_TRUE(b.night);
  TEST_ASSERT_EQUAL_UINT16(780, b.nightFrom);
  TEST_ASSERT_EQUAL_UINT16(840, b.nightTo);
  TEST_ASSERT_EQUAL_UINT8(50, b.nightBrightness);
}

static RotationTiming rot(bool on, uint32_t everySec, uint32_t showSec) {
  RotationTiming t;
  t.enabled = on;
  t.everyMs = everySec * 1000;
  t.showMs = showSec * 1000;
  return t;
}

static void test_rotation_default_off_never_shows_limits() {
  RotationClock r;
  RotationTiming t;  // defaults: disabled
  for (uint32_t ms = 0; ms < 600000; ms += 100) TEST_ASSERT_FALSE(r.update(t, false, ms));
}

static void test_rotation_period_and_show_timing() {
  RotationClock r;
  const RotationTiming t = rot(true, 60, 10);
  TEST_ASSERT_FALSE(r.update(t, false, 1000));
  TEST_ASSERT_FALSE(r.update(t, false, 1000 + 49999));
  TEST_ASSERT_TRUE(r.update(t, false, 1000 + 50000));  // Overview for every - show
  TEST_ASSERT_TRUE(r.showingLimits());
  TEST_ASSERT_TRUE(r.update(t, false, 1000 + 59999));
  TEST_ASSERT_FALSE(r.update(t, false, 1000 + 60000));  // Limits for show
  TEST_ASSERT_FALSE(r.update(t, false, 1000 + 109999));
  TEST_ASSERT_TRUE(r.update(t, false, 1000 + 110000));  // one Limits slot every 60 s
  TEST_ASSERT_FALSE(r.update(t, false, 1000 + 120000));
}

static void test_rotation_pending_blocks_and_restarts_wait() {
  RotationClock r;
  const RotationTiming t = rot(true, 20, 5);
  TEST_ASSERT_FALSE(r.update(t, false, 0));
  // Something pending from 10 s to 40 s: never rotates away meanwhile.
  for (uint32_t ms = 10000; ms <= 40000; ms += 100) TEST_ASSERT_FALSE(r.update(t, true, ms));
  TEST_ASSERT_FALSE(r.update(t, false, 40100));
  TEST_ASSERT_FALSE(r.update(t, false, 40000 + 14999));
  TEST_ASSERT_TRUE(r.update(t, false, 40000 + 15000));  // a full Overview wait after the last blocked frame
}

static void test_rotation_alert_preempts_limits_slot() {
  RotationClock r;
  const RotationTiming t = rot(true, 20, 5);
  r.update(t, false, 0);
  TEST_ASSERT_TRUE(r.update(t, false, 15000));
  TEST_ASSERT_FALSE(r.update(t, true, 16000));  // alert: Limits ends immediately
  TEST_ASSERT_FALSE(r.showingLimits());
  TEST_ASSERT_FALSE(r.update(t, false, 17000));  // slot not resumed after the alert
  TEST_ASSERT_FALSE(r.update(t, false, 16000 + 14999));
  TEST_ASSERT_TRUE(r.update(t, false, 16000 + 15000));
}

static void test_rotation_config_change_resets_cycle() {
  RotationClock r;
  RotationTiming t = rot(true, 20, 5);
  r.update(t, false, 0);
  TEST_ASSERT_TRUE(r.update(t, false, 15000));
  t = rot(true, 30, 5);
  TEST_ASSERT_FALSE(r.update(t, false, 16000));  // new timing: back to Overview, wait restarts
  TEST_ASSERT_FALSE(r.update(t, false, 16000 + 24999));
  TEST_ASSERT_TRUE(r.update(t, false, 16000 + 25000));
  // Turning it off and on again also restarts the wait.
  TEST_ASSERT_FALSE(r.update(rot(false, 30, 5), false, 50000));
  TEST_ASSERT_FALSE(r.update(t, false, 50100));
  TEST_ASSERT_FALSE(r.update(t, false, 50100 + 24999));
  TEST_ASSERT_TRUE(r.update(t, false, 50100 + 25000));
  // A malformed timing (show >= every) never shows Limits.
  RotationClock r2;
  for (uint32_t ms = 0; ms < 120000; ms += 500) TEST_ASSERT_FALSE(r2.update(rot(true, 10, 10), false, ms));
}

static void test_rotation_survives_millis_wrap() {
  RotationClock r;
  const RotationTiming t = rot(true, 20, 5);
  const uint32_t start = 0xFFFFFFFFu - 5000;
  TEST_ASSERT_FALSE(r.update(t, false, start));
  TEST_ASSERT_FALSE(r.update(t, false, start + 14999));  // wraps past 0
  TEST_ASSERT_TRUE(r.update(t, false, start + 15000));
  TEST_ASSERT_TRUE(r.update(t, false, start + 19999));
  TEST_ASSERT_FALSE(r.update(t, false, start + 20000));
}

// Quiet spells: All done, then (after kAllDoneMs) the Desk cycle: mascot, Limits arc, mascot,
// today's summary, and around; any activity starts over, and the cycle survives a millis() wrap.
static void test_quiet_clock_phases() {
  QuietClock q;
  TEST_ASSERT_EQUAL_INT((int)QuietPhase::Busy, (int)q.update(false, 0));
  TEST_ASSERT_EQUAL_INT((int)QuietPhase::AllDone, (int)q.update(true, 1000));
  TEST_ASSERT_EQUAL_INT((int)QuietPhase::AllDone, (int)q.update(true, 1000 + kAllDoneMs - 1));
  const uint32_t d = 1000 + kAllDoneMs;
  const uint32_t cycle = 2 * kDeskCatMs + kDeskArcMs + kDeskSummaryMs;
  TEST_ASSERT_EQUAL_INT((int)QuietPhase::Desk, (int)q.update(true, d));
  TEST_ASSERT_EQUAL_INT((int)QuietPhase::Desk, (int)q.update(true, d + kDeskCatMs - 1));
  TEST_ASSERT_EQUAL_INT((int)QuietPhase::Arc, (int)q.update(true, d + kDeskCatMs));
  TEST_ASSERT_EQUAL_INT((int)QuietPhase::Desk, (int)q.update(true, d + kDeskCatMs + kDeskArcMs));
  TEST_ASSERT_EQUAL_INT((int)QuietPhase::Summary, (int)q.update(true, d + 2 * kDeskCatMs + kDeskArcMs));
  TEST_ASSERT_EQUAL_INT((int)QuietPhase::Summary, (int)q.update(true, d + cycle - 1));
  TEST_ASSERT_EQUAL_INT((int)QuietPhase::Desk, (int)q.update(true, d + cycle));
  TEST_ASSERT_EQUAL_INT((int)QuietPhase::Busy, (int)q.update(false, 2000));
  // Across the wrap: All done ends past zero, and the cycle keeps its rhythm.
  TEST_ASSERT_EQUAL_INT((int)QuietPhase::AllDone, (int)q.update(true, 0xFFFFF000u));
  const uint32_t w = 0xFFFFF000u + kAllDoneMs;  // wraps
  TEST_ASSERT_EQUAL_INT((int)QuietPhase::Desk, (int)q.update(true, w));
  TEST_ASSERT_EQUAL_INT((int)QuietPhase::Arc, (int)q.update(true, w + kDeskCatMs));
}

// Mascot colours: 0..kMascotStyles-1, default 0, round trip.
static void test_mascot_style_config() {
  Config c;
  TEST_ASSERT_EQUAL_UINT8(0, c.mascot);
  const char* bad = nullptr;
  TEST_ASSERT_TRUE(patch(c, "{\"mascot\":3}"));
  TEST_ASSERT_EQUAL_UINT8(3, c.mascot);
  TEST_ASSERT_FALSE(patch(c, "{\"mascot\":4}", &bad));
  TEST_ASSERT_EQUAL_STRING("mascot", bad);
  TEST_ASSERT_FALSE(patch(c, "{\"mascot\":\"orange\"}", &bad));
  StaticJsonDocument<1024> doc;
  configToJson(c, doc.to<JsonObject>());
  TEST_ASSERT_EQUAL(3, doc["mascot"].as<int>());
  Config b;
  TEST_ASSERT_TRUE(applyConfigPatch(b, doc.as<JsonObjectConst>(), nullptr));
  TEST_ASSERT_EQUAL_UINT8(3, b.mascot);
}

// Screen care: the pixel shift goes round 9 distinct positions within 2 px; after a long idle the
// mascot wanders (pet mode, after petMin minutes) and after sleepMin minutes the panel sleeps
// (0 = never); a page view or a code request keeps the normal screens up.
// Pet mode latches after petMin idle minutes and holds until real activity; the panel turns off
// (afterMin - petMin) minutes into it, afterMin being sleepMin, or petMin + 15 when sleepMin is
// not later than petMin.
static void test_pet_latch() {
  const uint32_t never = UINT32_MAX, min = 60000, t0 = 1000;
  PetLatch p;
  TEST_ASSERT_FALSE(p.update(false, 15 * min - 1, never, 15, t0));
  TEST_ASSERT_FALSE(p.asleep(60, 15, t0 + 120 * min));
  TEST_ASSERT_TRUE(p.update(false, 15 * min, never, 15, t0));
  // a LimitReset celebration, an update notice, the computer flapping: the idle count restarts,
  // pet mode stays and so does its sleep count
  TEST_ASSERT_TRUE(p.update(false, 0, never, 15, t0 + 10 * min));
  TEST_ASSERT_TRUE(p.update(false, 1000, never, 15, t0 + 20 * min));
  TEST_ASSERT_FALSE(p.asleep(60, 15, t0 + 45 * min - 1));
  TEST_ASSERT_TRUE(p.asleep(60, 15, t0 + 45 * min));
  TEST_ASSERT_FALSE(p.asleep(0, 15, t0 + 1000 * min));  // 0 = never
  // never before pet mode has had its turn: 15 more minutes when sleepMin <= petMin
  TEST_ASSERT_FALSE(p.asleep(15, 15, t0 + 15 * min - 1));
  TEST_ASSERT_TRUE(p.asleep(15, 15, t0 + 15 * min));
  TEST_ASSERT_FALSE(p.asleep(30, 60, t0 + 15 * min - 1));
  TEST_ASSERT_TRUE(p.asleep(30, 60, t0 + 15 * min));
  // real activity (a session, an alert, setup) ends it and wakes the panel
  TEST_ASSERT_FALSE(p.update(true, 0, never, 15, t0 + 50 * min));
  TEST_ASSERT_FALSE(p.asleep(60, 15, t0 + 50 * min));
  TEST_ASSERT_FALSE(p.update(false, 0, never, 15, t0 + 51 * min));  // quiet again: counts afresh
  // latched again later: the sleep count starts from then
  TEST_ASSERT_TRUE(p.update(false, 15 * min, never, 15, t0 + 66 * min));
  TEST_ASSERT_FALSE(p.asleep(60, 15, t0 + 111 * min - 1));
  TEST_ASSERT_TRUE(p.asleep(60, 15, t0 + 111 * min));
  // someone at the gadget ends it too, even on an idle screen
  TEST_ASSERT_FALSE(p.update(false, 200 * min, kInteractionAwakeMs - 1, 15, t0 + 120 * min));
  TEST_ASSERT_FALSE(p.asleep(60, 15, t0 + 500 * min));

  // safe across millis() wrap
  PetLatch w;
  const uint32_t late = UINT32_MAX - 10 * min;
  TEST_ASSERT_TRUE(w.update(false, 15 * min, never, 15, late));
  TEST_ASSERT_TRUE(w.update(false, 0, never, 15, late + 20 * min));
  TEST_ASSERT_FALSE(w.asleep(60, 15, late + 45 * min - 1));
  TEST_ASSERT_TRUE(w.asleep(60, 15, late + 45 * min));
}

static void test_screen_care() {
  bool seen[5][5] = {};
  for (uint8_t i = 0; i < kShiftSteps; i++) {
    int8_t dx, dy;
    pixelShift(i, dx, dy);
    TEST_ASSERT_TRUE(dx >= -2 && dx <= 2 && dy >= -2 && dy <= 2);
    TEST_ASSERT_FALSE(seen[dx + 2][dy + 2]);
    seen[dx + 2][dy + 2] = true;
  }
  int8_t dx, dy;
  pixelShift(0, dx, dy);
  TEST_ASSERT_TRUE(dx == 0 && dy == 0);
  pixelShift(kShiftSteps, dx, dy);  // wraps around
  TEST_ASSERT_TRUE(dx == 0 && dy == 0);

  const uint32_t never = UINT32_MAX;
  // pet mode after petMin minutes of idle, unless someone just looked at the gadget
  TEST_ASSERT_FALSE(petMode(5 * 60000 - 1, never, 5));
  TEST_ASSERT_TRUE(petMode(5 * 60000, never, 5));
  TEST_ASSERT_TRUE(petMode(60000, never, 1));
  TEST_ASSERT_FALSE(petMode(5 * 60000, kInteractionAwakeMs - 1, 5));
  TEST_ASSERT_FALSE(petMode(0, never, 1));  // in use

  QuietClock q;
  TEST_ASSERT_EQUAL_UINT32(0, q.quietMs(5000));
  q.update(true, 1000);
  TEST_ASSERT_EQUAL_UINT32(4000, q.quietMs(5000));
  q.update(false, 6000);
  TEST_ASSERT_EQUAL_UINT32(0, q.quietMs(7000));

  Config c;
  TEST_ASSERT_EQUAL_UINT16(60, c.sleepMin);
  const char* bad = nullptr;
  TEST_ASSERT_TRUE(patch(c, "{\"sleepMin\":0}"));
  TEST_ASSERT_EQUAL_UINT16(0, c.sleepMin);
  TEST_ASSERT_FALSE(patch(c, "{\"sleepMin\":241}", &bad));
  TEST_ASSERT_EQUAL_STRING("sleepMin", bad);
  // pet mode delay: 15 min by default, 1..60
  TEST_ASSERT_EQUAL_UINT8(15, c.petMin);
  TEST_ASSERT_TRUE(patch(c, "{\"petMin\":1}"));
  TEST_ASSERT_EQUAL_UINT8(1, c.petMin);
  TEST_ASSERT_TRUE(patch(c, "{\"petMin\":60}"));
  TEST_ASSERT_EQUAL_UINT8(60, c.petMin);
  TEST_ASSERT_TRUE(patch(c, "{\"petMin\":5}"));
  TEST_ASSERT_EQUAL_UINT8(5, c.petMin);
  TEST_ASSERT_FALSE(patch(c, "{\"petMin\":0}", &bad));
  TEST_ASSERT_EQUAL_STRING("petMin", bad);
  TEST_ASSERT_FALSE(patch(c, "{\"petMin\":61}", &bad));
  TEST_ASSERT_EQUAL_STRING("petMin", bad);
  TEST_ASSERT_EQUAL_UINT8(5, c.petMin);
  TEST_ASSERT_EQUAL_UINT8(5, storedRoundTrip(c).petMin);  // survives a reboot
}

// Version order and the once-per-boot "update available" notice.
// The demo ends on its own after its minutes, or as soon as real activity starts: work already
// going on when it was asked for (the prompt that ran /miblo:demo) doesn't end it, the next does.
static void test_demo_ends_on_new_activity() {
  DemoBreak d;
  TEST_ASSERT_FALSE(d.update(true, true));   // asked for mid-turn: that turn doesn't count
  TEST_ASSERT_FALSE(d.update(true, true));
  TEST_ASSERT_FALSE(d.update(true, false));  // the turn ended: pet mode plays
  TEST_ASSERT_TRUE(d.update(true, true));    // a new prompt: the demo ends
  TEST_ASSERT_FALSE(d.update(true, true));   // once
  // outside a demo, activity coming and going ends nothing
  DemoBreak o;
  TEST_ASSERT_FALSE(o.update(false, false));
  TEST_ASSERT_FALSE(o.update(false, true));
  // a demo asked for while all was quiet ends at the first activity
  DemoBreak q;
  TEST_ASSERT_FALSE(q.update(false, false));
  TEST_ASSERT_TRUE(q.update(true, true));
}

static void test_update_notice() {
  TEST_ASSERT_TRUE(compareVersions("1.1.0", "1.0.9") > 0);
  TEST_ASSERT_TRUE(compareVersions("1.0.2", "1.0.10") < 0);
  TEST_ASSERT_EQUAL_INT(0, compareVersions("1.2.3", "1.2.3"));
  TEST_ASSERT_EQUAL_INT(0, compareVersions("1.2.3-rc", "1.2.3"));
  TEST_ASSERT_TRUE(compareVersions("2", "1.9.9") > 0);

  UpdateNotice u;
  u.observe("", "1.0.1", 1000);             // the plugin doesn't know the release yet
  TEST_ASSERT_FALSE(u.showing(1000));
  u.observe("1.1.0", "1.0.1", 2000);        // first snapshot that knows it: newer
  TEST_ASSERT_TRUE(u.showing(2000));
  TEST_ASSERT_TRUE(u.showing(2000 + UpdateNotice::kShowMs - 1));
  TEST_ASSERT_FALSE(u.showing(2000 + UpdateNotice::kShowMs));
  u.observe("1.2.0", "1.0.1", 9000);        // once per boot
  TEST_ASSERT_FALSE(u.showing(9000));

  UpdateNotice same;
  same.observe("1.0.1", "1.0.1", 1000);     // up to date: nothing, and nothing later either
  TEST_ASSERT_FALSE(same.showing(1000));
  same.observe("9.9.9", "1.0.1", 2000);
  TEST_ASSERT_FALSE(same.showing(2000));

  ScreenInputs in;
  in.nowMs = 100000;
  in.bootAnimDone = true;
  in.net = NetState::Connected;
  in.paired = true;
  in.hasSnapshot = true;
  in.lastSnapshotMs = 99000;
  in.updateNotice = true;
  TEST_ASSERT_EQUAL(ScreenId::UpdateAvailable, selectScreen(in));
  in.alert = AlertPhase::Flash;  // alerts come first
  TEST_ASSERT_EQUAL(ScreenId::AlertFlash, selectScreen(in));
}

// "All done" only after something actually finished: a new, idle session (e.g. after the
// computer was away) goes straight to the Desk cycle.
static void test_all_done_only_after_a_finish() {
  QuietClock q;
  TEST_ASSERT_EQUAL_INT((int)QuietPhase::Desk, (int)q.update(true, 1000, false));
  TEST_ASSERT_EQUAL_INT((int)QuietPhase::Desk, (int)q.update(true, 1000 + kAllDoneMs, false));
  TEST_ASSERT_EQUAL_INT((int)QuietPhase::Arc, (int)q.update(true, 1000 + kDeskCatMs, false));
  QuietClock done;
  TEST_ASSERT_EQUAL_INT((int)QuietPhase::AllDone, (int)done.update(true, 1000, true));
  // the flag only matters when quiet starts
  TEST_ASSERT_EQUAL_INT((int)QuietPhase::AllDone, (int)done.update(true, 2000, false));
  TEST_ASSERT_EQUAL_INT((int)QuietPhase::Desk, (int)done.update(true, 1000 + kAllDoneMs, false));
}

// Alert blinks: 2 by default, 2..5, each blink 750 ms of flash.
static void test_flash_blinks() {
  Config c;
  TEST_ASSERT_EQUAL_UINT8(2, c.flashBlinks);
  TEST_ASSERT_EQUAL_UINT32(1500, alertTiming(c).flashMs);
  const char* bad = nullptr;
  TEST_ASSERT_TRUE(patch(c, "{\"flashBlinks\":5}"));
  TEST_ASSERT_EQUAL_UINT32(5 * kBlinkMs, alertTiming(c).flashMs);
  TEST_ASSERT_FALSE(patch(c, "{\"flashBlinks\":1}", &bad));
  TEST_ASSERT_EQUAL_STRING("flashBlinks", bad);
  TEST_ASSERT_FALSE(patch(c, "{\"flashBlinks\":6}", &bad));
  StaticJsonDocument<1024> doc;
  configToJson(c, doc.to<JsonObject>());
  TEST_ASSERT_EQUAL(5, doc["flashBlinks"].as<int>());
}

// Blue light filter (its own schedule, separate from night dimming): off by default; 1 always,
// 2 between blueFrom and blueTo (overnight windows too); strength 1..3; survives a reboot.
static void test_blue_filter() {
  Config c;
  TEST_ASSERT_EQUAL_UINT8(0, c.blueFilter);
  TEST_ASSERT_EQUAL_UINT8(2, c.blueLevel);
  TEST_ASSERT_EQUAL_UINT16(21 * 60, c.blueFrom);
  TEST_ASSERT_EQUAL_UINT16(7 * 60, c.blueTo);
  TEST_ASSERT_EQUAL_UINT8(0, warmthAt(c, 23 * 60));  // off

  TEST_ASSERT_TRUE(patch(c, "{\"blueFilter\":1,\"blueLevel\":3}"));
  TEST_ASSERT_EQUAL_UINT8(3, warmthAt(c, 12 * 60));
  TEST_ASSERT_EQUAL_UINT8(3, warmthAt(c, -1));  // always: even before the clock is set

  // Scheduled: its own hours, whatever night dimming says.
  TEST_ASSERT_TRUE(patch(c, "{\"blueFilter\":2,\"blueFrom\":1290,\"blueTo\":390,\"night\":true,"
                            "\"nightFrom\":600,\"nightTo\":660}"));
  TEST_ASSERT_EQUAL_UINT8(3, warmthAt(c, 21 * 60 + 30));
  TEST_ASSERT_EQUAL_UINT8(3, warmthAt(c, 0));
  TEST_ASSERT_EQUAL_UINT8(3, warmthAt(c, 6 * 60 + 29));
  TEST_ASSERT_EQUAL_UINT8(0, warmthAt(c, 6 * 60 + 30));
  TEST_ASSERT_EQUAL_UINT8(0, warmthAt(c, 10 * 60 + 30));  // night dimming's hours: not the filter's
  TEST_ASSERT_EQUAL_UINT8(0, warmthAt(c, -1));            // unknown time: not scheduled
  TEST_ASSERT_TRUE(patch(c, "{\"blueFrom\":480,\"blueTo\":1020}"));  // a daytime window
  TEST_ASSERT_EQUAL_UINT8(3, warmthAt(c, 12 * 60));
  TEST_ASSERT_EQUAL_UINT8(0, warmthAt(c, 17 * 60));

  const char* bad = nullptr;
  TEST_ASSERT_FALSE(patch(c, "{\"blueFilter\":3}", &bad));
  TEST_ASSERT_EQUAL_STRING("blueFilter", bad);
  TEST_ASSERT_FALSE(patch(c, "{\"blueLevel\":0}", &bad));
  TEST_ASSERT_EQUAL_STRING("blueLevel", bad);
  TEST_ASSERT_FALSE(patch(c, "{\"blueLevel\":4}", &bad));
  TEST_ASSERT_FALSE(patch(c, "{\"blueFrom\":1440}", &bad));
  TEST_ASSERT_EQUAL_STRING("blueFrom", bad);
  TEST_ASSERT_FALSE(patch(c, "{\"blueFrom\":600,\"blueTo\":600}", &bad));  // an empty window
  TEST_ASSERT_EQUAL_UINT8(2, c.blueFilter);  // a rejected patch changes nothing
  TEST_ASSERT_FALSE(patch(c, "{\"blueFrom\":1020}", &bad));  // moved onto the end: that field is named
  TEST_ASSERT_EQUAL_STRING("blueFrom", bad);
  TEST_ASSERT_EQUAL_UINT16(480, c.blueFrom);
  TEST_ASSERT_FALSE(patch(c, "{\"blueTo\":480}", &bad));
  TEST_ASSERT_EQUAL_STRING("blueTo", bad);

  // The page and the API read the four settings back.
  StaticJsonDocument<1024> doc;
  configToJson(c, doc.to<JsonObject>());
  TEST_ASSERT_EQUAL(2, doc["blueFilter"].as<int>());
  TEST_ASSERT_EQUAL(3, doc["blueLevel"].as<int>());
  TEST_ASSERT_EQUAL(480, doc["blueFrom"].as<int>());
  TEST_ASSERT_EQUAL(1020, doc["blueTo"].as<int>());

  // A config saved before the filter existed loads with it off.
  Config old;
  TEST_ASSERT_TRUE(patch(old, "{\"night\":true,\"nightFrom\":1320,\"nightTo\":420}"));
  TEST_ASSERT_EQUAL_UINT8(0, old.blueFilter);
  TEST_ASSERT_EQUAL_UINT8(0, warmthAt(old, 23 * 60));

  const Config r = storedRoundTrip(c);
  TEST_ASSERT_EQUAL_UINT8(2, r.blueFilter);
  TEST_ASSERT_EQUAL_UINT8(3, r.blueLevel);
  TEST_ASSERT_EQUAL_UINT16(480, r.blueFrom);
  TEST_ASSERT_EQUAL_UINT16(1020, r.blueTo);
}

// The saved config is read back into a kConfigJsonCapacity-byte JSON document (storage.cpp
// loadConfig), and the settings page posts it whole into another (web.cpp handleSettings): a
// document that is too small fails the load and every setting falls back to the defaults. Worst
// case: every string at its byte limit, which must leave a third of the document free for keys
// to come. On the ESP8266 each member takes 16 bytes (ArduinoJson 6, 32-bit); the host's slots
// are bigger, so the usage is converted.
static void test_stored_config_fits_on_the_gadget() {
  Config c;
  memset(c.name, 'n', sizeof(c.name) - 1);
  memset(c.owner, 'o', sizeof(c.owner) - 1);
  memset(c.tz, 't', sizeof(c.tz) - 1);
  strcpy(c.birthday, "12-31");
  strcpy(c.born, "2026-10-01");
  c.mode = Mode::Overview;  // the longest mode code
  c.langSet = false;        // stored with "langAuto" too
  StaticJsonDocument<4096> out;
  configToStored(c, out.to<JsonObject>());
  char text[2048];
  const size_t len = serializeJson(out, text, sizeof(text));
  TEST_ASSERT_TRUE(len < sizeof(text) - 1);
  DynamicJsonDocument in(8192);
  TEST_ASSERT_FALSE(deserializeJson(in, (const char*)text));  // const: strings copied, as from a file
  const size_t members = in.as<JsonObjectConst>().size();
  const size_t onGadget = in.memoryUsage() - JSON_OBJECT_SIZE(members) + 16 * members;
  TEST_ASSERT_TRUE_MESSAGE(onGadget * 3 <= kConfigJsonCapacity * 2,
                           "a worst-case stored config leaves less than a third of kConfigJsonCapacity free");
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_match_spec);
  RUN_TEST(test_patch_applies_valid_fields_and_ignores_unknown);
  RUN_TEST(test_invalid_patch_changes_nothing);
  RUN_TEST(test_config_json_roundtrip);
  RUN_TEST(test_language_survives_reboot);
  RUN_TEST(test_power_cycle_reset_counter);
  RUN_TEST(test_non_power_on_boot_keeps_sequence);
  RUN_TEST(test_crash_between_quick_power_ons_does_not_break_sequence);
  RUN_TEST(test_crash_never_triggers_reset);
  RUN_TEST(test_erased_flash_counts_as_first_boot);
  RUN_TEST(test_net_policy_saved_credentials_then_router_down);
  RUN_TEST(test_net_policy_first_boot_and_wrong_password);
  RUN_TEST(test_screen_selection_order);
  RUN_TEST(test_classify_disconnect_reasons);
  RUN_TEST(test_trial_retries_until_connected);
  RUN_TEST(test_trial_network_not_found_fails_after_20s);
  RUN_TEST(test_trial_refused_fails_fast);
  RUN_TEST(test_trial_mixed_reasons_do_not_fail_early_then_time_out_with_last_reason);
  RUN_TEST(test_trial_silent_timeout);
  RUN_TEST(test_background_reasons_never_fail_a_saved_network);
  RUN_TEST(test_join_status_names);
  RUN_TEST(test_rotation_config_defaults_validation_and_json);
  RUN_TEST(test_rotation_default_off_never_shows_limits);
  RUN_TEST(test_rotation_period_and_show_timing);
  RUN_TEST(test_rotation_pending_blocks_and_restarts_wait);
  RUN_TEST(test_rotation_alert_preempts_limits_slot);
  RUN_TEST(test_rotation_config_change_resets_cycle);
  RUN_TEST(test_rotation_survives_millis_wrap);
  RUN_TEST(test_quiet_clock_phases);
  RUN_TEST(test_night_mode_config_and_brightness);
  RUN_TEST(test_mascot_style_config);
  RUN_TEST(test_screen_care);
  RUN_TEST(test_blue_filter);
  RUN_TEST(test_stored_config_fits_on_the_gadget);
  RUN_TEST(test_pet_latch);
  RUN_TEST(test_demo_ends_on_new_activity);
  RUN_TEST(test_update_notice);
  RUN_TEST(test_all_done_only_after_a_finish);
  RUN_TEST(test_flash_blinks);
  return UNITY_END();
}
