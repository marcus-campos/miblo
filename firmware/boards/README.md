# Boards supported by the Miblo firmware

| Board | PlatformIO env | Chip | Screen | Inputs (`caps`) |
|---|---|---|---|---|
| GeekMagic "Ultra" | `geekmagic_ultra` | ESP8266 (ESP-12E/F, 4 MB) | ST7789 240×240 | none |

## Layers

- `lib/miblo_core/` — pure logic (snapshot, alerts, i18n, pairing, mDNS…). Does not change per board.
- `lib/miblo_ui/` — all screens, drawn via `ui::Canvas` on a 240 grid scaled by the board's
  `ScreenSpec {w, h}` (`X()`, `Y()`, `Sz()` in `ui_base.cpp`).
- `boards/<board>/` — `board.h`/`board.cpp`: name, screen size, pins, backlight, font
  stacks per `ui::Font`, `Inputs`, and `caps`.
- `src/platform/` — Wi-Fi, web server, mDNS, OTA, and LittleFS behind `#if defined(ESP8266) / ESP32`
  (`platform.h`). Only ESP8266 compiles today; the ESP32 branch flags what will need adjusting.

## How to add a board

1. Create `boards/<board>/board.h` and `board.cpp` implementing the same interface as
   `boards/geekmagic_ultra/board.h` (`kName`, `kScreen`, `kCapCount`/`cap()`, `begin()`, `canvas()`,
   `setBacklight()`, `readInputs()`). If the screen uses TFT_eSPI, reuse `src/platform/tft_canvas.h`
   and pick larger u8g2 fonts for larger screens (the stack must end in a CJK font).
2. If the chip puts `.rodata` in RAM (ESP8266), create a `miblo_rom_*.h` with `MIBLO_ROM`/`mibloRomByte`
   in PROGMEM; on ESP32 this isn't necessary (omit `-D MIBLO_ROM_IMPL`).
3. Add `[env:<board>]` to `platformio.ini` with `-I boards/<board>`, `build_src_filter = +<*> +<../boards/<board>/>`,
   the screen driver flags, and the flash layout (reserve space for two images because of OTA).
4. Declare the capabilities in `kCapCount`/`cap()` (`"buttons"`, `"touch"`, `"buzzer"`, `"led"`);
   they're exposed in `GET /api/info` alongside `board` and `screen {w, h}`.
5. Run `.venv/bin/pio test -e native` (the UI tests already draw at 240×240, 320×240, 480×320, and
   170×320) and `scripts/build.sh <board>` → `dist/miblo-<board>-<version>.bin`.
