#include <ArduinoJson.h>
#include <string.h>
#include <unity.h>

#include "miblo_config.h"
#include "miblo_tz.h"
#include "miblo_tz_table.h"

using namespace miblo;

void setUp() {}
void tearDown() {}

static bool patch(Config& cfg, const char* json, const char** bad = nullptr) {
  StaticJsonDocument<256> doc;
  TEST_ASSERT_FALSE(deserializeJson(doc, json));
  return applyConfigPatch(cfg, doc.as<JsonObjectConst>(), bad);
}

static void test_lookup_known_zones() {
  char out[48];
  TEST_ASSERT_TRUE(tzLookup("America/Sao_Paulo", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("<-03>3", out);
  TEST_ASSERT_TRUE(tzLookup("Europe/Lisbon", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("WET0WEST,M3.5.0/1,M10.5.0", out);
  TEST_ASSERT_TRUE(tzLookup("Etc/UTC", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("UTC0", out);
  // First and last entries of the table (boundaries of the scan).
  TEST_ASSERT_TRUE(tzLookup("Africa/Abidjan", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("GMT0", out);
  const char* last = kTzNames + kTzNamesLen - 2;
  while (last > kTzNames && last[-1] != '\n') last--;
  char name[48];
  size_t n = (size_t)(kTzNames + kTzNamesLen - 1 - last);
  memcpy(name, last, n);
  name[n] = 0;
  TEST_ASSERT_TRUE(tzLookup(name, out, sizeof(out)));
}

static void test_lookup_unknown_or_malformed() {
  char out[48] = "x";
  TEST_ASSERT_FALSE(tzLookup("Mars/Olympus_Mons", out, sizeof(out)));
  TEST_ASSERT_FALSE(tzLookup("America/Sao", out, sizeof(out)));        // prefix of a name
  TEST_ASSERT_FALSE(tzLookup("America/Sao_Paulo2", out, sizeof(out)));  // longer than a name
  TEST_ASSERT_FALSE(tzLookup("america/sao_paulo", out, sizeof(out)));   // exact case only
  TEST_ASSERT_FALSE(tzLookup("Africa/Abidjan\nAfrica/Accra", out, sizeof(out)));
  TEST_ASSERT_FALSE(tzLookup("", out, sizeof(out)));
  TEST_ASSERT_FALSE(tzLookup(nullptr, out, sizeof(out)));
  TEST_ASSERT_FALSE(tzLookup("Europe/Lisbon", out, 5));  // rule does not fit
}

static void test_table_shape() {
  TEST_ASSERT_TRUE(kTzCount > 400);
  TEST_ASSERT_EQUAL_size_t(kTzNamesLen, strlen(kTzNames));
  TEST_ASSERT_EQUAL_CHAR('\n', kTzNames[kTzNamesLen - 1]);
  size_t lines = 0;
  for (size_t i = 0; i < kTzNamesLen; i++) lines += kTzNames[i] == '\n';
  TEST_ASSERT_EQUAL_size_t(kTzCount, lines);
  lines = 0;
  for (const char* p = kTzPosix; *p; p++) lines += *p == '\n';
  TEST_ASSERT_EQUAL_size_t(kTzCount, lines);
}

static void test_looks_posix() {
  TEST_ASSERT_TRUE(tzLooksPosix("UTC0"));
  TEST_ASSERT_TRUE(tzLooksPosix("<-03>3"));
  TEST_ASSERT_TRUE(tzLooksPosix("<+0530>-5:30"));
  TEST_ASSERT_TRUE(tzLooksPosix("EST5EDT,M3.2.0,M11.1.0"));
  TEST_ASSERT_TRUE(tzLooksPosix("CET-1CEST,M3.5.0,M10.5.0/3"));
  TEST_ASSERT_FALSE(tzLooksPosix("America/Sao_Paulo"));
  TEST_ASSERT_FALSE(tzLooksPosix("Mars/Olympus"));
  TEST_ASSERT_FALSE(tzLooksPosix("UT0"));
  TEST_ASSERT_FALSE(tzLooksPosix("<>3"));
  TEST_ASSERT_FALSE(tzLooksPosix("UTC 0"));
  TEST_ASSERT_FALSE(tzLooksPosix(""));
}

static void test_resolve() {
  char out[48];
  tzResolve("America/Sao_Paulo", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("<-03>3", out);
  tzResolve("<-03>3", out, sizeof(out));  // legacy config: used as-is
  TEST_ASSERT_EQUAL_STRING("<-03>3", out);
  tzResolve("UTC0", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("UTC0", out);
  tzResolve("Mars/Olympus_Mons", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("UTC0", out);
}

static void test_config_patch_tz() {
  Config c;
  TEST_ASSERT_TRUE(patch(c, "{\"tz\":\"America/Sao_Paulo\"}"));
  TEST_ASSERT_EQUAL_STRING("America/Sao_Paulo", c.tz);
  TEST_ASSERT_TRUE(patch(c, "{\"tz\":\"America/Argentina/Buenos_Aires\"}"));  // longest name
  TEST_ASSERT_TRUE(patch(c, "{\"tz\":\"EST5EDT,M3.2.0,M11.1.0\"}"));             // legacy POSIX
  TEST_ASSERT_EQUAL_STRING("EST5EDT,M3.2.0,M11.1.0", c.tz);
  const char* bad = nullptr;
  TEST_ASSERT_FALSE(patch(c, "{\"tz\":\"Mars/Olympus_Mons\"}", &bad));
  TEST_ASSERT_EQUAL_STRING("tz", bad);
  TEST_ASSERT_FALSE(patch(c, "{\"tz\":\"\"}"));
  TEST_ASSERT_FALSE(patch(c, "{\"tz\":\"garbage\"}"));
  TEST_ASSERT_FALSE(patch(c, "{\"tz\":3}"));
  TEST_ASSERT_EQUAL_STRING("EST5EDT,M3.2.0,M11.1.0", c.tz);  // unchanged by the failures
}

// kTzNameMax (generated) is the longest name in the table: Config::tz2 is sized from it.
static void test_longest_name_constant() {
  size_t longest = 0, cur = 0;
  for (size_t i = 0; i < kTzNamesLen; i++) {
    if (kTzNames[i] == '\n') {
      if (cur > longest) longest = cur;
      cur = 0;
    } else {
      cur++;
    }
  }
  TEST_ASSERT_EQUAL_UINT(longest, kTzNameMax);
  Config c;
  TEST_ASSERT_TRUE(sizeof(c.tz2) > kTzNameMax);
  TEST_ASSERT_TRUE(applyConfigPatch(c, [] {
    static StaticJsonDocument<128> d;
    deserializeJson(d, "{\"tz2\":\"America/Argentina/Buenos_Aires\"}");  // 30 characters
    return d.as<JsonObjectConst>();
  }(), nullptr));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_lookup_known_zones);
  RUN_TEST(test_lookup_unknown_or_malformed);
  RUN_TEST(test_table_shape);
  RUN_TEST(test_looks_posix);
  RUN_TEST(test_resolve);
  RUN_TEST(test_config_patch_tz);
  RUN_TEST(test_longest_name_constant);
  return UNITY_END();
}
