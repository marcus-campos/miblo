#include <ArduinoJson.h>
#include <string.h>
#include <unity.h>

#include "miblo_config.h"
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
  return UNITY_END();
}
