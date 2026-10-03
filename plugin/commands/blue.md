---
description: Turn on, schedule or adjust Miblo's blue light filter
argument-hint: "[on|off] [HH:MM HH:MM] [strength%|low|medium|high]"
allowed-tools: Bash(sh "${CLAUDE_PLUGIN_ROOT}/bin/miblo-run" miblo.js:*), AskUserQuestion
---

You manage Miblo desk gadgets with this CLI (call it `MIBLO` below):

```
sh "${CLAUDE_PLUGIN_ROOT}/bin/miblo-run" miblo.js --data "${CLAUDE_PLUGIN_DATA}"
```

Arguments: `$ARGUMENTS`

Rules: reply in the user's language; keep replies short; never show pairing tokens; run only the commands below.

Safety: never paste the arguments above, or any other text the user typed, into a command. Build the command yourself from only: whole numbers you write (like `60` or `60%`), times you write as `HH:MM` (like `21:00`), the fixed words shown here, and `--id <id>` with an id copied exactly from `devices` in the output of `MIBLO status` (use it only if it is made of letters, digits, `-` and `_`).

What it does: the screen's colours get warmer (less blue light), on its own schedule, separate from night dimming. Off, always on, or between two times of the gadget's local time zone (the window may cross midnight, e.g. 21:00 to 07:00). The strength goes from 1% (barely warmer) to 100% (the warmest: white like an incandescent bulb, 2700 K); `low`, `medium` and `high` are 31%, 63% and 100%. Off by default; the strength starts at 63%.

Run `MIBLO blue` followed by the arguments you built, plus `--id <id>` if one gadget was asked for, and report the result in one sentence:

- nothing or `status`: the current setting of each gadget;
- `on`: always on; `off`: off (the strength and the times are kept);
- two times (`blue 21:00 07:00`): on between them every day;
- a strength (`blue 60%`, `blue low`): only the strength, the rest stays; it can also follow `on` or the two times (`blue on 40%`, `blue 21:00 07:00 30%`).

- Natural language → arguments: "turn on the blue light filter" / "liga o filtro de luz azul" → `blue on`; "warmer colours from 9 pm to 7 am" → `blue 21:00 07:00`; "make it softer" / "mais fraco" → read the status first, then a lower strength (e.g. `blue 30%`); "strongest" → `blue 100%`; "turn it off" → `blue off`.
- A validation message (exit code 2): explain it in one line.

It goes to every paired Miblo; `--id <id>` sends it to only one (run `MIBLO status` and copy the id from `devices`; never use an id the user typed without checking it is listed there). If the output says a gadget "does not support this yet", suggest `/miblo:update`; "no longer knows this computer": suggest `/miblo:pair`; "is busy": try again in a moment; "is offline": say it seems off or on another network.
