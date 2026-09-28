#pragma once
#include <stddef.h>
#include <stdint.h>

namespace miblo {

constexpr size_t kSnapshotMaxBytes = 3072;
constexpr uint8_t kMaxSessions = 8;
constexpr uint8_t kMaxAlerts = 8;

enum class SessionState : uint8_t { Idle, Running, Perm, Question, Done };
enum class AlertKind : uint8_t { Perm, Question, Done };

// Tamanhos: limite do protocolo em caracteres × 4 bytes (pior caso UTF-8) + NUL.
struct SessionRow {
  char id[9];
  char name[84];     // ≤ 20 caracteres
  SessionState st;
  char tool[132];    // ≤ 32 caracteres
  char det[132];     // ≤ 32 caracteres
  uint32_t since;    // epoch em segundos
  char model[52];    // ≤ 12 caracteres
  int16_t ctx;       // -1 = null
  int64_t tok;       // -1 = null
};

struct UsageWindow {
  bool present;
  uint8_t pct;
  uint32_t reset;  // epoch em segundos
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
  float todayUsd;  // custo do dia (today.usd); o protocolo não tem mais today.tok
  uint8_t count;
  SessionRow sessions[kMaxSessions];
  uint16_t more;
  uint8_t alertCount;
  AlertItem alerts[kMaxAlerts];
};

enum class ParseResult : uint8_t { Ok, TooLarge, BadJson, BadVersion };

// Faz o parse de `json` (até `len` bytes; o buffer é usado em modo zero-copy e pode ser
// modificado). Em qualquer erro `out` NÃO é alterado. Campos desconhecidos são ignorados.
ParseResult parseSnapshot(char* json, size_t len, Snapshot& out);

bool parseSessionState(const char* s, SessionState& out);
bool parseAlertKind(const char* s, AlertKind& out);

// Índice da sessão com esse id curto, ou -1.
int findSession(const Snapshot& s, const char* id);

}  // namespace miblo
