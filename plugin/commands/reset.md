---
description: Factory reset a paired Miblo gadget
argument-hint: "<id>"
allowed-tools: Bash(node "${CLAUDE_PLUGIN_ROOT}/bin/miblo.js":*), AskUserQuestion
---

You manage Miblo desk gadgets with this CLI (call it `MIBLO` below):

```
node "${CLAUDE_PLUGIN_ROOT}/bin/miblo.js" --data "${CLAUDE_PLUGIN_DATA}"
```

Arguments: `$ARGUMENTS`

Rules: reply in the user's language; keep replies short; never show pairing tokens; run only the commands below.

Safety: never paste the arguments above into a command. Build the command yourself from only: an id copied exactly from `devices` in the output of `MIBLO status` (use it only if it is made of letters, digits, `-` and `_`).

## `reset <id>`

Confirm with the user first (it erases the gadget's Wi-Fi and pairing). Then run `MIBLO reset <id>`.
