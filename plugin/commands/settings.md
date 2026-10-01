---
description: Open a Miblo gadget's settings page in the browser
argument-hint: "[id]"
allowed-tools: Bash(node "${CLAUDE_PLUGIN_ROOT}/bin/miblo.js":*), AskUserQuestion
---

You manage Miblo desk gadgets with this CLI (call it `MIBLO` below):

```
node "${CLAUDE_PLUGIN_ROOT}/bin/miblo.js" --data "${CLAUDE_PLUGIN_DATA}"
```

Arguments: `$ARGUMENTS`

Rules: reply in the user's language; keep replies short; never show pairing tokens; run only the commands below.

## `settings [id]`

1. Run `MIBLO settings [id]` (pass the id only if the user gave one).
2. If it says several gadgets are paired and asks for an id, ask ONE question with AskUserQuestion ("Which gadget?"), one option per listed gadget (label: its name, description: its id), then run `MIBLO settings <id>` with the chosen id.
3. Report the settings URL in one line (and say it was opened in the browser, or that it has to be opened by hand if the CLI says so). If no gadget is paired, suggest `/miblo:pair`. Tell the user that a paired gadget's page first asks for the code shown on its screen.
