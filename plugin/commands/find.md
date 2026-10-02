---
description: Make a Miblo blink and wave so you can spot it
argument-hint: "[--id <id>]"
allowed-tools: Bash(node "${CLAUDE_PLUGIN_ROOT}/bin/miblo.js":*), AskUserQuestion
---

You manage Miblo desk gadgets with this CLI (call it `MIBLO` below):

```
node "${CLAUDE_PLUGIN_ROOT}/bin/miblo.js" --data "${CLAUDE_PLUGIN_DATA}"
```

Arguments: `$ARGUMENTS`

Rules: reply in the user's language; keep replies short; never show pairing tokens; run only the commands below.

What it does: for 10 seconds the gadget blinks, the cat waves and a QR code with the gadget's settings page shows (scan it with a phone on the same network). Handy with several Miblos in the same room.

Run `MIBLO find $ARGUMENTS` and report the result in one sentence. To find one specific gadget among several, run `MIBLO status`, pick its id from `devices` and run `MIBLO find --id <id>`.

It goes to every paired Miblo; `--id <id>` (the id from `/miblo:status`) sends it to only one. If the output says a gadget "does not support this yet", suggest `/miblo:update`; "no longer knows this computer": suggest `/miblo:pair`; "is offline": say it seems off or on another network.
