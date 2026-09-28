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
  TEST_ASSERT_FALSE(p.apWanted());
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

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_match_spec);
  RUN_TEST(test_patch_applies_valid_fields_and_ignores_unknown);
  RUN_TEST(test_invalid_patch_changes_nothing);
  RUN_TEST(test_config_json_roundtrip);
  RUN_TEST(test_power_cycle_reset_counter);
  RUN_TEST(test_non_power_on_boot_keeps_sequence);
  RUN_TEST(test_crash_between_quick_power_ons_does_not_break_sequence);
  RUN_TEST(test_crash_never_triggers_reset);
  RUN_TEST(test_erased_flash_counts_as_first_boot);
  RUN_TEST(test_net_policy_saved_credentials_then_router_down);
  RUN_TEST(test_net_policy_first_boot_and_wrong_password);
  RUN_TEST(test_screen_selection_order);
  return UNITY_END();
}
