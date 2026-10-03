#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

#include <string>

#include <ArduinoJson.h>

#include "miblo_headers.h"
#include "miblo_info.h"
#include "miblo_security.h"
#include "miblo_sha256.h"

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

// Wrong codes until a lockout starts: 5 the first time, 3 for each further one. Returns how many.
static int failPairing(PairingGuard& g, uint32_t nowMs) {
  int n = 0;
  while (g.lockRemainingMs(nowMs) == 0 && n < 10) {
    TEST_ASSERT_EQUAL(PairingGuard::Result::BadCode, g.check("0000", nowMs));
    n++;
  }
  return n;
}

static void test_pairing_lockout_escalates_and_success_clears() {
  PairingGuard g;
  g.setCode("4827");
  TEST_ASSERT_EQUAL(5, failPairing(g, 0));  // 1st lockout: 60 s, after 5 wrong codes
  TEST_ASSERT_EQUAL_UINT32(60000, g.lockRemainingMs(0));
  TEST_ASSERT_EQUAL(PairingGuard::Result::Locked, g.check("4827", 59999));
  TEST_ASSERT_EQUAL(3, failPairing(g, 60000));  // 2nd lockout: 120 s, after 3 more
  TEST_ASSERT_EQUAL_UINT32(120000, g.lockRemainingMs(60000));
  TEST_ASSERT_EQUAL(PairingGuard::Result::Locked, g.check("4827", 179999));
  TEST_ASSERT_EQUAL(3, failPairing(g, 180000));  // 3rd lockout: 240 s
  TEST_ASSERT_EQUAL_UINT32(240000, g.lockRemainingMs(180000));
  // Keep failing: the lockout caps at 24 h, reached after 38 guesses over ~34 h.
  uint32_t t = 420000;
  int guesses = 11;
  while (g.lockRemainingMs(t) < EscalatingLockout::kMaxMs) {
    t += g.lockRemainingMs(t);
    guesses += failPairing(g, t);
  }
  TEST_ASSERT_EQUAL(38, guesses);
  TEST_ASSERT_EQUAL_UINT32(86400000, g.lockRemainingMs(t));
  TEST_ASSERT_TRUE(t < 35u * 3600000u);
  t += 86400000;
  TEST_ASSERT_EQUAL(3, failPairing(g, t));  // from then on: 3 guesses a day
  TEST_ASSERT_EQUAL_UINT32(86400000, g.lockRemainingMs(t));
  t += 86400000;
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
// The pairing's host name: cut to 20 characters without splitting one; a name that is not
// clean text (malformed UTF-8, controls) is stored as "computer", so its row can be renamed.
static void test_token_store_add_cleans_the_host() {
  TokenStore s;
  s.add("t1", "abcdefghijklmnopqrstuvwxyz");
  TEST_ASSERT_EQUAL_STRING("abcdefghijklmnopqrst", s.at(0).host);
  s.add("t2", "bad\xffhost");
  TEST_ASSERT_EQUAL_STRING("computer", s.at(1).host);
  s.add("t3", "ctl\x01");
  TEST_ASSERT_EQUAL_STRING("computer", s.at(2).host);
  s.add("t4", "ééééééééééééééééééé");  // 19 two-byte characters: cut whole at 16 (32 bytes)
  TEST_ASSERT_EQUAL_STRING("éééééééééééééééé", s.at(3).host);
  TEST_ASSERT_TRUE(s.remove(3, "éééééééééééééééé"));
}

static void test_token_store_same_host_appends() {
  TokenStore s;
  s.add("a1", "mac");
  s.add("a2", "mac");
  TEST_ASSERT_EQUAL_UINT8(2, s.count());
  TEST_ASSERT_TRUE(s.matches("a1"));
  TEST_ASSERT_TRUE(s.matches("a2"));
}

// The settings page lists the paired computers and removes one: by its place in the list AND its
// host name (a list that changed in between never loses the wrong one); the rest keep working.
// Each entry remembers when its token last came in (not stored: from boot).
static void test_token_store_remove_and_seen() {
  TokenStore s;
  s.add("t1", "mac");
  s.add("t2", "pc");
  s.add("t3", "wsl");
  TEST_ASSERT_EQUAL_INT(1, s.find("t2"));
  TEST_ASSERT_EQUAL_INT(-1, s.find("nope"));
  TEST_ASSERT_EQUAL_INT(-1, s.find(nullptr));
  TEST_ASSERT_FALSE(s.everSeen(1));
  s.seen(1, 5000);
  TEST_ASSERT_TRUE(s.everSeen(1));
  TEST_ASSERT_EQUAL_UINT32(5000, s.seenAt(1));
  s.seen(2, 7000);
  TEST_ASSERT_FALSE(s.remove(1, "mac"));  // the host doesn't match that place: nothing removed
  TEST_ASSERT_FALSE(s.remove(7, "pc"));
  TEST_ASSERT_EQUAL_UINT8(3, s.count());
  TEST_ASSERT_TRUE(s.remove(1, "pc"));
  TEST_ASSERT_EQUAL_UINT8(2, s.count());
  TEST_ASSERT_FALSE(s.matches("t2"));
  TEST_ASSERT_TRUE(s.matches("t1"));
  TEST_ASSERT_TRUE(s.matches("t3"));
  TEST_ASSERT_EQUAL_STRING("wsl", s.at(1).host);  // the next one moved up, with its "seen"
  TEST_ASSERT_EQUAL_UINT32(7000, s.seenAt(1));
  TEST_ASSERT_FALSE(s.everSeen(0));
  s.add("t4", "linux");  // a new pairing starts unseen
  TEST_ASSERT_FALSE(s.everSeen(2));
  TEST_ASSERT_TRUE(s.remove(0, "mac"));
  TEST_ASSERT_TRUE(s.remove(0, "wsl"));
  TEST_ASSERT_TRUE(s.remove(0, "linux"));
  TEST_ASSERT_EQUAL_UINT8(0, s.count());
  TEST_ASSERT_FALSE(s.remove(0, "linux"));
}

// The token store with a failed save is put back exactly as before (pairing: the new token is
// gone and an evicted one is back; removing: the computer is back in its place, "seen" included),
// so the RAM never lets in a computer the flash would forget, or keep out one it would restore.
static bool same(const TokenStore& a, const TokenStore& b) {
  if (a.count() != b.count()) return false;
  for (uint8_t i = 0; i < a.count(); i++) {
    const TokenEntry &x = a.at(i), &y = b.at(i);
    if (strcmp(x.token, y.token) || strcmp(x.host, y.host) || x.custom != y.custom || x.order != y.order ||
        a.everSeen(i) != b.everSeen(i) || (a.everSeen(i) && a.seenAt(i) != b.seenAt(i)))
      return false;
  }
  return true;
}

static void test_token_store_undo_add() {
  TokenStore s;
  s.add("t1", "mac");
  s.seen(0, 100);
  TokenStore before = s;
  TokenUndo u;
  s.add("t2", "pc", &u);
  TEST_ASSERT_TRUE(s.matches("t2"));
  s.undo(u);
  TEST_ASSERT_TRUE(same(before, s));
  TEST_ASSERT_FALSE(s.matches("t2"));
  // Full: the evicted oldest comes back in its place, with its "seen".
  s.add("t2", "pc");
  s.add("t3", "wsl");
  s.add("t4", "linux");
  s.seen(2, 300);
  before = s;
  s.add("t5", "new", &u);
  TEST_ASSERT_FALSE(s.matches("t1"));
  s.undo(u);
  TEST_ASSERT_TRUE(same(before, s));
  TEST_ASSERT_TRUE(s.matches("t1"));
  TEST_ASSERT_FALSE(s.matches("t5"));
  TEST_ASSERT_TRUE(s.everSeen(0));
  TEST_ASSERT_EQUAL_UINT32(100, s.seenAt(0));
}

static void test_token_store_undo_remove() {
  TokenStore s;
  s.add("t1", "mac");
  s.add("t2", "pc");
  s.add("t3", "wsl");
  TEST_ASSERT_TRUE(s.rename(1, "pc", "Desk") == TokenStore::RenameResult::Ok);
  s.seen(1, 500);
  s.seen(2, 700);
  for (uint8_t i = 0; i < 3; i++) {
    const TokenStore before = s;
    TokenUndo u;
    TEST_ASSERT_TRUE(s.remove(i, before.at(i).host, &u));
    TEST_ASSERT_FALSE(s.matches(before.at(i).token));
    s.undo(u);
    TEST_ASSERT_TRUE(same(before, s));
  }
  TokenUndo u;
  TEST_ASSERT_FALSE(s.remove(1, "mac", &u));  // nothing removed: nothing to undo
  const TokenStore before = s;
  s.undo(u);
  TEST_ASSERT_TRUE(same(before, s));
}

// The settings page renames a paired computer: by its place AND its current label (like remove).
// The new label is the user's (custom): automatic labels from the computer's snapshots no longer
// replace it. An empty name gives the label back to the computer. Same rules as a gadget name.
static void test_token_store_rename() {
  TokenStore s;
  s.add("t1", "mac");
  s.add("t2", "pc");
  TEST_ASSERT_FALSE(s.at(0).custom);
  TEST_ASSERT_TRUE(s.rename(0, "mac", "Work laptop") == TokenStore::RenameResult::Ok);
  TEST_ASSERT_EQUAL_STRING("Work laptop", s.at(0).host);
  TEST_ASSERT_TRUE(s.at(0).custom);
  TEST_ASSERT_TRUE(s.matches("t1"));  // the token is untouched
  TEST_ASSERT_TRUE(s.rename(0, "mac", "x") == TokenStore::RenameResult::Changed);  // stale label
  TEST_ASSERT_TRUE(s.rename(5, "pc", "x") == TokenStore::RenameResult::Changed);
  TEST_ASSERT_TRUE(s.rename(1, nullptr, "x") == TokenStore::RenameResult::Changed);
  TEST_ASSERT_TRUE(s.rename(1, "pc", "123456789012345678901") == TokenStore::RenameResult::BadName);  // 21
  TEST_ASSERT_TRUE(s.rename(1, "pc", "tab\there") == TokenStore::RenameResult::BadName);
  TEST_ASSERT_TRUE(s.rename(1, "pc", "bad\xff") == TokenStore::RenameResult::BadName);
  TEST_ASSERT_TRUE(s.rename(1, "pc", "sur\xed\xa0\x80") == TokenStore::RenameResult::BadName);
  TEST_ASSERT_TRUE(s.rename(1, "pc", nullptr) == TokenStore::RenameResult::BadName);
  TEST_ASSERT_EQUAL_STRING("pc", s.at(1).host);
  TEST_ASSERT_FALSE(s.at(1).custom);
  // 17 two-byte characters (34 bytes) never fit the 32-byte label: refused, not cut.
  TEST_ASSERT_TRUE(s.rename(1, "pc", "ééééééééééééééééé") == TokenStore::RenameResult::BadName);
  TEST_ASSERT_TRUE(s.rename(1, "pc", "Café do João") == TokenStore::RenameResult::Ok);
  TEST_ASSERT_EQUAL_STRING("Café do João", s.at(1).host);
  // Removing goes by the current label.
  TEST_ASSERT_FALSE(s.remove(1, "pc"));
  TEST_ASSERT_TRUE(s.remove(1, "Café do João"));
  // Empty: automatic again (the label stays until the computer's next snapshot names it).
  TEST_ASSERT_TRUE(s.rename(0, "Work laptop", "") == TokenStore::RenameResult::Ok);
  TEST_ASSERT_FALSE(s.at(0).custom);
  TEST_ASSERT_EQUAL_STRING("Work laptop", s.at(0).host);
  TEST_ASSERT_TRUE(s.autoLabel(0, "mac"));
  TEST_ASSERT_EQUAL_STRING("mac", s.at(0).host);
  // A new pairing in a place a custom one held is automatic.
  TEST_ASSERT_TRUE(s.rename(0, "mac", "Mine") == TokenStore::RenameResult::Ok);
  TEST_ASSERT_TRUE(s.remove(0, "Mine"));
  s.add("t3", "linux");
  TEST_ASSERT_FALSE(s.at(0).custom);
}

// A snapshot names its computer (its host name): an automatic label follows it, a custom one
// never does. Saved at most once a minute (flash wear), and only when something changed.
static void test_token_store_auto_label() {
  TokenStore s;
  s.add("t1", "old-name");
  s.add("t2", "pc");
  TEST_ASSERT_FALSE(s.saveDue(0));
  TEST_ASSERT_FALSE(s.autoLabel(0, "old-name"));  // same: nothing to do
  TEST_ASSERT_FALSE(s.autoLabel(0, ""));          // none: kept
  TEST_ASSERT_FALSE(s.autoLabel(0, nullptr));
  TEST_ASSERT_FALSE(s.autoLabel(0, "bad\x01name"));
  TEST_ASSERT_FALSE(s.autoLabel(0, "bad\xffname"));      // malformed UTF-8: kept as it was
  TEST_ASSERT_FALSE(s.autoLabel(0, "over\xc0\xafname"));
  TEST_ASSERT_FALSE(s.autoLabel(9, "x"));
  TEST_ASSERT_FALSE(s.saveDue(0));
  TEST_ASSERT_TRUE(s.autoLabel(0, "new-name"));
  TEST_ASSERT_EQUAL_STRING("new-name", s.at(0).host);
  TEST_ASSERT_TRUE(s.saveDue(1000));  // first save: at once
  s.saved(1000);
  TEST_ASSERT_FALSE(s.saveDue(1000));
  TEST_ASSERT_TRUE(s.autoLabel(1, "pc-2"));
  TEST_ASSERT_FALSE(s.saveDue(30000));  // within a minute of the last save: waits
  TEST_ASSERT_TRUE(s.saveDue(61000));
  s.saved(61000);
  // A long host name is cut like the pairing's: 20 characters.
  TEST_ASSERT_TRUE(s.autoLabel(1, "abcdefghijklmnopqrstuvwxyz"));
  TEST_ASSERT_EQUAL_STRING("abcdefghijklmnopqrst", s.at(1).host);
  TEST_ASSERT_FALSE(s.autoLabel(1, "abcdefghijklmnopqrstuvwxyz"));  // same once cut
  // Custom: the snapshot's name is ignored.
  TEST_ASSERT_TRUE(s.rename(0, "new-name", "Desk") == TokenStore::RenameResult::Ok);
  TEST_ASSERT_FALSE(s.autoLabel(0, "new-name"));
  TEST_ASSERT_EQUAL_STRING("Desk", s.at(0).host);
}

// pairs.json: the custom flag round-trips ("c":true only when set); a file from before it loads
// every label as automatic; damaged entries are skipped.
static void test_tokens_json_round_trip() {
  TokenStore s;
  s.add("00112233445566778899aabbccddeeff", "mac");
  s.add("ffeeddccbbaa99887766554433221100", "pc");
  TEST_ASSERT_TRUE(s.rename(1, "pc", "Living room") == TokenStore::RenameResult::Ok);
  DynamicJsonDocument doc(1024);
  tokensToJson(s, doc.to<JsonObject>());
  TEST_ASSERT_TRUE(doc["pairs"][0]["c"].isNull());
  TEST_ASSERT_TRUE(doc["pairs"][1]["c"].as<bool>());
  char text[1024];
  serializeJson(doc, text, sizeof(text));
  DynamicJsonDocument back(1024);
  TEST_ASSERT_FALSE((bool)deserializeJson(back, text));
  TokenStore r;
  tokensFromJson(back.as<JsonObjectConst>(), r);
  TEST_ASSERT_EQUAL_UINT8(2, r.count());
  TEST_ASSERT_EQUAL_STRING("mac", r.at(0).host);
  TEST_ASSERT_FALSE(r.at(0).custom);
  TEST_ASSERT_EQUAL_STRING("Living room", r.at(1).host);
  TEST_ASSERT_TRUE(r.at(1).custom);
  TEST_ASSERT_EQUAL_UINT32(s.at(1).order, r.at(1).order);
  TEST_ASSERT_TRUE(r.matches("ffeeddccbbaa99887766554433221100"));

  const char old[] =
      "{\"pairs\":[{\"token\":\"00112233445566778899aabbccddeeff\",\"host\":\"mac\",\"order\":3},"
      "{\"token\":\"short\",\"host\":\"x\",\"order\":4},"
      "{\"token\":\"ffeeddccbbaa99887766554433221100\",\"host\":\"a-very-long-host-name-from-somewhere-else\",\"order\":5}]}";
  DynamicJsonDocument od(1024);
  TEST_ASSERT_FALSE((bool)deserializeJson(od, old));
  TokenStore o;
  tokensFromJson(od.as<JsonObjectConst>(), o);
  TEST_ASSERT_EQUAL_UINT8(2, o.count());
  TEST_ASSERT_FALSE(o.at(0).custom);
  TEST_ASSERT_FALSE(o.at(1).custom);
  TEST_ASSERT_EQUAL_UINT32(5, o.at(1).order);
  TEST_ASSERT_EQUAL_STRING("a-very-long-host-nam", o.at(1).host);  // cut like a label
}

// GET /settings-system: what the System panel draws. Program = the firmware against the largest
// program the flash layout takes (fwMax); Data = the filesystem.
static void test_system_json_fields() {
  SystemStats st{};
  st.cpu = 12;
  st.mhz = 80;
  st.ramUsed = 50000;
  st.ram = 81920;
  st.fw = 870000;
  st.fwMax = 1044464;
  st.fsUsed = 49152;
  st.fs = 1024000;
  StaticJsonDocument<256> doc;
  writeSystemInfo(doc.to<JsonObject>(), st);
  TEST_ASSERT_EQUAL(8, (int)doc.as<JsonObject>().size());
  TEST_ASSERT_EQUAL(12, doc["cpu"].as<int>());
  TEST_ASSERT_EQUAL(80, doc["mhz"].as<int>());
  TEST_ASSERT_EQUAL_UINT32(50000, doc["ramUsed"].as<uint32_t>());
  TEST_ASSERT_EQUAL_UINT32(81920, doc["ram"].as<uint32_t>());
  TEST_ASSERT_EQUAL_UINT32(870000, doc["fw"].as<uint32_t>());
  TEST_ASSERT_EQUAL_UINT32(1044464, doc["fwMax"].as<uint32_t>());
  TEST_ASSERT_EQUAL_UINT32(49152, doc["fsUsed"].as<uint32_t>());
  TEST_ASSERT_EQUAL_UINT32(1024000, doc["fs"].as<uint32_t>());
}

// The settings page marks the computer that opened it by a tag of its token (FNV-1a-64, first 8
// hex): stable, different per token, and only 32 bits of a 128-bit random token.
static void test_token_tag() {
  char tag[9];
  tokenTag("", tag);
  TEST_ASSERT_EQUAL_STRING("cbf29ce4", tag);  // FNV-1a-64 of "": cbf29ce484222325
  tokenTag("a", tag);
  TEST_ASSERT_EQUAL_STRING("af63dc4c", tag);
  tokenTag("00112233445566778899aabbccddeeff", tag);
  TEST_ASSERT_EQUAL_STRING("de18ad43", tag);
  tokenTag("00112233445566778899aabbccddeeff", tag);
  TEST_ASSERT_EQUAL_STRING("de18ad43", tag);  // stable
  tokenTag("ffeeddccbbaa99887766554433221100", tag);
  TEST_ASSERT_EQUAL_STRING("789a7dc7", tag);  // another token, another tag
  tokenTag(nullptr, tag);
  TEST_ASSERT_EQUAL_STRING("cbf29ce4", tag);
}

// M1: DNS rebinding. A page on attacker.example that rebinds its name to the gadget's IP sends
// "Host: attacker.example"; only the gadget's own names and IP literals are served.
static void test_host_policy() {
  const char* id = "miblo-4f2a";
  TEST_ASSERT_TRUE(hostAllowed(nullptr, id));  // no Host (an HTTP/1.0 tool): no browser sends that
  TEST_ASSERT_TRUE(hostAllowed("", id));
  TEST_ASSERT_TRUE(hostAllowed("192.168.0.41", id));
  TEST_ASSERT_TRUE(hostAllowed("192.168.0.41:80", id));
  TEST_ASSERT_TRUE(hostAllowed("10.0.0.7:8080", id));
  TEST_ASSERT_TRUE(hostAllowed("[fe80::1]", id));
  TEST_ASSERT_TRUE(hostAllowed("[fe80::1]:80", id));
  TEST_ASSERT_TRUE(hostAllowed("miblo-4f2a.local", id));
  TEST_ASSERT_TRUE(hostAllowed("MIBLO-4F2A.LOCAL.", id));
  TEST_ASSERT_TRUE(hostAllowed("miblo-4f2a.local:80", id));
  TEST_ASSERT_TRUE(hostAllowed("miblo-4f2a", id));  // the router's DNS (DHCP host name)
  TEST_ASSERT_TRUE(hostAllowed("Miblo-4F2A.", id));
  TEST_ASSERT_TRUE(hostAllowed("miblo-4f2a:8080", id));
  // F2: the id is only 16 bits, so <id>.<any domain> lets a rebinding domain guess its way in:
  // only <id>.local is the gadget's own name. The plugin (bearer token) is not affected.
  TEST_ASSERT_FALSE(hostAllowed("miblo-4f2a.fritz.box", id));
  TEST_ASSERT_FALSE(hostAllowed("miblo-4f2a.lan", id));
  TEST_ASSERT_FALSE(hostAllowed("miblo-4f2a.attacker.example", id));
  TEST_ASSERT_FALSE(hostAllowed("miblo-4f2a.local.attacker.example", id));
  TEST_ASSERT_FALSE(hostAllowed("miblo-4f2a.localx", id));
  TEST_ASSERT_FALSE(originAllowed("http://miblo-4f2a.lan", id));
  TEST_ASSERT_FALSE(hostAllowed("attacker.example", id));
  TEST_ASSERT_FALSE(hostAllowed("attacker.example:80", id));
  TEST_ASSERT_FALSE(hostAllowed("miblo-4f2b.local", id));     // another unit's name
  TEST_ASSERT_FALSE(hostAllowed("miblo-4f2a0.local", id));
  TEST_ASSERT_FALSE(hostAllowed("xmiblo-4f2a.local", id));
  TEST_ASSERT_FALSE(hostAllowed("192.168.0.41.attacker.example", id));
  TEST_ASSERT_FALSE(hostAllowed("192.168.0", id));
  TEST_ASSERT_FALSE(hostAllowed("192.168.0.256", id));
  TEST_ASSERT_FALSE(hostAllowed("1.2.3.4:x", id));
  TEST_ASSERT_FALSE(hostAllowed("[fe80::1", id));
  TEST_ASSERT_FALSE(hostAllowed("miblo-4f2a.local:80:80", id));
  TEST_ASSERT_FALSE(hostAllowed("miblo-4f2a..local", id));
  TEST_ASSERT_FALSE(hostAllowed("miblo-4f2a.local/x", id));
  TEST_ASSERT_TRUE(hostAllowed("192.168.0.41", ""));  // no id yet: IP literals are still fine
  TEST_ASSERT_FALSE(hostAllowed(".local", ""));
  TEST_ASSERT_TRUE(hostAllowed("192.168.0.41", nullptr));
  // Origin: absent (same-origin GET, curl, the plugin) or http:// one of those hosts.
  TEST_ASSERT_TRUE(originAllowed(nullptr, id));
  TEST_ASSERT_TRUE(originAllowed("", id));
  TEST_ASSERT_TRUE(originAllowed("http://192.168.0.41", id));
  TEST_ASSERT_TRUE(originAllowed("http://miblo-4f2a.local", id));
  TEST_ASSERT_TRUE(originAllowed("HTTP://miblo-4f2a.local:80", id));
  TEST_ASSERT_FALSE(originAllowed("http://attacker.example", id));
  TEST_ASSERT_FALSE(originAllowed("https://192.168.0.41", id));
  TEST_ASSERT_FALSE(originAllowed("null", id));
  TEST_ASSERT_FALSE(originAllowed("http://", id));
  TEST_ASSERT_FALSE(originAllowed("http://192.168.0.41/x", id));
}

static void hex(const uint8_t* b, size_t n, char* out) {
  for (size_t i = 0; i < n; i++) sprintf(out + i * 2, "%02x", b[i]);
}

static void test_sha256_and_hmac() {
  uint8_t d[32];
  char h[65];
  Sha256 s;
  s.update("abc", 3);
  s.finish(d);
  hex(d, 32, h);
  TEST_ASSERT_EQUAL_STRING("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", h);
  s.reset();
  const char* two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";  // 2 blocks
  s.update(two, strlen(two));
  s.finish(d);
  hex(d, 32, h);
  TEST_ASSERT_EQUAL_STRING("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", h);
  // RFC 4231 test case 2, and case 6 (a key longer than a block).
  hmacSha256((const uint8_t*)"Jefe", 4, "what do ya want ", 16, "for nothing?", 12, d);
  hex(d, 32, h);
  TEST_ASSERT_EQUAL_STRING("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843", h);
  uint8_t key[131];
  memset(key, 0xaa, sizeof(key));
  const char* msg = "Test Using Larger Than Block-Size Key - Hash Key First";
  hmacSha256(key, sizeof(key), msg, strlen(msg), "", 0, d);
  hex(d, 32, h);
  TEST_ASSERT_EQUAL_STRING("60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54", h);
}

// M3 + F1: the plugin's mirror (plugin/lib/relocation.js tokenTag + challengeMac) gives these same
// values: the contract is pinned on both sides. The mac covers the gadget's own station IP, so a
// spoofer relaying the plugin's nonce gets back a mac over the real gadget's IP, not its own.
static void test_address_challenge() {
  TokenStore ts;
  ts.add("ffeeddccbbaa99887766554433221100", "other");
  ts.add("00112233445566778899aabbccddeeff", "mac");
  char tag[9];
  tokenTag("00112233445566778899aabbccddeeff", tag);
  TEST_ASSERT_EQUAL_STRING("de18ad43", tag);
  char mac[65];
  const char* nonce = "0123456789abcdef0123456789abcdef";
  const char* ip = "192.168.15.181";
  TEST_ASSERT_EQUAL(ChallengeResult::Ok, answerChallenge(ts, nonce, "de18ad43", "miblo-4f2a", ip, mac));
  TEST_ASSERT_EQUAL_STRING("7a444960fa4cc8ad333a0aad73f69b699fdc65ec984a2cfa56e1207dd2d6bf4c", mac);
  // Another address gives another mac: the answer is bound to the IP.
  TEST_ASSERT_EQUAL(ChallengeResult::Ok, answerChallenge(ts, nonce, "de18ad43", "miblo-4f2a", "192.168.15.182", mac));
  TEST_ASSERT_FALSE(strcmp("7a444960fa4cc8ad333a0aad73f69b699fdc65ec984a2cfa56e1207dd2d6bf4c", mac) == 0);
  TEST_ASSERT_EQUAL(ChallengeResult::UnknownTag, answerChallenge(ts, nonce, "00000000", "miblo-4f2a", ip, mac));
  TEST_ASSERT_EQUAL_STRING("", mac);  // nothing computed with the dummy key ever leaves
  TEST_ASSERT_EQUAL(ChallengeResult::BadRequest, answerChallenge(ts, "0123", "de18ad43", "miblo-4f2a", ip, mac));
  TEST_ASSERT_EQUAL(ChallengeResult::BadRequest,
                    answerChallenge(ts, "0123456789ABCDEF0123456789abcdef", "de18ad43", "miblo-4f2a", ip, mac));
  TEST_ASSERT_EQUAL(ChallengeResult::BadRequest, answerChallenge(ts, nonce, "de18ad4", "miblo-4f2a", ip, mac));
  TEST_ASSERT_EQUAL(ChallengeResult::BadRequest, answerChallenge(ts, nonce, "de18ad43x", "miblo-4f2a", ip, mac));
  TEST_ASSERT_EQUAL(ChallengeResult::BadRequest, answerChallenge(ts, nullptr, "de18ad43", "miblo-4f2a", ip, mac));
  // Setup mode (no station IP): nothing to bind the answer to.
  TEST_ASSERT_EQUAL(ChallengeResult::NoNetwork, answerChallenge(ts, nonce, "de18ad43", "miblo-4f2a", nullptr, mac));
  TEST_ASSERT_EQUAL(ChallengeResult::NoNetwork, answerChallenge(ts, nonce, "de18ad43", "miblo-4f2a", "", mac));
  TEST_ASSERT_EQUAL(ChallengeResult::NoNetwork, answerChallenge(ts, nonce, "de18ad43", "miblo-4f2a", "0.0.0.0", mac));
  TEST_ASSERT_EQUAL_STRING("", mac);
  TokenStore none;
  TEST_ASSERT_EQUAL(ChallengeResult::UnknownTag, answerChallenge(none, nonce, "de18ad43", "miblo-4f2a", ip, mac));
}

// A paired computer's token proves it is no rebinding page (a page cannot know it): the plugin
// keeps working through a custom DNS alias (Pi-hole, router alias, MagicDNS). Everyone else, the
// web session and the pages included, needs the IP or the gadget's own name.
static void test_host_verdict() {
  const char* id = "miblo-4f2a";
  TEST_ASSERT_EQUAL(HostVerdict::Ok, judgeHost("192.168.0.41", true, nullptr, false, false, id));
  TEST_ASSERT_EQUAL(HostVerdict::Ok, judgeHost(nullptr, false, nullptr, false, false, id));
  TEST_ASSERT_EQUAL(HostVerdict::WrongHost, judgeHost("miblo.home.arpa", true, nullptr, false, false, id));
  TEST_ASSERT_EQUAL(HostVerdict::Ok, judgeHost("miblo.home.arpa", true, nullptr, false, true, id));
  TEST_ASSERT_EQUAL(HostVerdict::Ok, judgeHost("desk.tail1234.ts.net", true, "http://x.example", true, true, id));
  // Present but too long to read: never taken as absent.
  TEST_ASSERT_EQUAL(HostVerdict::WrongHost, judgeHost(nullptr, true, nullptr, false, false, id));
  TEST_ASSERT_EQUAL(HostVerdict::WrongOrigin,
                    judgeHost("192.168.0.41", true, "http://attacker.example", true, false, id));
  TEST_ASSERT_EQUAL(HostVerdict::WrongOrigin, judgeHost("192.168.0.41", true, nullptr, true, false, id));
  TEST_ASSERT_EQUAL(HostVerdict::Ok, judgeHost("192.168.0.41", true, "http://192.168.0.41", true, false, id));
}

// A browser refused for its Host gets a small page linking to the gadget's IP; API callers JSON.
static void test_wrong_host_reply() {
  TEST_ASSERT_TRUE(acceptsHtml("text/html,application/xhtml+xml,*/*;q=0.8"));
  TEST_ASSERT_TRUE(acceptsHtml("TEXT/HTML"));
  TEST_ASSERT_FALSE(acceptsHtml("application/json"));
  TEST_ASSERT_FALSE(acceptsHtml("*/*"));
  TEST_ASSERT_FALSE(acceptsHtml(nullptr));
  char out[400];
  size_t n = wrongHostReply(out, sizeof(out), "421 Misdirected Request", "192.168.0.41", "miblo-4f2a", true);
  TEST_ASSERT_EQUAL(strlen(out), n);
  // F2: the page says which names work: the IP (a link) or <id>.local.
  TEST_ASSERT_NOT_NULL(strstr(out, "http://miblo-4f2a.local/"));
  TEST_ASSERT_NOT_NULL(strstr(out, "HTTP/1.1 421 Misdirected Request\r\n"));
  TEST_ASSERT_NOT_NULL(strstr(out, "Content-Type: text/html; charset=utf-8\r\n"));
  TEST_ASSERT_NOT_NULL(strstr(out, "<a href=\"http://192.168.0.41/\">http://192.168.0.41/</a>"));
  const char* body = strstr(out, "\r\n\r\n") + 4;
  char cl[40];
  snprintf(cl, sizeof(cl), "Content-Length: %u\r\n", (unsigned)strlen(body));
  TEST_ASSERT_NOT_NULL(strstr(out, cl));
  n = wrongHostReply(out, sizeof(out), "403 Forbidden", "192.168.0.41", "miblo-4f2a", false);
  TEST_ASSERT_NOT_NULL(strstr(out, "Content-Type: application/json\r\n"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\r\n\r\n{\"error\":\"wrong host\"}"));
  // No IP to offer (not connected): the page still says where to go, without a link.
  wrongHostReply(out, sizeof(out), "421 Misdirected Request", "", "miblo-4f2a", true);
  TEST_ASSERT_NULL(strstr(out, "<a "));
  TEST_ASSERT_NOT_NULL(strstr(out, "http://miblo-4f2a.local/"));
  // An id that is not a plain host label never reaches the page.
  wrongHostReply(out, sizeof(out), "421 Misdirected Request", "192.168.0.41", "<b>", true);
  TEST_ASSERT_NULL(strstr(out, "<b>"));
  TEST_ASSERT_NOT_NULL(strstr(out, "http://192.168.0.41/"));
  // The longest IPv4 and id still fit the buffer refuseForeign uses.
  TEST_ASSERT_TRUE(wrongHostReply(out, 448, "421 Misdirected Request", "255.255.255.255", "miblo-ffff", true) > 0);
  TEST_ASSERT_EQUAL(0, wrongHostReply(out, 20, "421 Misdirected Request", "192.168.0.41", "miblo-4f2a", true));  // never cut
}

static void test_find_content_length() {
  uint32_t n = 0;
  const char h1[] = "Host: x\r\ncontent-LENGTH:  5000\r\nX: y\r\n\r\n";
  TEST_ASSERT_EQUAL(LengthVerdict::Ok, readContentLength(h1, sizeof(h1) - 1, n));
  TEST_ASSERT_EQUAL_UINT32(5000, n);
  const char h3[] = "Host: x\r\n\r\nContent-Length: 9";  // after the blank line: body, not a header
  TEST_ASSERT_EQUAL(LengthVerdict::None, readContentLength(h3, sizeof(h3) - 1, n));
  TEST_ASSERT_EQUAL(LengthVerdict::None, readContentLength("\r\n", 2, n));  // no headers at all
  TEST_ASSERT_EQUAL(LengthVerdict::Bad, readContentLength("Content-Length: x\r\n\r\n", 21, n));
  TEST_ASSERT_EQUAL(LengthVerdict::None, readContentLength("X-Content-Length: 5\r\n\r\n", 23, n));
  // Duplicates: the same value is fine; different values cannot be judged (the server keeps the
  // last one) and are refused.
  const char h5[] = "Content-Length: 10\r\nHost: x\r\nContent-Length: 010\r\n\r\n";
  TEST_ASSERT_EQUAL(LengthVerdict::Ok, readContentLength(h5, sizeof(h5) - 1, n));
  TEST_ASSERT_EQUAL_UINT32(10, n);
  const char h6[] = "Content-Length: 10\r\nHost: x\r\nContent-Length: 90000\r\n\r\n";
  TEST_ASSERT_EQUAL(LengthVerdict::Bad, readContentLength(h6, sizeof(h6) - 1, n));
  // A bare '\n' then '\r' line is not the end for the server (it reads lines up to '\r').
  const char h7[] = "X: y\n\r\nContent-Length: -1\r\n\r\n";
  TEST_ASSERT_EQUAL(LengthVerdict::Bad, readContentLength(h7, sizeof(h7) - 1, n));
}

// What ESP8266WebServer does with a Content-Length value: String::trim() (isspace), then toInt()
// (atol), stored in a uint32_t.
static long long serverAtol(const std::string& v) {
  size_t a = 0, b = v.size();
  while (a < b && isspace((unsigned char)v[a])) a++;
  while (b > a && isspace((unsigned char)v[b - 1])) b--;
  return atoll(v.substr(a, b - a).c_str());
}
static uint32_t serverLength(const std::string& v) { return (uint32_t)serverAtol(v); }

static bool plainDigits(const std::string& v) {
  size_t a = 0, b = v.size();
  while (a < b && isspace((unsigned char)v[a])) a++;
  while (b > a && isspace((unsigned char)v[b - 1])) b--;
  if (a == b) return false;
  for (size_t i = a; i < b; i++)
    if (v[i] < '0' || v[i] > '9') return false;
  return true;
}

// Differential: whenever the scanner accepts a value, the server reads the very same length; any
// value that is not plain digits (after the server's trim) is refused, so no request can carry a
// length the guards did not see (the H1 bypass was "-1", "+60000", "\v60000").
static void checkAgainstServer(const std::string& v) {
  const std::string h = "Host: x\r\nContent-Length:" + v + "\r\n\r\n";
  uint32_t n = 12345;
  const LengthVerdict got = readContentLength(h.data(), h.size(), n);
  char what[96];
  snprintf(what, sizeof(what), "value [%s]", v.c_str());
  TEST_ASSERT_NOT_EQUAL_MESSAGE((int)LengthVerdict::None, (int)got, what);
  if (got == LengthVerdict::Ok) {
    TEST_ASSERT_TRUE_MESSAGE(plainDigits(v), what);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(serverLength(v), n, what);
  } else {
    // Refused: either not plain digits, or too large to be worth judging (over 2^31 - 1).
    TEST_ASSERT_TRUE_MESSAGE(!plainDigits(v) || serverAtol(v) > 0x7FFFFFFF || v.size() > 10, what);
  }
  // The read-ahead's scanner (starting at the request line) agrees.
  const std::string req = "POST /x HTTP/1.1\r\n" + h;
  const size_t body = requestBodyLength(req.data(), req.size());
  if (got == LengthVerdict::Ok) TEST_ASSERT_EQUAL_UINT32_MESSAGE(n, (uint32_t)body, what);
  TEST_ASSERT_EQUAL_MESSAGE((int)got, (int)requestLengthVerdict(req.data(), req.size()), what);
}

static void test_content_length_matches_the_server() {
  static const char* const kValues[] = {
      " 0",       " 5000",     "-1",         "+60000",       "\v60000",       " 0060000 ",   "1e3",
      "",         " ",         "\t",         " 12 34",       "0x10",          "60000abc",    "\f60000",
      " 60000\v", "2147483647", "2147483648", "4294967295",   "4294967296",    "99999999999", "18446744073709551617",
      " -0",      "+0",        " \t 7 \t ",  "00000000000000000001", "١٢", "5\x01",
  };
  for (const char* v : kValues) checkAgainstServer(v);
  // Random values over a hostile alphabet.
  static const char kAlpha[] = "0123456789+- \t\v\fxe.";
  uint32_t seed = 12345;
  for (int k = 0; k < 20000; k++) {
    std::string v;
    seed = seed * 1103515245u + 12345u;
    const int len = (int)((seed >> 16) % 14);
    for (int i = 0; i < len; i++) {
      seed = seed * 1103515245u + 12345u;
      v += kAlpha[(seed >> 16) % (sizeof(kAlpha) - 1)];
    }
    checkAgainstServer(v);
  }
}

// checkRequestHeaders: raw header bytes as ESP8266WebServer will read them (lines end at '\r',
// the rest up to '\n' is skipped; an empty line or a line without ':' ends the headers).
static HeaderVerdict verdict(const char* h, char* b = nullptr, size_t cap = 0) {
  return checkRequestHeaders(h, strlen(h), b, cap);
}

static void test_headers_plain_and_complete() {
  TEST_ASSERT_EQUAL(HeaderVerdict::Plain, verdict("Host: x\r\nContent-Type: application/json\r\n\r\n{}"));
  TEST_ASSERT_EQUAL(HeaderVerdict::Plain, verdict("\r\n"));  // no headers at all
  // A line without ':' ends the headers for the server too: what follows is body.
  TEST_ASSERT_EQUAL(HeaderVerdict::Plain, verdict("Host: x\r\nnot a header\r\nContent-Type: multipart/x; boundary=a\r\n"));
  // After the blank line it is body, not a header.
  TEST_ASSERT_EQUAL(HeaderVerdict::Plain, verdict("Host: x\r\n\r\nContent-Type: multipart/form-data; boundary=a\r\n"));
  TEST_ASSERT_EQUAL(HeaderVerdict::Plain, verdict("X-Content-Type: multipart/x; boundary=a\r\n\r\n"));
  TEST_ASSERT_EQUAL(HeaderVerdict::Plain, verdict("Content-Type : multipart/x; boundary=a\r\n\r\n"));  // not the name
}

static void test_headers_incomplete() {
  // The header block must end within the bytes: unseen headers could carry a Content-Type.
  TEST_ASSERT_EQUAL(HeaderVerdict::Incomplete, verdict(""));
  TEST_ASSERT_EQUAL(HeaderVerdict::Incomplete, checkRequestHeaders(nullptr, 0));
  TEST_ASSERT_EQUAL(HeaderVerdict::Incomplete, verdict("Host: x\r\nX-Pad: aaaa"));
  TEST_ASSERT_EQUAL(HeaderVerdict::Incomplete, verdict("Host: x\r\n"));
  TEST_ASSERT_EQUAL(HeaderVerdict::Incomplete, verdict("Host: x\r"));  // where the next line starts is unknown
  TEST_ASSERT_EQUAL(HeaderVerdict::Incomplete, verdict("Content-Type: multipart/form-data; bound"));
  // A valid Content-Type seen first proves nothing while the block goes on (a later one wins).
  char b[8] = "junk";
  TEST_ASSERT_EQUAL(HeaderVerdict::Incomplete,
                    verdict("Content-Type: multipart/form-data; boundary=abc\r\nContent-Length: 9\r\nX-Pad: a", b, sizeof(b)));
  TEST_ASSERT_EQUAL_STRING("", b);  // a boundary only comes with a Multipart verdict
}

static void test_headers_multipart_boundary() {
  char b[80];
  TEST_ASSERT_EQUAL(HeaderVerdict::Multipart,
                    verdict("Host: x\r\ncontent-TYPE:  Multipart/form-data; boundary=----miblo0123\r\n\r\n", b, sizeof(b)));
  TEST_ASSERT_EQUAL_STRING("----miblo0123", b);
  TEST_ASSERT_EQUAL(HeaderVerdict::Multipart,
                    verdict("Content-Type: multipart/form-data; BOUNDARY=\"a b\"  \r\n\r\n", b, sizeof(b)));
  TEST_ASSERT_EQUAL_STRING("a b", b);  // quotes dropped, trailing blanks trimmed, as the server does
  TEST_ASSERT_EQUAL(HeaderVerdict::Multipart,
                    verdict("Content-Type:multipart/form-data;boundary=x\r\n\r\n", b, sizeof(b)));
  TEST_ASSERT_EQUAL_STRING("x", b);
  // Browser and Node boundaries.
  TEST_ASSERT_EQUAL(HeaderVerdict::Multipart,
                    verdict("Content-Type: multipart/form-data; boundary=----WebKitFormBoundary7MA4YWxkTrZu0gW\r\n\r\n"));
  TEST_ASSERT_EQUAL(HeaderVerdict::Multipart,
                    verdict("Content-Type: multipart/form-data; boundary=----geckoformboundaryc3a5f6b8e0d1a2b3c4d5e6f7a8b9c0d1\r\n\r\n"));
  // Lines split at '\r' only, like the server: a bare '\n' does not start a new header.
  TEST_ASSERT_EQUAL(HeaderVerdict::Plain, verdict("X: a\nContent-Type: multipart/x; boundary=a\r\n\r\n"));
  // ... but a line starting after "\r\n\n" is still a header line for the server (not the end).
  TEST_ASSERT_EQUAL(HeaderVerdict::Multipart, verdict("Host: x\r\n\nX: y\r\nContent-Type: multipart/x; boundary=a\r\n\r\n"));
}

static void test_headers_boundary_length() {
  char h[200];
  char b[80];
  const char* pre = "Content-Type: multipart/form-data; boundary=";
  // 70 characters: the RFC 2046 limit is accepted, 71 is refused.
  snprintf(h, sizeof(h), "%s%070d\r\n\r\n", pre, 7);
  TEST_ASSERT_EQUAL(HeaderVerdict::Multipart, verdict(h, b, sizeof(b)));
  TEST_ASSERT_EQUAL_size_t(70, strlen(b));
  snprintf(h, sizeof(h), "%s%071d\r\n\r\n", pre, 7);
  TEST_ASSERT_EQUAL(HeaderVerdict::BadMultipart, verdict(h));
  // Quotes do not count (the server removes them), so a quoted 70 is fine.
  snprintf(h, sizeof(h), "%s\"%070d\"\r\n\r\n", pre, 7);
  TEST_ASSERT_EQUAL(HeaderVerdict::Multipart, verdict(h));
  // A small output buffer only truncates the copy, never the verdict.
  char tiny[4];
  snprintf(h, sizeof(h), "%s%070d\r\n\r\n", pre, 7);
  TEST_ASSERT_EQUAL(HeaderVerdict::Multipart, verdict(h, tiny, sizeof(tiny)));
  TEST_ASSERT_EQUAL_STRING("000", tiny);
}

static void test_headers_huge_boundary() {
  // The attack: a multi-KB boundary (the server puts it in a stack array).
  static char h[6000];
  const char* pre = "Content-Type: multipart/form-data; boundary=";
  size_t n = strlen(pre);
  memcpy(h, pre, n);
  memset(h + n, 'A', 5000);
  memcpy(h + n + 5000, "\r\n\r\n", 4);
  TEST_ASSERT_EQUAL(HeaderVerdict::BadMultipart, checkRequestHeaders(h, n + 5004));
  TEST_ASSERT_EQUAL(HeaderVerdict::Incomplete, checkRequestHeaders(h, n + 3000));  // only the first part arrived
}

static void test_headers_bad_multipart() {
  // Missing or empty boundary.
  TEST_ASSERT_EQUAL(HeaderVerdict::BadMultipart, verdict("Content-Type: multipart/form-data\r\n\r\n"));
  TEST_ASSERT_EQUAL(HeaderVerdict::BadMultipart, verdict("Content-Type: multipart/form-data; boundary=\r\n\r\n"));
  TEST_ASSERT_EQUAL(HeaderVerdict::BadMultipart, verdict("Content-Type: multipart/form-data; boundary=\"\"\r\n\r\n"));
  // The server takes everything after the FIRST '=' as the boundary: another parameter first is refused.
  TEST_ASSERT_EQUAL(HeaderVerdict::BadMultipart,
                    verdict("Content-Type: multipart/form-data; charset=utf-8; boundary=abc\r\n\r\n"));
  TEST_ASSERT_EQUAL(HeaderVerdict::BadMultipart, verdict("Content-Type: multipart/form-data; xboundary=abc\r\n\r\n"));
  // Two Content-Type headers where one is multipart: the server keeps the multipart one's boundary.
  TEST_ASSERT_EQUAL(HeaderVerdict::BadMultipart,
                    verdict("Content-Type: multipart/form-data; boundary=a\r\nContent-Type: application/json\r\n\r\n"));
  TEST_ASSERT_EQUAL(HeaderVerdict::BadMultipart,
                    verdict("Content-Type: text/plain\r\nContent-Type: multipart/form-data; boundary=a\r\n\r\n"));
  // Leading blanks before the type are trimmed by the server too.
  TEST_ASSERT_EQUAL(HeaderVerdict::BadMultipart, verdict("Content-Type: \t multipart/mixed\r\n\r\n"));
}

static void test_headers_garbage() {
  // A NUL byte: the server's String functions would read the line differently. Refused.
  const char h[] = "Content-Type: multi\0part/x; boundary=a\r\n\r\n";
  TEST_ASSERT_EQUAL(HeaderVerdict::Malformed, checkRequestHeaders(h, sizeof(h) - 1));
  const char h2[] = "Host: x\r\0\nContent-Type: a\r\n\r\n";
  TEST_ASSERT_EQUAL(HeaderVerdict::Malformed, checkRequestHeaders(h2, sizeof(h2) - 1));
  // Binary noise without a line end: never "complete".
  uint8_t noise[256];
  for (int i = 0; i < 256; i++) noise[i] = (uint8_t)(i * 37 + 11) | 1;  // no NUL
  for (int i = 0; i < 256; i++) if (noise[i] == '\r') noise[i] = 'x';
  TEST_ASSERT_EQUAL(HeaderVerdict::Incomplete, checkRequestHeaders((const char*)noise, sizeof(noise)));
}

// Re-opening (a new code) must not grant fresh guesses: 4 bad, re-open, 1 bad → locked.
static void test_presence_failures_survive_reopen() {
  PresenceGate g;
  // (Reset is asked for by an authorised caller: trusted, no anonymous gap between codes.)
  TEST_ASSERT_TRUE(g.open(PresenceGate::Purpose::Reset, "1111", 0, true));
  for (int i = 0; i < 4; i++) TEST_ASSERT_FALSE(g.check(PresenceGate::Purpose::Reset, "0000", 10));
  TEST_ASSERT_FALSE(g.locked(PresenceGate::Purpose::Reset, 10));
  g.close();  // (closed, e.g. expired): a new code, same failures
  TEST_ASSERT_TRUE(g.open(PresenceGate::Purpose::Reset, "2222", 20, true));
  TEST_ASSERT_FALSE(g.check(PresenceGate::Purpose::Reset, "0000", 30));
  TEST_ASSERT_TRUE(g.locked(PresenceGate::Purpose::Reset, 30));
  TEST_ASSERT_EQUAL_UINT32(60000, g.lockRemainingMs(PresenceGate::Purpose::Reset, 30));
  TEST_ASSERT_FALSE(g.open(PresenceGate::Purpose::Reset, "3333", 40));  // 429 path
  // L2: each purpose has its own lockout: the owner can still update, unlock settings, move Wi-Fi.
  TEST_ASSERT_FALSE(g.locked(PresenceGate::Purpose::Update, 40));
  TEST_ASSERT_FALSE(g.locked(PresenceGate::Purpose::Settings, 40));
  TEST_ASSERT_TRUE(g.open(PresenceGate::Purpose::Update, "4444", 40));
  TEST_ASSERT_TRUE(g.check(PresenceGate::Purpose::Update, "4444", 50));
  // A correct code clears the accumulated failures.
  PresenceGate h;
  h.open(PresenceGate::Purpose::Update, "1234", 0);
  for (int i = 0; i < 4; i++) h.check(PresenceGate::Purpose::Update, "0000", 0);
  TEST_ASSERT_TRUE(h.check(PresenceGate::Purpose::Update, "1234", 0));
  h.open(PresenceGate::Purpose::Update, "1234", 0);
  for (int i = 0; i < 4; i++) h.check(PresenceGate::Purpose::Update, "0000", 0);
  TEST_ASSERT_FALSE(h.locked(PresenceGate::Purpose::Update, 0));
}

// The lockout is honoured across a clock wrap, and an expired one never comes back ~49.7 days
// later when the unsigned elapsed time wraps around.
static void test_presence_lockout_clock_wrap() {
  PresenceGate g;
  const uint32_t t0 = 0xFFFFF000u;  // 4096 ms before the wrap
  g.open(PresenceGate::Purpose::Update, "1234", t0);
  for (int i = 0; i < 5; i++) g.check(PresenceGate::Purpose::Update, "0000", t0);
  TEST_ASSERT_TRUE(g.locked(PresenceGate::Purpose::Update, 0x00000100u));  // wrapped, 4352 ms later: still locked
  TEST_ASSERT_EQUAL_UINT32(60000 - 4352, g.lockRemainingMs(PresenceGate::Purpose::Update, 0x00000100u));
  const uint32_t expired = t0 + 60000;
  g.update(expired);  // the app calls this every frame
  TEST_ASSERT_FALSE(g.locked(PresenceGate::Purpose::Update, expired));
  const uint32_t phantom = t0 + 10;  // same low bits one full wrap (2^32 ms) later
  TEST_ASSERT_FALSE(g.locked(PresenceGate::Purpose::Update, phantom));
  TEST_ASSERT_EQUAL_UINT32(0, g.waitMs(PresenceGate::Purpose::Update, phantom));  // nor the anonymous gap
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
    TEST_ASSERT_TRUE(g.locked(PresenceGate::Purpose::Update, t));
    TEST_ASSERT_EQUAL_UINT32(lock, g.lockRemainingMs(PresenceGate::Purpose::Update, t));
    TEST_ASSERT_FALSE(g.open(PresenceGate::Purpose::Update, "5555", t + 1));  // re-open refused
    TEST_ASSERT_FALSE(g.active(t + 1));
    t += lock - 1;
    TEST_ASSERT_TRUE(g.locked(PresenceGate::Purpose::Update, t));
    t += 1;
    TEST_ASSERT_FALSE(g.locked(PresenceGate::Purpose::Update, t));
  }
  // A correct code resets the escalation back to 60 s.
  TEST_ASSERT_TRUE(g.open(PresenceGate::Purpose::Update, "1234", t));
  TEST_ASSERT_TRUE(g.check(PresenceGate::Purpose::Update, "1234", t));
  TEST_ASSERT_TRUE(g.open(PresenceGate::Purpose::Update, "1234", t));
  for (int i = 0; i < 5; i++) g.check(PresenceGate::Purpose::Update, "0000", t);
  TEST_ASSERT_EQUAL_UINT32(60000, g.lockRemainingMs(PresenceGate::Purpose::Update, t));
}

static void test_presence_lockout_caps_at_one_day() {
  PresenceGate g;
  uint32_t t = 0;
  for (int round = 0; round < 12; round++) {
    TEST_ASSERT_TRUE(g.open(PresenceGate::Purpose::Update, "1234", t));
    for (int i = 0; i < 5; i++) g.check(PresenceGate::Purpose::Update, "0000", t);
    t += g.lockRemainingMs(PresenceGate::Purpose::Update, t);
  }
  TEST_ASSERT_TRUE(g.open(PresenceGate::Purpose::Update, "1234", t));
  for (int i = 0; i < 5; i++) g.check(PresenceGate::Purpose::Update, "0000", t);
  TEST_ASSERT_EQUAL_UINT32(PresenceGate::kLockMaxMs, g.lockRemainingMs(PresenceGate::Purpose::Update, t));
  TEST_ASSERT_EQUAL_UINT32(86400000, PresenceGate::kLockMaxMs);
}

// M2: the counters survive a restart (RTC memory): a reboot gives no fresh guesses.
static void test_lockout_survives_restore() {
  PairingGuard g;
  g.setCode("4827");
  failPairing(g, 0);
  for (int i = 0; i < 2; i++) g.check("0000", 70000);  // 2 of the next 3
  const LockoutState s = g.lockout().save(70000);
  TEST_ASSERT_EQUAL_UINT32(60000, s.lockMs);
  TEST_ASSERT_EQUAL(2, s.failures);
  PairingGuard after;  // the new boot
  after.setCode("4827");
  after.lockout().restore(s, 5);
  TEST_ASSERT_EQUAL(PairingGuard::Result::BadCode, after.check("0000", 10));  // the 3rd: locks
  TEST_ASSERT_EQUAL_UINT32(120000, after.lockRemainingMs(10));
  // A lockout in force restarts with its remaining time.
  const LockoutState locked = after.lockout().save(20010);
  TEST_ASSERT_EQUAL_UINT32(100000, locked.remainingMs);
  PairingGuard again;
  again.setCode("4827");
  again.lockout().restore(locked, 1000);
  TEST_ASSERT_EQUAL(PairingGuard::Result::Locked, again.check("4827", 100999));
  TEST_ASSERT_EQUAL(PairingGuard::Result::Ok, again.check("4827", 101000));
  // Garbage (a cold boot's RTC memory) is ignored.
  PairingGuard cold;
  LockoutState junk{};
  junk.lockMs = 0xFFFFFFFFu;
  junk.remainingMs = 5;
  cold.lockout().restore(junk, 0);
  TEST_ASSERT_EQUAL_UINT32(0, cold.lockRemainingMs(1));
  junk.lockMs = 60000;
  junk.remainingMs = 70000;
  cold.lockout().restore(junk, 0);
  TEST_ASSERT_EQUAL_UINT32(0, cold.lockRemainingMs(1));
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

static void test_web_session() {
  WebSession w;
  uint8_t rnd[16];
  for (int i = 0; i < 16; i++) rnd[i] = (uint8_t)(i * 7 + 1);
  char token[33];
  makeToken(rnd, token);
  TEST_ASSERT_FALSE(w.valid(token, 1000));  // nothing issued yet
  w.issue(rnd, 1000);
  TEST_ASSERT_TRUE(w.valid(token, 1000));
  TEST_ASSERT_TRUE(w.valid(token, 1000 + WebSession::kTtlMs - 1));
  TEST_ASSERT_FALSE(w.valid(token, 1000 + WebSession::kTtlMs));  // expired
  TEST_ASSERT_FALSE(w.valid("deadbeef", 1000));                  // wrong token
  TEST_ASSERT_FALSE(w.valid(nullptr, 1000));
  w.issue(rnd, 2000);  // a new session shifts the window
  TEST_ASSERT_TRUE(w.valid(token, 2000 + WebSession::kTtlMs - 1));
  w.clear();
  TEST_ASSERT_FALSE(w.valid(token, 2000));
}

static void test_rate_limiter_token_bucket() {
  RateLimiter rl(10, 5);  // burst 10, 5/s
  uint32_t t = 100000;
  // The burst: 10 allowed back to back, the 11th refused.
  for (int i = 0; i < 10; i++) TEST_ASSERT_TRUE(rl.allow(t));
  TEST_ASSERT_FALSE(rl.allow(t));
  TEST_ASSERT_FALSE(rl.allow(t + 999));  // less than a second later: still empty
  // One second on: 5 more tokens.
  t += 1000;
  for (int i = 0; i < 5; i++) TEST_ASSERT_TRUE(rl.allow(t));
  TEST_ASSERT_FALSE(rl.allow(t));
  // Idle a long time: refill is capped at the burst, never more.
  t += 100000;
  TEST_ASSERT_EQUAL_UINT8(10, rl.tokens(t));
  for (int i = 0; i < 10; i++) TEST_ASSERT_TRUE(rl.allow(t));
  TEST_ASSERT_FALSE(rl.allow(t));
  // Sub-second remainder is kept: 1500 ms after empty gives 5 (not 7) then 5 more at 2000.
  t += 1500;
  TEST_ASSERT_EQUAL_UINT8(5, rl.tokens(t));
  // Clock wrap: still refills, never floods.
  RateLimiter w(4, 2);
  uint32_t big = 0xFFFFF000u;
  for (int i = 0; i < 4; i++) TEST_ASSERT_TRUE(w.allow(big));
  TEST_ASSERT_FALSE(w.allow(big));
  TEST_ASSERT_TRUE(w.allow(big + 1000));   // 0x...FC00 -> wraps past 0
  TEST_ASSERT_TRUE(w.allow(big + 1000));
  TEST_ASSERT_FALSE(w.allow(big + 1000));
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

// GET /api/info: everything while the gadget is not paired yet (setup needs it) or for a paired
// computer's bearer token; anyone else on the LAN of a paired gadget gets only id/paired/proto.
static void test_info_view() {
  TokenStore none;
  TEST_ASSERT_TRUE(infoView(none, nullptr) == InfoView::Full);
  TEST_ASSERT_TRUE(infoView(none, "") == InfoView::Full);
  TEST_ASSERT_TRUE(infoView(none, "Bearer whatever") == InfoView::Full);
  TokenStore paired;
  paired.add("00112233445566778899aabbccddeeff", "mac");
  TEST_ASSERT_TRUE(infoView(paired, nullptr) == InfoView::Public);
  TEST_ASSERT_TRUE(infoView(paired, "") == InfoView::Public);
  TEST_ASSERT_TRUE(infoView(paired, "Bearer nope") == InfoView::Public);
  TEST_ASSERT_TRUE(infoView(paired, "00112233445566778899aabbccddeeff") == InfoView::Public);  // no scheme
  TEST_ASSERT_TRUE(infoView(paired, "Bearer 00112233445566778899aabbccddeeff") == InfoView::Full);
}

static void test_public_info_has_only_three_fields() {
  StaticJsonDocument<256> doc;
  writePublicInfo(doc.to<JsonObject>(), "miblo-4f2a", true, 1);
  TEST_ASSERT_EQUAL(3, (int)doc.as<JsonObject>().size());
  TEST_ASSERT_EQUAL_STRING("miblo-4f2a", doc["id"].as<const char*>());
  TEST_ASSERT_TRUE(doc["paired"].as<bool>());
  TEST_ASSERT_EQUAL(1, doc["proto"].as<int>());
}

// Nobody on the LAN can replace a code the owner is reading off the screen: while a code is
// active, a request for another purpose is refused (busy) and one for the same purpose keeps the
// code on screen. Once it expires (or is used and closed) a new one can be asked for.
static void test_presence_code_never_replaced_while_active() {
  PresenceGate g;
  TEST_ASSERT_TRUE(g.open(PresenceGate::Purpose::Settings, "1111", 0));
  TEST_ASSERT_TRUE(g.busyFor(PresenceGate::Purpose::Update, 1000));
  TEST_ASSERT_FALSE(g.busyFor(PresenceGate::Purpose::Settings, 1000));
  TEST_ASSERT_FALSE(g.open(PresenceGate::Purpose::Update, "2222", 1000));  // busy
  TEST_ASSERT_FALSE(g.open(PresenceGate::Purpose::Reset, "3333", 1000));
  TEST_ASSERT_FALSE(g.open(PresenceGate::Purpose::Wifi, "4444", 1000));
  TEST_ASSERT_FALSE(g.locked(PresenceGate::Purpose::Update, 1000));  // busy is not a lockout
  TEST_ASSERT_TRUE(g.purpose() == PresenceGate::Purpose::Settings);
  TEST_ASSERT_TRUE(g.open(PresenceGate::Purpose::Settings, "5555", 2000));  // same purpose: kept
  TEST_ASSERT_EQUAL_STRING("1111", g.code());
  TEST_ASSERT_EQUAL_UINT32(PresenceGate::kTtlMs - 2000, g.remainingMs(2000));  // timer not reset
  TEST_ASSERT_TRUE(g.check(PresenceGate::Purpose::Settings, "1111", 3000));
  // Expired: anyone can ask again, for any purpose.
  TEST_ASSERT_FALSE(g.busyFor(PresenceGate::Purpose::Update, PresenceGate::kTtlMs));
  TEST_ASSERT_TRUE(g.open(PresenceGate::Purpose::Update, "6666", PresenceGate::kTtlMs));
  TEST_ASSERT_EQUAL_STRING("6666", g.code());
  // Closed after use: likewise.
  g.close();
  TEST_ASSERT_TRUE(g.open(PresenceGate::Purpose::Reset, "7777", PresenceGate::kTtlMs + 1));
  TEST_ASSERT_EQUAL_STRING("7777", g.code());
}

// F4: an unauthenticated LAN host re-opening the Settings code must not keep the owner out of
// OTA, Reset and Wi-Fi. An authorised caller (bearer token, web session, the setup AP) replaces a
// code opened anonymously; a code opened by an authorised caller is never replaced.
static void test_presence_trusted_preempts_anonymous() {
  using P = PresenceGate::Purpose;
  PresenceGate g;
  TEST_ASSERT_TRUE(g.open(P::Settings, "1111", 0));            // anonymous (the prankster)
  TEST_ASSERT_FALSE(g.open(P::Reset, "2222", 1000));           // anonymous: still busy
  TEST_ASSERT_TRUE(g.open(P::Reset, "2222", 1000, true));      // the owner's plugin: replaces it
  TEST_ASSERT_TRUE(g.purpose() == P::Reset);
  TEST_ASSERT_EQUAL_STRING("2222", g.code());
  TEST_ASSERT_EQUAL_UINT32(PresenceGate::kTtlMs, g.remainingMs(1000));  // a fresh timer
  TEST_ASSERT_FALSE(g.check(P::Settings, "1111", 1100));       // the replaced code is gone
  TEST_ASSERT_FALSE(g.locked(P::Settings, 1100));
  TEST_ASSERT_FALSE(g.open(P::Settings, "3333", 1200));        // nor can it come back over it
  TEST_ASSERT_FALSE(g.open(P::Update, "4444", 1200, true));    // trusted never replaces trusted
  TEST_ASSERT_EQUAL_STRING("2222", g.code());
  TEST_ASSERT_TRUE(g.check(P::Reset, "2222", 1300));
  // A trusted request for the purpose already on the screen keeps the code and adopts it.
  PresenceGate h;
  TEST_ASSERT_TRUE(h.open(P::Update, "5555", 0));
  TEST_ASSERT_TRUE(h.open(P::Update, "6666", 10, true));
  TEST_ASSERT_EQUAL_STRING("5555", h.code());
  TEST_ASSERT_FALSE(h.open(P::Reset, "7777", 20, true));       // now held by an authorised caller
  // A locked purpose stays locked for a trusted caller too.
  for (int i = 0; i < 5; i++) h.check(P::Update, "0000", 30);
  TEST_ASSERT_FALSE(h.open(P::Update, "8888", 40, true));
}

// F4: an anonymous caller arms a new code for one purpose at most once per kAnonGapMs; a pending
// code is handed back as it is. The owner's flow is unchanged: a correct code lifts the wait.
static void test_presence_anonymous_rate_limit() {
  using P = PresenceGate::Purpose;
  PresenceGate g;
  TEST_ASSERT_TRUE(g.open(P::Settings, "1111", 0));
  TEST_ASSERT_TRUE(g.open(P::Settings, "2222", 5000));  // pending: reused, not re-armed
  TEST_ASSERT_EQUAL_STRING("1111", g.code());
  TEST_ASSERT_EQUAL_UINT32(PresenceGate::kTtlMs - 5000, g.remainingMs(5000));
  // Closed by a lockout (5 wrong guesses) or by its owner: no new anonymous code for 30 s.
  g.close();
  TEST_ASSERT_FALSE(g.open(P::Settings, "3333", 10000));
  TEST_ASSERT_FALSE(g.locked(P::Settings, 10000));
  TEST_ASSERT_EQUAL_UINT32(PresenceGate::kAnonGapMs - 10000, g.waitMs(P::Settings, 10000));
  TEST_ASSERT_TRUE(g.open(P::Update, "4444", 10000));  // per purpose
  g.close();
  TEST_ASSERT_TRUE(g.open(P::Settings, "3333", PresenceGate::kAnonGapMs));
  TEST_ASSERT_EQUAL_STRING("3333", g.code());
  // A trusted caller is not rate-limited.
  g.close();
  TEST_ASSERT_TRUE(g.open(P::Settings, "5555", PresenceGate::kAnonGapMs + 1, true));
  // A correct code proves presence: the owner may ask again at once.
  PresenceGate h;
  TEST_ASSERT_TRUE(h.open(P::Settings, "1111", 0));
  TEST_ASSERT_TRUE(h.check(P::Settings, "1111", 100));
  h.close();
  TEST_ASSERT_TRUE(h.open(P::Settings, "2222", 200));
  // waitMs while another purpose's anonymous code is up: until it may be replaced (kAnonPreemptMs).
  TEST_ASSERT_EQUAL_UINT32(PresenceGate::kAnonPreemptMs - 100, h.waitMs(P::Reset, 300));
  // ...and while a trusted one is up: what is left of it.
  PresenceGate t;
  TEST_ASSERT_TRUE(t.open(P::Settings, "1111", 0, true));
  TEST_ASSERT_EQUAL_UINT32(PresenceGate::kTtlMs - 300, t.waitMs(P::Reset, 300));
}

// F4 residual: a stranger re-opening the Update code every 5 min must not keep an unpaired browser
// user out of the Settings code. An anonymous request for another purpose replaces an anonymous code
// that has been on the screen kAnonPreemptMs; the 30 s per-purpose anonymous gap still applies, and
// an anonymous request never replaces a trusted code.
static void test_presence_anonymous_preempts_a_stale_anonymous_code() {
  using P = PresenceGate::Purpose;
  PresenceGate g;
  TEST_ASSERT_TRUE(g.open(P::Update, "1111", 0));  // the stranger
  TEST_ASSERT_FALSE(g.open(P::Settings, "2222", PresenceGate::kAnonPreemptMs - 1));  // too fresh
  TEST_ASSERT_EQUAL_UINT32(1, g.waitMs(P::Settings, PresenceGate::kAnonPreemptMs - 1));
  TEST_ASSERT_EQUAL_UINT32(0, g.waitMs(P::Settings, PresenceGate::kAnonPreemptMs));
  TEST_ASSERT_TRUE(g.open(P::Settings, "2222", PresenceGate::kAnonPreemptMs));  // the browser user
  TEST_ASSERT_TRUE(g.purpose() == P::Settings);
  TEST_ASSERT_EQUAL_STRING("2222", g.code());
  TEST_ASSERT_EQUAL_UINT32(PresenceGate::kTtlMs, g.remainingMs(PresenceGate::kAnonPreemptMs));
  TEST_ASSERT_FALSE(g.check(P::Update, "1111", PresenceGate::kAnonPreemptMs + 1));  // gone
  // The stranger cannot take it straight back: the new code is fresh for kAnonPreemptMs.
  const uint32_t t0 = PresenceGate::kAnonPreemptMs;
  TEST_ASSERT_FALSE(g.open(P::Update, "3333", t0 + 1000));
  TEST_ASSERT_TRUE(g.check(P::Settings, "2222", t0 + 2000));  // the user gets in

  // Replacing works again and again, each time only once the code on screen is stale.
  PresenceGate h;
  TEST_ASSERT_TRUE(h.open(P::Settings, "1111", 0));
  h.close();
  TEST_ASSERT_TRUE(h.open(P::Update, "2222", 1000));
  TEST_ASSERT_TRUE(h.open(P::Settings, "3333", 1000 + PresenceGate::kAnonPreemptMs));  // gap long over
  h.close();
  TEST_ASSERT_TRUE(h.open(P::Update, "4444", 200000));
  TEST_ASSERT_FALSE(h.open(P::Settings, "5555", 200000 + PresenceGate::kAnonPreemptMs - 1));
  // waitMs is the longer of the two: the code on screen going stale and the purpose's own gap.
  PresenceGate r;
  TEST_ASSERT_TRUE(r.open(P::Update, "1111", 0));
  TEST_ASSERT_TRUE(r.open(P::Reset, "2222", PresenceGate::kAnonPreemptMs));
  r.close();
  TEST_ASSERT_TRUE(r.open(P::Update, "3333", PresenceGate::kAnonPreemptMs + 1));
  TEST_ASSERT_EQUAL_UINT32(PresenceGate::kAnonPreemptMs - 1, r.waitMs(P::Reset, PresenceGate::kAnonPreemptMs + 2));

  // Never over a trusted code, however old.
  PresenceGate t;
  TEST_ASSERT_TRUE(t.open(P::Update, "1111", 0, true));
  TEST_ASSERT_FALSE(t.open(P::Settings, "2222", PresenceGate::kTtlMs - 1));
  // Trusted still pre-empts anonymous at once.
  PresenceGate u;
  TEST_ASSERT_TRUE(u.open(P::Update, "1111", 0));
  TEST_ASSERT_TRUE(u.open(P::Settings, "2222", 10, true));
}

// Joining another network from the setup portal: frictionless only on a fresh unit (never
// configured, nothing saved, not paired). Anything else needs the code on the screen, so whoever
// is near a configured unit that lost its Wi-Fi cannot move it to their network.
static void test_wifi_code_required() {
  // (hasWifiCreds, tokenCount)
  TEST_ASSERT_FALSE(wifiCodeRequired(false, 0));
  TEST_ASSERT_TRUE(wifiCodeRequired(true, 0));
  TEST_ASSERT_TRUE(wifiCodeRequired(false, 1));
  TEST_ASSERT_TRUE(wifiCodeRequired(true, 4));
}

// A factory-reset unit (resale, a return) has no saved network and no pairing: it sets up again
// without a code. everConfigured (the /.configured marker that survives a reset) is intentionally
// not an input here; it still gates OTA (otaCodeRequired).
static void test_wifi_code_a_reset_unit_is_frictionless() {
  TEST_ASSERT_FALSE(wifiCodeRequired(false, 0));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_presence_code_never_replaced_while_active);
  RUN_TEST(test_presence_trusted_preempts_anonymous);
  RUN_TEST(test_presence_anonymous_rate_limit);
  RUN_TEST(test_presence_anonymous_preempts_a_stale_anonymous_code);
  RUN_TEST(test_wifi_code_required);
  RUN_TEST(test_wifi_code_a_reset_unit_is_frictionless);
  RUN_TEST(test_info_view);
  RUN_TEST(test_public_info_has_only_three_fields);
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
  RUN_TEST(test_token_store_remove_and_seen);
  RUN_TEST(test_token_store_undo_add);
  RUN_TEST(test_token_store_undo_remove);
  RUN_TEST(test_token_store_add_cleans_the_host);
  RUN_TEST(test_token_store_rename);
  RUN_TEST(test_token_store_auto_label);
  RUN_TEST(test_tokens_json_round_trip);
  RUN_TEST(test_system_json_fields);
  RUN_TEST(test_token_tag);
  RUN_TEST(test_host_policy);
  RUN_TEST(test_sha256_and_hmac);
  RUN_TEST(test_address_challenge);
  RUN_TEST(test_host_verdict);
  RUN_TEST(test_wrong_host_reply);
  RUN_TEST(test_find_content_length);
  RUN_TEST(test_content_length_matches_the_server);
  RUN_TEST(test_headers_plain_and_complete);
  RUN_TEST(test_headers_incomplete);
  RUN_TEST(test_headers_multipart_boundary);
  RUN_TEST(test_headers_boundary_length);
  RUN_TEST(test_headers_huge_boundary);
  RUN_TEST(test_headers_bad_multipart);
  RUN_TEST(test_headers_garbage);
  RUN_TEST(test_presence_lockout_escalates);
  RUN_TEST(test_presence_lockout_caps_at_one_day);
  RUN_TEST(test_lockout_survives_restore);
  RUN_TEST(test_presence_failures_survive_reopen);
  RUN_TEST(test_presence_lockout_clock_wrap);
  RUN_TEST(test_ota_code_required);
  RUN_TEST(test_web_session);
  RUN_TEST(test_rate_limiter_token_bucket);
  RUN_TEST(test_via_soft_ap_subnet);
  return UNITY_END();
}
