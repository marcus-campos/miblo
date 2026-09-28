#pragma once
#include <stdint.h>

#include "miblo_alerts.h"
#include "miblo_config.h"
#include "miblo_i18n.h"
#include "miblo_overview.h"
#include "miblo_security.h"
#include "miblo_snapshot.h"

// Estado compartilhado entre os módulos de hardware. Os módulos (web, api, net) só alteram o
// estado e levantam flags; o loop do app (app.cpp) reage às flags.
struct Identity {
  char id[16];           // "miblo-4f2a" (ID do mDNS/TXT e host)
  char defaultName[16];  // "Miblo-4F2A"
  char apSsid[24];       // "Miblo-Setup-4F2A"
};

struct Context {
  Identity ident{};
  miblo::Config cfg;
  miblo::TokenStore tokens;
  miblo::PairingGuard pairing;
  miblo::PresenceGate presence;
  miblo::Snapshot snap{};
  miblo::AlertSequencer alerts;
  miblo::RunTracker runs;

  bool hasSnapshot = false;
  uint32_t lastSnapshotMs = 0;
  bool usageEverSeen = false;

  // flags para o loop principal
  bool configChanged = false;
  bool factoryResetRequested = false;
  bool rebootRequested = false;
  uint32_t rebootAtMs = 0;
  bool justPaired = false;
  uint32_t pairedAtMs = 0;
  char pairedHost[33] = "";
  bool showPairCode = false;
  uint32_t pairCodeAtMs = 0;
  bool updating = false;
  uint8_t updatePct = 0;
};

extern Context ctx;

// Idioma da tela: o escolhido na página ou, em modo automático, o último negociado.
inline miblo::Lang uiLang() { return ctx.cfg.lang; }
// Nome do aparelho: o configurado ou "Miblo-XXXX".
inline const char* deviceName() { return ctx.cfg.name[0] ? ctx.cfg.name : ctx.ident.defaultName; }
