---
description: Start or stop a visible timer on Miblo
argument-hint: "<minutes> | stop"
allowed-tools: Bash(node "${CLAUDE_PLUGIN_ROOT}/bin/miblo.js":*), AskUserQuestion
---

You manage Miblo desk gadgets with this CLI (call it `MIBLO` below):

```
node "${CLAUDE_PLUGIN_ROOT}/bin/miblo.js" --data "${CLAUDE_PLUGIN_DATA}"
```

Arguments: `$ARGUMENTS`

Rules: reply in the user's language; keep replies short; never show pairing tokens; run only the commands below.

Safety: never paste the arguments above, or any other text the user typed, into a command. Build the command yourself from only: whole numbers you write (like `50`), the fixed words shown here, and `--id <id>` with an id copied exactly from `devices` in the output of `MIBLO status`.

What it does: a big countdown on the gadget screen (the cat with an hourglass), 1–180 minutes. When it ends, the whole screen pulses slowly and the cat holds "Time's up!" for 5 minutes. Claude's alerts still show on top and the timer comes back after them.

Run `MIBLO timer` followed by the arguments you built: one whole number of minutes (`timer 10`) or `stop`, plus `--id <id>` if one gadget was asked for. Report the result in one sentence.

- Natural language → arguments: "10 minute timer" / "timer de 10 minutos" → `timer 10`; "an hour and a half" → `timer 90`; "cancel the timer" / "parar o timer" → `timer stop`.
- No minutes given: ask how many as a plain chat message, then run it.
- A validation message (exit code 2): explain it in one line.

It goes to every paired Miblo; `--id <id>` sends it to only one (run `MIBLO status` and copy the id from `devices`; never use an id the user typed without checking it is listed there). If the output says a gadget "does not support this yet", suggest `/miblo:update`; "no longer knows this computer": suggest `/miblo:pair`; "is offline": say it seems off or on another network.
