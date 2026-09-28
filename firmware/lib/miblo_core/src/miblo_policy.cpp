#include "miblo_policy.h"

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
    state_ = NetState::Connected;
    if (linger_ && nowMs - connectedAtMs_ >= kApLingerMs) linger_ = false;
    ap_ = linger_;
    sinceMs_ = nowMs;
    return state_;
  }
  linger_ = false;
  if (link == LinkStatus::WrongPassword) {
    trial_ = false;
    state_ = NetState::WrongPassword;
    ap_ = true;
    return state_;
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
      if (nowMs - sinceMs_ >= kFallbackMs) {
        if (trial_) {
          const JoinFailure why = classifyDisconnect(lastReason_);
          failTrial(why == JoinFailure::None ? JoinFailure::Timeout : why, lastReason_);
        } else {
          state_ = NetState::Portal;
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

}  // namespace miblo
