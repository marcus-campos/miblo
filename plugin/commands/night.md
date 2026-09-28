---
description: Dim a Miblo gadget's screen at night
argument-hint: "<on|off> [HH:MM HH:MM] [brightness%] [id]"
allowed-tools: Bash(node "${CLAUDE_PLUGIN_ROOT}/bin/miblo.js":*), AskUserQuestion
---

You manage Miblo desk gadgets with this CLI (call it `MIBLO` below):

```
node "${CLAUDE_PLUGIN_ROOT}/bin/miblo.js" --data "${CLAUDE_PLUGIN_DATA}"
```

Arguments: `$ARGUMENTS`

Rules: reply in the user's language; keep replies short; never show pairing tokens; run only the commands below.

What it does: between the start and end times (the gadget's local time zone; the window may cross midnight, e.g. 22:00 to 07:00) the screen uses the night brightness (1–100%, never brighter than the normal brightness). Outside the window the normal brightness comes back.

## With arguments: `<on|off> [HH:MM HH:MM] [brightness%] [id]`

Run `MIBLO night <on|off> [start end] [brightness] [id]` with the arguments as given and report the result. If it prints a validation message, explain it briefly.

## Without arguments

1. Run `MIBLO night --status` and tell the user the current setting in one line.
2. Ask ONE question with AskUserQuestion ("Dim the screen at night?"), options:
   - "Off"
   - "22:00 to 07:00 at 10% (Recommended)"
   - "23:00 to 07:00 at 5%"
   - "20:00 to 08:00 at 20%"

   The user may pick "Other" and type custom values (e.g. "23h to 6h at 15%", "22:30 06:30 8"); read them as start, end and brightness.
3. Run `MIBLO night off`, or `MIBLO night on <start> <end> <brightness>` (22:00 07:00 10, 23:00 07:00 5, 20:00 08:00 20, or the custom values).
4. Confirm the result in one line.
