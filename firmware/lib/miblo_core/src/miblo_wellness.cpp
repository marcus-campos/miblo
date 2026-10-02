#include "miblo_wellness.h"

namespace miblo {

static uint32_t nudgeMs(Nudge n) {
  return n == Nudge::Break ? kBreakNudgeMs : n == Nudge::Water ? kWaterNudgeMs : n == Nudge::Eyes ? kEyesNudgeMs : 0;
}

// All times are `now - since` in uint32_t: safe across the millis() wrap. A long stall (OTA, a
// big parse) only makes a nudge due once: showing or skipping it restarts its own cycle from now.
void WellnessClock::update(uint32_t nowMs, const Config& cfg, bool working, bool workHours, bool allowed) {
  if (shown_ != Nudge::None && nowMs - shownMs_ >= nudgeMs(shown_)) shown_ = Nudge::None;

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

  // What is due now, by priority: Break > Water > Eyes (a lower one stays due for later).
  Nudge due = Nudge::None;
  if (cfg.breakAfterMin > 0 && working_ && nowMs - workStartMs_ >= (uint32_t)cfg.breakAfterMin * 60000u) {
    due = Nudge::Break;
  } else if (cfg.waterMin > 0 && inHours_ && nowMs - waterFromMs_ >= (uint32_t)cfg.waterMin * 60000u) {
    due = Nudge::Water;
  } else if (cfg.eyes && working_ && nowMs - eyesFromMs_ >= kEyesEveryMs) {
    due = Nudge::Eyes;
  }
  if (due != due_) {
    due_ = due;
    dueMs_ = nowMs;  // the wait for a gap starts now
  }
  if (due_ == Nudge::None || shown_ != Nudge::None) return;  // never over another nudge
  if (!allowed && nowMs - dueMs_ <= kNudgeWaitMs) return;    // blocked: wait for a gap
  // Shown, or blocked for too long and skipped: either way its cycle starts over.
  if (due_ == Nudge::Break) {
    workStartMs_ = eyesFromMs_ = nowMs;  // a break rests the eyes too
  } else if (due_ == Nudge::Water) {
    waterFromMs_ = nowMs;
  } else {
    eyesFromMs_ = nowMs;
  }
  if (allowed) {
    shown_ = due_;
    shownMs_ = nowMs;
  }
  due_ = Nudge::None;
}

Nudge WellnessClock::showing(uint32_t nowMs) const {
  return shown_ != Nudge::None && nowMs - shownMs_ < nudgeMs(shown_) ? shown_ : Nudge::None;
}

void WellnessClock::reset() { *this = WellnessClock(); }

}  // namespace miblo
