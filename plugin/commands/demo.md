---
description: Show off pet mode and visits between Miblos right now
argument-hint: "[minutes|stop]"
allowed-tools: Bash(sh "${CLAUDE_PLUGIN_ROOT}/bin/miblo-run" miblo.js:*), AskUserQuestion
---

You manage Miblo desk gadgets with this CLI (call it `MIBLO` below):

```
sh "${CLAUDE_PLUGIN_ROOT}/bin/miblo-run" miblo.js --data "${CLAUDE_PLUGIN_DATA}"
```

Arguments: `$ARGUMENTS`

Rules: reply in the user's language; keep replies short; never show pairing tokens; run only the commands below.

Safety: never paste the arguments above into a command. Build the command yourself from only: a whole number of minutes you write (1 to 30) or the word `stop`.

What it does: puts all the paired gadgets in pet mode right away, for 10 minutes by default (1 to 30), instead of waiting for the idle delay (15 minutes by default). With two or more Miblos on the same network they greet each other and the first visit comes within about 10 seconds, then every 20 to 40 seconds. Alerts still show as usual; the demo ends on its own, when Claude Code starts new work (not the work that ran this command), or with `stop`.

Run `MIBLO demo <N>` with N the minutes you write (from the arguments; `MIBLO demo` alone for the default 10), or `MIBLO demo stop` to end it, and report the result line in one sentence. If several gadgets are in demo, add that the first visit comes in about 10 seconds. If a gadget does not support it yet, suggest `/miblo:update`. It needs at least two paired Miblos (it is about Miblos visiting each other): if the CLI says so, explain that and suggest `/miblo:pair` for another one.
