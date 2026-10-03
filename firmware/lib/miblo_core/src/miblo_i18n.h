#pragma once
#include <stddef.h>
#include <stdint.h>

namespace miblo {

enum class Lang : uint8_t { En, PtBR, PtPT, Es, Fr, It, De, Ru, Zh, Count };

// String identifiers. The order matches the tables in miblo_strings.cpp.
enum class S : uint16_t {
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
  WebBlue,              // settings: blue light filter (a select: off / always / scheduled)
  WebBlueOff,           // settings: blue light filter option "off"
  WebBlueAlways,        // settings: blue light filter option "always"
  WebBlueScheduled,     // settings: blue light filter option "on a schedule"
  WebBlueFrom,          // settings: blue light filter schedule start time
  WebBlueTo,            // settings: blue light filter schedule end time
  WebBlueLevel,         // settings: blue light filter strength (a 1..100 % slider)
  LimitFreed,           // "limit freed" screen band: the 5h window reset after real use
  RunsOutIn,            // burn-rate projection: "runs out in %s"
  TodayTitle,           // daily summary header
  SumResponses,         // daily summary: label under the number of responses
  SumWorked,            // daily summary: label under the time worked
  SumSpent,             // daily summary: label under the cost
  WebMascot,            // settings: the mascot's colour (every pet's body)
  WebMascotSphynx,      // settings: mascot style 0 (peach, Miblo's own)
  WebMascotOrange,      // settings: mascot style 1
  WebMascotBlack,       // settings: mascot style 2
  WebMascotGrey,        // settings: mascot style 3
  WebPet,               // settings: the pet select's label (which animal the mascot is)
  WebPetCat,            // settings: pet 0 cat
  WebPetDuck,           // settings: pet 1 rubber duck
  WebPetBug,            // settings: pet 2 bug (a beetle)
  WebPetDaemon,         // settings: pet 3 daemon (a little ghost)
  WebPetRobot,          // settings: pet 4 robot
  WebPetMug,            // settings: pet 5 coffee mug (with a face)
  WebPetPenguin,        // settings: pet 6 penguin
  WebPetCrab,           // settings: pet 7 crab
  WebPetOwl,            // settings: pet 8 owl
  WebPetDog,            // settings: pet 9 dog
  WebPetAlien,          // settings: pet 10 alien (one big eye, antennae)
  WebPetRiff,           // settings: pet 11 Riff, an original little rocker (a name: the same in every language)
  WebPetDev,            // settings: pet 12 Dev, a veteran developer with a ponytail (a name; zh: programmer)
  WebPetDino,           // settings: pet 14 (never 13) a little dinosaur ("Dino")
  WebPetDevChan,        // settings: pet 15 Dev-chan, a chibi dev companion (a name)
  WebSlotBody,          // settings: colour slot: the body
  WebSlotLine,          // settings: colour slot: outlines
  WebSlotDetail,        // settings: colour slot: inner ears, tongue, inside of the mouth
  WebSlotNose,          // settings: colour slot: nose, beak
  WebSlotLid,           // settings: colour slot: lines on the face (closed eyes, mouth)
  WebSlotEye,           // settings: colour slot: the eyes (iris)
  WebSlotAccent,        // settings: colour slot: the pet's own extra (antenna tips, patches...)
  WebEyeShape,          // settings: the eye shape select's label
  WebEyeRound,          // settings: eye shape 0
  WebEyeBig,            // settings: eye shape 1
  WebEyeSleepy,         // settings: eye shape 2
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
  WebPetAfter,          // settings: pet mode delay, "the mascot starts wandering after" (a select: 1 min ... 1 h)
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
  WebSystem,            // settings > advanced: the live System panel's heading
  WebCpu,               // System panel: processing load (last minute graph)
  WebRam,               // System panel: RAM in use (last minute graph)
  WebProgram,           // System panel, storage: the program (firmware) bar's label
  WebInUse,             // System panel: "%s in use · %s free of %s" ("66%", "26 KB", "80 KB")
  WebData,              // System panel, storage: the data (filesystem) bar's label
  WebComputers,         // settings > advanced: the paired computers' list heading
  WebRemove,            // computers list: the button that unpairs one
  WebRemoveConfirm,     // computers list: "Remove %s? ..." (%s: its host name)
  WebThisComputer,      // computers list: next to the computer that opened the page (/miblo:settings)
  WebActiveNow,         // computers list: its token came in within the last minute
  WebSeenAgo,           // computers list: "seen %s ago" (%s: "5 min", "2 h")
  WebNotSeen,           // computers list: not heard from since the gadget started
  WebRename,            // computers list: the button that names one
  WebRenameHint,        // computers list: the rename field's placeholder (empty = automatic label)
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
  FriendHighFive,       // visit: "High five with %s!"
  FriendPingPong,       // visit: "Ping-pong with %s"
  FriendDance,          // visit: "Dancing with %s"
  FriendPizza,          // visit: "Pizza with %s"
  FriendCake,           // visit: "Release cake with %s!"
  FriendMerge,          // visit: "Merge conflict with %s"
  FriendStandup,        // visit: "Daily standup with %s"
  FriendHackathon,      // visit: "Hackathon with %s"
  FriendSelfie,         // visit: "Selfie with %s!"
  FriendChess,          // visit: "Playing chess with %s"
  FriendGame,           // visit: "Gaming with %s"
  FriendGossip,         // visit: "%s has gossip"
  FriendToast,          // visit: "Cheers with %s!"
  FriendMovie,          // visit: "Movie time with %s"
  FriendBlocks,         // visit: "Stacking blocks with %s"
  FriendBrainstorm,     // visit: "Brainstorming with %s"
  FriendPomodoro,       // visit: "Pomodoro with %s"
  FriendHotfix,         // visit: "Hotfix with %s!"
  FriendTests,          // visit: "All tests green with %s"
  FriendNotFound,       // visit: "404 hunt with %s"
  FriendShipIt,         // visit: "Ship it with %s!"
  FriendSprint,         // visit: "Sprint with %s"
  FriendOrigami,        // visit: "Origami with %s"
  FriendNostalgia,      // visit: "Stack Underflow days, %s" (the old Q&A site, a parody name)
  FriendPanic,          // visit: "Kernel panic with %s!"
  FriendPicnic,         // visit: "Picnic with %s"
  FriendFishing,        // visit: "Fishing with %s"
  FriendUmbrella,       // visit: "Under an umbrella with %s"
  FriendCanPhone,       // visit: "Tin can phone with %s"
  FriendKite,           // visit: "Flying a kite with %s"
  WebFriendsSide,       // settings: where the other Miblos stand (our cat leaves that way)
  WebSideRight,
  WebSideLeft,
  WebSideUp,
  WebSideDown,
  FriendBusy,           // visit cut short: "%s got busy" (the host's human got back to work)
  // ---- Daily life (focus, wellness, meeting, notes, forecast) ----
  FocusUntil,           // focus screen: "focus until 15:30"
  FocusBreak,           // focus screen: the break between rounds
  FocusBack,            // focus screen: the minute after a break
  FocusLongBreak,       // focus screen: the break after the last round
  NudgeBreak,           // wellness: after long continuous work
  NudgeWater,           // wellness: water reminder
  NudgeEyes,            // wellness: 20-20-20 eye rest
  RestWell,             // end of the work day, no owner name
  RestWellName,         // end of the work day: "Have a good rest, Ana!"
  LastWeekTitle,        // Monday recap header
  BusiestDay,           // Monday recap: "busiest day: Wed"
  InMeeting,            // meeting mode badge
  ASessionNeedsYou,     // meeting mode: an alert without the session name
  FinishedAfter,        // long task fanfare: "api finished after 23 min"
  FinishedAfterAnon,    // long task fanfare in meeting mode (no name)
  RunsOutAt,            // Limits screen: the 5h forecast
  RunsOutShort,         // Overview: the 5h forecast under 30 min
  TimesUp,              // timer: the time is over
  CountdownDays,        // countdown: "release in 3 days"
  CountdownTomorrow,    // countdown: "release tomorrow"
  CountdownToday,       // countdown: "release is today!"
  FindMe,               // /miblo:find
  HappyProgrammersDay,  // greeting on day 256
  WebSecWellness,       // settings: wellness section title
  WebBreakAfter,        // settings: break after long work (minutes, 0 = off)
  WebWater,             // settings: water reminder (minutes, 0 = off)
  WebEyes,              // settings: 20-20-20 toggle
  WebBreakLen,          // settings: the break the nudge suggests (minutes)
  WebEyesEvery,         // settings: eye rest interval (minutes)
  WebEyesSec,           // settings: how long the eye rest lasts (seconds)
  WebWorkHours,         // settings: work hours
  WebWorkDays,          // settings: work days
  WebEndOfDay,          // settings: end of day summary toggle
  WebFanfare,           // settings: long task fanfare (select)
  WebFocusQuiet,        // settings: hold "finished" during focus
  WebInsist,            // settings: insistence toggle
  WebFrame,             // settings: status frame toggle
  WebTz2,               // settings: second time zone
  WebTz2Label,          // settings: the second zone's name on screen
  WebDeskQr,            // settings: settings QR on the Desk screen
  WebWeekly,            // settings: Monday recap toggle
  WebWearHead,          // settings: accessory select, the head slot
  WebWearFace,          // settings: accessory select, the face slot
  WebWearNeck,          // settings: accessory select, the neck slot
  WebWearNone,          // settings: accessory: nothing worn
  WebOccasionHats,      // settings: toggle, special days (Christmas, birthdays...) dress the pet
  WebPreview,           // settings: button, shows the unsaved pet look on the gadget for a few seconds
  WebWearCap,           // accessory 1: baseball cap
  WebWearBeanie,        // accessory 2: knitted beanie
  WebWearBeret,         // accessory 3: beret
  WebWearTopHat,        // accessory 4: top hat
  WebWearCrown,         // accessory 5: crown
  WebWearCowboyHat,     // accessory 6: cowboy hat
  WebWearChefHat,       // accessory 7: chef's hat
  WebWearBandana,       // accessory 8: bandana
  WebWearFlowerCrown,   // accessory 9: flower crown
  WebWearHalo,          // accessory 10: halo
  WebWearSunglasses,    // accessory 11: sunglasses
  WebWearNerdGlasses,   // accessory 12: nerd glasses (13 is never used)
  WebWearMonocle,       // accessory 14: monocle
  WebWearMoustache,     // accessory 15: moustache
  WebWearBowTie,        // accessory 16: bow tie
  WebWearScarf,         // accessory 17: scarf
  WebWearNeckerchief,   // accessory 18: neckerchief
  WebWearBeads,         // accessory 19: bead necklace
  WebWearHeadset,       // accessory 20: gamer headset with a mic
  WebWearMedal,         // accessory 21: a medal for shipping to production
  Count
};

// Per-language tables (MIBLO_ROM), indexed by Lang: the text as written (miblo_strings.cpp; only
// the tests read it, the firmware leaves it out) and the packed copy tr() reads
// (miblo_strings_packed.cpp, made by scripts/pack_strings.py).
extern const char* const kLangSource[];
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
// The language a web page is drawn in (`browser` = negotiateLang of its Accept-Language). Before
// pairing, automatic mode follows the browser and `store` says the gadget should keep it (setup:
// the screen speaks the phone's language). A paired gadget draws the page in the browser's
// language and never changes or saves anything because someone opened a page.
Lang pageLanguage(bool paired, bool langSet, Lang stored, Lang browser, bool& store);

// Copies the translated string into `out` (always NUL-terminated, never cutting UTF-8 mid-codepoint).
void tr(Lang lang, S id, char* out, size_t cap);
// The settings page's name for a pet (miblo::Pet); false for a value that is no pet (13 never is).
bool petName(uint8_t pet, S& out);

}  // namespace miblo
