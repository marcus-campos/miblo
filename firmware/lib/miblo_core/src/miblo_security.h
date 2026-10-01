#pragma once
#include <stddef.h>
#include <stdint.h>

namespace miblo {

// "0042" from a random number.
void formatCode(uint32_t rnd, char out[5]);
// 128-bit token as lowercase hex (32 characters + NUL).
void makeToken(const uint8_t rnd[16], char out[33]);
// "Bearer <token>" → token. false if the header isn't in that format or the token doesn't fit.
bool bearerToken(const char* header, char* out, size_t cap);
// Constant-time comparison (doesn't leak the length of the matching prefix).
bool constantTimeEquals(const char* a, const char* b);
// Finds the Content-Length header in raw HTTP header bytes (not NUL-terminated; starts at the
// first header line, stops at the blank line). Case-insensitive. false if absent or malformed.
bool findContentLength(const char* headers, size_t len, uint32_t& out);
// Same raw header bytes: true only if a Content-Type header is present and its value starts with
// "multipart/" (case-insensitive). Absent or truncated before the value → false.
bool contentTypeIsMultipart(const char* headers, size_t len);

// Escalating brute-force lockout shared by PairingGuard and PresenceGate: the 5th failure in a
// row locks for 60 s, each further lockout doubles it (capped at 1 h). Only success() ends the
// escalation. Call update() regularly (the app does, every frame) so a lockout from long ago can
// never look active again when the 32-bit millisecond clock wraps (~49.7 days).
class EscalatingLockout {
 public:
  static constexpr uint8_t kMaxFailures = 5;
  static constexpr uint32_t kBaseMs = 60000;
  static constexpr uint32_t kMaxMs = 3600000;

  bool locked(uint32_t nowMs) const { return remainingMs(nowMs) > 0; }
  uint32_t remainingMs(uint32_t nowMs) const;
  void update(uint32_t nowMs);  // clears an expired lockout
  bool fail(uint32_t nowMs);    // records a failure; true when it starts a lockout
  void success() {
    failures_ = 0;
    lockMs_ = 0;
  }

 private:
  uint8_t failures_ = 0;
  bool locked_ = false;  // a lockout is in force (cleared by update() once it expires)
  uint32_t lockMs_ = 0;  // duration of the current/last lockout, kept for escalation; 0 = none
  uint32_t lockedAtMs_ = 0;
};

// Pairing code: 5 wrong codes in a row lock pairing out (EscalatingLockout: 60 s doubling up to
// 1 h, cleared by a correct code). The app rotates the code after each successful pairing.
class PairingGuard {
 public:
  enum class Result : uint8_t { Ok, BadCode, Locked };
  static constexpr uint8_t kMaxFailures = EscalatingLockout::kMaxFailures;
  static constexpr uint32_t kLockMs = EscalatingLockout::kBaseMs;  // first lockout

  void setCode(const char* code4);
  const char* code() const { return code_; }
  Result check(const char* code, uint32_t nowMs);
  uint32_t lockRemainingMs(uint32_t nowMs) const { return lock_.remainingMs(nowMs); }
  void update(uint32_t nowMs) { lock_.update(nowMs); }

 private:
  char code_[5] = "0000";
  EscalatingLockout lock_;
};

struct TokenEntry {
  char token[33];
  char host[33];
  uint32_t order;  // higher = more recent
};

// Up to 4 paired computers. Every pairing appends a new token (host names are truncated and
// may collide, so they never replace one another); when full, the oldest pairing is evicted.
class TokenStore {
 public:
  static constexpr uint8_t kMax = 4;
  void add(const char* token, const char* host);
  bool matches(const char* token) const;
  uint8_t count() const { return n_; }
  const TokenEntry& at(uint8_t i) const { return e_[i]; }
  void restore(const TokenEntry* entries, uint8_t n);
  void clear() { n_ = 0; }

 private:
  TokenEntry e_[kMax] = {};
  uint8_t n_ = 0;
};

// Physical presence code: when opening the /update gate (POST /update/open) or requesting a
// factory reset from the browser, the screen shows a 4-digit code, valid for 5 min; the POST
// needs it. Brute-force lockout (EscalatingLockout, survives re-opens): failures accumulate across
// re-opens (a new code does not grant fresh guesses); on the 5th the gate closes and refuses to
// open again for 60 s, doubling on each further lockout (capped at 1 h). Only a correct code
// resets the failures and the escalation.
class PresenceGate {
 public:
  enum class Purpose : uint8_t { Update, Reset, Settings, Wifi };
  static constexpr uint32_t kTtlMs = 300000;
  static constexpr uint8_t kMaxFailures = EscalatingLockout::kMaxFailures;
  static constexpr uint32_t kLockBaseMs = EscalatingLockout::kBaseMs;
  static constexpr uint32_t kLockMaxMs = EscalatingLockout::kMaxMs;

  // false (and nothing changes) while locked out, or while a code for another purpose is still
  // active (busyFor): nobody can replace a code the owner is reading off the screen. For the same
  // purpose an active code is kept as it is (same code, same timer). The owner can always ask
  // again once it expires (kTtlMs) or after it was used (close()).
  bool open(Purpose p, const char* code4, uint32_t nowMs);
  bool busyFor(Purpose p, uint32_t nowMs) const { return active(nowMs) && p != purpose_; }
  bool locked(uint32_t nowMs) const { return lock_.locked(nowMs); }
  uint32_t lockRemainingMs(uint32_t nowMs) const { return lock_.remainingMs(nowMs); }
  // Clears an expired lockout (see EscalatingLockout::update).
  void update(uint32_t nowMs) { lock_.update(nowMs); }
  bool active(uint32_t nowMs) const;
  Purpose purpose() const { return purpose_; }
  const char* code() const { return code_; }
  uint32_t remainingMs(uint32_t nowMs) const;
  bool check(Purpose p, const char* code, uint32_t nowMs);
  void close() { open_ = false; }

 private:
  bool open_ = false;
  Purpose purpose_ = Purpose::Update;
  char code_[5] = "";
  uint32_t openedAtMs_ = 0;
  EscalatingLockout lock_;
};

// Does OTA (/update) need the on-screen presence code? It does NOT only for a unit that was NEVER
// configured (no Wi-Fi joined from the portal and no pairing, ever — a marker that survives
// factory reset), that also has no saved Wi-Fi credentials and no pairings right now, reached over
// its own setup AP — so fresh shelf units can be updated in bulk. A unit that was configured once
// keeps requiring the code even after a factory reset (a reset unit in the field must not become
// updatable without physical presence). Any saved network, any pairing, or a request from another
// interface (the home LAN) also keeps the code (and its escalating lockout) mandatory.
bool otaCodeRequired(bool everConfigured, bool hasWifiCreds, uint8_t tokenCount, bool viaSoftAp);

// Does joining a network from the setup portal (POST /wifi) need the on-screen code? Not on a
// fresh unit (never configured, no saved network, not paired): setup stays frictionless. Any of
// those, though, and it does: the portal of a configured unit that lost its Wi-Fi is an open AP,
// and whoever is nearby must not be able to move it to their own network.
bool wifiCodeRequired(bool everConfigured, bool hasWifiCreds, uint8_t tokenCount);

// A token bucket that throttles how often an EXPENSIVE, UNAUTHENTICATED response is produced
// (the settings/portal page and /api/info), so a flood from an unpaired client on the LAN cannot
// starve the display loop into a watchdog reset. Authenticated endpoints (/api/state, /api/config)
// are already cheap to refuse (a 401 before any work) and are not throttled. allow() grants a
// token when one is available, refilling `ratePerSec` tokens per second up to `burst`; over the
// budget it returns false and the caller answers 429 at once. Safe across the 32-bit ms wrap.
class RateLimiter {
 public:
  RateLimiter(uint8_t burst, uint8_t ratePerSec) : burst_(burst), rate_(ratePerSec), tokens_(burst) {}
  bool allow(uint32_t nowMs);
  // Tokens available right now (for diagnostics/tests), without consuming one.
  uint8_t tokens(uint32_t nowMs);

 private:
  void refill(uint32_t nowMs);
  uint8_t burst_;
  uint8_t rate_;
  uint8_t tokens_;
  uint32_t lastMs_ = 0;
  bool started_ = false;
};

// One short-lived web session. To change settings from the browser a person proves physical
// presence by typing the code shown on the gadget's screen (PresenceGate, Purpose::Settings);
// on success a 128-bit token is issued and kept here, so the code is asked once per session, not
// once per save. The browser sends it back in the "X-Miblo-Web" header. Cleared on expiry, on a
// factory reset, or when a new session is issued (only one browser session at a time).
class WebSession {
 public:
  static constexpr uint32_t kTtlMs = 3600000;  // 1 h
  void issue(const uint8_t rnd[16], uint32_t nowMs);
  bool valid(const char* token, uint32_t nowMs) const;
  void clear() { token_[0] = 0; }

 private:
  char token_[33] = "";
  uint32_t issuedAtMs_ = 0;
};

// Did this request arrive over the unit's own setup AP? true only when the soft AP is up, its IP
// is in 192.168.4.0/24, the client (remote) IP is in that same /24 and the local address the
// request was accepted on is the soft-AP IP. IPv4 addresses as 4 octets, most significant first.
bool viaSoftApSubnet(bool apActive, const uint8_t remote[4], const uint8_t local[4], const uint8_t softAp[4]);

}  // namespace miblo
