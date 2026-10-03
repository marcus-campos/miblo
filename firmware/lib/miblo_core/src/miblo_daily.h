#pragma once
#include <stdint.h>

#include "miblo_cues.h"
#include "miblo_desknotes.h"
#include "miblo_focus.h"
#include "miblo_policy.h"
#include "miblo_wellness.h"

// Which daily-life screen, if any, replaces the ordinary one this frame. Pure: app.cpp feeds it
// once the ordinary screen is known (alerts, pet mode, visits and greetings already decided).
namespace miblo {

struct DailyInputs {
  ScreenId screen = ScreenId::Main;
  bool fanfare = false;              // the hero on screen is a long task's "finished"
  CueKind cue = CueKind::None;       // StrongCue::active()
  bool find = false;
  NoteKind held = NoteKind::None;    // a reminder/alarm/timer text the cat is holding
  FocusPhase focus = FocusPhase::Off;
  bool timer = false;
  bool dayEnd = false;
  bool weekRecap = false;
  Nudge nudge = Nudge::None;
  bool say = false;
  bool passerby = false;             // Friday 13 in pet mode, the black cat crossing now
  bool preview = false;              // the settings page's preview (LookPreview::active)
};

// Ordinary screens a daily-life screen may take over (never setup, codes, updates or alerts).
bool dailyMayReplace(ScreenId s);
ScreenId dailyScreen(const DailyInputs& in);
// Counts as someone at the desk for pet mode and the panel's sleep: focus (any phase), a timer,
// a held text, a cue, find, the settings page's preview.
bool dailyActivity(const DailyInputs& in);
// The daily-life screens that take the whole screen for a while: Focus, Timer, Note, Find, Nudge,
// DayEnd, WeekRecap, Preview (not the short Cue pulse, the Fanfare, which is an alert, or
// Passerby, pet mode).
bool dailyFullScreen(ScreenId s);
// A session waiting for you ("needs you") is never hidden by daily life: on a daily full screen
// app.cpp draws screens::waitingMark() while `pending` > 0.
bool waitingMarkOn(ScreenId s, uint8_t pending);

}  // namespace miblo
