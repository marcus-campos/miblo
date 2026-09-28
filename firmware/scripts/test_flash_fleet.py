"""Tests for flash-fleet.py against local fake devices (http.server on 127.0.0.1 only).

Run: python3 -m unittest firmware/scripts/test_flash_fleet.py -v
"""
import importlib.util
import io
import sys
import json
import re
import tempfile
import threading
import time
import unittest
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
                    self.send(200, "Update Success! Rebooting...", "text/html")
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

    def tearDown(self):
        for d in self.devices:
            d.close()
        self.tmp.cleanup()

    def device(self, *a, **kw):
        d = FakeDevice(*a, **kw)
        self.devices.append(d)
        return d

    def run_fleet(self, *argv, codes=()):
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
                     interactive=True)
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

    def test_two_stage_install(self):
        stock = self.device("stock")
        loader = self.device("loader")
        rc, out, _ = self.run_fleet("--jobs", "2")
        self.assertEqual(rc, 0, out)
        self.assertEqual([k for k, _ in stock.uploads], ["stock", "loader"])
        self.assertEqual([k for k, _ in loader.uploads], ["loader"])
        for d in (stock, loader):
            self.assertEqual((d.state, d.fw), ("miblo", NEW))
        self.assertIn("miblo %s" % NEW, out)
        self.assertIn("2 flashed, 0 failed", out)

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
        rc, out, _ = self.run_fleet("--stage2-timeout", "0.3")
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

    @staticmethod
    def ssid_of(dev):
        return {"stock": dev.stock_ssid, "loader": "Miblo-Installer-" + dev.chip,
                "miblo": "Miblo-Setup-" + dev.chip}.get(dev.state)

    def broadcasting(self):
        return {self.ssid_of(d): d for d in self.devices if self.ssid_of(d)}

    def visible(self):
        return [self.home] + self.others + list(self.broadcasting())

    def host(self, ssid):
        d = self.broadcasting().get(ssid)
        return d.host if d else "127.0.0.1:1"

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
            vis = self.visible()
            cur = self.joined if self.joined in vis else None
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
            self.joined = ssid
            return 0, ""
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


if __name__ == "__main__":
    unittest.main()
