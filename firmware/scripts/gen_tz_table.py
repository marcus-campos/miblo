#!/usr/bin/env python3
"""Generates the IANA -> POSIX time zone table embedded in the firmware (flash, MIBLO_ROM).

Source: the IANA tz database as shipped by the PyPI `tzdata` package (pip install --upgrade
tzdata), never the system's files. Each zone's POSIX TZ rule is the footer of its TZif file (the
rule that applies after its last listed transition), checked against the zone's real offsets for
the next two years: when it disagrees (a zone whose listed transitions run past the footer, or a
rule POSIX cannot express), the fixed offset that is right for the longest part of that period is
used instead and the zone is reported. The names are those of the table already committed (a
stored zone must keep resolving) plus every zone of zone.tab and Etc/.

The output is, in the same (sorted) order:
  kTzNames: the names front-coded (~3.5 KB less flash than plain): per name, one byte 0x80 + the
            length of the prefix it shares with the previous name, then the rest of it
            ("\\x80Africa/Abidjan\\x87Accra..."). TzNames (miblo_tz.h) reads them back; GET
            /api/zones serves the plain list, "Africa/Abidjan\\nAfrica/Accra\\n..." (kTzNamesLen).
  kTzRules: "<+0330>-3:30\\nAEST-10\\n..."  (each distinct POSIX rule once, sorted)
  kTzRule:  {41, 41, ...}  (per name, the index of its rule in kTzRules)
Many zones share a rule (all of "GMT0", "CET-1CEST,M3.5.0,M10.5.0/3", ...): ~93 distinct rules
for ~460 names, so each rule is stored once and indexed (~4 KB less flash than one per name).
The release workflow runs it with the newest `tzdata` before building; the result is committed
when it changes (see firmware/README.md, "Time zones").

Usage: python3 firmware/scripts/gen_tz_table.py [--at YYYY-MM-DD]   (start of the checked period,
       default today)
"""
import argparse
import datetime as dt
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tzposix  # noqa: E402
from check_tz_table import read_table  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
DEST = os.path.join(HERE, "..", "lib", "miblo_core", "src")
MAX_NAME = 31  # Config::tz2 is char[32]
MAX_POSIX = 47  # the resolved rule buffer is 48 bytes
CHECK_DAYS = 730  # how far ahead each rule is checked against the zone's real offsets


def zone_names(path):
    """The names of the committed table, plus zone.tab and Etc/ of this tzdata."""
    names = set()
    if os.path.exists(os.path.join(DEST, "miblo_tz_table.cpp")):
        names.update(n for n, _ in read_table()[1])
    with open(os.path.join(path, "zone.tab"), encoding="utf-8") as f:
        names.update(line.split("\t")[2].strip() for line in f if not line.startswith("#"))
    with open(os.path.join(path, "..", "zones"), encoding="utf-8") as f:
        names.update(n for n in f.read().split() if n.startswith("Etc/"))
    return sorted(names)


def best_rule(path, name, start, end):
    """(rule, days it is wrong in [start, end)): the footer, else the best fixed offset."""
    zone = tzposix.zoneinfo.ZoneInfo(name)
    rule = tzposix.footer(path, name)
    bad, _ = tzposix.mismatch_seconds(rule, zone, start, end)
    if not bad:
        return rule, 0
    candidates = {rule}
    for t in [start] + [t for t, _, _ in tzposix.transitions(lambda u: tzposix.zone_offset(zone, u), start, end)]:
        local = dt.datetime.fromtimestamp(t, zone)
        candidates.add(tzposix.fixed_rule(int(local.utcoffset().total_seconds()), local.tzname()))
    scored = sorted((tzposix.mismatch_seconds(r, zone, start, end)[0], r != rule, r) for r in candidates)
    return scored[0][2], scored[0][0] / 86400


def fnv1a(s):
    h = 0x811C9DC5
    for b in s.encode("ascii"):
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def c_string(s, indent="    "):
    """Splits `s` into lines of C string literals (one zone per line)."""
    out = []
    for line in s.splitlines(keepends=True):
        esc = line.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
        out.append(f'{indent}"{esc}"')
    return "\n".join(out)


def c_bytes(values, indent="    ", per_line=24):
    """Comma-separated numbers, `per_line` per line."""
    lines = []
    for i in range(0, len(values), per_line):
        lines.append(indent + ", ".join(str(v) for v in values[i:i + per_line]) + ",")
    return "\n".join(lines)


def main(argv):
    ap = argparse.ArgumentParser(description="Regenerates miblo_tz_table.* from the PyPI tzdata package.")
    ap.add_argument("--at", help="start of the checked period, YYYY-MM-DD (default: today, UTC)")
    args = ap.parse_args(argv[1:])
    path, iana, pypi = tzposix.use_tzdata()
    day = dt.date.fromisoformat(args.at) if args.at else dt.datetime.now(dt.timezone.utc).date()
    start = int(dt.datetime(day.year, day.month, day.day, tzinfo=dt.timezone.utc).timestamp())
    end = start + CHECK_DAYS * 86400
    rows, approx = [], []
    for name in zone_names(path):
        if len(name) > MAX_NAME:  # (every name already in the table fits: none is ever dropped)
            print(f"skipped, longer than {MAX_NAME} bytes: {name}")
            continue
        rule, wrong_days = best_rule(path, name, start, end)
        for s in (name, rule):
            if not s or any(ord(c) < 0x21 or ord(c) > 0x7E for c in s):
                sys.exit(f"unexpected characters in {name!r}: {rule!r}")
        if len(name) > MAX_NAME or len(rule) > MAX_POSIX:
            sys.exit(f"too long: {name!r} {rule!r}")
        tzposix.Posix(rule)  # a rule the firmware's evaluator could not read fails here
        rows.append((name, rule))
        if wrong_days:
            approx.append(f"{name} {rule} (wrong {wrong_days:.1f} days of the next {CHECK_DAYS})")
    names = "".join(n + "\n" for n, _ in rows)
    packed, prev = [], ""
    for n, _ in rows:
        k = 0
        while k < min(len(n), len(prev)) and n[k] == prev[k]:
            k += 1
        packed.append(f"\\{0x80 + k:03o}" + n[k:])  # an octal escape: 3 digits, never longer
        prev = n
    distinct = sorted({r for _, r in rows})
    if len(distinct) > 256:
        sys.exit("more than 256 distinct rules: kTzRule needs wider entries")
    rules = "".join(r + "\n" for r in distinct)
    index = [distinct.index(r) for _, r in rows]
    src = f"the IANA tz database {iana} (PyPI tzdata {pypi}; public domain)"

    header = f"""#pragma once
// GENERATED by firmware/scripts/gen_tz_table.py from {src}.
// Do not edit by hand. Lookup helpers: miblo_tz.h.
// tzdata: {iana}
#include <stddef.h>

namespace miblo {{

constexpr size_t kTzCount = {len(rows)};
constexpr size_t kTzNamesLen = {len(names)};  // the plain list ("name\\n" per zone, GET /api/zones), bytes
constexpr size_t kTzNameMax = {max(len(n) for n, _ in rows)};  // the longest name, bytes
constexpr unsigned long kTzNamesHash = 0x{fnv1a(names):08x}UL;  // FNV-1a of the plain list (tests)
constexpr size_t kTzNamesPackedLen = {sum(1 + len(p) - 4 for p in packed)};  // bytes, without the NUL
// The sorted IANA names, front-coded (MIBLO_ROM): read them with TzNames (miblo_tz.h).
extern const char kTzNames[];
constexpr size_t kTzRuleCount = {len(distinct)};
// The distinct POSIX TZ rules, each followed by '\\n' (MIBLO_ROM).
extern const char kTzRules[];
// For each name (same order as kTzNames), the index of its rule in kTzRules (MIBLO_ROM).
extern const unsigned char kTzRule[];

}}  // namespace miblo
"""
    body = f"""// GENERATED by firmware/scripts/gen_tz_table.py from {src}.
// Do not edit by hand.
#include "miblo_tz_table.h"

#include "miblo_rom.h"

namespace miblo {{

// {sum(1 + len(p) - 4 for p in packed)} bytes ({len(names)} plain)
const char kTzNames[] MIBLO_ROM =
{chr(10).join(f'    "{p}"' for p in packed)};

// {len(rules)} bytes
const char kTzRules[] MIBLO_ROM =
{c_string(rules)};

const unsigned char kTzRule[] MIBLO_ROM = {{
{c_bytes(index)}
}};

}}  // namespace miblo
"""
    with open(os.path.join(DEST, "miblo_tz_table.h"), "w", encoding="utf-8", newline="\n") as f:
        f.write(header)
    with open(os.path.join(DEST, "miblo_tz_table.cpp"), "w", encoding="utf-8", newline="\n") as f:
        f.write(body)
    print(f"tzdata {iana} (PyPI {pypi}): {len(rows)} zones, names {len(names)} B, {len(distinct)} rules {len(rules)} B")
    for line in approx:
        print(f"not expressible as a POSIX rule, approximated: {line}")


if __name__ == "__main__":
    main(sys.argv)
