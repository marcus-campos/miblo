#include <string.h>
#include <unity.h>

#include "miblo_livetz.h"
#include "miblo_overview.h"
#include "miblo_snapshot.h"
#include "miblo_zone.h"

using namespace miblo;

void setUp() {}
void tearDown() {}

// 2026-10-02 12:00 UTC; Lisbon leaves daylight time on 2026-10-25 at 01:00 UTC.
constexpr uint32_t kNow = 1790942400;
constexpr uint32_t kLisbonEnd = 1792890000;
constexpr uint32_t kDay = 86400;
constexpr uint32_t kEve = kLisbonEnd - 2 * kDay;  // a snapshot two days before the change

static Snapshot withZones(uint32_t now, std::initializer_list<LiveZone> zones) {
  Snapshot s{};
  s.now = now;
  for (const LiveZone& z : zones) s.zones[s.zoneCount++] = z;
  return s;
}
static LiveZone zone(const char* name, int16_t off, uint32_t next, int16_t noff) {
  return LiveZone{hashStr(kHashSeed, name), off, noff, next};
}

static void test_offset_until_and_after_next() {
  LiveTz live;
  live.observe(withZones(kEve, {zone("Europe/Lisbon", 60, kLisbonEnd, 0)}));
  int32_t east = 0;
  TEST_ASSERT_TRUE(live.offset("Europe/Lisbon", kEve, east));
  TEST_ASSERT_EQUAL_INT32(3600, east);
  TEST_ASSERT_TRUE(live.offset("Europe/Lisbon", kLisbonEnd - 1, east));
  TEST_ASSERT_EQUAL_INT32(3600, east);
  // The change happens on time, with no new snapshot.
  TEST_ASSERT_TRUE(live.offset("Europe/Lisbon", kLisbonEnd, east));
  TEST_ASSERT_EQUAL_INT32(0, east);
}

static void test_no_change_announced() {
  LiveTz live;
  live.observe(withZones(kNow, {zone("Asia/Kathmandu", 345, 0, 345)}));
  int32_t east = 0;
  TEST_ASSERT_TRUE(live.offset("Asia/Kathmandu", kNow + 3 * kDay, east));
  TEST_ASSERT_EQUAL_INT32(345 * 60, east);
}

// Older than kLiveTzMaxAgeSec, or from a clock far ahead of ours: back to the table.
static void test_stale_falls_back() {
  LiveTz live;
  live.observe(withZones(kNow, {zone("Europe/Lisbon", 60, kLisbonEnd, 0)}));
  int32_t east = 0;
  TEST_ASSERT_TRUE(live.offset("Europe/Lisbon", kNow + kLiveTzMaxAgeSec, east));
  TEST_ASSERT_FALSE(live.offset("Europe/Lisbon", kNow + kLiveTzMaxAgeSec + 1, east));
  TEST_ASSERT_TRUE(live.offset("Europe/Lisbon", kNow - 60, east));  // a little clock skew is fine
  TEST_ASSERT_FALSE(live.offset("Europe/Lisbon", kNow - 2 * kDay, east));
  TEST_ASSERT_FALSE(live.offset("Europe/Lisbon", 0, east));  // time unknown
}

static void test_other_zone_falls_back() {
  LiveTz live;
  live.observe(withZones(kNow, {zone("Europe/Lisbon", 60, kLisbonEnd, 0)}));
  int32_t east = 0;
  TEST_ASSERT_FALSE(live.offset("Europe/London", kNow, east));
  TEST_ASSERT_FALSE(live.offset("", kNow, east));
  TEST_ASSERT_FALSE(live.offset(nullptr, kNow, east));
  LiveTz empty;
  TEST_ASSERT_FALSE(empty.offset("Europe/Lisbon", kNow, east));
}

// A snapshot without zones (an older bridge, the alerts-only one) keeps the last ones; a new set
// replaces them.
static void test_observe_keeps_and_replaces() {
  LiveTz live;
  live.observe(withZones(kNow, {zone("Europe/Lisbon", 60, kLisbonEnd, 0), zone("Asia/Tokyo", 540, 0, 540)}));
  live.observe(withZones(kNow + kDay, {}));
  int32_t east = 0;
  TEST_ASSERT_TRUE(live.offset("Asia/Tokyo", kNow + kDay, east));
  TEST_ASSERT_EQUAL_INT32(540 * 60, east);
  // Still counted from the snapshot that carried them.
  TEST_ASSERT_FALSE(live.offset("Asia/Tokyo", kNow + kLiveTzMaxAgeSec + 1, east));
  live.observe(withZones(kNow + 2 * kDay, {zone("Europe/Lisbon", 60, kLisbonEnd, 0)}));
  TEST_ASSERT_FALSE(live.offset("Asia/Tokyo", kNow + 2 * kDay, east));
  TEST_ASSERT_TRUE(live.offset("Europe/Lisbon", kNow + 15 * kDay, east));  // fresh again
  live.observe(withZones(0, {zone("Asia/Tokyo", 540, 0, 540)}));  // no time in it: ignored
  TEST_ASSERT_TRUE(live.offset("Europe/Lisbon", kNow + 2 * kDay, east));
}

// Epoch edges: a change at the very end of the 32-bit range, a change already past when sent.
static void test_epoch_edges() {
  LiveTz live;
  live.observe(withZones(0xFFFF0000u, {zone("A/B", 60, 0xFFFFFFFFu, 120)}));
  int32_t east = 0;
  TEST_ASSERT_TRUE(live.offset("A/B", 0xFFFFFFFEu, east));
  TEST_ASSERT_EQUAL_INT32(3600, east);
  TEST_ASSERT_TRUE(live.offset("A/B", 0xFFFFFFFFu, east));
  TEST_ASSERT_EQUAL_INT32(7200, east);
  live.observe(withZones(kNow, {zone("A/B", 60, kNow - 10, 120)}));
  TEST_ASSERT_TRUE(live.offset("A/B", kNow, east));
  TEST_ASSERT_EQUAL_INT32(7200, east);
}

// The rule the main clock runs on: a fixed offset while the live one is valid, else the table's.
static void test_live_rule() {
  LiveTz live;
  char rule[48];
  liveRule("Europe/Lisbon", live, kNow, rule, sizeof(rule));
  TEST_ASSERT_EQUAL_STRING("WET0WEST,M3.5.0/1,M10.5.0", rule);  // nothing live: the table
  live.observe(withZones(kEve, {zone("Europe/Lisbon", 60, kLisbonEnd, 0), zone("Asia/Kathmandu", 345, 0, 345)}));
  liveRule("Europe/Lisbon", live, kEve, rule, sizeof(rule));
  TEST_ASSERT_EQUAL_STRING("<+01>-1", rule);
  liveRule("Europe/Lisbon", live, kLisbonEnd, rule, sizeof(rule));
  TEST_ASSERT_EQUAL_STRING("<+00>0", rule);
  liveRule("Asia/Kathmandu", live, kEve, rule, sizeof(rule));
  TEST_ASSERT_EQUAL_STRING("<+0545>-5:45", rule);
  liveRule("Europe/Lisbon", live, kEve + kLiveTzMaxAgeSec + 1, rule, sizeof(rule));
  TEST_ASSERT_EQUAL_STRING("WET0WEST,M3.5.0/1,M10.5.0", rule);  // stale: the table again
  liveRule("<-03>3", live, kEve, rule, sizeof(rule));  // a legacy POSIX rule stays as it is
  TEST_ASSERT_EQUAL_STRING("<-03>3", rule);
}

static void test_fixed_rule_format() {
  char rule[16];
  fixedRule(-3 * 3600, rule, sizeof(rule));
  TEST_ASSERT_EQUAL_STRING("<-03>3", rule);
  fixedRule(-(3 * 3600 + 30 * 60), rule, sizeof(rule));
  TEST_ASSERT_EQUAL_STRING("<-0330>3:30", rule);
  fixedRule(13 * 3600 + 45 * 60, rule, sizeof(rule));
  TEST_ASSERT_EQUAL_STRING("<+1345>-13:45", rule);
  fixedRule(0, rule, sizeof(rule));
  TEST_ASSERT_EQUAL_STRING("<+00>0", rule);
}

// The second clock follows the live offset of its zone too, and the table without one.
static void test_second_clock() {
  LiveTz live;
  char hhmm[6];
  // 2026-12-01 12:00 UTC in Vancouver: the table says UTC-7 (permanent daylight time from 2026).
  const uint32_t dec1 = 1796126400;
  TEST_ASSERT_TRUE(zoneHHMM("America/Vancouver", dec1, hhmm, sizeof(hhmm), &live));
  TEST_ASSERT_EQUAL_STRING("05:00", hhmm);
  // A computer whose data says otherwise (a later rule change) wins while it is fresh.
  live.observe(withZones(dec1, {zone("America/Vancouver", -480, 0, -480)}));
  TEST_ASSERT_TRUE(zoneHHMM("America/Vancouver", dec1, hhmm, sizeof(hhmm), &live));
  TEST_ASSERT_EQUAL_STRING("04:00", hhmm);
  TEST_ASSERT_TRUE(zoneHHMM("America/Vancouver", dec1, hhmm, sizeof(hhmm)));  // without it: table
  TEST_ASSERT_EQUAL_STRING("05:00", hhmm);
  TEST_ASSERT_TRUE(zoneHHMM("America/Vancouver", dec1 + kLiveTzMaxAgeSec + 1, hhmm, sizeof(hhmm), &live));
  TEST_ASSERT_EQUAL_STRING("05:00", hhmm);  // stale
  TEST_ASSERT_FALSE(zoneHHMM("Mars/Base", dec1, hhmm, sizeof(hhmm), &live));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_offset_until_and_after_next);
  RUN_TEST(test_no_change_announced);
  RUN_TEST(test_stale_falls_back);
  RUN_TEST(test_other_zone_falls_back);
  RUN_TEST(test_observe_keeps_and_replaces);
  RUN_TEST(test_epoch_edges);
  RUN_TEST(test_live_rule);
  RUN_TEST(test_fixed_rule_format);
  RUN_TEST(test_second_clock);
  return UNITY_END();
}
