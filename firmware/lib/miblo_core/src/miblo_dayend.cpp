#include "miblo_dayend.h"

namespace miblo {

// Stub (daily-life foundation): track C implements it.
bool workDay(const Config& cfg, uint8_t weekday) {
  (void)cfg;
  (void)weekday;
  return false;
}

// Stub (daily-life foundation): track C implements it.
bool inWorkHours(const Config& cfg, uint8_t weekday, int minute) {
  (void)cfg;
  (void)weekday;
  (void)minute;
  return false;
}

// Stub (daily-life foundation): track C implements it.
void EndOfDay::update(uint32_t nowMs, const Config& cfg, uint32_t dayKey, uint8_t weekday, int minute, bool running,
                      bool allowed) {
  (void)nowMs;
  (void)cfg;
  (void)dayKey;
  (void)weekday;
  (void)minute;
  (void)running;
  (void)allowed;
}

// Stub (daily-life foundation): track C implements it.
bool EndOfDay::showing(uint32_t nowMs) const {
  (void)nowMs;
  return false;
}

// Stub (daily-life foundation): track C implements it.
uint8_t EndOfDay::petMinutes(const Config& cfg, uint32_t dayKey) const {
  (void)dayKey;
  return cfg.petMin;
}

// Stub (daily-life foundation): track C implements it.
void WeeklyRecap::update(uint32_t nowMs, bool enabled, uint32_t dayKey, uint8_t weekday, int minute, bool active,
                         bool hasWeek, bool allowed) {
  (void)nowMs;
  (void)enabled;
  (void)dayKey;
  (void)weekday;
  (void)minute;
  (void)active;
  (void)hasWeek;
  (void)allowed;
}

// Stub (daily-life foundation): track C implements it.
bool WeeklyRecap::showing(uint32_t nowMs) const {
  (void)nowMs;
  return false;
}

}  // namespace miblo
