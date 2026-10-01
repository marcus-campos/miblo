---
description: Rename a Miblo gadget
argument-hint: "[id] [name]"
allowed-tools: Bash(node "${CLAUDE_PLUGIN_ROOT}/bin/miblo.js":*), AskUserQuestion
---

You manage Miblo desk gadgets with this CLI (call it `MIBLO` below):

```
node "${CLAUDE_PLUGIN_ROOT}/bin/miblo.js" --data "${CLAUDE_PLUGIN_DATA}"
```

Arguments: `$ARGUMENTS`

Rules: reply in the user's language; keep replies short; never show pairing tokens; run only the commands below.

What it does: the name shows on the gadget screen (it greets with "Hi! I'm <name>"), in the /miblo commands and, with visits on, to the other Miblos it visits (the network itself only ever sees the id-based Miblo-XXXX). At most 20 characters. Handy with several Miblos (at the office, or one per computer).

Always pass the name as ONE single-quoted argument (e.g. `MIBLO rename miblo-4f2a 'Office desk'`).

## With arguments: `<id> <name>`

Run `MIBLO rename <id> '<name>'` and report the result line. To go back to the default name (`Miblo-XXXX`), run `MIBLO rename <id> --default`.

## Without arguments (or with only an id)

1. Unless an id was given, run `MIBLO status` and read `devices` from its JSON. None: suggest `/miblo:pair` and stop. Several: ask ONE question with AskUserQuestion ("Which gadget?"), one option per gadget (label: its name, description: its id).
2. Ask for the new name as a plain chat message (e.g. "What should Miblo-4F2A be called? Up to 20 characters, or 'default' for Miblo-4F2A.") and wait for the user's reply. Do NOT use AskUserQuestion for the name (it has no free-text field).
3. Run `MIBLO rename <id> '<name>'` (or `MIBLO rename <id> --default` if they asked for the default name).
   - Too long, empty or rejected by the gadget: explain in one line and ask once more the same way.
   - `Could not reach <name>.`: say the gadget seems offline (check that it is on and on the same network) and stop.
4. Confirm in one line (e.g. "Renamed Miblo-4F2A to Office desk. The gadget screen says hi with the new name.").
