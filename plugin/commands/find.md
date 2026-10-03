---
description: Make a Miblo blink and wave so you can spot it
argument-hint: "[--id <id>]"
allowed-tools: Bash(sh "${CLAUDE_PLUGIN_ROOT}/bin/miblo-run" miblo.js:*), AskUserQuestion
---

You manage Miblo desk gadgets with this CLI (call it `MIBLO` below):

```
sh "${CLAUDE_PLUGIN_ROOT}/bin/miblo-run" miblo.js --data "${CLAUDE_PLUGIN_DATA}"
```

Arguments: `$ARGUMENTS`

Rules: reply in the user's language; keep replies short; never show pairing tokens; run only the commands below.

Safety: never paste the arguments above, or any other text the user typed, into a command. Build the command yourself from only: whole numbers you write (like `50`), the fixed words shown here, and `--id <id>` with an id copied exactly from `devices` in the output of `MIBLO status` (use it only if it is made of letters, digits, `-` and `_`).

What it does: for 10 seconds the gadget blinks, the cat waves and a QR code with the gadget's settings page shows (scan it with a phone on the same network). Handy with several Miblos in the same room.

Run `MIBLO find` (all gadgets) or `MIBLO find --id <id>` and report the result in one sentence. To find one specific gadget among several, run `MIBLO status`, pick its id from `devices` and run `MIBLO find --id <id>`.

It goes to every paired Miblo; `--id <id>` sends it to only one (run `MIBLO status` and copy the id from `devices`; never use an id the user typed without checking it is listed there). If the output says a gadget "does not support this yet", suggest `/miblo:update`; "no longer knows this computer": suggest `/miblo:pair`; "is offline": say it seems off or on another network.
