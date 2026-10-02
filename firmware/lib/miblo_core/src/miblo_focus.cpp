#include "miblo_focus.h"

namespace miblo {

static constexpr uint32_t kMinuteMs = 60000;

uint8_t defaultBreakFor(uint8_t focusMin) {
  const uint8_t b = focusMin / 5;
  return b < 1 ? 1 : b;
}

uint8_t longBreakFor(uint8_t breakMin) {
  const unsigned b = 3u * breakMin;
  return b > 30 ? 30 : (uint8_t)b;
}

void FocusTimer::start(const FocusPlan& p, uint32_t nowMs) {
  plan_ = p;
  if (plan_.rounds < 1) plan_.rounds = 1;
  phase_ = FocusPhase::Focus;
  round_ = 1;
  phaseStartMs_ = nowMs;
}

void FocusTimer::stop() {
  phase_ = FocusPhase::Off;
  round_ = 0;
}

// How much an event matters to the screen (the cue it fires): a stall that crosses several
// transitions reports the strongest one, so the end-of-focus cue is never lost.
static uint8_t eventRank(FocusEvent e) {
  switch (e) {
    case FocusEvent::Finished: return 4;
    case FocusEvent::BreakStarted: return 3;
    case FocusEvent::BackPrompt: return 2;
    case FocusEvent::FocusStarted: return 1;
    case FocusEvent::None: break;
  }
  return 0;
}

// Advances phase by phase from phaseStartMs_ (never from nowMs), so a stalled loop lands on the
// same schedule; unsigned differences keep it correct across the millis() wrap.
FocusEvent FocusTimer::update(uint32_t nowMs) {
  FocusEvent best = FocusEvent::None;
  while (phase_ != FocusPhase::Off && nowMs - phaseStartMs_ >= phaseLenMs()) {
    phaseStartMs_ += phaseLenMs();
    FocusEvent ev = FocusEvent::None;
    switch (phase_) {
      case FocusPhase::Focus:
        phase_ = round_ < plan_.rounds ? FocusPhase::Break : FocusPhase::LongBreak;
        ev = FocusEvent::BreakStarted;
        break;
      case FocusPhase::Break:
        phase_ = FocusPhase::Back;
        ev = FocusEvent::BackPrompt;
        break;
      case FocusPhase::Back:
        phase_ = FocusPhase::Focus;
        round_++;
        ev = FocusEvent::FocusStarted;
        break;
      case FocusPhase::LongBreak:
        phase_ = FocusPhase::Off;
        round_ = 0;
        ev = FocusEvent::Finished;
        break;
      case FocusPhase::Off:
        break;
    }
    if (eventRank(ev) > eventRank(best)) best = ev;
  }
  return best;
}

uint32_t FocusTimer::phaseLenMs() const {
  switch (phase_) {
    case FocusPhase::Focus: return plan_.focusMin * kMinuteMs;
    case FocusPhase::Break: return plan_.breakMin * kMinuteMs;
    case FocusPhase::Back: return kBackMs;
    case FocusPhase::LongBreak: return longBreakFor(plan_.breakMin) * kMinuteMs;
    case FocusPhase::Off: break;
  }
  return 0;
}

uint32_t FocusTimer::leftMs(uint32_t nowMs) const {
  if (phase_ == FocusPhase::Off) return 0;
  const uint32_t len = phaseLenMs(), gone = nowMs - phaseStartMs_;
  return gone >= len ? 0 : len - gone;
}

// Reads an optional integer field in lo..hi into *out. False when present but not such an integer.
static bool optField(JsonObjectConst body, const char* key, int lo, int hi, uint8_t* out) {
  JsonVariantConst v = body[key];
  if (v.isNull()) return true;
  if (!v.is<int>()) return false;
  const int n = v.as<int>();
  if (n < lo || n > hi) return false;
  *out = (uint8_t)n;
  return true;
}

int focusRequest(FocusTimer& t, JsonObjectConst body, uint32_t nowMs, const char** bad) {
  FocusPlan p;
  uint8_t breakMin = 0;
  const char* field = nullptr;
  if (!optField(body, "focusMin", kFocusMinMin, kFocusMinMax, &p.focusMin)) field = "focusMin";
  else if (!optField(body, "breakMin", 1, kBreakMinMax, &breakMin)) field = "breakMin";
  else if (!optField(body, "rounds", 1, kRoundsMax, &p.rounds)) field = "rounds";
  JsonVariantConst stop = body["stop"];
  const bool stopping = !stop.isNull();
  if (!field && stopping && !(stop.is<bool>() && stop.as<bool>())) field = "stop";
  if (field) {
    if (bad) *bad = field;
    return 400;
  }
  if (stopping) {
    t.stop();
    return 200;
  }
  p.breakMin = breakMin ? breakMin : defaultBreakFor(p.focusMin);
  t.start(p, nowMs);
  return 200;
}

}  // namespace miblo
