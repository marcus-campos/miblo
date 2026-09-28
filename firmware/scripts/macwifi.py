"""macOS Wi-Fi control for flash-fleet.py --via-ap (stdlib only, Python 3.9+).

Every OS command goes through an injectable `run(argv, timeout) -> (returncode, output)` so the
tests can fake the radio without touching the Mac's Wi-Fi.

Commands used:
  networksetup -listallhardwareports             Wi-Fi interface ("Hardware Port: Wi-Fi" -> Device)
  system_profiler SPAirPortDataType -json        scan: current network + other local networks
  ipconfig getsummary <iface>                    current SSID (fallback)
  networksetup -getairportnetwork <iface>        current SSID (last fallback)
  networksetup -setairportnetwork <iface> <ssid> [password]   join
  ipconfig getifaddr <iface>                     the interface's IPv4 address
"""
from __future__ import annotations

import json
import re
import subprocess
from typing import Callable, List, Optional, Tuple

RunFn = Callable[[List[str], float], Tuple[int, str]]

REDACTED = "<redacted>"

LOCATION_HELP = """\
macOS hides Wi-Fi network names ("<redacted>") from apps without Location Services access.
Grant it to the terminal app you run this from, then run the script again:
  System Settings -> Privacy & Security -> Location Services
  -> turn Location Services on and enable it for Terminal / iTerm / your IDE
     (if the app is not listed, run the script once, then look again; restart the app after
     enabling it)."""


class WifiError(Exception):
    """A Wi-Fi command failed."""


class WifiPermissionError(WifiError):
    """SSIDs are redacted: the terminal app lacks Location Services permission."""

    def __init__(self):
        super().__init__(LOCATION_HELP)


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


class MacWifi:
    def __init__(self, run: RunFn = run_command, iface: Optional[str] = None):
        self.run = run
        self._iface = iface
        self.joined_other = False   # True once we joined something: the caller must restore

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

    def scan(self) -> List[str]:
        """Visible SSIDs. Raises WifiPermissionError when macOS redacts them."""
        cur, visible = self._profile()
        if any(s == REDACTED for s in visible):
            raise WifiPermissionError()
        return visible

    def current_ssid(self) -> Optional[str]:
        cur, visible = self._profile()
        if cur == REDACTED or any(s == REDACTED for s in visible):
            raise WifiPermissionError()
        if cur:
            return cur
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
