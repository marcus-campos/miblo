#pragma once
#include <stddef.h>
#include <stdint.h>

namespace miblo {

// "0042" a partir de um número aleatório.
void formatCode(uint32_t rnd, char out[5]);
// Token de 128 bits em hexadecimal minúsculo (32 caracteres + NUL).
void makeToken(const uint8_t rnd[16], char out[33]);
// "Bearer <token>" → token. false se o cabeçalho não tiver esse formato ou o token não couber.
bool bearerToken(const char* header, char* out, size_t cap);
// Comparação em tempo constante (não vaza o tamanho do prefixo correto).
bool constantTimeEquals(const char* a, const char* b);
// Finds the Content-Length header in raw HTTP header bytes (not NUL-terminated; starts at the
// first header line, stops at the blank line). Case-insensitive. false if absent or malformed.
bool findContentLength(const char* headers, size_t len, uint32_t& out);

// Código de pareamento: 5 erros seguidos bloqueiam novas tentativas por 60 s (spec §5.4).
class PairingGuard {
 public:
  enum class Result : uint8_t { Ok, BadCode, Locked };
  static constexpr uint8_t kMaxFailures = 5;
  static constexpr uint32_t kLockMs = 60000;

  void setCode(const char* code4);
  const char* code() const { return code_; }
  Result check(const char* code, uint32_t nowMs);
  uint32_t lockRemainingMs(uint32_t nowMs) const;

 private:
  char code_[5] = "0000";
  uint8_t failures_ = 0;
  bool locked_ = false;
  uint32_t lockedAtMs_ = 0;
};

struct TokenEntry {
  char token[33];
  char host[33];
  uint32_t order;  // maior = mais recente
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

// Código de presença física: ao abrir /update (ou pedir o reset de fábrica) pelo navegador, a
// tela mostra um código de 4 dígitos, válido por 5 min; o POST precisa dele. 5 erros fecham o portão.
// Brute-force lockout (survives re-opens): failures accumulate across re-opens (a new code does
// not grant fresh guesses); on the 5th the gate closes and refuses to open again for 60 s,
// doubling on each further lockout (capped at 1 h). Only a correct code resets the failures and
// the escalation.
class PresenceGate {
 public:
  enum class Purpose : uint8_t { Update, Reset };
  static constexpr uint32_t kTtlMs = 300000;
  static constexpr uint8_t kMaxFailures = 5;
  static constexpr uint32_t kLockBaseMs = 60000;
  static constexpr uint32_t kLockMaxMs = 3600000;

  // false (and nothing changes) while locked out.
  bool open(Purpose p, const char* code4, uint32_t nowMs);
  bool locked(uint32_t nowMs) const { return lockRemainingMs(nowMs) > 0; }
  uint32_t lockRemainingMs(uint32_t nowMs) const;
  // Clears an expired lockout. Call regularly (the app does, every frame) so a lockout from long
  // ago can never look active again when the 32-bit millisecond clock wraps (~49.7 days).
  void update(uint32_t nowMs);
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
  uint8_t failures_ = 0;
  bool locked_ = false;   // a lockout is in force (cleared by update() once it expires)
  uint32_t lockMs_ = 0;   // duration of the current/last lockout, kept for escalation; 0 = none
  uint32_t lockedAtMs_ = 0;
};

}  // namespace miblo
