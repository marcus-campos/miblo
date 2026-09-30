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
  // A full table makes room by dropping the one heard from longest ago.
  const char* ids[] = {"miblo-0001", "miblo-0002", "miblo-0003", "miblo-0004", "miblo-0005"};
  for (int i = 0; i < 5; i++) f.receive(packet(FriendPacket::Beacon, ids[i], "X", 0), 200000 + i);
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

// Two gadgets in pet mode: one asks, the other accepts, both run the same timeline.
static void test_visit_handshake_and_timeline() {
  FriendPlay a, b;
  a.setSelf("miblo-aaaa", "Tofu", 1);
  b.setSelf("miblo-bbbb", "Nina", 2);
  uint32_t t = 0;
  const uint8_t roam = kFriendRoaming;
  // Each hears the other's beacon.
  a.update(t, true, roam, 0);
  b.update(t, true, roam, 7);
  deliver(a, b, t);
  deliver(b, a, t);
  TEST_ASSERT_EQUAL(VisitRole::None, a.visit(t).role);
  // The first visit comes 1-3 minutes into pet mode (rnd 0 on A: exactly kFirstVisitMinMs).
  // B's own schedule is later (rnd 7 -> +7 ms), so A asks first.
  t = kFirstVisitMinMs;
  a.update(t, true, roam, 0);
  deliver(a, b, t);  // VisitAsk
  TEST_ASSERT_EQUAL(VisitRole::Host, b.visit(t).role);
  TEST_ASSERT_EQUAL_STRING("Tofu", b.visit(t).name);
  TEST_ASSERT_EQUAL_UINT8(1, b.visit(t).mascot);
  deliver(b, a, t);  // VisitOk
  TEST_ASSERT_EQUAL(VisitRole::Visitor, a.visit(t).role);
  TEST_ASSERT_EQUAL_STRING("Nina", a.visit(t).name);
  TEST_ASSERT_EQUAL_UINT32(1500, a.visit(t + 1500).ms);
  // Busy gadgets say so, and do not accept a second visit.
  b.update(t + 3000, true, roam, 0);
  FriendPacket p;
  bool sawBusy = false;
  while (b.nextPacket(p)) sawBusy |= (p.flags & kFriendBusy) != 0;
  TEST_ASSERT_TRUE(sawBusy);
  // Over after kVisitMs on both sides; the next one is 6-12 minutes away.
  a.update(t + kVisitMs, true, roam, 0);
  b.update(t + kVisitMs, true, roam, 0);
  TEST_ASSERT_EQUAL(VisitRole::None, a.visit(t + kVisitMs).role);
  TEST_ASSERT_EQUAL(VisitRole::None, b.visit(t + kVisitMs).role);
  drain(a);
  a.update(t + kVisitMs + kNextVisitMinMs - 1, true, roam, 0);
  TEST_ASSERT_FALSE(a.nextPacket(p) && p.type == FriendPacket::VisitAsk);
}

static void test_visit_brings_coffee_to_a_tired_friend() {
  FriendPlay a;
  a.setSelf("miblo-aaaa", "Tofu", 0);
  a.update(0, true, kFriendRoaming, 0);
  a.receive(packet(FriendPacket::Beacon, "miblo-bbbb", "Nina", kFriendRoaming), 0);
  a.receive(packet(FriendPacket::Beacon, "miblo-cccc", "Mochi", kFriendRoaming | kFriendTired), 0);
  drain(a);
  a.update(kFirstVisitMinMs, true, kFriendRoaming, 0);
  FriendPacket p;
  bool asked = false;
  while (a.nextPacket(p)) {
    if (p.type != FriendPacket::VisitAsk) continue;
    asked = true;
    TEST_ASSERT_EQUAL_STRING("miblo-cccc", p.to);
    TEST_ASSERT_EQUAL(Gift::Coffee, p.gift);
  }
  TEST_ASSERT_TRUE(asked);
  a.receive(packet(FriendPacket::VisitOk, "miblo-cccc", "Mochi", kFriendRoaming, "miblo-aaaa", Gift::Coffee),
            kFirstVisitMinMs + 100);
  TEST_ASSERT_EQUAL(VisitRole::Visitor, a.visit(kFirstVisitMinMs + 100).role);
  TEST_ASSERT_EQUAL(Gift::Coffee, a.visit(kFirstVisitMinMs + 100).gift);
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
  a.update(0, true, kFriendRoaming, 0);
  b.update(0, true, kFriendRoaming, 999999);
  deliver(a, b, 0);
  deliver(b, a, 0);
  a.update(kFirstVisitMinMs, true, kFriendRoaming, 0);
  deliver(a, b, kFirstVisitMinMs);
  deliver(b, a, kFirstVisitMinMs);
  TEST_ASSERT_EQUAL(VisitRole::Visitor, a.visit(kFirstVisitMinMs).role);
  TEST_ASSERT_EQUAL(VisitRole::Host, b.visit(kFirstVisitMinMs).role);
  // Someone sits down at A's computer: A's mascot is home at once, and B's guest leaves.
  a.update(kFirstVisitMinMs + 5000, true, 0, 0);
  TEST_ASSERT_EQUAL(VisitRole::None, a.visit(kFirstVisitMinMs + 5000).role);
  deliver(a, b, kFirstVisitMinMs + 5000);
  TEST_ASSERT_EQUAL(VisitRole::None, b.visit(kFirstVisitMinMs + 5000).role);
}

static void test_simultaneous_requests_lower_id_visits() {
  FriendPlay a, b;
  a.setSelf("miblo-aaaa", "Tofu", 0);
  b.setSelf("miblo-bbbb", "Nina", 0);
  a.update(0, true, kFriendRoaming, 0);
  b.update(0, true, kFriendRoaming, 0);
  deliver(a, b, 0);
  deliver(b, a, 0);
  a.update(kFirstVisitMinMs, true, kFriendRoaming, 0);
  b.update(kFirstVisitMinMs, true, kFriendRoaming, 0);
  deliver(a, b, kFirstVisitMinMs);  // B was asking A too: B (higher id) accepts and hosts
  deliver(b, a, kFirstVisitMinMs);  // A ignores B's request and takes the Ok
  TEST_ASSERT_EQUAL(VisitRole::Visitor, a.visit(kFirstVisitMinMs).role);
  TEST_ASSERT_EQUAL(VisitRole::Host, b.visit(kFirstVisitMinMs).role);
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
  return UNITY_END();
}
