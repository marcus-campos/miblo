#include <string.h>
#include <unity.h>

#include "miblo_limits.h"

using namespace miblo;

void setUp() {}
void tearDown() {}

static const uint32_t T0 = 1790616720;

static Snapshot at(uint32_t now, uint8_t pct, uint32_t reset, bool present = true) {
  Snapshot s;
  memset(&s, 0, sizeof(s));
  s.now = now;
  s.hasUsage = present;
  s.h5 = {present, pct, reset};
  return s;
}

// A reset after real use is celebrated for kCelebrateMs; a reset after light use is not.
static void test_reset_after_real_use_is_celebrated() {
  LimitWatch w;
  const uint32_t reset = T0 + 3600;
  w.observe(at(T0, 72, reset), 1000);
  TEST_ASSERT_FALSE(w.celebrating(1000));
  // The window resets: new reset 5 h later, usage back near zero.
  w.observe(at(reset + 60, 1, reset + 5 * 3600), 5000);
  TEST_ASSERT_TRUE(w.celebrating(5000));
  TEST_ASSERT_EQUAL_UINT8(1, w.freedPct());
  TEST_ASSERT_TRUE(w.celebrating(5000 + LimitWatch::kCelebrateMs - 1));
  TEST_ASSERT_FALSE(w.celebrating(5000 + LimitWatch::kCelebrateMs));

  LimitWatch light;
  light.observe(at(T0, 20, reset), 1000);
  light.observe(at(reset + 60, 0, reset + 5 * 3600), 5000);
  TEST_ASSERT_FALSE(light.celebrating(5000));
}

// A gap in the data (window dropped by the plugin, computer off) is not a reset by itself; the
// reset is seen when the data comes back with the new window.
static void test_missing_usage_is_not_a_reset() {
  LimitWatch w;
  const uint32_t reset = T0 + 3600;
  w.observe(at(T0, 90, reset), 1000);
  w.observe(at(T0 + 100, 0, 0, false), 2000);
  TEST_ASSERT_FALSE(w.celebrating(2000));
  w.observe(at(reset + 600, 3, reset + 5 * 3600), 3000);
  TEST_ASSERT_TRUE(w.celebrating(3000));
  // The same window again: no second celebration.
  w.observe(at(reset + 700, 4, reset + 5 * 3600), 3000 + LimitWatch::kCelebrateMs);
  TEST_ASSERT_FALSE(w.celebrating(3000 + LimitWatch::kCelebrateMs));
}

// The projection follows the recent pace and only speaks when the window would run out before
// it resets.
static void test_burn_rate_projection() {
  LimitWatch w;
  const uint32_t reset = T0 + 3 * 3600;
  w.observe(at(T0, 40, reset), 0);
  TEST_ASSERT_EQUAL_UINT32(0, w.exhaustAt());       // one sample: no pace yet
  w.observe(at(T0 + 120, 41, reset), 0);
  TEST_ASSERT_EQUAL_UINT32(0, w.exhaustAt());       // under 5 minutes of history
  w.observe(at(T0 + 1200, 50, reset), 0);           // 10 points in 20 min: 50 left -> 100 min
  TEST_ASSERT_EQUAL_UINT32(T0 + 1200 + 6000, w.exhaustAt());

  LimitWatch slow;
  slow.observe(at(T0, 40, T0 + 3600), 0);
  slow.observe(at(T0 + 1800, 45, T0 + 3600), 0);    // 55 left at 5 per 30 min: after the reset
  TEST_ASSERT_EQUAL_UINT32(0, slow.exhaustAt());

  LimitWatch flat;
  flat.observe(at(T0, 40, reset), 0);
  flat.observe(at(T0 + 1800, 41, reset), 0);        // under 2 points: no projection
  TEST_ASSERT_EQUAL_UINT32(0, flat.exhaustAt());

  LimitWatch full;
  full.observe(at(T0, 90, reset), 0);
  full.observe(at(T0 + 1800, 100, reset), 0);        // already out
  TEST_ASSERT_EQUAL_UINT32(0, full.exhaustAt());
}

// Samples older than the pace window are ignored; a reset starts the pace over.
static void test_pace_window_and_reset_clear_history() {
  LimitWatch w;
  const uint32_t reset = T0 + 5 * 3600;
  w.observe(at(T0, 10, reset), 0);
  w.observe(at(T0 + 4000, 12, reset), 0);   // the first sample is now out of the 45 min window
  w.observe(at(T0 + 4600, 20, reset), 0);   // 8 points in 10 min (from the second sample)
  TEST_ASSERT_EQUAL_UINT32(T0 + 4600 + 6000, w.exhaustAt());
  w.observe(at(reset + 10, 1, reset + 5 * 3600), 0);
  TEST_ASSERT_EQUAL_UINT32(0, w.exhaustAt());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_reset_after_real_use_is_celebrated);
  RUN_TEST(test_missing_usage_is_not_a_reset);
  RUN_TEST(test_burn_rate_projection);
  RUN_TEST(test_pace_window_and_reset_clear_history);
  return UNITY_END();
}
