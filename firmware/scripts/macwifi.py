"""macOS Wi-Fi control for flash-fleet.py --via-ap (stdlib only, Python 3.9+).

Every OS command goes through an injectable `run(argv, timeout) -> (returncode, output)` so the
tests can fake the radio without touching the Mac's Wi-Fi.

Scanning: macOS (14.4+, 26) shows network names only to apps with Location Services access, and
command-line tools cannot be granted it. So the scan runs in a tiny app bundle, MibloWiFiScan
(scripts/macos/MibloWiFiScan), built on first use into firmware/.cache/MibloWiFiScan.app with
swiftc and started with `open -W -n` so macOS attributes the location request to the app (it asks
once: "MibloWiFiScan would like to use your location"). Without Swift the scan falls back to
system_profiler, which may only show "<redacted>".

Commands used:
  networksetup -listallhardwareports             Wi-Fi interface ("Hardware Port: Wi-Fi" -> Device)
  xcode-select -p / xcrun --find swiftc          is Swift available (to build the helper)
  swiftc ... / codesign --force --sign - --deep  build the helper (once per source change)
  open -W -n MibloWiFiScan.app --args <out.json> scan with real names (JSON)
  system_profiler SPAirPortDataType -json        scan fallback: current network + other networks
  ipconfig getsummary <iface>                    current SSID (fallback)
  networksetup -getairportnetwork <iface>        current SSID (last fallback)
  networksetup -setairportnetwork <iface> <ssid> [password]   join (works by name, no scan needed)
  ipconfig getifaddr <iface>                     the interface's IPv4 address
  arp -n <ip>                                    the joined unit's MAC (its last 4 hex = chip id)
"""
from __future__ import annotations

import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Dict, List, Optional, Tuple

RunFn = Callable[[List[str], float], Tuple[int, str]]

REDACTED = "<redacted>"

HELPER_NAME = "MibloWiFiScan"
HELPER_SRC = Path(__file__).resolve().parent / "macos" / HELPER_NAME
CACHE_DIR = Path(__file__).resolve().parent.parent / ".cache"

NO_SCAN_HINT = """Or skip scanning: --ssid Miblo-Setup-XXXX (a unit by name, repeatable) or --try-ssid GIFTV
(stock units by their fixed name; GIFTV, SmallTV and GeekMagic are tried by default)."""

LOCATION_HELP = """macOS hides Wi-Fi network names ("<redacted>") from apps without Location Services access, and
command-line tools cannot be granted it on recent macOS. Install Swift (xcode-select --install)
so the scan can run in the MibloWiFiScan helper app, or grant it to your terminal app if your
macOS still lists it:
  System Settings -> Privacy & Security -> Location Services
""" + NO_SCAN_HINT

HELPER_LOCATION_HELP = """macOS hides Wi-Fi network names until the MibloWiFiScan helper may use Location Services.
  macOS will ask "MibloWiFiScan would like to use your location" -- click Allow, then run this again.
  If you dismissed it: System Settings -> Privacy & Security -> Location Services -> MibloWiFiScan
  (turn it on).
""" + NO_SCAN_HINT


class WifiError(Exception):
    """A Wi-Fi command failed."""


class WifiPermissionError(WifiError):
    """SSIDs are redacted: no Location Services permission (terminal or helper app)."""

    def __init__(self, message: str = LOCATION_HELP):
        super().__init__(message)


class HelperUnavailable(WifiError):
    """The MibloWiFiScan helper cannot be built or run (no Swift, build error)."""


def run_command(argv: List[str], timeout: float = 60.0) -> Tuple[int, str]:
    try:
        p = subprocess.run(argv, capture_output=True, text=True, timeout=timeout)
    except (OSError, subprocess.TimeoutExpired) as e:
        return 127, str(e)
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def parse_hardware_ports(text: str) -> Optional[str]:
    port = None
    for line in text.splitlines():
        m = re.match(r"\s*Hardware Port:\s*(.+?)\s*$", line)
        if m:
            port = m.group(1)
            continue
        m = re.match(r"\s*Device:\s*(\S+)", line)
        if m and port in ("Wi-Fi", "AirPort"):
            return m.group(1)
    return None


def parse_airport_json(text: str, iface: str) -> Tuple[Optional[str], List[str]]:
    """Returns (current SSID, visible SSIDs incl. the current one) for `iface`."""
    try:
        doc = json.loads(text)
    except ValueError as e:
        raise WifiError("cannot parse system_profiler output: %s" % e) from e
    interfaces = []
    for entry in doc.get("SPAirPortDataType", []) or []:
        interfaces += entry.get("spairport_airport_interfaces", []) or []
    chosen = [i for i in interfaces if i.get("_name") == iface] or \
             [i for i in interfaces if "spairport_airport_other_local_wireless_networks" in i]
    if not chosen:
        return None, []
    info = chosen[0]
    cur = (info.get("spairport_current_network_information") or {}).get("_name")
    names = [n.get("_name") for n in info.get("spairport_airport_other_local_wireless_networks", []) or []]
    visible = []
    for s in ([cur] if cur else []) + names:
        if s and s not in visible:
            visible.append(s)
    return cur, visible


def parse_summary_ssid(text: str) -> Optional[str]:
    for line in text.splitlines():
        m = re.match(r"^\s*SSID\s*:\s*(.+?)\s*$", line)
        if m:
            return m.group(1)
    return None


MAC_RE = re.compile(r"\bat\s+([0-9A-Fa-f]{1,2}(?::[0-9A-Fa-f]{1,2}){5})\b")


def parse_arp_mac(text: str) -> Optional[str]:
    """`arp -n 192.168.4.1` -> "5e:cf:7f:12:4f:2a" (None for "(incomplete)" / no entry)."""
    m = MAC_RE.search(text)
    if not m:
        return None
    return ":".join("%02x" % int(p, 16) for p in m.group(1).split(":"))


def mac_chip(mac: Optional[str]) -> Optional[str]:
    """The ESP8266 chip id is the MAC's low bytes: "5e:cf:7f:12:4f:2a" -> "4f2a"."""
    if not mac:
        return None
    hexes = re.sub(r"[^0-9A-Fa-f]", "", mac)
    return hexes[-4:].lower() if len(hexes) == 12 else None


def _clean_ssid(s) -> Optional[str]:
    if not isinstance(s, str) or not s or s == REDACTED:
        return None
    return s


def _int_or_none(v) -> Optional[int]:
    try:
        return int(v)
    except (TypeError, ValueError):
        return None


@dataclass
class ScanResult:
    authorized: bool
    current: Optional[str] = None
    current_bssid: Optional[str] = None
    networks: List[dict] = field(default_factory=list)  # {"ssid","bssid","rssi","channel"}
    status: str = ""
    error: Optional[str] = None
    source: str = HELPER_NAME


def parse_helper_json(text: str) -> ScanResult:
    """The JSON MibloWiFiScan writes (see main.swift)."""
    try:
        doc = json.loads(text)
    except ValueError as e:
        raise WifiError("cannot parse the %s output: %s" % (HELPER_NAME, e)) from e
    if not isinstance(doc, dict):
        raise WifiError("unexpected %s output: %r" % (HELPER_NAME, text[:100]))
    nets = []
    for n in doc.get("networks") or []:
        if not isinstance(n, dict):
            continue
        bssid = n.get("bssid")
        nets.append({"ssid": _clean_ssid(n.get("ssid")),
                     "bssid": bssid.lower() if isinstance(bssid, str) and bssid else None,
                     "rssi": _int_or_none(n.get("rssi")),
                     "channel": _int_or_none(n.get("channel"))})
    bssid = doc.get("current_bssid")
    return ScanResult(authorized=bool(doc.get("authorized")),
                      current=_clean_ssid(doc.get("current_ssid")),
                      current_bssid=bssid.lower() if isinstance(bssid, str) and bssid else None,
                      networks=nets, status=str(doc.get("status") or ""),
                      error=doc.get("error") or None)


def _stderr(msg: str) -> None:
    sys.stderr.write(msg + "\n")
    sys.stderr.flush()


class ScanHelper:
    """Builds (on first use, and again when its sources change) and runs MibloWiFiScan.app."""

    def __init__(self, run: RunFn = run_command, cache_dir: Path = CACHE_DIR, src_dir: Path = HELPER_SRC,
                 timeout: float = 150.0, log: Callable[[str], None] = _stderr):
        self.run = run
        self.cache_dir = Path(cache_dir)
        self.src_dir = Path(src_dir)
        self.timeout = timeout   # the app waits up to 60 s for the location prompt, then scans
        self.log = log

    @property
    def app(self) -> Path:
        return self.cache_dir / (HELPER_NAME + ".app")

    @property
    def executable(self) -> Path:
        return self.app / "Contents" / "MacOS" / HELPER_NAME

    @property
    def stamp(self) -> Path:
        return self.cache_dir / (HELPER_NAME + ".sha256")

    def source_hash(self) -> str:
        h = hashlib.sha256()
        for name in ("main.swift", "Info.plist"):
            h.update(name.encode() + b"\0" + (self.src_dir / name).read_bytes() + b"\0")
        return h.hexdigest()

    def is_current(self) -> bool:
        try:
            return self.executable.is_file() and self.stamp.read_text().strip() == self.source_hash()
        except OSError:
            return False

    def build(self) -> Path:
        """Returns the app, building it when missing or out of date. Raises HelperUnavailable."""
        if self.is_current():
            return self.app
        rc, out = self.run(["xcode-select", "-p"], 30.0)
        if rc != 0:
            raise HelperUnavailable("Swift is not installed (xcode-select --install)")
        rc, out = self.run(["xcrun", "--find", "swiftc"], 60.0)
        lines = out.strip().splitlines()
        swiftc = lines[-1].strip() if rc == 0 and lines else ""
        if not swiftc:
            raise HelperUnavailable("swiftc not found (xcode-select --install)")
        self.log("Building the Wi-Fi scan helper %s (once)..." % self.app)
        if self.app.exists():
            shutil.rmtree(self.app)
        self.executable.parent.mkdir(parents=True, exist_ok=True)
        # Through xcrun so the macOS SDK is found (a bare CommandLineTools swiftc can't load its stdlib).
        rc, out = self.run(["xcrun", "swiftc", "-O", str(self.src_dir / "main.swift"), "-framework", "CoreWLAN",
                            "-framework", "CoreLocation", "-o", str(self.executable)], 900.0)
        if rc != 0 or not self.executable.is_file():
            raise HelperUnavailable("building %s failed: %s" % (HELPER_NAME, out.strip()[-400:] or "exit %d" % rc))
        shutil.copyfile(self.src_dir / "Info.plist", self.app / "Contents" / "Info.plist")
        rc, out = self.run(["codesign", "--force", "--sign", "-", "--deep", str(self.app)], 120.0)
        if rc != 0:
            raise HelperUnavailable("codesign %s failed: %s" % (self.app, out.strip()[-300:]))
        self.stamp.write_text(self.source_hash() + "\n")
        return self.app

    def scan(self) -> ScanResult:
        app = self.build()
        tmp = Path(tempfile.mkdtemp(prefix="miblo-wifiscan-"))
        try:
            outfile = tmp / "scan.json"
            rc, out = self.run(["open", "-W", "-n", str(app), "--args", str(outfile)], self.timeout)
            try:
                text = outfile.read_text()
            except OSError:
                raise HelperUnavailable("%s wrote no result (open: %s)" % (HELPER_NAME, out.strip()[:200] or "exit %d" % rc))
            return parse_helper_json(text)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)


class MacWifi:
    def __init__(self, run: RunFn = run_command, iface: Optional[str] = None,
                 helper: Optional[ScanHelper] = None, log: Callable[[str], None] = _stderr):
        self.run = run
        self._iface = iface
        self.helper = helper        # None: system_profiler only
        self.log = log
        self.joined_other = False   # True once we joined something: the caller must restore
        self.bssids: Dict[str, List[str]] = {}   # ssid -> BSSIDs seen in the last scan
        self.last_scan: Optional[ScanResult] = None
        self.scan_error: Optional[WifiError] = None   # why the last scan failed

    @property
    def iface(self) -> str:
        if self._iface is None:
            rc, out = self.run(["networksetup", "-listallhardwareports"], 30.0)
            dev = parse_hardware_ports(out) if rc == 0 else None
            if not dev:
                raise WifiError("no Wi-Fi interface found (networksetup -listallhardwareports); pass --iface")
            self._iface = dev
        return self._iface

    def _profile(self) -> Tuple[Optional[str], List[str]]:
        rc, out = self.run(["system_profiler", "SPAirPortDataType", "-json"], 90.0)
        if rc != 0:
            raise WifiError("system_profiler SPAirPortDataType failed: %s" % out.strip()[:200])
        return parse_airport_json(out, self.iface)

    def _helper_scan(self) -> Optional[ScanResult]:
        if self.helper is None:
            return None
        try:
            res = self.helper.scan()
        except HelperUnavailable as e:
            self.log("Wi-Fi scan helper unavailable (%s); using system_profiler." % e)
            self.helper = None
            return None
        if not res.authorized:
            raise WifiPermissionError(HELPER_LOCATION_HELP)
        if res.error:
            raise WifiError(res.error)
        return res

    def scan_result(self) -> ScanResult:
        """A fresh scan (helper app, else system_profiler). Raises WifiPermissionError when the
        names are hidden."""
        try:
            res = self._scan_result()
        except WifiError as e:
            self.scan_error = e
            raise
        self.scan_error = None
        return res

    def _scan_result(self) -> ScanResult:
        res = self._helper_scan()
        if res is None:
            cur, visible = self._profile()
            if cur == REDACTED or any(s == REDACTED for s in visible):
                raise WifiPermissionError()
            res = ScanResult(authorized=True, current=cur, source="system_profiler",
                             networks=[{"ssid": s, "bssid": None, "rssi": None, "channel": None}
                                       for s in visible])
        self.last_scan = res
        self.bssids = {}
        for n in res.networks:
            if n["ssid"] and n["bssid"]:
                self.bssids.setdefault(n["ssid"], []).append(n["bssid"])
        return res

    def scan(self) -> List[str]:
        """Visible SSIDs, the current one first. Raises WifiPermissionError when macOS redacts them."""
        res = self.scan_result()
        visible = []
        for s in [res.current] + [n["ssid"] for n in res.networks]:
            if s and s not in visible:
                visible.append(s)
        return visible

    def current_ssid(self) -> Optional[str]:
        """The network the Mac is on, or None when it can't be told (hidden name, no network).
        Uses the last scan when there is one."""
        res = self.last_scan
        if res is None and self.scan_error is None:
            try:
                res = self.scan_result()
            except WifiError:
                res = None
        if res is not None and res.current:
            return res.current
        rc, out = self.run(["ipconfig", "getsummary", self.iface], 30.0)
        s = parse_summary_ssid(out) if rc == 0 else None
        if s and s != REDACTED:
            return s
        rc, out = self.run(["networksetup", "-getairportnetwork", self.iface], 30.0)
        m = re.search(r"Current (?:Wi-Fi|AirPort) Network:\s*(.+?)\s*$", out, re.M) if rc == 0 else None
        return m.group(1) if m and m.group(1) != REDACTED else None

    def join(self, ssid: str, password: Optional[str] = None) -> None:
        argv = ["networksetup", "-setairportnetwork", self.iface, ssid]
        if password:
            argv.append(password)
        self.joined_other = True
        rc, out = self.run(argv, 60.0)
        # networksetup often exits 0 even when it fails; the message tells.
        if rc != 0 or re.search(r"could not find|failed|error", out, re.I):
            raise WifiError("could not join %s: %s" % (ssid, out.strip()[:200] or "exit %d" % rc))

    def ip_address(self) -> Optional[str]:
        rc, out = self.run(["ipconfig", "getifaddr", self.iface], 10.0)
        ip = out.strip()
        return ip if rc == 0 and re.fullmatch(r"\d+\.\d+\.\d+\.\d+", ip) else None

    def device_mac(self, ip: str) -> Optional[str]:
        """MAC of a device on the joined network (from the ARP cache; no permission needed)."""
        rc, out = self.run(["arp", "-n", ip], 10.0)
        return parse_arp_mac(out) if rc == 0 else None
