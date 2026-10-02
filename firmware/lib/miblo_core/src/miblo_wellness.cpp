#include "miblo_wellness.h"

namespace miblo {

uint32_t WellnessClock::showMs() const {
  return shown_ == Nudge::Break   ? kBreakNudgeMs
         : shown_ == Nudge::Water ? kWaterNudgeMs
         : shown_ == Nudge::Eyes  ? (uint32_t)eyesSec_ * 1000u
                                  : 0;
}

// All times are `now - since` in uint32_t: safe across the millis() wrap. A long stall (OTA, a
// big parse) only makes a nudge due once: showing or skipping it restarts its own cycle from now.
void WellnessClock::update(uint32_t nowMs, const Config& cfg, bool working, bool workHours, bool allowed) {
  if (shown_ != Nudge::None && nowMs - shownMs_ >= showMs()) shown_ = Nudge::None;

  // Continuous work: from the first frame with a session running, as long as no gap without one
  // is longer than kWorkGapMs (the gap counts). A longer gap starts everything over.
  if (working) {
    if (!working_ || nowMs - lastWorkMs_ > kWorkGapMs) {
      working_ = true;
      workStartMs_ = eyesFromMs_ = nowMs;
    }
    lastWorkMs_ = nowMs;
  } else if (working_ && nowMs - lastWorkMs_ > kWorkGapMs) {
    working_ = false;
  }
  // Water counts wall-clock time inside the work hours, from entering them (or the last nudge).
  if (workHours && !inHours_) waterFromMs_ = nowMs;
  inHours_ = workHours;

  // What is due now. Break and eyes only while Claude is working (this frame or within a minute):
  // in a gap the person may already be resting, so they wait for work to resume.
  const bool active = working_ && nowMs - lastWorkMs_ < kActiveMs;
  const bool breakDue =
      cfg.breakAfterMin > 0 && active && nowMs - workStartMs_ >= (uint32_t)cfg.breakAfterMin * 60000u;
  const bool waterDue = cfg.waterMin > 0 && inHours_ && nowMs - waterFromMs_ >= (uint32_t)cfg.waterMin * 60000u;
  const bool eyesDue = cfg.eyes && active && nowMs - eyesFromMs_ >= (uint32_t)cfg.eyesEveryMin * 60000u;
  // By priority: Break > Water > Eyes (a lower one stays due for later).
  const Nudge due = breakDue ? Nudge::Break : waterDue ? Nudge::Water : eyesDue ? Nudge::Eyes : Nudge::None;
  // The wait for a gap starts when the first of them came due; a higher one coming due later
  // keeps that time, so nothing waits more than kNudgeWaitMs in all.
  if (due_ == Nudge::None) dueMs_ = nowMs;
  due_ = due;
  if (due_ == Nudge::None || shown_ != Nudge::None) return;  // never over another nudge
  if (allowed) {
    // Shown: its cycle starts over (a break rests the eyes too).
    if (due_ == Nudge::Break) workStartMs_ = eyesFromMs_ = nowMs;
    else if (due_ == Nudge::Water) waterFromMs_ = nowMs;
    else eyesFromMs_ = nowMs;
    shown_ = due_;
    shownMs_ = nowMs;
    eyesSec_ = cfg.eyesSec;
  } else if (nowMs - dueMs_ > kNudgeWaitMs) {
    // Blocked for too long: every nudge due now is skipped and starts over.
    if (breakDue) workStartMs_ = nowMs;
    if (waterDue) waterFromMs_ = nowMs;
    if (eyesDue || breakDue) eyesFromMs_ = nowMs;
  } else {
    return;  // blocked: wait for a gap
  }
  due_ = Nudge::None;
}

Nudge WellnessClock::showing(uint32_t nowMs) const {
  return shown_ != Nudge::None && nowMs - shownMs_ < showMs() ? shown_ : Nudge::None;
}

void WellnessClock::reset() { *this = WellnessClock(); }

}  // namespace miblo
