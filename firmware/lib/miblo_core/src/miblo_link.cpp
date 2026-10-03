#include "miblo_link.h"

namespace miblo {

void LinkKeeper::begin(uint32_t nowMs) {
  badSinceMs_ = nowMs;
  lastActMs_ = nowMs;
}

void LinkKeeper::confirmed() {
  good_ = true;
  proven_ = true;
  dead_ = false;
  fails_ = 0;
  waitMs_ = kDownMs;
}

LinkAction LinkKeeper::update(uint32_t nowMs, bool linkUp, bool mayAct, bool quiet) {
  if (!linkUp) {
    if (wasUp_) {
      wasUp_ = false;
      probing_ = false;
      fails_ = 0;
      dead_ = false;  // down now: the down rules apply
    }
    if (good_) {
      good_ = false;
      badSinceMs_ = nowMs;
      lastActMs_ = nowMs;
      waitMs_ = kDownMs;
    }
  } else {
    if (!wasUp_) {
      wasUp_ = true;
      confirmPending_ = true;
      fails_ = 0;
    }
    if (!quiet) confirmed();  // the computer is getting through: the link works
  }

  if (!mayAct) {
    paused_ = true;
    probing_ = false;
    return LinkAction::None;
  }
  if (paused_) {
    paused_ = false;
    lastActMs_ = nowMs;  // a fresh wait: the next cycle is waitMs_ away
  }

  if (probing_) {
    if (nowMs - probeAtMs_ < kProbeWaitMs) return LinkAction::None;
    probing_ = false;
    return LinkAction::ProbeCheck;
  }
  if (linkUp && !dead_ &&
      (confirmPending_ || (quiet && nowMs - probeAtMs_ >= (fails_ ? kProbeRetryMs : kProbeEveryMs)))) {
    confirmPending_ = false;
    probing_ = true;
    probeAtMs_ = nowMs;
    return LinkAction::Probe;
  }

  if (good_ || (linkUp && !dead_)) return LinkAction::None;
  if (proven_ && !restarted_ && nowMs - badSinceMs_ >= kRestartMs) {
    restarted_ = true;
    return LinkAction::Restart;
  }
  if (nowMs - lastActMs_ < waitMs_) return LinkAction::None;
  lastActMs_ = nowMs;
  waitMs_ = waitMs_ >= kMaxWaitMs / 2 ? kMaxWaitMs : waitMs_ * 2;
  dead_ = false;  // the cycle drops the link: judged afresh once it is up again
  fails_ = 0;
  reconnects_++;
  return LinkAction::Reconnect;
}

void LinkKeeper::probeAnswered(bool answered, uint32_t nowMs) {
  if (answered) {
    arpSeen_ = true;
    confirmed();
    return;
  }
  if (fails_ < 255) fails_++;
  if (!arpSeen_ || fails_ < kDeadAfter || dead_) return;
  dead_ = true;
  deadLinks_++;
  if (good_) {
    // It worked until now: cycle at once (the probes already took their time), then back off.
    good_ = false;
    badSinceMs_ = nowMs;
    waitMs_ = kDownMs;
    lastActMs_ = nowMs - kDownMs;
  }
}

}  // namespace miblo
