#include "miblo_preview.h"

#include <string.h>

namespace miblo {

bool previewFromJson(const Config& cfg, JsonObjectConst in, PreviewLook& out, const char** bad) {
  Config c = cfg;  // the validators are the settings patch's own; the copy is thrown away
  if (!applyConfigPatch(c, in, bad)) return false;
  out.mascot = c.mascot;
  out.pet = c.pet;
  out.petEyes = c.petEyes;
  memcpy(out.petColors, c.petColors, sizeof(out.petColors));
  out.accHead = c.accHead;
  out.accFace = c.accFace;
  out.accNeck = c.accNeck;
  return true;
}

bool LookPreview::start(const PreviewLook& look, uint32_t nowMs) {
  if (started_ && nowMs - sinceMs_ < kEveryMs) return false;
  look_ = look;
  sinceMs_ = nowMs;
  on_ = started_ = true;
  return true;
}

}  // namespace miblo
