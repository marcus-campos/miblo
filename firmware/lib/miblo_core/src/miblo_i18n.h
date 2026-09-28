#pragma once
#include <stddef.h>
#include <stdint.h>

namespace miblo {

enum class Lang : uint8_t { En, PtBR, PtPT, Es, Fr, It, De, Ru, Zh, Count };

// String identifiers. The order matches the tables in miblo_strings.cpp.
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

// Per-language tables (MIBLO_ROM), indexed by Lang.
extern const char* const kLangTables[];

// "en", "pt-BR", "pt-PT", "es", "fr", "it", "de", "ru", "zh".
const char* langCode(Lang lang);
// Language name in its own language ("Português (Brasil)"), for the page's selector.
const char* langName(Lang lang);
// Exact code (case-insensitive) → Lang. Returns false if not supported.
bool langFromCode(const char* code, Lang& out);
// Picks the language from the Accept-Language header (highest q wins; tie → order).
// "pt" with no region → pt-BR; "pt-XX" (other region) → pt-PT; "zh-*" → zh; nothing supported → en.
Lang negotiateLang(const char* acceptLanguage);

// Copies the translated string into `out` (always NUL-terminated, never cutting UTF-8 mid-codepoint).
void tr(Lang lang, S id, char* out, size_t cap);

}  // namespace miblo
