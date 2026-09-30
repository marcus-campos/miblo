#include "miblo_friends.h"

#include <string.h>

#include "miblo_utf8.h"

namespace miblo {

static const uint8_t kMagic[4] = {'M', 'B', 'L', 'O'};
constexpr uint8_t kVersion = 1;
constexpr uint8_t kMascotColours = 4;

static bool putStr(uint8_t* out, size_t cap, size_t& n, const char* s) {
  const size_t len = strlen(s);
  if (len > 255 || n + 1 + len > cap) return false;
  out[n++] = (uint8_t)len;
  memcpy(out + n, s, len);
  n += len;
  return true;
}

size_t encodeFriendPacket(const FriendPacket& p, uint8_t* out, size_t cap) {
  if (cap < 8) return 0;
  size_t n = 0;
  memcpy(out, kMagic, 4);
  n = 4;
  out[n++] = kVersion;
  out[n++] = (uint8_t)p.type;
  out[n++] = p.mascot;
  out[n++] = p.flags;
  if (n >= cap) return 0;
  out[n++] = (uint8_t)p.gift;
  if (!putStr(out, cap, n, p.id) || !putStr(out, cap, n, p.name) || !putStr(out, cap, n, p.to)) return 0;
  return n;
}

// An id: 1..15 of [a-z0-9-] ("miblo-4f2a"); `optional` allows empty (the "to" of a beacon).
static bool validId(const char* s, bool optional) {
  const size_t len = strlen(s);
  if (len == 0) return optional;
  for (const char* c = s; *c; c++) {
    if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '-')) return false;
  }
  return true;
}

static bool validName(const char* s) {
  if (utf8Length(s) > 20) return false;
  for (const char* p = s; *p; p++) {
    if ((uint8_t)*p < 0x20 || *p == 0x7F) return false;
  }
  const char* p = s;
  for (uint32_t cp; (cp = utf8Next(p)) != 0;) {
    if (cp == 0xFFFD) return false;  // not UTF-8
  }
  return true;
}

static bool getStr(const uint8_t* in, size_t len, size_t& n, char* dst, size_t cap) {
  if (n >= len) return false;
  const size_t l = in[n++];
  if (l >= cap || n + l > len) return false;
  memcpy(dst, in + n, l);
  dst[l] = 0;
  if (memchr(dst, 0, l)) return false;  // an embedded NUL
  n += l;
  return true;
}

bool decodeFriendPacket(const uint8_t* in, size_t len, FriendPacket& out) {
  if (!in || len < 9 || len > kFriendPacketMax * 2 || memcmp(in, kMagic, 4) != 0 || in[4] != kVersion) return false;
  FriendPacket p;
  if (in[5] < FriendPacket::Beacon || in[5] > FriendPacket::Home) return false;
  p.type = (FriendPacket::Type)in[5];
  p.mascot = in[6] < kMascotColours ? in[6] : 0;
  p.flags = in[7] & (kFriendRoaming | kFriendNapping | kFriendTired | kFriendBusy);
  p.gift = in[8] < (uint8_t)Gift::Count ? (Gift)in[8] : Gift::None;  // a newer activity: a plain visit
  size_t n = 9;
  if (!getStr(in, len, n, p.id, sizeof(p.id)) || !getStr(in, len, n, p.name, sizeof(p.name)) ||
      !getStr(in, len, n, p.to, sizeof(p.to))) {
    return false;
  }
  if (!validId(p.id, false) || !validId(p.to, true) || !validName(p.name)) return false;
  if (p.type != FriendPacket::Beacon && !p.to[0]) return false;
  out = p;  // anything after the known fields is ignored (a future version may add some)
  return true;
}

// ---- FriendPlay ----

void FriendPlay::setSelf(const char* id, const char* name, uint8_t mascot) {
  if (strcmp(id, id_) != 0 || strcmp(name, name_) != 0 || mascot != mascot_) {
    strncpy(id_, id, sizeof(id_) - 1);
    utf8Copy(name_, sizeof(name_), name, 20);
    mascot_ = mascot;
    announce_ = true;
  }
}

FriendPlay::Friend* FriendPlay::find(const char* id) {
  for (Friend& f : friends_) {
    if (f.used && strcmp(f.id, id) == 0) return &f;
  }
  return nullptr;
}

FriendPlay::Friend* FriendPlay::remember(const FriendPacket& p, uint32_t nowMs) {
  Friend* f = find(p.id);
  if (!f) {
    Friend* oldest = nullptr;
    for (Friend& c : friends_) {
      if (!c.used) {
        f = &c;
        break;
      }
      if (!oldest || nowMs - c.seenMs > nowMs - oldest->seenMs) oldest = &c;
    }
    if (!f) f = oldest;  // table full: the one heard from longest ago makes room
    *f = Friend();
    f->used = true;
    strcpy(f->id, p.id);
  }
  strcpy(f->name, p.name[0] ? p.name : p.id);
  f->mascot = p.mascot;
  f->flags = p.flags;
  f->seenMs = nowMs;
  return f;
}

void FriendPlay::queue(FriendPacket::Type type, const char* to, Gift gift) {
  if (outN_ >= sizeof(out_) / sizeof(out_[0])) return;
  FriendPacket& p = out_[outN_++];
  p = FriendPacket();
  p.type = type;
  strcpy(p.id, id_);
  strcpy(p.name, name_);
  p.mascot = mascot_;
  p.flags = (uint8_t)(flags_ | (visit_.role != VisitRole::None ? kFriendBusy : 0));
  if (to) strncpy(p.to, to, sizeof(p.to) - 1);
  p.gift = gift;
}

bool FriendPlay::nextPacket(FriendPacket& out) {
  if (!outN_) return false;
  out = out_[0];
  for (uint8_t i = 1; i < outN_; i++) out_[i - 1] = out_[i];
  outN_--;
  return true;
}

void FriendPlay::demo(uint32_t nowMs, uint32_t untilMs) {
  demo_ = (int32_t)(untilMs - nowMs) > 0;
  demoUntilMs_ = untilMs;
  if (!demo_) return;
  for (Friend& f : friends_) f.greeted = false;
  if (visit_.role == VisitRole::None) {
    asking_ = false;  // a request still waiting would push the demo's first visit back
    scheduleVisit(nowMs, kDemoFirstVisitMs, 4000);
  }
}

bool FriendPlay::demoOn(uint32_t nowMs) {
  if (demo_ && (int32_t)(demoUntilMs_ - nowMs) <= 0) demo_ = false;
  return demo_;
}

void FriendPlay::firstVisit(uint32_t nowMs) {
  if (demoOn(nowMs)) scheduleVisit(nowMs, kDemoNextVisitMs, kDemoNextVisitMs);
  else scheduleVisit(nowMs, kFirstVisitMinMs, kFirstVisitSpanMs);
}

void FriendPlay::nextVisit(uint32_t nowMs, bool hosted) {
  const bool demo = demoOn(nowMs);
  const uint32_t min = demo ? kDemoNextVisitMs : kNextVisitMinMs;
  const uint32_t span = demo ? kDemoNextVisitMs : kNextVisitSpanMs;
  scheduleVisit(nowMs, hosted ? min : min + span / 2, span / 2);
}

// Coffee for a tired friend half of the time; otherwise any activity, a plain visit included.
Gift FriendPlay::chooseGift(bool friendTired, uint32_t rnd) const {
  if (friendTired && (rnd & 1)) return Gift::Coffee;
  return (Gift)((rnd >> 1) % (uint32_t)Gift::Count);
}

void FriendPlay::scheduleVisit(uint32_t nowMs, uint32_t minMs, uint32_t spanMs) {
  scheduled_ = true;
  nextVisitMs_ = nowMs + minMs + (spanMs ? rnd_ % spanMs : 0);
}

void FriendPlay::startVisit(VisitRole role, Friend& f, Gift gift, uint32_t nowMs) {
  visit_ = VisitView();
  visit_.role = role;
  strcpy(visit_.name, f.name);
  visit_.mascot = f.mascot;
  visit_.gift = gift;
  visitMs_ = nowMs;
  strcpy(visitWith_, f.id);
  asking_ = false;
  greetOn_ = false;
  announce_ = true;  // busy now
}

void FriendPlay::endVisit(uint32_t nowMs) {
  const bool hosted = visit_.role == VisitRole::Host;
  if (Friend* f = find(visitWith_)) f->lastRole = hosted ? 2 : 1;
  visit_ = VisitView();
  visitWith_[0] = 0;
  announce_ = true;
  nextVisit(nowMs, hosted);  // the host goes out next: they take turns
}

void FriendPlay::update(uint32_t nowMs, bool enabled, uint8_t flags, uint32_t rnd) {
  rnd_ = rnd;
  if (!enabled) {
    if (enabled_) *this = FriendPlay();  // forget everyone (keeps nothing, sends nothing)
    return;
  }
  if (!enabled_) announce_ = true;
  enabled_ = true;
  const bool roaming = flags & kFriendRoaming;
  const bool wasRoaming = flags_ & kFriendRoaming;
  if (flags != flags_) announce_ = true;
  flags_ = flags;

  for (Friend& f : friends_) {
    if (f.used && nowMs - f.seenMs > kFriendTtlMs) f = Friend();
  }
  // Beacons: on a schedule, and at once (but not too often) when something changed.
  const uint32_t sinceBeacon = nowMs - beaconMs_;
  if (!beaconed_ || sinceBeacon >= kBeaconEveryMs || (announce_ && sinceBeacon >= kBeaconMinGapMs)) {
    queue(FriendPacket::Beacon, nullptr, Gift::None);
    beaconed_ = true;
    beaconMs_ = nowMs;
    announce_ = false;
  }

  // A visit in progress: over when its timeline ends, or when our side leaves pet mode (a
  // visitor tells its host, so the guest does not linger there).
  if (visit_.role != VisitRole::None) {
    if (nowMs - visitMs_ >= kVisitMs) {
      endVisit(nowMs);
    } else if (!roaming) {
      if (visit_.role == VisitRole::Visitor) queue(FriendPacket::Home, visitWith_, Gift::None);
      endVisit(nowMs);
    }
    return;
  }
  if (asking_ && nowMs - askMs_ >= kVisitAskMs) {
    asking_ = false;
    nextVisit(nowMs, true);
  }
  if (!roaming) {
    scheduled_ = false;
    greetOn_ = false;
    return;
  }
  if (!wasRoaming || !scheduled_) firstVisit(nowMs);

  // Say hi to a friend that is in pet mode too (once in a while each).
  if (!greetOn_ || nowMs - greetMs_ >= kGreetShowMs) {
    greetOn_ = false;
    for (Friend& f : friends_) {
      if (!f.used || (flags & kFriendNapping) || (f.flags & (kFriendRoaming | kFriendNapping)) != kFriendRoaming || (f.greeted && nowMs - f.greetedMs < kGreetEveryMs)) continue;
      f.greeted = true;
      f.greetedMs = nowMs;
      strcpy(greetName_, f.name);
      greetMs_ = nowMs;
      greetOn_ = true;
      break;
    }
  }

  // Time to go visiting: an awake friend in pet mode, a tired one first (it gets a coffee).
  if (!asking_ && !(flags & kFriendNapping) && (int32_t)(nowMs - nextVisitMs_) >= 0) {
    // Any friend in pet mode, awake and free, at random (with ten Miblos the table keeps the four
    // heard most recently, so over time every one of them gets visited).
    Friend* eligible[kMaxFriends];
    uint8_t n = 0;
    for (Friend& f : friends_) {
      if (f.used && (f.flags & (kFriendRoaming | kFriendNapping | kFriendBusy)) == kFriendRoaming) eligible[n++] = &f;
    }
    Friend* pick = n ? eligible[rnd % n] : nullptr;
    if (pick) {
      askGift_ = chooseGift(pick->flags & kFriendTired, rnd / kMaxFriends);
      strcpy(askTo_, pick->id);
      asking_ = true;
      askMs_ = nowMs;
      queue(FriendPacket::VisitAsk, askTo_, askGift_);
    } else {
      firstVisit(nowMs);  // nobody to visit: look again later
    }
  }
}

void FriendPlay::receive(const FriendPacket& p, uint32_t nowMs) {
  if (!enabled_ || !id_[0] || strcmp(p.id, id_) == 0) return;  // off, or our own broadcast
  Friend* f = remember(p, nowMs);
  if (p.type == FriendPacket::Beacon || strcmp(p.to, id_) != 0) return;
  switch (p.type) {
    case FriendPacket::VisitAsk: {
      const bool free = (flags_ & (kFriendRoaming | kFriendNapping)) == kFriendRoaming && visit_.role == VisitRole::None;
      if (!free) return;
      if (asking_) {
        // Both asked each other at once: whoever hosted last time goes visiting now (the other
        // side remembers the same visit, so both agree); the first time, the lower id.
        if (strcmp(askTo_, p.id) == 0) {
          const bool iVisit = f->lastRole == 2 || (f->lastRole == 0 && strcmp(id_, p.id) < 0);
          if (iVisit) return;
        }
        asking_ = false;
      }
      queue(FriendPacket::VisitOk, p.id, p.gift);
      startVisit(VisitRole::Host, *f, p.gift, nowMs);
      break;
    }
    case FriendPacket::VisitOk:
      if (asking_ && strcmp(askTo_, p.id) == 0 && nowMs - askMs_ < kVisitAskMs && visit_.role == VisitRole::None) {
        startVisit(VisitRole::Visitor, *f, askGift_, nowMs);
      } else {
        queue(FriendPacket::Home, p.id, Gift::None);  // not coming after all: they must not wait for us
      }
      break;
    case FriendPacket::Home:
      if (visit_.role == VisitRole::Host && strcmp(visitWith_, p.id) == 0) endVisit(nowMs);
      break;
    case FriendPacket::Beacon: break;
  }
}

VisitView FriendPlay::visit(uint32_t nowMs) const {
  VisitView v = visit_;
  if (v.role != VisitRole::None) v.ms = nowMs - visitMs_;
  return v;
}

const char* FriendPlay::greeting(uint32_t nowMs) const {
  return greetOn_ && nowMs - greetMs_ < kGreetShowMs ? greetName_ : nullptr;
}

const char* FriendPlay::napBuddy() const {
  if (!(flags_ & kFriendNapping)) return nullptr;
  for (const Friend& f : friends_) {
    if (f.used && (f.flags & kFriendNapping)) return f.name;
  }
  return nullptr;
}

uint8_t FriendPlay::count() const {
  uint8_t n = 0;
  for (const Friend& f : friends_) n += f.used;
  return n;
}

}  // namespace miblo
