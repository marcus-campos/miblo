#include "miblo_occasions.h"

#include <stdio.h>
#include <string.h>

#include "miblo_i18n.h"

namespace miblo {

static bool leap(uint16_t y) { return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0); }

static bool isBirthday(const char* mmdd, const Date& d) {
  uint8_t m, day;
  if (!parseMonthDay(mmdd, m, day)) return false;
  if (m == 2 && day == 29 && !leap(d.year)) return d.month == 2 && d.day == 28;
  return d.month == m && d.day == day;
}

uint16_t mibloAge(const Config& cfg, const Date& d) {
  uint16_t y;
  uint8_t m, day;
  if (!parseDate(cfg.born, y, m, day) || d.year <= y) return 0;
  uint16_t age = (uint16_t)(d.year - y);
  if (d.month < m || (d.month == m && d.day < day)) age--;
  return age;
}

Occasion occasionOn(const Config& cfg, const Date& d) {
  if (cfg.birthday[0] && isBirthday(cfg.birthday, d)) return Occasion::OwnerBirthday;
  if (cfg.born[0] && isBirthday(cfg.born + 5, d) && mibloAge(cfg, d) > 0) return Occasion::MibloBirthday;
  if ((d.month == 12 && d.day == 31) || (d.month == 1 && d.day == 1)) return Occasion::NewYear;
  if (d.month == 12 && d.day >= 20 && d.day <= 26) return Occasion::Christmas;
  if (d.month == 10 && d.day >= 29) return Occasion::Halloween;
  return Occasion::None;
}

Accessory accessoryFor(Occasion o) {
  switch (o) {
    case Occasion::Christmas: return Accessory::SantaHat;
    case Occasion::Halloween: return Accessory::WitchHat;
    case Occasion::NewYear:
    case Occasion::MibloBirthday:
    case Occasion::OwnerBirthday: return Accessory::PartyHat;
    // Stub (daily-life foundation): track F dresses these up.
    case Occasion::Valentine:
    case Occasion::Easter:
    case Occasion::ProgrammersDay:
    case Occasion::Friday13:
    case Occasion::None: break;
  }
  return Accessory::None;
}

void Greeter::start(Greeting g, uint32_t nowMs) {
  kind_ = g;
  sinceMs_ = nowMs;
}

void Greeter::named(uint32_t nowMs) { start(Greeting::Named, nowMs); }

void Greeter::update(uint32_t nowMs, bool active, bool timeKnown, const Date& today, int minuteOfDay,
                     const Config& cfg) {
  if (showing(nowMs) != Greeting::None || !active || !timeKnown || minuteOfDay < kDayStartsAt) return;
  const uint32_t day = (uint32_t)today.year * 400 + today.month * 32 + today.day;
  if (day == lastDay_) return;
  lastDay_ = day;
  Greeting g = Greeting::None;
  switch (occasionOn(cfg, today)) {
    case Occasion::OwnerBirthday: g = Greeting::OwnerBirthday; break;
    case Occasion::MibloBirthday: g = Greeting::MibloBirthday; break;
    case Occasion::NewYear:
      if (today.month == 1) g = Greeting::NewYear;
      break;
    case Occasion::Christmas:
      if (today.day == 25) g = Greeting::Christmas;
      break;
    case Occasion::Halloween:
    // Stub (daily-life foundation): track F greets on these.
    case Occasion::Valentine:
    case Occasion::Easter:
    case Occasion::ProgrammersDay:
    case Occasion::Friday13:
    case Occasion::None: break;
  }
  if (g == Greeting::None && cfg.owner[0]) {
    g = minuteOfDay < 12 * 60 ? Greeting::Morning : minuteOfDay < 18 * 60 ? Greeting::Afternoon : Greeting::Evening;
  }
  if (g != Greeting::None) start(g, nowMs);
}

Greeting Greeter::showing(uint32_t nowMs) const {
  if (kind_ == Greeting::None) return Greeting::None;
  const uint32_t len = greetingIsParty(kind_) ? kPartyMs : kShowMs;
  return nowMs - sinceMs_ < len ? kind_ : Greeting::None;
}

bool greetingIsParty(Greeting g) {
  return g == Greeting::OwnerBirthday || g == Greeting::MibloBirthday || g == Greeting::NewYear ||
         g == Greeting::Christmas;
}

void greetingLines(Lang lang, Greeting g, const char* owner, const char* self, char* line1, size_t cap1, char* line2,
                   size_t cap2) {
  line1[0] = line2[0] = 0;
  const bool named = owner && owner[0];
  // "Good morning" over "Ana"; without a name the phrase itself is the big line.
  auto greet = [&](S phrase) {
    if (named) {
      tr(lang, phrase, line1, cap1);
      snprintf(line2, cap2, "%s", owner);
    } else {
      tr(lang, phrase, line2, cap2);
    }
  };
  switch (g) {
    case Greeting::Named:
      tr(lang, S::HelloIAm, line1, cap1);
      snprintf(line2, cap2, "%s", self);
      break;
    case Greeting::Morning: greet(S::GoodMorning); break;
    case Greeting::Afternoon: greet(S::GoodAfternoon); break;
    case Greeting::Evening: greet(S::GoodEvening); break;
    case Greeting::OwnerBirthday: greet(S::HappyBirthday); break;
    case Greeting::MibloBirthday:
      snprintf(line1, cap1, "%s", self);
      tr(lang, S::MyBirthday, line2, cap2);
      break;
    case Greeting::Christmas:
      if (named) snprintf(line1, cap1, "%s", owner);
      tr(lang, S::MerryChristmas, line2, cap2);
      break;
    case Greeting::NewYear:
      if (named) snprintf(line1, cap1, "%s", owner);
      tr(lang, S::HappyNewYear, line2, cap2);
      break;
    case Greeting::ProgrammersDay:  // Stub (daily-life foundation): track F implements it.
    case Greeting::None: break;
  }
}

// Stub (daily-life foundation): track F implements it.
Date easterSunday(uint16_t year) { return Date{year, 4, 1}; }

// Stub (daily-life foundation): track F implements it.
uint8_t weekdayOf(const Date& d) {
  (void)d;
  return 0;
}

// Stub (daily-life foundation): track F implements it.
bool passerbyAt(uint32_t petMs, uint32_t* atMs) {
  (void)petMs;
  if (atMs) *atMs = 0;
  return false;
}

}  // namespace miblo
