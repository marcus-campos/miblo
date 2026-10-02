#pragma once
#include <stdint.h>

#include "miblo_config.h"

// End of the work day (spec 7) and Monday's summary of last week (spec 11).
namespace miblo {

// weekday 0 = Sunday .. 6; minute 0..1439.
bool workDay(const Config& cfg, uint8_t weekday);
bool inWorkHours(const Config& cfg, uint8_t weekday, int minute);

class EndOfDay {
 public:
  static constexpr uint32_t kShowMs = 60000;
  static constexpr uint32_t kMaxWaitMs = 3600000;  // a session still running: wait up to 1 h
  static constexpr uint8_t kEarlyPetMin = 5;       // after it: pet mode after 5 idle minutes
  // Every frame. `dayKey`: year * 400 + month * 32 + day (0 = time unknown: never shows).
  // `running`: a session is running. `allowed`: it may take the screen now.
  void update(uint32_t nowMs, const Config& cfg, uint32_t dayKey, uint8_t weekday, int minute, bool running,
              bool allowed);
  bool showing(uint32_t nowMs) const;
  uint32_t elapsed(uint32_t nowMs) const { return nowMs - shownMs_; }
  // Pet mode delay to use today: kEarlyPetMin once today's summary showed (until the next day),
  // else cfg.petMin.
  uint8_t petMinutes(const Config& cfg, uint32_t dayKey) const;

 private:
  uint32_t doneDay_ = 0, waitDay_ = 0, waitFromMs_ = 0, shownMs_ = 0;
  bool shown_ = false;
};

class WeeklyRecap {
 public:
  static constexpr uint32_t kShowMs = 60000;
  static constexpr int kAt = 9 * 60;  // or the first activity of Monday from 05:00, whichever first
  // Every frame. `hasWeek`: the snapshot carries `week`. `active`: a session is running.
  void update(uint32_t nowMs, bool enabled, uint32_t dayKey, uint8_t weekday, int minute, bool active, bool hasWeek,
              bool allowed);
  bool showing(uint32_t nowMs) const;
  uint32_t elapsed(uint32_t nowMs) const { return nowMs - shownMs_; }

 private:
  uint32_t doneDay_ = 0, shownMs_ = 0;
  bool shown_ = false;
};

}  // namespace miblo
