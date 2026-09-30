// String tables (UTF-8) — one packed string per language, entries separated by \0.
// The entry order exactly follows the miblo::S enum (miblo_i18n.h).
// test_i18n checks the count and the %s/%u placeholders for each language.
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
    "Alternate with limits\0"  // WebRotate
    "Show limits every (s)\0"  // WebRotateEvery
    "Keep limits on screen for (s)\0"  // WebRotateShow
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
    "No limits received yet: run /miblo:pair in Claude Code\0"  // WebLimitsHint
    "Paired computers: %u\0"  // WebPairedCount
    "Firmware version\0"  // WebVersion
    "Quick restarts left to reset: %u\0"  // HardResetCountdown
    "Leave it on to cancel\0"  // HardResetCancelHint
    "Network not found\0"  // NetNotFound
    "Use a 2.4 GHz network\0"  // Use24GHz
    "Network not found. Miblo only works with 2.4 GHz Wi-Fi.\0"  // WebNotFound
    "Connected! Open Miblo at:\0"  // WebConnectedAt
    "Could not connect. Check the network and try again.\0"  // WebConnectFailed
    "Try again\0"  // WebTryAgain
    "No reply from Miblo. Check the device screen.\0"  // WebNoReply
    "Connection refused\0"  // ConnRefused
    "Check password or use WPA2\0"  // RefusedHint
    "Could not connect\0"  // JoinFailed
    "Error code %u\0"  // ErrorCode
    "The router refused the connection. Check the password. If the router uses WPA3 or \"WPA2/WPA3\" mode, switch it to WPA2 (Miblo does not support WPA3).\0"  // WebRefused
    "Could not connect (code %u). Check the network and try again.\0"  // WebFailedCode
    "Waiting on %u agent\0"  // WaitAgent1
    "Waiting on %u agents\0"  // WaitAgentsN
    "Waiting on %u task\0"  // WaitTask1
    "Waiting on %u tasks\0"  // WaitTasksN
    "5h\0"  // Short5h
    "7d\0"  // Short7d
    "Auto\0"  // WebAuto
    "Compacting context\0"  // Compacting
    "Connect Miblo to the same Wi-Fi network as your computer, or they won't find each other.\0"  // WebSameNetwork
    "Night mode: dim the screen\0"  // WebNight
    "Night starts at\0"  // WebNightFrom
    "Night ends at\0"  // WebNightTo
    "Night brightness\0"  // WebNightBrightness
    "LIMIT FREED\0"  // LimitFreed
    "runs out in %s\0"  // RunsOutIn
    "TODAY\0"  // TodayTitle
    "responses\0"  // SumResponses
    "worked\0"  // SumWorked
    "spent\0"  // SumSpent
    "Mascot\0"  // WebMascot
    "Sphynx (peach)\0"  // WebMascotSphynx
    "Orange\0"  // WebMascotOrange
    "Black\0"  // WebMascotBlack
    "Grey\0"  // WebMascotGrey
    "Update available\0"  // UpdateAvailable
    "v%s (you have v%s)\0"  // UpdateVersions
    "Check for updates\0"  // WebCheckUpdates
    "Up to date (v%s)\0"  // WebUpToDate
    "Version %s is available.\0"  // WebNewVersion
    "In Claude Code, run /miblo:update. Or download the file and install it on the firmware update page.\0"  // WebUpdateHow
    "Couldn't check for updates (no internet?)\0"  // WebCheckFailed
    "Download the firmware\0"  // WebDownloadBin
    "Turn the screen off when idle\0"  // WebSleep
    "Never (the mascot keeps wandering)\0";  // WebSleepNever

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
    "Alternar com os limites\0"  // WebRotate
    "Mostrar limites a cada (s)\0"  // WebRotateEvery
    "Manter os limites por (s)\0"  // WebRotateShow
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
    "Nenhum limite recebido ainda: rode /miblo:pair no Claude Code\0"  // WebLimitsHint
    "Computadores pareados: %u\0"  // WebPairedCount
    "Versão do firmware\0"  // WebVersion
    "Reinícios rápidos restantes para resetar: %u\0"  // HardResetCountdown
    "Deixe ligado para cancelar\0"  // HardResetCancelHint
    "Rede não encontrada\0"  // NetNotFound
    "Use uma rede de 2,4 GHz\0"  // Use24GHz
    "Rede não encontrada. O Miblo só funciona com Wi-Fi de 2,4 GHz.\0"  // WebNotFound
    "Conectado! Abra o Miblo em:\0"  // WebConnectedAt
    "Não foi possível conectar. Verifique a rede e tente de novo.\0"  // WebConnectFailed
    "Tentar de novo\0"  // WebTryAgain
    "Sem resposta do Miblo. Veja a tela do aparelho.\0"  // WebNoReply
    "Conexão recusada\0"  // ConnRefused
    "Confira a senha ou use WPA2\0"  // RefusedHint
    "Não foi possível conectar\0"  // JoinFailed
    "Código de erro %u\0"  // ErrorCode
    "O roteador recusou a conexão. Confira a senha. Se o roteador usa WPA3 ou o modo \"WPA2/WPA3\", mude para WPA2 (o Miblo não suporta WPA3).\0"  // WebRefused
    "Não foi possível conectar (código %u). Verifique a rede e tente de novo.\0"  // WebFailedCode
    "Aguardando %u agente\0"  // WaitAgent1
    "Aguardando %u agentes\0"  // WaitAgentsN
    "Aguardando %u tarefa\0"  // WaitTask1
    "Aguardando %u tarefas\0"  // WaitTasksN
    "5h\0"  // Short5h
    "7d\0"  // Short7d
    "Automático\0"  // WebAuto
    "Compactando contexto\0"  // Compacting
    "Conecte o Miblo à mesma rede Wi-Fi do seu computador, senão eles não vão se encontrar.\0"  // WebSameNetwork
    "Modo noturno: reduzir o brilho\0"  // WebNight
    "Início da noite\0"  // WebNightFrom
    "Fim da noite\0"  // WebNightTo
    "Brilho à noite\0"  // WebNightBrightness
    "LIMITE LIBERADO\0"  // LimitFreed
    "acaba em %s\0"  // RunsOutIn
    "HOJE\0"  // TodayTitle
    "respostas\0"  // SumResponses
    "trabalhando\0"  // SumWorked
    "gasto\0"  // SumSpent
    "Mascote\0"  // WebMascot
    "Sphynx (pêssego)\0"  // WebMascotSphynx
    "Laranja\0"  // WebMascotOrange
    "Preto\0"  // WebMascotBlack
    "Cinza\0"  // WebMascotGrey
    "Atualização disponível\0"  // UpdateAvailable
    "v%s (você tem v%s)\0"  // UpdateVersions
    "Buscar atualizações\0"  // WebCheckUpdates
    "Tudo atualizado (v%s)\0"  // WebUpToDate
    "A versão %s está disponível.\0"  // WebNewVersion
    "No Claude Code, rode /miblo:update. Ou baixe o arquivo e instale na página de atualização de firmware.\0"  // WebUpdateHow
    "Não foi possível buscar atualizações (sem internet?)\0"  // WebCheckFailed
    "Baixar o firmware\0"  // WebDownloadBin
    "Desligar a tela quando ninguém estiver usando\0"  // WebSleep
    "Nunca (o mascote fica passeando)\0";  // WebSleepNever

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
    "Alternar com os limites\0"  // WebRotate
    "Mostrar limites a cada (s)\0"  // WebRotateEvery
    "Manter os limites durante (s)\0"  // WebRotateShow
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
    "Ainda não chegaram limites: execute /miblo:pair no Claude Code\0"  // WebLimitsHint
    "Computadores emparelhados: %u\0"  // WebPairedCount
    "Versão do firmware\0"  // WebVersion
    "Reinícios rápidos restantes para repor: %u\0"  // HardResetCountdown
    "Deixe ligado para cancelar\0"  // HardResetCancelHint
    "Rede não encontrada\0"  // NetNotFound
    "Use uma rede de 2,4 GHz\0"  // Use24GHz
    "Rede não encontrada. O Miblo só funciona com Wi-Fi de 2,4 GHz.\0"  // WebNotFound
    "Ligado! Abra o Miblo em:\0"  // WebConnectedAt
    "Não foi possível ligar. Verifique a rede e tente novamente.\0"  // WebConnectFailed
    "Tentar novamente\0"  // WebTryAgain
    "Sem resposta do Miblo. Veja o ecrã do aparelho.\0"  // WebNoReply
    "Ligação recusada\0"  // ConnRefused
    "Verifique a palavra-passe ou use WPA2\0"  // RefusedHint
    "Não foi possível ligar\0"  // JoinFailed
    "Código de erro %u\0"  // ErrorCode
    "O router recusou a ligação. Verifique a palavra-passe. Se o router usa WPA3 ou o modo \"WPA2/WPA3\", mude para WPA2 (o Miblo não suporta WPA3).\0"  // WebRefused
    "Não foi possível ligar (código %u). Verifique a rede e tente novamente.\0"  // WebFailedCode
    "À espera de %u agente\0"  // WaitAgent1
    "À espera de %u agentes\0"  // WaitAgentsN
    "À espera de %u tarefa\0"  // WaitTask1
    "À espera de %u tarefas\0"  // WaitTasksN
    "5h\0"  // Short5h
    "7d\0"  // Short7d
    "Automático\0"  // WebAuto
    "A compactar contexto\0"  // Compacting
    "Ligue o Miblo à mesma rede Wi-Fi do seu computador, caso contrário não se vão encontrar.\0"  // WebSameNetwork
    "Modo noturno: reduzir o brilho\0"  // WebNight
    "Início da noite\0"  // WebNightFrom
    "Fim da noite\0"  // WebNightTo
    "Brilho à noite\0"  // WebNightBrightness
    "LIMITE LIBERTADO\0"  // LimitFreed
    "acaba em %s\0"  // RunsOutIn
    "HOJE\0"  // TodayTitle
    "respostas\0"  // SumResponses
    "a trabalhar\0"  // SumWorked
    "gasto\0"  // SumSpent
    "Mascote\0"  // WebMascot
    "Sphynx (pêssego)\0"  // WebMascotSphynx
    "Laranja\0"  // WebMascotOrange
    "Preto\0"  // WebMascotBlack
    "Cinzento\0"  // WebMascotGrey
    "Atualização disponível\0"  // UpdateAvailable
    "v%s (tem a v%s)\0"  // UpdateVersions
    "Procurar atualizações\0"  // WebCheckUpdates
    "Tudo atualizado (v%s)\0"  // WebUpToDate
    "A versão %s está disponível.\0"  // WebNewVersion
    "No Claude Code, execute /miblo:update. Ou descarregue o ficheiro e instale-o na página de atualização de firmware.\0"  // WebUpdateHow
    "Não foi possível procurar atualizações (sem internet?)\0"  // WebCheckFailed
    "Descarregar o firmware\0"  // WebDownloadBin
    "Desligar o ecrã quando ninguém estiver a usar\0"  // WebSleep
    "Nunca (a mascote fica a passear)\0";  // WebSleepNever

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
    "Alternar con los límites\0"  // WebRotate
    "Mostrar límites cada (s)\0"  // WebRotateEvery
    "Mantener los límites durante (s)\0"  // WebRotateShow
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
    "Aún no llegan límites: ejecuta /miblo:pair en Claude Code\0"  // WebLimitsHint
    "Ordenadores vinculados: %u\0"  // WebPairedCount
    "Versión del firmware\0"  // WebVersion
    "Reinicios rápidos restantes para restablecer: %u\0"  // HardResetCountdown
    "Déjalo encendido para cancelar\0"  // HardResetCancelHint
    "Red no encontrada\0"  // NetNotFound
    "Usa una red de 2,4 GHz\0"  // Use24GHz
    "Red no encontrada. Miblo solo funciona con Wi-Fi de 2,4 GHz.\0"  // WebNotFound
    "¡Conectado! Abre Miblo en:\0"  // WebConnectedAt
    "No se pudo conectar. Revisa la red e inténtalo de nuevo.\0"  // WebConnectFailed
    "Reintentar\0"  // WebTryAgain
    "Miblo no responde. Mira la pantalla del dispositivo.\0"  // WebNoReply
    "Conexión rechazada\0"  // ConnRefused
    "Revisa la contraseña o usa WPA2\0"  // RefusedHint
    "No se pudo conectar\0"  // JoinFailed
    "Código de error %u\0"  // ErrorCode
    "El router rechazó la conexión. Revisa la contraseña. Si el router usa WPA3 o el modo \"WPA2/WPA3\", cámbialo a WPA2 (Miblo no admite WPA3).\0"  // WebRefused
    "No se pudo conectar (código %u). Revisa la red e inténtalo de nuevo.\0"  // WebFailedCode
    "Esperando %u agente\0"  // WaitAgent1
    "Esperando %u agentes\0"  // WaitAgentsN
    "Esperando %u tarea\0"  // WaitTask1
    "Esperando %u tareas\0"  // WaitTasksN
    "5h\0"  // Short5h
    "7d\0"  // Short7d
    "Automático\0"  // WebAuto
    "Compactando contexto\0"  // Compacting
    "Conecta Miblo a la misma red Wi-Fi que tu ordenador; si no, no se encontrarán.\0"  // WebSameNetwork
    "Modo nocturno: bajar el brillo\0"  // WebNight
    "Empieza a las\0"  // WebNightFrom
    "Termina a las\0"  // WebNightTo
    "Brillo nocturno\0"  // WebNightBrightness
    "LÍMITE LIBERADO\0"  // LimitFreed
    "se agota en %s\0"  // RunsOutIn
    "HOY\0"  // TodayTitle
    "respuestas\0"  // SumResponses
    "trabajando\0"  // SumWorked
    "gastado\0"  // SumSpent
    "Mascota\0"  // WebMascot
    "Sphynx (melocotón)\0"  // WebMascotSphynx
    "Naranja\0"  // WebMascotOrange
    "Negro\0"  // WebMascotBlack
    "Gris\0"  // WebMascotGrey
    "Actualización disponible\0"  // UpdateAvailable
    "v%s (tienes v%s)\0"  // UpdateVersions
    "Buscar actualizaciones\0"  // WebCheckUpdates
    "Todo actualizado (v%s)\0"  // WebUpToDate
    "La versión %s está disponible.\0"  // WebNewVersion
    "En Claude Code, ejecuta /miblo:update. O descarga el archivo e instálalo en la página de actualización de firmware.\0"  // WebUpdateHow
    "No se pudo buscar actualizaciones (¿sin internet?)\0"  // WebCheckFailed
    "Descargar el firmware\0"  // WebDownloadBin
    "Apagar la pantalla cuando nadie la use\0"  // WebSleep
    "Nunca (la mascota sigue paseando)\0";  // WebSleepNever

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
    "Alterner avec les limites\0"  // WebRotate
    "Afficher les limites toutes les (s)\0"  // WebRotateEvery
    "Garder les limites pendant (s)\0"  // WebRotateShow
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
    "Aucune limite reçue : lancez /miblo:pair dans Claude Code\0"  // WebLimitsHint
    "Ordinateurs appairés : %u\0"  // WebPairedCount
    "Version du firmware\0"  // WebVersion
    "Redémarrages rapides restants avant réinitialisation : %u\0"  // HardResetCountdown
    "Laissez-le allumé pour annuler\0"  // HardResetCancelHint
    "Réseau introuvable\0"  // NetNotFound
    "Utilisez un réseau 2,4 GHz\0"  // Use24GHz
    "Réseau introuvable. Miblo ne fonctionne qu'avec le Wi-Fi 2,4 GHz.\0"  // WebNotFound
    "Connecté ! Ouvrez Miblo sur :\0"  // WebConnectedAt
    "Connexion impossible. Vérifiez le réseau et réessayez.\0"  // WebConnectFailed
    "Réessayer\0"  // WebTryAgain
    "Miblo ne répond pas. Regardez l'écran de l'appareil.\0"  // WebNoReply
    "Connexion refusée\0"  // ConnRefused
    "Vérifiez le mot de passe ou WPA2\0"  // RefusedHint
    "Connexion impossible\0"  // JoinFailed
    "Code d'erreur %u\0"  // ErrorCode
    "Le routeur a refusé la connexion. Vérifiez le mot de passe. Si le routeur utilise WPA3 ou le mode \"WPA2/WPA3\", passez-le en WPA2 (Miblo ne prend pas en charge WPA3).\0"  // WebRefused
    "Connexion impossible (code %u). Vérifiez le réseau et réessayez.\0"  // WebFailedCode
    "Attend %u agent\0"  // WaitAgent1
    "Attend %u agents\0"  // WaitAgentsN
    "Attend %u tâche\0"  // WaitTask1
    "Attend %u tâches\0"  // WaitTasksN
    "5h\0"  // Short5h
    "7j\0"  // Short7d
    "Automatique\0"  // WebAuto
    "Compacte le contexte\0"  // Compacting
    "Connectez Miblo au même réseau Wi-Fi que votre ordinateur, sinon ils ne se trouveront pas.\0"  // WebSameNetwork
    "Mode nuit : baisser la luminosité\0"  // WebNight
    "Début de la nuit\0"  // WebNightFrom
    "Fin de la nuit\0"  // WebNightTo
    "Luminosité la nuit\0"  // WebNightBrightness
    "LIMITE LIBÉRÉE\0"  // LimitFreed
    "épuisée dans %s\0"  // RunsOutIn
    "AUJOURD'HUI\0"  // TodayTitle
    "réponses\0"  // SumResponses
    "de travail\0"  // SumWorked
    "dépensé\0"  // SumSpent
    "Mascotte\0"  // WebMascot
    "Sphynx (pêche)\0"  // WebMascotSphynx
    "Orange\0"  // WebMascotOrange
    "Noir\0"  // WebMascotBlack
    "Gris\0"  // WebMascotGrey
    "Mise à jour disponible\0"  // UpdateAvailable
    "v%s (vous avez v%s)\0"  // UpdateVersions
    "Rechercher des mises à jour\0"  // WebCheckUpdates
    "À jour (v%s)\0"  // WebUpToDate
    "La version %s est disponible.\0"  // WebNewVersion
    "Dans Claude Code, lancez /miblo:update. Ou téléchargez le fichier et installez-le sur la page de mise à jour du firmware.\0"  // WebUpdateHow
    "Impossible de rechercher les mises à jour (pas d'internet ?)\0"  // WebCheckFailed
    "Télécharger le firmware\0"  // WebDownloadBin
    "Éteindre l'écran quand personne ne l'utilise\0"  // WebSleep
    "Jamais (la mascotte continue de se promener)\0";  // WebSleepNever

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
    "Alterna con i limiti\0"  // WebRotate
    "Mostra i limiti ogni (s)\0"  // WebRotateEvery
    "Mantieni i limiti per (s)\0"  // WebRotateShow
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
    "Nessun limite ricevuto: esegui /miblo:pair in Claude Code\0"  // WebLimitsHint
    "Computer abbinati: %u\0"  // WebPairedCount
    "Versione firmware\0"  // WebVersion
    "Riavvii rapidi rimanenti per il reset: %u\0"  // HardResetCountdown
    "Lascialo acceso per annullare\0"  // HardResetCancelHint
    "Rete non trovata\0"  // NetNotFound
    "Usa una rete a 2,4 GHz\0"  // Use24GHz
    "Rete non trovata. Miblo funziona solo con Wi-Fi a 2,4 GHz.\0"  // WebNotFound
    "Connesso! Apri Miblo su:\0"  // WebConnectedAt
    "Impossibile connettersi. Controlla la rete e riprova.\0"  // WebConnectFailed
    "Riprova\0"  // WebTryAgain
    "Miblo non risponde. Guarda lo schermo del dispositivo.\0"  // WebNoReply
    "Connessione rifiutata\0"  // ConnRefused
    "Controlla la password o usa WPA2\0"  // RefusedHint
    "Impossibile connettersi\0"  // JoinFailed
    "Codice di errore %u\0"  // ErrorCode
    "Il router ha rifiutato la connessione. Controlla la password. Se il router usa WPA3 o la modalità \"WPA2/WPA3\", passa a WPA2 (Miblo non supporta WPA3).\0"  // WebRefused
    "Impossibile connettersi (codice %u). Controlla la rete e riprova.\0"  // WebFailedCode
    "In attesa di %u agente\0"  // WaitAgent1
    "In attesa di %u agenti\0"  // WaitAgentsN
    "In attesa di %u attività\0"  // WaitTask1
    "In attesa di %u attività\0"  // WaitTasksN
    "5h\0"  // Short5h
    "7g\0"  // Short7d
    "Automatica\0"  // WebAuto
    "Compatta il contesto\0"  // Compacting
    "Collega Miblo alla stessa rete Wi-Fi del tuo computer, altrimenti non si troveranno.\0"  // WebSameNetwork
    "Modalità notte: abbassa la luminosità\0"  // WebNight
    "Inizio della notte\0"  // WebNightFrom
    "Fine della notte\0"  // WebNightTo
    "Luminosità notturna\0"  // WebNightBrightness
    "LIMITE LIBERATO\0"  // LimitFreed
    "finisce tra %s\0"  // RunsOutIn
    "OGGI\0"  // TodayTitle
    "risposte\0"  // SumResponses
    "di lavoro\0"  // SumWorked
    "spesi\0"  // SumSpent
    "Mascotte\0"  // WebMascot
    "Sphynx (pesca)\0"  // WebMascotSphynx
    "Arancione\0"  // WebMascotOrange
    "Nero\0"  // WebMascotBlack
    "Grigio\0"  // WebMascotGrey
    "Aggiornamento disponibile\0"  // UpdateAvailable
    "v%s (hai la v%s)\0"  // UpdateVersions
    "Cerca aggiornamenti\0"  // WebCheckUpdates
    "Aggiornato (v%s)\0"  // WebUpToDate
    "La versione %s è disponibile.\0"  // WebNewVersion
    "In Claude Code, esegui /miblo:update. Oppure scarica il file e installalo nella pagina di aggiornamento del firmware.\0"  // WebUpdateHow
    "Impossibile cercare aggiornamenti (niente internet?)\0"  // WebCheckFailed
    "Scarica il firmware\0"  // WebDownloadBin
    "Spegni lo schermo quando nessuno lo usa\0"  // WebSleep
    "Mai (la mascotte continua a passeggiare)\0";  // WebSleepNever

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
    "Mit Limits abwechseln\0"  // WebRotate
    "Limits anzeigen alle (s)\0"  // WebRotateEvery
    "Limits anzeigen für (s)\0"  // WebRotateShow
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
    "Noch keine Limits empfangen: führe /miblo:pair in Claude Code aus\0"  // WebLimitsHint
    "Gekoppelte Computer: %u\0"  // WebPairedCount
    "Firmware-Version\0"  // WebVersion
    "Verbleibende schnelle Neustarts bis zum Zurücksetzen: %u\0"  // HardResetCountdown
    "Eingeschaltet lassen zum Abbrechen\0"  // HardResetCancelHint
    "Netzwerk nicht gefunden\0"  // NetNotFound
    "Nutze ein 2,4-GHz-WLAN\0"  // Use24GHz
    "Netzwerk nicht gefunden. Miblo funktioniert nur mit 2,4-GHz-WLAN.\0"  // WebNotFound
    "Verbunden! Öffne Miblo unter:\0"  // WebConnectedAt
    "Verbindung fehlgeschlagen. Prüfe das Netzwerk und versuche es erneut.\0"  // WebConnectFailed
    "Erneut versuchen\0"  // WebTryAgain
    "Keine Antwort von Miblo. Schau auf das Display des Geräts.\0"  // WebNoReply
    "Verbindung abgelehnt\0"  // ConnRefused
    "Passwort prüfen oder WPA2 nutzen\0"  // RefusedHint
    "Verbindung fehlgeschlagen\0"  // JoinFailed
    "Fehlercode %u\0"  // ErrorCode
    "Der Router hat die Verbindung abgelehnt. Prüfe das Passwort. Nutzt der Router WPA3 oder den Modus \"WPA2/WPA3\", stelle ihn auf WPA2 um (Miblo unterstützt kein WPA3).\0"  // WebRefused
    "Verbindung fehlgeschlagen (Code %u). Prüfe das Netzwerk und versuche es erneut.\0"  // WebFailedCode
    "Wartet auf %u Agent\0"  // WaitAgent1
    "Wartet auf %u Agenten\0"  // WaitAgentsN
    "Wartet auf %u Aufgabe\0"  // WaitTask1
    "Wartet auf %u Aufgaben\0"  // WaitTasksN
    "5h\0"  // Short5h
    "7T\0"  // Short7d
    "Automatisch\0"  // WebAuto
    "Kontext wird komprimiert\0"  // Compacting
    "Verbinde Miblo mit demselben WLAN wie deinen Computer, sonst finden sie sich nicht.\0"  // WebSameNetwork
    "Nachtmodus: Bildschirm dimmen\0"  // WebNight
    "Nacht beginnt um\0"  // WebNightFrom
    "Nacht endet um\0"  // WebNightTo
    "Helligkeit nachts\0"  // WebNightBrightness
    "LIMIT FREI\0"  // LimitFreed
    "reicht noch %s\0"  // RunsOutIn
    "HEUTE\0"  // TodayTitle
    "Antworten\0"  // SumResponses
    "gearbeitet\0"  // SumWorked
    "ausgegeben\0"  // SumSpent
    "Maskottchen\0"  // WebMascot
    "Sphynx (Pfirsich)\0"  // WebMascotSphynx
    "Orange\0"  // WebMascotOrange
    "Schwarz\0"  // WebMascotBlack
    "Grau\0"  // WebMascotGrey
    "Update verfügbar\0"  // UpdateAvailable
    "v%s (du hast v%s)\0"  // UpdateVersions
    "Nach Updates suchen\0"  // WebCheckUpdates
    "Aktuell (v%s)\0"  // WebUpToDate
    "Version %s ist verfügbar.\0"  // WebNewVersion
    "Führe in Claude Code /miblo:update aus. Oder lade die Datei herunter und installiere sie auf der Firmware-Update-Seite.\0"  // WebUpdateHow
    "Suche nach Updates fehlgeschlagen (kein Internet?)\0"  // WebCheckFailed
    "Firmware herunterladen\0"  // WebDownloadBin
    "Bildschirm ausschalten, wenn niemand ihn nutzt\0"  // WebSleep
    "Nie (das Maskottchen läuft weiter herum)\0";  // WebSleepNever

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
    "Чередовать с лимитами\0"  // WebRotate
    "Показывать лимиты каждые (с)\0"  // WebRotateEvery
    "Держать лимиты на экране (с)\0"  // WebRotateShow
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
    "Лимиты ещё не получены: выполните /miblo:pair в Claude Code\0"  // WebLimitsHint
    "Сопряжённых компьютеров: %u\0"  // WebPairedCount
    "Версия прошивки\0"  // WebVersion
    "Осталось быстрых перезапусков до сброса: %u\0"  // HardResetCountdown
    "Оставьте включённым для отмены\0"  // HardResetCancelHint
    "Сеть не найдена\0"  // NetNotFound
    "Нужна сеть 2,4 ГГц\0"  // Use24GHz
    "Сеть не найдена. Miblo работает только с Wi-Fi 2,4 ГГц.\0"  // WebNotFound
    "Подключено! Откройте Miblo:\0"  // WebConnectedAt
    "Не удалось подключиться. Проверьте сеть и попробуйте снова.\0"  // WebConnectFailed
    "Повторить\0"  // WebTryAgain
    "Miblo не отвечает. Смотрите на экран устройства.\0"  // WebNoReply
    "Подключение отклонено\0"  // ConnRefused
    "Проверьте пароль или WPA2\0"  // RefusedHint
    "Не удалось подключиться\0"  // JoinFailed
    "Код ошибки %u\0"  // ErrorCode
    "Роутер отклонил подключение. Проверьте пароль. Если роутер использует WPA3 или режим \"WPA2/WPA3\", переключите его на WPA2 (Miblo не поддерживает WPA3).\0"  // WebRefused
    "Не удалось подключиться (код %u). Проверьте сеть и попробуйте снова.\0"  // WebFailedCode
    "Ждёт агентов: %u\0"  // WaitAgent1
    "Ждёт агентов: %u\0"  // WaitAgentsN
    "Ждёт задач: %u\0"  // WaitTask1
    "Ждёт задач: %u\0"  // WaitTasksN
    "5ч\0"  // Short5h
    "7д\0"  // Short7d
    "Авто\0"  // WebAuto
    "Сжимает контекст\0"  // Compacting
    "Подключите Miblo к той же сети Wi-Fi, что и компьютер, иначе они не найдут друг друга.\0"  // WebSameNetwork
    "Ночной режим: приглушить экран\0"  // WebNight
    "Начало ночи\0"  // WebNightFrom
    "Конец ночи\0"  // WebNightTo
    "Яркость ночью\0"  // WebNightBrightness
    "ЛИМИТ ОБНОВЛЁН\0"  // LimitFreed
    "закончится через %s\0"  // RunsOutIn
    "СЕГОДНЯ\0"  // TodayTitle
    "ответов\0"  // SumResponses
    "в работе\0"  // SumWorked
    "потрачено\0"  // SumSpent
    "Талисман\0"  // WebMascot
    "Сфинкс (персик)\0"  // WebMascotSphynx
    "Рыжий\0"  // WebMascotOrange
    "Чёрный\0"  // WebMascotBlack
    "Серый\0"  // WebMascotGrey
    "Доступно обновление\0"  // UpdateAvailable
    "v%s (у вас v%s)\0"  // UpdateVersions
    "Проверить обновления\0"  // WebCheckUpdates
    "Всё актуально (v%s)\0"  // WebUpToDate
    "Доступна версия %s.\0"  // WebNewVersion
    "В Claude Code выполните /miblo:update. Или скачайте файл и установите его на странице обновления прошивки.\0"  // WebUpdateHow
    "Не удалось проверить обновления (нет интернета?)\0"  // WebCheckFailed
    "Скачать прошивку\0"  // WebDownloadBin
    "Выключать экран, когда им никто не пользуется\0"  // WebSleep
    "Никогда (талисман продолжает гулять)\0";  // WebSleepNever

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
    "与用量限制交替显示\0"  // WebRotate
    "用量限制显示间隔（秒）\0"  // WebRotateEvery
    "用量限制显示时长（秒）\0"  // WebRotateShow
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
    "尚未收到用量限制：请在 Claude Code 中运行 /miblo:pair\0"  // WebLimitsHint
    "已配对电脑：%u\0"  // WebPairedCount
    "固件版本\0"  // WebVersion
    "再快速重启 %u 次即可重置\0"  // HardResetCountdown
    "保持通电即可取消\0"  // HardResetCancelHint
    "未找到网络\0"  // NetNotFound
    "请使用 2.4 GHz 网络\0"  // Use24GHz
    "未找到网络。Miblo 仅支持 2.4 GHz Wi-Fi。\0"  // WebNotFound
    "已连接！在此打开 Miblo：\0"  // WebConnectedAt
    "无法连接。请检查网络后重试。\0"  // WebConnectFailed
    "重试\0"  // WebTryAgain
    "Miblo 无响应。请查看设备屏幕。\0"  // WebNoReply
    "连接被拒绝\0"  // ConnRefused
    "请检查密码或改用 WPA2\0"  // RefusedHint
    "无法连接\0"  // JoinFailed
    "错误代码 %u\0"  // ErrorCode
    "路由器拒绝了连接。请检查密码。如果路由器使用 WPA3 或 \"WPA2/WPA3\" 模式，请改为 WPA2（Miblo 不支持 WPA3）。\0"  // WebRefused
    "无法连接（代码 %u）。请检查网络后重试。\0"  // WebFailedCode
    "等待 %u 个子代理\0"  // WaitAgent1
    "等待 %u 个子代理\0"  // WaitAgentsN
    "等待 %u 个任务\0"  // WaitTask1
    "等待 %u 个任务\0"  // WaitTasksN
    "5h\0"  // Short5h
    "7天\0"  // Short7d
    "自动\0"  // WebAuto
    "正在压缩上下文\0"  // Compacting
    "请将 Miblo 连接到与电脑相同的 Wi-Fi 网络，否则它们无法互相找到。\0"  // WebSameNetwork
    "夜间模式：降低屏幕亮度\0"  // WebNight
    "夜间开始\0"  // WebNightFrom
    "夜间结束\0"  // WebNightTo
    "夜间亮度\0"  // WebNightBrightness
    "额度已恢复\0"  // LimitFreed
    "%s 后用完\0"  // RunsOutIn
    "今天\0"  // TodayTitle
    "次回复\0"  // SumResponses
    "工作时长\0"  // SumWorked
    "花费\0"  // SumSpent
    "吉祥物\0"  // WebMascot
    "斯芬克斯（桃色）\0"  // WebMascotSphynx
    "橙色\0"  // WebMascotOrange
    "黑色\0"  // WebMascotBlack
    "灰色\0"  // WebMascotGrey
    "有可用更新\0"  // UpdateAvailable
    "v%s（当前 v%s）\0"  // UpdateVersions
    "检查更新\0"  // WebCheckUpdates
    "已是最新（v%s）\0"  // WebUpToDate
    "版本 %s 可用。\0"  // WebNewVersion
    "在 Claude Code 中运行 /miblo:update，或下载文件并在固件更新页面安装。\0"  // WebUpdateHow
    "无法检查更新（没有网络？）\0"  // WebCheckFailed
    "下载固件\0"  // WebDownloadBin
    "无人使用时关闭屏幕\0"  // WebSleep
    "从不（吉祥物继续四处走动）\0";  // WebSleepNever

const char* const kLangTables[] = {kEn, kPtBR, kPtPT, kEs, kFr, kIt, kDe, kRu, kZh};

}  // namespace miblo
