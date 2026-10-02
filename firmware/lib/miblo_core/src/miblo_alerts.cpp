#include "miblo_alerts.h"

#include <string.h>

#include "miblo_overview.h"

namespace miblo {

static bool isAmber(AlertKind k) { return k == AlertKind::Perm || k == AlertKind::Question; }

static bool isWaiting(SessionState st) { return st == SessionState::Perm || st == SessionState::Question; }

// A session's key in the shown-waits table: the bridge's short ids are 8 hex digits, read as a
// number (exact); anything else (tests, a future id format) gets a 32-bit FNV-1a hash.
static uint32_t sessionKey(const char* sid) {
  uint32_t v = 0;
  int n = 0;
  for (; sid[n] && n < 9; n++) {
    const char c = sid[n];
    const int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
    if (d < 0) break;
    v = (v << 4) | (uint32_t)d;
  }
  if (n == 8 && sid[8] == 0) return v;
  uint32_t h = 2166136261u;
  for (const char* p = sid; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
  return h;
}

static uint8_t kindRank(AlertKind k) {
  switch (k) {
    case AlertKind::Perm: return 0;
    case AlertKind::Question: return 1;
    case AlertKind::Done: return 2;
  }
  return 2;
}

void AlertSequencer::setTiming(const AlertTiming& t) {
  t_ = t;
  if (!t_.enabled) clear();
}

// Read on every start (flash/hero lengths, level) and every dequeue (holdDone).
void AlertSequencer::setModifiers(const AlertModifiers& m) { mods_ = m; }

void AlertSequencer::extendHero(uint32_t ms) {
  if (view_.phase == AlertPhase::Hero && ms > heroLenMs_) heroLenMs_ = ms;
}

void AlertSequencer::clear() {
  qn_ = 0;
  view_.phase = AlertPhase::None;
}

bool AlertSequencer::stillValid(const Snapshot& s, AlertKind kind, const char* sid) {
  int i = findSession(s, sid);
  if (i < 0) return false;
  SessionState st = s.sessions[i].st;
  switch (kind) {
    case AlertKind::Perm: return st == SessionState::Perm;
    case AlertKind::Question: return st == SessionState::Question;
    case AlertKind::Done: return st == SessionState::Done;
  }
  return false;
}

void AlertSequencer::sortQueue(const Snapshot& s) {
  auto since = [&](const AlertItem& a) -> uint32_t {
    int i = findSession(s, a.sid);
    return i < 0 ? UINT32_MAX : s.sessions[i].since;
  };
  // insertion sort: queue is small (<= 8)
  for (int i = 1; i < qn_; i++) {
    AlertItem cur = queue_[i];
    int j = i - 1;
    while (j >= 0) {
      const AlertItem& p = queue_[j];
      bool after = kindRank(p.kind) > kindRank(cur.kind) ||
                   (kindRank(p.kind) == kindRank(cur.kind) &&
                    (since(p) > since(cur) || (since(p) == since(cur) && p.id > cur.id)));
      if (!after) break;
      queue_[j + 1] = queue_[j];
      j--;
    }
    queue_[j + 1] = cur;
  }
}

void AlertSequencer::ingest(const Snapshot& s, uint32_t nowMs) {
  (void)nowMs;
  if (haveSeq_ && s.seq < lastSeq_) maxId_ = 0;  // the bridge restarted: ids start over from 1
  haveSeq_ = true;
  lastSeq_ = s.seq;
  for (int i = 0; i < s.alertCount; i++) {
    const AlertItem& a = s.alerts[i];
    if (a.id <= maxId_) continue;
    maxId_ = a.id;
    // "Needs you" comes from the session list (update()), never from these records: a record
    // can be cut, expire or arrive late, the session list cannot.
    if (!t_.enabled || isAmber(a.kind)) continue;
    // One queued "finished" per session: a newer one replaces an older one still held for that
    // session (it would show twice at the break). Items whose session moved on make room.
    for (uint8_t j = 0; j < qn_;) {
      const AlertItem& o = queue_[j];
      if (strcmp(o.sid, a.sid) == 0 || !stillValid(s, o.kind, o.sid)) {
        removeAt(j);
      } else {
        j++;
      }
    }
    if (qn_ >= kMaxAlerts) continue;  // 8 "finished" already held: the overview shows the rest
    queue_[qn_++] = a;
  }
  sortQueue(s);
}

bool AlertSequencer::shown(uint32_t key, uint32_t since) const {
  for (uint8_t j = 0; j < shownN_; j++) {
    if (shownKey_[j] == key && shownSince_[j] == since) return true;
  }
  return false;
}

// Drops the shown waits whose session is in `s` but no longer in that wait (answered, or waiting
// again since another time). A session absent from `s` keeps its entry: an alerts-only snapshot
// or another paired computer's snapshot does not end a wait.
void AlertSequencer::forgetEndedWaits(const Snapshot& s, const uint32_t* keys) {
  for (uint8_t j = 0; j < shownN_;) {
    bool ended = false;
    for (int i = 0; i < s.count; i++) {
      if (keys[i] != shownKey_[j]) continue;
      ended = !isWaiting(s.sessions[i].st) || s.sessions[i].since != shownSince_[j];
      break;
    }
    if (ended) {
      shownN_--;
      shownKey_[j] = shownKey_[shownN_];
      shownSince_[j] = shownSince_[shownN_];
    } else {
      j++;
    }
  }
}

void AlertSequencer::markShown(uint32_t key, uint32_t since, const Snapshot& s, const uint32_t* keys) {
  if (shown(key, since)) return;
  if (shownN_ >= kMaxSessions) {
    // Full: every entry whose session is in `s` is one of its waits (forgetEndedWaits ran), and
    // `s` has at most kMaxSessions sessions, one of them this new wait: at least one entry
    // belongs to a session not in `s`. The oldest such wait gives up its place.
    int victim = -1;
    for (uint8_t j = 0; j < shownN_; j++) {
      bool present = false;
      for (int i = 0; i < s.count && !present; i++) present = keys[i] == shownKey_[j];
      if (!present && (victim < 0 || shownSince_[j] < shownSince_[victim])) victim = j;
    }
    if (victim < 0) victim = 0;  // unreachable (see above); never write past the table
    shownN_--;
    shownKey_[victim] = shownKey_[shownN_];
    shownSince_[victim] = shownSince_[shownN_];
  }
  shownKey_[shownN_] = key;
  shownSince_[shownN_] = since;
  shownN_++;
}

// The wait to show next: not shown yet, the oldest first (Perm before Question within the same
// second); -1 = none. Oldest first, not every Perm first: with many sessions a stream of new
// permissions must not keep an older question off the screen.
int AlertSequencer::nextUnshownWait(const Snapshot& s, const uint32_t* keys) const {
  int best = -1;
  for (int i = 0; i < s.count; i++) {
    const SessionRow& r = s.sessions[i];
    if (!isWaiting(r.st) || shown(keys[i], r.since)) continue;
    if (best < 0) {
      best = i;
      continue;
    }
    const SessionRow& b = s.sessions[best];
    if (r.since != b.since ? r.since < b.since
                           : r.st != b.st ? r.st == SessionState::Perm : keys[i] < keys[best]) {
      best = i;
    }
  }
  return best;
}

// The alert on screen goes on? "Finished": while its session is in `s` and done. "Needs you":
// unless its session is in `s` and out of that wait (answered, or waiting since another time:
// forgetEndedWaits already dropped the wait from the table). A session missing from `s` (another
// paired computer's snapshot, an alerts-only one) does not cut it; the screen then draws it
// anonymously.
bool AlertSequencer::onScreenStillValid(const Snapshot& s, const uint32_t* keys) const {
  if (!isAmber(view_.kind)) return stillValid(s, view_.kind, view_.sid);
  const int i = findSession(s, view_.sid);
  return i < 0 || (isWaiting(s.sessions[i].st) && shown(keys[i], s.sessions[i].since));
}

void AlertSequencer::removeAt(uint8_t i) {
  for (uint8_t j = i + 1; j < qn_; j++) queue_[j - 1] = queue_[j];
  qn_--;
}

void AlertSequencer::start(AlertKind kind, const char* sid, uint32_t nowMs, uint8_t level) {
  view_.phase = AlertPhase::Flash;
  view_.kind = kind;
  view_.level = level;
  const uint32_t times = level ? 2 : 1;
  // Meeting mode: one blink, whatever the level (a level-2 blink is still red on screen).
  flashLenMs_ = mods_.quietFlash ? kBlinkMs : times * t_.flashMs;
  heroLenMs_ = times * (isAmber(kind) ? t_.heroPermMs : t_.heroDoneMs);
  strncpy(view_.sid, sid, sizeof(view_.sid) - 1);
  view_.sid[sizeof(view_.sid) - 1] = 0;
  view_.phaseStartMs = nowMs;
}

// Insistence step for a "needs you" alert starting now (spec 3).
uint8_t AlertSequencer::levelNow() const {
  if (!mods_.insist) return 0;
  return reminders_ >= kInsistLevel2From ? 2 : reminders_ >= kInsistLevel1From ? 1 : 0;
}

void AlertSequencer::finish(uint32_t nowMs) {
  if (isAmber(view_.kind)) {
    amberShown_ = true;
    lastAmberEndMs_ = nowMs;
  }
  view_.phase = AlertPhase::None;
}

const AlertView& AlertSequencer::update(const Snapshot& s, uint32_t nowMs) {
  bool pending = countStates(s).pending > 0;
  if (pending && !pendingObserved_) {
    pendingObserved_ = true;
    pendingSinceMs_ = nowMs;
  } else if (!pending) {
    pendingObserved_ = false;
  }
  if (remindSid_[0]) {  // the session being reminded stopped waiting: insistence starts over
    const int i = findSession(s, remindSid_);
    if (i < 0 || (s.sessions[i].st != SessionState::Perm && s.sessions[i].st != SessionState::Question)) {
      reminders_ = 0;
      remindSid_[0] = 0;
    }
  }
  uint32_t keys[kMaxSessions];
  for (int i = 0; i < s.count; i++) keys[i] = sessionKey(s.sessions[i].id);
  forgetEndedWaits(s, keys);
  if (!t_.enabled) {
    // Alerts off: the waits going on now count as dealt with; turning alerts on later does not
    // replay them.
    for (int i = 0; i < s.count; i++) {
      if (isWaiting(s.sessions[i].st)) markShown(keys[i], s.sessions[i].since, s, keys);
    }
    view_.phase = AlertPhase::None;
    return view_;
  }

  if (view_.phase != AlertPhase::None) {
    uint32_t elapsed = nowMs - view_.phaseStartMs;
    if (!onScreenStillValid(s, keys)) {
      view_.phase = AlertPhase::None;  // answered/dismissed by usage
    } else if (view_.phase == AlertPhase::Flash && elapsed >= flashLenMs_) {
      view_.phase = AlertPhase::Hero;
      view_.phaseStartMs = nowMs;
    } else if (view_.phase == AlertPhase::Hero && elapsed >= heroLenMs_) {
      finish(nowMs);
    }
  }

  if (view_.phase == AlertPhase::None) {
    // A wait not shown yet goes first, straight from the session list (never held by focus).
    const int w = nextUnshownWait(s, keys);
    if (w >= 0) {
      const SessionRow& r = s.sessions[w];
      markShown(keys[w], r.since, s, keys);
      start(r.st == SessionState::Perm ? AlertKind::Perm : AlertKind::Question, r.id, nowMs, 0);
      return view_;  // a new alert: plain, whatever came before
    }
    // Stale items are dropped; a valid "finished" stays queued while holdDone (focus round),
    // and anything behind it (amber sorts first anyway) may still go.
    uint8_t i = 0;
    while (i < qn_) {
      const AlertItem next = queue_[i];
      const bool valid = stillValid(s, next.kind, next.sid);
      if (valid && next.kind == AlertKind::Done && mods_.holdDone) {
        i++;
        continue;
      }
      removeAt(i);
      if (valid) {
        start(next.kind, next.sid, nowMs, 0);  // a new alert: plain, whatever came before
        return view_;
      }
    }
    if (t_.reminderMs > 0 && pendingObserved_) {
      uint32_t base = pendingSinceMs_;
      if (amberShown_ && (int32_t)(lastAmberEndMs_ - base) > 0) base = lastAmberEndMs_;
      if (nowMs - base >= t_.reminderMs) {
        int h = selectHero(s, false);
        if (h >= 0) {
          const SessionRow& r = s.sessions[h];
          if (strcmp(remindSid_, r.id) != 0) {  // reminding another session: its own count
            reminders_ = 0;
            strncpy(remindSid_, r.id, sizeof(remindSid_) - 1);
            remindSid_[sizeof(remindSid_) - 1] = 0;
          }
          if (reminders_ < 255) reminders_++;
          start(r.st == SessionState::Perm ? AlertKind::Perm : AlertKind::Question, r.id, nowMs, levelNow());
        }
      }
    }
  }
  return view_;
}

}  // namespace miblo
