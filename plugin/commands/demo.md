---
description: Show off pet mode and visits between Miblos right now
argument-hint: "[minutes|stop]"
allowed-tools: Bash(node "${CLAUDE_PLUGIN_ROOT}/bin/miblo.js":*), AskUserQuestion
---

You manage Miblo desk gadgets with this CLI (call it `MIBLO` below):

```
node "${CLAUDE_PLUGIN_ROOT}/bin/miblo.js" --data "${CLAUDE_PLUGIN_DATA}"
```

Arguments: `$ARGUMENTS`

Rules: reply in the user's language; keep replies short; never show pairing tokens; run only the commands below.

What it does: puts all the paired gadgets in pet mode right away, for 10 minutes by default (1 to 30), instead of waiting for the idle delay (15 minutes by default). With two or more Miblos on the same network they greet each other and the first visit comes within about 10 seconds, then every 20 to 40 seconds. Alerts still show as usual; the demo ends on its own, or with `stop`.

Run `MIBLO demo $ARGUMENTS` and report the result line in one sentence. If several gadgets are in demo, add that the first visit comes in about 10 seconds. If a gadget does not support it yet, suggest `/miblo:update`. It needs at least two paired Miblos (it is about Miblos visiting each other): if the CLI says so, explain that and suggest `/miblo:pair` for another one.
