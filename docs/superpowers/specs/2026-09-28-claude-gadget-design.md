# Pulse — gadget de mesa para status do Claude Code (Fase A)

- **Data:** 2026-09-28
- **Status:** design aprovado, aguardando revisão do spec
- **Nome:** "Pulse" é provisório (ver [Riscos de produto](#riscos-de-produto))
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
| Lado do computador | Plugin do Claude Code em Node.js sem dependências externas |
| Tela Visão geral | Adaptativa (mistura das propostas A + B) — ver §4.1 |
| Modo Limites | L1 — medidor em arco grande |
| Modo Sessões | S1 — lista detalhada, rolagem automática a cada 5s |
| Alertas | Flash + herói temporário, em todos os modos (desligável por aparelho) |

## 3. Arquitetura

Mockup: `mockups/architecture.html`.

```
Claude Code ──hooks──▶ hook.js ──HTTP 127.0.0.1──▶ bridge (Node) ──HTTP LAN──▶ gadget(s)
                                                     │  ▲                         │
                                        transcripts ─┘  └─ endpoint de uso       └─ mDNS _cgadget._tcp
```

### 3.1 `hook.js` (plugin)

- Registrado para os hooks: `SessionStart`, `UserPromptSubmit`, `PreToolUse`, `PostToolUse`, `Notification`, `Stop`, `SessionEnd`.
- Lê o JSON do hook no stdin, acrescenta o PID do processo pai (Claude Code) e repassa via `POST http://127.0.0.1:<porta>/event`.
- Se o bridge não responder, sobe o bridge como processo destacado (detached) e reenvia uma vez.
- **Invariante:** nunca atrasa nem quebra o Claude Code. Timeout total de 300ms, sempre termina com código 0, sem saída no stdout.

### 3.2 Bridge (serviço local)

Processo Node único por usuário, escutando só em `127.0.0.1`. A porta e o estado de pareamento ficam num diretório de dados do plugin. Encerra sozinho após 30 min sem nenhuma sessão ativa.

Unidades (cada uma testável isoladamente):

- **SessionTracker** — máquina de estados por sessão (§5.1); produz a lista de sessões e a fila de alertas.
- **TranscriptReader** — lê incrementalmente o `.jsonl` do transcript (o caminho chega nos hooks) e extrai o modelo, o total de tokens de entrada/saída e o % de contexto da última mensagem do assistente.
- **UsageClient** — a cada 60s, consulta os limites de uso com a credencial OAuth do Claude Code (macOS: Keychain; Linux/Windows: `~/.claude/.credentials.json`). Degrada com elegância (§7).
- **DeviceManager** — descoberta mDNS, pareamento, envio do snapshot, reenvio com espera crescente e OTA.
- **SnapshotBuilder** — monta o JSON do protocolo (§5.3) a partir das outras unidades, aplicando os limites de tamanho.

### 3.3 Comandos `/gadget`

- `/gadget pair [ip]` — descobre gadgets (ou usa o IP informado), pede o código de 4 dígitos exibido na tela e salva o token. Roda automaticamente na primeira instalação.
- `/gadget status` — sessões, limites e gadgets pareados (online/offline).
- `/gadget mode <visao|limites|sessoes> [gadget]`
- `/gadget update` — envia o firmware mais recente para os gadgets pareados.
- `/gadget reset [gadget]` — reset de fábrica remoto.

### 3.4 Firmware (ESP8266, PlatformIO/Arduino)

- **Rede:** setup por captive portal; mDNS `pulse-xxxx.local`; anuncia `_cgadget._tcp` com o ID do aparelho.
- **API HTTP:** `POST /api/state` (exige `Authorization: Bearer <token>`), `POST /api/pair`, `GET /api/info` (ID, versão, modo, pareado?), `POST /api/config`, `POST /update` (OTA, protegido por token).
- **Página de configuração** (`http://pulse-xxxx.local`): modo, brilho, alertas liga/desliga, durações (herói de permissão, herói de término, intervalo do lembrete), modo discreto, fuso horário, nome do aparelho, reset de fábrica, update de firmware.
- **Renderizador:** desenha por regiões/sprites parciais (RAM livre ~80 KB < framebuffer de 115 KB); a tela só é redesenhada nas regiões que mudaram.
- **AlertQueue:** fila de alertas, deduplicada pelo `id`, com âmbar antes de azul.
- **Watchdog de conexão:** 30s sem snapshot → tela "desconectado" com relógio (via NTP).
- **Persistência:** WiFi, token, modo e configurações em flash (LittleFS/EEPROM).

## 4. Telas

### 4.1 Visão geral adaptativa (modo padrão)

Mockups: `mockups/overview-adaptive.html`, `mockups/alert-flow.html`.

- **Precisa de você** (existe sessão em permissão/pergunta): faixa âmbar fixa no topo com "N AGUARDANDO · <sessão>"; limites em tamanho grande; sessões pendentes no topo da lista compacta, em âmbar.
- **Trabalhando** (há sessões rodando, nenhuma pendente): limites grandes (5h com % e horário de reset; semanal menor); lista curta das sessões com a atividade atual.
- **Tudo pronto / ocioso:** limites grandes; no rodapé, a última sessão que terminou e os tokens do dia.
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

Mockup: `mockups/modes.html`. Arco grande com o % da janela de 5h e o tempo até o reset. Abaixo, barras da semana (geral e, quando existir, por modelo — ex. "Semana Opus"). Cores de alerta a partir de 80% e 95%.

### 4.4 Modo Sessões (S1)

Mockup: `mockups/modes.html`. Uma linha por sessão: nome do projeto, estado (cor + ícone), atividade atual, tempo no estado, modelo, % de contexto e tokens. Ordem: pendentes, rodando, terminou, ociosa. Página com 4 sessões; rola a cada 5s quando há mais.

### 4.5 Telas de sistema

Setup (QR de WiFi + nome da rede), WiFi conectado + comando de instalação + código de pareamento + IP, "Pareado com <host>", "Desconectado" com relógio, "Atualizando firmware…" com barra de progresso, "Senha incorreta".

## 5. Dados

### 5.1 Máquina de estados de sessão

Estados: `idle`, `running`, `perm`, `question`, `done`.

| Evento | Transição / efeito |
|---|---|
| `SessionStart` | cria a sessão em `idle`; nome = basename do `cwd` (desambiguado com sufixo se repetir) |
| `UserPromptSubmit` | → `running`; limpa `done` |
| `PreToolUse` (ferramenta ≠ `AskUserQuestion`) | → `running`; atividade = ferramenta + detalhe curto (arquivo sem caminho, comando truncado) |
| `PreToolUse` (`AskUserQuestion`) | → `question`; gera alerta âmbar |
| `Notification` do tipo `permission_prompt` | → `perm`; gera alerta âmbar |
| `PostToolUse` | `perm`/`question` → `running` |
| `Stop` | → `done`; gera alerta azul |
| `SessionEnd` ou PID do Claude inexistente | remove a sessão |

- O PID é verificado a cada 15s, para cobrir terminais fechados à força.
- Modo discreto: a atividade mostra só o tipo da ferramenta.
- A lista exata de campos e tipos de `Notification` deve ser confirmada contra a versão atual do Claude Code na implementação. Se `permission_prompt` não estiver disponível, usar o hook `PermissionRequest`.

### 5.2 Métricas

- **Tokens/ctx/modelo:** a partir do transcript, lendo só o que foi acrescentado desde a última leitura. O % de contexto é calculado pelo uso da última resposta ÷ janela do modelo.
- **Tokens do dia:** soma por dia local de todas as sessões vistas pelo bridge.
- **Limites:** janela de 5h, semanal e semanal por modelo (quando o retorno tiver), com os horários de reset.
- **Usuário de chave de API** (sem assinatura): não há limites. A área de limites mostra o custo estimado do dia.

### 5.3 Protocolo bridge → gadget

`POST /api/state`, `Authorization: Bearer <token>`, corpo < 2 KB:

```json
{
  "v": 1,
  "seq": 8812,
  "now": 1790600000,
  "host": "MacBook-Marcus",
  "usage": {
    "h5":  {"pct": 62, "reset": 1790607800},
    "d7":  {"pct": 38, "reset": 1790830000},
    "d7_opus": {"pct": 71, "reset": 1790830000}
  },
  "today": {"in": 1200000, "out": 310000},
  "sessions": [
    {"id": "a1", "name": "api-server", "st": "perm", "act": "Bash · npm run migrate",
     "since": 1790599958, "model": "opus", "ctx": 71, "tok": 412000}
  ],
  "more": 0,
  "alerts": [{"id": 311, "kind": "perm", "sid": "a1"}]
}
```

- Enviado a cada mudança de estado (com debounce de 150ms) e a cada 10s como sinal de vida.
- `usage` pode ser `null` quando indisponível; `sessions` tem no máximo 8 itens, e o excedente vai em `more`; `name` tem no máximo 20 caracteres e `act` no máximo 32.
- `alerts` contém os alertas ainda não confirmados. O gadget guarda o maior `id` já exibido e ignora os repetidos.
- `v` permite evolução: o gadget ignora campos desconhecidos, e o bridge lê o `v` suportado em `/api/info`.

### 5.4 Pareamento

O gadget exibe um código de 4 dígitos. `POST /api/pair {code, host}` → gadget responde com um token aleatório de 128 bits, salvo em flash nos dois lados. Após 5 códigos errados, novas tentativas ficam bloqueadas por 60s. Cada gadget aceita vários computadores pareados (até 4 tokens), mas o fluxo principal é um.

## 6. Setup do usuário final

Mockup: `mockups/setup-flow.html`. Meta: < 3 minutos, sem manual.

1. Ligar na tomada → a tela mostra um QR de WiFi (`WIFI:S:Pulse-Setup-XXXX;;`) e o nome da rede.
2. O celular conecta e o captive portal abre sozinho: escolher a rede, digitar a senha; o fuso é detectado pelo navegador.
3. A tela do gadget mostra: WiFi conectado, comando `/plugin install ...`, código de pareamento e IP.
4. No Claude Code: instalar o plugin → o `/gadget pair` roda automaticamente → encontra o gadget e pede o código.
5. A tela mostra "Pareado com <host>" e entra no modo Visão geral.

### Exceções

- **Senha errada** → volta ao passo 1 com "Senha incorreta".
- **mDNS indisponível** (WSL2, rede corporativa) → `/gadget pair <ip>`.
- **Vários gadgets** → a lista mostra todos; o código identifica qual.
- **IP mudou** → o bridge redescobre pelo ID via mDNS; na falha, faz varredura do IP antigo e avisa em `/gadget status`.
- **Roteador fora do ar** → o gadget mantém as credenciais; após 2 min sem conexão, abre a rede de setup **e continua tentando** a rede salva.
- **Reset de fábrica (sem botão)** → 3 ciclos de liga/desliga em menos de 10s (contador persistido na flash, zerado após 10s de uptime), pela página web ou com `/gadget reset`.

## 7. Tratamento de erros

| Falha | Comportamento |
|---|---|
| Bridge fora do ar | Os hooks tentam subir o bridge; o Claude Code nunca é afetado |
| Endpoint de limites falha/muda/401 | `usage: null`; o gadget mostra "limites indisponíveis"; nova tentativa em 60s com espera crescente até 10 min |
| Gadget inacessível | Espera crescente (1s → 60s); redescoberta mDNS; aparece como offline em `/gadget status` |
| Snapshot ausente por 30s | Gadget mostra "desconectado" + relógio |
| JSON inválido/grande demais | Gadget responde 400 e mantém a última tela válida |
| Transcript ilegível | Métricas da sessão ficam vazias; o estado continua vindo dos hooks |
| OTA falha no meio | O ESP8266 mantém a imagem anterior (OTA padrão com verificação); o bridge informa o erro |

## 8. Testes

- **Bridge — unitários** (`node:test`, sem dependências): testes em tabela de evento → estado para todas as transições de §5.1, incluindo PID morto, dedupe e prioridade de alertas; TranscriptReader com fixtures `.jsonl`; UsageClient com respostas simuladas (sucesso, 401, formato inesperado, timeout); SnapshotBuilder com os limites de tamanho.
- **Hook** — teste de que `hook.js` termina em < 300ms e com código 0 com o bridge fora do ar.
- **Contrato** — os mesmos arquivos em `fixtures/snapshots/*.json` são gerados/validados pelos testes do bridge e consumidos pelos testes do firmware.
- **Firmware — lógica** (`pio test -e native`): parse do snapshot, escolha do herói, AlertQueue (dedupe, ordem, lembrete), rotação de páginas, contador de reset por liga/desliga.
- **Preview de telas** — script que gera PNGs de cada tela a partir das fixtures de snapshot, para revisão visual e fotos do anúncio.
- **Checklist manual por release** — setup do zero, senha errada, queda do roteador, reset por liga/desliga, dois gadgets, computador desligado, OTA via `/gadget update`.

## 9. Ordem de construção

0. **Teste de hardware** — baixar o firmware oficial da GeekMagic (para restauração); gravar por OTA um firmware mínimo que acende a tela, desenha texto e **já inclui OTA**; confirmar pinos da tela e da luz de fundo, tamanho da flash e se a página de update original aceita o `.bin`. Nenhuma outra etapa começa antes disso.
1. Bridge — núcleo (SessionTracker, TranscriptReader, UsageClient, SnapshotBuilder).
2. Bridge — plugin (`hook.js`, subida automática, `/gadget status`).
3. Firmware — base (captive portal + QR, mDNS, pareamento, API, página de configuração, OTA).
4. Firmware — telas (Visão geral adaptativa, alertas, Limites, Sessões, sistema).
5. Integração (`/gadget pair/mode/update/reset`, reset por liga/desliga, vários gadgets, checklist manual).

## 10. Riscos de produto

1. **Marca** — "Claude" e o logo da Anthropic são marcas registradas. O produto, a caixa e o anúncio não devem usar o nome nem o logo como marca. Usar um nome próprio + "compatível com Claude Code". As telas do gadget usam o nome do produto, e não o logo da Anthropic.
2. **Endpoint de limites** — não documentado e acessado com a credencial OAuth do usuário; pode mudar sem aviso. Verificar os termos da Anthropic antes da venda. A funcionalidade é opcional e degrada com elegância.
3. **Anatel** — verificar se o GeekMagic tem homologação e se a revenda com firmware alterado mantém a conformidade.
4. **Hardware** — pinos e comportamento do OTA da GeekMagic Ultra ainda não confirmados (etapa 0). Revisões futuras do hardware podem mudar o chip, então o firmware verifica o ID da flash e o modelo em `/api/info`.
5. **RAM do ESP8266** — ~80 KB livres; o renderizador por regiões e o limite de 2 KB do snapshot existem por isso.
