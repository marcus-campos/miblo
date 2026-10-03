#!/bin/sh
# Miblo one-step installer for macOS and Linux.
#
#   curl -fsSL https://raw.githubusercontent.com/marcus-campos/miblo/main/install.sh | sh
#   curl -fsSL https://raw.githubusercontent.com/marcus-campos/miblo/main/install.sh | sh -s -- --no-pair
#
# "Install Miblo.command" is a byte-for-byte copy of this file, so it can be double-clicked on a
# Mac (a test keeps the two identical). It finds the Claude Code CLI (the terminal one, or the one
# the Claude desktop app bundles), adds the Miblo marketplace and installs the plugin, or updates
# both when they are already there. Then it pairs the gadget, so /miblo:pair is not needed: it
# gets Node.js ready through the plugin's launcher, finds the Miblo on the network and asks for the
# 4-digit code on its screen (typed in the terminal). --no-pair, or a run with no terminal to type
# in, skips that step. It never uses sudo and never prints a token.
#
# Exit codes (pairing never changes them: the plugin is installed either way):
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
#   MIBLO_NO_PAIR=1                same as --no-pair
#   MIBLO_TTY=/path                where the pairing answers are read from (default /dev/tty:
#                                  through `curl | sh`, standard input is this script)

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

# --- Pairing ---------------------------------------------------------------------------------

# Opens the terminal on file descriptor 3 for the answers. Fails when there is none (a run from
# another program, CI): opening /dev/tty without a controlling terminal fails. Tried in a subshell
# first, since a failed `exec` redirection ends a POSIX shell.
open_tty() {
  tty=${MIBLO_TTY:-/dev/tty}
  (exec 3<"$tty") 2>/dev/null || return 1
  exec 3<"$tty"
}

# ask <prompt>: one line typed by the user, in $answer. The end of input counts as "q".
ask() {
  printf '%s' "$1"
  IFS= read -r answer <&3 || answer=q
}

# The installed plugin's folder: its installPath from `claude plugin list --json`, else the newest
# version in Claude Code's plugin cache. Prints nothing when neither has the launcher.
plugin_dir() {
  pd_block=$(plugin_block "$("$1" plugin list --json </dev/null 2>/dev/null || true)")
  pd_dir=$(printf '%s\n' "$pd_block" | sed -n 's/^ *"installPath": *"\(.*\)",\{0,1\} *$/\1/p' | head -n 1)
  if [ -n "$pd_dir" ] && [ -f "$pd_dir/bin/miblo-run" ]; then
    printf '%s\n' "$pd_dir"
    return 0
  fi
  pd_base="${CLAUDE_CONFIG_DIR:-$HOME/.claude}/plugins/cache/miblo/miblo"
  [ -d "$pd_base" ] || return 0
  ls "$pd_base" | sort -t. -k1,1nr -k2,2nr -k3,3nr | while IFS= read -r pd_ver; do
    if [ -f "$pd_base/$pd_ver/bin/miblo-run" ]; then
      printf '%s\n' "$pd_base/$pd_ver"
      break
    fi
  done
}

# The plugin's data folder, the one Claude Code passes to it as CLAUDE_PLUGIN_DATA:
# <config>/plugins/data/miblo-miblo next to <config>/plugins/cache/miblo/miblo/<version>.
data_dir() {
  case $1 in
    */cache/miblo/miblo/*) printf '%s/data/miblo-miblo\n' "${1%/cache/miblo/miblo/*}" ;;
    *) printf '%s/plugins/data/miblo-miblo\n' "${CLAUDE_CONFIG_DIR:-$HOME/.claude}" ;;
  esac
}

# The plugin's CLI, the same one /miblo:pair runs.
miblo_cli() {
  sh "$plugin/bin/miblo-run" miblo.js --data "$data" "$@" </dev/null
}

pair_later() {
  say ""
  say "Next steps:"
  say "  1. Restart Claude Code: quit and reopen the Claude desktop app (then open the Code tab),"
  say "     your IDE's Claude Code panel, or the claude command in your terminal."
  say "  2. Plug in your Miblo and connect it to Wi-Fi (the screen shows a QR code)."
  say "  3. In Claude Code, type /miblo:pair and enter the 4-digit code from the screen."
}

pair_failed() {
  say ""
  say "Miblo is installed, but not paired yet. To pair it later, open Claude Code and type"
  say "/miblo:pair (restart Claude Code first if it was open)."
}

# Line <n> of the discovered gadgets ("id<TAB>name<TAB>address") into $name and $addr.
pick() {
  pk_line=$(printf '%s\n' "$list" | sed -n "$1p")
  pk_rest=${pk_line#*	}
  name=${pk_rest%%	*}
  addr=${pk_rest#*	}
}

# Finds the gadget to pair: sets $addr and $name, or fails when the user skips.
choose_gadget() {
  while :; do
    say "Searching your network..."
    list=$(miblo_cli discover | grep '	')
    count=$(printf '%s' "$list" | grep -c '	')
    if [ "$count" -eq 1 ]; then
      pick 1
      say "Found $name ($addr)."
      return 0
    fi
    if [ "$count" -gt 1 ]; then
      say "Found $count Miblos:"
      i=1
      while [ "$i" -le "$count" ]; do
        pick "$i"
        say "  $i) $name ($addr)"
        i=$((i + 1))
      done
      while :; do
        ask "Which one? Type its number (or q to skip): "
        case $answer in
          q | Q) return 1 ;;
          '' | *[!0-9]*) ;;
          *)
            if [ "$answer" -ge 1 ] && [ "$answer" -le "$count" ]; then
              pick "$answer"
              return 0
            fi
            ;;
        esac
        say "Type a number from 1 to $count."
      done
    fi
    say ""
    say "No Miblo found on this network. Check that your Miblo:"
    say "  - is plugged in and switched on;"
    say "  - is on the same Wi-Fi as this computer (if its screen shows a QR code, scan it with"
    say "    your phone to connect it to Wi-Fi first);"
    say "  - shows a 4-digit pairing code on its screen."
    ask "Press Return to search again, type the IP address shown at the bottom of its screen, or q to skip: "
    case $answer in
      q | Q) return 1 ;;
      '') ;;
      *[!0-9.:]* | .* | *.) say "That is not an IP address." ;;
      *.*.*.*)
        addr=$answer
        name="the Miblo at $answer"
        return 0
        ;;
      *) say "That is not an IP address." ;;
    esac
  done
}

pair_gadget() {
  step "Pairing your Miblo"
  plugin=$(plugin_dir "$1")
  if [ -z "$plugin" ]; then
    warn "Could not find the installed plugin's folder."
    pair_failed
    return 1
  fi
  data=$(data_dir "$plugin")

  # Downloads Node.js once when this computer has none; the launcher shows progress on stderr.
  say "Getting Node.js ready (the first time this can take a minute)..."
  check=$(sh "$plugin/bin/miblo-run" --check --data "$data" </dev/null)
  case $check in
    ok*) ;;
    *)
      say ""
      say "Miblo needs Node.js 20 or newer and could not set it up (${check#missing reason=})."
      say "Install it from https://nodejs.org, or open Claude Code and type /miblo:pair: it offers"
      say "to install Node.js for you, then pairs."
      return 1
      ;;
  esac

  choose_gadget || {
    pair_failed
    return 1
  }
  say ""
  say "Look at the screen of $name: it shows a 4-digit pairing code. (No code? Open its settings"
  say "page in a browser and click \"Show pairing code on the device\".)"
  tries=0
  while [ "$tries" -lt 3 ]; do
    ask "Type the 4-digit code (or q to skip): "
    case $answer in
      q | Q) break ;;
      [0-9][0-9][0-9][0-9]) ;;
      *)
        say "The code is 4 digits, like 4827."
        continue
        ;;
    esac
    out=$(miblo_cli pair "$addr" "$answer")
    case $out in
      "Paired with "*)
        pname=$(printf '%s\n' "$out" | sed -n 's/^Paired with \(.*\) ([^()]*) at .*$/\1/p')
        [ -n "$pname" ] || pname=$name
        case $pname in
          Miblo*) ;;
          *) pname="Miblo $pname" ;;
        esac
        say ""
        say "$pname paired. Open Claude (terminal, desktop app → Code, or your IDE) and it will start showing your sessions."
        say "If Claude Code is already open, quit and reopen it first. To see your usage limits on"
        say "the Miblo too, type /miblo:link-statusline in Claude Code."
        return 0
        ;;
      "Wrong pairing code."*)
        tries=$((tries + 1))
        if [ "$tries" -lt 3 ]; then
          say "Wrong code. Check the screen and try again."
        else
          say "Wrong code, 3 times."
        fi
        ;;
      *)
        say "$out"
        break
        ;;
    esac
  done
  pair_failed
  return 1
}

main() {
  no_pair=0
  [ "${MIBLO_NO_PAIR:-}" = 1 ] && no_pair=1
  for arg in "$@"; do
    case $arg in
      --no-pair) no_pair=1 ;;
      *) warn "Unknown option $arg (ignored)." ;;
    esac
  done

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
  if [ "$no_pair" = 1 ]; then
    pair_later
  elif ! open_tty; then
    say ""
    say "Skipping pairing: there is no terminal to type the pairing code in."
    pair_later
  else
    pair_gadget "$claude"
  fi
  finish 0
}

main "$@"
