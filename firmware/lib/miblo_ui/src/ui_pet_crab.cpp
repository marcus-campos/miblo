// The crab pet (miblo::Pet::Crab): a crab (original, not any project's mascot). See ui_pet.h for the contract.
// Not drawn yet: the cat stands in.
#include "miblo_rom.h"
#include "ui_pet.h"

namespace screens {

const PetDef kPetCrab MIBLO_ROM = {catHead, catFront, MIBLO_CAT_ANCHORS, MIBLO_CAT_COLORS};

}  // namespace screens
