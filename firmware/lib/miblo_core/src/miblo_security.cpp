#include "miblo_security.h"

#include <stdio.h>
#include <string.h>

#include "miblo_rom.h"
#include "miblo_sha256.h"
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

// The characters String::trim() removes (isspace in the C locale).
static bool blank(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r'; }

LengthVerdict scanContentLength(const char* p, size_t end, uint32_t& out) {
  static const char kName[] MIBLO_ROM = "content-length:";
  const size_t nameLen = sizeof(kName) - 1;
  constexpr uint32_t kMaxLength = 0x7FFFFFFFu;  // atol's range on the device (32-bit long)
  bool found = false;
  out = 0;
  if (!p) return LengthVerdict::None;
  for (size_t line = 0; line < end;) {
    size_t eol = line;
    while (eol < end && p[eol] != '\n') eol++;
    if (eol >= end) break;  // unterminated: not a line yet
    bool match = eol - line >= nameLen;
    for (size_t k = 0; match && k < nameLen; k++) match = lower(p[line + k]) == (char)mibloRomByte(kName + k);
    if (match) {
      size_t a = line + nameLen, b = a;
      while (b < eol && p[b] != '\r') b++;  // the server's value ends at the first '\r'
      while (a < b && blank(p[a])) a++;
      while (b > a && blank(p[b - 1])) b--;
      if (a == b || b - a > 10) return LengthVerdict::Bad;  // empty, or more digits than 2^31 - 1 has
      uint64_t v = 0;
      for (size_t i = a; i < b; i++) {
        if (p[i] < '0' || p[i] > '9') return LengthVerdict::Bad;
        v = v * 10 + (uint64_t)(p[i] - '0');
      }
      if (v > kMaxLength) return LengthVerdict::Bad;
      if (found && (uint32_t)v != out) return LengthVerdict::Bad;  // duplicates must agree
      out = (uint32_t)v;
      found = true;
    }
    line = eol + 1;
  }
  return found ? LengthVerdict::Ok : LengthVerdict::None;
}

LengthVerdict readContentLength(const char* headers, size_t len, uint32_t& out) {
  size_t end = len;
  if (headers && len >= 2 && headers[0] == '\r' && headers[1] == '\n') {
    end = 2;  // no header lines at all
  } else if (headers) {
    for (size_t i = 3; i < len; i++) {
      if (headers[i] == '\n' && headers[i - 1] == '\r' && headers[i - 2] == '\n' && headers[i - 3] == '\r') {
        end = i + 1;
        break;
      }
    }
  }
  return scanContentLength(headers, end, out);
}

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

namespace {
bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isLabelChar(char c) { return isDigit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-'; }

// Dotted-quad IPv4 in p[0..n): four 0..255 parts of 1..3 digits.
bool ipv4Literal(const char* p, size_t n) {
  size_t i = 0;
  for (int part = 0; part < 4; part++) {
    if (part && (i >= n || p[i++] != '.')) return false;
    unsigned v = 0;
    size_t digits = 0;
    while (i < n && isDigit(p[i]) && digits < 4) v = v * 10 + (unsigned)(p[i++] - '0'), digits++;
    if (digits == 0 || digits > 3 || v > 255) return false;
  }
  return i == n;
}

// "[...]" with only hex digits, ':' and '.' inside.
bool ipv6Literal(const char* p, size_t n) {
  if (n < 3 || p[0] != '[' || p[n - 1] != ']') return false;
  for (size_t i = 1; i + 1 < n; i++) {
    const char c = lower(p[i]);
    if (!(isDigit(c) || (c >= 'a' && c <= 'f') || c == ':' || c == '.')) return false;
  }
  return true;
}

// p[0..n) is `id`, alone or followed by "." and dot-separated non-empty labels.
bool ownName(const char* p, size_t n, const char* id) {
  const size_t idLen = id ? strlen(id) : 0;
  if (!idLen || n < idLen) return false;
  for (size_t k = 0; k < idLen; k++)
    if (lower(p[k]) != lower(id[k])) return false;
  if (n == idLen) return true;
  if (p[idLen] != '.') return false;
  size_t label = 0;
  for (size_t i = idLen + 1; i < n; i++) {
    if (p[i] == '.') {
      if (!label) return false;
      label = 0;
    } else if (isLabelChar(p[i])) {
      label++;
    } else {
      return false;
    }
  }
  return label > 0;
}

bool hostSpan(const char* p, size_t n, const char* id) {
  if (n == 0) return false;
  // ":port" (1..5 digits), never inside a bracketed IPv6 literal.
  size_t colon = n;
  for (size_t i = n; i > 0; i--) {
    if (p[i - 1] == ':' || p[i - 1] == ']') {
      if (p[i - 1] == ':') colon = i - 1;
      break;
    }
  }
  if (colon < n) {
    const size_t digits = n - colon - 1;
    if (digits == 0 || digits > 5) return false;
    for (size_t i = colon + 1; i < n; i++)
      if (!isDigit(p[i])) return false;
    n = colon;
  }
  if (n && p[n - 1] == '.') n--;  // a fully qualified name's trailing dot
  return ipv4Literal(p, n) || ipv6Literal(p, n) || ownName(p, n, id);
}
}  // namespace

bool hostAllowed(const char* host, const char* id) {
  if (!host || !host[0]) return true;
  return hostSpan(host, strlen(host), id);
}

bool originAllowed(const char* origin, const char* id) {
  if (!origin || !origin[0]) return true;
  static const char kScheme[] MIBLO_ROM = "http://";
  const size_t schemeLen = sizeof(kScheme) - 1;
  const size_t n = strlen(origin);
  if (n <= schemeLen || !matchesLower(origin, kScheme, schemeLen)) return false;
  return hostSpan(origin + schemeLen, n - schemeLen, id);
}

HostVerdict judgeHost(const char* host, bool hostPresent, const char* origin, bool originPresent, bool pairedBearer,
                      const char* id) {
  if (pairedBearer) return HostVerdict::Ok;
  if (hostPresent && (!host || !hostAllowed(host, id))) return HostVerdict::WrongHost;
  if (originPresent && (!origin || !originAllowed(origin, id))) return HostVerdict::WrongOrigin;
  return HostVerdict::Ok;
}

bool acceptsHtml(const char* accept) {
  static const char kHtml[] MIBLO_ROM = "text/html";
  const size_t n = sizeof(kHtml) - 1;
  if (!accept) return false;
  for (const char* p = accept; strlen(p) >= n; p++) {
    if (matchesLower(p, kHtml, n)) return true;
  }
  return false;
}

size_t wrongHostReply(char* out, size_t cap, const char* status, const char* ip, bool html) {
  // Formats kept in flash (MIBLO_ROM), copied to the stack only for this call.
  static const char kLinkPage[] MIBLO_ROM =
      "<!doctype html><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width\">"
      "<title>Miblo</title><p>Miblo: <a href=\"http://%s/\">http://%s/</a></p>";
  static const char kPlainPage[] MIBLO_ROM = "<!doctype html><meta charset=\"utf-8\"><title>Miblo</title><p>Miblo</p>";
  static const char kJson[] MIBLO_ROM = "{\"error\":\"wrong host\"}";
  static const char kHead[] MIBLO_ROM =
      "HTTP/1.1 %s\r\nContent-Type: %s\r\nCache-Control: no-store\r\nConnection: close\r\n"
      "Content-Length: %u\r\n\r\n%s";
  static const char kHtmlType[] MIBLO_ROM = "text/html; charset=utf-8";
  static const char kJsonType[] MIBLO_ROM = "application/json";
  char fmt[sizeof(kLinkPage)];
  char body[200];
  if (html) {
    const size_t ipLen = ip ? strlen(ip) : 0;
    const bool link = ipLen > 0 && ipLen <= 15 && ipv4Literal(ip, ipLen);
    if (link) {
      mibloRomCopy(fmt, kLinkPage, sizeof(kLinkPage));
      snprintf(body, sizeof(body), fmt, ip, ip);
    } else {
      mibloRomCopy(body, kPlainPage, sizeof(kPlainPage));
    }
  } else {
    mibloRomCopy(body, kJson, sizeof(kJson));
  }
  char type[sizeof(kHtmlType)];
  if (html) mibloRomCopy(type, kHtmlType, sizeof(kHtmlType));
  else mibloRomCopy(type, kJsonType, sizeof(kJsonType));
  mibloRomCopy(fmt, kHead, sizeof(kHead));
  const int n = snprintf(out, cap, fmt, status, type, (unsigned)strlen(body), body);
  if (n < 0 || (size_t)n >= cap) {
    if (cap) out[0] = 0;
    return 0;
  }
  return (size_t)n;
}

void PairingGuard::setCode(const char* code4) {
  strncpy(code_, code4, sizeof(code_) - 1);
  code_[sizeof(code_) - 1] = 0;
}

uint32_t EscalatingLockout::remainingMs(uint32_t nowMs) const {
  if (!locked_) return 0;
  const uint32_t elapsed = nowMs - lockedAtMs_;
  return elapsed >= lockLenMs_ ? 0 : lockLenMs_ - elapsed;
}

void EscalatingLockout::update(uint32_t nowMs) {
  if (locked_ && remainingMs(nowMs) == 0) locked_ = false;  // expired: nothing left to wrap
}

bool EscalatingLockout::fail(uint32_t nowMs) {
  if (++failures_ < (lockMs_ == 0 ? kMaxFailures : kNextFailures)) return false;
  failures_ = 0;
  lockMs_ = lockMs_ == 0 ? kBaseMs : (lockMs_ >= kMaxMs / 2 ? kMaxMs : lockMs_ * 2);
  lockLenMs_ = lockMs_;
  lockedAtMs_ = nowMs;
  locked_ = true;
  return true;
}

LockoutState EscalatingLockout::save(uint32_t nowMs) const {
  LockoutState s{};
  s.lockMs = lockMs_;
  s.remainingMs = remainingMs(nowMs);
  s.failures = failures_;
  return s;
}

void EscalatingLockout::restore(const LockoutState& s, uint32_t nowMs) {
  if (s.lockMs > kMaxMs || s.remainingMs > s.lockMs || s.failures >= kMaxFailures) return;
  if (s.lockMs && s.lockMs < kBaseMs) return;
  lockMs_ = s.lockMs;
  failures_ = s.failures;
  locked_ = s.remainingMs > 0;
  lockLenMs_ = s.remainingMs;
  lockedAtMs_ = nowMs;
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

void TokenStore::add(const char* token, const char* host, TokenUndo* undo) {
  uint32_t order = 1;
  for (uint8_t i = 0; i < n_; i++) {
    if (e_[i].order >= order) order = e_[i].order + 1;
  }
  int slot = -1;
  const bool full = n_ >= kMax;
  if (!full) slot = n_++;
  if (slot < 0) {  // full: evict the oldest pairing
    slot = 0;
    for (uint8_t i = 1; i < n_; i++) {
      if (e_[i].order < e_[slot].order) slot = i;
    }
  }
  TokenEntry& e = e_[slot];
  if (undo) {
    undo->kind = !full ? TokenUndo::Kind::Added : TokenUndo::Kind::Replaced;
    undo->slot = (uint8_t)slot;
    undo->entry = e;
    undo->seen = (seenSet_ >> slot) & 1u;
    undo->seenMs = seenMs_[slot];
  }
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

bool TokenStore::remove(uint8_t i, const char* host, TokenUndo* undo) {
  if (undo) undo->kind = TokenUndo::Kind::None;
  if (i >= n_ || !host || strcmp(e_[i].host, host) != 0) return false;
  if (undo) {
    undo->kind = TokenUndo::Kind::Removed;
    undo->slot = i;
    undo->entry = e_[i];
    undo->seen = (seenSet_ >> i) & 1u;
    undo->seenMs = seenMs_[i];
  }
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

void TokenStore::undo(const TokenUndo& u) {
  const uint8_t bit = (uint8_t)(1u << u.slot);
  switch (u.kind) {
    case TokenUndo::Kind::None:
      return;
    case TokenUndo::Kind::Added:  // the new entry was appended last
      if (n_ == 0 || u.slot != n_ - 1) return;
      n_--;
      e_[n_] = TokenEntry{};
      seenSet_ &= (uint8_t)~bit;
      return;
    case TokenUndo::Kind::Replaced:  // the oldest was evicted for it: back in its place
      if (u.slot >= n_) return;
      e_[u.slot] = u.entry;
      seenMs_[u.slot] = u.seenMs;
      seenSet_ = u.seen ? (uint8_t)(seenSet_ | bit) : (uint8_t)(seenSet_ & ~bit);
      return;
    case TokenUndo::Kind::Removed: {  // back in its place; the ones after it move down again
      if (n_ >= kMax || u.slot > n_) return;
      for (uint8_t j = n_; j > u.slot; j--) {
        e_[j] = e_[j - 1];
        seenMs_[j] = seenMs_[j - 1];
      }
      const uint8_t low = seenSet_ & (uint8_t)(bit - 1);
      seenSet_ = (uint8_t)(low | ((seenSet_ >> u.slot) << (u.slot + 1)) | (u.seen ? bit : 0));
      e_[u.slot] = u.entry;
      seenMs_[u.slot] = u.seenMs;
      n_++;
      return;
    }
  }
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

namespace {
bool lowerHex(const char* s, size_t n) {
  if (!s || strlen(s) != n) return false;
  for (size_t i = 0; i < n; i++) {
    if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return false;
  }
  return true;
}
}  // namespace

ChallengeResult answerChallenge(const TokenStore& tokens, const char* nonce, const char* tag, const char* id,
                                const char* ip, char macHex[65]) {
  macHex[0] = 0;
  if (!lowerHex(nonce, 32) || !lowerHex(tag, 8) || !id) return ChallengeResult::BadRequest;
  if (!ip || !ip[0] || strcmp(ip, "0.0.0.0") == 0) return ChallengeResult::NoNetwork;
  char tail[64];  // id || ip: the second HMAC part (the id is <= 32, an IPv4 <= 15 characters)
  if (snprintf(tail, sizeof(tail), "%s%s", id, ip) >= (int)sizeof(tail)) return ChallengeResult::BadRequest;
  char dummy[33];  // on the stack: a const table would sit in RAM for good
  memset(dummy, '0', 32);
  dummy[32] = 0;
  const char* key = dummy;
  bool known = false;
  for (uint8_t i = 0; i < tokens.count(); i++) {  // every entry: the time never tells which matched
    char t[9];
    tokenTag(tokens.at(i).token, t);
    if (constantTimeEquals(t, tag) && !known) {
      key = tokens.at(i).token;
      known = true;
    }
  }
  uint8_t mac[32];
  hmacSha256(reinterpret_cast<const uint8_t*>(key), strlen(key), nonce, 32, tail, strlen(tail), mac);
  if (!known) return ChallengeResult::UnknownTag;
  auto hex = [](int v) { return (char)(v < 10 ? '0' + v : 'a' + v - 10); };
  for (int i = 0; i < 32; i++) {
    macHex[i * 2] = hex(mac[i] >> 4);
    macHex[i * 2 + 1] = hex(mac[i] & 15);
  }
  macHex[64] = 0;
  return ChallengeResult::Ok;
}

bool PresenceGate::open(Purpose p, const char* code4, uint32_t nowMs) {
  EscalatingLockout& lock = lock_[idx(p)];
  lock.update(nowMs);
  if (lock.locked(nowMs)) return false;
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
  EscalatingLockout& lock = lock_[idx(p)];
  lock.update(nowMs);
  if (lock.locked(nowMs) || !active(nowMs) || p != purpose_) return false;
  if (code && constantTimeEquals(code, code_)) {
    lock.success();  // a correct code ends the escalation
    return true;
  }
  if (lock.fail(nowMs)) open_ = false;
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
