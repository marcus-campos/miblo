#include "miblo_activity.h"

#include <stdio.h>
#include <string.h>

namespace miblo {

bool toolVerb(const char* tool, S& out) {
  static const struct {
    const char* tool;
    S verb;
  } kMap[] = {
      {"Edit", S::VerbEditing},      {"MultiEdit", S::VerbEditing}, {"Write", S::VerbEditing},
      {"NotebookEdit", S::VerbEditing}, {"Read", S::VerbReading},  {"Grep", S::VerbSearching},
      {"Glob", S::VerbSearching},    {"WebFetch", S::VerbFetching}, {"WebSearch", S::VerbWebSearch},
      {"Task", S::VerbAgent},        {"Agent", S::VerbAgent},
  };
  if (!tool) return false;
  for (const auto& m : kMap) {
    if (strcmp(tool, m.tool) == 0) {
      out = m.verb;
      return true;
    }
  }
  return false;
}

void activityText(Lang lang, const char* tool, const char* det, bool discreet, char* out, size_t cap) {
  const bool hasDet = !discreet && det && det[0];
  if (!tool || !tool[0]) {
    tr(lang, S::VerbWorking, out, cap);
    return;
  }
  S verb;
  if (toolVerb(tool, verb)) {
    char v[48];
    tr(lang, verb, v, sizeof(v));
    if (hasDet) snprintf(out, cap, "%s %s", v, det);
    else snprintf(out, cap, "%s", v);
    return;
  }
  if (hasDet) snprintf(out, cap, "%s \xC2\xB7 %s", tool, det);
  else snprintf(out, cap, "%s", tool);
}

void sessionLine(Lang lang, const SessionRow& row, bool discreet, char* out, size_t cap) {
  switch (row.st) {
    case SessionState::Perm: {
      char label[48];
      tr(lang, S::StPerm, label, sizeof(label));
      if (row.tool[0]) snprintf(out, cap, "%s \xC2\xB7 %s", label, row.tool);
      else snprintf(out, cap, "%s", label);
      return;
    }
    case SessionState::Question:
      tr(lang, S::StQuestion, out, cap);
      return;
    case SessionState::Done:
      tr(lang, S::StDone, out, cap);
      return;
    case SessionState::Idle:
      tr(lang, S::StIdle, out, cap);
      return;
    case SessionState::Running:
      activityText(lang, row.tool, row.det, discreet, out, cap);
      return;
  }
}

}  // namespace miblo
