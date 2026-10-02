---
description: Start, check or stop a focus session (Pomodoro) on Miblo
argument-hint: "[focus-min [break-min [rounds]]] | stop | status"
allowed-tools: Bash(node "${CLAUDE_PLUGIN_ROOT}/bin/miblo.js":*), AskUserQuestion
---

You manage Miblo desk gadgets with this CLI (call it `MIBLO` below):

```
node "${CLAUDE_PLUGIN_ROOT}/bin/miblo.js" --data "${CLAUDE_PLUGIN_DATA}"
```

Arguments: `$ARGUMENTS`

Rules: reply in the user's language; keep replies short; never show pairing tokens; run only the commands below.

What it does: the gadget times rounds of focus and breaks on its own (it keeps going if the computer sleeps; restarting the gadget ends it). During focus the cat wears headphones and the screen shows the time left and "focus until 15:30", so people around know; Claude's alerts still show. After each break comes 1 minute of "Back to focus?", then the next round starts by itself; after the last round there is a long break (3 times the break, at most 30 min), then focus ends. Defaults: 25 min of focus, 5 min breaks, 4 rounds. Limits: focus 5–120 min, break 1–60 min, rounds 1–12. With only the focus length, the gadget picks the break (a fifth of it: 50 → 10).

Run `MIBLO focus $ARGUMENTS` and report the result in one sentence.

- No arguments: if a focus is already on, the CLI shows what's left instead of starting over ("Focus: round 2/4, 12 min left (Amon)"); otherwise it starts 25/5/4.
- Natural language → arguments: "50 minute focus" / "foco de 50 minutos" → `focus 50`; "25 and 5, 3 rounds" → `focus 25 5 3`; "stop the focus" / "parar o foco" → `focus stop`; "how long is left?" → `focus status`.
- A validation message (exit code 2): explain it in one line.

It goes to every paired Miblo; `--id <id>` (the id from `/miblo:status`) sends it to only one. If the output says a gadget "does not support this yet", suggest `/miblo:update`; "no longer knows this computer": suggest `/miblo:pair`; "is offline": say it seems off or on another network.
