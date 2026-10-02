// Cues for peripheral vision: the full-screen slow pulse and the status frame.
#include "ui_internal.h"
#include "ui_screens.h"

namespace screens {

void cue(miblo::CueKind kind, uint32_t elapsedMs) {
  // Stub (daily-life foundation): track E draws it.
  (void)kind;
  (void)elapsedMs;
}

void stateFrame(miblo::FrameColor c) {
  // Stub (daily-life foundation): track E draws it.
  (void)c;
}

}  // namespace screens
