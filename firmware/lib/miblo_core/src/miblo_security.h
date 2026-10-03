#pragma once
#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

namespace miblo {

// "0042" from a random number.
void formatCode(uint32_t rnd, char out[5]);
// 128-bit token as lowercase hex (32 characters + NUL).
void makeToken(const uint8_t rnd[16], char out[33]);
// "Bearer <token>" → token. false if the header isn't in that format or the token doesn't fit.
bool bearerToken(const char* header, char* out, size_t cap);
// The settings page's "(this computer)" tag: the first 8 hex of FNV-1a-64 over the token (the
// plugin puts the same after #me=). Only 32 bits of a 128-bit random token: it tells the rows
// apart and cannot give the token back (2^96 tokens share each tag). nullptr hashes as "".
void tokenTag(const char* token, char out[9]);
// Constant-time comparison (doesn't leak the length of the matching prefix).
bool constantTimeEquals(const char* a, const char* b);
// A request's Content-Length, judged exactly as ESP8266WebServer will read it, or refused.
//
// The server takes the LAST Content-Length line, String::trim()s its value (isspace: also \v and
// \f) and converts it with toInt() (atol) into a uint32_t: "-1" becomes 0xFFFFFFFF, "+60000"
// 60000, "1e3" 1. Any scanner that reads the value differently lets a request past the body
// guards (size, heap, the read-ahead's body wait) and into the server's blocking, unbounded body
// read. So the only values accepted are the ones where every reading agrees:
//   Ok    every Content-Length line holds, after the server's trim, 1..10 ASCII digits worth at
//         most 2^31 - 1, and they all hold the same number (`out`).
//   Bad   one of them does not (sign, inner blank, empty, hex, exponent, overflow, other bytes), or
//         two disagree: the request must be refused (400) before the server reads it.
//   None  no Content-Length line.
// Lines are split at '\n', a value ends at its first '\r' (the server reads a line up to '\r',
// then skips to '\n': each of its header lines starts where one of these does, and a value that
// is plain digits here is the same number to atol there). Case-insensitive name, no blank before
// the ':' (as the server's equalsIgnoreCase).
enum class LengthVerdict : uint8_t { None, Ok, Bad };
// Over the lines in p[0..end) (unterminated last line ignored).
LengthVerdict scanContentLength(const char* p, size_t end, uint32_t& out);
// Over raw header bytes starting at the first header line (not NUL-terminated), up to the blank
// line that ends the block (or the last complete line in `len` when it has not all arrived).
LengthVerdict readContentLength(const char* headers, size_t len, uint32_t& out);

// What ESP8266WebServer will make of a request's header block, judged from raw header bytes (the
// first TCP segment, or the block read ahead: miblo_headers.h). The scan follows the server's own reading
// exactly: a line ends at '\r' and the rest up to '\n' is skipped; an empty line, or a line
// without ':', ends the headers. For every body method the server parses a multipart/... body with
// _parseForm, which puts the boundary on the stack (a VLA): an unbounded boundary is a crash.
//   Plain        complete block, no multipart Content-Type.
//   Multipart    complete block with exactly one Content-Type, multipart/..., whose value has
//                "boundary=" as its first parameter and a boundary of 1..kMaxBoundary characters
//                as the server sees it (everything after the first '=', quotes dropped, trimmed).
//   BadMultipart complete block with a multipart Content-Type that is not like that (missing,
//                empty or long boundary, another '=' first, or more than one Content-Type).
//   Incomplete   the block does not end within the bytes: headers not seen here could still carry
//                a multipart Content-Type (a later one wins), so nothing can be said.
//   Malformed    a NUL byte inside the header lines (the server's String functions would read
//                such a line differently from this scan).
// Case-insensitive (the server matches "multipart/" case-sensitively: this is stricter). One pass
// over `len` bytes. When the verdict is Multipart and `boundary` is given, the boundary is copied
// there (truncated to cap - 1 characters, NUL-terminated).
enum class HeaderVerdict : uint8_t { Plain, Multipart, BadMultipart, Incomplete, Malformed };
constexpr size_t kMaxBoundary = 70;  // RFC 2046
HeaderVerdict checkRequestHeaders(const char* headers, size_t len, char* boundary = nullptr, size_t cap = 0);

// DNS rebinding and cross-site requests (M1). A web page cannot read the gadget's replies, but
// one whose own name was rebound to the gadget's IP can: its requests then carry the attacker's
// name in Host. So a request is served only when its Host is
//   - absent or empty (a browser always sends one);
//   - an IP literal: dotted-quad IPv4, or a bracketed IPv6;
//   - the gadget's id (its mDNS name and DHCP host name, "miblo-4f2a"), alone ("miblo-4f2a", from
//     a router's DNS) or as "miblo-4f2a.local". No other domain ("miblo-4f2a.fritz.box",
//     "miblo-4f2a.lan"): the id is only 16 bits, so "<id>.<any domain>" would let a rebinding
//     domain guess its way in (F2). The plugin's bearer token skips this check (judgeHost);
// each with an optional ":port" and trailing dot, case-insensitive. The setup AP (captive portal)
// is the caller's exception: there any Host is answered (and redirected).
bool hostAllowed(const char* host, const char* id);
// An Origin header (sent by browsers on cross-origin requests and on every POST): absent, or
// "http://" + an allowed host (hostAllowed), nothing after it. "null", https and anything else are
// another site.
bool originAllowed(const char* origin, const char* id);

// What an EscalatingLockout keeps across a restart (RTC memory on the device: it survives a reset
// or a crash, not a power cut). Plain data; restore() checks every field.
struct LockoutState {
  uint32_t lockMs;       // the current/last lockout's length (escalation), 0 = none yet
  uint32_t remainingMs;  // of the lockout in force, 0 = none
  uint8_t failures;
  uint8_t reserved[3];
};

// The whole Host/Origin decision for one request (the hook, web.cpp foreignRequest). `*Present`:
// the header is there (its value may be nullptr when it did not fit: refused, never "absent").
// `pairedBearer`: the request carries a bearer token of a paired computer; a rebinding page
// cannot know one, so the plugin keeps working through a custom DNS alias (Pi-hole, a router
// alias, Tailscale MagicDNS). A browser (the pages, the web session) needs the IP or <id>.local.
enum class HostVerdict : uint8_t { Ok, WrongHost, WrongOrigin };
HostVerdict judgeHost(const char* host, bool hostPresent, const char* origin, bool originPresent, bool pairedBearer,
                      const char* id);
// Whether an Accept header asks for HTML (a browser navigating), case-insensitive.
bool acceptsHtml(const char* accept);
// The full refusal for a wrong Host/Origin, written into out: `status` ("421 Misdirected
// Request"), then for a browser (html) a short page: "Open Miblo at http://<ip>/ (a link) or
// http://<id>.local/" (the ip only when a dotted IPv4, the id only when a plain host label), else
// {"error":"wrong host"}. Returns its length, 0 if it does not fit in cap (never cut). 448 bytes
// always fit.
size_t wrongHostReply(char* out, size_t cap, const char* status, const char* ip, const char* id, bool html);

// Escalating brute-force lockout behind PairingGuard and each PresenceGate purpose (M2). The 5th
// failure in a row locks for 60 s; from then on every 3rd failure locks again, each lockout twice
// as long as the last, up to 24 h. Only success() ends the escalation. Call update() regularly (the
// app does, every frame) so a lockout from long ago can never look active again when the 32-bit
// millisecond clock wraps (~49.7 days).
//
// Against a 4-digit code (10^4 values): reaching the 24 h cap takes 5 + 3 x 11 = 38 guesses over
// ~34 h (60 s + 2 + 4 ... + 1024 min); from then on 3 guesses a day. A 50 % chance needs ~5000
// guesses against a fixed code (the pairing code, which changes only when a pairing succeeds):
// ~4.5 years; against a code drawn afresh each time (the presence codes) ~6900 guesses, ~6.3
// years. (Before: 5 guesses an hour after a 1 h cap, 50 % in ~42 days.) The counters are kept
// across a reset (RTC memory), so a crash or a reboot does not give fresh guesses either.
class EscalatingLockout {
 public:
  static constexpr uint8_t kMaxFailures = 5;   // before the first lockout
  static constexpr uint8_t kNextFailures = 3;  // before each further one
  static constexpr uint32_t kBaseMs = 60000;
  static constexpr uint32_t kMaxMs = 86400000;  // 24 h

  bool locked(uint32_t nowMs) const { return remainingMs(nowMs) > 0; }
  uint32_t remainingMs(uint32_t nowMs) const;
  void update(uint32_t nowMs);  // clears an expired lockout
  bool fail(uint32_t nowMs);    // records a failure; true when it starts a lockout
  void success() {
    failures_ = 0;
    lockMs_ = 0;
  }
  LockoutState save(uint32_t nowMs) const;
  // Takes a saved state back as of nowMs (a lockout in force runs its full remaining time again:
  // the time the unit was off is unknown). A state out of range is ignored.
  void restore(const LockoutState& s, uint32_t nowMs);

 private:
  uint8_t failures_ = 0;
  bool locked_ = false;  // a lockout is in force (cleared by update() once it expires)
  uint32_t lockMs_ = 0;  // duration of the current/last lockout, kept for escalation; 0 = none
  uint32_t lockedAtMs_ = 0;
  uint32_t lockLenMs_ = 0;  // this lockout's length (lockMs_, or less after a restore)
};

// Pairing code: wrong codes lock pairing out (EscalatingLockout: 60 s doubling up to 24 h,
// cleared by a correct code). The app rotates the code after each successful pairing.
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
  EscalatingLockout& lockout() { return lock_; }

 private:
  char code_[5] = "0000";
  EscalatingLockout lock_;
};

struct TokenEntry {
  char token[33];
  char host[33];  // the label the settings page shows: the computer's host name, or the user's
  bool custom;    // the user named it (settings page): snapshots no longer relabel it
  uint32_t order;  // higher = more recent
};

// What one add() or remove() changed, so a change whose save failed can be taken back (undo()):
// the RAM then never trusts a computer the flash would forget, nor keeps out one it would restore.
struct TokenUndo {
  enum class Kind : uint8_t { None, Added, Replaced, Removed };
  Kind kind = Kind::None;
  uint8_t slot = 0;
  TokenEntry entry{};  // the entry replaced or removed
  bool seen = false;   // its "seen" state
  uint32_t seenMs = 0;
};

// Up to 4 paired computers. Every pairing appends a new token (host names are truncated and
// may collide, so they never replace one another); when full, the oldest pairing is evicted.
class TokenStore {
 public:
  static constexpr uint8_t kMax = 4;
  // `host` is cut to 20 characters; one that is not clean text (miblo::typedText) is "computer".
  void add(const char* token, const char* host, TokenUndo* undo = nullptr);
  bool matches(const char* token) const { return find(token) >= 0; }
  int find(const char* token) const;  // its place in the list, -1 if none (constant time)
  uint8_t count() const { return n_; }
  const TokenEntry& at(uint8_t i) const { return e_[i]; }
  void restore(const TokenEntry* entries, uint8_t n);
  void clear() { n_ = 0; }
  // The settings page's list: remove the computer at place i, only if that place still holds
  // `host` (the list may have changed since the page read it). The others keep their tokens.
  bool remove(uint8_t i, const char* host, TokenUndo* undo = nullptr);
  // Takes back the add() or remove() that filled `u` (the last change only).
  void undo(const TokenUndo& u);
  // The settings page names a computer: place i, only if it still holds `host` (as remove()).
  // `name` follows a gadget name's rules (<= 20 characters, no control characters, fits the
  // label); "" hands the label back to the computer (automatic: its next snapshot names it).
  enum class RenameResult : uint8_t { Ok, Changed, BadName };
  RenameResult rename(uint8_t i, const char* host, const char* name);
  // A snapshot from computer i says its host name: an automatic label follows it (cut to 20
  // characters, like the pairing's). true when the label changed; the change is in RAM until
  // saveDue() says to store it.
  bool autoLabel(uint8_t i, const char* host);
  // Automatic labels are stored at most once a minute (flash wear): true when one changed and
  // the last save is a minute old (or none yet). saved() after storing.
  static constexpr uint32_t kAutoSaveGapMs = 60000;
  bool saveDue(uint32_t nowMs) const {
    return dirty_ && (!savedOnce_ || nowMs - savedAtMs_ >= kAutoSaveGapMs);
  }
  void saved(uint32_t nowMs) {
    dirty_ = false;
    savedOnce_ = true;
    savedAtMs_ = nowMs;
  }
  // When each computer's token last came in (millis(); since boot, never stored).
  void seen(uint8_t i, uint32_t nowMs) {
    if (i < n_) seenMs_[i] = nowMs, seenSet_ |= (uint8_t)(1u << i);
  }
  bool everSeen(uint8_t i) const { return i < n_ && (seenSet_ >> i) & 1u; }
  uint32_t seenAt(uint8_t i) const { return seenMs_[i]; }

 private:
  TokenEntry e_[kMax] = {};
  uint8_t n_ = 0;
  uint32_t seenMs_[kMax] = {};
  uint8_t seenSet_ = 0;  // bit i: entry i has been seen since boot
  bool dirty_ = false;      // an automatic label changed and is not stored yet
  bool savedOnce_ = false;
  uint32_t savedAtMs_ = 0;
};

// pairs.json: {"pairs":[{"token","host","order","c":true (custom label only)}]}. Reading skips an
// entry without a 32-character token; a file from before the custom flag loads as automatic.
void tokensToJson(const TokenStore& tokens, JsonObject out);
void tokensFromJson(JsonObjectConst in, TokenStore& tokens);

// GET /api/challenge?n=<nonce>&t=<tag> (M3, F1): lets a computer check that an address answers as
// the gadget it paired with BEFORE it sends its token there (the plugin follows a gadget that moved,
// found again by mDNS, which any LAN host can answer). Contract v2 (plugin/lib/relocation.js):
//   n    exactly 32 lowercase hex characters (a fresh random nonce)
//   t    exactly 8 lowercase hex characters: tokenTag() of the caller's token
//   200  {"id":"<device id>","ip":"<station IPv4, dotted>","v":2,"mac":"<64 lowercase hex>"},
//        mac = HMAC-SHA256(key = the token's 32 ASCII characters, message = the 32 nonce
//        characters immediately followed by the id, immediately followed by the ip, e.g.
//        "0123...cdef" "miblo-4f2a" "192.168.15.181")
//   400  n or t malformed;  409 {"error":"no network"} no station IP (setup mode);  403  no paired
//        token has that tag (never 404: that means a firmware without the route, which the plugin
//        treats differently);  429  over the rate limit.
// Only someone holding the token can compute the mac, and the token never travels. The ip is the
// gadget's OWN station address (WiFi.localIP()): the plugin compares it with the address it
// connected to, so a spoofer that relays the nonce to the real gadget gets back a mac over the
// real gadget's IP, never its own (F1). Every stored token's tag is compared and a mac is always
// computed (a dummy key when none matches), so the time does not tell a known tag from an unknown.
enum class ChallengeResult : uint8_t { Ok, BadRequest, UnknownTag, NoNetwork };
ChallengeResult answerChallenge(const TokenStore& tokens, const char* nonce, const char* tag, const char* id,
                                const char* ip, char macHex[65]);

// Physical presence code: when opening the /update gate (POST /update/open), requesting a factory
// reset, unlocking the settings page or joining a network from the portal, the screen shows a
// 4-digit code, valid for 5 min; the POST needs it. Each purpose has its own brute-force lockout
// (EscalatingLockout, L2): a stranger guessing the settings code cannot keep the owner from
// updating, resetting or moving the unit's Wi-Fi. Failures accumulate across re-opens (a new code
// does not grant fresh guesses); a lockout closes the gate and its purpose cannot open again until
// it ends. Only a correct code for that purpose resets its failures and escalation.
class PresenceGate {
 public:
  enum class Purpose : uint8_t { Update, Reset, Settings, Wifi };
  static constexpr uint8_t kPurposes = 4;
  static constexpr uint32_t kTtlMs = 300000;
  static constexpr uint8_t kMaxFailures = EscalatingLockout::kMaxFailures;
  static constexpr uint32_t kLockBaseMs = EscalatingLockout::kBaseMs;
  static constexpr uint32_t kLockMaxMs = EscalatingLockout::kMaxMs;

  // false (and nothing changes) while that purpose is locked out, or while a code for another
  // purpose is still active (busyFor): nobody can replace a code the owner is reading off the
  // screen. For the same purpose an active code is kept as it is (same code, same timer). The owner
  // can always ask again once it expires (kTtlMs) or after it was used (close()).
  bool open(Purpose p, const char* code4, uint32_t nowMs);
  bool busyFor(Purpose p, uint32_t nowMs) const { return active(nowMs) && p != purpose_; }
  bool locked(Purpose p, uint32_t nowMs) const { return lock_[idx(p)].locked(nowMs); }
  uint32_t lockRemainingMs(Purpose p, uint32_t nowMs) const { return lock_[idx(p)].remainingMs(nowMs); }
  // Clears expired lockouts (see EscalatingLockout::update).
  void update(uint32_t nowMs) {
    for (auto& l : lock_) l.update(nowMs);
  }
  bool active(uint32_t nowMs) const;
  Purpose purpose() const { return purpose_; }
  const char* code() const { return code_; }
  uint32_t remainingMs(uint32_t nowMs) const;
  bool check(Purpose p, const char* code, uint32_t nowMs);
  void close() { open_ = false; }
  EscalatingLockout& lockout(Purpose p) { return lock_[idx(p)]; }

 private:
  static uint8_t idx(Purpose p) { return (uint8_t)p < kPurposes ? (uint8_t)p : 0; }
  bool open_ = false;
  Purpose purpose_ = Purpose::Update;
  char code_[5] = "";
  uint32_t openedAtMs_ = 0;
  EscalatingLockout lock_[kPurposes];
};

// Does OTA (/update) need the on-screen presence code? It does NOT only for a unit that was NEVER
// configured (no Wi-Fi joined from the portal and no pairing, ever — a marker that survives
// factory reset), that also has no saved Wi-Fi credentials and no pairings right now, reached over
// its own setup AP — so fresh shelf units can be updated in bulk. A unit that was configured once
// keeps requiring the code even after a factory reset (a reset unit in the field must not become
// updatable without physical presence). Any saved network, any pairing, or a request from another
// interface (the home LAN) also keeps the code (and its escalating lockout) mandatory.
bool otaCodeRequired(bool everConfigured, bool hasWifiCreds, uint8_t tokenCount, bool viaSoftAp);

// Does joining a network from the setup portal (POST /wifi) need the on-screen code? Yes on a unit
// with a saved network or a pairing: the portal of a unit that lost its Wi-Fi is an open AP, and
// whoever is nearby must not be able to move it to their own network. No on a fresh unit, and a
// factory-reset unit (resale, a return) has no network and no pairing, so it is fresh again and
// sets up without friction. The /.configured marker is deliberately not an input here (it
// survives a factory reset and still gates OTA: otaCodeRequired).
bool wifiCodeRequired(bool hasWifiCreds, uint8_t tokenCount);

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
