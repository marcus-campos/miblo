#pragma once
#include <stddef.h>
#include <stdint.h>

#include "miblo_snapshot.h"

namespace miblo {

// ---- Adaptive overview (spec §4.1) ----
enum class OverviewKind : uint8_t { Attention, Working, Idle };

struct StateCounts {
  uint8_t pending;  // perm + question
  uint8_t running;
  uint8_t done;
  uint8_t idle;
};

// perm 0, question 1, done 2, running 3, idle 4 (same order as the bridge).
uint8_t stateRank(SessionState st);
StateCounts countStates(const Snapshot& s);
// Attention if there's a pending item; Working if a session is running; otherwise Idle.
OverviewKind classifyOverview(const Snapshot& s);
// Hero: permission > question > finished (if includeDone); tie → smallest `since`. -1 if none.
int selectHero(const Snapshot& s, bool includeDone);
// Most recent `done` session (largest `since`), or -1.
int lastFinished(const Snapshot& s);

// ---- Duration of the last response (the "Finished" hero) ----
// The snapshot only carries `since` for the current state; the gadget remembers when each
// session started working so it can compute how long the response took once it reaches `done`.
class RunTracker {
 public:
  void observe(const Snapshot& s);
  // true if the session finished a response observed from start to end.
  bool stats(const char* sid, uint32_t& durationSec) const;
  void clear();

 private:
  struct Entry {
    char id[9];
    bool used;
    bool active;
    bool finished;
    uint32_t start;
    uint32_t duration;
  };
  Entry entries_[kMaxSessions] = {};
  Entry* find(const char* id);
  const Entry* find(const char* id) const;
};

// ---- Automatic pagination (flip the list every 5 s) ----
class Pager {
 public:
  explicit Pager(uint8_t perPage, uint32_t periodMs = 5000) : perPage_(perPage), periodMs_(periodMs) {}
  uint8_t pageCount(uint16_t itemCount) const;
  // Advances the page based on elapsed time and returns the current page (0-based).
  uint8_t update(uint16_t itemCount, uint32_t nowMs);
  uint8_t page() const { return page_; }
  uint8_t perPage() const { return perPage_; }

 private:
  uint8_t perPage_;
  uint32_t periodMs_;
  uint8_t page_ = 0;
  bool started_ = false;
  uint32_t lastFlipMs_ = 0;
};

// ---- Screen region cache (redraw only what changed) ----
constexpr uint32_t kHashSeed = 2166136261u;
uint32_t hashStr(uint32_t h, const char* s);
uint32_t hashInt(uint32_t h, uint32_t v);

class RegionCache {
 public:
  static constexpr uint8_t kRegions = 24;
  // true (and remembers it) if the region's content changed since the last draw.
  bool changed(uint8_t region, uint32_t hash);
  void invalidate();

 private:
  uint32_t hash_[kRegions] = {};
  bool valid_[kRegions] = {};
};

}  // namespace miblo
