#pragma once
#include <stddef.h>
#include <stdint.h>

namespace miblo {

constexpr size_t kSnapshotMaxBytes = 3072;
constexpr uint8_t kMaxSessions = 8;
constexpr uint8_t kMaxAlerts = 8;

enum class SessionState : uint8_t { Idle, Running, Perm, Question, Done };
enum class AlertKind : uint8_t { Perm, Question, Done };

// Sizes: protocol limit in characters × 4 bytes (worst-case UTF-8) + NUL.
struct SessionRow {
  char id[9];
  char name[84];     // <= 20 characters
  SessionState st;
  char tool[132];    // <= 32 characters
  char det[132];     // <= 32 characters
  uint32_t since;    // epoch in seconds
  char model[52];    // <= 12 characters
  int16_t ctx;       // -1 = null
  int64_t tok;       // -1 = null
};

struct UsageWindow {
  bool present;
  uint8_t pct;
  uint32_t reset;  // epoch in seconds
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

bool parseSessionState(const char* s, SessionState& out);
bool parseAlertKind(const char* s, AlertKind& out);

// Index of the session with that short id, or -1.
int findSession(const Snapshot& s, const char* id);

}  // namespace miblo
