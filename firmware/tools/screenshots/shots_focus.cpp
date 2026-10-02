// Screenshots of the focus screens (see shots.h): the focus (Pomodoro) screen in each phase.
#include "shots.h"

namespace shots {

using miblo::FocusPhase;

static constexpr uint32_t kMin = 60000;

static void focusShot(miblo::Lang L, const char* name, FocusPhase ph, uint8_t round, uint32_t leftMs, uint32_t lenMs,
                      uint32_t ms) {
  const screens::Clock clk = shots::clock();
  Shot s;
  screens::focus(L, clk, ph, round, 4, leftMs, lenMs, ph == FocusPhase::Focus ? gNow + leftMs / 1000 : 0, ms);
  save(s, name);
}

void renderFocus(miblo::Lang L) {
  focusShot(L, "50-focus-focus", FocusPhase::Focus, 2, (18 * 60 + 42) * 1000u, 25 * kMin, 0);
  focusShot(L, "50-focus-break-stretch", FocusPhase::Break, 2, 5 * kMin - 6000, 5 * kMin, 0);  // just after the end-of-focus cue
  focusShot(L, "50-focus-break", FocusPhase::Break, 2, (3 * 60 + 12) * 1000u, 5 * kMin, 0);
  focusShot(L, "50-focus-back", FocusPhase::Back, 2, 42000, miblo::kBackMs, 0);
  focusShot(L, "50-focus-longbreak", FocusPhase::LongBreak, 4, (11 * 60 + 5) * 1000u, 15 * kMin, 0);
}

}  // namespace shots
