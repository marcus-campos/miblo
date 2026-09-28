#include <unity.h>

#include "miblo_format.h"
#include "miblo_utf8.h"

using namespace miblo;

void setUp() {}
void tearDown() {}

static void test_utf8_next_decodes_all_widths() {
  const char* p = "aé€😀";
  TEST_ASSERT_EQUAL_UINT32('a', utf8Next(p));
  TEST_ASSERT_EQUAL_UINT32(0xE9, utf8Next(p));
  TEST_ASSERT_EQUAL_UINT32(0x20AC, utf8Next(p));
  TEST_ASSERT_EQUAL_UINT32(0x1F600, utf8Next(p));
  TEST_ASSERT_EQUAL_UINT32(0, utf8Next(p));
}

static void test_utf8_next_invalid_bytes_become_replacement() {
  const char bad[] = {(char)0xFF, 'a', (char)0xC3, 0};
  const char* p = bad;
  TEST_ASSERT_EQUAL_UINT32(0xFFFD, utf8Next(p));
  TEST_ASSERT_EQUAL_UINT32('a', utf8Next(p));
  TEST_ASSERT_EQUAL_UINT32(0xFFFD, utf8Next(p));
  TEST_ASSERT_EQUAL_UINT32(0, utf8Next(p));
}

static void test_utf8_length_counts_code_points() {
  TEST_ASSERT_EQUAL(5, utf8Length("héllo"));
  TEST_ASSERT_EQUAL(2, utf8Length("项目"));
  TEST_ASSERT_EQUAL(0, utf8Length(""));
}

static void test_utf8_copy_never_splits_sequences() {
  char buf[6];
  utf8Copy(buf, sizeof(buf), "项目项目");  // 3 bytes cada; só 1 cabe em 5 bytes + NUL
  TEST_ASSERT_EQUAL_STRING("项", buf);
  utf8Copy(buf, sizeof(buf), "abcdefgh", 3);
  TEST_ASSERT_EQUAL_STRING("abc", buf);
  utf8Copy(buf, sizeof(buf), nullptr);
  TEST_ASSERT_EQUAL_STRING("", buf);
}

static void test_format_elapsed() {
  char b[16];
  formatElapsed(42, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("0:42", b);
  formatElapsed(192, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("3:12", b);
  formatElapsed(3725, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("1:02:05", b);
}

static void test_format_ago() {
  char b[16];
  formatAgo(45, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("45s", b);
  formatAgo(150, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("2m", b);
  formatAgo(7300, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("2h", b);
  formatAgo(180000, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("2d", b);
}

static void test_format_countdown() {
  char b[16];
  formatCountdown(30, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("<1min", b);
  formatCountdown(2700, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("45min", b);
  formatCountdown(7800, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("2h10", b);
  formatCountdown(240000, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("2d18h", b);
}

static void test_format_tokens() {
  char b[16];
  const struct {
    uint64_t in;
    const char* out;
  } cases[] = {{0, "0"},          {950, "950"},       {1000, "1k"},        {1500, "1.5k"},
               {12300, "12.3k"},  {98000, "98k"},     {412000, "412k"},    {999999, "999k"},
               {1510000, "1.5M"}, {2000000, "2M"},    {123456789, "123M"}, {1000000000ULL, "1000M"}};
  for (const auto& c : cases) {
    formatTokens(c.in, b, sizeof(b));
    TEST_ASSERT_EQUAL_STRING(c.out, b);
  }
}

static void test_format_clock_and_usd() {
  char b[16];
  formatHHMM(9, 5, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("09:05", b);
  formatUsd(4.8f, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("$4.80", b);
  formatUsd(0.004f, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("$0.00", b);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_utf8_next_decodes_all_widths);
  RUN_TEST(test_utf8_next_invalid_bytes_become_replacement);
  RUN_TEST(test_utf8_length_counts_code_points);
  RUN_TEST(test_utf8_copy_never_splits_sequences);
  RUN_TEST(test_format_elapsed);
  RUN_TEST(test_format_ago);
  RUN_TEST(test_format_countdown);
  RUN_TEST(test_format_tokens);
  RUN_TEST(test_format_clock_and_usd);
  return UNITY_END();
}
