---
description: Put Miblo in meeting mode for a while
argument-hint: "[minutes] | off"
allowed-tools: Bash(node "${CLAUDE_PLUGIN_ROOT}/bin/miblo.js":*), AskUserQuestion
---

You manage Miblo desk gadgets with this CLI (call it `MIBLO` below):

```
node "${CLAUDE_PLUGIN_ROOT}/bin/miblo.js" --data "${CLAUDE_PLUGIN_DATA}"
```

Arguments: `$ARGUMENTS`

Rules: reply in the user's language; keep replies short; never show pairing tokens; run only the commands below.

What it does: for 60 minutes by default (1–480), the gadget hides project names, commands and tool names (also in alerts: "A session needs you"), blinks once instead of flashing, puts a tie on the cat and shows an "In a meeting" badge. It never hides that a session needs you. It ends by itself, with `off`, or when the gadget restarts.

Run `MIBLO meeting $ARGUMENTS` and report the result in one sentence.

- Natural language → arguments: "meeting for half an hour" / "reunião de 30 minutos" → `meeting 30`; "a 2 hour meeting" → `meeting 120`; "meeting's over" / "acabou a reunião" → `meeting off`.
- A validation message (exit code 2): explain it in one line.

It goes to every paired Miblo; `--id <id>` (the id from `/miblo:status`) sends it to only one. If the output says a gadget "does not support this yet", suggest `/miblo:update`; "no longer knows this computer": suggest `/miblo:pair`; "is offline": say it seems off or on another network.
