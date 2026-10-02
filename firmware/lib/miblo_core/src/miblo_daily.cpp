#include "miblo_daily.h"

namespace miblo {

bool dailyMayReplace(ScreenId s) {
  switch (s) {
    case ScreenId::Main:
    case ScreenId::Desk:
    case ScreenId::Summary:
    case ScreenId::Disconnected:
    case ScreenId::Roam:
    case ScreenId::Visit:
    case ScreenId::Hello:
    case ScreenId::LimitReset:
    case ScreenId::UpdateAvailable: return true;
    default: return false;
  }
}

// The screens where the cat may hold the /miblo:say note full screen (in pet mode it rides on
// the sign instead, and a visit or greeting keeps its own screen).
static bool sayScreen(ScreenId s) {
  return s == ScreenId::Main || s == ScreenId::Desk || s == ScreenId::Summary || s == ScreenId::Disconnected;
}

ScreenId dailyScreen(const DailyInputs& in) {
  // A long task's "finished" turns its hero into the fanfare; the flash before it stays.
  if (in.screen == ScreenId::AlertHero && in.fanfare) return ScreenId::Fanfare;
  // Alerts, setup, codes and updates are never covered.
  if (!dailyMayReplace(in.screen)) return in.screen;
  if (in.cue != CueKind::None) return ScreenId::Cue;
  if (in.find) return ScreenId::Find;
  if (in.screen == ScreenId::Hello) return ScreenId::Hello;  // a greeting is short: it goes first
  if (in.held != NoteKind::None) return ScreenId::Note;
  if (in.focus != FocusPhase::Off) return ScreenId::Focus;
  if (in.timer) return ScreenId::Timer;
  if (in.dayEnd) return ScreenId::DayEnd;
  if (in.weekRecap) return ScreenId::WeekRecap;
  // Wellness never interrupts pet mode or a visit.
  if (in.nudge != Nudge::None && in.screen != ScreenId::Roam && in.screen != ScreenId::Visit) return ScreenId::Nudge;
  if (in.say && sayScreen(in.screen)) return ScreenId::Note;
  if (in.passerby && in.screen == ScreenId::Roam) return ScreenId::Passerby;
  return in.screen;
}

bool dailyActivity(const DailyInputs& in) {
  return in.focus != FocusPhase::Off || in.timer || in.held != NoteKind::None || in.cue != CueKind::None || in.find;
}

}  // namespace miblo
