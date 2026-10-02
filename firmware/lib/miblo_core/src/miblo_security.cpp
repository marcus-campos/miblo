#include "miblo_security.h"

#include <stdio.h>
#include <string.h>

#include "miblo_rom.h"
#include "miblo_utf8.h"

namespace miblo {

// The custom flag sits in the padding before `order`: four pairings cost no more RAM than before.
static_assert(sizeof(TokenEntry) == 72, "TokenEntry grew");

void formatCode(uint32_t rnd, char out[5]) {
  snprintf(out, 5, "%04u", (unsigned)(rnd % 10000));
}

void makeToken(const uint8_t rnd[16], char out[33]) {
  auto hex = [](int v) { return (char)(v < 10 ? '0' + v : 'a' + v - 10); };  // no table: it would sit in RAM
  for (int i = 0; i < 16; i++) {
    out[i * 2] = hex(rnd[i] >> 4);
    out[i * 2 + 1] = hex(rnd[i] & 0x0F);
  }
  out[32] = 0;
}

void tokenTag(const char* token, char out[9]) {
  uint64_t h = 0xcbf29ce484222325ull;
  for (const char* p = token ? token : ""; *p; p++) h = (h ^ (uint8_t)*p) * 0x100000001b3ull;
  snprintf(out, 9, "%08x", (unsigned)(h >> 32));
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
  static const char kName[] MIBLO_ROM = "content-length:";
  const size_t nameLen = sizeof(kName) - 1;
  bool found = false;
  size_t i = 0;
  while (i < len) {
    if (headers[i] == '\r' || headers[i] == '\n') break;  // blank line: end of headers
    bool match = len - i > nameLen;
    for (size_t k = 0; match && k < nameLen; k++) match = lower(headers[i + k]) == (char)mibloRomByte(kName + k);
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

// The characters String::trim() removes (isspace in the C locale).
static bool blank(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r'; }

// Case-insensitive compare of `n` bytes at `p` with a lowercase literal kept in flash.
static bool matchesLower(const char* p, const char* romLower, size_t n) {
  for (size_t k = 0; k < n; k++) {
    if (lower(p[k]) != (char)mibloRomByte(romLower + k)) return false;
  }
  return true;
}

// One Content-Type value [v, end) (after the ':'). Returns false if it is multipart without a
// usable boundary; `multipart` tells whether it is multipart at all.
static bool checkContentType(const char* v, const char* end, bool& multipart, char* out, size_t cap) {
  static const char kPrefix[] MIBLO_ROM = "multipart/";
  static const char kParam[] MIBLO_ROM = "boundary";
  const size_t prefixLen = sizeof(kPrefix) - 1;
  const size_t paramLen = sizeof(kParam) - 1;
  while (v < end && blank(*v)) v++;  // the server trims the value
  while (end > v && blank(end[-1])) end--;
  multipart = (size_t)(end - v) >= prefixLen && matchesLower(v, kPrefix, prefixLen);
  if (!multipart) return true;
  // The server's boundary: everything after the FIRST '=' of the value, with every '"' removed.
  const char* eq = v;
  while (eq < end && *eq != '=') eq++;
  if (eq == end) return false;
  // That '=' must be the boundary parameter's ("...; boundary=").
  const char* name = eq;
  while (name > v && (name[-1] == ' ' || name[-1] == '\t')) name--;
  if ((size_t)(name - v) < paramLen + 1) return false;
  name -= paramLen;
  if (!matchesLower(name, kParam, paramLen)) return false;
  if (name[-1] != ';' && name[-1] != ' ' && name[-1] != '\t') return false;
  size_t n = 0;
  for (const char* p = eq + 1; p < end; p++) {
    if (*p == '"') continue;
    if (out && n + 1 < cap) out[n] = *p;
    n++;
  }
  if (out && cap) out[n < cap ? n : cap - 1] = 0;
  return n >= 1 && n <= kMaxBoundary;
}

// The scan behind checkRequestHeaders; may leave a boundary copied whatever the verdict.
static HeaderVerdict scanHeaders(const char* h, size_t len, char* boundary, size_t cap) {
  static const char kName[] MIBLO_ROM = "content-type";
  const size_t nameLen = sizeof(kName) - 1;
  if (!h) return HeaderVerdict::Incomplete;
  uint8_t contentTypes = 0;  // saturates: any count above 1 is refused anyway
  bool multipart = false;
  bool bad = false;
  size_t i = 0;
  for (;;) {
    // The line, as the server reads it: up to '\r'.
    size_t e = i;
    while (e < len && h[e] != '\r') {
      if (h[e] == 0) return HeaderVerdict::Malformed;
      e++;
    }
    if (e >= len) return HeaderVerdict::Incomplete;
    if (e == i) break;  // empty line: end of the headers
    size_t colon = i;
    while (colon < e && h[colon] != ':') colon++;
    if (colon == e) break;  // no ':': the server stops reading headers here too
    if (colon - i == nameLen && matchesLower(h + i, kName, nameLen)) {
      if (contentTypes < 2) contentTypes++;
      bool isMultipart = false;
      // Copy only the first multipart's boundary; with several Content-Types the verdict is bad.
      if (!checkContentType(h + colon + 1, h + e, isMultipart, multipart ? nullptr : boundary, cap)) bad = true;
      multipart = multipart || isMultipart;
    }
    // The server then skips up to and including '\n'.
    size_t n = e + 1;
    while (n < len && h[n] != '\n') {
      if (h[n] == 0) return HeaderVerdict::Malformed;
      n++;
    }
    if (n >= len) return HeaderVerdict::Incomplete;
    i = n + 1;
  }
  if (!multipart) return HeaderVerdict::Plain;
  return bad || contentTypes > 1 ? HeaderVerdict::BadMultipart : HeaderVerdict::Multipart;
}

HeaderVerdict checkRequestHeaders(const char* headers, size_t len, char* boundary, size_t cap) {
  if (boundary && cap) boundary[0] = 0;
  const HeaderVerdict v = scanHeaders(headers, len, boundary, cap);
  if (v != HeaderVerdict::Multipart && boundary && cap) boundary[0] = 0;
  return v;
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
  seenSet_ &= (uint8_t)~(1u << slot);  // a new pairing: not seen yet
  strncpy(e.token, token, sizeof(e.token) - 1);
  e.token[sizeof(e.token) - 1] = 0;
  // Cut like a label (20 characters, never inside one); not clean text: a plain "computer", so
  // the row can still be renamed and removed by what the page shows.
  utf8Copy(e.host, sizeof(e.host), host, 20);
  if (!typedText(e.host, sizeof(e.host), 20)) strcpy(e.host, "computer");
  e.custom = false;
  e.order = order;
}

int TokenStore::find(const char* token) const {
  if (!token || !token[0]) return -1;
  int found = -1;
  for (uint8_t i = 0; i < n_; i++) {  // every entry compared: the time never tells which matched
    if (constantTimeEquals(token, e_[i].token) && found < 0) found = i;
  }
  return found;
}

bool TokenStore::remove(uint8_t i, const char* host) {
  if (i >= n_ || !host || strcmp(e_[i].host, host) != 0) return false;
  for (uint8_t j = i; j + 1 < n_; j++) {
    e_[j] = e_[j + 1];
    seenMs_[j] = seenMs_[j + 1];
  }
  // Bits above i move down one place; those below stay.
  const uint8_t low = seenSet_ & (uint8_t)((1u << i) - 1);
  seenSet_ = (uint8_t)(low | ((seenSet_ >> (i + 1)) << i));
  n_--;
  e_[n_] = TokenEntry{};
  return true;
}

TokenStore::RenameResult TokenStore::rename(uint8_t i, const char* host, const char* name) {
  if (i >= n_ || !host || strcmp(e_[i].host, host) != 0) return RenameResult::Changed;
  if (!typedText(name, sizeof(e_[i].host), 20)) return RenameResult::BadName;
  if (!name[0]) {
    e_[i].custom = false;  // automatic again: the label stays until the next snapshot
    return RenameResult::Ok;
  }
  strcpy(e_[i].host, name);
  e_[i].custom = true;
  return RenameResult::Ok;
}

bool TokenStore::autoLabel(uint8_t i, const char* host) {
  if (i >= n_ || e_[i].custom || !host || !host[0]) return false;
  char label[sizeof(e_[i].host)];
  utf8Copy(label, sizeof(label), host, 20);
  if (!typedText(label, sizeof(label), 20) || strcmp(label, e_[i].host) == 0) return false;
  strcpy(e_[i].host, label);
  dirty_ = true;
  return true;
}

void TokenStore::restore(const TokenEntry* entries, uint8_t n) {
  seenSet_ = 0;
  n_ = n > kMax ? kMax : n;
  for (uint8_t i = 0; i < n_; i++) e_[i] = entries[i];
}

bool PresenceGate::open(Purpose p, const char* code4, uint32_t nowMs) {
  lock_.update(nowMs);
  if (lock_.locked(nowMs)) return false;
  if (active(nowMs)) return p == purpose_;  // never replace a code that is on the screen
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

bool wifiCodeRequired(bool hasWifiCreds, uint8_t tokenCount) { return hasWifiCreds || tokenCount != 0; }

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

void tokensToJson(const TokenStore& tokens, JsonObject out) {
  JsonArray arr = out.createNestedArray("pairs");
  for (uint8_t i = 0; i < tokens.count(); i++) {
    const TokenEntry& t = tokens.at(i);
    JsonObject e = arr.createNestedObject();
    e["token"] = (const char*)t.token;
    e["host"] = (const char*)t.host;
    e["order"] = t.order;
    if (t.custom) e["c"] = true;
  }
}

void tokensFromJson(JsonObjectConst in, TokenStore& tokens) {
  TokenEntry entries[TokenStore::kMax] = {};
  uint8_t n = 0;
  for (JsonObjectConst e : in["pairs"].as<JsonArrayConst>()) {
    if (n >= TokenStore::kMax) break;
    const char* token = e["token"] | "";
    if (strlen(token) != 32) continue;
    TokenEntry& t = entries[n];
    memcpy(t.token, token, 33);
    utf8Copy(t.host, sizeof(t.host), e["host"] | "", 20);
    if (!typedText(t.host, sizeof(t.host), 20)) strcpy(t.host, "computer");  // never a label the page can't send back
    t.custom = e["c"] | false;
    t.order = e["order"] | 0;
    n++;
  }
  tokens.restore(entries, n);
}

}  // namespace miblo
