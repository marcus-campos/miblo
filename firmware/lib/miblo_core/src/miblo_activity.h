#pragma once
#include <stddef.h>

#include "miblo_i18n.h"
#include "miblo_snapshot.h"

namespace miblo {

// Localized verb for the known Claude Code tools. Bash and unknown ones → false
// (the tool name is shown as-is).
bool toolVerb(const char* tool, S& out);

// Activity text for a running session:
//   known verb → "Editing Header.tsx" (or just "Editing" without det / in discreet mode)
//   other      → "Bash · npm test"    (or just "Bash")
//   no tool    → "Working"
void activityText(Lang lang, const char* tool, const char* det, bool discreet, char* out, size_t cap);

// Status line for a session, for the lists:
//   perm → "permission · Bash", question → "question", done → "finished", idle → "idle",
//   running → activityText(...).
void sessionLine(Lang lang, const SessionRow& row, bool discreet, char* out, size_t cap);

}  // namespace miblo
