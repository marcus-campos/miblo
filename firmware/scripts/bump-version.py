#!/usr/bin/env python3
"""Choose the next release version and write it everywhere it lives.

WHY TWO FILES: the firmware reports MIBLO_FW_VERSION (boot screen, /api/info) and the plugin
reports plugin/.claude-plugin/plugin.json "version". The release workflow refuses a tag unless
both match it, so they always move together here.

OUTPUT CONTRACT (the Makefile captures stdout): the chosen version goes ALONE to stdout; the
prompt, warnings and errors all go to stderr.

Usage:
  bump-version.py            # asks (when there is a terminal); Enter = patch
  bump-version.py 0.3.0      # explicit, no question

Without a terminal (CI, pipe) it keeps the current version instead of hanging.
"""

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FW_HEADER = ROOT / "firmware" / "include" / "miblo_version.h"
PLUGIN_JSON = ROOT / "plugin" / ".claude-plugin" / "plugin.json"
SEMVER = re.compile(r"^(\d+)\.(\d+)\.(\d+)$")
FW_RE = re.compile(r'(#define MIBLO_FW_VERSION ")([^"]*)(")')
PLUGIN_RE = re.compile(r'("version"\s*:\s*")([^"]*)(")')


def err(msg: str) -> None:
    sys.stderr.write(msg + "\n")


def current_versions() -> tuple:
    fw = FW_RE.search(FW_HEADER.read_text())
    plugin = json.loads(PLUGIN_JSON.read_text()).get("version")
    if not fw or not SEMVER.match(fw.group(2)):
        raise SystemExit(f"error: MIBLO_FW_VERSION missing or invalid in {FW_HEADER}")
    if not plugin or not SEMVER.match(plugin):
        raise SystemExit(f"error: version missing or invalid in {PLUGIN_JSON}")
    return fw.group(2), plugin


def bump(version: str, part: str) -> str:
    major, minor, patch = (int(x) for x in SEMVER.match(version).groups())
    if part == "major":
        return f"{major + 1}.0.0"
    if part == "minor":
        return f"{major}.{minor + 1}.0"
    return f"{major}.{minor}.{patch + 1}"


def write(version: str) -> None:
    # Replace only the values in place so neither file gets reformatted (clean diffs).
    for path, pattern in ((FW_HEADER, FW_RE), (PLUGIN_JSON, PLUGIN_RE)):
        text = path.read_text()
        updated, n = pattern.subn(lambda m: m.group(1) + version + m.group(3), text, count=1)
        if n != 1:
            raise SystemExit(f"error: could not find the version field in {path}")
        path.write_text(updated)


def ask(current: str) -> str:
    options = [
        ("patch", bump(current, "patch")),
        ("minor", bump(current, "minor")),
        ("major", bump(current, "major")),
        ("keep", current),
    ]
    err(f"\nCurrent version: {current}")
    for i, (label, version) in enumerate(options, 1):
        err(f"  {i}) {label:<6} {version}")
    err("  5) other  (type it)")
    while True:
        sys.stderr.write("\nChoose [1-5, Enter = patch]: ")
        sys.stderr.flush()
        answer = sys.stdin.readline().strip()
        if answer == "":
            return options[0][1]
        if answer in ("1", "2", "3", "4"):
            return options[int(answer) - 1][1]
        if answer == "5":
            sys.stderr.write("Version (x.y.z): ")
            sys.stderr.flush()
            typed = sys.stdin.readline().strip()
            if SEMVER.match(typed):
                return typed
            err(f'"{typed}" is not x.y.z.')
            continue
        err(f'"{answer}" is not an option.')


def main() -> None:
    fw, plugin = current_versions()
    if fw != plugin:
        err(f"warning: firmware ({fw}) and plugin ({plugin}) versions differ; both will be set.")
    current = fw
    explicit = sys.argv[1].strip() if len(sys.argv) > 1 else ""
    if explicit:
        if not SEMVER.match(explicit):
            raise SystemExit(f'error: version "{explicit}" is not x.y.z')
        chosen = explicit
    elif sys.stdin.isatty():
        chosen = ask(current)
    else:
        err(f"no terminal to ask — keeping the current version ({current}).")
        chosen = current
    if chosen != fw or chosen != plugin:
        write(chosen)
        err(f"version: {current} -> {chosen} (miblo_version.h + plugin.json)")
    sys.stdout.write(chosen)


if __name__ == "__main__":
    main()
