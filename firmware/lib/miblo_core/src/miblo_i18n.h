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
  WebRotate,  // Overview mode: alternate with Limits
  WebRotateEvery,
  WebRotateShow,
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
  NetNotFound,          // setup screen title: the submitted network was never seen
  Use24GHz,             // setup screen hint under NetNotFound
  WebNotFound,          // portal: "Network not found. Miblo only works with 2.4 GHz Wi-Fi."
  WebConnectedAt,       // portal: "Connected! Open Miblo at:" + address
  WebConnectFailed,     // portal: the attempt timed out
  WebTryAgain,          // portal: link back to the form
  WebNoReply,           // portal: the phone lost the setup network (e.g. after the channel hop)
  ConnRefused,          // setup screen title: auth/handshake rejected
  RefusedHint,          // setup screen hint under ConnRefused
  JoinFailed,           // setup screen title: any other failure
  ErrorCode,            // setup screen hint under JoinFailed: "Error code %u"
  WebRefused,           // portal: wrong password or WPA3/"WPA2/WPA3" router
  WebFailedCode,        // portal: "Could not connect (code %u). ..."
  WaitAgent1,           // activity of a Stop waiting on background work: "Waiting on %u agent"
  WaitAgentsN,          // "Waiting on %u agents"
  WaitTask1,            // "Waiting on %u task"
  WaitTasksN,           // "Waiting on %u tasks"
  Short5h,              // compact limits strip label for the 5-hour window ("5h")
  Short7d,              // compact limits strip label for the weekly window ("7d")
  WebAuto,              // settings page: automatic language option ("Auto")
  Compacting,           // activity while Claude Code compacts the conversation
  WebSameNetwork,       // portal: the gadget must join the same network as the computer
  WebNight,             // settings: night mode checkbox
  WebNightFrom,         // settings: night mode start time
  WebNightTo,           // settings: night mode end time
  WebNightBrightness,   // settings: brightness during the night window
  LimitFreed,           // "limit freed" screen band: the 5h window reset after real use
  RunsOutIn,            // burn-rate projection: "runs out in %s"
  TodayTitle,           // daily summary header
  SumResponses,         // daily summary: label under the number of responses
  SumWorked,            // daily summary: label under the time worked
  SumSpent,             // daily summary: label under the cost
  WebMascot,            // settings: mascot colours
  WebMascotSphynx,      // settings: mascot style 0
  WebMascotOrange,      // settings: mascot style 1
  WebMascotBlack,       // settings: mascot style 2
  WebMascotGrey,        // settings: mascot style 3
  UpdateAvailable,      // boot notice title: a newer firmware was released
  UpdateVersions,       // boot notice: "v%s (you have v%s)" (latest, current)
  WebCheckUpdates,      // settings: button that asks GitHub (from the browser) for the latest release
  WebUpToDate,          // settings: "Up to date (v%s)"
  WebNewVersion,        // settings: "Version %s is available."
  WebUpdateHow,         // settings: how to install it
  WebCheckFailed,       // settings: the browser couldn't reach GitHub
  WebDownloadBin,       // settings: link to the release's .bin for this board
  WebSleep,             // settings: screen-off delay (a select: never / 15 min ... 4 h)
  WebSleepNever,        // settings: the "never" option (pet mode keeps going)
  WebPetAfter,          // settings: pet mode delay (a select: 1 min ... 1 h)
  WebFlashBlinks,       // settings: how many times the alert flash blinks (2..5)
  HelloIAm,             // greeting after (re)naming, over the gadget's name: "Hi! I'm" / "Tofu"
  GoodMorning,          // first activity of the day, over the owner's name (no punctuation)
  GoodAfternoon,
  GoodEvening,
  HappyBirthday,        // the owner's birthday, over their name
  MyBirthday,           // the gadget's own birthday (a year after it was first used)
  MerryChristmas,
  HappyNewYear,
  FriendHi,             // pet mode: another Miblo showed up on the network: "Hi, %s!"
  FriendVisiting,       // a friend's mascot is here: "%s came to visit!"
  FriendAway,           // our mascot went out: "Visiting %s"
  FriendCoffee,         // the visitor brought a coffee: "%s brought you coffee"
  FriendNap,            // both asleep: "Napping with %s"
  WebOwner,             // settings: the owner's name
  WebBirthday,          // settings: the owner's birthday (day and month selects)
  WebFriends,           // settings: pet mode visits between Miblos on the network
  WebSecScreen,         // settings: section heading (mode, brightness, mascot, screen off)
  WebSecYou,            // settings: section heading (owner name and birthday)
  WebSecDevice,         // settings: section heading (device name, time zone, language, visits)
  WebAdvanced,          // settings: collapsed section (firmware, pairing code, factory reset)
  WebCheckField,        // settings: a save was refused; the offending field is highlighted
  CodeSettings,         // PresenceCode screen: "Code to change settings"
  WebUnlock,            // settings page: "Type the code on the gadget screen to change settings"
  WebUnlockTitle,       // settings page: "Enter the code" (prompt)
  WebUnlockBad,         // settings page: "Wrong code"
  FriendDuck,           // visit: "%s brought the rubber duck" (rubber duck debugging)
  FriendPair,           // visit: "Pair programming with %s"
  FriendReview,         // visit: "%s reviewed it: LGTM!"
  FriendBug,            // visit: "Hunting a bug with %s"
  FriendDeploy,         // visit: "Friday deploy with %s!"
  WebFriendsSide,       // settings: where the other Miblos stand (our cat leaves that way)
  WebSideRight,
  WebSideLeft,
  WebSideUp,
  WebSideDown,
  FriendBusy,           // visit cut short: "%s got busy" (the host's human got back to work)
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
