// The Wi-Fi safety net (miblo::LinkKeeper) simulated against a fake station and router, together
// with the NetPolicy that drives the screen and the setup network, the way net.cpp runs them.
#include <stdio.h>
#include <unity.h>

#include "miblo_link.h"
#include "miblo_policy.h"

using namespace miblo;

void setUp() {}
void tearDown() {}

// A station and its router, as net.cpp sees them through WiFi.status() and an ARP probe.
// `sdkGivesUp`: the SDK stops rejoining by itself once the link drops (what the field unit did:
// only an explicit disconnect+begin, or a power cycle, brings it back). `zombie`: the SDK keeps
// reporting WL_CONNECTED but nothing gets through (the gateway never answers a probe).
struct FakeWifi {
  bool routerUp = true;
  bool sdkGivesUp = false;
  bool zombie = false;
  bool wedged = false;       // a reconnect does not clear the zombie state (only a restart would)
  bool gatewayArp = true;    // the router answers ARP for its own address
  bool up = false;           // the station has an IP (WiFi.status() == WL_CONNECTED)
  bool joining = false;      // a join is in progress (explicit begin, or the SDK's own retry)
  uint32_t joinAtMs = 0;
  uint32_t now = 0;
  int probes = 0, drops = 0, joins = 0, restarts = 0;
  bool lastWasDrop = false;  // order check: a join right after a drop

  void sendProbe() { probes++; }
  bool probeAnswered() const { return up && routerUp && gatewayArp && !zombie; }
  void dropStation() {
    drops++;
    up = false;
    joining = false;
    lastWasDrop = true;
    if (!wedged) zombie = false;
  }
  void joinSaved() {
    TEST_ASSERT_TRUE_MESSAGE(lastWasDrop, "begin() must follow disconnect()");
    lastWasDrop = false;
    joins++;
    joining = true;
    joinAtMs = now;
  }
  void restart() { restarts++; }

  // One step of the radio: the link drops with the router; a join completes 3 s after it starts
  // while the router is up; without sdkGivesUp the SDK starts one by itself.
  void step(uint32_t t) {
    now = t;
    if (zombie) return;  // the SDK believes it is connected: nothing changes
    if (!routerUp && up) up = false;
    if (!up && !joining && routerUp && !sdkGivesUp) {
      joining = true;
      joinAtMs = t;
    }
    if (joining && routerUp && t - joinAtMs >= 3000) {
      joining = false;
      up = true;
    }
  }
};

// Runs everything the way net.cpp's loop does, in 100 ms passes.
struct Sim {
  FakeWifi w;
  NetPolicy policy;
  LinkKeeper keeper;
  uint32_t t = 0;
  bool phoneOnAp = false;
  bool traffic = false;  // the computer is posting snapshots (the network is visibly working)
  int reconnects = 0, restartsSeen = 0;
  uint32_t lastReconnectMs = 0, maxGapMs = 0, minGapMs = UINT32_MAX;

  Sim() {
    policy.begin(true, 0);
    keeper.begin(0);
  }
  void run(uint32_t untilMs) {
    for (; t < untilMs; t += 100) {
      w.step(t);
      policy.update(w.up ? LinkStatus::Connected : LinkStatus::Down, t);
      const bool mayAct = !phoneOnAp;
      const LinkAction a = runLinkKeeper(keeper, w, t, w.up, mayAct, !traffic || !w.up || w.zombie);
      if (a == LinkAction::Reconnect) {
        if (reconnects) {
          const uint32_t gap = t - lastReconnectMs;
          if (gap > maxGapMs) maxGapMs = gap;
          if (gap < minGapMs) minGapMs = gap;
        }
        reconnects++;
        lastReconnectMs = t;
      }
      if (a == LinkAction::Restart) restartsSeen++;
    }
  }
};

static constexpr uint32_t kMin = 60000;

// Outages of 10 s, 60 s, 4 min and 6 min on a unit whose SDK does not rejoin by itself: the safety
// net keeps issuing disconnect+begin cycles until the router is back, and the unit rejoins.
static void outage(uint32_t lengthMs, uint32_t backByMs) {
  Sim s;
  s.w.sdkGivesUp = false;
  s.run(30000);  // joined and confirmed
  TEST_ASSERT_TRUE(s.w.up);
  TEST_ASSERT_EQUAL(NetState::Connected, s.policy.state());
  s.w.sdkGivesUp = true;
  s.w.routerUp = false;
  s.run(30000 + lengthMs);
  s.w.routerUp = true;
  s.run(30000 + backByMs);
  char msg[64];
  snprintf(msg, sizeof(msg), "outage of %u s", (unsigned)(lengthMs / 1000));
  TEST_ASSERT_TRUE_MESSAGE(s.w.up, msg);
  TEST_ASSERT_EQUAL_MESSAGE(NetState::Connected, s.policy.state(), msg);
  TEST_ASSERT_TRUE_MESSAGE(s.reconnects >= 1, msg);
  TEST_ASSERT_EQUAL_MESSAGE(0, s.restartsSeen, msg);
}

static void test_outages_end_in_a_rejoin_without_the_sdk() {
  // First cycle LinkKeeper::kDownMs after the drop, then waits that double.
  outage(10000, 30000 + LinkKeeper::kDownMs + 5000);
  outage(60000, 30000 + LinkKeeper::kDownMs + 5000);
  outage(4 * kMin, LinkKeeper::kDownMs * 3 + 10000);    // 3 min: still down; 3 + 6 min: back
  outage(6 * kMin, LinkKeeper::kDownMs * 3 + 10000);
}

// With an SDK that does rejoin on its own, a short outage never needs a cycle.
static void test_short_outage_with_a_working_sdk_needs_no_cycle() {
  Sim s;
  s.run(30000);
  s.w.routerUp = false;
  s.run(30000 + 60000);
  s.w.routerUp = true;
  s.run(30000 + 60000 + 10000);
  TEST_ASSERT_TRUE(s.w.up);
  TEST_ASSERT_EQUAL(0, s.reconnects);
}

// The router never comes back: cycles keep coming (never more than kMaxWaitMs apart, never
// closer than kDownMs), and after kRestartMs one clean restart, once.
static void test_router_gone_for_good() {
  Sim s;
  s.run(30000);
  s.w.sdkGivesUp = true;
  s.w.routerUp = false;
  s.run(30000 + 120 * kMin);
  TEST_ASSERT_TRUE(s.reconnects >= 8);
  TEST_ASSERT_TRUE(s.maxGapMs <= LinkKeeper::kMaxWaitMs + 100);
  TEST_ASSERT_TRUE(s.minGapMs >= LinkKeeper::kDownMs);
  TEST_ASSERT_EQUAL(1, s.restartsSeen);
  TEST_ASSERT_EQUAL(1, s.w.restarts);
  TEST_ASSERT_EQUAL_UINT32((uint32_t)s.reconnects, s.keeper.reconnects());
  // Still cycling at the end: the last one within kMaxWaitMs.
  TEST_ASSERT_TRUE(s.t - s.lastReconnectMs <= LinkKeeper::kMaxWaitMs + 100);
}

// A network never reached this boot: cycles, but never a restart (it would only loop).
static void test_never_connected_never_restarts() {
  Sim s;
  s.w.routerUp = false;
  s.run(120 * kMin);
  TEST_ASSERT_TRUE(s.reconnects >= 8);
  TEST_ASSERT_EQUAL(0, s.restartsSeen);
}

// The field case: WiFi.status() stays WL_CONNECTED (the screen showed "Disconnected", which is
// drawn only while the policy is Connected) but nothing answers. Unanswered gateway probes mark
// the link dead and a cycle follows; the unit is reachable again.
static void test_zombie_link_is_found_and_cycled() {
  Sim s;
  s.traffic = true;
  s.run(10 * kMin);
  TEST_ASSERT_EQUAL(0, s.reconnects);
  const int probesBefore = s.w.probes;
  TEST_ASSERT_TRUE(probesBefore >= 1);  // the probe at link-up: the gateway answers ARP
  TEST_ASSERT_TRUE(probesBefore <= 2);  // while the computer talks, no more probes
  s.w.zombie = true;
  const uint32_t at = s.t;
  s.run(at + 3 * kMin);
  TEST_ASSERT_EQUAL(NetState::Connected, s.policy.state());  // the policy alone never notices
  TEST_ASSERT_EQUAL(1, s.reconnects);
  TEST_ASSERT_TRUE(s.lastReconnectMs - at <= LinkKeeper::kProbeEveryMs + 3 * LinkKeeper::kProbeRetryMs);
  TEST_ASSERT_FALSE(s.w.zombie);
  TEST_ASSERT_TRUE(s.w.up);
  TEST_ASSERT_EQUAL_UINT32(1, s.keeper.deadLinks());
}

// A zombie that a cycle does not cure: cycles with backoff, then one restart.
static void test_wedged_zombie_ends_in_a_restart() {
  Sim s;
  s.run(2 * kMin);
  s.w.zombie = true;
  s.w.wedged = true;
  s.run(2 * kMin + LinkKeeper::kRestartMs + 5 * kMin);
  TEST_ASSERT_TRUE(s.reconnects >= 3);
  TEST_ASSERT_EQUAL(1, s.restartsSeen);
}

// A gateway that never answers ARP (seen on no real router, but possible): probes then prove
// nothing, so a quiet but working unit is never cycled.
static void test_gateway_without_arp_never_cycles() {
  Sim s;
  s.w.gatewayArp = false;
  s.run(120 * kMin);
  TEST_ASSERT_TRUE(s.w.up);
  TEST_ASSERT_EQUAL(0, s.reconnects);
  TEST_ASSERT_EQUAL(0, s.restartsSeen);
}

// A newly submitted network replaces the one whose gateway answered ARP: what was learnt there
// proves nothing here, so a new gateway that never answers ARP is not judged dead.
static void test_new_network_forgets_the_old_gateway() {
  Sim s;
  s.run(10 * kMin);  // the old network: its gateway answers the probes
  TEST_ASSERT_EQUAL(0, s.reconnects);
  s.w.gatewayArp = false;  // the new network's gateway never does
  s.keeper.networkChanged();
  s.run(10 * kMin + 120 * kMin);
  TEST_ASSERT_TRUE(s.w.up);
  TEST_ASSERT_EQUAL(0, s.reconnects);
  TEST_ASSERT_EQUAL(0, s.restartsSeen);
  TEST_ASSERT_EQUAL_UINT32(0, s.keeper.deadLinks());
}

// Quiet and healthy (no computer): a probe a minute, never a cycle.
static void test_quiet_healthy_unit_only_probes() {
  Sim s;
  s.run(60 * kMin);
  TEST_ASSERT_EQUAL(0, s.reconnects);
  TEST_ASSERT_TRUE(s.w.probes >= 55 && s.w.probes <= 62);
}

// A phone on the setup network (or a submitted network being tried, an update) holds everything:
// a cycle would drop the phone's channel. Once it leaves, the wait starts over.
static void test_phone_on_the_setup_network_holds_cycles() {
  Sim s;
  s.run(30000);
  s.w.sdkGivesUp = true;
  s.w.routerUp = false;
  s.phoneOnAp = true;
  s.run(30000 + 60 * kMin);
  TEST_ASSERT_EQUAL(0, s.reconnects);
  TEST_ASSERT_EQUAL(0, s.restartsSeen);
  s.phoneOnAp = false;
  const uint32_t left = s.t;
  s.run(left + LinkKeeper::kDownMs - 1000);
  TEST_ASSERT_EQUAL(0, s.reconnects);
  s.run(left + LinkKeeper::kDownMs + 1000);
  TEST_ASSERT_EQUAL(1, s.reconnects);
}

// The policy's own setup network opens after 5 min on a proven network; the safety net does not
// depend on it (a low heap defers the setup network and with it the 60 s retries).
static void test_cycles_do_not_need_the_setup_network() {
  Sim s;
  s.run(30000);
  s.w.sdkGivesUp = true;
  s.w.routerUp = false;
  s.run(30000 + 22 * kMin);
  TEST_ASSERT_TRUE(s.policy.apWanted());
  TEST_ASSERT_EQUAL(3, s.reconnects);  // 3, 9 and 21 min after the drop
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_outages_end_in_a_rejoin_without_the_sdk);
  RUN_TEST(test_short_outage_with_a_working_sdk_needs_no_cycle);
  RUN_TEST(test_router_gone_for_good);
  RUN_TEST(test_never_connected_never_restarts);
  RUN_TEST(test_zombie_link_is_found_and_cycled);
  RUN_TEST(test_wedged_zombie_ends_in_a_restart);
  RUN_TEST(test_gateway_without_arp_never_cycles);
  RUN_TEST(test_new_network_forgets_the_old_gateway);
  RUN_TEST(test_quiet_healthy_unit_only_probes);
  RUN_TEST(test_phone_on_the_setup_network_holds_cycles);
  RUN_TEST(test_cycles_do_not_need_the_setup_network);
  return UNITY_END();
}
