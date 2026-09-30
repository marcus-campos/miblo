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
  out[n++] = p.type == FriendPacket::Who ? p.chance : (uint8_t)p.gift;
  if (!putStr(out, cap, n, p.id) || !putStr(out, cap, n, p.name) || !putStr(out, cap, n, p.to)) return 0;
  if (p.type == FriendPacket::Invite || p.type == FriendPacket::Host) {
    if (n >= cap) return 0;
    out[n++] = p.offset;
    if (p.type == FriendPacket::Invite && !putStr(out, cap, n, p.host)) return 0;
  }
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
  if (in[5] < FriendPacket::Beacon || in[5] > FriendPacket::Host) return false;
  p.type = (FriendPacket::Type)in[5];
  p.mascot = in[6] < kMascotColours ? in[6] : 0;
  p.flags = in[7] & (kFriendRoaming | kFriendNapping | kFriendTired | kFriendBusy);
  if (p.type == FriendPacket::Who) p.chance = in[8];  // Who: that byte is the answer chance
  else p.gift = in[8] < (uint8_t)Gift::Count ? (Gift)in[8] : Gift::None;  // a newer activity: a plain visit
  size_t n = 9;
  if (!getStr(in, len, n, p.id, sizeof(p.id)) || !getStr(in, len, n, p.name, sizeof(p.name)) ||
      !getStr(in, len, n, p.to, sizeof(p.to))) {
    return false;
  }
  if ((p.type == FriendPacket::Invite || p.type == FriendPacket::Host) && n < len) {
    p.offset = in[n++];
    if (p.type == FriendPacket::Invite && n < len && !getStr(in, len, n, p.host, sizeof(p.host))) return false;
    if (!validId(p.host, true)) return false;
  }
  if (!validId(p.id, false) || !validId(p.to, true) || !validName(p.name)) return false;
  // Home with no addressee: "all my guests, go home" (the host's human got back to work).
  if (p.type != FriendPacket::Beacon && p.type != FriendPacket::Who && p.type != FriendPacket::Home && !p.to[0]) {
    return false;
  }
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
    for (Friend& c : friends_) {
      if (!c.used) {
        f = &c;
        break;
      }
    }
    if (!f) {
      // Table full: a random one makes room (not the oldest: Miblos powered on together announce
      // themselves in the same order, which would always keep the same few and leave out the rest).
      // Never the one we are visiting with.
      const uint32_t r = nextRnd();
      f = &friends_[r % kMaxFriends];
      if (visitWith_[0] && strcmp(f->id, visitWith_) == 0) f = &friends_[(r + 1) % kMaxFriends];
    }
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
    asking_ = polling_ = awaiting_ = false;  // a poll under way would push the demo's first visit back
    membersN_ = 0;
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
  (void)hosted;  // who hosts is drawn within each group: no need to steer the turns
  scheduleVisit(nowMs, min, span);
}

uint32_t FriendPlay::nextRnd() {
  rnd_ = rnd_ * 1103515245u + 12345u;
  return rnd_ >> 8;
}

bool FriendPlay::free() const {
  return (flags_ & (kFriendRoaming | kFriendNapping)) == kFriendRoaming && visit_.role == VisitRole::None &&
         !polling_ && !awaiting_ && !asking_;
}

// "Who is free?" to the whole network, with the answer chance set so about 2x `wanted` come back.
void FriendPlay::queuePoll(uint8_t wanted) {
  const uint16_t others = heardLast_ > heard_ ? heardLast_ : heard_;
  const uint32_t want = (uint32_t)wanted * kPollWanted;
  const uint32_t chance = others <= want ? 255u : 255u * want / others;
  queue(FriendPacket::Who, nullptr, Gift::None);
  out_[outN_ - 1].chance = (uint8_t)(chance ? chance : 1);
}

// The group is formed (us + the members that answered): draw the host among all of us, uniformly,
// so no Miblo is always the one visited, then tell everyone where to go.
void FriendPlay::formGroup(uint32_t nowMs) {
  polling_ = false;
  const uint8_t n = (uint8_t)(membersN_ + 1);
  const uint8_t h = (uint8_t)(nextRnd() % n);  // 0 = us, 1.. = members_[h - 1]
  const char* host = h ? members_[h - 1] : id_;
  const Friend* hf0 = h ? find(host) : nullptr;
  const bool hostTired = h ? (hf0 && (hf0->flags & kFriendTired)) : (flags_ & kFriendTired);
  const Gift gift = chooseGift(hostTired, nextRnd());  // a tired host more likely gets a coffee
  if (h) {  // someone else hosts: tell them to wait, then go there ourselves
    queue(FriendPacket::Host, host, gift);
    Friend* hf = find(host);
    if (hf) {
      startVisit(VisitRole::Visitor, *hf, gift, nowMs);
      queue(FriendPacket::VisitOk, host, gift);
    }
  } else {  // we host: wait for the guests
    awaiting_ = true;
    anchorMs_ = nowMs;
    hostGift_ = gift;
  }
  for (uint8_t i = 0; i < membersN_; i++) {
    if (i + 1 == h) continue;
    queue(FriendPacket::Invite, members_[i], gift);
    if (h) strcpy(out_[outN_ - 1].host, host);
  }
  membersN_ = 0;
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
  polling_ = false;
  awaiting_ = false;
  greetOn_ = false;
  announce_ = true;  // busy now
}

void FriendPlay::endVisit(uint32_t nowMs) {
  const bool hosted = visit_.role == VisitRole::Host;
  if (Friend* f = find(visitWith_)) f->lastRole = hosted ? 2 : 1;
  visit_ = VisitView();
  visitWith_[0] = 0;
  announce_ = true;
  nextVisit(nowMs, hosted);
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
  if (nowMs - heardAtMs_ >= kBeaconEveryMs) {  // a new 30 s window: keep the last one's count
    heardLast_ = heard_;
    heard_ = 0;
    heardAtMs_ = nowMs;
  }
  // Our answer to someone's "who is free?", once its random delay is up (if still free).
  if (replying_ && (int32_t)(nowMs - replyAtMs_) >= 0) {
    replying_ = false;
    if (free()) {
      queue(FriendPacket::Here, replyTo_, Gift::None);
      strcpy(reservedFor_, replyTo_);  // held for them until they ask
      reservedUntilMs_ = nowMs + kReserveMs;
    }
  }
  if (reservedFor_[0] && (int32_t)(nowMs - reservedUntilMs_) >= 0) reservedFor_[0] = 0;
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
      // Our human is back: a visitor goes home at once (telling its host); a host sends all its
      // guests home, and they walk back disappointed.
      queue(FriendPacket::Home, visit_.role == VisitRole::Visitor ? visitWith_ : nullptr, Gift::None);
      endVisit(nowMs);
    }
    return;
  }
  if (asking_ && nowMs - askMs_ >= kVisitAskMs) {
    asking_ = false;
    nextVisit(nowMs, true);
  }
  if (!roaming) {
    if (awaiting_) queue(FriendPacket::Home, nullptr, Gift::None);  // chosen as host, now busy: nobody come
    scheduled_ = false;
    greetOn_ = false;
    awaiting_ = polling_ = false;
    membersN_ = 0;
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

  // Chosen as host but no guest came (they changed their mind, or a packet was lost).
  if (awaiting_ && nowMs - anchorMs_ >= kVisitArriveMs) {
    awaiting_ = false;
    firstVisit(nowMs);
  }
  // The poll is over: go with whoever answered (a smaller group), or look again later.
  if (polling_ && nowMs - pollMs_ >= kPollMs) {
    if (membersN_) formGroup(nowMs);
    else {
      polling_ = false;
      firstVisit(nowMs);
    }
  }
  // Our turn to organise a visit: draw the size of the group (1:1 most often, sometimes 1:2 or
  // 1:3; in a demo each as likely) and ask the network who is free. Each one answers with a chance
  // that brings back a handful of answers, whether there are 2 Miblos or 1000.
  // (Not while we answered someone else's poll: we are promised to their group.)
  if (free() && !reservedFor_[0] && !replying_ && !(flags & kFriendNapping) &&
      (int32_t)(nowMs - nextVisitMs_) >= 0) {
    const uint32_t r = nextRnd() % 100;
    groupSize_ = (uint8_t)(2 + (demoOn(nowMs) ? r % kMaxGuests : r < 60 ? 0 : r < 88 ? 1 : 2));
    membersN_ = 0;
    polling_ = true;
    pollMs_ = nowMs;
    queuePoll((uint8_t)(groupSize_ - 1));
    scheduled_ = false;  // rescheduled when the visit ends, or when nobody answers
  }
}

void FriendPlay::receive(const FriendPacket& p, uint32_t nowMs) {
  if (!enabled_ || !id_[0] || strcmp(p.id, id_) == 0) return;  // off, or our own broadcast
  Friend* f = remember(p, nowMs);
  if (p.type == FriendPacket::Beacon) {
    if (heard_ < 0xFFFF) heard_++;
    return;
  }
  if (p.type == FriendPacket::Who) {  // someone is organising a visit: answer, maybe
    if (free() && !reservedFor_[0] && !replying_ && (nextRnd() & 0xFF) < p.chance) {
      replying_ = true;
      strcpy(replyTo_, p.id);
      replyAtMs_ = nowMs + nextRnd() % kHereSpreadMs;
    }
    return;
  }
  if (p.type == FriendPacket::Home && !p.to[0]) {  // a host sending its guests home
    turnAway(p.id, nowMs);
    return;
  }
  if (strcmp(p.to, id_) != 0) return;
  const bool mine = !reservedFor_[0] || strcmp(reservedFor_, p.id) == 0;  // not promised elsewhere
  switch (p.type) {
    case FriendPacket::Here: {
      // An answer to our poll: one more member; the group forms once it is full.
      if (!polling_ || membersN_ >= kMaxGuests) break;
      bool dup = false;
      for (uint8_t i = 0; i < membersN_; i++) dup |= strcmp(members_[i], p.id) == 0;
      if (dup) break;
      strcpy(members_[membersN_++], p.id);
      if (membersN_ + 1 >= groupSize_) formGroup(nowMs);
      break;
    }
    case FriendPacket::Host:
      // Drawn as the host of a group we answered: wait for the guests on that timeline.
      if (!mine || visit_.role != VisitRole::None || polling_) break;
      reservedFor_[0] = 0;
      awaiting_ = true;
      anchorMs_ = nowMs - (uint32_t)p.offset * 100;
      hostGift_ = p.gift;
      break;
    case FriendPacket::Invite: {
      // Go visiting: to the host named, or to the sender.
      if (!mine || visit_.role != VisitRole::None || polling_ || awaiting_) break;
      const char* hostId = p.host[0] ? p.host : p.id;
      Friend* hf = find(hostId);
      if (!hf) break;  // never heard of it: cannot show where we went
      reservedFor_[0] = 0;
      startVisit(VisitRole::Visitor, *hf, p.gift, nowMs);
      visitMs_ = nowMs - (uint32_t)p.offset * 100;
      queue(FriendPacket::VisitOk, hostId, p.gift);
      break;
    }
    case FriendPacket::VisitOk:
      // A guest is coming: the first starts the visit (on the group's timeline), the next ones join.
      if (awaiting_) {
        const uint32_t anchor = anchorMs_;
        startVisit(VisitRole::Host, *f, hostGift_, anchor);
      } else if (visit_.role == VisitRole::Host && strcmp(visitWith_, p.id) != 0 &&
                 nowMs - visitMs_ < kVisitArriveMs && visit_.extra < kMaxGuests - 1) {
        strcpy(extraIds_[visit_.extra], p.id);
        visit_.extraMascot[visit_.extra++] = p.mascot;
      } else if (visit_.role != VisitRole::Host || strcmp(visitWith_, p.id) != 0) {
        queue(FriendPacket::Home, p.id, Gift::None);  // too late, or not expected: they must not wait
      }
      break;
    case FriendPacket::VisitAsk:
      // An older firmware asking to visit us directly: host it 1:1.
      if (!free() || !mine) break;
      reservedFor_[0] = 0;
      queue(FriendPacket::VisitOk, p.id, p.gift);
      startVisit(VisitRole::Host, *f, p.gift, nowMs);
      break;
    case FriendPacket::Home:
      if (visit_.role == VisitRole::Visitor && strcmp(visitWith_, p.id) == 0) {  // the host cannot have us
        turnAway(p.id, nowMs);
        break;
      }
      if (visit_.role != VisitRole::Host) break;
      if (strcmp(visitWith_, p.id) == 0 && !visit_.extra) {
        endVisit(nowMs);
        break;
      }
      if (strcmp(visitWith_, p.id) == 0) {  // the first guest left: the next one takes its place
        strcpy(visitWith_, extraIds_[0]);
        if (Friend* nf = find(visitWith_)) strcpy(visit_.name, nf->name);
        visit_.mascot = visit_.extraMascot[0];
        for (uint8_t j = 1; j < visit_.extra; j++) {
          strcpy(extraIds_[j - 1], extraIds_[j]);
          visit_.extraMascot[j - 1] = visit_.extraMascot[j];
        }
        visit_.extra--;
        break;
      }
      for (uint8_t i = 0; i < visit_.extra; i++) {  // an extra guest went home early
        if (strcmp(extraIds_[i], p.id) != 0) continue;
        for (uint8_t j = i + 1; j < visit_.extra; j++) {
          strcpy(extraIds_[j - 1], extraIds_[j]);
          visit_.extraMascot[j - 1] = visit_.extraMascot[j];
        }
        visit_.extra--;
        break;
      }
      break;
    case FriendPacket::Beacon:
    case FriendPacket::Who: break;
  }
}

// Our host got busy: come back home now, disappointed (straight into the walk back).
void FriendPlay::turnAway(const char* hostId, uint32_t nowMs) {
  if (visit_.role != VisitRole::Visitor || strcmp(visitWith_, hostId) != 0 || visit_.turnedAway) return;
  visit_.turnedAway = true;
  const uint32_t back = kVisitMs - kVisitWalkMs;
  if (nowMs - visitMs_ < back) visitMs_ = nowMs - back;
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
