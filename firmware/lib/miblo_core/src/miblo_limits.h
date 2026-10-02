#pragma once
#include <stdint.h>

#include "miblo_snapshot.h"

namespace miblo {

// Watches the 5-hour window across snapshots:
// - it tells when the window resets after real use (the "limit freed" screen), and
// - it projects when the window runs out at the recent pace (burn rate).
// Times come from the snapshots' `now` (Unix seconds, the computer's clock); the celebration
// is timed with millis().
class LimitWatch {
 public:
  static constexpr uint8_t kResetMinPct = 50;      // a reset is celebrated only after this much use
  static constexpr uint32_t kCelebrateMs = 8000;   // how long the "limit freed" screen stays up
  static constexpr uint32_t kPaceWindowSec = 2700;  // pace measured over the last 45 minutes
  static constexpr uint32_t kPaceMinSec = 300;      // ...but over at least 5 minutes
  static constexpr uint8_t kPaceMinPct = 2;         // ...and at least 2 points of usage

  // Every accepted snapshot.
  void observe(const Snapshot& s, uint32_t nowMs);
  // True while the "limit freed" screen should show.
  bool celebrating(uint32_t nowMs) const;
  // The 5h usage right after the reset (what the celebration shows).
  uint8_t freedPct() const { return freedPct_; }
  // Unix time at which the 5h window reaches 100% at the recent pace; 0 when unknown, not
  // growing, or not before the window resets anyway.
  uint32_t exhaustAt() const { return exhaustAt_; }

 private:
  struct Sample {
    uint32_t t;
    uint8_t pct;
  };
  static constexpr uint8_t kSamples = 8;
  void addSample(uint32_t t, uint8_t pct);
  void project(const UsageWindow& w, uint32_t now);

  bool have_ = false;
  uint8_t lastPct_ = 0;
  uint32_t lastReset_ = 0;
  bool celebrate_ = false;
  uint32_t celebrateMs_ = 0;
  uint8_t freedPct_ = 0;
  uint32_t exhaustAt_ = 0;
  Sample samples_[kSamples] = {};
  uint8_t count_ = 0;
};

// The 5h window's expected exhaustion: the bridge's forecast (h5.eta) when it sent one, else the
// gadget's own projection (older plugin); 0 without a 5h window.
uint32_t etaFor(const Snapshot& s, const LimitWatch& w);

}  // namespace miblo
