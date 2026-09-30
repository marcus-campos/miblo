#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "miblo_friends.h"

using namespace miblo;

void setUp() {}
void tearDown() {}

static FriendPacket packet(FriendPacket::Type type, const char* id, const char* name, uint8_t flags,
                           const char* to = "", Gift gift = Gift::None) {
  FriendPacket p;
  p.type = type;
  strcpy(p.id, id);
  strcpy(p.name, name);
  p.mascot = 1;
  p.flags = flags;
  strcpy(p.to, to);
  p.gift = gift;
  return p;
}

// Delivers every queued packet of `from` to `to`.
static void deliver(FriendPlay& from, FriendPlay& to, uint32_t now) {
  FriendPacket p;
  uint8_t buf[kFriendPacketMax];
  while (from.nextPacket(p)) {
    const size_t n = encodeFriendPacket(p, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    FriendPacket q;
    TEST_ASSERT_TRUE(decodeFriendPacket(buf, n, q));
    to.receive(q, now);
  }
}

static void drain(FriendPlay& f) {
  FriendPacket p;
  while (f.nextPacket(p)) {
  }
}

// A little network: every gadget hears every packet (UDP broadcast), ticked in 100 ms steps.
struct Sim {
  FriendPlay* g[8];
  uint8_t flags[8];
  uint8_t n = 0;
  uint32_t t = 0;
  uint32_t rnd = 12345;
  void add(FriendPlay& f, uint8_t fl = kFriendRoaming) { g[n] = &f; flags[n++] = fl; }
  void tick(uint32_t ms = 100) {
    t += ms;
    for (uint8_t i = 0; i < n; i++) g[i]->update(t, true, flags[i], rnd = rnd * 1103515245u + 12345u);
    for (int round = 0; round < 3; round++) {
      for (uint8_t i = 0; i < n; i++) {
        FriendPacket p;
        uint8_t buf[kFriendPacketMax];
        while (g[i]->nextPacket(p)) {
          const size_t len = encodeFriendPacket(p, buf, sizeof(buf));
          FriendPacket q;
          TEST_ASSERT_TRUE(decodeFriendPacket(buf, len, q));
          for (uint8_t j = 0; j < n; j++) {
            if (j != i) g[j]->receive(q, t);
          }
        }
      }
    }
  }
  // Runs until some gadget is in a visit (or the time runs out); returns whether one started.
  bool untilVisit(uint32_t maxMs) {
    for (uint32_t e = 0; e < maxMs; e += 100) {
      tick();
      for (uint8_t i = 0; i < n; i++) {
        if (g[i]->visit(t).role != VisitRole::None) return true;
      }
    }
    return false;
  }
};

static void test_packet_round_trip() {
  FriendPacket p = packet(FriendPacket::VisitAsk, "miblo-4f2a", "Escritório 3", kFriendRoaming | kFriendTired,
                          "miblo-b452", Gift::Coffee);
  p.mascot = 3;
  uint8_t buf[kFriendPacketMax];
  const size_t n = encodeFriendPacket(p, buf, sizeof(buf));
  TEST_ASSERT_TRUE(n > 9);
  FriendPacket q;
  TEST_ASSERT_TRUE(decodeFriendPacket(buf, n, q));
  TEST_ASSERT_EQUAL(FriendPacket::VisitAsk, q.type);
  TEST_ASSERT_EQUAL_STRING("miblo-4f2a", q.id);
  TEST_ASSERT_EQUAL_STRING("Escritório 3", q.name);
  TEST_ASSERT_EQUAL_STRING("miblo-b452", q.to);
  TEST_ASSERT_EQUAL_UINT8(3, q.mascot);
  TEST_ASSERT_EQUAL_UINT8(kFriendRoaming | kFriendTired, q.flags);
  TEST_ASSERT_EQUAL(Gift::Coffee, q.gift);
  // Extra bytes from a future version are ignored.
  buf[n] = 7;
  TEST_ASSERT_TRUE(decodeFriendPacket(buf, n + 1, q));
}

static void test_malformed_packets_are_rejected() {
  FriendPacket p = packet(FriendPacket::Beacon, "miblo-4f2a", "Tofu", kFriendRoaming);
  uint8_t buf[kFriendPacketMax];
  const size_t n = encodeFriendPacket(p, buf, sizeof(buf));
  FriendPacket q;
  for (size_t cut = 0; cut < n; cut++) TEST_ASSERT_FALSE(decodeFriendPacket(buf, cut, q));  // truncated
  uint8_t bad[kFriendPacketMax];
  memcpy(bad, buf, n);
  bad[0] = 'X';  // magic
  TEST_ASSERT_FALSE(decodeFriendPacket(bad, n, q));
  memcpy(bad, buf, n);
  bad[4] = 2;  // version
  TEST_ASSERT_FALSE(decodeFriendPacket(bad, n, q));
  memcpy(bad, buf, n);
  bad[5] = 9;  // type
  TEST_ASSERT_FALSE(decodeFriendPacket(bad, n, q));
  memcpy(bad, buf, n);
  bad[10] = 'A';  // id: upper case not allowed
  TEST_ASSERT_FALSE(decodeFriendPacket(bad, n, q));
  // A name with a control character, or longer than 20 characters.
  p = packet(FriendPacket::Beacon, "miblo-4f2a", "To\x01" "fu", 0);
  TEST_ASSERT_FALSE(decodeFriendPacket(buf, encodeFriendPacket(p, buf, sizeof(buf)), q));
  p = packet(FriendPacket::Beacon, "miblo-4f2a", "123456789012345678901", 0);
  TEST_ASSERT_FALSE(decodeFriendPacket(buf, encodeFriendPacket(p, buf, sizeof(buf)), q));
  // A visit request must name its addressee.
  p = packet(FriendPacket::VisitAsk, "miblo-4f2a", "Tofu", 0);
  TEST_ASSERT_FALSE(decodeFriendPacket(buf, encodeFriendPacket(p, buf, sizeof(buf)), q));
}

static void test_beacons_on_schedule_and_on_change() {
  FriendPlay f;
  f.setSelf("miblo-aaaa", "Tofu", 0);
  FriendPacket p;
  f.update(0, true, 0, 1);
  TEST_ASSERT_TRUE(f.nextPacket(p));
  TEST_ASSERT_EQUAL(FriendPacket::Beacon, p.type);
  TEST_ASSERT_EQUAL_STRING("Tofu", p.name);
  TEST_ASSERT_FALSE(f.nextPacket(p));
  f.update(1000, true, 0, 1);
  TEST_ASSERT_FALSE(f.nextPacket(p));
  f.update(kBeaconEveryMs, true, 0, 1);
  TEST_ASSERT_TRUE(f.nextPacket(p));
  // Entering pet mode is announced right away (after the minimum gap).
  f.update(kBeaconEveryMs + 500, true, kFriendRoaming, 1);
  TEST_ASSERT_FALSE(f.nextPacket(p));
  f.update(kBeaconEveryMs + kBeaconMinGapMs, true, kFriendRoaming, 1);
  TEST_ASSERT_TRUE(f.nextPacket(p));
  TEST_ASSERT_EQUAL_UINT8(kFriendRoaming, p.flags);
  // Off: silent, and everyone is forgotten.
  f.receive(packet(FriendPacket::Beacon, "miblo-bbbb", "Nina", kFriendRoaming), 40000);
  TEST_ASSERT_EQUAL_UINT8(1, f.count());
  f.update(41000, false, kFriendRoaming, 1);
  TEST_ASSERT_EQUAL_UINT8(0, f.count());
  f.update(200000, false, kFriendRoaming, 1);
  TEST_ASSERT_FALSE(f.nextPacket(p));
}

static void test_friends_expire_and_own_packets_are_ignored() {
  FriendPlay f;
  f.setSelf("miblo-aaaa", "Tofu", 0);
  f.update(0, true, 0, 1);
  f.receive(packet(FriendPacket::Beacon, "miblo-aaaa", "Tofu", 0), 10);  // our own broadcast
  TEST_ASSERT_EQUAL_UINT8(0, f.count());
  f.receive(packet(FriendPacket::Beacon, "miblo-bbbb", "Nina", 0), 10);
  TEST_ASSERT_EQUAL_UINT8(1, f.count());
  f.update(10 + kFriendTtlMs, true, 0, 1);
  TEST_ASSERT_EQUAL_UINT8(1, f.count());
  f.update(11 + kFriendTtlMs, true, 0, 1);
  TEST_ASSERT_EQUAL_UINT8(0, f.count());
  // A full table makes room for a newcomer (a random one leaves): never more than kMaxFriends.
  char id[16];
  for (int i = 0; i < kMaxFriends + 3; i++) {
    snprintf(id, sizeof(id), "miblo-%04d", i);
    f.receive(packet(FriendPacket::Beacon, id, "X", 0), 200000 + i);
  }
  TEST_ASSERT_EQUAL_UINT8(kMaxFriends, f.count());
}

static void test_greets_a_friend_in_pet_mode_once_in_a_while() {
  FriendPlay f;
  f.setSelf("miblo-aaaa", "Tofu", 0);
  f.update(0, true, kFriendRoaming, 1);
  TEST_ASSERT_NULL(f.greeting(0));
  f.receive(packet(FriendPacket::Beacon, "miblo-bbbb", "Nina", 0), 100);  // not in pet mode: no hi
  f.update(200, true, kFriendRoaming, 1);
  TEST_ASSERT_NULL(f.greeting(200));
  f.receive(packet(FriendPacket::Beacon, "miblo-bbbb", "Nina", kFriendRoaming), 300);
  f.update(400, true, kFriendRoaming, 1);
  TEST_ASSERT_EQUAL_STRING("Nina", f.greeting(400));
  TEST_ASSERT_NULL(f.greeting(400 + kGreetShowMs));
  f.update(400 + kGreetShowMs, true, kFriendRoaming, 1);
  f.receive(packet(FriendPacket::Beacon, "miblo-bbbb", "Nina", kFriendRoaming), 30000);
  f.update(30000, true, kFriendRoaming, 1);
  TEST_ASSERT_NULL(f.greeting(30000));  // already greeted
}

// Two gadgets in pet mode: one asks the network who is free, the other answers, the visit runs the
// same timeline on both.
static void test_visit_handshake_and_timeline() {
  FriendPlay a, b;
  a.setSelf("miblo-aaaa", "Tofu", 1);
  b.setSelf("miblo-bbbb", "Nina", 2);
  Sim sim;
  sim.add(a);
  sim.add(b);
  TEST_ASSERT_TRUE(sim.untilVisit(kFirstVisitMinMs + kFirstVisitSpanMs + 5000));
  const uint32_t t = sim.t;
  const bool aVisits = a.visit(t).role == VisitRole::Visitor;
  FriendPlay& host = aVisits ? b : a;
  TEST_ASSERT_EQUAL(VisitRole::Host, host.visit(t).role);
  TEST_ASSERT_EQUAL_STRING(aVisits ? "Tofu" : "Nina", host.visit(t).name);
  TEST_ASSERT_EQUAL_UINT8(aVisits ? 1 : 2, host.visit(t).mascot);
  // Over after kVisitMs on both sides.
  for (uint32_t e = 0; e < kVisitMs; e += 100) sim.tick();
  TEST_ASSERT_EQUAL(VisitRole::None, a.visit(sim.t).role);
  TEST_ASSERT_EQUAL(VisitRole::None, b.visit(sim.t).role);
}

// A tired friend gets a coffee more often than one with room left.
static void test_visit_brings_coffee_to_a_tired_friend() {
  auto coffeeRate = [](uint8_t friendFlags) {
    int coffee = 0;
    for (uint32_t trial = 0; trial < 200; trial++) {
      FriendPlay a, b;
      a.setSelf("miblo-aaaa", "Tofu", 0);
      b.setSelf("miblo-bbbb", "Nina", 0);
      Sim sim;
      sim.rnd = trial * 7919 + 1;
      sim.add(a);
      sim.add(b, friendFlags);
      if (!sim.untilVisit(kFirstVisitMinMs + kFirstVisitSpanMs + 5000)) continue;
      if (a.visit(sim.t).role == VisitRole::Visitor && a.visit(sim.t).gift == Gift::Coffee) coffee++;
    }
    return coffee;
  };
  TEST_ASSERT_TRUE(coffeeRate(kFriendRoaming | kFriendTired) > coffeeRate(kFriendRoaming));
}

static void test_visits_need_both_in_pet_mode_and_awake() {
  FriendPlay b;
  b.setSelf("miblo-bbbb", "Nina", 0);
  b.update(0, true, 0, 0);  // not in pet mode
  b.receive(packet(FriendPacket::VisitAsk, "miblo-aaaa", "Tofu", kFriendRoaming, "miblo-bbbb"), 10);
  TEST_ASSERT_EQUAL(VisitRole::None, b.visit(10).role);
  b.update(20, true, kFriendRoaming | kFriendNapping, 0);  // napping
  b.receive(packet(FriendPacket::VisitAsk, "miblo-aaaa", "Tofu", kFriendRoaming, "miblo-bbbb"), 30);
  TEST_ASSERT_EQUAL(VisitRole::None, b.visit(30).role);
  // Addressed to someone else.
  b.update(40, true, kFriendRoaming, 0);
  b.receive(packet(FriendPacket::VisitAsk, "miblo-aaaa", "Tofu", kFriendRoaming, "miblo-cccc"), 50);
  TEST_ASSERT_EQUAL(VisitRole::None, b.visit(50).role);
  // A late answer (after kVisitAskMs) does not start a visit.
  FriendPlay a;
  a.setSelf("miblo-aaaa", "Tofu", 0);
  a.update(0, true, kFriendRoaming, 0);
  a.receive(packet(FriendPacket::Beacon, "miblo-bbbb", "Nina", kFriendRoaming), 0);
  a.update(kFirstVisitMinMs, true, kFriendRoaming, 0);
  a.receive(packet(FriendPacket::VisitOk, "miblo-bbbb", "Nina", kFriendRoaming, "miblo-aaaa"),
            kFirstVisitMinMs + kVisitAskMs);
  TEST_ASSERT_EQUAL(VisitRole::None, a.visit(kFirstVisitMinMs + kVisitAskMs).role);
}

static void test_leaving_pet_mode_sends_the_visitor_home() {
  FriendPlay a, b;
  a.setSelf("miblo-aaaa", "Tofu", 0);
  b.setSelf("miblo-bbbb", "Nina", 0);
  Sim sim;
  sim.add(a);
  sim.add(b);
  TEST_ASSERT_TRUE(sim.untilVisit(kFirstVisitMinMs + kFirstVisitSpanMs + 5000));
  sim.tick();
  const uint8_t vi = a.visit(sim.t).role == VisitRole::Visitor ? 0 : 1;
  // Someone sits down at the visitor's computer: its mascot is home at once, the guest leaves.
  sim.flags[vi] = 0;
  sim.tick();
  sim.tick();
  TEST_ASSERT_EQUAL(VisitRole::None, a.visit(sim.t).role);
  TEST_ASSERT_EQUAL(VisitRole::None, b.visit(sim.t).role);
}

// Both look for a friend at the same moment: still exactly one visit, with opposite roles.
static void test_simultaneous_requests_lower_id_visits() {
  for (uint32_t seed = 1; seed < 40; seed++) {
    FriendPlay a, b;
    a.setSelf("miblo-aaaa", "Tofu", 0);
    b.setSelf("miblo-bbbb", "Nina", 0);
    Sim sim;
    sim.rnd = seed;
    sim.add(a);
    sim.add(b);
    sim.tick();
    a.demo(sim.t, sim.t + 600000);  // demo: both schedule their first visit within the same few seconds
    b.demo(sim.t, sim.t + 600000);
    TEST_ASSERT_TRUE(sim.untilVisit(120000));  // both polling at once: nobody answers, they retry apart
    sim.tick();
    sim.tick();
    const VisitRole ra = a.visit(sim.t).role, rb = b.visit(sim.t).role;
    TEST_ASSERT_TRUE((ra == VisitRole::Visitor && rb == VisitRole::Host) ||
                     (ra == VisitRole::Host && rb == VisitRole::Visitor));
  }
}

static void test_nap_buddy() {
  FriendPlay a;
  a.setSelf("miblo-aaaa", "Tofu", 0);
  a.update(0, true, kFriendRoaming | kFriendNapping, 0);
  TEST_ASSERT_NULL(a.napBuddy());
  a.receive(packet(FriendPacket::Beacon, "miblo-bbbb", "Nina", kFriendRoaming | kFriendNapping), 10);
  TEST_ASSERT_EQUAL_STRING("Nina", a.napBuddy());
  a.update(20, true, kFriendRoaming, 0);  // we woke up
  TEST_ASSERT_NULL(a.napBuddy());
}

// Demo: the first visit comes within seconds, later visits are quick too.
static void test_demo_hurries_visits() {
  FriendPlay a, b;
  a.setSelf("miblo-aaaa", "Tofu", 0);
  b.setSelf("miblo-bbbb", "Nina", 0);
  Sim sim;
  sim.add(a);
  sim.add(b);
  sim.tick();
  a.demo(sim.t, sim.t + 600000);
  b.demo(sim.t, sim.t + 600000);
  // Without the demo the first visit is 1-3 minutes away; with it, about 10 seconds.
  TEST_ASSERT_TRUE(sim.untilVisit(kDemoFirstVisitMs + 4000 + kPollMs + 2000));
  for (uint32_t e = 0; e < kVisitMs; e += 100) sim.tick();
  // The next one: within the demo's 20-40 s, not minutes.
  TEST_ASSERT_TRUE(sim.untilVisit(2 * kDemoNextVisitMs + kPollMs + 2000));
}

// The host is drawn within each group: over many visits both Miblos host a fair share (nobody is
// always the one visited, nobody never is).
static void test_visits_take_turns() {
  FriendPlay a, b;
  a.setSelf("miblo-aaaa", "Tofu", 0);
  b.setSelf("miblo-bbbb", "Nina", 0);
  Sim sim;
  sim.add(a);
  sim.add(b);
  sim.tick();
  a.demo(sim.t, sim.t + 3600000);
  b.demo(sim.t, sim.t + 3600000);
  int aHosts = 0, bHosts = 0;
  for (int visit = 0; visit < 24; visit++) {
    TEST_ASSERT_TRUE(sim.untilVisit(180000));
    sim.tick();
    if (a.visit(sim.t).role == VisitRole::Host) aHosts++;
    if (b.visit(sim.t).role == VisitRole::Host) bHosts++;
    for (uint32_t e = 0; e < kVisitMs; e += 100) sim.tick();
  }
  TEST_ASSERT_TRUE(aHosts >= 5);
  TEST_ASSERT_TRUE(bHosts >= 5);
}

static void test_every_activity_can_come_up_and_unknown_ones_decode_as_visits() {
  bool seen[(int)Gift::Count] = {};
  for (uint32_t seed = 1; seed < 120; seed++) {
    FriendPlay a, b;
    a.setSelf("miblo-aaaa", "Tofu", 0);
    b.setSelf("miblo-bbbb", "Nina", 0);
    Sim sim;
    sim.rnd = seed * 2654435761u;
    sim.add(a);
    sim.add(b);
    if (!sim.untilVisit(kFirstVisitMinMs + kFirstVisitSpanMs + 5000)) continue;
    seen[(int)a.visit(sim.t).gift] = true;
  }
  for (int i = 0; i < (int)Gift::Count; i++) TEST_ASSERT_TRUE_MESSAGE(seen[i], "activity never chosen");
  // A gift value from a newer firmware is a plain visit, not a rejected packet.
  FriendPacket p = packet(FriendPacket::VisitAsk, "miblo-bbbb", "Nina", kFriendRoaming, "miblo-aaaa");
  uint8_t buf[kFriendPacketMax];
  const size_t n = encodeFriendPacket(p, buf, sizeof(buf));
  buf[8] = 200;
  FriendPacket q;
  TEST_ASSERT_TRUE(decodeFriendPacket(buf, n, q));
  TEST_ASSERT_EQUAL(Gift::None, q.gift);
}

// A crowd: visits happen, every visitor has exactly one host, and the answer chance of the
// "who is free?" poll shrinks with the size of the network (about kPollWanted answer).
static void test_poll_scales_and_reserves() {
  static FriendPlay many[8];
  Sim sim;
  char id[16], name[8];
  for (int i = 0; i < 8; i++) {
    snprintf(id, sizeof(id), "miblo-%04d", i);
    snprintf(name, sizeof(name), "M%d", i);
    many[i] = FriendPlay();
    many[i].setSelf(id, name, 0);
    sim.add(many[i]);
  }
  int visitTicks = 0, groups = 0;
  bool hosted[8] = {};
  for (int i = 0; i < 8; i++) many[i].demo(sim.t, sim.t + 3600000);  // frequent visits
  for (uint32_t e = 0; e < 15UL * 60000; e += 100) {
    sim.tick();
    int visitors = 0, hosts = 0;
    for (int i = 0; i < 8; i++) {
      if (many[i].visit(sim.t).role == VisitRole::Visitor) visitors++;
      if (many[i].visit(sim.t).role == VisitRole::Host) hosts++;
    }
    // Groups of 2 to 4: every host has 1 to 3 guests (allowing for the step in which they start).
    TEST_ASSERT_TRUE(hosts <= visitors + 1);
    TEST_ASSERT_TRUE(visitors <= (int)kMaxGuests * (hosts + 1));
    if (visitors) visitTicks++;
    for (int i = 0; i < 8; i++) {
      if (many[i].visit(sim.t).role == VisitRole::Host) hosted[i] = true;
      if (many[i].visit(sim.t).role == VisitRole::Host && many[i].visit(sim.t).extra) groups++;
    }
  }
  TEST_ASSERT_TRUE(visitTicks > 0);
  TEST_ASSERT_TRUE(groups > 0);  // some visits were 1:2 or 1:3
  int hosts = 0;
  for (int i = 0; i < 8; i++) hosts += hosted[i];
  TEST_ASSERT_TRUE(hosts >= 6);  // nearly everyone hosted at least once (no cliques)
  FriendPlay lone;
  lone.setSelf("miblo-zzzz", "Z", 0);
  lone.update(0, true, kFriendRoaming, 1);  // pet mode: first visit about a minute away
  auto crowd = [&](uint32_t at) {
    for (int i = 0; i < 400; i++) {
      snprintf(id, sizeof(id), "miblo-%04d", i);
      lone.receive(packet(FriendPacket::Beacon, id, "X", kFriendRoaming), at);
    }
  };
  crowd(100);
  lone.update(kBeaconEveryMs + 1, true, kFriendRoaming, 1);  // a 30 s window closes: 400 heard
  crowd(kBeaconEveryMs + 100);
  FriendPacket p;
  while (lone.nextPacket(p)) {
  }
  bool polled = false;
  for (uint32_t t = kBeaconEveryMs + 200; t < kFirstVisitMinMs + kFirstVisitSpanMs && !polled; t += 100) {
    lone.update(t, true, kFriendRoaming, 1);
    while (lone.nextPacket(p)) {
      if (p.type != FriendPacket::Who) continue;
      polled = true;
      TEST_ASSERT_TRUE(p.chance <= 255u * kPollWanted / 400 + 1);  // ~4 of 400 answer
    }
    if (!polled && (t % kBeaconEveryMs) < 100) crowd(t);  // the crowd keeps beaconing
  }
  TEST_ASSERT_TRUE(polled);
}

// The host's human gets back to work mid-visit: every guest is told and walks home disappointed.
static void test_busy_host_sends_every_guest_home() {
  for (uint32_t seed = 1; seed < 30; seed++) {
    static FriendPlay g[4];
    Sim sim;
    sim.rnd = seed * 97;
    char id[16];
    for (int i = 0; i < 4; i++) {
      snprintf(id, sizeof(id), "miblo-%04d", i);
      g[i] = FriendPlay();
      g[i].setSelf(id, "M", 0);
      sim.add(g[i]);
    }
    sim.tick();
    for (int i = 0; i < 4; i++) g[i].demo(sim.t, sim.t + 3600000);
    if (!sim.untilVisit(180000)) continue;
    for (int k = 0; k < 40; k++) sim.tick();  // everyone arrived
    int host = -1;
    for (int i = 0; i < 4; i++) {
      if (g[i].visit(sim.t).role == VisitRole::Host) host = i;
    }
    if (host < 0) continue;
    sim.flags[host] = 0;  // back to work
    sim.tick();
    sim.tick();
    TEST_ASSERT_EQUAL(VisitRole::None, g[host].visit(sim.t).role);
    for (int i = 0; i < 4; i++) {
      const VisitView v = g[i].visit(sim.t);
      if (v.role != VisitRole::Visitor || strcmp(v.name, "M") != 0) continue;
      TEST_ASSERT_TRUE(v.turnedAway);
      TEST_ASSERT_TRUE(v.ms >= kVisitMs - kVisitWalkMs);  // already walking back
    }
    return;
  }
  TEST_FAIL_MESSAGE("no visit happened");
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_packet_round_trip);
  RUN_TEST(test_malformed_packets_are_rejected);
  RUN_TEST(test_beacons_on_schedule_and_on_change);
  RUN_TEST(test_friends_expire_and_own_packets_are_ignored);
  RUN_TEST(test_greets_a_friend_in_pet_mode_once_in_a_while);
  RUN_TEST(test_visit_handshake_and_timeline);
  RUN_TEST(test_visit_brings_coffee_to_a_tired_friend);
  RUN_TEST(test_visits_need_both_in_pet_mode_and_awake);
  RUN_TEST(test_leaving_pet_mode_sends_the_visitor_home);
  RUN_TEST(test_simultaneous_requests_lower_id_visits);
  RUN_TEST(test_nap_buddy);
  RUN_TEST(test_demo_hurries_visits);
  RUN_TEST(test_visits_take_turns);
  RUN_TEST(test_every_activity_can_come_up_and_unknown_ones_decode_as_visits);
  RUN_TEST(test_poll_scales_and_reserves);
  RUN_TEST(test_busy_host_sends_every_guest_home);
  return UNITY_END();
}
