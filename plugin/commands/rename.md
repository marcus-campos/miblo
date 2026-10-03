---
description: Rename a Miblo gadget
argument-hint: "[id] [name]"
allowed-tools: Bash(sh "${CLAUDE_PLUGIN_ROOT}/bin/miblo-run" miblo.js:*), AskUserQuestion
---

You manage Miblo desk gadgets with this CLI (call it `MIBLO` below):

```
sh "${CLAUDE_PLUGIN_ROOT}/bin/miblo-run" miblo.js --data "${CLAUDE_PLUGIN_DATA}"
```

Arguments: `$ARGUMENTS`

Rules: reply in the user's language; keep replies short; never show pairing tokens; run only the commands below.

Safety: never paste the arguments above into a command. Build the command yourself from only: an id copied exactly from `devices` in the output of `MIBLO status` (use it only if it is made of letters, digits, `-` and `_`), the word `--default`, and the new name only as ONE single-quoted argument, as below.

What it does: the name shows on the gadget screen (it greets with "Hi! I'm <name>"), in the /miblo commands and, with visits on, to the other Miblos it visits (the network itself only ever sees the id-based Miblo-XXXX). At most 20 characters. Handy with several Miblos (at the office, or one per computer).

Pass the name as ONE single-quoted argument and write each `'` inside it as `'\''` (e.g. `MIBLO rename miblo-4f2a 'Office desk'`, `MIBLO rename miblo-4f2a 'Ana'\''s desk'`). Never put it in double quotes, backticks or `$(...)`, and never add other shell commands.

## With arguments: `<id> <name>`

Run `MIBLO rename <id> '<name>'` and report the result line. To go back to the default name (`Miblo-XXXX`), run `MIBLO rename <id> --default`.

## Without arguments (or with only an id)

1. Unless an id was given, run `MIBLO status` and read `devices` from its JSON. None: suggest `/miblo:pair` and stop. Several: ask ONE question with AskUserQuestion ("Which gadget?"), one option per gadget (label: its name, description: its id).
2. Ask for the new name as a plain chat message (e.g. "What should Miblo-4F2A be called? Up to 20 characters, or 'default' for Miblo-4F2A.") and wait for the user's reply. Do NOT use AskUserQuestion for the name (it has no free-text field).
3. Run `MIBLO rename <id> '<name>'` (or `MIBLO rename <id> --default` if they asked for the default name).
   - Too long, empty or rejected by the gadget: explain in one line and ask once more the same way.
   - `Could not reach <name>.`: say the gadget seems offline (check that it is on and on the same network) and stop.
4. Confirm in one line (e.g. "Renamed Miblo-4F2A to Office desk. The gadget screen says hi with the new name.").
