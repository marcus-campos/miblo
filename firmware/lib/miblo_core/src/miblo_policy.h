#pragma once
#include <stdint.h>

#include "miblo_alerts.h"

namespace miblo {

// ---- Wi-Fi: quando abrir a rede de setup (spec §6) ----
enum class NetState : uint8_t { Connecting, Connected, Portal, WrongPassword };
enum class LinkStatus : uint8_t { Down, Connected, WrongPassword };

class NetPolicy {
 public:
  static constexpr uint32_t kFallbackMs = 120000;  // 2 min sem conexão → abre a rede de setup
  void begin(bool hasCredentials, uint32_t nowMs);
  void credentialsSubmitted(uint32_t nowMs);
  NetState update(LinkStatus link, uint32_t nowMs);
  NetState state() const { return state_; }
  // Rede de setup (AP) deve estar no ar? A conexão com a rede salva continua sendo tentada.
  bool apWanted() const { return ap_; }

 private:
  NetState state_ = NetState::Connecting;
  bool ap_ = false;
  uint32_t sinceMs_ = 0;
};

// ---- Qual tela mostrar ----
enum class ScreenId : uint8_t {
  Boot,           // mascote + "Conectando ao Wi-Fi"
  Setup,          // QR + nome da rede de setup
  WrongPassword,  // Setup com "Senha incorreta"
  Welcome,        // Wi-Fi conectado + comando + código de pareamento + IP
  Paired,         // "Pareado com <host>"
  PairCode,       // código de pareamento pedido pela página
  PresenceCode,   // código para update/reset pelo navegador
  Updating,       // barra de progresso do OTA
  Disconnected,   // relógio (sem snapshot há 30 s)
  AlertFlash,
  AlertHero,
  Main,           // modo do aparelho (Visão geral, Limites ou Sessões)
  HardResetCountdown  // quick-restarts-left countdown during the first 10 s of a quick boot
};

constexpr uint32_t kPairedScreenMs = 5000;
constexpr uint32_t kSnapshotTimeoutMs = 30000;
constexpr uint32_t kPairCodeScreenMs = 120000;

struct ScreenInputs {
  uint32_t nowMs = 0;
  bool bootAnimDone = false;
  NetState net = NetState::Connecting;
  bool updating = false;
  bool presenceActive = false;
  bool hardResetCountdown = false;  // quick-boot countdown still inside its 10 s window
  bool pairCodeRequested = false;
  bool paired = false;
  bool justPaired = false;
  uint32_t pairedAtMs = 0;
  bool hasSnapshot = false;
  uint32_t lastSnapshotMs = 0;
  AlertPhase alert = AlertPhase::None;
};

ScreenId selectScreen(const ScreenInputs& in);

}  // namespace miblo
