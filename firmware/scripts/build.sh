#!/usr/bin/env bash
# Produces the release binary: firmware/dist/miblo-<board>-<version>.bin
# Usage: firmware/scripts/build.sh [board]      (default: geekmagic_ultra)
#
# PlatformIO binary resolution (env PIO):
#   1. If $PIO is set, use it as-is (e.g. `PIO=pio ./scripts/build.sh geekmagic_ultra` in CI,
#      where PlatformIO is on PATH rather than in the local venv).
#   2. Otherwise, use firmware/.venv/bin/pio if it exists (the local dev venv).
#   3. Otherwise, fall back to `pio` on PATH.
# Resolved relative to the script's own location, so this works from any cwd.
set -euo pipefail

BOARD="${1:-geekmagic_ultra}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

if [ -z "${PIO:-}" ]; then
  if [ -x "$ROOT/.venv/bin/pio" ]; then
    PIO="$ROOT/.venv/bin/pio"
  else
    PIO="pio"
  fi
fi
if ! command -v "$PIO" >/dev/null 2>&1; then
  echo "PlatformIO not found (PIO=$PIO)" >&2
  echo "Install: python3 -m venv firmware/.venv && firmware/.venv/bin/pip install platformio" >&2
  exit 1
fi

VERSION="$(sed -n 's/^#define MIBLO_FW_VERSION "\(.*\)"$/\1/p' "$ROOT/include/miblo_version.h")"
if [ -z "$VERSION" ]; then
  echo "MIBLO_FW_VERSION not found in include/miblo_version.h" >&2
  exit 1
fi

cd "$ROOT"
"$PIO" test -e native
"$PIO" run -e "$BOARD"

mkdir -p dist
OUT="dist/miblo-$BOARD-$VERSION.bin"
cp ".pio/build/$BOARD/firmware.bin" "$OUT"
echo "OK: firmware/$OUT ($(wc -c < "$OUT" | tr -d ' ') bytes)"
if command -v shasum >/dev/null 2>&1; then shasum -a 256 "$OUT"; else sha256sum "$OUT"; fi
