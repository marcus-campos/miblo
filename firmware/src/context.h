#pragma once
#include <stdint.h>

#include "miblo_alerts.h"
#include "miblo_config.h"
#include "miblo_desknotes.h"
#include "miblo_focus.h"
#include "miblo_friends.h"
#include "miblo_i18n.h"
#include "miblo_limits.h"
#include "miblo_livetz.h"
#include "miblo_meeting.h"
#include "miblo_occasions.h"
#include "miblo_policy.h"
#include "miblo_overview.h"
#include "miblo_security.h"
#include "miblo_snapshot.h"

// State shared between the hardware modules. The modules (web, api, net) only change the
// state and raise flags; the app loop (app.cpp) reacts to the flags.
struct Identity {
  char id[16];           // "miblo-4f2a" (mDNS/TXT and host ID)
  char defaultName[16];  // "Miblo-4F2A"
  char apSsid[24];       // "Miblo-Setup-4F2A"
};

struct Context {
  Identity ident{};
  miblo::Config cfg;
  miblo::TokenStore tokens;
  miblo::PairingGuard pairing;
  miblo::PresenceGate presence;
  // Throttles expensive UNAUTHENTICATED responses (the page and /api/info) so a flood from an
  // unpaired client cannot starve the display loop: 120/min sustained, burst 20. The plugin's
  // authenticated /api/state push never passes through here.
  miblo::RateLimiter publicReqs{20, 2};
  miblo::WebSession webSession;  // browser proved the on-screen code: may change settings for a while
  miblo::Snapshot snap{};
  miblo::LiveTz liveTz;  // the bridge's live offsets for cfg.tz and tz2 (snapshot "tz")
  miblo::AlertSequencer alerts;
  miblo::RunTracker runs;
  miblo::LimitWatch limits;  // "limit freed" and the burn-rate projection
  miblo::UpdateNotice update;  // "update available" once per boot
  miblo::FriendPlay friends;   // other Miblos on the network (pet mode visits)
  miblo::Greeter greeter;      // "Hi! I'm Tofu", "Good morning, Ana", birthdays
  miblo::FocusTimer focus;      // /miblo:focus
  miblo::MeetingMode meeting;   // /miblo:meeting
  miblo::DeskNotes notes;       // say, reminders, alarms, timer, countdown, find

  bool hasSnapshot = false;
  uint32_t lastSnapshotMs = 0;
  bool usageEverSeen = false;

  // flags for the main loop
  bool configChanged = false;
  bool factoryResetRequested = false;
  bool rebootRequested = false;
  uint32_t rebootAtMs = 0;
  bool justPaired = false;
  uint32_t pairedAtMs = 0;
  char pairedHost[33] = "";
  bool showPairCode = false;
  uint32_t pairCodeAtMs = 0;
  uint32_t lastInteractionMs = 0;  // someone opened one of the gadget's pages (keeps the screen on)
  // /miblo:demo: pet mode right away (and quick visits) until demoUntilMs; demoKick = just asked.
  bool demo = false;
  bool demoKick = false;
  uint32_t demoUntilMs = 0;
  bool updating = false;
  uint8_t updatePct = 0;
};

extern Context ctx;

// Screen language: the one chosen on the page or, in automatic mode, the last one negotiated.
inline miblo::Lang uiLang() { return ctx.cfg.lang; }
// Device name: the configured one or "Miblo-XXXX".
inline const char* deviceName() { return ctx.cfg.name[0] ? ctx.cfg.name : ctx.ident.defaultName; }
