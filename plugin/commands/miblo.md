---
description: Pair and manage Miblo desk gadgets (pair, status, mode, link-statusline, unlink-statusline, reset)
argument-hint: "[pair [ip] | status | mode <overview|limits|sessions> [id] | link-statusline | unlink-statusline | reset <id>]"
allowed-tools: Bash(node:*), AskUserQuestion
---

You manage Miblo desk gadgets with this CLI (call it `MIBLO` below):

```
node "${CLAUDE_PLUGIN_ROOT}/bin/miblo.js" --data "${CLAUDE_PLUGIN_DATA}"
```

Arguments: `$ARGUMENTS`

Rules: reply in the user's language; keep replies short; never show pairing tokens; run only the commands below.

## No arguments, or `pair [ip]`

1. If an IP was given, use it as the address. Otherwise run `MIBLO discover`.
   - No gadget found: tell the user to check that the gadget shows a 4-digit code on screen and is on the same network, and that on WSL2 or corporate networks they can run `/miblo pair <ip>` with the IP shown at the bottom of the gadget screen. Stop.
   - More than one: ask which one (AskUserQuestion, one option per gadget name).
2. Ask for the 4-digit code shown on the gadget screen.
3. Run `MIBLO pair <address> <code>`. If it prints `Wrong pairing code.`, ask again (max 3 attempts).
4. Ask for consent to link the status line, explaining in one sentence: "To show your 5h/weekly limits and token usage, Miblo reads the official status line data. Your current status line keeps working exactly the same; you can undo with /miblo unlink-statusline." If yes, run `MIBLO link-statusline`.
5. Confirm success and say the gadget will update on the next Claude Code activity.

## `status`

Run `MIBLO status` and summarize: bridge running or stopped, status line linked or not (if linked but `statuslineSeen` is false, say limits appear after the next response), each gadget online/offline, active sessions and limits.

## `mode <overview|limits|sessions> [id]`

Run `MIBLO mode <mode> [id]` and report the result.

## `link-statusline` / `unlink-statusline`

Run the matching command and report the result. Before uninstalling the plugin, run `/miblo unlink-statusline` so the original status line is restored.

## `reset <id>`

Confirm with the user first (it erases the gadget's Wi-Fi and pairing). Then run `MIBLO reset <id>`.
