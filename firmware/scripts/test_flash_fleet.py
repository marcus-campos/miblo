"""Tests for flash-fleet.py against local fake devices (http.server on 127.0.0.1 only).

Run: python3 -m unittest firmware/scripts/test_flash_fleet.py -v
"""
import importlib.util
import io
import sys
import json
import os
import re
import shutil
import socket
import subprocess
import tempfile
import threading
import time
import unittest
from unittest import mock
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

_spec = importlib.util.spec_from_file_location("flash_fleet", Path(__file__).with_name("flash-fleet.py"))
ff = importlib.util.module_from_spec(_spec)
sys.modules["flash_fleet"] = ff  # dataclasses look the module up
_spec.loader.exec_module(ff)
import macwifi  # noqa: E402  (flash-fleet.py put its directory on sys.path)

NEW = "0.2.0"
FAST = dict(probe_timeout=0.5, http_timeout=1.0, upload_timeout=5.0, poll_interval=0.02,
            stage1_timeout=1.5, stage2_timeout=1.5, max_retry_wait=0.05,
            ap_join_timeout=1.0, scan_interval=0.02)


def collapse(joins):
    """Join attempts with retries folded: joining by name while a unit reboots fails and is retried."""
    names = []
    for j in joins:
        if not names or names[-1] != j[0]:
            names.append(j[0])
    return names


class FakeDevice:
    """One unit. state: stock | loader | miblo | rebooting | other.

    stock  : GET / GeekMagic page; POST /update accepts files up to `space` bytes, then reboots
             into the loader (unless `stuck`), otherwise answers "Update error: ERROR[4]: ...".
    loader : GET /info; POST /update reboots into Miblo `next_fw`.
    miblo  : GET /api/info; POST /update/open shows `code`; POST /update?code= reboots into
             Miblo `next_fw`. `lock_once` makes the first /update/open answer 429.
    """

    def __init__(self, state, fw=None, build="abc1234", space=1000, next_fw=NEW, stuck=False,
                 code="4821", lock_once=False, board="geekmagic_ultra", chip="4F2A",
                 stock_ssid="GIFTV", codeless=False):
        self.state, self.fw, self.build, self.space = state, fw, build, space
        self.next_fw, self.stuck, self.code, self.lock_once = next_fw, stuck, code, lock_once
        self.board = board
        self.chip, self.stock_ssid, self.codeless = chip, stock_ssid, codeless
        self.uploads = []      # (path, size) of accepted uploads
        self.keepwifi = []     # loader uploads: was ?keepwifi=1 passed
        self.opened = 0
        dev = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *a):
                pass

            def send(self, status, body, ctype="text/plain"):
                data = body.encode() if isinstance(body, str) else body
                self.send_response(status)
                self.send_header("Content-Type", ctype)
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)

            def do_GET(self):
                path = urlparse(self.path).path
                if dev.state == "rebooting":
                    self.close_connection = True
                    return
                if dev.state == "miblo" and path == "/api/info":
                    return self.send(200, json.dumps({"id": "miblo-" + dev.chip.lower(), "name": "Miblo-" + dev.chip, "fw": dev.fw,
                                                      "build": dev.build, "board": dev.board}),
                                     "application/json")
                if dev.state == "loader" and path == "/info":
                    return self.send(200, json.dumps({"app": "miblo-loader", "fw": NEW, "board": dev.board,
                                                      "id": "miblo-" + dev.chip.lower()}), "application/json")
                if dev.state == "stock" and path == "/":
                    return self.send(200, "<html><title>GeekMagic</title><a href=/update>Update</a></html>",
                                     "text/html")
                if dev.state == "other" and path == "/":
                    return self.send(200, "<html>Router admin</html>", "text/html")
                self.send(404, "Not found")

            def do_POST(self):
                url = urlparse(self.path)
                body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
                if dev.state == "rebooting":
                    self.close_connection = True
                    return
                if url.path == "/update/open" and dev.state == "miblo":
                    if self.headers.get("Content-Type") != "application/json":
                        return self.send(415, "{}", "application/json")
                    dev.opened += 1
                    if dev.lock_once:
                        dev.lock_once = False
                        return self.send(429, '{"error":"locked","retryAfter":1}', "application/json")
                    if dev.codeless:
                        return self.send(200, '{"ok":true,"codeRequired":false}', "application/json")
                    return self.send(200, '{"ok":true}', "application/json")
                if url.path != "/update" or b'name="firmware"' not in body:
                    return self.send(400, "bad request")
                size = len(body)
                if dev.state == "stock":
                    if size > dev.space:
                        return self.send(200, "Update error: ERROR[4]: Not Enough Space", "text/html")
                    dev.uploads.append(("stock", size))
                    self.send(200, "Update Success! Rebooting...", "text/html")
                    if not dev.stuck:
                        dev.reboot("loader")
                    return
                if dev.state == "loader":
                    dev.uploads.append(("loader", size))
                    dev.keepwifi.append(parse_qs(url.query).get("keepwifi") == ["1"])
                    self.send(200, "OK")  # the installer's reply (the stock one says "Update Success!")
                    dev.reboot("miblo", dev.next_fw)
                    return
                if dev.state == "miblo":
                    if not dev.codeless and parse_qs(url.query).get("code", [""])[0] != dev.code:
                        return self.send(403, '{"error":"bad code"}', "application/json")
                    dev.uploads.append(("miblo", size))
                    self.send(200, "OK")
                    dev.reboot("miblo", dev.next_fw)
                    return
                self.send(404, "Not found")

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.server.daemon_threads = True
        self.host = "127.0.0.1:%d" % self.server.server_address[1]
        threading.Thread(target=self.server.serve_forever, kwargs={"poll_interval": 0.02}, daemon=True).start()

    def reboot(self, state, fw=None):
        def later():
            time.sleep(0.05)
            if fw is not None:
                self.fw = fw
            self.state = state
        self.state = "rebooting"
        threading.Thread(target=later, daemon=True).start()

    def close(self):
        self.server.shutdown()
        self.server.server_close()


class FleetTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        dist = Path(self.tmp.name)
        (dist / "miblo-loader-geekmagic_ultra-0.1.0.bin").write_bytes(b"L" * 50)
        (dist / ("miblo-loader-geekmagic_ultra-%s.bin" % NEW)).write_bytes(b"L" * 300)
        (dist / "miblo-geekmagic_ultra-0.1.0.bin").write_bytes(b"F" * 100)
        (dist / ("miblo-geekmagic_ultra-%s.bin" % NEW)).write_bytes(b"F" * 5000)
        self.dist = str(dist)
        self.devices = []
        # Never touch the Mac's real Wi-Fi: every MacWifi in a test is built on a fake runner.
        guard = mock.patch.object(ff, "MacWifi", side_effect=AssertionError("real Wi-Fi used in a test"))
        guard.start()
        self.addCleanup(guard.stop)
        self.lan_air = FakeAir(self.devices)

    def tearDown(self):
        for d in self.devices:
            d.close()
        self.tmp.cleanup()

    def device(self, *a, **kw):
        d = FakeDevice(*a, **kw)
        self.devices.append(d)
        return d

    def run_fleet(self, *argv, codes=(), wifi=True):
        out = io.StringIO()
        answers = list(codes)
        prompts = []

        def ask(prompt):
            prompts.append(prompt)
            return answers.pop(0)

        args = ["--dist", self.dist]
        for d in self.devices:
            args += ["--host", d.host]
        rc = ff.main(args + list(argv), timing=ff.Timing(**FAST), out=out, ask=ask, sleep=time.sleep,
                     interactive=True, wifi=macwifi.MacWifi(run=self.lan_air.run) if wifi else None,
                     ap_host=self.lan_air.host)
        return rc, out.getvalue(), prompts

    def test_picks_newest_images(self):
        args = ff.parse_args(["--host", "x", "--dist", self.dist])
        images = ff.resolve_images(args)
        self.assertEqual(images.version, NEW)
        self.assertTrue(images.firmware.name.endswith("-%s.bin" % NEW))
        self.assertTrue(images.loader.name.startswith("miblo-loader-") and NEW in images.loader.name)

    def test_classification(self):
        timing = ff.Timing(**FAST)
        cases = [(self.device("stock"), ff.STOCK), (self.device("loader"), ff.LOADER),
                 (self.device("miblo", fw="0.1.0"), ff.MIBLO), (self.device("other"), ff.UNKNOWN)]
        for dev, kind in cases:
            u = ff.Unit(dev.host)
            ff.classify(u, timing)
            self.assertEqual(u.kind, kind, dev.host)
        u = ff.Unit("127.0.0.1:1")  # nothing listens there
        ff.classify(u, timing)
        self.assertEqual(u.kind, ff.UNKNOWN)

    def test_dry_run_changes_nothing(self):
        stock = self.device("stock")
        miblo = self.device("miblo", fw="0.1.0")
        current = self.device("miblo", fw=NEW)
        rc, out, _ = self.run_fleet("--dry-run")
        self.assertEqual(rc, 0)
        self.assertIn(ff.INSTALL, out)
        self.assertIn(ff.SKIP_NEEDS_UPDATE, out)
        self.assertIn(ff.SKIP_CURRENT, out)
        self.assertNotIn("Summary", out)
        self.assertEqual(stock.uploads + miblo.uploads + current.uploads, [])
        self.assertEqual(stock.state, "stock")

    def test_subnet_scan(self):
        dev = self.device("stock")
        port = int(dev.host.split(":")[1])
        out = io.StringIO()
        rc = ff.main(["--dist", self.dist, "--subnet", "127.0.0.1/32", "--port", str(port), "--dry-run"],
                     timing=ff.Timing(**FAST), out=out)
        self.assertEqual(rc, 0)
        self.assertIn(dev.host, out.getvalue())
        self.assertIn(ff.INSTALL, out.getvalue())

    def test_discover_merges_and_dedupes_with_host(self):
        known = self.device("miblo", fw="0.1.0")   # given explicitly via --host
        found_only = "127.0.0.2:1"                 # only found via mDNS (different IP, nothing listens)

        def fake_discover(**kw):
            return [
                {"id": "miblo-aaaa", "name": "Known", "addr": known.host, "fw": "0.1.0"},  # dup of --host
                {"id": "miblo-bbbb", "name": "Found", "addr": found_only, "fw": "0.1.0"},
            ]

        out = io.StringIO()
        rc = ff.main(["--dist", self.dist, "--host", known.host, "--discover", "--dry-run"],
                     timing=ff.Timing(**FAST), out=out, mdns_discover=fake_discover)
        self.assertEqual(rc, 0)
        text = out.getvalue()
        self.assertIn("Discovering Miblo units via mDNS", text)
        self.assertIn("Found", text)  # the discovered-only device's name, in the "found" table
        plan = text.split("Plan:")[1]
        self.assertIn(known.host, plan)
        self.assertIn(found_only, plan)
        self.assertEqual(plan.count(known.host), 1)  # not duplicated: same IP as --host

    def test_discover_alone_reports_nothing_found(self):
        out = io.StringIO()
        rc = ff.main(["--dist", self.dist, "--discover", "--dry-run"],
                     timing=ff.Timing(**FAST), out=out, mdns_discover=lambda **kw: [])
        self.assertEqual(rc, 0)
        self.assertIn("no units found", out.getvalue())
        self.assertIn("No devices found", out.getvalue())

    def test_two_stage_install(self):
        # Default: the installer erases the units' Wi-Fi; they restart on Miblo-Setup-XXXX, where
        # the Mac checks them one by one, then rejoins its network.
        stock = self.device("stock")
        loader = self.device("loader", chip="1A2B")
        rc, out, _ = self.run_fleet("--jobs", "2")
        self.assertEqual(rc, 0, out)
        self.assertEqual([k for k, _ in stock.uploads], ["stock", "loader"])
        self.assertEqual([k for k, _ in loader.uploads], ["loader"])
        self.assertEqual(stock.keepwifi + loader.keepwifi, [False, False])
        for d in (stock, loader):
            self.assertEqual((d.state, d.fw), ("miblo", NEW))
        self.assertIn("miblo %s" % NEW, out)
        self.assertEqual(sorted(collapse(self.lan_air.joins)[:2]), ["Miblo-Setup-1A2B", "Miblo-Setup-4F2A"])
        self.assertEqual(self.lan_air.joins[-1], ["HomeNet"])
        self.assertIn("2 flashed, 0 failed", out)

    def test_keep_wifi_verifies_on_lan(self):
        dev = self.device("loader")
        rc, out, _ = self.run_fleet("--keep-wifi")
        self.assertEqual(rc, 0, out)
        self.assertEqual(dev.keepwifi, [True])
        self.assertEqual(self.lan_air.joins, [])
        self.assertIn("1 flashed, 0 failed", out)

    def test_no_ap_verify_reports_unverified(self):
        dev = self.device("loader")
        rc, out, _ = self.run_fleet("--no-ap-verify")
        self.assertEqual(rc, 0, out)
        self.assertEqual(dev.keepwifi, [False])
        self.assertIn(ff.UNVERIFIED, out)
        self.assertEqual(self.lan_air.joins, [])
        self.assertIn("1 flashed, 0 failed", out)

    def test_not_macos_reports_unverified(self):
        self.device("loader")
        with mock.patch.object(ff.sys, "platform", "linux"):
            rc, out, _ = self.run_fleet(wifi=False)
        self.assertEqual(rc, 0, out)
        self.assertIn(ff.UNVERIFIED, out)
        self.assertIn("--keep-wifi", out)

    def test_ap_verify_failure(self):
        self.device("loader")
        self.lan_air.fail_join = {"Miblo-Setup-4F2A"}
        rc, out, _ = self.run_fleet("--stage2-timeout", "0.3")
        self.assertEqual(rc, 1, out)
        self.assertIn("FAILED: after install", out)
        self.assertEqual(self.lan_air.joins[-1], ["HomeNet"])

    def test_not_enough_space(self):
        dev = self.device("stock", space=100)  # the 300-byte loader does not fit
        rc, out, _ = self.run_fleet()
        self.assertEqual(rc, 1)
        self.assertIn("FAILED: Update error: ERROR[4]: Not Enough Space", out)
        self.assertEqual(dev.state, "stock")

    def test_up_to_date_is_skipped(self):
        dev = self.device("miblo", fw=NEW)
        rc, out, prompts = self.run_fleet("--update")
        self.assertEqual(rc, 0)
        self.assertIn(ff.SKIP_CURRENT, out)
        self.assertEqual((dev.uploads, dev.opened, prompts), ([], 0, []))

    def test_other_build_is_not_up_to_date(self):
        self.device("miblo", fw=NEW, build="abc1234")
        rc, out, _ = self.run_fleet("--dry-run", "--update", "--build", "fff0000")
        self.assertIn(ff.OTA, out)

    def test_reboot_timeout(self):
        dev = self.device("stock", stuck=True)
        rc, out, _ = self.run_fleet("--stage1-timeout", "0.2")
        self.assertIn("after 0.2s", out)
        self.assertEqual(rc, 1)
        self.assertIn("timed out", out)
        self.assertEqual(dev.state, "stock")

    def test_wrong_version_after_install_fails(self):
        self.device("loader", next_fw="0.0.9")
        rc, out, _ = self.run_fleet("--stage2-timeout", "0.3", "--keep-wifi")
        self.assertEqual(rc, 1)
        self.assertIn("timed out", out)

    def test_update_with_code(self):
        dev = self.device("miblo", fw="0.1.0", code="4821", lock_once=True)
        rc, out, prompts = self.run_fleet("--update", codes=["12", "0000", "4821"])
        self.assertEqual(rc, 0, out)
        self.assertEqual(dev.opened, 2)  # first one answered 429 retryAfter
        self.assertIn("locked", out)
        self.assertIn("wrong code", out)
        self.assertEqual(len(prompts), 3)
        self.assertEqual(prompts[0], "Enter the 4-digit code shown on Miblo-4F2A (%s): " % dev.host)
        self.assertEqual((dev.state, dev.fw), ("miblo", NEW))
        self.assertIn("1 flashed", out)

    def test_needs_update_flag(self):
        dev = self.device("miblo", fw="0.1.0")
        rc, out, prompts = self.run_fleet()
        self.assertEqual(rc, 0)
        self.assertIn(ff.SKIP_NEEDS_UPDATE, out)
        self.assertEqual((dev.opened, prompts), (0, []))


# ---------------------------------------------------------------------------------------------
# Access-point mode: a fake macOS Wi-Fi (no real command runs, the Mac's Wi-Fi is never touched)

class FakeAir:
    """Answers the macOS commands macwifi.MacWifi runs. The visible networks follow the fake
    devices' states (stock -> its stock SSID, loader -> Miblo-Installer-<chip>, miblo ->
    Miblo-Setup-<chip>, rebooting -> nothing); joins are recorded."""

    def __init__(self, devices, home="HomeNet", others=("Neighbor",)):
        self.devices = devices
        self.home, self.others = home, list(others)
        self.joined = home
        self.joins = []           # argv after the interface: [ssid] or [ssid, password]
        self.redacted = False
        self.fail_join = set()
        self.raise_on = None      # a command word that raises (simulates an unexpected crash)
        self.scans = 0
        self.on_scan = None
        self.hidden = False       # the scan works but shows no unit network
        self.arp = True           # `arp -n` knows the joined unit's MAC
        self.joined_dev = None    # the unit joined (several may share a stock name)
        self.bssid_joins = []     # BSSIDs joined through the helper app

    @staticmethod
    def bssid_of(dev):
        return "5e:cf:7f:12:%s:%s" % (dev.chip[:2].lower(), dev.chip[2:].lower())

    def scanned(self):
        """What a scan shows (the fake devices keep broadcasting; `hidden` just hides them)."""
        return [self.home] + self.others + ([] if self.hidden else [s for s, _ in self.pairs()])

    @staticmethod
    def ssid_of(dev):
        return {"stock": dev.stock_ssid, "loader": "Miblo-Installer-" + dev.chip,
                "miblo": "Miblo-Setup-" + dev.chip}.get(dev.state)

    def pairs(self):
        """(ssid, unit) per broadcasting unit, strongest first: stock units may share a name."""
        return [(self.ssid_of(d), d) for d in self.devices if self.ssid_of(d)]

    def broadcasting(self):
        """ssid -> the strongest unit on it (the one joining by name reaches)."""
        out = {}
        for ssid, d in self.pairs():
            out.setdefault(ssid, d)
        return out

    def visible(self):
        return [self.home] + self.others + [s for s, _ in self.pairs()]

    def dev_joined(self):
        d = self.joined_dev
        if d is not None and self.ssid_of(d) == self.joined:
            return d
        return self.broadcasting().get(self.joined)

    def host(self, ssid):
        d = self.dev_joined() if ssid == self.joined else None
        d = d or self.broadcasting().get(ssid)
        return d.host if d else "127.0.0.1:1"

    def join_bssid(self, bssid):
        for ssid, d in self.pairs():
            if self.bssid_of(d) == bssid.lower():
                self.bssid_joins.append(bssid.lower())
                self.joined, self.joined_dev = ssid, d
                return True
        return False

    def run(self, argv, timeout):
        if self.raise_on and self.raise_on in argv:
            raise RuntimeError("boom")
        if argv[:2] == ["networksetup", "-listallhardwareports"]:
            return 0, ("Hardware Port: Ethernet\nDevice: en1\nEthernet Address: aa\n\n"
                       "Hardware Port: Wi-Fi\nDevice: en0\nEthernet Address: bb\n")
        if argv[0] == "system_profiler":
            self.scans += 1
            if self.on_scan:
                self.on_scan(self.scans)
            vis = self.scanned()
            cur = self.joined if self.joined in self.visible() else None
            others = [s for s in vis if s != cur]
            if self.redacted:
                cur, others = ("<redacted>" if cur else None), ["<redacted>"] * len(others)
            iface = {"_name": "en0",
                     "spairport_airport_other_local_wireless_networks": [{"_name": s} for s in others]}
            if cur:
                iface["spairport_current_network_information"] = {"_name": cur}
            return 0, json.dumps({"SPAirPortDataType": [{"spairport_airport_interfaces": [iface]}]})
        if argv[:3] == ["networksetup", "-setairportnetwork", "en0"]:
            ssid = argv[3]
            self.joins.append(argv[3:])
            if ssid in self.fail_join:
                return 0, "Failed to join network %s.\nError: -3900  The operation couldn't be completed." % ssid
            if ssid not in self.visible():
                return 0, "Could not find network %s." % ssid
            self.joined, self.joined_dev = ssid, None
            return 0, ""
        if argv[:2] == ["arp", "-n"]:
            dev = self.dev_joined()
            if self.arp and dev:
                return 0, "? (%s) at %s on en0 ifscope [ethernet]\n" % (argv[2], self.bssid_of(dev))
            return 1, "%s (%s) -- no entry\n" % (argv[2], argv[2])
        if argv[:3] == ["ipconfig", "getifaddr", "en0"]:
            if self.joined == self.home:
                return 0, "192.168.0.10\n"
            if self.joined in self.broadcasting():
                return 0, "192.168.4.2\n"
            return 1, ""
        return 1, "unexpected command %r" % (argv,)


class AccessPointTest(unittest.TestCase):
    setUp_bench = FleetTest.setUp
    tearDown = FleetTest.tearDown
    device = FleetTest.device

    def setUp(self):
        self.setUp_bench()
        self.air = FakeAir(self.devices)

    def run_ap(self, *argv, codes=(), interactive=True):
        out = io.StringIO()
        answers = list(codes)
        prompts = []

        def ask(prompt):
            prompts.append(prompt)
            return answers.pop(0)

        rc = ff.main(["--dist", self.dist, "--via-ap"] + list(argv), timing=ff.Timing(**FAST), out=out,
                     ask=ask, sleep=time.sleep, interactive=interactive,
                     wifi=macwifi.MacWifi(run=self.air.run), ap_host=self.air.host)
        return rc, out.getvalue(), prompts

    def test_ssid_patterns(self):
        stock = re.compile(ff.DEFAULT_STOCK_SSID, re.I)
        for name in ("GIFTV", "SmallTV", "GeekMagic", "SmallTV-Ultra", "giftv_1234"):
            self.assertEqual(ff.ssid_kind(name, stock), ff.STOCK, name)
        for name in ("HomeNet", "MyGIFTV", "Miblo-Setup"):
            self.assertIsNone(ff.ssid_kind(name, stock), name)
        self.assertEqual(ff.ssid_kind("Miblo-Installer-4F2A", stock), ff.LOADER)
        self.assertEqual(ff.ssid_kind("Miblo-Setup-4F2A", stock), ff.MIBLO)
        self.assertEqual(ff.ssid_key("Miblo-Setup-4F2A"), ff.ssid_key("Miblo-Installer-4f2a"))

    def test_macos_output_parsing(self):
        self.assertEqual(macwifi.parse_hardware_ports(self.air.run(["networksetup", "-listallhardwareports"], 1)[1]),
                         "en0")
        self.assertEqual(macwifi.parse_summary_ssid("<dictionary> {\n  SSID : Home Net\n}"), "Home Net")
        cur, vis = macwifi.parse_airport_json(self.air.run(["system_profiler"], 1)[1], "en0")
        self.assertEqual((cur, vis), ("HomeNet", ["HomeNet", "Neighbor"]))

    def test_stock_to_installer_to_miblo(self):
        dev = self.device("stock", stock_ssid="GIFTV")
        rc, out, _ = self.run_ap()
        self.assertEqual(rc, 0, out)
        self.assertEqual([k for k, _ in dev.uploads], ["stock", "loader"])
        self.assertEqual((dev.state, dev.fw), ("miblo", NEW))
        self.assertEqual(self.air.joins, [["GIFTV"], ["Miblo-Installer-4F2A"], ["Miblo-Setup-4F2A"], ["HomeNet"]])
        self.assertEqual(self.air.joined, "HomeNet")
        self.assertIn("1 flashed, 0 failed, 0 skipped", out)
        self.assertIn("GIFTV", out.split("Summary:")[1])

    def test_stock_password_only_for_stock_network(self):
        dev = self.device("stock", stock_ssid="SmallTV")
        rc, out, _ = self.run_ap("--stock-pass", "s3cret")
        self.assertEqual(rc, 0, out)
        self.assertEqual(self.air.joins[0], ["SmallTV", "s3cret"])
        self.assertEqual(self.air.joins[1:], [["Miblo-Installer-4F2A"], ["Miblo-Setup-4F2A"], ["HomeNet"]])
        self.assertEqual(dev.fw, NEW)

    def test_installer_only(self):
        dev = self.device("loader", chip="1A2B")
        rc, out, _ = self.run_ap()
        self.assertEqual(rc, 0, out)
        self.assertEqual([k for k, _ in dev.uploads], ["loader"])
        self.assertEqual(self.air.joins, [["Miblo-Installer-1A2B"], ["Miblo-Setup-1A2B"], ["HomeNet"]])
        self.assertIn("1 flashed", out)
        self.assertEqual(dev.keepwifi, [False])  # units ship clean

    def test_keep_wifi_passed_in_ap_mode(self):
        dev = self.device("loader", chip="1A2B")
        rc, out, _ = self.run_ap("--keep-wifi")
        self.assertEqual(rc, 0, out)
        self.assertEqual(dev.keepwifi, [True])

    def test_setup_up_to_date_is_skipped(self):
        dev = self.device("miblo", fw=NEW)
        rc, out, prompts = self.run_ap()
        self.assertEqual(rc, 0, out)
        self.assertIn(ff.SKIP_CURRENT, out)
        self.assertEqual((dev.uploads, dev.opened, prompts), ([], 0, []))
        self.assertEqual(self.air.joins, [["Miblo-Setup-4F2A"], ["HomeNet"]])

    def test_setup_older_updated_without_code(self):
        dev = self.device("miblo", fw="0.1.0", codeless=True)
        rc, out, prompts = self.run_ap()  # no --update needed in AP mode
        self.assertEqual(rc, 0, out)
        self.assertEqual(prompts, [])
        self.assertEqual([k for k, _ in dev.uploads], ["miblo"])
        self.assertEqual((dev.state, dev.fw), ("miblo", NEW))
        self.assertEqual(self.air.joins[0], ["Miblo-Setup-4F2A"])
        self.assertEqual(self.air.joins[-2:], [["Miblo-Setup-4F2A"], ["HomeNet"]])
        self.assertIn("no code needed", out)

    def test_setup_older_asks_for_code(self):
        dev = self.device("miblo", fw="0.1.0", code="4821")
        rc, out, prompts = self.run_ap(codes=["4821"])
        self.assertEqual(rc, 0, out)
        self.assertEqual(prompts, ["Enter the 4-digit code shown on Miblo-Setup-4F2A: "])
        self.assertEqual((dev.state, dev.fw), ("miblo", NEW))
        self.assertIn("1 flashed", out)

    def test_setup_needing_code_skipped_without_terminal(self):
        dev = self.device("miblo", fw="0.1.0")
        rc, out, prompts = self.run_ap(interactive=False)
        self.assertEqual(rc, 0, out)
        self.assertEqual((prompts, dev.uploads, dev.fw), ([], [], "0.1.0"))
        self.assertIn(ff.SKIP_NO_TTY, out)
        self.assertIn("0 flashed, 0 failed, 1 skipped", out)

    def test_redacted_ssids(self):
        self.device("stock")
        self.air.redacted = True
        rc, out, _ = self.run_ap()
        self.assertEqual(rc, 2)
        self.assertIn("Location Services", out)
        self.assertIn("Privacy & Security", out)
        self.assertEqual(self.air.joins, [])

    def test_restores_wifi_on_crash(self):
        self.device("loader")
        self.air.raise_on = "getifaddr"
        with self.assertRaises(RuntimeError):
            self.run_ap()
        self.assertEqual(self.air.joins, [["Miblo-Installer-4F2A"], ["HomeNet"]])
        self.assertEqual(self.air.joined, "HomeNet")

    def test_join_failure(self):
        dev = self.device("stock", stock_ssid="GeekMagic")
        self.air.fail_join = {"GeekMagic"}
        rc, out, _ = self.run_ap()
        self.assertEqual(rc, 1)
        self.assertIn("FAILED: could not join GeekMagic", out)
        self.assertEqual(dev.uploads, [])
        self.assertEqual(self.air.joins[-1], ["HomeNet"])

    def test_dry_run_joins_nothing(self):
        stock = self.device("stock", stock_ssid="GIFTV")
        self.device("loader", chip="1A2B")
        self.device("miblo", fw="0.1.0", chip="3C4D")
        rc, out, _ = self.run_ap("--dry-run")
        self.assertEqual(rc, 0, out)
        plan = out.split("Plan")[1]
        for text in ("GIFTV", ff.INSTALL, "Miblo-Installer-1A2B", ff.STAGE2, "Miblo-Setup-3C4D", ff.CHECK):
            self.assertIn(text, plan)
        self.assertNotIn("Neighbor", plan)
        self.assertEqual(self.air.joins, [])
        self.assertEqual(stock.uploads, [])

    def test_max_units(self):
        a = self.device("loader", chip="1A2B")
        b = self.device("loader", chip="3C4D")
        rc, out, _ = self.run_ap("--max", "1")
        self.assertEqual(rc, 0, out)
        self.assertEqual(sorted([len(a.uploads), len(b.uploads)]), [0, 1])
        self.assertIn("1 flashed, 0 failed, 0 skipped", out)
        self.assertEqual(self.air.joins[-1], ["HomeNet"])

    def test_loop_picks_up_units_powered_later(self):
        first = self.device("loader", chip="1A2B")
        later = []

        def power_on(_):
            if not later and first.state == "miblo" and first.fw == NEW:
                later.append(self.device("stock", stock_ssid="SmallTV", chip="3C4D"))

        self.air.on_scan = power_on
        rc, out, _ = self.run_ap("--loop", "--max", "2")
        self.assertEqual(rc, 0, out)
        self.assertEqual(len(later), 1)
        self.assertEqual((later[0].state, later[0].fw), ("miblo", NEW))
        self.assertIn("Waiting for more units", out)
        self.assertIn("2 flashed", out)
        self.assertEqual(self.air.joins[-1], ["HomeNet"])


    def test_loop_never_revisits_a_unit_finished_in_this_run(self):
        # A finished unit that the Mac reaches again by the shared stock name (here: it shows up
        # as GIFTV once more) is recognized by its MAC and left alone; the next unit is flashed.
        first = self.device("stock", stock_ssid="GIFTV", chip="1A2B")
        later = []

        def on_scan(_):
            # after it is finished (the Mac reached its Miblo-Setup network)
            if self.air.joined == "Miblo-Setup-1A2B" and first.fw == NEW and not later:
                first.state = "stock"  # back on the shared name, as far as the Mac can tell
                later.append(self.device("stock", stock_ssid="SmallTV", chip="3C4D"))

        self.air.on_scan = on_scan
        rc, out, _ = self.run_ap("--loop", "--max", "2")
        self.assertEqual(rc, 0, out)
        self.assertEqual(len(first.uploads), 2)  # installer + firmware, once
        self.assertIn("already done in this run (chip 1a2b): skipping", out)
        self.assertEqual((later[0].state, later[0].fw), ("miblo", NEW))
        self.assertIn("2 flashed, 0 failed, 0 skipped", out)

    def test_pending_skips_names_whose_scanned_units_are_all_done(self):
        class Wifi:
            bssids = {"GIFTV": ["5e:cf:7f:12:1a:2b", "5e:cf:7f:12:3c:4d"], "SmallTV": ["5e:cf:7f:12:1a:2b"],
                      "GeekMagic": []}

        runner = type("R", (), {"timing": ff.Timing()})()
        args = type("A", (), {"stock_ssid": ff.DEFAULT_STOCK_SSID})()
        fleet = ff.ApFleet(runner, Wifi(), args, ff.ap_device_host)
        names = ["GIFTV", "SmallTV", "GeekMagic", "HomeNet"]
        self.assertEqual(fleet.pending(names), ["GIFTV", "SmallTV", "GeekMagic"])
        fleet.done_chips.add("1a2b")
        # SmallTV only has the finished unit; GIFTV still has another one; no BSSIDs: unknown.
        self.assertEqual(fleet.pending(names), ["GIFTV", "GeekMagic"])
        fleet.done_chips.add("3c4d")
        self.assertEqual(fleet.pending(names), ["GeekMagic"])


class FakeHelperRun:
    """Answers the commands ScanHelper runs: xcode-select, xcrun, swiftc (creates the binary),
    codesign and `open -W -n MibloWiFiScan.app --args <out>` (writes the scan JSON)."""

    def __init__(self, air, authorized=True, swift=True):
        self.air, self.authorized, self.swift = air, authorized, swift
        self.join_fails = False
        self.calls = []

    def run(self, argv, timeout):
        self.calls.append(list(argv))
        if argv == ["xcode-select", "-p"]:
            return (0, "/Library/Developer/CommandLineTools\n") if self.swift else (2, "error: no developer tools")
        if argv == ["xcrun", "--find", "swiftc"]:
            return 0, "/usr/bin/swiftc\n"
        if argv[:2] == ["xcrun", "swiftc"]:
            out = Path(argv[argv.index("-o") + 1])
            out.write_bytes(b"binary")
            return 0, ""
        if argv[0] == "codesign":
            return 0, ""
        if argv[:3] == ["open", "-W", "-n"] and "--join" in argv:
            ok = self.authorized and not self.join_fails and self.air.join_bssid(argv[argv.index("--join") + 1])
            doc = {"authorized": self.authorized, "joined": ok, "error": None if ok else "access point not found"}
            Path(argv[argv.index("--args") + 1]).write_text(json.dumps(doc))
            return 0, ""
        if argv[:3] == ["open", "-W", "-n"]:
            units = [] if self.air.hidden else self.air.pairs()
            nets = [{"ssid": s if self.authorized else None, "bssid": None, "rssi": -40, "channel": 6}
                    for s in [self.air.home] + self.air.others]
            nets += [{"ssid": s if self.authorized else None,
                      "bssid": FakeAir.bssid_of(d).upper() if self.authorized else None,
                      "rssi": -50, "channel": 6} for s, d in units]
            doc = {"authorized": self.authorized, "status": "authorized" if self.authorized else "denied",
                   "interface": "en0", "current_ssid": self.air.joined if self.authorized else None,
                   "networks": nets, "error": None}
            Path(argv[-1]).write_text(json.dumps(doc))
            return 0, ""
        return 1, "unexpected command %r" % (argv,)


class ScanHelperTest(unittest.TestCase):
    setUp_ap = AccessPointTest.setUp
    setUp_bench = FleetTest.setUp
    tearDown = FleetTest.tearDown
    device = FleetTest.device

    def setUp(self):
        self.setUp_ap()
        self.cache = Path(self.tmp.name) / "cache"
        self.src = Path(self.tmp.name) / "src"
        shutil.copytree(macwifi.HELPER_SRC, self.src)
        self.hrun = FakeHelperRun(self.air)
        self.logs = []
        self.helper = macwifi.ScanHelper(run=self.hrun.run, cache_dir=self.cache, src_dir=self.src,
                                         log=self.logs.append)

    def run_ap(self, *argv, codes=(), interactive=True):
        out = io.StringIO()
        wifi = macwifi.MacWifi(run=self.air.run, helper=self.helper, log=self.logs.append)
        rc = ff.main(["--dist", self.dist, "--via-ap"] + list(argv), timing=ff.Timing(**FAST), out=out,
                     ask=lambda p: codes[0], sleep=time.sleep, interactive=interactive,
                     wifi=wifi, ap_host=self.air.host)
        return rc, out.getvalue(), []

    def test_helper_sources_exist(self):
        plist = (macwifi.HELPER_SRC / "Info.plist").read_text()
        for text in ("dev.miblo.wifiscan", "MibloWiFiScan", "LSUIElement", "NSLocationWhenInUseUsageDescription",
                     "NSLocationUsageDescription", "Miblo needs Wi-Fi network names to find gadgets to flash."):
            self.assertIn(text, plist)
        swift = (macwifi.HELPER_SRC / "main.swift").read_text()
        for text in ("scanForNetworks(withName: nil)", "CLLocationManager", "requestWhenInUseAuthorization",
                     "MIBLO_WIFISCAN_OUT"):
            self.assertIn(text, swift)

    def test_helper_json_parsing(self):
        res = macwifi.parse_helper_json(json.dumps({
            "authorized": True, "status": "authorized", "current_ssid": "HomeNet",
            "current_bssid": "AA:BB:CC:DD:EE:FF",
            "networks": [{"ssid": "GIFTV", "bssid": "5E:CF:7F:12:4F:2A", "rssi": -48, "channel": 6},
                         {"ssid": "<redacted>", "bssid": None, "rssi": "-70", "channel": None},
                         {"ssid": "", "bssid": "", "rssi": None}, "junk"]}))
        self.assertTrue(res.authorized)
        self.assertEqual((res.current, res.current_bssid), ("HomeNet", "aa:bb:cc:dd:ee:ff"))
        self.assertEqual(res.networks[0], {"ssid": "GIFTV", "bssid": "5e:cf:7f:12:4f:2a", "rssi": -48, "channel": 6})
        self.assertEqual(res.networks[1], {"ssid": None, "bssid": None, "rssi": -70, "channel": None})
        self.assertEqual(len(res.networks), 3)
        self.assertFalse(macwifi.parse_helper_json('{"authorized": false}').authorized)
        with self.assertRaises(macwifi.WifiError):
            macwifi.parse_helper_json("not json")
        self.assertEqual(macwifi.mac_chip("5e:cf:7f:12:4f:2a"), "4f2a")
        self.assertEqual(macwifi.parse_arp_mac("? (192.168.4.1) at 5e:cf:7f:12:4f:2a on en0"), "5e:cf:7f:12:4f:2a")
        self.assertEqual(macwifi.parse_arp_mac("? (192.168.4.1) at 5e:cf:7f:2:f:a on en0"), "5e:cf:7f:02:0f:0a")
        self.assertIsNone(macwifi.parse_arp_mac("? (192.168.4.1) at (incomplete) on en0"))

    def test_build_on_first_use_then_reuse_then_rebuild(self):
        res = self.helper.scan()
        self.assertTrue(res.authorized)
        app = self.cache / "MibloWiFiScan.app"
        exe = app / "Contents" / "MacOS" / "MibloWiFiScan"
        self.assertEqual([c[:2] for c in self.hrun.calls][:3], [["xcode-select", "-p"], ["xcrun", "--find"], ["xcrun", "swiftc"]])
        self.assertEqual([c[0] for c in self.hrun.calls][3:], ["codesign", "open"])
        self.assertEqual(self.hrun.calls[2], ["xcrun", "swiftc", "-O", str(self.src / "main.swift"), "-framework",
                                              "CoreWLAN", "-framework", "CoreLocation", "-o", str(exe)])
        self.assertEqual(self.hrun.calls[3], ["codesign", "--force", "--sign", "-", "--deep", str(app)])
        self.assertEqual(self.hrun.calls[4][:5], ["open", "-W", "-n", str(app), "--args"])
        self.assertEqual((app / "Contents" / "Info.plist").read_bytes(), (self.src / "Info.plist").read_bytes())
        self.assertTrue(any("Building" in m for m in self.logs))

        self.hrun.calls.clear()
        self.helper.scan()   # up to date: just run it
        self.assertEqual([c[0] for c in self.hrun.calls], ["open"])

        with open(self.src / "main.swift", "a") as f:
            f.write("// changed\n")
        self.hrun.calls.clear()
        self.helper.scan()   # sources changed: rebuilt
        self.assertEqual([c[0] for c in self.hrun.calls], ["xcode-select", "xcrun", "xcrun", "codesign", "open"])

    def test_not_authorized_exits_2_with_instructions(self):
        self.device("stock")
        self.hrun.authorized = False
        rc, out, _ = self.run_ap()
        self.assertEqual(rc, 2, out)
        self.assertIn("MibloWiFiScan would like to use your location", out)
        self.assertIn("Privacy & Security -> Location Services -> MibloWiFiScan", out)
        self.assertEqual(self.air.joins, [])
        self.assertEqual(self.air.scans, 0)  # no system_profiler fallback once the helper runs

    def test_install_through_helper_scan(self):
        dev = self.device("stock", stock_ssid="GIFTV")
        self.air.arp = False  # the chip comes from the scanned BSSID instead
        rc, out, _ = self.run_ap()
        self.assertEqual(rc, 0, out)
        self.assertEqual((dev.state, dev.fw), ("miblo", NEW))
        # The stock name is joined by BSSID (through the helper); the others by name.
        self.assertEqual(self.air.bssid_joins, ["5e:cf:7f:12:4f:2a"])
        self.assertEqual(self.air.joins, [["Miblo-Installer-4F2A"], ["Miblo-Setup-4F2A"], ["HomeNet"]])
        self.assertIn("current network: HomeNet", out)
        self.assertEqual(self.air.scans, 0)

    def test_units_sharing_the_stock_name_are_each_flashed_once_by_bssid(self):
        # Ten factory units all call themselves GIFTV: joining by name would let macOS pick any of
        # them again and again. Each is joined by its BSSID and visited exactly once.
        units = [self.device("stock", stock_ssid="GIFTV", chip=c) for c in ("1A2B", "3C4D", "5E6F")]
        rc, out, _ = self.run_ap("--loop", "--max", "3")
        self.assertEqual(rc, 0, out)
        for u in units:
            self.assertEqual((u.state, u.fw, len(u.uploads)), ("miblo", NEW, 2), u.chip)
        self.assertEqual(self.air.bssid_joins, ["5e:cf:7f:12:1a:2b", "5e:cf:7f:12:3c:4d", "5e:cf:7f:12:5e:6f"])
        self.assertEqual([j[0] for j in self.air.joins if j[0].startswith("GIFTV")], [])  # never by name
        summary = out.split("Summary:")[1]
        for label in ("GIFTV 1a2b", "GIFTV 3c4d", "GIFTV 5e6f"):
            self.assertEqual(summary.count(label), 1, label)
        self.assertIn("3 flashed, 0 failed, 0 skipped", out)

    def test_bssid_join_failure_falls_back_to_the_name(self):
        dev = self.device("stock", stock_ssid="GIFTV")
        self.hrun.join_fails = True
        rc, out, _ = self.run_ap()
        self.assertEqual(rc, 0, out)  # the helper answers "not joined": joined by name instead
        self.assertTrue(any("can't join 5e:cf:7f:12:4f:2a" in m for m in self.logs), self.logs)
        self.assertEqual(self.air.joins[0], ["GIFTV"])
        self.assertEqual((dev.state, dev.fw), ("miblo", NEW))

    def test_no_swift_falls_back_to_system_profiler(self):
        self.device("loader", chip="1A2B")
        self.hrun.swift = False
        rc, out, _ = self.run_ap()
        self.assertEqual(rc, 0, out)
        self.assertTrue(any("using system_profiler" in m for m in self.logs), self.logs)
        self.assertGreater(self.air.scans, 0)
        self.assertEqual(self.air.joins, [["Miblo-Installer-1A2B"], ["Miblo-Setup-1A2B"], ["HomeNet"]])

    def test_scan_only_prints_table(self):
        self.device("stock", stock_ssid="GIFTV")
        self.device("miblo", fw=NEW, chip="3C4D")
        out = io.StringIO()
        wifi = macwifi.MacWifi(run=self.air.run, helper=self.helper, log=self.logs.append)
        rc = ff.main(["--via-ap", "--scan-only", "--dist", "/nonexistent"], out=out, wifi=wifi)
        text = out.getvalue()
        self.assertEqual(rc, 0, text)
        self.assertIn("Scanner: MibloWiFiScan", text)
        self.assertRegex(text, r"GIFTV\s+5e:cf:7f:12:4f:2a\s+-50\s+6\s+stock")
        self.assertRegex(text, r"Miblo-Setup-3C4D\s+\S+\s+-50\s+6\s+miblo")
        self.assertEqual(self.air.joins, [])


class NoScanTest(unittest.TestCase):
    setUp = AccessPointTest.setUp
    setUp_bench = FleetTest.setUp
    tearDown = FleetTest.tearDown
    device = FleetTest.device
    run_ap = AccessPointTest.run_ap
    def test_try_ssid_defaults_when_scan_finds_nothing(self):
        dev = self.device("stock", stock_ssid="SmallTV")
        self.air.hidden = True
        rc, out, _ = self.run_ap()
        self.assertEqual(rc, 0, out)
        self.assertEqual((dev.state, dev.fw), ("miblo", NEW))
        joins = collapse(self.air.joins)
        # GIFTV isn't there; SmallTV is, then its installer/setup by the derived names; SmallTV
        # again (next unit?) and GeekMagic are not there; back home.
        self.assertEqual(joins, ["GIFTV", "SmallTV", "Miblo-Installer-4F2A", "Miblo-Setup-4F2A", "SmallTV",
                                 "GeekMagic", "HomeNet"])
        self.assertIn("[GIFTV] not in range", out)
        self.assertIn("1 flashed, 0 failed, 0 skipped", out)
        self.assertNotIn("GeekMagic", out.split("Summary:")[1])

    def test_hidden_names_with_try_ssid_joins_by_name(self):
        dev = self.device("stock", stock_ssid="MyTV")
        self.air.redacted = True  # scanning unavailable: every name hidden
        rc, out, _ = self.run_ap("--try-ssid", "MyTV")
        self.assertEqual(rc, 0, out)
        self.assertEqual((dev.state, dev.fw), ("miblo", NEW))
        # Installer and setup names come from the chip id (MAC via ARP, then GET /info).
        self.assertEqual(collapse(self.air.joins), ["MyTV", "Miblo-Installer-4F2A", "Miblo-Setup-4F2A", "MyTV"])
        self.assertEqual(self.air.scans, 1)  # one scan attempt; everything else by name
        # The home network's name was hidden too: the user is told to rejoin it.
        self.assertIn("rejoin your network by hand", out)
        self.assertNotIn(["HomeNet"], self.air.joins)

    def test_derived_setup_name_after_stage2(self):
        dev = self.device("loader", chip="1A2B")
        self.air.redacted = True
        self.air.arp = False
        rc, out, _ = self.run_ap("--ssid", "Miblo-Installer-1A2B")
        self.assertEqual(rc, 0, out)
        self.assertEqual(dev.fw, NEW)
        self.assertEqual(collapse(self.air.joins), ["Miblo-Installer-1A2B", "Miblo-Setup-1A2B"])
        self.assertIn("waiting for Miblo-Setup-1A2B", out)

    def test_stage1_derives_installer_name_from_info_chip(self):
        # No ARP entry, no scan: the unit is found by name; its chip id comes from the stock SSID
        # suffix, and after stage 1 GET /info confirms it ("miblo-4f2a").
        dev = self.device("stock", stock_ssid="GIFTV-4F2A")
        self.air.redacted = True
        self.air.arp = False
        rc, out, _ = self.run_ap("--ssid", "GIFTV-4F2A")
        self.assertEqual(rc, 0, out)
        self.assertEqual(dev.fw, NEW)
        self.assertEqual(collapse(self.air.joins), ["GIFTV-4F2A", "Miblo-Installer-4F2A", "Miblo-Setup-4F2A"])

    def test_explicit_ssid_units_only(self):
        a = self.device("miblo", fw="0.1.0", chip="3C4D", codeless=True)
        b = self.device("loader", chip="1A2B")
        rc, out, _ = self.run_ap("--ssid", "Miblo-Setup-3C4D")
        self.assertEqual(rc, 0, out)
        self.assertEqual((a.fw, b.uploads), (NEW, []))
        self.assertEqual(self.air.joins, [["Miblo-Setup-3C4D"], ["Miblo-Setup-3C4D"], ["HomeNet"]])
        self.assertIn("1 flashed", out)

    def test_explicit_ssid_not_found_fails(self):
        rc, out, _ = self.run_ap("--ssid", "Miblo-Setup-9999")
        self.assertEqual(rc, 1, out)
        self.assertIn("FAILED: could not join Miblo-Setup-9999", out)
        self.assertEqual(self.air.joins[-1], ["HomeNet"])

    def test_dry_run_lists_names_to_try(self):
        self.air.hidden = True
        rc, out, _ = self.run_ap("--dry-run", "--ssid", "Miblo-Setup-3C4D")
        self.assertEqual(rc, 0, out)
        self.assertIn("Miblo-Setup-3C4D", out.split("Plan")[1])
        rc, out, _ = self.run_ap("--dry-run")
        for name in ff.DEFAULT_TRY_SSIDS:
            self.assertIn(name, out.split("Plan")[1])
        self.assertIn("try by name", out)
        self.assertEqual(self.air.joins, [])


# --- mDNS helpers: build DNS wire-format packets the way a gadget would, mirroring
# plugin/test/mdns.test.js's fixtures (Python port of plugin/lib/mdns.js).

def mdns_enc(name):
    out = bytearray()
    for label in name.split("."):
        if not label:
            continue
        b = label.encode("utf-8")
        out += bytes([len(b)]) + b
    out += b"\x00"
    return bytes(out)


def mdns_rr(name, rtype, rdata):
    head = bytearray(10)
    head[0:2] = rtype.to_bytes(2, "big")
    head[2:4] = (1).to_bytes(2, "big")
    head[4:8] = (120).to_bytes(4, "big")
    head[8:10] = len(rdata).to_bytes(2, "big")
    return mdns_enc(name) + bytes(head) + rdata


def mdns_response(records):
    h = bytearray(12)
    h[2:4] = (0x8400).to_bytes(2, "big")
    h[6:8] = len(records).to_bytes(2, "big")
    return bytes(h) + b"".join(records)


def mdns_txt(pairs):
    out = bytearray()
    for s in pairs:
        b = s.encode("utf-8")
        out += bytes([len(b)]) + b
    return bytes(out)


def gadget_response(inst="Miblo-4F2A._miblo._tcp.local", host="miblo-4f2a.local",
                    ip=(192, 168, 0, 42), port=80, dev_id="miblo-4f2a"):
    srv = bytes([0, 0, 0, 0, port >> 8, port & 255]) + mdns_enc(host)
    txt = mdns_txt(["id=%s" % dev_id, "name=Miblo-4F2A", "fw=0.1.0"])
    return mdns_response([
        mdns_rr("_miblo._tcp.local", ff.T_PTR, mdns_enc(inst)),
        mdns_rr(inst, ff.T_SRV, srv),
        mdns_rr(inst, ff.T_TXT, txt),
        mdns_rr(host, ff.T_A, bytes(ip)),
    ])


class FakeMdnsSocket:
    """A stand-in for socket.socket() good enough for discover_mdns(): sendto() can trigger a
    canned reply (queued for a later recvfrom()), and select.select() is patched (see
    fake_select below) to treat any socket with a queued message as ready."""

    def __init__(self, kind, log, on_send=None, bind_error=False):
        self.kind = kind
        self.log = log
        self.on_send = on_send
        self.bind_error = bind_error
        self.queue = []
        self.closed = False

    def setsockopt(self, level, optname, value=None):
        if self.kind == "multicast" and optname == socket.IP_ADD_MEMBERSHIP:
            self.log.append("%s:join" % self.kind)

    def bind(self, addr):
        if self.bind_error:
            raise OSError("EADDRINUSE")
        self.log.append("%s:bind:%d" % (self.kind, addr[1]))

    def sendto(self, data, addr):
        self.log.append("%s:send:%s:%d:%s" % (self.kind, addr[0], addr[1], "QU" if data[-2] else "QM"))
        if self.on_send:
            self.on_send(self)
        return len(data)

    def recvfrom(self, n):
        return self.queue.pop(0)

    def fileno(self):
        return id(self)

    def close(self):
        self.closed = True
        self.log.append("%s:close" % self.kind)


def fake_select(rlist, wlist, xlist, timeout):
    return [s for s in rlist if s.queue], [], []


class MdnsTest(unittest.TestCase):
    def test_build_query_sets_qu_bit(self):
        q = ff.build_mdns_query("_miblo._tcp.local")
        self.assertEqual(int.from_bytes(q[4:6], "big"), 1)
        self.assertEqual(list(q[-4:]), [0x00, 0x0C, 0x80, 0x01])
        q = ff.build_mdns_query("_miblo._tcp.local", unicast=False)
        self.assertEqual(list(q[-4:]), [0x00, 0x0C, 0x00, 0x01])

    def test_parse_and_resolve_gadget_response(self):
        records = ff.parse_mdns_message(gadget_response())
        self.assertEqual(ff.resolve_mdns_devices(records, "_miblo._tcp.local"),
                         [{"id": "miblo-4f2a", "name": "Miblo-4F2A", "addr": "192.168.0.42:80", "fw": "0.1.0"}])

    def test_parse_handles_name_compression_pointers(self):
        h = bytearray(12)
        h[6:8] = (1).to_bytes(2, "big")
        name = mdns_enc("_miblo._tcp.local")
        ptr_target = bytes([4]) + b"inst" + bytes([0xC0, 12])
        head = bytearray(10)
        head[0:2] = ff.T_PTR.to_bytes(2, "big")
        head[2:4] = (1).to_bytes(2, "big")
        head[8:10] = len(ptr_target).to_bytes(2, "big")
        records = ff.parse_mdns_message(bytes(h) + name + bytes(head) + ptr_target)
        self.assertEqual(records[0]["data"], "inst._miblo._tcp.local")

    def test_resolve_ignores_incomplete_announcements(self):
        records = ff.parse_mdns_message(mdns_response(
            [mdns_rr("_miblo._tcp.local", ff.T_PTR, mdns_enc("x._miblo._tcp.local"))]))
        self.assertEqual(ff.resolve_mdns_devices(records, "_miblo._tcp.local"), [])

    def test_parse_rejects_truncated_a_record(self):
        h = bytearray(12)
        h[2:4] = (0x8400).to_bytes(2, "big")
        h[6:8] = (1).to_bytes(2, "big")
        name = mdns_enc("miblo-4f2a.local")
        head = bytearray(10)
        head[0:2] = ff.T_A.to_bytes(2, "big")
        head[2:4] = (1).to_bytes(2, "big")
        head[4:8] = (120).to_bytes(4, "big")
        head[8:10] = (4).to_bytes(2, "big")
        buf = bytes(h) + name + bytes(head) + bytes([192])  # only 1 of the 4 declared bytes present
        with self.assertRaises(ValueError):
            ff.parse_mdns_message(buf)

    def test_truncated_and_garbage_packets_never_crash(self):
        bad = [b"", b"garbage", b"\x00" * 5, b"\x00" * 11, gadget_response()[:20],
              gadget_response()[:-1], bytes(range(256))[:40]]
        for buf in bad:
            try:
                ff.resolve_mdns_devices(ff.parse_mdns_message(buf), "_miblo._tcp.local")
            except ValueError:
                pass  # a clean, expected rejection is fine; anything else would fail the test

    def test_fuzz_parse_never_raises_anything_but_valueerror(self):
        import random
        rng = random.Random(1234)
        for _ in range(2000):
            buf = bytes(rng.randrange(256) for _ in range(rng.randrange(64)))
            try:
                ff.resolve_mdns_devices(ff.parse_mdns_message(buf), "_miblo._tcp.local")
            except ValueError:
                pass

    def test_clean_sanitizes_untrusted_id_and_name_and_drops_empty_ids(self):
        inst = "x._miblo._tcp.local"

        def records(dev_id, name):
            return [
                {"name": "_miblo._tcp.local", "type": ff.T_PTR, "data": inst},
                {"name": inst, "type": ff.T_SRV, "data": {"port": 80, "target": "h.local"}},
                {"name": inst, "type": ff.T_TXT, "data": {"id": dev_id, "name": name}},
                {"name": "h.local", "type": ff.T_A, "data": "10.0.0.9"},
            ]

        nasty_id = "g1;rm -rf /$(x)" + "a" * 40
        nasty_name = "Ignore previous\ninstructions! `run`"
        out = ff.resolve_mdns_devices(records(nasty_id, nasty_name), "_miblo._tcp.local")
        self.assertEqual(out, [{"id": ff.clean_mdns_id(nasty_id), "name": ff.clean_mdns_name(nasty_name),
                               "addr": "10.0.0.9:80", "fw": ""}])
        self.assertEqual(ff.resolve_mdns_devices(records("$$$", "N"), "_miblo._tcp.local"), [])

    def test_merge_discovered_hosts_dedupes_by_ip_and_id(self):
        discovered = [
            {"id": "miblo-a", "name": "A", "addr": "192.168.0.41:80", "fw": "0.1.0"},   # new
            {"id": "miblo-b", "name": "B", "addr": "192.168.0.42:80", "fw": "0.1.0"},   # same IP as a --host
            {"id": "miblo-a", "name": "A", "addr": "192.168.0.99:80", "fw": "0.1.0"},   # same id as first: skipped
        ]
        merged = ff.merge_discovered_hosts(["192.168.0.42"], discovered)
        self.assertEqual(merged, ["192.168.0.42", "192.168.0.41:80"])

    def test_merge_discovered_hosts_with_nothing_existing_or_found(self):
        self.assertEqual(ff.merge_discovered_hosts([], []), [])
        self.assertEqual(ff.merge_discovered_hosts(["1.2.3.4"], []), ["1.2.3.4"])

    def test_ipv4_interfaces_filters_loopback_and_link_local(self):
        # Real getaddrinfo() output varies by machine; this only pins the filtering rules using
        # ipv4_interfaces()'s own `add()` behavior indirectly is not possible without a real
        # lookup, so we sanity-check the live result shape instead.
        for ip in ff.ipv4_interfaces():
            self.assertFalse(ip.startswith("127."))
            self.assertFalse(ip.startswith("169.254."))

    def test_discover_mdns_uses_one_socket_per_interface_and_dedupes(self):
        log = []

        def factory(kind):
            return FakeMdnsSocket(kind, log, on_send=lambda s: s.queue.append((gadget_response(), ("x", 0))))

        with mock.patch("select.select", side_effect=fake_select):
            found = ff.discover_mdns(interfaces=["192.168.0.158", "10.8.0.2"], socket_factory=factory,
                                     timeout=0.05)
        self.assertEqual(found, [{"id": "miblo-4f2a", "name": "Miblo-4F2A", "addr": "192.168.0.42:80",
                                  "fw": "0.1.0"}])
        sends = [l for l in log if ":send:" in l]
        self.assertEqual(len(sends), 2)  # one unicast query per interface, not one shared/looped socket
        self.assertTrue(all(l.endswith(":QU") for l in sends))
        self.assertTrue(any(l.startswith("multicast:bind:5353") for l in log))

    def test_discover_mdns_tolerates_bind_failures(self):
        log = []

        def factory(kind):
            return FakeMdnsSocket(kind, log, bind_error=(kind == "multicast"),
                                  on_send=lambda s: s.queue.append((gadget_response(), ("x", 0))))

        with mock.patch("select.select", side_effect=fake_select):
            found = ff.discover_mdns(interfaces=["192.168.0.158"], socket_factory=factory, timeout=0.05)
        self.assertEqual([d["id"] for d in found], ["miblo-4f2a"])
        self.assertTrue(any(":send:" in l for l in log))

    def test_discover_mdns_ignores_garbage_alongside_a_good_reply(self):
        log = []

        def on_send(s):
            s.queue.append((b"garbage", ("x", 0)))
            s.queue.append((gadget_response(), ("x", 0)))

        def factory(kind):
            return FakeMdnsSocket(kind, log, on_send=on_send)

        with mock.patch("select.select", side_effect=fake_select):
            found = ff.discover_mdns(interfaces=["192.168.0.158"], socket_factory=factory, timeout=0.05)
        self.assertEqual([d["id"] for d in found], ["miblo-4f2a"])

    def test_discover_mdns_returns_empty_when_socket_creation_fails(self):
        def factory(kind):
            raise OSError("no sockets available")

        found = ff.discover_mdns(interfaces=["192.168.0.158"], socket_factory=factory, timeout=0.05)
        self.assertEqual(found, [])


class ImageVersionTest(unittest.TestCase):
    setUp = FleetTest.setUp
    tearDown = FleetTest.tearDown

    def test_images_follow_the_sources_version(self):
        args = ff.parse_args(["--host", "x", "--dist", self.dist])
        with mock.patch.object(ff, "source_version", lambda: "0.1.0"):
            images = ff.resolve_images(args)
        self.assertEqual(images.version, "0.1.0")
        self.assertTrue(images.firmware.name.endswith("-0.1.0.bin"))
        self.assertTrue(images.loader.name.endswith("-0.1.0.bin"))
        out = io.StringIO()
        with mock.patch.object(ff, "source_version", lambda: "9.9.9"):
            images = ff.resolve_images(args, out)  # explicit --dist: newest, with a warning
            self.assertEqual(images.version, NEW)
            self.assertIn("WARNING", out.getvalue())
            with mock.patch.object(ff, "DEFAULT_DIST", Path(self.dist)):
                with self.assertRaises(SystemExit) as cm:
                    ff.resolve_images(args)  # the default dist must hold the sources' version
                self.assertIn("9.9.9", str(cm.exception))
        header = Path(self.tmp.name) / "v.h"
        header.write_text('#pragma once\n#define MIBLO_FW_VERSION "1.2.3"\n')
        self.assertEqual(ff.source_version(header), "1.2.3")


class MakefileTest(unittest.TestCase):
    """`make -n` (print, don't run) against the real Makefile: pins how fleet-update expands
    --discover/--host/--subnet, without touching a network or a device."""

    def make_n(self, *args):
        env = dict(os.environ)
        env.pop("SUBNET", None)
        env.pop("HOSTS", None)
        firmware_dir = Path(__file__).resolve().parent.parent
        return subprocess.run(["make", "-n", *args], cwd=firmware_dir, env=env,
                              capture_output=True, text=True)

    def test_fleet_update_defaults_to_discover_with_no_hosts_or_subnet(self):
        proc = self.make_n("fleet-update")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertIn("--discover", proc.stdout)
        self.assertIn("--update", proc.stdout)
        self.assertNotIn("--subnet", proc.stdout)
        self.assertNotIn("--host ", proc.stdout)

    def test_fleet_update_hosts_overrides_discover(self):
        proc = self.make_n("fleet-update", 'HOSTS=192.168.0.41 192.168.0.42')
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertNotIn("--discover", proc.stdout)
        self.assertIn("--host 192.168.0.41 --host 192.168.0.42", proc.stdout)
        self.assertIn("--update", proc.stdout)

    def test_fleet_update_explicit_subnet_is_added_alongside_discover(self):
        proc = self.make_n("fleet-update", "SUBNET=192.168.5.0/24")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertIn("--discover --subnet 192.168.5.0/24", proc.stdout)

    def test_fleet_update_plan_adds_dry_run_and_needs_no_images(self):
        proc = self.make_n("fleet-update-plan")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertIn("--discover", proc.stdout)
        self.assertIn("--dry-run", proc.stdout)
        self.assertNotIn("build.sh", proc.stdout)  # no version-guard/build step for a plan

    def test_fleet_update_skips_the_network_detection_guard(self):
        proc = self.make_n("fleet-update")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertNotIn("Targets:", proc.stdout)  # that's check-target's echo; fleet-update drops it

    def test_fleet_and_fleet_plan_are_unaffected_by_the_discover_changes(self):
        proc = self.make_n("fleet-plan", "SUBNET=192.168.9.0/24")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertIn("--subnet 192.168.9.0/24 --dry-run", proc.stdout)
        self.assertNotIn("--discover", proc.stdout)


if __name__ == "__main__":
    unittest.main()
