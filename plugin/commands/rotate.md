---
description: Alternate a Miblo gadget's Overview with the Limits screen
argument-hint: "<on|off> [every-seconds] [show-seconds] [id]"
allowed-tools: Bash(node "${CLAUDE_PLUGIN_ROOT}/bin/miblo.js":*), AskUserQuestion
---

You manage Miblo desk gadgets with this CLI (call it `MIBLO` below):

```
node "${CLAUDE_PLUGIN_ROOT}/bin/miblo.js" --data "${CLAUDE_PLUGIN_DATA}"
```

Arguments: `$ARGUMENTS`

Rules: reply in the user's language; keep replies short; never show pairing tokens; run only the commands below.

What it does: in Overview mode, the gadget switches to the Limits screen for `show-seconds` once every `every-seconds`, then back. Alerts and sessions waiting on the user always take priority. Ranges: every 10–3600 s, show 3–300 s, and show must be shorter than every.

## With arguments: `<on|off> [every-seconds] [show-seconds] [id]`

Run `MIBLO rotate <on|off> [every-seconds] [show-seconds] [id]` with the arguments as given and report the result. If it prints a validation message, explain it briefly.

## Without arguments

1. Run `MIBLO rotate --status` and tell the user the current setting in one line.
2. Ask ONE question with AskUserQuestion ("Alternate Overview with Limits?"), options:
   - "Off"
   - "Every 1 min, show 10 s (Recommended)"
   - "Every 5 min, show 15 s"
   - "Every 15 min, show 20 s"

   The user may pick "Other" and type custom values (e.g. "every 90 show 12", "90 12", "2 min / 15 s"); read them as every-seconds then show-seconds, converting minutes to seconds.
3. Run `MIBLO rotate off`, or `MIBLO rotate on <every-seconds> <show-seconds>` (60 10, 300 15, 900 20, or the custom values).
4. Confirm the result in one line.
