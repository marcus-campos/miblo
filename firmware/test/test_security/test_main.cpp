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
  TEST_ASSERT_EQUAL(PairingGuard::Result::BadCode, g.check("1111", 2000));  // 5º erro → bloqueia
  TEST_ASSERT_EQUAL(PairingGuard::Result::Locked, g.check("4827", 2001));  // nem o certo passa
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

static void test_token_store_up_to_four_replacing_oldest() {
  TokenStore s;
  s.add("t1", "mac");
  s.add("t2", "pc");
  s.add("t3", "wsl");
  s.add("t4", "linux");
  TEST_ASSERT_EQUAL_UINT8(4, s.count());
  s.add("t5", "new");  // cheio: sai o mais antigo (t1)
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
  TEST_ASSERT_FALSE(g.check(PresenceGate::Purpose::Reset, "1234", 2000));  // outro propósito
  TEST_ASSERT_TRUE(g.check(PresenceGate::Purpose::Update, "1234", 2000));
  TEST_ASSERT_FALSE(g.active(301000));  // expirou
  g.open(PresenceGate::Purpose::Reset, "9999", 0);
  for (int i = 0; i < 5; i++) TEST_ASSERT_FALSE(g.check(PresenceGate::Purpose::Reset, "0000", 10));
  TEST_ASSERT_FALSE(g.check(PresenceGate::Purpose::Reset, "9999", 10));  // fechado após 5 erros
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_codes_and_tokens);
  RUN_TEST(test_bearer_parsing);
  RUN_TEST(test_constant_time_equals);
  RUN_TEST(test_pairing_lockout_after_five_bad_codes);
  RUN_TEST(test_success_resets_failure_count);
  RUN_TEST(test_token_store_up_to_four_replacing_oldest);
  RUN_TEST(test_presence_gate);
  RUN_TEST(test_token_store_same_host_appends);
  RUN_TEST(test_find_content_length);
  RUN_TEST(test_presence_lockout_escalates);
  RUN_TEST(test_presence_lockout_caps_at_one_hour);
  return UNITY_END();
}
