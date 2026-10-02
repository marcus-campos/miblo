#!/usr/bin/env python3
"""Checks the committed time zone table (lib/miblo_core/src/miblo_tz_table.*) against the IANA tz
database of the installed PyPI `tzdata` package (pip install --upgrade tzdata).

  python3 firmware/scripts/check_tz_table.py [--from 2026-01-01] [--to 2028-01-01] [--step 900]
      Every zone of the table, every `step` seconds of the period plus one second either side of
      each real transition: the offset the firmware works out from the zone's POSIX rule against
      zoneinfo's. Prints each zone that differs (and for how long); exits 1 when any does.
  python3 firmware/scripts/check_tz_table.py --version
      Compares the tzdata release the table was generated from with the installed one; exits 2
      when the table is older (CI prints it as a warning: the release workflow regenerates it).
"""
import argparse
import datetime as dt
import os
import re
import sys
from concurrent.futures import ProcessPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tzposix  # noqa: E402

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib", "miblo_core", "src")


def read_table():
    """(tzdata version or None, [(name, rule)]) from the committed miblo_tz_table.*."""
    with open(os.path.join(SRC, "miblo_tz_table.h"), encoding="utf-8") as f:
        m = re.search(r"^// tzdata: (\S+)", f.read(), re.M)
    with open(os.path.join(SRC, "miblo_tz_table.cpp"), encoding="utf-8") as f:
        cpp = f.read()

    def literal(var):
        block = re.search(rf"{var}\[\] MIBLO_ROM =\n(.*?);\n", cpp, re.S).group(1)
        raw = "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', block))
        return raw.encode("ascii").decode("unicode_escape")

    names, prev = [], ""
    for chunk in re.split(r"(?=[\x80-\xff])", literal("kTzNames")):
        if chunk:
            prev = prev[: ord(chunk[0]) - 0x80] + chunk[1:]
            names.append(prev)
    rules = literal("kTzRules").split("\n")[:-1]
    block = re.search(r"kTzRule\[\] MIBLO_ROM = \{(.*?)\};", cpp, re.S).group(1)
    index = [int(v) for v in re.findall(r"\d+", block)]
    if len(index) != len(names):
        raise SystemExit("table: names and rule indexes differ in length")
    return (m.group(1) if m else None), [(n, rules[i]) for n, i in zip(names, index)]


def check_zone(args):
    name, rule, start, end, step = args
    tzposix.use_tzdata()
    zone = tzposix.zoneinfo.ZoneInfo(name)
    p = tzposix.Posix(rule)
    instants = set(range(start, end, step))
    for t, _, _ in tzposix.transitions(lambda u: tzposix.zone_offset(zone, u), start, end):
        instants.update((t - 1, t, t + 1))
    bad = sorted(t for t in instants if p.offset(t) != tzposix.zone_offset(zone, t))
    bad_secs, _ = tzposix.mismatch_seconds(rule, zone, start, end) if bad else (0, None)
    return name, rule, len(bad), bad[0] if bad else None, bad_secs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--from", dest="start", default="2026-01-01")
    ap.add_argument("--to", dest="end", default="2028-01-01")
    ap.add_argument("--step", type=int, default=900)
    ap.add_argument("--version", action="store_true")
    a = ap.parse_args()
    _, iana, pypi = tzposix.use_tzdata()
    made_from, table = read_table()
    if a.version:
        print(f"table: tzdata {made_from or 'unknown'}; installed: tzdata {iana} (PyPI {pypi})")
        return 2 if made_from != iana else 0
    epoch = lambda s: int(dt.datetime.fromisoformat(s).replace(tzinfo=dt.timezone.utc).timestamp())
    start, end = epoch(a.start), epoch(a.end)
    jobs = [(n, r, start, end, a.step) for n, r in table]
    with ProcessPoolExecutor() as pool:
        results = list(pool.map(check_zone, jobs, chunksize=8))
    bad = [r for r in results if r[2]]
    for name, rule, count, first, secs in bad:
        when = dt.datetime.fromtimestamp(first, dt.timezone.utc).strftime("%Y-%m-%d %H:%M")
        print(f"MISMATCH {name:32} {rule:36} {count:6} samples, {secs / 86400:6.1f} days, from {when}")
    print(f"{len(table)} zones, {a.start}..{a.end}, tzdata {iana}: {len(bad)} differ")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
