// Fuzz targets over miblo_core: everything that parses bytes from the network or from flash.
#include <ArduinoJson.h>
#include <math.h>
#include <stdio.h>

#include <string>

#include "fuzz.h"
#include "miblo_config.h"
#include "miblo_cues.h"
#include "miblo_desknotes.h"
#include "miblo_focus.h"
#include "miblo_format.h"
#include "miblo_friends.h"
#include "miblo_info.h"
#include "miblo_limits.h"
#include "miblo_mdns.h"
#include "miblo_meeting.h"
#include "miblo_occasions.h"
#include "miblo_overview.h"
#include "miblo_security.h"
#include "miblo_snapshot.h"
#include "miblo_tz.h"
#include "miblo_utf8.h"
#include "miblo_zone.h"

using namespace miblo;
using fuzz::CStr;
using fuzz::ExactBuf;
using fuzz::Reader;

namespace {

// A NUL-terminated string field of a fixed array: the terminator is inside it.
template <size_t N>
void checkStr(const char (&s)[N], size_t maxChars, const char* what) {
  const size_t len = strnlen(s, N);
  FUZZ_CHECK(len < N, "%s not terminated", what);
  FUZZ_CHECK(utf8Length(s) <= maxChars, "%s: %zu characters > %zu", what, utf8Length(s), maxChars);
}

// ============================== snapshot ==============================
// POST /api/state (api.cpp): the body goes to parseSnapshot as it is, then to every screen.

const char* const kSnapshotSeeds[] = {
    R"({"v":1,"seq":1,"now":1790000000,"host":"mac","sessions":[],"alerts":[]})",
    R"({"v":1,"seq":7,"now":1790000000,"host":"marcus-mbp","usage":{"h5":{"pct":42,"reset":1790003600,"eta":1790002000},"d7":{"pct":12.5,"reset":1790400000}},"today":{"usd":3.25,"turns":12,"work":5400},"latest":"1.10.1","week":{"work":90000,"turns":300,"usd":40.5,"top":2},"sessions":[{"id":"a1b2c3d4","name":"claude_gadget","st":"running","tool":"Bash","det":"npm test","since":1789999000,"ts":1789999900,"model":"opus","ctx":45,"tok":91000},{"id":"e5f6","name":"other","st":"perm","tool":"Edit","det":"x.js","since":1789999500,"model":"sonnet","ctx":null,"tok":null}],"more":3,"alerts":[{"id":9,"kind":"perm","sid":"e5f6"},{"id":10,"kind":"done","sid":"a1b2c3d4"}]})",
    R"({"v":1,"seq":2,"now":0,"sessions":[{"id":"x","st":"question"},{"id":"y","st":"done"},{"id":"z","st":"idle"}],"alerts":[{"id":1,"kind":"question","sid":"x"}]})",
    R"({"v":2,"seq":4294967295,"now":4294967295,"host":"é项😀","usage":{"h5":{"pct":100},"d7":{"pct":-5}},"today":{"usd":1e30},"more":65535})",
    nullptr};
const char* const kSnapshotDict[] = {
    "\"v\":1",  "\"seq\":",    "\"now\":",  "\"host\":",  "\"usage\":", "\"h5\":",   "\"d7\":",    "\"pct\":",
    "\"reset\":", "\"eta\":",  "\"today\":", "\"usd\":",  "\"turns\":", "\"work\":", "\"latest\":", "\"week\":",
    "\"top\":", "\"sessions\":", "\"alerts\":", "\"more\":", "\"id\":",  "\"name\":", "\"st\":",    "\"tool\":",
    "\"det\":", "\"since\":",  "\"ts\":",   "\"model\":", "\"ctx\":",  "\"tok\":",  "\"kind\":",  "\"sid\":",
    "\"running\"", "\"perm\"", "\"question\"", "\"done\"", "\"idle\"", "\"Bash\"", nullptr};

void consumeSnapshot(Snapshot& s, Reader& r) {
  FUZZ_CHECK(s.count <= kMaxSessions, "count %u", s.count);
  FUZZ_CHECK(s.alertCount <= kMaxAlerts, "alertCount %u", s.alertCount);
  checkStr(s.host, 20, "host");
  checkStr(s.latest, 15, "latest");
  FUZZ_CHECK(s.h5.pct <= 100 && s.d7.pct <= 100, "pct > 100");
  FUZZ_CHECK(isfinite(s.todayUsd) && s.todayUsd >= 0 && isfinite(s.week.usd) && s.week.usd >= 0, "usd not sane");
  FUZZ_CHECK(s.week.busiest <= 6 || s.week.busiest == 255, "busiest %u", s.week.busiest);
  char buf[32];
  formatUsd(s.todayUsd, buf, sizeof(buf));
  formatUsd(s.week.usd, buf, sizeof(buf));
  formatElapsed(s.todayWorkSec, buf, sizeof(buf));
  formatElapsed(s.week.workSec, buf, sizeof(buf));
  compareVersions(s.latest, "1.10.1");
  for (uint8_t i = 0; i < s.count; i++) {
    const SessionRow& row = s.sessions[i];
    checkStr(row.id, 8, "id");
    checkStr(row.name, 20, "name");
    checkStr(row.tool, 32, "tool");
    checkStr(row.det, 32, "det");
    checkStr(row.model, 12, "model");
    FUZZ_CHECK((uint8_t)row.st <= (uint8_t)SessionState::Done, "state %u", (unsigned)row.st);
    FUZZ_CHECK(row.tok >= -1, "tok %d", (int)row.tok);
    FUZZ_CHECK(findSession(s, row.id) >= 0 && findSession(s, row.id) <= i, "findSession");
    if (row.tok >= 0) formatTokens((uint64_t)row.tok, buf, sizeof(buf));
    formatAgo(s.now - row.since, buf, sizeof(buf));
    formatInState(s.now - row.since, buf, sizeof(buf));
    longCommandSec(row, s.now);
  }
  for (uint8_t i = 0; i < s.alertCount; i++) {
    checkStr(s.alerts[i].sid, 8, "sid");
    findSession(s, s.alerts[i].sid);
  }
  countStates(s);
  classifyOverview(s);
  const int hero = selectHero(s, true);
  FUZZ_CHECK(hero < (int)s.count, "selectHero %d of %u", hero, s.count);
  const int last = lastFinished(s);
  FUZZ_CHECK(last < (int)s.count, "lastFinished %d", last);
  frameColorFor(s, s.now);
  // State over time: the same snapshot seen a few times as the clock and usage move.
  RunTracker runs;
  LimitWatch watch;
  uint32_t nowMs = r.u32();
  for (int k = 0; k < 4; k++) {
    runs.observe(s);
    watch.observe(s, nowMs);
    watch.celebrating(nowMs);
    etaFor(s, watch);
    uint32_t dur;
    for (uint8_t i = 0; i < s.count; i++) runs.stats(s.sessions[i].id, dur);
    nowMs += r.u32() % 4000000;
    s.now += r.u16();
    s.h5.pct = r.u8() % 101;
    if (s.count) s.sessions[r.u8() % s.count].st = (SessionState)(r.u8() % 5);
  }
}

void fuzzSnapshot(const uint8_t* d, size_t n) {
  ExactBuf buf(d, n);  // parsed in place, like the String body on the device; no terminator
  static Snapshot s;
  memset(&s, 0xA5, sizeof(s));
  const ParseResult res = parseSnapshot(buf.p, buf.n, s);
  if (n > kSnapshotMaxBytes) FUZZ_CHECK(res == ParseResult::TooLarge, "oversized body accepted");
  if (res != ParseResult::Ok) return;
  fuzz::reached();
  Reader r(d, n);
  consumeSnapshot(s, r);
}
FUZZ_REGISTER(snapshot, fuzzSnapshot, kSnapshotSeeds, kSnapshotDict, 7000);

// ============================== config ==============================
// PATCH /api/config and POST /config (web.cpp), and /config.json read back at boot (storage.cpp).

const char* const kConfigSeeds[] = {
    R"({"mode":"limits","brightness":80,"alerts":true,"heroPermSec":10,"tz":"America/Sao_Paulo","lang":"pt-BR"})",
    R"({"name":"Desk","night":true,"nightFrom":1320,"nightTo":420,"nightBrightness":5,"blueFilter":true,"blueFrom":1200,"blueTo":360,"blueLevel":2})",
    R"({"owner":"Marcus","birthday":"03-14","born":"2026-01-02","friends":false,"insist":false,"mascot":3,"sleepMin":0,"petMin":30})",
    R"({"rotate":true,"rotateEverySec":60,"rotateShowSec":10,"workFrom":540,"workTo":1080,"workDays":62,"fanfareMin":10})",
    R"({"tz":"<-03>3","tz2":"Asia/Tokyo","tz2Label":"Tokyo 東京","deskQr":true,"waterMin":45,"breakAfterMin":50,"eyes":true,"endOfDay":true,"weekly":true,"focusQuiet":true,"frame":true,"friendsSide":1,"langAuto":true})",
    R"({"tz":"EST5EDT,M3.2.0,M11.1.0","reminderMin":0,"flashBlinks":5,"discreet":true,"heroDoneSec":2})",
    nullptr};
const char* const kConfigDict[] = {
    "\"mode\":", "\"brightness\":", "\"alerts\":", "\"heroPermSec\":", "\"heroDoneSec\":", "\"flashBlinks\":",
    "\"reminderMin\":", "\"discreet\":", "\"tz\":", "\"name\":", "\"lang\":", "\"langAuto\":", "\"night\":",
    "\"nightFrom\":", "\"nightTo\":", "\"nightBrightness\":", "\"blueFilter\":", "\"blueFrom\":", "\"blueTo\":",
    "\"blueLevel\":", "\"rotate\":", "\"rotateEverySec\":", "\"rotateShowSec\":", "\"mascot\":", "\"sleepMin\":",
    "\"petMin\":", "\"owner\":", "\"birthday\":", "\"born\":", "\"friends\":", "\"friendsSide\":", "\"insist\":",
    "\"workFrom\":", "\"workTo\":", "\"workDays\":", "\"fanfareMin\":", "\"tz2\":", "\"tz2Label\":", "\"deskQr\":",
    "\"waterMin\":", "\"breakAfterMin\":", "\"eyes\":", "\"endOfDay\":", "\"weekly\":", "\"focusQuiet\":",
    "\"frame\":", "\"overview\"", "\"sessions\"", "\"limits\"", "\"UTC0\"", "\"Europe/Lisbon\"", "\"zh\"",
    "\"pt-PT\"", "\"02-29\"", "\"2024-02-29\"", "\"13-01\"", nullptr};

std::string storedJson(const Config& c, bool& overflowed) {
  DynamicJsonDocument st(kConfigJsonCapacity);
  configToStored(c, st.to<JsonObject>());
  overflowed = st.overflowed();
  std::string out;
  serializeJson(st, out);
  return out;
}

// storage.cpp loadConfig(), on a file's bytes.
bool loadConfigLike(const char* bytes, size_t n, Config& cfg) {
  DynamicJsonDocument doc(kConfigJsonCapacity);
  if (deserializeJson(doc, bytes, n)) return false;
  Config loaded;
  for (int attempt = 0;; attempt++) {
    const char* bad = nullptr;
    if (applyConfigPatch(loaded, doc.as<JsonObjectConst>(), &bad)) break;
    if (attempt == 2 || !bad) return false;
    if (strcmp(bad, "tz") == 0) doc.remove("tz");
    else if (strcmp(bad, "tz2") == 0) doc.remove("tz2");
    else return false;
    loaded = Config();
  }
  restoreStoredLang(loaded, doc.as<JsonObjectConst>());
  cfg = loaded;
  return true;
}

void checkConfig(const Config& c) {
  bool over = false;
  const std::string stored = storedJson(c, over);
  FUZZ_CHECK(!over, "stored config overflows kConfigJsonCapacity: %s", stored.c_str());
  // What was saved loads back as the same settings.
  Config again;
  FUZZ_CHECK(loadConfigLike(stored.data(), stored.size(), again), "saved config does not load: %s", stored.c_str());
  const std::string stored2 = storedJson(again, over);
  FUZZ_CHECK(stored == stored2, "config round trip differs:\n%s\n%s", stored.c_str(), stored2.c_str());
  for (bool priv : {true, false}) {
    DynamicJsonDocument page(kConfigJsonCapacity);
    configToJson(c, page.to<JsonObject>(), priv);
    FUZZ_CHECK(!page.overflowed(), "configToJson overflows kConfigJsonCapacity");
  }
  alertTiming(c);
  rotationTiming(c);
  for (int m = -1; m <= 1440; m += 7) {
    nightActive(c, m);
    FUZZ_CHECK(brightnessAt(c, m) <= 100, "brightness > 100");
    warmthAt(c, m);
    cueBrightness(c, m);
  }
  char label[64];
  zoneLabel(c, label, sizeof(label));
  char hhmm[8];
  zoneHHMM(c.tz2, 1790000000, hhmm, sizeof(hhmm));
  Date d{2026, 10, 2};
  occasionOn(c, d);
  mibloAge(c, d);
}

void fuzzConfig(const uint8_t* d, size_t n) {
  const char* text = reinterpret_cast<const char*>(d);
  // The settings page / API: a patch applied to the running settings.
  {
    DynamicJsonDocument doc(kConfigJsonCapacity);
    if (!deserializeJson(doc, text, n) && doc.is<JsonObject>()) {
      Config c;
      bool over;
      const std::string before = storedJson(c, over);
      const char* bad = nullptr;
      if (applyConfigPatch(c, doc.as<JsonObjectConst>(), &bad)) {
        fuzz::reached();
        checkConfig(c);
      } else {
        // The field goes raw into the error reply: always a known key, never request text.
        FUZZ_CHECK(fuzz::plainIdent(bad), "badField not a plain identifier");
        FUZZ_CHECK(storedJson(c, over) == before, "a refused patch changed the settings");
      }
    }
  }
  // The same bytes as a (possibly damaged) /config.json.
  Config c;
  if (loadConfigLike(text, n, c)) checkConfig(c);
}
FUZZ_REGISTER(config, fuzzConfig, kConfigSeeds, kConfigDict, 3000);

// ============================== daily-life API ==============================
// api.cpp dailyRoute(): body <= 300 bytes into a StaticJsonDocument<384>, then the module's handler
// and a 768-byte reply document (counted at the device's slot size). The input is a script: lines
// "<route><json>", a route letter per handler; the same desk notes / timers live across the lines,
// the clock moves.

const char* const kDailySeeds[] = {
    "f{\"focusMin\":25,\"breakMin\":5,\"rounds\":4}\nu\nf{\"stop\":true}",
    "m{\"min\":30}\nm{\"off\":true}\nm{\"min\":480}",
    "s{\"text\":\"Back in 5\",\"min\":10}\ns{\"off\":true}\ns{\"text\":\"\\u9879\\u76ee \\ud83d\\ude00\",\"min\":1}",
    "r{\"in\":15,\"text\":\"tea\"}\nr{\"at\":\"09:45\",\"days\":62,\"text\":\"standup\"}\nl\nu\nr{\"dismiss\":true}\nr{\"delete\":5}",
    "r{\"at\":\"23:59\",\"text\":\"late\"}\nr{\"in\":1,\"text\":\"a\"}\nr{\"in\":2,\"text\":\"b\"}\nr{\"in\":3,\"text\":\"c\"}\nr{\"in\":4,\"text\":\"d\"}\nr{\"in\":5,\"text\":\"e\"}\nl",
    "t{\"min\":25}\nu\nt{\"stop\":true}\nt{\"min\":180}",
    "c{\"label\":\"Vacation\",\"date\":\"2026-12-20\"}\nc{\"label\":\"Bday\",\"md\":\"02-29\"}\nc{\"off\":true}",
    "n\nl\nu\nj",
    "f\nm\ns\nr\nt\nc",
    nullptr};
const char* const kDailyDict[] = {
    "\"focusMin\":", "\"breakMin\":", "\"rounds\":", "\"stop\":true", "\"min\":", "\"off\":true", "\"text\":",
    "\"in\":", "\"at\":", "\"days\":", "\"dismiss\":true", "\"delete\":", "\"label\":", "\"date\":", "\"md\":",
    "\"09:45\"", "\"24:00\"", "\"9:5\"", "\"2026-02-29\"", "\"2024-02-29\"", "\"02-30\"", "\"12-31\"",
    "\nf{", "\nm{", "\ns{", "\nr{", "\nt{", "\nc{", "\nl", "\nu", "\nn", "\nj", nullptr};

// A handler's answer: a known status, and on an error a field name safe to put raw into JSON.
void checkStatus(int code, const char* bad, const char* route) {
  FUZZ_CHECK(code == 200 || code == 400 || code == 409, "%s: status %d", route, code);
  if (code != 200) FUZZ_CHECK(fuzz::plainIdent(bad), "%s: bad field not a plain identifier", route);
}

// ArduinoJson's pool holds 16-byte slots on the ESP8266 (32-bit pointers) but bigger ones on this
// 64-bit host, so a host document of the device's capacity overflows on content the device holds
// fine. Documents are made bigger here and their use recounted as the device would.
constexpr size_t kDeviceSlot = 16;
constexpr size_t kHostSlot = JSON_OBJECT_SIZE(1);  // sizeof(VariantSlot) on this host
constexpr size_t hostCapacity(size_t deviceCapacity) { return deviceCapacity / kDeviceSlot * kHostSlot + kHostSlot; }

size_t slotsBelow(JsonVariantConst v) {  // one per member or element, nested ones included
  size_t n = 0;
  if (v.is<JsonObjectConst>()) {
    for (JsonPairConst p : v.as<JsonObjectConst>()) n += 1 + slotsBelow(p.value());
  } else if (v.is<JsonArrayConst>()) {
    for (JsonVariantConst e : v.as<JsonArrayConst>()) n += 1 + slotsBelow(e);
  }
  return n;
}

// What `doc` would take on the device: its slots at 16 B, plus the strings it copied.
size_t deviceUsage(const JsonDocument& doc) {
  const size_t slots = slotsBelow(doc.as<JsonVariantConst>());
  return slots * kDeviceSlot + (doc.memoryUsage() - slots * kHostSlot);
}

// True when `doc` would not fit a device document of `deviceCapacity` bytes.
bool overflowsOnDevice(const JsonDocument& doc, size_t deviceCapacity) {
  return doc.overflowed() || deviceUsage(doc) > deviceCapacity;
}

std::string notesJson(const DeskNotes& n, bool& overflowed) {
  DynamicJsonDocument doc(hostCapacity(768));  // storage.cpp kNotesJsonCapacity
  n.toJson(doc.to<JsonObject>());
  overflowed = overflowsOnDevice(doc, 768);
  std::string s;
  serializeJson(doc, s);
  return s;
}

void checkNotes(DeskNotes& notes, uint32_t nowMs, uint32_t epoch) {
  bool over;
  const std::string saved = notesJson(notes, over);
  FUZZ_CHECK(!over, "notes.json overflows its 768-byte document: %s", saved.c_str());
  FUZZ_CHECK(saved.size() <= 1024, "notes.json over the 1024 bytes loadNotes accepts");
  DynamicJsonDocument reply(hostCapacity(768));  // api.cpp dailyRoute kReplyDoc
  JsonObject out = reply.to<JsonObject>();
  notes.listJson(out.createNestedArray("items"), nowMs, epoch);
  out["ok"] = true;  // dailyRoute adds it
  FUZZ_CHECK(!overflowsOnDevice(reply, 768), "GET /api/remind overflows the 768-byte reply (%zu B on the device)",
             deviceUsage(reply));
  const char* held = notes.heldText(nowMs);
  FUZZ_CHECK(held && fuzz::validUtf8(held) && utf8Length(held) <= kNoteChars, "held text");
  const char* say = notes.saying(nowMs);
  if (say) FUZZ_CHECK(fuzz::validUtf8(say) && utf8Length(say) <= kNoteChars, "say text");
  FUZZ_CHECK(utf8Length(notes.countdown().label) <= kNoteChars, "countdown label");
}

void fuzzDaily(const uint8_t* d, size_t n) {
  FocusTimer focus;
  MeetingMode meeting;
  DeskNotes notes;
  uint32_t nowMs = 1000;
  uint32_t epoch = 1790000000;
  int minute = 600;
  const char* p = reinterpret_cast<const char*>(d);
  const char* end = p + n;
  int lines = 0, handled = 0;
  while (p < end && lines++ < 64) {
    const char* nl = static_cast<const char*>(memchr(p, '\n', (size_t)(end - p)));
    const char* lineEnd = nl ? nl : end;
    const char route = *p;
    const char* body = p + 1;
    const size_t len = lineEnd > body ? (size_t)(lineEnd - body) : 0;
    p = nl ? nl + 1 : end;
    // The clock moves by an amount the line decides (0 .. ~70 min), sometimes backwards (wrap).
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) h = (h ^ (uint8_t)body[i]) * 16777619u;
    const uint32_t step = (h % 4) == 0 ? 0 : (h >> 4) % 4200000;
    nowMs += (h % 97) == 0 ? 0xFFFFF000u : step;  // one line in ~97 crosses the 32-bit wrap
    epoch += step / 1000;
    minute = (minute + (int)(step / 60000)) % 1440;
    if ((h % 53) == 0) minute = -1;  // time unknown

    StaticJsonDocument<384> doc;
    if (len > 300 || (len && (deserializeJson(doc, body, len) || !doc.is<JsonObject>()))) continue;
    if (!len) doc.to<JsonObject>();
    if (len && !handled++) fuzz::reached();
    JsonObjectConst b = doc.as<JsonObjectConst>();
    DynamicJsonDocument reply(hostCapacity(768));  // api.cpp dailyRoute kReplyDoc
    JsonObject out = reply.to<JsonObject>();
    const char* bad = nullptr;
    const Date today{(uint16_t)(2026 + (h >> 20) % 3), (uint8_t)(1 + (h >> 8) % 12), (uint8_t)(1 + (h >> 12) % 31)};
    switch (route) {
      case 'f': checkStatus(focusRequest(focus, b, nowMs, &bad), bad, "focus"); break;
      case 'm': checkStatus(meetingRequest(meeting, b, nowMs, &bad), bad, "meeting"); break;
      case 's': checkStatus(sayRequest(notes, b, nowMs, &bad), bad, "say"); break;
      case 'r': {
        const int code = remindRequest(notes, b, nowMs, minute, out, &bad);
        checkStatus(code, bad, "remind");
        FUZZ_CHECK(!overflowsOnDevice(reply, 768), "remind reply overflows");
        break;
      }
      case 't': checkStatus(timerRequest(notes, b, nowMs, &bad), bad, "timer"); break;
      case 'c': checkStatus(countdownRequest(notes, b, (h & 1) ? &today : nullptr, &bad), bad, "countdown"); break;
      case 'n': notes.find(nowMs); break;
      case 'j': {  // the saved part survives a reboot
        bool over;
        const std::string saved = notesJson(notes, over);
        DeskNotes back;
        DynamicJsonDocument in(hostCapacity(768));
        FUZZ_CHECK(!deserializeJson(in, saved) && back.fromJson(in.as<JsonObjectConst>()), "notes.json reload");
        const std::string again = notesJson(back, over);
        FUZZ_CHECK(saved == again, "notes.json round trip differs:\n%s\n%s", saved.c_str(), again.c_str());
        break;
      }
      default: break;  // 'u', 'l' and anything else: just the clock and the checks below
    }
    focus.update(nowMs);
    focus.leftMs(nowMs);
    FUZZ_CHECK(focus.leftMs(nowMs) <= focus.phaseLenMs() || focus.phase() == FocusPhase::Off, "focus left > length");
    meeting.update(nowMs);
    meeting.leftMs(nowMs);
    notes.update(nowMs, epoch / 86400, (uint8_t)((epoch / 86400 + 4) % 7), minute);
    notes.timerLeftMs(nowMs);
    FUZZ_CHECK(notes.timerLeftMs(nowMs) <= notes.timerLenMs(), "timer left > length");
    notes.finding(nowMs);
    notes.takeDirty();
    char line[96];
    for (uint8_t l = 0; l < (uint8_t)Lang::Count; l++) {
      if (notes.countdown().label[0]) countdownLine((Lang)l, notes.countdown(), today, line, sizeof(line));
    }
    checkNotes(notes, nowMs, epoch);
    if (route == 'j') notes.dismiss();
  }
}
FUZZ_REGISTER(daily, fuzzDaily, kDailySeeds, kDailyDict, 2400);

// ============================== notes.json ==============================
// storage.cpp loadNotes(): <= 1024 bytes into a 768-byte document, DeskNotes::fromJson.

const char* const kNotesSeeds[] = {
    R"({"v":1,"alarms":[{"m":585,"d":62,"t":"standup","ld":20000},{"m":0,"d":127,"t":"midnight"}],"cd":{"l":"Trip","y":2026,"mo":12,"d":20}})",
    R"({"v":1,"alarms":[{"m":1439,"d":1,"t":"项目"},{"m":1,"d":2,"t":"b"},{"m":2,"d":4,"t":"c"},{"m":3,"d":8,"t":"d"},{"m":4,"d":16,"t":"e"}]})",
    R"({"v":1,"cd":{"l":"Leap","y":2028,"mo":2,"d":29}})",
    R"({"v":2})",
    nullptr};
const char* const kNotesDict[] = {"\"v\":1", "\"alarms\":", "\"m\":", "\"d\":", "\"t\":", "\"ld\":", "\"cd\":",
                                  "\"l\":", "\"y\":", "\"mo\":", "{\"m\":1,\"d\":1,\"t\":\"x\"},", nullptr};

void fuzzNotes(const uint8_t* d, size_t n) {
  if (n > 1024) return;  // loadNotes never parses a bigger file
  DynamicJsonDocument doc(768);
  if (deserializeJson(doc, reinterpret_cast<const char*>(d), n)) return;
  DeskNotes notes;
  if (!notes.fromJson(doc.as<JsonObjectConst>())) return;
  fuzz::reached();
  uint32_t nowMs = 5000;
  uint32_t day = 20000;
  for (int i = 0; i < 6; i++) {
    notes.update(nowMs, day, (uint8_t)(i % 7), (i * 300) % 1440);
    checkNotes(notes, nowMs, 1790000000 + i * 3600);
    nowMs += 3600000;
    day += i & 1;
  }
  bool over;
  const std::string saved = notesJson(notes, over);
  DeskNotes back;
  DynamicJsonDocument in(hostCapacity(768));
  FUZZ_CHECK(!deserializeJson(in, saved) && back.fromJson(in.as<JsonObjectConst>()), "reload");
  FUZZ_CHECK(notesJson(back, over) == saved, "notes.json round trip differs");
  char line[96];
  for (uint8_t l = 0; l < (uint8_t)Lang::Count; l++) {
    countdownLine((Lang)l, notes.countdown(), Date{2026, 10, 2}, line, sizeof(line));
    countdownLine((Lang)l, notes.countdown(), Date{2199, 12, 31}, line, sizeof(line));
  }
}
FUZZ_REGISTER(notes, fuzzNotes, kNotesSeeds, kNotesDict, 1100);

// ============================== UTF-8 and text helpers ==============================

const char* const kTextSeeds[] = {"hello", "h\xc3\xa9llo", "\xe9\xa1\xb9\xe7\x9b\xae", "\xf0\x9f\x98\x80 cat",
                                  "\xff\xfe\xc3", "\xed\xa0\x80\xc0\x80", "1.10.1", "1.2.3-rc", "", nullptr};

void fuzzUtf8(const uint8_t* d, size_t n) {
  Reader r(d, n);
  const uint8_t capByte = r.u8();
  const uint8_t maxChars = r.u8();
  const uint32_t num = r.u32();
  const std::string rest = r.rest();
  CStr s(rest.data(), rest.size());
  // utf8Next always advances and stops at the terminator.
  size_t count = 0;
  const char* p = s.s;
  const size_t len = strlen(s.s);
  while (*p) {
    const char* before = p;
    const uint32_t cp = utf8Next(p);
    FUZZ_CHECK(p > before && p <= s.s + len, "utf8Next did not advance within the string");
    // Lenient by design (miblo_utf8.h): F5..F7 leads, overlongs and surrogates decode as they are;
    // the consumers that need strict UTF-8 check it themselves (cleanText, validName).
    FUZZ_CHECK(cp <= 0x1FFFFF, "code point %x", cp);
    count++;
  }
  FUZZ_CHECK(utf8Next(p) == 0 && *p == 0, "utf8Next past the end");
  FUZZ_CHECK(utf8Length(s.s) == count, "utf8Length %zu != %zu", utf8Length(s.s), count);
  // utf8Copy: within cap, whole sequences only, a prefix of the source, at most maxChars.
  const size_t cap = capByte % 80;
  char* dst = static_cast<char*>(malloc(cap ? cap : 1));
  const size_t used = utf8Copy(dst, cap, s.s, maxChars);
  if (cap) {
    FUZZ_CHECK(used < cap && dst[used] == 0 && strlen(dst) == used, "utf8Copy length");
    FUZZ_CHECK(memcmp(dst, s.s, used) == 0, "utf8Copy not a prefix");
    FUZZ_CHECK(utf8Length(dst) <= maxChars, "utf8Copy too many characters");
  } else {
    FUZZ_CHECK(used == 0, "utf8Copy wrote with cap 0");
  }
  free(dst);
  // Formatting helpers on any number, into small buffers.
  char out[40];
  const size_t ocap = 1 + (capByte % sizeof(out));
  formatElapsed(num, out, ocap);
  FUZZ_CHECK(strlen(out) < ocap, "formatElapsed");
  formatAgo(num, out, ocap);
  formatCountdown(num, out, ocap);
  formatInState(num, out, ocap);
  formatTokens(((uint64_t)num << (maxChars % 33)) | capByte, out, ocap);
  FUZZ_CHECK(strlen(out) < ocap, "formatTokens");
  formatHHMM((int)(int8_t)capByte, (int)(int8_t)maxChars, out, ocap);
  formatUsd((float)(num % 1000000000u) / 100.0f, out, ocap);  // money() keeps usd in [0, 1e9)
  FUZZ_CHECK(strlen(out) < ocap, "formatUsd");
  const size_t half = len / 2;
  std::string a(s.s, half);
  compareVersions(a.c_str(), s.s + half);
  compareVersions(s.s, s.s);
  // Header helpers (security.cpp): token comparison and HTTP header scanning.
  constantTimeEquals(a.c_str(), s.s + half);
  fuzz::reached();
}
FUZZ_REGISTER(utf8, fuzzUtf8, kTextSeeds, nullptr, 600);

// ============================== time zones ==============================

const char* const kTzSeeds[] = {"\x20\x02" "America/Sao_Paulo",  "\x10\x05<-03>3",       "\x30\x01" "EST5EDT,M3.2.0,M11.1.0",
                                "\x02\x03" "UTC0",               "\x08\x07" "Asia/Kolkata", "\x40\x02<+0545>-5:45",
                                "\x30\x0c" "Europe/Lisbon",      "\x01\x01X",            nullptr};
const char* const kTzDict[] = {"America/", "Europe/", "Asia/", "Pacific/", "Etc/GMT+12", "UTC", "<", ">", "+",
                               "-", ",M3.2.0", "/2", nullptr};

void fuzzTz(const uint8_t* d, size_t n) {
  Reader r(d, n);
  const size_t cap = 1 + r.u8() % 72;
  const uint8_t which = r.u8();
  const std::string rest = r.rest();
  CStr s(rest.data(), rest.size());
  char* out = static_cast<char*>(malloc(cap));
  if (tzLookup(s.s, out, cap)) FUZZ_CHECK(strlen(out) < cap && tzLooksPosix(out), "tzLookup result");
  if (tzLooksPosix(s.s)) fuzz::reached();
  tzResolve(s.s, out, cap);
  FUZZ_CHECK(strlen(out) < cap, "tzResolve overflow");
  free(out);
  char hhmm[8];
  zoneHHMM(s.s, 1790000000u + which * 977u, hhmm, 1 + which % sizeof(hhmm));
  Config c;
  utf8Copy(c.tz2, sizeof(c.tz2), s.s);
  utf8Copy(c.tz2Label, sizeof(c.tz2Label), s.s + strlen(s.s) / 2, 12);
  char label[40];
  zoneLabel(c, label, 1 + which % sizeof(label));
  uint8_t mo, dd;
  uint16_t y;
  if (parseMonthDay(s.s, mo, dd)) FUZZ_CHECK(mo >= 1 && mo <= 12 && dd >= 1 && dd <= 31, "parseMonthDay");
  if (parseDate(s.s, y, mo, dd)) FUZZ_CHECK(mo >= 1 && mo <= 12 && dd >= 1 && dd <= 31, "parseDate");
  Mode m;
  modeFromCode(s.s, m);
}
FUZZ_REGISTER(tz, fuzzTz, kTzSeeds, kTzDict, 200);

// ============================== friends (LAN packets) ==============================
// UDP packets from any device on the network. friends_play replays a stream of them, with the
// clock and our own state moving, into one FriendPlay; everything it sends must encode and decode.

std::string hexPacket(const FriendPacket& p) {
  uint8_t buf[kFriendPacketMax];
  const size_t n = encodeFriendPacket(p, buf, sizeof(buf));
  static const char* hx = "0123456789abcdef";
  std::string s = "hex:";
  for (size_t i = 0; i < n; i++) s += hx[buf[i] >> 4], s += hx[buf[i] & 15];
  return s;
}

std::vector<std::string> friendSeeds() {
  std::vector<std::string> out;
  const FriendPacket::Type types[] = {FriendPacket::Beacon, FriendPacket::VisitAsk, FriendPacket::VisitOk,
                                      FriendPacket::Home,   FriendPacket::Who,      FriendPacket::Here,
                                      FriendPacket::Invite, FriendPacket::Host};
  for (FriendPacket::Type t : types) {
    FriendPacket p;
    p.type = t;
    strcpy(p.id, "miblo-4f2a");
    strcpy(p.name, "Nina \xe9\xa1\xb9");
    p.mascot = 2;
    p.flags = kFriendRoaming;
    if (t != FriendPacket::Beacon && t != FriendPacket::Who) strcpy(p.to, "miblo-0001");
    p.gift = Gift::Coffee;
    p.offset = 12;
    if (t == FriendPacket::Invite) strcpy(p.host, "miblo-0002");
    out.push_back(hexPacket(p));
  }
  return out;
}

const char* const* friendSeedTable() {
  static std::vector<std::string> s = friendSeeds();
  static std::vector<const char*> ptrs;
  if (ptrs.empty()) {
    for (const std::string& x : s) ptrs.push_back(x.c_str());
    ptrs.push_back(nullptr);
  }
  return ptrs.data();
}

void checkPacket(const FriendPacket& p) {
  checkStr(p.id, 15, "id");
  checkStr(p.name, 20, "name");
  checkStr(p.to, 15, "to");
  checkStr(p.host, 15, "host");
  FUZZ_CHECK((uint8_t)p.gift < (uint8_t)Gift::Count, "gift %u", (unsigned)p.gift);
  FUZZ_CHECK(p.type >= FriendPacket::Beacon && p.type <= FriendPacket::Host, "type");
  // What we decoded encodes again, and decodes to the same packet.
  uint8_t buf[kFriendPacketMax];
  const size_t len = encodeFriendPacket(p, buf, sizeof(buf));
  FUZZ_CHECK(len > 0, "a decoded packet does not encode");
  FriendPacket q;
  FUZZ_CHECK(decodeFriendPacket(buf, len, q), "re-encoded packet does not decode");
  FUZZ_CHECK(q.type == p.type && !strcmp(q.id, p.id) && !strcmp(q.name, p.name) && !strcmp(q.to, p.to) &&
                 q.gift == p.gift && q.mascot == p.mascot && q.flags == p.flags,
             "friend packet round trip differs");
}

void fuzzFriendPacket(const uint8_t* d, size_t n) {
  ExactBuf buf(d, n);
  FriendPacket p;
  if (decodeFriendPacket(reinterpret_cast<const uint8_t*>(buf.p), buf.n, p)) {
    fuzz::reached();
    checkPacket(p);
  }
}
FUZZ_REGISTER(friends_packet, fuzzFriendPacket, friendSeedTable(), nullptr, 300);

void drainFriends(FriendPlay& f) {
  FriendPacket out;
  int k = 0;
  while (f.nextPacket(out)) {
    FUZZ_CHECK(++k <= 64, "nextPacket never runs dry");
    uint8_t buf[kFriendPacketMax];
    const size_t len = encodeFriendPacket(out, buf, sizeof(buf));
    FUZZ_CHECK(len > 0, "an outgoing packet does not encode");
    FriendPacket q;
    FUZZ_CHECK(decodeFriendPacket(buf, len, q), "an outgoing packet does not decode (type %d id \"%s\" name \"%s\" to \"%s\" host \"%s\")", (int)out.type, out.id, out.name, out.to, out.host);
  }
}

// Records: [op][arg][len][packet bytes]. op%4: 0 receive, 1 update, 2 demo, 3 self change.
void fuzzFriendPlay(const uint8_t* d, size_t n) {
  FriendPlay f;
  // Like app.cpp: our identity is set every frame, right before update().
  std::string selfName = "Desk";
  const char* selfId = "miblo-0001";
  uint8_t selfMascot = 1;
  uint32_t now = 1000, rnd = 7;
  int heard = 0;
  Reader r(d, n);
  int steps = 0;
  while (r.left() && steps++ < 200) {
    const uint8_t op = r.u8();
    const uint8_t arg = r.u8();
    now += (uint32_t)arg * (op & 0x80 ? 1000 : 37);
    rnd = rnd * 1103515245u + 12345u + arg;
    switch (op % 4) {
      case 0: {
        std::string pkt = r.bytes(160);
        ExactBuf buf(pkt.data(), pkt.size());
        FriendPacket p;
        if (decodeFriendPacket(reinterpret_cast<const uint8_t*>(buf.p), buf.n, p)) {
          if (!heard++) fuzz::reached();
          f.receive(p, now, 0xC0A80000u | (uint32_t)(arg & 7));
        }
        break;
      }
      case 1:
        f.setSelf(selfId, selfName.c_str(), selfMascot);
        f.update(now, (arg & 0x40) == 0, arg & 0x0F, rnd);
        break;
      case 2: f.demo(now, now + (uint32_t)arg * 1000); break;
      case 3: {
        const std::string name = r.bytes(40);
        CStr nm(name.data(), name.size());
        selfName = nm.s;  // any bytes: the settings accept more than the other Miblos do
        selfId = (arg & 1) ? "miblo-0001" : "miblo-00ff";
        selfMascot = arg % 4;
        f.setSelf(selfId, selfName.c_str(), selfMascot);
        break;
      }
    }
    drainFriends(f);
    FUZZ_CHECK(f.count() <= kMaxFriends, "friends %u > %u", f.count(), kMaxFriends);
    const VisitView v = f.visit(now);
    checkStr(v.name, 20, "visit name");
    FUZZ_CHECK(v.extra < kMaxGuests, "extra guests %u", v.extra);
    if (const char* g = f.greeting(now)) FUZZ_CHECK(utf8Length(g) <= 20 && strlen(g) < 64, "greeting");
    if (const char* b = f.napBuddy()) FUZZ_CHECK(strlen(b) < 64, "nap buddy");
  }
}

std::vector<std::string> playSeeds() {
  std::vector<std::string> out;
  const char* const* pk = friendSeedTable();
  std::string all = "hex:";
  for (size_t i = 0; pk[i]; i++) {
    const std::string h = pk[i] + 4;
    char rec[16];
    snprintf(rec, sizeof(rec), "%02x%02x%02zx", (unsigned)(i * 4) & 0xFC, 3, h.size() / 2);
    all += rec + h;
    all += "0124";  // update, enabled
    out.push_back(std::string("hex:0124") + rec + h + "0188");
  }
  out.push_back(all);
  return out;
}
const char* const* playSeedTable() {
  static std::vector<std::string> s = playSeeds();
  static std::vector<const char*> ptrs;
  if (ptrs.empty()) {
    for (const std::string& x : s) ptrs.push_back(x.c_str());
    ptrs.push_back(nullptr);
  }
  return ptrs.data();
}
FUZZ_REGISTER(friends_play, fuzzFriendPlay, playSeedTable(), nullptr, 2000);

// ============================== mDNS ==============================

const char* const kMdnsSeeds[] = {
    // 2-byte source port, 1-byte reply cap, then the packet.
    // PTR query for _miblo._tcp.local
    "hex:14e9ff" "000000000001000000000000" "065f6d69626c6f045f746370056c6f63616c00" "000c0001",
    // A query for miblo-4f2a.local with the QU bit
    "hex:14e9ff" "000000000001000000000000" "0a6d69626c6f2d34663261056c6f63616c00" "00018001",
    // _services._dns-sd._udp.local PTR, legacy port
    "hex:d431ff" "123400000001000000000000" "095f7365727669636573075f646e732d7364045f756470056c6f63616c00" "000c0001",
    // SRV + TXT for the instance, via a compression pointer
    "hex:14e9ff" "000000000002000000000000" "0a4d69626c6f2d34463241065f6d69626c6f045f746370056c6f63616c00" "00210001"
    "c00c00100001",
    // a response packet (QR set): ignored
    "hex:14e9ff" "000084000000000100000000" "c00c000c0001",
    nullptr};

void fuzzMdns(const uint8_t* d, size_t n) {
  Reader r(d, n);
  const uint16_t port = (uint16_t)(r.u8() << 8 | r.u8());
  const uint8_t capByte = r.u8();
  const std::string pkt = r.rest();
  MdnsInfo info{};
  char txt[40];
  mdnsPublicIdentity(info, "miblo-4f2a", "Miblo-4F2A", txt, sizeof(txt));
  info.ip[0] = 192, info.ip[1] = 168, info.ip[2] = 0, info.ip[3] = 41;
  info.port = 80;
  ExactBuf in(pkt.data(), pkt.size());
  const size_t cap = capByte == 0xFF ? 512 : capByte * 2u;
  uint8_t* out = static_cast<uint8_t*>(malloc(cap ? cap : 1));
  const MdnsReply rep = mdnsRespond(reinterpret_cast<const uint8_t*>(in.p), in.n, port, info, out, cap);
  FUZZ_CHECK(rep.len <= cap, "mdns reply %zu > cap %zu", rep.len, cap);
  if (rep.len) fuzz::reached();
  free(out);
}
FUZZ_REGISTER(mdns, fuzzMdns, kMdnsSeeds, nullptr, 1500);

// The identity and announcement from any id/name, into any buffer size.
void fuzzMdnsAnnounce(const uint8_t* d, size_t n) {
  Reader r(d, n);
  const size_t txtCap = 1 + r.u8() % 64;  // the firmware passes a fixed buffer (never 0 bytes)
  const size_t cap = r.u16() % 600;
  const std::string id = r.bytes(80), name = r.bytes(80);
  CStr ids(id.data(), id.size()), names(name.data(), name.size());
  char* txt = static_cast<char*>(malloc(txtCap ? txtCap : 1));
  MdnsInfo info{};
  mdnsPublicIdentity(info, ids.s, names.s, txt, txtCap);
  if (txtCap) FUZZ_CHECK(strnlen(txt, txtCap) < txtCap, "txt not terminated");
  uint8_t* out = static_cast<uint8_t*>(malloc(cap ? cap : 1));
  const size_t len = mdnsAnnounce(info, out, cap);
  FUZZ_CHECK(len <= cap, "announce %zu > cap %zu", len, cap);
  if (len) fuzz::reached();
  free(out);
  free(txt);
}
const char* const kAnnounceSeeds[] = {"\x28\x01\x02\x0a" "miblo-4f2a\x0a" "Miblo-4F2A", "\x05\x10\x01\x3f" "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", nullptr};
FUZZ_REGISTER(mdns_announce, fuzzMdnsAnnounce, kAnnounceSeeds, nullptr, 300);

// ============================== HTTP headers and pairing ==============================
// Raw header bytes (the upload guard), Authorization headers, pairing and presence codes.

const char* const kHttpSeeds[] = {
    "POST /update HTTP/1.1\r\nHost: 192.168.0.41\r\nContent-Type: multipart/form-data; boundary=x\r\nContent-Length: 482113\r\n\r\n",
    "Bearer 0123456789abcdef0123456789abcdef",
    "content-length:   12\r\ncontent-type: MULTIPART/form-data\r\n",
    "Content-Length: 99999999999999999999\r\n",
    "Content-Length: -1\r\nContent-Length: 5\r\n",
    "1234",
    nullptr};
const char* const kHttpDict[] = {"Content-Length:", "content-length: ", "Content-Type:", "multipart/", "Bearer ",
                                 "bearer\t", "\r\n", "\r\n\r\n", ": ", nullptr};

void fuzzHttp(const uint8_t* d, size_t n) {
  ExactBuf hdr(d, n);  // the raw header buffer is not NUL-terminated
  uint32_t clen = 0;
  if (findContentLength(hdr.p, hdr.n, clen)) fuzz::reached();
  contentTypeIsMultipart(hdr.p, hdr.n);
  CStr s(d, n);
  for (size_t cap : {(size_t)1, (size_t)8, (size_t)33, (size_t)64}) {
    char* tok = static_cast<char*>(malloc(cap));
    if (bearerToken(s.s, tok, cap)) FUZZ_CHECK(strlen(tok) < cap, "bearerToken overflow");
    free(tok);
  }
  TokenStore tokens;
  tokens.add("0123456789abcdef0123456789abcdef", "mac");
  infoView(tokens, s.s);
  tokens.find(s.s);
  tokens.remove(0, s.s);
  tokens.add(s.s, s.s);  // a host name from a request: truncated, never overflows
  FUZZ_CHECK(strlen(tokens.at(tokens.count() - 1).host) < sizeof(TokenEntry::host), "host");
  PairingGuard pg;
  pg.setCode("1234");
  pg.check(s.s, 1000);
  PresenceGate gate;
  gate.open(PresenceGate::Purpose::Settings, "5678", 1000);
  gate.check(PresenceGate::Purpose::Settings, s.s, 2000);
  WebSession ws;
  const uint8_t rnd[16] = {1, 2, 3};
  ws.issue(rnd, 1000);
  ws.valid(s.s, 2000);
}
FUZZ_REGISTER(http, fuzzHttp, kHttpSeeds, kHttpDict, 1200);

}  // namespace
