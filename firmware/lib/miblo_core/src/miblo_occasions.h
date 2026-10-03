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

enum class Occasion : uint8_t {
  None,
  Halloween,
  Christmas,
  NewYear,
  MibloBirthday,
  OwnerBirthday,
  Valentine,       // Feb 14
  Easter,          // Easter Sunday (easterSunday)
  ProgrammersDay,  // day 256 of the year (Sep 13, Sep 12 in leap years)
  Friday13         // no hat: a black cat crosses pet mode now and then (passerbyAt)
};
// The occasion of a local date, the most personal first: the owner's birthday, the gadget's own
// (a year or more after cfg.born), New Year (Dec 31, Jan 1), Christmas (Dec 20-26), Halloween
// (Oct 29-31). An owner born on Feb 29 celebrates on Feb 28 in other years.
Occasion occasionOn(const Config& cfg, const Date& d);
// The holiday of a date, for everyone (no birthdays): what guests from other Miblos wear here.
Occasion holidayOn(const Date& d);
// Whole years since cfg.born (0 when unknown or not a year yet).
uint16_t mibloAge(const Config& cfg, const Date& d);

enum class Accessory : uint8_t { None, SantaHat, WitchHat, PartyHat, BunnyEars, Glasses, Hearts };
Accessory accessoryFor(Occasion o);

// What the owner dresses the pet in, every day (config accHead, accFace, accNeck): one item per
// slot, 0 = none. The ids are shared by every Miblo (a visiting friend's packet carries them as
// bytes), so they are never renumbered or reused; 13 is never an id (the owner's rule) and is
// refused wherever an id is read.
enum class WearSlot : uint8_t { Head, Face, Neck };
enum class Wear : uint8_t {
  None = 0,
  Cap = 1, Beanie = 2, Beret = 3, TopHat = 4, Crown = 5, CowboyHat = 6, ChefHat = 7, Bandana = 8,
  FlowerCrown = 9, Halo = 10,                                      // head
  Sunglasses = 11, NerdGlasses = 12, Monocle = 14, Moustache = 15,  // face (no 13, ever)
  BowTie = 16, Scarf = 17, Neckerchief = 18, Beads = 19,           // neck
  Headset = 20,                                                     // face: gamer headset with a mic
  Medal = 21,                                                       // neck: "shipped to prod"
};
constexpr uint8_t kWearMax = 21;  // the highest id
// The slot item `id` goes in; false for 0, 13 and ids this firmware does not know.
bool wearSlotOf(uint8_t id, WearSlot& slot);
// `id` may be worn in `slot`: 0 (nothing) or one of the slot's items.
bool wearFits(WearSlot slot, uint8_t id);

// What the pet wears today: the special day's accessory (Accessory) and the owner's items.
// With cfg.occasionHats on, a special day's accessory takes the slot it is worn in: the head
// (hats, bunny ears and the Valentine's hearts floating around it) or, for Programmer's Day's
// glasses, the face; the other slots keep the owner's items. Off, the owner's items always show
// and the special days dress nothing.
struct Outfit {
  Accessory occasion = Accessory::None;
  uint8_t head = 0, face = 0, neck = 0;  // Wear ids
};
Outfit outfitFor(const Config& cfg, Accessory occasion);
// The same rule for any pet (a visiting friend's items from its packet): `occasion` takes its slot;
// an item that does not fit its slot (13, unknown) is nothing.
Outfit outfitWith(Accessory occasion, uint8_t head, uint8_t face, uint8_t neck);

Date easterSunday(uint16_t year);    // Gregorian (anonymous algorithm)
uint8_t weekdayOf(const Date& d);    // 0 = Sunday .. 6
// Friday the 13th: a black cat crosses the screen in pet mode for kPasserbyMs every kPasserbyEveryMs.
constexpr uint32_t kPasserbyEveryMs = 600000, kPasserbyMs = 8000;
bool passerbyAt(uint32_t petMs, uint32_t* atMs);  // true while it is crossing; *atMs = time into it

enum class Greeting : uint8_t {
  None,
  Named,          // the gadget was (re)named: "Hi! I'm Tofu"
  Morning,        // first activity of the day, the owner's name known
  Afternoon,
  Evening,
  OwnerBirthday,  // "Happy birthday, Ana!"
  MibloBirthday,  // "Today is my birthday!"
  Christmas,      // Dec 25
  NewYear,        // Jan 1
  ProgrammersDay  // day 256: "Happy Programmer's Day!"
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
  // The owner's name or birthday changed: today's greeting may come again (at the next activity),
  // so a birthday set on the day itself is still celebrated.
  void rearm() { lastDay_ = 0; }
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
