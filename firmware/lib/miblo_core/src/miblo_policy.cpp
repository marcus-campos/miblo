#include "miblo_policy.h"

#include "miblo_format.h"
#include "miblo_rom.h"

namespace miblo {

JoinFailure classifyDisconnect(uint8_t reason) {
  switch (reason) {
    case 0: return JoinFailure::None;
    case 201: return JoinFailure::NotFound;  // NO_AP_FOUND
    case 2:                                  // AUTH_EXPIRE
    case 202:                                // AUTH_FAIL
    case 203:                                // ASSOC_FAIL
    case 204:                                // HANDSHAKE_TIMEOUT
      return JoinFailure::Refused;
    default:
      // 13..24: 802.11i key negotiation (4-way handshake timeout, MIC failure, AKMP/cipher/RSN
      // capabilities rejected): a wrong password, or security the ESP8266 lacks (WPA3, PMF required).
      return reason >= 13 && reason <= 24 ? JoinFailure::Refused : JoinFailure::Other;
  }
}

void NetPolicy::begin(bool hasCredentials, uint32_t nowMs) {
  sinceMs_ = nowMs;
  trial_ = false;
  if (hasCredentials) {
    state_ = NetState::Connecting;
    ap_ = false;
  } else {
    state_ = NetState::Portal;
    ap_ = true;
  }
}

void NetPolicy::credentialsSubmitted(uint32_t nowMs) {
  state_ = NetState::Connecting;
  sinceMs_ = nowMs;
  lastEventMs_ = nowMs;
  trial_ = true;
  proven_ = false;  // a new network: nothing known about it yet
  lastReason_ = 0;
  sameCount_ = 0;
  failure_ = JoinFailure::None;
  failureCode_ = 0;
  // the AP stays up so the phone can see the result; it drops kApLingerMs after connecting
}

void NetPolicy::disconnected(uint8_t reason, uint32_t nowMs) {
  lastEventMs_ = nowMs;
  if (reason == lastReason_ && sameCount_ > 0) {
    if (sameCount_ < 255) sameCount_++;
  } else {
    lastReason_ = reason;
    sameCount_ = 1;
    sameSinceMs_ = nowMs;
  }
}

void NetPolicy::associated(uint32_t nowMs) {
  lastEventMs_ = nowMs;  // an attempt is progressing (DHCP): not the moment to re-issue the join
}

bool NetPolicy::trialRetryDue(uint32_t nowMs) {
  if (!trial_ || state_ == NetState::Connected || nowMs - lastEventMs_ < kTrialRetryMs) return false;
  lastEventMs_ = nowMs;
  return true;
}

void NetPolicy::failTrial(JoinFailure why, uint8_t code) {
  trial_ = false;
  state_ = NetState::JoinFailed;
  ap_ = true;
  failure_ = why;
  failureCode_ = code;
}

NetState NetPolicy::update(LinkStatus link, uint32_t nowMs) {
  if (link == LinkStatus::Connected) {
    if (state_ != NetState::Connected) {
      connectedAtMs_ = nowMs;
      linger_ = trial_;
    }
    trial_ = false;
    proven_ = true;
    wrongPass_ = false;
    state_ = NetState::Connected;
    if (linger_ && nowMs - connectedAtMs_ >= kApLingerMs) linger_ = false;
    ap_ = linger_;
    sinceMs_ = nowMs;
    return state_;
  }
  linger_ = false;
  // On a network that already connected this boot (and with no trial running), "wrong password" is
  // any failed 4-way handshake, which a working router produces now and then: an outage like any
  // other, below. Otherwise it opens the setup network at once.
  if (link == LinkStatus::WrongPassword) {
    if (proven_ && !trial_) {
      wrongPass_ = true;
    } else {
      trial_ = false;
      state_ = NetState::WrongPassword;
      ap_ = true;
      return state_;
    }
  }
  if (trial_ && sameCount_ >= 2) {
    // Give up early on a failure that keeps repeating: rejections after a few tries, anything
    // else (typically "network not found") once it has lasted kSameReasonMs.
    const JoinFailure why = classifyDisconnect(lastReason_);
    if ((why == JoinFailure::Refused && sameCount_ >= kRefusedRepeats) || nowMs - sameSinceMs_ >= kSameReasonMs) {
      failTrial(why, lastReason_);
      return state_;
    }
  }
  switch (state_) {
    case NetState::Connected:
      state_ = NetState::Connecting;
      sinceMs_ = nowMs;
      break;
    case NetState::Connecting:
      if (nowMs - sinceMs_ >= (proven_ && !trial_ ? kLostFallbackMs : kFallbackMs)) {
        if (trial_) {
          const JoinFailure why = classifyDisconnect(lastReason_);
          failTrial(why == JoinFailure::None ? JoinFailure::Timeout : why, lastReason_);
        } else {
          state_ = wrongPass_ ? NetState::WrongPassword : NetState::Portal;
          ap_ = true;
        }
      }
      break;
    case NetState::Portal:
    case NetState::WrongPassword:
    case NetState::JoinFailed:
      break;
  }
  return state_;
}

const char* joinStatusName(NetState state, JoinFailure failure, bool busy) {
  if (busy) return "connecting";
  switch (state) {
    case NetState::Connecting: return "connecting";
    case NetState::Connected: return "connected";
    case NetState::WrongPassword: return "wrong_password";
    case NetState::Portal: return "idle";
    case NetState::JoinFailed: break;
  }
  switch (failure) {
    case JoinFailure::NotFound: return "not_found";
    case JoinFailure::Refused: return "refused";
    case JoinFailure::Timeout: return "timeout";
    case JoinFailure::None:
    case JoinFailure::Other: break;
  }
  return "failed";
}

ScreenId selectScreen(const ScreenInputs& in) {
  if (in.updating) return ScreenId::Updating;
  if (in.presenceActive) return ScreenId::PresenceCode;
  if (in.hardResetCountdown) return ScreenId::HardResetCountdown;  // over the boot animation
  if (!in.bootAnimDone) return ScreenId::Boot;
  switch (in.net) {
    case NetState::Portal: return ScreenId::Setup;
    case NetState::WrongPassword: return ScreenId::WrongPassword;
    case NetState::JoinFailed: return ScreenId::JoinFailed;
    case NetState::Connecting: return ScreenId::Boot;
    case NetState::Connected: break;
  }
  if (in.pairCodeRequested) return ScreenId::PairCode;
  if (in.justPaired && in.nowMs - in.pairedAtMs < kPairedScreenMs) return ScreenId::Paired;
  if (!in.paired) return ScreenId::Welcome;
  if (!in.hasSnapshot || in.nowMs - in.lastSnapshotMs >= kSnapshotTimeoutMs) return ScreenId::Disconnected;
  if (in.alert == AlertPhase::Flash) return ScreenId::AlertFlash;
  if (in.alert == AlertPhase::Hero) return ScreenId::AlertHero;
  if (in.limitReset) return ScreenId::LimitReset;
  if (in.updateNotice) return ScreenId::UpdateAvailable;
  return ScreenId::Main;
}

bool RotationClock::update(const RotationTiming& t, bool blocked, uint32_t nowMs) {
  const bool changed =
      !started_ || t.enabled != last_.enabled || t.everyMs != last_.everyMs || t.showMs != last_.showMs;
  if (changed || !t.enabled || blocked || t.showMs >= t.everyMs) {
    started_ = true;
    last_ = t;
    limits_ = false;
    sinceMs_ = nowMs;  // the next Limits slot is a full Overview wait away
    return false;
  }
  const uint32_t elapsed = nowMs - sinceMs_;
  if (limits_) {
    if (elapsed >= t.showMs) {
      limits_ = false;
      sinceMs_ = nowMs;
    }
  } else if (elapsed >= t.everyMs - t.showMs) {
    limits_ = true;
    sinceMs_ = nowMs;
  }
  return limits_;
}

QuietPhase QuietClock::update(bool quiet, uint32_t nowMs, bool justFinished) {
  if (!quiet) {
    phase_ = QuietPhase::Busy;
    return phase_;
  }
  if (phase_ == QuietPhase::Busy) {
    sinceMs_ = nowMs;
    if (justFinished) {
      phase_ = QuietPhase::AllDone;
    } else {
      phase_ = QuietPhase::Desk;  // nothing to report as done: straight to the Desk cycle
      deskMs_ = nowMs;
    }
  }
  if (phase_ == QuietPhase::AllDone) {
    if (nowMs - sinceMs_ < kAllDoneMs) return phase_;
    deskMs_ = nowMs;  // from here on only the cycle position matters: safe across millis() wrap
  }
  const uint32_t t = (nowMs - deskMs_) % (2 * kDeskCatMs + kDeskArcMs + kDeskSummaryMs);
  if (t < kDeskCatMs) phase_ = QuietPhase::Desk;
  else if (t < kDeskCatMs + kDeskArcMs) phase_ = QuietPhase::Arc;
  else if (t < 2 * kDeskCatMs + kDeskArcMs) phase_ = QuietPhase::Desk;
  else phase_ = QuietPhase::Summary;
  return phase_;
}

void pixelShift(uint8_t i, int8_t& dx, int8_t& dy) {
  static const int8_t kSteps[kShiftSteps][2] MIBLO_ROM = {{0, 0},  {2, 0},   {2, 2},  {0, 2}, {-2, 2},
                                                {-2, 0}, {-2, -2}, {0, -2}, {2, -2}};
  i %= kShiftSteps;
  dx = (int8_t)mibloRomByte((const char*)&kSteps[i][0]);
  dy = (int8_t)mibloRomByte((const char*)&kSteps[i][1]);
}

void UpdateNotice::observe(const char* latest, const char* current, uint32_t nowMs) {
  if (decided_ || !latest || !latest[0]) return;  // wait for a snapshot that knows the release
  decided_ = true;
  if (compareVersions(latest, current) > 0) {
    active_ = true;
    sinceMs_ = nowMs;
  }
}

bool petMode(uint32_t idleMs, uint32_t sinceInteractionMs, uint8_t petMin) {
  return sinceInteractionMs >= kInteractionAwakeMs && idleMs >= (uint32_t)petMin * 60000;
}

bool PetLatch::update(bool activity, uint32_t idleMs, uint32_t sinceInteractionMs, uint8_t petMin,
                      uint32_t nowMs) {
  if (activity || sinceInteractionMs < kInteractionAwakeMs) {
    on_ = false;
  } else if (!on_ && petMode(idleMs, sinceInteractionMs, petMin)) {
    on_ = true;
    sinceMs_ = nowMs;
  }
  return on_;
}

bool PetLatch::asleep(uint16_t sleepMin, uint8_t petMin, uint32_t nowMs) const {
  if (!on_ || !sleepMin) return false;
  const uint32_t afterMin = sleepMin > petMin ? sleepMin : (uint32_t)petMin + 15;
  return nowMs - sinceMs_ >= (afterMin - petMin) * 60000;
}

}  // namespace miblo
