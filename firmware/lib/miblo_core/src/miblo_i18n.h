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
  HardResetCountdown,   // "Quick restarts left to reset: %u"
  HardResetCancelHint,  // "Leave it on to cancel"
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
