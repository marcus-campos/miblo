#include "miblo_snapshot.h"

#include <ArduinoJson.h>
#include <math.h>
#include <string.h>

#include "miblo_utf8.h"

namespace miblo {

// 4 KB bastam no ESP8266 (slots de 16 bytes, strings zero-copy); no host 64-bit os slots dobram.
static constexpr size_t kDocCapacity = sizeof(void*) == 4 ? 4096 : 8192;

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
}

ParseResult parseSnapshot(char* json, size_t len, Snapshot& out) {
  if (len > kSnapshotMaxBytes) return ParseResult::TooLarge;

  DynamicJsonDocument filter(1024);
  filter["v"] = true;
  filter["seq"] = true;
  filter["now"] = true;
  filter["host"] = true;
  filter["usage"] = true;
  filter["today"] = true;
  filter["more"] = true;
  JsonObject fs = filter["sessions"].createNestedObject();
  for (const char* k : {"id", "name", "st", "tool", "det", "since", "model", "ctx", "tok"}) fs[k] = true;
  JsonObject fa = filter["alerts"].createNestedObject();
  for (const char* k : {"id", "kind", "sid"}) fa[k] = true;

  DynamicJsonDocument doc(kDocCapacity);
  DeserializationError err =
      deserializeJson(doc, json, len, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(6));
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

  out.todayUsd = doc["today"]["usd"].as<float>();

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
    copyStr(r.model, sizeof(r.model), s["model"], 12);
    r.ctx = s["ctx"].is<int>() ? (int16_t)s["ctx"].as<int>() : -1;
    r.tok = s["tok"].is<int64_t>() ? s["tok"].as<int64_t>() : -1;
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
