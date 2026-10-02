// Screenshots of track C's screens (see shots.h): the wellness nudges, the end of the work day and
// Monday's recap of last week.
#include "shots.h"

namespace shots {

void renderDayRhythm(miblo::Lang L) {
  using miblo::Nudge;
  { Shot s; screens::nudge(L, Nudge::Break, 0); save(s, "54-nudge-break"); }
  { Shot s; screens::nudge(L, Nudge::Water, 0); save(s, "54-nudge-water"); }
  { Shot s; screens::nudge(L, Nudge::Eyes, 3000); save(s, "54-nudge-eyes"); }
  idle();
  snap.todayTurns = 47;
  snap.todayWorkSec = 3 * 3600 + 12 * 60;
  snap.todayUsd = 4.2f;
  { Shot s; screens::dayEnd(L, snap, "Marcus", 0); save(s, "55-day-end"); }
  snap.week = {true, 31 * 3600 + 20 * 60, 212, 38.5f, 3};
  { Shot s; screens::weekRecap(L, snap, 0); save(s, "55-week-recap"); }
}

}  // namespace shots
