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
  size_t i = 0;
  while (i < len) {
    if (headers[i] == '\r' || headers[i] == '\n') return false;  // blank line: end of headers
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
      if (digits == 0) return false;
      out = (uint32_t)v;
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

uint32_t PairingGuard::lockRemainingMs(uint32_t nowMs) const {
  if (!locked_) return 0;
  uint32_t elapsed = nowMs - lockedAtMs_;
  return elapsed >= kLockMs ? 0 : kLockMs - elapsed;
}

PairingGuard::Result PairingGuard::check(const char* code, uint32_t nowMs) {
  if (locked_) {
    if (lockRemainingMs(nowMs) > 0) return Result::Locked;
    locked_ = false;
    failures_ = 0;
  }
  if (code && constantTimeEquals(code, code_)) {
    failures_ = 0;
    return Result::Ok;
  }
  if (++failures_ >= kMaxFailures) {
    locked_ = true;
    lockedAtMs_ = nowMs;
  }
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

uint32_t PresenceGate::lockRemainingMs(uint32_t nowMs) const {
  if (lockMs_ == 0) return 0;
  const uint32_t elapsed = nowMs - lockedAtMs_;
  return elapsed >= lockMs_ ? 0 : lockMs_ - elapsed;
}

bool PresenceGate::open(Purpose p, const char* code4, uint32_t nowMs) {
  if (locked(nowMs)) return false;
  open_ = true;
  purpose_ = p;
  strncpy(code_, code4, sizeof(code_) - 1);
  code_[sizeof(code_) - 1] = 0;
  openedAtMs_ = nowMs;
  failures_ = 0;
  return true;
}

bool PresenceGate::active(uint32_t nowMs) const {
  return open_ && (nowMs - openedAtMs_) < kTtlMs;
}

uint32_t PresenceGate::remainingMs(uint32_t nowMs) const {
  return active(nowMs) ? kTtlMs - (nowMs - openedAtMs_) : 0;
}

bool PresenceGate::check(Purpose p, const char* code, uint32_t nowMs) {
  if (locked(nowMs) || !active(nowMs) || p != purpose_) return false;
  if (code && constantTimeEquals(code, code_)) {
    lockMs_ = 0;  // a correct code ends the escalation
    return true;
  }
  if (++failures_ >= kMaxFailures) {
    open_ = false;
    lockMs_ = lockMs_ == 0 ? kLockBaseMs : (lockMs_ >= kLockMaxMs / 2 ? kLockMaxMs : lockMs_ * 2);
    lockedAtMs_ = nowMs;
  }
  return false;
}

}  // namespace miblo
