#pragma once
#include <stdint.h>

#include "miblo_alerts.h"

namespace miblo {

// ---- Wi-Fi: when to open the setup network (spec §6) ----
enum class NetState : uint8_t { Connecting, Connected, Portal, WrongPassword };
enum class LinkStatus : uint8_t { Down, Connected, WrongPassword };

class NetPolicy {
 public:
  static constexpr uint32_t kFallbackMs = 120000;  // 2 min with no connection -> open the setup network
  void begin(bool hasCredentials, uint32_t nowMs);
  void credentialsSubmitted(uint32_t nowMs);
  NetState update(LinkStatus link, uint32_t nowMs);
  NetState state() const { return state_; }
  // Should the setup network (AP) be up? Connecting to the saved network keeps being retried.
  bool apWanted() const { return ap_; }

 private:
  NetState state_ = NetState::Connecting;
  bool ap_ = false;
  uint32_t sinceMs_ = 0;
};

// ---- Which screen to show ----
enum class ScreenId : uint8_t {
  Boot,           // mascot + "Connecting to Wi-Fi"
  Setup,          // QR + setup network name
  WrongPassword,  // Setup with "Wrong password"
  Welcome,        // Wi-Fi connected + command + pairing code + IP
  Paired,         // "Paired with <host>"
  PairCode,       // pairing code requested by the page
  PresenceCode,   // code for update/reset from the browser
  Updating,       // OTA progress bar
  Disconnected,   // clock (no snapshot for 30 s)
  AlertFlash,
  AlertHero,
  Main,           // device mode (Overview, Limits, or Sessions)
  HardResetCountdown  // quick-restarts-left countdown during the first 10 s of a quick boot
};

constexpr uint32_t kPairedScreenMs = 5000;
constexpr uint32_t kSnapshotTimeoutMs = 30000;
constexpr uint32_t kPairCodeScreenMs = 120000;

struct ScreenInputs {
  uint32_t nowMs = 0;
  bool bootAnimDone = false;
  NetState net = NetState::Connecting;
  bool updating = false;
  bool presenceActive = false;
  bool hardResetCountdown = false;  // quick-boot countdown still inside its 10 s window
  bool pairCodeRequested = false;
  bool paired = false;
  bool justPaired = false;
  uint32_t pairedAtMs = 0;
  bool hasSnapshot = false;
  uint32_t lastSnapshotMs = 0;
  AlertPhase alert = AlertPhase::None;
};

ScreenId selectScreen(const ScreenInputs& in);

}  // namespace miblo
