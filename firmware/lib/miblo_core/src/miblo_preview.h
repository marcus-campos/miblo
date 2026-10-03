#pragma once
#include <stdint.h>

#include "miblo_config.h"

// The settings page's "Preview on Miblo": the pet as the form has it right now (kind, colours,
// eyes, accessories), unsaved, on the gadget's screen for a few seconds (app.cpp shows it as a
// daily-life screen; an alert ends it at once). Nothing is saved: the person still presses Save.
namespace miblo {

struct PreviewLook {
  uint8_t mascot = 0, pet = 0, petEyes = 0;
  uint32_t petColors[kPetSlots] = {};
  uint8_t accHead = 0, accFace = 0, accNeck = 0;
};

// The look in a POST body: the config's own fields (mascot, pet, petEyes, petColors, accHead,
// accFace, accNeck), validated exactly as a settings patch (applied to a copy of `cfg` made in
// `scratch`: a field left out keeps the saved value; any other config field is validated and
// ignored). `scratch` is the caller's, so the copy need not sit on the 4 KB loop stack next to
// applyConfigPatch's own. false: `*bad` (if not null) names the invalid field.
bool previewFromJson(const Config& cfg, Config& scratch, JsonObjectConst in, PreviewLook& out, const char** bad);

class LookPreview {
 public:
  static constexpr uint32_t kShowMs = 15000;  // how long it stays up
  static constexpr uint32_t kEveryMs = 2000;  // at most one new preview this often
  // Shows `look` from now on; false (nothing changes) within kEveryMs of the last one started.
  bool start(const PreviewLook& look, uint32_t nowMs);
  bool active(uint32_t nowMs) const { return on_ && nowMs - sinceMs_ < kShowMs; }
  void end() { on_ = false; }  // an alert came
  uint32_t elapsed(uint32_t nowMs) const { return nowMs - sinceMs_; }
  const PreviewLook& look() const { return look_; }

 private:
  PreviewLook look_;
  uint32_t sinceMs_ = 0;
  bool on_ = false;
  bool started_ = false;  // one ever started (sinceMs_ means something)
};

}  // namespace miblo
