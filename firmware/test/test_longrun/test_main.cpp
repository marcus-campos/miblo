// Long-run stability: every daily-life module driven together, frame by frame, the way app.cpp
// drives them (alert queue, focus, meeting, wellness, end of day, Monday's recap, desk notes, the
// strong cue, pet mode, friends), over simulated days of random life: snapshots with random
// sessions, states and alerts, API commands through the real request handlers, NTP arriving
// late, clock jumps of +-1 h and DST changes, Wi-Fi drops, the computer going away, loop stalls
// of up to 10 s, millis() wrapping, reboots that reload notes.json.
//
// Invariants checked on every frame (any break fails the test, with the first few listed):
//   - nothing grows without bound (alert queue, friends, notes, saved and listed JSON fit);
//   - no screen is stuck: every daily screen (and alert, visit, cue...) ends within its bound;
//   - a session that needs you is always visible (alert, the main list or the waiting mark)
//     within one alert cycle while the computer is connected;
//   - no reminder, alarm (per day), timer or focus end fires twice; nothing fires early;
//     reminders are never lost;
//   - pet mode and the sleeping panel give way on the first frame of real activity.
// Under `pio test -e native_asan` the same run also catches memory errors and UB.
//
// The suite runs a short deterministic version. The long one (60+ days):
//   MIBLO_LONGRUN_DAYS=61 MIBLO_LONGRUN_SEEDS=4 pio test -e native_asan -f test_longrun
#include <ArduinoJson.h>
#include <unity.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <initializer_list>
#include <map>
#include <vector>
#include <set>
#include <string>

#include "miblo_alerts.h"
#include "miblo_config.h"
#include "miblo_cues.h"
#include "miblo_daily.h"
#include "miblo_dayend.h"
#include "miblo_desknotes.h"
#include "miblo_focus.h"
#include "miblo_friends.h"
#include "miblo_limits.h"
#include "miblo_meeting.h"
#include "miblo_overview.h"
#include "miblo_policy.h"
#include "miblo_snapshot.h"
#include "miblo_wellness.h"

using namespace miblo;

void setUp() {}
void tearDown() {}

namespace {

// ---- small deterministic PRNG (xorshift64*) ----
struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 1) {}
  uint32_t next() {
    s ^= s >> 12;
    s ^= s << 25;
    s ^= s >> 27;
    return (uint32_t)((s * 0x2545F4914F6CDD1Dull) >> 32);
  }
  uint32_t below(uint32_t n) { return n ? next() % n : 0; }
  bool chance(uint32_t perMillion) { return below(1000000) < perMillion; }
};

// Days since 1970-01-01 -> civil date (H. Hinnant).
Date civil(int64_t z) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = (unsigned)(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t y = (int64_t)yoe + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned d = doy - (153 * mp + 2) / 5 + 1;
  const unsigned m = mp < 10 ? mp + 3 : mp - 9;
  return Date{(uint16_t)(y + (m <= 2)), (uint8_t)m, (uint8_t)d};
}

uint8_t pick(Rng& r, std::initializer_list<uint8_t> xs) { return xs.begin()[r.below((uint32_t)xs.size())]; }

uint32_t dayKeyOf(const Date& d) { return d.year * 400u + d.month * 32u + d.day; }  // as app.cpp

// The documents api.cpp and storage.cpp size for the ESP8266 (16-byte slots), scaled to this
// host's slots (64-bit: about twice as big), as kDocMax in miblo_snapshot.cpp does.
constexpr size_t kSlotScale = sizeof(void*) / 4;
constexpr size_t kBodyDoc = 384 * kSlotScale, kReplyDoc = 768 * kSlotScale, kNotesDoc = 768 * kSlotScale;

constexpr uint32_t kMaxStepMs = 10000;  // the longest loop stall simulated
constexpr uint8_t kSimSessions = 6;

struct Violations {
  uint32_t n = 0;
  std::string first;
  void add(const char* what, uint64_t simMs) {
    if (n++ < 8) {
      char line[200];
      snprintf(line, sizeof(line), "[t=%.1fh] %s; ", simMs / 3600000.0, what);
      first += line;
    }
  }
};

struct Stats {
  uint64_t frames = 0, snapshots = 0, commands = 0, reminders = 0, alarms = 0, timers = 0, focusEnds = 0,
           cues = 0, visits = 0, wakes = 0, reboots = 0, nudges = 0, dayEnds = 0, recaps = 0, alerts = 0,
           alarmDays = 0, alarmMissedDays = 0;
};

class Sim {
 public:
  Sim(uint64_t seed, bool reboots) : rng_(seed), reboots_(reboots) {
    // Every daily-life feature on, with random choices; reminders on or off.
    cfg_.reminderMin = pick(rng_, {0, 1, 2, 5});
    cfg_.heroPermSec = 10;
    cfg_.heroDoneSec = 5;
    cfg_.flashBlinks = 2;
    cfg_.petMin = pick(rng_, {5, 10, 15});
    cfg_.sleepMin = pick(rng_, {0, 30, 60});
    cfg_.breakAfterMin = pick(rng_, {0, 60, 90, 120});
    cfg_.waterMin = pick(rng_, {0, 60, 90});
    cfg_.eyes = rng_.below(2);
    cfg_.endOfDay = true;
    cfg_.weekly = true;
    cfg_.fanfareMin = pick(rng_, {0, 3, 5, 10});
    cfg_.focusQuiet = rng_.below(2);
    utc_ = 1790000000 + rng_.below(86400 * 7);  // early October 2026, any time of the week
    tz_ = 3600 * ((int)rng_.below(5) - 2);
    boot(reboots_ ? rng_.below(600000) : 0xFFFFFFFFu - 3600000u);  // no reboots: wraps after 1 h and ~49.7 d later
  }

  void run(uint32_t days) {
    const uint64_t end = (uint64_t)days * 86400000ull;
    while (simMs_ < end) step();
    finish();
  }

  Violations v;
  Stats st;

 private:
  // ---------------- the world ----------------
  Rng rng_;
  bool reboots_;
  uint64_t simMs_ = 0;    // real time since the start
  int64_t utc_ = 0;       // real UTC, s (computer's clock)
  uint32_t subMs_ = 0;    // ms within the current UTC second
  int tz_ = 0;            // local offset, s (DST moves it)
  int64_t clockErr_ = 0;  // device clock error, s (a wrong NTP answer, then corrected)
  bool wifi_ = true;
  uint64_t wifiBackAt_ = 0;
  bool present_ = true;  // the computer and the bridge
  uint64_t presentToggleAt_ = 0;
  uint64_t nextSnapAt_ = 0;
  uint64_t nextCmdAt_ = 0;
  uint64_t nextSessionChangeAt_ = 0;
  uint64_t nextDstAt_ = 0;
  // sessions on the computer
  struct Sess {
    bool used = false;
    char id[9] = "";
    SessionState st = SessionState::Idle;
    uint32_t since = 0;
  } sess_[kSimSessions];
  uint32_t bridgeSeq_ = 0, nextAlertId_ = 1;
  AlertItem ring_[kMaxAlerts] = {};  // the bridge's recent alerts (resent until they age out)
  uint8_t ringN_ = 0;
  Snapshot snap_{};
  uint8_t h5Pct_ = 0, d7Pct_ = 0;
  uint32_t h5Reset_ = 0, d7Reset_ = 0;

  // ---------------- the device ----------------
  uint32_t ms_ = 0;  // millis()
  bool timeKnown_ = false;
  uint64_t ntpAt_ = 0;
  Config cfg_;
  AlertSequencer alerts_;
  RunTracker runs_;
  LimitWatch limits_;
  FocusTimer focus_;
  MeetingMode meeting_;
  DeskNotes notes_;
  StrongCue cue_;
  WellnessClock wellness_;
  EndOfDay dayEnd_;
  WeeklyRecap weekly_;
  PetLatch petLatch_;
  QuietClock quiet_;
  FriendPlay friends_;
  FriendPlay peer_;  // another Miblo on the network
  bool peerRoaming_ = false;
  bool hasSnapshot_ = false;
  uint32_t lastSnapshotMs_ = 0, lastInteractionMs_ = 0, awaySinceMs_ = 0, bootMs_ = 0;
  bool bootAnimDone_ = false;
  ScreenId current_ = ScreenId::Boot;
  std::string savedNotes_;  // /notes.json

  // ---------------- invariant bookkeeping ----------------
  ScreenId runScreen_ = ScreenId::Boot;
  uint64_t runSinceSim_ = 0;
  uint64_t pendingSinceSim_ = 0, lastShownSim_ = 0;
  bool pendingFresh_ = false;
  bool wasPet_ = false, wasAsleep_ = false;
  uint32_t textN_ = 0;
  std::string idText_[kMaxReminders + kMaxAlarms + 1];  // what each note id holds now
  struct Due {
    uint32_t dueMs;
    uint64_t dueSim;
    bool at;  // "at HH:MM": follows the local clock, up to kAtSlackMs either side of dueMs
  };
  std::map<std::string, Due> remPending_;  // reminders not fired yet
  std::set<std::string> remFired_;
  std::set<std::pair<std::string, uint32_t>> alarmFired_;  // (text, dayKey of the frame)
  std::map<std::string, uint64_t> alarmLastFire_;          // sim time of each alarm's last fire
  std::map<std::string, uint64_t> alarmDeleted_;           // sim time it was deleted
  std::set<std::string> alarmLive_;                      // alarms set now
  struct AlarmSpec {
    uint16_t minute;
    uint8_t days;
  };
  std::map<std::string, AlarmSpec> alarmSpec_;
  uint32_t timerGen_ = 0, timerFiredGen_ = 0;  // a timer start and the last one that fired
  bool timerLive_ = false;
  uint32_t timerDueMs_ = 0;
  uint32_t focusGen_ = 0, focusFinishedGen_ = 0;
  // alarm coverage: per (alarm, local day) whether its minute passed in a calm stretch
  int prevMinute_ = -1;
  uint32_t prevDayKey_ = 0;
  uint64_t lastJumpSim_ = 0;
  std::vector<std::pair<std::string, uint64_t>> alarmExpected_;  // (alarm, sim time its minute passed)

  // ---------------- time ----------------
  bool localNow(Date& d, int& minute, uint8_t& weekday) const {
    if (!timeKnown_) return false;
    const int64_t t = utc_ + clockErr_ + tz_;
    const int64_t days = t >= 0 ? t / 86400 : (t - 86399) / 86400;
    const int64_t sec = t - days * 86400;
    d = civil(days);
    minute = (int)(sec / 60);
    weekday = (uint8_t)(((days % 7) + 11) % 7);  // 1970-01-01 was a Thursday (4)
    return true;
  }

  void boot(uint32_t ms0) {
    bootTimes_.push_back(simMs_);
    ms_ = ms0;
    bootMs_ = ms0;
    timeKnown_ = false;
    ntpAt_ = simMs_ + rng_.below(3 * 3600000u);  // NTP up to 3 h late
    alerts_ = AlertSequencer();
    alerts_.setTiming(alertTiming(cfg_));
    runs_ = RunTracker();
    limits_ = LimitWatch();
    focus_ = FocusTimer();
    meeting_ = MeetingMode();
    notes_ = DeskNotes();
    cue_ = StrongCue();
    wellness_ = WellnessClock();
    dayEnd_ = EndOfDay();
    weekly_ = WeeklyRecap();
    petLatch_ = PetLatch();
    quiet_ = QuietClock();
    friends_ = FriendPlay();
    hasSnapshot_ = false;
    bootAnimDone_ = false;
    lastInteractionMs_ = ms0 - 10 * 60000;
    current_ = ScreenId::Boot;
    // What survives a reboot: the alarms and the countdown (notes.json).
    for (auto& t : idText_) t.clear();
    remPending_.clear();
    timerLive_ = false;
    focusGen_++;
    alarmLive_.clear();
    if (!savedNotes_.empty()) {
      DynamicJsonDocument doc(kNotesDoc);
      if (deserializeJson(doc, savedNotes_)) {
        v.add("notes.json does not parse", simMs_);
      } else if (!notes_.fromJson(doc.as<JsonObjectConst>())) {
        v.add("notes.json refused on reload", simMs_);
      }
      // The ids and texts that came back.
      DynamicJsonDocument list(kReplyDoc);
      notes_.listJson(list.to<JsonArray>(), ms_, 0);
      for (JsonObjectConst e : list.as<JsonArrayConst>()) {
        const int id = e["id"] | 0;
        const char* text = e["text"] | "";
        if (id > 0 && id <= kMaxReminders + kMaxAlarms) idText_[id] = text;
        if (id > kMaxReminders) alarmLive_.insert(text);
      }
    }
  }

  // ---------------- the computer ----------------
  void addAlert(AlertKind k, const char* sid) {
    AlertItem a{};
    a.id = nextAlertId_++;
    a.kind = k;
    snprintf(a.sid, sizeof(a.sid), "%s", sid);
    if (ringN_ == kMaxAlerts) {
      memmove(ring_, ring_ + 1, sizeof(AlertItem) * (kMaxAlerts - 1));
      ringN_--;
    }
    ring_[ringN_++] = a;
  }

  void computerStep() {
    if (simMs_ >= presentToggleAt_) {
      present_ = !present_;
      // Away for minutes to a night; present for an hour to a long day.
      presentToggleAt_ = simMs_ + (present_ ? 3600000ull + rng_.below(12 * 3600000u) : 60000ull + rng_.below(14 * 3600000u));
      if (present_ && rng_.chance(200000)) {  // the bridge restarted: seq and ids start over
        bridgeSeq_ = 0;
        nextAlertId_ = 1;
        ringN_ = 0;
      }
    }
    if (!present_) return;
    if (simMs_ >= nextSessionChangeAt_) {
      nextSessionChangeAt_ = simMs_ + 2000 + rng_.below(rng_.chance(100000) ? 900000 : 120000);
      Sess& s = sess_[rng_.below(kSimSessions)];
      if (!s.used) {
        s.used = true;
        snprintf(s.id, sizeof(s.id), "s%07u", rng_.below(10000000));
        s.st = SessionState::Idle;
      } else if (rng_.chance(30000)) {
        s.used = false;  // the session ended
        return;
      }
      static const SessionState kNext[] = {SessionState::Running, SessionState::Running, SessionState::Perm,
                                           SessionState::Question, SessionState::Done, SessionState::Idle};
      const SessionState next = kNext[rng_.below(6)];
      if (next == s.st) return;
      s.st = next;
      s.since = (uint32_t)utc_;
      if (next == SessionState::Perm) addAlert(AlertKind::Perm, s.id);
      if (next == SessionState::Question) addAlert(AlertKind::Question, s.id);
      if (next == SessionState::Done) addAlert(AlertKind::Done, s.id);
    }
  }

  void buildSnapshot() {
    Snapshot& s = snap_;
    s = Snapshot{};
    s.seq = ++bridgeSeq_;
    s.now = (uint32_t)utc_;
    snprintf(s.host, sizeof(s.host), "mac");
    s.hasUsage = true;
    // Usage climbs while sessions run; each window starts over (low) when it resets.
    if ((int64_t)h5Reset_ <= utc_) {
      h5Reset_ = (uint32_t)utc_ + 5 * 3600;
      h5Pct_ = 0;
    }
    if ((int64_t)d7Reset_ <= utc_) {
      d7Reset_ = (uint32_t)utc_ + 7 * 86400;
      d7Pct_ = 0;
    }
    if (h5Pct_ < 100 && rng_.chance(20000)) h5Pct_++;
    if (d7Pct_ < 100 && rng_.chance(3000)) d7Pct_++;
    s.h5 = UsageWindow{true, h5Pct_, h5Reset_, 0, false};
    s.d7 = UsageWindow{true, d7Pct_, d7Reset_, 0, false};
    s.todayTurns = (uint16_t)rng_.below(300);
    s.todayWorkSec = rng_.below(36000);
    Date d;
    int minute;
    uint8_t wd = 0;
    if (localNow(d, minute, wd) && wd == 1) s.week = WeekStats{true, (uint8_t)rng_.below(7), 212, 61200, 31.5f};
    for (const Sess& x : sess_) {
      if (!x.used) continue;
      SessionRow& r = s.sessions[s.count++];
      memset(&r, 0, sizeof(r));
      snprintf(r.id, sizeof(r.id), "%s", x.id);
      snprintf(r.name, sizeof(r.name), "proj-%c", x.id[1]);
      r.st = x.st;
      r.ctx = -1;
      r.tok = -1;
      r.since = x.since;
      if (x.st == SessionState::Running && rng_.chance(300000)) r.ts = x.since;  // a long Bash command
    }
    s.alertCount = ringN_;
    memcpy(s.alerts, ring_, sizeof(AlertItem) * ringN_);
  }

  // ---------------- commands (the real request handlers, as api.cpp's dailyRoute calls them) ----------------
  std::string newText(char kind) {
    char t[16];
    snprintf(t, sizeof(t), "%c%u", kind, ++textN_);
    return t;
  }

  int call(int (*fn)(Sim&, JsonObjectConst, JsonObject, const char**), const std::string& body) {
    DynamicJsonDocument doc(kBodyDoc);
    if (body.size() > 300 || deserializeJson(doc, body) || !doc.is<JsonObject>()) {
      v.add("a command body did not parse", simMs_);
      return 0;
    }
    DynamicJsonDocument reply(kReplyDoc);
    const char* bad = nullptr;
    const int code = fn(*this, doc.as<JsonObjectConst>(), reply.to<JsonObject>(), &bad);
    if (code != 200 && code != 400 && code != 409) v.add("a command returned an odd status", simMs_);
    if (code != 200 && !bad) v.add("an error without a field", simMs_);
    if (reply.overflowed()) v.add("a reply overflowed its document", simMs_);
    lastId_ = reply["id"] | 0;
    lastInteractionMs_ = ms_;
    runSinceSim_ = simMs_;  // a new request may rightly keep the same screen up longer
    st.commands++;
    return code;
  }
  int lastId_ = 0;

  int minuteNow() {
    Date d;
    int m;
    uint8_t wd;
    return localNow(d, m, wd) ? m : -1;
  }

  void command() {
    char body[200];
    switch (rng_.below(14)) {
      case 0:
      case 1: {
        snprintf(body, sizeof(body), "{\"focusMin\":%u,\"breakMin\":%u,\"rounds\":%u}", 5 + rng_.below(56),
                 1 + rng_.below(15), 1 + rng_.below(6));
        if (call([](Sim& s, JsonObjectConst b, JsonObject, const char** bad) {
              return focusRequest(s.focus_, b, s.ms_, bad);
            }, body) == 200)
          focusGen_++;
        break;
      }
      case 2:
        call([](Sim& s, JsonObjectConst b, JsonObject, const char** bad) { return focusRequest(s.focus_, b, s.ms_, bad); },
             "{\"stop\":true}");
        focusGen_++;
        break;
      case 3:
        snprintf(body, sizeof(body), rng_.below(3) ? "{\"min\":%u}" : "{\"off\":true}", 1 + rng_.below(120));
        call([](Sim& s, JsonObjectConst b, JsonObject, const char** bad) {
          return meetingRequest(s.meeting_, b, s.ms_, bad);
        }, body);
        break;
      case 4:
        snprintf(body, sizeof(body), rng_.below(3) ? "{\"text\":\"back in 5\",\"min\":%u}" : "{\"off\":true}",
                 1 + rng_.below(kSayMaxMin));
        call([](Sim& s, JsonObjectConst b, JsonObject, const char** bad) { return sayRequest(s.notes_, b, s.ms_, bad); },
             body);
        break;
      case 5:
      case 6: {  // a reminder in N minutes
        const std::string t = newText('R');
        const unsigned in = 1 + rng_.below(rng_.below(4) ? 90 : 1440);
        snprintf(body, sizeof(body), "{\"in\":%u,\"text\":\"%s\"}", in, t.c_str());
        if (call(remind, body) == 200) noteReminder(t, ms_ + in * 60000u, simMs_ + in * 60000ull, false);
        break;
      }
      case 7: {  // a reminder at HH:MM
        const std::string t = newText('R');
        const int now = minuteNow();
        const unsigned at = rng_.below(1440);
        snprintf(body, sizeof(body), "{\"at\":\"%02u:%02u\",\"text\":\"%s\"}", at / 60, at % 60, t.c_str());
        if (call(remind, body) == 200) {
          int ahead = (int)at - now;
          if (ahead <= 0) ahead += 1440;
          noteReminder(t, ms_ + (uint32_t)ahead * 60000u, simMs_ + (uint64_t)ahead * 60000u, true);
        }
        break;
      }
      case 8: {  // a recurring alarm
        const std::string t = newText('A');
        const unsigned at = rng_.below(1440), days = 1 + rng_.below(0x7F);
        snprintf(body, sizeof(body), "{\"at\":\"%02u:%02u\",\"days\":%u,\"text\":\"%s\"}", at / 60, at % 60, days,
                 t.c_str());
        if (call(remind, body) == 200) {
          if (lastId_ <= kMaxReminders) v.add("an alarm got a reminder id", simMs_);
          if (lastId_ > 0 && lastId_ <= kMaxReminders + kMaxAlarms) idText_[lastId_] = t;
          alarmLive_.insert(t);
          alarmSpec_[t] = AlarmSpec{(uint16_t)at, (uint8_t)days};
        }
        break;
      }
      case 9: {  // delete one
        const unsigned id = 1 + rng_.below(kMaxReminders + kMaxAlarms);
        snprintf(body, sizeof(body), "{\"delete\":%u}", id);
        if (call(remind, body) == 200) {
          remPending_.erase(idText_[id]);
          if (alarmLive_.erase(idText_[id])) alarmDeleted_[idText_[id]] = simMs_;
          idText_[id].clear();
        }
        break;
      }
      case 10:
        call(remind, "{\"dismiss\":true}");
        break;
      case 11: {
        const unsigned min = 1 + rng_.below(rng_.below(4) ? 30 : kTimerMaxMin);
        snprintf(body, sizeof(body), rng_.below(4) ? "{\"min\":%u}" : "{\"stop\":true}", min);
        const bool stop = strstr(body, "stop") != nullptr;
        if (call([](Sim& s, JsonObjectConst b, JsonObject, const char** bad) { return timerRequest(s.notes_, b, s.ms_, bad); },
                 body) == 200) {
          timerGen_++;
          timerLive_ = !stop;
          timerDueMs_ = ms_ + min * 60000u;
        }
        break;
      }
      case 12: {
        snprintf(body, sizeof(body), rng_.below(3) ? "{\"label\":\"release\",\"md\":\"%02u-%02u\"}" : "{\"off\":true}",
                 1 + rng_.below(12), 1 + rng_.below(28));
        call([](Sim& s, JsonObjectConst b, JsonObject, const char** bad) {
          Date d;
          int m;
          uint8_t wd;
          return countdownRequest(s.notes_, b, s.localNow(d, m, wd) ? &d : nullptr, bad);
        }, body);
        break;
      }
      case 13:
        notes_.find(ms_);
        lastInteractionMs_ = ms_;
        runSinceSim_ = simMs_;
        break;
    }
    // GET /api/remind after every command: the listing always fits the reply document.
    DynamicJsonDocument list(kReplyDoc);
    notes_.listJson(list.to<JsonObject>().createNestedArray("items"), ms_, timeKnown_ ? (uint32_t)(utc_ + clockErr_) : 0);
    list["ok"] = true;
    if (list.overflowed()) v.add("GET /api/remind overflowed", simMs_);
    if (list["items"].size() > kMaxReminders + kMaxAlarms) v.add("more notes listed than slots", simMs_);
  }

  static int remind(Sim& s, JsonObjectConst b, JsonObject out, const char** bad) {
    return remindRequest(s.notes_, b, s.ms_, s.minuteNow(), out, bad);
  }

  void noteReminder(const std::string& t, uint32_t dueMs, uint64_t dueSim, bool at) {
    if (lastId_ < 1 || lastId_ > kMaxReminders) {
      v.add("a reminder got an alarm id", simMs_);
      return;
    }
    // A full set of reminders gives the held one's slot to the new one (already fired).
    if (!idText_[lastId_].empty() && remPending_.count(idText_[lastId_]))
      v.add("a pending reminder was overwritten", simMs_);
    idText_[lastId_] = t;
    remPending_[t] = Due{dueMs, dueSim, at};
  }

  // ---------------- one frame of app.cpp ----------------
  void frame() {
    st.frames++;
    Date day{};
    int minute = 0;
    uint8_t weekday = 0;
    const bool timeKnown = localNow(day, minute, weekday);
    const int minuteNow = timeKnown ? minute : -1;
    const uint32_t dayKey = timeKnown ? dayKeyOf(day) : 0;
    trackAlarmCoverage(timeKnown, dayKey, minuteNow, weekday);

    meeting_.update(ms_);
    const FocusEvent fe = focus_.update(ms_);
    if (fe == FocusEvent::BreakStarted || fe == FocusEvent::Finished) cue_.fire(CueKind::FocusEnd, ms_);
    else if (fe == FocusEvent::BackPrompt) cue_.fire(CueKind::BreakEnd, ms_);
    if (fe == FocusEvent::Finished) {
      st.focusEnds++;
      if (focusFinishedGen_ == focusGen_) v.add("a focus session finished twice", simMs_);
      focusFinishedGen_ = focusGen_;
    }
    const NoteKind fired = notes_.update(ms_, dayKey, weekday, minuteNow);
    if (fired == NoteKind::Timer) cue_.fire(CueKind::Timer, ms_);
    else if (fired == NoteKind::Alarm) cue_.fire(CueKind::Alarm, ms_);
    else if (fired == NoteKind::Reminder) cue_.fire(CueKind::Reminder, ms_);
    if (fired != NoteKind::None) {
      st.cues++;
      checkFired(fired, dayKey);
    }
    if (notes_.takeDirty()) save();

    alerts_.setModifiers({cfg_.insist, meeting_.on(), focus_.phase() == FocusPhase::Focus && cfg_.focusQuiet});
    const AlertView& alert = alerts_.update(snap_, ms_);
    if (alerts_.queued() > kMaxAlerts) v.add("alert queue over its size", simMs_);
    bool fanfare = false;
    uint32_t fanDur = 0;
    if (alert.phase == AlertPhase::Hero && alert.kind == AlertKind::Done && cfg_.fanfareMin &&
        runs_.stats(alert.sid, fanDur) && fanDur >= cfg_.fanfareMin * 60u) {
      fanfare = true;
      alerts_.extendHero(kFanfareMs);
    }

    ScreenInputs in;
    in.nowMs = ms_;
    if (!bootAnimDone_ && ms_ - bootMs_ >= 2400) bootAnimDone_ = true;
    in.bootAnimDone = bootAnimDone_;
    in.net = wifi_ ? NetState::Connected : NetState::Connecting;
    in.paired = true;
    in.hasSnapshot = hasSnapshot_;
    in.lastSnapshotMs = lastSnapshotMs_;
    in.alert = alert.phase;
    in.limitReset = limits_.celebrating(ms_);
    ScreenId screen = selectScreen(in);
    const StateCounts counts = countStates(snap_);
    const QuietPhase qp = quiet_.update(screen == ScreenId::Main && counts.pending == 0 && counts.running == 0, ms_);
    if (qp == QuietPhase::Desk) screen = ScreenId::Desk;
    if (qp == QuietPhase::Summary) screen = ScreenId::Summary;
    if (screen == ScreenId::Disconnected && current_ != ScreenId::Disconnected) awaySinceMs_ = ms_;
    const bool idleScreen = screen == ScreenId::Disconnected || screen == ScreenId::Desk ||
                            screen == ScreenId::Summary || (screen == ScreenId::Main && qp != QuietPhase::Busy);
    const uint32_t idleMs = !idleScreen ? 0 : screen == ScreenId::Disconnected ? ms_ - awaySinceMs_ : quiet_.quietMs(ms_);
    const bool away = screen == ScreenId::Disconnected;
    const bool ordinaryScreen = idleScreen || screen == ScreenId::Main || screen == ScreenId::LimitReset ||
                                screen == ScreenId::UpdateAvailable;
    DailyInputs di;
    di.fanfare = fanfare;
    di.cue = cue_.active(ms_);
    di.find = notes_.finding(ms_);
    di.held = notes_.held(ms_);
    di.focus = focus_.phase();
    di.timer = notes_.timerRunning();
    di.say = notes_.saying(ms_) != nullptr;
    const bool realActivity = !ordinaryScreen || (!away && (counts.running > 0 || counts.pending > 0));
    const bool activity = realActivity || dailyActivity(di);
    const uint8_t petMin = dayEnd_.petMinutes(cfg_, dayKey);
    const bool pet = petLatch_.update(activity, idleMs, ms_ - lastInteractionMs_, petMin, ms_);
    if (pet) screen = ScreenId::Roam;

    // Friends: the other Miblo roams now and then; packets both ways, some lost.
    const bool napping = pet && away && ms_ - awaySinceMs_ >= kAwayNapMs;
    friends_.setSelf("miblo-0001", "Tofu", 1);
    friends_.update(ms_, cfg_.friends && wifi_, (pet ? kFriendRoaming : 0) | (napping ? kFriendNapping : 0),
                    rng_.next());
    if (rng_.chance(50)) peerRoaming_ = !peerRoaming_;
    peer_.setSelf("miblo-0002", "Nina", 2);
    peer_.update(ms_, wifi_, peerRoaming_ ? kFriendRoaming : 0, rng_.next());
    exchange(friends_, peer_, 0x0A000002);
    exchange(peer_, friends_, 0x0A000001);
    if (friends_.count() > kMaxFriends) v.add("friends over the table size", simMs_);
    const VisitView visit = friends_.visit(ms_);
    if (screen == ScreenId::Roam && visit.role != VisitRole::None) screen = ScreenId::Visit;
    if (visit.role != VisitRole::None && visit.ms > kVisitMs + kMaxStepMs) v.add("a visit outlasted kVisitMs", simMs_);

    const bool plainScreen = screen == ScreenId::Main || screen == ScreenId::Desk || screen == ScreenId::Summary ||
                             screen == ScreenId::Disconnected;
    const bool quietDesk = plainScreen && alert.phase == AlertPhase::None && di.focus == FocusPhase::Off &&
                           !meeting_.on() && !di.timer && di.held == NoteKind::None && !di.say &&
                           di.cue == CueKind::None && !di.find;
    dayEnd_.update(ms_, cfg_, dayKey, weekday, minuteNow, counts.running > 0, quietDesk);
    weekly_.update(ms_, cfg_.weekly, dayKey, weekday, minuteNow, counts.running > 0, snap_.week.present, quietDesk);
    di.dayEnd = dayEnd_.showing(ms_);
    di.weekRecap = weekly_.showing(ms_);
    wellness_.update(ms_, cfg_, counts.running > 0, inWorkHours(cfg_, weekday, minuteNow),
                     quietDesk && !di.dayEnd && !di.weekRecap);
    di.nudge = wellness_.showing(ms_);
    di.screen = screen;
    screen = dailyScreen(di);
    const bool asleep = petLatch_.asleep(cfg_.sleepMin, petMin, ms_) && !di.say;
    const bool mark = waitingMarkOn(screen, counts.pending);

    // ---- invariants ----
    if (realActivity && !away && (pet || asleep)) v.add("pet mode or sleep stayed on with real activity", simMs_);
    if ((wasPet_ || wasAsleep_) && !pet && !asleep) st.wakes++;
    wasPet_ = pet;
    wasAsleep_ = asleep;
    checkStuck(screen);
    // Needs you: while the computer is connected and a session waits, it is on screen (the
    // alert, the main list or the waiting mark) at least once per alert cycle.
    const bool fresh = screen != ScreenId::Disconnected && screen != ScreenId::Boot && hasSnapshot_ &&
                       ms_ - lastSnapshotMs_ < kSnapshotTimeoutMs;
    if (fresh && counts.pending > 0) {
      if (!pendingFresh_) pendingSinceSim_ = lastShownSim_ = simMs_;
      pendingFresh_ = true;
      const bool shown = screen == ScreenId::AlertFlash || screen == ScreenId::AlertHero || screen == ScreenId::Main ||
                         mark;
      if (shown) lastShownSim_ = simMs_;
      // The longest anything else may cover it: a cue, a limit reset or update notice, the
      // fanfare or a "finished" hero before its turn (the alert flash then comes first).
      const uint64_t bound = 2 * (cfg_.heroDoneSec * 1000ull + cfg_.flashBlinks * kBlinkMs) + LimitWatch::kCelebrateMs +
                             kFanfareMs + kMaxStepMs;
      if (simMs_ - lastShownSim_ > bound) {
        v.add("a waiting session was hidden for too long", simMs_);
        lastShownSim_ = simMs_;
      }
    } else {
      pendingFresh_ = false;
    }
    if (screen == ScreenId::Nudge && current_ != ScreenId::Nudge) st.nudges++;
    if (screen == ScreenId::DayEnd && current_ != ScreenId::DayEnd) st.dayEnds++;
    if (screen == ScreenId::WeekRecap && current_ != ScreenId::WeekRecap) st.recaps++;
    if (screen == ScreenId::Visit && current_ != ScreenId::Visit) st.visits++;
    if (screen == ScreenId::AlertFlash && current_ != ScreenId::AlertFlash) st.alerts++;
    current_ = screen;
  }

  void exchange(FriendPlay& from, FriendPlay& to, uint32_t ip) {
    FriendPacket p;
    for (int i = 0; i < 8 && from.nextPacket(p); i++) {
      uint8_t buf[kFriendPacketMax];
      const size_t n = encodeFriendPacket(p, buf, sizeof(buf));
      if (!n) {
        v.add("a friend packet did not encode", simMs_);
        continue;
      }
      FriendPacket q;
      if (!decodeFriendPacket(buf, n, q)) {
        v.add("a friend packet did not decode", simMs_);
        continue;
      }
      if (!wifi_ || rng_.chance(50000)) continue;  // lost
      to.receive(q, ms_, ip);
    }
  }

  void save() {
    DynamicJsonDocument doc(kNotesDoc);  // storage.cpp kNotesJsonCapacity
    notes_.toJson(doc.to<JsonObject>());
    if (doc.overflowed()) v.add("notes.json overflowed its document", simMs_);
    savedNotes_.clear();
    serializeJson(doc, savedNotes_);
    if (savedNotes_.size() > 1024) v.add("notes.json over 1024 B (loadNotes would refuse it)", simMs_);
  }

  void checkFired(NoteKind k, uint32_t dayKey) {
    const std::string text = notes_.heldText(ms_);
    if (k == NoteKind::Reminder) {
      st.reminders++;
      auto it = remPending_.find(text);
      if (remFired_.count(text)) v.add("a reminder fired twice", simMs_);
      else if (it == remPending_.end()) v.add("an unknown or deleted reminder fired", simMs_);
      else if ((int32_t)(ms_ - it->second.dueMs) < (it->second.at ? -(int32_t)kAtSlackMs : 0))
        v.add("a reminder fired early", simMs_);
      if (it != remPending_.end()) remPending_.erase(it);
      remFired_.insert(text);
    } else if (k == NoteKind::Alarm) {
      st.alarms++;
      if (!alarmLive_.count(text)) v.add("a deleted or unknown alarm fired", simMs_);
      // Once per day: one minute a day at most, so two fires are ~a day apart (less an hour of
      // clock jump or DST, less the time it may wait behind other held texts).
      auto last = alarmLastFire_.find(text);
      if (last != alarmLastFire_.end() && simMs_ - last->second < 20ull * 3600000) v.add("an alarm fired twice in a day", simMs_);
      alarmLastFire_[text] = simMs_;
      fireTimes_[text].push_back(simMs_);
      alarmFired_.insert({text, dayKey});
    } else if (k == NoteKind::Timer) {
      st.timers++;
      if (!timerLive_) v.add("a stopped or finished timer fired", simMs_);
      else if (timerFiredGen_ == timerGen_) v.add("a timer fired twice", simMs_);
      else if ((int32_t)(ms_ - timerDueMs_) < 0) v.add("a timer fired early", simMs_);
      timerFiredGen_ = timerGen_;
      timerLive_ = false;
    }
  }

  // An alarm's minute passed on a matching day, with the clock steady around it: it must fire
  // that day (or, held behind other texts, within the next hour).
  void trackAlarmCoverage(bool timeKnown, uint32_t dayKey, int minute, uint8_t weekday) {
    const bool steady = timeKnown && prevMinute_ >= 0 && dayKey == prevDayKey_ && minute >= prevMinute_ &&
                        minute - prevMinute_ <= 1 && simMs_ - lastJumpSim_ > 120000;
    if (steady) {
      for (const std::string& t : alarmLive_) {
        const AlarmSpec& a = alarmSpec_[t];
        if ((a.days >> weekday & 1) && prevMinute_ < a.minute && minute >= a.minute && !alarmFired_.count({t, dayKey}))
          alarmExpected_.push_back({t, simMs_});
      }
    }
    prevMinute_ = timeKnown ? minute : -1;
    prevDayKey_ = dayKey;
  }

  // Each screen's longest continuous stay (plus one stalled frame).
  uint64_t stuckBound(ScreenId s) const {
    switch (s) {
      case ScreenId::Cue: return 3ull * kCuePulseMs;
      case ScreenId::Find: return kFindMs;
      // Different nudges may follow each other (a break, then water, then the eyes).
      case ScreenId::Nudge: return kBreakNudgeMs + kWaterNudgeMs + kEyesNudgeMs;
      case ScreenId::DayEnd: return EndOfDay::kShowMs;
      case ScreenId::WeekRecap: return WeeklyRecap::kShowMs;
      // A held text (5 min) after another, or the /miblo:say note (8 h).
      case ScreenId::Note: return kSayMaxMin * 60000ull + (kMaxReminders + kMaxAlarms + 1) * kHeldMs;
      case ScreenId::Timer: return kTimerMaxMin * 60000ull;
      case ScreenId::Focus: return 6ull * (60 + 15 + 1) * 60000 + 45 * 60000ull;  // the longest plan this sim sends
      case ScreenId::Fanfare: return kFanfareMs;
      case ScreenId::AlertFlash: return cfg_.flashBlinks * kBlinkMs;
      case ScreenId::AlertHero: return cfg_.heroPermSec * 1000ull;
      case ScreenId::Visit: return kVisitMs;
      case ScreenId::LimitReset: return LimitWatch::kCelebrateMs;
      default: return 0;  // not bounded
    }
  }

  void checkStuck(ScreenId screen) {
    if (screen != runScreen_) {
      runScreen_ = screen;
      runSinceSim_ = simMs_;
      return;
    }
    const uint64_t b = stuckBound(screen);
    // AlertHero/Flash back to back for different alerts look continuous: allow a queue's worth.
    const uint64_t slack = (screen == ScreenId::AlertHero || screen == ScreenId::AlertFlash ? kMaxAlerts : 1) * b;
    if (b && simMs_ - runSinceSim_ > slack + kMaxStepMs + 1000) {
      char what[96];
      snprintf(what, sizeof(what), "screen %d stuck for %.0f s", (int)screen, (simMs_ - runSinceSim_) / 1000.0);
      v.add(what, simMs_);
      runSinceSim_ = simMs_;
    }
  }

  // ---------------- the main loop ----------------
  void step() {
    // A frame every ~100 ms; most of the time we jump a second (nothing here needs finer steps
    // to hold its invariants), sometimes a stall of up to 10 s.
    uint32_t dt = rng_.chance(20000) ? 1000 + rng_.below(kMaxStepMs - 999) : rng_.chance(300000) ? 100 : 1000;
    simMs_ += dt;
    ms_ += dt;
    subMs_ += dt;
    utc_ += subMs_ / 1000;
    subMs_ %= 1000;

    // The world changes.
    if (simMs_ >= nextDstAt_) {  // DST in or out
      if (nextDstAt_) {
        tz_ += rng_.below(2) ? 3600 : -3600;
        lastJumpSim_ = simMs_;
      }
      nextDstAt_ = simMs_ + 86400000ull * (5 + rng_.below(25));
    }
    if (rng_.chance(5)) {  // the clock jumps (bad NTP answer, then corrected later)
      clockErr_ = clockErr_ ? 0 : (rng_.below(2) ? 3600 : -3600);
      lastJumpSim_ = simMs_;
    }
    if (!timeKnown_ && simMs_ >= ntpAt_ && wifi_) {
      timeKnown_ = true;
      lastJumpSim_ = simMs_;
    }
    if (wifi_ && rng_.chance(8)) {
      wifi_ = false;
      wifiBackAt_ = simMs_ + 5000 + rng_.below(15 * 60000);
    } else if (!wifi_ && simMs_ >= wifiBackAt_) {
      wifi_ = true;
    }
    if (reboots_ && rng_.chance(2)) {
      st.reboots++;
      boot(rng_.below(5000));
      return;
    }
    computerStep();
    if (present_ && wifi_ && simMs_ >= nextSnapAt_) {
      nextSnapAt_ = simMs_ + 1000 + rng_.below(2000);
      buildSnapshot();
      st.snapshots++;
      hasSnapshot_ = true;
      lastSnapshotMs_ = ms_;
      limits_.observe(snap_, ms_);
      alerts_.ingest(snap_, ms_);
      runs_.observe(snap_);
      if (!timeKnown_) {  // the computer's clock, before NTP
        timeKnown_ = true;
        clockErr_ = 0;
        lastJumpSim_ = simMs_;
      }
    }
    if (wifi_ && simMs_ >= nextCmdAt_) {
      nextCmdAt_ = simMs_ + 60000 + rng_.below(40 * 60000);
      if (present_) command();
    }
    frame();
    checkLost();
  }

  // A reminder never just disappears: once due (plus a queue of held texts), it fired.
  void checkLost() {
    if (st.frames % 64) return;
    for (auto it = remPending_.begin(); it != remPending_.end();) {
      const uint64_t slack = it->second.at ? kAtSlackMs : 0;
      if (simMs_ > it->second.dueSim + slack + (kMaxReminders + kMaxAlarms + 2) * (uint64_t)kHeldMs + 60000) {
        v.add(("reminder " + it->first + " never fired").c_str(), simMs_);
        it = remPending_.erase(it);
      } else {
        ++it;
      }
    }
  }

  void finish() {
    // Alarm coverage: every steady pass of an alarm's minute fired it within the hour after
    // (it may wait behind other held texts), unless it was deleted, the device rebooted or the
    // run ended in between.
    for (const auto& e : alarmExpected_) {
      if (e.second + 3600000 > simMs_) continue;
      auto del = alarmDeleted_.find(e.first);
      if (del != alarmDeleted_.end() && del->second >= e.second && del->second - e.second < 3600000) continue;
      bool rebooted = false;
      for (uint64_t b : bootTimes_) rebooted = rebooted || (b >= e.second && b - e.second < 3600000);
      if (rebooted) continue;
      st.alarmDays++;
      auto f = fireTimes_.find(e.first);
      bool ok = false;
      if (f != fireTimes_.end()) {
        for (uint64_t t : f->second) ok = ok || (t + 1000 >= e.second && t - e.second < 3600000);
      }
      if (!ok) st.alarmMissedDays++;
    }
  }
  std::vector<uint64_t> bootTimes_;
  std::map<std::string, std::vector<uint64_t>> fireTimes_;
};

uint32_t envU32(const char* name, uint32_t def) {
  const char* s = getenv(name);
  return s && *s ? (uint32_t)strtoul(s, nullptr, 10) : def;
}

void runSeeds(uint32_t days, uint32_t seeds) {
  uint32_t failures = 0;
  std::string first;
  for (uint32_t seed = 1; seed <= seeds; seed++) {
    Sim sim(seed, seed % 2 == 0);
    sim.run(days);
    const Stats& s = sim.st;
    printf("seed %u (%s): %u days, %llu frames, %llu snapshots, %llu commands, %llu alerts, %llu reminders, "
           "%llu alarms (%llu/%llu alarm days missed), %llu timers, %llu focus ends, %llu nudges, %llu day ends, "
           "%llu recaps, %llu visits, %llu wakes, %llu reboots: %u violations\n",
           (unsigned)seed, seed % 2 == 0 ? "reboots" : "millis wraps", (unsigned)days,
           (unsigned long long)s.frames, (unsigned long long)s.snapshots, (unsigned long long)s.commands,
           (unsigned long long)s.alerts, (unsigned long long)s.reminders, (unsigned long long)s.alarms,
           (unsigned long long)s.alarmMissedDays, (unsigned long long)s.alarmDays, (unsigned long long)s.timers,
           (unsigned long long)s.focusEnds, (unsigned long long)s.nudges, (unsigned long long)s.dayEnds,
           (unsigned long long)s.recaps, (unsigned long long)s.visits, (unsigned long long)s.wakes,
           (unsigned long long)s.reboots, (unsigned)sim.v.n);
    if (s.alarmMissedDays) sim.v.add("an alarm missed its day with the clock steady", 0);
    if (sim.v.n) {
      printf("  %s\n", sim.v.first.c_str());
      failures++;
      if (first.empty()) first = "seed " + std::to_string(seed) + ": " + sim.v.first;
    }
  }
  TEST_ASSERT_EQUAL_MESSAGE(0, failures, first.c_str());
}

}  // namespace

static void test_daily_life_holds_its_invariants_over_days() {
  runSeeds(envU32("MIBLO_LONGRUN_DAYS", 3), envU32("MIBLO_LONGRUN_SEEDS", 4));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_daily_life_holds_its_invariants_over_days);
  return UNITY_END();
}
