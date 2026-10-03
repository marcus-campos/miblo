---
description: Show the Claude Code usage limits and when they run out
argument-hint: ""
allowed-tools: Bash(sh "${CLAUDE_PLUGIN_ROOT}/bin/miblo-run" miblo.js:*), AskUserQuestion
---

You manage Miblo desk gadgets with this CLI (call it `MIBLO` below):

```
sh "${CLAUDE_PLUGIN_ROOT}/bin/miblo-run" miblo.js --data "${CLAUDE_PLUGIN_DATA}"
```

Arguments: `$ARGUMENTS`

Rules: reply in the user's language; keep replies short; never show pairing tokens; run only the commands below.

Safety: never paste the arguments above into a command; run only the exact command below (it takes no arguments).

What it does: the 5-hour and weekly limits from the local Miblo bridge (it does not need a gadget), when each resets and, when the recent pace would use up the 5-hour window before it resets, the time it runs out. Times are in this computer's time zone ("Thu 09:00" when not today).

Run `MIBLO limits` and give the result in one line in the user's language, keeping the numbers as they are. If the bridge isn't running, say there is nothing to show until Claude Code has some activity. If there are no limits yet, suggest `/miblo:link-statusline` (the limits come from Claude Code's status line).
