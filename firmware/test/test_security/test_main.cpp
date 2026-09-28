#include <string.h>
#include <unity.h>

#include "miblo_security.h"

using namespace miblo;

void setUp() {}
void tearDown() {}

static void test_codes_and_tokens() {
  char code[5];
  formatCode(42, code);
  TEST_ASSERT_EQUAL_STRING("0042", code);
  formatCode(123456789, code);
  TEST_ASSERT_EQUAL_STRING("6789", code);
  uint8_t rnd[16];
  for (int i = 0; i < 16; i++) rnd[i] = (uint8_t)(i * 17);
  char tok[33];
  makeToken(rnd, tok);
  TEST_ASSERT_EQUAL_STRING("00112233445566778899aabbccddeeff", tok);
}

static void test_bearer_parsing() {
  char t[40];
  TEST_ASSERT_TRUE(bearerToken("Bearer abc123", t, sizeof(t)));
  TEST_ASSERT_EQUAL_STRING("abc123", t);
  TEST_ASSERT_TRUE(bearerToken("Bearer   spaced  ", t, sizeof(t)));
  TEST_ASSERT_EQUAL_STRING("spaced", t);
  TEST_ASSERT_FALSE(bearerToken("Basic abc", t, sizeof(t)));
  TEST_ASSERT_FALSE(bearerToken("Bearer ", t, sizeof(t)));
  TEST_ASSERT_FALSE(bearerToken(nullptr, t, sizeof(t)));
  TEST_ASSERT_FALSE(bearerToken("Bearer 0123456789", t, 5));
}

static void test_constant_time_equals() {
  TEST_ASSERT_TRUE(constantTimeEquals("4827", "4827"));
  TEST_ASSERT_FALSE(constantTimeEquals("4827", "4828"));
  TEST_ASSERT_FALSE(constantTimeEquals("482", "4827"));
  TEST_ASSERT_FALSE(constantTimeEquals("", "4827"));
  TEST_ASSERT_TRUE(constantTimeEquals("", ""));
}

static void test_pairing_lockout_after_five_bad_codes() {
  PairingGuard g;
  g.setCode("4827");
  TEST_ASSERT_EQUAL(PairingGuard::Result::Ok, g.check("4827", 0));
  for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL(PairingGuard::Result::BadCode, g.check("0000", 1000));
  TEST_ASSERT_EQUAL(PairingGuard::Result::BadCode, g.check("1111", 2000));  // 5th failure -> locks
  TEST_ASSERT_EQUAL(PairingGuard::Result::Locked, g.check("4827", 2001));  // not even the right one passes
  TEST_ASSERT_EQUAL_UINT32(59999, g.lockRemainingMs(2001));
  TEST_ASSERT_EQUAL(PairingGuard::Result::Locked, g.check("4827", 61999));
  TEST_ASSERT_EQUAL(PairingGuard::Result::Ok, g.check("4827", 62000));
  TEST_ASSERT_EQUAL(PairingGuard::Result::BadCode, g.check(nullptr, 62001));
}

static void test_success_resets_failure_count() {
  PairingGuard g;
  g.setCode("1234");
  for (int i = 0; i < 4; i++) g.check("0000", 0);
  TEST_ASSERT_EQUAL(PairingGuard::Result::Ok, g.check("1234", 0));
  for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL(PairingGuard::Result::BadCode, g.check("0000", 0));
  TEST_ASSERT_EQUAL(PairingGuard::Result::Ok, g.check("1234", 0));
}

static void failPairing(PairingGuard& g, uint32_t nowMs) {
  for (int i = 0; i < 5; i++) TEST_ASSERT_EQUAL(PairingGuard::Result::BadCode, g.check("0000", nowMs));
}

static void test_pairing_lockout_escalates_and_success_clears() {
  PairingGuard g;
  g.setCode("4827");
  failPairing(g, 0);  // 1st lockout: 60 s
  TEST_ASSERT_EQUAL_UINT32(60000, g.lockRemainingMs(0));
  TEST_ASSERT_EQUAL(PairingGuard::Result::Locked, g.check("4827", 59999));
  failPairing(g, 60000);  // 2nd lockout: 120 s
  TEST_ASSERT_EQUAL_UINT32(120000, g.lockRemainingMs(60000));
  TEST_ASSERT_EQUAL(PairingGuard::Result::Locked, g.check("4827", 179999));
  failPairing(g, 180000);  // 3rd lockout: 240 s
  TEST_ASSERT_EQUAL_UINT32(240000, g.lockRemainingMs(180000));
  // Keep failing: the lockout caps at 1 h.
  uint32_t t = 420000;
  for (int i = 0; i < 6; i++) {
    failPairing(g, t);
    t += g.lockRemainingMs(t);
  }
  failPairing(g, t);
  TEST_ASSERT_EQUAL_UINT32(3600000, g.lockRemainingMs(t));
  t += 3600000;
  // A correct code clears the escalation: the next lockout is back to 60 s.
  TEST_ASSERT_EQUAL(PairingGuard::Result::Ok, g.check("4827", t));
  failPairing(g, t);
  TEST_ASSERT_EQUAL_UINT32(60000, g.lockRemainingMs(t));
}

static void test_pairing_lockout_clock_wrap() {
  PairingGuard g;
  g.setCode("4827");
  failPairing(g, 1000);
  g.update(62000);  // the app calls update() every frame: the expired lockout is cleared
  // ~49.7 days later the clock wraps back near the old lock time: still unlocked.
  TEST_ASSERT_EQUAL_UINT32(0, g.lockRemainingMs(1500));
  TEST_ASSERT_EQUAL(PairingGuard::Result::Ok, g.check("4827", 1500));
}

static void test_token_store_up_to_four_replacing_oldest() {
  TokenStore s;
  s.add("t1", "mac");
  s.add("t2", "pc");
  s.add("t3", "wsl");
  s.add("t4", "linux");
  TEST_ASSERT_EQUAL_UINT8(4, s.count());
  s.add("t5", "new");  // full: the oldest one (t1) is evicted
  TEST_ASSERT_EQUAL_UINT8(4, s.count());
  TEST_ASSERT_FALSE(s.matches("t1"));
  TEST_ASSERT_TRUE(s.matches("t5"));
  s.add("t6", "pc");  // same host as t2: still appended; full, so the oldest (t2) is evicted
  TEST_ASSERT_EQUAL_UINT8(4, s.count());
  TEST_ASSERT_FALSE(s.matches("t2"));
  TEST_ASSERT_TRUE(s.matches("t6"));
  TEST_ASSERT_TRUE(s.matches("t3"));
  TEST_ASSERT_FALSE(s.matches(""));
  TEST_ASSERT_FALSE(s.matches(nullptr));
  TokenEntry copy[TokenStore::kMax];
  for (uint8_t i = 0; i < s.count(); i++) copy[i] = s.at(i);
  TokenStore r;
  r.restore(copy, s.count());
  TEST_ASSERT_TRUE(r.matches("t6"));
  r.clear();
  TEST_ASSERT_FALSE(r.matches("t6"));
}

// Host names are truncated and may collide: pairing the same host again never drops a token.
static void test_token_store_same_host_appends() {
  TokenStore s;
  s.add("a1", "mac");
  s.add("a2", "mac");
  TEST_ASSERT_EQUAL_UINT8(2, s.count());
  TEST_ASSERT_TRUE(s.matches("a1"));
  TEST_ASSERT_TRUE(s.matches("a2"));
}

static void test_find_content_length() {
  uint32_t n = 0;
  const char h1[] = "Host: x\r\ncontent-LENGTH:  5000\r\nX: y\r\n\r\n";
  TEST_ASSERT_TRUE(findContentLength(h1, sizeof(h1) - 1, n));
  TEST_ASSERT_EQUAL_UINT32(5000, n);
  const char h2[] = "Content-Length: 99999999999\r\n";
  TEST_ASSERT_TRUE(findContentLength(h2, sizeof(h2) - 1, n));
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, n);  // saturates
  const char h3[] = "Host: x\r\n\r\nContent-Length: 9";  // after the blank line: body, not a header
  TEST_ASSERT_FALSE(findContentLength(h3, sizeof(h3) - 1, n));
  const char h4[] = "Content-Length: 12";  // truncated buffer: parse only what is there
  TEST_ASSERT_TRUE(findContentLength(h4, 17, n));
  TEST_ASSERT_EQUAL_UINT32(1, n);
  TEST_ASSERT_FALSE(findContentLength("Content-Length: x\r\n", 19, n));
  TEST_ASSERT_FALSE(findContentLength("X-Content-Length: 5\r\n", 21, n));
  // Duplicates: the core honours the last one, so the check uses the largest of them.
  const char h5[] = "Content-Length: 10\r\nHost: x\r\nContent-Length: 90000\r\nContent-Length: 20\r\n\r\n";
  TEST_ASSERT_TRUE(findContentLength(h5, sizeof(h5) - 1, n));
  TEST_ASSERT_EQUAL_UINT32(90000, n);
}

static void test_content_type_is_multipart() {
  const char h1[] = "Host: x\r\ncontent-TYPE:  Multipart/form-data; boundary=abc\r\n\r\n";
  TEST_ASSERT_TRUE(contentTypeIsMultipart(h1, sizeof(h1) - 1));
  const char h2[] = "Content-Type: application/json\r\n\r\n";
  TEST_ASSERT_FALSE(contentTypeIsMultipart(h2, sizeof(h2) - 1));
  const char h3[] = "Host: x\r\n\r\nContent-Type: multipart/form-data";  // body, not a header
  TEST_ASSERT_FALSE(contentTypeIsMultipart(h3, sizeof(h3) - 1));
  const char h4[] = "Content-Type: multi";  // truncated
  TEST_ASSERT_FALSE(contentTypeIsMultipart(h4, sizeof(h4) - 1));
  TEST_ASSERT_FALSE(contentTypeIsMultipart("X-Content-Type: multipart/x\r\n", 29));
  TEST_ASSERT_FALSE(contentTypeIsMultipart("Host: x\r\n", 9));
}

// Re-opening (a new code) must not grant fresh guesses: 4 bad, re-open, 1 bad → locked.
static void test_presence_failures_survive_reopen() {
  PresenceGate g;
  TEST_ASSERT_TRUE(g.open(PresenceGate::Purpose::Reset, "1111", 0));
  for (int i = 0; i < 4; i++) TEST_ASSERT_FALSE(g.check(PresenceGate::Purpose::Reset, "0000", 10));
  TEST_ASSERT_FALSE(g.locked(10));
  TEST_ASSERT_TRUE(g.open(PresenceGate::Purpose::Update, "2222", 20));  // new code, same failures
  TEST_ASSERT_FALSE(g.check(PresenceGate::Purpose::Update, "0000", 30));
  TEST_ASSERT_TRUE(g.locked(30));
  TEST_ASSERT_EQUAL_UINT32(60000, g.lockRemainingMs(30));
  TEST_ASSERT_FALSE(g.open(PresenceGate::Purpose::Update, "3333", 40));  // 429 path
  // A correct code clears the accumulated failures.
  PresenceGate h;
  h.open(PresenceGate::Purpose::Update, "1234", 0);
  for (int i = 0; i < 4; i++) h.check(PresenceGate::Purpose::Update, "0000", 0);
  TEST_ASSERT_TRUE(h.check(PresenceGate::Purpose::Update, "1234", 0));
  h.open(PresenceGate::Purpose::Update, "1234", 0);
  for (int i = 0; i < 4; i++) h.check(PresenceGate::Purpose::Update, "0000", 0);
  TEST_ASSERT_FALSE(h.locked(0));
}

// The lockout is honoured across a clock wrap, and an expired one never comes back ~49.7 days
// later when the unsigned elapsed time wraps around.
static void test_presence_lockout_clock_wrap() {
  PresenceGate g;
  const uint32_t t0 = 0xFFFFF000u;  // 4096 ms before the wrap
  g.open(PresenceGate::Purpose::Update, "1234", t0);
  for (int i = 0; i < 5; i++) g.check(PresenceGate::Purpose::Update, "0000", t0);
  TEST_ASSERT_TRUE(g.locked(0x00000100u));  // wrapped, 4352 ms later: still locked
  TEST_ASSERT_EQUAL_UINT32(60000 - 4352, g.lockRemainingMs(0x00000100u));
  const uint32_t expired = t0 + 60000;
  g.update(expired);  // the app calls this every frame
  TEST_ASSERT_FALSE(g.locked(expired));
  const uint32_t phantom = t0 + 10;  // same low bits one full wrap (2^32 ms) later
  TEST_ASSERT_FALSE(g.locked(phantom));
  TEST_ASSERT_TRUE(g.open(PresenceGate::Purpose::Update, "1234", phantom));
}

// Brute force: the lockout survives re-opening the gate and escalates until a correct code.
static void test_presence_lockout_escalates() {
  PresenceGate g;
  uint32_t t = 0;
  const uint32_t expected[] = {60000, 120000, 240000};
  for (uint32_t lock : expected) {
    TEST_ASSERT_TRUE(g.open(PresenceGate::Purpose::Update, "1234", t));
    for (int i = 0; i < 5; i++) TEST_ASSERT_FALSE(g.check(PresenceGate::Purpose::Update, "0000", t));
    TEST_ASSERT_FALSE(g.active(t));
    TEST_ASSERT_TRUE(g.locked(t));
    TEST_ASSERT_EQUAL_UINT32(lock, g.lockRemainingMs(t));
    TEST_ASSERT_FALSE(g.open(PresenceGate::Purpose::Reset, "5555", t + 1));  // re-open refused
    TEST_ASSERT_FALSE(g.active(t + 1));
    t += lock - 1;
    TEST_ASSERT_TRUE(g.locked(t));
    t += 1;
    TEST_ASSERT_FALSE(g.locked(t));
  }
  // A correct code resets the escalation back to 60 s.
  TEST_ASSERT_TRUE(g.open(PresenceGate::Purpose::Update, "1234", t));
  TEST_ASSERT_TRUE(g.check(PresenceGate::Purpose::Update, "1234", t));
  TEST_ASSERT_TRUE(g.open(PresenceGate::Purpose::Update, "1234", t));
  for (int i = 0; i < 5; i++) g.check(PresenceGate::Purpose::Update, "0000", t);
  TEST_ASSERT_EQUAL_UINT32(60000, g.lockRemainingMs(t));
}

static void test_presence_lockout_caps_at_one_hour() {
  PresenceGate g;
  uint32_t t = 0;
  for (int round = 0; round < 10; round++) {
    TEST_ASSERT_TRUE(g.open(PresenceGate::Purpose::Update, "1234", t));
    for (int i = 0; i < 5; i++) g.check(PresenceGate::Purpose::Update, "0000", t);
    t += g.lockRemainingMs(t);
  }
  TEST_ASSERT_TRUE(g.open(PresenceGate::Purpose::Update, "1234", t));
  for (int i = 0; i < 5; i++) g.check(PresenceGate::Purpose::Update, "0000", t);
  TEST_ASSERT_EQUAL_UINT32(PresenceGate::kLockMaxMs, g.lockRemainingMs(t));
}

static void test_presence_gate() {
  PresenceGate g;
  TEST_ASSERT_FALSE(g.active(0));
  TEST_ASSERT_FALSE(g.check(PresenceGate::Purpose::Update, "1234", 0));
  g.open(PresenceGate::Purpose::Update, "1234", 1000);
  TEST_ASSERT_TRUE(g.active(1000));
  TEST_ASSERT_EQUAL_UINT32(300000, g.remainingMs(1000));
  TEST_ASSERT_FALSE(g.check(PresenceGate::Purpose::Reset, "1234", 2000));  // different purpose
  TEST_ASSERT_TRUE(g.check(PresenceGate::Purpose::Update, "1234", 2000));
  TEST_ASSERT_FALSE(g.active(301000));  // expired
  g.open(PresenceGate::Purpose::Reset, "9999", 0);
  for (int i = 0; i < 5; i++) TEST_ASSERT_FALSE(g.check(PresenceGate::Purpose::Reset, "0000", 10));
  TEST_ASSERT_FALSE(g.check(PresenceGate::Purpose::Reset, "9999", 10));  // closed after 5 failures
}

static void test_ota_code_required() {
  // (everConfigured, hasWifiCreds, tokenCount, viaSoftAp)
  // Never-configured shelf unit, empty, on its own setup AP: codeless.
  TEST_ASSERT_FALSE(otaCodeRequired(false, false, 0, true));
  // Configured once, then factory reset (no Wi-Fi, no pairings, on the setup AP): code required.
  TEST_ASSERT_TRUE(otaCodeRequired(true, false, 0, true));
  TEST_ASSERT_TRUE(otaCodeRequired(false, true, 0, true));    // saved Wi-Fi
  TEST_ASSERT_TRUE(otaCodeRequired(false, false, 1, true));   // paired
  TEST_ASSERT_TRUE(otaCodeRequired(false, false, 4, true));
  TEST_ASSERT_TRUE(otaCodeRequired(false, false, 0, false));  // not via the soft AP
  TEST_ASSERT_TRUE(otaCodeRequired(false, true, 2, false));
  TEST_ASSERT_TRUE(otaCodeRequired(false, true, 0, false));
  TEST_ASSERT_TRUE(otaCodeRequired(false, false, 3, false));
  TEST_ASSERT_TRUE(otaCodeRequired(false, true, 1, true));
  TEST_ASSERT_TRUE(otaCodeRequired(true, true, 1, false));
  TEST_ASSERT_TRUE(otaCodeRequired(true, false, 0, false));
}

static void test_via_soft_ap_subnet() {
  const uint8_t ap[4] = {192, 168, 4, 1};
  const uint8_t phone[4] = {192, 168, 4, 2};
  const uint8_t lanLocal[4] = {192, 168, 1, 50};
  const uint8_t lanPeer[4] = {192, 168, 1, 10};
  const uint8_t otherAp[4] = {10, 0, 0, 1};
  TEST_ASSERT_TRUE(viaSoftApSubnet(true, phone, ap, ap));
  TEST_ASSERT_FALSE(viaSoftApSubnet(false, phone, ap, ap));         // AP not up
  TEST_ASSERT_FALSE(viaSoftApSubnet(true, lanPeer, lanLocal, ap));  // came in over the LAN
  TEST_ASSERT_FALSE(viaSoftApSubnet(true, lanPeer, ap, ap));        // peer outside 192.168.4.0/24
  TEST_ASSERT_FALSE(viaSoftApSubnet(true, phone, lanLocal, ap));    // accepted on another interface
  TEST_ASSERT_FALSE(viaSoftApSubnet(true, phone, otherAp, otherAp));  // soft AP not on 192.168.4.x
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_codes_and_tokens);
  RUN_TEST(test_bearer_parsing);
  RUN_TEST(test_constant_time_equals);
  RUN_TEST(test_pairing_lockout_after_five_bad_codes);
  RUN_TEST(test_success_resets_failure_count);
  RUN_TEST(test_pairing_lockout_escalates_and_success_clears);
  RUN_TEST(test_pairing_lockout_clock_wrap);
  RUN_TEST(test_token_store_up_to_four_replacing_oldest);
  RUN_TEST(test_presence_gate);
  RUN_TEST(test_token_store_same_host_appends);
  RUN_TEST(test_find_content_length);
  RUN_TEST(test_content_type_is_multipart);
  RUN_TEST(test_presence_lockout_escalates);
  RUN_TEST(test_presence_lockout_caps_at_one_hour);
  RUN_TEST(test_presence_failures_survive_reopen);
  RUN_TEST(test_presence_lockout_clock_wrap);
  RUN_TEST(test_ota_code_required);
  RUN_TEST(test_via_soft_ap_subnet);
  return UNITY_END();
}
