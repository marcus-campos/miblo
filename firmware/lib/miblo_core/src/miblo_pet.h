#pragma once
#include <stdint.h>

// Which animal the mascot is (config "pet"). The values are stored in the config and sent to
// other Miblos (miblo_friends.h): never renumber them, only add new kinds at the end. A value
// this firmware does not know (a newer friend's pet) is drawn as the cat.
namespace miblo {

enum class Pet : uint8_t {
  Cat = 0,      // Miblo's own: the sphynx cat
  Duck = 1,     // a rubber duck (rubber duck debugging)
  Bug = 2,      // a small cute beetle
  Daemon = 3,   // a little ghost
  Robot = 4,
  Mug = 5,      // a coffee mug with a face
  Penguin = 6,
  Crab = 7,
  Owl = 8,
  Dog = 9,
  Alien = 10,   // an alien: one big eye, antennae
  Riff = 11,    // an original little rocker: spiky mohawk, studded collar, a crooked grin
};
constexpr uint8_t kPetKinds = 12;

// The pet's colours, one slot per part it draws (one set for whichever pet is chosen). Each slot
// is kPetAuto (derived from the preset in "mascot", or from a custom body colour, exactly as
// before custom colours existed) or a custom colour, held as 0xRRGGBB + 1. The config stores them
// as one string, "rrggbb" or "" (Auto) per slot, comma separated: "f55110,,,,,,".
enum PetSlot : uint8_t {
  kSlotBody = 0,    // the body / skin
  kSlotLine = 1,    // outlines and creases
  kSlotDetail = 2,  // the light secondary detail: inner ears, tongue, inside of the mouth
  kSlotNose = 3,    // nose (or beak, snout...)
  kSlotLid = 4,     // the dark lines drawn on the face: closed eyes, mouth
  kSlotEye = 5,     // the eyes (iris)
  kSlotAccent = 6,  // the pet's own extra: antenna tips, patches, a beak, claw tips, lights...
};
constexpr uint8_t kPetSlots = 7;
constexpr uint32_t kPetAuto = 0;
constexpr uint32_t kPetColorMax = 0x1000000;  // 0xFFFFFF + 1 (white)
// Eye shapes (config "petEyes"): every pet draws all three, in every Eyes state.
enum class EyeShape : uint8_t { Round = 0, Big = 1, Sleepy = 2 };  // Big: big and shiny
constexpr uint8_t kEyeShapes = 3;

// A received or stored value, as a known kind (anything else is the cat).
inline uint8_t knownPet(uint8_t v) { return v < kPetKinds ? v : 0; }

}  // namespace miblo
