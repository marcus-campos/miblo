# Miblo

**A tiny desk display for Claude Code.** Miblo sits next to your keyboard and shows what your Claude Code sessions are doing, flashes when one of them needs you (a permission prompt or a question), tells you when a response is really finished, and keeps your 5-hour and weekly usage limits in sight. It is an open-source (MIT) Claude Code plugin plus ESP8266 firmware for an inexpensive off-the-shelf desk clock, so you can buy a ready-made Miblo or build your own in a few minutes.

> **Disclaimer:** Miblo is an independent project. It is not affiliated with, sponsored by or endorsed by Anthropic. "Claude" and "Claude Code" are trademarks of Anthropic.

## Contents

- [Features](#features)
- [How it works](#how-it-works)
- [Quick start](#quick-start)
- [Commands](#commands)
- [Do It Yourself: build your own Miblo](#do-it-yourself-build-your-own-miblo)
- [Updating](#updating)
- [Reset and recovery](#reset-and-recovery)
- [Troubleshooting](#troubleshooting)
- [Development](#development)
- [License](#license)

## Features

**On the screen**

- **Overview** (the default mode) adapts to what you're doing:
  - **While something is running,** your sessions come first. You get up to 3 session cards per page, each with the name, how long it has been in that state and what it's doing now ("Editing Header.tsx", "Bash · npm test"). Above them is a compact `5h / 7d` limits strip.
  - **When nothing is running,** you see big 5-hour and weekly limits with their reset times, the last session that finished and today's cost.
- **Alerts:**
  - **Amber** when a session needs you (a permission request or a question). You get a flash, then a highlight with the tool and command, then a fixed amber band until you respond. The reminder repeats every ~2 minutes while it's still pending.
  - **Blue** when a response is truly finished. A session that is still waiting on subagents or background tasks stays "running" ("Waiting on 2 agents") and doesn't trigger the blue alert.
- **Limits mode:** a large arc for the 5-hour window, a bar for the week and the time until each resets.
- **Sessions mode:** a detailed list of big cards that you can read at arm's length. It pages every 5 s when there are more than 3 sessions.
- **Optional rotation:** in Overview, switch to the Limits screen for a few seconds every so often. Alerts always take priority.
- **9 languages** for the screen and the setup/settings pages: English, Português (BR), Português (PT), Español, Français, Italiano, Deutsch, Русский and 中文.

**Setup and maintenance**

- **Phone setup through a captive portal:** scan the QR code on the screen, join `Miblo-Setup-XXXX`, then pick your Wi-Fi. The time zone and language come from your phone.
- **Automatic discovery** over mDNS (`miblo-xxxx.local`, `_miblo._tcp`) and pairing with a 4-digit code.
- **Settings page** in the browser (`http://miblo-xxxx.local`) for mode, brightness, alerts and their durations, discreet mode (hides commands and file paths), rotation, time zone, language and device name.
- **Firmware updates over Wi-Fi.** On a configured unit, every update needs a 4-digit code shown on the gadget's screen.
- **AirTag-style hard reset:** power it on 6 times in quick succession, with an on-screen countdown you can cancel.

**Privacy**

- Everything stays on your local network. The plugin's bridge listens only on `127.0.0.1` and talks to the gadget over your LAN. There's no cloud and no account.
- Usage limits, context and cost come only from Claude Code's **official status-line data**, never from undocumented endpoints. The status line is linked only with your consent, and your existing status line keeps working exactly as before.

## How it works

```
 Claude Code                        your computer                          your LAN
┌──────────────┐  hooks (async)   ┌──────────────────────────┐  HTTP + token  ┌──────────────┐
│  sessions    │ ───────────────▶ │  Miblo bridge (Node.js)  │ ─────────────▶ │ Miblo gadget │
│  status line │ ───────────────▶ │  listens on 127.0.0.1    │ ◀── mDNS ───── │  (ESP8266)   │
└──────────────┘  official JSON   └──────────────────────────┘                └──────────────┘
```

- **Plugin** (`plugin/`) is a Claude Code plugin in Node.js with no dependencies. Async hooks track every session's state and never slow Claude Code down. The status-line tap forwards a copy of the official status-line JSON and then runs your original status line command unchanged. A local bridge (started on demand, it exits after 30 minutes without sessions) builds a small snapshot and pushes it to every paired gadget.
- **Firmware** (`firmware/`) is ESP8266 firmware for the GeekMagic "Ultra" desk clock (240×240 IPS). It joins your Wi-Fi, announces itself over mDNS, pairs with a 4-digit code (which issues a random 128-bit token; 5 wrong codes in a row lock pairing for 60 s, doubling up to 1 h) and draws the screens.

## Quick start

For someone who already has a Miblo gadget. To build one, see [Do It Yourself](#do-it-yourself-build-your-own-miblo).

**Requirement:** [Node.js](https://nodejs.org) **20 or newer** on your `PATH`. The native Claude Code installer doesn't include Node. macOS, Linux and Windows (including WSL) are supported.

1. **Install the plugin.** In Claude Code, run:

   ```
   /plugin marketplace add marcus-campos/miblo
   /plugin install miblo@miblo
   /reload-plugins
   ```

2. **Connect the gadget to Wi-Fi.** Plug it in. The screen shows a QR code and the network name `Miblo-Setup-XXXX`. Scan the code with your phone (or join that network), and the setup page opens. Pick your network and type the password.
   - Miblo works only with **2.4 GHz** Wi-Fi.
   - Your router needs **WPA2**. WPA3-only routers aren't supported. If a "WPA2/WPA3" mixed-mode router refuses the connection, switch it to WPA2.

   Once it connects, the screen shows "Wi-Fi connected", a 4-digit pairing code and the gadget's IP.
3. **Pair.** In Claude Code, run `/miblo:pair` and type the 4-digit code from the screen.
4. **Allow the status line link.** `/miblo:pair` asks before linking your status line. This link is what the limits, context and cost need. Your current status line keeps working exactly the same. If you say no, you still get session states and alerts, but no limits. You can undo it any time with `/miblo:unlink-statusline`.

The gadget updates on your next Claude Code activity.

## Commands

The gadget `id` is shown by `/miblo:status`. When only one gadget is paired, you can leave it out wherever it's optional.

| Command | Arguments | What it does |
|---|---|---|
| `/miblo:pair` | `[ip]` | Finds gadgets over mDNS (or uses the IP you give), asks for the 4-digit code on the screen and pairs. It then offers to link the status line. |
| `/miblo:status` | | Shows whether the bridge is running and the status line is linked, and lists each paired gadget (online/offline), the active sessions and the limits. |
| `/miblo:mode` | `<overview\|limits\|sessions> [id]` | Sets a gadget's display mode. |
| `/miblo:rotate` | `<on\|off> [every-seconds] [show-seconds] [id]` | In Overview, shows the Limits screen for `show-seconds` once every `every-seconds`. `every` must be 10–3600 s, `show` must be 3–300 s and shorter than `every`. With no arguments, it shows the current setting and offers presets (every 1 min for 10 s, every 5 min for 15 s, every 15 min for 20 s, or off). |
| `/miblo:update` | `[id] [--file path]` | Updates the plugin and then the gadget firmware from the latest GitHub release. You confirm each step and type the on-screen code. `--file` sends a local `miblo-<board>-<version>.bin` instead. |
| `/miblo:link-statusline` | | Links Claude Code's status line to Miblo. Your original status line keeps its exact output. |
| `/miblo:unlink-statusline` | | Restores your original status line. **Run this before uninstalling the plugin.** |
| `/miblo:reset` | `<id>` | Factory-resets a paired gadget, which erases its Wi-Fi, pairings and settings. It asks you to confirm first. |

## Do It Yourself: build your own Miblo

### What to buy

A **GeekMagic "Ultra"** desk clock (also sold as **SmallTV Ultra**). It has an ESP8266 (ESP-12E/F, 4 MB flash) and a 240×240 ST7789 IPS screen. Out of the box it broadcasts its own Wi-Fi network named **`GIFTV`**, **`SmallTV`** or **`GeekMagic`**. You also need a decent **5 V / 1 A** USB power supply.

Other boards can be added: the firmware separates board-independent logic from a small per-board layer. See [`firmware/boards/README.md`](firmware/boards/README.md).

### Option A: no toolchain (browser only)

The stock GeekMagic firmware has too little space for over-the-air updates to take the full Miblo image in one go (the upload fails with **"Not Enough Space"**). So you install it in two steps: first a tiny installer, then the full image.

1. From the [latest release](https://github.com/marcus-campos/miblo/releases/latest), download both files:
   - `miblo-loader-geekmagic_ultra-<version>.bin` (the installer)
   - `miblo-geekmagic_ultra-<version>.bin` (the full firmware)
2. Connect the clock to your Wi-Fi through its stock setup portal: join its `GIFTV` / `SmallTV` / `GeekMagic` network and follow the clock's instructions. Note the IP it shows.
3. Open the clock's web page (`http://<clock-ip>/`), go to its **firmware update** page and upload the **loader** (`miblo-loader-…bin`) first.
4. The clock reboots and shows **"Miblo installer"** with an address like `http://<ip>/update`. Open that address and upload the **full image** (`miblo-geekmagic_ultra-…bin`). The screen shows "Installing Miblo..." without a progress bar. Wait for the browser to show "OK".
   - If the installer can't reach your Wi-Fi within 20 s, it also opens an open network `Miblo-Installer-XXXX`. Join it and use the address shown on the screen.
5. The clock reboots into Miblo and shows the **`Miblo-Setup-XXXX`** QR code. After a successful install, the installer erases the Wi-Fi the clock was using, so continue with the [Quick start](#quick-start).
   - To keep the current Wi-Fi instead, upload to `http://<ip>/update?keepwifi=1`.
   - A failed upload erases nothing, and the installer stays up so you can retry.

Keep the official GeekMagic `.bin` if you ever want to go back (see [Restoring the stock firmware](#restoring-the-stock-firmware)).

### Option B: build from source

You need Python 3 and git. PlatformIO goes in a local virtualenv:

```sh
git clone https://github.com/marcus-campos/miblo.git
cd miblo/firmware
python3 -m venv .venv && .venv/bin/pip install platformio
make build     # native tests + both images
make images    # rebuild only if the images are missing or older than the sources
```

The images land in `firmware/dist/`:

- `firmware/dist/miblo-geekmagic_ultra-<version>.bin` (the full firmware)
- `firmware/dist/miblo-loader-geekmagic_ultra-<version>.bin` (the installer)

The version comes from `firmware/include/miblo_version.h`. The git short hash is embedded as the build ID, which is shown on the boot screen and in `GET /api/info`. Flash them as in option A, or use the fleet tools below. `make help` lists every target.

### Flashing many units

`firmware/scripts/flash-fleet.py` (Python 3.9+, no dependencies) does the two-step install and version check for you, and the `Makefile` wraps it. Run these from `firmware/`. The fleet targets rebuild the images first when they're missing or out of date.

**Straight out of the box, through each unit's own access point (macOS).** Factory units aren't on any network. The Mac joins each unit's Wi-Fi in turn (one at a time, because it has one radio), flashes it at `192.168.4.1` and rejoins its own network at the end, also on errors and Ctrl-C.

```sh
make fleet-ap-plan    # scan for GIFTV/SmallTV/GeekMagic/Miblo-* units and show the plan (joins nothing)
make fleet-ap         # flash/update every unit in range, then stop
make fleet-ap-loop    # keep going as you power units on, one after another (Ctrl-C to stop)
make fleet-ap SSIDS="Miblo-Setup-4F2A GIFTV"   # only these networks, joined by name (no scan)
make fleet-ap-scan    # print what the Wi-Fi scan sees
```

- **Stock units** get the installer, then the full image. The script then joins `Miblo-Setup-XXXX` to check the version. Use `ARGS="--stock-ssid REGEX"` / `--stock-pass` if yours use other network names or a password.
- **Units already on `Miblo-Setup-XXXX`** are skipped when they're up to date and otherwise updated. A **never-configured** unit (one that never joined a Wi-Fi from its portal and was never paired) updates from its own access point without a code. Any unit that was configured once asks for the 4-digit code on its screen, even after a factory reset.
- **Location permission.** macOS shows Wi-Fi network names only to apps with Location Services access, and command-line tools can't get it. So the scan runs in a tiny helper app, **`MibloWiFiScan`**. The script builds it with Swift on first use (this needs the Command Line Tools: `xcode-select --install`). When macOS asks **"MibloWiFiScan would like to use your location", click Allow**. If you dismissed the prompt, turn it on in System Settings → Privacy & Security → Location Services → MibloWiFiScan. Without the scan, the script can still join networks by name (`SSIDS=…`, `ARGS="--try-ssid NAME"`, and `GIFTV`/`SmallTV`/`GeekMagic` are tried automatically).

**Over the LAN.** For units that are already on the bench Wi-Fi (stock units joined once through their stock portal):

```sh
make fleet-plan              # find units on this Mac's /24 and print what would happen
make fleet                   # install Miblo on every stock/installer unit found
make fleet HOSTS="192.168.0.41 192.168.0.42"   # only these units (or SUBNET=192.168.0.0/24)
make fleet-update-plan       # find configured Miblo units via mDNS and show what would be updated
make fleet-update            # update the older ones; each unit shows a 4-digit code you type in
```

`fleet-update` finds units by itself over mDNS (`_miblo._tcp.local`), so you don't need `HOSTS=` or `SUBNET=`. `HOSTS=` overrides discovery, and an explicit `SUBNET=` is searched alongside it. Units are flashed in parallel (`JOBS=`, default 50 in the Makefile; the script alone defaults to 4). Updates of configured units run one at a time because each one needs its code. The script prints a line per step and a final table, and exits non-zero if any unit failed. For every flag, run `python3 firmware/scripts/flash-fleet.py --help`.

**Units ship clean.** After a LAN install, the installer erases the bench Wi-Fi, so each unit restarts on its own `Miblo-Setup-XXXX` network, ready for its owner. On macOS the script then joins each of those networks to check the version. `ARGS=--keep-wifi` keeps the units on the bench network instead, and `ARGS=--no-ap-verify` skips the check.

### Safety notes

- **Use a proper 5 V / 1 A supply.** A weak supply (a phone charger, a laptop port or an unpowered hub) can brown out during Wi-Fi bursts. **Six quick power-ons in a row count as a hard reset**, and repeated brown-outs can look exactly like that.
- **Configured units always need the on-screen code for firmware updates**, whether from the web page, `/miblo:update` or the fleet script. A Bearer token alone isn't enough.
- Keep a copy of the official GeekMagic firmware before flashing.

## Updating

- **From Claude Code:** `/miblo:update` checks the latest GitHub release and offers to update the **plugin** first, then the **firmware** of each paired gadget that's out of date. Type the 4-digit code shown on the gadget. It takes about a minute, so don't unplug the gadget while it runs. Pairing and settings are kept. If Claude Code asks you to, run `/reload-plugins` afterwards.
- **From the browser:** open `http://miblo-xxxx.local/update` (or `http://<ip>/update`). The gadget shows a 4-digit code. Type it, choose the `.bin` and upload. The screen shows a progress bar and the gadget reboots when it's done.

The boot screen shows the firmware version and build (for example `v0.2.3 (4534fb8)`), and `GET /api/info` reports the same.

## Reset and recovery

- **From the settings page:** `http://miblo-xxxx.local` → **Factory reset**, confirmed with the code shown on the screen.
- **From Claude Code:** `/miblo:reset <id>`.
- **Without a computer (AirTag-style):** power-cycle the gadget **6 times in a row**, unplugging it within 10 seconds of each power-on. From the 3rd quick restart on, the screen shows an amber countdown ("Quick restarts left to reset: N"). To cancel, just leave it on for 10 seconds. Only real power-ons count: crashes, updates and software restarts don't.

A factory reset erases Wi-Fi, pairings and settings, and brings back the setup QR code. Ordinary power cuts erase nothing. If your router is down for 2 minutes, the gadget opens its setup network and keeps retrying the saved one, so changing routers doesn't need a reset.

### Restoring the stock firmware

Miblo's own update page (`http://miblo-xxxx.local/update`, with the on-screen code) also accepts the official GeekMagic `.bin`. Upload it there to go back to the original clock firmware.

## Troubleshooting

| Problem | What to do |
|---|---|
| `/miblo:pair` finds no gadget | Check that the gadget shows a pairing code and is on the same network as your computer. mDNS is often blocked on WSL2, VPNs and corporate networks. In that case, run `/miblo:pair <ip>` with the IP shown on the gadget's screen. |
| Screen says **Wrong password** | The password was wrong. Scan the QR again and retype it. |
| Screen says **Network not found / Use a 2.4 GHz network** | The gadget can't see the network. Miblo supports only 2.4 GHz. Enable the 2.4 GHz band or use a separate 2.4 GHz network name, and move closer to the router. |
| Screen says **Connection refused / Check password or use WPA2** | The router rejected the gadget. Check the password. If the router uses WPA3 or "WPA2/WPA3" mode, switch it to WPA2, because Miblo doesn't support WPA3. |
| Screen says **Could not connect / Error code N** | Another connection failure. Check the network and try again from the setup QR. |
| No limits on the screen ("limits unavailable") | Run `/miblo:status` to see whether the status line is linked. If it isn't, run `/miblo:link-statusline`. Limits appear after the next response. The 5-hour and weekly limits exist only on **Pro/Max** subscriptions. With an API key, the gadget shows today's cost instead. |
| Gadget shows **Disconnected** | It hasn't received anything for 30 s. The bridge starts again with the next Claude Code activity. Check `/miblo:status`. |
| Blank status line after uninstalling the plugin | Always run `/miblo:unlink-statusline` **before** uninstalling. If you already uninstalled, reinstall the plugin, run `/miblo:unlink-statusline`, then uninstall again. |

## Development

### Repository layout

```
.claude-plugin/        marketplace manifest
plugin/                Claude Code plugin (Node.js ≥ 20, zero dependencies)
  bin/                 hook.js, statusline-tap.mjs, bridge.js, miblo.js (CLI), onboard.js
  commands/            the /miblo:* slash commands
  hooks/hooks.json     hook registrations
  lib/                 session tracker, metrics, snapshot builder, device manager, mDNS, updates
  test/                node:test suites + a fake device (plugin/test/fakes)
firmware/              ESP8266 firmware (PlatformIO / Arduino)
  lib/miblo_core/      board-independent logic: snapshot, alerts, i18n, pairing, mDNS, time zones
  lib/miblo_ui/        screens, drawn on a 240 grid scaled to the board's screen
  boards/<board>/      per-board pins, screen, fonts, capabilities
  src/                 app, HTTP API, web pages, platform (Wi-Fi, OTA, storage)
  loader/              the stage-1 installer
  scripts/             build.sh, flash-fleet.py (+ tests), bump-version.py, macOS helper
  test/                native unit tests
fixtures/snapshots/    protocol fixtures shared by plugin and firmware tests
docs/                  on-device release checklist
```

### Running tests

```sh
cd plugin && npm test                                  # plugin (also regenerates fixtures/snapshots)
cd firmware && .venv/bin/pio test -e native            # firmware logic + UI on the host
python3 -m unittest firmware/scripts/test_flash_fleet.py  # fleet script (or: cd firmware && make fleet-test)
cd firmware && make test                               # firmware native tests + fleet script tests
```

The bridge-to-gadget protocol is pinned by `fixtures/snapshots/*.json`: the plugin tests generate them and the firmware tests consume them. Before a release, a person runs the on-device [firmware checklist](docs/firmware-checklist.md).

### Releasing

From a clean working tree, run:

```sh
cd firmware && make release        # or: make release VERSION=0.3.0
```

It asks for the version (patch/minor/major/keep/other; Enter means patch) and bumps both `firmware/include/miblo_version.h` and `plugin/.claude-plugin/plugin.json`. It then runs the plugin and script tests, builds both images (with the firmware native tests) and commits `chore: release vX.Y.Z`. Finally it asks whether to create and push the `vX.Y.Z` tag.

### CI

- **`.github/workflows/ci.yml`** runs on pushes to `main` and on pull requests. It runs the plugin tests on Ubuntu, macOS and Windows with Node 20 and 22, and the firmware native tests plus the device and loader builds.
- **`.github/workflows/release.yml`** runs on `v*.*.*` tags. It checks that the tag matches both version files, runs the tests, builds the images and publishes a GitHub release with the `.bin` files and `SHA256SUMS.txt`.

### Adding a board

Add `firmware/boards/<board>/` and a PlatformIO environment, then declare the screen size and capabilities. The step-by-step guide is in [`firmware/boards/README.md`](firmware/boards/README.md).

## License

[MIT](LICENSE) © 2026 Marcus Vinícius Campos.

Embedded third-party fonts and libraries keep their own licenses:

- [`firmware/lib/U8g2TFT/THIRD_PARTY_NOTICES.md`](firmware/lib/U8g2TFT/THIRD_PARTY_NOTICES.md): U8g2_for_TFT_eSPI (BSD-2-Clause) and the bitmap fonts. These include WenQuanYi Bitmap Song, which is GPLv2 with a font-embedding exception.
- [`firmware/THIRD_PARTY_NOTICES.md`](firmware/THIRD_PARTY_NOTICES.md): the time zone table from posix_tz_db (MIT).
