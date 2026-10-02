#include <unity.h>

#include "miblo_saveretry.h"

using namespace miblo;

void setUp() {}
void tearDown() {}

static constexpr uint32_t M = 60000;

static void test_idle_never_due() {
  SaveRetry r;
  TEST_ASSERT_FALSE(r.pending());
  TEST_ASSERT_FALSE(r.due(0));
  TEST_ASSERT_FALSE(r.due(123456789u));
}

static void test_request_is_due_at_once_and_success_clears() {
  SaveRetry r;
  r.request(5000);
  TEST_ASSERT_TRUE(r.pending());
  TEST_ASSERT_TRUE(r.due(5000));
  r.succeeded();
  TEST_ASSERT_FALSE(r.pending());
  TEST_ASSERT_FALSE(r.due(5000 + 10 * M));
}

// 1, 2, 4, 8, 16, 32 minutes, then never more than an hour apart.
static void test_backoff_doubles_up_to_an_hour() {
  SaveRetry r;
  uint32_t t = 1000;
  r.request(t);
  const uint32_t waits[] = {1, 2, 4, 8, 16, 32, 60, 60, 60};
  for (uint32_t w : waits) {
    TEST_ASSERT_TRUE(r.due(t));
    r.failed(t);
    TEST_ASSERT_TRUE(r.pending());
    TEST_ASSERT_FALSE(r.due(t + w * M - 1));
    TEST_ASSERT_TRUE(r.due(t + w * M));
    t += w * M;
  }
}

// A save that works again starts the next failure's backoff from a minute.
static void test_success_resets_backoff() {
  SaveRetry r;
  r.request(0);
  r.failed(0);
  r.failed(M);
  r.failed(3 * M);  // next wait would be 8 min
  r.succeeded();
  r.request(100 * M);
  r.failed(100 * M);
  TEST_ASSERT_FALSE(r.due(101 * M - 1));
  TEST_ASSERT_TRUE(r.due(101 * M));
}

// L1: a stream of changes (an API client in a loop, anonymous page views) is written at most once
// every kMinGapMs, and the last change is never lost: it is in the next write.
static void test_successful_saves_are_spaced() {
  SaveRetry r;
  r.request(1000);
  TEST_ASSERT_TRUE(r.due(1000));  // the first change: at once
  r.succeeded(1000);
  r.request(1200);  // the next, 200 ms later: waits for the gap
  TEST_ASSERT_FALSE(r.due(1200));
  TEST_ASSERT_FALSE(r.due(1000 + SaveRetry::kMinGapMs - 1));
  r.request(3000);  // more changes meanwhile: still one write, at the same time
  TEST_ASSERT_TRUE(r.due(1000 + SaveRetry::kMinGapMs));
  r.succeeded(1000 + SaveRetry::kMinGapMs);
  TEST_ASSERT_FALSE(r.pending());
  // Long after the last write: at once again.
  r.request(100000);
  TEST_ASSERT_TRUE(r.due(100000));
  TEST_ASSERT_TRUE(SaveRetry::kMinGapMs >= 2000);
  // Across the clock's wrap.
  SaveRetry w;
  w.request(0xFFFFFF00u);
  w.succeeded(0xFFFFFF00u);
  w.request(0x00000010u);
  TEST_ASSERT_FALSE(w.due(0x00000010u));
  TEST_ASSERT_TRUE(w.due(0xFFFFFF00u + SaveRetry::kMinGapMs));
}

// A change while a failed save waits is tried a minute after that failure at the latest, never
// sooner: a stream of changes on a failing flash is at most one attempt a minute.
static void test_change_during_backoff_is_rate_limited() {
  SaveRetry r;
  r.request(0);
  for (int i = 0; i < 6; i++) r.failed(0);  // waiting an hour now
  r.request(10 * 1000);
  TEST_ASSERT_FALSE(r.due(M - 1));
  TEST_ASSERT_TRUE(r.due(M));
  // Long after the failure: tried at once.
  SaveRetry s;
  s.request(0);
  s.failed(0);
  s.failed(M);  // due at 3 min
  s.request(2 * M + 30000);
  TEST_ASSERT_TRUE(s.due(2 * M + 30000));
}

// On an hourly backoff, a change more than a minute after the failure is tried at once.
static void test_change_long_after_failure_on_hourly_backoff() {
  SaveRetry r;
  r.request(0);
  for (int i = 0; i < 7; i++) r.failed(0);  // the next attempt waits an hour
  TEST_ASSERT_FALSE(r.due(59 * M));
  r.request(M + 1);
  TEST_ASSERT_TRUE(r.due(M + 1));
  r.request(10 * M);  // still pending: stays due
  TEST_ASSERT_TRUE(r.due(10 * M));
}

// A change while a retry is due soon never pushes it later.
static void test_change_never_delays_a_retry() {
  SaveRetry r;
  r.request(0);
  r.failed(0);  // due at 1 min
  r.request(30000);
  TEST_ASSERT_TRUE(r.due(M));
}

// millis() wraps after ~49.7 days; the schedule keeps working across it.
static void test_survives_millis_wrap() {
  SaveRetry r;
  const uint32_t t = 0xFFFFFFFFu - 30000;
  r.request(t);
  TEST_ASSERT_TRUE(r.due(t));
  r.failed(t);
  TEST_ASSERT_FALSE(r.due(t + 30000));
  TEST_ASSERT_FALSE(r.due(t + M - 1));
  TEST_ASSERT_TRUE(r.due(t + M));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_idle_never_due);
  RUN_TEST(test_request_is_due_at_once_and_success_clears);
  RUN_TEST(test_backoff_doubles_up_to_an_hour);
  RUN_TEST(test_success_resets_backoff);
  RUN_TEST(test_successful_saves_are_spaced);
  RUN_TEST(test_change_during_backoff_is_rate_limited);
  RUN_TEST(test_change_long_after_failure_on_hourly_backoff);
  RUN_TEST(test_change_never_delays_a_retry);
  RUN_TEST(test_survives_millis_wrap);
  return UNITY_END();
}
