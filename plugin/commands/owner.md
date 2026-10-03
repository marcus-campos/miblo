---
description: Tell a Miblo gadget your name and birthday
argument-hint: "[id] [--name <name>] [--birthday <DD/MM>]"
allowed-tools: Bash(sh "${CLAUDE_PLUGIN_ROOT}/bin/miblo-run" miblo.js:*), AskUserQuestion
---

You manage Miblo desk gadgets with this CLI (call it `MIBLO` below):

```
sh "${CLAUDE_PLUGIN_ROOT}/bin/miblo-run" miblo.js --data "${CLAUDE_PLUGIN_DATA}"
```

Arguments: `$ARGUMENTS`

Rules: reply in the user's language; keep replies short; never show pairing tokens; run only the commands below.

Safety: never paste the arguments above into a command. Build the command yourself from only: an id copied exactly from `devices` in the output of `MIBLO status` (use it only if it is made of letters, digits, `-` and `_`), the fixed words shown here (`--name`, `--birthday`, `clear`), a date you write as `DD/MM` or `MM-DD`, and the name only as ONE single-quoted argument, as below.

What it does: the gadget greets its owner by name ("Good morning, Ana!", once a day) and wishes them a happy birthday on the day. Both stay on the gadget only: the plugin keeps no copy, and the gadget never sends them to other Miblos or reports them in its API (only its own settings page shows them). The name is at most 20 characters. The birthday is day and month only, no year: pass it as `DD/MM` (day first, e.g. `14/03` for March 14) or `MM-DD` (e.g. `03-14`). `clear` removes either one.

Pass the name as ONE single-quoted argument after `--name` and write each `'` inside it as `'\''` (e.g. `MIBLO owner miblo-4f2a --name 'Ana Maria' --birthday 14/03`, `MIBLO owner miblo-4f2a --name 'D'\''Arcy'`). Never put it in double quotes, backticks or `$(...)`, and never add other shell commands.

## With arguments: `<id> [--name <name>|clear] [--birthday <DD/MM|MM-DD|clear>]`

Build `MIBLO owner <id> [--name '<name>'|--name clear] [--birthday <DD/MM>|--birthday clear]` from the arguments (as in Safety above) and report the result line. If it prints a validation message, explain it briefly.

## Without arguments (or with only an id)

1. Unless an id was given, run `MIBLO status` and read `devices` from its JSON. None: suggest `/miblo:pair` and stop. Several: ask ONE question with AskUserQuestion ("Which gadget?"), one option per gadget (label: its name, description: its id).
2. Ask as a plain chat message (NOT AskUserQuestion, it has no free-text field): "Want <gadget name> to know your name and birthday? It will greet you and wish you a happy birthday on the day. Reply like 'Ana, 14/03', just a name, or 'skip'. To forget them, reply 'forget'." Wait for the reply.
   - 'skip', 'no' or similar: stop.
   - 'forget': run `MIBLO owner <id> --name clear --birthday clear`.
   - Otherwise read a name and/or a day-first date (if the date is ambiguous and the user clearly wrote month first, e.g. "March 14", convert it to `14/03`).
3. Run `MIBLO owner <id> --name '<name>' --birthday <DD/MM>` (leave out the part they did not give).
   - Validation message or rejection (name too long, not a real day): explain in one line and ask once more the same way.
   - `Could not reach <name>.`: say the gadget seems offline and stop. `does not support names and birthdays yet`: suggest `/miblo:update`.
4. Confirm in one line.
