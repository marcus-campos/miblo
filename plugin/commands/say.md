---
description: Show a message on Miblo for people passing by your desk
argument-hint: "<text> [--min N] | off"
allowed-tools: Bash(node "${CLAUDE_PLUGIN_ROOT}/bin/miblo.js":*), AskUserQuestion
---

You manage Miblo desk gadgets with this CLI (call it `MIBLO` below):

```
node "${CLAUDE_PLUGIN_ROOT}/bin/miblo.js" --data "${CLAUDE_PLUGIN_DATA}"
```

Arguments: `$ARGUMENTS`

Rules: reply in the user's language; keep replies short; never show pairing tokens; run only the commands below.

Safety: never paste the arguments above into a command. Build the command yourself: numbers, times and dates you write in the formats shown, the fixed words shown here, `--id <id>` with an id copied exactly from `devices` in the output of `MIBLO status`, and the user's free text only as ONE single-quoted argument, as below.

What it does: the cat holds up the message on the gadget screen (in pet mode it goes on the cat's sign) for 30 minutes by default (`--min` 1–480), or until `off`. The text is at most 40 characters (fewer with emoji or Chinese/Japanese characters: 47 bytes at most), on one line.

Pass the user's text as ONE single-quoted argument and write each `'` inside it as `'\''` (e.g. `MIBLO say 'back in 10 min'`, `MIBLO say 'it'\''s lunch'`). Never put it in double quotes, backticks or `$(...)`, and never add other shell commands.

Run `MIBLO say '<text>' [--min N]` (or `MIBLO say off`) and report the result in one sentence.

- Natural language → arguments: "tell people I'm back in 10 min" / "avisa que volto em 10 min" → `say 'Back in 10 min'` (keep the user's own words and language when they gave the text; write it short when they only described it); "for an hour" → `--min 60`; "remove the message" / "tirar o recado" → `say off`.
- Too long, or a character the gadget cannot show: the CLI says so (exit code 2); offer a shorter version and run it once the user agrees.

It goes to every paired Miblo; `--id <id>` sends it to only one (run `MIBLO status` and copy the id from `devices`; never use an id the user typed without checking it is listed there). If the output says a gadget "does not support this yet", suggest `/miblo:update`; "no longer knows this computer": suggest `/miblo:pair`; "is offline": say it seems off or on another network.
