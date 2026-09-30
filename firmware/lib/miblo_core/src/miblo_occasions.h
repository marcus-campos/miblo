#pragma once
#include <stdint.h>

#include "miblo_config.h"

// Special days: the mascot dresses up (a hat on every mascot, all day) and, on the first activity
// of the day, greets its owner by name ("Good morning, Ana!", "Happy birthday, Ana!").
namespace miblo {

struct Date {
  uint16_t year;
  uint8_t month;  // 1..12
  uint8_t day;    // 1..31
};

enum class Occasion : uint8_t { None, Halloween, Christmas, NewYear, MibloBirthday, OwnerBirthday };
// The occasion of a local date, the most personal first: the owner's birthday, the gadget's own
// (a year or more after cfg.born), New Year (Dec 31, Jan 1), Christmas (Dec 20-26), Halloween
// (Oct 29-31). An owner born on Feb 29 celebrates on Feb 28 in other years.
Occasion occasionOn(const Config& cfg, const Date& d);
// Whole years since cfg.born (0 when unknown or not a year yet).
uint16_t mibloAge(const Config& cfg, const Date& d);

enum class Accessory : uint8_t { None, SantaHat, WitchHat, PartyHat };
Accessory accessoryFor(Occasion o);

enum class Greeting : uint8_t {
  None,
  Named,          // the gadget was (re)named: "Hi! I'm Tofu"
  Morning,        // first activity of the day, the owner's name known
  Afternoon,
  Evening,
  OwnerBirthday,  // "Happy birthday, Ana!"
  MibloBirthday,  // "Today is my birthday!"
  Christmas,      // Dec 25
  NewYear         // Jan 1
};

// Decides when the greeting screen comes up. Daily greetings wait for the first activity of the
// day (a session running, from 05:00 local time on) and show once per day.
class Greeter {
 public:
  static constexpr uint32_t kShowMs = 6000;
  static constexpr uint32_t kPartyMs = 10000;  // birthdays and holidays last longer
  static constexpr int kDayStartsAt = 5 * 60;  // minute of the day

  // The gadget's name changed (not at boot): "Hi! I'm <name>" right away.
  void named(uint32_t nowMs);
  // Every frame. `active`: a session is running and nothing more urgent is on screen.
  // `today`/`minuteOfDay`: local, only meaningful when `timeKnown`.
  void update(uint32_t nowMs, bool active, bool timeKnown, const Date& today, int minuteOfDay, const Config& cfg);
  Greeting showing(uint32_t nowMs) const;
  // How long the current greeting has been up.
  uint32_t elapsed(uint32_t nowMs) const { return nowMs - sinceMs_; }

 private:
  void start(Greeting g, uint32_t nowMs);
  Greeting kind_ = Greeting::None;
  uint32_t sinceMs_ = 0;
  uint32_t lastDay_ = 0;  // year * 400 + month * 32 + day of the last daily greeting
};

// A greeting's two lines: `line1` (small, may be empty) and `line2` (big), in `lang`. `owner` and
// `self` are the owner's and the gadget's names.
void greetingLines(Lang lang, Greeting g, const char* owner, const char* self, char* line1, size_t cap1, char* line2,
                   size_t cap2);
// Confetti on screen for these.
bool greetingIsParty(Greeting g);

}  // namespace miblo
