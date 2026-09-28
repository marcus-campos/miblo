# Plano 0 — Teste de hardware (GeekMagic Ultra) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. **Várias etapas exigem o humano com o aparelho em mãos — marcadas com 🧑.**

**Goal:** Provar que conseguimos gravar firmware próprio no GeekMagic Ultra por OTA, sem abrir o aparelho, e registrar os parâmetros reais de hardware (pinos, ordem de cores, inversão, luz de fundo, flash) que o Plano 2 (firmware) vai usar.

**Architecture:** Um firmware descartável (`firmware/spike/`) em PlatformIO/Arduino que desenha um padrão de teste na ST7789 e **já inclui uma página de update OTA** (`/update`) e um modo AP de fallback, para que nenhuma gravação futura dependa de abrir o aparelho. Os resultados vão para `docs/hardware/geekmagic-ultra.md`.

**Tech Stack:** PlatformIO, framework Arduino ESP8266 (`espressif8266`), TFT_eSPI 2.5.x, `ESP8266WebServer` + `ESP8266HTTPUpdateServer`.

**Spec:** `docs/superpowers/specs/2026-09-28-claude-gadget-design.md` (§9 etapa 0, §10 riscos 4 e 6)

## Global Constraints

- **Nunca** gravar um firmware sem página de update OTA funcional + fallback AP. Um firmware sem isso transforma o próximo flash em solda na serial.
- Antes de gravar qualquer coisa, ter o **firmware oficial** do modelo exato salvo localmente (para restauração).
- Se a página de update original **recusar** o `.bin`, PARAR e relatar. Não tentar outros métodos (serial, exploits) sem nova aprovação.
- Pinos candidatos (da comunidade ESPHome para o SmallTV Ultra — **não confirmados**): SCLK=GPIO14, MOSI=GPIO13, DC=GPIO0, RST=GPIO2, CS=nenhum, SPI mode 3, luz de fundo GPIO5 ativa em nível baixo.
- `firmware/spike/include/secrets.h` nunca é commitado.

---

### Task 1: Ferramentas e firmware oficial de restauração 🧑

**Files:**
- Create: `docs/hardware/geekmagic-ultra.md`

- [ ] **Step 1: Instalar PlatformIO**

```bash
brew install platformio
pio --version
```
Expected: `PlatformIO Core, version 6.x`

- [ ] **Step 2: 🧑 Anotar a identidade do aparelho**

Ligar o gadget, deixá-lo na rede WiFi pela configuração original e abrir `http://<ip-do-gadget>/` no navegador. Anotar: modelo exibido, versão do firmware, e onde fica a página de atualização de firmware (URL e o que ela pede — tipo de arquivo, prefixo de nome, etc.). Tirar prints.

- [ ] **Step 3: 🧑 Baixar o firmware oficial correspondente**

Procurar a versão exata anotada no Step 2 nos canais oficiais da GeekMagic (página de suporte/GitHub da GeekMagic para o "SmallTV Ultra"/"Weather Clock Ultra"). Salvar o `.bin` em `~/miblo-backup/` (fora do repositório — é binário de terceiros). Se não houver `.bin` oficial para o modelo, PARAR e relatar: sem arquivo de restauração, não seguimos.

- [ ] **Step 4: Registrar tudo**

Criar `docs/hardware/geekmagic-ultra.md`:

```markdown
# GeekMagic Ultra — parâmetros de hardware

## Identidade (firmware original)
- Modelo exibido: <preencher>
- Versão do firmware original: <preencher>
- Página de update original: <URL> — aceita: <tipo/prefixo>
- Firmware oficial de restauração: `~/miblo-backup/<arquivo>.bin` (fonte: <URL>)

## Medido com o firmware de teste (Plano 0)
(preenchido na Task 4)
```

Os `<preencher>` deste arquivo são dados a coletar do aparelho, não lacunas do plano.

- [ ] **Step 5: Commit**

```bash
git add docs/hardware/geekmagic-ultra.md
git commit -m "docs(hardware): GeekMagic Ultra identity and restore firmware"
```

---

### Task 2: Firmware de teste com OTA e fallback AP

**Files:**
- Create: `firmware/spike/platformio.ini`
- Create: `firmware/spike/src/main.cpp`
- Create: `firmware/spike/include/secrets.example.h`
- Modify: `.gitignore`

- [ ] **Step 1: Ignorar segredos e artefatos**

Acrescentar ao `.gitignore`:

```
firmware/**/include/secrets.h
```

- [ ] **Step 2: `platformio.ini`**

```ini
[env:spike]
platform = espressif8266
board = esp12e
framework = arduino
board_build.flash_mode = dio
board_build.ldscript = eagle.flash.4m1m.ld
monitor_speed = 115200
lib_deps =
  bodmer/TFT_eSPI@^2.5.43
build_flags =
  -D USER_SETUP_LOADED=1
  -D ST7789_DRIVER=1
  -D TFT_WIDTH=240
  -D TFT_HEIGHT=240
  -D TFT_MOSI=13
  -D TFT_SCLK=14
  -D TFT_CS=-1
  -D TFT_DC=0
  -D TFT_RST=2
  -D TFT_SPI_MODE=SPI_MODE3
  -D LOAD_GLCD=1
  -D LOAD_FONT2=1
  -D LOAD_FONT4=1
  -D SPI_FREQUENCY=40000000
  -D BL_PIN=5
```

- [ ] **Step 3: `include/secrets.example.h`**

```cpp
#pragma once
#define WIFI_SSID "sua-rede"
#define WIFI_PASS "sua-senha"
```

- [ ] **Step 4: `src/main.cpp`**

```cpp
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <TFT_eSPI.h>
#include "secrets.h"

TFT_eSPI tft;
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer updater;

bool blActiveLow = true;
bool inverted = false;
uint8_t rotation = 0;
int blLevel = 1023;  // 0..1023, "brilho lógico"

void applyBacklight() {
  int raw = blActiveLow ? (1023 - blLevel) : blLevel;
  analogWrite(BL_PIN, raw);
}

void drawPattern(const String& net, const String& ip) {
  tft.setRotation(rotation);
  tft.invertDisplay(inverted);
  tft.fillScreen(TFT_BLACK);
  // Barras nomeadas: se o texto não bater com a cor vista, a ordem RGB/BGR está trocada.
  tft.fillRect(0, 0, 80, 60, TFT_RED);
  tft.fillRect(80, 0, 80, 60, TFT_GREEN);
  tft.fillRect(160, 0, 80, 60, TFT_BLUE);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("RED", 22, 22, 2);
  tft.drawString("GREEN", 100, 22, 2);
  tft.drawString("BLUE", 182, 22, 2);
  // Canto superior esquerdo marcado: confirma a rotação.
  tft.fillTriangle(0, 60, 20, 60, 0, 80, TFT_YELLOW);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Miblo spike", 10, 90, 4);
  tft.drawString("net: " + net, 10, 130, 2);
  tft.drawString("ip:  " + ip, 10, 150, 2);
  tft.drawString("flash: " + String(ESP.getFlashChipRealSize() / 1024) + " KB", 10, 170, 2);
  tft.drawString("heap:  " + String(ESP.getFreeHeap()) + " B", 10, 190, 2);
  tft.drawString("inv=" + String(inverted) + " rot=" + String(rotation) +
                 " blLow=" + String(blActiveLow), 10, 210, 2);
}

String currentNet, currentIp;
void redraw() { drawPattern(currentNet, currentIp); }

void handleRoot() {
  String j = "{";
  j += "\"chip_id\":\"" + String(ESP.getChipId(), HEX) + "\",";
  j += "\"flash_real_kb\":" + String(ESP.getFlashChipRealSize() / 1024) + ",";
  j += "\"flash_ide_kb\":" + String(ESP.getFlashChipSize() / 1024) + ",";
  j += "\"sketch_kb\":" + String(ESP.getSketchSize() / 1024) + ",";
  j += "\"free_sketch_kb\":" + String(ESP.getFreeSketchSpace() / 1024) + ",";
  j += "\"free_heap\":" + String(ESP.getFreeHeap()) + ",";
  j += "\"inverted\":" + String(inverted) + ",";
  j += "\"rotation\":" + String(rotation) + ",";
  j += "\"bl_active_low\":" + String(blActiveLow) + ",";
  j += "\"bl_level\":" + String(blLevel);
  j += "}";
  server.send(200, "application/json", j);
}

void handleSet() {
  if (server.hasArg("invert")) inverted = server.arg("invert") == "1";
  if (server.hasArg("rot")) rotation = server.arg("rot").toInt() % 4;
  if (server.hasArg("bllow")) blActiveLow = server.arg("bllow") == "1";
  if (server.hasArg("bl")) blLevel = constrain(server.arg("bl").toInt(), 0, 1023);
  applyBacklight();
  redraw();
  handleRoot();
}

void setup() {
  Serial.begin(115200);
  pinMode(BL_PIN, OUTPUT);
  analogWriteRange(1023);
  applyBacklight();
  tft.init();
  drawPattern("connecting...", "-");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) delay(200);

  if (WiFi.status() == WL_CONNECTED) {
    currentNet = WIFI_SSID;
    currentIp = WiFi.localIP().toString();
  } else {
    // Fallback: nunca ficar sem caminho de OTA.
    WiFi.mode(WIFI_AP);
    WiFi.softAP("Miblo-Spike", "miblo1234");
    currentNet = "AP Miblo-Spike / miblo1234";
    currentIp = WiFi.softAPIP().toString();
  }

  updater.setup(&server, "/update");
  server.on("/", handleRoot);
  server.on("/set", handleSet);
  server.begin();
  redraw();
}

void loop() {
  server.handleClient();
}
```

- [ ] **Step 5: Compilar**

```bash
cp firmware/spike/include/secrets.example.h firmware/spike/include/secrets.h
# editar secrets.h com a rede WiFi real
cd firmware/spike && pio run -e spike
```
Expected: `SUCCESS`, e o arquivo `firmware/spike/.pio/build/spike/firmware.bin` existe. Anotar o tamanho do `.bin`.

- [ ] **Step 6: Commit**

```bash
git add .gitignore firmware/spike/platformio.ini firmware/spike/src/main.cpp firmware/spike/include/secrets.example.h
git commit -m "feat(spike): test firmware with OTA update page and AP fallback"
```

---

### Task 3: Gravar por OTA pela página original 🧑

**Files:** nenhum (operação no aparelho)

- [ ] **Step 1: 🧑 Conferir o pré-requisito**

Confirmar que `~/miblo-backup/<oficial>.bin` existe (Task 1). Sem ele, não prosseguir.

- [ ] **Step 2: 🧑 Enviar o `firmware.bin` pela página de update original**

Usar a URL anotada na Task 1, Step 2. Se a página exigir prefixo no nome do arquivo, renomear uma cópia (ex.: `cp firmware.bin <PREFIXO>_miblo_spike.bin`). Aguardar o aparelho reiniciar (~30–60s).

Resultado esperado: a tela mostra as barras RED/GREEN/BLUE e "Miblo spike".
- Se a página **recusar** o arquivo → PARAR, registrar a mensagem exata em `docs/hardware/geekmagic-ultra.md` e relatar.
- Se a tela ficar **preta** mas o aparelho aparecer na rede (ou a rede `Miblo-Spike` surgir) → a gravação funcionou e o problema é de pinos/luz de fundo: seguir para a Task 4 usando `/set`.
- Se o aparelho **não aparecer** na rede nem criar `Miblo-Spike` em 2 minutos → PARAR e relatar.

- [ ] **Step 3: Verificar a página de update própria**

```bash
curl -s http://<ip>/ | python3 -m json.tool
```
Expected: JSON com `chip_id`, `flash_real_kb`, `free_heap`. Abrir `http://<ip>/update` no navegador e confirmar que aparece o formulário de upload.

- [ ] **Step 4: Provar o ciclo de OTA próprio**

Mudar o texto `"Miblo spike"` para `"Miblo spike v2"` em `main.cpp`, compilar (`pio run -e spike`) e enviar pelo `http://<ip>/update`. Expected: a tela mostra "Miblo spike v2". Reverter a mudança de texto sem commitar.

---

### Task 4: Medir e registrar os parâmetros reais 🧑

**Files:**
- Modify: `docs/hardware/geekmagic-ultra.md`

- [ ] **Step 1: 🧑 Ordem de cores**

Olhando a tela: a barra rotulada RED está vermelha? Se RED aparece azul e BLUE aparece vermelha → a ordem é BGR (no Plano 2 usar `-D TFT_RGB_ORDER=TFT_BGR`).

- [ ] **Step 2: 🧑 Inversão**

Se o fundo parece branco/cores parecem negativas: `curl "http://<ip>/set?invert=1"` e reavaliar. Anotar o valor que deixa o fundo preto.

- [ ] **Step 3: 🧑 Rotação**

O triângulo amarelo deve ficar logo abaixo da barra vermelha, no lado esquerdo, com o texto legível na orientação natural do cubo. Testar `curl "http://<ip>/set?rot=1"` … `rot=3` até acertar. Anotar.

- [ ] **Step 4: 🧑 Luz de fundo**

```bash
curl "http://<ip>/set?bl=1023"   # deve ficar no máximo
curl "http://<ip>/set?bl=100"    # deve ficar fraco, mas visível
curl "http://<ip>/set?bl=0"      # deve apagar
```
Se o comportamento for o oposto, repetir com `bllow=0` (ex.: `curl "http://<ip>/set?bllow=0&bl=1023"`). Anotar a polaridade e se o PWM escurece suavemente ou só liga/desliga.

- [ ] **Step 5: Registrar**

Completar a seção "Medido com o firmware de teste" em `docs/hardware/geekmagic-ultra.md`:

```markdown
## Medido com o firmware de teste (Plano 0)
- OTA pela página original: aceitou / recusou (<detalhes>)
- OTA pela página própria `/update`: funcionou
- Pinos: SCLK=14 MOSI=13 DC=0 RST=2 CS=nenhum SPI_MODE3 — confirmados? <sim/não + ajustes>
- Ordem de cores: RGB | BGR
- invertDisplay: true | false
- Rotação: <0-3>
- Luz de fundo: GPIO5, ativa em <baixo|alto>, PWM <suave|liga-desliga>
- Flash real: <KB>  (JSON `flash_real_kb`)
- Heap livre com Wi-Fi + servidor web + TFT: <bytes>
- Tamanho do firmware de teste: <KB>
- Conclusão para o Plano 2: <orçamento de flash para OTA + LittleFS com fontes>
```

- [ ] **Step 6: Commit**

```bash
git add docs/hardware/geekmagic-ultra.md
git commit -m "docs(hardware): measured GeekMagic Ultra parameters from spike"
```

---

### Task 5: Provar a restauração 🧑

**Files:**
- Modify: `docs/hardware/geekmagic-ultra.md`

- [ ] **Step 1: 🧑 Restaurar o firmware oficial**

Enviar `~/miblo-backup/<oficial>.bin` pela página própria `http://<ip>/update`. Expected: o aparelho volta à interface original da GeekMagic.

- [ ] **Step 2: 🧑 Voltar ao firmware de teste**

Repetir a Task 3, Step 2 (gravar o spike pela página original). Isso prova o ciclo completo ida-e-volta que vamos oferecer aos clientes.

- [ ] **Step 3: Registrar e commitar**

Acrescentar em `docs/hardware/geekmagic-ultra.md`:

```markdown
## Restauração
- Oficial → spike → oficial → spike: <ok | falhou em ...>
```

```bash
git add docs/hardware/geekmagic-ultra.md
git commit -m "docs(hardware): verified restore round-trip"
```

**Critério de conclusão do Plano 0:** `docs/hardware/geekmagic-ultra.md` completo, OTA confirmado nos dois sentidos. Só então o Plano 2 (firmware) é escrito.
