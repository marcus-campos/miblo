#include "miblo_overview.h"

#include <string.h>

namespace miblo {

uint8_t stateRank(SessionState st) {
  switch (st) {
    case SessionState::Perm: return 0;
    case SessionState::Question: return 1;
    case SessionState::Done: return 2;
    case SessionState::Running: return 3;
    case SessionState::Idle: return 4;
  }
  return 4;
}

StateCounts countStates(const Snapshot& s) {
  StateCounts c{0, 0, 0, 0};
  for (int i = 0; i < s.count; i++) {
    switch (s.sessions[i].st) {
      case SessionState::Perm:
      case SessionState::Question: c.pending++; break;
      case SessionState::Running: c.running++; break;
      case SessionState::Done: c.done++; break;
      case SessionState::Idle: c.idle++; break;
    }
  }
  return c;
}

OverviewKind classifyOverview(const Snapshot& s) {
  StateCounts c = countStates(s);
  if (c.pending > 0) return OverviewKind::Attention;
  if (c.running > 0) return OverviewKind::Working;
  return OverviewKind::Idle;
}

int selectHero(const Snapshot& s, bool includeDone) {
  int best = -1;
  for (int i = 0; i < s.count; i++) {
    const SessionRow& r = s.sessions[i];
    uint8_t rank = stateRank(r.st);
    if (rank > (includeDone ? 2 : 1)) continue;
    if (best < 0) {
      best = i;
      continue;
    }
    const SessionRow& b = s.sessions[best];
    uint8_t bestRank = stateRank(b.st);
    if (rank < bestRank || (rank == bestRank && r.since < b.since)) best = i;
  }
  return best;
}

int lastFinished(const Snapshot& s) {
  int best = -1;
  for (int i = 0; i < s.count; i++) {
    if (s.sessions[i].st != SessionState::Done) continue;
    if (best < 0 || s.sessions[i].since > s.sessions[best].since) best = i;
  }
  return best;
}

RunTracker::Entry* RunTracker::find(const char* id) {
  for (auto& e : entries_) {
    if (e.used && strcmp(e.id, id) == 0) return &e;
  }
  return nullptr;
}

const RunTracker::Entry* RunTracker::find(const char* id) const {
  for (const auto& e : entries_) {
    if (e.used && strcmp(e.id, id) == 0) return &e;
  }
  return nullptr;
}

void RunTracker::clear() {
  for (auto& e : entries_) e = Entry{};
}

void RunTracker::observe(const Snapshot& s) {
  // forget sessions that dropped out of the snapshot
  for (auto& e : entries_) {
    if (e.used && findSession(s, e.id) < 0) e = Entry{};
  }
  for (int i = 0; i < s.count; i++) {
    const SessionRow& r = s.sessions[i];
    Entry* e = find(r.id);
    if (!e) {
      for (auto& slot : entries_) {
        if (!slot.used) {
          slot = Entry{};
          slot.used = true;
          strncpy(slot.id, r.id, sizeof(slot.id) - 1);
          e = &slot;
          break;
        }
      }
      if (!e) continue;
    }
    switch (r.st) {
      case SessionState::Running:
      case SessionState::Perm:
      case SessionState::Question:
        if (!e->active) {
          e->active = true;
          e->finished = false;
          e->start = r.since;
        }
        break;
      case SessionState::Done:
        if (e->active) {
          e->active = false;
          e->finished = true;
          e->duration = r.since >= e->start ? r.since - e->start : 0;
        }
        break;
      case SessionState::Idle:
        e->active = false;
        e->finished = false;
        break;
    }
  }
}

bool RunTracker::stats(const char* sid, uint32_t& durationSec) const {
  const Entry* e = find(sid);
  if (!e || !e->finished) return false;
  durationSec = e->duration;
  return true;
}

uint8_t Pager::pageCount(uint16_t itemCount) const {
  if (perPage_ == 0 || itemCount == 0) return 1;
  return (uint8_t)((itemCount + perPage_ - 1) / perPage_);
}

uint8_t Pager::update(uint16_t itemCount, uint32_t nowMs) {
  uint8_t pages = pageCount(itemCount);
  if (!started_) {
    started_ = true;
    lastFlipMs_ = nowMs;
  }
  if (pages <= 1) {
    page_ = 0;
    lastFlipMs_ = nowMs;
    return 0;
  }
  if (page_ >= pages) page_ = 0;
  if (nowMs - lastFlipMs_ >= periodMs_) {
    page_ = (uint8_t)((page_ + 1) % pages);
    lastFlipMs_ = nowMs;
  }
  return page_;
}

uint32_t hashStr(uint32_t h, const char* s) {
  if (!s) return hashInt(h, 0);
  while (*s) {
    h ^= (uint8_t)*s++;
    h *= 16777619u;
  }
  h ^= 0xFF;  // separator, so "ab"+"c" != "a"+"bc"
  h *= 16777619u;
  return h;
}

uint32_t hashInt(uint32_t h, uint32_t v) {
  for (int i = 0; i < 4; i++) {
    h ^= (v >> (i * 8)) & 0xFF;
    h *= 16777619u;
  }
  return h;
}

bool RegionCache::changed(uint8_t region, uint32_t hash) {
  if (region >= kRegions) return true;
  if (valid_[region] && hash_[region] == hash) return false;
  valid_[region] = true;
  hash_[region] = hash;
  return true;
}

void RegionCache::invalidate() {
  for (auto& v : valid_) v = false;
}

}  // namespace miblo
