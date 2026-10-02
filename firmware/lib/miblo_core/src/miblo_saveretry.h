#pragma once
#include <stdint.h>

// When to write a small file (config, pairings, desk notes) to flash: at once when it changes
// (at most once every kMinGapMs), and again after a failed save (no heap for its document at that moment, a short write on a full
// or failing flash) after 1, 2, 4... minutes, at most an hour apart. A change while a failed save
// waits is tried at the latest a minute after that failure, never sooner, so a flash that keeps
// failing is not worn out by a stream of changes (at most one attempt a minute).
namespace miblo {

class SaveRetry {
 public:
  static constexpr uint32_t kFirstRetryMs = 60000;
  static constexpr uint32_t kMaxRetryMs = 3600000;
  // Successful writes are at least this far apart (L1, flash wear): a stream of changes, from a
  // computer looping on the API or anonymous page views, is coalesced into one write every 5 s,
  // which always holds the latest data.
  static constexpr uint32_t kMinGapMs = 5000;

  // The data changed: save it.
  void request(uint32_t nowMs) {
    if (!pending_) {
      dueMs_ = nowMs;
      if (wrote_ && (int32_t)(lastWriteMs_ + kMinGapMs - nowMs) > 0) dueMs_ = lastWriteMs_ + kMinGapMs;
    } else if (!failed_) {
      // already due (at once or after the gap): the coming write holds this change too
    } else {
      const uint32_t soonest = lastFailMs_ + kFirstRetryMs;
      if ((int32_t)(soonest - dueMs_) < 0) dueMs_ = soonest;
    }
    pending_ = true;
  }
  bool pending() const { return pending_; }
  bool due(uint32_t nowMs) const { return pending_ && (int32_t)(nowMs - dueMs_) >= 0; }
  void succeeded() {
    pending_ = failed_ = false;
    waitMs_ = kFirstRetryMs;
  }
  // The same, for a write made at nowMs: the next one waits kMinGapMs.
  void succeeded(uint32_t nowMs) {
    succeeded();
    wrote_ = true;
    lastWriteMs_ = nowMs;
  }
  void failed(uint32_t nowMs) {
    pending_ = failed_ = true;
    lastFailMs_ = nowMs;
    dueMs_ = nowMs + waitMs_;
    waitMs_ = waitMs_ >= kMaxRetryMs / 2 ? kMaxRetryMs : waitMs_ * 2;
  }

 private:
  bool pending_ = false;
  bool failed_ = false;  // the last attempt failed (and nothing was saved since)
  uint32_t dueMs_ = 0;
  uint32_t lastFailMs_ = 0;
  uint32_t waitMs_ = kFirstRetryMs;
  bool wrote_ = false;
  uint32_t lastWriteMs_ = 0;
};

}  // namespace miblo
