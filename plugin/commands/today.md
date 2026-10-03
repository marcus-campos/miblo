---
description: Show today's Claude Code summary and limits
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

What it does: today's summary from the local Miblo bridge (it does not need a gadget): responses finished, time with Claude working, today's cost, the 5-hour and weekly limits with when they reset and, when the recent pace would use up the 5-hour window before it resets, when it runs out; with several sessions today, which ones worked the most. Times are in this computer's time zone.

Run `MIBLO today` and give the result in the user's language, keeping the numbers as they are (2–3 short lines). If the bridge isn't running, say there is nothing to show until Claude Code has some activity. If there are no limits yet, suggest `/miblo:link-statusline`.
