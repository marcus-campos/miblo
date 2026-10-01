// ui_visit_a.cpp — visits between Miblos: high five, ping-pong, dancing, pizza, release cake,
// merge conflict, daily standup, hackathon, selfie, chess (and their props, kinds 64..95).
#include "ui_visit_kit.h"

namespace screens {

bool visitActivityA(miblo::Gift g, const VisitStage& s, VisitFrame& f) {
  (void)s;
  (void)f;
  switch (g) {
    default: return false;
  }
}

void drawVisitItemA(const VisitItem& it) { (void)it; }

}  // namespace screens
