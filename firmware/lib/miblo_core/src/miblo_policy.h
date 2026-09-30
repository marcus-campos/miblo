#pragma once
#include <stdint.h>

#include "miblo_alerts.h"

namespace miblo {

// ---- Wi-Fi: when to open the setup network ----
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
  HardResetCountdown,  // quick-restarts-left countdown during the first 10 s of a quick boot
  Desk,           // quiet spell: the mascot playing with the limits
  Roam,           // long idle, screen left on: the mascot wanders around the screen (pet mode)
  UpdateAvailable,  // right after boot: a newer firmware was released (for kShowMs)
  LimitReset,     // the 5h window just reset after real use: "limit freed"
  Summary         // quiet spell: today's responses, time worked and cost
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
  bool limitReset = false;  // LimitWatch::celebrating()
  bool updateNotice = false;  // UpdateNotice::showing()
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

// ---- Quiet spells: what the main screen becomes when nothing is going on ----
// Quiet = the main screen with no session running or waiting (Overview shows "All done").
// After kAllDoneMs of quiet any mode gives way to the Desk cycle: the mascot with the limits
// (kDeskCatMs), the Limits arc (kDeskArcMs), the mascot again, today's summary (kDeskSummaryMs),
// and around. (Disconnected has its own mascot.)
enum class QuietPhase : uint8_t {
  Busy,     // not quiet: normal screens
  AllDone,  // just went quiet
  Desk,     // the mascot's turn in the Desk cycle
  Arc,      // the Limits arc's turn
  Summary   // today's summary's turn
};

constexpr uint32_t kAllDoneMs = 20000;
constexpr uint32_t kDeskCatMs = 60000;
constexpr uint32_t kDeskArcMs = 15000;
constexpr uint32_t kDeskSummaryMs = 15000;
// Disconnected: the mascot looks around for the computer, then falls asleep after this long.
constexpr uint32_t kAwayNapMs = 600000;

// ---- Screen care: an LCD keeps a ghost of what it shows unchanged for hours ----
// The whole picture moves a little every kShiftEveryMs (see ui::ShiftCanvas), going round
// kShiftSteps positions within 2 px of the original.
constexpr uint32_t kShiftEveryMs = 300000;
constexpr uint8_t kShiftSteps = 9;
// Offset for step `i` (0..kShiftSteps-1): the centre, then a ring around it.
void pixelShift(uint8_t i, int8_t& dx, int8_t& dy);

// Nobody using it: `idleMs` is how long the computer has been away (Disconnected) or everything
// has been quiet (the Desk cycle); 0 on any other screen. After kRoamAfterMs the mascot wanders
// around the screen (pet mode: nothing stays still); after sleepMin minutes (0 = never) the
// panel turns off. Someone opening the gadget's pages, or the plugin looking for it, keeps the
// normal screens up for kInteractionAwakeMs (the address and pairing code are on them).
constexpr uint32_t kRoamAfterMs = 20UL * 60000;
constexpr uint32_t kInteractionAwakeMs = 120000;
bool petMode(uint32_t idleMs, uint32_t sinceInteractionMs);
bool screenAsleep(uint32_t idleMs, uint32_t sinceInteractionMs, uint16_t sleepMin);

// Once per boot, when the first snapshot that names the latest release shows a newer version
// than this firmware, the screen says so for kShowMs, then carries on.
class UpdateNotice {
 public:
  static constexpr uint32_t kShowMs = 5000;
  void observe(const char* latest, const char* current, uint32_t nowMs);
  bool showing(uint32_t nowMs) const { return active_ && nowMs - sinceMs_ < kShowMs; }

 private:
  bool decided_ = false;  // one decision per boot
  bool active_ = false;
  uint32_t sinceMs_ = 0;
};

class QuietClock {
 public:
  // Call on every frame; anything not quiet (activity, an alert, any other screen) restarts the
  // spell. `justFinished`: a session finished moments ago; only then does a new spell open with
  // "All done" (a new idle session, or the computer coming back, goes straight to the Desk
  // cycle). Safe across millis() wrap.
  QuietPhase update(bool quiet, uint32_t nowMs, bool justFinished = true);
  QuietPhase phase() const { return phase_; }
  // How long it has been quiet (0 while busy).
  uint32_t quietMs(uint32_t nowMs) const { return phase_ == QuietPhase::Busy ? 0 : nowMs - sinceMs_; }

 private:
  QuietPhase phase_ = QuietPhase::Busy;
  uint32_t sinceMs_ = 0;
  uint32_t deskMs_ = 0;  // when the Desk cycle started
};

}  // namespace miblo
