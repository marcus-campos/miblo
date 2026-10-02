// Screenshots of track E's screens (see shots.h): the strong cue's pulse and the status frame.
#include "miblo_cues.h"
#include "miblo_overview.h"
#include "shots.h"

namespace shots {

void renderCues(miblo::Lang L) {
  using miblo::CueKind;
  const uint32_t peak = miblo::kCuePulseMs / 2;  // the top of the first pulse
  { Shot s; screens::cue(CueKind::Timer, peak); save(s, "57-cue-timer-peak"); }
  { Shot s; screens::cue(CueKind::FocusEnd, peak / 2); save(s, "57-cue-focus-rising"); }
  { Shot s; screens::cue(CueKind::Reminder, peak); save(s, "57-cue-reminder-peak"); }
  // With the blue light filter at full strength (night): the pulse is warmed like everything else.
  { Shot s(3); screens::cue(CueKind::FocusEnd, peak); save(s, "57-cue-focus-peak-warm"); }

  // The status frame drawn over a screen: amber while a session waits, green after a finish.
  miblo::Pager pager(3, 5000);
  attention();
  {
    Shot s;
    screens::overview(L, snap, pager, 0, shots::clock(), false);
    screens::stateFrame(miblo::FrameColor::Amber);
    save(s, "57-frame-amber-overview");
  }
  idle();
  {
    Shot s;
    screens::desk(L, snap, shots::clock(), 0);
    screens::stateFrame(miblo::FrameColor::Green);
    save(s, "57-frame-green-desk");
  }
}

}  // namespace shots
