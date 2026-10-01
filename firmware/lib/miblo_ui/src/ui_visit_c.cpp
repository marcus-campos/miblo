// ui_visit_c.cpp — visits between Miblos: ship it, sprint, origami, nostalgia, kernel panic,
// picnic, fishing, umbrella, tin can phone, kite (and their props, kinds 128..159).
#include "ui_visit_kit.h"

namespace screens {

bool visitActivityC(miblo::Gift g, const VisitStage& s, VisitFrame& f) {
  (void)s;
  (void)f;
  switch (g) {
    default: return false;
  }
}

void drawVisitItemC(const VisitItem& it) { (void)it; }

}  // namespace screens
