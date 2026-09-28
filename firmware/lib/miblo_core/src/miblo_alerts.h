#pragma once
#include <stdint.h>

#include "miblo_snapshot.h"

namespace miblo {

// Alert sequence timings. All configurable from the page/API.
struct AlertTiming {
  bool enabled = true;
  uint32_t flashMs = 1500;
  uint32_t heroPermMs = 10000;   // hero for "needs you" (permission/question)
  uint32_t heroDoneMs = 5000;    // hero for "finished"
  uint32_t reminderMs = 120000;  // 0 = no reminder
};

enum class AlertPhase : uint8_t { None, Flash, Hero };

struct AlertView {
  AlertPhase phase;
  AlertKind kind;
  char sid[9];
  uint32_t phaseStartMs;
};

// Alert queue: deduplicates by `id` (highest id seen so far), amber before blue, and every
// `reminderMs` repeats flash + hero while a session is still pending.
class AlertSequencer {
 public:
  void setTiming(const AlertTiming& t);
  const AlertTiming& timing() const { return t_; }
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
  AlertView view_ = {AlertPhase::None, AlertKind::Done, {0}, 0};
  bool amberShown_ = false;
  uint32_t lastAmberEndMs_ = 0;
  bool pendingObserved_ = false;
  uint32_t pendingSinceMs_ = 0;

  static bool stillValid(const Snapshot& s, AlertKind kind, const char* sid);
  void start(AlertKind kind, const char* sid, uint32_t nowMs);
  void finish(uint32_t nowMs);
  void sortQueue(const Snapshot& s);
};

}  // namespace miblo
