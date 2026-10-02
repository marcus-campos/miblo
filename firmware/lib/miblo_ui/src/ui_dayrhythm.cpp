// The day's rhythm: wellness nudges, the end of the work day, Monday's recap of last week.
#include "ui_internal.h"
#include "ui_screens.h"

namespace screens {

void nudge(Lang lang, miblo::Nudge kind, uint32_t ms) {
  // Stub (daily-life foundation): track C draws it.
  (void)lang;
  (void)kind;
  (void)ms;
}

void dayEnd(Lang lang, const miblo::Snapshot& s, const char* owner, uint32_t ms) {
  // Stub (daily-life foundation): track C draws it.
  (void)lang;
  (void)s;
  (void)owner;
  (void)ms;
}

void weekRecap(Lang lang, const miblo::Snapshot& s, uint32_t ms) {
  // Stub (daily-life foundation): track C draws it.
  (void)lang;
  (void)s;
  (void)ms;
}

}  // namespace screens
