#include "miblo_security.h"

#include <stdio.h>
#include <string.h>

namespace miblo {

void formatCode(uint32_t rnd, char out[5]) {
  snprintf(out, 5, "%04u", (unsigned)(rnd % 10000));
}

void makeToken(const uint8_t rnd[16], char out[33]) {
  static const char hex[] = "0123456789abcdef";
  for (int i = 0; i < 16; i++) {
    out[i * 2] = hex[rnd[i] >> 4];
    out[i * 2 + 1] = hex[rnd[i] & 0x0F];
  }
  out[32] = 0;
}

bool bearerToken(const char* header, char* out, size_t cap) {
  if (!header || strncmp(header, "Bearer ", 7) != 0) return false;
  const char* t = header + 7;
  while (*t == ' ') t++;
  size_t len = strlen(t);
  while (len > 0 && t[len - 1] == ' ') len--;
  if (len == 0 || len >= cap) return false;
  memcpy(out, t, len);
  out[len] = 0;
  return true;
}

bool constantTimeEquals(const char* a, const char* b) {
  size_t la = strlen(a);
  size_t lb = strlen(b);
  uint8_t diff = (uint8_t)(la != lb);
  for (size_t i = 0; i < la; i++) diff |= (uint8_t)(a[i] ^ b[i % (lb ? lb : 1)]);
  return diff == 0;
}

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

bool findContentLength(const char* headers, size_t len, uint32_t& out) {
  static const char kName[] = "content-length:";
  const size_t nameLen = sizeof(kName) - 1;
  bool found = false;
  size_t i = 0;
  while (i < len) {
    if (headers[i] == '\r' || headers[i] == '\n') break;  // blank line: end of headers
    bool match = len - i > nameLen;
    for (size_t k = 0; match && k < nameLen; k++) match = lower(headers[i + k]) == kName[k];
    if (match) {
      size_t j = i + nameLen;
      while (j < len && (headers[j] == ' ' || headers[j] == '\t')) j++;
      uint64_t v = 0;
      size_t digits = 0;
      while (j < len && headers[j] >= '0' && headers[j] <= '9') {
        v = v * 10 + (uint64_t)(headers[j] - '0');
        if (v > 0xFFFFFFFFull) v = 0xFFFFFFFFull;  // saturate: "huge" is all callers need
        j++;
        digits++;
      }
      if (digits == 0) return false;  // malformed: let the server's own parser deal with it
      if (!found || (uint32_t)v > out) out = (uint32_t)v;  // duplicates: keep the largest
      found = true;
    }
    while (i < len && headers[i] != '\n') i++;  // next line
    i++;
  }
  return found;
}

bool contentTypeIsMultipart(const char* headers, size_t len) {
  static const char kName[] = "content-type:";
  static const char kPrefix[] = "multipart/";
  const size_t nameLen = sizeof(kName) - 1;
  const size_t prefixLen = sizeof(kPrefix) - 1;
  size_t i = 0;
  while (i < len) {
    if (headers[i] == '\r' || headers[i] == '\n') break;  // blank line: end of headers
    bool match = len - i > nameLen;
    for (size_t k = 0; match && k < nameLen; k++) match = lower(headers[i + k]) == kName[k];
    if (match) {
      size_t j = i + nameLen;
      while (j < len && (headers[j] == ' ' || headers[j] == '\t')) j++;
      if (len - j < prefixLen) return false;
      for (size_t k = 0; k < prefixLen; k++) {
        if (lower(headers[j + k]) != kPrefix[k]) return false;
      }
      return true;
    }
    while (i < len && headers[i] != '\n') i++;  // next line
    i++;
  }
  return false;
}

void PairingGuard::setCode(const char* code4) {
  strncpy(code_, code4, sizeof(code_) - 1);
  code_[sizeof(code_) - 1] = 0;
}

uint32_t EscalatingLockout::remainingMs(uint32_t nowMs) const {
  if (!locked_) return 0;
  const uint32_t elapsed = nowMs - lockedAtMs_;
  return elapsed >= lockMs_ ? 0 : lockMs_ - elapsed;
}

void EscalatingLockout::update(uint32_t nowMs) {
  if (locked_ && remainingMs(nowMs) == 0) locked_ = false;  // expired: nothing left to wrap
}

bool EscalatingLockout::fail(uint32_t nowMs) {
  if (++failures_ < kMaxFailures) return false;
  failures_ = 0;
  lockMs_ = lockMs_ == 0 ? kBaseMs : (lockMs_ >= kMaxMs / 2 ? kMaxMs : lockMs_ * 2);
  lockedAtMs_ = nowMs;
  locked_ = true;
  return true;
}

PairingGuard::Result PairingGuard::check(const char* code, uint32_t nowMs) {
  lock_.update(nowMs);
  if (lock_.locked(nowMs)) return Result::Locked;
  if (code && constantTimeEquals(code, code_)) {
    lock_.success();
    return Result::Ok;
  }
  lock_.fail(nowMs);
  return Result::BadCode;
}

void TokenStore::add(const char* token, const char* host) {
  uint32_t order = 1;
  for (uint8_t i = 0; i < n_; i++) {
    if (e_[i].order >= order) order = e_[i].order + 1;
  }
  int slot = -1;
  if (n_ < kMax) slot = n_++;
  if (slot < 0) {  // full: evict the oldest pairing
    slot = 0;
    for (uint8_t i = 1; i < n_; i++) {
      if (e_[i].order < e_[slot].order) slot = i;
    }
  }
  TokenEntry& e = e_[slot];
  strncpy(e.token, token, sizeof(e.token) - 1);
  e.token[sizeof(e.token) - 1] = 0;
  strncpy(e.host, host, sizeof(e.host) - 1);
  e.host[sizeof(e.host) - 1] = 0;
  e.order = order;
}

bool TokenStore::matches(const char* token) const {
  if (!token || !token[0]) return false;
  bool ok = false;
  for (uint8_t i = 0; i < n_; i++) ok |= constantTimeEquals(token, e_[i].token);
  return ok;
}

void TokenStore::restore(const TokenEntry* entries, uint8_t n) {
  n_ = n > kMax ? kMax : n;
  for (uint8_t i = 0; i < n_; i++) e_[i] = entries[i];
}

bool PresenceGate::open(Purpose p, const char* code4, uint32_t nowMs) {
  lock_.update(nowMs);
  if (lock_.locked(nowMs)) return false;
  open_ = true;
  purpose_ = p;
  strncpy(code_, code4, sizeof(code_) - 1);
  code_[sizeof(code_) - 1] = 0;
  openedAtMs_ = nowMs;
  // The failure count is deliberately kept: re-opening (a new code) must not grant 5 fresh guesses.
  return true;
}

bool PresenceGate::active(uint32_t nowMs) const {
  return open_ && (nowMs - openedAtMs_) < kTtlMs;
}

uint32_t PresenceGate::remainingMs(uint32_t nowMs) const {
  return active(nowMs) ? kTtlMs - (nowMs - openedAtMs_) : 0;
}

bool PresenceGate::check(Purpose p, const char* code, uint32_t nowMs) {
  lock_.update(nowMs);
  if (lock_.locked(nowMs) || !active(nowMs) || p != purpose_) return false;
  if (code && constantTimeEquals(code, code_)) {
    lock_.success();  // a correct code ends the escalation
    return true;
  }
  if (lock_.fail(nowMs)) open_ = false;
  return false;
}

bool otaCodeRequired(bool everConfigured, bool hasWifiCreds, uint8_t tokenCount, bool viaSoftAp) {
  return everConfigured || hasWifiCreds || tokenCount != 0 || !viaSoftAp;
}

bool viaSoftApSubnet(bool apActive, const uint8_t remote[4], const uint8_t local[4], const uint8_t softAp[4]) {
  if (!apActive) return false;
  static const uint8_t kNet[3] = {192, 168, 4};
  for (int i = 0; i < 3; i++) {
    if (softAp[i] != kNet[i] || remote[i] != kNet[i]) return false;
  }
  for (int i = 0; i < 4; i++) {
    if (local[i] != softAp[i]) return false;
  }
  return true;
}


void WebSession::issue(const uint8_t rnd[16], uint32_t nowMs) {
  makeToken(rnd, token_);
  issuedAtMs_ = nowMs;
}

bool WebSession::valid(const char* token, uint32_t nowMs) const {
  if (!token_[0] || !token || nowMs - issuedAtMs_ >= kTtlMs) return false;
  return constantTimeEquals(token_, token);
}

void RateLimiter::refill(uint32_t nowMs) {
  if (!started_) {
    started_ = true;
    lastMs_ = nowMs;
    return;
  }
  const uint32_t elapsed = nowMs - lastMs_;  // wrap-safe (unsigned)
  if (elapsed < 1000 || rate_ == 0) return;
  const uint32_t add = (elapsed / 1000) * rate_;
  uint32_t t = tokens_ + add;
  tokens_ = t > burst_ ? burst_ : (uint8_t)t;
  lastMs_ += (elapsed / 1000) * 1000;  // keep the sub-second remainder
}

bool RateLimiter::allow(uint32_t nowMs) {
  refill(nowMs);
  if (tokens_ == 0) return false;
  tokens_--;
  return true;
}

uint8_t RateLimiter::tokens(uint32_t nowMs) {
  refill(nowMs);
  return tokens_;
}

}  // namespace miblo
