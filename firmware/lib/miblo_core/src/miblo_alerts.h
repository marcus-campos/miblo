#pragma once
#include <stdint.h>

#include "miblo_snapshot.h"

namespace miblo {

// One blink of the alert flash: colour then dark (ui kFlashPhaseMs each).
constexpr uint32_t kBlinkMs = 750;

// Alert sequence timings. All configurable from the page/API.
struct AlertTiming {
  bool enabled = true;
  uint32_t flashMs = 1500;  // kBlinkMs per blink: 2 blinks by default
  uint32_t heroPermMs = 10000;   // hero for "needs you" (permission/question)
  uint32_t heroDoneMs = 5000;    // hero for "finished"
  uint32_t reminderMs = 120000;  // 0 = no reminder
};

enum class AlertPhase : uint8_t { None, Flash, Hero };

// Insistence (spec 3): from the 3rd reminder of the same wait twice the blinks and twice the
// hero time; from the 5th, red blinks. Counted per reminded session: back to 0 when it stops
// waiting or another session is reminded; a wait's own (first) alert is always plain.
constexpr uint8_t kInsistLevel1From = 3, kInsistLevel2From = 5;
constexpr uint32_t kFanfareMs = 8000;  // a long task's "finished" stays this long (spec 6)

struct AlertModifiers {
  bool insist = true;       // cfg.insist
  bool quietFlash = false;  // meeting mode: a single blink
  bool holdDone = false;    // focus with cfg.focusQuiet: "finished" alerts wait (still queued)
};

struct AlertView {
  AlertPhase phase;
  AlertKind kind;
  char sid[9];
  uint32_t phaseStartMs;
  uint8_t level;  // insistence step of this alert: 0, 1, 2
};

// Turns snapshots into alerts (flash, then hero). Amber before blue; the oldest wait first (Perm
// before Question within the same second); every `reminderMs` a reminder repeats flash + hero while a session waits.
//
// "Needs you" (Perm/Question) is level-triggered: a wait is (session, its `since`) while the
// session is Perm or Question, and each wait is shown exactly once, read straight from the
// snapshot's session list on every update. A small table remembers the waits already shown. So
// however many sessions wait at once, whatever the focus hold holds, and whether or not the
// bridge's alert record ever reached the gadget (the parser keeps the first 8, the bridge drops
// them after 30 s, a snapshot can be lost), every wait is shown once; the bridge's perm/question
// records are only used for `lastSeenId`. A wait shown is forgotten when its session shows up
// again not waiting or with another `since`; a session missing from a snapshot (an alerts-only
// snapshot, another paired computer's) keeps its entry, and does not cut that wait's flash or
// hero (the screen draws it without the session's name/tool: AlertView.sid not in the snapshot). Known limits, both accepted: a bridge
// restart that gives a still-waiting session a new `since` shows it once more; more than
// kMaxSessions waits across several computers can evict an entry and show that wait again.
// Booting (or turning alerts on) with sessions already waiting shows each of them once.
//
// "Finished" stays id-based: records are deduplicated by `id`, the highest id seen so far per
// sending computer (the snapshot's `host`; up to kMaxHosts, least recently heard evicted; one
// entry for old plugins without `host`), and a seq going backwards starts over only that
// computer's ids (its bridge restarted). One per session, queued (kMaxAlerts) and held while
// `holdDone` (focus), then shown once. A queued or running "finished" survives another
// computer's snapshot (its session is not in it); it is dropped when its session is in the
// snapshot and no longer done, or missing from its own computer's snapshot.
//
// alertName(): the session name the alert started with, so the screen keeps showing it while
// another computer's snapshot (without that row) is up; "" when it started without a row.
class AlertSequencer {
 public:
  void setTiming(const AlertTiming& t);
  const AlertTiming& timing() const { return t_; }
  void setModifiers(const AlertModifiers& m);
  // The hero on screen stays at least `ms` in total (the fanfare). No-op without a hero.
  void extendHero(uint32_t ms);
  // On every accepted snapshot: queues its new "finished" records (ids; perm/question records
  // only advance lastSeenId).
  void ingest(const Snapshot& s, uint32_t nowMs);
  // On every loop iteration: advances the phases and returns what should be on screen; picks the
  // next wait not shown yet straight from `s` (so `s` must be the latest snapshot).
  const AlertView& update(const Snapshot& s, uint32_t nowMs);
  uint32_t lastSeenId() const { return hostN_ ? hosts_[0].maxId : 0; }  // of the last computer heard
  const char* alertName() const { return name_; }
  uint8_t queued() const { return qn_; }  // "finished" alerts queued (or held)
  void clear();

 private:
  AlertTiming t_;
  AlertItem queue_[kMaxAlerts] = {};
  uint8_t qn_ = 0;
  // Per sending computer: its host hash, last seq and highest alert id; [0] = heard last.
  struct HostSeq {
    uint32_t host, lastSeq, maxId;
  };
  static constexpr uint8_t kMaxHosts = 4;
  HostSeq hosts_[kMaxHosts] = {};
  uint8_t hostN_ = 0;
  uint16_t viewHost_ = 0;  // hostTag of the snapshot the running alert started on
  AlertView view_ = {AlertPhase::None, AlertKind::Done, {0}, 0, 0};
  AlertModifiers mods_;
  bool amberShown_ = false;
  uint32_t lastAmberEndMs_ = 0;
  bool pendingObserved_ = false;
  uint32_t pendingSinceMs_ = 0;
  uint8_t reminders_ = 0;     // reminders started for remindSid_'s current wait (insistence)
  char remindSid_[9] = {0};   // the session the reminders are about ("" = none)
  uint32_t flashLenMs_ = 0;   // the running alert's flash length (level, quiet flash)
  uint32_t heroLenMs_ = 0;    // the running alert's hero length (level, extendHero)
  char name_[sizeof(SessionRow::name)] = {0};  // the running alert's session name at its start
  // Waits already shown: (session key, since). A key is the session's 8-hex-digit short id read
  // as a number (exact for every id the bridge sends; other strings are hashed), so presence and
  // identity are exact while an entry costs 8 B instead of 13 (160 B in all, not 260).
  uint32_t shownKey_[kMaxSessions] = {};
  uint32_t shownSince_[kMaxSessions] = {};
  uint8_t shownN_ = 0;

  static bool doneStillValid(const Snapshot& s, const char* sid, uint16_t host);
  HostSeq& hostOf(const Snapshot& s);
  void start(const Snapshot& s, AlertKind kind, const char* sid, uint32_t nowMs, uint8_t level, uint16_t host);
  void finish(uint32_t nowMs);
  uint8_t levelNow() const;
  void removeAt(uint8_t i);
  bool shown(uint32_t key, uint32_t since) const;
  void forgetEndedWaits(const Snapshot& s, const uint32_t* keys);
  void markShown(uint32_t key, uint32_t since, const Snapshot& s, const uint32_t* keys);
  int nextUnshownWait(const Snapshot& s, const uint32_t* keys) const;
  bool onScreenStillValid(const Snapshot& s, const uint32_t* keys) const;
  void sortQueue(const Snapshot& s);
};

}  // namespace miblo
