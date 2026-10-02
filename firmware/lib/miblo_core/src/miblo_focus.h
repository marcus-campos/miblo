#pragma once
#include <ArduinoJson.h>
#include <stdint.h>

// Focus (Pomodoro), /miblo:focus: rounds of focus and breaks timed on the gadget (survives the
// bridge going away, not a reboot). Phases: Focus -> Break -> Back (1 min "back to focus?") ->
// Focus ... and after the last round LongBreak -> Off.
namespace miblo {

enum class FocusPhase : uint8_t { Off, Focus, Break, Back, LongBreak };
enum class FocusEvent : uint8_t { None, BreakStarted, BackPrompt, FocusStarted, Finished };

struct FocusPlan {
  uint8_t focusMin = 25;  // 5..120
  uint8_t breakMin = 5;   // 1..60
  uint8_t rounds = 4;     // 1..12
};
constexpr uint8_t kFocusMinMin = 5, kFocusMinMax = 120, kBreakMinMax = 60, kRoundsMax = 12;
constexpr uint32_t kBackMs = 60000;
uint8_t defaultBreakFor(uint8_t focusMin);  // focus / 5, at least 1 (25 -> 5, 50 -> 10)
uint8_t longBreakFor(uint8_t breakMin);     // 3 x break, at most 30 (5 -> 15)

class FocusTimer {
 public:
  void start(const FocusPlan& p, uint32_t nowMs);
  void stop();
  // Every frame. Returns the last transition crossed since the previous call (a long stall may
  // cross several; each phase still starts where the previous one ended, never at nowMs).
  FocusEvent update(uint32_t nowMs);
  FocusPhase phase() const { return phase_; }
  uint8_t round() const { return round_; }  // 1-based
  const FocusPlan& plan() const { return plan_; }
  uint32_t phaseLenMs() const;              // length of the current phase
  uint32_t leftMs(uint32_t nowMs) const;    // left in the current phase (0 when Off)

 private:
  FocusPlan plan_;
  FocusPhase phase_ = FocusPhase::Off;
  uint8_t round_ = 0;
  uint32_t phaseStartMs_ = 0;
};

// POST /api/focus: {"focusMin":25,"breakMin":5,"rounds":4} (each optional; breakMin defaults to
// defaultBreakFor(focusMin)) or {"stop":true}. Returns the HTTP status: 200, or 400 with *bad =
// the field. Starts/stops `t` only on 200. *bad must be a string literal (api.cpp puts it in the
// JSON reply as is, unescaped, after the handler returned).
int focusRequest(FocusTimer& t, JsonObjectConst body, uint32_t nowMs, const char** bad);

}  // namespace miblo
