#include "miblo_alerts.h"

#include <string.h>

#include "miblo_overview.h"

namespace miblo {

static bool isAmber(AlertKind k) { return k == AlertKind::Perm || k == AlertKind::Question; }

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
    if (!t_.enabled || qn_ >= kMaxAlerts) continue;
    queue_[qn_++] = a;
  }
  sortQueue(s);
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
    reminders_ = 0;  // nobody waits any more: insistence starts over
  }
  if (!t_.enabled) {
    view_.phase = AlertPhase::None;
    return view_;
  }

  if (view_.phase != AlertPhase::None) {
    uint32_t elapsed = nowMs - view_.phaseStartMs;
    if (!stillValid(s, view_.kind, view_.sid)) {
      view_.phase = AlertPhase::None;  // answered/dismissed by usage
    } else if (view_.phase == AlertPhase::Flash && elapsed >= flashLenMs_) {
      view_.phase = AlertPhase::Hero;
      view_.phaseStartMs = nowMs;
    } else if (view_.phase == AlertPhase::Hero && elapsed >= heroLenMs_) {
      finish(nowMs);
    }
  }

  if (view_.phase == AlertPhase::None) {
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
      for (uint8_t j = i + 1; j < qn_; j++) queue_[j - 1] = queue_[j];
      qn_--;
      if (valid) {
        start(next.kind, next.sid, nowMs, isAmber(next.kind) ? levelNow() : 0);
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
          if (reminders_ < 255) reminders_++;
          start(r.st == SessionState::Perm ? AlertKind::Perm : AlertKind::Question, r.id, nowMs, levelNow());
        }
      }
    }
  }
  return view_;
}

}  // namespace miblo
