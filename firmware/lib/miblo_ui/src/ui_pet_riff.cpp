// The Riff pet (miblo::Pet::Riff): an original little rocker, a round creature (not a human) with
// a tall spiky mohawk (the accent colour), a studded collar, a guitar-pick earring, one raised
// eyebrow and a crooked grin. See ui_pet.h for the contract.
// Not drawn yet: the cat stands in.
#include "miblo_rom.h"
#include "ui_pet.h"

namespace screens {

const PetDef kPetRiff MIBLO_ROM = {catHead, catFront, MIBLO_CAT_ANCHORS, MIBLO_CAT_COLORS};

}  // namespace screens
