#include "miblo_snapshot.h"

#include <ArduinoJson.h>
#include <math.h>
#include <string.h>

#include "miblo_utf8.h"

namespace miblo {

// 4 KB is enough on the ESP8266 (16-byte slots, zero-copy strings); on a 64-bit host the slots double.
// The parsed document (zero-copy: strings stay in the body) needs about as many bytes of slots as
// the JSON has text (~160 B of slots for a ~250 B session); sized per request, never above this.
static constexpr size_t kDocMax = sizeof(void*) == 4 ? 7168 : 14336;
static void (*g_probe)() = nullptr;
void setParseProbe(void (*probe)()) { g_probe = probe; }

bool parseSessionState(const char* s, SessionState& out) {
  if (!s) return false;
  if (strcmp(s, "idle") == 0) out = SessionState::Idle;
  else if (strcmp(s, "running") == 0) out = SessionState::Running;
  else if (strcmp(s, "perm") == 0) out = SessionState::Perm;
  else if (strcmp(s, "question") == 0) out = SessionState::Question;
  else if (strcmp(s, "done") == 0) out = SessionState::Done;
  else return false;
  return true;
}

bool parseAlertKind(const char* s, AlertKind& out) {
  if (!s) return false;
  if (strcmp(s, "perm") == 0) out = AlertKind::Perm;
  else if (strcmp(s, "question") == 0) out = AlertKind::Question;
  else if (strcmp(s, "done") == 0) out = AlertKind::Done;
  else return false;
  return true;
}

int findSession(const Snapshot& s, const char* id) {
  for (int i = 0; i < s.count; i++) {
    if (strcmp(s.sessions[i].id, id) == 0) return i;
  }
  return -1;
}

static void copyStr(char* dst, size_t cap, JsonVariantConst v, size_t maxChars) {
  const char* s = v.is<const char*>() ? v.as<const char*>() : "";
  utf8Copy(dst, cap, s, maxChars);
}

// A cost: finite and >= 0, else 0 (a negative, NaN or overflowing number from a bad bridge).
static float money(JsonVariantConst v) {
  const float f = v.as<float>();
  return f > 0 && f < 1e9f ? f : 0;
}

static uint8_t clampPct(JsonVariantConst v) {
  float f = v.as<float>();
  if (!(f > 0)) return 0;
  if (f >= 100) return 100;
  return (uint8_t)lroundf(f);
}

static void readWindow(JsonVariantConst v, UsageWindow& w) {
  w.present = v.is<JsonObjectConst>() && v["pct"].is<float>();
  w.pct = w.present ? clampPct(v["pct"]) : 0;
  w.reset = w.present ? v["reset"].as<uint32_t>() : 0;
  w.etaSent = w.present && v.containsKey("eta");    // a new bridge always sends it with h5
  w.eta = w.etaSent ? v["eta"].as<uint32_t>() : 0;  // 0 = no forecast
}

uint32_t longCommandSec(const SessionRow& r, uint32_t nowEpoch) {
  if (r.st != SessionState::Running || strcmp(r.tool, "Bash") != 0 || !r.ts) return 0;
  // Compared without overflow: nowEpoch >= ts + kLongCommandSec.
  if (nowEpoch < r.ts || nowEpoch - r.ts < kLongCommandSec) return 0;
  return nowEpoch - r.ts;
}

ParseResult parseSnapshot(char* json, size_t len, Snapshot& out) {
  if (len > kSnapshotMaxBytes) return ParseResult::TooLarge;

  // Sized on any platform: 11 top-level keys (+1 slot of headroom), a 10-key session template, a
  // 3-key alert one. A key added without growing it would be dropped silently, so an overflowed
  // filter refuses every parse (test_every_filtered_field_arrives catches it).
  DynamicJsonDocument filter(JSON_OBJECT_SIZE(12) + 2 * JSON_ARRAY_SIZE(1) + JSON_OBJECT_SIZE(10) +
                             JSON_OBJECT_SIZE(3));
  filter["v"] = true;
  filter["seq"] = true;
  filter["now"] = true;
  filter["host"] = true;
  filter["usage"] = true;
  filter["today"] = true;
  filter["latest"] = true;
  filter["more"] = true;
  filter["week"] = true;
  JsonObject fs = filter["sessions"].createNestedObject();
  for (const char* k : {"id", "name", "st", "tool", "det", "since", "ts", "model", "ctx", "tok"}) fs[k] = true;
  JsonObject fa = filter["alerts"].createNestedObject();
  for (const char* k : {"id", "kind", "sid"}) fa[k] = true;
  if (filter.overflowed()) return ParseResult::BadJson;

  const size_t cap = (len + 1024) * (sizeof(void*) == 4 ? 1 : 2);
  DynamicJsonDocument doc(cap < kDocMax ? cap : kDocMax);
  DeserializationError err =
      deserializeJson(doc, json, len, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(6));
  if (g_probe) g_probe();
  if (err) return ParseResult::BadJson;
  if (!doc.is<JsonObject>()) return ParseResult::BadJson;
  if (!doc["v"].is<int>() || doc["v"].as<int>() < 1) return ParseResult::BadVersion;

  out.seq = doc["seq"].as<uint32_t>();
  out.now = doc["now"].as<uint32_t>();
  copyStr(out.host, sizeof(out.host), doc["host"], 20);

  JsonVariantConst usage = doc["usage"];
  out.hasUsage = usage.is<JsonObjectConst>();
  readWindow(usage["h5"], out.h5);
  readWindow(usage["d7"], out.d7);
  out.hasUsage = out.hasUsage && (out.h5.present || out.d7.present);

  out.todayUsd = money(doc["today"]["usd"]);
  out.todayTurns = doc["today"]["turns"].as<uint16_t>();
  out.todayWorkSec = doc["today"]["work"].as<uint32_t>();
  copyStr(out.latest, sizeof(out.latest), doc["latest"], 15);

  JsonVariantConst week = doc["week"];
  out.week.present = week.is<JsonObjectConst>();
  out.week.workSec = week["work"].as<uint32_t>();
  out.week.turns = week["turns"].as<uint16_t>();
  out.week.usd = money(week["usd"]);
  const int top = week["top"].is<int>() ? week["top"].as<int>() : -1;
  out.week.busiest = top >= 0 && top <= 6 ? (uint8_t)top : 255;

  out.count = 0;
  uint16_t skipped = 0;
  for (JsonObjectConst s : doc["sessions"].as<JsonArrayConst>()) {
    SessionState st;
    if (!parseSessionState(s["st"].as<const char*>(), st)) st = SessionState::Idle;
    if (out.count >= kMaxSessions) {
      skipped++;
      continue;
    }
    SessionRow& r = out.sessions[out.count++];
    copyStr(r.id, sizeof(r.id), s["id"], 8);
    copyStr(r.name, sizeof(r.name), s["name"], 20);
    r.st = st;
    copyStr(r.tool, sizeof(r.tool), s["tool"], 32);
    copyStr(r.det, sizeof(r.det), s["det"], 32);
    r.since = s["since"].as<uint32_t>();
    r.ts = s["ts"].as<uint32_t>();
    copyStr(r.model, sizeof(r.model), s["model"], 12);
    r.ctx = s["ctx"].is<int>() ? (int16_t)s["ctx"].as<int>() : -1;
    const int64_t tok = s["tok"].is<int64_t>() ? s["tok"].as<int64_t>() : -1;
    r.tok = tok < 0 ? -1 : tok > INT32_MAX ? INT32_MAX : (int32_t)tok;
  }
  out.more = (uint16_t)(doc["more"].as<uint16_t>() + skipped);

  out.alertCount = 0;
  for (JsonObjectConst a : doc["alerts"].as<JsonArrayConst>()) {
    AlertKind kind;
    if (!parseAlertKind(a["kind"].as<const char*>(), kind)) continue;
    if (out.alertCount >= kMaxAlerts) break;
    AlertItem& it = out.alerts[out.alertCount++];
    it.id = a["id"].as<uint32_t>();
    it.kind = kind;
    copyStr(it.sid, sizeof(it.sid), a["sid"], 8);
  }
  return ParseResult::Ok;
}

}  // namespace miblo
