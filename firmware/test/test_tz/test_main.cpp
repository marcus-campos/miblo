#include <ArduinoJson.h>
#include <string.h>
#include <unity.h>

#include <string>

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

// The plain list GET /api/zones serves: every name of the table followed by '\n'.
static std::string plainNames() {
  std::string out;
  TzNames n;
  while (n.next()) out += std::string(n.name) + "\n";
  return out;
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
  const std::string all = plainNames();
  const size_t start = all.rfind('\n', all.size() - 2) + 1;
  TEST_ASSERT_TRUE(tzLookup(all.substr(start, all.size() - 1 - start).c_str(), out, sizeof(out)));
}

// Every name resolves to its own rule (kTzRule indexes kTzRules).
static void test_every_name_resolves() {
  TzNames n;
  while (n.next()) {
    const char* r = kTzRules;
    for (unsigned k = 0; k < kTzRule[n.index]; k++) r = strchr(r, '\n') + 1;
    char out[48];
    TEST_ASSERT_TRUE_MESSAGE(tzLookup(n.name, out, sizeof(out)), n.name);
    TEST_ASSERT_EQUAL_size_t(strchr(r, '\n') - r, strlen(out));
    TEST_ASSERT_EQUAL_MEMORY(r, out, strlen(out));
  }
  TEST_ASSERT_EQUAL_size_t(kTzCount - 1, n.index);
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
  TEST_ASSERT_EQUAL_size_t(kTzNamesPackedLen, strlen(kTzNames));
  const std::string all = plainNames();
  TEST_ASSERT_EQUAL_size_t(kTzNamesLen, all.size());
  TEST_ASSERT_EQUAL_CHAR('\n', all.back());
  size_t lines = 0;
  for (char c : all) lines += c == '\n';
  TEST_ASSERT_EQUAL_size_t(kTzCount, lines);
  // Sorted, distinct, printable ASCII.
  TzNames n;
  std::string prev;
  while (n.next()) {
    TEST_ASSERT_TRUE_MESSAGE(prev < n.name, n.name);
    for (const char* c = n.name; *c; c++) TEST_ASSERT_TRUE(*c >= 0x21 && *c <= 0x7E);
    prev = n.name;
  }
  lines = 0;
  for (const char* p = kTzRules; *p; p++) lines += *p == '\n';
  TEST_ASSERT_EQUAL_size_t(kTzRuleCount, lines);
  for (size_t i = 0; i < kTzCount; i++) TEST_ASSERT_TRUE(kTzRule[i] < kTzRuleCount);
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
// GET /api/zones serves exactly the plain list the generator front-coded (its FNV-1a is
// generated beside the table, so a regenerated table needs no test change).
static void test_served_list_unchanged() {
  uint32_t h = 0x811c9dc5;
  for (char c : plainNames()) h = (h ^ (uint8_t)c) * 0x01000193;
  TEST_ASSERT_EQUAL_HEX32((uint32_t)kTzNamesHash, h);
}

// Zones that changed rules after the old table was made (tz database 2026): permanent daylight
// time in British Columbia, permanent UTC-6 in Alberta, Morocco back on UTC+0.
static void test_recent_rule_changes() {
  char out[48];
  TEST_ASSERT_TRUE(tzLookup("America/Vancouver", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("MST7", out);
  TEST_ASSERT_TRUE(tzLookup("America/Edmonton", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("CST6", out);
  TEST_ASSERT_TRUE(tzLookup("Africa/Casablanca", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("<+00>0", out);
  TEST_ASSERT_TRUE(tzLookup("Europe/Kyiv", out, sizeof(out)));  // new names are added...
  TEST_ASSERT_TRUE(tzLookup("Europe/Kiev", out, sizeof(out)));  // ...and old ones are kept
}

static void test_longest_name_constant() {
  size_t longest = 0;
  TzNames n;
  while (n.next()) longest = strlen(n.name) > longest ? strlen(n.name) : longest;
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
  RUN_TEST(test_every_name_resolves);
  RUN_TEST(test_served_list_unchanged);
  RUN_TEST(test_recent_rule_changes);
  RUN_TEST(test_lookup_unknown_or_malformed);
  RUN_TEST(test_table_shape);
  RUN_TEST(test_looks_posix);
  RUN_TEST(test_resolve);
  RUN_TEST(test_config_patch_tz);
  RUN_TEST(test_longest_name_constant);
  return UNITY_END();
}
