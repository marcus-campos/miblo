#include "miblo_activity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "miblo_rom.h"

namespace miblo {

bool toolVerb(const char* tool, S& out) {
  // In flash (MIBLO_ROM), names inline: a table of pointers and its strings would sit in RAM.
  static const struct {
    char tool[13];
    S verb;
  } kMap[] MIBLO_ROM = {
      {"Edit", S::VerbEditing},      {"MultiEdit", S::VerbEditing}, {"Write", S::VerbEditing},
      {"NotebookEdit", S::VerbEditing}, {"Read", S::VerbReading},  {"Grep", S::VerbSearching},
      {"Glob", S::VerbSearching},    {"WebFetch", S::VerbFetching}, {"WebSearch", S::VerbWebSearch},
      {"Task", S::VerbAgent},        {"Agent", S::VerbAgent},
  };
  if (!tool) return false;
  for (const auto& m : kMap) {
    size_t k = 0;
    while (k < sizeof(m.tool) && tool[k] && tool[k] == (char)mibloRomByte(m.tool + k)) k++;
    if (k < sizeof(m.tool) && !tool[k] && !mibloRomByte(m.tool + k)) {
      mibloRomCopy(&out, &m.verb, sizeof(S));  // S is 16 bits: copy it whole from flash
      return true;
    }
  }
  return false;
}

// Parses an unsigned count that is the whole string ("5"). False on anything else.
static bool parseCount(const char* s, unsigned& out) {
  if (!s || *s < '0' || *s > '9') return false;
  char* end = nullptr;
  const unsigned long v = strtoul(s, &end, 10);
  if (*end || v > 9999) return false;
  out = (unsigned)v;
  return true;
}

bool backgroundWait(const char* tool, const char* det, bool& agents, unsigned& count) {
  if (!tool) return false;
  if (strcmp(tool, kWaitAgents) == 0 || strcmp(tool, kWaitTasks) == 0) {
    agents = strcmp(tool, kWaitAgents) == 0;
    if (!parseCount(det, count)) count = 0;
    return true;
  }
  // Plugins before 0.2.3 sent tool "Agent" + det "waiting 2 agents" / "waiting 1 task".
  if (strcmp(tool, "Agent") != 0 || !det || strncmp(det, "waiting ", 8) != 0) return false;
  char num[8];
  const char* p = det + 8;
  size_t n = 0;
  while (p[n] >= '0' && p[n] <= '9' && n < sizeof(num) - 1) {
    num[n] = p[n];
    n++;
  }
  num[n] = 0;
  if (n == 0 || !parseCount(num, count) || p[n] != ' ') return false;
  const char* word = p + n + 1;
  if (strcmp(word, "agent") == 0 || strcmp(word, "agents") == 0) agents = true;
  else if (strcmp(word, "task") == 0 || strcmp(word, "tasks") == 0) agents = false;
  else return false;
  return true;
}

void activityText(Lang lang, const char* tool, const char* det, bool discreet, char* out, size_t cap) {
  const bool hasDet = !discreet && det && det[0];
  if (!tool || !tool[0]) {
    tr(lang, S::VerbWorking, out, cap);
    return;
  }
  if (strcmp(tool, kCompact) == 0) {
    tr(lang, S::Compacting, out, cap);
    return;
  }
  bool agents = true;
  unsigned count = 0;
  if (backgroundWait(tool, det, agents, count)) {
    // The count is not a secret (no file or command): shown in discreet mode too.
    if (count == 0) {
      tr(lang, S::VerbAgent, out, cap);
      return;
    }
    char fmt[64];
    tr(lang, agents ? (count == 1 ? S::WaitAgent1 : S::WaitAgentsN) : (count == 1 ? S::WaitTask1 : S::WaitTasksN),
       fmt, sizeof(fmt));
    snprintf(out, cap, fmt, count);
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

// True if activityText shows `tool`'s own name (not a verb, a background wait or compaction).
static bool showsToolName(const char* tool, const char* det) {
  S verb;
  bool agents = true;
  unsigned count = 0;
  return tool[0] && strcmp(tool, kCompact) != 0 && !toolVerb(tool, verb) && !backgroundWait(tool, det, agents, count);
}

void sessionLine(Lang lang, const SessionRow& row, bool discreet, char* out, size_t cap, bool anonymous) {
  switch (row.st) {
    case SessionState::Perm: {
      char label[48];
      tr(lang, S::StPerm, label, sizeof(label));
      if (row.tool[0] && !anonymous) snprintf(out, cap, "%s \xC2\xB7 %s", label, row.tool);
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
      if (anonymous && showsToolName(row.tool, row.det)) tr(lang, S::VerbWorking, out, cap);
      else activityText(lang, row.tool, row.det, discreet || anonymous, out, cap);
      return;
  }
}

}  // namespace miblo
