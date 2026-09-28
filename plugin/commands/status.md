---
description: Show Miblo bridge, status line and gadget status
allowed-tools: Bash(node "${CLAUDE_PLUGIN_ROOT}/bin/miblo.js":*), AskUserQuestion
---

You manage Miblo desk gadgets with this CLI (call it `MIBLO` below):

```
node "${CLAUDE_PLUGIN_ROOT}/bin/miblo.js" --data "${CLAUDE_PLUGIN_DATA}"
```

Arguments: `$ARGUMENTS`

Rules: reply in the user's language; keep replies short; never show pairing tokens; run only the commands below.

## `status`

Run `MIBLO status` and summarize: bridge running or stopped, status line linked or not (if linked but `statuslineSeen` is false, say limits appear after the next response), each gadget online/offline, active sessions and limits.
