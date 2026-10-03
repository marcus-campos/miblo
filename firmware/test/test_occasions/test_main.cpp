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
  StaticJsonDocument<1536> out;  // the whole stored config
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
  // A birthday set on the day itself, after today's greeting was already decided: re-armed, it
  // is celebrated at the next activity.
  Config late;
  Greeter l;
  l.update(0, true, true, D(2026, 9, 30), 9 * 60, late);
  TEST_ASSERT_EQUAL(Greeting::None, l.showing(0));
  strcpy(late.owner, "Marcus");
  strcpy(late.birthday, "09-30");
  l.update(100, true, true, D(2026, 9, 30), 9 * 60, late);
  TEST_ASSERT_EQUAL(Greeting::None, l.showing(100));
  l.rearm();
  l.update(200, true, true, D(2026, 9, 30), 9 * 60, late);
  TEST_ASSERT_EQUAL(Greeting::OwnerBirthday, l.showing(200));
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

static void test_easter_2024_to_2040() {
  const struct {
    uint16_t y;
    uint8_t m, d;
  } kEaster[] = {{2024, 3, 31}, {2025, 4, 20}, {2026, 4, 5},  {2027, 3, 28}, {2028, 4, 16}, {2029, 4, 1},
                 {2030, 4, 21}, {2031, 4, 13}, {2032, 3, 28}, {2033, 4, 17}, {2034, 4, 9},  {2035, 3, 25},
                 {2036, 4, 13}, {2037, 4, 5},  {2038, 4, 25}, {2039, 4, 10}, {2040, 4, 1}};
  for (const auto& e : kEaster) {
    const Date d = easterSunday(e.y);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(e.y, d.year, "year");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(e.m, d.month, "month");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(e.d, d.day, "day");
    TEST_ASSERT_EQUAL_UINT8(0, weekdayOf(d));  // a Sunday
  }
}

static void test_weekday_of() {
  TEST_ASSERT_EQUAL_UINT8(5, weekdayOf(D(2026, 11, 13)));  // a Friday
  TEST_ASSERT_EQUAL_UINT8(4, weekdayOf(D(2026, 10, 1)));   // a Thursday
  TEST_ASSERT_EQUAL_UINT8(1, weekdayOf(D(2024, 1, 1)));    // a Monday
  TEST_ASSERT_EQUAL_UINT8(4, weekdayOf(D(2024, 2, 29)));   // a Thursday
  TEST_ASSERT_EQUAL_UINT8(6, weekdayOf(D(2000, 1, 1)));    // a Saturday
}

static void test_new_global_occasions() {
  Config c;
  TEST_ASSERT_EQUAL(Occasion::Valentine, occasionOn(c, D(2027, 2, 14)));
  TEST_ASSERT_EQUAL(Occasion::Easter, occasionOn(c, D(2026, 4, 5)));
  TEST_ASSERT_EQUAL(Occasion::None, occasionOn(c, D(2026, 4, 6)));
  TEST_ASSERT_EQUAL(Occasion::ProgrammersDay, occasionOn(c, D(2027, 9, 13)));
  TEST_ASSERT_EQUAL(Occasion::ProgrammersDay, occasionOn(c, D(2028, 9, 12)));  // leap year: day 256
  TEST_ASSERT_EQUAL(Occasion::None, occasionOn(c, D(2028, 9, 13)));
  TEST_ASSERT_EQUAL(Occasion::Friday13, occasionOn(c, D(2026, 11, 13)));
  TEST_ASSERT_EQUAL(Occasion::None, occasionOn(c, D(2026, 10, 13)));           // a Tuesday
  TEST_ASSERT_EQUAL(Occasion::ProgrammersDay, occasionOn(c, D(2030, 9, 13)));  // a Friday: the holiday wins
  TEST_ASSERT_EQUAL(Occasion::None, occasionOn(c, D(2026, 6, 12)));           // no regional dates
  strcpy(c.birthday, "02-14");
  TEST_ASSERT_EQUAL(Occasion::OwnerBirthday, occasionOn(c, D(2027, 2, 14)));
  TEST_ASSERT_EQUAL(Accessory::BunnyEars, accessoryFor(Occasion::Easter));
  TEST_ASSERT_EQUAL(Accessory::Glasses, accessoryFor(Occasion::ProgrammersDay));
  TEST_ASSERT_EQUAL(Accessory::Hearts, accessoryFor(Occasion::Valentine));
  TEST_ASSERT_EQUAL(Accessory::None, accessoryFor(Occasion::Friday13));
}

static void test_occasion_priority() {
  // Owner's birthday > Miblo's birthday > New Year > Christmas > Halloween > Easter > Valentine's >
  // Programmer's Day > Friday the 13th.
  Config c;
  strcpy(c.born, "2020-04-05");
  TEST_ASSERT_EQUAL(Occasion::MibloBirthday, occasionOn(c, D(2026, 4, 5)));  // over Easter
  strcpy(c.birthday, "04-05");
  TEST_ASSERT_EQUAL(Occasion::OwnerBirthday, occasionOn(c, D(2026, 4, 5)));
  Config m;
  strcpy(m.born, "2020-09-13");
  TEST_ASSERT_EQUAL(Occasion::MibloBirthday, occasionOn(m, D(2030, 9, 13)));  // over Programmer's Day
  Config f;
  strcpy(f.born, "2020-11-13");
  TEST_ASSERT_EQUAL(Occasion::MibloBirthday, occasionOn(f, D(2026, 11, 13)));  // over Friday the 13th
  strcpy(f.birthday, "10-31");
  TEST_ASSERT_EQUAL(Occasion::OwnerBirthday, occasionOn(f, D(2026, 10, 31)));  // over Halloween
}

static void test_programmers_day_greeting() {
  Config c;
  strcpy(c.owner, "Ana");
  Greeter g;
  g.update(0, true, true, D(2027, 9, 13), 9 * 60, c);
  TEST_ASSERT_EQUAL(Greeting::ProgrammersDay, g.showing(0));
  char l1[64], l2[64];
  greetingLines(Lang::En, Greeting::ProgrammersDay, "Ana", "Tofu", l1, sizeof(l1), l2, sizeof(l2));
  TEST_ASSERT_EQUAL_STRING("Ana", l1);
  TEST_ASSERT_EQUAL_STRING("Happy Programmer's Day!", l2);
  greetingLines(Lang::En, Greeting::ProgrammersDay, "", "Tofu", l1, sizeof(l1), l2, sizeof(l2));
  TEST_ASSERT_EQUAL_STRING("", l1);
  TEST_ASSERT_EQUAL_STRING("Happy Programmer's Day!", l2);
  TEST_ASSERT_TRUE(greetingIsParty(Greeting::ProgrammersDay));
  // Without a name it still greets, like Christmas.
  Config anon;
  Greeter a;
  a.update(0, true, true, D(2028, 9, 12), 9 * 60, anon);
  TEST_ASSERT_EQUAL(Greeting::ProgrammersDay, a.showing(0));
  // The other new dates bring only their look, not a greeting of their own.
  Greeter v, e;
  v.update(0, true, true, D(2027, 2, 14), 9 * 60, anon);
  TEST_ASSERT_EQUAL(Greeting::None, v.showing(0));
  e.update(0, true, true, D(2026, 4, 5), 9 * 60, c);
  TEST_ASSERT_EQUAL(Greeting::Morning, e.showing(0));
}

static void test_black_cat_schedule() {
  uint32_t at = 0;
  TEST_ASSERT_FALSE(passerbyAt(kPasserbyEveryMs - 1, &at));
  TEST_ASSERT_TRUE(passerbyAt(kPasserbyEveryMs, &at));
  TEST_ASSERT_EQUAL_UINT32(0, at);
  TEST_ASSERT_TRUE(passerbyAt(kPasserbyEveryMs + kPasserbyMs - 1, &at));
  TEST_ASSERT_EQUAL_UINT32(kPasserbyMs - 1, at);
  TEST_ASSERT_FALSE(passerbyAt(kPasserbyEveryMs + kPasserbyMs, &at));
  TEST_ASSERT_TRUE(passerbyAt(2 * kPasserbyEveryMs + 10, &at));
  TEST_ASSERT_EQUAL_UINT32(10, at);
  TEST_ASSERT_FALSE(passerbyAt(0, nullptr));
}

// The owner's own accessories: ids by slot, 0 = nothing, 13 never (the owner's rule), unknown
// ids refused (a newer Miblo's item is "nothing" here, never a wrong drawing).
static void test_wear_ids_by_slot() {
  WearSlot s;
  for (uint8_t id = 1; id <= 10; id++) {
    TEST_ASSERT_TRUE(wearSlotOf(id, s));
    TEST_ASSERT_EQUAL(WearSlot::Head, s);
  }
  for (uint8_t id : {11, 12, 14, 15, 20}) {
    TEST_ASSERT_TRUE(wearSlotOf(id, s));
    TEST_ASSERT_EQUAL(WearSlot::Face, s);
  }
  for (uint8_t id : {16, 17, 18, 19, 21}) {
    TEST_ASSERT_TRUE(wearSlotOf(id, s));
    TEST_ASSERT_EQUAL(WearSlot::Neck, s);
  }
  TEST_ASSERT_FALSE(wearSlotOf(0, s));
  TEST_ASSERT_FALSE(wearSlotOf(13, s));
  for (int id = kWearMax + 1; id < 256; id++) TEST_ASSERT_FALSE(wearSlotOf((uint8_t)id, s));
  for (WearSlot slot : {WearSlot::Head, WearSlot::Face, WearSlot::Neck}) {
    TEST_ASSERT_TRUE(wearFits(slot, 0));
    TEST_ASSERT_FALSE(wearFits(slot, 13));
    TEST_ASSERT_FALSE(wearFits(slot, 22));
    TEST_ASSERT_FALSE(wearFits(slot, 255));
  }
  TEST_ASSERT_TRUE(wearFits(WearSlot::Head, (uint8_t)Wear::Halo));
  TEST_ASSERT_FALSE(wearFits(WearSlot::Face, (uint8_t)Wear::Halo));
  TEST_ASSERT_TRUE(wearFits(WearSlot::Face, (uint8_t)Wear::Headset));
  TEST_ASSERT_FALSE(wearFits(WearSlot::Neck, (uint8_t)Wear::Headset));
  TEST_ASSERT_TRUE(wearFits(WearSlot::Neck, (uint8_t)Wear::Medal));
  TEST_ASSERT_FALSE(wearFits(WearSlot::Head, (uint8_t)Wear::Medal));
}

static bool patchCfg(Config& c, const char* json, const char** bad = nullptr) {
  StaticJsonDocument<256> doc;
  deserializeJson(doc, json);
  return applyConfigPatch(c, doc.as<JsonObjectConst>(), bad);
}

static void test_config_wear_fields() {
  Config c;
  TEST_ASSERT_EQUAL_UINT8(0, c.accHead);  // absent: nothing worn
  TEST_ASSERT_EQUAL_UINT8(0, c.accFace);
  TEST_ASSERT_EQUAL_UINT8(0, c.accNeck);
  TEST_ASSERT_TRUE(c.occasionHats);
  TEST_ASSERT_TRUE(patchCfg(c, "{\"accHead\":5,\"accFace\":20,\"accNeck\":21,\"occasionHats\":false}"));
  TEST_ASSERT_EQUAL_UINT8(5, c.accHead);
  TEST_ASSERT_EQUAL_UINT8(20, c.accFace);
  TEST_ASSERT_EQUAL_UINT8(21, c.accNeck);
  TEST_ASSERT_FALSE(c.occasionHats);
  // Wrong slot, 13, unknown, not a number: refused, nothing changes.
  const char* const bad[] = {"{\"accHead\":11}", "{\"accFace\":13}", "{\"accNeck\":13}", "{\"accHead\":13}",
                             "{\"accNeck\":22}", "{\"accFace\":\"x\"}", "{\"accHead\":-1}",
                             "{\"occasionHats\":2}"};
  for (const char* j : bad) {
    Config d = c;
    const char* field = nullptr;
    TEST_ASSERT_FALSE_MESSAGE(patchCfg(d, j, &field), j);
    TEST_ASSERT_NOT_NULL(field);
    TEST_ASSERT_EQUAL_UINT8(5, d.accHead);
    TEST_ASSERT_EQUAL_UINT8(20, d.accFace);
  }
  TEST_ASSERT_TRUE(patchCfg(c, "{\"accHead\":0,\"accFace\":0,\"accNeck\":0}"));
  TEST_ASSERT_EQUAL_UINT8(0, c.accHead + c.accFace + c.accNeck);
  // The page and the API see them; they survive the flash round-trip.
  c.accHead = 9;
  c.accNeck = 16;
  StaticJsonDocument<4096> out;
  configToStored(c, out.to<JsonObject>());
  TEST_ASSERT_EQUAL_INT(9, out["accHead"].as<int>());
  TEST_ASSERT_FALSE(out["occasionHats"].as<bool>());
  Config e;
  TEST_ASSERT_TRUE(applyConfigPatch(e, out.as<JsonObjectConst>(), nullptr));
  TEST_ASSERT_EQUAL_UINT8(9, e.accHead);
  TEST_ASSERT_EQUAL_UINT8(0, e.accFace);
  TEST_ASSERT_EQUAL_UINT8(16, e.accNeck);
  TEST_ASSERT_FALSE(e.occasionHats);
}

static void test_outfit_for_the_day() {
  Config c;
  c.accHead = (uint8_t)Wear::Crown;
  c.accFace = (uint8_t)Wear::Sunglasses;
  c.accNeck = (uint8_t)Wear::Scarf;
  // An ordinary day: the owner's items.
  Outfit o = outfitFor(c, Accessory::None);
  TEST_ASSERT_EQUAL(Accessory::None, o.occasion);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)Wear::Crown, o.head);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)Wear::Sunglasses, o.face);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)Wear::Scarf, o.neck);
  // A special day's hat (or ears, or hearts) takes the head; face and neck stay.
  for (Accessory a : {Accessory::SantaHat, Accessory::WitchHat, Accessory::PartyHat, Accessory::BunnyEars,
                      Accessory::Hearts}) {
    o = outfitFor(c, a);
    TEST_ASSERT_EQUAL(a, o.occasion);
    TEST_ASSERT_EQUAL_UINT8(0, o.head);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Wear::Sunglasses, o.face);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Wear::Scarf, o.neck);
  }
  // Programmer's Day's glasses take the face: the crown stays.
  o = outfitFor(c, Accessory::Glasses);
  TEST_ASSERT_EQUAL(Accessory::Glasses, o.occasion);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)Wear::Crown, o.head);
  TEST_ASSERT_EQUAL_UINT8(0, o.face);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)Wear::Scarf, o.neck);
  // Special days off: the owner's items always, no special-day accessory.
  c.occasionHats = false;
  o = outfitFor(c, Accessory::SantaHat);
  TEST_ASSERT_EQUAL(Accessory::None, o.occasion);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)Wear::Crown, o.head);
  // An id that does not fit (a config edited by hand) is worn as nothing.
  c.accHead = 13;
  c.accFace = (uint8_t)Wear::Medal;
  o = outfitFor(c, Accessory::None);
  TEST_ASSERT_EQUAL_UINT8(0, o.head);
  TEST_ASSERT_EQUAL_UINT8(0, o.face);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_month_day_and_date_parsing);
  RUN_TEST(test_config_accepts_owner_birthday_and_friends);
  RUN_TEST(test_occasions_and_accessories);
  RUN_TEST(test_daily_greeting_waits_for_the_first_activity_of_the_day);
  RUN_TEST(test_party_greetings);
  RUN_TEST(test_greeting_lines);
  RUN_TEST(test_easter_2024_to_2040);
  RUN_TEST(test_weekday_of);
  RUN_TEST(test_new_global_occasions);
  RUN_TEST(test_occasion_priority);
  RUN_TEST(test_programmers_day_greeting);
  RUN_TEST(test_black_cat_schedule);
  RUN_TEST(test_wear_ids_by_slot);
  RUN_TEST(test_config_wear_fields);
  RUN_TEST(test_outfit_for_the_day);
  return UNITY_END();
}
