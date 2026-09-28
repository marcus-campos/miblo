# Plano 2 — Firmware Miblo (ESP8266) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Firmware do Miblo para o GeekMagic "Ultra" (ESP8266 + ST7789 240×240) que entra no Wi-Fi já salvo (ou abre um portal cativo em 9 idiomas), é descoberto por mDNS, pareia com o bridge do Plano 1, recebe snapshots por HTTP e mostra a Visão geral adaptativa, os alertas (flash + herói), os modos Limites (L1) e Sessões (S1) e as telas de sistema — com OTA sempre disponível e protegido por código na tela.

**Architecture:** Três camadas. `lib/miblo_core` é C++ puro (sem headers de Arduino/ESP; só ArduinoJson): parse do snapshot, fila de alertas, herói, paginação, i18n, formatação, configuração, pareamento, políticas de rede/tela e o respondedor mDNS — tudo coberto por `pio test -e native`. `lib/miblo_ui` desenha todas as telas através de uma interface `ui::Canvas` dimensionada por `ScreenSpec {w, h}` (layouts numa grade de 240 escalada), também testada no host com um Canvas falso. A placa (`boards/geekmagic_ultra/`) fornece o Canvas real (TFT_eSPI + fontes u8g2 embutidas no binário), a luz de fundo e um stub de entradas; `src/platform/` isola Wi-Fi, servidor web, mDNS, OTA e persistência atrás de aliases `#if defined(ESP8266)/ESP32`; `src/app.cpp` orquestra.

**Tech Stack:** PlatformIO (venv local em `firmware/.venv`), `espressif8266@4.2.1` (core Arduino 3.1.2), TFT_eSPI 2.5.43, U8g2_for_TFT_eSPI (vendorizada, commit `a170ef8`), ArduinoJson 6.21.5, ricmoo/QRCode 0.0.1, Unity (testes nativos), LittleFS.

**Spec:** docs/superpowers/specs/2026-09-28-claude-gadget-design.md

## Global Constraints

- Hardware: GeekMagic "Ultra" = ESP8266 ESP-12E/F, flash 4 MB, ST7789 240×240, sem botões nem touch; pinos MOSI=13 SCLK=14 CS=15 DC=0 RST=2, luz de fundo GPIO5 **ativa em nível baixo**; `TFT_RGB_ORDER=TFT_BGR`, SPI 40 MHz, rotação 0, `board = esp12e`, `flash_mode = dio`, CPU 80 MHz (flags copiadas do firmware de referência comprovado).
- Layout de flash: `eagle.flash.4m1m.ld` (o mesmo padrão do `esp12e` usado pelo firmware de referência): sketch ≤ 1 044 464 B (limite do segmento `irom0`), ~2,9 MB entre o fim do sketch e o LittleFS para receber a imagem OTA, LittleFS de 1 MB só para JSONs pequenos. O binário atual fica em ~700 KB; um sketch de ~1 MB ainda cabe duas vezes (atual + OTA).
- Fontes **dentro do binário** (seção `.irom.text`, lidas com `pgm_read_byte`), nunca no LittleFS: o usuário envia um único `.bin`. Glyph ausente → retângulo, nunca trava.
- `lib/miblo_core` e `lib/miblo_ui` não incluem headers de Arduino/ESP/TFT (ArduinoJson e QRCode são C/C++ portáveis). Dados grandes do núcleo usam `MIBLO_ROM` + `mibloRomByte()`; a placa injeta a versão PROGMEM com `-D MIBLO_ROM_IMPL=\"miblo_rom_esp8266.h\"`.
- Layouts calculam posições com `X()/Y()/Sz()` (grade de 240 escalada pelo `ScreenSpec`); nenhuma coordenada absoluta fora dessa escala. Hoje só 240×240 é obrigatório; os testes também desenham em 320×240, 480×320 e 170×320 sem sair da tela.
- Sem framebuffer (RAM livre ~30 KB após o Wi-Fi): cada tela redesenha só as regiões cujo hash mudou (`RegionCache`).
- Um env do PlatformIO por placa: `[env:geekmagic_ultra]` (+ `[env:native]` para testes). Só o ESP8266 precisa compilar; o ramo `ESP32` de `src/platform/platform.h` é um marcador para placas futuras.
- **Toda build tem `/update`**: o `main.cpp` "seguro" da Task 1 e o demo da Task 11 usam o `ESP8266HTTPUpdateServer` do firmware de referência; a partir da Task 13 o `/update` próprio exige o **código de 4 dígitos mostrado na tela** — inclusive quando vem com token Bearer (o token trafega em texto puro na rede local). O bridge faz `GET /update` (a tela passa a mostrar o código) e envia `POST /update?code=XXXX`.
- API HTTP (contrato do Plano 1, Task 5 + revisão final): `GET /api/info → {id, name, fw, proto, paired, board, screen:{w,h}, caps:[], flash}`; `POST /api/pair {code, host} → 200 {token}` | `403` código errado | `429 {retryAfter}` durante 60 s após 5 códigos errados; `POST /api/state`, `POST /api/config {mode?, …}`, `POST /api/reset` exigem `Authorization: Bearer <token>` (401 sem token válido); até 4 tokens (o mais antigo sai).
- Snapshot (spec §5.3, `v:1`): corpo ≤ 3072 B; campos desconhecidos ignorados; `today` agora é só `{usd}` (sem `today.tok`); `tok` por sessão = tokens de contexto, exibido como veio. JSON inválido/grande → 400 e a tela anterior continua. 30 s sem snapshot → tela "Desconectado" com relógio.
- mDNS (contrato do Plano 1, Task 7): serviço `_miblo._tcp.local`, instância `<nome>._miblo._tcp.local`, host `miblo-xxxx.local`, TXT `id=`, `name=`, `fw=`; consulta com porta de origem ≠ 5353 ou bit QU → resposta unicast para a origem.
- Idiomas: `en` (padrão), `pt-BR`, `pt-PT`, `es`, `fr`, `it`, `de`, `ru`, `zh`; `Accept-Language` com `pt` sem região → `pt-BR`, `pt-XX` → `pt-PT`, `zh-*` → `zh`. Toda string de tela/página vem das tabelas de `miblo_strings.cpp` (exceções: "Miblo", o comando `/plugin install miblo@miblo`, números).
- Identificadores e comentários de código em inglês ou português (como no Plano 1); textos para o usuário só via tabelas de i18n.
- Todos os comandos rodam a partir de `firmware/` com o PlatformIO do venv: `cd firmware && .venv/bin/pio ...`.

## File Structure

```
.gitignore                                   + firmware/.venv/ e firmware/dist/
fixtures/snapshots/*.json                    contrato (gerado pelo Plano 1, Task 4) — só leitura aqui
firmware/
  platformio.ini                             envs geekmagic_ultra + native
  include/miblo_version.h                    MIBLO_FW_VERSION, MIBLO_PROTO
  scripts/build.sh                           dist/miblo-<placa>-<versão>.bin
  scripts/vendor_u8g2.py                     vendoriza U8g2_for_TFT_eSPI + fontes em PROGMEM
  boards/README.md                           como adicionar uma placa
  boards/geekmagic_ultra/board.h|.cpp        pinos, tela, luz de fundo, fontes, Inputs (stub)
  boards/geekmagic_ultra/miblo_rom_esp8266.h MIBLO_ROM = PROGMEM
  lib/miblo_core/src/                        lógica pura (testada no host)
    miblo_rom.h                              acesso a dados "ROM" injetável
    miblo_utf8.*  miblo_format.*             UTF-8, tempos, tokens, dólares
    miblo_snapshot.*                         parse do snapshot (ArduinoJson, filtro)
    miblo_i18n.*  miblo_strings.cpp          9 idiomas, negociação, tabelas
    miblo_activity.*                         ferramenta → verbo localizado, linha de estado
    miblo_overview.*                         Visão geral, herói, RunTracker, Pager, RegionCache
    miblo_alerts.*                           AlertSequencer (fila, dedupe, flash/herói, lembrete)
    miblo_security.*                         PairingGuard, TokenStore, PresenceGate, Bearer
    miblo_config.*                           Config + patch JSON + contador de liga/desliga
    miblo_policy.*                           NetPolicy (quando abrir o AP) + selectScreen
    miblo_mdns.*                             respondedor DNS-SD
  lib/miblo_ui/src/                          layouts via ui::Canvas (testados no host)
    ui_canvas.h  ui_screens.h  ui_base.cpp  ui_system.cpp  ui_main.cpp
  lib/U8g2TFT/src/                           vendorizada (gerada pelo script) + miblo_fonts.c
  src/
    main.cpp                                 setup()/loop() → app
    app.h|.cpp                               orquestra tudo
    context.h|.cpp                           estado compartilhado (Config, tokens, snapshot, flags)
    web.h|.cpp                               portal cativo + página de configuração (localizados)
    api.h|.cpp                               /api/*
    platform/platform.h                      aliases ESP8266/ESP32 (WebServerT, hwRandom…)
    platform/tft_canvas.h|.cpp               ui::Canvas sobre TFT_eSPI + u8g2
    platform/storage.h|.cpp                  LittleFS: config, pareamentos, contador de boot
    platform/net.h|.cpp                      Wi-Fi do SDK, AP de setup, DNS cativo, NTP
    platform/mdns_service.h|.cpp             UDP 5353 ↔ miblo_mdns
    platform/ota.h|.cpp                      /update com código de presença
  test/support/fake_canvas.h                 Canvas falso para os testes de UI
  test/test_*/test_main.cpp                  Unity (env native)
```

---

### Task 1: Scaffold do PlatformIO, firmware "seguro" com `/update` e utilitários de texto

**Files:**
- Modify: `.gitignore`
- Create: `firmware/platformio.ini`
- Create: `firmware/include/miblo_version.h`
- Create: `firmware/boards/geekmagic_ultra/miblo_rom_esp8266.h`
- Create: `firmware/src/main.cpp`
- Create: `firmware/lib/miblo_core/src/miblo_utf8.h`, `firmware/lib/miblo_core/src/miblo_utf8.cpp`
- Create: `firmware/lib/miblo_core/src/miblo_format.h`, `firmware/lib/miblo_core/src/miblo_format.cpp`
- Test: `firmware/test/test_text/test_main.cpp`

**Interfaces:**
- Produces: `miblo::utf8Next(const char*& p) → uint32_t`, `miblo::utf8Length(const char*) → size_t`, `miblo::utf8Copy(char* dst, size_t cap, const char* src, size_t maxChars = (size_t)-1) → size_t`; `miblo::formatElapsed/formatAgo/formatCountdown(uint32_t secs, char*, size_t)`, `formatTokens(uint64_t, char*, size_t)`, `formatHHMM(int h, int m, char*, size_t)`, `formatUsd(float, char*, size_t)`; macros `MIBLO_FW_VERSION`, `MIBLO_PROTO`; envs `geekmagic_ultra` e `native`.

- [ ] **Step 1: Instalar o PlatformIO no venv local e ignorar artefatos**

```bash
python3 -m venv firmware/.venv
firmware/.venv/bin/pip install platformio
firmware/.venv/bin/pio --version
printf 'firmware/.venv/\nfirmware/dist/\n' >> .gitignore
```

Expected: `PlatformIO Core, version 6.x`; `.gitignore` termina com as duas linhas novas (`.pio/` já estava lá).

- [ ] **Step 2: `firmware/platformio.ini`** (versão final — as tasks seguintes não mudam este arquivo)

```ini
; Miblo — firmware do gadget. Um env por placa; hoje só a GeekMagic "Ultra" (ESP8266 + ST7789 240x240).
; Para adicionar uma placa: boards/README.md.

[platformio]
default_envs = geekmagic_ultra

[env]
lib_deps =
	bblanchon/ArduinoJson@6.21.5
	ricmoo/QRCode@0.0.1

[env:geekmagic_ultra]
platform = espressif8266@4.2.1
board = esp12e
framework = arduino
monitor_speed = 115200
board_build.flash_mode = dio
board_build.f_cpu = 80000000L
; 4 MB: ~1 MB de sketch, ~2 MB livres para a imagem OTA, 1 MB de LittleFS (config/tokens).
board_build.ldscript = eagle.flash.4m1m.ld
board_build.filesystem = littlefs
lib_deps =
	${env.lib_deps}
	bodmer/TFT_eSPI@2.5.43
build_src_filter = +<*> +<../boards/geekmagic_ultra/>
build_flags =
	-I boards/geekmagic_ultra
	-D MIBLO_BOARD_GEEKMAGIC_ULTRA=1
	-D MIBLO_ROM_IMPL=\"miblo_rom_esp8266.h\"
	-D USER_SETUP_LOADED=1
	-D ST7789_DRIVER=1
	-D TFT_WIDTH=240
	-D TFT_HEIGHT=240
	-D TFT_MOSI=13
	-D TFT_SCLK=14
	-D TFT_CS=15
	-D TFT_DC=0
	-D TFT_RST=2
	-D TFT_BL=5
	-D TFT_BACKLIGHT_ON=LOW
	-D SPI_FREQUENCY=40000000
	-D TFT_RGB_ORDER=TFT_BGR
	-D LOAD_GLCD=1

[env:native]
platform = native
test_framework = unity
; QRCode declara-se biblioteca Arduino, mas é C puro: liberar no host.
lib_compat_mode = off
build_flags =
	-std=gnu++17
	-Wall
lib_ignore =
	U8g2TFT
```

- [ ] **Step 3: `firmware/include/miblo_version.h`**

```cpp
#pragma once

// Versão do firmware. scripts/build.sh lê esta linha para nomear dist/miblo-<versão>.bin.
#define MIBLO_FW_VERSION "0.1.0"
#define MIBLO_PROTO 1
```

- [ ] **Step 4: `firmware/boards/geekmagic_ultra/miblo_rom_esp8266.h`** (usado pelo núcleo a partir da Task 3)

```cpp
#pragma once
// ESP8266: .rodata vai para a RAM; as tabelas de strings precisam ficar na flash (PROGMEM) e
// ser lidas byte a byte com pgm_read_byte. Injetado no núcleo por -D MIBLO_ROM_IMPL.
#include <pgmspace.h>
#define MIBLO_ROM PROGMEM
inline uint8_t mibloRomByte(const char* p) { return pgm_read_byte(p); }
```

- [ ] **Step 5: `firmware/src/main.cpp` — firmware mínimo "seguro"**

Igual ao firmware de referência: tela, Wi-Fi salvo no SDK e `/update` sem senha (ESP8266HTTPUpdateServer). Garante que qualquer build desta fase possa ser regravada pelo navegador.

```cpp
// Firmware mínimo "seguro" (Task 1): tela + Wi-Fi salvo no SDK + /update sem senha, igual ao
// firmware de referência. Toda build do Miblo mantém uma página /update; este main é
// substituído na Task 11 (demo da tela) e na Task 13 (aplicativo completo).
#include <Arduino.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <TFT_eSPI.h>

#include "miblo_version.h"

TFT_eSPI tft;
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer updater;

void setup() {
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  tft.drawString("Miblo " MIBLO_FW_VERSION, 120, 100, 1);
  tft.setTextSize(1);

  WiFi.mode(WIFI_STA);
  WiFi.begin();  // credenciais salvas no SDK pelo firmware anterior
  for (int i = 0; i < 30 && WiFi.status() != WL_CONNECTED; i++) delay(500);
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("Miblo-Recovery");
  }
  updater.setup(&server, "/update");
  server.begin();
  String ip = (WiFi.getMode() & WIFI_AP) ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  tft.drawString("http://" + ip + "/update", 120, 130, 1);
}

void loop() { server.handleClient(); }
```

- [ ] **Step 6: Write the failing test** — `firmware/test/test_text/test_main.cpp`

```cpp
#include <unity.h>

#include "miblo_format.h"
#include "miblo_utf8.h"

using namespace miblo;

void setUp() {}
void tearDown() {}

static void test_utf8_next_decodes_all_widths() {
  const char* p = "aé€😀";
  TEST_ASSERT_EQUAL_UINT32('a', utf8Next(p));
  TEST_ASSERT_EQUAL_UINT32(0xE9, utf8Next(p));
  TEST_ASSERT_EQUAL_UINT32(0x20AC, utf8Next(p));
  TEST_ASSERT_EQUAL_UINT32(0x1F600, utf8Next(p));
  TEST_ASSERT_EQUAL_UINT32(0, utf8Next(p));
}

static void test_utf8_next_invalid_bytes_become_replacement() {
  const char bad[] = {(char)0xFF, 'a', (char)0xC3, 0};
  const char* p = bad;
  TEST_ASSERT_EQUAL_UINT32(0xFFFD, utf8Next(p));
  TEST_ASSERT_EQUAL_UINT32('a', utf8Next(p));
  TEST_ASSERT_EQUAL_UINT32(0xFFFD, utf8Next(p));
  TEST_ASSERT_EQUAL_UINT32(0, utf8Next(p));
}

static void test_utf8_length_counts_code_points() {
  TEST_ASSERT_EQUAL(5, utf8Length("héllo"));
  TEST_ASSERT_EQUAL(2, utf8Length("项目"));
  TEST_ASSERT_EQUAL(0, utf8Length(""));
}

static void test_utf8_copy_never_splits_sequences() {
  char buf[6];
  utf8Copy(buf, sizeof(buf), "项目项目");  // 3 bytes cada; só 1 cabe em 5 bytes + NUL
  TEST_ASSERT_EQUAL_STRING("项", buf);
  utf8Copy(buf, sizeof(buf), "abcdefgh", 3);
  TEST_ASSERT_EQUAL_STRING("abc", buf);
  utf8Copy(buf, sizeof(buf), nullptr);
  TEST_ASSERT_EQUAL_STRING("", buf);
}

static void test_format_elapsed() {
  char b[16];
  formatElapsed(42, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("0:42", b);
  formatElapsed(192, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("3:12", b);
  formatElapsed(3725, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("1:02:05", b);
}

static void test_format_ago() {
  char b[16];
  formatAgo(45, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("45s", b);
  formatAgo(150, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("2m", b);
  formatAgo(7300, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("2h", b);
  formatAgo(180000, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("2d", b);
}

static void test_format_countdown() {
  char b[16];
  formatCountdown(30, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("<1min", b);
  formatCountdown(2700, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("45min", b);
  formatCountdown(7800, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("2h10", b);
  formatCountdown(240000, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("2d18h", b);
}

static void test_format_tokens() {
  char b[16];
  const struct {
    uint64_t in;
    const char* out;
  } cases[] = {{0, "0"},          {950, "950"},       {1000, "1k"},        {1500, "1.5k"},
               {12300, "12.3k"},  {98000, "98k"},     {412000, "412k"},    {999999, "999k"},
               {1510000, "1.5M"}, {2000000, "2M"},    {123456789, "123M"}, {1000000000ULL, "1000M"}};
  for (const auto& c : cases) {
    formatTokens(c.in, b, sizeof(b));
    TEST_ASSERT_EQUAL_STRING(c.out, b);
  }
}

static void test_format_clock_and_usd() {
  char b[16];
  formatHHMM(9, 5, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("09:05", b);
  formatUsd(4.8f, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("$4.80", b);
  formatUsd(0.004f, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("$0.00", b);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_utf8_next_decodes_all_widths);
  RUN_TEST(test_utf8_next_invalid_bytes_become_replacement);
  RUN_TEST(test_utf8_length_counts_code_points);
  RUN_TEST(test_utf8_copy_never_splits_sequences);
  RUN_TEST(test_format_elapsed);
  RUN_TEST(test_format_ago);
  RUN_TEST(test_format_countdown);
  RUN_TEST(test_format_tokens);
  RUN_TEST(test_format_clock_and_usd);
  return UNITY_END();
}
```

- [ ] **Step 7: Run test to verify it fails**

Run: `cd firmware && .venv/bin/pio test -e native -f test_text`
Expected: FAIL — erro de compilação `miblo_format.h: No such file or directory` (ou `'miblo_format.h' file not found`).

- [ ] **Step 8: Implement** — `firmware/lib/miblo_core/src/miblo_utf8.h`

```cpp
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace miblo {

// Decodifica o próximo code point de `p` e avança o ponteiro.
// Byte inválido ou sequência truncada → U+FFFD, avançando 1 byte. Fim da string → 0 (não avança).
uint32_t utf8Next(const char*& p);

// Quantidade de code points.
size_t utf8Length(const char* s);

// Copia no máximo `maxChars` code points de `src` para `dst` (capacidade `cap` bytes, sempre
// terminada em NUL), sem nunca cortar uma sequência UTF-8 no meio. Retorna os bytes escritos.
size_t utf8Copy(char* dst, size_t cap, const char* src, size_t maxChars = (size_t)-1);

}  // namespace miblo
```

`firmware/lib/miblo_core/src/miblo_utf8.cpp`:

```cpp
#include "miblo_utf8.h"

namespace miblo {

static int seqLen(uint8_t b) {
  if (b < 0x80) return 1;
  if ((b & 0xE0) == 0xC0) return 2;
  if ((b & 0xF0) == 0xE0) return 3;
  if ((b & 0xF8) == 0xF0) return 4;
  return 0;
}

uint32_t utf8Next(const char*& p) {
  const uint8_t* s = reinterpret_cast<const uint8_t*>(p);
  if (s[0] == 0) return 0;
  int n = seqLen(s[0]);
  if (n == 0) {
    p += 1;
    return 0xFFFD;
  }
  if (n == 1) {
    p += 1;
    return s[0];
  }
  uint32_t cp = s[0] & (0xFF >> (n + 1));
  for (int i = 1; i < n; i++) {
    if ((s[i] & 0xC0) != 0x80) {
      p += 1;
      return 0xFFFD;
    }
    cp = (cp << 6) | (s[i] & 0x3F);
  }
  p += n;
  return cp;
}

size_t utf8Length(const char* s) {
  size_t n = 0;
  const char* p = s;
  while (*p) {
    utf8Next(p);
    n++;
  }
  return n;
}

size_t utf8Copy(char* dst, size_t cap, const char* src, size_t maxChars) {
  if (cap == 0) return 0;
  size_t used = 0;
  size_t chars = 0;
  const char* p = src ? src : "";
  while (*p && chars < maxChars) {
    const char* start = p;
    utf8Next(p);
    size_t len = (size_t)(p - start);
    if (used + len + 1 > cap) break;
    for (size_t i = 0; i < len; i++) dst[used + i] = start[i];
    used += len;
    chars++;
  }
  dst[used] = 0;
  return used;
}

}  // namespace miblo
```

`firmware/lib/miblo_core/src/miblo_format.h`:

```cpp
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace miblo {

// Tempo decorrido: 42 → "0:42", 192 → "3:12", 3725 → "1:02:05".
void formatElapsed(uint32_t secs, char* out, size_t cap);

// Tempo curto (lista/"há X"): 45 → "45s", 150 → "2m", 7300 → "2h", 180000 → "2d".
void formatAgo(uint32_t secs, char* out, size_t cap);

// Contagem regressiva: 30 → "<1min", 2700 → "45min", 7800 → "2h10", 240000 → "2d18h".
void formatCountdown(uint32_t secs, char* out, size_t cap);

// Tokens (sempre arredondado para baixo): 950 → "950", 1500 → "1.5k", 12300 → "12.3k",
// 98000 → "98k", 412000 → "412k", 1510000 → "1.5M", 2000000 → "2M", 123456789 → "123M".
void formatTokens(uint64_t tok, char* out, size_t cap);

// "HH:MM" com zeros à esquerda.
void formatHHMM(int hour, int minute, char* out, size_t cap);

// Dólares com 2 casas: 4.8 → "$4.80".
void formatUsd(float usd, char* out, size_t cap);

}  // namespace miblo
```

`firmware/lib/miblo_core/src/miblo_format.cpp`:

```cpp
#include "miblo_format.h"

#include <stdio.h>

namespace miblo {

void formatElapsed(uint32_t secs, char* out, size_t cap) {
  uint32_t h = secs / 3600;
  uint32_t m = (secs / 60) % 60;
  uint32_t s = secs % 60;
  if (h > 0) {
    snprintf(out, cap, "%u:%02u:%02u", (unsigned)h, (unsigned)m, (unsigned)s);
  } else {
    snprintf(out, cap, "%u:%02u", (unsigned)m, (unsigned)s);
  }
}

void formatAgo(uint32_t secs, char* out, size_t cap) {
  if (secs < 60) {
    snprintf(out, cap, "%us", (unsigned)secs);
  } else if (secs < 3600) {
    snprintf(out, cap, "%um", (unsigned)(secs / 60));
  } else if (secs < 86400) {
    snprintf(out, cap, "%uh", (unsigned)(secs / 3600));
  } else {
    snprintf(out, cap, "%ud", (unsigned)(secs / 86400));
  }
}

void formatCountdown(uint32_t secs, char* out, size_t cap) {
  if (secs < 60) {
    snprintf(out, cap, "<1min");
  } else if (secs < 3600) {
    snprintf(out, cap, "%umin", (unsigned)(secs / 60));
  } else if (secs < 86400) {
    snprintf(out, cap, "%uh%02u", (unsigned)(secs / 3600), (unsigned)((secs / 60) % 60));
  } else {
    snprintf(out, cap, "%ud%uh", (unsigned)(secs / 86400), (unsigned)((secs / 3600) % 24));
  }
}

static void withTenths(uint64_t tenths, char suffix, char* out, size_t cap) {
  unsigned whole = (unsigned)(tenths / 10);
  unsigned frac = (unsigned)(tenths % 10);
  if (whole >= 100 || frac == 0) {
    snprintf(out, cap, "%u%c", whole, suffix);
  } else {
    snprintf(out, cap, "%u.%u%c", whole, frac, suffix);
  }
}

void formatTokens(uint64_t tok, char* out, size_t cap) {
  if (tok < 1000) {
    snprintf(out, cap, "%u", (unsigned)tok);
  } else if (tok < 1000000) {
    withTenths(tok / 100, 'k', out, cap);
  } else {
    withTenths(tok / 100000, 'M', out, cap);
  }
}

void formatHHMM(int hour, int minute, char* out, size_t cap) {
  snprintf(out, cap, "%02d:%02d", hour, minute);
}

void formatUsd(float usd, char* out, size_t cap) {
  if (usd < 0) usd = 0;
  unsigned cents = (unsigned)(usd * 100.0f + 0.5f);
  snprintf(out, cap, "$%u.%02u", cents / 100, cents % 100);
}

}  // namespace miblo
```

- [ ] **Step 9: Run tests and the device build**

Run: `cd firmware && .venv/bin/pio test -e native -f test_text && .venv/bin/pio run -e geekmagic_ultra`
Expected: `9 test cases: 9 succeeded`; depois `[SUCCESS]` com `RAM: ... (used ~29 KB ...)` e `Flash: ... (used ~320 KB ...)`. A primeira execução baixa o toolchain (alguns minutos).

- [ ] **Step 10: Commit**

```bash
git add .gitignore firmware/platformio.ini firmware/include/miblo_version.h firmware/boards/geekmagic_ultra/miblo_rom_esp8266.h firmware/src/main.cpp firmware/lib/miblo_core/src/miblo_utf8.h firmware/lib/miblo_core/src/miblo_utf8.cpp firmware/lib/miblo_core/src/miblo_format.h firmware/lib/miblo_core/src/miblo_format.cpp firmware/test/test_text/test_main.cpp
git commit -m "feat(firmware): PlatformIO scaffold, safe OTA main and text utilities"
```

---

### Task 2: Parse do snapshot (contrato com o bridge)

**Pré-requisito:** `fixtures/snapshots/{attention,working,idle,overflow}.json` existem (Plano 1, Task 4, já com `today: {usd}`). Se faltarem: `cd plugin && UPDATE_FIXTURES=1 node --test test/contract.test.js`. Os testes daqui não fixam valores de tokens nem horários absolutos (dependem do fuso de quem gerou), só relações.

**Files:**
- Create: `firmware/lib/miblo_core/src/miblo_snapshot.h`, `firmware/lib/miblo_core/src/miblo_snapshot.cpp`
- Test: `firmware/test/test_snapshot/test_main.cpp`

**Interfaces:**
- Consumes: `utf8Copy` (Task 1); ArduinoJson 6.21.5.
- Produces: `miblo::kSnapshotMaxBytes = 3072`, `kMaxSessions = 8`, `kMaxAlerts = 8`; `enum class SessionState {Idle, Running, Perm, Question, Done}`; `enum class AlertKind {Perm, Question, Done}`; `struct SessionRow {id[9], name[84], st, tool[132], det[132], since, model[52], ctx(-1=null), tok(-1=null)}`; `struct UsageWindow {present, pct, reset}`; `struct AlertItem {id, kind, sid[9]}`; `struct Snapshot {seq, now, host[84], hasUsage, h5, d7, todayUsd, count, sessions[8], more, alertCount, alerts[8]}`; `enum class ParseResult {Ok, TooLarge, BadJson, BadVersion}`; `parseSnapshot(char* json, size_t len, Snapshot& out) → ParseResult` (em erro `out` não muda); `parseSessionState(const char*, SessionState&)`, `parseAlertKind(const char*, AlertKind&)`, `findSession(const Snapshot&, const char* id) → int`.

- [ ] **Step 1: Write the failing test** — `firmware/test/test_snapshot/test_main.cpp`

```cpp
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

#include <string>
#include <vector>

#include "miblo_snapshot.h"

using namespace miblo;

void setUp() {}
void tearDown() {}

// O `pio test` roda o programa com cwd = firmware/. Os fixtures são gerados pelos testes do
// bridge (Plano 1, Task 4) em <repo>/fixtures/snapshots.
static std::string loadFixture(const char* name) {
  const char* dirs[] = {"../fixtures/snapshots/", "fixtures/snapshots/", "../../fixtures/snapshots/"};
  for (const char* d : dirs) {
    std::string path = std::string(d) + name;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) continue;
    std::string data;
    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) data.append(buf, n);
    fclose(f);
    return data;
  }
  TEST_FAIL_MESSAGE("fixture não encontrado — rode os testes de contrato do bridge (Plano 1, Task 4)");
  return "";
}

static Snapshot snap;

static ParseResult parseText(std::string text, Snapshot& out) {
  std::vector<char> buf(text.begin(), text.end());
  buf.push_back(0);
  return parseSnapshot(buf.data(), text.size(), out);
}

static void test_attention_fixture() {
  TEST_ASSERT_EQUAL(ParseResult::Ok, parseText(loadFixture("attention.json"), snap));
  TEST_ASSERT_EQUAL_UINT32(42, snap.seq);
  TEST_ASSERT_EQUAL_STRING("MacBook-Marcus", snap.host);
  TEST_ASSERT_TRUE(snap.hasUsage);
  TEST_ASSERT_TRUE(snap.h5.present);
  TEST_ASSERT_EQUAL_UINT8(62, snap.h5.pct);
  TEST_ASSERT_EQUAL_UINT32(7800, snap.h5.reset - snap.now);
  TEST_ASSERT_EQUAL_UINT8(38, snap.d7.pct);
  TEST_ASSERT_EQUAL_UINT32(240000, snap.d7.reset - snap.now);
  TEST_ASSERT_EQUAL_UINT8(4, snap.count);
  TEST_ASSERT_EQUAL_UINT16(0, snap.more);
  TEST_ASSERT_TRUE(snap.todayUsd > 0.0f);

  const SessionRow& a = snap.sessions[0];
  TEST_ASSERT_EQUAL_STRING("11111111", a.id);
  TEST_ASSERT_EQUAL_STRING("api-server", a.name);
  TEST_ASSERT_EQUAL(SessionState::Perm, a.st);
  TEST_ASSERT_EQUAL_STRING("Bash", a.tool);
  TEST_ASSERT_EQUAL_STRING("npm run migrate", a.det);
  TEST_ASSERT_EQUAL_STRING("Opus", a.model);
  TEST_ASSERT_EQUAL_INT16(71, a.ctx);
  TEST_ASSERT_TRUE(a.tok > 0);  // tokens de contexto da sessão (valor exato é do bridge)
  TEST_ASSERT_TRUE(a.since <= snap.now);

  TEST_ASSERT_EQUAL(SessionState::Question, snap.sessions[1].st);
  TEST_ASSERT_EQUAL_STRING("infra", snap.sessions[1].name);
  TEST_ASSERT_EQUAL_INT16(-1, snap.sessions[1].ctx);
  TEST_ASSERT_EQUAL_INT64(-1, snap.sessions[1].tok);
  TEST_ASSERT_EQUAL(SessionState::Running, snap.sessions[2].st);
  TEST_ASSERT_EQUAL_STRING("Header.tsx", snap.sessions[2].det);
  TEST_ASSERT_EQUAL(SessionState::Idle, snap.sessions[3].st);

  TEST_ASSERT_EQUAL_UINT8(2, snap.alertCount);
  TEST_ASSERT_EQUAL_UINT32(1, snap.alerts[0].id);
  TEST_ASSERT_EQUAL(AlertKind::Perm, snap.alerts[0].kind);
  TEST_ASSERT_EQUAL_STRING("11111111", snap.alerts[0].sid);
  TEST_ASSERT_EQUAL(AlertKind::Question, snap.alerts[1].kind);
  TEST_ASSERT_EQUAL_INT(1, findSession(snap, "22222222"));
  TEST_ASSERT_EQUAL_INT(-1, findSession(snap, "nope"));
}

static void test_working_fixture() {
  TEST_ASSERT_EQUAL(ParseResult::Ok, parseText(loadFixture("working.json"), snap));
  TEST_ASSERT_EQUAL_UINT8(2, snap.count);
  TEST_ASSERT_EQUAL(SessionState::Running, snap.sessions[0].st);
  TEST_ASSERT_EQUAL(SessionState::Running, snap.sessions[1].st);
  TEST_ASSERT_EQUAL_STRING("npm test", snap.sessions[1].det);
  TEST_ASSERT_EQUAL_UINT8(0, snap.alertCount);
}

static void test_idle_fixture() {
  TEST_ASSERT_EQUAL(ParseResult::Ok, parseText(loadFixture("idle.json"), snap));
  TEST_ASSERT_EQUAL_UINT8(1, snap.count);
  TEST_ASSERT_EQUAL(SessionState::Done, snap.sessions[0].st);
  TEST_ASSERT_EQUAL_STRING("docs", snap.sessions[0].name);
  TEST_ASSERT_EQUAL_UINT8(1, snap.alertCount);
  TEST_ASSERT_EQUAL(AlertKind::Done, snap.alerts[0].kind);
}

static void test_overflow_fixture() {
  TEST_ASSERT_EQUAL(ParseResult::Ok, parseText(loadFixture("overflow.json"), snap));
  TEST_ASSERT_FALSE(snap.hasUsage);
  TEST_ASSERT_EQUAL_UINT8(8, snap.count);
  TEST_ASSERT_EQUAL_UINT16(2, snap.more);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, snap.todayUsd);
}

static void test_unknown_fields_are_ignored_and_extra_sessions_go_to_more() {
  std::string json = "{\"v\":1,\"seq\":1,\"now\":100,\"future\":{\"x\":[1,2,3]},\"usage\":null,\"today\":{\"tok\":5,\"usd\":1.25},\"sessions\":[";
  for (int i = 0; i < 10; i++) {
    if (i) json += ",";
    json += "{\"id\":\"s" + std::to_string(i) + "\",\"name\":\"p\",\"st\":\"running\",\"newField\":true}";
  }
  json += "],\"more\":1,\"alerts\":[{\"id\":5,\"kind\":\"alien\",\"sid\":\"s0\"},{\"id\":6,\"kind\":\"done\",\"sid\":\"s1\"}]}";
  TEST_ASSERT_EQUAL(ParseResult::Ok, parseText(json, snap));
  TEST_ASSERT_EQUAL_UINT8(8, snap.count);
  TEST_ASSERT_EQUAL_UINT16(3, snap.more);
  TEST_ASSERT_FALSE(snap.hasUsage);
  TEST_ASSERT_EQUAL_FLOAT(1.25f, snap.todayUsd);  // campo antigo today.tok é ignorado
  TEST_ASSERT_EQUAL_UINT8(1, snap.alertCount);
  TEST_ASSERT_EQUAL_UINT32(6, snap.alerts[0].id);
}

static void test_truncates_long_strings_by_characters() {
  std::string json = "{\"v\":1,\"sessions\":[{\"id\":\"abcdefghijkl\",\"name\":\"";
  for (int i = 0; i < 30; i++) json += "项";
  json += "\",\"st\":\"perm\"}]}";
  TEST_ASSERT_EQUAL(ParseResult::Ok, parseText(json, snap));
  TEST_ASSERT_EQUAL_STRING("abcdefgh", snap.sessions[0].id);
  TEST_ASSERT_EQUAL(60u, strlen(snap.sessions[0].name));  // 20 caracteres × 3 bytes
}

static void test_errors_leave_previous_snapshot_untouched() {
  TEST_ASSERT_EQUAL(ParseResult::Ok, parseText("{\"v\":1,\"seq\":7,\"host\":\"keep\"}", snap));
  TEST_ASSERT_EQUAL(ParseResult::BadJson, parseText("{\"v\":1,\"seq\":8", snap));
  TEST_ASSERT_EQUAL(ParseResult::BadJson, parseText("[1,2]", snap));
  TEST_ASSERT_EQUAL(ParseResult::BadVersion, parseText("{\"seq\":9}", snap));
  std::string big = "{\"v\":1,\"host\":\"" + std::string(3100, 'x') + "\"}";
  TEST_ASSERT_EQUAL(ParseResult::TooLarge, parseText(big, snap));
  TEST_ASSERT_EQUAL_UINT32(7, snap.seq);
  TEST_ASSERT_EQUAL_STRING("keep", snap.host);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_attention_fixture);
  RUN_TEST(test_working_fixture);
  RUN_TEST(test_idle_fixture);
  RUN_TEST(test_overflow_fixture);
  RUN_TEST(test_unknown_fields_are_ignored_and_extra_sessions_go_to_more);
  RUN_TEST(test_truncates_long_strings_by_characters);
  RUN_TEST(test_errors_leave_previous_snapshot_untouched);
  return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd firmware && .venv/bin/pio test -e native -f test_snapshot`
Expected: FAIL — `miblo_snapshot.h: No such file or directory`.

- [ ] **Step 3: Implement** — `firmware/lib/miblo_core/src/miblo_snapshot.h`

```cpp
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace miblo {

constexpr size_t kSnapshotMaxBytes = 3072;
constexpr uint8_t kMaxSessions = 8;
constexpr uint8_t kMaxAlerts = 8;

enum class SessionState : uint8_t { Idle, Running, Perm, Question, Done };
enum class AlertKind : uint8_t { Perm, Question, Done };

// Tamanhos: limite do protocolo em caracteres × 4 bytes (pior caso UTF-8) + NUL.
struct SessionRow {
  char id[9];
  char name[84];     // ≤ 20 caracteres
  SessionState st;
  char tool[132];    // ≤ 32 caracteres
  char det[132];     // ≤ 32 caracteres
  uint32_t since;    // epoch em segundos
  char model[52];    // ≤ 12 caracteres
  int16_t ctx;       // -1 = null
  int64_t tok;       // -1 = null
};

struct UsageWindow {
  bool present;
  uint8_t pct;
  uint32_t reset;  // epoch em segundos
};

struct AlertItem {
  uint32_t id;
  AlertKind kind;
  char sid[9];
};

struct Snapshot {
  uint32_t seq;
  uint32_t now;
  char host[84];
  bool hasUsage;
  UsageWindow h5;
  UsageWindow d7;
  float todayUsd;  // custo do dia (today.usd); o protocolo não tem mais today.tok
  uint8_t count;
  SessionRow sessions[kMaxSessions];
  uint16_t more;
  uint8_t alertCount;
  AlertItem alerts[kMaxAlerts];
};

enum class ParseResult : uint8_t { Ok, TooLarge, BadJson, BadVersion };

// Faz o parse de `json` (até `len` bytes; o buffer é usado em modo zero-copy e pode ser
// modificado). Em qualquer erro `out` NÃO é alterado. Campos desconhecidos são ignorados.
ParseResult parseSnapshot(char* json, size_t len, Snapshot& out);

bool parseSessionState(const char* s, SessionState& out);
bool parseAlertKind(const char* s, AlertKind& out);

// Índice da sessão com esse id curto, ou -1.
int findSession(const Snapshot& s, const char* id);

}  // namespace miblo
```

`firmware/lib/miblo_core/src/miblo_snapshot.cpp` (filtro do ArduinoJson descarta campos desconhecidos antes de alocar; 4 KB de documento no ESP8266):

```cpp
#include "miblo_snapshot.h"

#include <ArduinoJson.h>
#include <math.h>
#include <string.h>

#include "miblo_utf8.h"

namespace miblo {

// 4 KB bastam no ESP8266 (slots de 16 bytes, strings zero-copy); no host 64-bit os slots dobram.
static constexpr size_t kDocCapacity = sizeof(void*) == 4 ? 4096 : 8192;

bool parseSessionState(const char* s, SessionState& out) {
  if (!s) return false;
  if (strcmp(s, "idle") == 0) out = SessionState::Idle;
  else if (strcmp(s, "running") == 0) out = SessionState::Running;
  else if (strcmp(s, "perm") == 0) out = SessionState::Perm;
  else if (strcmp(s, "question") == 0) out = SessionState::Question;
  else if (strcmp(s, "done") == 0) out = SessionState::Done;
  else return false;
  return true;
}

bool parseAlertKind(const char* s, AlertKind& out) {
  if (!s) return false;
  if (strcmp(s, "perm") == 0) out = AlertKind::Perm;
  else if (strcmp(s, "question") == 0) out = AlertKind::Question;
  else if (strcmp(s, "done") == 0) out = AlertKind::Done;
  else return false;
  return true;
}

int findSession(const Snapshot& s, const char* id) {
  for (int i = 0; i < s.count; i++) {
    if (strcmp(s.sessions[i].id, id) == 0) return i;
  }
  return -1;
}

static void copyStr(char* dst, size_t cap, JsonVariantConst v, size_t maxChars) {
  const char* s = v.is<const char*>() ? v.as<const char*>() : "";
  utf8Copy(dst, cap, s, maxChars);
}

static uint8_t clampPct(JsonVariantConst v) {
  float f = v.as<float>();
  if (!(f > 0)) return 0;
  if (f >= 100) return 100;
  return (uint8_t)lroundf(f);
}

static void readWindow(JsonVariantConst v, UsageWindow& w) {
  w.present = v.is<JsonObjectConst>() && v["pct"].is<float>();
  w.pct = w.present ? clampPct(v["pct"]) : 0;
  w.reset = w.present ? v["reset"].as<uint32_t>() : 0;
}

ParseResult parseSnapshot(char* json, size_t len, Snapshot& out) {
  if (len > kSnapshotMaxBytes) return ParseResult::TooLarge;

  DynamicJsonDocument filter(1024);
  filter["v"] = true;
  filter["seq"] = true;
  filter["now"] = true;
  filter["host"] = true;
  filter["usage"] = true;
  filter["today"] = true;
  filter["more"] = true;
  JsonObject fs = filter["sessions"].createNestedObject();
  for (const char* k : {"id", "name", "st", "tool", "det", "since", "model", "ctx", "tok"}) fs[k] = true;
  JsonObject fa = filter["alerts"].createNestedObject();
  for (const char* k : {"id", "kind", "sid"}) fa[k] = true;

  DynamicJsonDocument doc(kDocCapacity);
  DeserializationError err =
      deserializeJson(doc, json, len, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(6));
  if (err) return ParseResult::BadJson;
  if (!doc.is<JsonObject>()) return ParseResult::BadJson;
  if (!doc["v"].is<int>() || doc["v"].as<int>() < 1) return ParseResult::BadVersion;

  out.seq = doc["seq"].as<uint32_t>();
  out.now = doc["now"].as<uint32_t>();
  copyStr(out.host, sizeof(out.host), doc["host"], 20);

  JsonVariantConst usage = doc["usage"];
  out.hasUsage = usage.is<JsonObjectConst>();
  readWindow(usage["h5"], out.h5);
  readWindow(usage["d7"], out.d7);
  out.hasUsage = out.hasUsage && (out.h5.present || out.d7.present);

  out.todayUsd = doc["today"]["usd"].as<float>();

  out.count = 0;
  uint16_t skipped = 0;
  for (JsonObjectConst s : doc["sessions"].as<JsonArrayConst>()) {
    SessionState st;
    if (!parseSessionState(s["st"].as<const char*>(), st)) st = SessionState::Idle;
    if (out.count >= kMaxSessions) {
      skipped++;
      continue;
    }
    SessionRow& r = out.sessions[out.count++];
    copyStr(r.id, sizeof(r.id), s["id"], 8);
    copyStr(r.name, sizeof(r.name), s["name"], 20);
    r.st = st;
    copyStr(r.tool, sizeof(r.tool), s["tool"], 32);
    copyStr(r.det, sizeof(r.det), s["det"], 32);
    r.since = s["since"].as<uint32_t>();
    copyStr(r.model, sizeof(r.model), s["model"], 12);
    r.ctx = s["ctx"].is<int>() ? (int16_t)s["ctx"].as<int>() : -1;
    r.tok = s["tok"].is<int64_t>() ? s["tok"].as<int64_t>() : -1;
  }
  out.more = (uint16_t)(doc["more"].as<uint16_t>() + skipped);

  out.alertCount = 0;
  for (JsonObjectConst a : doc["alerts"].as<JsonArrayConst>()) {
    AlertKind kind;
    if (!parseAlertKind(a["kind"].as<const char*>(), kind)) continue;
    if (out.alertCount >= kMaxAlerts) break;
    AlertItem& it = out.alerts[out.alertCount++];
    it.id = a["id"].as<uint32_t>();
    it.kind = kind;
    copyStr(it.sid, sizeof(it.sid), a["sid"], 8);
  }
  return ParseResult::Ok;
}

}  // namespace miblo
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd firmware && .venv/bin/pio test -e native -f test_snapshot`
Expected: PASS — `7 test cases: 7 succeeded`.

- [ ] **Step 5: Commit**

```bash
git add firmware/lib/miblo_core/src/miblo_snapshot.h firmware/lib/miblo_core/src/miblo_snapshot.cpp firmware/test/test_snapshot/test_main.cpp
git commit -m "feat(firmware): snapshot parser validated against bridge contract fixtures"
```

---

### Task 3: i18n em 9 idiomas, negociação de idioma e verbos das ferramentas

**Files:**
- Create: `firmware/lib/miblo_core/src/miblo_rom.h`
- Create: `firmware/lib/miblo_core/src/miblo_i18n.h`, `firmware/lib/miblo_core/src/miblo_i18n.cpp`, `firmware/lib/miblo_core/src/miblo_strings.cpp`
- Create: `firmware/lib/miblo_core/src/miblo_activity.h`, `firmware/lib/miblo_core/src/miblo_activity.cpp`
- Test: `firmware/test/test_i18n/test_main.cpp`

**Interfaces:**
- Consumes: `SessionRow`, `SessionState` (Task 2); `MIBLO_ROM_IMPL` (Task 1).
- Produces: `enum class Lang {En, PtBR, PtPT, Es, Fr, It, De, Ru, Zh, Count}`; `enum class S {…91 ids…, Count}`; `extern const char* const kLangTables[]`; `langCode(Lang)`, `langName(Lang)`, `langFromCode(const char*, Lang&) → bool`, `negotiateLang(const char* acceptLanguage) → Lang`, `tr(Lang, S, char* out, size_t cap)`; `toolVerb(const char* tool, S&) → bool`, `activityText(Lang, tool, det, bool discreet, char*, size_t)`, `sessionLine(Lang, const SessionRow&, bool discreet, char*, size_t)`; `MIBLO_ROM`, `mibloRomByte(const char*) → uint8_t`.

Notas: cada idioma é uma única string compactada (`"a\0b\0…"`) com `MIBLO_ROM`, na ordem exata do enum `S`; o teste confere contagem e marcadores `%s/%u` de todos os idiomas. As strings de tela usam só caracteres presentes nas fontes da Task 11 (latim estendido, cirílico, GB2312 nível 1 — por isso `你好!` usa `!` ASCII).

- [ ] **Step 1: Write the failing test** — `firmware/test/test_i18n/test_main.cpp`

```cpp
#include <string.h>
#include <unity.h>

#include <string>
#include <vector>

#include "miblo_activity.h"
#include "miblo_i18n.h"
#include "miblo_utf8.h"

using namespace miblo;

void setUp() {}
void tearDown() {}

static std::string T(Lang l, S id) {
  char b[160];
  tr(l, id, b, sizeof(b));
  return b;
}

static std::vector<std::string> markers(const std::string& s) {
  std::vector<std::string> out;
  for (size_t i = 0; i + 1 < s.size(); i++) {
    if (s[i] == '%') out.push_back(s.substr(i, 2));
  }
  return out;
}

static void test_every_language_has_every_string_with_same_placeholders() {
  for (int l = 0; l < (int)Lang::Count; l++) {
    // a tabela tem exatamente S::Count entradas: a entrada seguinte à última é a string vazia
    // que o compilador põe no fim do literal (o "\0" final + o terminador implícito).
    const char* p = kLangTables[l];
    for (int i = 0; i < (int)S::Count; i++) {
      TEST_ASSERT_TRUE_MESSAGE(strlen(p) > 0, langCode((Lang)l));
      p += strlen(p) + 1;
    }
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, (uint8_t)*p, langCode((Lang)l));
    for (int i = 0; i < (int)S::Count; i++) {
      TEST_ASSERT_TRUE_MESSAGE(markers(T(Lang::En, (S)i)) == markers(T((Lang)l, (S)i)), langCode((Lang)l));
    }
  }
}

static void test_known_strings() {
  TEST_ASSERT_EQUAL_STRING("NEEDS YOU", T(Lang::En, S::NeedsYou).c_str());
  TEST_ASSERT_EQUAL_STRING("PRECISA DE VOCÊ", T(Lang::PtBR, S::NeedsYou).c_str());
  TEST_ASSERT_EQUAL_STRING("Pediu permissão", T(Lang::PtBR, S::AskedPermission).c_str());
  TEST_ASSERT_EQUAL_STRING("Palavra-passe incorreta", T(Lang::PtPT, S::WrongPassword).c_str());
  TEST_ASSERT_EQUAL_STRING("Неверный пароль", T(Lang::Ru, S::WrongPassword).c_str());
  TEST_ASSERT_EQUAL_STRING("密码错误", T(Lang::Zh, S::WrongPassword).c_str());
  TEST_ASSERT_EQUAL_STRING("Firmware version", T(Lang::En, S::WebVersion).c_str());
  TEST_ASSERT_EQUAL_STRING("qui", T(Lang::PtBR, S::WdThu).c_str());
}

static void test_tr_truncates_on_utf8_boundary() {
  char b[8];
  tr(Lang::Zh, S::WaitingComputer, b, sizeof(b));  // "等待电脑连接": 3 bytes por caractere
  TEST_ASSERT_EQUAL_STRING("等待", b);
  tr(Lang::En, S::Count, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("", b);
}

static void test_lang_codes_roundtrip() {
  for (int l = 0; l < (int)Lang::Count; l++) {
    Lang out;
    TEST_ASSERT_TRUE(langFromCode(langCode((Lang)l), out));
    TEST_ASSERT_EQUAL((int)l, (int)out);
  }
  Lang out;
  TEST_ASSERT_TRUE(langFromCode("PT-br", out));
  TEST_ASSERT_EQUAL(Lang::PtBR, out);
  TEST_ASSERT_FALSE(langFromCode("ja", out));
  TEST_ASSERT_FALSE(langFromCode(nullptr, out));
  TEST_ASSERT_EQUAL_STRING("Português (Brasil)", langName(Lang::PtBR));
}

static void test_negotiate_accept_language() {
  const struct {
    const char* header;
    Lang expected;
  } cases[] = {
      {nullptr, Lang::En},
      {"", Lang::En},
      {"ja-JP,ko;q=0.8", Lang::En},
      {"pt-BR,pt;q=0.9,en;q=0.8", Lang::PtBR},
      {"pt", Lang::PtBR},
      {"pt-PT,pt;q=0.9", Lang::PtPT},
      {"pt-AO", Lang::PtPT},
      {"zh-CN,zh;q=0.9", Lang::Zh},
      {"zh-Hant-TW", Lang::Zh},
      {"en-US,en;q=0.9,fr;q=0.8", Lang::En},
      {"ja;q=1.0, de;q=0.7, fr;q=0.9", Lang::Fr},
      {"it-IT", Lang::It},
      {"es-419,es;q=0.9", Lang::Es},
      {"ru-RU,ru;q=0.9,en-US;q=0.8", Lang::Ru},
      {"de-CH;q=0.5, en;q=0.4", Lang::De},
      {"fr;q=0.8, es;q=0.8", Lang::Fr},
      {"*;q=0.5", Lang::En},
  };
  for (const auto& c : cases) {
    TEST_ASSERT_EQUAL_MESSAGE((int)c.expected, (int)negotiateLang(c.header), c.header ? c.header : "null");
  }
}

static std::string act(Lang l, const char* tool, const char* det, bool discreet = false) {
  char b[200];
  activityText(l, tool, det, discreet, b, sizeof(b));
  return b;
}

static void test_activity_text() {
  TEST_ASSERT_EQUAL_STRING("Editando Header.tsx", act(Lang::PtBR, "Edit", "Header.tsx").c_str());
  TEST_ASSERT_EQUAL_STRING("Editando", act(Lang::PtBR, "Edit", "Header.tsx", true).c_str());
  TEST_ASSERT_EQUAL_STRING("Reading y.md", act(Lang::En, "Read", "y.md").c_str());
  TEST_ASSERT_EQUAL_STRING("Searching TODO", act(Lang::En, "Grep", "TODO").c_str());
  TEST_ASSERT_EQUAL_STRING("Web search", act(Lang::En, "WebSearch", "").c_str());
  TEST_ASSERT_EQUAL_STRING("Agent Find usages", act(Lang::En, "Task", "Find usages").c_str());
  TEST_ASSERT_EQUAL_STRING("Bash · npm test", act(Lang::PtBR, "Bash", "npm test").c_str());
  TEST_ASSERT_EQUAL_STRING("Bash", act(Lang::PtBR, "Bash", "npm test", true).c_str());
  TEST_ASSERT_EQUAL_STRING("create_issue", act(Lang::En, "create_issue", "").c_str());
  TEST_ASSERT_EQUAL_STRING("Trabalhando", act(Lang::PtBR, "", "").c_str());
  TEST_ASSERT_EQUAL_STRING("工作中", act(Lang::Zh, nullptr, nullptr).c_str());
}

static void test_session_line() {
  SessionRow r{};
  strcpy(r.tool, "Bash");
  strcpy(r.det, "npm run migrate");
  char b[200];
  r.st = SessionState::Perm;
  sessionLine(Lang::PtBR, r, false, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("permissão · Bash", b);
  r.st = SessionState::Question;
  sessionLine(Lang::PtBR, r, false, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("pergunta", b);
  r.st = SessionState::Done;
  sessionLine(Lang::En, r, false, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("finished", b);
  r.st = SessionState::Idle;
  sessionLine(Lang::En, r, false, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("idle", b);
  r.st = SessionState::Running;
  sessionLine(Lang::En, r, true, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("Bash", b);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_every_language_has_every_string_with_same_placeholders);
  RUN_TEST(test_known_strings);
  RUN_TEST(test_tr_truncates_on_utf8_boundary);
  RUN_TEST(test_lang_codes_roundtrip);
  RUN_TEST(test_negotiate_accept_language);
  RUN_TEST(test_activity_text);
  RUN_TEST(test_session_line);
  return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd firmware && .venv/bin/pio test -e native -f test_i18n`
Expected: FAIL — `miblo_activity.h: No such file or directory`.

- [ ] **Step 3: Implement** — `firmware/lib/miblo_core/src/miblo_rom.h`

```cpp
#pragma once
// Dados constantes grandes (tabelas de strings) em "ROM". No host e em placas com .rodata na
// flash (ESP32) é memória comum. Uma placa pode injetar outra implementação com
//   -D MIBLO_ROM_IMPL=\"<header>\"   (ex.: boards/geekmagic_ultra/miblo_rom_esp8266.h → PROGMEM)
// Assim o núcleo não inclui nenhum header de Arduino/ESP.
#include <stdint.h>
#include <string.h>

#if defined(MIBLO_ROM_IMPL)
#include MIBLO_ROM_IMPL
#else
#define MIBLO_ROM
inline uint8_t mibloRomByte(const char* p) { return (uint8_t)*p; }
#endif
```

`firmware/lib/miblo_core/src/miblo_i18n.h`:

```cpp
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace miblo {

enum class Lang : uint8_t { En, PtBR, PtPT, Es, Fr, It, De, Ru, Zh, Count };

// Identificadores das strings. A ordem é a mesma das tabelas em miblo_strings.cpp.
enum class S : uint8_t {
  Connecting,
  Hello,
  ScanPhone,
  OrJoin,
  WrongPassword,
  WifiConnected,
  RunInClaude,
  PairingCode,
  PairedWith,
  ModeOverview,
  ModeLimits,
  ModeSessions,
  Disconnected,
  WaitingComputer,
  Updating,
  DoNotUnplug,
  CodeUpdate,
  CodeReset,
  ExpiresIn,
  NeedsYou,
  NWaiting,
  NRunning,
  AllDone,
  Finished,
  AskedPermission,
  AskedQuestion,
  WaitingFor,
  PlusRunning,
  NIdle,
  Session5h,
  Week,
  ResetsAt,
  InTime,
  LimitsUnavailable,
  CostToday,
  FinishedAgo,
  Took,
  LimitsTitle,
  SessionsTitle,
  StPerm,
  StQuestion,
  StDone,
  StIdle,
  VerbEditing,
  VerbReading,
  VerbSearching,
  VerbFetching,
  VerbWebSearch,
  VerbAgent,
  VerbWorking,
  NoSessions,
  WdSun,
  WdMon,
  WdTue,
  WdWed,
  WdThu,
  WdFri,
  WdSat,
  WebSetupTitle,
  WebChooseNetwork,
  WebOtherNetwork,
  WebNetworkName,
  WebPassword,
  WebTimezone,
  WebLanguage,
  WebConnect,
  WebConnecting,
  WebSettings,
  WebMode,
  WebBrightness,
  WebAlerts,
  WebHeroPerm,
  WebHeroDone,
  WebReminder,
  WebDiscreet,
  WebDeviceName,
  WebSave,
  WebSaved,
  WebFactoryReset,
  WebResetConfirm,
  WebFirmware,
  WebShowPairCode,
  WebCodeHint,
  WebCode,
  WebUpload,
  WebUpdateOk,
  WebFailed,
  WebBadCode,
  WebLimitsHint,
  WebPairedCount,
  WebVersion,
  Count
};

// Tabelas por idioma (MIBLO_ROM), indexadas por Lang.
extern const char* const kLangTables[];

// "en", "pt-BR", "pt-PT", "es", "fr", "it", "de", "ru", "zh".
const char* langCode(Lang lang);
// Nome do idioma no próprio idioma ("Português (Brasil)"), para o seletor da página.
const char* langName(Lang lang);
// Código exato (sem diferenciar maiúsculas) → Lang. Retorna false se não suportado.
bool langFromCode(const char* code, Lang& out);
// Escolhe o idioma a partir do cabeçalho Accept-Language (maior q vence; empate → ordem).
// "pt" sem região → pt-BR; "pt-XX" (outra região) → pt-PT; "zh-*" → zh; nada suportado → en.
Lang negotiateLang(const char* acceptLanguage);

// Copia a string traduzida para `out` (sempre terminada em NUL, sem cortar UTF-8 no meio).
void tr(Lang lang, S id, char* out, size_t cap);

}  // namespace miblo
```

`firmware/lib/miblo_core/src/miblo_i18n.cpp`:

```cpp
#include "miblo_i18n.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "miblo_rom.h"

namespace miblo {

static const char* const kCodes[] = {"en", "pt-BR", "pt-PT", "es", "fr", "it", "de", "ru", "zh"};
static const char* const kNames[] = {"English",  "Português (Brasil)", "Português (Portugal)",
                                     "Español",  "Français",           "Italiano",
                                     "Deutsch",  "Русский",            "中文"};

const char* langCode(Lang lang) {
  return lang < Lang::Count ? kCodes[(int)lang] : kCodes[0];
}

const char* langName(Lang lang) {
  return lang < Lang::Count ? kNames[(int)lang] : kNames[0];
}

bool langFromCode(const char* code, Lang& out) {
  if (!code) return false;
  for (int i = 0; i < (int)Lang::Count; i++) {
    if (strcasecmp(code, kCodes[i]) == 0) {
      out = (Lang)i;
      return true;
    }
  }
  return false;
}

// Mapeia uma tag de idioma (ex.: "pt-br", "zh-Hans-CN", "fr") para Lang.
static bool matchTag(const char* tag, size_t len, Lang& out) {
  char buf[16];
  if (len == 0 || len >= sizeof(buf)) return false;
  for (size_t i = 0; i < len; i++) buf[i] = (char)tolower((unsigned char)tag[i]);
  buf[len] = 0;
  char primary[4] = {0};
  size_t p = 0;
  while (p < len && p < 3 && buf[p] != '-' && buf[p] != '_') {
    primary[p] = buf[p];
    p++;
  }
  if (p < len && buf[p] != '-' && buf[p] != '_') return false;  // primária com > 3 letras
  const char* region = (p < len) ? buf + p + 1 : "";
  if (strcmp(primary, "pt") == 0) {
    out = (region[0] == 0 || strcmp(region, "br") == 0) ? Lang::PtBR : Lang::PtPT;
    return true;
  }
  static const struct {
    const char* primary;
    Lang lang;
  } simple[] = {{"en", Lang::En}, {"es", Lang::Es}, {"fr", Lang::Fr}, {"it", Lang::It},
                {"de", Lang::De}, {"ru", Lang::Ru}, {"zh", Lang::Zh}};
  for (const auto& s : simple) {
    if (strcmp(primary, s.primary) == 0) {
      out = s.lang;
      return true;
    }
  }
  return false;
}

Lang negotiateLang(const char* header) {
  Lang best = Lang::En;
  int bestQ = -1;
  if (!header) return best;
  const char* p = header;
  while (*p) {
    while (*p == ' ' || *p == ',') p++;
    const char* tag = p;
    while (*p && *p != ';' && *p != ',' && *p != ' ') p++;
    size_t tagLen = (size_t)(p - tag);
    int q = 1000;
    while (*p == ' ') p++;
    if (*p == ';') {
      const char* qs = strstr(p, "q=");
      const char* next = strchr(p, ',');
      if (qs && (!next || qs < next)) q = (int)(atof(qs + 2) * 1000.0 + 0.5);
    }
    while (*p && *p != ',') p++;
    Lang l;
    if (tagLen > 0 && q > bestQ && matchTag(tag, tagLen, l)) {
      best = l;
      bestQ = q;
    }
  }
  return best;
}

void tr(Lang lang, S id, char* out, size_t cap) {
  if (cap == 0) return;
  if (lang >= Lang::Count) lang = Lang::En;
  if (id >= S::Count) {
    out[0] = 0;
    return;
  }
  const char* p = kLangTables[(int)lang];
  for (int i = 0; i < (int)id; i++) {
    while (mibloRomByte(p)) p++;
    p++;
  }
  size_t len = 0;
  while (mibloRomByte(p + len)) len++;
  if (len >= cap) {
    len = cap - 1;
    // não cortar no meio de uma sequência UTF-8
    while (len > 0 && (mibloRomByte(p + len) & 0xC0) == 0x80) len--;
  }
  for (size_t i = 0; i < len; i++) out[i] = (char)mibloRomByte(p + i);
  out[len] = 0;
}

}  // namespace miblo
```

`firmware/lib/miblo_core/src/miblo_strings.cpp` (todas as traduções):

```cpp
// Tabelas de strings (UTF-8) — uma string compactada por idioma, entradas separadas por \0.
// A ordem das entradas segue exatamente o enum miblo::S (miblo_i18n.h).
// test_i18n confere a contagem e os marcadores %s/%u de cada idioma.
#include "miblo_i18n.h"
#include "miblo_rom.h"

namespace miblo {

static const char kEn[] MIBLO_ROM =
    "Connecting to Wi-Fi\0"  // Connecting
    "Hello!\0"  // Hello
    "Scan with your phone\0"  // ScanPhone
    "or join the Wi-Fi network\0"  // OrJoin
    "Wrong password\0"  // WrongPassword
    "Wi-Fi connected\0"  // WifiConnected
    "In Claude Code, run:\0"  // RunInClaude
    "Pairing code\0"  // PairingCode
    "Paired with\0"  // PairedWith
    "Overview\0"  // ModeOverview
    "Limits\0"  // ModeLimits
    "Sessions\0"  // ModeSessions
    "Disconnected\0"  // Disconnected
    "Waiting for the computer\0"  // WaitingComputer
    "Updating firmware\0"  // Updating
    "Do not unplug\0"  // DoNotUnplug
    "Firmware update code\0"  // CodeUpdate
    "Factory reset code\0"  // CodeReset
    "expires in %s\0"  // ExpiresIn
    "NEEDS YOU\0"  // NeedsYou
    "%u WAITING\0"  // NWaiting
    "%u RUNNING\0"  // NRunning
    "ALL DONE\0"  // AllDone
    "FINISHED\0"  // Finished
    "Asked permission\0"  // AskedPermission
    "Asked a question\0"  // AskedQuestion
    "waiting %s\0"  // WaitingFor
    "+%u running\0"  // PlusRunning
    "%u idle\0"  // NIdle
    "5h session\0"  // Session5h
    "Week\0"  // Week
    "resets %s\0"  // ResetsAt
    "in %s\0"  // InTime
    "limits unavailable\0"  // LimitsUnavailable
    "today %s\0"  // CostToday
    "%s finished %s ago\0"  // FinishedAgo
    "took %s\0"  // Took
    "LIMITS\0"  // LimitsTitle
    "SESSIONS · %u\0"  // SessionsTitle
    "permission\0"  // StPerm
    "question\0"  // StQuestion
    "finished\0"  // StDone
    "idle\0"  // StIdle
    "Editing\0"  // VerbEditing
    "Reading\0"  // VerbReading
    "Searching\0"  // VerbSearching
    "Fetching\0"  // VerbFetching
    "Web search\0"  // VerbWebSearch
    "Agent\0"  // VerbAgent
    "Working\0"  // VerbWorking
    "No active sessions\0"  // NoSessions
    "Sun\0"  // WdSun
    "Mon\0"  // WdMon
    "Tue\0"  // WdTue
    "Wed\0"  // WdWed
    "Thu\0"  // WdThu
    "Fri\0"  // WdFri
    "Sat\0"  // WdSat
    "Miblo Wi-Fi setup\0"  // WebSetupTitle
    "Choose your network\0"  // WebChooseNetwork
    "Other network (hidden)\0"  // WebOtherNetwork
    "Network name\0"  // WebNetworkName
    "Password\0"  // WebPassword
    "Time zone\0"  // WebTimezone
    "Language\0"  // WebLanguage
    "Connect\0"  // WebConnect
    "Connecting. Check the device screen.\0"  // WebConnecting
    "Settings\0"  // WebSettings
    "Mode\0"  // WebMode
    "Brightness\0"  // WebBrightness
    "Alerts (flash + highlight)\0"  // WebAlerts
    "Highlight: needs you (s)\0"  // WebHeroPerm
    "Highlight: finished (s)\0"  // WebHeroDone
    "Reminder every (min, 0 = off)\0"  // WebReminder
    "Discreet mode (hide commands and files)\0"  // WebDiscreet
    "Device name\0"  // WebDeviceName
    "Save\0"  // WebSave
    "Saved\0"  // WebSaved
    "Factory reset\0"  // WebFactoryReset
    "This erases Wi-Fi, pairings and settings.\0"  // WebResetConfirm
    "Firmware update\0"  // WebFirmware
    "Show pairing code on the device\0"  // WebShowPairCode
    "Enter the 4-digit code shown on the device screen\0"  // WebCodeHint
    "Code\0"  // WebCode
    "Upload\0"  // WebUpload
    "Done. The device is restarting.\0"  // WebUpdateOk
    "Failed\0"  // WebFailed
    "Wrong code\0"  // WebBadCode
    "No limits received yet: run /miblo pair in Claude Code\0"  // WebLimitsHint
    "Paired computers: %u\0"  // WebPairedCount
    "Firmware version\0";  // WebVersion

static const char kPtBR[] MIBLO_ROM =
    "Conectando ao Wi-Fi\0"  // Connecting
    "Olá!\0"  // Hello
    "Aponte a câmera do celular\0"  // ScanPhone
    "ou conecte na rede Wi-Fi\0"  // OrJoin
    "Senha incorreta\0"  // WrongPassword
    "Wi-Fi conectado\0"  // WifiConnected
    "No Claude Code, rode:\0"  // RunInClaude
    "Código de pareamento\0"  // PairingCode
    "Pareado com\0"  // PairedWith
    "Visão geral\0"  // ModeOverview
    "Limites\0"  // ModeLimits
    "Sessões\0"  // ModeSessions
    "Desconectado\0"  // Disconnected
    "Aguardando o computador\0"  // WaitingComputer
    "Atualizando firmware\0"  // Updating
    "Não desligue da tomada\0"  // DoNotUnplug
    "Código para atualizar\0"  // CodeUpdate
    "Código para resetar\0"  // CodeReset
    "expira em %s\0"  // ExpiresIn
    "PRECISA DE VOCÊ\0"  // NeedsYou
    "%u AGUARDANDO\0"  // NWaiting
    "%u RODANDO\0"  // NRunning
    "TUDO PRONTO\0"  // AllDone
    "TERMINOU\0"  // Finished
    "Pediu permissão\0"  // AskedPermission
    "Fez uma pergunta\0"  // AskedQuestion
    "esperando há %s\0"  // WaitingFor
    "+%u rodando\0"  // PlusRunning
    "ociosas: %u\0"  // NIdle
    "Sessão 5h\0"  // Session5h
    "Semana\0"  // Week
    "reseta %s\0"  // ResetsAt
    "em %s\0"  // InTime
    "limites indisponíveis\0"  // LimitsUnavailable
    "hoje %s\0"  // CostToday
    "%s terminou há %s\0"  // FinishedAgo
    "levou %s\0"  // Took
    "LIMITES\0"  // LimitsTitle
    "SESSÕES · %u\0"  // SessionsTitle
    "permissão\0"  // StPerm
    "pergunta\0"  // StQuestion
    "terminou\0"  // StDone
    "ociosa\0"  // StIdle
    "Editando\0"  // VerbEditing
    "Lendo\0"  // VerbReading
    "Buscando\0"  // VerbSearching
    "Acessando\0"  // VerbFetching
    "Pesquisando\0"  // VerbWebSearch
    "Agente\0"  // VerbAgent
    "Trabalhando\0"  // VerbWorking
    "Nenhuma sessão ativa\0"  // NoSessions
    "dom\0"  // WdSun
    "seg\0"  // WdMon
    "ter\0"  // WdTue
    "qua\0"  // WdWed
    "qui\0"  // WdThu
    "sex\0"  // WdFri
    "sáb\0"  // WdSat
    "Configurar o Wi-Fi do Miblo\0"  // WebSetupTitle
    "Escolha sua rede\0"  // WebChooseNetwork
    "Outra rede (oculta)\0"  // WebOtherNetwork
    "Nome da rede\0"  // WebNetworkName
    "Senha\0"  // WebPassword
    "Fuso horário\0"  // WebTimezone
    "Idioma\0"  // WebLanguage
    "Conectar\0"  // WebConnect
    "Conectando. Veja a tela do aparelho.\0"  // WebConnecting
    "Configurações\0"  // WebSettings
    "Modo\0"  // WebMode
    "Brilho\0"  // WebBrightness
    "Alertas (flash + destaque)\0"  // WebAlerts
    "Destaque: precisa de você (s)\0"  // WebHeroPerm
    "Destaque: terminou (s)\0"  // WebHeroDone
    "Lembrete a cada (min, 0 = desligado)\0"  // WebReminder
    "Modo discreto (oculta comandos e arquivos)\0"  // WebDiscreet
    "Nome do aparelho\0"  // WebDeviceName
    "Salvar\0"  // WebSave
    "Salvo\0"  // WebSaved
    "Reset de fábrica\0"  // WebFactoryReset
    "Isso apaga Wi-Fi, pareamentos e configurações.\0"  // WebResetConfirm
    "Atualizar firmware\0"  // WebFirmware
    "Mostrar código de pareamento no aparelho\0"  // WebShowPairCode
    "Digite o código de 4 dígitos mostrado na tela do aparelho\0"  // WebCodeHint
    "Código\0"  // WebCode
    "Enviar\0"  // WebUpload
    "Pronto. O aparelho está reiniciando.\0"  // WebUpdateOk
    "Falhou\0"  // WebFailed
    "Código incorreto\0"  // WebBadCode
    "Nenhum limite recebido ainda: rode /miblo pair no Claude Code\0"  // WebLimitsHint
    "Computadores pareados: %u\0"  // WebPairedCount
    "Versão do firmware\0";  // WebVersion

static const char kPtPT[] MIBLO_ROM =
    "A ligar ao Wi-Fi\0"  // Connecting
    "Olá!\0"  // Hello
    "Aponte a câmara do telemóvel\0"  // ScanPhone
    "ou ligue-se à rede Wi-Fi\0"  // OrJoin
    "Palavra-passe incorreta\0"  // WrongPassword
    "Wi-Fi ligado\0"  // WifiConnected
    "No Claude Code, execute:\0"  // RunInClaude
    "Código de emparelhamento\0"  // PairingCode
    "Emparelhado com\0"  // PairedWith
    "Visão geral\0"  // ModeOverview
    "Limites\0"  // ModeLimits
    "Sessões\0"  // ModeSessions
    "Desligado\0"  // Disconnected
    "À espera do computador\0"  // WaitingComputer
    "A atualizar o firmware\0"  // Updating
    "Não desligue da tomada\0"  // DoNotUnplug
    "Código para atualizar\0"  // CodeUpdate
    "Código para repor\0"  // CodeReset
    "expira em %s\0"  // ExpiresIn
    "PRECISA DE SI\0"  // NeedsYou
    "%u EM ESPERA\0"  // NWaiting
    "%u A CORRER\0"  // NRunning
    "TUDO PRONTO\0"  // AllDone
    "TERMINOU\0"  // Finished
    "Pediu permissão\0"  // AskedPermission
    "Fez uma pergunta\0"  // AskedQuestion
    "à espera há %s\0"  // WaitingFor
    "+%u a correr\0"  // PlusRunning
    "inativas: %u\0"  // NIdle
    "Sessão 5h\0"  // Session5h
    "Semana\0"  // Week
    "repõe %s\0"  // ResetsAt
    "em %s\0"  // InTime
    "limites indisponíveis\0"  // LimitsUnavailable
    "hoje %s\0"  // CostToday
    "%s terminou há %s\0"  // FinishedAgo
    "demorou %s\0"  // Took
    "LIMITES\0"  // LimitsTitle
    "SESSÕES · %u\0"  // SessionsTitle
    "permissão\0"  // StPerm
    "pergunta\0"  // StQuestion
    "terminou\0"  // StDone
    "inativa\0"  // StIdle
    "A editar\0"  // VerbEditing
    "A ler\0"  // VerbReading
    "A procurar\0"  // VerbSearching
    "A aceder\0"  // VerbFetching
    "A pesquisar\0"  // VerbWebSearch
    "Agente\0"  // VerbAgent
    "A trabalhar\0"  // VerbWorking
    "Nenhuma sessão ativa\0"  // NoSessions
    "dom\0"  // WdSun
    "seg\0"  // WdMon
    "ter\0"  // WdTue
    "qua\0"  // WdWed
    "qui\0"  // WdThu
    "sex\0"  // WdFri
    "sáb\0"  // WdSat
    "Configurar o Wi-Fi do Miblo\0"  // WebSetupTitle
    "Escolha a sua rede\0"  // WebChooseNetwork
    "Outra rede (oculta)\0"  // WebOtherNetwork
    "Nome da rede\0"  // WebNetworkName
    "Palavra-passe\0"  // WebPassword
    "Fuso horário\0"  // WebTimezone
    "Idioma\0"  // WebLanguage
    "Ligar\0"  // WebConnect
    "A ligar. Veja o ecrã do aparelho.\0"  // WebConnecting
    "Definições\0"  // WebSettings
    "Modo\0"  // WebMode
    "Brilho\0"  // WebBrightness
    "Alertas (flash + destaque)\0"  // WebAlerts
    "Destaque: precisa de si (s)\0"  // WebHeroPerm
    "Destaque: terminou (s)\0"  // WebHeroDone
    "Lembrete a cada (min, 0 = desligado)\0"  // WebReminder
    "Modo discreto (oculta comandos e ficheiros)\0"  // WebDiscreet
    "Nome do aparelho\0"  // WebDeviceName
    "Guardar\0"  // WebSave
    "Guardado\0"  // WebSaved
    "Repor definições de fábrica\0"  // WebFactoryReset
    "Isto apaga o Wi-Fi, os emparelhamentos e as definições.\0"  // WebResetConfirm
    "Atualizar firmware\0"  // WebFirmware
    "Mostrar código de emparelhamento no aparelho\0"  // WebShowPairCode
    "Introduza o código de 4 dígitos mostrado no ecrã do aparelho\0"  // WebCodeHint
    "Código\0"  // WebCode
    "Enviar\0"  // WebUpload
    "Concluído. O aparelho está a reiniciar.\0"  // WebUpdateOk
    "Falhou\0"  // WebFailed
    "Código incorreto\0"  // WebBadCode
    "Ainda não chegaram limites: execute /miblo pair no Claude Code\0"  // WebLimitsHint
    "Computadores emparelhados: %u\0"  // WebPairedCount
    "Versão do firmware\0";  // WebVersion

static const char kEs[] MIBLO_ROM =
    "Conectando al Wi-Fi\0"  // Connecting
    "¡Hola!\0"  // Hello
    "Escanea con tu móvil\0"  // ScanPhone
    "o conéctate a la red Wi-Fi\0"  // OrJoin
    "Contraseña incorrecta\0"  // WrongPassword
    "Wi-Fi conectado\0"  // WifiConnected
    "En Claude Code, ejecuta:\0"  // RunInClaude
    "Código de vinculación\0"  // PairingCode
    "Vinculado con\0"  // PairedWith
    "Resumen\0"  // ModeOverview
    "Límites\0"  // ModeLimits
    "Sesiones\0"  // ModeSessions
    "Desconectado\0"  // Disconnected
    "Esperando al ordenador\0"  // WaitingComputer
    "Actualizando firmware\0"  // Updating
    "No lo desenchufes\0"  // DoNotUnplug
    "Código de actualización\0"  // CodeUpdate
    "Código de restablecimiento\0"  // CodeReset
    "caduca en %s\0"  // ExpiresIn
    "TE NECESITA\0"  // NeedsYou
    "%u ESPERANDO\0"  // NWaiting
    "%u EN CURSO\0"  // NRunning
    "TODO LISTO\0"  // AllDone
    "TERMINÓ\0"  // Finished
    "Pidió permiso\0"  // AskedPermission
    "Hizo una pregunta\0"  // AskedQuestion
    "esperando %s\0"  // WaitingFor
    "+%u en curso\0"  // PlusRunning
    "inactivas: %u\0"  // NIdle
    "Sesión 5h\0"  // Session5h
    "Semana\0"  // Week
    "se reinicia %s\0"  // ResetsAt
    "en %s\0"  // InTime
    "límites no disponibles\0"  // LimitsUnavailable
    "hoy %s\0"  // CostToday
    "%s terminó hace %s\0"  // FinishedAgo
    "tardó %s\0"  // Took
    "LÍMITES\0"  // LimitsTitle
    "SESIONES · %u\0"  // SessionsTitle
    "permiso\0"  // StPerm
    "pregunta\0"  // StQuestion
    "terminó\0"  // StDone
    "inactiva\0"  // StIdle
    "Editando\0"  // VerbEditing
    "Leyendo\0"  // VerbReading
    "Buscando\0"  // VerbSearching
    "Descargando\0"  // VerbFetching
    "Buscando en la web\0"  // VerbWebSearch
    "Agente\0"  // VerbAgent
    "Trabajando\0"  // VerbWorking
    "Ninguna sesión activa\0"  // NoSessions
    "dom\0"  // WdSun
    "lun\0"  // WdMon
    "mar\0"  // WdTue
    "mié\0"  // WdWed
    "jue\0"  // WdThu
    "vie\0"  // WdFri
    "sáb\0"  // WdSat
    "Configurar el Wi-Fi de Miblo\0"  // WebSetupTitle
    "Elige tu red\0"  // WebChooseNetwork
    "Otra red (oculta)\0"  // WebOtherNetwork
    "Nombre de la red\0"  // WebNetworkName
    "Contraseña\0"  // WebPassword
    "Zona horaria\0"  // WebTimezone
    "Idioma\0"  // WebLanguage
    "Conectar\0"  // WebConnect
    "Conectando. Mira la pantalla del dispositivo.\0"  // WebConnecting
    "Ajustes\0"  // WebSettings
    "Modo\0"  // WebMode
    "Brillo\0"  // WebBrightness
    "Alertas (destello + destacado)\0"  // WebAlerts
    "Destacado: te necesita (s)\0"  // WebHeroPerm
    "Destacado: terminó (s)\0"  // WebHeroDone
    "Recordatorio cada (min, 0 = desactivado)\0"  // WebReminder
    "Modo discreto (oculta comandos y archivos)\0"  // WebDiscreet
    "Nombre del dispositivo\0"  // WebDeviceName
    "Guardar\0"  // WebSave
    "Guardado\0"  // WebSaved
    "Restablecer de fábrica\0"  // WebFactoryReset
    "Esto borra el Wi-Fi, las vinculaciones y los ajustes.\0"  // WebResetConfirm
    "Actualizar firmware\0"  // WebFirmware
    "Mostrar el código de vinculación en el dispositivo\0"  // WebShowPairCode
    "Introduce el código de 4 dígitos que aparece en la pantalla\0"  // WebCodeHint
    "Código\0"  // WebCode
    "Subir\0"  // WebUpload
    "Listo. El dispositivo se está reiniciando.\0"  // WebUpdateOk
    "Error\0"  // WebFailed
    "Código incorrecto\0"  // WebBadCode
    "Aún no llegan límites: ejecuta /miblo pair en Claude Code\0"  // WebLimitsHint
    "Ordenadores vinculados: %u\0"  // WebPairedCount
    "Versión del firmware\0";  // WebVersion

static const char kFr[] MIBLO_ROM =
    "Connexion au Wi-Fi\0"  // Connecting
    "Bonjour !\0"  // Hello
    "Scannez avec votre téléphone\0"  // ScanPhone
    "ou rejoignez le réseau Wi-Fi\0"  // OrJoin
    "Mot de passe incorrect\0"  // WrongPassword
    "Wi-Fi connecté\0"  // WifiConnected
    "Dans Claude Code, lancez :\0"  // RunInClaude
    "Code d'appairage\0"  // PairingCode
    "Appairé avec\0"  // PairedWith
    "Vue d'ensemble\0"  // ModeOverview
    "Limites\0"  // ModeLimits
    "Sessions\0"  // ModeSessions
    "Déconnecté\0"  // Disconnected
    "En attente de l'ordinateur\0"  // WaitingComputer
    "Mise à jour du firmware\0"  // Updating
    "Ne débranchez pas\0"  // DoNotUnplug
    "Code de mise à jour\0"  // CodeUpdate
    "Code de réinitialisation\0"  // CodeReset
    "expire dans %s\0"  // ExpiresIn
    "BESOIN DE VOUS\0"  // NeedsYou
    "%u EN ATTENTE\0"  // NWaiting
    "%u EN COURS\0"  // NRunning
    "TOUT EST FINI\0"  // AllDone
    "TERMINÉ\0"  // Finished
    "Demande une permission\0"  // AskedPermission
    "A posé une question\0"  // AskedQuestion
    "en attente depuis %s\0"  // WaitingFor
    "+%u en cours\0"  // PlusRunning
    "inactives : %u\0"  // NIdle
    "Session 5 h\0"  // Session5h
    "Semaine\0"  // Week
    "réinit. %s\0"  // ResetsAt
    "dans %s\0"  // InTime
    "limites indisponibles\0"  // LimitsUnavailable
    "aujourd'hui %s\0"  // CostToday
    "%s a fini il y a %s\0"  // FinishedAgo
    "durée %s\0"  // Took
    "LIMITES\0"  // LimitsTitle
    "SESSIONS · %u\0"  // SessionsTitle
    "permission\0"  // StPerm
    "question\0"  // StQuestion
    "terminée\0"  // StDone
    "inactive\0"  // StIdle
    "Modifie\0"  // VerbEditing
    "Lit\0"  // VerbReading
    "Cherche\0"  // VerbSearching
    "Télécharge\0"  // VerbFetching
    "Recherche web\0"  // VerbWebSearch
    "Agent\0"  // VerbAgent
    "Travaille\0"  // VerbWorking
    "Aucune session active\0"  // NoSessions
    "dim\0"  // WdSun
    "lun\0"  // WdMon
    "mar\0"  // WdTue
    "mer\0"  // WdWed
    "jeu\0"  // WdThu
    "ven\0"  // WdFri
    "sam\0"  // WdSat
    "Configurer le Wi-Fi de Miblo\0"  // WebSetupTitle
    "Choisissez votre réseau\0"  // WebChooseNetwork
    "Autre réseau (masqué)\0"  // WebOtherNetwork
    "Nom du réseau\0"  // WebNetworkName
    "Mot de passe\0"  // WebPassword
    "Fuseau horaire\0"  // WebTimezone
    "Langue\0"  // WebLanguage
    "Se connecter\0"  // WebConnect
    "Connexion en cours. Regardez l'écran de l'appareil.\0"  // WebConnecting
    "Réglages\0"  // WebSettings
    "Mode\0"  // WebMode
    "Luminosité\0"  // WebBrightness
    "Alertes (flash + mise en avant)\0"  // WebAlerts
    "Mise en avant : besoin de vous (s)\0"  // WebHeroPerm
    "Mise en avant : terminé (s)\0"  // WebHeroDone
    "Rappel toutes les (min, 0 = désactivé)\0"  // WebReminder
    "Mode discret (masque commandes et fichiers)\0"  // WebDiscreet
    "Nom de l'appareil\0"  // WebDeviceName
    "Enregistrer\0"  // WebSave
    "Enregistré\0"  // WebSaved
    "Réinitialisation d'usine\0"  // WebFactoryReset
    "Cela efface le Wi-Fi, les appairages et les réglages.\0"  // WebResetConfirm
    "Mise à jour du firmware\0"  // WebFirmware
    "Afficher le code d'appairage sur l'appareil\0"  // WebShowPairCode
    "Saisissez le code à 4 chiffres affiché sur l'écran de l'appareil\0"  // WebCodeHint
    "Code\0"  // WebCode
    "Envoyer\0"  // WebUpload
    "Terminé. L'appareil redémarre.\0"  // WebUpdateOk
    "Échec\0"  // WebFailed
    "Code incorrect\0"  // WebBadCode
    "Aucune limite reçue : lancez /miblo pair dans Claude Code\0"  // WebLimitsHint
    "Ordinateurs appairés : %u\0"  // WebPairedCount
    "Version du firmware\0";  // WebVersion

static const char kIt[] MIBLO_ROM =
    "Connessione al Wi-Fi\0"  // Connecting
    "Ciao!\0"  // Hello
    "Inquadra con il telefono\0"  // ScanPhone
    "oppure connettiti alla rete Wi-Fi\0"  // OrJoin
    "Password errata\0"  // WrongPassword
    "Wi-Fi connesso\0"  // WifiConnected
    "In Claude Code, esegui:\0"  // RunInClaude
    "Codice di abbinamento\0"  // PairingCode
    "Abbinato con\0"  // PairedWith
    "Panoramica\0"  // ModeOverview
    "Limiti\0"  // ModeLimits
    "Sessioni\0"  // ModeSessions
    "Disconnesso\0"  // Disconnected
    "In attesa del computer\0"  // WaitingComputer
    "Aggiornamento firmware\0"  // Updating
    "Non scollegare\0"  // DoNotUnplug
    "Codice di aggiornamento\0"  // CodeUpdate
    "Codice di ripristino\0"  // CodeReset
    "scade tra %s\0"  // ExpiresIn
    "TOCCA A TE\0"  // NeedsYou
    "%u IN ATTESA\0"  // NWaiting
    "%u IN CORSO\0"  // NRunning
    "TUTTO FATTO\0"  // AllDone
    "FINITO\0"  // Finished
    "Chiede un permesso\0"  // AskedPermission
    "Ha fatto una domanda\0"  // AskedQuestion
    "in attesa da %s\0"  // WaitingFor
    "+%u in corso\0"  // PlusRunning
    "inattive: %u\0"  // NIdle
    "Sessione 5h\0"  // Session5h
    "Settimana\0"  // Week
    "si azzera %s\0"  // ResetsAt
    "tra %s\0"  // InTime
    "limiti non disponibili\0"  // LimitsUnavailable
    "oggi %s\0"  // CostToday
    "%s ha finito %s fa\0"  // FinishedAgo
    "durata %s\0"  // Took
    "LIMITI\0"  // LimitsTitle
    "SESSIONI · %u\0"  // SessionsTitle
    "permesso\0"  // StPerm
    "domanda\0"  // StQuestion
    "finita\0"  // StDone
    "inattiva\0"  // StIdle
    "Modifica\0"  // VerbEditing
    "Legge\0"  // VerbReading
    "Cerca\0"  // VerbSearching
    "Scarica\0"  // VerbFetching
    "Ricerca web\0"  // VerbWebSearch
    "Agente\0"  // VerbAgent
    "Lavora\0"  // VerbWorking
    "Nessuna sessione attiva\0"  // NoSessions
    "dom\0"  // WdSun
    "lun\0"  // WdMon
    "mar\0"  // WdTue
    "mer\0"  // WdWed
    "gio\0"  // WdThu
    "ven\0"  // WdFri
    "sab\0"  // WdSat
    "Configura il Wi-Fi di Miblo\0"  // WebSetupTitle
    "Scegli la tua rete\0"  // WebChooseNetwork
    "Altra rete (nascosta)\0"  // WebOtherNetwork
    "Nome della rete\0"  // WebNetworkName
    "Password\0"  // WebPassword
    "Fuso orario\0"  // WebTimezone
    "Lingua\0"  // WebLanguage
    "Connetti\0"  // WebConnect
    "Connessione in corso. Guarda lo schermo del dispositivo.\0"  // WebConnecting
    "Impostazioni\0"  // WebSettings
    "Modalità\0"  // WebMode
    "Luminosità\0"  // WebBrightness
    "Avvisi (lampeggio + evidenza)\0"  // WebAlerts
    "Evidenza: tocca a te (s)\0"  // WebHeroPerm
    "Evidenza: finito (s)\0"  // WebHeroDone
    "Promemoria ogni (min, 0 = spento)\0"  // WebReminder
    "Modalità discreta (nasconde comandi e file)\0"  // WebDiscreet
    "Nome del dispositivo\0"  // WebDeviceName
    "Salva\0"  // WebSave
    "Salvato\0"  // WebSaved
    "Ripristino di fabbrica\0"  // WebFactoryReset
    "Cancella Wi-Fi, abbinamenti e impostazioni.\0"  // WebResetConfirm
    "Aggiorna firmware\0"  // WebFirmware
    "Mostra il codice di abbinamento sul dispositivo\0"  // WebShowPairCode
    "Inserisci il codice di 4 cifre mostrato sullo schermo del dispositivo\0"  // WebCodeHint
    "Codice\0"  // WebCode
    "Carica\0"  // WebUpload
    "Fatto. Il dispositivo si sta riavviando.\0"  // WebUpdateOk
    "Non riuscito\0"  // WebFailed
    "Codice errato\0"  // WebBadCode
    "Nessun limite ricevuto: esegui /miblo pair in Claude Code\0"  // WebLimitsHint
    "Computer abbinati: %u\0"  // WebPairedCount
    "Versione firmware\0";  // WebVersion

static const char kDe[] MIBLO_ROM =
    "Verbinde mit WLAN\0"  // Connecting
    "Hallo!\0"  // Hello
    "Mit dem Handy scannen\0"  // ScanPhone
    "oder mit dem WLAN verbinden\0"  // OrJoin
    "Falsches Passwort\0"  // WrongPassword
    "WLAN verbunden\0"  // WifiConnected
    "In Claude Code ausführen:\0"  // RunInClaude
    "Kopplungscode\0"  // PairingCode
    "Gekoppelt mit\0"  // PairedWith
    "Übersicht\0"  // ModeOverview
    "Limits\0"  // ModeLimits
    "Sitzungen\0"  // ModeSessions
    "Getrennt\0"  // Disconnected
    "Warte auf den Computer\0"  // WaitingComputer
    "Firmware wird aktualisiert\0"  // Updating
    "Nicht vom Strom trennen\0"  // DoNotUnplug
    "Code für Update\0"  // CodeUpdate
    "Code für Zurücksetzen\0"  // CodeReset
    "läuft ab in %s\0"  // ExpiresIn
    "BRAUCHT DICH\0"  // NeedsYou
    "%u WARTEN\0"  // NWaiting
    "%u LAUFEN\0"  // NRunning
    "ALLES FERTIG\0"  // AllDone
    "FERTIG\0"  // Finished
    "Fragt nach Erlaubnis\0"  // AskedPermission
    "Hat eine Frage\0"  // AskedQuestion
    "wartet seit %s\0"  // WaitingFor
    "+%u laufen\0"  // PlusRunning
    "inaktiv: %u\0"  // NIdle
    "5-h-Sitzung\0"  // Session5h
    "Woche\0"  // Week
    "Reset %s\0"  // ResetsAt
    "in %s\0"  // InTime
    "Limits nicht verfügbar\0"  // LimitsUnavailable
    "heute %s\0"  // CostToday
    "%s fertig vor %s\0"  // FinishedAgo
    "Dauer %s\0"  // Took
    "LIMITS\0"  // LimitsTitle
    "SITZUNGEN · %u\0"  // SessionsTitle
    "Erlaubnis\0"  // StPerm
    "Frage\0"  // StQuestion
    "fertig\0"  // StDone
    "inaktiv\0"  // StIdle
    "Bearbeitet\0"  // VerbEditing
    "Liest\0"  // VerbReading
    "Sucht\0"  // VerbSearching
    "Lädt\0"  // VerbFetching
    "Websuche\0"  // VerbWebSearch
    "Agent\0"  // VerbAgent
    "Arbeitet\0"  // VerbWorking
    "Keine aktiven Sitzungen\0"  // NoSessions
    "So\0"  // WdSun
    "Mo\0"  // WdMon
    "Di\0"  // WdTue
    "Mi\0"  // WdWed
    "Do\0"  // WdThu
    "Fr\0"  // WdFri
    "Sa\0"  // WdSat
    "Miblo-WLAN einrichten\0"  // WebSetupTitle
    "Wähle dein Netzwerk\0"  // WebChooseNetwork
    "Anderes Netzwerk (versteckt)\0"  // WebOtherNetwork
    "Netzwerkname\0"  // WebNetworkName
    "Passwort\0"  // WebPassword
    "Zeitzone\0"  // WebTimezone
    "Sprache\0"  // WebLanguage
    "Verbinden\0"  // WebConnect
    "Verbinde. Schau auf das Display des Geräts.\0"  // WebConnecting
    "Einstellungen\0"  // WebSettings
    "Modus\0"  // WebMode
    "Helligkeit\0"  // WebBrightness
    "Hinweise (Blinken + Hervorhebung)\0"  // WebAlerts
    "Hervorhebung: braucht dich (s)\0"  // WebHeroPerm
    "Hervorhebung: fertig (s)\0"  // WebHeroDone
    "Erinnerung alle (min, 0 = aus)\0"  // WebReminder
    "Diskreter Modus (verbirgt Befehle und Dateien)\0"  // WebDiscreet
    "Gerätename\0"  // WebDeviceName
    "Speichern\0"  // WebSave
    "Gespeichert\0"  // WebSaved
    "Werksreset\0"  // WebFactoryReset
    "Löscht WLAN, Kopplungen und Einstellungen.\0"  // WebResetConfirm
    "Firmware-Update\0"  // WebFirmware
    "Kopplungscode auf dem Gerät anzeigen\0"  // WebShowPairCode
    "Gib den 4-stelligen Code vom Display des Geräts ein\0"  // WebCodeHint
    "Code\0"  // WebCode
    "Hochladen\0"  // WebUpload
    "Fertig. Das Gerät startet neu.\0"  // WebUpdateOk
    "Fehlgeschlagen\0"  // WebFailed
    "Falscher Code\0"  // WebBadCode
    "Noch keine Limits empfangen: führe /miblo pair in Claude Code aus\0"  // WebLimitsHint
    "Gekoppelte Computer: %u\0"  // WebPairedCount
    "Firmware-Version\0";  // WebVersion

static const char kRu[] MIBLO_ROM =
    "Подключение к Wi-Fi\0"  // Connecting
    "Привет!\0"  // Hello
    "Отсканируйте телефоном\0"  // ScanPhone
    "или подключитесь к сети Wi-Fi\0"  // OrJoin
    "Неверный пароль\0"  // WrongPassword
    "Wi-Fi подключён\0"  // WifiConnected
    "В Claude Code выполните:\0"  // RunInClaude
    "Код сопряжения\0"  // PairingCode
    "Сопряжено с\0"  // PairedWith
    "Обзор\0"  // ModeOverview
    "Лимиты\0"  // ModeLimits
    "Сессии\0"  // ModeSessions
    "Нет связи\0"  // Disconnected
    "Ожидание компьютера\0"  // WaitingComputer
    "Обновление прошивки\0"  // Updating
    "Не отключайте питание\0"  // DoNotUnplug
    "Код обновления\0"  // CodeUpdate
    "Код сброса\0"  // CodeReset
    "истекает через %s\0"  // ExpiresIn
    "НУЖНЫ ВЫ\0"  // NeedsYou
    "ЖДУТ: %u\0"  // NWaiting
    "РАБОТАЮТ: %u\0"  // NRunning
    "ВСЁ ГОТОВО\0"  // AllDone
    "ГОТОВО\0"  // Finished
    "Просит разрешение\0"  // AskedPermission
    "Задала вопрос\0"  // AskedQuestion
    "ждёт %s\0"  // WaitingFor
    "+%u работают\0"  // PlusRunning
    "простаивают: %u\0"  // NIdle
    "Сессия 5ч\0"  // Session5h
    "Неделя\0"  // Week
    "сброс %s\0"  // ResetsAt
    "через %s\0"  // InTime
    "лимиты недоступны\0"  // LimitsUnavailable
    "сегодня %s\0"  // CostToday
    "%s завершена %s назад\0"  // FinishedAgo
    "заняло %s\0"  // Took
    "ЛИМИТЫ\0"  // LimitsTitle
    "СЕССИИ · %u\0"  // SessionsTitle
    "разрешение\0"  // StPerm
    "вопрос\0"  // StQuestion
    "завершена\0"  // StDone
    "простаивает\0"  // StIdle
    "Редактирует\0"  // VerbEditing
    "Читает\0"  // VerbReading
    "Ищет\0"  // VerbSearching
    "Загружает\0"  // VerbFetching
    "Поиск в сети\0"  // VerbWebSearch
    "Агент\0"  // VerbAgent
    "Работает\0"  // VerbWorking
    "Нет активных сессий\0"  // NoSessions
    "Вс\0"  // WdSun
    "Пн\0"  // WdMon
    "Вт\0"  // WdTue
    "Ср\0"  // WdWed
    "Чт\0"  // WdThu
    "Пт\0"  // WdFri
    "Сб\0"  // WdSat
    "Настройка Wi-Fi Miblo\0"  // WebSetupTitle
    "Выберите сеть\0"  // WebChooseNetwork
    "Другая сеть (скрытая)\0"  // WebOtherNetwork
    "Имя сети\0"  // WebNetworkName
    "Пароль\0"  // WebPassword
    "Часовой пояс\0"  // WebTimezone
    "Язык\0"  // WebLanguage
    "Подключить\0"  // WebConnect
    "Подключение. Смотрите на экран устройства.\0"  // WebConnecting
    "Настройки\0"  // WebSettings
    "Режим\0"  // WebMode
    "Яркость\0"  // WebBrightness
    "Оповещения (вспышка + выделение)\0"  // WebAlerts
    "Выделение: нужны вы (с)\0"  // WebHeroPerm
    "Выделение: готово (с)\0"  // WebHeroDone
    "Напоминание каждые (мин, 0 = выкл.)\0"  // WebReminder
    "Скрытный режим (без команд и файлов)\0"  // WebDiscreet
    "Имя устройства\0"  // WebDeviceName
    "Сохранить\0"  // WebSave
    "Сохранено\0"  // WebSaved
    "Сброс к заводским настройкам\0"  // WebFactoryReset
    "Будут удалены Wi-Fi, сопряжения и настройки.\0"  // WebResetConfirm
    "Обновление прошивки\0"  // WebFirmware
    "Показать код сопряжения на устройстве\0"  // WebShowPairCode
    "Введите 4-значный код с экрана устройства\0"  // WebCodeHint
    "Код\0"  // WebCode
    "Загрузить\0"  // WebUpload
    "Готово. Устройство перезагружается.\0"  // WebUpdateOk
    "Ошибка\0"  // WebFailed
    "Неверный код\0"  // WebBadCode
    "Лимиты ещё не получены: выполните /miblo pair в Claude Code\0"  // WebLimitsHint
    "Сопряжённых компьютеров: %u\0"  // WebPairedCount
    "Версия прошивки\0";  // WebVersion

static const char kZh[] MIBLO_ROM =
    "正在连接 Wi-Fi\0"  // Connecting
    "你好!\0"  // Hello
    "用手机扫码\0"  // ScanPhone
    "或连接 Wi-Fi 网络\0"  // OrJoin
    "密码错误\0"  // WrongPassword
    "Wi-Fi 已连接\0"  // WifiConnected
    "在 Claude Code 中运行:\0"  // RunInClaude
    "配对码\0"  // PairingCode
    "已配对\0"  // PairedWith
    "概览\0"  // ModeOverview
    "用量限制\0"  // ModeLimits
    "会话\0"  // ModeSessions
    "已断开\0"  // Disconnected
    "等待电脑连接\0"  // WaitingComputer
    "正在更新固件\0"  // Updating
    "请勿断电\0"  // DoNotUnplug
    "固件更新码\0"  // CodeUpdate
    "恢复出厂码\0"  // CodeReset
    "%s 后过期\0"  // ExpiresIn
    "需要你处理\0"  // NeedsYou
    "%u 个等待中\0"  // NWaiting
    "%u 个运行中\0"  // NRunning
    "全部完成\0"  // AllDone
    "已完成\0"  // Finished
    "请求权限\0"  // AskedPermission
    "提出了问题\0"  // AskedQuestion
    "已等待 %s\0"  // WaitingFor
    "+%u 个运行中\0"  // PlusRunning
    "%u 个空闲\0"  // NIdle
    "5 小时会话\0"  // Session5h
    "本周\0"  // Week
    "%s 重置\0"  // ResetsAt
    "%s 后\0"  // InTime
    "用量限制不可用\0"  // LimitsUnavailable
    "今日 %s\0"  // CostToday
    "%s %s前完成\0"  // FinishedAgo
    "用时 %s\0"  // Took
    "用量限制\0"  // LimitsTitle
    "会话 · %u\0"  // SessionsTitle
    "权限\0"  // StPerm
    "问题\0"  // StQuestion
    "已完成\0"  // StDone
    "空闲\0"  // StIdle
    "编辑中\0"  // VerbEditing
    "读取中\0"  // VerbReading
    "搜索中\0"  // VerbSearching
    "获取中\0"  // VerbFetching
    "网页搜索\0"  // VerbWebSearch
    "子代理\0"  // VerbAgent
    "工作中\0"  // VerbWorking
    "没有活动会话\0"  // NoSessions
    "周日\0"  // WdSun
    "周一\0"  // WdMon
    "周二\0"  // WdTue
    "周三\0"  // WdWed
    "周四\0"  // WdThu
    "周五\0"  // WdFri
    "周六\0"  // WdSat
    "设置 Miblo 的 Wi-Fi\0"  // WebSetupTitle
    "选择你的网络\0"  // WebChooseNetwork
    "其他网络（隐藏）\0"  // WebOtherNetwork
    "网络名称\0"  // WebNetworkName
    "密码\0"  // WebPassword
    "时区\0"  // WebTimezone
    "语言\0"  // WebLanguage
    "连接\0"  // WebConnect
    "正在连接，请查看设备屏幕。\0"  // WebConnecting
    "设置\0"  // WebSettings
    "模式\0"  // WebMode
    "亮度\0"  // WebBrightness
    "提醒（闪烁 + 突出显示）\0"  // WebAlerts
    "突出显示：需要你处理（秒）\0"  // WebHeroPerm
    "突出显示：已完成（秒）\0"  // WebHeroDone
    "提醒间隔（分钟，0 = 关闭）\0"  // WebReminder
    "低调模式（隐藏命令和文件）\0"  // WebDiscreet
    "设备名称\0"  // WebDeviceName
    "保存\0"  // WebSave
    "已保存\0"  // WebSaved
    "恢复出厂设置\0"  // WebFactoryReset
    "这将清除 Wi-Fi、配对和设置。\0"  // WebResetConfirm
    "固件更新\0"  // WebFirmware
    "在设备上显示配对码\0"  // WebShowPairCode
    "输入设备屏幕上显示的 4 位代码\0"  // WebCodeHint
    "代码\0"  // WebCode
    "上传\0"  // WebUpload
    "完成，设备正在重启。\0"  // WebUpdateOk
    "失败\0"  // WebFailed
    "代码错误\0"  // WebBadCode
    "尚未收到用量限制：请在 Claude Code 中运行 /miblo pair\0"  // WebLimitsHint
    "已配对电脑：%u\0"  // WebPairedCount
    "固件版本\0";  // WebVersion

const char* const kLangTables[] = {kEn, kPtBR, kPtPT, kEs, kFr, kIt, kDe, kRu, kZh};

}  // namespace miblo
```

`firmware/lib/miblo_core/src/miblo_activity.h`:

```cpp
#pragma once
#include <stddef.h>

#include "miblo_i18n.h"
#include "miblo_snapshot.h"

namespace miblo {

// Verbo localizado para as ferramentas conhecidas do Claude Code. Bash e desconhecidas → false
// (o nome da ferramenta é mostrado como veio).
bool toolVerb(const char* tool, S& out);

// Atividade de uma sessão rodando:
//   verbo conhecido → "Editando Header.tsx" (ou só "Editando" sem det / em modo discreto)
//   outras          → "Bash · npm test"    (ou só "Bash")
//   sem ferramenta  → "Trabalhando"
void activityText(Lang lang, const char* tool, const char* det, bool discreet, char* out, size_t cap);

// Linha de estado de uma sessão para as listas:
//   perm → "permissão · Bash", question → "pergunta", done → "terminou", idle → "ociosa",
//   running → activityText(...).
void sessionLine(Lang lang, const SessionRow& row, bool discreet, char* out, size_t cap);

}  // namespace miblo
```

`firmware/lib/miblo_core/src/miblo_activity.cpp`:

```cpp
#include "miblo_activity.h"

#include <stdio.h>
#include <string.h>

namespace miblo {

bool toolVerb(const char* tool, S& out) {
  static const struct {
    const char* tool;
    S verb;
  } kMap[] = {
      {"Edit", S::VerbEditing},      {"MultiEdit", S::VerbEditing}, {"Write", S::VerbEditing},
      {"NotebookEdit", S::VerbEditing}, {"Read", S::VerbReading},  {"Grep", S::VerbSearching},
      {"Glob", S::VerbSearching},    {"WebFetch", S::VerbFetching}, {"WebSearch", S::VerbWebSearch},
      {"Task", S::VerbAgent},        {"Agent", S::VerbAgent},
  };
  if (!tool) return false;
  for (const auto& m : kMap) {
    if (strcmp(tool, m.tool) == 0) {
      out = m.verb;
      return true;
    }
  }
  return false;
}

void activityText(Lang lang, const char* tool, const char* det, bool discreet, char* out, size_t cap) {
  const bool hasDet = !discreet && det && det[0];
  if (!tool || !tool[0]) {
    tr(lang, S::VerbWorking, out, cap);
    return;
  }
  S verb;
  if (toolVerb(tool, verb)) {
    char v[48];
    tr(lang, verb, v, sizeof(v));
    if (hasDet) snprintf(out, cap, "%s %s", v, det);
    else snprintf(out, cap, "%s", v);
    return;
  }
  if (hasDet) snprintf(out, cap, "%s \xC2\xB7 %s", tool, det);
  else snprintf(out, cap, "%s", tool);
}

void sessionLine(Lang lang, const SessionRow& row, bool discreet, char* out, size_t cap) {
  switch (row.st) {
    case SessionState::Perm: {
      char label[48];
      tr(lang, S::StPerm, label, sizeof(label));
      if (row.tool[0]) snprintf(out, cap, "%s \xC2\xB7 %s", label, row.tool);
      else snprintf(out, cap, "%s", label);
      return;
    }
    case SessionState::Question:
      tr(lang, S::StQuestion, out, cap);
      return;
    case SessionState::Done:
      tr(lang, S::StDone, out, cap);
      return;
    case SessionState::Idle:
      tr(lang, S::StIdle, out, cap);
      return;
    case SessionState::Running:
      activityText(lang, row.tool, row.det, discreet, out, cap);
      return;
  }
}

}  // namespace miblo
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd firmware && .venv/bin/pio test -e native -f test_i18n`
Expected: PASS — `7 test cases: 7 succeeded`.

- [ ] **Step 5: Commit**

```bash
git add firmware/lib/miblo_core/src/miblo_rom.h firmware/lib/miblo_core/src/miblo_i18n.h firmware/lib/miblo_core/src/miblo_i18n.cpp firmware/lib/miblo_core/src/miblo_strings.cpp firmware/lib/miblo_core/src/miblo_activity.h firmware/lib/miblo_core/src/miblo_activity.cpp firmware/test/test_i18n/test_main.cpp
git commit -m "feat(firmware): i18n tables for 9 languages, Accept-Language negotiation and tool verbs"
```

---

### Task 4: Lógica da Visão geral — classificação, herói, duração da resposta, paginação e cache de regiões

**Files:**
- Create: `firmware/lib/miblo_core/src/miblo_overview.h`, `firmware/lib/miblo_core/src/miblo_overview.cpp`
- Test: `firmware/test/test_overview/test_main.cpp`

**Interfaces:**
- Consumes: `Snapshot`, `SessionRow`, `findSession` (Task 2).
- Produces: `enum class OverviewKind {Attention, Working, Idle}`; `struct StateCounts {pending, running, done, idle}`; `stateRank(SessionState) → uint8_t`; `countStates(const Snapshot&)`; `classifyOverview(const Snapshot&)`; `selectHero(const Snapshot&, bool includeDone) → int`; `lastFinished(const Snapshot&) → int`; `class RunTracker { void observe(const Snapshot&); bool stats(const char* sid, uint32_t& durationSec) const; void clear(); }`; `class Pager { Pager(uint8_t perPage, uint32_t periodMs = 5000); uint8_t pageCount(uint16_t) const; uint8_t update(uint16_t itemCount, uint32_t nowMs); uint8_t page() const; uint8_t perPage() const; }`; `kHashSeed`, `hashStr(uint32_t, const char*)`, `hashInt(uint32_t, uint32_t)`; `class RegionCache { static constexpr uint8_t kRegions = 16; bool changed(uint8_t region, uint32_t hash); void invalidate(); }`.

- [ ] **Step 1: Write the failing test** — `firmware/test/test_overview/test_main.cpp`

```cpp
#include <string.h>
#include <unity.h>

#include "miblo_overview.h"

using namespace miblo;

void setUp() {}
void tearDown() {}

static Snapshot snap;

static void reset() { memset(&snap, 0, sizeof(snap)); }

static void add(const char* id, SessionState st, uint32_t since) {
  SessionRow& r = snap.sessions[snap.count++];
  memset(&r, 0, sizeof(r));
  strcpy(r.id, id);
  strcpy(r.name, id);
  r.st = st;
  r.since = since;
  r.ctx = -1;
  r.tok = -1;
}

static void test_classify_and_counts() {
  reset();
  TEST_ASSERT_EQUAL(OverviewKind::Idle, classifyOverview(snap));
  add("a", SessionState::Idle, 1);
  add("b", SessionState::Done, 2);
  TEST_ASSERT_EQUAL(OverviewKind::Idle, classifyOverview(snap));
  add("c", SessionState::Running, 3);
  TEST_ASSERT_EQUAL(OverviewKind::Working, classifyOverview(snap));
  add("d", SessionState::Question, 4);
  TEST_ASSERT_EQUAL(OverviewKind::Attention, classifyOverview(snap));
  StateCounts c = countStates(snap);
  TEST_ASSERT_EQUAL_UINT8(1, c.pending);
  TEST_ASSERT_EQUAL_UINT8(1, c.running);
  TEST_ASSERT_EQUAL_UINT8(1, c.done);
  TEST_ASSERT_EQUAL_UINT8(1, c.idle);
}

static void test_hero_priority_and_tie_break() {
  reset();
  add("run", SessionState::Running, 1);
  add("done", SessionState::Done, 2);
  TEST_ASSERT_EQUAL_INT(-1, selectHero(snap, false));
  TEST_ASSERT_EQUAL_INT(1, selectHero(snap, true));
  add("q", SessionState::Question, 3);
  TEST_ASSERT_EQUAL_INT(2, selectHero(snap, true));
  add("p-new", SessionState::Perm, 50);
  add("p-old", SessionState::Perm, 10);
  TEST_ASSERT_EQUAL_INT(4, selectHero(snap, false));  // quem espera há mais tempo
  TEST_ASSERT_EQUAL_INT(4, selectHero(snap, true));
}

static void test_last_finished() {
  reset();
  TEST_ASSERT_EQUAL_INT(-1, lastFinished(snap));
  add("a", SessionState::Done, 10);
  add("b", SessionState::Done, 30);
  add("c", SessionState::Running, 40);
  TEST_ASSERT_EQUAL_INT(1, lastFinished(snap));
}

static void test_run_tracker_measures_a_full_response() {
  RunTracker rt;
  uint32_t dur;
  reset();
  add("s1", SessionState::Idle, 100);
  rt.observe(snap);
  snap.sessions[0].st = SessionState::Running;
  snap.sessions[0].since = 200;
  rt.observe(snap);
  snap.sessions[0].st = SessionState::Perm;  // pendência no meio não reinicia a contagem
  snap.sessions[0].since = 230;
  rt.observe(snap);
  TEST_ASSERT_FALSE(rt.stats("s1", dur));
  snap.sessions[0].st = SessionState::Done;
  snap.sessions[0].since = 458;
  rt.observe(snap);
  TEST_ASSERT_TRUE(rt.stats("s1", dur));
  TEST_ASSERT_EQUAL_UINT32(258, dur);
  snap.sessions[0].st = SessionState::Idle;  // novo ciclo: estatística some
  rt.observe(snap);
  TEST_ASSERT_FALSE(rt.stats("s1", dur));
}

static void test_run_tracker_unknown_start_and_forgetting() {
  RunTracker rt;
  uint32_t dur;
  reset();
  add("late", SessionState::Done, 500);  // já chegou terminado: sem estatística
  add("run", SessionState::Running, 100);
  rt.observe(snap);
  TEST_ASSERT_FALSE(rt.stats("late", dur));
  snap.sessions[1].st = SessionState::Done;
  snap.sessions[1].since = 160;
  rt.observe(snap);
  TEST_ASSERT_TRUE(rt.stats("run", dur));
  TEST_ASSERT_EQUAL_UINT32(60, dur);
  reset();  // sessão sumiu → esquecida
  rt.observe(snap);
  TEST_ASSERT_FALSE(rt.stats("run", dur));
}

static void test_pager_rotates_every_period() {
  Pager p(4, 5000);
  TEST_ASSERT_EQUAL_UINT8(1, p.pageCount(0));
  TEST_ASSERT_EQUAL_UINT8(1, p.pageCount(4));
  TEST_ASSERT_EQUAL_UINT8(3, p.pageCount(9));
  TEST_ASSERT_EQUAL_UINT8(0, p.update(9, 1000));
  TEST_ASSERT_EQUAL_UINT8(0, p.update(9, 5999));
  TEST_ASSERT_EQUAL_UINT8(1, p.update(9, 6000));
  TEST_ASSERT_EQUAL_UINT8(2, p.update(9, 11000));
  TEST_ASSERT_EQUAL_UINT8(0, p.update(9, 16000));
  TEST_ASSERT_EQUAL_UINT8(1, p.update(9, 21000));
  TEST_ASSERT_EQUAL_UINT8(0, p.update(3, 21001));  // lista encolheu: volta para a página 0
  TEST_ASSERT_EQUAL_UINT8(0, p.update(3, 60000));
}

static void test_region_cache() {
  RegionCache rc;
  uint32_t h1 = hashStr(kHashSeed, "62%");
  uint32_t h2 = hashStr(kHashSeed, "63%");
  TEST_ASSERT_TRUE(rc.changed(0, h1));
  TEST_ASSERT_FALSE(rc.changed(0, h1));
  TEST_ASSERT_TRUE(rc.changed(0, h2));
  TEST_ASSERT_TRUE(rc.changed(1, h2));
  rc.invalidate();
  TEST_ASSERT_TRUE(rc.changed(0, h2));
  TEST_ASSERT_TRUE(rc.changed(200, h2));  // fora do intervalo: sempre desenha
  TEST_ASSERT_NOT_EQUAL(hashStr(hashStr(kHashSeed, "ab"), "c"), hashStr(hashStr(kHashSeed, "a"), "bc"));
  TEST_ASSERT_NOT_EQUAL(hashInt(kHashSeed, 1), hashInt(kHashSeed, 2));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_classify_and_counts);
  RUN_TEST(test_hero_priority_and_tie_break);
  RUN_TEST(test_last_finished);
  RUN_TEST(test_run_tracker_measures_a_full_response);
  RUN_TEST(test_run_tracker_unknown_start_and_forgetting);
  RUN_TEST(test_pager_rotates_every_period);
  RUN_TEST(test_region_cache);
  return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd firmware && .venv/bin/pio test -e native -f test_overview`
Expected: FAIL — `miblo_overview.h: No such file or directory`.

- [ ] **Step 3: Implement** — `firmware/lib/miblo_core/src/miblo_overview.h`

```cpp
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "miblo_snapshot.h"

namespace miblo {

// ---- Visão geral adaptativa (spec §4.1) ----
enum class OverviewKind : uint8_t { Attention, Working, Idle };

struct StateCounts {
  uint8_t pending;  // perm + question
  uint8_t running;
  uint8_t done;
  uint8_t idle;
};

// perm 0, question 1, done 2, running 3, idle 4 (mesma ordem do bridge).
uint8_t stateRank(SessionState st);
StateCounts countStates(const Snapshot& s);
// Attention se há pendência; Working se há sessão rodando; senão Idle.
OverviewKind classifyOverview(const Snapshot& s);
// Herói: permissão > pergunta > terminou (se includeDone); empate → menor `since`. -1 se nenhum.
int selectHero(const Snapshot& s, bool includeDone);
// Sessão `done` mais recente (maior `since`), ou -1.
int lastFinished(const Snapshot& s);

// ---- Duração da última resposta (herói "Terminou") ----
// O snapshot só traz `since` do estado atual; o gadget memoriza quando cada sessão começou a
// trabalhar para calcular quanto a resposta durou quando ela chega em `done`.
class RunTracker {
 public:
  void observe(const Snapshot& s);
  // true se a sessão terminou uma resposta observada do início ao fim.
  bool stats(const char* sid, uint32_t& durationSec) const;
  void clear();

 private:
  struct Entry {
    char id[9];
    bool used;
    bool active;
    bool finished;
    uint32_t start;
    uint32_t duration;
  };
  Entry entries_[kMaxSessions] = {};
  Entry* find(const char* id);
  const Entry* find(const char* id) const;
};

// ---- Paginação automática (lista a cada 5 s) ----
class Pager {
 public:
  explicit Pager(uint8_t perPage, uint32_t periodMs = 5000) : perPage_(perPage), periodMs_(periodMs) {}
  uint8_t pageCount(uint16_t itemCount) const;
  // Avança a página conforme o tempo e devolve a página atual (0-based).
  uint8_t update(uint16_t itemCount, uint32_t nowMs);
  uint8_t page() const { return page_; }
  uint8_t perPage() const { return perPage_; }

 private:
  uint8_t perPage_;
  uint32_t periodMs_;
  uint8_t page_ = 0;
  bool started_ = false;
  uint32_t lastFlipMs_ = 0;
};

// ---- Cache de regiões da tela (redesenhar só o que mudou) ----
constexpr uint32_t kHashSeed = 2166136261u;
uint32_t hashStr(uint32_t h, const char* s);
uint32_t hashInt(uint32_t h, uint32_t v);

class RegionCache {
 public:
  static constexpr uint8_t kRegions = 16;
  // true (e memoriza) se o conteúdo da região mudou desde o último desenho.
  bool changed(uint8_t region, uint32_t hash);
  void invalidate();

 private:
  uint32_t hash_[kRegions] = {};
  bool valid_[kRegions] = {};
};

}  // namespace miblo
```

`firmware/lib/miblo_core/src/miblo_overview.cpp`:

```cpp
#include "miblo_overview.h"

#include <string.h>

namespace miblo {

uint8_t stateRank(SessionState st) {
  switch (st) {
    case SessionState::Perm: return 0;
    case SessionState::Question: return 1;
    case SessionState::Done: return 2;
    case SessionState::Running: return 3;
    case SessionState::Idle: return 4;
  }
  return 4;
}

StateCounts countStates(const Snapshot& s) {
  StateCounts c{0, 0, 0, 0};
  for (int i = 0; i < s.count; i++) {
    switch (s.sessions[i].st) {
      case SessionState::Perm:
      case SessionState::Question: c.pending++; break;
      case SessionState::Running: c.running++; break;
      case SessionState::Done: c.done++; break;
      case SessionState::Idle: c.idle++; break;
    }
  }
  return c;
}

OverviewKind classifyOverview(const Snapshot& s) {
  StateCounts c = countStates(s);
  if (c.pending > 0) return OverviewKind::Attention;
  if (c.running > 0) return OverviewKind::Working;
  return OverviewKind::Idle;
}

int selectHero(const Snapshot& s, bool includeDone) {
  int best = -1;
  for (int i = 0; i < s.count; i++) {
    const SessionRow& r = s.sessions[i];
    uint8_t rank = stateRank(r.st);
    if (rank > (includeDone ? 2 : 1)) continue;
    if (best < 0) {
      best = i;
      continue;
    }
    const SessionRow& b = s.sessions[best];
    uint8_t bestRank = stateRank(b.st);
    if (rank < bestRank || (rank == bestRank && r.since < b.since)) best = i;
  }
  return best;
}

int lastFinished(const Snapshot& s) {
  int best = -1;
  for (int i = 0; i < s.count; i++) {
    if (s.sessions[i].st != SessionState::Done) continue;
    if (best < 0 || s.sessions[i].since > s.sessions[best].since) best = i;
  }
  return best;
}

RunTracker::Entry* RunTracker::find(const char* id) {
  for (auto& e : entries_) {
    if (e.used && strcmp(e.id, id) == 0) return &e;
  }
  return nullptr;
}

const RunTracker::Entry* RunTracker::find(const char* id) const {
  for (const auto& e : entries_) {
    if (e.used && strcmp(e.id, id) == 0) return &e;
  }
  return nullptr;
}

void RunTracker::clear() {
  for (auto& e : entries_) e = Entry{};
}

void RunTracker::observe(const Snapshot& s) {
  // esquece sessões que sumiram do snapshot
  for (auto& e : entries_) {
    if (e.used && findSession(s, e.id) < 0) e = Entry{};
  }
  for (int i = 0; i < s.count; i++) {
    const SessionRow& r = s.sessions[i];
    Entry* e = find(r.id);
    if (!e) {
      for (auto& slot : entries_) {
        if (!slot.used) {
          slot = Entry{};
          slot.used = true;
          strncpy(slot.id, r.id, sizeof(slot.id) - 1);
          e = &slot;
          break;
        }
      }
      if (!e) continue;
    }
    switch (r.st) {
      case SessionState::Running:
      case SessionState::Perm:
      case SessionState::Question:
        if (!e->active) {
          e->active = true;
          e->finished = false;
          e->start = r.since;
        }
        break;
      case SessionState::Done:
        if (e->active) {
          e->active = false;
          e->finished = true;
          e->duration = r.since >= e->start ? r.since - e->start : 0;
        }
        break;
      case SessionState::Idle:
        e->active = false;
        e->finished = false;
        break;
    }
  }
}

bool RunTracker::stats(const char* sid, uint32_t& durationSec) const {
  const Entry* e = find(sid);
  if (!e || !e->finished) return false;
  durationSec = e->duration;
  return true;
}

uint8_t Pager::pageCount(uint16_t itemCount) const {
  if (perPage_ == 0 || itemCount == 0) return 1;
  return (uint8_t)((itemCount + perPage_ - 1) / perPage_);
}

uint8_t Pager::update(uint16_t itemCount, uint32_t nowMs) {
  uint8_t pages = pageCount(itemCount);
  if (!started_) {
    started_ = true;
    lastFlipMs_ = nowMs;
  }
  if (pages <= 1) {
    page_ = 0;
    lastFlipMs_ = nowMs;
    return 0;
  }
  if (page_ >= pages) page_ = 0;
  if (nowMs - lastFlipMs_ >= periodMs_) {
    page_ = (uint8_t)((page_ + 1) % pages);
    lastFlipMs_ = nowMs;
  }
  return page_;
}

uint32_t hashStr(uint32_t h, const char* s) {
  if (!s) return hashInt(h, 0);
  while (*s) {
    h ^= (uint8_t)*s++;
    h *= 16777619u;
  }
  h ^= 0xFF;  // separador, para "ab"+"c" ≠ "a"+"bc"
  h *= 16777619u;
  return h;
}

uint32_t hashInt(uint32_t h, uint32_t v) {
  for (int i = 0; i < 4; i++) {
    h ^= (v >> (i * 8)) & 0xFF;
    h *= 16777619u;
  }
  return h;
}

bool RegionCache::changed(uint8_t region, uint32_t hash) {
  if (region >= kRegions) return true;
  if (valid_[region] && hash_[region] == hash) return false;
  valid_[region] = true;
  hash_[region] = hash;
  return true;
}

void RegionCache::invalidate() {
  for (auto& v : valid_) v = false;
}

}  // namespace miblo
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd firmware && .venv/bin/pio test -e native -f test_overview`
Expected: PASS — `7 test cases: 7 succeeded`.

- [ ] **Step 5: Commit**

```bash
git add firmware/lib/miblo_core/src/miblo_overview.h firmware/lib/miblo_core/src/miblo_overview.cpp firmware/test/test_overview/test_main.cpp
git commit -m "feat(firmware): overview classification, hero priority, run tracker, pager and region cache"
```

---
### Task 5: `AlertSequencer` — fila de alertas, flash → herói → resumo, lembrete

**Files:**
- Create: `firmware/lib/miblo_core/src/miblo_alerts.h`, `firmware/lib/miblo_core/src/miblo_alerts.cpp`
- Test: `firmware/test/test_alerts/test_main.cpp`

**Interfaces:**
- Consumes: `Snapshot`, `AlertItem`, `findSession` (Task 2); `countStates`, `selectHero` (Task 4).
- Produces: `struct AlertTiming {enabled=true, flashMs=1500, heroPermMs=10000, heroDoneMs=5000, reminderMs=120000}`; `enum class AlertPhase {None, Flash, Hero}`; `struct AlertView {phase, kind, sid[9], phaseStartMs}`; `class AlertSequencer { void setTiming(const AlertTiming&); const AlertTiming& timing() const; void ingest(const Snapshot&, uint32_t nowMs); const AlertView& update(const Snapshot&, uint32_t nowMs); uint32_t lastSeenId() const; uint8_t queued() const; void clear(); }`.

Regras (spec §4.2 e §5.3): dedupe pelo maior `id` já visto (zerado se o `seq` do snapshot voltar — bridge reiniciado); fila ordenada por permissão < pergunta < terminou, depois `since`, depois `id`; um alerta cuja sessão já saiu do estado (usuário respondeu) termina na hora ou é descartado da fila; enquanto houver pendência, repete flash + herói a cada `reminderMs` (contado do fim do último herói âmbar ou de quando a pendência foi vista).

- [ ] **Step 1: Write the failing test** — `firmware/test/test_alerts/test_main.cpp`

```cpp
#include <string.h>
#include <unity.h>

#include "miblo_alerts.h"

using namespace miblo;

void setUp() {}
void tearDown() {}

static Snapshot snap;

static void reset(uint32_t seq = 1) {
  memset(&snap, 0, sizeof(snap));
  snap.seq = seq;
}

static void session(const char* id, SessionState st, uint32_t since = 100) {
  SessionRow& r = snap.sessions[snap.count++];
  memset(&r, 0, sizeof(r));
  strcpy(r.id, id);
  r.st = st;
  r.since = since;
  r.ctx = -1;
  r.tok = -1;
}

static void alert(uint32_t id, AlertKind kind, const char* sid) {
  AlertItem& a = snap.alerts[snap.alertCount++];
  a.id = id;
  a.kind = kind;
  strcpy(a.sid, sid);
}

static void test_flash_then_hero_then_summary_for_permission() {
  AlertSequencer q;
  reset();
  session("a", SessionState::Perm);
  alert(1, AlertKind::Perm, "a");
  q.ingest(snap, 0);
  const AlertView* v = &q.update(snap, 0);
  TEST_ASSERT_EQUAL(AlertPhase::Flash, v->phase);
  TEST_ASSERT_EQUAL(AlertKind::Perm, v->kind);
  TEST_ASSERT_EQUAL_STRING("a", v->sid);
  TEST_ASSERT_EQUAL(AlertPhase::Flash, q.update(snap, 1499).phase);
  TEST_ASSERT_EQUAL(AlertPhase::Hero, q.update(snap, 1500).phase);
  TEST_ASSERT_EQUAL(AlertPhase::Hero, q.update(snap, 11499).phase);
  TEST_ASSERT_EQUAL(AlertPhase::None, q.update(snap, 11500).phase);
}

static void test_done_hero_is_shorter() {
  AlertSequencer q;
  reset();
  session("d", SessionState::Done);
  alert(1, AlertKind::Done, "d");
  q.ingest(snap, 0);
  q.update(snap, 0);
  TEST_ASSERT_EQUAL(AlertPhase::Hero, q.update(snap, 1500).phase);
  TEST_ASSERT_EQUAL(AlertPhase::None, q.update(snap, 6500).phase);
}

static void test_dedupe_by_id() {
  AlertSequencer q;
  reset();
  session("d", SessionState::Done);
  alert(7, AlertKind::Done, "d");
  q.ingest(snap, 0);
  q.ingest(snap, 10);  // o mesmo alerta chega em snapshots seguidos
  TEST_ASSERT_EQUAL_UINT8(1, q.queued());
  TEST_ASSERT_EQUAL_UINT32(7, q.lastSeenId());
  q.update(snap, 0);
  q.update(snap, 1500);
  q.update(snap, 6500);
  q.ingest(snap, 7000);
  TEST_ASSERT_EQUAL(AlertPhase::None, q.update(snap, 7000).phase);
}

static void test_amber_before_blue_and_perm_before_question() {
  AlertSequencer q;
  reset();
  session("done", SessionState::Done, 10);
  session("ask", SessionState::Question, 20);
  session("perm", SessionState::Perm, 30);
  alert(1, AlertKind::Done, "done");
  alert(2, AlertKind::Question, "ask");
  alert(3, AlertKind::Perm, "perm");
  q.ingest(snap, 0);
  TEST_ASSERT_EQUAL_STRING("perm", q.update(snap, 0).sid);
  q.update(snap, 1500);
  TEST_ASSERT_EQUAL_STRING("ask", q.update(snap, 11500).sid);
  q.update(snap, 13000);
  TEST_ASSERT_EQUAL_STRING("done", q.update(snap, 23000).sid);
}

static void test_answered_alert_ends_early_and_stale_queue_is_dropped() {
  AlertSequencer q;
  reset();
  session("a", SessionState::Perm);
  session("b", SessionState::Question);
  alert(1, AlertKind::Perm, "a");
  alert(2, AlertKind::Question, "b");
  q.ingest(snap, 0);
  q.update(snap, 0);
  q.update(snap, 1500);
  snap.sessions[0].st = SessionState::Running;  // usuário aprovou
  snap.sessions[1].st = SessionState::Running;  // e respondeu a outra
  TEST_ASSERT_EQUAL(AlertPhase::None, q.update(snap, 2000).phase);
  TEST_ASSERT_EQUAL_UINT8(0, q.queued());
}

static void test_reminder_every_interval_while_pending() {
  AlertSequencer q;
  reset();
  session("a", SessionState::Perm);
  alert(1, AlertKind::Perm, "a");
  q.ingest(snap, 0);
  q.update(snap, 0);
  q.update(snap, 1500);
  q.update(snap, 11500);  // herói terminou em 11,5 s
  TEST_ASSERT_EQUAL(AlertPhase::None, q.update(snap, 131499).phase);
  const AlertView& v = q.update(snap, 131500);
  TEST_ASSERT_EQUAL(AlertPhase::Flash, v.phase);
  TEST_ASSERT_EQUAL(AlertKind::Perm, v.kind);
  TEST_ASSERT_EQUAL_STRING("a", v.sid);
}

static void test_reminder_for_pending_seen_without_alert_and_can_be_disabled() {
  AlertSequencer q;
  reset();
  session("x", SessionState::Question);
  q.ingest(snap, 0);  // o gadget ligou com a pendência já em andamento (alerta expirou no bridge)
  TEST_ASSERT_EQUAL(AlertPhase::None, q.update(snap, 1000).phase);
  TEST_ASSERT_EQUAL(AlertKind::Question, q.update(snap, 121000).kind);

  AlertSequencer off;
  AlertTiming t;
  t.reminderMs = 0;
  off.setTiming(t);
  off.ingest(snap, 0);
  off.update(snap, 1000);
  TEST_ASSERT_EQUAL(AlertPhase::None, off.update(snap, 999999).phase);
}

static void test_bridge_restart_resets_dedupe() {
  AlertSequencer q;
  reset(500);
  session("a", SessionState::Done);
  alert(9, AlertKind::Done, "a");
  q.ingest(snap, 0);
  q.update(snap, 0);
  q.update(snap, 1500);
  q.update(snap, 6500);
  reset(1);  // seq voltou: bridge novo, ids recomeçam
  session("b", SessionState::Done);
  alert(1, AlertKind::Done, "b");
  q.ingest(snap, 7000);
  TEST_ASSERT_EQUAL(AlertPhase::Flash, q.update(snap, 7000).phase);
}

static void test_disabled_alerts_show_nothing() {
  AlertSequencer q;
  AlertTiming t;
  t.enabled = false;
  q.setTiming(t);
  reset();
  session("a", SessionState::Perm);
  alert(1, AlertKind::Perm, "a");
  q.ingest(snap, 0);
  TEST_ASSERT_EQUAL(AlertPhase::None, q.update(snap, 0).phase);
  TEST_ASSERT_EQUAL(AlertPhase::None, q.update(snap, 500000).phase);
  TEST_ASSERT_EQUAL_UINT32(1, q.lastSeenId());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_flash_then_hero_then_summary_for_permission);
  RUN_TEST(test_done_hero_is_shorter);
  RUN_TEST(test_dedupe_by_id);
  RUN_TEST(test_amber_before_blue_and_perm_before_question);
  RUN_TEST(test_answered_alert_ends_early_and_stale_queue_is_dropped);
  RUN_TEST(test_reminder_every_interval_while_pending);
  RUN_TEST(test_reminder_for_pending_seen_without_alert_and_can_be_disabled);
  RUN_TEST(test_bridge_restart_resets_dedupe);
  RUN_TEST(test_disabled_alerts_show_nothing);
  return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd firmware && .venv/bin/pio test -e native -f test_alerts`
Expected: FAIL — `miblo_alerts.h: No such file or directory`.

- [ ] **Step 3: Implement** — `firmware/lib/miblo_core/src/miblo_alerts.h`

```cpp
#pragma once
#include <stdint.h>

#include "miblo_snapshot.h"

namespace miblo {

// Tempos da sequência de alerta (spec §4.2). Todos configuráveis pela página/API.
struct AlertTiming {
  bool enabled = true;
  uint32_t flashMs = 1500;
  uint32_t heroPermMs = 10000;   // herói de "precisa de você" (permissão/pergunta)
  uint32_t heroDoneMs = 5000;    // herói de "terminou"
  uint32_t reminderMs = 120000;  // 0 = sem lembrete
};

enum class AlertPhase : uint8_t { None, Flash, Hero };

struct AlertView {
  AlertPhase phase;
  AlertKind kind;
  char sid[9];
  uint32_t phaseStartMs;
};

// Fila de alertas: deduplica pelo `id` (maior id já visto), âmbar antes de azul, e a cada
// `reminderMs` repete flash + herói enquanto houver sessão pendente.
class AlertSequencer {
 public:
  void setTiming(const AlertTiming& t);
  const AlertTiming& timing() const { return t_; }
  // A cada snapshot aceito.
  void ingest(const Snapshot& s, uint32_t nowMs);
  // A cada volta do loop: avança as fases e devolve o que deve estar na tela.
  const AlertView& update(const Snapshot& s, uint32_t nowMs);
  uint32_t lastSeenId() const { return maxId_; }
  uint8_t queued() const { return qn_; }
  void clear();

 private:
  AlertTiming t_;
  AlertItem queue_[kMaxAlerts] = {};
  uint8_t qn_ = 0;
  uint32_t maxId_ = 0;
  uint32_t lastSeq_ = 0;
  bool haveSeq_ = false;
  AlertView view_ = {AlertPhase::None, AlertKind::Done, {0}, 0};
  bool amberShown_ = false;
  uint32_t lastAmberEndMs_ = 0;
  bool pendingObserved_ = false;
  uint32_t pendingSinceMs_ = 0;

  static bool stillValid(const Snapshot& s, AlertKind kind, const char* sid);
  void start(AlertKind kind, const char* sid, uint32_t nowMs);
  void finish(uint32_t nowMs);
  void sortQueue(const Snapshot& s);
};

}  // namespace miblo
```

`firmware/lib/miblo_core/src/miblo_alerts.cpp`:

```cpp
#include "miblo_alerts.h"

#include <string.h>

#include "miblo_overview.h"

namespace miblo {

static bool isAmber(AlertKind k) { return k == AlertKind::Perm || k == AlertKind::Question; }

static uint8_t kindRank(AlertKind k) {
  switch (k) {
    case AlertKind::Perm: return 0;
    case AlertKind::Question: return 1;
    case AlertKind::Done: return 2;
  }
  return 2;
}

void AlertSequencer::setTiming(const AlertTiming& t) {
  t_ = t;
  if (!t_.enabled) clear();
}

void AlertSequencer::clear() {
  qn_ = 0;
  view_.phase = AlertPhase::None;
}

bool AlertSequencer::stillValid(const Snapshot& s, AlertKind kind, const char* sid) {
  int i = findSession(s, sid);
  if (i < 0) return false;
  SessionState st = s.sessions[i].st;
  switch (kind) {
    case AlertKind::Perm: return st == SessionState::Perm;
    case AlertKind::Question: return st == SessionState::Question;
    case AlertKind::Done: return st == SessionState::Done;
  }
  return false;
}

void AlertSequencer::sortQueue(const Snapshot& s) {
  auto since = [&](const AlertItem& a) -> uint32_t {
    int i = findSession(s, a.sid);
    return i < 0 ? UINT32_MAX : s.sessions[i].since;
  };
  // inserção: fila pequena (≤ 8)
  for (int i = 1; i < qn_; i++) {
    AlertItem cur = queue_[i];
    int j = i - 1;
    while (j >= 0) {
      const AlertItem& p = queue_[j];
      bool after = kindRank(p.kind) > kindRank(cur.kind) ||
                   (kindRank(p.kind) == kindRank(cur.kind) &&
                    (since(p) > since(cur) || (since(p) == since(cur) && p.id > cur.id)));
      if (!after) break;
      queue_[j + 1] = queue_[j];
      j--;
    }
    queue_[j + 1] = cur;
  }
}

void AlertSequencer::ingest(const Snapshot& s, uint32_t nowMs) {
  (void)nowMs;
  if (haveSeq_ && s.seq < lastSeq_) maxId_ = 0;  // o bridge reiniciou: ids recomeçam do 1
  haveSeq_ = true;
  lastSeq_ = s.seq;
  for (int i = 0; i < s.alertCount; i++) {
    const AlertItem& a = s.alerts[i];
    if (a.id <= maxId_) continue;
    maxId_ = a.id;
    if (!t_.enabled || qn_ >= kMaxAlerts) continue;
    queue_[qn_++] = a;
  }
  sortQueue(s);
}

void AlertSequencer::start(AlertKind kind, const char* sid, uint32_t nowMs) {
  view_.phase = AlertPhase::Flash;
  view_.kind = kind;
  strncpy(view_.sid, sid, sizeof(view_.sid) - 1);
  view_.sid[sizeof(view_.sid) - 1] = 0;
  view_.phaseStartMs = nowMs;
}

void AlertSequencer::finish(uint32_t nowMs) {
  if (isAmber(view_.kind)) {
    amberShown_ = true;
    lastAmberEndMs_ = nowMs;
  }
  view_.phase = AlertPhase::None;
}

const AlertView& AlertSequencer::update(const Snapshot& s, uint32_t nowMs) {
  bool pending = countStates(s).pending > 0;
  if (pending && !pendingObserved_) {
    pendingObserved_ = true;
    pendingSinceMs_ = nowMs;
  } else if (!pending) {
    pendingObserved_ = false;
  }
  if (!t_.enabled) {
    view_.phase = AlertPhase::None;
    return view_;
  }

  if (view_.phase != AlertPhase::None) {
    uint32_t elapsed = nowMs - view_.phaseStartMs;
    if (!stillValid(s, view_.kind, view_.sid)) {
      view_.phase = AlertPhase::None;  // respondida/dispensada pelo uso
    } else if (view_.phase == AlertPhase::Flash && elapsed >= t_.flashMs) {
      view_.phase = AlertPhase::Hero;
      view_.phaseStartMs = nowMs;
    } else if (view_.phase == AlertPhase::Hero &&
               elapsed >= (isAmber(view_.kind) ? t_.heroPermMs : t_.heroDoneMs)) {
      finish(nowMs);
    }
  }

  if (view_.phase == AlertPhase::None) {
    while (qn_ > 0) {
      AlertItem next = queue_[0];
      for (int i = 1; i < qn_; i++) queue_[i - 1] = queue_[i];
      qn_--;
      if (stillValid(s, next.kind, next.sid)) {
        start(next.kind, next.sid, nowMs);
        return view_;
      }
    }
    if (t_.reminderMs > 0 && pendingObserved_) {
      uint32_t base = pendingSinceMs_;
      if (amberShown_ && (int32_t)(lastAmberEndMs_ - base) > 0) base = lastAmberEndMs_;
      if (nowMs - base >= t_.reminderMs) {
        int h = selectHero(s, false);
        if (h >= 0) {
          const SessionRow& r = s.sessions[h];
          start(r.st == SessionState::Perm ? AlertKind::Perm : AlertKind::Question, r.id, nowMs);
        }
      }
    }
  }
  return view_;
}

}  // namespace miblo
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd firmware && .venv/bin/pio test -e native -f test_alerts`
Expected: PASS — `9 test cases: 9 succeeded`.

- [ ] **Step 5: Commit**

```bash
git add firmware/lib/miblo_core/src/miblo_alerts.h firmware/lib/miblo_core/src/miblo_alerts.cpp firmware/test/test_alerts/test_main.cpp
git commit -m "feat(firmware): alert sequencer with dedupe, amber-first ordering and reminders"
```

---

### Task 6: Pareamento, tokens, código de presença e Bearer

**Files:**
- Create: `firmware/lib/miblo_core/src/miblo_security.h`, `firmware/lib/miblo_core/src/miblo_security.cpp`
- Test: `firmware/test/test_security/test_main.cpp`

**Interfaces:**
- Produces: `formatCode(uint32_t rnd, char out[5])`; `makeToken(const uint8_t rnd[16], char out[33])`; `bearerToken(const char* header, char* out, size_t cap) → bool`; `constantTimeEquals(const char*, const char*) → bool`; `class PairingGuard { enum class Result {Ok, BadCode, Locked}; kMaxFailures = 5; kLockMs = 60000; void setCode(const char*); const char* code() const; Result check(const char* code, uint32_t nowMs); uint32_t lockRemainingMs(uint32_t) const; }`; `struct TokenEntry {token[33], host[33], order}`; `class TokenStore { kMax = 4; void add(const char* token, const char* host); bool matches(const char*) const; uint8_t count() const; const TokenEntry& at(uint8_t) const; void restore(const TokenEntry*, uint8_t); void clear(); }`; `class PresenceGate { enum class Purpose {Update, Reset}; kTtlMs = 300000; kMaxFailures = 5; void open(Purpose, const char* code4, uint32_t nowMs); bool active(uint32_t) const; Purpose purpose() const; const char* code() const; uint32_t remainingMs(uint32_t) const; bool check(Purpose, const char* code, uint32_t nowMs); void close(); }`.

- [ ] **Step 1: Write the failing test** — `firmware/test/test_security/test_main.cpp`

```cpp
#include <string.h>
#include <unity.h>

#include "miblo_security.h"

using namespace miblo;

void setUp() {}
void tearDown() {}

static void test_codes_and_tokens() {
  char code[5];
  formatCode(42, code);
  TEST_ASSERT_EQUAL_STRING("0042", code);
  formatCode(123456789, code);
  TEST_ASSERT_EQUAL_STRING("6789", code);
  uint8_t rnd[16];
  for (int i = 0; i < 16; i++) rnd[i] = (uint8_t)(i * 17);
  char tok[33];
  makeToken(rnd, tok);
  TEST_ASSERT_EQUAL_STRING("00112233445566778899aabbccddeeff", tok);
}

static void test_bearer_parsing() {
  char t[40];
  TEST_ASSERT_TRUE(bearerToken("Bearer abc123", t, sizeof(t)));
  TEST_ASSERT_EQUAL_STRING("abc123", t);
  TEST_ASSERT_TRUE(bearerToken("Bearer   spaced  ", t, sizeof(t)));
  TEST_ASSERT_EQUAL_STRING("spaced", t);
  TEST_ASSERT_FALSE(bearerToken("Basic abc", t, sizeof(t)));
  TEST_ASSERT_FALSE(bearerToken("Bearer ", t, sizeof(t)));
  TEST_ASSERT_FALSE(bearerToken(nullptr, t, sizeof(t)));
  TEST_ASSERT_FALSE(bearerToken("Bearer 0123456789", t, 5));
}

static void test_constant_time_equals() {
  TEST_ASSERT_TRUE(constantTimeEquals("4827", "4827"));
  TEST_ASSERT_FALSE(constantTimeEquals("4827", "4828"));
  TEST_ASSERT_FALSE(constantTimeEquals("482", "4827"));
  TEST_ASSERT_FALSE(constantTimeEquals("", "4827"));
  TEST_ASSERT_TRUE(constantTimeEquals("", ""));
}

static void test_pairing_lockout_after_five_bad_codes() {
  PairingGuard g;
  g.setCode("4827");
  TEST_ASSERT_EQUAL(PairingGuard::Result::Ok, g.check("4827", 0));
  for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL(PairingGuard::Result::BadCode, g.check("0000", 1000));
  TEST_ASSERT_EQUAL(PairingGuard::Result::BadCode, g.check("1111", 2000));  // 5º erro → bloqueia
  TEST_ASSERT_EQUAL(PairingGuard::Result::Locked, g.check("4827", 2001));  // nem o certo passa
  TEST_ASSERT_EQUAL_UINT32(59999, g.lockRemainingMs(2001));
  TEST_ASSERT_EQUAL(PairingGuard::Result::Locked, g.check("4827", 61999));
  TEST_ASSERT_EQUAL(PairingGuard::Result::Ok, g.check("4827", 62000));
  TEST_ASSERT_EQUAL(PairingGuard::Result::BadCode, g.check(nullptr, 62001));
}

static void test_success_resets_failure_count() {
  PairingGuard g;
  g.setCode("1234");
  for (int i = 0; i < 4; i++) g.check("0000", 0);
  TEST_ASSERT_EQUAL(PairingGuard::Result::Ok, g.check("1234", 0));
  for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL(PairingGuard::Result::BadCode, g.check("0000", 0));
  TEST_ASSERT_EQUAL(PairingGuard::Result::Ok, g.check("1234", 0));
}

static void test_token_store_up_to_four_replacing_oldest() {
  TokenStore s;
  s.add("t1", "mac");
  s.add("t2", "pc");
  s.add("t3", "wsl");
  s.add("t4", "linux");
  TEST_ASSERT_EQUAL_UINT8(4, s.count());
  s.add("t5", "new");  // cheio: sai o mais antigo (t1)
  TEST_ASSERT_EQUAL_UINT8(4, s.count());
  TEST_ASSERT_FALSE(s.matches("t1"));
  TEST_ASSERT_TRUE(s.matches("t5"));
  s.add("t6", "pc");  // mesmo host: substitui, não ocupa vaga
  TEST_ASSERT_FALSE(s.matches("t2"));
  TEST_ASSERT_TRUE(s.matches("t6"));
  TEST_ASSERT_TRUE(s.matches("t3"));
  TEST_ASSERT_FALSE(s.matches(""));
  TEST_ASSERT_FALSE(s.matches(nullptr));
  TokenEntry copy[TokenStore::kMax];
  for (uint8_t i = 0; i < s.count(); i++) copy[i] = s.at(i);
  TokenStore r;
  r.restore(copy, s.count());
  TEST_ASSERT_TRUE(r.matches("t6"));
  r.clear();
  TEST_ASSERT_FALSE(r.matches("t6"));
}

static void test_presence_gate() {
  PresenceGate g;
  TEST_ASSERT_FALSE(g.active(0));
  TEST_ASSERT_FALSE(g.check(PresenceGate::Purpose::Update, "1234", 0));
  g.open(PresenceGate::Purpose::Update, "1234", 1000);
  TEST_ASSERT_TRUE(g.active(1000));
  TEST_ASSERT_EQUAL_UINT32(300000, g.remainingMs(1000));
  TEST_ASSERT_FALSE(g.check(PresenceGate::Purpose::Reset, "1234", 2000));  // outro propósito
  TEST_ASSERT_TRUE(g.check(PresenceGate::Purpose::Update, "1234", 2000));
  TEST_ASSERT_FALSE(g.active(301000));  // expirou
  g.open(PresenceGate::Purpose::Reset, "9999", 0);
  for (int i = 0; i < 5; i++) TEST_ASSERT_FALSE(g.check(PresenceGate::Purpose::Reset, "0000", 10));
  TEST_ASSERT_FALSE(g.check(PresenceGate::Purpose::Reset, "9999", 10));  // fechado após 5 erros
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_codes_and_tokens);
  RUN_TEST(test_bearer_parsing);
  RUN_TEST(test_constant_time_equals);
  RUN_TEST(test_pairing_lockout_after_five_bad_codes);
  RUN_TEST(test_success_resets_failure_count);
  RUN_TEST(test_token_store_up_to_four_replacing_oldest);
  RUN_TEST(test_presence_gate);
  return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd firmware && .venv/bin/pio test -e native -f test_security`
Expected: FAIL — `miblo_security.h: No such file or directory`.

- [ ] **Step 3: Implement** — `firmware/lib/miblo_core/src/miblo_security.h`

```cpp
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace miblo {

// "0042" a partir de um número aleatório.
void formatCode(uint32_t rnd, char out[5]);
// Token de 128 bits em hexadecimal minúsculo (32 caracteres + NUL).
void makeToken(const uint8_t rnd[16], char out[33]);
// "Bearer <token>" → token. false se o cabeçalho não tiver esse formato ou o token não couber.
bool bearerToken(const char* header, char* out, size_t cap);
// Comparação em tempo constante (não vaza o tamanho do prefixo correto).
bool constantTimeEquals(const char* a, const char* b);

// Código de pareamento: 5 erros seguidos bloqueiam novas tentativas por 60 s (spec §5.4).
class PairingGuard {
 public:
  enum class Result : uint8_t { Ok, BadCode, Locked };
  static constexpr uint8_t kMaxFailures = 5;
  static constexpr uint32_t kLockMs = 60000;

  void setCode(const char* code4);
  const char* code() const { return code_; }
  Result check(const char* code, uint32_t nowMs);
  uint32_t lockRemainingMs(uint32_t nowMs) const;

 private:
  char code_[5] = "0000";
  uint8_t failures_ = 0;
  bool locked_ = false;
  uint32_t lockedAtMs_ = 0;
};

struct TokenEntry {
  char token[33];
  char host[33];
  uint32_t order;  // maior = mais recente
};

// Até 4 computadores pareados. Parear de novo o mesmo host substitui o token antigo;
// com a lista cheia, o pareamento mais antigo sai.
class TokenStore {
 public:
  static constexpr uint8_t kMax = 4;
  void add(const char* token, const char* host);
  bool matches(const char* token) const;
  uint8_t count() const { return n_; }
  const TokenEntry& at(uint8_t i) const { return e_[i]; }
  void restore(const TokenEntry* entries, uint8_t n);
  void clear() { n_ = 0; }

 private:
  TokenEntry e_[kMax] = {};
  uint8_t n_ = 0;
};

// Código de presença física: ao abrir /update (ou pedir o reset de fábrica) pelo navegador, a
// tela mostra um código de 4 dígitos, válido por 5 min; o POST precisa dele. 5 erros fecham o portão.
class PresenceGate {
 public:
  enum class Purpose : uint8_t { Update, Reset };
  static constexpr uint32_t kTtlMs = 300000;
  static constexpr uint8_t kMaxFailures = 5;

  void open(Purpose p, const char* code4, uint32_t nowMs);
  bool active(uint32_t nowMs) const;
  Purpose purpose() const { return purpose_; }
  const char* code() const { return code_; }
  uint32_t remainingMs(uint32_t nowMs) const;
  bool check(Purpose p, const char* code, uint32_t nowMs);
  void close() { open_ = false; }

 private:
  bool open_ = false;
  Purpose purpose_ = Purpose::Update;
  char code_[5] = "";
  uint32_t openedAtMs_ = 0;
  uint8_t failures_ = 0;
};

}  // namespace miblo
```

`firmware/lib/miblo_core/src/miblo_security.cpp`:

```cpp
#include "miblo_security.h"

#include <stdio.h>
#include <string.h>

namespace miblo {

void formatCode(uint32_t rnd, char out[5]) {
  snprintf(out, 5, "%04u", (unsigned)(rnd % 10000));
}

void makeToken(const uint8_t rnd[16], char out[33]) {
  static const char hex[] = "0123456789abcdef";
  for (int i = 0; i < 16; i++) {
    out[i * 2] = hex[rnd[i] >> 4];
    out[i * 2 + 1] = hex[rnd[i] & 0x0F];
  }
  out[32] = 0;
}

bool bearerToken(const char* header, char* out, size_t cap) {
  if (!header || strncmp(header, "Bearer ", 7) != 0) return false;
  const char* t = header + 7;
  while (*t == ' ') t++;
  size_t len = strlen(t);
  while (len > 0 && t[len - 1] == ' ') len--;
  if (len == 0 || len >= cap) return false;
  memcpy(out, t, len);
  out[len] = 0;
  return true;
}

bool constantTimeEquals(const char* a, const char* b) {
  size_t la = strlen(a);
  size_t lb = strlen(b);
  uint8_t diff = (uint8_t)(la != lb);
  for (size_t i = 0; i < la; i++) diff |= (uint8_t)(a[i] ^ b[i % (lb ? lb : 1)]);
  return diff == 0;
}

void PairingGuard::setCode(const char* code4) {
  strncpy(code_, code4, sizeof(code_) - 1);
  code_[sizeof(code_) - 1] = 0;
}

uint32_t PairingGuard::lockRemainingMs(uint32_t nowMs) const {
  if (!locked_) return 0;
  uint32_t elapsed = nowMs - lockedAtMs_;
  return elapsed >= kLockMs ? 0 : kLockMs - elapsed;
}

PairingGuard::Result PairingGuard::check(const char* code, uint32_t nowMs) {
  if (locked_) {
    if (lockRemainingMs(nowMs) > 0) return Result::Locked;
    locked_ = false;
    failures_ = 0;
  }
  if (code && constantTimeEquals(code, code_)) {
    failures_ = 0;
    return Result::Ok;
  }
  if (++failures_ >= kMaxFailures) {
    locked_ = true;
    lockedAtMs_ = nowMs;
  }
  return Result::BadCode;
}

void TokenStore::add(const char* token, const char* host) {
  uint32_t order = 1;
  for (uint8_t i = 0; i < n_; i++) {
    if (e_[i].order >= order) order = e_[i].order + 1;
  }
  int slot = -1;
  for (uint8_t i = 0; i < n_; i++) {
    if (strcmp(e_[i].host, host) == 0) slot = i;
  }
  if (slot < 0 && n_ < kMax) slot = n_++;
  if (slot < 0) {
    slot = 0;
    for (uint8_t i = 1; i < n_; i++) {
      if (e_[i].order < e_[slot].order) slot = i;
    }
  }
  TokenEntry& e = e_[slot];
  strncpy(e.token, token, sizeof(e.token) - 1);
  e.token[sizeof(e.token) - 1] = 0;
  strncpy(e.host, host, sizeof(e.host) - 1);
  e.host[sizeof(e.host) - 1] = 0;
  e.order = order;
}

bool TokenStore::matches(const char* token) const {
  if (!token || !token[0]) return false;
  bool ok = false;
  for (uint8_t i = 0; i < n_; i++) ok |= constantTimeEquals(token, e_[i].token);
  return ok;
}

void TokenStore::restore(const TokenEntry* entries, uint8_t n) {
  n_ = n > kMax ? kMax : n;
  for (uint8_t i = 0; i < n_; i++) e_[i] = entries[i];
}

void PresenceGate::open(Purpose p, const char* code4, uint32_t nowMs) {
  open_ = true;
  purpose_ = p;
  strncpy(code_, code4, sizeof(code_) - 1);
  code_[sizeof(code_) - 1] = 0;
  openedAtMs_ = nowMs;
  failures_ = 0;
}

bool PresenceGate::active(uint32_t nowMs) const {
  return open_ && (nowMs - openedAtMs_) < kTtlMs;
}

uint32_t PresenceGate::remainingMs(uint32_t nowMs) const {
  return active(nowMs) ? kTtlMs - (nowMs - openedAtMs_) : 0;
}

bool PresenceGate::check(Purpose p, const char* code, uint32_t nowMs) {
  if (!active(nowMs) || p != purpose_) return false;
  if (code && constantTimeEquals(code, code_)) return true;
  if (++failures_ >= kMaxFailures) open_ = false;
  return false;
}

}  // namespace miblo
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd firmware && .venv/bin/pio test -e native -f test_security`
Expected: PASS — `7 test cases: 7 succeeded`.

- [ ] **Step 5: Commit**

```bash
git add firmware/lib/miblo_core/src/miblo_security.h firmware/lib/miblo_core/src/miblo_security.cpp firmware/test/test_security/test_main.cpp
git commit -m "feat(firmware): pairing lockout, token store, presence code gate and bearer parsing"
```

---

### Task 7: Configuração, reset por liga/desliga, política de Wi-Fi e escolha da tela

**Files:**
- Create: `firmware/lib/miblo_core/src/miblo_config.h`, `firmware/lib/miblo_core/src/miblo_config.cpp`
- Create: `firmware/lib/miblo_core/src/miblo_policy.h`, `firmware/lib/miblo_core/src/miblo_policy.cpp`
- Test: `firmware/test/test_config_policy/test_main.cpp`

**Interfaces:**
- Consumes: `AlertTiming`, `AlertPhase` (Task 5); `Lang`, `langFromCode`, `langCode` (Task 3); `utf8Length` (Task 1).
- Produces: `enum class Mode {Overview, Limits, Sessions}`; `modeCode(Mode)`, `modeFromCode(const char*, Mode&)`; `struct Config {mode, brightness=80, alerts=true, heroPermSec=10, heroDoneSec=5, reminderMin=2, discreet=false, tz[48]="UTC0", name[64]="", lang=En, langSet=false}`; `applyConfigPatch(Config&, JsonObjectConst, const char** badField) → bool` (tudo ou nada); `configToJson(const Config&, JsonObject)`; `alertTiming(const Config&) → AlertTiming`; `kPowerCyclesForReset = 3`, `kPowerCycleWindowMs = 10000`, `struct BootDecision {storeCount, factoryReset}`, `decideBoot(uint8_t stored)`; `enum class NetState {Connecting, Connected, Portal, WrongPassword}`, `enum class LinkStatus {Down, Connected, WrongPassword}`, `class NetPolicy { kFallbackMs = 120000; void begin(bool hasCredentials, uint32_t); void credentialsSubmitted(uint32_t); NetState update(LinkStatus, uint32_t); NetState state() const; bool apWanted() const; }`; `enum class ScreenId {Boot, Setup, WrongPassword, Welcome, Paired, PairCode, PresenceCode, Updating, Disconnected, AlertFlash, AlertHero, Main}`; `kPairedScreenMs = 5000`, `kSnapshotTimeoutMs = 30000`, `kPairCodeScreenMs = 120000`; `struct ScreenInputs {…}`; `selectScreen(const ScreenInputs&) → ScreenId`.

Chaves JSON da configuração (página e `POST /api/config`): `mode` (`overview|limits|sessions`), `brightness` (5–100), `alerts` (bool), `heroPermSec` (3–60), `heroDoneSec` (2–60), `reminderMin` (0–30, 0 = sem lembrete), `discreet` (bool), `tz` (TZ POSIX, ASCII sem espaços, < 48), `name` (≤ 20 caracteres), `lang` (código ou `""` = automático).

- [ ] **Step 1: Write the failing test** — `firmware/test/test_config_policy/test_main.cpp`

```cpp
#include <ArduinoJson.h>
#include <string.h>
#include <unity.h>

#include "miblo_config.h"
#include "miblo_policy.h"

using namespace miblo;

void setUp() {}
void tearDown() {}

static bool patch(Config& cfg, const char* json, const char** bad = nullptr) {
  StaticJsonDocument<1024> doc;
  TEST_ASSERT_FALSE(deserializeJson(doc, json));
  return applyConfigPatch(cfg, doc.as<JsonObjectConst>(), bad);
}

static void test_defaults_match_spec() {
  Config c;
  TEST_ASSERT_EQUAL(Mode::Overview, c.mode);
  TEST_ASSERT_TRUE(c.alerts);
  AlertTiming t = alertTiming(c);
  TEST_ASSERT_TRUE(t.enabled);
  TEST_ASSERT_EQUAL_UINT32(1500, t.flashMs);
  TEST_ASSERT_EQUAL_UINT32(10000, t.heroPermMs);
  TEST_ASSERT_EQUAL_UINT32(5000, t.heroDoneMs);
  TEST_ASSERT_EQUAL_UINT32(120000, t.reminderMs);
}

static void test_patch_applies_valid_fields_and_ignores_unknown() {
  Config c;
  TEST_ASSERT_TRUE(patch(c, "{\"mode\":\"limits\",\"brightness\":40,\"alerts\":false,\"heroPermSec\":20,"
                            "\"heroDoneSec\":3,\"reminderMin\":0,\"discreet\":true,\"tz\":\"<-03>3\","
                            "\"name\":\"Mesa\",\"lang\":\"pt-BR\",\"future\":123}"));
  TEST_ASSERT_EQUAL(Mode::Limits, c.mode);
  TEST_ASSERT_EQUAL_UINT8(40, c.brightness);
  TEST_ASSERT_FALSE(c.alerts);
  TEST_ASSERT_EQUAL_UINT8(20, c.heroPermSec);
  TEST_ASSERT_EQUAL_UINT8(0, c.reminderMin);
  TEST_ASSERT_TRUE(c.discreet);
  TEST_ASSERT_EQUAL_STRING("<-03>3", c.tz);
  TEST_ASSERT_EQUAL_STRING("Mesa", c.name);
  TEST_ASSERT_TRUE(c.langSet);
  TEST_ASSERT_EQUAL(Lang::PtBR, c.lang);
  TEST_ASSERT_EQUAL_UINT32(0, alertTiming(c).reminderMs);
  TEST_ASSERT_TRUE(patch(c, "{\"lang\":\"\"}"));
  TEST_ASSERT_FALSE(c.langSet);
}

static void test_invalid_patch_changes_nothing() {
  Config c;
  const char* bad = nullptr;
  TEST_ASSERT_FALSE(patch(c, "{\"mode\":\"sessions\",\"brightness\":101}", &bad));
  TEST_ASSERT_EQUAL_STRING("brightness", bad);
  TEST_ASSERT_EQUAL(Mode::Overview, c.mode);  // nem o campo válido foi aplicado
  TEST_ASSERT_FALSE(patch(c, "{\"mode\":\"grid\"}", &bad));
  TEST_ASSERT_EQUAL_STRING("mode", bad);
  TEST_ASSERT_FALSE(patch(c, "{\"alerts\":1}", &bad));
  TEST_ASSERT_FALSE(patch(c, "{\"tz\":\"America/Sao Paulo\"}", &bad));
  TEST_ASSERT_FALSE(patch(c, "{\"lang\":\"ja\"}", &bad));
  TEST_ASSERT_FALSE(patch(c, "{\"name\":\"123456789012345678901\"}", &bad));
  TEST_ASSERT_TRUE(patch(c, "{\"name\":\"项目项目项目项目项目项目项目项目项目项目\"}"));  // 20 caracteres
}

static void test_config_json_roundtrip() {
  Config a;
  TEST_ASSERT_TRUE(patch(a, "{\"mode\":\"sessions\",\"lang\":\"zh\",\"name\":\"X\"}"));
  StaticJsonDocument<1024> doc;
  configToJson(a, doc.to<JsonObject>());
  TEST_ASSERT_EQUAL_STRING("sessions", doc["mode"]);
  TEST_ASSERT_EQUAL_STRING("zh", doc["lang"]);
  Config b;
  TEST_ASSERT_TRUE(applyConfigPatch(b, doc.as<JsonObjectConst>(), nullptr));
  TEST_ASSERT_EQUAL(Mode::Sessions, b.mode);
  TEST_ASSERT_EQUAL(Lang::Zh, b.lang);
  TEST_ASSERT_EQUAL_STRING("X", b.name);
}

static void test_power_cycle_reset_counter() {
  BootDecision d = decideBoot(0);
  TEST_ASSERT_EQUAL_UINT8(1, d.storeCount);
  TEST_ASSERT_FALSE(d.factoryReset);
  d = decideBoot(d.storeCount);
  TEST_ASSERT_EQUAL_UINT8(2, d.storeCount);
  TEST_ASSERT_FALSE(d.factoryReset);
  d = decideBoot(d.storeCount);
  TEST_ASSERT_TRUE(d.factoryReset);
  TEST_ASSERT_EQUAL_UINT8(0, d.storeCount);
  d = decideBoot(0xFF);  // lixo na flash conta como primeiro boot
  TEST_ASSERT_EQUAL_UINT8(1, d.storeCount);
  TEST_ASSERT_FALSE(d.factoryReset);
}

static void test_net_policy_saved_credentials_then_router_down() {
  NetPolicy p;
  p.begin(true, 0);
  TEST_ASSERT_EQUAL(NetState::Connecting, p.state());
  TEST_ASSERT_FALSE(p.apWanted());
  TEST_ASSERT_EQUAL(NetState::Connected, p.update(LinkStatus::Connected, 3000));
  TEST_ASSERT_EQUAL(NetState::Connecting, p.update(LinkStatus::Down, 10000));  // roteador caiu
  TEST_ASSERT_EQUAL(NetState::Connecting, p.update(LinkStatus::Down, 129999));
  TEST_ASSERT_FALSE(p.apWanted());
  TEST_ASSERT_EQUAL(NetState::Portal, p.update(LinkStatus::Down, 130000));  // 2 min → rede de setup
  TEST_ASSERT_TRUE(p.apWanted());
  TEST_ASSERT_EQUAL(NetState::Connected, p.update(LinkStatus::Connected, 200000));  // voltou sozinho
  TEST_ASSERT_FALSE(p.apWanted());
}

static void test_net_policy_first_boot_and_wrong_password() {
  NetPolicy p;
  p.begin(false, 0);
  TEST_ASSERT_EQUAL(NetState::Portal, p.state());
  TEST_ASSERT_TRUE(p.apWanted());
  p.credentialsSubmitted(5000);
  TEST_ASSERT_EQUAL(NetState::Connecting, p.state());
  TEST_ASSERT_TRUE(p.apWanted());
  TEST_ASSERT_EQUAL(NetState::WrongPassword, p.update(LinkStatus::WrongPassword, 9000));
  TEST_ASSERT_TRUE(p.apWanted());
  TEST_ASSERT_EQUAL(NetState::WrongPassword, p.update(LinkStatus::Down, 20000));
  p.credentialsSubmitted(30000);
  TEST_ASSERT_EQUAL(NetState::Connected, p.update(LinkStatus::Connected, 34000));
  TEST_ASSERT_FALSE(p.apWanted());
}

static void test_screen_selection_order() {
  ScreenInputs in;
  in.nowMs = 100000;
  TEST_ASSERT_EQUAL(ScreenId::Boot, selectScreen(in));
  in.bootAnimDone = true;
  TEST_ASSERT_EQUAL(ScreenId::Boot, selectScreen(in));  // conectando
  in.net = NetState::Portal;
  TEST_ASSERT_EQUAL(ScreenId::Setup, selectScreen(in));
  in.net = NetState::WrongPassword;
  TEST_ASSERT_EQUAL(ScreenId::WrongPassword, selectScreen(in));
  in.net = NetState::Connected;
  TEST_ASSERT_EQUAL(ScreenId::Welcome, selectScreen(in));
  in.paired = true;
  in.justPaired = true;
  in.pairedAtMs = 97000;
  TEST_ASSERT_EQUAL(ScreenId::Paired, selectScreen(in));
  in.pairedAtMs = 95000;
  TEST_ASSERT_EQUAL(ScreenId::Disconnected, selectScreen(in));  // pareado, sem snapshot ainda
  in.hasSnapshot = true;
  in.lastSnapshotMs = 71000;
  TEST_ASSERT_EQUAL(ScreenId::Main, selectScreen(in));
  in.lastSnapshotMs = 70000;
  TEST_ASSERT_EQUAL(ScreenId::Disconnected, selectScreen(in));  // 30 s sem snapshot
  in.lastSnapshotMs = 99000;
  in.alert = AlertPhase::Flash;
  TEST_ASSERT_EQUAL(ScreenId::AlertFlash, selectScreen(in));
  in.alert = AlertPhase::Hero;
  TEST_ASSERT_EQUAL(ScreenId::AlertHero, selectScreen(in));
  in.pairCodeRequested = true;
  TEST_ASSERT_EQUAL(ScreenId::PairCode, selectScreen(in));
  in.presenceActive = true;
  TEST_ASSERT_EQUAL(ScreenId::PresenceCode, selectScreen(in));
  in.updating = true;
  TEST_ASSERT_EQUAL(ScreenId::Updating, selectScreen(in));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_match_spec);
  RUN_TEST(test_patch_applies_valid_fields_and_ignores_unknown);
  RUN_TEST(test_invalid_patch_changes_nothing);
  RUN_TEST(test_config_json_roundtrip);
  RUN_TEST(test_power_cycle_reset_counter);
  RUN_TEST(test_net_policy_saved_credentials_then_router_down);
  RUN_TEST(test_net_policy_first_boot_and_wrong_password);
  RUN_TEST(test_screen_selection_order);
  return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd firmware && .venv/bin/pio test -e native -f test_config_policy`
Expected: FAIL — `miblo_config.h: No such file or directory`.

- [ ] **Step 3: Implement** — `firmware/lib/miblo_core/src/miblo_config.h`

```cpp
#pragma once
#include <ArduinoJson.h>
#include <stdint.h>

#include "miblo_alerts.h"
#include "miblo_i18n.h"

namespace miblo {

enum class Mode : uint8_t { Overview, Limits, Sessions };
const char* modeCode(Mode m);  // "overview" | "limits" | "sessions"
bool modeFromCode(const char* s, Mode& out);

struct Config {
  Mode mode = Mode::Overview;
  uint8_t brightness = 80;  // %, 5..100
  bool alerts = true;
  uint8_t heroPermSec = 10;  // 3..60
  uint8_t heroDoneSec = 5;   // 2..60
  uint8_t reminderMin = 2;   // 0..30 (0 = sem lembrete)
  bool discreet = false;
  char tz[48] = "UTC0";      // TZ POSIX, ex. "<-03>3"
  char name[64] = "";        // ≤ 20 caracteres; vazio = nome padrão "Miblo-XXXX"
  Lang lang = Lang::En;
  bool langSet = false;      // false = idioma automático (Accept-Language)
};

// Valida todos os campos presentes e só então aplica. Campos desconhecidos são ignorados.
// Em erro, `cfg` não muda e `*badField` (se não nulo) aponta para o nome do campo inválido.
bool applyConfigPatch(Config& cfg, JsonObjectConst patch, const char** badField);
void configToJson(const Config& cfg, JsonObject out);
AlertTiming alertTiming(const Config& cfg);

// ---- Reset de fábrica por liga/desliga (spec §6) ----
constexpr uint8_t kPowerCyclesForReset = 3;
constexpr uint32_t kPowerCycleWindowMs = 10000;
struct BootDecision {
  uint8_t storeCount;  // valor a gravar na flash agora
  bool factoryReset;
};
// No boot: incrementa o contador salvo; no 3º boot seguido (cada um com < 10 s de uptime) → reset.
// Depois de 10 s de uptime o firmware grava 0.
BootDecision decideBoot(uint8_t storedCount);

}  // namespace miblo
```

`firmware/lib/miblo_core/src/miblo_config.cpp`:

```cpp
#include "miblo_config.h"

#include <string.h>

#include "miblo_utf8.h"

namespace miblo {

const char* modeCode(Mode m) {
  switch (m) {
    case Mode::Overview: return "overview";
    case Mode::Limits: return "limits";
    case Mode::Sessions: return "sessions";
  }
  return "overview";
}

bool modeFromCode(const char* s, Mode& out) {
  if (!s) return false;
  if (strcmp(s, "overview") == 0) out = Mode::Overview;
  else if (strcmp(s, "limits") == 0) out = Mode::Limits;
  else if (strcmp(s, "sessions") == 0) out = Mode::Sessions;
  else return false;
  return true;
}

static bool intIn(JsonVariantConst v, int lo, int hi, uint8_t& out) {
  if (!v.is<int>()) return false;
  int x = v.as<int>();
  if (x < lo || x > hi) return false;
  out = (uint8_t)x;
  return true;
}

static bool printableAscii(const char* s) {
  for (; *s; s++) {
    if (*s < 0x21 || *s > 0x7E) return false;
  }
  return true;
}

bool applyConfigPatch(Config& cfg, JsonObjectConst patch, const char** badField) {
  Config next = cfg;
  const char* bad = nullptr;
  for (JsonPairConst kv : patch) {
    const char* k = kv.key().c_str();
    JsonVariantConst v = kv.value();
    bool ok = true;
    if (strcmp(k, "mode") == 0) {
      ok = modeFromCode(v.as<const char*>(), next.mode);
    } else if (strcmp(k, "brightness") == 0) {
      ok = intIn(v, 5, 100, next.brightness);
    } else if (strcmp(k, "alerts") == 0) {
      ok = v.is<bool>();
      if (ok) next.alerts = v.as<bool>();
    } else if (strcmp(k, "heroPermSec") == 0) {
      ok = intIn(v, 3, 60, next.heroPermSec);
    } else if (strcmp(k, "heroDoneSec") == 0) {
      ok = intIn(v, 2, 60, next.heroDoneSec);
    } else if (strcmp(k, "reminderMin") == 0) {
      ok = intIn(v, 0, 30, next.reminderMin);
    } else if (strcmp(k, "discreet") == 0) {
      ok = v.is<bool>();
      if (ok) next.discreet = v.as<bool>();
    } else if (strcmp(k, "tz") == 0) {
      const char* s = v.as<const char*>();
      ok = s && s[0] && strlen(s) < sizeof(next.tz) && printableAscii(s);
      if (ok) strcpy(next.tz, s);
    } else if (strcmp(k, "name") == 0) {
      const char* s = v.as<const char*>();
      ok = s && strlen(s) < sizeof(next.name) && utf8Length(s) <= 20;
      if (ok) strcpy(next.name, s);
    } else if (strcmp(k, "lang") == 0) {
      const char* s = v.as<const char*>();
      if (s && s[0] == 0) {
        next.langSet = false;
      } else {
        ok = langFromCode(s, next.lang);
        if (ok) next.langSet = true;
      }
    }
    if (!ok) {
      bad = k;
      break;
    }
  }
  if (bad) {
    if (badField) *badField = bad;
    return false;
  }
  cfg = next;
  return true;
}

void configToJson(const Config& cfg, JsonObject out) {
  out["mode"] = modeCode(cfg.mode);
  out["brightness"] = cfg.brightness;
  out["alerts"] = cfg.alerts;
  out["heroPermSec"] = cfg.heroPermSec;
  out["heroDoneSec"] = cfg.heroDoneSec;
  out["reminderMin"] = cfg.reminderMin;
  out["discreet"] = cfg.discreet;
  out["tz"] = cfg.tz;
  out["name"] = cfg.name;
  out["lang"] = cfg.langSet ? langCode(cfg.lang) : "";
}

AlertTiming alertTiming(const Config& cfg) {
  AlertTiming t;
  t.enabled = cfg.alerts;
  t.heroPermMs = (uint32_t)cfg.heroPermSec * 1000;
  t.heroDoneMs = (uint32_t)cfg.heroDoneSec * 1000;
  t.reminderMs = (uint32_t)cfg.reminderMin * 60000;
  return t;
}

BootDecision decideBoot(uint8_t storedCount) {
  uint8_t n = storedCount < kPowerCyclesForReset ? (uint8_t)(storedCount + 1) : 1;
  if (n >= kPowerCyclesForReset) return BootDecision{0, true};
  return BootDecision{n, false};
}

}  // namespace miblo
```

`firmware/lib/miblo_core/src/miblo_policy.h`:

```cpp
#pragma once
#include <stdint.h>

#include "miblo_alerts.h"

namespace miblo {

// ---- Wi-Fi: quando abrir a rede de setup (spec §6) ----
enum class NetState : uint8_t { Connecting, Connected, Portal, WrongPassword };
enum class LinkStatus : uint8_t { Down, Connected, WrongPassword };

class NetPolicy {
 public:
  static constexpr uint32_t kFallbackMs = 120000;  // 2 min sem conexão → abre a rede de setup
  void begin(bool hasCredentials, uint32_t nowMs);
  void credentialsSubmitted(uint32_t nowMs);
  NetState update(LinkStatus link, uint32_t nowMs);
  NetState state() const { return state_; }
  // Rede de setup (AP) deve estar no ar? A conexão com a rede salva continua sendo tentada.
  bool apWanted() const { return ap_; }

 private:
  NetState state_ = NetState::Connecting;
  bool ap_ = false;
  uint32_t sinceMs_ = 0;
};

// ---- Qual tela mostrar ----
enum class ScreenId : uint8_t {
  Boot,           // mascote + "Conectando ao Wi-Fi"
  Setup,          // QR + nome da rede de setup
  WrongPassword,  // Setup com "Senha incorreta"
  Welcome,        // Wi-Fi conectado + comando + código de pareamento + IP
  Paired,         // "Pareado com <host>"
  PairCode,       // código de pareamento pedido pela página
  PresenceCode,   // código para update/reset pelo navegador
  Updating,       // barra de progresso do OTA
  Disconnected,   // relógio (sem snapshot há 30 s)
  AlertFlash,
  AlertHero,
  Main            // modo do aparelho (Visão geral, Limites ou Sessões)
};

constexpr uint32_t kPairedScreenMs = 5000;
constexpr uint32_t kSnapshotTimeoutMs = 30000;
constexpr uint32_t kPairCodeScreenMs = 120000;

struct ScreenInputs {
  uint32_t nowMs = 0;
  bool bootAnimDone = false;
  NetState net = NetState::Connecting;
  bool updating = false;
  bool presenceActive = false;
  bool pairCodeRequested = false;
  bool paired = false;
  bool justPaired = false;
  uint32_t pairedAtMs = 0;
  bool hasSnapshot = false;
  uint32_t lastSnapshotMs = 0;
  AlertPhase alert = AlertPhase::None;
};

ScreenId selectScreen(const ScreenInputs& in);

}  // namespace miblo
```

`firmware/lib/miblo_core/src/miblo_policy.cpp`:

```cpp
#include "miblo_policy.h"

namespace miblo {

void NetPolicy::begin(bool hasCredentials, uint32_t nowMs) {
  sinceMs_ = nowMs;
  if (hasCredentials) {
    state_ = NetState::Connecting;
    ap_ = false;
  } else {
    state_ = NetState::Portal;
    ap_ = true;
  }
}

void NetPolicy::credentialsSubmitted(uint32_t nowMs) {
  state_ = NetState::Connecting;
  sinceMs_ = nowMs;
  // o AP continua no ar para o celular ver o resultado; cai quando conectar
}

NetState NetPolicy::update(LinkStatus link, uint32_t nowMs) {
  if (link == LinkStatus::Connected) {
    state_ = NetState::Connected;
    ap_ = false;
    sinceMs_ = nowMs;
    return state_;
  }
  if (link == LinkStatus::WrongPassword) {
    state_ = NetState::WrongPassword;
    ap_ = true;
    return state_;
  }
  switch (state_) {
    case NetState::Connected:
      state_ = NetState::Connecting;
      sinceMs_ = nowMs;
      break;
    case NetState::Connecting:
      if (nowMs - sinceMs_ >= kFallbackMs) {
        state_ = NetState::Portal;
        ap_ = true;
      }
      break;
    case NetState::Portal:
    case NetState::WrongPassword:
      break;
  }
  return state_;
}

ScreenId selectScreen(const ScreenInputs& in) {
  if (in.updating) return ScreenId::Updating;
  if (in.presenceActive) return ScreenId::PresenceCode;
  if (!in.bootAnimDone) return ScreenId::Boot;
  switch (in.net) {
    case NetState::Portal: return ScreenId::Setup;
    case NetState::WrongPassword: return ScreenId::WrongPassword;
    case NetState::Connecting: return ScreenId::Boot;
    case NetState::Connected: break;
  }
  if (in.pairCodeRequested) return ScreenId::PairCode;
  if (in.justPaired && in.nowMs - in.pairedAtMs < kPairedScreenMs) return ScreenId::Paired;
  if (!in.paired) return ScreenId::Welcome;
  if (!in.hasSnapshot || in.nowMs - in.lastSnapshotMs >= kSnapshotTimeoutMs) return ScreenId::Disconnected;
  if (in.alert == AlertPhase::Flash) return ScreenId::AlertFlash;
  if (in.alert == AlertPhase::Hero) return ScreenId::AlertHero;
  return ScreenId::Main;
}

}  // namespace miblo
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd firmware && .venv/bin/pio test -e native -f test_config_policy`
Expected: PASS — `8 test cases: 8 succeeded`.

- [ ] **Step 5: Commit**

```bash
git add firmware/lib/miblo_core/src/miblo_config.h firmware/lib/miblo_core/src/miblo_config.cpp firmware/lib/miblo_core/src/miblo_policy.h firmware/lib/miblo_core/src/miblo_policy.cpp firmware/test/test_config_policy/test_main.cpp
git commit -m "feat(firmware): config patching, power-cycle reset, Wi-Fi fallback policy and screen selection"
```

---

### Task 8: Respondedor mDNS/DNS-SD (lógica pura)

Um respondedor próprio (em vez do `ESP8266mDNS`) porque o contrato exige responder à consulta "one-shot" do bridge (porta de origem efêmera + bit QU) por unicast — e assim o formato do pacote fica testado no host contra os mesmos bytes que `plugin/lib/mdns.js` envia.

**Files:**
- Create: `firmware/lib/miblo_core/src/miblo_mdns.h`, `firmware/lib/miblo_core/src/miblo_mdns.cpp`
- Test: `firmware/test/test_mdns/test_main.cpp`

**Interfaces:**
- Produces: `struct MdnsInfo {instance, host, ip[4], port, txt[4], txtCount}`; `struct MdnsReply {len, unicast}`; `kMdnsPort = 5353`; `mdnsRespond(const uint8_t* pkt, size_t len, uint16_t srcPort, const MdnsInfo&, uint8_t* out, size_t cap) → MdnsReply`; `mdnsAnnounce(const MdnsInfo&, uint8_t* out, size_t cap) → size_t`.

- [ ] **Step 1: Write the failing test** — `firmware/test/test_mdns/test_main.cpp`

```cpp
#include <string.h>
#include <unity.h>

#include <string>
#include <vector>

#include "miblo_mdns.h"

using namespace miblo;

void setUp() {}
void tearDown() {}

static MdnsInfo info() {
  MdnsInfo i{};
  i.instance = "Miblo-4F2A";
  i.host = "miblo-4f2a";
  i.ip[0] = 192;
  i.ip[1] = 168;
  i.ip[2] = 0;
  i.ip[3] = 42;
  i.port = 80;
  i.txt[0] = "id=miblo-4f2a";
  i.txt[1] = "name=Miblo-4F2A";
  i.txt[2] = "fw=0.1.0";
  i.txtCount = 3;
  return i;
}

static void putName(std::vector<uint8_t>& b, const std::string& name) {
  size_t start = 0;
  while (start < name.size()) {
    size_t dot = name.find('.', start);
    if (dot == std::string::npos) dot = name.size();
    b.push_back((uint8_t)(dot - start));
    b.insert(b.end(), name.begin() + start, name.begin() + dot);
    start = dot + 1;
  }
  b.push_back(0);
}

// Mesma consulta que plugin/lib/mdns.js buildQuery(): id 0, 1 pergunta, PTR, classe IN + bit QU.
static std::vector<uint8_t> query(const std::string& name, uint16_t type, bool qu, uint16_t id = 0) {
  std::vector<uint8_t> b = {(uint8_t)(id >> 8), (uint8_t)id, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0};
  putName(b, name);
  b.push_back((uint8_t)(type >> 8));
  b.push_back((uint8_t)type);
  b.push_back(qu ? 0x80 : 0x00);
  b.push_back(0x01);
  return b;
}

struct Rec {
  std::string name;
  uint16_t type;
  std::string data;  // PTR/SRV-target: nome; A: "a.b.c.d"; TXT: strings unidas por '|'; SRV: "porta target"
};

static std::string readName(const uint8_t* p, size_t len, size_t& off) {
  std::string out;
  size_t o = off;
  bool jumped = false;
  while (o < len && p[o]) {
    if ((p[o] & 0xC0) == 0xC0) {
      if (!jumped) off = o + 2;
      jumped = true;
      o = ((p[o] & 0x3F) << 8) | p[o + 1];
      continue;
    }
    if (!out.empty()) out += ".";
    out.append((const char*)p + o + 1, p[o]);
    o += 1 + p[o];
  }
  if (!jumped) off = o + 1;
  return out;
}

static std::vector<Rec> parse(const uint8_t* p, size_t len, uint16_t& id, uint16_t& qd, uint16_t& an, uint16_t& ar) {
  id = (uint16_t)(p[0] << 8 | p[1]);
  qd = (uint16_t)(p[4] << 8 | p[5]);
  an = (uint16_t)(p[6] << 8 | p[7]);
  ar = (uint16_t)(p[10] << 8 | p[11]);
  size_t off = 12;
  for (int i = 0; i < qd; i++) {
    readName(p, len, off);
    off += 4;
  }
  std::vector<Rec> recs;
  for (int i = 0; i < an + ar; i++) {
    Rec r;
    r.name = readName(p, len, off);
    r.type = (uint16_t)(p[off] << 8 | p[off + 1]);
    uint16_t rdlen = (uint16_t)(p[off + 8] << 8 | p[off + 9]);
    size_t rd = off + 10;
    if (r.type == 12) {
      size_t o = rd;
      r.data = readName(p, len, o);
    } else if (r.type == 33) {
      size_t o = rd + 6;
      r.data = std::to_string(p[rd + 4] << 8 | p[rd + 5]) + " " + readName(p, len, o);
    } else if (r.type == 1) {
      r.data = std::to_string(p[rd]) + "." + std::to_string(p[rd + 1]) + "." + std::to_string(p[rd + 2]) + "." +
               std::to_string(p[rd + 3]);
    } else if (r.type == 16) {
      for (size_t o = rd; o < rd + rdlen; o += 1 + p[o]) {
        if (!r.data.empty()) r.data += "|";
        r.data.append((const char*)p + o + 1, p[o]);
      }
    }
    recs.push_back(r);
    off = rd + rdlen;
  }
  TEST_ASSERT_EQUAL_UINT32(len, off);
  return recs;
}

static const Rec* find(const std::vector<Rec>& recs, uint16_t type) {
  for (const auto& r : recs) {
    if (r.type == type) return &r;
  }
  return nullptr;
}

static void test_bridge_query_gets_unicast_answer_with_everything() {
  MdnsInfo in = info();
  auto q = query("_miblo._tcp.local", 12, true, 0);
  uint8_t out[512];
  MdnsReply r = mdnsRespond(q.data(), q.size(), 53123, in, out, sizeof(out));
  TEST_ASSERT_TRUE(r.len > 0);
  TEST_ASSERT_TRUE(r.unicast);
  uint16_t id, qd, an, ar;
  auto recs = parse(out, r.len, id, qd, an, ar);
  TEST_ASSERT_EQUAL_UINT16(1, qd);  // legacy: pergunta ecoada
  TEST_ASSERT_EQUAL_UINT16(1, an);
  TEST_ASSERT_EQUAL_UINT16(3, ar);
  const Rec* ptr = find(recs, 12);
  TEST_ASSERT_NOT_NULL(ptr);
  TEST_ASSERT_EQUAL_STRING("_miblo._tcp.local", ptr->name.c_str());
  TEST_ASSERT_EQUAL_STRING("Miblo-4F2A._miblo._tcp.local", ptr->data.c_str());
  TEST_ASSERT_EQUAL_STRING("80 miblo-4f2a.local", find(recs, 33)->data.c_str());
  TEST_ASSERT_EQUAL_STRING("id=miblo-4f2a|name=Miblo-4F2A|fw=0.1.0", find(recs, 16)->data.c_str());
  TEST_ASSERT_EQUAL_STRING("192.168.0.42", find(recs, 1)->data.c_str());
  TEST_ASSERT_EQUAL_STRING("miblo-4f2a.local", find(recs, 1)->name.c_str());
}

static void test_legacy_query_echoes_id() {
  MdnsInfo in = info();
  auto q = query("_miblo._tcp.local", 12, false, 0xBEEF);
  uint8_t out[512];
  MdnsReply r = mdnsRespond(q.data(), q.size(), 40000, in, out, sizeof(out));
  TEST_ASSERT_TRUE(r.unicast);
  TEST_ASSERT_EQUAL_HEX8(0xBE, out[0]);
  TEST_ASSERT_EQUAL_HEX8(0xEF, out[1]);
}

static void test_multicast_query_for_host_address() {
  MdnsInfo in = info();
  auto q = query("MIBLO-4F2A.local", 1, false);
  uint8_t out[512];
  MdnsReply r = mdnsRespond(q.data(), q.size(), kMdnsPort, in, out, sizeof(out));
  TEST_ASSERT_TRUE(r.len > 0);
  TEST_ASSERT_FALSE(r.unicast);
  uint16_t id, qd, an, ar;
  auto recs = parse(out, r.len, id, qd, an, ar);
  TEST_ASSERT_EQUAL_UINT16(0, qd);
  TEST_ASSERT_EQUAL_UINT16(1, an);
  TEST_ASSERT_EQUAL_UINT16(0, ar);
  TEST_ASSERT_EQUAL_STRING("192.168.0.42", recs[0].data.c_str());
}

static void test_qu_bit_from_5353_is_unicast() {
  MdnsInfo in = info();
  auto q = query("miblo-4f2a.local", 1, true);
  uint8_t out[512];
  TEST_ASSERT_TRUE(mdnsRespond(q.data(), q.size(), kMdnsPort, in, out, sizeof(out)).unicast);
}

static void test_instance_srv_and_service_enumeration() {
  MdnsInfo in = info();
  uint8_t out[512];
  uint16_t id, qd, an, ar;
  auto q = query("Miblo-4F2A._miblo._tcp.local", 33, false);
  MdnsReply r = mdnsRespond(q.data(), q.size(), kMdnsPort, in, out, sizeof(out));
  auto recs = parse(out, r.len, id, qd, an, ar);
  TEST_ASSERT_EQUAL_UINT16(2, an);  // SRV + TXT
  TEST_ASSERT_EQUAL_UINT16(1, ar);  // A
  q = query("_services._dns-sd._udp.local", 12, false);
  r = mdnsRespond(q.data(), q.size(), kMdnsPort, in, out, sizeof(out));
  recs = parse(out, r.len, id, qd, an, ar);
  TEST_ASSERT_EQUAL_STRING("_miblo._tcp.local", recs[0].data.c_str());
}

static void test_ignores_other_names_responses_and_garbage() {
  MdnsInfo in = info();
  uint8_t out[512];
  auto q = query("_http._tcp.local", 12, true);
  TEST_ASSERT_EQUAL(0, mdnsRespond(q.data(), q.size(), 5353, in, out, sizeof(out)).len);
  q = query("_miblo._tcp.local", 12, true);
  q[2] = 0x84;  // é uma resposta de outro aparelho
  TEST_ASSERT_EQUAL(0, mdnsRespond(q.data(), q.size(), 5353, in, out, sizeof(out)).len);
  uint8_t garbage[] = {0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 60, 'x'};
  TEST_ASSERT_EQUAL(0, mdnsRespond(garbage, sizeof(garbage), 5353, in, out, sizeof(out)).len);
  q = query("_miblo._tcp.local", 12, true);
  TEST_ASSERT_EQUAL(0, mdnsRespond(q.data(), q.size(), 5353, in, out, 40).len);  // não cabe
}

static void test_announcement_contains_all_records() {
  MdnsInfo in = info();
  uint8_t out[512];
  size_t n = mdnsAnnounce(in, out, sizeof(out));
  TEST_ASSERT_TRUE(n > 0);
  uint16_t id, qd, an, ar;
  auto recs = parse(out, n, id, qd, an, ar);
  TEST_ASSERT_EQUAL_UINT16(4, an);
  TEST_ASSERT_NOT_NULL(find(recs, 12));
  TEST_ASSERT_NOT_NULL(find(recs, 33));
  TEST_ASSERT_NOT_NULL(find(recs, 16));
  TEST_ASSERT_NOT_NULL(find(recs, 1));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_bridge_query_gets_unicast_answer_with_everything);
  RUN_TEST(test_legacy_query_echoes_id);
  RUN_TEST(test_multicast_query_for_host_address);
  RUN_TEST(test_qu_bit_from_5353_is_unicast);
  RUN_TEST(test_instance_srv_and_service_enumeration);
  RUN_TEST(test_ignores_other_names_responses_and_garbage);
  RUN_TEST(test_announcement_contains_all_records);
  return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd firmware && .venv/bin/pio test -e native -f test_mdns`
Expected: FAIL — `miblo_mdns.h: No such file or directory`.

- [ ] **Step 3: Implement** — `firmware/lib/miblo_core/src/miblo_mdns.h`

```cpp
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace miblo {

// Respondedor mDNS/DNS-SD mínimo para `_miblo._tcp.local` (contrato: Plano 1, Task 7).
// Responde PTR do serviço (e de _services._dns-sd._udp), SRV/TXT da instância e A do host.
// Consulta de porta ≠ 5353 (legacy/"one-shot") ou com bit QU → resposta unicast para a origem.
struct MdnsInfo {
  const char* instance;  // "Miblo-4F2A"  → Miblo-4F2A._miblo._tcp.local
  const char* host;      // "miblo-4f2a"  → miblo-4f2a.local
  uint8_t ip[4];
  uint16_t port;
  const char* txt[4];    // "id=miblo-4f2a", "name=Miblo-4F2A", "fw=0.1.0"
  uint8_t txtCount;
};

struct MdnsReply {
  size_t len;    // 0 = não responder
  bool unicast;  // true = enviar para o IP/porta de origem
};

constexpr uint16_t kMdnsPort = 5353;

MdnsReply mdnsRespond(const uint8_t* pkt, size_t len, uint16_t srcPort, const MdnsInfo& info, uint8_t* out,
                      size_t cap);

// Anúncio não solicitado (PTR + SRV + TXT + A), enviado por multicast ao conectar.
size_t mdnsAnnounce(const MdnsInfo& info, uint8_t* out, size_t cap);

}  // namespace miblo
```

`firmware/lib/miblo_core/src/miblo_mdns.cpp`:

```cpp
#include "miblo_mdns.h"

#include <ctype.h>
#include <string.h>

namespace miblo {

namespace {

constexpr uint16_t T_A = 1;
constexpr uint16_t T_PTR = 12;
constexpr uint16_t T_TXT = 16;
constexpr uint16_t T_SRV = 33;
constexpr uint16_t T_ANY = 255;
constexpr uint16_t CLASS_IN = 1;
constexpr uint16_t CACHE_FLUSH = 0x8000;
constexpr const char* kService = "_miblo._tcp.local";
constexpr const char* kEnum = "_services._dns-sd._udp.local";

class Writer {
 public:
  Writer(uint8_t* out, size_t cap) : out_(out), cap_(cap) {}
  bool ok() const { return ok_; }
  size_t size() const { return n_; }
  void u8(uint8_t v) {
    if (n_ + 1 > cap_) {
      ok_ = false;
      return;
    }
    out_[n_++] = v;
  }
  void u16(uint16_t v) {
    u8((uint8_t)(v >> 8));
    u8((uint8_t)v);
  }
  void u32(uint32_t v) {
    u16((uint16_t)(v >> 16));
    u16((uint16_t)v);
  }
  void bytes(const void* p, size_t len) {
    for (size_t i = 0; i < len; i++) u8(((const uint8_t*)p)[i]);
  }
  // Nome em labels separados por '.', sem compressão. `first` é um label único (pode ter '.').
  void name(const char* first, const char* rest) {
    if (first) label(first, strlen(first));
    const char* p = rest;
    while (*p) {
      const char* dot = strchr(p, '.');
      size_t len = dot ? (size_t)(dot - p) : strlen(p);
      label(p, len);
      p += len;
      if (*p == '.') p++;
    }
    u8(0);
  }
  size_t mark() const { return n_; }
  void patch16(size_t at, uint16_t v) {
    if (at + 2 <= n_) {
      out_[at] = (uint8_t)(v >> 8);
      out_[at + 1] = (uint8_t)v;
    }
  }

 private:
  void label(const char* s, size_t len) {
    if (len > 63) len = 63;
    u8((uint8_t)len);
    bytes(s, len);
  }
  uint8_t* out_;
  size_t cap_;
  size_t n_ = 0;
  bool ok_ = true;
};

// Lê um nome (com ponteiros de compressão) para `dst` como "a.b.c". Retorna o offset após o nome.
bool readName(const uint8_t* pkt, size_t len, size_t off, char* dst, size_t cap, size_t& next) {
  size_t used = 0;
  bool jumped = false;
  int guard = 0;
  while (true) {
    if (off >= len || ++guard > 64) return false;
    uint8_t l = pkt[off];
    if (l == 0) {
      if (!jumped) next = off + 1;
      break;
    }
    if ((l & 0xC0) == 0xC0) {
      if (off + 1 >= len) return false;
      if (!jumped) next = off + 2;
      jumped = true;
      off = ((size_t)(l & 0x3F) << 8) | pkt[off + 1];
      continue;
    }
    if (off + 1 + l > len) return false;
    if (used && used + 1 < cap) dst[used++] = '.';
    for (uint8_t i = 0; i < l && used + 1 < cap; i++) dst[used++] = (char)pkt[off + 1 + i];
    off += 1 + l;
  }
  dst[used < cap ? used : cap - 1] = 0;
  return true;
}

bool sameName(const char* a, const char* b) {
  while (*a && *b) {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    a++;
    b++;
  }
  return *a == 0 && *b == 0;
}

// "<instance>._miblo._tcp.local" e "<host>.local" para comparação.
void join(char* dst, size_t cap, const char* a, const char* b) {
  size_t la = strlen(a);
  size_t lb = strlen(b);
  if (la + 1 + lb + 1 > cap) {
    dst[0] = 0;
    return;
  }
  memcpy(dst, a, la);
  dst[la] = '.';
  memcpy(dst + la + 1, b, lb + 1);
}

void rrHeader(Writer& w, uint16_t type, uint16_t cls, uint32_t ttl) {
  w.u16(type);
  w.u16(cls);
  w.u32(ttl);
}

void writePtr(Writer& w, const MdnsInfo& info, uint32_t ttl) {
  w.name(nullptr, kService);
  rrHeader(w, T_PTR, CLASS_IN, ttl);
  size_t at = w.mark();
  w.u16(0);
  w.name(info.instance, kService);
  w.patch16(at, (uint16_t)(w.mark() - at - 2));
}

void writeSrv(Writer& w, const MdnsInfo& info, uint32_t ttl, uint16_t flush) {
  w.name(info.instance, kService);
  rrHeader(w, T_SRV, CLASS_IN | flush, ttl);
  size_t at = w.mark();
  w.u16(0);
  w.u16(0);  // prioridade
  w.u16(0);  // peso
  w.u16(info.port);
  w.name(info.host, "local");
  w.patch16(at, (uint16_t)(w.mark() - at - 2));
}

void writeTxt(Writer& w, const MdnsInfo& info, uint32_t ttl, uint16_t flush) {
  w.name(info.instance, kService);
  rrHeader(w, T_TXT, CLASS_IN | flush, ttl);
  size_t at = w.mark();
  w.u16(0);
  for (uint8_t i = 0; i < info.txtCount; i++) {
    size_t l = strlen(info.txt[i]);
    if (l > 255) l = 255;
    w.u8((uint8_t)l);
    w.bytes(info.txt[i], l);
  }
  w.patch16(at, (uint16_t)(w.mark() - at - 2));
}

void writeA(Writer& w, const MdnsInfo& info, uint32_t ttl, uint16_t flush) {
  w.name(info.host, "local");
  rrHeader(w, T_A, CLASS_IN | flush, ttl);
  w.u16(4);
  w.bytes(info.ip, 4);
}

void writeEnum(Writer& w, uint32_t ttl) {
  w.name(nullptr, kEnum);
  rrHeader(w, T_PTR, CLASS_IN, ttl);
  size_t at = w.mark();
  w.u16(0);
  w.name(nullptr, kService);
  w.patch16(at, (uint16_t)(w.mark() - at - 2));
}

enum Want : uint8_t { W_PTR = 1, W_SRV = 2, W_TXT = 4, W_A = 8, W_ENUM = 16 };

}  // namespace

MdnsReply mdnsRespond(const uint8_t* pkt, size_t len, uint16_t srcPort, const MdnsInfo& info, uint8_t* out,
                      size_t cap) {
  MdnsReply none{0, false};
  if (len < 12) return none;
  uint16_t flags = (uint16_t)((pkt[2] << 8) | pkt[3]);
  if (flags & 0x8000) return none;  // é resposta, não consulta
  uint16_t qd = (uint16_t)((pkt[4] << 8) | pkt[5]);
  if (qd == 0 || qd > 16) return none;

  char instFull[128];
  char hostFull[80];
  join(instFull, sizeof(instFull), info.instance, kService);
  join(hostFull, sizeof(hostFull), info.host, "local");

  const bool legacy = srcPort != kMdnsPort;
  bool unicast = legacy;
  uint8_t want = 0;
  size_t off = 12;
  size_t qStart = off;
  for (uint16_t i = 0; i < qd; i++) {
    char name[128];
    size_t next;
    if (!readName(pkt, len, off, name, sizeof(name), next)) return none;
    if (next + 4 > len) return none;
    uint16_t qtype = (uint16_t)((pkt[next] << 8) | pkt[next + 1]);
    uint16_t qclass = (uint16_t)((pkt[next + 2] << 8) | pkt[next + 3]);
    off = next + 4;
    uint8_t hit = 0;
    if (sameName(name, kService) && (qtype == T_PTR || qtype == T_ANY)) hit = W_PTR | W_SRV | W_TXT | W_A;
    else if (sameName(name, kEnum) && (qtype == T_PTR || qtype == T_ANY)) hit = W_ENUM;
    else if (sameName(name, instFull) && (qtype == T_SRV || qtype == T_ANY)) hit = W_SRV | W_TXT | W_A;
    else if (sameName(name, instFull) && qtype == T_TXT) hit = W_TXT;
    else if (sameName(name, hostFull) && (qtype == T_A || qtype == T_ANY)) hit = W_A;
    if (hit && (qclass & 0x8000)) unicast = true;
    want |= hit;
  }
  if (!want) return none;
  size_t qEnd = off;

  const uint32_t ttl = legacy ? 10 : 120;
  const uint16_t flush = legacy ? 0 : CACHE_FLUSH;
  Writer w(out, cap);
  w.u16(legacy ? (uint16_t)((pkt[0] << 8) | pkt[1]) : 0);  // id
  w.u16(0x8400);                                             // resposta autoritativa
  w.u16(legacy ? qd : 0);
  size_t anAt = w.mark();
  w.u16(0);
  w.u16(0);
  size_t arAt = w.mark();
  w.u16(0);
  if (legacy) w.bytes(pkt + qStart, qEnd - qStart);  // ecoa as perguntas

  uint16_t an = 0;
  uint16_t ar = 0;
  if (want & W_ENUM) {
    writeEnum(w, ttl);
    an++;
  }
  if (want & W_PTR) {
    writePtr(w, info, ttl);
    an++;
    writeSrv(w, info, ttl, flush);
    writeTxt(w, info, ttl, flush);
    writeA(w, info, ttl, flush);
    ar += 3;
  } else {
    if (want & W_SRV) {
      writeSrv(w, info, ttl, flush);
      an++;
    }
    if (want & W_TXT) {
      writeTxt(w, info, ttl, flush);
      an++;
    }
    if (want & W_A) {
      writeA(w, info, ttl, flush);
      if (want & W_SRV) ar++;
      else an++;
    }
  }
  w.patch16(anAt, an);
  w.patch16(arAt, ar);
  if (!w.ok()) return none;
  return MdnsReply{w.size(), unicast};
}

size_t mdnsAnnounce(const MdnsInfo& info, uint8_t* out, size_t cap) {
  Writer w(out, cap);
  w.u16(0);
  w.u16(0x8400);
  w.u16(0);
  w.u16(4);
  w.u16(0);
  w.u16(0);
  writePtr(w, info, 120);
  writeSrv(w, info, 120, CACHE_FLUSH);
  writeTxt(w, info, 120, CACHE_FLUSH);
  writeA(w, info, 120, CACHE_FLUSH);
  return w.ok() ? w.size() : 0;
}

}  // namespace miblo
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd firmware && .venv/bin/pio test -e native -f test_mdns`
Expected: PASS — `7 test cases: 7 succeeded`.

- [ ] **Step 5 (opcional, recomendado): conferir com o parser do bridge**

Se `plugin/lib/mdns.js` já existe: compile um executável de 10 linhas que imprime em hex a resposta de `mdnsRespond` para a consulta do teste `test_bridge_query_gets_unicast_answer_with_everything` e rode `node -e "import('./plugin/lib/mdns.js').then(m=>console.log(m.resolveDevices(m.parseMessage(Buffer.from(process.argv[1],'hex')),'_miblo._tcp.local')))" <hex>`.
Expected: `[ { id: 'miblo-4f2a', name: 'Miblo-4F2A', addr: '192.168.0.42:80' } ]`. Não commitar o executável.

- [ ] **Step 6: Commit**

```bash
git add firmware/lib/miblo_core/src/miblo_mdns.h firmware/lib/miblo_core/src/miblo_mdns.cpp firmware/test/test_mdns/test_main.cpp
git commit -m "feat(firmware): DNS-SD responder for _miblo._tcp with unicast replies"
```

---

### Task 9: `miblo_ui` — interface `Canvas`, base dos layouts e telas de sistema

**Files:**
- Create: `firmware/lib/miblo_ui/src/ui_canvas.h`
- Create: `firmware/lib/miblo_ui/src/ui_screens.h`
- Create: `firmware/lib/miblo_ui/src/ui_base.cpp`
- Create: `firmware/lib/miblo_ui/src/ui_system.cpp`
- Create: `firmware/test/support/fake_canvas.h`
- Test: `firmware/test/test_ui_system/test_main.cpp`

**Interfaces:**
- Consumes: `tr`, `S`, `Lang` (Task 3); `RegionCache`, `hashStr`, `hashInt`, `kHashSeed` (Task 4); `formatElapsed`, `formatHHMM` (Task 1); ricmoo/QRCode.
- Produces: `ui::ScreenSpec {w, h}`; `enum class ui::Font {Small, SmallBold, Body, BodyBold, Title, Hero, NumL, NumM, Count}`; `enum class ui::Align {Left, Center, Right}`; `ui::color::*` (paleta RGB565 dos mockups); `class ui::Canvas` (métodos virtuais `spec`, `fillRect`, `fillRoundRect`, `drawRect`, `fillCircle`, `wideLine`, `arc`, `text(x, y, s, font, fg, align, maxW) → int`, `textWidth`); `screens::bind(ui::Canvas&)`, `canvas()`, `reset()`, `region(id, hash, x, y, w, h, bg) → bool`, `t(Lang, S) → const char*`, `X/Y/Sz(int) → int`, `bar`, `check`, `mascot`, `qr`, e as telas `boot`, `setup`, `welcome`, `paired`, `code`, `updating`, `disconnected` (assinaturas em `ui_screens.h`).

Layouts seguem os mockups (`setup-flow.html` e `overview-adaptive.html`): coordenadas na grade de 240, sempre passadas por `X()`, `Y()` ou `Sz()`.

- [ ] **Step 1: Canvas falso para os testes** — `firmware/test/support/fake_canvas.h`

```cpp
#pragma once
// Canvas falso para testar layouts no host: registra textos/arcos e confere se tudo cabe na tela.
#include <stdlib.h>

#include <string>
#include <vector>

#include "miblo_utf8.h"
#include "ui_canvas.h"

class FakeCanvas : public ui::Canvas {
 public:
  explicit FakeCanvas(ui::ScreenSpec s) : spec_(s) {}
  ui::ScreenSpec spec() const override { return spec_; }
  void fillRect(int x, int y, int w, int h, uint16_t) override { box(x, y, w, h); }
  void fillRoundRect(int x, int y, int w, int h, int, uint16_t) override { box(x, y, w, h); }
  void drawRect(int x, int y, int w, int h, uint16_t) override { box(x, y, w, h); }
  void fillCircle(int cx, int cy, int r, uint16_t) override { box(cx - r, cy - r, 2 * r, 2 * r); }
  void wideLine(int x0, int y0, int x1, int y1, int, uint16_t, uint16_t) override {
    box(x0 < x1 ? x0 : x1, y0 < y1 ? y0 : y1, abs(x1 - x0), abs(y1 - y0));
  }
  void arc(int cx, int cy, int r, int, int a0, int a1, uint16_t, uint16_t) override {
    box(cx - r, cy - r, 2 * r, 2 * r);
    arcs.push_back(a1 - a0);
  }
  // Texto: 6 px por caractere, 10 px de altura acima da linha de base.
  int text(int x, int y, const char* s, ui::Font, uint16_t, ui::Align a, int maxW) override {
    int w = textWidth(s, ui::Font::Small);
    if (w > maxW) w = maxW;
    const int left = a == ui::Align::Left ? x : (a == ui::Align::Center ? x - w / 2 : x - w);
    box(left, y - 10, w, 10);
    texts.push_back(s ? s : "");
    return w;
  }
  int textWidth(const char* s, ui::Font) override { return 6 * (int)miblo::utf8Length(s ? s : ""); }

  bool drew(const std::string& needle) const {
    for (const auto& t : texts) {
      if (t.find(needle) != std::string::npos) return true;
    }
    return false;
  }
  void clearLog() {
    texts.clear();
    arcs.clear();
    calls = 0;
  }

  ui::ScreenSpec spec_;
  std::vector<std::string> texts;
  std::vector<int> arcs;
  int calls = 0;
  int outOfBounds = 0;

 private:
  void box(int x, int y, int w, int h) {
    calls++;
    if (x < 0 || y < 0 || x + w > spec_.w || y + h > spec_.h) outOfBounds++;
  }
};
```

- [ ] **Step 2: Write the failing test** — `firmware/test/test_ui_system/test_main.cpp`

```cpp
#include <unity.h>

#include "../support/fake_canvas.h"
#include "ui_screens.h"

using miblo::Lang;
using miblo::S;

void setUp() {}
void tearDown() {}

static void renderSystem(FakeCanvas& fc) {
  screens::bind(fc);
  screens::reset();
  screens::boot(Lang::En, 1);
  screens::reset();
  screens::setup(Lang::Ru, "Miblo-Setup-4F2A", true);
  screens::reset();
  screens::welcome(Lang::PtBR, "4827", "192.168.0.42");
  screens::reset();
  screens::paired(Lang::Zh, "MacBook-Marcus", "Overview", "miblo-4f2a");
  screens::reset();
  screens::code(Lang::De, S::CodeUpdate, "1234", 299);
  screens::reset();
  screens::updating(Lang::Fr, 42);
  screens::reset();
  screens::disconnected(Lang::It, true, 14, 32, 1, 28, "192.168.0.42", "miblo-4f2a", "4827");
}

static void test_system_screens_fit_any_resolution() {
  const ui::ScreenSpec specs[] = {{240, 240}, {320, 240}, {480, 320}, {170, 320}};
  for (const auto& sp : specs) {
    FakeCanvas fc(sp);
    renderSystem(fc);
    TEST_ASSERT_TRUE(fc.calls > 20);
    TEST_ASSERT_EQUAL_INT(0, fc.outOfBounds);
  }
}

static void test_setup_and_welcome_content() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  screens::reset();
  fc.clearLog();
  screens::setup(Lang::PtBR, "Miblo-Setup-4F2A", false);
  TEST_ASSERT_TRUE(fc.drew("Olá!"));
  TEST_ASSERT_TRUE(fc.drew("Miblo-Setup-4F2A"));
  TEST_ASSERT_TRUE(fc.calls > 100);  // módulos do QR
  screens::reset();
  fc.clearLog();
  screens::setup(Lang::PtBR, "Miblo-Setup-4F2A", true);
  TEST_ASSERT_TRUE(fc.drew("Senha incorreta"));
  screens::reset();
  fc.clearLog();
  screens::welcome(Lang::En, "4827", "192.168.0.42");
  TEST_ASSERT_TRUE(fc.drew("/plugin install miblo@miblo"));
  TEST_ASSERT_TRUE(fc.drew("4827"));
  TEST_ASSERT_TRUE(fc.drew("192.168.0.42"));
}

static void test_regions_only_redraw_on_change() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  screens::reset();
  screens::code(Lang::En, S::CodeUpdate, "1234", 299);
  fc.clearLog();
  screens::code(Lang::En, S::CodeUpdate, "1234", 299);
  TEST_ASSERT_EQUAL_INT(0, fc.calls);
  screens::code(Lang::En, S::CodeUpdate, "1234", 298);  // só a contagem muda
  TEST_ASSERT_TRUE(fc.drew("expires in 4:58"));
  TEST_ASSERT_FALSE(fc.drew("1234"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_system_screens_fit_any_resolution);
  RUN_TEST(test_setup_and_welcome_content);
  RUN_TEST(test_regions_only_redraw_on_change);
  return UNITY_END();
}
```

- [ ] **Step 3: Run test to verify it fails**

Run: `cd firmware && .venv/bin/pio test -e native -f test_ui_system`
Expected: FAIL — `ui_canvas.h: No such file or directory`.

- [ ] **Step 4: Implement** — `firmware/lib/miblo_ui/src/ui_canvas.h`

```cpp
#pragma once
#include <stdint.h>

// Interface de desenho independente de placa. Cada placa implementa um Canvas (ex.: TFT_eSPI +
// fontes u8g2 na GeekMagic Ultra); os layouts em ui_screens.h só falam com esta interface.
namespace ui {

struct ScreenSpec {
  int16_t w;
  int16_t h;
};

// Estilos de texto; a placa mapeia cada um para fontes reais (com fallback latim/cirílico/CJK).
enum class Font : uint8_t { Small, SmallBold, Body, BodyBold, Title, Hero, NumL, NumM, Count };
enum class Align : uint8_t { Left, Center, Right };

// Paleta dos mockups (RGB565).
namespace color {
constexpr uint16_t BG = 0x0841;          // #0b0b0d
constexpr uint16_t TEXT = 0xEF7D;        // #eeeeee
constexpr uint16_t MUTED = 0xAD55;       // #aaaaaa
constexpr uint16_t DIM = 0x73AE;         // #777777
constexpr uint16_t FAINT = 0x52AA;       // #555555
constexpr uint16_t AMBER = 0xF524;       // #f5a524 precisa de você
constexpr uint16_t GREEN = 0x4EF0;       // #4ade80 rodando
constexpr uint16_t BLUE = 0x653F;        // #60a5fa terminou
constexpr uint16_t FLASH_BLUE = 0x3C1E;  // #3b82f6 flash azul
constexpr uint16_t CORAL = 0xDBAA;       // #d97757 janela de 5h / mascote
constexpr uint16_t VIOLET = 0x8C5E;      // #8b8bf5 semana
constexpr uint16_t RED = 0xEA28;         // #ef4444 limite ≥ 95%
constexpr uint16_t TRACK = 0x2125;       // #262629 fundo das barras
constexpr uint16_t DIVIDER = 0x2104;     // #222222
constexpr uint16_t CARD = 0x10A3;        // #16161a
constexpr uint16_t CARD_AMBER = 0x18A1;  // #1c160a
constexpr uint16_t CMD_BG = 0x18C3;      // #1a1a1e
constexpr uint16_t BLACK = 0x0000;
constexpr uint16_t WHITE = 0xFFFF;
}  // namespace color

class Canvas {
 public:
  virtual ~Canvas() = default;
  virtual ScreenSpec spec() const = 0;
  virtual void fillRect(int x, int y, int w, int h, uint16_t c) = 0;
  virtual void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) = 0;
  virtual void drawRect(int x, int y, int w, int h, uint16_t c) = 0;
  virtual void fillCircle(int cx, int cy, int r, uint16_t c) = 0;
  virtual void wideLine(int x0, int y0, int x1, int y1, int width, uint16_t c, uint16_t bg) = 0;
  // Arco anti-aliased: ângulos em graus, 0 = 6 h, sentido horário (convenção do TFT_eSPI).
  virtual void arc(int cx, int cy, int r, int ir, int a0, int a1, uint16_t fg, uint16_t bg) = 0;
  // Texto UTF-8 com linha de base em y; corta com "..." se passar de maxW. Retorna a largura.
  // Glyph ausente nas fontes → retângulo, nunca trava.
  virtual int text(int x, int y, const char* s, Font f, uint16_t fg, Align a, int maxW) = 0;
  virtual int textWidth(const char* s, Font f) = 0;
};

}  // namespace ui
```

`firmware/lib/miblo_ui/src/ui_screens.h` (a Task 10 acrescenta as telas principais neste arquivo):

```cpp
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "miblo_config.h"
#include "miblo_i18n.h"
#include "miblo_overview.h"
#include "miblo_snapshot.h"
#include "ui_canvas.h"

// Layouts de todas as telas. Coordenadas são pensadas numa grade de 240×240 e escaladas pelo
// ScreenSpec do Canvas (X/Y/Sz em ui_base.cpp). Cada função é chamada a cada quadro e só
// redesenha as regiões cujo conteúdo mudou (RegionCache) — sem framebuffer.
namespace screens {

using miblo::Lang;

// Liga os layouts a um Canvas (uma vez, no boot).
void bind(ui::Canvas& canvas);
ui::Canvas& canvas();
// Troca de tela: limpa tudo e invalida o cache de regiões.
void reset();
// Se o hash da região mudou, limpa o retângulo (coordenadas já escaladas) e retorna true.
bool region(uint8_t id, uint32_t hash, int x, int y, int w, int h, uint16_t bg = ui::color::BG);
// Texto traduzido (4 buffers rotativos).
const char* t(Lang lang, miblo::S id);
// Escala da grade de 240: X para larguras/posições horizontais, Y para verticais, Sz para tamanhos.
int X(int v);
int Y(int v);
int Sz(int v);

// Primitivas compostas.
void bar(int x, int y, int w, int h, uint8_t pct, uint16_t fg);
void check(int cx, int cy, int size, uint16_t c);
void mascot(int cx, int cy, uint8_t frame);  // placeholder do mascote Miblo (3 quadros)
void qr(const char* payload, int x, int y, int scale);

// ---- telas de sistema (§4.5) ----
void boot(Lang lang, uint8_t frame);
void setup(Lang lang, const char* apSsid, bool wrongPassword);
void welcome(Lang lang, const char* pairCode, const char* ip);
void paired(Lang lang, const char* host, const char* modeName, const char* mdnsHost);
void code(Lang lang, miblo::S title, const char* code, uint32_t remainingSec);
void updating(Lang lang, uint8_t pct);
void disconnected(Lang lang, bool timeValid, int hour, int minute, int wday, int mday, const char* ip,
                  const char* mdnsHost, const char* pairCode);

}  // namespace screens
```

`firmware/lib/miblo_ui/src/ui_base.cpp`:

```cpp
#include <qrcode.h>

#include "ui_screens.h"

namespace screens {

namespace color = ui::color;

static ui::Canvas* g_canvas = nullptr;
static int16_t g_w = 240;
static int16_t g_h = 240;
static miblo::RegionCache g_cache;

void bind(ui::Canvas& c) {
  g_canvas = &c;
  g_w = c.spec().w;
  g_h = c.spec().h;
  g_cache.invalidate();
}

ui::Canvas& canvas() { return *g_canvas; }

int X(int v) { return v * g_w / 240; }
int Y(int v) { return v * g_h / 240; }
int Sz(int v) { return v * (g_w < g_h ? g_w : g_h) / 240; }

void reset() {
  g_canvas->fillRect(0, 0, g_w, g_h, color::BG);
  g_cache.invalidate();
}

bool region(uint8_t id, uint32_t hash, int x, int y, int w, int h, uint16_t bg) {
  if (!g_cache.changed(id, hash)) return false;
  g_canvas->fillRect(x, y, w, h, bg);
  return true;
}

const char* t(Lang lang, miblo::S id) {
  static char buf[4][128];
  static uint8_t next = 0;
  char* b = buf[next];
  next = (uint8_t)((next + 1) % 4);
  miblo::tr(lang, id, b, sizeof(buf[0]));
  return b;
}

void bar(int x, int y, int w, int h, uint8_t pct, uint16_t fg) {
  const int r = h / 2;
  g_canvas->fillRoundRect(x, y, w, h, r, color::TRACK);
  const int fw = (int)((long)w * (pct > 100 ? 100 : pct) / 100);
  if (fw >= h) g_canvas->fillRoundRect(x, y, fw, h, r, fg);
  else if (fw > 0) g_canvas->fillRect(x, y, fw, h, fg);
}

void check(int cx, int cy, int size, uint16_t c) {
  const int w = size / 6 < 2 ? 2 : size / 6;
  g_canvas->wideLine(cx - size / 2, cy, cx - size / 6, cy + size / 3, w, c, color::BG);
  g_canvas->wideLine(cx - size / 6, cy + size / 3, cx + size / 2, cy - size / 3, w, c, color::BG);
}

void mascot(int cx, int cy, uint8_t frame) {
  // Placeholder do Miblo: bolha coral com olhos. 0 = normal, 1 = piscando, 2 = pulinho.
  const int u = Sz(1) < 1 ? 1 : Sz(1);
  const int bounce = (frame % 3 == 2) ? -6 * u : 0;
  g_canvas->fillRect(cx - 46 * u, cy - 46 * u, 92 * u, 92 * u, color::BG);
  const int top = cy - 32 * u + bounce;
  g_canvas->fillRoundRect(cx - 40 * u, top, 80 * u, 60 * u, 24 * u, color::CORAL);
  g_canvas->fillRect(cx - 26 * u, top + 58 * u, 12 * u, 8 * u, color::CORAL);  // pezinhos
  g_canvas->fillRect(cx + 14 * u, top + 58 * u, 12 * u, 8 * u, color::CORAL);
  const int eyeY = top + 26 * u;
  if (frame % 3 == 1) {
    g_canvas->fillRect(cx - 22 * u, eyeY - u, 14 * u, 3 * u, color::BG);
    g_canvas->fillRect(cx + 8 * u, eyeY - u, 14 * u, 3 * u, color::BG);
  } else {
    g_canvas->fillCircle(cx - 15 * u, eyeY, 7 * u, color::WHITE);
    g_canvas->fillCircle(cx + 15 * u, eyeY, 7 * u, color::WHITE);
    g_canvas->fillCircle(cx - 13 * u, eyeY + u, 3 * u, color::BG);
    g_canvas->fillCircle(cx + 17 * u, eyeY + u, 3 * u, color::BG);
  }
}

void qr(const char* payload, int x, int y, int scale) {
  QRCode code;
  uint8_t data[(29 * 29 + 7) / 8];  // = qrcode_getBufferSize(3): versão 3, 29×29 módulos
  qrcode_initText(&code, data, 3, ECC_LOW, payload);
  const int quiet = 2;
  const int size = (code.size + quiet * 2) * scale;
  g_canvas->fillRect(x, y, size, size, color::WHITE);
  for (uint8_t my = 0; my < code.size; my++) {
    for (uint8_t mx = 0; mx < code.size; mx++) {
      if (qrcode_getModule(&code, mx, my)) {
        g_canvas->fillRect(x + (mx + quiet) * scale, y + (my + quiet) * scale, scale, scale, color::BLACK);
      }
    }
  }
}

}  // namespace screens
```

`firmware/lib/miblo_ui/src/ui_system.cpp`:

```cpp
#include <stdio.h>
#include <string.h>

#include "miblo_format.h"
#include "ui_screens.h"

namespace screens {

using miblo::hashInt;
using miblo::hashStr;
using miblo::kHashSeed;
using miblo::S;
using ui::Align;
using ui::Font;
namespace color = ui::color;

static ui::Canvas& C() { return canvas(); }

void boot(Lang lang, uint8_t frame) {
  if (region(0, hashInt(kHashSeed, frame % 3), X(74), Y(54), Sz(92), Sz(92))) mascot(X(120), Y(100), frame);
  if (region(1, hashInt(kHashSeed, (uint32_t)lang), 0, Y(150), X(240), Y(70))) {
    C().text(X(120), Y(176), "Miblo", Font::Title, color::TEXT, Align::Center, X(240));
    C().text(X(120), Y(204), t(lang, S::Connecting), Font::Small, color::MUTED, Align::Center, X(232));
  }
}

void setup(Lang lang, const char* apSsid, bool wrongPassword) {
  uint32_t h = hashStr(hashInt(hashInt(kHashSeed, (uint32_t)lang), wrongPassword), apSsid);
  if (!region(0, h, 0, 0, X(240), Y(240))) return;
  if (wrongPassword) {
    C().text(X(120), Y(26), t(lang, S::WrongPassword), Font::BodyBold, color::RED, Align::Center, X(232));
  } else {
    C().text(X(120), Y(28), t(lang, S::Hello), Font::Title, color::TEXT, Align::Center, X(232));
  }
  C().text(X(120), Y(48), t(lang, S::ScanPhone), Font::Small, color::MUTED, Align::Center, X(232));
  char payload[64];
  snprintf(payload, sizeof(payload), "WIFI:S:%s;;", apSsid);
  const int scale = Sz(4) < 2 ? 2 : Sz(4);
  const int size = (29 + 4) * scale;  // QR versão 3 (29 módulos) + margem de 2 módulos
  qr(payload, (X(240) - size) / 2, Y(56), scale);
  C().text(X(120), Y(206), t(lang, S::OrJoin), Font::Small, color::MUTED, Align::Center, X(232));
  C().text(X(120), Y(228), apSsid, Font::BodyBold, color::AMBER, Align::Center, X(232));
}

void welcome(Lang lang, const char* pairCode, const char* ip) {
  uint32_t h = hashStr(hashStr(hashInt(kHashSeed, (uint32_t)lang), pairCode), ip);
  if (!region(0, h, 0, 0, X(240), Y(240))) return;
  const char* ok = t(lang, S::WifiConnected);
  const int w = C().textWidth(ok, Font::BodyBold);
  check(X(120) - w / 2 - Sz(4), Y(22), Sz(14), color::GREEN);
  C().text(X(120) + Sz(8), Y(28), ok, Font::BodyBold, color::GREEN, Align::Center, X(200));
  C().text(X(12), Y(58), t(lang, S::RunInClaude), Font::Small, color::MUTED, Align::Left, X(216));
  C().fillRoundRect(X(12), Y(66), X(216), Y(30), Sz(4), color::CMD_BG);
  C().text(X(20), Y(86), "/plugin install miblo@miblo", Font::Small, color::CORAL, Align::Left, X(200));
  C().text(X(120), Y(128), t(lang, S::PairingCode), Font::Small, color::MUTED, Align::Center, X(232));
  C().text(X(120), Y(176), pairCode, Font::NumL, color::TEXT, Align::Center, X(232));
  C().text(X(120), Y(228), ip, Font::Small, color::FAINT, Align::Center, X(232));
}

void paired(Lang lang, const char* host, const char* modeName, const char* mdnsHost) {
  uint32_t h = hashStr(hashStr(hashStr(hashInt(kHashSeed, (uint32_t)lang), host), modeName), mdnsHost);
  if (!region(0, h, 0, 0, X(240), Y(240))) return;
  check(X(120), Y(70), Sz(48), color::GREEN);
  C().text(X(120), Y(132), t(lang, S::PairedWith), Font::Title, color::TEXT, Align::Center, X(232));
  C().text(X(120), Y(158), host, Font::Body, color::MUTED, Align::Center, X(220));
  C().text(X(120), Y(208), modeName, Font::Small, color::FAINT, Align::Center, X(232));
  char url[48];
  snprintf(url, sizeof(url), "http://%s.local", mdnsHost);
  C().text(X(120), Y(226), url, Font::Small, color::FAINT, Align::Center, X(232));
}

void code(Lang lang, S title, const char* codeStr, uint32_t remainingSec) {
  uint32_t h = hashStr(hashInt(hashInt(kHashSeed, (uint32_t)lang), (uint32_t)title), codeStr);
  if (region(0, h, 0, Y(30), X(240), Y(120))) {
    C().text(X(120), Y(64), t(lang, title), Font::Body, color::TEXT, Align::Center, X(232));
    C().text(X(120), Y(132), codeStr, Font::NumL, color::AMBER, Align::Center, X(232));
  }
  if (region(1, hashInt(kHashSeed, remainingSec), 0, Y(150), X(240), Y(40))) {
    char left[16];
    char line[64];
    miblo::formatElapsed(remainingSec, left, sizeof(left));
    snprintf(line, sizeof(line), t(lang, S::ExpiresIn), left);
    C().text(X(120), Y(174), line, Font::Small, color::MUTED, Align::Center, X(232));
  }
}

void updating(Lang lang, uint8_t pct) {
  if (region(0, hashInt(kHashSeed, (uint32_t)lang), 0, Y(40), X(240), Y(60))) {
    C().text(X(120), Y(84), t(lang, S::Updating), Font::Title, color::TEXT, Align::Center, X(232));
  }
  if (region(1, hashInt(kHashSeed, pct), 0, Y(104), X(240), Y(60))) {
    bar(X(30), Y(110), X(180), Y(14), pct, color::CORAL);
    char b[8];
    snprintf(b, sizeof(b), "%u%%", (unsigned)pct);
    C().text(X(120), Y(154), b, Font::Body, color::TEXT, Align::Center, X(232));
  }
  if (region(2, hashInt(kHashSeed + 1, (uint32_t)lang), 0, Y(176), X(240), Y(30))) {
    C().text(X(120), Y(196), t(lang, S::DoNotUnplug), Font::Small, color::MUTED, Align::Center, X(232));
  }
}

void disconnected(Lang lang, bool timeValid, int hour, int minute, int wday, int mday, const char* ip,
                  const char* mdnsHost, const char* pairCode) {
  if (region(0, hashInt(kHashSeed, (uint32_t)lang), 0, 0, X(240), Y(30))) {
    C().text(X(12), Y(20), t(lang, S::Disconnected), Font::SmallBold, color::DIM, Align::Left, X(216));
  }
  uint32_t ht = timeValid ? hashInt(hashInt(kHashSeed, (uint32_t)(hour * 60 + minute)), (uint32_t)(wday * 32 + mday))
                          : 1;
  if (region(1, hashInt(ht, (uint32_t)lang), 0, Y(60), X(240), Y(90))) {
    char hhmm[8];
    if (timeValid) miblo::formatHHMM(hour, minute, hhmm, sizeof(hhmm));
    else strcpy(hhmm, "--:--");
    C().text(X(120), Y(112), hhmm, Font::NumL, color::TEXT, Align::Center, X(232));
    if (timeValid && wday >= 0 && wday < 7) {
      char date[32];
      snprintf(date, sizeof(date), "%s %d", t(lang, (S)((int)S::WdSun + wday)), mday);
      C().text(X(120), Y(140), date, Font::Small, color::MUTED, Align::Center, X(232));
    }
  }
  uint32_t hf = hashStr(hashStr(hashStr(hashInt(kHashSeed, (uint32_t)lang), ip), mdnsHost), pairCode);
  if (region(2, hf, 0, Y(156), X(240), Y(84))) {
    C().text(X(120), Y(176), t(lang, S::WaitingComputer), Font::Small, color::MUTED, Align::Center, X(232));
    char line[64];
    snprintf(line, sizeof(line), "%s \xC2\xB7 %s.local", ip, mdnsHost);
    C().text(X(120), Y(208), line, Font::Small, color::FAINT, Align::Center, X(232));
    snprintf(line, sizeof(line), "%s %s", t(lang, S::PairingCode), pairCode);
    C().text(X(120), Y(228), line, Font::Small, color::FAINT, Align::Center, X(232));
  }
}

}  // namespace screens
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cd firmware && .venv/bin/pio test -e native -f test_ui_system`
Expected: PASS — `3 test cases: 3 succeeded`.

- [ ] **Step 6: Commit**

```bash
git add firmware/lib/miblo_ui/src/ui_canvas.h firmware/lib/miblo_ui/src/ui_screens.h firmware/lib/miblo_ui/src/ui_base.cpp firmware/lib/miblo_ui/src/ui_system.cpp firmware/test/support/fake_canvas.h firmware/test/test_ui_system/test_main.cpp
git commit -m "feat(firmware): board-independent Canvas, layout grid and system screens"
```

---

### Task 10: `miblo_ui` — Visão geral adaptativa, alertas, Limites (L1) e Sessões (S1)

**Files:**
- Modify: `firmware/lib/miblo_ui/src/ui_screens.h`
- Create: `firmware/lib/miblo_ui/src/ui_main.cpp`
- Test: `firmware/test/test_ui_main/test_main.cpp`

**Interfaces:**
- Consumes: tudo da Task 9; `Snapshot`, `SessionRow` (Task 2); `sessionLine` (Task 3); `classifyOverview`, `countStates`, `selectHero`, `lastFinished`, `RunTracker`, `Pager` (Task 4); `formatAgo`, `formatCountdown`, `formatTokens`, `formatUsd` (Task 1).
- Produces: `struct screens::Clock {valid, hhmm[6], epoch}`; `screens::formatWhen(Lang, uint32_t epoch, uint32_t now, char*, size_t)`; `screens::flash(Lang, AlertKind, const char* name, uint32_t elapsedMs)`; `screens::hero(Lang, const Snapshot&, int idx, AlertKind, bool discreet, const Clock&, const RunTracker&)`; `screens::overview(Lang, const Snapshot&, Pager&, uint32_t nowMs, const Clock&, bool discreet)`; `screens::limits(Lang, const Snapshot&, const Clock&)`; `screens::sessions(Lang, const Snapshot&, Pager&, uint32_t nowMs, const Clock&, bool discreet)`.

Layouts (mockups `overview-adaptive.html`, `alert-flow.html`, `modes.html`): faixa âmbar "N AGUARDANDO · sessão" + limites grandes + lista compacta (Precisa de você); "● N RODANDO" + limites + lista (Trabalhando); "✓ TUDO PRONTO" + limites + "docs terminou há 2m" + custo do dia (Ocioso). Sem limites: custo do dia (conta sem assinatura) ou "limites indisponíveis". Cores dos limites: ≥ 80% âmbar, ≥ 95% vermelho. Lista e Sessões: 4 por página, troca a cada 5 s. Flash pisca a cada 250 ms.

- [ ] **Step 1: Write the failing test** — `firmware/test/test_ui_main/test_main.cpp`

```cpp
#include <string.h>
#include <unity.h>

#include "../support/fake_canvas.h"
#include "ui_screens.h"

using namespace miblo;
using ui::Align;
using ui::Font;

void setUp() {}
void tearDown() {}

static Snapshot snap;
static const uint32_t NOW = 1790616720;

static void session(const char* id, const char* name, SessionState st, const char* tool, const char* det,
                    uint32_t ago) {
  SessionRow& r = snap.sessions[snap.count++];
  memset(&r, 0, sizeof(r));
  strcpy(r.id, id);
  strcpy(r.name, name);
  r.st = st;
  strcpy(r.tool, tool);
  strcpy(r.det, det);
  r.since = NOW - ago;
  strcpy(r.model, "Opus");
  r.ctx = 71;
  r.tok = 412000;
}

static void attention() {
  memset(&snap, 0, sizeof(snap));
  snap.now = NOW;
  snap.hasUsage = true;
  snap.h5 = {true, 62, NOW + 7800};
  snap.d7 = {true, 38, NOW + 240000};
  snap.todayUsd = 3.5f;
  session("11111111", "api-server", SessionState::Perm, "Bash", "npm run migrate", 42);
  session("22222222", "infra", SessionState::Question, "", "", 10);
  session("33333333", "front-app", SessionState::Running, "Edit", "Header.tsx", 192);
  session("44444444", "docs", SessionState::Idle, "", "", 600);
}

static screens::Clock testClock() {
  screens::Clock c{};
  c.valid = true;
  strcpy(c.hhmm, "14:32");
  c.epoch = NOW;
  return c;
}

static void renderMain(FakeCanvas& fc) {
  screens::bind(fc);
  Pager pager(4, 5000);
  RunTracker runs;
  const screens::Clock clk = testClock();
  attention();
  screens::reset();
  screens::flash(Lang::En, AlertKind::Perm, "api-server", 0);
  screens::reset();
  screens::hero(Lang::PtBR, snap, 0, AlertKind::Perm, false, clk, runs);
  screens::reset();
  screens::hero(Lang::En, snap, 3, AlertKind::Done, false, clk, runs);
  screens::reset();
  screens::overview(Lang::PtBR, snap, pager, 0, clk, false);
  screens::reset();
  screens::limits(Lang::En, snap, clk);
  screens::reset();
  screens::sessions(Lang::En, snap, pager, 0, clk, false);
}

static void test_main_screens_fit_any_resolution() {
  const ui::ScreenSpec specs[] = {{240, 240}, {320, 240}, {480, 320}, {170, 320}};
  for (const auto& sp : specs) {
    FakeCanvas fc(sp);
    renderMain(fc);
    TEST_ASSERT_TRUE(fc.calls > 50);
    TEST_ASSERT_EQUAL_INT(0, fc.outOfBounds);
  }
}

static void test_overview_attention_content() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  attention();
  Pager pager(4, 5000);
  screens::reset();
  fc.clearLog();
  screens::overview(Lang::PtBR, snap, pager, 0, testClock(), false);
  TEST_ASSERT_TRUE(fc.drew("2 AGUARDANDO · api-server"));
  TEST_ASSERT_TRUE(fc.drew("62%"));
  TEST_ASSERT_TRUE(fc.drew("esperando há 0:42"));
  TEST_ASSERT_TRUE(fc.drew("Editando Header.tsx"));
  // segunda chamada com os mesmos dados: nada é redesenhado
  fc.clearLog();
  screens::overview(Lang::PtBR, snap, pager, 100, testClock(), false);
  TEST_ASSERT_EQUAL_INT(0, fc.calls);
}

static void test_discreet_mode_hides_details() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  attention();
  Pager pager(4, 5000);
  RunTracker runs;
  screens::reset();
  fc.clearLog();
  screens::hero(Lang::En, snap, 0, AlertKind::Perm, true, testClock(), runs);
  TEST_ASSERT_TRUE(fc.drew("Asked permission"));
  TEST_ASSERT_FALSE(fc.drew("npm run migrate"));
  screens::reset();
  fc.clearLog();
  screens::overview(Lang::En, snap, pager, 0, testClock(), true);
  TEST_ASSERT_FALSE(fc.drew("Header.tsx"));
  TEST_ASSERT_TRUE(fc.drew("Editing"));
}

static void test_limits_arc_and_cost_fallback() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  attention();
  screens::reset();
  fc.clearLog();
  screens::limits(Lang::En, snap, testClock());
  TEST_ASSERT_EQUAL_INT(2, (int)fc.arcs.size());
  TEST_ASSERT_EQUAL_INT(270, fc.arcs[0]);
  TEST_ASSERT_EQUAL_INT(167, fc.arcs[1]);  // 62% de 270°
  snap.hasUsage = false;
  screens::reset();
  fc.clearLog();
  screens::limits(Lang::PtBR, snap, testClock());
  TEST_ASSERT_TRUE(fc.drew("hoje $3.50"));
  TEST_ASSERT_TRUE(fc.drew("limites indisponíveis"));
}

static void test_sessions_pages_and_flash_blinks() {
  FakeCanvas fc({240, 240});
  screens::bind(fc);
  attention();
  session("55555555", "worker", SessionState::Running, "Bash", "npm test", 18);
  snap.more = 3;
  Pager pager(4, 5000);
  screens::reset();
  fc.clearLog();
  screens::sessions(Lang::En, snap, pager, 0, testClock(), false);
  TEST_ASSERT_TRUE(fc.drew("SESSIONS · 8"));
  TEST_ASSERT_TRUE(fc.drew("1/2"));
  TEST_ASSERT_TRUE(fc.drew("Opus · ctx 71% · 412k tok"));
  fc.clearLog();
  screens::sessions(Lang::En, snap, pager, 5000, testClock(), false);
  TEST_ASSERT_TRUE(fc.drew("2/2"));
  TEST_ASSERT_TRUE(fc.drew("worker"));

  screens::reset();
  fc.clearLog();
  screens::flash(Lang::En, AlertKind::Done, "docs", 0);
  int first = fc.calls;
  screens::flash(Lang::En, AlertKind::Done, "docs", 100);  // mesma fase: nada muda
  TEST_ASSERT_EQUAL_INT(first, fc.calls);
  screens::flash(Lang::En, AlertKind::Done, "docs", 260);  // próxima fase: redesenha
  TEST_ASSERT_TRUE(fc.calls > first);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_main_screens_fit_any_resolution);
  RUN_TEST(test_overview_attention_content);
  RUN_TEST(test_discreet_mode_hides_details);
  RUN_TEST(test_limits_arc_and_cost_fallback);
  RUN_TEST(test_sessions_pages_and_flash_blinks);
  return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd firmware && .venv/bin/pio test -e native -f test_ui_main`
Expected: FAIL — erro de compilação: `no type named 'Clock' in namespace 'screens'` (e `no member named 'overview'`).

- [ ] **Step 3: Declarar as telas principais** — em `firmware/lib/miblo_ui/src/ui_screens.h`, substituir a última linha

```cpp
}  // namespace screens
```

por:

```cpp
// ---- telas principais (§4.1–4.4) ----
struct Clock {
  bool valid;      // hora local conhecida
  char hhmm[6];    // "14:32" ou "--:--"
  uint32_t epoch;  // agora, em segundos Unix (0 = desconhecido)
};

// "16:42" (mesmo dia) ou "qui 09:00" (outro dia), no fuso local (TZ do sistema).
void formatWhen(Lang lang, uint32_t epoch, uint32_t now, char* out, size_t cap);

void flash(Lang lang, miblo::AlertKind kind, const char* name, uint32_t elapsedMs);
void hero(Lang lang, const miblo::Snapshot& s, int idx, miblo::AlertKind kind, bool discreet, const Clock& clk,
          const miblo::RunTracker& runs);
void overview(Lang lang, const miblo::Snapshot& s, miblo::Pager& pager, uint32_t nowMs, const Clock& clk,
              bool discreet);
void limits(Lang lang, const miblo::Snapshot& s, const Clock& clk);
void sessions(Lang lang, const miblo::Snapshot& s, miblo::Pager& pager, uint32_t nowMs, const Clock& clk,
              bool discreet);

}  // namespace screens
```

- [ ] **Step 4: Implement** — `firmware/lib/miblo_ui/src/ui_main.cpp`

```cpp
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "miblo_activity.h"
#include "miblo_format.h"
#include "ui_screens.h"

namespace screens {

using miblo::AlertKind;
using miblo::hashInt;
using miblo::hashStr;
using miblo::kHashSeed;
using miblo::OverviewKind;
using miblo::S;
using miblo::SessionRow;
using miblo::SessionState;
using miblo::Snapshot;
using ui::Align;
using ui::Font;
namespace color = ui::color;

// Regiões (ids do RegionCache) das telas principais.
enum : uint8_t { R_HEADER = 0, R_LIMITS = 1, R_WEEK = 2, R_DIVIDER = 3, R_ROW0 = 4, R_BODY = 9, R_FOOT = 10 };

static ui::Canvas& C() { return canvas(); }
static const char* const kDot = " \xC2\xB7 ";  // " · "

static uint16_t levelColor(uint8_t pct, uint16_t base) {
  if (pct >= 95) return color::RED;
  if (pct >= 80) return color::AMBER;
  return base;
}

static uint16_t stateColor(SessionState st) {
  switch (st) {
    case SessionState::Perm:
    case SessionState::Question: return color::AMBER;
    case SessionState::Running: return color::GREEN;
    case SessionState::Done: return color::BLUE;
    case SessionState::Idle: return color::FAINT;
  }
  return color::FAINT;
}

static bool isPending(SessionState st) { return st == SessionState::Perm || st == SessionState::Question; }

static uint32_t since(const SessionRow& r, const Clock& clk) {
  return (clk.epoch && clk.epoch > r.since) ? clk.epoch - r.since : 0;
}

// "Opus · ctx 71% · 412k tok" (partes ausentes são omitidas; tok = tokens de contexto da sessão).
static void metaLine(const SessionRow& r, char* out, size_t cap) {
  char tmp[48];
  out[0] = 0;
  if (r.model[0]) snprintf(out, cap, "%s", r.model);
  if (r.ctx >= 0) {
    snprintf(tmp, sizeof(tmp), "%sctx %d%%", out[0] ? kDot : "", r.ctx);
    strncat(out, tmp, cap - strlen(out) - 1);
  }
  if (r.tok >= 0) {
    char tk[16];
    miblo::formatTokens((uint64_t)r.tok, tk, sizeof(tk));
    snprintf(tmp, sizeof(tmp), "%s%s tok", out[0] ? kDot : "", tk);
    strncat(out, tmp, cap - strlen(out) - 1);
  }
}

void formatWhen(Lang lang, uint32_t epoch, uint32_t now, char* out, size_t cap) {
  time_t te = (time_t)epoch;
  time_t tn = (time_t)now;
  struct tm a;
  struct tm b;
  localtime_r(&te, &a);
  localtime_r(&tn, &b);
  char hhmm[8];
  miblo::formatHHMM(a.tm_hour, a.tm_min, hhmm, sizeof(hhmm));
  if (a.tm_yday == b.tm_yday && a.tm_year == b.tm_year) {
    snprintf(out, cap, "%s", hhmm);
  } else {
    snprintf(out, cap, "%s %s", t(lang, (S)((int)S::WdSun + a.tm_wday)), hhmm);
  }
}

// ---------------- alertas (§4.2) ----------------

void flash(Lang lang, AlertKind kind, const char* name, uint32_t elapsedMs) {
  (void)lang;
  const bool amber = kind != AlertKind::Done;
  const bool on = ((elapsedMs / 250) % 2) == 0;  // pisca a cada 250 ms
  const uint16_t bg = on ? (amber ? color::AMBER : color::FLASH_BLUE) : color::BG;
  const uint16_t fg = on ? (amber ? color::BLACK : color::WHITE) : (amber ? color::AMBER : color::BLUE);
  if (!region(R_BODY, hashStr(hashInt(hashInt(kHashSeed, amber), on), name), 0, 0, X(240), Y(240), bg)) return;
  if (amber) {  // "!" num círculo
    C().fillCircle(X(120), Y(90), Sz(30), fg);
    C().fillRect(X(116), Y(70), Sz(8), Y(26), bg);
    C().fillRect(X(116), Y(102), Sz(8), Sz(8), bg);
  } else {
    C().wideLine(X(96), Y(92), X(112), Y(108), Sz(8), fg, bg);
    C().wideLine(X(112), Y(108), X(146), Y(72), Sz(8), fg, bg);
  }
  C().text(X(120), Y(160), name, Font::Hero, fg, Align::Center, X(224));
}

void hero(Lang lang, const Snapshot& s, int idx, AlertKind kind, bool discreet, const Clock& clk,
          const miblo::RunTracker& runs) {
  if (idx < 0 || idx >= s.count) return;
  const SessionRow& r = s.sessions[idx];
  const bool amber = kind != AlertKind::Done;
  char buf[160];
  char tmp[48];

  uint32_t h = hashStr(hashInt(hashInt(kHashSeed, (uint32_t)lang), (uint32_t)kind), r.id);
  h = hashStr(hashStr(hashStr(h, r.name), discreet ? "" : r.det), r.tool);
  h = hashInt(hashInt(h, (uint32_t)r.ctx), (uint32_t)r.tok);
  if (region(R_HEADER, h, 0, 0, X(240), Y(150))) {
    if (amber) {
      C().fillRect(0, 0, X(240), Y(4), color::AMBER);
      C().fillCircle(X(16), Y(15), Sz(4), color::AMBER);
      C().text(X(26), Y(20), t(lang, S::NeedsYou), Font::SmallBold, color::AMBER, Align::Left, X(200));
    } else {
      check(X(18), Y(14), Sz(12), color::BLUE);
      C().text(X(30), Y(20), t(lang, S::Finished), Font::SmallBold, color::BLUE, Align::Left, X(200));
    }
    C().text(X(12), Y(62), r.name, Font::Hero, color::TEXT, Align::Left, X(216));
    if (amber) {
      C().text(X(12), Y(92), t(lang, kind == AlertKind::Perm ? S::AskedPermission : S::AskedQuestion), Font::Body,
               color::AMBER, Align::Left, X(216));
      if (r.tool[0]) {
        C().fillRoundRect(X(12), Y(104), X(216), Y(32), Sz(4), color::CMD_BG);
        if (discreet || !r.det[0]) snprintf(buf, sizeof(buf), "%s", r.tool);
        else snprintf(buf, sizeof(buf), "%s: %s", r.tool, r.det);
        C().text(X(20), Y(125), buf, Font::Body, color::TEXT, Align::Left, X(200));
      }
    } else {
      uint32_t dur;
      if (runs.stats(r.id, dur)) {
        miblo::formatElapsed(dur, tmp, sizeof(tmp));
        snprintf(buf, sizeof(buf), t(lang, S::Took), tmp);
        C().text(X(12), Y(92), buf, Font::Body, color::TEXT, Align::Left, X(216));
      }
      metaLine(r, buf, sizeof(buf));
      C().text(X(12), Y(118), buf, Font::Small, color::MUTED, Align::Left, X(216));
    }
  }

  if (amber) {
    const uint32_t waited = since(r, clk);
    if (region(R_BODY, hashInt(hashInt(kHashSeed, waited), (uint32_t)lang), 0, Y(150), X(240), Y(24))) {
      miblo::formatElapsed(waited, tmp, sizeof(tmp));
      snprintf(buf, sizeof(buf), t(lang, S::WaitingFor), tmp);
      C().text(X(12), Y(166), buf, Font::Small, color::DIM, Align::Left, X(216));
    }
  }

  const miblo::StateCounts c = miblo::countStates(s);
  if (region(R_FOOT, hashInt(hashInt(hashInt(kHashSeed, c.running), c.idle), (uint32_t)lang), 0, Y(210), X(240),
             Y(30))) {
    buf[0] = 0;
    if (c.running) snprintf(buf, sizeof(buf), t(lang, S::PlusRunning), (unsigned)c.running);
    if (c.idle) {
      snprintf(tmp, sizeof(tmp), t(lang, S::NIdle), (unsigned)c.idle);
      if (buf[0]) strncat(buf, kDot, sizeof(buf) - strlen(buf) - 1);
      strncat(buf, tmp, sizeof(buf) - strlen(buf) - 1);
    }
    C().text(X(12), Y(228), buf, Font::Small, color::FAINT, Align::Left, X(216));
  }
}

// ---------------- blocos compartilhados ----------------

static void clockRight(const Clock& clk, int y, uint16_t c) {
  C().text(X(228), y, clk.hhmm, Font::Small, c, Align::Right, X(60));
}

// "reseta 16:42 · em 2h10"
static void resetLine(Lang lang, const miblo::UsageWindow& w, const Clock& clk, uint32_t fallbackNow, bool withCountdown,
                      char* out, size_t cap) {
  char when[32];
  char a[48];
  const uint32_t now = clk.epoch ? clk.epoch : fallbackNow;
  formatWhen(lang, w.reset, now, when, sizeof(when));
  snprintf(a, sizeof(a), t(lang, S::ResetsAt), when);
  if (!withCountdown) {
    snprintf(out, cap, "%s", a);
    return;
  }
  char left[16];
  char b[32];
  miblo::formatCountdown(w.reset > now ? w.reset - now : 0, left, sizeof(left));
  snprintf(b, sizeof(b), t(lang, S::InTime), left);
  snprintf(out, cap, "%s%s%s", a, kDot, b);
}

// Custo do dia quando não há limites (conta sem assinatura) ou "limites indisponíveis".
static void noLimits(Lang lang, const Snapshot& s, int cy) {
  if (s.todayUsd > 0.0f) {
    char usd[16];
    char buf[64];
    miblo::formatUsd(s.todayUsd, usd, sizeof(usd));
    snprintf(buf, sizeof(buf), t(lang, S::CostToday), usd);
    C().text(X(120), cy, buf, Font::Title, color::TEXT, Align::Center, X(232));
    C().text(X(120), cy + Y(26), t(lang, S::LimitsUnavailable), Font::Small, color::DIM, Align::Center, X(232));
  } else {
    C().text(X(120), cy, t(lang, S::LimitsUnavailable), Font::Body, color::MUTED, Align::Center, X(232));
  }
}

// Limites grandes da Visão geral: y 26..146 da grade.
static void limitsBlock(Lang lang, const Snapshot& s, const Clock& clk) {
  uint32_t h = hashInt(hashInt(kHashSeed, (uint32_t)lang), clk.epoch / 60);
  h = hashInt(hashInt(hashInt(h, s.hasUsage), s.h5.present ? s.h5.pct : 255), s.h5.reset);
  h = hashInt(hashInt(hashInt(h, s.d7.present ? s.d7.pct : 255), s.d7.reset), (uint32_t)(s.todayUsd * 100));
  if (!region(R_LIMITS, h, 0, Y(26), X(240), Y(122))) return;
  if (!s.hasUsage) {
    noLimits(lang, s, Y(84));
    return;
  }
  char buf[96];
  C().text(X(12), Y(52), t(lang, S::Session5h), Font::Body, color::MUTED, Align::Left, X(140));
  if (s.h5.present) {
    snprintf(buf, sizeof(buf), "%u%%", s.h5.pct);
    C().text(X(228), Y(58), buf, Font::NumL, color::TEXT, Align::Right, X(100));
    bar(X(12), Y(64), X(216), Y(10), s.h5.pct, levelColor(s.h5.pct, color::CORAL));
    resetLine(lang, s.h5, clk, s.now, true, buf, sizeof(buf));
    C().text(X(12), Y(90), buf, Font::Small, color::DIM, Align::Left, X(216));
  } else {
    C().text(X(228), Y(58), "--", Font::NumM, color::DIM, Align::Right, X(100));
  }
  C().text(X(12), Y(116), t(lang, S::Week), Font::Body, color::MUTED, Align::Left, X(140));
  if (s.d7.present) {
    snprintf(buf, sizeof(buf), "%u%%", s.d7.pct);
    C().text(X(228), Y(118), buf, Font::NumM, color::TEXT, Align::Right, X(100));
    bar(X(12), Y(124), X(216), Y(6), s.d7.pct, levelColor(s.d7.pct, color::VIOLET));
    resetLine(lang, s.d7, clk, s.now, false, buf, sizeof(buf));
    C().text(X(12), Y(144), buf, Font::Small, color::DIM, Align::Left, X(216));
  } else {
    C().text(X(228), Y(118), "--", Font::NumM, color::DIM, Align::Right, X(100));
  }
}

// Uma linha da lista compacta (Visão geral), com linha de base y.
static void listRow(uint8_t slot, Lang lang, const SessionRow* r, const Clock& clk, bool discreet, int y) {
  char line[160];
  line[0] = 0;
  if (r) {
    if (isPending(r->st)) {
      char tmp[24];
      miblo::formatElapsed(since(*r, clk), tmp, sizeof(tmp));
      snprintf(line, sizeof(line), t(lang, S::WaitingFor), tmp);
    } else {
      miblo::sessionLine(lang, *r, discreet, line, sizeof(line));
    }
  }
  const uint32_t h = r ? hashStr(hashStr(hashInt(kHashSeed, (uint32_t)r->st), r->name), line) : 7;
  if (!region(R_ROW0 + slot, h, 0, y - Y(13), X(240), Y(18))) return;
  if (!r) return;
  const bool pending = isPending(r->st);
  C().fillCircle(X(16), y - Y(4), Sz(3), stateColor(r->st));
  C().text(X(26), y, r->name, Font::SmallBold, pending ? color::AMBER : color::TEXT, Align::Left, X(92));
  C().text(X(124), y, line, Font::Small, pending ? color::AMBER : color::MUTED, Align::Left, X(104));
}

// ---------------- Visão geral adaptativa (§4.1) ----------------

void overview(Lang lang, const Snapshot& s, miblo::Pager& pager, uint32_t nowMs, const Clock& clk, bool discreet) {
  const OverviewKind kind = miblo::classifyOverview(s);
  const miblo::StateCounts c = miblo::countStates(s);
  char buf[128];
  char tmp[48];

  const int heroIdx = miblo::selectHero(s, false);
  const char* heroName = heroIdx >= 0 ? s.sessions[heroIdx].name : "";
  uint32_t h = hashInt(hashInt(hashInt(kHashSeed, (uint32_t)kind), c.pending), c.running);
  h = hashStr(hashStr(hashInt(h, (uint32_t)lang), clk.hhmm), heroName);
  if (region(R_HEADER, h, 0, 0, X(240), Y(26))) {
    if (kind == OverviewKind::Attention) {  // faixa âmbar fixa: "1 AGUARDANDO · api-server"
      C().fillRect(0, 0, X(240), Y(22), color::AMBER);
      snprintf(tmp, sizeof(tmp), t(lang, S::NWaiting), (unsigned)c.pending);
      snprintf(buf, sizeof(buf), "%s%s%s", tmp, kDot, heroName);
      C().text(X(10), Y(16), buf, Font::SmallBold, color::BLACK, Align::Left, X(180));
      clockRight(clk, Y(16), color::BLACK);
    } else if (kind == OverviewKind::Working) {
      C().fillCircle(X(16), Y(13), Sz(4), color::GREEN);
      snprintf(buf, sizeof(buf), t(lang, S::NRunning), (unsigned)c.running);
      C().text(X(26), Y(18), buf, Font::SmallBold, color::GREEN, Align::Left, X(150));
      clockRight(clk, Y(18), color::DIM);
    } else {
      check(X(17), Y(12), Sz(12), color::BLUE);
      C().text(X(28), Y(18), t(lang, S::AllDone), Font::SmallBold, color::BLUE, Align::Left, X(150));
      clockRight(clk, Y(18), color::DIM);
    }
  }

  limitsBlock(lang, s, clk);
  if (region(R_DIVIDER, 1, 0, Y(150), X(240), 2)) C().fillRect(X(12), Y(150), X(216), 1, color::DIVIDER);

  const int ys[4] = {Y(168), Y(186), Y(204), Y(222)};
  if (kind == OverviewKind::Idle) {
    // rodapé: última sessão que terminou + custo do dia
    const int last = miblo::lastFinished(s);
    buf[0] = 0;
    if (last >= 0) {
      miblo::formatAgo(since(s.sessions[last], clk), tmp, sizeof(tmp));
      snprintf(buf, sizeof(buf), t(lang, S::FinishedAgo), s.sessions[last].name, tmp);
    }
    if (region(R_ROW0, hashStr(kHashSeed, buf), 0, ys[0] - Y(13), X(240), Y(18))) {
      C().text(X(12), ys[0], buf, Font::Small, color::MUTED, Align::Left, X(216));
    }
    buf[0] = 0;
    if (s.todayUsd > 0.0f) {
      miblo::formatUsd(s.todayUsd, tmp, sizeof(tmp));
      snprintf(buf, sizeof(buf), t(lang, S::CostToday), tmp);
    }
    if (region(R_ROW0 + 1, hashStr(kHashSeed + 1, buf), 0, ys[1] - Y(13), X(240), Y(18))) {
      C().text(X(12), ys[1], buf, Font::Small, color::DIM, Align::Left, X(216));
    }
    (void)region(R_ROW0 + 2, 0, 0, ys[2] - Y(13), X(240), Y(18));  // limpa linhas da lista anterior
    (void)region(R_ROW0 + 3, 0, 0, ys[3] - Y(13), X(240), Y(18));
    return;
  }

  const uint8_t page = pager.update(s.count, nowMs);
  for (uint8_t i = 0; i < 4; i++) {
    const int idx = page * pager.perPage() + i;
    listRow(i, lang, idx < s.count ? &s.sessions[idx] : nullptr, clk, discreet, ys[i]);
  }
}

// ---------------- Modo Limites — L1 (§4.3) ----------------

void limits(Lang lang, const Snapshot& s, const Clock& clk) {
  char buf[96];
  if (region(R_HEADER, hashStr(hashInt(kHashSeed, (uint32_t)lang), clk.hhmm), 0, 0, X(240), Y(24))) {
    C().text(X(12), Y(18), t(lang, S::LimitsTitle), Font::SmallBold, color::DIM, Align::Left, X(150));
    clockRight(clk, Y(18), color::DIM);
  }
  uint32_t h = hashInt(hashInt(hashInt(kHashSeed, s.hasUsage), s.h5.present ? s.h5.pct : 255), clk.epoch / 60);
  h = hashInt(hashInt(h, (uint32_t)lang), (uint32_t)(s.todayUsd * 100));
  if (region(R_LIMITS, h, 0, Y(24), X(240), Y(150))) {
    if (!s.hasUsage || !s.h5.present) {
      noLimits(lang, s, Y(104));
    } else {
      const uint8_t pct = s.h5.pct;
      const int cx = X(120);
      const int cy = Y(104);
      const int r = Sz(78);
      const int ir = Sz(64);
      // arco de 270° com a abertura embaixo
      C().arc(cx, cy, r, ir, 45, 315, color::TRACK, color::BG);
      if (pct > 0) {
        int end = 45 + 270 * pct / 100;
        if (end <= 45) end = 46;
        C().arc(cx, cy, r, ir, 45, end, levelColor(pct, color::CORAL), color::BG);
      }
      snprintf(buf, sizeof(buf), "%u%%", pct);
      C().text(cx, Y(112), buf, Font::NumL, color::TEXT, Align::Center, 2 * ir);
      C().text(cx, Y(134), t(lang, S::Session5h), Font::Small, color::MUTED, Align::Center, 2 * ir);
      char left[16];
      miblo::formatCountdown(s.h5.reset > clk.epoch ? s.h5.reset - clk.epoch : 0, left, sizeof(left));
      snprintf(buf, sizeof(buf), t(lang, S::InTime), left);
      C().text(cx, Y(152), buf, Font::Small, color::DIM, Align::Center, 2 * ir);
    }
  }
  h = hashInt(hashInt(hashInt(kHashSeed, s.d7.present ? s.d7.pct : 255), s.d7.reset), (uint32_t)lang);
  if (region(R_WEEK, hashInt(h, clk.epoch / 3600), 0, Y(176), X(240), Y(40))) {
    if (s.hasUsage && s.d7.present) {
      char when[32];
      C().text(X(12), Y(192), t(lang, S::Week), Font::Small, color::MUTED, Align::Left, X(100));
      formatWhen(lang, s.d7.reset, clk.epoch ? clk.epoch : s.now, when, sizeof(when));
      snprintf(buf, sizeof(buf), "%u%%%s%s", s.d7.pct, kDot, when);
      C().text(X(228), Y(192), buf, Font::Small, color::MUTED, Align::Right, X(120));
      bar(X(12), Y(200), X(216), Y(6), s.d7.pct, levelColor(s.d7.pct, color::VIOLET));
    }
  }
}

// ---------------- Modo Sessões — S1 (§4.4) ----------------

void sessions(Lang lang, const Snapshot& s, miblo::Pager& pager, uint32_t nowMs, const Clock& clk, bool discreet) {
  char buf[160];
  char tmp[48];
  const uint8_t page = pager.update(s.count, nowMs);
  const uint8_t pages = pager.pageCount(s.count);
  const unsigned total = (unsigned)s.count + s.more;
  if (region(R_HEADER, hashInt(hashInt(hashInt(kHashSeed, total), page * 16u + pages), (uint32_t)lang), 0, 0, X(240),
             Y(24))) {
    snprintf(buf, sizeof(buf), t(lang, S::SessionsTitle), total);
    C().text(X(10), Y(16), buf, Font::SmallBold, color::DIM, Align::Left, X(170));
    snprintf(tmp, sizeof(tmp), "%u/%u", (unsigned)page + 1, (unsigned)pages);
    C().text(X(230), Y(16), tmp, Font::Small, color::DIM, Align::Right, X(50));
  }
  if (s.count == 0) {
    if (region(R_ROW0, hashInt(kHashSeed + 9, (uint32_t)lang), 0, Y(24), X(240), Y(216))) {
      C().text(X(120), Y(130), t(lang, S::NoSessions), Font::Body, color::MUTED, Align::Center, X(232));
    }
    for (uint8_t i = 1; i < 4; i++) (void)region(R_ROW0 + i, 0xFFFFFFFFu, 0, 0, 0, 0);  // força redesenho depois
    return;
  }
  for (uint8_t i = 0; i < 4; i++) {
    const int idx = page * pager.perPage() + i;
    const int y0 = Y(26 + i * 53);
    const SessionRow* r = idx < s.count ? &s.sessions[idx] : nullptr;
    char timeStr[16] = "";
    char state[160] = "";
    char meta[96] = "";
    if (r) {
      if (isPending(r->st) || r->st == SessionState::Running) miblo::formatElapsed(since(*r, clk), timeStr, sizeof(timeStr));
      else miblo::formatAgo(since(*r, clk), timeStr, sizeof(timeStr));
      miblo::sessionLine(lang, *r, discreet, state, sizeof(state));
      metaLine(*r, meta, sizeof(meta));
    }
    const uint32_t h =
        r ? hashStr(hashStr(hashStr(hashStr(hashInt(kHashSeed, (uint32_t)r->st), r->name), timeStr), state), meta) : 3;
    if (!region(R_ROW0 + i, h, 0, y0 - 1, X(240), Y(53))) continue;
    if (!r) continue;
    const bool pending = isPending(r->st);
    const uint16_t sc = stateColor(r->st);
    C().fillRect(X(8), y0, X(224), Y(50), pending ? color::CARD_AMBER : color::CARD);
    C().fillRect(X(8), y0, Sz(3), Y(50), sc);
    C().text(X(16), y0 + Y(16), r->name, Font::BodyBold, color::TEXT, Align::Left, X(150));
    C().text(X(226), y0 + Y(16), timeStr, Font::Small, pending ? color::AMBER : color::DIM, Align::Right, X(60));
    C().text(X(16), y0 + Y(31), state, Font::Small, sc, Align::Left, X(208));
    C().text(X(16), y0 + Y(45), meta, Font::Small, color::DIM, Align::Left, X(208));
  }
}

}  // namespace screens
```

- [ ] **Step 5: Run all native tests**

Run: `cd firmware && .venv/bin/pio test -e native`
Expected: PASS — `69 test cases: 69 succeeded` (10 suítes).

- [ ] **Step 6: Commit**

```bash
git add firmware/lib/miblo_ui/src/ui_screens.h firmware/lib/miblo_ui/src/ui_main.cpp firmware/test/test_ui_main/test_main.cpp
git commit -m "feat(firmware): adaptive overview, alert flash/hero, limits arc and sessions list layouts"
```

---
### Task 11: Placa GeekMagic Ultra — fontes embutidas, `TftCanvas`, `board` e demo na tela real

**Files:**
- Create: `firmware/scripts/vendor_u8g2.py`
- Create (gerados pelo script): `firmware/lib/U8g2TFT/src/U8g2_for_TFT_eSPI.h`, `U8g2_for_TFT_eSPI.cpp`, `u8g2_fonts.h` (com patch), `miblo_fonts.c`, `LICENSE`
- Create: `firmware/src/platform/tft_canvas.h`, `firmware/src/platform/tft_canvas.cpp`
- Create: `firmware/boards/geekmagic_ultra/board.h`, `firmware/boards/geekmagic_ultra/board.cpp`
- Create: `firmware/boards/README.md`
- Modify: `firmware/src/main.cpp` (substituído pelo demo)

**Interfaces:**
- Consumes: `ui::Canvas`, `ui::Font`, `ui::ScreenSpec`, `screens::*` (Tasks 9–10); `utf8Next` (Task 1).
- Produces: `class TftCanvas : public ui::Canvas { using FontStack = const uint8_t* const*; TftCanvas(TFT_eSPI&, ui::ScreenSpec, const FontStack* stacks); void begin(); }`; `namespace board { kName = "geekmagic_ultra"; kScreen = {240, 240}; kPin*; kCapCount = 0; cap(uint8_t); struct Inputs {button, touched, x, y}; void begin(); ui::Canvas& canvas(); void setBacklight(uint8_t percent); Inputs readInputs(); }`.

Por que vendorizar a U8g2_for_TFT_eSPI: no ESP8266 ela deixa as fontes em `.rodata` (RAM — a fonte chinesa sozinha tem 134 KB) e lê bytes da flash por ponteiro direto. O script copia a biblioteca num commit fixo, aplica o mesmo tratamento que o u8g2 oficial usa no ESP8266 (seção `.irom.text` + `pgm_read_byte`) e extrai só as 14 fontes usadas (~224 KB): Helvetica `_te` (latim até U+02BD), `*_t_cyrillic` e `u8g2_font_wqy14_t_gb2312a` (3 755 ideogramas de GB2312 nível 1).

- [ ] **Step 1: Script de vendorização** — `firmware/scripts/vendor_u8g2.py`

```python
#!/usr/bin/env python3
"""Vendoriza U8g2_for_TFT_eSPI (Bodmer) + só as fontes u8g2 que o Miblo usa, em PROGMEM.

Por que vendorizar: no ESP8266 a biblioteca original deixa as fontes em `.rodata` (RAM!) e lê
os bytes com ponteiro direto. O patch abaixo coloca as fontes em `.irom.text` (flash) e troca a
leitura por pgm_read_byte — igual ao que o u8g2 oficial faz para ESP8266. Rodar uma vez; o
resultado (lib/U8g2TFT/) é commitado.

Uso: firmware/.venv/bin/python firmware/scripts/vendor_u8g2.py
"""
import os
import re
import sys
import urllib.request

COMMIT = "a170ef8b6d8414b1ee2ecc97b5b913e08f5597ac"
BASE = f"https://raw.githubusercontent.com/Bodmer/U8g2_for_TFT_eSPI/{COMMIT}"
HERE = os.path.dirname(os.path.abspath(__file__))
DEST = os.path.join(HERE, "..", "lib", "U8g2TFT", "src")

# Fontes usadas por src/display.cpp (estilos de texto). Tamanho total ≈ 224 KB.
FONTS = [
    "u8g2_font_helvR10_te",
    "u8g2_font_helvB10_te",
    "u8g2_font_helvR12_te",
    "u8g2_font_helvB12_te",
    "u8g2_font_helvB18_te",
    "u8g2_font_helvB24_te",
    "u8g2_font_fub20_tf",
    "u8g2_font_fub30_tn",
    "u8g2_font_6x13_t_cyrillic",
    "u8g2_font_6x13B_t_cyrillic",
    "u8g2_font_8x13_t_cyrillic",
    "u8g2_font_10x20_t_cyrillic",
    "u8g2_font_inr24_t_cyrillic",
    "u8g2_font_wqy14_t_gb2312a",
]

PATCH = """
/* --- Miblo: fontes em flash no ESP8266 (como no u8g2 oficial) --- */
#if defined(ESP8266)
#  include <pgmspace.h>
#  define U8X8_FONT_SECTION(name) __attribute__((section(".irom.text." name)))
#  define u8x8_pgm_read(adr) pgm_read_byte(adr)
#  define U8X8_PROGMEM
#endif
/* --- fim do patch Miblo --- */

"""


def fetch(path):
    with urllib.request.urlopen(f"{BASE}/{path}", timeout=120) as r:
        # latin-1 preserva os bytes exatamente (há comentários que não são UTF-8 válido)
        return r.read().decode("latin-1")


def main():
    os.makedirs(DEST, exist_ok=True)
    for name in ["U8g2_for_TFT_eSPI.h", "U8g2_for_TFT_eSPI.cpp"]:
        with open(os.path.join(DEST, name), "w", encoding="latin-1") as f:
            f.write(fetch(f"src/{name}"))
    with open(os.path.join(DEST, "LICENSE"), "w", encoding="latin-1") as f:
        f.write(fetch("LICENSE"))

    header = fetch("src/u8g2_fonts.h")
    anchor = "#ifndef U8X8_FONT_SECTION"
    if anchor not in header:
        sys.exit("u8g2_fonts.h mudou: âncora do patch não encontrada")
    header = header.replace(anchor, PATCH + anchor, 1)
    with open(os.path.join(DEST, "u8g2_fonts.h"), "w", encoding="latin-1") as f:
        f.write(header)

    print("baixando u8g2_fonts.c (~26 MB)...")
    source = fetch("src/u8g2_fonts.c").split("\n")
    out = ['/* Gerado por scripts/vendor_u8g2.py - apenas as fontes usadas pelo Miblo. */',
           '#include "u8g2_fonts.h"', ""]
    total = 0
    for font in FONTS:
        start = next((i for i, l in enumerate(source) if l.startswith(f"const uint8_t {font}[")), None)
        if start is None:
            sys.exit(f"fonte não encontrada: {font}")
        end = start
        while not source[end].rstrip().endswith('";'):
            end += 1
        out.extend(source[start:end + 1])
        out.append("")
        size = int(re.search(r"\[(\d+)\]", source[start]).group(1))
        total += size
        print(f"  {font}: {size} bytes")
    with open(os.path.join(DEST, "miblo_fonts.c"), "w", encoding="latin-1") as f:
        f.write("\n".join(out) + "\n")
    print(f"total das fontes: {total} bytes")


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Rodar o script**

Run: `cd firmware && .venv/bin/python scripts/vendor_u8g2.py`
Expected: lista as 14 fontes com os tamanhos (ex.: `u8g2_font_wqy14_t_gb2312a: 134054 bytes`) e termina com `total das fontes: 229297 bytes`; `lib/U8g2TFT/src/` contém `LICENSE`, `U8g2_for_TFT_eSPI.cpp`, `U8g2_for_TFT_eSPI.h`, `miblo_fonts.c` (~640 KB de texto) e `u8g2_fonts.h` com o bloco `/* --- Miblo: fontes em flash no ESP8266`.

- [ ] **Step 3: Canvas sobre TFT_eSPI** — `firmware/src/platform/tft_canvas.h`

```cpp
#pragma once
#include <TFT_eSPI.h>
#include <U8g2_for_TFT_eSPI.h>

#include "ui_canvas.h"

// Canvas sobre TFT_eSPI + fontes u8g2 (UTF-8). Serve para qualquer placa com TFT_eSPI; a placa
// informa o tamanho da tela e, para cada ui::Font, uma pilha de fontes u8g2 (terminada em
// nullptr): o primeiro que tiver o glyph desenha o caractere; nenhum → retângulo.
class TftCanvas : public ui::Canvas {
 public:
  using FontStack = const uint8_t* const*;
  TftCanvas(TFT_eSPI& tft, ui::ScreenSpec spec, const FontStack* stacks) : tft_(tft), spec_(spec), stacks_(stacks) {}
  void begin();

  ui::ScreenSpec spec() const override { return spec_; }
  void fillRect(int x, int y, int w, int h, uint16_t c) override { tft_.fillRect(x, y, w, h, c); }
  void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) override { tft_.fillRoundRect(x, y, w, h, r, c); }
  void drawRect(int x, int y, int w, int h, uint16_t c) override { tft_.drawRect(x, y, w, h, c); }
  void fillCircle(int cx, int cy, int r, uint16_t c) override { tft_.fillCircle(cx, cy, r, c); }
  void wideLine(int x0, int y0, int x1, int y1, int width, uint16_t c, uint16_t bg) override {
    tft_.drawWideLine(x0, y0, x1, y1, width, c, bg);
  }
  void arc(int cx, int cy, int r, int ir, int a0, int a1, uint16_t fg, uint16_t bg) override {
    tft_.drawSmoothArc(cx, cy, r, ir, a0, a1, fg, bg, true);
  }
  int text(int x, int y, const char* s, ui::Font f, uint16_t fg, ui::Align a, int maxW) override;
  int textWidth(const char* s, ui::Font f) override;

 private:
  TFT_eSPI& tft_;
  ui::ScreenSpec spec_;
  const FontStack* stacks_;
  U8g2_for_TFT_eSPI u8_;

  const uint8_t* fontFor(ui::Font f, uint32_t cp);
  int glyphAdvance(ui::Font f, uint32_t cp, const uint8_t** font);
  int ascent(ui::Font f);
  int layout(const char* s, ui::Font f, int maxW, const char** end);
  int drawRun(int x, int y, const char* s, const char* end, ui::Font f, uint16_t fg);
};
```

`firmware/src/platform/tft_canvas.cpp`:

```cpp
#include "tft_canvas.h"

#include "miblo_utf8.h"

namespace {
constexpr uint32_t kEllipsis = 0x2026;  // "…" não existe nas fontes: vira "..."
}

void TftCanvas::begin() {
  u8_.begin(tft_);
  u8_.setFontMode(1);  // transparente: a região é limpa antes de desenhar
  u8_.setFontDirection(0);
}

const uint8_t* TftCanvas::fontFor(ui::Font f, uint32_t cp) {
  if (cp > 0xFFFF) return nullptr;
  for (FontStack p = stacks_[(int)f]; *p; p++) {
    u8_.setFont(*p);
    if (u8g2_IsGlyph(&u8_.u8g2, (uint16_t)cp)) return *p;
  }
  return nullptr;
}

int TftCanvas::ascent(ui::Font f) {
  u8_.setFont(stacks_[(int)f][0]);
  return u8_.getFontAscent();
}

int TftCanvas::glyphAdvance(ui::Font f, uint32_t cp, const uint8_t** font) {
  const uint8_t* fnt = fontFor(f, cp);
  if (font) *font = fnt;
  if (!fnt) return ascent(f) * 2 / 3 + 3;  // largura do retângulo de glyph ausente
  u8_.setFont(fnt);
  return u8g2_GetGlyphWidth(&u8_.u8g2, (uint16_t)cp);
}

// Largura de `s`. Se passar de maxW, *end aponta para onde cortar de modo que prefixo + "..."
// caiba, e o retorno é a largura do prefixo + "...". Sem corte, *end = nullptr.
int TftCanvas::layout(const char* s, ui::Font f, int maxW, const char** end) {
  const int dots = glyphAdvance(f, '.', nullptr) * 3;
  int w = 0;
  int fitW = 0;
  const char* fit = s;
  const char* p = s;
  *end = nullptr;
  while (*p) {
    uint32_t cp = miblo::utf8Next(p);
    w += cp == kEllipsis ? dots : glyphAdvance(f, cp, nullptr);
    if (w + dots <= maxW) {
      fit = p;
      fitW = w;
    }
  }
  if (w <= maxW) return w;
  *end = fit;
  return fitW + dots;
}

int TftCanvas::drawRun(int x, int y, const char* s, const char* end, ui::Font f, uint16_t fg) {
  const int x0 = x;
  const char* p = s;
  while (*p && (!end || p < end)) {
    uint32_t cp = miblo::utf8Next(p);
    if (cp == kEllipsis) {
      x += drawRun(x, y, "...", nullptr, f, fg);
      continue;
    }
    const uint8_t* fnt;
    const int adv = glyphAdvance(f, cp, &fnt);
    if (fnt) {
      u8_.setFont(fnt);
      u8_.setForegroundColor(fg);
      u8_.drawGlyph(x, y, (uint16_t)cp);
    } else {
      const int h = ascent(f);
      tft_.drawRect(x + 1, y - h, adv - 2, h, fg);  // glyph ausente: retângulo, nunca trava
    }
    x += adv;
  }
  return x - x0;
}

int TftCanvas::textWidth(const char* s, ui::Font f) {
  const char* end;
  return layout(s ? s : "", f, 30000, &end);
}

int TftCanvas::text(int x, int y, const char* s, ui::Font f, uint16_t fg, ui::Align a, int maxW) {
  if (!s) s = "";
  const char* end;
  const int w = layout(s, f, maxW, &end);
  const int left = a == ui::Align::Left ? x : (a == ui::Align::Center ? x - w / 2 : x - w);
  int drawn = drawRun(left, y, s, end, f, fg);
  if (end) drawn += drawRun(left + drawn, y, "...", nullptr, f, fg);
  return drawn;
}
```

- [ ] **Step 4: Placa** — `firmware/boards/geekmagic_ultra/board.h`

```cpp
#pragma once
#include <stdint.h>

#include "ui_canvas.h"

// Placa: GeekMagic "Ultra" — ESP8266 ESP-12E/F, ST7789 240x240, sem botões nem touch.
// Os pinos também vão como -D para o TFT_eSPI no platformio.ini ([env:geekmagic_ultra]);
// board.cpp confere que os dois lugares batem.
namespace board {

constexpr const char* kName = "geekmagic_ultra";
constexpr ui::ScreenSpec kScreen = {240, 240};

constexpr uint8_t kPinMosi = 13;
constexpr uint8_t kPinSclk = 14;
constexpr uint8_t kPinCs = 15;
constexpr uint8_t kPinDc = 0;
constexpr uint8_t kPinRst = 2;
constexpr uint8_t kPinBacklight = 5;  // ativo em nível BAIXO

// Capacidades extras anunciadas em /api/info ("buttons", "touch", "buzzer", "led"). Ultra: nenhuma.
constexpr uint8_t kCapCount = 0;
inline const char* cap(uint8_t) { return ""; }

// Entradas físicas. Ultra não tem: sempre vazio (o seam existe para placas futuras).
struct Inputs {
  bool button;
  bool touched;
  int16_t x;
  int16_t y;
};

void begin();                        // tela + luz de fundo
ui::Canvas& canvas();
void setBacklight(uint8_t percent);  // 0..100
Inputs readInputs();

}  // namespace board
```

`firmware/boards/geekmagic_ultra/board.cpp`:

```cpp
#include "board.h"

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <U8g2_for_TFT_eSPI.h>

#include "platform/tft_canvas.h"

static_assert(TFT_MOSI == board::kPinMosi && TFT_SCLK == board::kPinSclk && TFT_CS == board::kPinCs &&
                  TFT_DC == board::kPinDc && TFT_RST == board::kPinRst && TFT_BL == board::kPinBacklight,
              "pinos do platformio.ini diferentes de boards/geekmagic_ultra/board.h");
static_assert(TFT_WIDTH == board::kScreen.w && TFT_HEIGHT == board::kScreen.h, "tamanho da tela");

namespace board {

namespace {

// Pilhas de fontes por ui::Font (latim → cirílico → CJK). Vendorizadas em lib/U8g2TFT.
const uint8_t* const kSmall[] = {u8g2_font_helvR10_te, u8g2_font_6x13_t_cyrillic, u8g2_font_wqy14_t_gb2312a, nullptr};
const uint8_t* const kSmallBold[] = {u8g2_font_helvB10_te, u8g2_font_6x13B_t_cyrillic, u8g2_font_wqy14_t_gb2312a,
                                     nullptr};
const uint8_t* const kBody[] = {u8g2_font_helvR12_te, u8g2_font_8x13_t_cyrillic, u8g2_font_wqy14_t_gb2312a, nullptr};
const uint8_t* const kBodyBold[] = {u8g2_font_helvB12_te, u8g2_font_8x13_t_cyrillic, u8g2_font_wqy14_t_gb2312a,
                                    nullptr};
const uint8_t* const kTitle[] = {u8g2_font_helvB18_te, u8g2_font_10x20_t_cyrillic, u8g2_font_wqy14_t_gb2312a, nullptr};
const uint8_t* const kHero[] = {u8g2_font_helvB24_te, u8g2_font_inr24_t_cyrillic, u8g2_font_wqy14_t_gb2312a, nullptr};
const uint8_t* const kNumL[] = {u8g2_font_fub30_tn, u8g2_font_fub20_tf, u8g2_font_helvB18_te, nullptr};
const uint8_t* const kNumM[] = {u8g2_font_fub20_tf, u8g2_font_helvB12_te, nullptr};
const TftCanvas::FontStack kStacks[] = {kSmall, kSmallBold, kBody, kBodyBold, kTitle, kHero, kNumL, kNumM};
static_assert(sizeof(kStacks) / sizeof(kStacks[0]) == (size_t)ui::Font::Count, "uma pilha por ui::Font");

TFT_eSPI tft;
TftCanvas tftCanvas(tft, kScreen, kStacks);

}  // namespace

void begin() {
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(0x0841);
  tftCanvas.begin();
  analogWriteRange(255);
  setBacklight(80);
}

ui::Canvas& canvas() { return tftCanvas; }

void setBacklight(uint8_t percent) {
  if (percent > 100) percent = 100;
  const uint32_t duty = (uint32_t)percent * 255 / 100;
  analogWrite(kPinBacklight, 255 - duty);  // ativo em nível BAIXO
}

Inputs readInputs() { return Inputs{false, false, 0, 0}; }

}  // namespace board
```

- [ ] **Step 5: Documentar como adicionar placas** — `firmware/boards/README.md`

```markdown
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
```

- [ ] **Step 6: Demo na tela real** — substituir `firmware/src/main.cpp` por:

```cpp
// Demo da Task 11: placa + Canvas + fontes reais. Mostra o mascote e passa pelas telas de
// sistema e principais com dados de exemplo. Mantém o /update do firmware seguro (Task 1).
// Substituído pelo aplicativo completo na Task 13.
#include <Arduino.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <string.h>

#include "board.h"
#include "miblo_version.h"
#include "ui_screens.h"

using miblo::Lang;
using ui::Align;
using ui::Font;

ESP8266WebServer server(80);
ESP8266HTTPUpdateServer updater;
static miblo::Snapshot demo;
static miblo::Pager pager(4, 5000);
static miblo::RunTracker runs;

static void fontSample() {
  ui::Canvas& c = board::canvas();
  c.text(12, 24, "Miblo " MIBLO_FW_VERSION, Font::Title, ui::color::TEXT, Align::Left, 216);
  c.text(12, 50, "Ação às 14h · déjà vu", Font::Body, ui::color::MUTED, Align::Left, 216);
  c.text(12, 72, "Übersicht · ¡Hola!", Font::Body, ui::color::MUTED, Align::Left, 216);
  c.text(12, 94, "Ожидание компьютера", Font::Body, ui::color::MUTED, Align::Left, 216);
  c.text(12, 116, "等待电脑连接", Font::Body, ui::color::MUTED, Align::Left, 216);
  c.text(12, 138, "sem glyph: \xF0\x9F\x98\x80 fim", Font::Body, ui::color::AMBER, Align::Left, 216);
  c.text(12, 160, "texto comprido demais que precisa ser cortado", Font::Small, ui::color::DIM, Align::Left, 216);
  c.text(228, 226, "62%", Font::NumL, ui::color::TEXT, Align::Right, 216);
}

static void makeDemo() {
  memset(&demo, 0, sizeof(demo));
  const uint32_t now = 1790616720;
  demo.now = now;
  demo.hasUsage = true;
  demo.h5 = {true, 62, now + 7800};
  demo.d7 = {true, 38, now + 240000};
  demo.todayUsd = 3.5f;
  const struct {
    const char* id;
    const char* name;
    miblo::SessionState st;
    const char* tool;
    const char* det;
  } rows[] = {{"1", "api-server", miblo::SessionState::Perm, "Bash", "npm run migrate"},
              {"2", "front-app", miblo::SessionState::Running, "Edit", "Header.tsx"},
              {"3", "docs", miblo::SessionState::Done, "", ""}};
  for (const auto& r : rows) {
    miblo::SessionRow& s = demo.sessions[demo.count++];
    strcpy(s.id, r.id);
    strcpy(s.name, r.name);
    s.st = r.st;
    strcpy(s.tool, r.tool);
    strcpy(s.det, r.det);
    s.since = now - 42;
    strcpy(s.model, "Opus");
    s.ctx = 71;
    s.tok = 412000;
  }
}

void setup() {
  board::begin();
  screens::bind(board::canvas());
  makeDemo();
  WiFi.mode(WIFI_STA);
  WiFi.begin();
  const uint32_t t0 = millis();
  uint8_t frame = 0;
  screens::reset();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    screens::boot(Lang::PtBR, frame++);
    delay(300);
  }
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("Miblo-Recovery");
  }
  updater.setup(&server, "/update");
  server.begin();
}

void loop() {
  server.handleClient();
  static uint32_t last = 0;
  static uint8_t step = 0;
  if (millis() - last < 4000) return;
  last = millis();
  const String ip = (WiFi.getMode() & WIFI_AP) ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  screens::Clock clk{true, "14:32", demo.now};
  screens::reset();
  switch (step++ % 11) {
    case 0: fontSample(); break;
    case 1: screens::setup(Lang::En, "Miblo-Setup-4F2A", false); break;
    case 2: screens::setup(Lang::Ru, "Miblo-Setup-4F2A", true); break;
    case 3: screens::welcome(Lang::PtBR, "4827", ip.c_str()); break;
    case 4: screens::paired(Lang::Zh, "MacBook-Marcus", "概览", "miblo-4f2a"); break;
    case 5: screens::code(Lang::De, miblo::S::CodeUpdate, "1234", 299); break;
    case 6: screens::disconnected(Lang::Fr, true, 14, 32, 1, 28, ip.c_str(), "miblo-4f2a", "4827"); break;
    case 7: screens::hero(Lang::PtBR, demo, 0, miblo::AlertKind::Perm, false, clk, runs); break;
    case 8: screens::overview(Lang::PtBR, demo, pager, millis(), clk, false); break;
    case 9: screens::limits(Lang::PtBR, demo, clk); break;
    case 10: screens::sessions(Lang::PtBR, demo, pager, millis(), clk, false); break;
  }
}
```

- [ ] **Step 7: Build do aparelho + testes**

Run: `cd firmware && .venv/bin/pio test -e native && .venv/bin/pio run -e geekmagic_ultra`
Expected: `69 test cases: 69 succeeded`; `[SUCCESS]` com `RAM: … (used ~36 KB from 81920 bytes)` — prova de que as fontes **não** foram para a RAM — e `Flash: … (used ~610 KB from 1044464 bytes)`. Conferir também que as fontes estão na flash:
`~/.platformio/packages/toolchain-xtensa/bin/xtensa-lx106-elf-nm -S .pio/build/geekmagic_ultra/firmware.elf | grep wqy14` → endereço `402…` (irom), não `3ff…` (RAM).

- [ ] **Step 8 (opcional, com o aparelho à mão): ver o demo**

Enviar `.pio/build/geekmagic_ultra/firmware.bin` pela página `/update` do firmware atual. Expected: mascote piscando enquanto conecta; depois, a cada 4 s: amostra de fontes (acentos, cirílico e chinês legíveis; o emoji vira um retângulo; texto longo termina em "..."), setup com QR, "Неверный пароль", boas-vindas, "Pareado com", código, relógio, herói, visão geral, arco de limites e lista de sessões. O `/update` continua acessível.

- [ ] **Step 9: Commit**

```bash
git add firmware/scripts/vendor_u8g2.py firmware/lib/U8g2TFT firmware/src/platform/tft_canvas.h firmware/src/platform/tft_canvas.cpp firmware/boards/geekmagic_ultra/board.h firmware/boards/geekmagic_ultra/board.cpp firmware/boards/README.md firmware/src/main.cpp
git commit -m "feat(firmware): GeekMagic Ultra board, embedded u8g2 fonts in flash and TFT canvas"
```

---

### Task 12: Serviços de plataforma — persistência, Wi-Fi com AP de recuperação, mDNS e páginas web

**Files:**
- Create: `firmware/src/platform/platform.h`
- Create: `firmware/src/platform/storage.h`, `firmware/src/platform/storage.cpp`
- Create: `firmware/src/platform/net.h`, `firmware/src/platform/net.cpp`
- Create: `firmware/src/platform/mdns_service.h`, `firmware/src/platform/mdns_service.cpp`
- Create: `firmware/src/context.h`, `firmware/src/context.cpp`
- Create: `firmware/src/web.h`, `firmware/src/web.cpp`

**Interfaces:**
- Consumes: `Config`, `applyConfigPatch`, `configToJson` (Task 7); `NetPolicy`, `NetState`, `LinkStatus` (Task 7); `TokenStore`, `PairingGuard`, `PresenceGate`, `formatCode` (Task 6); `mdnsRespond`, `mdnsAnnounce` (Task 8); `negotiateLang`, `tr`, `langCode`, `langName` (Task 3); `AlertSequencer`, `RunTracker`, `Snapshot`.
- Produces: `WebServerT`, `hwRandom()`, `chipId()`, `flashChipId()` (`platform.h`); `storage::begin/loadConfig/saveConfig/loadTokens/saveTokens/readBootCount/writeBootCount/factoryReset`; `net::begin(uint32_t)`, `net::loop(uint32_t)`, `net::state()`, `net::apActive()`, `net::connected()`, `net::connectionId()`, `net::ip() → String`, `net::submitCredentials(ssid, pass, nowMs)`, `net::applyTimezone()`; `mdns::loop(uint32_t)`, `mdns::announce()`; `struct Identity {id[16], defaultName[16], apSsid[24]}`, `struct Context {…}`, `extern Context ctx`, `uiLang()`, `deviceName()`; `web::begin(WebServerT&)`, `web::pageLang`, `web::pageStart`, `web::pageEnd`, `web::appendEscaped`, `web::tr`, `web::sendJson`.

Comportamento:
- **Wi-Fi:** `WiFi.persistent(true)`; se o SDK tem SSID salvo (inclusive o do firmware de referência) → `WiFi.begin()` direto, sem portal. Sem credenciais, com senha errada (`WL_WRONG_PASSWORD`) ou após 2 min sem conexão → AP aberto `Miblo-Setup-XXXX` (XXXX = 16 bits baixos do chip ID) em `192.168.4.1` + DNS cativo, **continuando** a tentar a rede salva (`WiFi.begin()` a cada 60 s). Conectou → derruba o AP. Nunca chamar `WiFi.disconnect()` fora do reset de fábrica (com `persistent(true)` ele apaga as credenciais).
- **Portal (AP):** `GET /` lista redes (por sinal, sem repetidas), rede oculta, senha, fuso (preenchido por `posixTz()` no navegador) e idioma (pré-selecionado pelo `Accept-Language`); `POST /wifi` salva fuso/idioma e conecta 0,5 s depois de responder. Rotas de detecção (`/generate_204`, `/hotspot-detect.html`, `/connecttest.txt`, …) e qualquer host desconhecido redirecionam para `http://192.168.4.1/`.
- **Página de configuração (rede de casa, `http://miblo-xxxx.local`):** todos os campos da §3.5, dica "rode /miblo pair" quando nunca chegou `usage`, link para `/update`, "Mostrar código de pareamento" (`POST /pair-code`) e reset de fábrica com código na tela (`POST /reset-code` → `POST /factory-reset?code=`). Com fuso ainda `UTC0`, a página grava sozinha o fuso do navegador. Em idioma automático, a tela passa a usar o idioma do último navegador.
- **mDNS:** socket UDP multicast em 5353, reaberto a cada nova conexão; dois anúncios com 1 s de intervalo; respostas unicast/multicast conforme `MdnsReply`.

- [ ] **Step 1: Aliases de plataforma** — `firmware/src/platform/platform.h`

```cpp
#pragma once
// Diferenças entre os cores Arduino do ESP8266 e do ESP32, num só lugar. Só o ESP8266 é
// compilado hoje; o ramo ESP32 marca onde uma placa futura precisa de ajuste.
#include <Arduino.h>

#if defined(ESP8266)
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <Updater.h>
using WebServerT = ESP8266WebServer;
inline uint32_t hwRandom() { return RANDOM_REG32; }  // gerador de hardware
inline uint32_t chipId() { return ESP.getChipId(); }
inline uint32_t flashChipId() { return ESP.getFlashChipId(); }
#elif defined(ESP32)
#include <LittleFS.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
using WebServerT = WebServer;
inline uint32_t hwRandom() { return esp_random(); }
inline uint32_t chipId() { return (uint32_t)ESP.getEfuseMac(); }
inline uint32_t flashChipId() { return ESP.getFlashChipSize(); }
#else
#error "Miblo: plataforma não suportada (use ESP8266 ou ESP32)"
#endif
```

- [ ] **Step 2: Persistência** — `firmware/src/platform/storage.h`

```cpp
#pragma once
#include <stdint.h>

#include "miblo_config.h"
#include "miblo_security.h"

// Persistência em LittleFS (arquivos JSON pequenos). As credenciais de Wi-Fi ficam no SDK.
namespace storage {

bool begin();
bool loadConfig(miblo::Config& cfg);
bool saveConfig(const miblo::Config& cfg);
bool loadTokens(miblo::TokenStore& tokens);
bool saveTokens(const miblo::TokenStore& tokens);
uint8_t readBootCount();
void writeBootCount(uint8_t n);
// Apaga configuração, pareamentos e o Wi-Fi salvo no SDK, e reinicia. Não retorna.
void factoryReset();

}  // namespace storage
```

`firmware/src/platform/storage.cpp`:

```cpp
#include "storage.h"

#include <ArduinoJson.h>

#include "platform.h"

namespace storage {

static const char* kConfig = "/miblo/config.json";
static const char* kTokens = "/miblo/pairs.json";
static const char* kBoot = "/miblo/boot.cnt";

bool begin() {
#if defined(ESP32)
  if (!LittleFS.begin(true)) {  // true = formata se não montar
#else
  if (!LittleFS.begin()) {
#endif
    LittleFS.format();
    if (!LittleFS.begin()) return false;
  }
  if (!LittleFS.exists("/miblo")) LittleFS.mkdir("/miblo");
  return true;
}

bool loadConfig(miblo::Config& cfg) {
  File f = LittleFS.open(kConfig, "r");
  if (!f) return false;
  DynamicJsonDocument doc(1024);
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;
  miblo::Config loaded;
  if (!miblo::applyConfigPatch(loaded, doc.as<JsonObjectConst>(), nullptr)) return false;
  cfg = loaded;
  return true;
}

bool saveConfig(const miblo::Config& cfg) {
  DynamicJsonDocument doc(1024);
  miblo::configToJson(cfg, doc.to<JsonObject>());
  File f = LittleFS.open(kConfig, "w");
  if (!f) return false;
  bool ok = serializeJson(doc, f) > 0;
  f.close();
  return ok;
}

bool loadTokens(miblo::TokenStore& tokens) {
  File f = LittleFS.open(kTokens, "r");
  if (!f) return false;
  DynamicJsonDocument doc(1024);
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;
  miblo::TokenEntry entries[miblo::TokenStore::kMax];
  uint8_t n = 0;
  for (JsonObjectConst e : doc["pairs"].as<JsonArrayConst>()) {
    if (n >= miblo::TokenStore::kMax) break;
    const char* token = e["token"] | "";
    const char* host = e["host"] | "";
    if (strlen(token) != 32) continue;
    strlcpy(entries[n].token, token, sizeof(entries[n].token));
    strlcpy(entries[n].host, host, sizeof(entries[n].host));
    entries[n].order = e["order"] | 0;
    n++;
  }
  tokens.restore(entries, n);
  return true;
}

bool saveTokens(const miblo::TokenStore& tokens) {
  DynamicJsonDocument doc(1024);
  JsonArray arr = doc.createNestedArray("pairs");
  for (uint8_t i = 0; i < tokens.count(); i++) {
    JsonObject e = arr.createNestedObject();
    e["token"] = tokens.at(i).token;
    e["host"] = tokens.at(i).host;
    e["order"] = tokens.at(i).order;
  }
  File f = LittleFS.open(kTokens, "w");
  if (!f) return false;
  bool ok = serializeJson(doc, f) > 0;
  f.close();
  return ok;
}

uint8_t readBootCount() {
  File f = LittleFS.open(kBoot, "r");
  if (!f) return 0;
  int v = f.read();
  f.close();
  return v < 0 ? 0 : (uint8_t)v;
}

void writeBootCount(uint8_t n) {
  File f = LittleFS.open(kBoot, "w");
  if (!f) return;
  f.write(n);
  f.close();
}

void factoryReset() {
  LittleFS.remove(kConfig);
  LittleFS.remove(kTokens);
  LittleFS.remove(kBoot);
  WiFi.persistent(true);
#if defined(ESP8266)
  WiFi.disconnect(true);  // com persistent(true), apaga SSID/senha salvos no SDK
  ESP.eraseConfig();
#else
  WiFi.disconnect(true, true);
#endif
  delay(200);
  ESP.restart();
  while (true) delay(100);
}

}  // namespace storage
```

- [ ] **Step 3: Estado compartilhado** — `firmware/src/context.h`

```cpp
#pragma once
#include <stdint.h>

#include "miblo_alerts.h"
#include "miblo_config.h"
#include "miblo_i18n.h"
#include "miblo_overview.h"
#include "miblo_security.h"
#include "miblo_snapshot.h"

// Estado compartilhado entre os módulos de hardware. Os módulos (web, api, net) só alteram o
// estado e levantam flags; o loop do app (app.cpp) reage às flags.
struct Identity {
  char id[16];           // "miblo-4f2a" (ID do mDNS/TXT e host)
  char defaultName[16];  // "Miblo-4F2A"
  char apSsid[24];       // "Miblo-Setup-4F2A"
};

struct Context {
  Identity ident{};
  miblo::Config cfg;
  miblo::TokenStore tokens;
  miblo::PairingGuard pairing;
  miblo::PresenceGate presence;
  miblo::Snapshot snap{};
  miblo::AlertSequencer alerts;
  miblo::RunTracker runs;

  bool hasSnapshot = false;
  uint32_t lastSnapshotMs = 0;
  bool usageEverSeen = false;

  // flags para o loop principal
  bool configChanged = false;
  bool factoryResetRequested = false;
  bool rebootRequested = false;
  uint32_t rebootAtMs = 0;
  bool justPaired = false;
  uint32_t pairedAtMs = 0;
  char pairedHost[33] = "";
  bool showPairCode = false;
  uint32_t pairCodeAtMs = 0;
  bool updating = false;
  uint8_t updatePct = 0;
};

extern Context ctx;

// Idioma da tela: o escolhido na página ou, em modo automático, o último negociado.
inline miblo::Lang uiLang() { return ctx.cfg.lang; }
// Nome do aparelho: o configurado ou "Miblo-XXXX".
inline const char* deviceName() { return ctx.cfg.name[0] ? ctx.cfg.name : ctx.ident.defaultName; }
```

`firmware/src/context.cpp`:

```cpp
#include "context.h"

Context ctx;
```

- [ ] **Step 4: Wi-Fi** — `firmware/src/platform/net.h`

```cpp
#pragma once
#include <Arduino.h>

#include "miblo_policy.h"

// Wi-Fi: usa as credenciais salvas no SDK (inclusive as do firmware anterior); sem credenciais,
// com senha errada ou após 2 min sem conexão, abre a rede de setup "Miblo-Setup-XXXX" com DNS
// cativo — e continua tentando a rede salva.
namespace net {

void begin(uint32_t nowMs);
void loop(uint32_t nowMs);
miblo::NetState state();
bool apActive();
bool connected();
// Número que muda a cada nova conexão (para reanunciar o mDNS).
uint32_t connectionId();
String ip();
// Chamado pelo portal: conecta na nova rede logo depois de a resposta HTTP sair.
void submitCredentials(const char* ssid, const char* pass, uint32_t nowMs);
// Reaplica o fuso (TZ POSIX de ctx.cfg.tz) e o NTP.
void applyTimezone();

}  // namespace net
```

`firmware/src/platform/net.cpp`:

```cpp
#include "net.h"

#include <DNSServer.h>
#include <time.h>

#include "../context.h"
#include "platform.h"

namespace net {

static miblo::NetPolicy policy;
static DNSServer dns;
static bool apOn = false;
static bool wasConnected = false;
static uint32_t connId = 0;
static uint32_t lastRetryMs = 0;
static bool pendingCreds = false;
static uint32_t pendingAtMs = 0;
static char pendingSsid[33];
static char pendingPass[65];

static miblo::LinkStatus link() {
  switch (WiFi.status()) {
    case WL_CONNECTED: return miblo::LinkStatus::Connected;
#if defined(ESP8266)
    case WL_WRONG_PASSWORD: return miblo::LinkStatus::WrongPassword;
#endif
    default: return miblo::LinkStatus::Down;
  }
}

static void startAp() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(ctx.ident.apSsid);
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());
  apOn = true;
}

static void stopAp() {
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apOn = false;
}

void applyTimezone() { configTime(ctx.cfg.tz, "pool.ntp.org", "time.google.com"); }

void begin(uint32_t nowMs) {
  uint32_t chip = chipId() & 0xFFFF;
  snprintf(ctx.ident.id, sizeof(ctx.ident.id), "miblo-%04x", (unsigned)chip);
  snprintf(ctx.ident.defaultName, sizeof(ctx.ident.defaultName), "Miblo-%04X", (unsigned)chip);
  snprintf(ctx.ident.apSsid, sizeof(ctx.ident.apSsid), "Miblo-Setup-%04X", (unsigned)chip);

  WiFi.persistent(true);
  WiFi.mode(WIFI_STA);
#if defined(ESP8266)
  WiFi.hostname(ctx.ident.id);
#else
  WiFi.setHostname(ctx.ident.id);
#endif
  WiFi.setAutoReconnect(true);
  bool hasCreds = WiFi.SSID().length() > 0;  // salvo no SDK (firmware anterior ou nosso portal)
  if (hasCreds) WiFi.begin();
  policy.begin(hasCreds, nowMs);
  lastRetryMs = nowMs;
  applyTimezone();
}

void loop(uint32_t nowMs) {
  if (pendingCreds && nowMs - pendingAtMs >= 500) {
    pendingCreds = false;
    WiFi.begin(pendingSsid, pendingPass);  // persistent(true): o SDK salva
    policy.credentialsSubmitted(nowMs);
    lastRetryMs = nowMs;
  }

  miblo::NetState st = policy.update(link(), nowMs);
  if (policy.apWanted() && !apOn) startAp();
  if (!policy.apWanted() && apOn) stopAp();
  if (apOn) dns.processNextRequest();

  bool isConnected = st == miblo::NetState::Connected;
  if (isConnected && !wasConnected) {
    connId++;
    applyTimezone();
  }
  wasConnected = isConnected;

  // Com a rede de setup no ar, continua tentando a rede salva a cada 60 s.
  if (!isConnected && apOn && WiFi.SSID().length() > 0 && nowMs - lastRetryMs >= 60000) {
    lastRetryMs = nowMs;
    WiFi.begin();
  }
}

miblo::NetState state() { return policy.state(); }
bool apActive() { return apOn; }
bool connected() { return policy.state() == miblo::NetState::Connected; }
uint32_t connectionId() { return connId; }

String ip() {
  if (connected()) return WiFi.localIP().toString();
  if (apOn) return WiFi.softAPIP().toString();
  return String("0.0.0.0");
}

void submitCredentials(const char* ssid, const char* pass, uint32_t nowMs) {
  strlcpy(pendingSsid, ssid, sizeof(pendingSsid));
  strlcpy(pendingPass, pass, sizeof(pendingPass));
  pendingCreds = true;
  pendingAtMs = nowMs;
}

}  // namespace net
```

- [ ] **Step 5: mDNS** — `firmware/src/platform/mdns_service.h`

```cpp
#pragma once
#include <stdint.h>

// Anuncia e responde `_miblo._tcp.local` e `miblo-xxxx.local` (respondedor próprio: ver miblo_mdns).
namespace mdns {

void loop(uint32_t nowMs);
// Reanuncia (ex.: o nome do aparelho mudou).
void announce();

}  // namespace mdns
```

`firmware/src/platform/mdns_service.cpp`:

```cpp
#include "mdns_service.h"

#include <WiFiUdp.h>

#include "../context.h"
#include "miblo_mdns.h"
#include "miblo_version.h"
#include "net.h"
#include "platform.h"

namespace mdns {

static WiFiUDP udp;
static uint32_t boundConn = 0;
static bool bound = false;
static uint8_t announcesLeft = 0;
static uint32_t nextAnnounceMs = 0;
static uint8_t in[512];
static uint8_t out[512];
static char txtId[32];
static char txtName[80];
static char txtFw[24];

static miblo::MdnsInfo info() {
  miblo::MdnsInfo i{};
  i.instance = deviceName();
  i.host = ctx.ident.id;
  IPAddress ip = WiFi.localIP();
  for (int k = 0; k < 4; k++) i.ip[k] = ip[k];
  i.port = 80;
  snprintf(txtId, sizeof(txtId), "id=%s", ctx.ident.id);
  snprintf(txtName, sizeof(txtName), "name=%s", deviceName());
  snprintf(txtFw, sizeof(txtFw), "fw=%s", MIBLO_FW_VERSION);
  i.txt[0] = txtId;
  i.txt[1] = txtName;
  i.txt[2] = txtFw;
  i.txtCount = 3;
  return i;
}

static void sendMulticast(const uint8_t* data, size_t len) {
#if defined(ESP8266)
  udp.beginPacketMulticast(IPAddress(224, 0, 0, 251), miblo::kMdnsPort, WiFi.localIP());
#else
  udp.beginPacket(IPAddress(224, 0, 0, 251), miblo::kMdnsPort);
#endif
  udp.write(data, len);
  udp.endPacket();
}

void announce() {
  announcesLeft = 2;  // RFC 6762: pelo menos dois anúncios, 1 s de intervalo
  nextAnnounceMs = millis();
}

void loop(uint32_t nowMs) {
  if (!net::connected()) {
    if (bound) {
      udp.stop();
      bound = false;
    }
    return;
  }
  if (!bound || boundConn != net::connectionId()) {
    udp.stop();
#if defined(ESP8266)
    bound = udp.beginMulticast(WiFi.localIP(), IPAddress(224, 0, 0, 251), miblo::kMdnsPort);
#else
    bound = udp.beginMulticast(IPAddress(224, 0, 0, 251), miblo::kMdnsPort);
#endif
    boundConn = net::connectionId();
    announce();
  }
  if (!bound) return;

  if (announcesLeft > 0 && (int32_t)(nowMs - nextAnnounceMs) >= 0) {
    miblo::MdnsInfo i = info();
    size_t n = miblo::mdnsAnnounce(i, out, sizeof(out));
    if (n) sendMulticast(out, n);
    announcesLeft--;
    nextAnnounceMs = nowMs + 1000;
  }

  int len = udp.parsePacket();
  if (len <= 0) return;
  if (len > (int)sizeof(in)) {
    udp.flush();
    return;
  }
  udp.read(in, len);
  IPAddress from = udp.remoteIP();
  uint16_t port = udp.remotePort();
  miblo::MdnsInfo i = info();
  miblo::MdnsReply r = miblo::mdnsRespond(in, (size_t)len, port, i, out, sizeof(out));
  if (!r.len) return;
  if (r.unicast) {
    udp.beginPacket(from, port);
    udp.write(out, r.len);
    udp.endPacket();
  } else {
    sendMulticast(out, r.len);
  }
}

}  // namespace mdns
```

- [ ] **Step 6: Páginas web** — `firmware/src/web.h`

```cpp
#pragma once
#include "platform/platform.h"

#include "miblo_i18n.h"

// Páginas para humanos (localizadas): portal cativo de Wi-Fi e página de configuração.
namespace web {

void begin(WebServerT& server);

// Helpers reutilizados pela página de update (ota.cpp).
miblo::Lang pageLang(WebServerT& server);
void pageStart(String& out, miblo::Lang lang, const char* title);
void pageEnd(String& out);
void appendEscaped(String& out, const char* s);
String tr(miblo::Lang lang, miblo::S id);
void sendJson(WebServerT& server, int code, const char* json);

}  // namespace web
```

`firmware/src/web.cpp`:

```cpp
#include "web.h"

#include <ArduinoJson.h>

#include "context.h"
#include "miblo_version.h"
#include "platform/net.h"

namespace web {

using miblo::Lang;
using miblo::S;

static WebServerT* srv = nullptr;

static const char kCss[] PROGMEM =
    "body{font-family:system-ui,sans-serif;background:#0b0b0d;color:#eee;margin:0;padding:16px;max-width:480px}"
    "h1{font-size:20px;margin:4px 0 12px}h2{font-size:16px;margin:24px 0 8px;color:#f5a524}"
    "label{display:block;margin:12px 0 4px;color:#aaa}input,select{width:100%;box-sizing:border-box;padding:10px;"
    "background:#1a1a1e;color:#eee;border:1px solid #333;border-radius:6px;font-size:16px}"
    "input[type=checkbox]{width:auto;margin-right:8px}button{margin-top:16px;padding:12px;width:100%;border:0;"
    "border-radius:8px;background:#f5a524;color:#111;font-weight:700;font-size:16px}"
    "button.s{background:#333;color:#eee}button.d{background:#ef4444;color:#fff}.m{color:#888}.w{color:#f5a524}"
    "a{color:#60a5fa}";

// Monta um POSIX TZ a partir do fuso do navegador (offset de janeiro/julho + regra de horário de
// verão por região). Sem horário de verão → offset fixo, ex. "<-03>3".
static const char kTzJs[] PROGMEM =
    "function posixTz(){const y=new Date().getFullYear();"
    "const jan=-new Date(y,0,1).getTimezoneOffset(),jul=-new Date(y,6,1).getTimezoneOffset();"
    "const std=Math.min(jan,jul),dst=Math.max(jan,jul);"
    "const p=n=>String(n).padStart(2,'0');"
    "const nm=m=>'<'+(m<0?'-':'+')+p(Math.floor(Math.abs(m)/60))+(Math.abs(m)%60?p(Math.abs(m)%60):'')+'>';"
    "const off=m=>{const a=Math.abs(m);return (m>0?'-':'')+Math.floor(a/60)+(a%60?':'+p(a%60):'')};"
    "let tz=nm(std)+off(std);if(jan===jul)return tz;"
    "const zone=(Intl.DateTimeFormat().resolvedOptions().timeZone||'');let rule;"
    "if(zone.startsWith('Europe/')){const h=1+std/60;rule=',M3.5.0/'+h+',M10.5.0/'+(h+1);}"
    "else if(jul>jan){rule=',M3.2.0,M11.1.0';}else{rule=',M10.1.0,M4.1.0/3';}"
    "return tz+nm(dst)+(dst-std!==60?off(dst):'')+rule;}";

String tr(Lang lang, S id) {
  char b[160];
  miblo::tr(lang, id, b, sizeof(b));
  return String(b);
}

void appendEscaped(String& out, const char* s) {
  for (; *s; s++) {
    switch (*s) {
      case '&': out += F("&amp;"); break;
      case '<': out += F("&lt;"); break;
      case '>': out += F("&gt;"); break;
      case '"': out += F("&quot;"); break;
      case '\'': out += F("&#39;"); break;
      default: out += *s;
    }
  }
}

void sendJson(WebServerT& server, int code, const char* json) {
  server.send(code, F("application/json"), json);
}

Lang pageLang(WebServerT& server) {
  if (ctx.cfg.langSet) return ctx.cfg.lang;
  Lang l = miblo::negotiateLang(server.header(F("Accept-Language")).c_str());
  if (l != ctx.cfg.lang) {  // modo automático: a tela acompanha o idioma do último navegador
    ctx.cfg.lang = l;
    ctx.configChanged = true;
  }
  return l;
}

void pageStart(String& out, Lang lang, const char* title) {
  out.reserve(7000);
  out += F("<!doctype html><html lang=\"");
  out += miblo::langCode(lang);
  out += F("\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
           "<title>");
  appendEscaped(out, title);
  out += F("</title><style>");
  out += FPSTR(kCss);
  out += F("</style></head><body>");
}

void pageEnd(String& out) { out += F("</body></html>"); }

static void langOptions(String& out, Lang selected, bool withAuto) {
  if (withAuto) {
    out += F("<option value=\"\">Auto</option>");
  }
  for (int i = 0; i < (int)Lang::Count; i++) {
    out += F("<option value=\"");
    out += miblo::langCode((Lang)i);
    out += '"';
    if (!withAuto && (Lang)i == selected) out += F(" selected");
    out += '>';
    out += miblo::langName((Lang)i);
    out += F("</option>");
  }
}

// ---------- portal cativo (rede de setup) ----------

static void portalPage() {
  Lang lang = pageLang(*srv);
  String out;
  pageStart(out, lang, tr(lang, S::WebSetupTitle).c_str());
  out += F("<h1>");
  appendEscaped(out, tr(lang, S::WebSetupTitle).c_str());
  out += F("</h1><form method=\"post\" action=\"/wifi\"><label>");
  appendEscaped(out, tr(lang, S::WebChooseNetwork).c_str());
  out += F("</label><select name=\"ssid\" id=\"ssid\" onchange=\"o()\">");
  int n = WiFi.scanNetworks();
  if (n > 32) n = 32;
  int order[32];
  for (int i = 0; i < n; i++) order[i] = i;
  for (int i = 1; i < n; i++) {  // ordena por sinal (inserção; n é pequeno)
    int cur = order[i];
    int j = i - 1;
    while (j >= 0 && WiFi.RSSI(order[j]) < WiFi.RSSI(cur)) {
      order[j + 1] = order[j];
      j--;
    }
    order[j + 1] = cur;
  }
  int shown = 0;
  for (int i = 0; i < n && shown < 15; i++) {
    String ssid = WiFi.SSID(order[i]);
    bool dup = ssid.length() == 0;
    for (int k = 0; k < i && !dup; k++) {
      if (WiFi.SSID(order[k]) == ssid) dup = true;
    }
    if (dup) continue;
    out += F("<option value=\"");
    appendEscaped(out, ssid.c_str());
    out += F("\">");
    appendEscaped(out, ssid.c_str());
    out += F("</option>");
    shown++;
  }
  WiFi.scanDelete();
  out += F("<option value=\"\">");
  appendEscaped(out, tr(lang, S::WebOtherNetwork).c_str());
  out += F("</option></select><div id=\"other\" hidden><label>");
  appendEscaped(out, tr(lang, S::WebNetworkName).c_str());
  out += F("</label><input name=\"ssid_other\" maxlength=\"32\"></div><label>");
  appendEscaped(out, tr(lang, S::WebPassword).c_str());
  out += F("</label><input name=\"pass\" type=\"password\" maxlength=\"64\"><label>");
  appendEscaped(out, tr(lang, S::WebTimezone).c_str());
  out += F("</label><input name=\"tz\" id=\"tz\" maxlength=\"47\" value=\"");
  appendEscaped(out, ctx.cfg.tz);
  out += F("\"><label>");
  appendEscaped(out, tr(lang, S::WebLanguage).c_str());
  out += F("</label><select name=\"lang\">");
  langOptions(out, lang, false);
  out += F("</select><button>");
  appendEscaped(out, tr(lang, S::WebConnect).c_str());
  out += F("</button></form><script>");
  out += FPSTR(kTzJs);
  out += F("document.getElementById('tz').value=posixTz();"
           "function o(){document.getElementById('other').hidden=document.getElementById('ssid').value!==''}o();"
           "</script>");
  pageEnd(out);
  srv->send(200, F("text/html; charset=utf-8"), out);
}

static void handleWifi() {
  String ssid = srv->arg(F("ssid"));
  if (ssid.length() == 0) ssid = srv->arg(F("ssid_other"));
  String pass = srv->arg(F("pass"));
  if (ssid.length() == 0 || ssid.length() > 32 || pass.length() > 64) {
    srv->send(400, F("text/plain"), F("bad ssid"));
    return;
  }
  StaticJsonDocument<256> patch;
  if (srv->arg(F("tz")).length()) patch["tz"] = srv->arg(F("tz"));
  if (srv->arg(F("lang")).length()) patch["lang"] = srv->arg(F("lang"));
  if (miblo::applyConfigPatch(ctx.cfg, patch.as<JsonObjectConst>(), nullptr)) ctx.configChanged = true;

  Lang lang = ctx.cfg.lang;
  String out;
  pageStart(out, lang, tr(lang, S::WebSetupTitle).c_str());
  out += F("<h1>");
  appendEscaped(out, tr(lang, S::WebConnecting).c_str());
  out += F("</h1>");
  pageEnd(out);
  srv->send(200, F("text/html; charset=utf-8"), out);
  net::submitCredentials(ssid.c_str(), pass.c_str(), millis());
}

// ---------- página de configuração (rede de casa) ----------

static void settingsPage() {
  Lang lang = pageLang(*srv);
  String out;
  pageStart(out, lang, deviceName());
  out += F("<h1>");
  appendEscaped(out, deviceName());
  out += F("</h1><p class=\"m\">");
  appendEscaped(out, tr(lang, S::WebVersion).c_str());
  out += F(" " MIBLO_FW_VERSION " &middot; ");
  char line[96];
  snprintf(line, sizeof(line), tr(lang, S::WebPairedCount).c_str(), (unsigned)ctx.tokens.count());
  appendEscaped(out, line);
  out += F("</p>");
  if (!ctx.usageEverSeen) {
    out += F("<p class=\"w\">");
    appendEscaped(out, tr(lang, S::WebLimitsHint).c_str());
    out += F("</p>");
  }
  out += F("<h2>");
  appendEscaped(out, tr(lang, S::WebSettings).c_str());
  out += F("</h2><label>");
  appendEscaped(out, tr(lang, S::WebMode).c_str());
  out += F("</label><select id=\"mode\"><option value=\"overview\">");
  appendEscaped(out, tr(lang, S::ModeOverview).c_str());
  out += F("</option><option value=\"limits\">");
  appendEscaped(out, tr(lang, S::ModeLimits).c_str());
  out += F("</option><option value=\"sessions\">");
  appendEscaped(out, tr(lang, S::ModeSessions).c_str());
  out += F("</option></select><label>");
  appendEscaped(out, tr(lang, S::WebBrightness).c_str());
  out += F("</label><input id=\"brightness\" type=\"range\" min=\"5\" max=\"100\"><label><input id=\"alerts\" "
           "type=\"checkbox\">");
  appendEscaped(out, tr(lang, S::WebAlerts).c_str());
  out += F("</label><label>");
  appendEscaped(out, tr(lang, S::WebHeroPerm).c_str());
  out += F("</label><input id=\"heroPermSec\" type=\"number\" min=\"3\" max=\"60\"><label>");
  appendEscaped(out, tr(lang, S::WebHeroDone).c_str());
  out += F("</label><input id=\"heroDoneSec\" type=\"number\" min=\"2\" max=\"60\"><label>");
  appendEscaped(out, tr(lang, S::WebReminder).c_str());
  out += F("</label><input id=\"reminderMin\" type=\"number\" min=\"0\" max=\"30\"><label><input id=\"discreet\" "
           "type=\"checkbox\">");
  appendEscaped(out, tr(lang, S::WebDiscreet).c_str());
  out += F("</label><label>");
  appendEscaped(out, tr(lang, S::WebDeviceName).c_str());
  out += F("</label><input id=\"name\" maxlength=\"20\" placeholder=\"");
  appendEscaped(out, ctx.ident.defaultName);
  out += F("\"><label>");
  appendEscaped(out, tr(lang, S::WebTimezone).c_str());
  out += F("</label><input id=\"tz\" maxlength=\"47\"><label>");
  appendEscaped(out, tr(lang, S::WebLanguage).c_str());
  out += F("</label><select id=\"lang\">");
  langOptions(out, lang, true);
  out += F("</select><button onclick=\"save()\">");
  appendEscaped(out, tr(lang, S::WebSave).c_str());
  out += F("</button><p id=\"st\" class=\"m\"></p><h2>");
  appendEscaped(out, tr(lang, S::WebFirmware).c_str());
  out += F("</h2><p><a href=\"/update\">");
  appendEscaped(out, tr(lang, S::WebFirmware).c_str());
  out += F("</a></p><button class=\"s\" onclick=\"post('/pair-code')\">");
  appendEscaped(out, tr(lang, S::WebShowPairCode).c_str());
  out += F("</button><h2>");
  appendEscaped(out, tr(lang, S::WebFactoryReset).c_str());
  out += F("</h2><p class=\"m\">");
  appendEscaped(out, tr(lang, S::WebResetConfirm).c_str());
  out += F("</p><button class=\"d\" onclick=\"rst()\">");
  appendEscaped(out, tr(lang, S::WebFactoryReset).c_str());
  out += F("</button><script>const C=");

  DynamicJsonDocument cfg(768);
  miblo::configToJson(ctx.cfg, cfg.to<JsonObject>());
  serializeJson(cfg, out);
  out += F(";const T=");
  DynamicJsonDocument txt(768);
  txt["saved"] = tr(lang, S::WebSaved);
  txt["failed"] = tr(lang, S::WebFailed);
  txt["hint"] = tr(lang, S::WebCodeHint);
  txt["bad"] = tr(lang, S::WebBadCode);
  serializeJson(txt, out);
  out += F(";");
  out += FPSTR(kTzJs);
  out += F(
      "const $=k=>document.getElementById(k);"
      "for(const k in C){const e=$(k);if(!e)continue;if(e.type==='checkbox')e.checked=C[k];else e.value=C[k];}"
      "function val(k){const e=$(k);return e.type==='checkbox'?e.checked:"
      "(e.type==='number'||e.type==='range')?Number(e.value):e.value;}"
      "function save(){const b={};for(const k of ['mode','brightness','alerts','heroPermSec','heroDoneSec',"
      "'reminderMin','discreet','name','tz','lang'])b[k]=val(k);"
      "fetch('/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)})"
      ".then(r=>{$('st').textContent=r.ok?T.saved:T.failed;}).catch(()=>{$('st').textContent=T.failed;});}"
      "function post(u){return fetch(u,{method:'POST'});}"
      "function rst(){post('/reset-code').then(()=>{const c=prompt(T.hint);if(!c)return;"
      "fetch('/factory-reset?code='+encodeURIComponent(c),{method:'POST'}).then(r=>{if(!r.ok)alert(T.bad);});});}"
      "if(C.tz==='UTC0'){const z=posixTz();if(z!=='UTC0'){$('tz').value=z;save();}}"
      "</script>");
  pageEnd(out);
  srv->send(200, F("text/html; charset=utf-8"), out);
}

static void handleSettings() {
  DynamicJsonDocument doc(1024);
  if (deserializeJson(doc, srv->arg(F("plain"))) || !doc.is<JsonObject>()) {
    sendJson(*srv, 400, "{\"error\":\"bad json\"}");
    return;
  }
  const char* bad = nullptr;
  if (!miblo::applyConfigPatch(ctx.cfg, doc.as<JsonObjectConst>(), &bad)) {
    String err = String(F("{\"error\":\"invalid\",\"field\":\"")) + bad + F("\"}");
    sendJson(*srv, 400, err.c_str());
    return;
  }
  ctx.configChanged = true;
  sendJson(*srv, 200, "{\"ok\":true}");
}

static void handleRoot() {
  if (net::apActive() && !net::connected()) portalPage();
  else settingsPage();
}

static void handlePairCode() {
  ctx.showPairCode = true;
  ctx.pairCodeAtMs = millis();
  sendJson(*srv, 200, "{\"ok\":true}");
}

static void handleResetCode() {
  char code[5];
  miblo::formatCode(hwRandom(), code);
  ctx.presence.open(miblo::PresenceGate::Purpose::Reset, code, millis());
  sendJson(*srv, 200, "{\"ok\":true}");
}

static void handleFactoryReset() {
  if (!ctx.presence.check(miblo::PresenceGate::Purpose::Reset, srv->arg(F("code")).c_str(), millis())) {
    sendJson(*srv, 403, "{\"error\":\"bad code\"}");
    return;
  }
  sendJson(*srv, 200, "{\"ok\":true}");
  ctx.factoryResetRequested = true;
}

static bool captiveRedirect() {
  if (!net::apActive() || net::connected()) return false;
  String host = srv->hostHeader();
  if (host == WiFi.softAPIP().toString()) return false;
  srv->sendHeader(F("Location"), String(F("http://")) + WiFi.softAPIP().toString() + F("/"), true);
  srv->send(302, F("text/plain"), "");
  return true;
}

void begin(WebServerT& server) {
  srv = &server;
  // Content-Length: progresso do OTA (ota.cpp); Authorization: API (api.cpp).
  server.collectHeaders("Accept-Language", "Authorization", "Content-Length");
  server.on(F("/"), HTTP_GET, handleRoot);
  server.on(F("/wifi"), HTTP_POST, handleWifi);
  server.on(F("/settings"), HTTP_POST, handleSettings);
  server.on(F("/pair-code"), HTTP_POST, handlePairCode);
  server.on(F("/reset-code"), HTTP_POST, handleResetCode);
  server.on(F("/factory-reset"), HTTP_POST, handleFactoryReset);
  // Detecção de portal cativo (Android, iOS/macOS, Windows): tudo vai para a página de setup.
  for (const char* path : {"/generate_204", "/gen_204", "/hotspot-detect.html", "/library/test/success.html",
                           "/ncsi.txt", "/connecttest.txt", "/redirect", "/fwlink"}) {
    server.on(path, HTTP_GET, [] {
      if (!captiveRedirect()) handleRoot();
    });
  }
  server.onNotFound([] {
    if (captiveRedirect()) return;
    sendJson(*srv, 404, "{\"error\":\"not found\"}");
  });
}

}  // namespace web
```

- [ ] **Step 7: Build do aparelho**

Run: `cd firmware && .venv/bin/pio run -e geekmagic_ultra`
Expected: `[SUCCESS]`, sem `warning:` vindos de `src/` ou `lib/miblo_*` (os avisos de `TOUCH_CS` do TFT_eSPI e `#pragma mark` do QRCode são esperados). O demo da Task 11 continua sendo o `main.cpp`; os módulos novos compilam mas só entram em uso na Task 13.

- [ ] **Step 8: Commit**

```bash
git add firmware/src/platform/platform.h firmware/src/platform/storage.h firmware/src/platform/storage.cpp firmware/src/platform/net.h firmware/src/platform/net.cpp firmware/src/platform/mdns_service.h firmware/src/platform/mdns_service.cpp firmware/src/context.h firmware/src/context.cpp firmware/src/web.h firmware/src/web.cpp
git commit -m "feat(firmware): persistence, Wi-Fi with recovery AP and captive portal, mDNS service, localized pages"
```

---

### Task 13: API do bridge, OTA com código na tela e aplicativo completo

**Files:**
- Create: `firmware/src/api.h`, `firmware/src/api.cpp`
- Create: `firmware/src/platform/ota.h`, `firmware/src/platform/ota.cpp`
- Create: `firmware/src/app.h`, `firmware/src/app.cpp`
- Modify: `firmware/src/main.cpp` (versão final)

**Interfaces:**
- Consumes: tudo das Tasks 1–12; `board::*` (Task 11).
- Produces: rotas `GET /api/info`, `POST /api/pair`, `POST /api/state`, `POST /api/config`, `POST /api/reset`, `GET /update`, `POST /update?code=XXXX` (multipart, campo `firmware`); `api::begin(WebServerT&)`; `ota::begin(WebServerT&, ota::ProgressHook)`; `app::setup()`, `app::loop()`.

Comportamento:
- `POST /api/state`: 401 sem Bearer válido; > 3072 B ou JSON inválido → 400 e a tela anterior continua; aceito → `lastSnapshotMs`, `AlertSequencer::ingest`, `RunTracker::observe`, e se o relógio ainda não tem NTP usa `snap.now`.
- `POST /api/pair`: `PairingGuard` (403 / 429 com `retryAfter`) → token de 128 bits do gerador de hardware, salvo em `/miblo/pairs.json`, tela "Pareado com <host>" por 5 s.
- `/update`: `GET` abre o `PresenceGate(Update)` (tela mostra o código por até 5 min) e serve a página; o `POST` só grava se `?code=` bater (Bearer **não** substitui o código); falha → 500 e a imagem antiga continua; sucesso → "OK" e reinício em 0,8 s. Barra de progresso na tela durante o upload.
- `app`: boot com mascote (≥ 2,4 s), contador de liga/desliga (grava 0 após 10 s), código de pareamento aleatório por boot, ~10 quadros/s, `selectScreen` decide a tela; trocar de tela limpa e invalida o cache; config alterada → salva, aplica brilho/tempos/fuso e reanuncia o mDNS.

- [ ] **Step 1: API** — `firmware/src/api.h`

```cpp
#pragma once
#include "platform/platform.h"

// API HTTP usada pelo bridge (contrato: Plano 1, Task 5 — plugin/test/fakes/fake-device.js).
namespace api {

void begin(WebServerT& server);

}  // namespace api
```

`firmware/src/api.cpp`:

```cpp
#include "api.h"

#include <ArduinoJson.h>
#include <time.h>

#include "board.h"
#include "context.h"
#include "miblo_utf8.h"
#include "miblo_version.h"
#include "platform/storage.h"
#include "web.h"

namespace api {

static WebServerT* srv = nullptr;
static char body[miblo::kSnapshotMaxBytes + 1];

static void json(int code, const char* s) { web::sendJson(*srv, code, s); }

static bool authorized() {
  char token[40];
  if (!miblo::bearerToken(srv->header(F("Authorization")).c_str(), token, sizeof(token))) return false;
  return ctx.tokens.matches(token);
}

static void handleInfo() {
  StaticJsonDocument<384> doc;
  doc["id"] = ctx.ident.id;
  doc["name"] = deviceName();
  doc["fw"] = MIBLO_FW_VERSION;
  doc["proto"] = MIBLO_PROTO;
  doc["paired"] = ctx.tokens.count() > 0;
  doc["board"] = board::kName;
  JsonObject screen = doc.createNestedObject("screen");
  screen["w"] = board::kScreen.w;
  screen["h"] = board::kScreen.h;
  JsonArray caps = doc.createNestedArray("caps");  // futuro: "buttons", "touch", "buzzer", "led"
  for (uint8_t i = 0; i < board::kCapCount; i++) caps.add(board::cap(i));
  char flash[12];
  snprintf(flash, sizeof(flash), "%06x", (unsigned)flashChipId());
  doc["flash"] = flash;
  String out;
  serializeJson(doc, out);
  json(200, out.c_str());
}

static void handlePair() {
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, srv->arg(F("plain")))) {
    json(400, "{\"error\":\"bad json\"}");
    return;
  }
  char code[8];
  JsonVariantConst c = doc["code"];
  if (c.is<const char*>()) strlcpy(code, c.as<const char*>(), sizeof(code));
  else if (c.is<int>()) snprintf(code, sizeof(code), "%04d", c.as<int>());
  else code[0] = 0;
  uint32_t now = millis();
  switch (ctx.pairing.check(code, now)) {
    case miblo::PairingGuard::Result::Locked: {
      char out[64];
      snprintf(out, sizeof(out), "{\"error\":\"locked\",\"retryAfter\":%u}",
               (unsigned)((ctx.pairing.lockRemainingMs(now) + 999) / 1000));
      json(429, out);
      return;
    }
    case miblo::PairingGuard::Result::BadCode:
      json(403, "{\"error\":\"bad code\"}");
      return;
    case miblo::PairingGuard::Result::Ok:
      break;
  }
  uint8_t rnd[16];
  for (int i = 0; i < 16; i += 4) {
    uint32_t r = hwRandom();
    memcpy(rnd + i, &r, 4);
  }
  char token[33];
  miblo::makeToken(rnd, token);
  char host[33];
  miblo::utf8Copy(host, sizeof(host), doc["host"] | "computer", 20);
  ctx.tokens.add(token, host);
  storage::saveTokens(ctx.tokens);
  strlcpy(ctx.pairedHost, host, sizeof(ctx.pairedHost));
  ctx.justPaired = true;
  ctx.pairedAtMs = now;
  ctx.showPairCode = false;
  String out = String(F("{\"token\":\"")) + token + F("\"}");
  json(200, out.c_str());
}

static void handleState() {
  if (!authorized()) {
    json(401, "{\"error\":\"unauthorized\"}");
    return;
  }
  const String& plain = srv->arg(F("plain"));
  if (plain.length() > miblo::kSnapshotMaxBytes) {
    json(400, "{\"error\":\"too large\"}");
    return;
  }
  memcpy(body, plain.c_str(), plain.length());
  body[plain.length()] = 0;
  miblo::ParseResult r = miblo::parseSnapshot(body, plain.length(), ctx.snap);
  if (r != miblo::ParseResult::Ok) {
    json(400, r == miblo::ParseResult::BadVersion ? "{\"error\":\"bad version\"}" : "{\"error\":\"bad json\"}");
    return;  // a última tela válida continua
  }
  uint32_t now = millis();
  ctx.hasSnapshot = true;
  ctx.lastSnapshotMs = now;
  if (ctx.snap.hasUsage) ctx.usageEverSeen = true;
  ctx.alerts.ingest(ctx.snap, now);
  ctx.runs.observe(ctx.snap);
  if (time(nullptr) < 1600000000 && ctx.snap.now > 1600000000) {
    timeval tv{(time_t)ctx.snap.now, 0};  // relógio antes do NTP: usa a hora do computador
    settimeofday(&tv, nullptr);
  }
  json(200, "{\"ok\":true}");
}

static void handleConfig() {
  if (!authorized()) {
    json(401, "{\"error\":\"unauthorized\"}");
    return;
  }
  DynamicJsonDocument doc(1024);
  if (deserializeJson(doc, srv->arg(F("plain"))) || !doc.is<JsonObject>()) {
    json(400, "{\"error\":\"bad json\"}");
    return;
  }
  const char* bad = nullptr;
  if (!miblo::applyConfigPatch(ctx.cfg, doc.as<JsonObjectConst>(), &bad)) {
    String out = String(F("{\"error\":\"invalid\",\"field\":\"")) + bad + F("\"}");
    json(400, out.c_str());
    return;
  }
  ctx.configChanged = true;
  json(200, "{\"ok\":true}");
}

static void handleReset() {
  if (!authorized()) {
    json(401, "{\"error\":\"unauthorized\"}");
    return;
  }
  json(200, "{\"ok\":true}");
  ctx.factoryResetRequested = true;
}

void begin(WebServerT& server) {
  srv = &server;
  server.on(F("/api/info"), HTTP_GET, handleInfo);
  server.on(F("/api/pair"), HTTP_POST, handlePair);
  server.on(F("/api/state"), HTTP_POST, handleState);
  server.on(F("/api/config"), HTTP_POST, handleConfig);
  server.on(F("/api/reset"), HTTP_POST, handleReset);
}

}  // namespace api
```

- [ ] **Step 2: OTA** — `firmware/src/platform/ota.h`

```cpp
#pragma once
#include "platform.h"

// /update sempre disponível (inclusive na rede de setup). GET mostra a página e um código de
// 4 dígitos na tela; o POST (multipart, campo "firmware") só grava com ?code=<código da tela>.
// O código é exigido mesmo com token Bearer (o token trafega em texto puro na rede local).
namespace ota {

using ProgressHook = void (*)(uint8_t pct);
void begin(WebServerT& server, ProgressHook onProgress);

}  // namespace ota
```

`firmware/src/platform/ota.cpp`:

```cpp
#include "ota.h"

#include <ArduinoJson.h>

#include "../context.h"
#include "../web.h"
#include "miblo_version.h"

namespace ota {

using miblo::Lang;
using miblo::PresenceGate;
using miblo::S;

static WebServerT* srv = nullptr;
static ProgressHook hook = nullptr;
static bool rejected = false;
static bool started = false;
static size_t expected = 0;
static uint8_t lastPct = 255;

static void page() {
  uint32_t now = millis();
  if (!ctx.presence.active(now) || ctx.presence.purpose() != PresenceGate::Purpose::Update) {
    char code[5];
    miblo::formatCode(hwRandom(), code);
    ctx.presence.open(PresenceGate::Purpose::Update, code, now);  // a tela passa a mostrar o código
  }
  Lang lang = web::pageLang(*srv);
  String out;
  web::pageStart(out, lang, web::tr(lang, S::WebFirmware).c_str());
  out += F("<h1>");
  web::appendEscaped(out, web::tr(lang, S::WebFirmware).c_str());
  out += F("</h1><p class=\"m\">");
  web::appendEscaped(out, web::tr(lang, S::WebVersion).c_str());
  out += F(" " MIBLO_FW_VERSION "</p><label>");
  web::appendEscaped(out, web::tr(lang, S::WebCodeHint).c_str());
  out += F("</label><input id=\"code\" inputmode=\"numeric\" maxlength=\"4\" autocomplete=\"off\"><label>.bin</label>"
           "<input id=\"f\" type=\"file\" accept=\".bin\"><button onclick=\"up()\">");
  web::appendEscaped(out, web::tr(lang, S::WebUpload).c_str());
  out += F("</button><p id=\"st\" class=\"m\"></p><script>const T=");
  StaticJsonDocument<512> t;
  t["ok"] = web::tr(lang, S::WebUpdateOk);
  t["bad"] = web::tr(lang, S::WebBadCode);
  t["failed"] = web::tr(lang, S::WebFailed);
  serializeJson(t, out);
  out += F(";const $=k=>document.getElementById(k);"
           "function up(){const f=$('f').files[0];if(!f)return;const d=new FormData();d.append('firmware',f);"
           "$('st').textContent='...';"
           "fetch('/update?code='+encodeURIComponent($('code').value),{method:'POST',body:d})"
           ".then(r=>r.text().then(x=>{$('st').textContent=r.ok?T.ok:(r.status===403?T.bad:T.failed+': '+x);}))"
           ".catch(()=>{$('st').textContent=T.failed;});}</script>");
  web::pageEnd(out);
  srv->send(200, F("text/html; charset=utf-8"), out);
}

static void upload() {
  HTTPUpload& up = srv->upload();
  if (up.status == UPLOAD_FILE_START) {
    rejected = !ctx.presence.check(PresenceGate::Purpose::Update, srv->arg(F("code")).c_str(), millis());
    started = false;
    if (rejected) return;
    expected = (size_t)srv->header(F("Content-Length")).toInt();  // inclui o envelope multipart
#if defined(ESP8266)
    uint32_t maxSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    started = Update.begin(maxSpace, U_FLASH);
#else
    started = Update.begin(UPDATE_SIZE_UNKNOWN);
#endif
    ctx.updating = started;
    ctx.updatePct = 0;
    lastPct = 255;
    if (hook) hook(0);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (rejected || !started) return;
    if (Update.write(up.buf, up.currentSize) != up.currentSize) return;
    uint8_t pct = expected ? (uint8_t)min<size_t>(100, up.totalSize * 100 / expected) : 0;
    if (pct != lastPct) {
      lastPct = pct;
      ctx.updatePct = pct;
      if (hook) hook(pct);
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (rejected || !started) return;
    Update.end(true);
    if (hook) hook(100);
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (started) Update.end(false);
    ctx.updating = false;
  }
}

static void done() {
  if (rejected) {
    web::sendJson(*srv, 403, "{\"error\":\"bad code\"}");
    return;
  }
  ctx.updating = false;
  if (!started || Update.hasError()) {
    String err = Update.getErrorString();
    srv->send(500, F("text/plain"), err.length() ? err : String(F("update failed")));
    return;  // a imagem anterior continua valendo
  }
  ctx.presence.close();
  srv->send(200, F("text/plain"), F("OK"));
  ctx.rebootRequested = true;
  ctx.rebootAtMs = millis() + 800;
}

void begin(WebServerT& server, ProgressHook onProgress) {
  srv = &server;
  hook = onProgress;
  server.on(F("/update"), HTTP_GET, page);
  server.on(F("/update"), HTTP_POST, done, upload);
}

}  // namespace ota
```

- [ ] **Step 3: Aplicativo** — `firmware/src/app.h`

```cpp
#pragma once

// Orquestra tudo: persistência, Wi-Fi, servidor HTTP, mDNS, alertas e telas.
namespace app {

void setup();
void loop();

}  // namespace app
```

`firmware/src/app.cpp`:

```cpp
#include "app.h"

#include <time.h>

#include "api.h"
#include "board.h"
#include "context.h"
#include "miblo_format.h"
#include "miblo_policy.h"
#include "platform/mdns_service.h"
#include "platform/net.h"
#include "platform/ota.h"
#include "platform/platform.h"
#include "platform/storage.h"
#include "ui_screens.h"
#include "web.h"

namespace app {

using miblo::Lang;
using miblo::S;
using miblo::ScreenId;

static WebServerT server(80);
static uint32_t bootMs = 0;
static bool bootCountCleared = false;
static bool bootAnimDone = false;
static ScreenId current = ScreenId::Boot;
static bool firstFrame = true;
static Lang drawnLang = Lang::En;
static uint32_t lastFrameMs = 0;
static miblo::Pager listPager(4, 5000);
static miblo::Pager sessionPager(4, 5000);

static void enter(ScreenId s) {
  if (!firstFrame && s == current && drawnLang == uiLang()) return;
  firstFrame = false;
  current = s;
  drawnLang = uiLang();
  screens::reset();
}

// Chamado pelo OTA durante o upload (o loop fica parado enquanto o arquivo chega).
static void onOtaProgress(uint8_t pct) {
  enter(ScreenId::Updating);
  screens::updating(uiLang(), pct);
}

static const char* modeName(Lang lang) {
  switch (ctx.cfg.mode) {
    case miblo::Mode::Limits: return screens::t(lang, S::ModeLimits);
    case miblo::Mode::Sessions: return screens::t(lang, S::ModeSessions);
    case miblo::Mode::Overview: break;
  }
  return screens::t(lang, S::ModeOverview);
}

static screens::Clock clockNow() {
  screens::Clock c{};
  time_t now = time(nullptr);
  if (now > 1600000000) {
    struct tm lt;
    localtime_r(&now, &lt);
    c.valid = true;
    miblo::formatHHMM(lt.tm_hour, lt.tm_min, c.hhmm, sizeof(c.hhmm));
    c.epoch = (uint32_t)now;
  } else {
    strcpy(c.hhmm, "--:--");
    c.epoch = ctx.hasSnapshot ? ctx.snap.now + (millis() - ctx.lastSnapshotMs) / 1000 : 0;
  }
  return c;
}

static void applyConfig() {
  board::setBacklight(ctx.cfg.brightness);
  ctx.alerts.setTiming(miblo::alertTiming(ctx.cfg));
}

void setup() {
  Serial.begin(115200);
  board::begin();
  screens::bind(board::canvas());

  storage::begin();
  // Reset de fábrica sem botão: 3 ciclos de liga/desliga com menos de 10 s de uptime cada.
  miblo::BootDecision boot = miblo::decideBoot(storage::readBootCount());
  storage::writeBootCount(boot.storeCount);
  if (boot.factoryReset) {
    screens::reset();
    screens::canvas().text(board::kScreen.w / 2, board::kScreen.h / 2, "Factory reset", ui::Font::Title,
                           ui::color::RED, ui::Align::Center, board::kScreen.w);
    storage::factoryReset();
  }

  storage::loadConfig(ctx.cfg);
  storage::loadTokens(ctx.tokens);
  applyConfig();
  char code[5];
  miblo::formatCode(hwRandom(), code);
  ctx.pairing.setCode(code);

  bootMs = millis();
  net::begin(bootMs);
  web::begin(server);
  api::begin(server);
  ota::begin(server, onOtaProgress);
  server.begin();
}

void loop() {
  const uint32_t now = millis();
  server.handleClient();
  net::loop(now);
  mdns::loop(now);

  if (!bootCountCleared && now - bootMs >= miblo::kPowerCycleWindowMs) {
    storage::writeBootCount(0);
    bootCountCleared = true;
  }
  if (ctx.configChanged) {
    ctx.configChanged = false;
    storage::saveConfig(ctx.cfg);
    applyConfig();
    net::applyTimezone();
    mdns::announce();
    firstFrame = true;  // idioma/modo podem ter mudado: redesenha tudo
  }
  if (ctx.factoryResetRequested) {
    delay(300);  // deixa a resposta HTTP sair
    storage::factoryReset();
  }
  if (ctx.rebootRequested && (int32_t)(now - ctx.rebootAtMs) >= 0) ESP.restart();
  if (ctx.showPairCode && now - ctx.pairCodeAtMs >= miblo::kPairCodeScreenMs) ctx.showPairCode = false;

  if (now - lastFrameMs < 100) return;  // ~10 quadros/s
  lastFrameMs = now;

  if (!bootAnimDone && now - bootMs >= 2400) bootAnimDone = true;
  static const miblo::Snapshot kEmpty{};
  const miblo::AlertView& alert = ctx.alerts.update(ctx.hasSnapshot ? ctx.snap : kEmpty, now);

  miblo::ScreenInputs in;
  in.nowMs = now;
  in.bootAnimDone = bootAnimDone;
  in.net = net::state();
  in.updating = ctx.updating;
  in.presenceActive = ctx.presence.active(now);
  in.pairCodeRequested = ctx.showPairCode;
  in.paired = ctx.tokens.count() > 0;
  in.justPaired = ctx.justPaired;
  in.pairedAtMs = ctx.pairedAtMs;
  in.hasSnapshot = ctx.hasSnapshot;
  in.lastSnapshotMs = ctx.lastSnapshotMs;
  in.alert = alert.phase;
  const ScreenId screen = miblo::selectScreen(in);
  enter(screen);

  const Lang lang = uiLang();
  const screens::Clock clk = clockNow();
  switch (screen) {
    case ScreenId::Boot:
      screens::boot(lang, (uint8_t)((now - bootMs) / 400));
      break;
    case ScreenId::Setup:
    case ScreenId::WrongPassword:
      screens::setup(lang, ctx.ident.apSsid, screen == ScreenId::WrongPassword);
      break;
    case ScreenId::Welcome:
      screens::welcome(lang, ctx.pairing.code(), net::ip().c_str());
      break;
    case ScreenId::Paired:
      screens::paired(lang, ctx.pairedHost, modeName(lang), ctx.ident.id);
      break;
    case ScreenId::PairCode:
      screens::code(lang, S::PairingCode, ctx.pairing.code(),
                    (miblo::kPairCodeScreenMs - (now - ctx.pairCodeAtMs)) / 1000);
      break;
    case ScreenId::PresenceCode:
      screens::code(lang,
                    ctx.presence.purpose() == miblo::PresenceGate::Purpose::Update ? S::CodeUpdate : S::CodeReset,
                    ctx.presence.code(), ctx.presence.remainingMs(now) / 1000);
      break;
    case ScreenId::Updating:
      screens::updating(lang, ctx.updatePct);
      break;
    case ScreenId::Disconnected: {
      time_t t = time(nullptr);
      struct tm lt;
      localtime_r(&t, &lt);
      screens::disconnected(lang, clk.valid, lt.tm_hour, lt.tm_min, lt.tm_wday, lt.tm_mday, net::ip().c_str(),
                            ctx.ident.id, ctx.pairing.code());
      break;
    }
    case ScreenId::AlertFlash: {
      int idx = miblo::findSession(ctx.snap, alert.sid);
      screens::flash(lang, alert.kind, idx >= 0 ? ctx.snap.sessions[idx].name : "", now - alert.phaseStartMs);
      break;
    }
    case ScreenId::AlertHero:
      screens::hero(lang, ctx.snap, miblo::findSession(ctx.snap, alert.sid), alert.kind, ctx.cfg.discreet, clk,
                    ctx.runs);
      break;
    case ScreenId::Main:
      switch (ctx.cfg.mode) {
        case miblo::Mode::Overview:
          screens::overview(lang, ctx.snap, listPager, now, clk, ctx.cfg.discreet);
          break;
        case miblo::Mode::Limits:
          screens::limits(lang, ctx.snap, clk);
          break;
        case miblo::Mode::Sessions:
          screens::sessions(lang, ctx.snap, sessionPager, now, clk, ctx.cfg.discreet);
          break;
      }
      break;
  }
}

}  // namespace app
```

- [ ] **Step 4: `firmware/src/main.cpp` final**

```cpp
#include <Arduino.h>

#include "app.h"

void setup() { app::setup(); }

void loop() { app::loop(); }
```

- [ ] **Step 5: Testes + build do aparelho**

Run: `cd firmware && .venv/bin/pio test -e native && .venv/bin/pio run -e geekmagic_ultra`
Expected: `69 test cases: 69 succeeded`; `[SUCCESS]` com `RAM: … (used ~47 KB from 81920 bytes)` e `Flash: … (used ~694 KB from 1044464 bytes)`; nenhum `warning:` de `src/`, `boards/` ou `lib/miblo_*`.

- [ ] **Step 6: Conferir o contrato contra o gadget falso (leitura)**

Comparar, rota a rota, `firmware/src/api.cpp` com `plugin/test/fakes/fake-device.js`: mesmos caminhos e métodos, `code` como string, resposta `{token}`, 403 para código errado, 429 no bloqueio, 401 sem Bearer, `{ok:true}` nos POSTs autenticados. Divergência → corrigir o firmware (o gadget falso é a referência executável).

- [ ] **Step 7: Commit**

```bash
git add firmware/src/api.h firmware/src/api.cpp firmware/src/platform/ota.h firmware/src/platform/ota.cpp firmware/src/app.h firmware/src/app.cpp firmware/src/main.cpp
git commit -m "feat(firmware): bridge HTTP API, presence-code OTA and app orchestration"
```

---

### Task 14: Build de release e checklist manual no aparelho

**Files:**
- Create: `firmware/scripts/build.sh`

**Interfaces:**
- Consumes: env `native`, env `<placa>`, `MIBLO_FW_VERSION`.
- Produces: `firmware/scripts/build.sh [placa]` (padrão `geekmagic_ultra`) → roda os testes nativos, compila e copia para `firmware/dist/miblo-<placa>-<versão>.bin`, imprimindo tamanho e SHA-256.

- [ ] **Step 1: Script** — `firmware/scripts/build.sh`

```bash
#!/usr/bin/env bash
# Gera o binário de release: firmware/dist/miblo-<placa>-<versão>.bin
# Uso: firmware/scripts/build.sh [placa]      (padrão: geekmagic_ultra)
set -euo pipefail

BOARD="${1:-geekmagic_ultra}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PIO="$ROOT/.venv/bin/pio"
if [ ! -x "$PIO" ]; then
  echo "PlatformIO não encontrado em $PIO" >&2
  echo "Instale: python3 -m venv firmware/.venv && firmware/.venv/bin/pip install platformio" >&2
  exit 1
fi
VERSION="$(sed -n 's/^#define MIBLO_FW_VERSION "\(.*\)"$/\1/p' "$ROOT/include/miblo_version.h")"
if [ -z "$VERSION" ]; then
  echo "MIBLO_FW_VERSION não encontrado em include/miblo_version.h" >&2
  exit 1
fi

cd "$ROOT"
"$PIO" test -e native
"$PIO" run -e "$BOARD"

mkdir -p dist
OUT="dist/miblo-$BOARD-$VERSION.bin"
cp ".pio/build/$BOARD/firmware.bin" "$OUT"
echo "OK: firmware/$OUT ($(wc -c < "$OUT" | tr -d ' ') bytes)"
if command -v shasum >/dev/null 2>&1; then shasum -a 256 "$OUT"; else sha256sum "$OUT"; fi
```

- [ ] **Step 2: Gerar o binário**

Run: `chmod +x firmware/scripts/build.sh && firmware/scripts/build.sh`
Expected: `69 test cases: 69 succeeded`, `[SUCCESS]` e `OK: firmware/dist/miblo-geekmagic_ultra-0.1.0.bin (~698000 bytes)` seguido do SHA-256. `git status` não mostra `firmware/dist/` (ignorado na Task 1).

- [ ] **Step 3: Commit**

```bash
git add firmware/scripts/build.sh
git commit -m "build(firmware): release script producing dist/miblo-<board>-<version>.bin"
```

- [ ] **Step 4: 🧑 Checklist manual no aparelho** (o humano executa; registrar o resultado de cada item no PR/commit de release)

Preparação: computador na mesma rede; Claude Code com o plugin do Plano 1; anotar o IP atual do gadget (tela do firmware de referência).

1. **Primeiro envio (firmware de referência):** abrir `http://<ip>/update` no navegador, escolher `firmware/dist/miblo-geekmagic_ultra-0.1.0.bin`, enviar. Expected: o aparelho reinicia e mostra o mascote piscando.
2. **Wi-Fi salvo:** sem nenhuma interação, sai do mascote para "✓ Wi-Fi conectado" com `/plugin install miblo@miblo`, código de 4 dígitos e o IP (as credenciais do SDK foram reaproveitadas; nenhuma rede `Miblo-Setup-…` aparece).
3. **Página:** `http://miblo-xxxx.local` (ou pelo IP) abre a configuração no idioma do navegador; o fuso é gravado sozinho; trocar o idioma para `pt-BR` muda a tela.
4. **Pareamento:** `/miblo pair` → encontra `Miblo-XXXX`, pede o código da tela → tela "Pareado com <host>" por 5 s, depois "Desconectado"/relógio até o primeiro snapshot. `GET http://<ip>/api/info` mostra `paired: true`, `board: "geekmagic_ultra"`, `screen: {w:240,h:240}`, `caps: []`. Cinco códigos errados seguidos → 429 por 60 s.
5. **Telas:** com sessões reais — Trabalhando (● N RODANDO, 5h/semana grandes, lista), Precisa de você (faixa âmbar; peça algo que exija permissão), Tudo pronto ("<sessão> terminou há …", custo do dia). `/miblo mode limits` → arco L1; `/miblo mode sessions` → lista S1 com troca de página a cada 5 s quando há mais de 4 sessões; `/miblo mode overview` volta.
6. **Alertas:** permissão → flash âmbar ~1,5 s, herói ~10 s com comando e "esperando há", resumo com faixa; sem responder por 2 min → repete; aprovar → faixa some. `Stop` → flash azul + herói ~5 s ("levou …", ctx/tokens) → "✓ terminou" na lista. Modo discreto na página → comandos/arquivos somem.
7. **Desconectado:** fechar o Claude Code/bridge → após 30 s, tela "Desconectado" com relógio certo (NTP + fuso) e código de pareamento no rodapé.
8. **Senha errada:** reset de fábrica (item 10) e, no portal, digitar senha errada → tela "Senha incorreta" com o QR; corrigir → conecta.
9. **Recuperação:** desligar o roteador → após ~2 min aparece a rede `Miblo-Setup-XXXX` + QR; religar o roteador → o gadget volta sozinho à rede salva e a rede de setup some.
10. **Reset por liga/desliga:** 3 ciclos de tomada com < 10 s ligado cada → "Factory reset" → volta ao QR de setup; o celular que lê o QR entra na rede e o portal abre sozinho no idioma do celular.
11. **OTA próprio:** abrir `http://<ip>/update` → a tela mostra o código de 4 dígitos; enviar o mesmo `.bin` com código errado → "Código incorreto" e nada muda; com o código certo → barra de progresso na tela, "OK", reinício, pareamento mantido.
12. **OTA na rede de setup:** com o gadget no AP `Miblo-Setup-XXXX`, `http://192.168.4.1/update` também funciona (mesmo fluxo do código).

---

## Self-review

**Cobertura do spec (requisitos de firmware → task):**

| Requisito | Task |
|---|---|
| §3.5 Rede: captive portal, `miblo-xxxx.local`, `_miblo._tcp` com ID | 8, 12 |
| §3.5 API HTTP (`/api/info`, `/api/pair` 403/429, Bearer em state/config/reset, 4 tokens) | 6, 13 |
| §3.5 mDNS TXT id/name/fw, resposta unicast a QU/porta ≠ 5353 | 8, 12 |
| §3.5 Página de configuração (modo, brilho, alertas, durações, discreto, fuso, nome, reset, update) | 7, 12, 13 |
| §3.5 Renderizador por regiões, sem framebuffer | 4 (`RegionCache`), 9, 10 |
| §3.5 AlertQueue (dedupe por id, âmbar antes de azul) | 5 |
| §3.5 Watchdog 30 s → "desconectado" com relógio NTP | 7 (`selectScreen`), 9, 13 |
| §3.5 Persistência (Wi-Fi no SDK, token, modo, config) | 12 |
| §3.5 i18n 9 idiomas + Accept-Language + atividade localizada | 3, 12 |
| §3.5 Fontes latim/cirílico/CJK, glyph ausente → □ (agora no binário, decisão do controlador) | 11 |
| §4.1 Visão geral adaptativa (3 situações, lista rotativa 5 s) | 4, 10 |
| §4.2 Alertas: flash 1,5 s → herói 10 s/5 s → resumo; lembrete 2 min; prioridade; fila; configurável/desligável | 5, 7, 10 |
| §4.3 L1 arco + semana, cores 80/95 % | 10 |
| §4.4 S1 lista, ordem do bridge, 4 por página, 5 s | 10 |
| §4.5 Boot com mascote, Setup (QR + rede), Wi-Fi conectado + comando + código + IP, Pareado, Desconectado, Atualizando, Senha incorreta | 9, 11, 13 |
| §5.3 Snapshot ≤ 3 KB, campos desconhecidos ignorados, `usage: null`, `more`, discreto oculta `det`, `today.usd` | 2, 10 |
| §5.4 Código de 4 dígitos, token 128 bits, bloqueio 60 s após 5 erros, até 4 tokens | 6, 13 |
| §6 Setup < 3 min, senha errada, roteador fora (AP após 2 min + retentativa), reset por 3 ciclos/página/`/miblo reset` | 7, 12, 13, 14 |
| §7 JSON inválido → 400 e mantém a tela; OTA com falha mantém a imagem | 2, 13 |
| §8 Testes nativos (snapshot, herói, AlertQueue, paginação, contador de reset) + fixtures compartilhados + checklist manual | 1–10, 14 |
| Revisão do Plano 1: `today.usd`, 429, 4 tokens, código de presença obrigatório no OTA | 2, 6, 13 |
| Suporte futuro a outras placas: `miblo_core`/`miblo_ui`/`boards/`/`src/platform/`, env por placa, `board/screen/caps` no `/api/info`, README | 1, 9, 11, 12, 13, 14 |

**Placeholders:** nenhum "TBD"/"similar à Task N"; todo código aparece completo. Os únicos arquivos não escritos à mão no plano são os da biblioteca vendorizada, gerados de forma determinística pelo script da Task 11 (commit fixo).

**Consistência de nomes:** `ScreenSpec`, `Canvas`, `Font`, `Align` (namespace `ui`); telas em `screens::`; lógica em `miblo::`; `board::kName/kScreen/kCapCount/cap()` usados por `api.cpp`; `WebServerT`/`hwRandom()` de `platform.h` usados por `web.cpp`, `api.cpp`, `ota.cpp`, `app.cpp`; `ScreenId` e `kPairCodeScreenMs` de `miblo_policy.h` usados por `app.cpp`; `RunTracker::stats(sid, durationSec)` (sem tokens) usado por `ui_main.cpp`.

**Fora deste plano (deliberado):** script de preview de telas em PNG (§8) — o `FakeCanvas` da Task 9 é o ponto de partida; barra "Semana Opus" do mockup L1 (o protocolo não tem esse dado); arte final do mascote (Fase B).
