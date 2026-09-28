---
description: Restore the original Claude Code status line
allowed-tools: Bash(node "${CLAUDE_PLUGIN_ROOT}/bin/miblo.js":*), AskUserQuestion
---

You manage Miblo desk gadgets with this CLI (call it `MIBLO` below):

```
node "${CLAUDE_PLUGIN_ROOT}/bin/miblo.js" --data "${CLAUDE_PLUGIN_DATA}"
```

Arguments: `$ARGUMENTS`

Rules: reply in the user's language; keep replies short; never show pairing tokens; run only the commands below.

## `unlink-statusline`

Run `MIBLO unlink-statusline` and report the result. Before uninstalling the plugin, run `/miblo:unlink-statusline` so the original status line is restored.
