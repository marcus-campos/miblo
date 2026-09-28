#!/usr/bin/env python3
"""Flash many Miblo gadgets over the LAN (bench tool, stdlib only, Python 3.9+).

Every unit must already be on the bench Wi-Fi (join it once through the stock GeekMagic setup
portal). The script finds the units, works out what each one runs and installs Miblo:

  stock GeekMagic  -> stage 1: upload the installer (loader) to http://<ip>/update, wait for it
                      to boot (GET /info -> {"app":"miblo-loader"}), then stage 2
  Miblo installer  -> stage 2: upload the full image to http://<ip>/update, wait for
                      GET /api/info and check that "fw" is the expected version
  Miblo            -> skipped when already on the expected version; otherwise updated only with
                      --update (POST /update/open shows a 4-digit code on the screen, you type it
                      here, then the image goes to POST /update?code=XXXX); one unit at a time
  anything else    -> skipped

Examples:
  flash-fleet.py --subnet 192.168.0.0/24 --dry-run
  flash-fleet.py --subnet 192.168.0.0/24
  flash-fleet.py --host 192.168.0.41 --host 192.168.0.42 --update
"""
from __future__ import annotations

import argparse
import ipaddress
import json
import re
import socket
import sys
import threading
import time
import urllib.error
import urllib.request
import uuid
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, List, Optional, Tuple

DEFAULT_BOARD = "geekmagic_ultra"
DEFAULT_DIST = Path(__file__).resolve().parent.parent / "dist"

STOCK, LOADER, MIBLO, UNKNOWN = "stock", "loader", "miblo", "unknown"

# Planned actions.
INSTALL = "install (stage 1 + 2)"
STAGE2 = "install (stage 2)"
OTA = "update (needs code)"
SKIP_CURRENT = "skip: up to date"
SKIP_NEEDS_UPDATE = "skip: older/other version (use --update)"
SKIP_UNKNOWN = "skip: not a GeekMagic/Miblo device"
SKIP_BOARD = "skip: different board"


class FlashError(Exception):
    """A step failed for one unit; the message goes to the summary."""


@dataclass
class Timing:
    probe_timeout: float = 1.0      # TCP connect when scanning a subnet
    http_timeout: float = 4.0       # plain GET/POST requests
    upload_timeout: float = 180.0   # multipart firmware upload
    poll_interval: float = 2.0      # between reboot polls
    stage1_timeout: float = 90.0    # loader must answer GET /info within this
    stage2_timeout: float = 120.0   # Miblo must answer GET /api/info within this
    max_retry_wait: float = 120.0   # cap for a 429 retryAfter


@dataclass
class Images:
    firmware: Path
    version: str
    loader: Optional[Path] = None
    build: Optional[str] = None


@dataclass
class Unit:
    host: str                       # "ip" or "ip:port"
    kind: str = UNKNOWN
    fw: Optional[str] = None
    build: Optional[str] = None
    board: Optional[str] = None
    name: Optional[str] = None
    action: str = SKIP_UNKNOWN
    after: str = "-"
    result: str = ""
    failed: bool = False
    seconds: float = 0.0

    @property
    def before(self) -> str:
        if self.kind == MIBLO:
            return "miblo %s%s" % (self.fw or "?", " (%s)" % self.build if self.build else "")
        if self.kind == LOADER:
            return "installer %s" % (self.fw or "?")
        return self.kind


# ---------------------------------------------------------------------------------------------
# HTTP helpers

def http(method: str, url: str, body: Optional[bytes] = None, headers: Optional[dict] = None,
         timeout: float = 4.0) -> Tuple[int, bytes]:
    """Returns (status, body) for any HTTP status; raises OSError when the host does not answer."""
    req = urllib.request.Request(url, data=body, method=method, headers=headers or {})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.status, resp.read()
    except urllib.error.HTTPError as e:
        try:
            data = e.read()
        except OSError:
            data = b""
        return e.code, data
    except urllib.error.URLError as e:
        raise OSError(str(e.reason)) from e


def get_json(url: str, timeout: float) -> Optional[dict]:
    try:
        status, data = http("GET", url, timeout=timeout)
    except (OSError, ValueError):
        return None
    if status != 200:
        return None
    try:
        doc = json.loads(data.decode("utf-8", "replace"))
    except ValueError:
        return None
    return doc if isinstance(doc, dict) else None


def multipart(path: Path, field_name: str = "firmware") -> Tuple[bytes, str]:
    boundary = "----miblo" + uuid.uuid4().hex
    head = ('--%s\r\nContent-Disposition: form-data; name="%s"; filename="%s"\r\n'
            "Content-Type: application/octet-stream\r\n\r\n" % (boundary, field_name, path.name))
    body = head.encode() + path.read_bytes() + ("\r\n--%s--\r\n" % boundary).encode()
    return body, "multipart/form-data; boundary=" + boundary


def text_of(data: bytes) -> str:
    return re.sub(r"\s+", " ", re.sub(r"<[^>]*>", " ", data.decode("utf-8", "replace"))).strip()


UPDATE_ERROR = re.compile(r"Update error:\s*([^<\r\n]+)", re.I)


def upload(url: str, image: Path, timing: Timing) -> Tuple[int, bytes]:
    """Multipart POST of `image`. A connection dropped after sending counts as accepted (the
    device may reboot before the response gets out); the reboot poll then decides."""
    body, ctype = multipart(image)
    try:
        return http("POST", url, body, {"Content-Type": ctype}, timing.upload_timeout)
    except OSError:
        return 0, b""


def check_update_server_reply(status: int, data: bytes) -> None:
    """Checks an ESP8266HTTPUpdateServer reply (stock firmware and Miblo installer)."""
    m = UPDATE_ERROR.search(data.decode("utf-8", "replace"))
    if m:
        raise FlashError("Update error: " + m.group(1).strip())
    if status >= 400:
        raise FlashError("upload rejected: HTTP %d %s" % (status, text_of(data)[:80]))


# ---------------------------------------------------------------------------------------------
# Discovery and classification

def base_url(host: str) -> str:
    return "http://" + host


def subnet_hosts(cidr: str) -> List[str]:
    net = ipaddress.ip_network(cidr, strict=False)
    if net.num_addresses <= 2:  # /32 and /31 have no network/broadcast address
        return [str(ip) for ip in net]
    return [str(ip) for ip in net.hosts()]


def port_open(ip: str, port: int, timeout: float) -> bool:
    try:
        with socket.create_connection((ip, port), timeout=timeout):
            return True
    except OSError:
        return False


def discover(cidr: str, port: int, timing: Timing) -> List[str]:
    ips = subnet_hosts(cidr)
    with ThreadPoolExecutor(max_workers=min(128, max(1, len(ips)))) as pool:
        found = list(pool.map(lambda ip: port_open(ip, port, timing.probe_timeout), ips))
    suffix = "" if port == 80 else ":%d" % port
    return [ip + suffix for ip, ok in zip(ips, found) if ok]


FORM_UPDATE = re.compile(r"""action\s*=\s*["']?/update""", re.I)


def classify(unit: Unit, timing: Timing) -> None:
    base = base_url(unit.host)
    info = get_json(base + "/api/info", timing.http_timeout)
    if info and "fw" in info:
        unit.kind = MIBLO
        unit.fw = str(info.get("fw"))
        unit.build = info.get("build")
        unit.board = info.get("board")
        unit.name = info.get("name") or info.get("id")
        return
    info = get_json(base + "/info", timing.http_timeout)
    if info and info.get("app") == "miblo-loader":
        unit.kind = LOADER
        unit.fw = info.get("fw")
        unit.board = info.get("board")
        unit.name = info.get("id")
        return
    try:
        status, data = http("GET", base + "/", timeout=timing.http_timeout)
    except (OSError, ValueError):
        unit.kind = UNKNOWN
        return
    page = data.decode("utf-8", "replace")
    if status == 200 and ("geekmagic" in page.lower() or FORM_UPDATE.search(page)):
        unit.kind = STOCK
    else:
        unit.kind = UNKNOWN


def plan(unit: Unit, images: Images, board: str, allow_update: bool) -> None:
    if unit.kind in (MIBLO, LOADER) and unit.board and unit.board != board:
        unit.action = SKIP_BOARD
    elif unit.kind == STOCK:
        unit.action = INSTALL
    elif unit.kind == LOADER:
        unit.action = STAGE2
    elif unit.kind == MIBLO:
        same = unit.fw == images.version
        if same and images.build and unit.build:
            same = unit.build == images.build
        if same:
            unit.action = SKIP_CURRENT
        else:
            unit.action = OTA if allow_update else SKIP_NEEDS_UPDATE
    else:
        unit.action = SKIP_UNKNOWN


# ---------------------------------------------------------------------------------------------
# Flashing

class Runner:
    def __init__(self, images: Images, timing: Timing, out, ask: Callable[[str], str],
                 sleep: Callable[[float], None] = time.sleep):
        self.images = images
        self.timing = timing
        self.out = out
        self.ask = ask
        self.sleep = sleep
        self.lock = threading.Lock()

    def log(self, unit: Unit, msg: str) -> None:
        with self.lock:
            self.out.write("[%s] %s\n" % (unit.host, msg))
            self.out.flush()

    def poll(self, unit: Unit, path: str, ok: Callable[[dict], bool], limit: float,
             what: str) -> dict:
        deadline = time.monotonic() + limit
        last = None
        while True:
            doc = get_json(base_url(unit.host) + path, self.timing.http_timeout)
            if doc is not None:
                last = doc
                if ok(doc):
                    return doc
            if time.monotonic() >= deadline:
                seen = " (last answer: %s)" % json.dumps(last) if last else ""
                raise FlashError("timed out after %gs waiting for %s%s" % (limit, what, seen))
            self.sleep(self.timing.poll_interval)

    def wait_for_miblo(self, unit: Unit) -> None:
        want = self.images.version
        self.log(unit, "waiting for Miblo %s to boot..." % want)
        doc = self.poll(unit, "/api/info", lambda d: d.get("fw") == want,
                        self.timing.stage2_timeout, "Miblo %s on /api/info" % want)
        build = doc.get("build")
        if self.images.build and build and build != self.images.build:
            raise FlashError("running build %s, expected %s" % (build, self.images.build))
        unit.after = "miblo %s%s" % (doc.get("fw"), " (%s)" % build if build else "")

    def stage1(self, unit: Unit) -> None:
        loader = self.images.loader
        if loader is None:
            raise FlashError("no installer image (miblo-loader-*.bin) for this board")
        self.log(unit, "stage 1: uploading installer %s (%d KB)..." % (loader.name, loader.stat().st_size // 1024))
        status, data = upload(base_url(unit.host) + "/update", loader, self.timing)
        check_update_server_reply(status, data)
        self.log(unit, "stage 1: uploaded, waiting for the installer to boot...")
        self.poll(unit, "/info", lambda d: d.get("app") == "miblo-loader",
                  self.timing.stage1_timeout, "the installer on /info")
        self.log(unit, "stage 1: installer running")

    def stage2(self, unit: Unit) -> None:
        image = self.images.firmware
        self.log(unit, "stage 2: uploading %s (%d KB)..." % (image.name, image.stat().st_size // 1024))
        status, data = upload(base_url(unit.host) + "/update", image, self.timing)
        check_update_server_reply(status, data)
        self.wait_for_miblo(unit)

    def locked_wait(self, unit: Unit, data: bytes) -> None:
        try:
            wait = float(json.loads(data.decode()).get("retryAfter", 60))
        except (ValueError, AttributeError):
            wait = 60.0
        wait = min(max(wait, 1.0), self.timing.max_retry_wait)
        self.log(unit, "device locked (too many attempts), retrying in %gs..." % wait)
        self.sleep(wait)

    def open_gate(self, unit: Unit) -> None:
        for _ in range(4):
            status, data = http("POST", base_url(unit.host) + "/update/open", b"{}",
                                {"Content-Type": "application/json"}, self.timing.http_timeout)
            if status == 200:
                return
            if status == 429:
                self.locked_wait(unit, data)
                continue
            raise FlashError("POST /update/open: HTTP %d %s" % (status, text_of(data)[:80]))
        raise FlashError("device stayed locked")

    def read_code(self, unit: Unit) -> str:
        prompt = "Enter the 4-digit code shown on %s (%s): " % (unit.name or unit.host, unit.host)
        for _ in range(5):
            with self.lock:
                code = (self.ask(prompt) or "").strip()
            if re.fullmatch(r"\d{4}", code):
                return code
            self.log(unit, "the code is 4 digits")
        raise FlashError("no valid code entered")

    def ota(self, unit: Unit) -> None:
        image = self.images.firmware
        self.log(unit, "update: asking the device to show a code...")
        self.open_gate(unit)
        for attempt in range(3):
            code = self.read_code(unit)
            self.log(unit, "update: uploading %s (%d KB)..." % (image.name, image.stat().st_size // 1024))
            status, data = upload(base_url(unit.host) + "/update?code=" + code, image, self.timing)
            if status in (200, 0):
                self.wait_for_miblo(unit)
                return
            if status == 403:
                self.log(unit, "wrong code, try again")
                continue
            if status == 429:
                self.locked_wait(unit, data)
                self.open_gate(unit)
                continue
            raise FlashError("update failed: HTTP %d %s" % (status, text_of(data)[:80]))
        raise FlashError("wrong code too many times")

    def run_unit(self, unit: Unit) -> None:
        start = time.monotonic()
        try:
            if unit.action == INSTALL:
                self.stage1(unit)
                self.stage2(unit)
            elif unit.action == STAGE2:
                self.stage2(unit)
            elif unit.action == OTA:
                self.ota(unit)
            unit.result = "ok"
            self.log(unit, "done: %s" % unit.after)
        except FlashError as e:
            unit.failed = True
            unit.result = "FAILED: %s" % e
            self.log(unit, unit.result)
        except Exception as e:  # keep the other units going
            unit.failed = True
            unit.result = "FAILED: %s: %s" % (type(e).__name__, e)
            self.log(unit, unit.result)
        unit.seconds = time.monotonic() - start


# ---------------------------------------------------------------------------------------------
# Images

VERSION_RE = re.compile(r"-(\d+(?:\.\d+)*[^/]*?)\.bin$")


def version_key(v: str):
    return [(1, int(p), "") if p.isdigit() else (0, 0, p) for p in re.split(r"[.\-+]", v)]


def pick(dist: Path, prefix: str) -> List[Tuple[str, Path]]:
    found = []
    for p in dist.glob(prefix + "*.bin"):
        v = p.name[len(prefix):-len(".bin")]
        if re.match(r"\d", v):
            found.append((v, p))
    return sorted(found, key=lambda t: version_key(t[0]))


def resolve_images(args) -> Images:
    loader = Path(args.loader) if args.loader else None
    if args.firmware:
        firmware = Path(args.firmware)
        m = VERSION_RE.search(firmware.name)
        version = args.fw_version or (m.group(1) if m else None)
        if not version:
            raise SystemExit("cannot tell the version from %s; pass --fw-version" % firmware.name)
    else:
        dist = Path(args.dist)
        full = pick(dist, "miblo-%s-" % args.board)
        if not full:
            raise SystemExit("no miblo-%s-<version>.bin in %s (run firmware/scripts/build.sh)" % (args.board, dist))
        version, firmware = full[-1]
        version = args.fw_version or version
    if loader is None:
        dist = Path(args.dist) if args.dist else firmware.parent
        loaders = pick(dist, "miblo-loader-%s-" % args.board)
        same = [p for v, p in loaders if v == version]
        loader = same[0] if same else (loaders[-1][1] if loaders else None)
    for p in (firmware, loader):
        if p is not None and not p.is_file():
            raise SystemExit("image not found: %s" % p)
    return Images(firmware=firmware, version=version, loader=loader, build=args.build)


# ---------------------------------------------------------------------------------------------
# Output

def table(rows: List[List[str]]) -> str:
    widths = [max(len(r[i]) for r in rows) for i in range(len(rows[0]))]
    lines = []
    for n, r in enumerate(rows):
        lines.append("  ".join(c.ljust(w) for c, w in zip(r, widths)).rstrip())
        if n == 0:
            lines.append("  ".join("-" * w for w in widths))
    return "\n".join(lines)


def parse_args(argv):
    ap = argparse.ArgumentParser(
        prog="flash-fleet.py",
        description=__doc__.split("\n\n", 1)[1].split("\nExamples:")[0],
        epilog="Examples:\n  %(prog)s --subnet 192.168.0.0/24 --dry-run\n"
               "  %(prog)s --subnet 192.168.0.0/24\n"
               "  %(prog)s --host 192.168.0.41 --host 192.168.0.42 --update",
        formatter_class=argparse.RawDescriptionHelpFormatter)
    t = ap.add_argument_group("targets")
    t.add_argument("--subnet", action="append", default=[], metavar="CIDR",
                   help="scan this network for devices answering on the HTTP port (repeatable)")
    t.add_argument("--host", action="append", default=[], metavar="IP[:PORT]",
                   help="a unit's address (repeatable)")
    t.add_argument("--port", type=int, default=80, help="HTTP port probed in --subnet scans (default 80)")
    i = ap.add_argument_group("images")
    i.add_argument("--board", default=DEFAULT_BOARD, help="board name in the image files (default %(default)s)")
    i.add_argument("--dist", default=None, metavar="DIR",
                   help="where the images live (default firmware/dist next to this script); the newest "
                        "miblo-<board>-<ver>.bin and matching miblo-loader-<board>-<ver>.bin are used")
    i.add_argument("--loader", metavar="BIN", help="installer image to use instead of the one in --dist")
    i.add_argument("--firmware", metavar="BIN", help="full Miblo image to use instead of the one in --dist")
    i.add_argument("--fw-version", metavar="VER", help="expected version (default: from the image file name)")
    i.add_argument("--build", metavar="SHA",
                   help="expected build hash; when given, units on the same version but another build "
                        "are not considered up to date")
    r = ap.add_argument_group("run")
    r.add_argument("--jobs", type=int, default=4, help="units flashed in parallel (default %(default)s)")
    r.add_argument("--dry-run", action="store_true", help="discover and classify only, print the plan")
    r.add_argument("--update", action="store_true",
                   help="also update units already running another Miblo version (asks for the "
                        "4-digit code shown on each screen; done one unit at a time)")
    r.add_argument("--poll-interval", type=float, default=None, help=argparse.SUPPRESS)
    r.add_argument("--stage1-timeout", type=float, default=None,
                   help="seconds to wait for the installer after stage 1 (default %g)" % Timing.stage1_timeout)
    r.add_argument("--stage2-timeout", type=float, default=None,
                   help="seconds to wait for Miblo after stage 2 / update (default %g)" % Timing.stage2_timeout)
    args = ap.parse_args(argv)
    if not args.subnet and not args.host:
        ap.error("give at least one --subnet or --host")
    if args.jobs < 1:
        ap.error("--jobs must be at least 1")
    return args


def main(argv=None, timing: Optional[Timing] = None, out=None,
         ask: Callable[[str], str] = input, sleep: Callable[[float], None] = time.sleep) -> int:
    args = parse_args(argv)
    out = out or sys.stdout
    timing = timing or Timing()
    for name in ("poll_interval", "stage1_timeout", "stage2_timeout"):
        if getattr(args, name) is not None:
            setattr(timing, name, getattr(args, name))
    if args.dist is None and not args.firmware:
        args.dist = str(DEFAULT_DIST)
    images = resolve_images(args)
    out.write("Image: %s (fw %s%s)\nInstaller: %s\n" % (
        images.firmware, images.version, ", build %s" % images.build if images.build else "",
        images.loader or "(none)"))

    hosts: List[str] = []
    for cidr in args.subnet:
        out.write("Scanning %s on port %d...\n" % (cidr, args.port))
        out.flush()
        found = discover(cidr, args.port, timing)
        out.write("  %d host(s) answer\n" % len(found))
        hosts += found
    hosts += args.host
    seen = set()
    units = [Unit(h) for h in hosts if not (h in seen or seen.add(h))]

    with ThreadPoolExecutor(max_workers=max(1, min(32, len(units)))) as pool:
        list(pool.map(lambda u: classify(u, timing), units))
    for u in units:
        plan(u, images, args.board, args.update)

    if not units:
        out.write("No devices found.\n")
        return 0
    out.write("\nPlan:\n" + table([["host", "found", "action"]] +
                                  [[u.host, u.before, u.action] for u in units]) + "\n\n")
    if args.dry_run:
        return 0

    runner = Runner(images, timing, out, ask, sleep)
    parallel = [u for u in units if u.action in (INSTALL, STAGE2)]
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        list(pool.map(runner.run_unit, parallel))
    for u in units:  # a code prompt each: strictly one at a time
        if u.action == OTA:
            runner.run_unit(u)
    for u in units:
        if not u.result:
            u.result = u.action

    rows = [["host", "before", "after", "result", "seconds"]]
    rows += [[u.host, u.before, u.after, u.result, "%.0f" % u.seconds] for u in units]
    out.write("\nSummary:\n" + table(rows) + "\n")
    failed = sum(u.failed for u in units)
    done = sum(u.result == "ok" for u in units)
    out.write("\n%d flashed, %d failed, %d skipped\n" % (done, failed, len(units) - done - failed))
    out.flush()
    return 1 if failed else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
