#include "miblo_limits.h"

namespace miblo {

void LimitWatch::observe(const Snapshot& s, uint32_t nowMs) {
  if (!s.hasUsage || !s.h5.present) return;  // keep what we knew: no data is not a reset
  const UsageWindow& w = s.h5;
  if (have_) {
    // A new window: its reset moved at least an hour ahead, or usage fell sharply.
    const bool moved = w.reset && lastReset_ && w.reset >= lastReset_ + 3600;
    const bool fell = w.pct + 30 <= lastPct_;
    if ((moved || fell) && lastPct_ >= kResetMinPct) {
      celebrate_ = true;
      celebrateMs_ = nowMs;
      freedPct_ = w.pct;
    }
    if (moved || fell) count_ = 0;  // the pace of the old window says nothing about the new one
  }
  have_ = true;
  lastPct_ = w.pct;
  lastReset_ = w.reset;
  addSample(s.now, w.pct);
  project(w, s.now);
}

bool LimitWatch::celebrating(uint32_t nowMs) const {
  return celebrate_ && nowMs - celebrateMs_ < kCelebrateMs;
}

void LimitWatch::addSample(uint32_t t, uint8_t pct) {
  if (count_ && samples_[count_ - 1].pct == pct && t - samples_[count_ - 1].t < 300) return;  // nothing new
  if (count_ == kSamples) {
    for (uint8_t i = 1; i < kSamples; i++) samples_[i - 1] = samples_[i];
    count_--;
  }
  samples_[count_++] = {t, pct};
}

void LimitWatch::project(const UsageWindow& w, uint32_t now) {
  exhaustAt_ = 0;
  if (w.pct >= 100 || !now) return;
  // The oldest sample inside the pace window that shows growth.
  for (uint8_t i = 0; i < count_; i++) {
    const Sample& a = samples_[i];
    if (a.t > now || now - a.t > kPaceWindowSec) continue;
    const uint32_t dt = now - a.t;
    if (dt < kPaceMinSec || w.pct < a.pct + kPaceMinPct) return;
    const uint32_t left = (uint32_t)(100 - w.pct) * dt / (uint32_t)(w.pct - a.pct);
    const uint32_t at = now + left;
    if (!w.reset || at < w.reset) exhaustAt_ = at;  // otherwise it resets before running out
    return;
  }
}

}  // namespace miblo
