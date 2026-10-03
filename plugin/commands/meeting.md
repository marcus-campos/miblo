---
description: Put Miblo in meeting mode for a while
argument-hint: "[minutes] | off"
allowed-tools: Bash(sh "${CLAUDE_PLUGIN_ROOT}/bin/miblo-run" miblo.js:*), AskUserQuestion
---

You manage Miblo desk gadgets with this CLI (call it `MIBLO` below):

```
sh "${CLAUDE_PLUGIN_ROOT}/bin/miblo-run" miblo.js --data "${CLAUDE_PLUGIN_DATA}"
```

Arguments: `$ARGUMENTS`

Rules: reply in the user's language; keep replies short; never show pairing tokens; run only the commands below.

Safety: never paste the arguments above, or any other text the user typed, into a command. Build the command yourself from only: whole numbers you write (like `50`), the fixed words shown here, and `--id <id>` with an id copied exactly from `devices` in the output of `MIBLO status` (use it only if it is made of letters, digits, `-` and `_`).

What it does: for 60 minutes by default (1–480), the gadget hides project names, commands and tool names (also in alerts: "A session needs you"), blinks once instead of flashing, puts a tie on the cat and shows an "In a meeting" badge. It never hides that a session needs you. It ends by itself, with `off`, or when the gadget restarts.

Run `MIBLO meeting` followed by the arguments you built: nothing (60 min), one whole number of minutes (`meeting 30`) or `off`, plus `--id <id>` if one gadget was asked for. Report the result in one sentence.

- Natural language → arguments: "meeting for half an hour" / "reunião de 30 minutos" → `meeting 30`; "a 2 hour meeting" → `meeting 120`; "meeting's over" / "acabou a reunião" → `meeting off`.
- A validation message (exit code 2): explain it in one line.

It goes to every paired Miblo; `--id <id>` sends it to only one (run `MIBLO status` and copy the id from `devices`; never use an id the user typed without checking it is listed there). If the output says a gadget "does not support this yet", suggest `/miblo:update`; "no longer knows this computer": suggest `/miblo:pair`; "is offline": say it seems off or on another network.
