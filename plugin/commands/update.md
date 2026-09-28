---
description: Update the Miblo plugin and a paired gadget's firmware
argument-hint: "[id] [--file path]"
allowed-tools: Bash(node "${CLAUDE_PLUGIN_ROOT}/bin/miblo.js":*), Bash(claude plugin marketplace update miblo), Bash(claude plugin update miblo@miblo), AskUserQuestion
---

You manage Miblo desk gadgets with this CLI (call it `MIBLO` below):

```
node "${CLAUDE_PLUGIN_ROOT}/bin/miblo.js" --data "${CLAUDE_PLUGIN_DATA}"
```

Arguments: `$ARGUMENTS` (optional gadget id, optional `--file <path>` to a local `miblo-<board>-<version>.bin`; pass `--file <path>` through to `check` and `open` when given)

Rules: reply in the user's language; keep replies short; never show pairing tokens; run only the commands below.

## 1. Check

Run `MIBLO update check [id] [--file path]`. It prints JSON:
`{ plugin: {current, latest, needsUpdate}, firmware: {latest, error?}, devices: [{id, name, online, fw, board, latest, boardMatch, needsUpdate}] }`.

- If `plugin.needsUpdate` is false/null and no device has `needsUpdate: true`: say everything is up to date (mention offline gadgets, `boardMatch: false`, or `firmware.error` if present) and stop.

## 2. Plugin (before the firmware)

If `plugin.needsUpdate` is true, ask with AskUserQuestion: "Update the Miblo plugin too? (<current> → <latest>)" Yes/No.
- Yes: run `claude plugin marketplace update miblo`, then `claude plugin update miblo@miblo`. If either fails (or `claude` is not on PATH), tell the user to type these themselves: `/plugin marketplace update miblo`, `/plugin update miblo@miblo`, `/reload-plugins`. On success, tell the user to type `/reload-plugins` (you cannot run slash commands). The local bridge restarts on its own with the new version at the next Claude Code activity.

## 3. Firmware

Consider only devices with `needsUpdate: true`. If none, stop after step 2.
1. If several need an update and no id was given, ask which one with AskUserQuestion (one option per gadget name).
2. Confirm with AskUserQuestion: "Update <name> from <fw> to <latest>?" Yes/No. No: stop.
3. Run `MIBLO update open <id> [--file path]`. It prints `{id, name, from, to, codeRequired}` or an error line (no releases → suggest `/miblo:update --file <path>`; board mismatch, checksum mismatch, offline, or "Try again in N s" → report it and stop).
4. If `codeRequired` is true: ask the user, as a plain chat message, to type the 4-digit code now shown on the gadget screen (e.g. "Type the 4-digit code shown on Miblo-B452") and wait for the reply. Do NOT use AskUserQuestion for the code (it has no free-text field).
5. Run `MIBLO update send <id> <code>` (no code when `codeRequired` is false). It uploads and waits up to 2 minutes for the gadget to reboot; tell the user it may take a minute and not to unplug the gadget.
   - `Wrong code.`: ask for the code again the same way (max 3 attempts in total), then run `send` again.
   - `Too many wrong codes. Try again in N s.`: report it and stop.
6. Report the result line (e.g. "Miblo-B452 updated from 0.2.1 to 0.2.2.").
