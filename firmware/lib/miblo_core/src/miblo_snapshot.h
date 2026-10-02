#pragma once
#include <stddef.h>
#include <stdint.h>

namespace miblo {

// Sized on the device (stress-tested with /api/info "diag"): the biggest snapshot, parsed, still
// leaves more than 10% of the RAM free. The plugin reads both from /api/info (maxSessions,
// maxBytes) and trims what it sends; older firmware without them gets 8 sessions / 3072 bytes.
constexpr size_t kSnapshotMaxBytes = 6144;
constexpr uint8_t kMaxSessions = 20;
constexpr uint8_t kMaxAlerts = 8;

enum class SessionState : uint8_t { Idle, Running, Perm, Question, Done };
enum class AlertKind : uint8_t { Perm, Question, Done };

// Text fields hold the protocol's character limit at up to 3 bytes per character (accents, CJK,
// Cyrillic); longer UTF-8 (emoji) is cut at a character boundary, and the screen shows at most
// ~30 characters per line anyway. 240 bytes a row (was 432 at 4 bytes per character).
struct SessionRow {
  char id[9];
  char name[61];     // <= 20 characters
  SessionState st;
  char tool[33];     // <= 32 characters (tool names are ASCII)
  char det[97];      // <= 32 characters
  char model[25];    // <= 12 characters
  int16_t ctx;       // -1 = null
  uint32_t since;    // epoch in seconds
  uint32_t ts;       // when the running Bash command started, epoch s (0 = not sent / not a long command)
  int32_t tok;       // -1 = null (context size: well under 2^31)
};

struct UsageWindow {
  bool present;
  uint8_t pct;
  uint32_t reset;  // epoch in seconds
  uint32_t eta;    // h5 only: when the bridge expects 100% at the current pace (0 = no forecast)
  bool etaSent;    // the bridge sent `eta` (even 0): it decides; an older plugin doesn't (see etaFor)
};

// Last week's totals, sent by the bridge on Mondays only (miblo_dayend.h WeeklyRecap).
struct WeekStats {
  bool present;
  uint8_t busiest;  // weekday with the most work, 0 = Sunday .. 6; 255 = unknown
  uint16_t turns;
  uint32_t workSec;
  float usd;
};

struct AlertItem {
  uint32_t id;
  AlertKind kind;
  char sid[9];
};

struct Snapshot {
  uint32_t seq;
  uint32_t now;
  char host[84];
  bool hasUsage;
  UsageWindow h5;
  UsageWindow d7;
  float todayUsd;  // today's cost (today.usd); the protocol no longer has today.tok
  uint16_t todayTurns;    // responses finished today (today.turns; 0 from older plugins)
  uint32_t todayWorkSec;  // time with a session working today (today.work, seconds)
  WeekStats week;         // last week (Mondays only; week.present false otherwise / older plugin)
  char latest[16];        // newest released firmware, "1.0.2" ("" = unknown / older plugin)
  uint8_t count;
  SessionRow sessions[kMaxSessions];
  uint16_t more;
  uint8_t alertCount;
  AlertItem alerts[kMaxAlerts];
};

enum class ParseResult : uint8_t { Ok, TooLarge, BadJson, BadVersion };

// Parses `json` (up to `len` bytes; the buffer is used zero-copy and may be
// modified). On any error `out` is NOT changed. Unknown fields are ignored.
ParseResult parseSnapshot(char* json, size_t len, Snapshot& out);
// Optional: called while the parsed document is alive (the moment the parse uses the most memory),
// for the device's diagnostics.
void setParseProbe(void (*probe)());

bool parseSessionState(const char* s, SessionState& out);
bool parseAlertKind(const char* s, AlertKind& out);

// Index of the session with that short id, or -1.
int findSession(const Snapshot& s, const char* id);

// A running Bash command's elapsed seconds once it passed kLongCommandSec, else 0 (spec 10).
constexpr uint32_t kLongCommandSec = 30;
uint32_t longCommandSec(const SessionRow& r, uint32_t nowEpoch);

}  // namespace miblo
