#include "miblo_dayend.h"

namespace miblo {

bool workDay(const Config& cfg, uint8_t weekday) { return weekday < 7 && (cfg.workDays >> weekday) & 1; }

bool inWorkHours(const Config& cfg, uint8_t weekday, int minute) {
  return minute >= 0 && workDay(cfg, weekday) && minute >= cfg.workFrom && minute < cfg.workTo;
}

// Once a day, in the kWindowMin after the end of the work hours (time known, a work day, the
// day's stats known): a restart later in the evening or plugging the gadget in at night does not
// bring it back. Something more important on screen (`allowed` false) holds it until a gap. A
// session still running then holds it up to kMaxWaitMs, counted from the first frame it was
// allowed; then it shows anyway. Still held when the window closes: skipped that day.
void EndOfDay::update(uint32_t nowMs, const Config& cfg, uint32_t dayKey, uint8_t weekday, int minute, bool running,
                      bool known, bool allowed) {
  if (shown_ && (!cfg.endOfDay || nowMs - shownMs_ >= kShowMs)) shown_ = false;
  if (!cfg.endOfDay || dayKey == 0 || doneDay_ == dayKey || !workDay(cfg, weekday) || minute < cfg.workTo ||
      minute >= cfg.workTo + kWindowMin)
    return;
  if (!known || !allowed) return;
  if (waitDay_ != dayKey) {
    waitDay_ = dayKey;
    waitFromMs_ = nowMs;
  }
  if (running && nowMs - waitFromMs_ < kMaxWaitMs) return;
  doneDay_ = dayKey;
  shown_ = true;
  shownMs_ = nowMs;
}

bool EndOfDay::showing(uint32_t nowMs) const { return shown_ && nowMs - shownMs_ < kShowMs; }

uint8_t EndOfDay::petMinutes(const Config& cfg, uint32_t dayKey) const {
  return dayKey != 0 && doneDay_ == dayKey && cfg.petMin > kEarlyPetMin ? kEarlyPetMin : cfg.petMin;
}

// Mondays (the bridge sends `week` only then): at the first activity from 05:00, or at kAt,
// whichever comes first, before kUntil; once per day.
void WeeklyRecap::update(uint32_t nowMs, bool enabled, uint32_t dayKey, uint8_t weekday, int minute, bool active,
                         bool hasWeek, bool allowed) {
  if (shown_ && (!enabled || nowMs - shownMs_ >= kShowMs)) shown_ = false;
  if (!enabled || dayKey == 0 || weekday != 1 || !hasWeek || doneDay_ == dayKey || !allowed) return;
  if (minute >= kUntil || !(minute >= kAt || (active && minute >= 5 * 60))) return;
  doneDay_ = dayKey;
  shown_ = true;
  shownMs_ = nowMs;
}

bool WeeklyRecap::showing(uint32_t nowMs) const { return shown_ && nowMs - shownMs_ < kShowMs; }

}  // namespace miblo
