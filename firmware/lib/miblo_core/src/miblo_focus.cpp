#include "miblo_focus.h"

namespace miblo {

// Stub (daily-life foundation): track A implements it.
uint8_t defaultBreakFor(uint8_t focusMin) {
  (void)focusMin;
  return 5;
}

// Stub (daily-life foundation): track A implements it.
uint8_t longBreakFor(uint8_t breakMin) {
  (void)breakMin;
  return 15;
}

// Stub (daily-life foundation): track A implements it.
void FocusTimer::start(const FocusPlan& p, uint32_t nowMs) {
  (void)p;
  (void)nowMs;
}

// Stub (daily-life foundation): track A implements it.
void FocusTimer::stop() {}

// Stub (daily-life foundation): track A implements it.
FocusEvent FocusTimer::update(uint32_t nowMs) {
  (void)nowMs;
  return FocusEvent::None;
}

// Stub (daily-life foundation): track A implements it.
uint32_t FocusTimer::phaseLenMs() const { return 0; }

// Stub (daily-life foundation): track A implements it.
uint32_t FocusTimer::leftMs(uint32_t nowMs) const {
  (void)nowMs;
  return 0;
}

// Stub (daily-life foundation): track A implements it.
int focusRequest(FocusTimer& t, JsonObjectConst body, uint32_t nowMs, const char** bad) {
  (void)t;
  (void)body;
  (void)nowMs;
  if (bad) *bad = "focus";
  return 400;
}

}  // namespace miblo
