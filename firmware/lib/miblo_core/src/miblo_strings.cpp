// String tables (UTF-8) — one packed string per language, entries separated by \0.
// The entry order exactly follows the miblo::S enum (miblo_i18n.h).
// test_i18n checks the count and the %s/%u placeholders for each language.
// The firmware carries a packed copy (miblo_strings_packed.cpp): after any change here, run
//   python3 firmware/scripts/pack_strings.py
// (test_i18n compares every packed string with this file and fails until then).
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
    "Blue light filter\0"  // WebBlue
    "Off\0"  // WebBlueOff
    "Always\0"  // WebBlueAlways
    "Scheduled\0"  // WebBlueScheduled
    "Starts at\0"  // WebBlueFrom
    "Ends at\0"  // WebBlueTo
    "Strength\0"  // WebBlueLevel
    "LIMIT FREED\0"  // LimitFreed
    "runs out in %s\0"  // RunsOutIn
    "TODAY\0"  // TodayTitle
    "responses\0"  // SumResponses
    "worked\0"  // SumWorked
    "spent\0"  // SumSpent
    "Colour\0"  // WebMascot
    "Peach\0"  // WebMascotSphynx
    "Orange\0"  // WebMascotOrange
    "Black\0"  // WebMascotBlack
    "Grey\0"  // WebMascotGrey
    "Pet\0"  // WebPet
    "Cat\0"  // WebPetCat
    "Rubber duck\0"  // WebPetDuck
    "Bug (beetle)\0"  // WebPetBug
    "Daemon (little ghost)\0"  // WebPetDaemon
    "Robot\0"  // WebPetRobot
    "Coffee mug\0"  // WebPetMug
    "Penguin\0"  // WebPetPenguin
    "Crab\0"  // WebPetCrab
    "Owl\0"  // WebPetOwl
    "Dog\0"  // WebPetDog
    "Alien\0"  // WebPetAlien
    "Riff\0"  // WebPetRiff
    "Body\0"  // WebSlotBody
    "Outline\0"  // WebSlotLine
    "Inner ears, tongue\0"  // WebSlotDetail
    "Nose, beak\0"  // WebSlotNose
    "Face lines\0"  // WebSlotLid
    "Eyes\0"  // WebSlotEye
    "Accent\0"  // WebSlotAccent
    "Eyes\0"  // WebEyeShape
    "Round\0"  // WebEyeRound
    "Big and shiny\0"  // WebEyeBig
    "Sleepy\0"  // WebEyeSleepy
    "Update available\0"  // UpdateAvailable
    "v%s (you have v%s)\0"  // UpdateVersions
    "Check for updates\0"  // WebCheckUpdates
    "Up to date (v%s)\0"  // WebUpToDate
    "Version %s is available.\0"  // WebNewVersion
    "In Claude Code, run /miblo:update. Or download the file and install it on the firmware update page.\0"  // WebUpdateHow
    "Couldn't check for updates (no internet?)\0"  // WebCheckFailed
    "Download the firmware\0"  // WebDownloadBin
    "Turn the screen off when idle\0"  // WebSleep
    "Never (the mascot keeps wandering)\0"  // WebSleepNever
    "The mascot starts wandering after\0"  // WebPetAfter
    "Alert blinks\0"  // WebFlashBlinks
    "Hi! I'm\0"  // HelloIAm
    "Good morning\0"  // GoodMorning
    "Good afternoon\0"  // GoodAfternoon
    "Good evening\0"  // GoodEvening
    "Happy birthday\0"  // HappyBirthday
    "It's my birthday!\0"  // MyBirthday
    "Merry Christmas!\0"  // MerryChristmas
    "Happy New Year!\0"  // HappyNewYear
    "Hi, %s!\0"  // FriendHi
    "%s came to visit!\0"  // FriendVisiting
    "Visiting %s\0"  // FriendAway
    "%s brought you coffee\0"  // FriendCoffee
    "Napping with %s\0"  // FriendNap
    "Your name (so Miblo can greet you)\0"  // WebOwner
    "Your birthday (day / month)\0"  // WebBirthday
    "Play with other Miblos on the network\0"  // WebFriends
    "Screen\0"  // WebSecScreen
    "About you\0"  // WebSecYou
    "Device\0"  // WebSecDevice
    "Advanced\0"  // WebAdvanced
    "System\0"  // WebSystem
    "Processing\0"  // WebCpu
    "Memory (RAM)\0"  // WebRam
    "Program (firmware)\0"  // WebProgram
    "%s in use · %s free of %s\0"  // WebInUse
    "Data\0"  // WebData
    "Paired computers\0"  // WebComputers
    "Remove\0"  // WebRemove
    "Remove %s? It stops updating this Miblo until it is paired again with /miblo:pair.\0"  // WebRemoveConfirm
    "this computer\0"  // WebThisComputer
    "active now\0"  // WebActiveNow
    "seen %s ago\0"  // WebSeenAgo
    "not seen since the Miblo started\0"  // WebNotSeen
    "Rename\0"  // WebRename
    "Empty: the computer's own name\0"  // WebRenameHint
    "Check the highlighted field\0"  // WebCheckField
    "Code to change settings\0"  // CodeSettings
    "To change settings, type the code shown on the gadget screen.\0"  // WebUnlock
    "Code on the screen\0"  // WebUnlockTitle
    "Wrong code\0"  // WebUnlockBad
    "Rubber ducking with %s\0"  // FriendDuck
    "Pair programming with %s\0"  // FriendPair
    "%s reviewed it: LGTM!\0"  // FriendReview
    "Hunting a bug with %s\0"  // FriendBug
    "Friday deploy with %s!\0"  // FriendDeploy
    "High five with %s!\0"  // FriendHighFive
    "Ping-pong with %s\0"  // FriendPingPong
    "Dancing with %s\0"  // FriendDance
    "Pizza with %s\0"  // FriendPizza
    "Release cake with %s!\0"  // FriendCake
    "Merge conflict with %s\0"  // FriendMerge
    "Daily standup with %s\0"  // FriendStandup
    "Hackathon with %s\0"  // FriendHackathon
    "Selfie with %s!\0"  // FriendSelfie
    "Playing chess with %s\0"  // FriendChess
    "Gaming with %s\0"  // FriendGame
    "%s has gossip\0"  // FriendGossip
    "Cheers with %s!\0"  // FriendToast
    "Movie time with %s\0"  // FriendMovie
    "Stacking blocks with %s\0"  // FriendBlocks
    "Brainstorming with %s\0"  // FriendBrainstorm
    "Pomodoro with %s\0"  // FriendPomodoro
    "Hotfix with %s!\0"  // FriendHotfix
    "All tests green with %s\0"  // FriendTests
    "404 hunt with %s\0"  // FriendNotFound
    "Ship it with %s!\0"  // FriendShipIt
    "Sprint with %s\0"  // FriendSprint
    "Origami with %s\0"  // FriendOrigami
    "Stack Underflow days, %s\0"  // FriendNostalgia
    "Kernel panic with %s!\0"  // FriendPanic
    "Picnic with %s\0"  // FriendPicnic
    "Fishing with %s\0"  // FriendFishing
    "Under an umbrella with %s\0"  // FriendUmbrella
    "Tin can phone with %s\0"  // FriendCanPhone
    "Flying a kite with %s\0"  // FriendKite
    "The other Miblos are on my\0"  // WebFriendsSide
    "Right\0"  // WebSideRight
    "Left\0"  // WebSideLeft
    "Top\0"  // WebSideUp
    "Bottom\0"  // WebSideDown
    "%s got busy\0"  // FriendBusy
    "focus until %s\0"  // FocusUntil
    "Break time\0"  // FocusBreak
    "Back to focus?\0"  // FocusBack
    "Long break\0"  // FocusLongBreak
    "How about a %u min break?\0"  // NudgeBreak
    "Time to drink water\0"  // NudgeWater
    "Look far away\0"  // NudgeEyes
    "Have a good rest!\0"  // RestWell
    "Have a good rest, %s!\0"  // RestWellName
    "LAST WEEK\0"  // LastWeekTitle
    "busiest day: %s\0"  // BusiestDay
    "in a meeting\0"  // InMeeting
    "A session needs you\0"  // ASessionNeedsYou
    "%s finished after %s\0"  // FinishedAfter
    "Finished after %s\0"  // FinishedAfterAnon
    "at this pace, runs out at %s\0"  // RunsOutAt
    "runs out ~%s\0"  // RunsOutShort
    "Time's up!\0"  // TimesUp
    "%s in %u days\0"  // CountdownDays
    "%s tomorrow\0"  // CountdownTomorrow
    "%s is today!\0"  // CountdownToday
    "Here I am!\0"  // FindMe
    "Happy Programmer's Day!\0"  // HappyProgrammersDay
    "Wellness\0"  // WebSecWellness
    "Break after long work (min, 0 = off)\0"  // WebBreakAfter
    "Drink water every (min, 0 = off)\0"  // WebWater
    "Eye rest (20-20-20)\0"  // WebEyes
    "Break length (min)\0"  // WebBreakLen
    "Eye rest every (min)\0"  // WebEyesEvery
    "Look away for (s)\0"  // WebEyesSec
    "Work hours\0"  // WebWorkHours
    "Work days\0"  // WebWorkDays
    "End of day summary\0"  // WebEndOfDay
    "Celebrate long tasks\0"  // WebFanfare
    "During focus, only \"needs you\" alerts\0"  // WebFocusQuiet
    "Insist more on long waits\0"  // WebInsist
    "Status frame around the screen\0"  // WebFrame
    "Second time zone\0"  // WebTz2
    "Its name on screen\0"  // WebTz2Label
    "Settings QR on the desk screen\0"  // WebDeskQr
    "Monday: last week's summary\0";  // WebWeekly

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
    "Filtro de luz azul\0"  // WebBlue
    "Desligado\0"  // WebBlueOff
    "Sempre\0"  // WebBlueAlways
    "Programado\0"  // WebBlueScheduled
    "Começa às\0"  // WebBlueFrom
    "Termina às\0"  // WebBlueTo
    "Intensidade\0"  // WebBlueLevel
    "LIMITE LIBERADO\0"  // LimitFreed
    "acaba em %s\0"  // RunsOutIn
    "HOJE\0"  // TodayTitle
    "respostas\0"  // SumResponses
    "trabalhando\0"  // SumWorked
    "gasto\0"  // SumSpent
    "Cor\0"  // WebMascot
    "Pêssego\0"  // WebMascotSphynx
    "Laranja\0"  // WebMascotOrange
    "Preto\0"  // WebMascotBlack
    "Cinza\0"  // WebMascotGrey
    "Bichinho\0"  // WebPet
    "Gato\0"  // WebPetCat
    "Patinho de borracha\0"  // WebPetDuck
    "Bug (besouro)\0"  // WebPetBug
    "Daemon (fantasminha)\0"  // WebPetDaemon
    "Robô\0"  // WebPetRobot
    "Caneca de café\0"  // WebPetMug
    "Pinguim\0"  // WebPetPenguin
    "Caranguejo\0"  // WebPetCrab
    "Coruja\0"  // WebPetOwl
    "Cachorro\0"  // WebPetDog
    "Alienígena\0"  // WebPetAlien
    "Riff\0"  // WebPetRiff
    "Corpo\0"  // WebSlotBody
    "Contorno\0"  // WebSlotLine
    "Orelhas por dentro, língua\0"  // WebSlotDetail
    "Nariz, bico\0"  // WebSlotNose
    "Traços do rosto\0"  // WebSlotLid
    "Olhos\0"  // WebSlotEye
    "Detalhe\0"  // WebSlotAccent
    "Olhos\0"  // WebEyeShape
    "Redondos\0"  // WebEyeRound
    "Grandes e brilhantes\0"  // WebEyeBig
    "Sonolentos\0"  // WebEyeSleepy
    "Atualização disponível\0"  // UpdateAvailable
    "v%s (você tem v%s)\0"  // UpdateVersions
    "Buscar atualizações\0"  // WebCheckUpdates
    "Tudo atualizado (v%s)\0"  // WebUpToDate
    "A versão %s está disponível.\0"  // WebNewVersion
    "No Claude Code, rode /miblo:update. Ou baixe o arquivo e instale na página de atualização de firmware.\0"  // WebUpdateHow
    "Não foi possível buscar atualizações (sem internet?)\0"  // WebCheckFailed
    "Baixar o firmware\0"  // WebDownloadBin
    "Desligar a tela quando ninguém estiver usando\0"  // WebSleep
    "Nunca (o mascote fica passeando)\0"  // WebSleepNever
    "O mascote começa a passear após\0"  // WebPetAfter
    "Piscadas do alerta\0"  // WebFlashBlinks
    "Oi! Meu nome é\0"  // HelloIAm
    "Bom dia\0"  // GoodMorning
    "Boa tarde\0"  // GoodAfternoon
    "Boa noite\0"  // GoodEvening
    "Feliz aniversário\0"  // HappyBirthday
    "Hoje é meu aniversário!\0"  // MyBirthday
    "Feliz Natal!\0"  // MerryChristmas
    "Feliz Ano Novo!\0"  // HappyNewYear
    "Oi, %s!\0"  // FriendHi
    "%s veio visitar!\0"  // FriendVisiting
    "Visitando %s\0"  // FriendAway
    "%s trouxe um café\0"  // FriendCoffee
    "Soneca com %s\0"  // FriendNap
    "Seu nome (para o Miblo te cumprimentar)\0"  // WebOwner
    "Seu aniversário (dia / mês)\0"  // WebBirthday
    "Brincar com outros Miblos da rede\0"  // WebFriends
    "Tela\0"  // WebSecScreen
    "Sobre você\0"  // WebSecYou
    "Aparelho\0"  // WebSecDevice
    "Avançado\0"  // WebAdvanced
    "Sistema\0"  // WebSystem
    "Processamento\0"  // WebCpu
    "Memória (RAM)\0"  // WebRam
    "Programa (firmware)\0"  // WebProgram
    "%s em uso · %s livres de %s\0"  // WebInUse
    "Dados\0"  // WebData
    "Computadores pareados\0"  // WebComputers
    "Remover\0"  // WebRemove
    "Remover %s? Ele deixa de atualizar este Miblo até ser pareado de novo com /miblo:pair.\0"  // WebRemoveConfirm
    "este computador\0"  // WebThisComputer
    "ativo agora\0"  // WebActiveNow
    "visto há %s\0"  // WebSeenAgo
    "sem contato desde que o Miblo ligou\0"  // WebNotSeen
    "Renomear\0"  // WebRename
    "Vazio: o nome do próprio computador\0"  // WebRenameHint
    "Confira o campo destacado\0"  // WebCheckField
    "Código para mudar as configurações\0"  // CodeSettings
    "Para mudar as configurações, digite o código que aparece na tela do aparelho.\0"  // WebUnlock
    "Código na tela\0"  // WebUnlockTitle
    "Código incorreto\0"  // WebUnlockBad
    "Debug de pato com %s\0"  // FriendDuck
    "Pair programming com %s\0"  // FriendPair
    "%s revisou: LGTM!\0"  // FriendReview
    "Caçando um bug com %s\0"  // FriendBug
    "Deploy na sexta com %s!\0"  // FriendDeploy
    "Toca aqui com %s!\0"  // FriendHighFive
    "Ping-pong com %s\0"  // FriendPingPong
    "Dançando com %s\0"  // FriendDance
    "Pizza com %s\0"  // FriendPizza
    "Bolo de release com %s!\0"  // FriendCake
    "Merge conflict com %s\0"  // FriendMerge
    "Daily com %s\0"  // FriendStandup
    "Hackathon com %s\0"  // FriendHackathon
    "Selfie com %s!\0"  // FriendSelfie
    "Xadrez com %s\0"  // FriendChess
    "Jogando com %s\0"  // FriendGame
    "%s tem fofoca\0"  // FriendGossip
    "Um brinde com %s!\0"  // FriendToast
    "Cinema com %s\0"  // FriendMovie
    "Torre de blocos com %s\0"  // FriendBlocks
    "Brainstorm com %s\0"  // FriendBrainstorm
    "Pomodoro com %s\0"  // FriendPomodoro
    "Hotfix com %s!\0"  // FriendHotfix
    "Testes verdes com %s\0"  // FriendTests
    "Caçando o 404 com %s\0"  // FriendNotFound
    "Ship it com %s!\0"  // FriendShipIt
    "Sprint com %s\0"  // FriendSprint
    "Origami com %s\0"  // FriendOrigami
    "Stack Underflow raiz, %s\0"  // FriendNostalgia
    "Kernel panic com %s!\0"  // FriendPanic
    "Piquenique com %s\0"  // FriendPicnic
    "Pescando com %s\0"  // FriendFishing
    "Guarda-chuva com %s\0"  // FriendUmbrella
    "Telefone de lata com %s\0"  // FriendCanPhone
    "Soltando pipa com %s\0"  // FriendKite
    "Os outros Miblos ficam\0"  // WebFriendsSide
    "À minha direita\0"  // WebSideRight
    "À minha esquerda\0"  // WebSideLeft
    "Em cima\0"  // WebSideUp
    "Embaixo\0"  // WebSideDown
    "%s ficou ocupado\0"  // FriendBusy
    "em foco até %s\0"  // FocusUntil
    "Hora da pausa\0"  // FocusBreak
    "De volta ao foco?\0"  // FocusBack
    "Pausa longa\0"  // FocusLongBreak
    "Que tal uma pausa de %u min?\0"  // NudgeBreak
    "Hora de beber água\0"  // NudgeWater
    "Olhe para longe\0"  // NudgeEyes
    "Bom descanso!\0"  // RestWell
    "Bom descanso, %s!\0"  // RestWellName
    "SEMANA PASSADA\0"  // LastWeekTitle
    "dia mais puxado: %s\0"  // BusiestDay
    "em reunião\0"  // InMeeting
    "Uma sessão precisa de você\0"  // ASessionNeedsYou
    "%s terminou após %s\0"  // FinishedAfter
    "Terminou após %s\0"  // FinishedAfterAnon
    "no ritmo atual, acaba às %s\0"  // RunsOutAt
    "acaba ~%s\0"  // RunsOutShort
    "Acabou o tempo!\0"  // TimesUp
    "%s em %u dias\0"  // CountdownDays
    "%s amanhã\0"  // CountdownTomorrow
    "%s é hoje!\0"  // CountdownToday
    "Estou aqui!\0"  // FindMe
    "Feliz dia do programador!\0"  // HappyProgrammersDay
    "Bem-estar\0"  // WebSecWellness
    "Pausa depois de muito trabalho (min, 0 = desligado)\0"  // WebBreakAfter
    "Beber água a cada (min, 0 = desligado)\0"  // WebWater
    "Descanso dos olhos (20-20-20)\0"  // WebEyes
    "Duração da pausa (min)\0"  // WebBreakLen
    "Descanso dos olhos a cada (min)\0"  // WebEyesEvery
    "Olhar para longe por (s)\0"  // WebEyesSec
    "Horário de trabalho\0"  // WebWorkHours
    "Dias de trabalho\0"  // WebWorkDays
    "Resumo no fim do expediente\0"  // WebEndOfDay
    "Comemorar tarefas longas\0"  // WebFanfare
    "Durante o foco, só avisos de \"precisa de você\"\0"  // WebFocusQuiet
    "Insistir mais quando a espera for longa\0"  // WebInsist
    "Moldura de estado na tela\0"  // WebFrame
    "Outro fuso\0"  // WebTz2
    "Nome na tela\0"  // WebTz2Label
    "QR das configurações na tela da mesa\0"  // WebDeskQr
    "Segunda: resumo da semana\0";  // WebWeekly

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
    "Filtro de luz azul\0"  // WebBlue
    "Desligado\0"  // WebBlueOff
    "Sempre\0"  // WebBlueAlways
    "Programado\0"  // WebBlueScheduled
    "Começa às\0"  // WebBlueFrom
    "Termina às\0"  // WebBlueTo
    "Intensidade\0"  // WebBlueLevel
    "LIMITE LIBERTADO\0"  // LimitFreed
    "acaba em %s\0"  // RunsOutIn
    "HOJE\0"  // TodayTitle
    "respostas\0"  // SumResponses
    "a trabalhar\0"  // SumWorked
    "gasto\0"  // SumSpent
    "Cor\0"  // WebMascot
    "Pêssego\0"  // WebMascotSphynx
    "Laranja\0"  // WebMascotOrange
    "Preto\0"  // WebMascotBlack
    "Cinzento\0"  // WebMascotGrey
    "Animal de estimação\0"  // WebPet
    "Gato\0"  // WebPetCat
    "Patinho de borracha\0"  // WebPetDuck
    "Bug (escaravelho)\0"  // WebPetBug
    "Daemon (fantasminha)\0"  // WebPetDaemon
    "Robô\0"  // WebPetRobot
    "Caneca de café\0"  // WebPetMug
    "Pinguim\0"  // WebPetPenguin
    "Caranguejo\0"  // WebPetCrab
    "Mocho\0"  // WebPetOwl
    "Cão\0"  // WebPetDog
    "Extraterrestre\0"  // WebPetAlien
    "Riff\0"  // WebPetRiff
    "Corpo\0"  // WebSlotBody
    "Contorno\0"  // WebSlotLine
    "Interior das orelhas, língua\0"  // WebSlotDetail
    "Nariz, bico\0"  // WebSlotNose
    "Traços do rosto\0"  // WebSlotLid
    "Olhos\0"  // WebSlotEye
    "Detalhe\0"  // WebSlotAccent
    "Olhos\0"  // WebEyeShape
    "Redondos\0"  // WebEyeRound
    "Grandes e brilhantes\0"  // WebEyeBig
    "Ensonados\0"  // WebEyeSleepy
    "Atualização disponível\0"  // UpdateAvailable
    "v%s (tem a v%s)\0"  // UpdateVersions
    "Procurar atualizações\0"  // WebCheckUpdates
    "Tudo atualizado (v%s)\0"  // WebUpToDate
    "A versão %s está disponível.\0"  // WebNewVersion
    "No Claude Code, execute /miblo:update. Ou descarregue o ficheiro e instale-o na página de atualização de firmware.\0"  // WebUpdateHow
    "Não foi possível procurar atualizações (sem internet?)\0"  // WebCheckFailed
    "Descarregar o firmware\0"  // WebDownloadBin
    "Desligar o ecrã quando ninguém estiver a usar\0"  // WebSleep
    "Nunca (a mascote fica a passear)\0"  // WebSleepNever
    "A mascote começa a passear após\0"  // WebPetAfter
    "Piscadelas do alerta\0"  // WebFlashBlinks
    "Olá! Chamo-me\0"  // HelloIAm
    "Bom dia\0"  // GoodMorning
    "Boa tarde\0"  // GoodAfternoon
    "Boa noite\0"  // GoodEvening
    "Feliz aniversário\0"  // HappyBirthday
    "Hoje faço anos!\0"  // MyBirthday
    "Feliz Natal!\0"  // MerryChristmas
    "Feliz Ano Novo!\0"  // HappyNewYear
    "Olá, %s!\0"  // FriendHi
    "%s veio de visita!\0"  // FriendVisiting
    "De visita a %s\0"  // FriendAway
    "%s trouxe-te um café\0"  // FriendCoffee
    "Sesta com %s\0"  // FriendNap
    "O seu nome (para o Miblo o cumprimentar)\0"  // WebOwner
    "O seu aniversário (dia / mês)\0"  // WebBirthday
    "Brincar com outros Miblos da rede\0"  // WebFriends
    "Ecrã\0"  // WebSecScreen
    "Sobre si\0"  // WebSecYou
    "Aparelho\0"  // WebSecDevice
    "Avançado\0"  // WebAdvanced
    "Sistema\0"  // WebSystem
    "Processamento\0"  // WebCpu
    "Memória (RAM)\0"  // WebRam
    "Programa (firmware)\0"  // WebProgram
    "%s em uso · %s livres de %s\0"  // WebInUse
    "Dados\0"  // WebData
    "Computadores emparelhados\0"  // WebComputers
    "Remover\0"  // WebRemove
    "Remover %s? Deixa de atualizar este Miblo até ser emparelhado de novo com /miblo:pair.\0"  // WebRemoveConfirm
    "este computador\0"  // WebThisComputer
    "ativo agora\0"  // WebActiveNow
    "visto há %s\0"  // WebSeenAgo
    "sem contacto desde que o Miblo ligou\0"  // WebNotSeen
    "Mudar o nome\0"  // WebRename
    "Vazio: o nome do próprio computador\0"  // WebRenameHint
    "Verifique o campo destacado\0"  // WebCheckField
    "Código para alterar as definições\0"  // CodeSettings
    "Para alterar as definições, introduza o código no ecrã do aparelho.\0"  // WebUnlock
    "Código no ecrã\0"  // WebUnlockTitle
    "Código incorreto\0"  // WebUnlockBad
    "Debug com o pato e %s\0"  // FriendDuck
    "Pair programming com %s\0"  // FriendPair
    "%s reviu: LGTM!\0"  // FriendReview
    "À caça de um bug com %s\0"  // FriendBug
    "Deploy à sexta com %s!\0"  // FriendDeploy
    "Dá cá cinco, %s!\0"  // FriendHighFive
    "Pingue-pongue com %s\0"  // FriendPingPong
    "A dançar com %s\0"  // FriendDance
    "Pizza com %s\0"  // FriendPizza
    "Bolo de release com %s!\0"  // FriendCake
    "Merge conflict com %s\0"  // FriendMerge
    "Daily com %s\0"  // FriendStandup
    "Hackathon com %s\0"  // FriendHackathon
    "Selfie com %s!\0"  // FriendSelfie
    "Xadrez com %s\0"  // FriendChess
    "A jogar com %s\0"  // FriendGame
    "%s tem mexericos\0"  // FriendGossip
    "Um brinde com %s!\0"  // FriendToast
    "Cinema com %s\0"  // FriendMovie
    "A empilhar blocos com %s\0"  // FriendBlocks
    "Brainstorm com %s\0"  // FriendBrainstorm
    "Pomodoro com %s\0"  // FriendPomodoro
    "Hotfix com %s!\0"  // FriendHotfix
    "Testes verdes com %s\0"  // FriendTests
    "À caça do 404 com %s\0"  // FriendNotFound
    "Ship it com %s!\0"  // FriendShipIt
    "Sprint com %s\0"  // FriendSprint
    "Origami com %s\0"  // FriendOrigami
    "Stack Underflow com %s\0"  // FriendNostalgia
    "Kernel panic com %s!\0"  // FriendPanic
    "Piquenique com %s\0"  // FriendPicnic
    "À pesca com %s\0"  // FriendFishing
    "Guarda-chuva com %s\0"  // FriendUmbrella
    "Telefone de lata com %s\0"  // FriendCanPhone
    "Papagaio de papel com %s\0"  // FriendKite
    "Os outros Miblos ficam\0"  // WebFriendsSide
    "À minha direita\0"  // WebSideRight
    "À minha esquerda\0"  // WebSideLeft
    "Em cima\0"  // WebSideUp
    "Em baixo\0"  // WebSideDown
    "%s ficou ocupado\0"  // FriendBusy
    "em foco até às %s\0"  // FocusUntil
    "Hora da pausa\0"  // FocusBreak
    "De volta ao foco?\0"  // FocusBack
    "Pausa longa\0"  // FocusLongBreak
    "Que tal uma pausa de %u min?\0"  // NudgeBreak
    "Hora de beber água\0"  // NudgeWater
    "Olhe para longe\0"  // NudgeEyes
    "Bom descanso!\0"  // RestWell
    "Bom descanso, %s!\0"  // RestWellName
    "SEMANA PASSADA\0"  // LastWeekTitle
    "dia mais intenso: %s\0"  // BusiestDay
    "em reunião\0"  // InMeeting
    "Uma sessão precisa de si\0"  // ASessionNeedsYou
    "%s terminou após %s\0"  // FinishedAfter
    "Terminou após %s\0"  // FinishedAfterAnon
    "a este ritmo, acaba às %s\0"  // RunsOutAt
    "acaba ~%s\0"  // RunsOutShort
    "Acabou o tempo!\0"  // TimesUp
    "%s daqui a %u dias\0"  // CountdownDays
    "%s amanhã\0"  // CountdownTomorrow
    "%s é hoje!\0"  // CountdownToday
    "Estou aqui!\0"  // FindMe
    "Feliz Dia do Programador!\0"  // HappyProgrammersDay
    "Bem-estar\0"  // WebSecWellness
    "Pausa depois de muito trabalho (min, 0 = desligado)\0"  // WebBreakAfter
    "Beber água a cada (min, 0 = desligado)\0"  // WebWater
    "Descanso dos olhos (20-20-20)\0"  // WebEyes
    "Duração da pausa (min)\0"  // WebBreakLen
    "Descanso dos olhos a cada (min)\0"  // WebEyesEvery
    "Olhar para longe durante (s)\0"  // WebEyesSec
    "Horário de trabalho\0"  // WebWorkHours
    "Dias de trabalho\0"  // WebWorkDays
    "Resumo no fim do dia de trabalho\0"  // WebEndOfDay
    "Celebrar tarefas longas\0"  // WebFanfare
    "Durante o foco, só avisos de \"precisa de si\"\0"  // WebFocusQuiet
    "Insistir mais quando a espera for longa\0"  // WebInsist
    "Moldura de estado no ecrã\0"  // WebFrame
    "Outro fuso horário\0"  // WebTz2
    "Nome no ecrã\0"  // WebTz2Label
    "QR das definições no ecrã da secretária\0"  // WebDeskQr
    "Segunda: resumo da semana\0";  // WebWeekly

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
    "Filtro de luz azul\0"  // WebBlue
    "Desactivado\0"  // WebBlueOff
    "Siempre\0"  // WebBlueAlways
    "Programado\0"  // WebBlueScheduled
    "Empieza a las\0"  // WebBlueFrom
    "Termina a las\0"  // WebBlueTo
    "Intensidad\0"  // WebBlueLevel
    "LÍMITE LIBERADO\0"  // LimitFreed
    "se agota en %s\0"  // RunsOutIn
    "HOY\0"  // TodayTitle
    "respuestas\0"  // SumResponses
    "trabajando\0"  // SumWorked
    "gastado\0"  // SumSpent
    "Color\0"  // WebMascot
    "Melocotón\0"  // WebMascotSphynx
    "Naranja\0"  // WebMascotOrange
    "Negro\0"  // WebMascotBlack
    "Gris\0"  // WebMascotGrey
    "Mascota\0"  // WebPet
    "Gato\0"  // WebPetCat
    "Patito de goma\0"  // WebPetDuck
    "Bug (escarabajo)\0"  // WebPetBug
    "Daemon (fantasmita)\0"  // WebPetDaemon
    "Robot\0"  // WebPetRobot
    "Taza de café\0"  // WebPetMug
    "Pingüino\0"  // WebPetPenguin
    "Cangrejo\0"  // WebPetCrab
    "Búho\0"  // WebPetOwl
    "Perro\0"  // WebPetDog
    "Alienígena\0"  // WebPetAlien
    "Riff\0"  // WebPetRiff
    "Cuerpo\0"  // WebSlotBody
    "Contorno\0"  // WebSlotLine
    "Orejas por dentro, lengua\0"  // WebSlotDetail
    "Nariz, pico\0"  // WebSlotNose
    "Trazos de la cara\0"  // WebSlotLid
    "Ojos\0"  // WebSlotEye
    "Detalle\0"  // WebSlotAccent
    "Ojos\0"  // WebEyeShape
    "Redondos\0"  // WebEyeRound
    "Grandes y brillantes\0"  // WebEyeBig
    "Somnolientos\0"  // WebEyeSleepy
    "Actualización disponible\0"  // UpdateAvailable
    "v%s (tienes v%s)\0"  // UpdateVersions
    "Buscar actualizaciones\0"  // WebCheckUpdates
    "Todo actualizado (v%s)\0"  // WebUpToDate
    "La versión %s está disponible.\0"  // WebNewVersion
    "En Claude Code, ejecuta /miblo:update. O descarga el archivo e instálalo en la página de actualización de firmware.\0"  // WebUpdateHow
    "No se pudo buscar actualizaciones (¿sin internet?)\0"  // WebCheckFailed
    "Descargar el firmware\0"  // WebDownloadBin
    "Apagar la pantalla cuando nadie la use\0"  // WebSleep
    "Nunca (la mascota sigue paseando)\0"  // WebSleepNever
    "La mascota empieza a pasear tras\0"  // WebPetAfter
    "Parpadeos de la alerta\0"  // WebFlashBlinks
    "¡Hola! Me llamo\0"  // HelloIAm
    "Buenos días\0"  // GoodMorning
    "Buenas tardes\0"  // GoodAfternoon
    "Buenas noches\0"  // GoodEvening
    "Feliz cumpleaños\0"  // HappyBirthday
    "¡Hoy es mi cumpleaños!\0"  // MyBirthday
    "¡Feliz Navidad!\0"  // MerryChristmas
    "¡Feliz Año Nuevo!\0"  // HappyNewYear
    "¡Hola, %s!\0"  // FriendHi
    "¡%s vino de visita!\0"  // FriendVisiting
    "Visitando a %s\0"  // FriendAway
    "%s te trajo un café\0"  // FriendCoffee
    "Siesta con %s\0"  // FriendNap
    "Tu nombre (para que Miblo te salude)\0"  // WebOwner
    "Tu cumpleaños (día / mes)\0"  // WebBirthday
    "Jugar con otros Miblos de la red\0"  // WebFriends
    "Pantalla\0"  // WebSecScreen
    "Sobre ti\0"  // WebSecYou
    "Dispositivo\0"  // WebSecDevice
    "Avanzado\0"  // WebAdvanced
    "Sistema\0"  // WebSystem
    "Procesamiento\0"  // WebCpu
    "Memoria (RAM)\0"  // WebRam
    "Programa (firmware)\0"  // WebProgram
    "%s en uso · %s libres de %s\0"  // WebInUse
    "Datos\0"  // WebData
    "Equipos emparejados\0"  // WebComputers
    "Quitar\0"  // WebRemove
    "¿Quitar %s? Dejará de actualizar este Miblo hasta que se empareje de nuevo con /miblo:pair.\0"  // WebRemoveConfirm
    "este equipo\0"  // WebThisComputer
    "activo ahora\0"  // WebActiveNow
    "visto hace %s\0"  // WebSeenAgo
    "sin contacto desde que el Miblo se encendió\0"  // WebNotSeen
    "Renombrar\0"  // WebRename
    "Vacío: el nombre propio del equipo\0"  // WebRenameHint
    "Revisa el campo resaltado\0"  // WebCheckField
    "Código para cambiar los ajustes\0"  // CodeSettings
    "Para cambiar los ajustes, escribe el código que aparece en la pantalla.\0"  // WebUnlock
    "Código en la pantalla\0"  // WebUnlockTitle
    "Código incorrecto\0"  // WebUnlockBad
    "%s trajo el patito de goma\0"  // FriendDuck
    "Pair programming con %s\0"  // FriendPair
    "%s lo revisó: LGTM!\0"  // FriendReview
    "Cazando un bug con %s\0"  // FriendBug
    "¡Deploy viernes con %s!\0"  // FriendDeploy
    "¡Choca esos cinco, %s!\0"  // FriendHighFive
    "Ping-pong con %s\0"  // FriendPingPong
    "Bailando con %s\0"  // FriendDance
    "Pizza con %s\0"  // FriendPizza
    "¡Pastel de release con %s!\0"  // FriendCake
    "Merge conflict con %s\0"  // FriendMerge
    "Daily con %s\0"  // FriendStandup
    "Hackathon con %s\0"  // FriendHackathon
    "¡Selfie con %s!\0"  // FriendSelfie
    "Ajedrez con %s\0"  // FriendChess
    "Jugando con %s\0"  // FriendGame
    "%s trae chismes\0"  // FriendGossip
    "¡Brindis con %s!\0"  // FriendToast
    "Cine con %s\0"  // FriendMovie
    "Apilando bloques con %s\0"  // FriendBlocks
    "Brainstorming con %s\0"  // FriendBrainstorm
    "Pomodoro con %s\0"  // FriendPomodoro
    "¡Hotfix con %s!\0"  // FriendHotfix
    "Tests en verde con %s\0"  // FriendTests
    "Buscando el 404 con %s\0"  // FriendNotFound
    "¡Ship it con %s!\0"  // FriendShipIt
    "Sprint con %s\0"  // FriendSprint
    "Origami con %s\0"  // FriendOrigami
    "Stack Underflow con %s\0"  // FriendNostalgia
    "¡Kernel panic con %s!\0"  // FriendPanic
    "Pícnic con %s\0"  // FriendPicnic
    "Pescando con %s\0"  // FriendFishing
    "Bajo el paraguas con %s\0"  // FriendUmbrella
    "Teléfono de lata con %s\0"  // FriendCanPhone
    "Volando cometa con %s\0"  // FriendKite
    "Los otros Miblos están\0"  // WebFriendsSide
    "A mi derecha\0"  // WebSideRight
    "A mi izquierda\0"  // WebSideLeft
    "Arriba\0"  // WebSideUp
    "Abajo\0"  // WebSideDown
    "%s se puso a trabajar\0"  // FriendBusy
    "en foco hasta las %s\0"  // FocusUntil
    "Hora del descanso\0"  // FocusBreak
    "¿Volvemos al foco?\0"  // FocusBack
    "Descanso largo\0"  // FocusLongBreak
    "¿Qué tal un descanso de %u min?\0"  // NudgeBreak
    "Hora de beber agua\0"  // NudgeWater
    "Mira a lo lejos\0"  // NudgeEyes
    "¡Buen descanso!\0"  // RestWell
    "¡Buen descanso, %s!\0"  // RestWellName
    "SEMANA PASADA\0"  // LastWeekTitle
    "día más intenso: %s\0"  // BusiestDay
    "en reunión\0"  // InMeeting
    "Una sesión te necesita\0"  // ASessionNeedsYou
    "%s terminó tras %s\0"  // FinishedAfter
    "Terminó tras %s\0"  // FinishedAfterAnon
    "a este ritmo, se agota a las %s\0"  // RunsOutAt
    "se agota ~%s\0"  // RunsOutShort
    "¡Se acabó el tiempo!\0"  // TimesUp
    "%s en %u días\0"  // CountdownDays
    "%s mañana\0"  // CountdownTomorrow
    "%s es hoy!\0"  // CountdownToday
    "¡Aquí estoy!\0"  // FindMe
    "¡Feliz Día del Programador!\0"  // HappyProgrammersDay
    "Bienestar\0"  // WebSecWellness
    "Pausa tras mucho trabajo (min, 0 = desactivado)\0"  // WebBreakAfter
    "Beber agua cada (min, 0 = desactivado)\0"  // WebWater
    "Descanso visual (20-20-20)\0"  // WebEyes
    "Duración de la pausa (min)\0"  // WebBreakLen
    "Descanso visual cada (min)\0"  // WebEyesEvery
    "Mirar a lo lejos durante (s)\0"  // WebEyesSec
    "Horario laboral\0"  // WebWorkHours
    "Días laborables\0"  // WebWorkDays
    "Resumen al final de la jornada\0"  // WebEndOfDay
    "Celebrar tareas largas\0"  // WebFanfare
    "Durante el foco, solo avisos de \"te necesita\"\0"  // WebFocusQuiet
    "Insistir más en esperas largas\0"  // WebInsist
    "Marco de estado en la pantalla\0"  // WebFrame
    "Otra zona horaria\0"  // WebTz2
    "Nombre en pantalla\0"  // WebTz2Label
    "QR de ajustes en la pantalla del escritorio\0"  // WebDeskQr
    "Lunes: resumen de la semana\0";  // WebWeekly

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
    "Filtre de lumière bleue\0"  // WebBlue
    "Désactivé\0"  // WebBlueOff
    "Toujours\0"  // WebBlueAlways
    "Programmé\0"  // WebBlueScheduled
    "Début\0"  // WebBlueFrom
    "Fin\0"  // WebBlueTo
    "Intensité\0"  // WebBlueLevel
    "LIMITE LIBÉRÉE\0"  // LimitFreed
    "épuisée dans %s\0"  // RunsOutIn
    "AUJOURD'HUI\0"  // TodayTitle
    "réponses\0"  // SumResponses
    "de travail\0"  // SumWorked
    "dépensé\0"  // SumSpent
    "Couleur\0"  // WebMascot
    "Pêche\0"  // WebMascotSphynx
    "Orange\0"  // WebMascotOrange
    "Noir\0"  // WebMascotBlack
    "Gris\0"  // WebMascotGrey
    "Compagnon\0"  // WebPet
    "Chat\0"  // WebPetCat
    "Canard en caoutchouc\0"  // WebPetDuck
    "Bug (scarabée)\0"  // WebPetBug
    "Daemon (petit fantôme)\0"  // WebPetDaemon
    "Robot\0"  // WebPetRobot
    "Tasse de café\0"  // WebPetMug
    "Manchot\0"  // WebPetPenguin
    "Crabe\0"  // WebPetCrab
    "Hibou\0"  // WebPetOwl
    "Chien\0"  // WebPetDog
    "Alien\0"  // WebPetAlien
    "Riff\0"  // WebPetRiff
    "Corps\0"  // WebSlotBody
    "Contour\0"  // WebSlotLine
    "Intérieur des oreilles, langue\0"  // WebSlotDetail
    "Nez, bec\0"  // WebSlotNose
    "Traits du visage\0"  // WebSlotLid
    "Yeux\0"  // WebSlotEye
    "Détail\0"  // WebSlotAccent
    "Yeux\0"  // WebEyeShape
    "Ronds\0"  // WebEyeRound
    "Grands et brillants\0"  // WebEyeBig
    "Endormis\0"  // WebEyeSleepy
    "Mise à jour disponible\0"  // UpdateAvailable
    "v%s (vous avez v%s)\0"  // UpdateVersions
    "Rechercher des mises à jour\0"  // WebCheckUpdates
    "À jour (v%s)\0"  // WebUpToDate
    "La version %s est disponible.\0"  // WebNewVersion
    "Dans Claude Code, lancez /miblo:update. Ou téléchargez le fichier et installez-le sur la page de mise à jour du firmware.\0"  // WebUpdateHow
    "Impossible de rechercher les mises à jour (pas d'internet ?)\0"  // WebCheckFailed
    "Télécharger le firmware\0"  // WebDownloadBin
    "Éteindre l'écran quand personne ne l'utilise\0"  // WebSleep
    "Jamais (la mascotte continue de se promener)\0"  // WebSleepNever
    "La mascotte commence à se promener après\0"  // WebPetAfter
    "Clignotements de l'alerte\0"  // WebFlashBlinks
    "Salut ! Je m'appelle\0"  // HelloIAm
    "Bonjour\0"  // GoodMorning
    "Bon après-midi\0"  // GoodAfternoon
    "Bonsoir\0"  // GoodEvening
    "Joyeux anniversaire\0"  // HappyBirthday
    "C'est mon anniversaire !\0"  // MyBirthday
    "Joyeux Noël !\0"  // MerryChristmas
    "Bonne année !\0"  // HappyNewYear
    "Salut, %s !\0"  // FriendHi
    "%s est venu me voir !\0"  // FriendVisiting
    "En visite chez %s\0"  // FriendAway
    "%s t'a apporté un café\0"  // FriendCoffee
    "Sieste avec %s\0"  // FriendNap
    "Votre prénom (pour que Miblo vous salue)\0"  // WebOwner
    "Votre anniversaire (jour / mois)\0"  // WebBirthday
    "Jouer avec les autres Miblos du réseau\0"  // WebFriends
    "Écran\0"  // WebSecScreen
    "À propos de vous\0"  // WebSecYou
    "Appareil\0"  // WebSecDevice
    "Avancé\0"  // WebAdvanced
    "Système\0"  // WebSystem
    "Processeur\0"  // WebCpu
    "Mémoire (RAM)\0"  // WebRam
    "Programme (firmware)\0"  // WebProgram
    "%s utilisés · %s libres sur %s\0"  // WebInUse
    "Données\0"  // WebData
    "Ordinateurs associés\0"  // WebComputers
    "Retirer\0"  // WebRemove
    "Retirer %s ? Il ne mettra plus ce Miblo à jour jusqu'à un nouvel appairage avec /miblo:pair.\0"  // WebRemoveConfirm
    "cet ordinateur\0"  // WebThisComputer
    "actif maintenant\0"  // WebActiveNow
    "vu il y a %s\0"  // WebSeenAgo
    "pas vu depuis le démarrage du Miblo\0"  // WebNotSeen
    "Renommer\0"  // WebRename
    "Vide : le nom de l'ordinateur\0"  // WebRenameHint
    "Vérifiez le champ en surbrillance\0"  // WebCheckField
    "Code pour modifier les réglages\0"  // CodeSettings
    "Pour modifier les réglages, saisissez le code affiché à l'écran.\0"  // WebUnlock
    "Code à l'écran\0"  // WebUnlockTitle
    "Code incorrect\0"  // WebUnlockBad
    "%s a apporté le canard\0"  // FriendDuck
    "Pair programming avec %s\0"  // FriendPair
    "%s a relu : LGTM !\0"  // FriendReview
    "Chasse au bug avec %s\0"  // FriendBug
    "Prod le vendredi avec %s !\0"  // FriendDeploy
    "Tope là avec %s !\0"  // FriendHighFive
    "Ping-pong avec %s\0"  // FriendPingPong
    "Danse avec %s\0"  // FriendDance
    "Pizza avec %s\0"  // FriendPizza
    "Gâteau de release, %s !\0"  // FriendCake
    "Merge conflict avec %s\0"  // FriendMerge
    "Daily avec %s\0"  // FriendStandup
    "Hackathon avec %s\0"  // FriendHackathon
    "Selfie avec %s !\0"  // FriendSelfie
    "Échecs avec %s\0"  // FriendChess
    "Jeu vidéo avec %s\0"  // FriendGame
    "%s a des potins\0"  // FriendGossip
    "Tchin-tchin avec %s !\0"  // FriendToast
    "Ciné avec %s\0"  // FriendMovie
    "Tour de blocs avec %s\0"  // FriendBlocks
    "Brainstorming avec %s\0"  // FriendBrainstorm
    "Pomodoro avec %s\0"  // FriendPomodoro
    "Hotfix avec %s !\0"  // FriendHotfix
    "Tests au vert avec %s\0"  // FriendTests
    "Chasse au 404 avec %s\0"  // FriendNotFound
    "Ship it avec %s !\0"  // FriendShipIt
    "Sprint avec %s\0"  // FriendSprint
    "Origami avec %s\0"  // FriendOrigami
    "Stack Underflow avec %s\0"  // FriendNostalgia
    "Kernel panic avec %s !\0"  // FriendPanic
    "Pique-nique avec %s\0"  // FriendPicnic
    "Pêche avec %s\0"  // FriendFishing
    "Sous le parapluie avec %s\0"  // FriendUmbrella
    "Pots de yaourt avec %s\0"  // FriendCanPhone
    "Cerf-volant avec %s\0"  // FriendKite
    "Les autres Miblos sont\0"  // WebFriendsSide
    "À ma droite\0"  // WebSideRight
    "À ma gauche\0"  // WebSideLeft
    "Au-dessus\0"  // WebSideUp
    "En dessous\0"  // WebSideDown
    "%s est occupé\0"  // FriendBusy
    "concentré jusqu'à %s\0"  // FocusUntil
    "C'est la pause\0"  // FocusBreak
    "On s'y remet ?\0"  // FocusBack
    "Longue pause\0"  // FocusLongBreak
    "Une pause de %u min ?\0"  // NudgeBreak
    "Buvez un verre d'eau\0"  // NudgeWater
    "Regardez au loin\0"  // NudgeEyes
    "Bon repos !\0"  // RestWell
    "Bon repos, %s !\0"  // RestWellName
    "SEMAINE DERNIÈRE\0"  // LastWeekTitle
    "jour le plus chargé : %s\0"  // BusiestDay
    "en réunion\0"  // InMeeting
    "Une session a besoin de vous\0"  // ASessionNeedsYou
    "%s a terminé après %s\0"  // FinishedAfter
    "Terminé après %s\0"  // FinishedAfterAnon
    "à ce rythme, épuisé à %s\0"  // RunsOutAt
    "épuisé ~%s\0"  // RunsOutShort
    "C'est l'heure !\0"  // TimesUp
    "%s dans %u jours\0"  // CountdownDays
    "%s demain\0"  // CountdownTomorrow
    "%s, c'est aujourd'hui !\0"  // CountdownToday
    "Je suis là !\0"  // FindMe
    "Bonne fête des programmeurs !\0"  // HappyProgrammersDay
    "Bien-être\0"  // WebSecWellness
    "Pause après un long travail (min, 0 = désactivé)\0"  // WebBreakAfter
    "Boire de l'eau toutes les (min, 0 = désactivé)\0"  // WebWater
    "Repos des yeux (20-20-20)\0"  // WebEyes
    "Durée de la pause (min)\0"  // WebBreakLen
    "Repos des yeux toutes les (min)\0"  // WebEyesEvery
    "Regarder au loin pendant (s)\0"  // WebEyesSec
    "Heures de travail\0"  // WebWorkHours
    "Jours de travail\0"  // WebWorkDays
    "Résumé en fin de journée\0"  // WebEndOfDay
    "Fêter les longues tâches\0"  // WebFanfare
    "Pendant le focus, seulement « besoin de vous »\0"  // WebFocusQuiet
    "Insister plus si l'attente est longue\0"  // WebInsist
    "Cadre d'état autour de l'écran\0"  // WebFrame
    "Second fuseau horaire\0"  // WebTz2
    "Nom à l'écran\0"  // WebTz2Label
    "QR des réglages sur l'écran du bureau\0"  // WebDeskQr
    "Lundi : bilan de la semaine\0";  // WebWeekly

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
    "Filtro luce blu\0"  // WebBlue
    "Disattivato\0"  // WebBlueOff
    "Sempre\0"  // WebBlueAlways
    "Programmato\0"  // WebBlueScheduled
    "Inizia alle\0"  // WebBlueFrom
    "Finisce alle\0"  // WebBlueTo
    "Intensità\0"  // WebBlueLevel
    "LIMITE LIBERATO\0"  // LimitFreed
    "finisce tra %s\0"  // RunsOutIn
    "OGGI\0"  // TodayTitle
    "risposte\0"  // SumResponses
    "di lavoro\0"  // SumWorked
    "spesi\0"  // SumSpent
    "Colore\0"  // WebMascot
    "Pesca\0"  // WebMascotSphynx
    "Arancione\0"  // WebMascotOrange
    "Nero\0"  // WebMascotBlack
    "Grigio\0"  // WebMascotGrey
    "Animaletto\0"  // WebPet
    "Gatto\0"  // WebPetCat
    "Paperella di gomma\0"  // WebPetDuck
    "Bug (scarabeo)\0"  // WebPetBug
    "Daemon (fantasmino)\0"  // WebPetDaemon
    "Robot\0"  // WebPetRobot
    "Tazza di caffè\0"  // WebPetMug
    "Pinguino\0"  // WebPetPenguin
    "Granchio\0"  // WebPetCrab
    "Gufo\0"  // WebPetOwl
    "Cane\0"  // WebPetDog
    "Alieno\0"  // WebPetAlien
    "Riff\0"  // WebPetRiff
    "Corpo\0"  // WebSlotBody
    "Contorno\0"  // WebSlotLine
    "Interno orecchie, lingua\0"  // WebSlotDetail
    "Naso, becco\0"  // WebSlotNose
    "Tratti del viso\0"  // WebSlotLid
    "Occhi\0"  // WebSlotEye
    "Dettaglio\0"  // WebSlotAccent
    "Occhi\0"  // WebEyeShape
    "Rotondi\0"  // WebEyeRound
    "Grandi e luminosi\0"  // WebEyeBig
    "Assonnati\0"  // WebEyeSleepy
    "Aggiornamento disponibile\0"  // UpdateAvailable
    "v%s (hai la v%s)\0"  // UpdateVersions
    "Cerca aggiornamenti\0"  // WebCheckUpdates
    "Aggiornato (v%s)\0"  // WebUpToDate
    "La versione %s è disponibile.\0"  // WebNewVersion
    "In Claude Code, esegui /miblo:update. Oppure scarica il file e installalo nella pagina di aggiornamento del firmware.\0"  // WebUpdateHow
    "Impossibile cercare aggiornamenti (niente internet?)\0"  // WebCheckFailed
    "Scarica il firmware\0"  // WebDownloadBin
    "Spegni lo schermo quando nessuno lo usa\0"  // WebSleep
    "Mai (la mascotte continua a passeggiare)\0"  // WebSleepNever
    "La mascotte inizia a passeggiare dopo\0"  // WebPetAfter
    "Lampeggi dell'avviso\0"  // WebFlashBlinks
    "Ciao! Mi chiamo\0"  // HelloIAm
    "Buongiorno\0"  // GoodMorning
    "Buon pomeriggio\0"  // GoodAfternoon
    "Buonasera\0"  // GoodEvening
    "Buon compleanno\0"  // HappyBirthday
    "Oggi è il mio compleanno!\0"  // MyBirthday
    "Buon Natale!\0"  // MerryChristmas
    "Felice anno nuovo!\0"  // HappyNewYear
    "Ciao, %s!\0"  // FriendHi
    "%s è venuto a trovarmi!\0"  // FriendVisiting
    "In visita da %s\0"  // FriendAway
    "%s ti ha portato un caffè\0"  // FriendCoffee
    "Pisolino con %s\0"  // FriendNap
    "Il tuo nome (così Miblo ti saluta)\0"  // WebOwner
    "Il tuo compleanno (giorno / mese)\0"  // WebBirthday
    "Giocare con gli altri Miblo della rete\0"  // WebFriends
    "Schermo\0"  // WebSecScreen
    "Su di te\0"  // WebSecYou
    "Dispositivo\0"  // WebSecDevice
    "Avanzate\0"  // WebAdvanced
    "Sistema\0"  // WebSystem
    "Elaborazione\0"  // WebCpu
    "Memoria (RAM)\0"  // WebRam
    "Programma (firmware)\0"  // WebProgram
    "%s in uso · %s liberi su %s\0"  // WebInUse
    "Dati\0"  // WebData
    "Computer associati\0"  // WebComputers
    "Rimuovi\0"  // WebRemove
    "Rimuovere %s? Smetterà di aggiornare questo Miblo finché non verrà associato di nuovo con /miblo:pair.\0"  // WebRemoveConfirm
    "questo computer\0"  // WebThisComputer
    "attivo ora\0"  // WebActiveNow
    "visto %s fa\0"  // WebSeenAgo
    "non visto dall'accensione del Miblo\0"  // WebNotSeen
    "Rinomina\0"  // WebRename
    "Vuoto: il nome del computer\0"  // WebRenameHint
    "Controlla il campo evidenziato\0"  // WebCheckField
    "Codice per cambiare le impostazioni\0"  // CodeSettings
    "Per cambiare le impostazioni, digita il codice mostrato sullo schermo.\0"  // WebUnlock
    "Codice sullo schermo\0"  // WebUnlockTitle
    "Codice errato\0"  // WebUnlockBad
    "%s ha portato la papera\0"  // FriendDuck
    "Pair programming con %s\0"  // FriendPair
    "%s ha revisionato: LGTM!\0"  // FriendReview
    "A caccia di bug con %s\0"  // FriendBug
    "Deploy di venerdì con %s!\0"  // FriendDeploy
    "Batti cinque con %s!\0"  // FriendHighFive
    "Ping-pong con %s\0"  // FriendPingPong
    "Ballando con %s\0"  // FriendDance
    "Pizza con %s\0"  // FriendPizza
    "Torta di release con %s!\0"  // FriendCake
    "Merge conflict con %s\0"  // FriendMerge
    "Daily con %s\0"  // FriendStandup
    "Hackathon con %s\0"  // FriendHackathon
    "Selfie con %s!\0"  // FriendSelfie
    "Scacchi con %s\0"  // FriendChess
    "Videogiochi con %s\0"  // FriendGame
    "%s ha un pettegolezzo\0"  // FriendGossip
    "Cin cin con %s!\0"  // FriendToast
    "Film con %s\0"  // FriendMovie
    "Torre di blocchi con %s\0"  // FriendBlocks
    "Brainstorming con %s\0"  // FriendBrainstorm
    "Pomodoro con %s\0"  // FriendPomodoro
    "Hotfix con %s!\0"  // FriendHotfix
    "Test verdi con %s\0"  // FriendTests
    "A caccia del 404 con %s\0"  // FriendNotFound
    "Ship it con %s!\0"  // FriendShipIt
    "Sprint con %s\0"  // FriendSprint
    "Origami con %s\0"  // FriendOrigami
    "Stack Underflow con %s\0"  // FriendNostalgia
    "Kernel panic con %s!\0"  // FriendPanic
    "Picnic con %s\0"  // FriendPicnic
    "A pesca con %s\0"  // FriendFishing
    "Sotto l'ombrello con %s\0"  // FriendUmbrella
    "Telefono di latta con %s\0"  // FriendCanPhone
    "Aquilone con %s\0"  // FriendKite
    "Gli altri Miblo sono\0"  // WebFriendsSide
    "Alla mia destra\0"  // WebSideRight
    "Alla mia sinistra\0"  // WebSideLeft
    "Sopra\0"  // WebSideUp
    "Sotto\0"  // WebSideDown
    "%s è impegnato\0"  // FriendBusy
    "focus fino alle %s\0"  // FocusUntil
    "Pausa\0"  // FocusBreak
    "Si torna al lavoro?\0"  // FocusBack
    "Pausa lunga\0"  // FocusLongBreak
    "Che ne dici di %u min di pausa?\0"  // NudgeBreak
    "È ora di bere acqua\0"  // NudgeWater
    "Guarda lontano\0"  // NudgeEyes
    "Buon riposo!\0"  // RestWell
    "Buon riposo, %s!\0"  // RestWellName
    "SETTIMANA SCORSA\0"  // LastWeekTitle
    "giorno più intenso: %s\0"  // BusiestDay
    "in riunione\0"  // InMeeting
    "Una sessione ha bisogno di te\0"  // ASessionNeedsYou
    "%s ha finito dopo %s\0"  // FinishedAfter
    "Finito dopo %s\0"  // FinishedAfterAnon
    "a questo ritmo, finisce alle %s\0"  // RunsOutAt
    "finisce ~%s\0"  // RunsOutShort
    "Tempo scaduto!\0"  // TimesUp
    "%s tra %u giorni\0"  // CountdownDays
    "%s domani\0"  // CountdownTomorrow
    "%s è oggi!\0"  // CountdownToday
    "Sono qui!\0"  // FindMe
    "Buona festa dei programmatori!\0"  // HappyProgrammersDay
    "Benessere\0"  // WebSecWellness
    "Pausa dopo tanto lavoro (min, 0 = spento)\0"  // WebBreakAfter
    "Bere acqua ogni (min, 0 = spento)\0"  // WebWater
    "Riposo degli occhi (20-20-20)\0"  // WebEyes
    "Durata della pausa (min)\0"  // WebBreakLen
    "Riposo degli occhi ogni (min)\0"  // WebEyesEvery
    "Guardare lontano per (s)\0"  // WebEyesSec
    "Orario di lavoro\0"  // WebWorkHours
    "Giorni lavorativi\0"  // WebWorkDays
    "Riepilogo a fine giornata\0"  // WebEndOfDay
    "Festeggia i compiti lunghi\0"  // WebFanfare
    "Durante il focus, solo avvisi \"tocca a te\"\0"  // WebFocusQuiet
    "Insisti di più nelle attese lunghe\0"  // WebInsist
    "Cornice di stato sullo schermo\0"  // WebFrame
    "Secondo fuso orario\0"  // WebTz2
    "Nome sullo schermo\0"  // WebTz2Label
    "QR delle impostazioni sulla scrivania\0"  // WebDeskQr
    "Lunedì: riepilogo della settimana\0";  // WebWeekly

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
    "Blaulichtfilter\0"  // WebBlue
    "Aus\0"  // WebBlueOff
    "Immer\0"  // WebBlueAlways
    "Nach Zeitplan\0"  // WebBlueScheduled
    "Beginnt um\0"  // WebBlueFrom
    "Endet um\0"  // WebBlueTo
    "Stärke\0"  // WebBlueLevel
    "LIMIT FREI\0"  // LimitFreed
    "reicht noch %s\0"  // RunsOutIn
    "HEUTE\0"  // TodayTitle
    "Antworten\0"  // SumResponses
    "gearbeitet\0"  // SumWorked
    "ausgegeben\0"  // SumSpent
    "Farbe\0"  // WebMascot
    "Pfirsich\0"  // WebMascotSphynx
    "Orange\0"  // WebMascotOrange
    "Schwarz\0"  // WebMascotBlack
    "Grau\0"  // WebMascotGrey
    "Haustier\0"  // WebPet
    "Katze\0"  // WebPetCat
    "Quietscheente\0"  // WebPetDuck
    "Bug (Käfer)\0"  // WebPetBug
    "Daemon (Gespenst)\0"  // WebPetDaemon
    "Roboter\0"  // WebPetRobot
    "Kaffeetasse\0"  // WebPetMug
    "Pinguin\0"  // WebPetPenguin
    "Krabbe\0"  // WebPetCrab
    "Eule\0"  // WebPetOwl
    "Hund\0"  // WebPetDog
    "Alien\0"  // WebPetAlien
    "Riff\0"  // WebPetRiff
    "Körper\0"  // WebSlotBody
    "Umriss\0"  // WebSlotLine
    "Innenohren, Zunge\0"  // WebSlotDetail
    "Nase, Schnabel\0"  // WebSlotNose
    "Gesichtslinien\0"  // WebSlotLid
    "Augen\0"  // WebSlotEye
    "Akzent\0"  // WebSlotAccent
    "Augen\0"  // WebEyeShape
    "Rund\0"  // WebEyeRound
    "Groß und glänzend\0"  // WebEyeBig
    "Verschlafen\0"  // WebEyeSleepy
    "Update verfügbar\0"  // UpdateAvailable
    "v%s (du hast v%s)\0"  // UpdateVersions
    "Nach Updates suchen\0"  // WebCheckUpdates
    "Aktuell (v%s)\0"  // WebUpToDate
    "Version %s ist verfügbar.\0"  // WebNewVersion
    "Führe in Claude Code /miblo:update aus. Oder lade die Datei herunter und installiere sie auf der Firmware-Update-Seite.\0"  // WebUpdateHow
    "Suche nach Updates fehlgeschlagen (kein Internet?)\0"  // WebCheckFailed
    "Firmware herunterladen\0"  // WebDownloadBin
    "Bildschirm ausschalten, wenn niemand ihn nutzt\0"  // WebSleep
    "Nie (das Maskottchen läuft weiter herum)\0"  // WebSleepNever
    "Das Maskottchen läuft los nach\0"  // WebPetAfter
    "Blinken bei Hinweisen\0"  // WebFlashBlinks
    "Hallo! Ich bin\0"  // HelloIAm
    "Guten Morgen\0"  // GoodMorning
    "Guten Tag\0"  // GoodAfternoon
    "Guten Abend\0"  // GoodEvening
    "Alles Gute zum Geburtstag\0"  // HappyBirthday
    "Heute ist mein Geburtstag!\0"  // MyBirthday
    "Frohe Weihnachten!\0"  // MerryChristmas
    "Frohes neues Jahr!\0"  // HappyNewYear
    "Hallo, %s!\0"  // FriendHi
    "%s ist zu Besuch!\0"  // FriendVisiting
    "Zu Besuch bei %s\0"  // FriendAway
    "%s bringt dir Kaffee\0"  // FriendCoffee
    "Schläfchen mit %s\0"  // FriendNap
    "Dein Name (damit Miblo dich begrüßt)\0"  // WebOwner
    "Dein Geburtstag (Tag / Monat)\0"  // WebBirthday
    "Mit anderen Miblos im Netzwerk spielen\0"  // WebFriends
    "Bildschirm\0"  // WebSecScreen
    "Über dich\0"  // WebSecYou
    "Gerät\0"  // WebSecDevice
    "Erweitert\0"  // WebAdvanced
    "System\0"  // WebSystem
    "Prozessor\0"  // WebCpu
    "Arbeitsspeicher (RAM)\0"  // WebRam
    "Programm (Firmware)\0"  // WebProgram
    "%s belegt · %s frei von %s\0"  // WebInUse
    "Daten\0"  // WebData
    "Gekoppelte Computer\0"  // WebComputers
    "Entfernen\0"  // WebRemove
    "%s entfernen? Er aktualisiert diesen Miblo erst wieder nach erneutem Koppeln mit /miblo:pair.\0"  // WebRemoveConfirm
    "dieser Computer\0"  // WebThisComputer
    "gerade aktiv\0"  // WebActiveNow
    "vor %s gesehen\0"  // WebSeenAgo
    "seit dem Start des Miblo nicht gesehen\0"  // WebNotSeen
    "Umbenennen\0"  // WebRename
    "Leer: der eigene Name des Computers\0"  // WebRenameHint
    "Prüfe das markierte Feld\0"  // WebCheckField
    "Code zum Ändern der Einstellungen\0"  // CodeSettings
    "Zum Ändern der Einstellungen den Code auf dem Bildschirm eingeben.\0"  // WebUnlock
    "Code auf dem Bildschirm\0"  // WebUnlockTitle
    "Falscher Code\0"  // WebUnlockBad
    "Enten-Debugging mit %s\0"  // FriendDuck
    "Pair Programming mit %s\0"  // FriendPair
    "%s hat reviewt: LGTM!\0"  // FriendReview
    "Bugjagd mit %s\0"  // FriendBug
    "Freitags-Deploy mit %s!\0"  // FriendDeploy
    "Abklatschen mit %s!\0"  // FriendHighFive
    "Tischtennis mit %s\0"  // FriendPingPong
    "Tanzen mit %s\0"  // FriendDance
    "Pizza mit %s\0"  // FriendPizza
    "Release-Kuchen mit %s!\0"  // FriendCake
    "Merge-Konflikt mit %s\0"  // FriendMerge
    "Daily Stand-up mit %s\0"  // FriendStandup
    "Hackathon mit %s\0"  // FriendHackathon
    "Selfie mit %s!\0"  // FriendSelfie
    "Schach mit %s\0"  // FriendChess
    "Zocken mit %s\0"  // FriendGame
    "%s hat Tratsch\0"  // FriendGossip
    "Prost mit %s!\0"  // FriendToast
    "Kino mit %s\0"  // FriendMovie
    "Klötzchen stapeln mit %s\0"  // FriendBlocks
    "Brainstorming mit %s\0"  // FriendBrainstorm
    "Pomodoro mit %s\0"  // FriendPomodoro
    "Hotfix mit %s!\0"  // FriendHotfix
    "Alle Tests grün mit %s\0"  // FriendTests
    "404-Suche mit %s\0"  // FriendNotFound
    "Ship it mit %s!\0"  // FriendShipIt
    "Sprint mit %s\0"  // FriendSprint
    "Origami mit %s\0"  // FriendOrigami
    "Stack Underflow mit %s\0"  // FriendNostalgia
    "Kernel Panic mit %s!\0"  // FriendPanic
    "Picknick mit %s\0"  // FriendPicnic
    "Angeln mit %s\0"  // FriendFishing
    "Unterm Schirm mit %s\0"  // FriendUmbrella
    "Dosentelefon mit %s\0"  // FriendCanPhone
    "Drachen steigen mit %s\0"  // FriendKite
    "Die anderen Miblos stehen\0"  // WebFriendsSide
    "Rechts von mir\0"  // WebSideRight
    "Links von mir\0"  // WebSideLeft
    "Oben\0"  // WebSideUp
    "Unten\0"  // WebSideDown
    "%s ist beschäftigt\0"  // FriendBusy
    "Fokus bis %s\0"  // FocusUntil
    "Pausenzeit\0"  // FocusBreak
    "Zurück zum Fokus?\0"  // FocusBack
    "Lange Pause\0"  // FocusLongBreak
    "Wie wär's mit %u Min. Pause?\0"  // NudgeBreak
    "Zeit, Wasser zu trinken\0"  // NudgeWater
    "Schau in die Ferne\0"  // NudgeEyes
    "Erhol dich gut!\0"  // RestWell
    "Erhol dich gut, %s!\0"  // RestWellName
    "LETZTE WOCHE\0"  // LastWeekTitle
    "stärkster Tag: %s\0"  // BusiestDay
    "im Meeting\0"  // InMeeting
    "Eine Sitzung braucht dich\0"  // ASessionNeedsYou
    "%s fertig nach %s\0"  // FinishedAfter
    "Fertig nach %s\0"  // FinishedAfterAnon
    "bei diesem Tempo leer um %s\0"  // RunsOutAt
    "leer ~%s\0"  // RunsOutShort
    "Zeit ist um!\0"  // TimesUp
    "%s in %u Tagen\0"  // CountdownDays
    "%s morgen\0"  // CountdownTomorrow
    "%s ist heute!\0"  // CountdownToday
    "Hier bin ich!\0"  // FindMe
    "Frohen Programmierertag!\0"  // HappyProgrammersDay
    "Wohlbefinden\0"  // WebSecWellness
    "Pause nach langer Arbeit (min, 0 = aus)\0"  // WebBreakAfter
    "Wasser trinken alle (min, 0 = aus)\0"  // WebWater
    "Augenpause (20-20-20)\0"  // WebEyes
    "Pausenlänge (min)\0"  // WebBreakLen
    "Augenpause alle (min)\0"  // WebEyesEvery
    "In die Ferne schauen für (s)\0"  // WebEyesSec
    "Arbeitszeit\0"  // WebWorkHours
    "Arbeitstage\0"  // WebWorkDays
    "Zusammenfassung zum Feierabend\0"  // WebEndOfDay
    "Lange Aufgaben feiern\0"  // WebFanfare
    "Im Fokus nur Hinweise \"braucht dich\"\0"  // WebFocusQuiet
    "Bei langem Warten mehr drängen\0"  // WebInsist
    "Statusrahmen um den Bildschirm\0"  // WebFrame
    "Zweite Zeitzone\0"  // WebTz2
    "Name auf dem Bildschirm\0"  // WebTz2Label
    "Einstellungs-QR auf dem Schreibtisch\0"  // WebDeskQr
    "Montag: Wochenrückblick\0";  // WebWeekly

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
    "Фильтр синего света\0"  // WebBlue
    "Выключен\0"  // WebBlueOff
    "Всегда\0"  // WebBlueAlways
    "По расписанию\0"  // WebBlueScheduled
    "Начало\0"  // WebBlueFrom
    "Конец\0"  // WebBlueTo
    "Интенсивность\0"  // WebBlueLevel
    "ЛИМИТ ОБНОВЛЁН\0"  // LimitFreed
    "закончится через %s\0"  // RunsOutIn
    "СЕГОДНЯ\0"  // TodayTitle
    "ответов\0"  // SumResponses
    "в работе\0"  // SumWorked
    "потрачено\0"  // SumSpent
    "Цвет\0"  // WebMascot
    "Персиковый\0"  // WebMascotSphynx
    "Рыжий\0"  // WebMascotOrange
    "Чёрный\0"  // WebMascotBlack
    "Серый\0"  // WebMascotGrey
    "Питомец\0"  // WebPet
    "Кошка\0"  // WebPetCat
    "Резиновая уточка\0"  // WebPetDuck
    "Жучок\0"  // WebPetBug
    "Демон (привидение)\0"  // WebPetDaemon
    "Робот\0"  // WebPetRobot
    "Кружка кофе\0"  // WebPetMug
    "Пингвин\0"  // WebPetPenguin
    "Краб\0"  // WebPetCrab
    "Сова\0"  // WebPetOwl
    "Собака\0"  // WebPetDog
    "Пришелец\0"  // WebPetAlien
    "Riff\0"  // WebPetRiff
    "Тело\0"  // WebSlotBody
    "Контур\0"  // WebSlotLine
    "Уши внутри, язык\0"  // WebSlotDetail
    "Нос, клюв\0"  // WebSlotNose
    "Черты лица\0"  // WebSlotLid
    "Глаза\0"  // WebSlotEye
    "Акцент\0"  // WebSlotAccent
    "Глаза\0"  // WebEyeShape
    "Круглые\0"  // WebEyeRound
    "Большие и блестящие\0"  // WebEyeBig
    "Сонные\0"  // WebEyeSleepy
    "Доступно обновление\0"  // UpdateAvailable
    "v%s (у вас v%s)\0"  // UpdateVersions
    "Проверить обновления\0"  // WebCheckUpdates
    "Всё актуально (v%s)\0"  // WebUpToDate
    "Доступна версия %s.\0"  // WebNewVersion
    "В Claude Code выполните /miblo:update. Или скачайте файл и установите его на странице обновления прошивки.\0"  // WebUpdateHow
    "Не удалось проверить обновления (нет интернета?)\0"  // WebCheckFailed
    "Скачать прошивку\0"  // WebDownloadBin
    "Выключать экран, когда им никто не пользуется\0"  // WebSleep
    "Никогда (талисман продолжает гулять)\0"  // WebSleepNever
    "Талисман начинает гулять через\0"  // WebPetAfter
    "Мигания при оповещении\0"  // WebFlashBlinks
    "Привет! Меня зовут\0"  // HelloIAm
    "Доброе утро\0"  // GoodMorning
    "Добрый день\0"  // GoodAfternoon
    "Добрый вечер\0"  // GoodEvening
    "С днём рождения\0"  // HappyBirthday
    "Сегодня мой день рождения!\0"  // MyBirthday
    "С Рождеством!\0"  // MerryChristmas
    "С Новым годом!\0"  // HappyNewYear
    "Привет, %s!\0"  // FriendHi
    "%s пришёл в гости!\0"  // FriendVisiting
    "В гостях у %s\0"  // FriendAway
    "%s принёс тебе кофе\0"  // FriendCoffee
    "Дремлю с %s\0"  // FriendNap
    "Ваше имя (Miblo будет вас приветствовать)\0"  // WebOwner
    "Ваш день рождения (день / месяц)\0"  // WebBirthday
    "Играть с другими Miblo в сети\0"  // WebFriends
    "Экран\0"  // WebSecScreen
    "О вас\0"  // WebSecYou
    "Устройство\0"  // WebSecDevice
    "Дополнительно\0"  // WebAdvanced
    "Система\0"  // WebSystem
    "Процессор\0"  // WebCpu
    "Память (RAM)\0"  // WebRam
    "Программа (прошивка)\0"  // WebProgram
    "занято %s · свободно %s из %s\0"  // WebInUse
    "Данные\0"  // WebData
    "Подключённые компьютеры\0"  // WebComputers
    "Удалить\0"  // WebRemove
    "Удалить %s? Он перестанет обновлять этот Miblo, пока его снова не подключат через /miblo:pair.\0"  // WebRemoveConfirm
    "этот компьютер\0"  // WebThisComputer
    "активен сейчас\0"  // WebActiveNow
    "был %s назад\0"  // WebSeenAgo
    "не на связи с запуска Miblo\0"  // WebNotSeen
    "Переименовать\0"  // WebRename
    "Пусто: имя самого компьютера\0"  // WebRenameHint
    "Проверьте выделенное поле\0"  // WebCheckField
    "Код для изменения настроек\0"  // CodeSettings
    "Чтобы изменить настройки, введите код с экрана устройства.\0"  // WebUnlock
    "Код на экране\0"  // WebUnlockTitle
    "Неверный код\0"  // WebUnlockBad
    "%s принёс резиновую уточку\0"  // FriendDuck
    "Кодим в паре с %s\0"  // FriendPair
    "%s сделал ревью: LGTM!\0"  // FriendReview
    "Ловим баг с %s\0"  // FriendBug
    "Деплой в пятницу с %s!\0"  // FriendDeploy
    "Дай пять, %s!\0"  // FriendHighFive
    "Пинг-понг с %s\0"  // FriendPingPong
    "Танцуем с %s\0"  // FriendDance
    "Пицца с %s\0"  // FriendPizza
    "Релизный торт с %s!\0"  // FriendCake
    "Мерж-конфликт с %s\0"  // FriendMerge
    "Стендап с %s\0"  // FriendStandup
    "Хакатон с %s\0"  // FriendHackathon
    "Селфи с %s!\0"  // FriendSelfie
    "Шахматы с %s\0"  // FriendChess
    "Видеоигры с %s\0"  // FriendGame
    "%s делится сплетней\0"  // FriendGossip
    "Чокаемся с %s!\0"  // FriendToast
    "Кино с %s\0"  // FriendMovie
    "Строим башню с %s\0"  // FriendBlocks
    "Брейншторм с %s\0"  // FriendBrainstorm
    "Помодоро с %s\0"  // FriendPomodoro
    "Хотфикс с %s!\0"  // FriendHotfix
    "Все тесты зелёные с %s\0"  // FriendTests
    "Ищем 404 с %s\0"  // FriendNotFound
    "Ship it с %s!\0"  // FriendShipIt
    "Спринт с %s\0"  // FriendSprint
    "Оригами с %s\0"  // FriendOrigami
    "Былой Stack Underflow, %s\0"  // FriendNostalgia
    "Kernel panic с %s!\0"  // FriendPanic
    "Пикник с %s\0"  // FriendPicnic
    "Рыбалка с %s\0"  // FriendFishing
    "Под зонтом с %s\0"  // FriendUmbrella
    "Телефон из банок с %s\0"  // FriendCanPhone
    "Запускаем змея с %s\0"  // FriendKite
    "Другие Miblo стоят\0"  // WebFriendsSide
    "Справа\0"  // WebSideRight
    "Слева\0"  // WebSideLeft
    "Сверху\0"  // WebSideUp
    "Снизу\0"  // WebSideDown
    "%s занят\0"  // FriendBusy
    "фокус до %s\0"  // FocusUntil
    "Время перерыва\0"  // FocusBreak
    "Снова за работу?\0"  // FocusBack
    "Долгий перерыв\0"  // FocusLongBreak
    "Может, перерыв на %u мин?\0"  // NudgeBreak
    "Пора выпить воды\0"  // NudgeWater
    "Посмотрите вдаль\0"  // NudgeEyes
    "Хорошего отдыха!\0"  // RestWell
    "Хорошего отдыха, %s!\0"  // RestWellName
    "ПРОШЛАЯ НЕДЕЛЯ\0"  // LastWeekTitle
    "самый загруженный день: %s\0"  // BusiestDay
    "на встрече\0"  // InMeeting
    "Сессии нужны вы\0"  // ASessionNeedsYou
    "%s: готово за %s\0"  // FinishedAfter
    "Готово за %s\0"  // FinishedAfterAnon
    "в таком темпе кончится в %s\0"  // RunsOutAt
    "кончится ~%s\0"  // RunsOutShort
    "Время вышло!\0"  // TimesUp
    "%s через %u дн.\0"  // CountdownDays
    "%s завтра\0"  // CountdownTomorrow
    "%s сегодня!\0"  // CountdownToday
    "Я здесь!\0"  // FindMe
    "С Днём программиста!\0"  // HappyProgrammersDay
    "Самочувствие\0"  // WebSecWellness
    "Перерыв после долгой работы (мин, 0 = выкл.)\0"  // WebBreakAfter
    "Пить воду каждые (мин, 0 = выкл.)\0"  // WebWater
    "Отдых для глаз (20-20-20)\0"  // WebEyes
    "Длина перерыва (мин)\0"  // WebBreakLen
    "Отдых для глаз каждые (мин)\0"  // WebEyesEvery
    "Смотреть вдаль (с)\0"  // WebEyesSec
    "Рабочие часы\0"  // WebWorkHours
    "Рабочие дни\0"  // WebWorkDays
    "Итоги в конце рабочего дня\0"  // WebEndOfDay
    "Праздновать долгие задачи\0"  // WebFanfare
    "Во время фокуса только «нужны вы»\0"  // WebFocusQuiet
    "Настойчивее при долгом ожидании\0"  // WebInsist
    "Рамка состояния по краю экрана\0"  // WebFrame
    "Второй часовой пояс\0"  // WebTz2
    "Название на экране\0"  // WebTz2Label
    "QR настроек на экране стола\0"  // WebDeskQr
    "Понедельник: итоги недели\0";  // WebWeekly

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
    "蓝光过滤\0"  // WebBlue
    "关闭\0"  // WebBlueOff
    "始终开启\0"  // WebBlueAlways
    "定时\0"  // WebBlueScheduled
    "开始时间\0"  // WebBlueFrom
    "结束时间\0"  // WebBlueTo
    "强度\0"  // WebBlueLevel
    "额度已恢复\0"  // LimitFreed
    "%s 后用完\0"  // RunsOutIn
    "今天\0"  // TodayTitle
    "次回复\0"  // SumResponses
    "工作时长\0"  // SumWorked
    "花费\0"  // SumSpent
    "颜色\0"  // WebMascot
    "桃色\0"  // WebMascotSphynx
    "橙色\0"  // WebMascotOrange
    "黑色\0"  // WebMascotBlack
    "灰色\0"  // WebMascotGrey
    "宠物\0"  // WebPet
    "猫\0"  // WebPetCat
    "小黄鸭\0"  // WebPetDuck
    "小甲虫\0"  // WebPetBug
    "守护进程（小幽灵）\0"  // WebPetDaemon
    "机器人\0"  // WebPetRobot
    "咖啡杯\0"  // WebPetMug
    "企鹅\0"  // WebPetPenguin
    "螃蟹\0"  // WebPetCrab
    "猫头鹰\0"  // WebPetOwl
    "小狗\0"  // WebPetDog
    "外星人\0"  // WebPetAlien
    "Riff\0"  // WebPetRiff
    "身体\0"  // WebSlotBody
    "轮廓\0"  // WebSlotLine
    "耳朵内侧、舌头\0"  // WebSlotDetail
    "鼻子、嘴\0"  // WebSlotNose
    "脸部线条\0"  // WebSlotLid
    "眼睛\0"  // WebSlotEye
    "点缀\0"  // WebSlotAccent
    "眼睛\0"  // WebEyeShape
    "圆眼\0"  // WebEyeRound
    "大眼闪亮\0"  // WebEyeBig
    "睡眼\0"  // WebEyeSleepy
    "有可用更新\0"  // UpdateAvailable
    "v%s (当前 v%s)\0"  // UpdateVersions
    "检查更新\0"  // WebCheckUpdates
    "已是最新（v%s）\0"  // WebUpToDate
    "版本 %s 可用。\0"  // WebNewVersion
    "在 Claude Code 中运行 /miblo:update，或下载文件并在固件更新页面安装。\0"  // WebUpdateHow
    "无法检查更新（没有网络？）\0"  // WebCheckFailed
    "下载固件\0"  // WebDownloadBin
    "无人使用时关闭屏幕\0"  // WebSleep
    "从不（吉祥物继续四处走动）\0"  // WebSleepNever
    "吉祥物开始走动前等待\0"  // WebPetAfter
    "提醒闪烁次数\0"  // WebFlashBlinks
    "你好! 我是\0"  // HelloIAm
    "早上好\0"  // GoodMorning
    "下午好\0"  // GoodAfternoon
    "晚上好\0"  // GoodEvening
    "生日快乐\0"  // HappyBirthday
    "今天是我的生日!\0"  // MyBirthday
    "圣诞快乐!\0"  // MerryChristmas
    "新年快乐!\0"  // HappyNewYear
    "你好, %s!\0"  // FriendHi
    "%s 来串门了!\0"  // FriendVisiting
    "正在拜访 %s\0"  // FriendAway
    "%s 给你带了咖啡\0"  // FriendCoffee
    "和 %s 一起睡觉\0"  // FriendNap
    "你的名字(Miblo 会向你问好)\0"  // WebOwner
    "你的生日(日 / 月)\0"  // WebBirthday
    "和网络上的其他 Miblo 一起玩\0"  // WebFriends
    "屏幕\0"  // WebSecScreen
    "关于你\0"  // WebSecYou
    "设备\0"  // WebSecDevice
    "高级\0"  // WebAdvanced
    "系统\0"  // WebSystem
    "处理器\0"  // WebCpu
    "内存 (RAM)\0"  // WebRam
    "程序（固件）\0"  // WebProgram
    "已用 %s · 可用 %s / 共 %s\0"  // WebInUse
    "数据\0"  // WebData
    "已配对的电脑\0"  // WebComputers
    "移除\0"  // WebRemove
    "移除 %s？在用 /miblo:pair 重新配对之前，它将不再更新此 Miblo。\0"  // WebRemoveConfirm
    "本机\0"  // WebThisComputer
    "当前活跃\0"  // WebActiveNow
    "%s前活跃\0"  // WebSeenAgo
    "Miblo 启动后未连接\0"  // WebNotSeen
    "重命名\0"  // WebRename
    "留空：使用电脑自己的名称\0"  // WebRenameHint
    "请检查标出的字段\0"  // WebCheckField
    "修改设置的验证码\0"  // CodeSettings
    "要修改设置, 请输入屏幕上显示的验证码.\0"  // WebUnlock
    "屏幕上的验证码\0"  // WebUnlockTitle
    "验证码错误\0"  // WebUnlockBad
    "%s 带来了小黄鸭\0"  // FriendDuck
    "和 %s 结对编程\0"  // FriendPair
    "%s 审查通过: LGTM!\0"  // FriendReview
    "和 %s 一起抓 bug\0"  // FriendBug
    "和 %s 周五上线!\0"  // FriendDeploy
    "和 %s 击掌!\0"  // FriendHighFive
    "和 %s 打乒乓\0"  // FriendPingPong
    "和 %s 跳舞\0"  // FriendDance
    "和 %s 吃披萨\0"  // FriendPizza
    "和 %s 吃发布蛋糕!\0"  // FriendCake
    "和 %s 解决合并冲突\0"  // FriendMerge
    "和 %s 开站会\0"  // FriendStandup
    "和 %s 参加黑客松\0"  // FriendHackathon
    "和 %s 自拍!\0"  // FriendSelfie
    "和 %s 下棋\0"  // FriendChess
    "和 %s 打游戏\0"  // FriendGame
    "%s 有小道消息\0"  // FriendGossip
    "和 %s 干杯!\0"  // FriendToast
    "和 %s 看电影\0"  // FriendMovie
    "和 %s 搭积木\0"  // FriendBlocks
    "和 %s 头脑风暴\0"  // FriendBrainstorm
    "和 %s 用番茄钟\0"  // FriendPomodoro
    "和 %s 紧急修复!\0"  // FriendHotfix
    "和 %s 测试全绿\0"  // FriendTests
    "和 %s 找 404\0"  // FriendNotFound
    "和 %s Ship it!\0"  // FriendShipIt
    "和 %s 冲刺\0"  // FriendSprint
    "和 %s 折纸\0"  // FriendOrigami
    "和 %s 怀念 Stack Underflow\0"  // FriendNostalgia
    "和 %s 内核恐慌!\0"  // FriendPanic
    "和 %s 野餐\0"  // FriendPicnic
    "和 %s 钓鱼\0"  // FriendFishing
    "和 %s 共撑一把伞\0"  // FriendUmbrella
    "和 %s 玩纸杯电话\0"  // FriendCanPhone
    "和 %s 放风筝\0"  // FriendKite
    "其他 Miblo 在我的\0"  // WebFriendsSide
    "右边\0"  // WebSideRight
    "左边\0"  // WebSideLeft
    "上方\0"  // WebSideUp
    "下方\0"  // WebSideDown
    "%s 忙起来了\0"  // FriendBusy
    "专注到 %s\0"  // FocusUntil
    "休息时间\0"  // FocusBreak
    "继续专注吗?\0"  // FocusBack
    "长休息\0"  // FocusLongBreak
    "休息 %u 分钟吧?\0"  // NudgeBreak
    "该喝水了\0"  // NudgeWater
    "看看远处\0"  // NudgeEyes
    "好好休息!\0"  // RestWell
    "%s, 好好休息!\0"  // RestWellName
    "上周\0"  // LastWeekTitle
    "最忙的一天: %s\0"  // BusiestDay
    "开会中\0"  // InMeeting
    "有会话需要你处理\0"  // ASessionNeedsYou
    "%s 用时 %s 完成\0"  // FinishedAfter
    "用时 %s 完成\0"  // FinishedAfterAnon
    "按当前速度 %s 用完\0"  // RunsOutAt
    "约 %s 用完\0"  // RunsOutShort
    "时间到!\0"  // TimesUp
    "%s 还有 %u 天\0"  // CountdownDays
    "%s 就在明天\0"  // CountdownTomorrow
    "%s 就是今天!\0"  // CountdownToday
    "我在这里!\0"  // FindMe
    "程序员节快乐!\0"  // HappyProgrammersDay
    "健康\0"  // WebSecWellness
    "长时间工作后休息（分钟，0 = 关闭）\0"  // WebBreakAfter
    "喝水间隔（分钟，0 = 关闭）\0"  // WebWater
    "护眼休息 (20-20-20)\0"  // WebEyes
    "休息时长（分钟）\0"  // WebBreakLen
    "护眼间隔（分钟）\0"  // WebEyesEvery
    "远眺时长（秒）\0"  // WebEyesSec
    "工作时间\0"  // WebWorkHours
    "工作日\0"  // WebWorkDays
    "下班时的总结\0"  // WebEndOfDay
    "庆祝长任务完成\0"  // WebFanfare
    "专注时只提醒\"需要你处理\"\0"  // WebFocusQuiet
    "等待太久时更强提醒\0"  // WebInsist
    "屏幕状态边框\0"  // WebFrame
    "第二时区\0"  // WebTz2
    "屏幕上的名称\0"  // WebTz2Label
    "桌面屏幕显示设置二维码\0"  // WebDeskQr
    "周一: 上周总结\0";  // WebWeekly

const char* const kLangSource[] MIBLO_ROM = {kEn, kPtBR, kPtPT, kEs, kFr, kIt, kDe, kRu, kZh};

}  // namespace miblo
