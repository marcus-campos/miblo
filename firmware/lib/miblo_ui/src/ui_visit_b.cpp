// ui_visit_b.cpp — visits between Miblos: a video game, gossip, a toast, a movie, stacking
// blocks, brainstorm, pomodoro, hotfix, tests, 404 not found (and their props, kinds 96..127).
#include "ui_visit_kit.h"

namespace screens {

bool visitActivityB(miblo::Gift g, const VisitStage& s, VisitFrame& f) {
  (void)s;
  (void)f;
  switch (g) {
    default: return false;
  }
}

void drawVisitItemB(const VisitItem& it) { (void)it; }

}  // namespace screens
