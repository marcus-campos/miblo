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

Access-point mode (--via-ap, macOS): for factory units that are not on any network. Each unit
broadcasts its own Wi-Fi; the Mac joins them one at a time (one radio, strictly sequential) and
talks to the device at 192.168.4.1:

  stock AP (--stock-ssid)  -> stage 1, then join the new "Miblo-Installer-XXXX" network, stage 2,
                              then join "Miblo-Setup-XXXX" and check the version
  Miblo-Installer-XXXX     -> stage 2, then join "Miblo-Setup-XXXX" and check the version
  Miblo-Setup-XXXX         -> skipped when up to date; otherwise updated (no --update needed):
                              no code when the unit says so ("codeRequired": false), else the
                              on-screen code is asked (skipped when stdin is not a terminal)
The Wi-Fi network the Mac was on is rejoined at the end (also on errors and Ctrl-C).

macOS only shows network names to apps with Location Services permission, which command-line
tools can't get: the scan runs in a small helper app (MibloWiFiScan, built with Swift on first
use into firmware/.cache; macOS asks once to allow its location access). Without scanning:
  --try-ssid NAME   stock units have fixed names: join by name, go on if 192.168.4.1 answers
                    (GIFTV, SmallTV, GeekMagic are tried when the scan is unavailable or finds none)
  --ssid NAME       a unit given by name, e.g. a shelf unit on Miblo-Setup-4F2A (repeatable)
After stage 1 the unit's chip id (from its MAC, or GET /info "miblo-4f2a") gives the next
networks' names (Miblo-Installer-4F2A, Miblo-Setup-4F2A), which are joined by name directly.

Examples:
  flash-fleet.py --subnet 192.168.0.0/24 --dry-run
  flash-fleet.py --subnet 192.168.0.0/24
  flash-fleet.py --host 192.168.0.41 --host 192.168.0.42 --update
  flash-fleet.py --via-ap --dry-run
  flash-fleet.py --via-ap --loop
  flash-fleet.py --via-ap --ssid Miblo-Setup-4F2A --ssid GIFTV
  flash-fleet.py --via-ap --scan-only
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

sys.path.insert(0, str(Path(__file__).resolve().parent))
from macwifi import (MacWifi, ScanHelper, WifiError, WifiPermissionError,  # noqa: E402  (next to this script)
                     mac_chip)

DEFAULT_BOARD = "geekmagic_ultra"
DEFAULT_DIST = Path(__file__).resolve().parent.parent / "dist"
VERSION_HEADER = Path(__file__).resolve().parent.parent / "include" / "miblo_version.h"

STOCK, LOADER, MIBLO, UNKNOWN = "stock", "loader", "miblo", "unknown"

# Planned actions.
INSTALL = "install (stage 1 + 2)"
STAGE2 = "install (stage 2)"
OTA = "update (needs code)"
SKIP_CURRENT = "skip: up to date"
SKIP_NEEDS_UPDATE = "skip: older/other version (use --update)"
SKIP_UNKNOWN = "skip: not a GeekMagic/Miblo device"
SKIP_BOARD = "skip: different board"


SKIP_NO_TTY = "skip: needs the 4-digit code on its screen (run from a terminal)"
UNVERIFIED = "installed (unverified: unit is in setup mode)"


class FlashError(Exception):
    """A step failed for one unit; the message goes to the summary."""


class SkipUnit(Exception):
    """The unit is left alone on purpose; the message goes to the summary."""


@dataclass
class Timing:
    probe_timeout: float = 1.0      # TCP connect when scanning a subnet
    http_timeout: float = 4.0       # plain GET/POST requests
    upload_timeout: float = 180.0   # multipart firmware upload
    poll_interval: float = 2.0      # between reboot polls
    stage1_timeout: float = 90.0    # loader must answer GET /info within this
    stage2_timeout: float = 120.0   # Miblo must answer GET /api/info within this
    max_retry_wait: float = 120.0   # cap for a 429 retryAfter
    ap_join_timeout: float = 30.0   # --via-ap: joined network must give an IP and answer on :80
    scan_interval: float = 3.0      # --via-ap: between Wi-Fi scans


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
    ssid: Optional[str] = None      # --via-ap: the network the unit was found on
    chip: Optional[str] = None      # --via-ap: "4f2a" (from the SSID or the device id)
    verify: bool = False            # LAN: installed, the unit restarted on its Miblo-Setup network

    @property
    def label(self) -> str:
        return self.ssid or self.host

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


def installer_update_url(host: str, keep_wifi: bool) -> str:
    """The installer's /update erases the unit's saved Wi-Fi after a good upload (units ship
    clean, restarting on Miblo-Setup-XXXX) unless ?keepwifi=1."""
    return base_url(host) + "/update" + ("?keepwifi=1" if keep_wifi else "")


def check_update_server_reply(status: int, data: bytes) -> None:
    """Checks an /update reply: the stock ESP8266HTTPUpdateServer ("Update Success! ...") or the
    Miblo installer ("OK"); both report failures as "Update error: ..." or an HTTP error."""
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
                 sleep: Callable[[float], None] = time.sleep, interactive: bool = True,
                 keep_wifi: bool = False):
        self.images = images
        self.timing = timing
        self.out = out
        self.ask = ask
        self.sleep = sleep
        self.interactive = interactive  # False: units that need the on-screen code are skipped
        self.keep_wifi = keep_wifi      # stage 2 with ?keepwifi=1: the unit stays on the LAN
        self.lock = threading.Lock()

    def log(self, unit: Unit, msg: str) -> None:
        with self.lock:
            self.out.write("[%s] %s\n" % (unit.label, msg))
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

    def wait_for_miblo(self, unit: Unit, limit: Optional[float] = None) -> None:
        want = self.images.version
        self.log(unit, "waiting for Miblo %s to boot..." % want)
        doc = self.poll(unit, "/api/info", lambda d: d.get("fw") == want,
                        self.timing.stage2_timeout if limit is None else limit,
                        "Miblo %s on /api/info" % want)
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
        doc = self.poll(unit, "/info", lambda d: d.get("app") == "miblo-loader",
                        self.timing.stage1_timeout, "the installer on /info")
        unit.name = doc.get("id") or unit.name
        self.log(unit, "stage 1: installer running")

    def stage2(self, unit: Unit) -> None:
        image = self.images.firmware
        self.log(unit, "stage 2: uploading %s (%d KB)..." % (image.name, image.stat().st_size // 1024))
        status, data = upload(installer_update_url(unit.host, self.keep_wifi), image, self.timing)
        check_update_server_reply(status, data)
        if self.keep_wifi:
            self.wait_for_miblo(unit)
            return
        # The installer erased the unit's Wi-Fi: it restarts on its own Miblo-Setup-XXXX network.
        m = CHIP_SUFFIX.search(unit.name or "")
        unit.chip = m.group(1).lower() if m else None
        unit.verify = True
        unit.after = "miblo %s?" % self.images.version
        self.log(unit, "stage 2: uploaded; the unit restarts in setup mode (%s)"
                 % ("Miblo-Setup-" + unit.chip.upper() if unit.chip else "Miblo-Setup-XXXX"))

    def locked_wait(self, unit: Unit, data: bytes) -> None:
        try:
            wait = float(json.loads(data.decode()).get("retryAfter", 60))
        except (ValueError, AttributeError):
            wait = 60.0
        wait = min(max(wait, 1.0), self.timing.max_retry_wait)
        self.log(unit, "device locked (too many attempts), retrying in %gs..." % wait)
        self.sleep(wait)

    def open_gate(self, unit: Unit) -> dict:
        """POST /update/open; returns the JSON answer ({} when it is not JSON)."""
        for _ in range(4):
            status, data = http("POST", base_url(unit.host) + "/update/open", b"{}",
                                {"Content-Type": "application/json"}, self.timing.http_timeout)
            if status == 200:
                try:
                    doc = json.loads(data.decode("utf-8", "replace"))
                except ValueError:
                    doc = {}
                return doc if isinstance(doc, dict) else {}
            if status == 429:
                self.locked_wait(unit, data)
                continue
            raise FlashError("POST /update/open: HTTP %d %s" % (status, text_of(data)[:80]))
        raise FlashError("device stayed locked")

    def read_code(self, unit: Unit) -> str:
        if unit.ssid:
            prompt = "Enter the 4-digit code shown on %s: " % unit.ssid
        else:
            prompt = "Enter the 4-digit code shown on %s (%s): " % (unit.name or unit.host, unit.host)
        for _ in range(5):
            with self.lock:
                code = (self.ask(prompt) or "").strip()
            if re.fullmatch(r"\d{4}", code):
                return code
            self.log(unit, "the code is 4 digits")
        raise FlashError("no valid code entered")

    def ota(self, unit: Unit, after_upload: Optional[Callable[[Unit], None]] = None) -> None:
        image = self.images.firmware
        after = after_upload or self.wait_for_miblo
        self.log(unit, "update: opening the update gate...")
        gate = self.open_gate(unit)
        if gate.get("codeRequired") is False:
            # Unconfigured unit asked on its own setup network: no on-screen code needed.
            self.log(unit, "update: no code needed, uploading %s (%d KB)..." % (image.name, image.stat().st_size // 1024))
            status, data = upload(base_url(unit.host) + "/update", image, self.timing)
            if status not in (200, 0):
                raise FlashError("update failed: HTTP %d %s" % (status, text_of(data)[:80]))
            after(unit)
            return
        if not self.interactive:
            raise SkipUnit(SKIP_NO_TTY)
        self.log(unit, "update: the device shows a 4-digit code")
        for attempt in range(3):
            code = self.read_code(unit)
            self.log(unit, "update: uploading %s (%d KB)..." % (image.name, image.stat().st_size // 1024))
            status, data = upload(base_url(unit.host) + "/update?code=" + code, image, self.timing)
            if status in (200, 0):
                after(unit)
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
            if unit.verify:
                unit.result = UNVERIFIED
            else:
                unit.result = "ok"
                self.log(unit, "done: %s" % unit.after)
        except SkipUnit as e:
            unit.result = str(e)
            self.log(unit, unit.result)
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
# Access-point mode (--via-ap)

AP_SUBNET = ipaddress.ip_network("192.168.4.0/24")
DEFAULT_STOCK_SSID = r"^(GIFTV|SmallTV|GeekMagic)"
DEFAULT_TRY_SSIDS = ["GIFTV", "SmallTV", "GeekMagic"]   # the stock units' fixed network names
INSTALLER_SSID = re.compile(r"^Miblo-Installer-([0-9A-Fa-f]{4})$")
SETUP_SSID = re.compile(r"^Miblo-Setup-([0-9A-Fa-f]{4})$")
CHECK = "check version, update if different"
CHIP_SUFFIX = re.compile(r"([0-9A-Fa-f]{4})$")


def ap_device_host(ssid: str) -> str:
    """Every unit's own network puts it at 192.168.4.1 (tests map SSIDs to fake devices)."""
    return "192.168.4.1"


def ssid_kind(ssid: str, stock_re) -> Optional[str]:
    if INSTALLER_SSID.match(ssid):
        return LOADER
    if SETUP_SSID.match(ssid):
        return MIBLO
    if stock_re.search(ssid):
        return STOCK
    return None


def ssid_chip(ssid: str) -> Optional[str]:
    m = INSTALLER_SSID.match(ssid) or SETUP_SSID.match(ssid)
    return m.group(1).lower() if m else None


def ssid_key(ssid: str) -> str:
    chip = ssid_chip(ssid)
    return "chip:" + chip if chip else "ssid:" + ssid


def split_host(host: str) -> Tuple[str, int]:
    ip, _, port = host.partition(":")
    return ip, int(port or 80)


class ApFleet:
    """Joins each unit's own network in turn (one radio: strictly sequential) and installs Miblo."""

    def __init__(self, runner: Runner, wifi: MacWifi, args, ap_host: Callable[[str], str]):
        self.runner = runner
        self.wifi = wifi
        self.args = args
        self.timing = runner.timing
        self.ap_host = ap_host
        self.stock_re = re.compile(args.stock_ssid, re.I)
        self.handled = set()          # ssid_key()s processed, or produced by a processed unit
        self.units: List[Unit] = []
        self.current: Optional[Unit] = None
        self.scan_blocked = False     # names are hidden: skip scans inside a unit's steps

    def log(self, unit: Unit, msg: str) -> None:
        self.runner.log(unit, msg)

    def pending(self, ssids: List[str]) -> List[str]:
        return [s for s in ssids if ssid_kind(s, self.stock_re) and ssid_key(s) not in self.handled]

    def scan(self) -> Optional[List[str]]:
        """Visible SSIDs, or None when scanning is unavailable (hidden names, no helper...)."""
        if self.scan_blocked:
            return None
        try:
            return self.wifi.scan()
        except WifiPermissionError:
            self.scan_blocked = True  # won't change during a unit: join by name from now on
            return None
        except WifiError:
            return None

    def connect(self, unit: Unit, ssid: str, password: Optional[str] = None, attempts: int = 3) -> None:
        """Joins `ssid` and waits for a 192.168.4.x address and the device answering on HTTP."""
        self.log(unit, "joining Wi-Fi %s..." % ssid)
        last = None
        for _ in range(attempts):
            try:
                self.wifi.join(ssid, password)
                break
            except WifiError as e:
                last = e
                self.runner.sleep(self.timing.scan_interval)
        else:
            raise FlashError(str(last))
        unit.host = self.ap_host(ssid)
        ip, port = split_host(unit.host)
        deadline = time.monotonic() + self.timing.ap_join_timeout
        while True:
            mine = self.wifi.ip_address()
            if mine and ipaddress.ip_address(mine) in AP_SUBNET and port_open(ip, port, self.timing.probe_timeout):
                return
            if time.monotonic() >= deadline:
                raise FlashError("joined %s but got no 192.168.4.x address or no answer from %s within %gs "
                                 "(Mac address: %s)" % (ssid, unit.host, self.timing.ap_join_timeout, mine))
            self.runner.sleep(self.timing.poll_interval)

    def reach(self, unit: Unit, pick: Callable[[List[str]], Optional[str]], name: Optional[str],
              limit: float, what: str) -> str:
        """Joins the network `pick` finds in a scan. With a known `name` it is joined by name
        directly when scanning is unavailable, or when the scan hasn't shown it after half of
        `limit` (scans can miss a network)."""
        start = time.monotonic()
        deadline = start + limit
        last = None
        while True:
            ssids = self.scan()
            ssid = pick(ssids) if ssids is not None else None
            if ssid is None and name and (ssids is None or time.monotonic() >= start + limit / 2):
                ssid = name
            if ssid:
                try:
                    self.connect(unit, ssid, attempts=1)
                    return ssid
                except FlashError as e:
                    last = e
            if time.monotonic() >= deadline:
                raise FlashError("timed out after %gs waiting for %s%s" % (limit, what, " (%s)" % last if last else ""))
            self.runner.sleep(self.timing.scan_interval)

    def chip_hint(self, unit: Unit) -> Optional[str]:
        """The chip id of the unit we're joined to: its MAC's low bytes (ARP), else a 4-hex
        suffix of its network name, else the scanned BSSID (when only one unit has that name)."""
        chip = mac_chip(self.wifi.device_mac(split_host(unit.host)[0]))
        if chip:
            return chip
        m = CHIP_SUFFIX.search(unit.ssid or "")
        if m:
            return m.group(1).lower()
        bssids = self.wifi.bssids.get(unit.ssid or "", [])
        return mac_chip(bssids[0]) if len(bssids) == 1 else None

    def note_chip(self, unit: Unit) -> None:
        m = CHIP_SUFFIX.search(unit.name or "")  # device id "miblo-4f2a"
        if m:
            unit.chip = m.group(1).lower()
            self.handled.add("chip:" + unit.chip)

    def classify(self, unit: Unit, found_as: Optional[str]) -> None:
        classify(unit, self.timing)
        if unit.kind == UNKNOWN and found_as == STOCK:
            # The stock setup portal may not mention GeekMagic on its home page: an upload form counts.
            try:
                status, data = http("GET", base_url(unit.host) + "/update", timeout=self.timing.http_timeout)
            except (OSError, ValueError):
                status, data = 0, b""
            if status == 200 and re.search(r"""type\s*=\s*["']?file""", data.decode("utf-8", "replace"), re.I):
                unit.kind = STOCK
        self.note_chip(unit)

    def stage1(self, unit: Unit) -> None:
        loader = self.runner.images.loader
        if loader is None:
            raise FlashError("no installer image (miblo-loader-*.bin) for this board")
        hint = self.chip_hint(unit)
        before = {s for s in (self.scan() or []) if INSTALLER_SSID.match(s)}
        self.log(unit, "stage 1: uploading installer %s (%d KB)..." % (loader.name, loader.stat().st_size // 1024))
        status, data = upload(base_url(unit.host) + "/update", loader, self.timing)
        check_update_server_reply(status, data)
        name = "Miblo-Installer-%s" % hint.upper() if hint else None
        self.log(unit, "stage 1: uploaded, waiting for %s..." % (name or "its Miblo-Installer-XXXX network"))

        def pick(ssids):
            fresh = [s for s in ssids if INSTALLER_SSID.match(s) and ssid_key(s) not in self.handled]
            same = [s for s in fresh if hint and ssid_chip(s) == hint]
            new = [s for s in fresh if s not in before]  # can't correlate: one not seen before
            return (same or new or [None])[0]

        ssid = self.reach(unit, pick, name, self.timing.stage1_timeout, name or "a new Miblo-Installer-XXXX network")
        self.handled.add(ssid_key(ssid))
        doc = self.runner.poll(unit, "/info", lambda d: d.get("app") == "miblo-loader",
                               self.timing.ap_join_timeout, "the installer on /info")
        unit.name = doc.get("id") or unit.name
        self.note_chip(unit)
        self.log(unit, "stage 1: installer running (%s)" % ssid)

    def stage2(self, unit: Unit) -> None:
        image = self.runner.images.firmware
        before = set() if unit.chip else {s for s in (self.scan() or []) if SETUP_SSID.match(s)}
        self.log(unit, "stage 2: uploading %s (%d KB)..." % (image.name, image.stat().st_size // 1024))
        status, data = upload(installer_update_url(unit.host, self.runner.keep_wifi), image, self.timing)
        check_update_server_reply(status, data)
        self.after_reboot(unit, before)

    def after_reboot(self, unit: Unit, before=frozenset()) -> None:
        """The unit rebooted into Miblo: join its Miblo-Setup-XXXX (by name when the chip id is
        known) and check /api/info."""
        name = "Miblo-Setup-%s" % unit.chip.upper() if unit.chip else None
        want = name.lower() if name else None

        def pick(ssids):
            if want:
                return next((s for s in ssids if s.lower() == want), None)
            new = [s for s in ssids if SETUP_SSID.match(s) and s not in before
                   and ssid_key(s) not in self.handled]
            return new[0] if new else None

        what = name or "a new Miblo-Setup-XXXX network"
        self.log(unit, "waiting for %s..." % what)
        deadline = time.monotonic() + self.timing.stage2_timeout
        ssid = self.reach(unit, pick, name, self.timing.stage2_timeout, what)
        self.handled.add(ssid_key(ssid))
        self.runner.wait_for_miblo(unit, max(deadline - time.monotonic(), self.timing.ap_join_timeout))

    def process(self, ssid: str, probe: bool = False) -> Optional[Unit]:
        """Joins `ssid` and installs/updates the unit there. `probe`: the network was not seen
        in a scan (joined by name only); when it can't be joined, it is not a unit: None."""
        unit = Unit(host=self.ap_host(ssid), ssid=ssid, chip=ssid_chip(ssid))
        self.handled.add(ssid_key(ssid))
        self.units.append(unit)
        self.current = unit
        start = time.monotonic()
        found_as = ssid_kind(ssid, self.stock_re)
        password = self.args.stock_pass if found_as in (STOCK, None) else None
        if probe:
            try:
                self.connect(unit, ssid, password, attempts=1)
            except FlashError:
                self.units.remove(unit)
                self.handled.discard(ssid_key(ssid))
                self.current = None
                self.log(unit, "not in range")
                return None
        try:
            if not probe:
                self.connect(unit, ssid, password)
            self.classify(unit, found_as)
            # Units on their setup network are unconfigured shelf stock: update them by default.
            plan(unit, self.runner.images, self.args.board, allow_update=True)
            self.log(unit, "found %s: %s" % (unit.before, unit.action))
            if unit.action == INSTALL:
                self.stage1(unit)
                self.stage2(unit)
            elif unit.action == STAGE2:
                self.stage2(unit)
            elif unit.action == OTA:
                self.runner.ota(unit, after_upload=self.after_reboot)
            if unit.action in (INSTALL, STAGE2, OTA):
                unit.result = "ok"
                self.log(unit, "done: %s" % unit.after)
                if unit.action == INSTALL and not ssid_chip(ssid):
                    self.handled.discard(ssid_key(ssid))  # the next stock unit has the same name
            else:
                unit.result = unit.action
        except SkipUnit as e:
            unit.result = str(e)
            self.log(unit, unit.result)
        except (FlashError, WifiError) as e:
            if isinstance(e, WifiPermissionError):
                raise
            unit.failed = True
            unit.result = "FAILED: %s" % e
            self.log(unit, unit.result)
        finally:
            unit.seconds = time.monotonic() - start
        self.current = None
        return unit


def restore_wifi(wifi: MacWifi, original: Optional[str], out) -> None:
    if not wifi.joined_other:
        return
    if not original:
        out.write("\nWARNING: the name of the Wi-Fi network the Mac was on is unknown (macOS hides it, or "
                  "there was none); rejoin your network by hand.\n")
        out.flush()
        return
    out.write("\nRejoining Wi-Fi %s...\n" % original)
    try:
        wifi.join(original)
    except Exception as e:  # a failed restore must not hide the run's outcome
        out.write("WARNING: could not rejoin %s (%s); rejoin it by hand.\n" % (original, e))
    out.flush()


def scan_only(args, out, wifi: MacWifi) -> int:
    """--via-ap --scan-only: print what the scan sees (a debug aid; joins nothing)."""
    try:
        res = wifi.scan_result()
    except WifiPermissionError as e:
        out.write("\n%s\n" % e)
        return 2
    except WifiError as e:
        out.write("Wi-Fi: %s\n" % e)
        return 2
    stock_re = re.compile(args.stock_ssid, re.I)
    names = {STOCK: "stock", LOADER: "installer", MIBLO: "miblo"}
    out.write("Scanner: %s%s, current network: %s\n" % (
        res.source, " (%s)" % res.status if res.status else "", res.current or "(unknown)"))
    rows = [["ssid", "bssid", "rssi", "channel", "unit"]]
    for n in res.networks:
        ssid = n["ssid"] or "(hidden)"
        kind = ssid_kind(n["ssid"], stock_re) if n["ssid"] else None
        rows.append([ssid, n["bssid"] or "-", "-" if n["rssi"] is None else str(n["rssi"]),
                     "-" if n["channel"] is None else str(n["channel"]), names.get(kind, "")])
    out.write(table(rows) + "\n\n%d network(s)\n" % len(res.networks))
    return 0


def run_via_ap(args, images: Images, timing: Timing, out, ask, sleep, interactive: bool,
               wifi: MacWifi, ap_host: Callable[[str], str]) -> int:
    runner = Runner(images, timing, out, ask, sleep, interactive, args.keep_wifi)
    fleet = ApFleet(runner, wifi, args, ap_host)
    by_name = bool(args.ssid or args.try_ssid)   # the user gave names: a scan is optional
    try_names = args.try_ssid or DEFAULT_TRY_SSIDS

    def scan() -> Optional[List[str]]:
        """SSIDs, or None when scanning is unavailable. Hidden names without --ssid/--try-ssid
        raise WifiPermissionError (the run stops with the instructions)."""
        try:
            # A single CoreWLAN scan often misses the ESP8266 soft APs (short beacon window),
            # so rescan a couple of times before concluding that no unit is around.
            ssids = wifi.scan()
            for _ in range(2):
                if fleet.pending(ssids):
                    break
                sleep(2.0)
                for s in wifi.scan():
                    if s not in ssids:
                        ssids.append(s)
            fleet.scan_blocked = False
            return ssids
        except WifiPermissionError:
            if not by_name:
                raise
            fleet.scan_blocked = True
            return None
        except WifiError as e:
            out.write("Wi-Fi scan unavailable: %s\n" % e)
            return None

    ssids: Optional[List[str]] = []
    try:
        iface = wifi.iface
        if not args.ssid:  # explicit units need no scan
            out.write("Scanning for units (stock networks: /%s/i)...\n" % args.stock_ssid)
            out.flush()
            ssids = scan()
            if ssids is None:
                out.write("Scanning is unavailable (network names hidden by macOS); joining networks by name.\n")
        # Remember the network to come back to BEFORE joining anything.
        try:
            original = wifi.current_ssid()
        except WifiError:
            original = None
        out.write("Wi-Fi interface %s, current network: %s\n" % (iface, original or "(unknown)"))
        if not original:
            out.write("WARNING: can't read the current network's name; after the run, rejoin it by hand.\n")
        out.flush()
    except WifiPermissionError as e:
        out.write("\n%s\n" % e)
        return 2
    except WifiError as e:
        out.write("Wi-Fi: %s\n" % e)
        return 2

    def targets(first: bool) -> List[Tuple[str, bool]]:
        """(ssid, probe) to process this round; probe: joined by name, not seen in a scan."""
        if args.ssid:
            return [(s, False) for s in args.ssid if ssid_key(s) not in fleet.handled] if first else []
        found = fleet.pending(ssids or [])
        result = [(s, False) for s in found]
        if ssids is None or (first and (not found or args.try_ssid)):
            result += [(n, True) for n in try_names if n not in found and ssid_key(n) not in fleet.handled]
        return result

    def full() -> bool:
        return bool(args.max) and len(fleet.units) >= args.max

    if args.dry_run:
        kinds = {STOCK: ("stock", INSTALL), LOADER: ("installer", STAGE2), MIBLO: ("miblo", CHECK)}
        rows = [["ssid", "found", "action"]]
        for s, probe in targets(True):
            found, action = kinds.get(ssid_kind(s, fleet.stock_re), ("?", "classify, then install/update"))
            if probe:
                found, action = "not scanned", "try by name, then " + action
            elif args.ssid:
                found = "given"
            rows.append([s, found, action])
        if len(rows) == 1:
            out.write("No unit networks visible (%d other network(s)).\n" % len(ssids or []))
            return 0
        out.write("\nPlan (nothing joined):\n" + table(rows) + "\n")
        return 0

    status = 0
    try:
        waiting = False
        first = True
        while True:
            for ssid, probe in targets(first):
                while not full() and ssid_key(ssid) not in fleet.handled:
                    waiting = False
                    out.write("\n== %s%s ==\n" % (ssid, " (by name)" if probe else ""))
                    unit = fleet.process(ssid, probe)
                    # A stock name joined blindly: the next unit may use the same name.
                    if not probe or unit is None or unit.result != "ok":
                        break
            first = False
            if full() or not args.loop or args.ssid:
                break
            if not waiting:
                out.write("\nWaiting for more units (power the next one on; Ctrl-C to stop)...\n")
                out.flush()
                waiting = True
            sleep(timing.scan_interval)
            ssids = scan()
    except KeyboardInterrupt:
        out.write("\nStopped.\n")
        if fleet.current is not None:
            fleet.current.failed = True
            fleet.current.result = "FAILED: interrupted"
    except WifiPermissionError as e:
        out.write("\n%s\n" % e)
        status = 2
    finally:
        restore_wifi(wifi, original, out)

    units = fleet.units
    if not units:
        out.write("No units found.\n")
        return status
    rows = [["ssid", "before", "after", "result", "seconds"]]
    rows += [[u.label, u.before, u.after, u.result, "%.0f" % u.seconds] for u in units]
    out.write("\nSummary:\n" + table(rows) + "\n")
    failed = sum(u.failed for u in units)
    done = sum(u.result in ("ok", UNVERIFIED) for u in units)
    out.write("\n%d flashed, %d failed, %d skipped\n" % (done, failed, len(units) - done - failed))
    out.flush()
    return status or (1 if failed else 0)


def verify_via_ap(units: List[Unit], runner: Runner, wifi: MacWifi, args, ap_host: Callable[[str], str],
                  out) -> None:
    """LAN installs without --keep-wifi: each unit restarted on its own Miblo-Setup-XXXX network.
    Join each one by name (one radio: in turn), check GET /api/info at 192.168.4.1, then rejoin
    the Mac's network."""
    fleet = ApFleet(runner, wifi, args, ap_host)
    fleet.scan_blocked = True  # the names are known: join directly, no scan (no permission needed)
    try:
        original = wifi.current_ssid()
    except WifiError:
        original = None
    out.write("\nChecking %d unit(s) on their setup networks (the Mac leaves %s meanwhile)...\n"
              % (len(units), original or "its network"))
    if not original:
        out.write("WARNING: can't read the current network's name; after the run, rejoin it by hand.\n")
    out.flush()
    try:
        for u in units:
            if not u.chip:
                runner.log(u, "can't tell its Miblo-Setup-XXXX name (no chip id); not verified")
                continue
            lan_host, start = u.host, time.monotonic()
            try:
                fleet.after_reboot(u)
                u.verify = False
                u.result = "ok"
                runner.log(u, "done: %s" % u.after)
            except (FlashError, WifiError) as e:
                u.failed = True
                u.result = "FAILED: after install: %s" % e
                runner.log(u, u.result)
            finally:
                u.host = lan_host
                u.seconds += time.monotonic() - start
    finally:
        restore_wifi(wifi, original, out)


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


def source_version(header: Path = VERSION_HEADER) -> Optional[str]:
    """MIBLO_FW_VERSION from include/miblo_version.h (the version the sources build)."""
    try:
        m = re.search(r'^#define MIBLO_FW_VERSION "([^"]+)"', header.read_text(), re.M)
    except OSError:
        return None
    return m.group(1) if m else None


def resolve_images(args, out=None) -> Images:
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
        want = args.fw_version or source_version()
        match = [(v, p) for v, p in full if v == want]
        if match:
            version, firmware = match[-1]
        elif want and (args.fw_version or Path(args.dist).resolve() == DEFAULT_DIST.resolve()):
            # The sources are at `want`: never flash an older image left in dist/ by mistake.
            raise SystemExit("no miblo-%s-%s.bin in %s (run make build, or firmware/scripts/build.sh)"
                             % (args.board, want, dist))
        elif full:
            version, firmware = full[-1]
            if want and out is not None:
                out.write("WARNING: no image for the sources' version %s in %s; using the newest, %s\n"
                          % (want, dist, version))
        else:
            raise SystemExit("no miblo-%s-<version>.bin in %s (run firmware/scripts/build.sh)" % (args.board, dist))
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
               "  %(prog)s --host 192.168.0.41 --host 192.168.0.42 --update\n"
               "  %(prog)s --via-ap --dry-run\n"
               "  %(prog)s --via-ap --loop",
        formatter_class=argparse.RawDescriptionHelpFormatter)
    t = ap.add_argument_group("targets")
    t.add_argument("--subnet", action="append", default=[], metavar="CIDR",
                   help="scan this network for devices answering on the HTTP port (repeatable)")
    t.add_argument("--host", action="append", default=[], metavar="IP[:PORT]",
                   help="a unit's address (repeatable)")
    t.add_argument("--port", type=int, default=80, help="HTTP port probed in --subnet scans (default 80)")
    w = ap.add_argument_group("access-point mode (macOS)")
    w.add_argument("--via-ap", action="store_true",
                   help="join each unit's own Wi-Fi network in turn instead of using the LAN; units on "
                        "Miblo-Setup-XXXX with another version are updated without --update")
    w.add_argument("--stock-ssid", default=DEFAULT_STOCK_SSID, metavar="REGEX",
                   help="stock GeekMagic network names, case-insensitive (default %(default)s)")
    w.add_argument("--stock-pass", default=None, metavar="PASS",
                   help="password of the stock networks (default: open network)")
    w.add_argument("--ssid", action="append", default=[], metavar="NAME",
                   help="process only this unit network, joined by name without scanning, e.g. a shelf "
                        "unit's Miblo-Setup-4F2A (repeatable)")
    w.add_argument("--try-ssid", action="append", default=[], metavar="NAME",
                   help="network name to try joining even when a scan doesn't show it; the unit is used "
                        "if 192.168.4.1 answers (repeatable; when the scan is unavailable or finds no "
                        "unit, %s are tried)" % ", ".join(DEFAULT_TRY_SSIDS))
    w.add_argument("--scan-only", action="store_true",
                   help="print the Wi-Fi scan (names, BSSIDs, signal; builds the MibloWiFiScan helper "
                        "if needed) and exit")
    w.add_argument("--iface", default=None, help="Wi-Fi interface (default: from networksetup)")
    w.add_argument("--max", type=int, default=0, metavar="N", help="stop after N units")
    w.add_argument("--loop", action="store_true",
                   help="keep scanning for newly powered units until Ctrl-C")
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
    r.add_argument("--dry-run", action="store_true",
                   help="discover and classify only, print the plan (--via-ap: scan only, join nothing)")
    r.add_argument("--update", action="store_true",
                   help="also update units already running another Miblo version (asks for the "
                        "4-digit code shown on each screen; done one unit at a time)")
    r.add_argument("--keep-wifi", action="store_true",
                   help="stage 2 keeps the unit's Wi-Fi (?keepwifi=1) and checks it on the LAN; by default "
                        "the installer erases it, the unit restarts on Miblo-Setup-XXXX and (macOS) the Mac "
                        "joins that network to check it, then rejoins its own")
    r.add_argument("--no-ap-verify", action="store_true",
                   help="don't join the units' setup networks to check them after a LAN install (they are "
                        "reported as installed, unverified)")
    r.add_argument("--poll-interval", type=float, default=None, help=argparse.SUPPRESS)
    r.add_argument("--stage1-timeout", type=float, default=None,
                   help="seconds to wait for the installer after stage 1 (default %g)" % Timing.stage1_timeout)
    r.add_argument("--stage2-timeout", type=float, default=None,
                   help="seconds to wait for Miblo after stage 2 / update (default %g)" % Timing.stage2_timeout)
    args = ap.parse_args(argv)
    if args.via_ap:
        if args.subnet or args.host:
            ap.error("--via-ap does not take --subnet/--host")
        try:
            re.compile(args.stock_ssid)
        except re.error as e:
            ap.error("--stock-ssid: %s" % e)
        if args.max < 0:
            ap.error("--max must be positive")
    elif args.ssid or args.try_ssid or args.scan_only:
        ap.error("--ssid/--try-ssid/--scan-only need --via-ap")
    elif not args.subnet and not args.host:
        ap.error("give at least one --subnet or --host (or use --via-ap)")
    if args.jobs < 1:
        ap.error("--jobs must be at least 1")
    return args


def main(argv=None, timing: Optional[Timing] = None, out=None,
         ask: Callable[[str], str] = input, sleep: Callable[[float], None] = time.sleep,
         interactive: Optional[bool] = None, wifi: Optional[MacWifi] = None,
         ap_host: Callable[[str], str] = ap_device_host) -> int:
    args = parse_args(argv)
    if interactive is None:
        interactive = sys.stdin is not None and sys.stdin.isatty()
    out = out or sys.stdout
    timing = timing or Timing()
    for name in ("poll_interval", "stage1_timeout", "stage2_timeout"):
        if getattr(args, name) is not None:
            setattr(timing, name, getattr(args, name))
    if args.via_ap and wifi is None:
        if sys.platform != "darwin":
            raise SystemExit("--via-ap needs macOS (networksetup, CoreWLAN)")
        wifi = MacWifi(iface=args.iface, helper=ScanHelper(log=lambda m: (out.write(m + "\n"), out.flush())),
                       log=lambda m: (out.write(m + "\n"), out.flush()))
    if args.via_ap and args.scan_only:
        return scan_only(args, out, wifi)
    if args.dist is None and not args.firmware:
        args.dist = str(DEFAULT_DIST)
    images = resolve_images(args, out)
    out.write("Installing Miblo %s\n" % images.version)
    out.write("Image: %s (fw %s%s)\nInstaller: %s\n" % (
        images.firmware, images.version, ", build %s" % images.build if images.build else "",
        images.loader or "(none)"))

    if args.via_ap:
        return run_via_ap(args, images, timing, out, ask, sleep, interactive, wifi, ap_host)

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

    runner = Runner(images, timing, out, ask, sleep, interactive, args.keep_wifi)
    parallel = [u for u in units if u.action in (INSTALL, STAGE2)]
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        list(pool.map(runner.run_unit, parallel))
    pending = [u for u in parallel if u.verify and not u.failed]
    if pending:
        if args.no_ap_verify:
            out.write("\n%d unit(s) left unverified (--no-ap-verify).\n" % len(pending))
        elif wifi is None and sys.platform != "darwin":
            out.write("\n%d unit(s) can't be verified here: they restarted on their Miblo-Setup-XXXX "
                      "networks (use --keep-wifi to keep them on the LAN).\n" % len(pending))
        else:
            if wifi is None:
                wifi = MacWifi(iface=args.iface, helper=ScanHelper(log=lambda m: (out.write(m + "\n"), out.flush())),
                               log=lambda m: (out.write(m + "\n"), out.flush()))
            verify_via_ap(pending, runner, wifi, args, ap_host, out)
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
    done = sum(u.result in ("ok", UNVERIFIED) for u in units)
    out.write("\n%d flashed, %d failed, %d skipped\n" % (done, failed, len(units) - done - failed))
    out.flush()
    return 1 if failed else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
