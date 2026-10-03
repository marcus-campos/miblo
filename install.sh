#!/bin/sh
# Miblo one-step installer for macOS and Linux.
#
#   curl -fsSL https://raw.githubusercontent.com/marcus-campos/miblo/main/install.sh | sh
#
# "Install Miblo.command" is a byte-for-byte copy of this file, so it can be double-clicked on a
# Mac (a test keeps the two identical). It finds the Claude Code CLI (the terminal one, or the one
# the Claude desktop app bundles), adds the Miblo marketplace and installs the plugin, or updates
# both when they are already there. It never uses sudo and never prints a token.
#
# Exit codes:
#   0  installed or updated
#   1  no Claude Code found
#   2  the Miblo marketplace could not be added or updated
#   3  the plugin could not be installed or updated
#   4  run as root (it would install for the wrong user)
#
# Environment (optional):
#   MIBLO_CLAUDE=/path/to/claude   use that CLI instead of searching
#   MIBLO_SYSTEM_BIN_DIRS="..."    system directories searched after PATH (default below; the
#                                  tests point it elsewhere so a real Claude Code is never used)

# Everything sits in functions and runs from the last line, so a download cut short by the
# network runs nothing at all.

MIBLO_REPO_URL="https://github.com/marcus-campos/miblo.git"
MIBLO_MARKETPLACE="miblo"
MIBLO_PLUGIN="miblo@miblo"

say() { printf '%s\n' "$*"; }
step() { printf '\n==> %s\n' "$*"; }
warn() { printf 'Warning: %s\n' "$*" >&2; }

# Double-clicked from Finder, Terminal closes the window as soon as the script ends: keep it open
# so the result can be read.
pause_if_double_clicked() {
  case "$0" in
    *.command)
      if [ -t 0 ]; then
        printf '\nPress Return to close this window.'
        read -r _ || true
      fi
      ;;
  esac
}

finish() {
  code=$1
  pause_if_double_clicked
  exit "$code"
}

# A candidate is usable when it is an executable file that answers --version.
usable() {
  [ -n "$1" ] && [ -f "$1" ] && [ -x "$1" ] && "$1" --version </dev/null >/dev/null 2>&1
}

# The Claude desktop app keeps its own Claude Code under
#   ~/Library/Application Support/Claude/claude-code/<version>/<hash>/claude.app/Contents/MacOS/claude
# Prints the newest one (highest version) that runs, or nothing.
desktop_claude() {
  base="$HOME/Library/Application Support/Claude/claude-code"
  [ -d "$base" ] || return 0
  list=""
  for bin in "$base"/*/*/claude.app/Contents/MacOS/claude; do
    [ -f "$bin" ] || continue
    ver=${bin#"$base"/}
    ver=${ver%%/*}
    list="$list$ver	$bin
"
  done
  [ -n "$list" ] || return 0
  # Newest version first (numeric per dotted part), then the first of those that runs.
  printf '%s' "$list" | sort -t. -k1,1nr -k2,2nr -k3,3nr | while IFS='	' read -r _ bin; do
    if usable "$bin"; then
      printf '%s\n' "$bin"
      break
    fi
  done
}

find_claude() {
  if [ -n "${MIBLO_CLAUDE:-}" ]; then
    if usable "$MIBLO_CLAUDE"; then
      printf '%s\n' "$MIBLO_CLAUDE"
      return 0
    fi
    warn "MIBLO_CLAUDE=$MIBLO_CLAUDE is not a working Claude Code CLI; searching instead."
  fi
  if found=$(command -v claude 2>/dev/null) && usable "$found"; then
    printf '%s\n' "$found"
    return 0
  fi
  # Usual install locations that a double-clicked script's PATH may not include.
  for bin in "$HOME/.local/bin/claude" "$HOME/.claude/local/claude" "$HOME/.npm-global/bin/claude"; do
    if usable "$bin"; then
      printf '%s\n' "$bin"
      return 0
    fi
  done
  for dir in ${MIBLO_SYSTEM_BIN_DIRS-/opt/homebrew/bin /usr/local/bin}; do
    if usable "$dir/claude"; then
      printf '%s\n' "$dir/claude"
      return 0
    fi
  done
  desktop_claude
}

no_claude() {
  say ""
  say "Claude Code was not found on this computer."
  say ""
  say "Miblo is a Claude Code plugin, so Claude Code comes first. Either:"
  say "  - install the Claude desktop app from https://claude.ai/download, open it once,"
  say "    sign in and open the Code tab; or"
  say "  - install Claude Code for the terminal: https://docs.claude.com/en/docs/claude-code/setup"
  say ""
  say "Then run this installer again."
}

# Prints the JSON block of one installed plugin (from `claude plugin list --json`), or nothing.
plugin_block() {
  printf '%s\n' "$1" | awk -v id="\"$MIBLO_PLUGIN\"" '
    index($0, "\"id\"") && index($0, id) { on = 1 }
    on { print }
    on && /}/ { exit }'
}

main() {
  say "Miblo installer"
  say "---------------"

  if [ "$(id -u 2>/dev/null || echo 1)" = "0" ]; then
    say ""
    say "Please run this installer as yourself, not as root or with sudo:"
    say "the plugin is installed for the user who runs it."
    finish 4
  fi

  step "Looking for Claude Code"
  claude=$(find_claude)
  if [ -z "$claude" ]; then
    no_claude
    finish 1
  fi
  version=$("$claude" --version </dev/null 2>/dev/null | head -n 1)
  say "Found: $claude${version:+ ($version)}"

  # Every claude call reads from /dev/null: when this script arrives through `curl | sh`, its
  # standard input is the rest of the script, which no prompt may swallow.
  step "Adding the Miblo marketplace"
  markets=$("$claude" plugin marketplace list --json </dev/null 2>/dev/null || true)
  if printf '%s\n' "$markets" | grep -q "\"name\": *\"$MIBLO_MARKETPLACE\""; then
    say "Already added; refreshing it."
    if ! "$claude" plugin marketplace update "$MIBLO_MARKETPLACE" </dev/null; then
      warn "Could not refresh the marketplace (offline?). Continuing with the copy on disk."
    fi
  elif ! "$claude" plugin marketplace add "$MIBLO_REPO_URL" </dev/null; then
    say ""
    say "Could not add the Miblo marketplace. Check your internet connection and try again."
    say "To do it by hand, in Claude Code run: /plugin marketplace add marcus-campos/miblo"
    finish 2
  fi

  step "Installing the Miblo plugin"
  plugins=$("$claude" plugin list --json </dev/null 2>/dev/null || true)
  block=$(plugin_block "$plugins")
  if [ -n "$block" ]; then
    say "Already installed; updating it."
    if ! "$claude" plugin update "$MIBLO_PLUGIN" </dev/null; then
      say ""
      say "Could not update the plugin. Check your internet connection and try again."
      finish 3
    fi
    if printf '%s\n' "$block" | grep -q '"enabled": *false'; then
      say "It was turned off; turning it back on."
      "$claude" plugin enable "$MIBLO_PLUGIN" </dev/null || warn "Could not enable it: run /plugin in Claude Code to turn it on."
    fi
  elif ! "$claude" plugin install "$MIBLO_PLUGIN" </dev/null; then
    say ""
    say "Could not install the plugin. Check your internet connection and try again."
    say "To do it by hand, in Claude Code run: /plugin install miblo@miblo"
    finish 3
  fi

  say ""
  say "Miblo is installed."
  say ""
  say "Next steps:"
  say "  1. Restart Claude Code: quit and reopen the Claude desktop app (then open the Code tab),"
  say "     your IDE's Claude Code panel, or the claude command in your terminal."
  say "  2. Plug in your Miblo and connect it to Wi-Fi (the screen shows a QR code)."
  say "  3. In Claude Code, type /miblo:pair and enter the 4-digit code from the screen."
  finish 0
}

main "$@"
