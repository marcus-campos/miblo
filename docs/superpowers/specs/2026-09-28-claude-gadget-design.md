# Miblo — gadget de mesa para status do Claude Code (Fase A)

- **Data:** 2026-09-28
- **Status:** design aprovado, aguardando revisão do spec
- **Nome:** **Miblo** (pendente de busca de marca WIPO/INPI classe 9 — ver §10). Os mockups ainda usam o nome provisório "Pulse"
- **Mockups:** `docs/superpowers/specs/mockups/*.html` (abrir no navegador)

## 1. Objetivo

Transformar o relógio de mesa **GeekMagic "Ultra"** (ESP8266 + LCD IPS ST7789 240×240, sem botões nem touch) num display que mostra, em tempo real, o estado do **Claude Code** no computador do usuário:

- quais sessões estão rodando e o que cada uma está fazendo;
- quando uma sessão **precisa do humano** (pedido de permissão ou pergunta);
- quando uma sessão **terminou**;
- limites de uso (janela de 5h e semanal) e consumo de tokens.

O objetivo final é **vender o produto no Mercado Livre**. Por isso, a experiência do usuário final (setup, configuração, recuperação de falhas) é requisito de primeira classe, e não acabamento.

### Escopo da Fase A (este spec)

Firmware do gadget + plugin do Claude Code (bridge), com a experiência de produto completa: setup, descoberta, pareamento, modos, alertas e OTA.

### Fora de escopo (Fase B, comercial — spec separado)

Nome e marca definitivos, gravação de firmware em lote, manual e embalagem, verificação de homologação Anatel, relay na nuvem, app de bandeja/instalador nativo, suporte a Claude Desktop/claude.ai, monitoramento de várias máquinas.

## 2. Decisões tomadas

| Tema | Decisão |
|---|---|
| O que monitorar | Somente Claude Code (CLI/IDE) na máquina local |
| Sistemas operacionais | macOS, Windows (incl. WSL) e Linux |
| Vários gadgets | Mesmo firmware; cada aparelho escolhe seu **modo**: Visão geral (padrão), Limites ou Sessões |
| Transporte | Rede local; o **computador envia** para o gadget (push HTTP), com descoberta por mDNS |
| Lado do computador | Plugin do Claude Code em Node.js ≥ 18 sem dependências externas (requisito a comunicar no anúncio: o Claude Code nativo não traz Node) |
| Tela Visão geral | Adaptativa (mistura das propostas A + B) — ver §4.1 |
| Modo Limites | L1 — medidor em arco grande |
| Modo Sessões | S1 — lista detalhada, rolagem automática a cada 5s |
| Alertas | Flash + herói temporário, em todos os modos (desligável por aparelho) |
| Fonte de limites/métricas | JSON oficial da status line do Claude Code, encadeado com consentimento (§3.2) — sem endpoints não documentados |
| Mercado | Internacional: gadget e página de configuração em 9 idiomas — `en` (padrão), `pt-BR`, `pt-PT`, `es`, `fr`, `it`, `de`, `ru`, `zh` (chinês simplificado) |
| Hardware | Fase A implementa só o GeekMagic Ultra, mas o firmware é organizado para outros hardwares: núcleo independente de placa (`miblo_core`), layouts calculados a partir de `ScreenSpec {w,h}` (`miblo_ui`), pasta por placa (`boards/<placa>/`) e serviços de plataforma com aliases ESP8266/ESP32. `/api/info` informa `board`, `screen` e `caps`; o bridge e o protocolo já são independentes de hardware |
| Identidade | Nome **Miblo**; mascote na tela de boot/loading (arte final do mascote na Fase B; Fase A usa um placeholder de poucos quadros) |

## 3. Arquitetura

Mockup: `mockups/architecture.html`.

```
Claude Code ──hooks (async)──▶ hook.js ──────────┐
            ──statusLine─────▶ statusline-tap.js ─┼─HTTP 127.0.0.1──▶ bridge (Node) ──HTTP LAN──▶ gadget(s)
                                   │              │                                            │
                  status line original do usuário (saída idêntica)          mDNS _miblo._tcp ──┘
```

### 3.1 `hook.js` (plugin)

- Registrado com `"async": true` para os hooks: `SessionStart`, `UserPromptSubmit`, `PreToolUse`, `PermissionRequest`, `PostToolUse`, `Notification` (matcher `elicitation_dialog`), `Stop`, `SessionEnd`.
- Lê o JSON do hook no stdin, acrescenta o PID do processo pai (Claude Code) e repassa via `POST http://127.0.0.1:<porta>/event`.
- Se o bridge não responder, sobe o bridge como processo destacado (detached) e reenvia uma vez.
- **Invariante:** nunca atrasa nem quebra o Claude Code. Roda em background (`async`), timeout interno de 1s, sempre termina com código 0, sem saída no stdout.

### 3.2 `statusline-tap.js` (métricas oficiais)

Plugins não podem definir `statusLine` (só `agent`/`subagentStatusLine`). Por isso, com **consentimento do usuário**, `/miblo:pair` substitui o `statusLine` de `~/.claude/settings.json` por `statusline-tap.js`, salvando o comando original em `${CLAUDE_PLUGIN_DATA}/statusline-original.json`.

- Lê o JSON da status line no stdin, repassa uma cópia (fire-and-forget, timeout 200ms) para `POST /statusline` no bridge.
- Executa o comando original do usuário com o mesmo stdin e devolve **exatamente** a sua saída (stdout e código). Sem comando original, não imprime nada.
- Fornece por sessão (`session_id`): `rate_limits.five_hour` / `seven_day` (`used_percentage`, `resets_at`), `context_window.used_percentage`, `context_window.total_input_tokens` / `total_output_tokens`, `model.display_name`, `cost.total_cost_usd`.
- `/miblo:unlink-statusline` restaura o comando original. Se o usuário recusar o encadeamento, o gadget funciona só com os estados das sessões (sem limites/tokens/ctx).

### 3.3 Bridge (serviço local)

Processo Node único por usuário, escutando só em `127.0.0.1`. A porta e o estado de pareamento ficam em `${CLAUDE_PLUGIN_DATA}`. Encerra sozinho após 30 min sem nenhuma sessão ativa.

Unidades (cada uma testável isoladamente):

- **SessionTracker** — máquina de estados por sessão (§5.1); produz a lista de sessões e a fila de alertas.
- **MetricsStore** — guarda a última leitura da status line por sessão e os limites globais (a leitura mais recente de qualquer sessão); calcula os tokens do dia a partir dos deltas de `total_*_tokens`.
- **DeviceManager** — descoberta mDNS, pareamento, envio do snapshot, reenvio com espera crescente e OTA.
- **SnapshotBuilder** — monta o JSON do protocolo (§5.3) a partir das outras unidades, aplicando os limites de tamanho.

### 3.4 Comandos `/miblo:*`

Um comando do plugin por ação (Claude Code namespacea comandos de plugin por nome de arquivo):

- `/miblo:pair [ip]` — descobre gadgets (ou usa o IP informado), pede o código de 4 dígitos exibido na tela e salva o token. Pergunta se pode encadear a status line (§3.2). Roda automaticamente na primeira instalação.
- `/miblo:status` — sessões, limites e gadgets pareados (online/offline).
- `/miblo:mode <overview|limits|sessions> [gadget]`
- `/miblo:update` — envia o firmware mais recente para os gadgets pareados.
- `/miblo:reset [gadget]` — reset de fábrica remoto.
- `/miblo:unlink-statusline` — desfaz o encadeamento da status line.

### 3.5 Firmware (ESP8266, PlatformIO/Arduino)

- **Rede:** setup por captive portal; mDNS `miblo-xxxx.local`; anuncia `_miblo._tcp` com o ID do aparelho.
- **API HTTP:** `GET /api/info → {id, name, fw, proto, paired}`; `POST /api/pair {code, host} → {token}` (403 com código errado); `POST /api/state`, `POST /api/config {mode?, …}`, `POST /api/reset` e `POST /update` (OTA) exigem `Authorization: Bearer <token>` (401 sem token válido). O gadget falso em `plugin/test/fakes/fake-device.js` é a referência executável deste contrato.
- **mDNS:** serviço `_miblo._tcp` com TXT `id=<id>`, `name=<nome>`, `fw=<versão>`; responde a consultas com bit QU/porta de origem ≠ 5353 (unicast).
- **Página de configuração** (`http://miblo-xxxx.local`): modo, brilho, alertas liga/desliga, durações (herói de permissão, herói de término, intervalo do lembrete), modo discreto, fuso horário, nome do aparelho, reset de fábrica, update de firmware.
- **Renderizador:** desenha por regiões/sprites parciais (RAM livre ~80 KB < framebuffer de 115 KB); a tela só é redesenhada nas regiões que mudaram.
- **AlertQueue:** fila de alertas, deduplicada pelo `id`, com âmbar antes de azul.
- **Watchdog de conexão:** 30s sem snapshot → tela "desconectado" com relógio (via NTP).
- **Persistência:** WiFi, token, modo e configurações em flash (LittleFS/EEPROM).
- **i18n:** todas as strings da tela e da página vêm de tabelas por idioma: `en` (padrão), `pt-BR`, `pt-PT`, `es`, `fr`, `it`, `de`, `ru`, `zh`. O idioma é detectado pelo `Accept-Language` do celular no captive portal (`pt` sem região → `pt-BR`; `zh-*` → `zh`) e pode ser trocado na página de configuração. Os textos de atividade são localizados no gadget: o bridge envia a ferramenta e o detalhe separados (§5.3).
- **Fontes:** a tela precisa de latim estendido (acentos de PT/ES/FR/IT/DE), cirílico (RU) e CJK (ZH). As fontes ficam no LittleFS, não no binário: um subconjunto gerado no build com todos os glyphs das strings fixas de todos os idiomas, mais uma fonte de conteúdo dinâmico (nomes de projeto, comandos) com Latin-1/Latin Extended-A, cirílico e ~3.500 ideogramas chineses comuns (GB2312 nível 1, 16px 1bpp ≈ 110 KB). Um glyph ausente é desenhado como `□`, nunca trava a tela.

## 4. Telas

### 4.1 Visão geral adaptativa (modo padrão)

Mockups: `mockups/overview-adaptive.html`, `mockups/alert-flow.html`.

- **Precisa de você** (existe sessão em permissão/pergunta): faixa âmbar fixa no topo com "N AGUARDANDO · <sessão>"; limites em tamanho grande; sessões pendentes no topo da lista compacta, em âmbar.
- **Trabalhando** (há sessões rodando, nenhuma pendente): limites grandes (5h com % e horário de reset; semanal menor); lista curta das sessões com a atividade atual.
- **Tudo pronto / ocioso:** limites grandes; no rodapé, a última sessão que terminou e o custo do dia.
- Lista com mais itens do que cabe: rotaciona a cada 5s.

### 4.2 Alertas (todos os modos)

| Tipo | Gatilho | Sequência |
|---|---|---|
| Precisa de você | permissão ou pergunta | flash âmbar ~1,5s → herói ~10s → resumo com faixa âmbar fixa; repete flash + herói a cada ~2 min enquanto pendente |
| Terminou | `Stop` | flash azul ~1,5s → herói ~5s (nome, duração, tokens da resposta, ctx) → resumo com "✓ terminou" até o próximo prompt da sessão |

- **Herói de permissão:** nome da sessão, motivo ("Pediu permissão"), ferramenta e comando/arquivo, tempo esperando, "+N rodando".
- **Prioridade do herói:** permissão > pergunta > terminou; empate → quem espera há mais tempo.
- **Alertas simultâneos** entram em fila.
- Todos os tempos são configuráveis; os valores acima são os padrões. O lembrete pode ser desligado.
- **Sem botão:** a pendência é dispensada pelo próprio uso (§5.1).

### 4.3 Modo Limites (L1)

Mockup: `mockups/modes.html`. Arco grande com o % da janela de 5h e o tempo até o reset. Abaixo, a barra da semana. Cores de alerta a partir de 80% e 95%.

### 4.4 Modo Sessões (S1)

Mockup: `mockups/modes.html`. Uma linha por sessão: nome do projeto, estado (cor + ícone), atividade atual, tempo no estado, modelo, % de contexto e tokens. Ordem: pendentes, rodando, terminou, ociosa. Página com 4 sessões; rola a cada 5s quando há mais.

### 4.5 Telas de sistema

**Boot/loading com o mascote do Miblo** (animação curta de poucos quadros, também usada enquanto conecta ao WiFi), Setup (QR de WiFi + nome da rede), WiFi conectado + comando de instalação + código de pareamento + IP, "Pareado com <host>", "Desconectado" com relógio, "Atualizando firmware…" com barra de progresso, "Senha incorreta".

## 5. Dados

### 5.1 Máquina de estados de sessão

Estados: `idle`, `running`, `perm`, `question`, `done`.

| Evento | Transição / efeito |
|---|---|
| `SessionStart` | cria a sessão em `idle`; nome = basename do `cwd` (desambiguado com sufixo se repetir) |
| `UserPromptSubmit` | → `running`; limpa `done` |
| `PreToolUse` (ferramenta ≠ `AskUserQuestion`) | → `running`; `tool`/`det` = ferramenta + detalhe curto (arquivo sem caminho, 1ª linha do comando, padrão de busca, host da URL, descrição do agente) |
| `PreToolUse` (`AskUserQuestion`) ou `Notification` com `notification_type = elicitation_dialog` | → `question`; gera alerta âmbar |
| `PermissionRequest` | → `perm`; `tool`/`det` = ferramenta + comando/arquivo de `tool_input`; gera alerta âmbar |
| `PostToolUse` | `perm`/`question` → `running` |
| `Stop` sem trabalho em segundo plano | → `done`; gera alerta azul |
| `Stop` com trabalho em segundo plano (`background_tasks` não vazio; sem o campo, em versões antigas do Claude Code, subagentes ativos contados por `SubagentStart`/`SubagentStop`) | fica `running`, sem alerta; `tool` = `Agent`, `det` = `waiting N agents` (ou `waiting N tasks` se houver shell/monitor etc.). A sessão será reacordada pelas notificações das tarefas; só um `Stop` sem nada pendente dá `done` |
| `SubagentStart` / `SubagentStop` (com `agent_id`; `agent_type` vazio = agente interno, ignorado) | só registra/remove o subagente ativo da sessão; não muda o estado |
| Evento vindo de dentro de um subagente (traz `agent_id`) | não muda o estado da sessão principal (nunca gera `question`/`done`), exceto `PermissionRequest`, que o usuário vê: → `perm` com alerta âmbar; o próximo evento desse subagente sai da pendência |
| Subagente sem eventos por 30 min, ou sessão aguardando segundo plano sem eventos por 30 min | o subagente expira; a sessão vai para `done` sem alerta |
| Qualquer outro evento da sessão enquanto em `perm`/`question` | sai da pendência (permissão negada não gera `PostToolUse`) |
| `SessionEnd` ou PID do Claude inexistente | remove a sessão (e seus subagentes) |

- O PID é verificado a cada 15s, para cobrir terminais fechados à força.
- Modo discreto: a atividade mostra só o tipo da ferramenta.

### 5.2 Métricas

- **Fonte única: a status line** (§3.2), documentada oficialmente pelo Claude Code.
- **ctx/modelo por sessão:** `context_window.used_percentage` e `model.display_name` da leitura mais recente da sessão.
- **Tokens por sessão:** `total_input_tokens + total_output_tokens` — segundo a documentação oficial, são os tokens **do contexto atual** (não um total acumulado); exibidos como tamanho de contexto.
- **Custo do dia:** soma dos deltas positivos de `cost.total_cost_usd` (este sim acumulado por sessão), zerada à meia-noite local. A primeira leitura de uma sessão só conta integralmente se o bridge viu o `SessionStart` dela; caso contrário vira linha de base (evita contar em dobro após reiniciar o bridge). Não há contagem de tokens do dia.
- **Limites:** `rate_limits.five_hour` e `rate_limits.seven_day` da leitura mais recente de qualquer sessão. Cada janela pode vir ausente; o bridge mantém o último valor até o `resets_at` passar e então o descarta.
- **Sem assinatura Pro/Max** (chave de API): `rate_limits` nunca vem. A área de limites mostra o custo do dia.
- **Sem status line encadeada:** `usage: null` e sessões sem ctx/tokens.

### 5.3 Protocolo bridge → gadget

`POST /api/state`, `Authorization: Bearer <token>`, corpo ≤ 3 KB (se exceder, o bridge move sessões do fim da lista para `more`):

```json
{
  "v": 1,
  "seq": 8812,
  "now": 1790600000,
  "host": "MacBook-Marcus",
  "usage": {
    "h5":  {"pct": 62, "reset": 1790607800},
    "d7":  {"pct": 38, "reset": 1790830000}
  },
  "today": {"usd": 4.8},
  "sessions": [
    {"id": "a1", "name": "api-server", "st": "perm", "tool": "Bash", "det": "npm run migrate",
     "since": 1790599958, "model": "Opus", "ctx": 71, "tok": 412000}
  ],
  "more": 0,
  "alerts": [{"id": 311, "kind": "perm", "sid": "a1"}]
}
```

- Enviado a cada mudança de estado (com debounce de 150ms) e a cada 10s como sinal de vida.
- `usage` pode ser `null` quando indisponível; `sessions` tem no máximo 8 itens, e o excedente vai em `more`; `name` tem no máximo 20 caracteres e `det` no máximo 32; `tool` é o nome da ferramenta do Claude Code (o gadget traduz as conhecidas — Bash, Edit, Write, Read, Grep, Glob, WebFetch, WebSearch, Task/Agent — e mostra as demais como vieram; ferramentas MCP `mcp__srv__x` chegam como `x`). Em modo discreto o gadget oculta `det`.
- `alerts` contém os alertas ainda não confirmados. O gadget guarda o maior `id` já exibido e ignora os repetidos.
- `v` permite evolução: o gadget ignora campos desconhecidos, e o bridge lê o `v` suportado em `/api/info`.

### 5.4 Pareamento

O gadget exibe um código de 4 dígitos. `POST /api/pair {code, host}` → gadget responde com um token aleatório de 128 bits, salvo em flash nos dois lados. Após 5 códigos errados, novas tentativas recebem HTTP 429 por 60s. Cada gadget aceita vários computadores pareados (até 4 tokens), mas o fluxo principal é um.

## 6. Setup do usuário final

Mockup: `mockups/setup-flow.html`. Meta: < 3 minutos, sem manual.

1. Ligar na tomada → a tela mostra um QR de WiFi (`WIFI:S:Miblo-Setup-XXXX;;`) e o nome da rede.
2. O celular conecta e o captive portal abre sozinho: escolher a rede, digitar a senha; o fuso é detectado pelo navegador.
3. A tela do gadget mostra: WiFi conectado, comando `/plugin install ...`, código de pareamento e IP.
4. No Claude Code: instalar o plugin → o `/miblo:pair` roda automaticamente → encontra o gadget e pede o código.
5. A tela mostra "Pareado com <host>" e entra no modo Visão geral.

### Exceções

- **Senha errada** → volta ao passo 1 com "Senha incorreta".
- **mDNS indisponível** (WSL2, rede corporativa) → `/miblo:pair <ip>`.
- **Vários gadgets** → a lista mostra todos; o código identifica qual.
- **IP mudou** → o bridge redescobre pelo ID via mDNS; na falha, faz varredura do IP antigo e avisa em `/miblo:status`.
- **Roteador fora do ar** → o gadget mantém as credenciais; após 2 min sem conexão, abre a rede de setup **e continua tentando** a rede salva.
- **Reset de fábrica** → (a) pela página do gadget, confirmado com o código de 4 dígitos mostrado na tela; (b) por `/miblo:reset` (token do pareamento); (c) **hard reset estilo AirTag**: 6 boots rápidos seguidos (cada um desligado antes de 10 s de uptime; o contador zera após 10 s ligado). A partir do 3º boot rápido a tela mostra "Mais N reinícios rápidos para resetar · deixe ligado para cancelar" (N = 3, 2, 1); no 6º, reset de fábrica e tela de setup. Só contam boots por energia (`REASON_DEFAULT_RST`/`REASON_EXT_SYS_RST`) — crash, watchdog, OTA e reinício por software não contam. Quedas de energia comuns não apagam nada: Wi-Fi, pareamento e configurações ficam na flash; trocar de roteador não exige reset (após 2 min sem conexão o gadget abre a rede de setup).

## 7. Tratamento de erros

| Falha | Comportamento |
|---|---|
| Bridge fora do ar | Os hooks tentam subir o bridge; o Claude Code nunca é afetado |
| Status line não encadeada ou sem `rate_limits` | `usage: null`; o gadget mostra "limites indisponíveis" (com dica "rode /miblo:pair" na página) |
| Status line original do usuário falha | O tap devolve a mesma saída/código de erro — o comportamento do usuário não muda |
| Gadget inacessível | Espera crescente (1s → 60s); redescoberta mDNS; aparece como offline em `/miblo:status` |
| Snapshot ausente por 30s | Gadget mostra "desconectado" + relógio |
| JSON inválido/grande demais | Gadget responde 400 e mantém a última tela válida |
| OTA falha no meio | O ESP8266 mantém a imagem anterior (OTA padrão com verificação); o bridge informa o erro |

## 8. Testes

- **Bridge — unitários** (`node:test`, sem dependências): testes em tabela de evento → estado para todas as transições de §5.1, incluindo PID morto, dedupe e prioridade de alertas; MetricsStore com fixtures de JSON de status line (com/sem `rate_limits`, janela expirada, deltas, virada do dia); SnapshotBuilder com os limites de tamanho.
- **Hook e tap** — `hook.js` termina com código 0 e sem stdout com o bridge fora do ar; `statusline-tap.js` devolve byte a byte a saída do comando original (e nada quando não há original), com o bridge fora do ar.
- **Settings** — encadear/desencadear a status line preserva o resto do `settings.json` e é idempotente.
- **Contrato** — os mesmos arquivos em `fixtures/snapshots/*.json` são gerados/validados pelos testes do bridge e consumidos pelos testes do firmware.
- **Firmware — lógica** (`pio test -e native`): parse do snapshot, escolha do herói, AlertQueue (dedupe, ordem, lembrete), rotação de páginas.
- **Preview de telas** — script que gera PNGs de cada tela a partir das fixtures de snapshot, para revisão visual e fotos do anúncio.
- **Checklist manual por release** — setup do zero, senha errada, queda do roteador, dois gadgets, computador desligado, OTA via `/miblo:update`.

## 9. Ordem de construção

0. **Teste de hardware** — baixar o firmware oficial da GeekMagic (para restauração); gravar por OTA um firmware mínimo que acende a tela, desenha texto e **já inclui OTA**; confirmar pinos da tela e da luz de fundo, tamanho da flash e se a página de update original aceita o `.bin`. Nenhuma outra etapa começa antes disso.
1. Bridge — núcleo (SessionTracker, MetricsStore, SnapshotBuilder).
2. Bridge — plugin (`hook.js`, `statusline-tap.js`, subida automática, `/miblo:status`).
3. Firmware — base (captive portal + QR, mDNS, pareamento, API, página de configuração, OTA).
4. Firmware — telas (Visão geral adaptativa, alertas, Limites, Sessões, sistema).
5. Integração (`/miblo:pair`, `/miblo:mode`, `/miblo:update`, `/miblo:reset`, vários gadgets, checklist manual).

## 10. Riscos de produto

1. **Marca** — "Claude" e o logo da Anthropic são marcas registradas. O produto, a caixa e o anúncio não devem usar o nome nem o logo como marca. Usar um nome próprio + "compatível com Claude Code". As telas do gadget usam o nome do produto, e não o logo da Anthropic.
2. **Encadear a status line** — altera o `~/.claude/settings.json` do usuário. O tap e o comando original ficam em `~/.claude/miblo/` (sobrevivem à desinstalação do plugin; um tap órfão continua executando a status line original). Mitigação: só com consentimento, comando original salvo, reversível com `/miblo:unlink-statusline`. Se o usuário trocar a status line depois, o encadeamento se desfaz (o bridge detecta ausência de leituras e `/miblo:status` avisa).
3. **Anatel** — verificar se o GeekMagic tem homologação e se a revenda com firmware alterado mantém a conformidade.
4. **Hardware** — pinos e comportamento do OTA da GeekMagic Ultra ainda não confirmados (etapa 0). Revisões futuras do hardware podem mudar o chip, então o firmware verifica o ID da flash e o modelo em `/api/info`.
5. **RAM do ESP8266** — ~80 KB livres; o renderizador por regiões e o limite de 3 KB do snapshot existem por isso.
6. **Fontes CJK/cirílico na flash** — o orçamento de 4 MB precisa acomodar 2× firmware (OTA) + LittleFS com fontes (~1 MB). Confirmar o tamanho da flash na etapa 0; se for 2 MB, a fonte chinesa dinâmica é reduzida às strings fixas e nomes de projeto em chinês mostram `□`.
