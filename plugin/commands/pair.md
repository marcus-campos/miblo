---
description: Pair a Miblo desk gadget
argument-hint: "[ip]"
allowed-tools: Bash(node "${CLAUDE_PLUGIN_ROOT}/bin/miblo.js":*), Bash(claude plugin marketplace update miblo), Bash(claude plugin update miblo@miblo), AskUserQuestion
---

You manage Miblo desk gadgets with this CLI (call it `MIBLO` below):

```
node "${CLAUDE_PLUGIN_ROOT}/bin/miblo.js" --data "${CLAUDE_PLUGIN_DATA}"
```

Arguments: `$ARGUMENTS`

Rules: reply in the user's language; keep replies short; never show pairing tokens; run only the commands below.

## `pair [ip]`

1. If an IP was given, use it as the address. Otherwise run `MIBLO discover`.
   - No gadget found: tell the user to check that the gadget shows a 4-digit code on screen and is on the same network, and that on WSL2 or corporate networks they can run `/miblo:pair <ip>` with the IP shown at the bottom of the gadget screen. Stop.
   - More than one: ask which one (AskUserQuestion, one option per gadget name).
2. Ask for the 4-digit code shown on the gadget screen as a plain chat message (e.g. "Type the 4-digit code shown on Miblo-B452") and wait for the user's reply. Do NOT use AskUserQuestion for the code (it has no free-text field).
3. Run `MIBLO pair <address> <code>`. If it prints `Wrong pairing code.`, ask again the same way (max 3 attempts).
4. Offer a name, as a plain chat message (NOT AskUserQuestion, it has no free-text field), using the name and id printed by `pair` (`Paired with <name> (<id>) ...`), e.g.: "What should this Miblo be called? It's <name> now; reply with a name (up to 20 characters) or 'keep'." A name helps when there are several Miblos (at the office, or one per computer); it shows on the gadget screen, in the /miblo commands and on the network.
   - A name: run `MIBLO rename <id> '<name>'` (the name as ONE single-quoted argument). If it is rejected (too long, empty, or the gadget refuses it), explain in one line and ask once more the same way; after a second failure keep the current name and go on. If the gadget cannot be reached, keep the name and go on (they can use /miblo:rename later).
   - 'keep', an empty reply or similar: keep the current name.
5. Optional, one plain chat message (NOT AskUserQuestion), with the gadget's current name: "Want <gadget name> to know your name and birthday? It will greet you and wish you a happy birthday on the day. Reply like 'Ana, 14/03', just a name, or 'skip'."
   - 'skip', 'no', an empty reply or similar: go on.
   - Otherwise run `MIBLO owner <id> --name '<name>' --birthday <DD/MM>` (name as ONE single-quoted argument; the date day first, e.g. `14/03`; leave out the part they did not give). On a validation message or rejection, explain in one line and ask once more; after that, or if the gadget cannot be reached or does not support it, go on (they can use /miblo:owner later).
6. Ask for consent to link the status line, explaining in one sentence: "To show your 5h/weekly limits and token usage, Miblo reads the official status line data. Your current status line keeps working exactly the same; you can undo with /miblo:unlink-statusline." If yes, run `MIBLO link-statusline`.
7. Confirm success and say the gadget will update on the next Claude Code activity.
8. Offer the update: run `MIBLO update check <id>` with the id printed by `pair` (`Paired with <name> (<id>) ...`); if the gadget was renamed, use the new name when you talk about it. It prints JSON:
   `{ plugin: {current, latest, needsUpdate}, firmware: {latest, error?}, devices: [{id, name, online, fw, board, latest, boardMatch, needsUpdate}] }`.
   - If the command fails, or neither `plugin.needsUpdate` nor the gadget's `needsUpdate` is true: say nothing more and stop.
   - Otherwise ask ONE AskUserQuestion: "<name> is on <fw>; version <latest> is available. Update now?" Yes/No (when only the plugin is outdated: "The Miblo plugin is on <current>; version <latest> is available. Update now?"; when both are, add "The plugin will be updated too.").
   - No: tell the user they can run /miblo:update any time. Stop.
   - Yes, plugin (when `plugin.needsUpdate` is true, before the firmware): run `claude plugin marketplace update miblo`, then `claude plugin update miblo@miblo`. If either fails (or `claude` is not on PATH), tell the user to type these themselves: `/plugin marketplace update miblo`, `/plugin update miblo@miblo`, `/reload-plugins`. On success, tell the user to type `/reload-plugins` (you cannot run slash commands).
   - Yes, firmware (when the gadget's `needsUpdate` is true), without asking again:
     1. Run `MIBLO update open <id>`. It prints `{id, name, from, to, codeRequired}` or an error line (board mismatch, checksum mismatch, offline, or "Try again in N s" → report it and stop).
     2. If `codeRequired` is true: ask the user, as a plain chat message, to type the 4-digit code now shown on the gadget screen (e.g. "Type the 4-digit code shown on Miblo-B452") and wait for the reply. Do NOT use AskUserQuestion for the code.
     3. Run `MIBLO update send <id> <code>` (no code when `codeRequired` is false). It uploads and waits up to 2 minutes for the gadget to reboot; tell the user it may take a minute and not to unplug the gadget. `Wrong code.`: ask for the code again the same way (max 3 attempts in total), then run `send` again. `Too many wrong codes. Try again in N s.`: report it and stop.
     4. Report the result line (e.g. "Miblo-B452 updated from 0.2.1 to 0.2.2.").
