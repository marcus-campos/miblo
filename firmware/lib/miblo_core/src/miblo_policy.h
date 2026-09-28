#pragma once
#include <stdint.h>

#include "miblo_alerts.h"

namespace miblo {

// ---- Wi-Fi: when to open the setup network (spec §6) ----
// JoinFailed: a network submitted from the setup page was given up on; failure() says why.
enum class NetState : uint8_t { Connecting, Connected, Portal, WrongPassword, JoinFailed };
enum class LinkStatus : uint8_t { Down, Connected, WrongPassword };

// Why a portal trial was given up on (derived from the SDK's station disconnect reasons).
enum class JoinFailure : uint8_t {
  None,
  NotFound,  // the SSID was never seen: out of range or 5 GHz only (the ESP8266 is 2.4 GHz only)
  Refused,   // auth/association/handshake rejected: wrong password or WPA3 / PMF-required security
  Other,     // any other disconnect reason (failureCode() has it)
  Timeout    // no connection within kFallbackMs and no disconnect reason ever reported
};

// Station disconnect reason (WIFI_DISCONNECT_REASON_*, 802.11 + Espressif 200..204) -> class.
JoinFailure classifyDisconnect(uint8_t reason);

class NetPolicy {
 public:
  static constexpr uint32_t kFallbackMs = 120000;   // 2 min with no connection -> open the setup network
  static constexpr uint32_t kTrialRetryMs = 10000;  // trial: re-issue the join after this long with no Wi-Fi event
  static constexpr uint32_t kSameReasonMs = 20000;  // trial: the same failure for this long -> give up with it
  static constexpr uint8_t kRefusedRepeats = 3;     // trial: this many rejections in a row -> give up at once
  static constexpr uint32_t kApLingerMs = 30000;    // setup network kept up after a trial succeeds (phone sees it)

  void begin(bool hasCredentials, uint32_t nowMs);
  // A network was submitted from the setup page and WiFi.begin() was just issued for it.
  void credentialsSubmitted(uint32_t nowMs);
  // The station reported a disconnect (connection attempt failed or link lost).
  void disconnected(uint8_t reason, uint32_t nowMs);
  // The station associated (it may still be waiting for DHCP).
  void associated(uint32_t nowMs);
  NetState update(LinkStatus link, uint32_t nowMs);
  NetState state() const { return state_; }
  // Should the setup network (AP) be up? Connecting to the saved network keeps being retried.
  bool apWanted() const { return ap_; }
  // A submitted network is being tried (ends on success, wrong password, failure or timeout).
  bool trialActive() const { return trial_; }
  // During a trial: true when WiFi.begin() should be issued again (nothing heard for kTrialRetryMs).
  bool trialRetryDue(uint32_t nowMs);
  JoinFailure failure() const { return failure_; }
  uint8_t failureCode() const { return failureCode_; }
  uint8_t lastReason() const { return lastReason_; }

  // The SDK's own auto-reconnect is paused while a phone is on the setup network (it steals
  // airtime from the portal), except during a trial: then retrying is the whole point.
  static bool autoReconnectWanted(bool apHasStations, bool trialActive) { return trialActive || !apHasStations; }

 private:
  void failTrial(JoinFailure why, uint8_t code);

  NetState state_ = NetState::Connecting;
  bool ap_ = false;
  bool trial_ = false;
  bool linger_ = false;  // connected by a trial: keep the AP up until kApLingerMs
  uint32_t sinceMs_ = 0;
  uint32_t lastEventMs_ = 0;
  uint32_t connectedAtMs_ = 0;
  uint8_t lastReason_ = 0;
  uint8_t sameCount_ = 0;
  uint32_t sameSinceMs_ = 0;
  JoinFailure failure_ = JoinFailure::None;
  uint8_t failureCode_ = 0;
};

// Value of "state" in GET /api/wifi-status. `busy`: a submission is queued or being tried.
const char* joinStatusName(NetState state, JoinFailure failure, bool busy);

// ---- Which screen to show ----
enum class ScreenId : uint8_t {
  Boot,           // mascot + "Connecting to Wi-Fi"
  Setup,          // QR + setup network name
  WrongPassword,  // Setup with "Wrong password"
  JoinFailed,     // Setup with the reason the submitted network failed (not found, refused, code)
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

// ---- Optional Overview/Limits rotation ----
// With rotation on (and the device in Overview mode), Limits takes over the main screen for
// `showMs` once every `everyMs` (period between two Limits slots; showMs < everyMs), so Overview
// stays up for everyMs - showMs in between. See rotationTiming() in miblo_config.h.
struct RotationTiming {
  bool enabled = false;
  uint32_t everyMs = 60000;
  uint32_t showMs = 10000;
};

class RotationClock {
 public:
  // Call on every frame. `blocked`: something needs the user or the main screen is not showing
  // (an alert flash/hero, a session waiting on a permission/question, any other screen). While
  // blocked it never rotates away, and a Limits slot in progress ends at once; the Overview wait
  // then restarts from the moment it unblocks. A timing change also restarts the cycle.
  // Returns true while Limits should replace Overview. Safe across millis() wrap.
  bool update(const RotationTiming& t, bool blocked, uint32_t nowMs);
  bool showingLimits() const { return limits_; }

 private:
  RotationTiming last_;
  bool started_ = false;
  bool limits_ = false;
  uint32_t sinceMs_ = 0;
};

}  // namespace miblo
