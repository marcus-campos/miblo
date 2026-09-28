#include "miblo_policy.h"

namespace miblo {

void NetPolicy::begin(bool hasCredentials, uint32_t nowMs) {
  sinceMs_ = nowMs;
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
  // o AP continua no ar para o celular ver o resultado; cai quando conectar
}

NetState NetPolicy::update(LinkStatus link, uint32_t nowMs) {
  if (link == LinkStatus::Connected) {
    state_ = NetState::Connected;
    ap_ = false;
    sinceMs_ = nowMs;
    return state_;
  }
  if (link == LinkStatus::WrongPassword) {
    state_ = NetState::WrongPassword;
    ap_ = true;
    return state_;
  }
  switch (state_) {
    case NetState::Connected:
      state_ = NetState::Connecting;
      sinceMs_ = nowMs;
      break;
    case NetState::Connecting:
      if (nowMs - sinceMs_ >= kFallbackMs) {
        state_ = NetState::Portal;
        ap_ = true;
      }
      break;
    case NetState::Portal:
    case NetState::WrongPassword:
      break;
  }
  return state_;
}

ScreenId selectScreen(const ScreenInputs& in) {
  if (in.updating) return ScreenId::Updating;
  if (in.presenceActive) return ScreenId::PresenceCode;
  if (!in.bootAnimDone) return ScreenId::Boot;
  switch (in.net) {
    case NetState::Portal: return ScreenId::Setup;
    case NetState::WrongPassword: return ScreenId::WrongPassword;
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

}  // namespace miblo
