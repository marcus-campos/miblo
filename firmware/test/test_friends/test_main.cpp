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
            if (j != i) g[j]->receive(q, t, 0x0A00A8C0u + i);  // each gadget its own address
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
  for (uint32_t seed = 1; seed < 600; seed++) {  // 37 activities: enough visits to see each
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

// 30 new activities: the visitor draws all 37 (a tired friend still gets more coffee), and the
// byte on the wire decodes back to the same activity; anything newer is a plain visit.
static void test_all_activities_reachable() {
  FriendPlay fp;
  bool seen[(int)Gift::Count] = {};
  int coffeeTired = 0, coffeeRested = 0;
  for (uint32_t r = 0; r < 20000; r++) {
    seen[(int)fp.chooseGiftForTest(false, r * 2654435761u)] = true;
    coffeeTired += fp.chooseGiftForTest(true, r * 2654435761u) == Gift::Coffee;
    coffeeRested += fp.chooseGiftForTest(false, r * 2654435761u) == Gift::Coffee;
  }
  for (int g = 0; g < (int)Gift::Count; g++) TEST_ASSERT_TRUE(seen[g]);
  TEST_ASSERT_TRUE(coffeeTired > 4 * coffeeRested);
  TEST_ASSERT_EQUAL_INT(37, (int)Gift::Count);
  // The last activity survives the wire; the next byte up is a newer one: a plain visit.
  FriendPacket p = packet(FriendPacket::VisitAsk, "miblo-bbbb", "Nina", kFriendRoaming, "miblo-aaaa", Gift::Kite);
  uint8_t buf[kFriendPacketMax];
  const size_t n = encodeFriendPacket(p, buf, sizeof(buf));
  TEST_ASSERT_TRUE(n > 0);
  FriendPacket q;
  TEST_ASSERT_TRUE(decodeFriendPacket(buf, n, q));
  TEST_ASSERT_EQUAL(Gift::Kite, q.gift);
  buf[8] = 37;
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

// ---- Security: visits stay open to any Miblo, but impostors and noisy ones get nowhere ----

constexpr uint32_t kIpA = 0x0A01A8C0, kIpB = 0x0B01A8C0, kIpC = 0x0C01A8C0, kIpD = 0x0D01A8C0,
                   kIpE = 0x0E01A8C0, kIpEvil = 0x6301A8C0;

// Pops every queued packet; true if one of `type` went to `to`.
static bool sent(FriendPlay& f, FriendPacket::Type type, const char* to) {
  bool found = false;
  FriendPacket p;
  while (f.nextPacket(p)) found |= p.type == type && strcmp(p.to, to) == 0;
  return found;
}

// `f` answers a "who is free?" from `from` (sent at `t`); `t` advances to the answer.
static bool answerPoll(FriendPlay& f, const char* from, uint32_t ip, uint32_t& t) {
  FriendPacket who = packet(FriendPacket::Who, from, "Tofu", kFriendRoaming);
  who.chance = 255;
  f.receive(who, t, ip);
  for (uint32_t e = 0; e <= kHereSpreadMs + 200; e += 100) {
    t += 100;
    f.update(t, true, kFriendRoaming, 7);
    if (sent(f, FriendPacket::Here, from)) return true;
  }
  return false;
}

// Nina (b) answers Tofu's (a) poll and is drawn as the host: waiting for guests at time `t`.
static void chosenAsHost(FriendPlay& b, uint32_t& t) {
  b.setSelf("miblo-bbbb", "Nina", 2);
  b.update(t, true, kFriendRoaming, 7);
  drain(b);
  b.receive(packet(FriendPacket::Beacon, "miblo-aaaa", "Tofu", kFriendRoaming), t, kIpA);
  TEST_ASSERT_TRUE(answerPoll(b, "miblo-aaaa", kIpA, t));
  b.receive(packet(FriendPacket::Host, "miblo-aaaa", "Tofu", kFriendRoaming, "miblo-bbbb", Gift::Duck), t, kIpA);
}

static FriendPacket invite(const char* from, const char* to, const char* host) {
  FriendPacket p = packet(FriendPacket::Invite, from, "Tofu", kFriendRoaming, to, Gift::Duck);
  strcpy(p.host, host);
  return p;
}

// Only the guests of this visit come in: the organiser and those it invited (its Invites are
// broadcast, so the host hears them). Anyone else saying "I'm coming" is sent home.
static void test_host_admits_only_invited_guests() {
  FriendPlay b;
  uint32_t t = 1000;
  chosenAsHost(b, t);
  drain(b);
  b.receive(packet(FriendPacket::VisitOk, "miblo-eeee", "Evil", kFriendRoaming, "miblo-bbbb"), t, kIpEvil);
  TEST_ASSERT_EQUAL(VisitRole::None, b.visit(t).role);  // a stranger does not start the visit
  TEST_ASSERT_TRUE(sent(b, FriendPacket::Home, "miblo-eeee"));
  // Someone other than the organiser "inviting" a friend of theirs to our place does not count.
  b.receive(invite("miblo-eeee", "miblo-ffff", "miblo-bbbb"), t, kIpEvil);
  // The organiser invites Coco to our place: Coco is expected.
  b.receive(invite("miblo-aaaa", "miblo-cccc", "miblo-bbbb"), t, kIpA);
  b.receive(packet(FriendPacket::VisitOk, "miblo-aaaa", "Tofu", kFriendRoaming, "miblo-bbbb", Gift::Duck), t, kIpA);
  TEST_ASSERT_EQUAL(VisitRole::Host, b.visit(t).role);
  TEST_ASSERT_EQUAL_STRING("Tofu", b.visit(t).name);
  b.receive(packet(FriendPacket::VisitOk, "miblo-cccc", "Coco", kFriendRoaming, "miblo-bbbb"), t, kIpC);
  b.receive(packet(FriendPacket::VisitOk, "miblo-ffff", "Fifi", kFriendRoaming, "miblo-bbbb"), t, kIpD);
  b.receive(packet(FriendPacket::VisitOk, "miblo-eeee", "Evil", kFriendRoaming, "miblo-bbbb"), t, kIpEvil);
  TEST_ASSERT_EQUAL_UINT8(1, b.visit(t).extra);  // Coco only
  // The legacy 1:1 visit (VisitAsk) admits only the one that asked, too.
  FriendPlay h;
  h.setSelf("miblo-hhhh", "Hana", 0);
  h.update(0, true, kFriendRoaming, 7);
  h.receive(packet(FriendPacket::VisitAsk, "miblo-aaaa", "Tofu", kFriendRoaming, "miblo-hhhh"), 10, kIpA);
  TEST_ASSERT_EQUAL(VisitRole::Host, h.visit(10).role);
  drain(h);
  h.receive(packet(FriendPacket::VisitOk, "miblo-eeee", "Evil", kFriendRoaming, "miblo-hhhh"), 20, kIpEvil);
  TEST_ASSERT_EQUAL_UINT8(0, h.visit(20).extra);
  TEST_ASSERT_TRUE(sent(h, FriendPacket::Home, "miblo-eeee"));
}

// Four cats at most (the host and 3 guests): a fourth guest is refused even if invited, and a
// guest saying it again is not counted twice.
static void test_fifth_cat_is_refused() {
  FriendPlay b;
  uint32_t t = 1000;
  chosenAsHost(b, t);
  b.receive(invite("miblo-aaaa", "miblo-cccc", "miblo-bbbb"), t, kIpA);
  b.receive(invite("miblo-aaaa", "miblo-dddd", "miblo-bbbb"), t, kIpA);
  b.receive(invite("miblo-aaaa", "miblo-eeee", "miblo-bbbb"), t, kIpA);
  b.receive(packet(FriendPacket::VisitOk, "miblo-aaaa", "Tofu", kFriendRoaming, "miblo-bbbb"), t, kIpA);
  b.receive(packet(FriendPacket::VisitOk, "miblo-cccc", "Coco", kFriendRoaming, "miblo-bbbb"), t, kIpC);
  b.receive(packet(FriendPacket::VisitOk, "miblo-cccc", "Coco", kFriendRoaming, "miblo-bbbb"), t, kIpC);
  TEST_ASSERT_EQUAL_UINT8(1, b.visit(t).extra);  // Coco once
  b.receive(packet(FriendPacket::VisitOk, "miblo-dddd", "Dodo", kFriendRoaming, "miblo-bbbb"), t, kIpD);
  TEST_ASSERT_EQUAL_UINT8(2, b.visit(t).extra);
  drain(b);
  b.receive(packet(FriendPacket::VisitOk, "miblo-eeee", "Eve", kFriendRoaming, "miblo-bbbb"), t, kIpE);
  TEST_ASSERT_EQUAL_UINT8(2, b.visit(t).extra);
  TEST_ASSERT_TRUE(sent(b, FriendPacket::Home, "miblo-eeee"));
}

// An id belongs to the address it was first heard from, until that friend is forgotten.
static void test_spoofed_id_from_another_ip_is_ignored() {
  FriendPlay b;
  b.setSelf("miblo-bbbb", "Nina", 0);
  b.update(0, true, kFriendRoaming | kFriendNapping, 7);
  b.receive(packet(FriendPacket::Beacon, "miblo-aaaa", "Tofu", kFriendRoaming), 10, kIpA);
  b.receive(packet(FriendPacket::Beacon, "miblo-aaaa", "Evil", kFriendRoaming | kFriendNapping), 20, kIpEvil);
  TEST_ASSERT_NULL(b.napBuddy());  // the impostor changed nothing
  // A spoofed "go home" does not end a visit either.
  FriendPlay h;
  h.setSelf("miblo-hhhh", "Hana", 0);
  h.update(0, true, kFriendRoaming, 7);
  h.receive(packet(FriendPacket::VisitAsk, "miblo-aaaa", "Tofu", kFriendRoaming, "miblo-hhhh"), 10, kIpA);
  h.receive(packet(FriendPacket::Home, "miblo-aaaa", "Tofu", kFriendRoaming, "miblo-hhhh"), 20, kIpEvil);
  TEST_ASSERT_EQUAL(VisitRole::Host, h.visit(20).role);
  // Once Tofu is forgotten (not heard for kFriendTtlMs), the id is free again.
  b.update(11 + kFriendTtlMs, true, kFriendRoaming | kFriendNapping, 7);
  b.receive(packet(FriendPacket::Beacon, "miblo-aaaa", "Moved", kFriendRoaming | kFriendNapping), 12 + kFriendTtlMs, kIpEvil);
  TEST_ASSERT_EQUAL_STRING("Moved", b.napBuddy());
}

// A sender talking too fast: the excess is dropped (a few packets per second each).
static void test_packet_flood_is_dropped() {
  FriendPlay b;
  b.setSelf("miblo-bbbb", "Nina", 0);
  b.update(0, true, kFriendRoaming | kFriendNapping, 7);
  for (uint32_t i = 0; i < kFriendRateMax; i++) {
    b.receive(packet(FriendPacket::Beacon, "miblo-aaaa", "Tofu", kFriendRoaming), 1000 + i, kIpA);
  }
  b.receive(packet(FriendPacket::Beacon, "miblo-aaaa", "Tofu", kFriendRoaming | kFriendNapping), 1500, kIpA);
  TEST_ASSERT_NULL(b.napBuddy());  // over the limit: dropped
  b.receive(packet(FriendPacket::Beacon, "miblo-aaaa", "Tofu", kFriendRoaming | kFriendNapping), 2000, kIpA);
  TEST_ASSERT_EQUAL_STRING("Tofu", b.napBuddy());  // a new second: heard again
}

// One visit per sender every few minutes (Invite, Host or the legacy VisitAsk alike).
static void test_second_visit_from_same_sender_within_3_min_is_ignored() {
  FriendPlay h;
  h.setSelf("miblo-hhhh", "Hana", 0);
  h.update(0, true, kFriendRoaming, 7);
  h.receive(packet(FriendPacket::VisitAsk, "miblo-aaaa", "Tofu", kFriendRoaming, "miblo-hhhh"), 10, kIpA);
  TEST_ASSERT_EQUAL(VisitRole::Host, h.visit(10).role);
  uint32_t t = 10;
  while (h.visit(t).role != VisitRole::None) {
    t += 1000;
    h.update(t, true, kFriendRoaming, 7);
    h.receive(packet(FriendPacket::Beacon, "miblo-aaaa", "Tofu", kFriendRoaming), t, kIpA);
  }
  drain(h);
  h.receive(packet(FriendPacket::VisitAsk, "miblo-aaaa", "Tofu", kFriendRoaming, "miblo-hhhh"), t, kIpA);
  TEST_ASSERT_EQUAL(VisitRole::None, h.visit(t).role);
  TEST_ASSERT_FALSE(answerPoll(h, "miblo-aaaa", kIpA, t));  // its polls are not answered either
  // Someone else is welcome meanwhile.
  t += 100;
  h.receive(packet(FriendPacket::VisitAsk, "miblo-cccc", "Coco", kFriendRoaming, "miblo-hhhh"), t, kIpC);
  TEST_ASSERT_EQUAL(VisitRole::Host, h.visit(t).role);
  while (h.visit(t).role != VisitRole::None) {
    t += 1000;
    h.update(t, true, kFriendRoaming, 7);
    h.receive(packet(FriendPacket::Beacon, "miblo-aaaa", "Tofu", kFriendRoaming), t, kIpA);
  }
  // Three minutes after its last visit started, Tofu may come again.
  for (; t < 10 + kVisitStartGapMs; t += 1000) {
    h.update(t, true, kFriendRoaming, 7);
    h.receive(packet(FriendPacket::Beacon, "miblo-aaaa", "Tofu", kFriendRoaming), t, kIpA);
  }
  h.receive(packet(FriendPacket::VisitAsk, "miblo-aaaa", "Tofu", kFriendRoaming, "miblo-hhhh"), t, kIpA);
  TEST_ASSERT_EQUAL(VisitRole::Host, h.visit(t).role);
}

// A crowd of new ids cannot flood the screen with "Hi, X!": at most kGreetMax per window.
static void test_greetings_are_capped() {
  FriendPlay b;
  b.setSelf("miblo-bbbb", "Nina", 0);
  b.update(0, true, kFriendRoaming, 7);
  char id[16];
  int greeted = 0;
  char last[64] = "";
  for (uint32_t t = 100; t < kGreetWindowMs; t += 1000) {
    for (int i = 0; i < 6; i++) {
      snprintf(id, sizeof(id), "miblo-%04d", i);
      if (t % kBeaconEveryMs < 1000) b.receive(packet(FriendPacket::Beacon, id, id, kFriendRoaming), t, kIpA + i);
    }
    b.update(t, true, kFriendRoaming, 7);
    const char* g = b.greeting(t);
    if (g && strcmp(g, last) != 0) greeted++;
    strcpy(last, g ? g : "");
  }
  TEST_ASSERT_EQUAL_INT(kGreetMax, greeted);
  // The next window greets the others.
  bool more = false;
  for (uint32_t t = kGreetWindowMs + 100; t < kGreetWindowMs + 60000; t += 1000) {
    for (int i = 0; i < 6; i++) {
      snprintf(id, sizeof(id), "miblo-%04d", i);
      if (t % kBeaconEveryMs < 1000) b.receive(packet(FriendPacket::Beacon, id, id, kFriendRoaming), t, kIpA + i);
    }
    b.update(t, true, kFriendRoaming, 7);
    more |= b.greeting(t) != nullptr;
  }
  TEST_ASSERT_TRUE(more);
}

// Invite and Host only from the one whose poll we answered (current firmware always polls first).
static void test_unsolicited_invite_or_host_is_ignored() {
  FriendPlay b;
  b.setSelf("miblo-bbbb", "Nina", 0);
  b.update(0, true, kFriendRoaming, 7);
  b.receive(packet(FriendPacket::Beacon, "miblo-aaaa", "Tofu", kFriendRoaming), 10, kIpA);
  b.receive(invite("miblo-aaaa", "miblo-bbbb", ""), 20, kIpA);
  TEST_ASSERT_EQUAL(VisitRole::None, b.visit(20).role);
  b.receive(packet(FriendPacket::Host, "miblo-aaaa", "Tofu", kFriendRoaming, "miblo-bbbb"), 30, kIpA);
  b.receive(packet(FriendPacket::VisitOk, "miblo-aaaa", "Tofu", kFriendRoaming, "miblo-bbbb"), 40, kIpA);
  TEST_ASSERT_EQUAL(VisitRole::None, b.visit(40).role);
  // After answering its poll, it may.
  uint32_t t = 100;
  TEST_ASSERT_TRUE(answerPoll(b, "miblo-aaaa", kIpA, t));
  b.receive(invite("miblo-aaaa", "miblo-bbbb", ""), t, kIpA);
  TEST_ASSERT_EQUAL(VisitRole::Visitor, b.visit(t).role);
}

// A packet cannot place us deep into a visit timeline (offset is clamped to the walk in).
static void test_visit_offset_is_clamped() {
  FriendPlay b;
  b.setSelf("miblo-bbbb", "Nina", 0);
  b.update(0, true, kFriendRoaming, 7);
  b.receive(packet(FriendPacket::Beacon, "miblo-aaaa", "Tofu", kFriendRoaming), 10, kIpA);
  uint32_t t = 100;
  TEST_ASSERT_TRUE(answerPoll(b, "miblo-aaaa", kIpA, t));
  FriendPacket p = invite("miblo-aaaa", "miblo-bbbb", "");
  p.offset = 250;  // 25 s in
  b.receive(p, t, kIpA);
  TEST_ASSERT_EQUAL(VisitRole::Visitor, b.visit(t).role);
  TEST_ASSERT_TRUE(b.visit(t).ms <= kVisitWalkMs);
}

// Names: printable text only. Broken or overlong UTF-8 and control characters drop the packet;
// invisible or direction-changing characters are removed (the rest of the name is kept).
// Packets naming themselves as the addressee or host are dropped.
static void test_names_and_odd_packets_are_rejected() {
  const char* bad[] = {"Ni\xC2\x85na", "Ni\xC0\xAEna", "Ni\xED\xA0\x80na", "Ni\xF4\x90\x80\x80na"};
  uint8_t buf[kFriendPacketMax];
  FriendPacket q;
  for (const char* name : bad) {
    FriendPacket p = packet(FriendPacket::Beacon, "miblo-aaaa", name, 0);
    TEST_ASSERT_FALSE_MESSAGE(decodeFriendPacket(buf, encodeFriendPacket(p, buf, sizeof(buf)), q), name);
  }
  const char* hidden[] = {"Ni\xE2\x80\xAEna", "Ni\xE2\x80\x8Bna", "\xEF\xBB\xBFNina", "Nin\xE2\x81\xA6\xE2\x81\xA9" "a"};
  for (const char* name : hidden) {
    FriendPacket p = packet(FriendPacket::Beacon, "miblo-aaaa", name, 0);
    TEST_ASSERT_TRUE(decodeFriendPacket(buf, encodeFriendPacket(p, buf, sizeof(buf)), q));
    TEST_ASSERT_EQUAL_STRING("Nina", q.name);
  }
  FriendPacket ok = packet(FriendPacket::Beacon, "miblo-aaaa", "Zoë 猫", 0);
  TEST_ASSERT_TRUE(decodeFriendPacket(buf, encodeFriendPacket(ok, buf, sizeof(buf)), q));
  TEST_ASSERT_EQUAL_STRING("Zoë 猫", q.name);
  FriendPacket self = packet(FriendPacket::VisitAsk, "miblo-aaaa", "Tofu", 0, "miblo-aaaa");
  TEST_ASSERT_FALSE(decodeFriendPacket(buf, encodeFriendPacket(self, buf, sizeof(buf)), q));
  FriendPacket loop = invite("miblo-aaaa", "miblo-bbbb", "miblo-bbbb");
  TEST_ASSERT_FALSE(decodeFriendPacket(buf, encodeFriendPacket(loop, buf, sizeof(buf)), q));
}

// A flood of new ids does not push out friends we have known for a while (newcomers make room
// among newcomers; a known friend leaves at most every kEvictGapMs).
static void test_id_flood_keeps_known_friends() {
  FriendPlay b;
  b.setSelf("miblo-bbbb", "Nina", 0);
  b.update(0, true, 0, 7);
  char id[16];
  for (uint32_t t = 0; t <= kBeaconEveryMs; t += kBeaconEveryMs) {  // 4 friends, heard twice
    for (int i = 0; i < 4; i++) {
      snprintf(id, sizeof(id), "miblo-f%03d", i);
      b.receive(packet(FriendPacket::Beacon, id, "F", 0), t, kIpA + i);
    }
  }
  uint32_t t = kBeaconEveryMs + 10;
  for (int i = 0; i < 1000; i++) {
    snprintf(id, sizeof(id), "miblo-x%04d", i);
    b.receive(packet(FriendPacket::Beacon, id, "X", 0), t + i, 0x10000000u + i);  // many machines
  }
  TEST_ASSERT_EQUAL_UINT8(kMaxFriends, b.count());
  // At most one known friend made room in that second.
  b.update(t + 1000, true, 0, 7);
  int known = 0;
  for (int i = 0; i < 4; i++) {
    snprintf(id, sizeof(id), "miblo-f%03d", i);
    known += b.knows(id);
  }
  TEST_ASSERT_TRUE(known >= 3);
}

// One machine is one Miblo: a second id from an address a live friend holds is ignored (so one
// machine cannot multiply its per-id limits), until that friend is forgotten.
static void test_one_machine_is_one_miblo() {
  FriendPlay b;
  b.setSelf("miblo-bbbb", "Nina", 0);
  b.update(0, true, 0, 7);
  b.receive(packet(FriendPacket::Beacon, "miblo-aaaa", "Tofu", 0), 10, kIpA);
  b.receive(packet(FriendPacket::Beacon, "miblo-zzzz", "Zed", 0), 20, kIpA);
  TEST_ASSERT_FALSE(b.knows("miblo-zzzz"));
  TEST_ASSERT_EQUAL_UINT8(1, b.count());
  b.update(11 + kFriendTtlMs, true, 0, 7);  // Tofu forgotten: the address is free again
  b.receive(packet(FriendPacket::Beacon, "miblo-zzzz", "Zed", 0), 12 + kFriendTtlMs, kIpA);
  TEST_ASSERT_TRUE(b.knows("miblo-zzzz"));
}

// The network size behind the poll's answer chance counts each friend once per beacon period: a
// chatty one (or ids that were not let in) cannot make everyone's polls go unanswered.
static void test_network_size_counts_each_friend_once() {
  FriendPlay lone;
  lone.setSelf("miblo-zzzz", "Z", 0);
  lone.update(0, true, kFriendRoaming, 1);
  char id[16];
  bool polled = false;
  FriendPacket p;
  for (uint32_t t = 100; t < kFirstVisitMinMs + kFirstVisitSpanMs + 1000 && !polled; t += 100) {
    if (t % 1000 == 0) {
      for (uint32_t k = 0; k < kFriendRateMax; k++) {  // as fast as allowed
        lone.receive(packet(FriendPacket::Beacon, "miblo-aaaa", "Tofu", 0), t - kFriendRateMax + k, kIpA);
      }
      for (int i = 0; i < 50; i++) {  // more ids from that same machine: not let in
        snprintf(id, sizeof(id), "miblo-x%04d", i);
        lone.receive(packet(FriendPacket::Beacon, id, "X", 0), t, kIpA);
      }
    }
    lone.update(t, true, kFriendRoaming, 1);
    while (lone.nextPacket(p)) {
      if (p.type != FriendPacket::Who) continue;
      polled = true;
      TEST_ASSERT_EQUAL_UINT8(255, p.chance);  // one other Miblo: everyone answers
    }
  }
  TEST_ASSERT_TRUE(polled);
}

// Organising a group of 4 (1:3): every packet of it goes out, whether we host or another does.
static void test_full_group_sends_every_invite() {
  int outside = 0, inside = 0;
  for (uint32_t seed = 1; seed < 400 && (!outside || !inside); seed++) {
    FriendPlay a;
    a.setSelf("miblo-aaaa", "Tofu", 0);
    a.update(0, true, kFriendRoaming, seed);
    a.demo(0, 600000);
    const char* m[3] = {"miblo-cccc", "miblo-dddd", "miblo-eeee"};
    for (int i = 0; i < 3; i++) a.receive(packet(FriendPacket::Beacon, m[i], "M", kFriendRoaming), 0, kIpC + i);
    FriendPacket p;
    bool polled = false;
    uint32_t t = 0;
    while (!polled && t < 20000) {
      t += 100;
      a.update(t, true, kFriendRoaming, seed * 7919 + t);
      while (a.nextPacket(p)) polled |= p.type == FriendPacket::Who;
    }
    TEST_ASSERT_TRUE(polled);
    int hostN = 0, okN = 0, invN = 0, formedAt = 0;
    for (int i = 0; i < 3 && !formedAt; i++) {
      a.receive(packet(FriendPacket::Here, m[i], "M", kFriendRoaming, "miblo-aaaa"), t, kIpC + i);
      if (i == 2) a.update(t, true, kFriendRoaming, 1);  // a beacon may join the same batch
      while (a.nextPacket(p)) {
        hostN += p.type == FriendPacket::Host;
        okN += p.type == FriendPacket::VisitOk;
        invN += p.type == FriendPacket::Invite;
      }
      if (hostN || invN) formedAt = i + 1;
    }
    if (formedAt != 3) continue;  // a smaller group
    if (hostN) {
      outside++;
      TEST_ASSERT_EQUAL_INT(1, okN);
      TEST_ASSERT_EQUAL_INT(2, invN);
    } else {
      inside++;
      TEST_ASSERT_EQUAL_INT(3, invN);
    }
  }
  TEST_ASSERT_TRUE(outside > 0);
  TEST_ASSERT_TRUE(inside > 0);
}

// "Go home" answers to strangers cannot fill the outgoing queue (a slot always stays free for the
// real packets), and one sender gets at most one per second.
static void test_home_replies_do_not_crowd_the_queue() {
  FriendPlay b;
  uint32_t t = 1000;
  chosenAsHost(b, t);
  drain(b);
  for (int k = 0; k < 5; k++) {
    b.receive(packet(FriendPacket::VisitOk, "miblo-eeee", "Evil", kFriendRoaming, "miblo-bbbb"), t + k, kIpEvil);
  }
  FriendPacket p;
  int homes = 0;
  while (b.nextPacket(p)) homes += p.type == FriendPacket::Home;
  TEST_ASSERT_EQUAL_INT(1, homes);
  char id[16];
  for (int i = 0; i < 20; i++) {
    snprintf(id, sizeof(id), "miblo-s%04d", i);
    b.receive(packet(FriendPacket::VisitOk, id, "S", kFriendRoaming, "miblo-bbbb"), t + 2000, 0x20000000u + i);
  }
  homes = 0;
  while (b.nextPacket(p)) homes += p.type == FriendPacket::Home;
  TEST_ASSERT_TRUE(homes > 0);
  TEST_ASSERT_TRUE(homes < (int)kFriendOutMax);
}

// Filler characters that render as nothing are removed too, the name is trimmed, and a name left
// empty becomes the id's default name.
static void test_blank_looking_names_fall_back_to_the_default() {
  const char* filler[] = {"\xC2\xAD", "\xCD\x8F", "\xE1\x85\x9F", "\xE1\x85\xA0", "\xE1\xA0\x8E", "\xE3\x85\xA4",
                          "\xEF\xBE\xA0", "\xEF\xB8\x80", "\xEF\xB8\x8F", "\xF3\xA0\x80\x81", "\xF3\xA0\x81\xBF"};
  uint8_t buf[kFriendPacketMax];
  FriendPacket q;
  char name[64];
  for (const char* f : filler) {
    snprintf(name, sizeof(name), "Ni%sna", f);
    FriendPacket p = packet(FriendPacket::Beacon, "miblo-4f2a", name, 0);
    TEST_ASSERT_TRUE(decodeFriendPacket(buf, encodeFriendPacket(p, buf, sizeof(buf)), q));
    TEST_ASSERT_EQUAL_STRING("Nina", q.name);
    snprintf(name, sizeof(name), " %s%s ", f, f);
    p = packet(FriendPacket::Beacon, "miblo-4f2a", name, 0);
    TEST_ASSERT_TRUE(decodeFriendPacket(buf, encodeFriendPacket(p, buf, sizeof(buf)), q));
    TEST_ASSERT_EQUAL_STRING("Miblo-4F2A", q.name);
  }
  FriendPacket p = packet(FriendPacket::Beacon, "miblo-4f2a", "  Nina Bo \xE3\x80\x80", 0);
  TEST_ASSERT_TRUE(decodeFriendPacket(buf, encodeFriendPacket(p, buf, sizeof(buf)), q));
  TEST_ASSERT_EQUAL_STRING("Nina Bo", q.name);
  p = packet(FriendPacket::Beacon, "miblo-4f2a", "", 0);
  TEST_ASSERT_TRUE(decodeFriendPacket(buf, encodeFriendPacket(p, buf, sizeof(buf)), q));
  TEST_ASSERT_EQUAL_STRING("Miblo-4F2A", q.name);
}

// Our own name goes out only if the other Miblos accept it: a name the settings allow but
// decodeFriendPacket refuses (a C1 control, U+FFFD, invalid UTF-8) would make every beacon of ours
// dropped, so it is sent as the default name instead. Found by the fuzz harness (friends_play).
static void test_own_name_others_would_refuse_goes_out_as_the_default() {
  const char* names[] = {"Ti\xC2\x85na", "\xEF\xBF\xBDTofu", "To\xFF" "fu", "\xC3"};
  for (const char* n : names) {
    FriendPlay f;
    f.setSelf("miblo-aaaa", n, 0);
    FriendPacket p;
    f.update(0, true, 0, 1);
    TEST_ASSERT_TRUE(f.nextPacket(p));
    uint8_t buf[kFriendPacketMax];
    FriendPacket q;
    TEST_ASSERT_TRUE(decodeFriendPacket(buf, encodeFriendPacket(p, buf, sizeof(buf)), q));
    TEST_ASSERT_EQUAL_STRING("Miblo-AAAA", q.name);
    // Called every frame with the same name: no new announcement each time.
    f.setSelf("miblo-aaaa", n, 0);
    f.update(1000, true, 0, 1);
    TEST_ASSERT_FALSE(f.nextPacket(p));
  }
  // A name with only invisible fillers around it is trimmed like the receivers do.
  FriendPlay f;
  f.setSelf("miblo-aaaa", " Tofu\xE2\x80\x8B", 0);
  FriendPacket p;
  f.update(0, true, 0, 1);
  TEST_ASSERT_TRUE(f.nextPacket(p));
  TEST_ASSERT_EQUAL_STRING("Tofu", p.name);
  f.setSelf("miblo-aaaa", " Tofu\xE2\x80\x8B", 0);
  f.update(1000, true, 0, 1);
  TEST_ASSERT_FALSE(f.nextPacket(p));
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
  RUN_TEST(test_all_activities_reachable);
  RUN_TEST(test_poll_scales_and_reserves);
  RUN_TEST(test_busy_host_sends_every_guest_home);
  RUN_TEST(test_host_admits_only_invited_guests);
  RUN_TEST(test_fifth_cat_is_refused);
  RUN_TEST(test_spoofed_id_from_another_ip_is_ignored);
  RUN_TEST(test_packet_flood_is_dropped);
  RUN_TEST(test_second_visit_from_same_sender_within_3_min_is_ignored);
  RUN_TEST(test_greetings_are_capped);
  RUN_TEST(test_unsolicited_invite_or_host_is_ignored);
  RUN_TEST(test_visit_offset_is_clamped);
  RUN_TEST(test_names_and_odd_packets_are_rejected);
  RUN_TEST(test_id_flood_keeps_known_friends);
  RUN_TEST(test_one_machine_is_one_miblo);
  RUN_TEST(test_network_size_counts_each_friend_once);
  RUN_TEST(test_full_group_sends_every_invite);
  RUN_TEST(test_home_replies_do_not_crowd_the_queue);
  RUN_TEST(test_blank_looking_names_fall_back_to_the_default);
  RUN_TEST(test_own_name_others_would_refuse_goes_out_as_the_default);
  return UNITY_END();
}
