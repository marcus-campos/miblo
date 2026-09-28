# Miblo

A tiny desk display for **Claude Code**. Miblo shows what your sessions are doing, flashes when one of them needs you (a permission prompt or a question), tells you when a response is done, and keeps your 5‑hour and weekly usage limits in sight.

> Miblo is an independent project. It is not affiliated with or endorsed by Anthropic. "Claude" and "Claude Code" are trademarks of Anthropic.

## How it works

```
Claude Code ──hooks──▶ miblo plugin (local bridge, 127.0.0.1) ──Wi‑Fi (LAN)──▶ Miblo gadget
            ──status line──┘
```

- **Plugin** (`plugin/`): a zero‑dependency Node.js (≥ 20) Claude Code plugin. Async hooks track every session; the official status line data provides context size, cost and rate limits. Nothing leaves your local network.
- **Firmware** (`firmware/`): ESP8266 firmware for the GeekMagic “Ultra” desk clock (240×240 IPS). It joins your Wi‑Fi, is discovered via mDNS, pairs with a 4‑digit code and renders the screens. UI in 9 languages: English, Português (BR), Português (PT), Español, Français, Italiano, Deutsch, Русский, 中文.

## Install the plugin

In Claude Code:

```
/plugin marketplace add marcus-campos/miblo
/plugin install miblo@miblo
```

Then run `/miblo:pair` and type the 4‑digit code shown on the gadget. Miblo will ask before linking your status line (needed for limits and context); your existing status line keeps working exactly the same. Undo any time with `/miblo:unlink-statusline` — run it **before uninstalling** the plugin.

Other commands: `/miblo:status`, `/miblo:mode overview|limits|sessions`, `/miblo:rotate on|off [every-seconds] [show-seconds] [id]` (alternate Overview with the Limits screen now and then; with no arguments it asks), `/miblo:update [id]` (updates the plugin and the gadget firmware from the latest GitHub release; `--file <path>` for a local .bin), `/miblo:reset <id>`.

## Flash the firmware (GeekMagic Ultra)

Installing from the stock GeekMagic firmware takes two uploads: its update page has too little space for the full Miblo image (it fails with `Not Enough Space`), so a tiny installer goes first.

1. Download `miblo-loader-geekmagic_ultra-<version>.bin` and `miblo-geekmagic_ultra-<version>.bin` from the [latest release](https://github.com/marcus-campos/miblo/releases/latest).
2. Open your clock's web page (`http://<clock-ip>/`), go to its firmware update page and upload `miblo-loader-geekmagic_ultra-<version>.bin`.
3. The clock reboots showing "Miblo installer" and an address like `http://<ip>/update`. (If it cannot reach your Wi‑Fi within 20 s it also opens an open network `Miblo-Installer-XXXX`; join it and use the address on screen.) Open that address and upload `miblo-geekmagic_ultra-<version>.bin` (the screen shows "Installing Miblo..." without progress; wait for the browser to show "OK").
4. The clock reboots into Miblo in setup mode: after a successful install the installer erases the Wi‑Fi the clock was using (so a unit never ships with the bench or shop network), so join the `Miblo-Setup-XXXX` network with your phone (scan the QR code on screen) and pick your Wi‑Fi. (To keep the current Wi‑Fi instead, upload to `http://<ip>/update?keepwifi=1`.) A failed upload erases nothing.

Keep the official GeekMagic firmware file around if you ever want to go back — Miblo's own `/update` page (`http://miblo-xxxx.local/update`) accepts it; it shows a 4-digit code on the gadget's screen that you type in the page. The boot screen shows the firmware version and build (e.g. `v0.1.0 (4534fb8)`); `/api/info` reports the same plus free heap. See the [on-device checklist](docs/firmware-checklist.md).

Use a proper 5 V / 1 A USB supply: a weak supply can brown-out during Wi-Fi bursts, and six quick resets in a row count as a hard reset.

## Flashing many units

`firmware/scripts/flash-fleet.py` (Python 3.9+, no dependencies) flashes a whole bench over the LAN. Each unit must first join the bench Wi‑Fi once through the stock GeekMagic setup portal; after that the script finds them, detects what each one runs (stock GeekMagic, Miblo installer, Miblo) and does the two-stage install on its own, several units in parallel (`--jobs`, default 4). Images come from `firmware/dist` (the newest `miblo-<board>-<ver>.bin` and its `miblo-loader-…` pair) unless you pass `--loader`/`--firmware`.

```sh
# see what is on the network and what would happen, without touching anything
python3 firmware/scripts/flash-fleet.py --subnet 192.168.0.0/24 --dry-run
# install Miblo on every stock/installer unit found (units already on this version are skipped)
python3 firmware/scripts/flash-fleet.py --subnet 192.168.0.0/24
# also update units running an older Miblo: each one shows a 4-digit code that you type in
python3 firmware/scripts/flash-fleet.py --subnet 192.168.0.0/24 --update
```

It prints a line per step per unit and a final table (host, before, after, result, seconds), and exits non-zero if any unit failed. Use `--host <ip>` (repeatable) instead of `--subnet` for specific units. By default it installs the images for the version in `firmware/include/miblo_version.h` (and says which); `make fleet` rebuilds them first when they are missing or older than the sources.

Units ship clean: the installer erases the bench Wi‑Fi after the full image is uploaded, so each unit restarts on its own `Miblo-Setup-XXXX` network. On macOS the script then joins those networks one by one to check the version (at `192.168.4.1`) and rejoins the Mac's network at the end. `--no-ap-verify` skips that check (units are reported as "installed (unverified: unit is in setup mode)", as on other systems); `--keep-wifi` keeps the units on the bench network and checks them there.

**Straight out of the box (macOS, `--via-ap`).** Factory units are on no network: each one broadcasts its own Wi‑Fi. With `--via-ap` the Mac joins those networks itself, one unit at a time (one radio), and talks to the device at `192.168.4.1`:

- stock GeekMagic network (`GIFTV`, `SmallTV`, `GeekMagic`…; change with `--stock-ssid REGEX`, add `--stock-pass` if yours has a password) → installer upload → joins the new `Miblo-Installer-XXXX` → full image → joins `Miblo-Setup-XXXX` and checks the version;
- `Miblo-Installer-XXXX` → full image, then the same check;
- `Miblo-Setup-XXXX` (installed, not configured) → skipped when up to date, otherwise updated right away (no `--update` needed): without a code only when the unit was never configured (never joined a Wi‑Fi from its portal and never paired — a factory reset does not bring that back), else it asks for the 4‑digit code on the screen (units that need a code are skipped when the script is not run from a terminal).

```sh
python3 firmware/scripts/flash-fleet.py --via-ap --dry-run   # scan and show the plan, join nothing
python3 firmware/scripts/flash-fleet.py --via-ap             # every unit in range, then stop
python3 firmware/scripts/flash-fleet.py --via-ap --loop      # keep going as you power units on; Ctrl-C to stop
```

The Mac goes back to its Wi‑Fi network at the end (also on errors and Ctrl‑C; a warning is printed if it can't). `--max N` stops after N units, `--iface` picks the Wi‑Fi interface.

**Location permission (MibloWiFiScan).** macOS only reveals Wi‑Fi network names to apps with Location Services access, and command-line tools (Terminal, Python) can't be granted it on recent macOS — they only see `<redacted>`. So the scan runs in a tiny helper app, `MibloWiFiScan` (source in `firmware/scripts/macos/MibloWiFiScan`), which the script builds with Swift on first use into `firmware/.cache/MibloWiFiScan.app` (rebuilt when its source changes; needs the Command Line Tools: `xcode-select --install`). The first scan makes macOS ask **"MibloWiFiScan would like to use your location" — click Allow**. If you dismissed it, turn it on in System Settings → Privacy & Security → Location Services → MibloWiFiScan. Check what the scan sees with `make fleet-ap-scan` (`flash-fleet.py --via-ap --scan-only`). Without Swift the script falls back to `system_profiler`.

**Without scanning.** Joining a network by name needs no permission, so the script can work blind:

- stock units have fixed names: `--try-ssid NAME` (repeatable) joins that name and goes on if `192.168.4.1` answers; `GIFTV`, `SmallTV` and `GeekMagic` are tried automatically when the scan is unavailable or finds no unit;
- after stage 1 the unit's chip id (from its MAC, and the installer's `GET /info` id `miblo-4f2a`) gives the names of its next networks, `Miblo-Installer-4F2A` and `Miblo-Setup-4F2A`, which are joined directly;
- specific units, e.g. shelf units already on Miblo: `--ssid Miblo-Setup-4F2A` (repeatable; `make fleet-ap SSIDS="Miblo-Setup-4F2A Miblo-Setup-9C01"`).

If the current network's name can't be read either, the script says so and you rejoin it by hand at the end.

## Reset

- From the gadget's page (`http://miblo-xxxx.local`) → Factory reset (confirm with the on‑screen code), or `/miblo:reset <id>`.
- **Hard reset without a computer:** power‑cycle the gadget 6 times in a row, unplugging it within 10 seconds each time. From the 3rd quick restart the screen counts down; leave it on to cancel.

## Development

```bash
# plugin
cd plugin && npm test

# firmware (PlatformIO in a local venv)
cd firmware
python3 -m venv .venv && .venv/bin/pip install platformio
.venv/bin/pio test -e native          # host unit tests
.venv/bin/pio run -e geekmagic_ultra  # device build
./scripts/build.sh geekmagic_ultra    # dist/miblo-geekmagic_ultra-<version>.bin
```

Adding a new board: see [`firmware/boards/README.md`](firmware/boards/README.md). The bridge ↔ gadget protocol is documented in the design spec and pinned by `fixtures/snapshots/*.json` (generated by the plugin tests, consumed by the firmware tests).

Releases: bump `firmware/include/miblo_version.h` and `plugin/.claude-plugin/plugin.json` to the same version, then push a `vX.Y.Z` tag — CI checks both match and publishes the binaries.

## License

See [LICENSE](LICENSE). Embedded third‑party fonts and libraries keep their own licenses — see [`firmware/lib/U8g2TFT/THIRD_PARTY_NOTICES.md`](firmware/lib/U8g2TFT/THIRD_PARTY_NOTICES.md) (includes WenQuanYi Bitmap Song, GPLv2 with font‑embedding exception) and [`firmware/THIRD_PARTY_NOTICES.md`](firmware/THIRD_PARTY_NOTICES.md) (time zone table from posix_tz_db, MIT).
