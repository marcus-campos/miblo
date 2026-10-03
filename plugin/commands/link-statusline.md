---
description: Link Claude Code's status line to Miblo
allowed-tools: Bash(sh "${CLAUDE_PLUGIN_ROOT}/bin/miblo-run" miblo.js:*), Bash(sh "${CLAUDE_PLUGIN_ROOT}/bin/miblo-run" --check:*), AskUserQuestion
---

You manage Miblo desk gadgets with this CLI (call it `MIBLO` below):

```
sh "${CLAUDE_PLUGIN_ROOT}/bin/miblo-run" miblo.js --data "${CLAUDE_PLUGIN_DATA}"
```

Arguments: `$ARGUMENTS`

Rules: reply in the user's language; keep replies short; never show pairing tokens; run only the commands below.

Safety: never paste the arguments above into a command. Run only the exact commands below (they take no arguments).

## 0. Node.js check (always first)

Miblo runs on Node.js 20 or newer. Before anything else run `sh "${CLAUDE_PLUGIN_ROOT}/bin/miblo-run" --check --data "${CLAUDE_PLUGIN_DATA}"` (the first time it may download Node.js once, which can take a minute). It prints one line:

- `ok version=<v> node=<path>`: say nothing about it and go on.
- `missing reason=<why>`: tell the user in one or two sentences that Miblo needs Node.js 20 or newer and could not find or download it (give the reason in plain words). Then ask with AskUserQuestion: "Install Node.js now?" Yes/No.
  - No: say Miblo can't work until Node.js 20 or newer is installed (https://nodejs.org) and stop.
  - Yes: install it the platform's standard way. Say which command you are about to run before running it, and never run `sudo` without telling the user first; if a command needs a password you can't type, ask the user to run it themselves in a terminal and tell you when it's done.
    - macOS: if `command -v brew` finds Homebrew, run `brew install node`. Otherwise download the official installer from nodejs.org with `curl -fLo /tmp/miblo-node.pkg https://nodejs.org/dist/v24.21.0/node-v24.21.0.pkg` and open it with `open /tmp/miblo-node.pkg`; explain that the macOS installer opens and asks for their password, and wait until they say it's finished.
    - Linux: the distribution's package manager when it ships Node.js 20 or newer (e.g. `sudo dnf install nodejs` on Fedora, `sudo pacman -S nodejs` on Arch; many Debian/Ubuntu releases ship an older one: check with `apt-cache policy nodejs` first); otherwise point the user to https://nodejs.org/en/download.
    - Windows: `winget install OpenJS.NodeJS.LTS`.

    Then run the check again. `ok`: go on with the steps below. Still `missing`: say so, point to https://nodejs.org, and stop.

## `link-statusline`

Run `MIBLO link-statusline` and report the result.
