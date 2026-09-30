#include <string.h>
#include <unity.h>

#include "miblo_occasions.h"

using namespace miblo;

void setUp() {}
void tearDown() {}

static Date D(uint16_t y, uint8_t m, uint8_t d) { return Date{y, m, d}; }

static void test_month_day_and_date_parsing() {
  uint8_t m, d;
  uint16_t y;
  TEST_ASSERT_TRUE(parseMonthDay("03-14", m, d));
  TEST_ASSERT_EQUAL_UINT8(3, m);
  TEST_ASSERT_EQUAL_UINT8(14, d);
  TEST_ASSERT_TRUE(parseMonthDay("02-29", m, d));
  TEST_ASSERT_FALSE(parseMonthDay("02-30", m, d));
  TEST_ASSERT_FALSE(parseMonthDay("04-31", m, d));
  TEST_ASSERT_FALSE(parseMonthDay("13-01", m, d));
  TEST_ASSERT_FALSE(parseMonthDay("3-14", m, d));
  TEST_ASSERT_FALSE(parseMonthDay("14/03", m, d));
  TEST_ASSERT_FALSE(parseMonthDay(nullptr, m, d));
  TEST_ASSERT_TRUE(parseDate("2026-09-30", y, m, d));
  TEST_ASSERT_EQUAL_UINT16(2026, y);
  TEST_ASSERT_TRUE(parseDate("2028-02-29", y, m, d));
  TEST_ASSERT_FALSE(parseDate("2027-02-29", y, m, d));
  TEST_ASSERT_FALSE(parseDate("1999-01-01", y, m, d));
}

static void test_config_accepts_owner_birthday_and_friends() {
  Config c;
  TEST_ASSERT_TRUE(c.friends);  // visits between Miblos come on by default
  StaticJsonDocument<256> doc;
  deserializeJson(doc, "{\"owner\":\"Ana\",\"birthday\":\"03-14\",\"friends\":false,\"born\":\"2026-09-30\"}");
  TEST_ASSERT_TRUE(applyConfigPatch(c, doc.as<JsonObjectConst>(), nullptr));
  TEST_ASSERT_EQUAL_STRING("Ana", c.owner);
  TEST_ASSERT_EQUAL_STRING("03-14", c.birthday);
  TEST_ASSERT_EQUAL_STRING("2026-09-30", c.born);
  TEST_ASSERT_FALSE(c.friends);
  const char* bad = nullptr;
  deserializeJson(doc, "{\"birthday\":\"14/03\"}");
  TEST_ASSERT_FALSE(applyConfigPatch(c, doc.as<JsonObjectConst>(), &bad));
  TEST_ASSERT_EQUAL_STRING("birthday", bad);
  deserializeJson(doc, "{\"owner\":\"123456789012345678901\"}");
  TEST_ASSERT_FALSE(applyConfigPatch(c, doc.as<JsonObjectConst>(), &bad));
  TEST_ASSERT_EQUAL_STRING("owner", bad);
  deserializeJson(doc, "{\"owner\":\"A\\nB\"}");
  TEST_ASSERT_FALSE(applyConfigPatch(c, doc.as<JsonObjectConst>(), &bad));
  deserializeJson(doc, "{\"owner\":\"\",\"birthday\":\"\"}");  // cleared
  TEST_ASSERT_TRUE(applyConfigPatch(c, doc.as<JsonObjectConst>(), nullptr));
  TEST_ASSERT_EQUAL_STRING("", c.owner);
  TEST_ASSERT_EQUAL_STRING("", c.birthday);
  // Round trip through the stored JSON.
  strcpy(c.owner, "Zoë");
  strcpy(c.birthday, "12-01");
  StaticJsonDocument<1024> out;
  configToStored(c, out.to<JsonObject>());
  Config back;
  TEST_ASSERT_TRUE(applyConfigPatch(back, out.as<JsonObjectConst>(), nullptr));
  TEST_ASSERT_EQUAL_STRING("Zoë", back.owner);
  TEST_ASSERT_EQUAL_STRING("12-01", back.birthday);
  TEST_ASSERT_FALSE(back.friends);
}

static void test_occasions_and_accessories() {
  Config c;
  TEST_ASSERT_EQUAL(Occasion::None, occasionOn(c, D(2026, 9, 30)));
  TEST_ASSERT_EQUAL(Occasion::Halloween, occasionOn(c, D(2026, 10, 31)));
  TEST_ASSERT_EQUAL(Occasion::None, occasionOn(c, D(2026, 10, 28)));
  TEST_ASSERT_EQUAL(Occasion::Christmas, occasionOn(c, D(2026, 12, 20)));
  TEST_ASSERT_EQUAL(Occasion::Christmas, occasionOn(c, D(2026, 12, 26)));
  TEST_ASSERT_EQUAL(Occasion::None, occasionOn(c, D(2026, 12, 27)));
  TEST_ASSERT_EQUAL(Occasion::NewYear, occasionOn(c, D(2026, 12, 31)));
  TEST_ASSERT_EQUAL(Occasion::NewYear, occasionOn(c, D(2027, 1, 1)));
  TEST_ASSERT_EQUAL(Accessory::SantaHat, accessoryFor(Occasion::Christmas));
  TEST_ASSERT_EQUAL(Accessory::WitchHat, accessoryFor(Occasion::Halloween));
  TEST_ASSERT_EQUAL(Accessory::PartyHat, accessoryFor(Occasion::NewYear));
  TEST_ASSERT_EQUAL(Accessory::None, accessoryFor(Occasion::None));
  // The owner's birthday wins over a holiday.
  strcpy(c.birthday, "12-25");
  TEST_ASSERT_EQUAL(Occasion::OwnerBirthday, occasionOn(c, D(2026, 12, 25)));
  TEST_ASSERT_EQUAL(Accessory::PartyHat, accessoryFor(Occasion::OwnerBirthday));
  // Born on Feb 29: celebrated on Feb 28 in other years.
  strcpy(c.birthday, "02-29");
  TEST_ASSERT_EQUAL(Occasion::OwnerBirthday, occasionOn(c, D(2027, 2, 28)));
  TEST_ASSERT_EQUAL(Occasion::OwnerBirthday, occasionOn(c, D(2028, 2, 29)));
  TEST_ASSERT_EQUAL(Occasion::None, occasionOn(c, D(2028, 2, 28)));
  // The gadget's own birthday: from its first anniversary on.
  Config g;
  strcpy(g.born, "2026-09-30");
  TEST_ASSERT_EQUAL(Occasion::None, occasionOn(g, D(2026, 9, 30)));
  TEST_ASSERT_EQUAL(Occasion::MibloBirthday, occasionOn(g, D(2027, 9, 30)));
  TEST_ASSERT_EQUAL_UINT16(1, mibloAge(g, D(2027, 9, 30)));
  TEST_ASSERT_EQUAL_UINT16(0, mibloAge(g, D(2027, 9, 29)));
  TEST_ASSERT_EQUAL_UINT16(2, mibloAge(g, D(2028, 10, 1)));
}

static void test_daily_greeting_waits_for_the_first_activity_of_the_day() {
  Config c;
  Greeter g;
  const Date day = D(2026, 9, 30);
  // No owner name on an ordinary day: nothing to say.
  g.update(0, true, true, day, 9 * 60, c);
  TEST_ASSERT_EQUAL(Greeting::None, g.showing(0));
  strcpy(c.owner, "Ana");
  g.update(10, true, true, day, 9 * 60, c);
  TEST_ASSERT_EQUAL(Greeting::None, g.showing(10));  // already decided for today
  // The next day: not before 05:00, not without activity or a clock.
  const Date next = D(2026, 10, 1);
  g.update(20, true, true, next, 4 * 60 + 59, c);
  g.update(30, false, true, next, 9 * 60, c);
  g.update(40, true, false, next, 9 * 60, c);
  TEST_ASSERT_EQUAL(Greeting::None, g.showing(40));
  g.update(50, true, true, next, 9 * 60, c);
  TEST_ASSERT_EQUAL(Greeting::Morning, g.showing(50));
  TEST_ASSERT_EQUAL(Greeting::Morning, g.showing(50 + Greeter::kShowMs - 1));
  TEST_ASSERT_EQUAL(Greeting::None, g.showing(50 + Greeter::kShowMs));
  g.update(50 + Greeter::kShowMs, true, true, next, 9 * 60 + 1, c);
  TEST_ASSERT_EQUAL(Greeting::None, g.showing(50 + Greeter::kShowMs));  // once a day
  Greeter a, e;
  a.update(0, true, true, next, 14 * 60, c);
  TEST_ASSERT_EQUAL(Greeting::Afternoon, a.showing(0));
  e.update(0, true, true, next, 20 * 60, c);
  TEST_ASSERT_EQUAL(Greeting::Evening, e.showing(0));
}

static void test_party_greetings() {
  Config c;
  strcpy(c.birthday, "03-14");
  Greeter g;
  g.update(0, true, true, D(2027, 3, 14), 8 * 60, c);  // no name needed for a birthday
  TEST_ASSERT_EQUAL(Greeting::OwnerBirthday, g.showing(0));
  TEST_ASSERT_TRUE(greetingIsParty(Greeting::OwnerBirthday));
  TEST_ASSERT_EQUAL(Greeting::OwnerBirthday, g.showing(Greeter::kPartyMs - 1));
  TEST_ASSERT_EQUAL(Greeting::None, g.showing(Greeter::kPartyMs));
  Greeter x, n, h;
  x.update(0, true, true, D(2026, 12, 25), 10 * 60, c);
  TEST_ASSERT_EQUAL(Greeting::Christmas, x.showing(0));
  n.update(0, true, true, D(2027, 1, 1), 10 * 60, c);
  TEST_ASSERT_EQUAL(Greeting::NewYear, n.showing(0));
  h.update(0, true, true, D(2026, 10, 31), 10 * 60, c);  // Halloween: the hat only
  TEST_ASSERT_EQUAL(Greeting::None, h.showing(0));
  // Renaming greets at once, whatever the day.
  Greeter r;
  r.named(100);
  TEST_ASSERT_EQUAL(Greeting::Named, r.showing(100));
}

static void test_greeting_lines() {
  char l1[64], l2[64];
  greetingLines(Lang::En, Greeting::Named, "", "Tofu", l1, sizeof(l1), l2, sizeof(l2));
  TEST_ASSERT_EQUAL_STRING("Hi! I'm", l1);
  TEST_ASSERT_EQUAL_STRING("Tofu", l2);
  greetingLines(Lang::PtBR, Greeting::Morning, "Ana", "Tofu", l1, sizeof(l1), l2, sizeof(l2));
  TEST_ASSERT_EQUAL_STRING("Bom dia", l1);
  TEST_ASSERT_EQUAL_STRING("Ana", l2);
  greetingLines(Lang::En, Greeting::OwnerBirthday, "", "Tofu", l1, sizeof(l1), l2, sizeof(l2));
  TEST_ASSERT_EQUAL_STRING("", l1);
  TEST_ASSERT_EQUAL_STRING("Happy birthday", l2);
  greetingLines(Lang::En, Greeting::MibloBirthday, "Ana", "Tofu", l1, sizeof(l1), l2, sizeof(l2));
  TEST_ASSERT_EQUAL_STRING("Tofu", l1);
  TEST_ASSERT_EQUAL_STRING("It's my birthday!", l2);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_month_day_and_date_parsing);
  RUN_TEST(test_config_accepts_owner_birthday_and_friends);
  RUN_TEST(test_occasions_and_accessories);
  RUN_TEST(test_daily_greeting_waits_for_the_first_activity_of_the_day);
  RUN_TEST(test_party_greetings);
  RUN_TEST(test_greeting_lines);
  return UNITY_END();
}
