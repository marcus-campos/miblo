---
description: Update the Miblo plugin and a paired gadget's firmware
argument-hint: "[id] [--file path]"
allowed-tools: Bash(sh "${CLAUDE_PLUGIN_ROOT}/bin/miblo-run" miblo.js:*), Bash(sh "${CLAUDE_PLUGIN_ROOT}/bin/miblo-run" --check), Bash(claude plugin marketplace update miblo), Bash(claude plugin update miblo@miblo), AskUserQuestion
---

You manage Miblo desk gadgets with this CLI (call it `MIBLO` below):

```
sh "${CLAUDE_PLUGIN_ROOT}/bin/miblo-run" miblo.js --data "${CLAUDE_PLUGIN_DATA}"
```

Arguments: `$ARGUMENTS`

The arguments may hold a gadget id and `--file <path>` to a local `miblo-<board>-<version>.bin`; when a path is given, pass `--file '<path>'` to `check` and `open`.

Rules: reply in the user's language; keep replies short; never show pairing tokens; run only the commands below.

Safety: never paste the arguments above into a command. Build the command yourself from only: the fixed words shown here, an id copied exactly from `devices` in the output of `MIBLO update check` or `MIBLO status` (use it only if it is made of letters, digits, `-` and `_`), the code as the 4 digits the user typed, and a file path (ending in `.bin`) only as ONE single-quoted argument after `--file`, as below.

A path goes as ONE single-quoted argument with each `'` inside written as `'\''` (e.g. `MIBLO update check --file '/Users/ana/Downloads/miblo-geekmagic_ultra-1.14.0.bin'`). Never put it in double quotes, backticks or `$(...)`, and never add other shell commands.

## 0. Node.js check (always first)

Miblo runs on Node.js 20 or newer. Before anything else run `sh "${CLAUDE_PLUGIN_ROOT}/bin/miblo-run" --check` (the first time it may download Node.js once, which can take a minute). It prints one line:

- `ok version=<v> node=<path>`: say nothing about it and go on.
- `missing reason=<why>`: tell the user in one or two sentences that Miblo needs Node.js 20 or newer and could not find or download it (give the reason in plain words). Then ask with AskUserQuestion: "Install Node.js now?" Yes/No.
  - No: say Miblo can't work until Node.js 20 or newer is installed (https://nodejs.org) and stop.
  - Yes: install it the platform's standard way. Say which command you are about to run before running it, and never run `sudo` without telling the user first; if a command needs a password you can't type, ask the user to run it themselves in a terminal and tell you when it's done.
    - macOS: if `command -v brew` finds Homebrew, run `brew install node`. Otherwise use the official installer from nodejs.org: run `mktemp -d` and use the folder it prints as `<dir>`; run `curl -fLo <dir>/node.pkg https://nodejs.org/dist/v24.21.0/node-v24.21.0.pkg`; then verify it: `shasum -a 256 <dir>/node.pkg` must print `9831a74b04c270a429bd5a240e37712c4fe229b02b032e18ff2e0702c17c20fd`, and `pkgutil --check-signature <dir>/node.pkg` must show `Developer ID Installer: Node.js Foundation (HX7739G8FX)`. If either check fails, delete the folder, tell the user, and stop. Only then run `open <dir>/node.pkg`; explain that the macOS installer opens and asks for their password, and wait until they say it's finished.
    - Linux: the distribution's package manager when it ships Node.js 20 or newer (e.g. `sudo dnf install nodejs` on Fedora, `sudo pacman -S nodejs` on Arch; many Debian/Ubuntu releases ship an older one: check with `apt-cache policy nodejs` first); otherwise point the user to https://nodejs.org/en/download.
    - Windows: `winget install OpenJS.NodeJS.LTS`.

    Then run the check again. `ok`: go on with the steps below. Still `missing`: say so, point to https://nodejs.org, and stop.

## 1. Check

Run `MIBLO update check [id] [--file '<path>']`. It prints JSON:
`{ plugin: {current, latest, needsUpdate}, firmware: {latest, error?}, devices: [{id, name, online, fw, board, latest, boardMatch, needsUpdate}] }`.

- If `plugin.needsUpdate` is false/null and no device has `needsUpdate: true`: say everything is up to date (mention offline gadgets, gadgets with `busy: true` (low on memory just now: try again in a moment), gadgets with `unauthorized: true` (they no longer accept this computer's pairing: suggest `/miblo:pair`), `boardMatch: false`, or `firmware.error` if present) and stop.

## 2. Plugin (before the firmware)

If `plugin.needsUpdate` is true, ask with AskUserQuestion: "Update the Miblo plugin too? (<current> → <latest>)" Yes/No.
- Yes: run `claude plugin marketplace update miblo`, then `claude plugin update miblo@miblo`. If either fails (or `claude` is not on PATH), tell the user to type these themselves: `/plugin marketplace update miblo`, `/plugin update miblo@miblo`, `/reload-plugins`. On success, tell the user to type `/reload-plugins` (you cannot run slash commands). After `/reload-plugins`, the first Claude Code activity replaces the running local bridge with the new version on its own: nothing else to restart.

## 3. Firmware

Consider only devices with `needsUpdate: true`. If none, stop after step 2.
1. If several need an update and no id was given, ask which one with AskUserQuestion (one option per gadget name).
2. Confirm with AskUserQuestion: "Update <name> from <fw> to <latest>?" Yes/No. No: stop.
3. Run `MIBLO update open <id> [--file '<path>']`. It prints `{id, name, from, to, codeRequired}` or an error line (no releases → suggest `/miblo:update --file <path>`; board mismatch, checksum mismatch, offline, busy, "no longer accepts this computer's pairing" (suggest `/miblo:pair`), or "Try again in N s" → report it and stop).
4. If `codeRequired` is true: ask the user, as a plain chat message, to type the 4-digit code now shown on the gadget screen (e.g. "Type the 4-digit code shown on Miblo-B452") and wait for the reply. Do NOT use AskUserQuestion for the code (it has no free-text field).
5. Run `MIBLO update send <id> <code>` (no code when `codeRequired` is false). It uploads and waits up to 2 minutes for the gadget to reboot; tell the user it may take a minute and not to unplug the gadget.
   - `Wrong code.`: ask for the code again the same way (max 3 attempts in total), then run `send` again.
   - `Too many wrong codes. Try again in N s.` or `Another code is on the gadget screen. Try again in N s.`: report it and stop.
6. Report the result line (e.g. "Miblo-B452 updated from 0.2.1 to 0.2.2.").
