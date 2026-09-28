# Placas suportadas pelo firmware Miblo

| Placa | Env do PlatformIO | Chip | Tela | Entradas (`caps`) |
|---|---|---|---|---|
| GeekMagic "Ultra" | `geekmagic_ultra` | ESP8266 (ESP-12E/F, 4 MB) | ST7789 240×240 | nenhuma |

## Camadas

- `lib/miblo_core/` — lógica pura (snapshot, alertas, i18n, pareamento, mDNS…). Não muda por placa.
- `lib/miblo_ui/` — todas as telas, desenhadas via `ui::Canvas` numa grade de 240 escalada pelo
  `ScreenSpec {w, h}` da placa (`X()`, `Y()`, `Sz()` em `ui_base.cpp`).
- `boards/<placa>/` — `board.h`/`board.cpp`: nome, tamanho da tela, pinos, luz de fundo, pilhas de
  fontes por `ui::Font`, `Inputs` e `caps`.
- `src/platform/` — Wi-Fi, servidor web, mDNS, OTA e LittleFS atrás de `#if defined(ESP8266) / ESP32`
  (`platform.h`). Só o ESP8266 compila hoje; o ramo ESP32 marca o que precisará de ajuste.

## Como adicionar uma placa

1. Crie `boards/<placa>/board.h` e `board.cpp` implementando a mesma interface de
   `boards/geekmagic_ultra/board.h` (`kName`, `kScreen`, `kCapCount`/`cap()`, `begin()`, `canvas()`,
   `setBacklight()`, `readInputs()`). Se a tela usa TFT_eSPI, reutilize `src/platform/tft_canvas.h`
   e escolha fontes u8g2 maiores para telas maiores (a pilha precisa terminar em uma fonte CJK).
2. Se o chip coloca `.rodata` na RAM (ESP8266), crie um `miblo_rom_*.h` com `MIBLO_ROM`/`mibloRomByte`
   em PROGMEM; em ESP32 não é necessário (omita `-D MIBLO_ROM_IMPL`).
3. Adicione `[env:<placa>]` ao `platformio.ini` com `-I boards/<placa>`, `build_src_filter = +<*> +<../boards/<placa>/>`,
   as flags do driver de tela e o layout de flash (reserve espaço para duas imagens por causa do OTA).
4. Declare as capacidades em `kCapCount`/`cap()` (`"buttons"`, `"touch"`, `"buzzer"`, `"led"`);
   elas saem em `GET /api/info` junto com `board` e `screen {w, h}`.
5. Rode `.venv/bin/pio test -e native` (os testes de UI já desenham em 240×240, 320×240, 480×320 e
   170×320) e `scripts/build.sh <placa>` → `dist/miblo-<placa>-<versão>.bin`.
