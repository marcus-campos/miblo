#pragma once
#include <stddef.h>

#include "miblo_i18n.h"
#include "miblo_snapshot.h"

namespace miblo {

// Localized verb for the known Claude Code tools. Bash and unknown ones → false
// (the tool name is shown as-is).
bool toolVerb(const char* tool, S& out);

// Reserved activity tools the plugin sends while a Stop waits on background work; det = the
// count ("2"). Localized on the device ("Aguardando 2 agentes").
constexpr const char* kWaitAgents = "_wait_agents";
constexpr const char* kWaitTasks = "_wait_tasks";
// Reserved activity tool while Claude Code compacts the conversation ("Compactando contexto").
constexpr const char* kCompact = "_compact";

// True if tool/det describe a background wait: the structured form above, or the English one of
// older plugins (tool "Agent", det "waiting 2 agents" / "waiting 1 task"). count 0 = unknown.
bool backgroundWait(const char* tool, const char* det, bool& agents, unsigned& count);

// Activity text for a running session:
//   background wait → "Waiting on 2 agents" (localized)
//   compaction      → "Compacting context" (localized)
//   known verb → "Editing Header.tsx" (or just "Editing" without det / in discreet mode)
//   other      → "Bash · npm test"    (or just "Bash")
//   no tool    → "Working"
void activityText(Lang lang, const char* tool, const char* det, bool discreet, char* out, size_t cap);

// Status line for a session, for the lists:
//   perm → "permission · Bash", question → "question", done → "finished", idle → "idle",
//   running → activityText(...).
void sessionLine(Lang lang, const SessionRow& row, bool discreet, char* out, size_t cap);

}  // namespace miblo
