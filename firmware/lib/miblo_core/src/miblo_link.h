#pragma once
#include <stdint.h>

namespace miblo {

// ---- Wi-Fi safety net ----
// NetPolicy decides what the screen says and when the setup network opens; rejoining the saved
// network is left to the SDK's auto-reconnect. That is not always enough: a unit in the field
// showed "Disconnected" (drawn only while WiFi.status() is WL_CONNECTED) while it answered nothing,
// not even ARP, until it was power-cycled. LinkKeeper watches the link and acts on its own:
// - Connected but quiet (nothing heard from the computer lately), and at every link-up: an ARP
//   probe of the gateway. Once the gateway has answered one this boot, kDeadAfter unanswered in a
//   row mark the link dead (a gateway that never answers ARP proves nothing, so it is never
//   judged).
// - Disconnected for kDownMs, or dead: an explicit disconnect + begin of the saved network. The
//   waits between such cycles double, up to kMaxWaitMs, and start over once the link works.
// - Without a working link for kRestartMs, on a network that worked earlier this boot: one clean
//   restart (pending saves flushed). A network never reached this boot is never restarted for.
// Nothing happens while `mayAct` is false (no saved network, a submitted network being tried, an
// update, a phone on the setup network); the wait starts over when it is true again.
enum class LinkAction : uint8_t {
  None,
  Probe,       // send an ARP request to the gateway (forgetting what the ARP table knows)
  ProbeCheck,  // see whether it was answered and call probeAnswered()
  Reconnect,   // WiFi.disconnect() then begin() of the saved network
  Restart      // restart the unit through the reboot path
};

class LinkKeeper {
 public:
  static constexpr uint32_t kDownMs = 180000;        // disconnected this long: the first cycle
  static constexpr uint32_t kMaxWaitMs = 900000;     // the longest wait between cycles
  static constexpr uint32_t kRestartMs = 1800000;    // no working link this long: restart (once)
  static constexpr uint32_t kProbeEveryMs = 60000;   // connected and quiet: a probe this often
  static constexpr uint32_t kProbeRetryMs = 10000;   // after an unanswered probe
  static constexpr uint32_t kProbeWaitMs = 2000;     // time the gateway has to answer
  static constexpr uint8_t kDeadAfter = 3;           // unanswered probes in a row: the link is dead

  void begin(uint32_t nowMs);
  // Every loop pass. linkUp: WiFi.status() is WL_CONNECTED. quiet: nothing heard from the network
  // lately (the computer is not posting), so only a probe can tell whether the link works.
  LinkAction update(uint32_t nowMs, bool linkUp, bool mayAct, bool quiet);
  // After ProbeCheck: whether the gateway answered.
  void probeAnswered(bool answered, uint32_t nowMs);

  bool dead() const { return dead_; }
  uint32_t reconnects() const { return reconnects_; }  // cycles since boot (/api/info)
  uint32_t deadLinks() const { return deadLinks_; }    // links found dead since boot (/api/info)

 private:
  void confirmed();

  bool good_ = false;            // up and seen working (traffic or an answered probe)
  bool wasUp_ = false;
  bool proven_ = false;          // seen working at least once this boot
  bool arpSeen_ = false;         // the gateway answered a probe this boot
  bool dead_ = false;
  bool probing_ = false;
  bool confirmPending_ = false;  // the link just came up: probe at once
  bool paused_ = false;
  bool restarted_ = false;
  uint8_t fails_ = 0;
  uint32_t badSinceMs_ = 0;      // no working link since
  uint32_t lastActMs_ = 0;       // the last cycle (or the start of the wait for the first)
  uint32_t waitMs_ = kDownMs;
  uint32_t probeAtMs_ = 0;
  uint32_t reconnects_ = 0;
  uint32_t deadLinks_ = 0;
};

// One pass of the safety net, carried out on `w`: net.cpp passes the ESP8266 WiFi (through a thin
// adapter), the tests a fake. W provides sendProbe(), probeAnswered(), dropStation(), joinSaved()
// and restart().
template <class W>
LinkAction runLinkKeeper(LinkKeeper& k, W& w, uint32_t nowMs, bool linkUp, bool mayAct, bool quiet) {
  const LinkAction a = k.update(nowMs, linkUp, mayAct, quiet);
  switch (a) {
    case LinkAction::None: break;
    case LinkAction::Probe: w.sendProbe(); break;
    case LinkAction::ProbeCheck: k.probeAnswered(w.probeAnswered(), nowMs); break;
    case LinkAction::Reconnect:
      w.dropStation();
      w.joinSaved();
      break;
    case LinkAction::Restart: w.restart(); break;
  }
  return a;
}

}  // namespace miblo
