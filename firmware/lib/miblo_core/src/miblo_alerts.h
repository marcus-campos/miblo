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
// hero time; from the 5th, red blinks. Back to 0 when no session waits any more.
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

// Alert queue: deduplicates by `id` (highest id seen so far), amber before blue, and every
// `reminderMs` repeats flash + hero while a session is still pending.
class AlertSequencer {
 public:
  void setTiming(const AlertTiming& t);
  const AlertTiming& timing() const { return t_; }
  void setModifiers(const AlertModifiers& m);
  // The hero on screen stays at least `ms` in total (the fanfare). No-op without a hero.
  void extendHero(uint32_t ms);
  // On every accepted snapshot.
  void ingest(const Snapshot& s, uint32_t nowMs);
  // On every loop iteration: advances the phases and returns what should be on screen.
  const AlertView& update(const Snapshot& s, uint32_t nowMs);
  uint32_t lastSeenId() const { return maxId_; }
  uint8_t queued() const { return qn_; }
  void clear();

 private:
  AlertTiming t_;
  AlertItem queue_[kMaxAlerts] = {};
  uint8_t qn_ = 0;
  uint32_t maxId_ = 0;
  uint32_t lastSeq_ = 0;
  bool haveSeq_ = false;
  AlertView view_ = {AlertPhase::None, AlertKind::Done, {0}, 0, 0};
  AlertModifiers mods_;
  bool amberShown_ = false;
  uint32_t lastAmberEndMs_ = 0;
  bool pendingObserved_ = false;
  uint32_t pendingSinceMs_ = 0;
  uint8_t reminders_ = 0;     // reminders started during the current wait (insistence)
  uint32_t flashLenMs_ = 0;   // the running alert's flash length (level, quiet flash)
  uint32_t heroLenMs_ = 0;    // the running alert's hero length (level, extendHero)

  static bool stillValid(const Snapshot& s, AlertKind kind, const char* sid);
  void start(AlertKind kind, const char* sid, uint32_t nowMs, uint8_t level);
  void finish(uint32_t nowMs);
  uint8_t levelNow() const;
  void sortQueue(const Snapshot& s);
};

}  // namespace miblo
