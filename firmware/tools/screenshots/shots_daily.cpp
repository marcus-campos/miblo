// Screenshots of the overlays shared by every daily-life screen (see shots.h).
#include "shots.h"

namespace shots {

void renderDaily(miblo::Lang L) {
  // A session waiting while a daily screen is up (the screen itself is drawn by its own file; here a
  // blank one): the amber mark with its name and how many more wait, and in meeting mode.
  attention();
  { Shot s; screens::waitingMark(L, "checkout", 2); save(s, "51-waiting-mark"); }
  { Shot s; screens::waitingMark(L, "", 1); save(s, "51-waiting-mark-meeting"); }
}

}  // namespace shots
