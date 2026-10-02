// Screenshots of the alert screens (see shots.h): meeting mode, insistence and the fanfare.
#include "miblo_overview.h"
#include "shots.h"

using miblo::AlertKind;
using miblo::SessionState;

namespace shots {

void renderAlerts(miblo::Lang L) {
  const screens::Clock clk = shots::clock();
  miblo::Pager pager(3, 5000);
  miblo::RunTracker none;

  // Meeting mode: discreet screens with the badge in the corner, alerts without a name.
  screens::setMascotTie(true);
  working();
  {
    Shot s;
    screens::overview(L, snap, pager, 0, clk, true);
    screens::meetingBadge(L);
    save(s, "52-meeting-overview");
  }
  attention();
  {
    Shot s;
    screens::limits(L, snap, clk);
    screens::meetingBadge(L);
    save(s, "52-meeting-limits");
  }
  {
    Shot s;
    screens::sessions(L, snap, pager, 0, clk, true);
    screens::meetingBadge(L);
    save(s, "52-meeting-sessions");
  }
  {
    Shot s;
    screens::desk(L, snap, clk, 0);
    screens::meetingBadge(L);
    save(s, "52-meeting-desk");
  }
  { Shot s; screens::flash(L, AlertKind::Perm, "checkout", 0, 0, true); save(s, "52-meeting-flash"); }
  {
    Shot s;
    screens::hero(L, snap, 0, AlertKind::Perm, true, clk, none, true);
    screens::meetingBadge(L);
    save(s, "52-meeting-hero");
  }
  // "Finished", anonymous: how long it took instead of which session.
  working();
  miblo::RunTracker runs;
  snap.sessions[0].since = gNow - 7 * 60 - 12;
  runs.observe(snap);
  snap.sessions[0].st = SessionState::Done;
  snap.sessions[0].since = gNow - 5;
  runs.observe(snap);
  {
    Shot s;
    screens::hero(L, snap, 0, AlertKind::Done, true, clk, runs, true);
    screens::meetingBadge(L);
    save(s, "52-meeting-finished");
  }
  screens::setMascotTie(false);

  // Insistence: from the 5th reminder of the same wait the flash blinks red.
  attention();
  { Shot s; screens::flash(L, AlertKind::Perm, "checkout", 0, 2); save(s, "52-insist-red"); }

  // Long task fanfare: app-mobile ran for 23 minutes (in a meeting: no name, and the badge).
  { Shot s; screens::fanfare(L, "app-mobile", 23 * 60 + 7, 650); save(s, "53-fanfare"); }
  {
    Shot s;
    screens::fanfare(L, "", 23 * 60 + 7, 650);
    screens::meetingBadge(L);
    save(s, "53-fanfare-meeting");
  }
}

}  // namespace shots
