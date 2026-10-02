"""Shared helpers for gen_tz_table.py and check_tz_table.py: the IANA tz data of the PyPI `tzdata`
package (never the system's), and a POSIX TZ rule evaluator that follows the firmware's own
(lib/miblo_core/src/miblo_zone.cpp utcOffset: Mm.w.d transitions, times may be negative or > 24h).
"""
import datetime as dt
import importlib.resources
import os
import re
import zoneinfo


def tzdata_dir():
    """(zoneinfo directory, IANA version, PyPI version) of the installed `tzdata` package."""
    try:
        import tzdata  # noqa: F401  (the PyPI package; `pip install tzdata`)
    except ImportError:
        raise SystemExit("the PyPI `tzdata` package is required: pip install --upgrade tzdata")
    from importlib.metadata import version

    root = importlib.resources.files("tzdata")
    return str(root / "zoneinfo"), tzdata.IANA_VERSION, version("tzdata")


def use_tzdata():
    """Points zoneinfo at the `tzdata` package only, so the system's tz files never take part."""
    path, iana, pypi = tzdata_dir()
    zoneinfo.reset_tzpath(to=[path])
    return path, iana, pypi


def footer(path, name):
    """The POSIX TZ string at the end of a TZif (v2+) file: the rule after its last transition."""
    with open(os.path.join(path, *name.split("/")), "rb") as f:
        data = f.read()
    if not data.startswith(b"TZif") or data[4:5] < b"2":
        raise SystemExit(f"{name}: not a TZif v2+ file")
    return data.rstrip(b"\n").rsplit(b"\n", 1)[1].decode("ascii")


_NAME = r"(?:<[^>]+>|[A-Za-z]{3,})"
_OFF = r"[+-]?\d+(?::\d+){0,2}"
_RULE = re.compile(rf"^{_NAME}({_OFF})(?:{_NAME}({_OFF})?(?:,(M[^,]+),(M[^,]+))?)?$")


def _secs(s):
    sign = -1 if s.startswith("-") else 1
    parts = [int(p) for p in s.lstrip("+-").split(":")]
    return sign * sum(v * u for v, u in zip(parts, (3600, 60, 1)))


def _days_from_civil(y, m, d):
    return (dt.date(y, m, d) - dt.date(1970, 1, 1)).days


def _transition(spec, year):
    """'Mm.w.d[/time]' in `year`: (day since 1970, local seconds of day)."""
    date, _, at = spec[1:].partition("/")
    m, w, d = (int(v) for v in date.split("."))
    first = _days_from_civil(year, m, 1)
    nxt = _days_from_civil(year + (m == 12), m % 12 + 1, 1)
    first_wday = (first + 4) % 7  # 1970-01-01 was a Thursday
    day = first + (d + 7 - first_wday) % 7 + 7 * (w - 1)
    while day >= nxt:
        day -= 7
    return day, _secs(at) if at else 7200


class Posix:
    """A POSIX TZ rule, evaluated like the firmware does."""

    def __init__(self, rule):
        m = _RULE.match(rule)
        if not m:
            raise ValueError(f"unsupported POSIX rule {rule!r}")
        self.rule = rule
        self.std = _secs(m.group(1))  # west of Greenwich
        self.has_dst = bool(m.group(3))
        self.dst = _secs(m.group(2)) if m.group(2) else self.std - 3600
        self.start, self.end = m.group(3), m.group(4)
        self._years = {}

    def _bounds(self, year):
        if year not in self._years:
            d0, t0 = _transition(self.start, year)
            d1, t1 = _transition(self.end, year)
            self._years[year] = (d0 * 86400 + t0 + self.std, d1 * 86400 + t1 + self.dst)
        return self._years[year]

    def offset(self, utc):
        """Seconds east of UTC at `utc` (epoch seconds)."""
        if not self.has_dst:
            return -self.std
        year = (dt.datetime(1970, 1, 1) + dt.timedelta(seconds=(utc - self.std) // 86400 * 86400)).year
        start, end = self._bounds(year)
        dst = (start <= utc < end) if start < end else (utc >= start or utc < end)
        return -(self.dst if dst else self.std)


def zone_offset(zone, utc):
    """Seconds east of UTC at `utc` per zoneinfo."""
    return int(dt.datetime.fromtimestamp(utc, zone).utcoffset().total_seconds())


def transitions(offset_fn, start, end, step=86400):
    """The instants in [start, end) where offset_fn changes: (utc of the first second with the new
    offset, offset before, offset after). Steps a day at a time and bisects each change down to the
    second (no zone changes twice within a day)."""
    out = []
    t, cur = start, offset_fn(start)
    while t < end:
        n = min(t + step, end)
        o = offset_fn(n)
        if o != cur:
            lo, hi = t, n  # offset_fn(lo) == cur, offset_fn(hi) != cur
            while hi - lo > 1:
                mid = (lo + hi) // 2
                if offset_fn(mid) == cur:
                    lo = mid
                else:
                    hi = mid
            out.append((hi, cur, offset_fn(hi)))
            cur = offset_fn(hi)
        t = n
    return out


def mismatch_seconds(rule, zone, start, end):
    """How long in [start, end) the POSIX rule and the zone disagree, and the first instant they do."""
    p = Posix(rule)
    cuts = {start, end}
    for fn in (p.offset, lambda t: zone_offset(zone, t)):
        cuts.update(t for t, _, _ in transitions(fn, start, end))
    cuts = sorted(cuts)
    bad, first = 0, None
    for a, b in zip(cuts, cuts[1:]):
        if p.offset(a) != zone_offset(zone, a):
            bad += b - a
            first = a if first is None else first
    return bad, first


def fixed_rule(offset_east, abbr):
    """A POSIX rule for a fixed offset: '<+0545>-5:45', 'GMT0', 'JST-9'."""
    name = abbr if re.fullmatch(r"[A-Za-z]{3,}", abbr or "") else None
    if not name:
        a = abs(offset_east)
        hhmm = f"{a // 3600:02d}" + (f"{a // 60 % 60:02d}" if a % 3600 else "")
        name = f"<{'-' if offset_east < 0 else '+'}{hhmm}>"
    west = -offset_east
    a = abs(west)
    off = f"{a // 3600}" + (f":{a // 60 % 60:02d}" if a % 3600 else "")
    return f"{name}{'-' if west < 0 else ''}{off}"
