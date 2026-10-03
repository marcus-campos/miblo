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

uint8_t knownAccessory(AccSlot slot, uint8_t id) {
  bool ok = false;
  switch (slot) {
    case kAccHead: ok = id >= 1 && id <= 10; break;
    case kAccFace: ok = id == 11 || id == 12 || id == 14 || id == 15 || id == 20; break;
    case kAccNeck: ok = (id >= 16 && id <= 19) || id == 21; break;
  }
  return ok ? id : 0;
}

FriendLook friendLook(const uint32_t (&slots)[kPetSlots], uint8_t eyes, uint8_t head, uint8_t face, uint8_t neck) {
  FriendLook l;
  l.accHead = knownAccessory(kAccHead, head);
  l.accFace = knownAccessory(kAccFace, face);
  l.accNeck = knownAccessory(kAccNeck, neck);
  l.eyes = eyes < kEyeShapes ? eyes : 0;
  for (uint8_t i = 0; i < kPetSlots; i++) {
    if (slots[i] == kPetAuto || slots[i] > kPetColorMax) continue;
    const uint32_t rgb = slots[i] - 1;
    l.custom |= (uint8_t)(1u << i);
    l.rgb[i] = (uint16_t)(((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
  }
  return l;
}

void lookSlots(const FriendLook& look, uint32_t (&slots)[kPetSlots]) {
  for (uint8_t i = 0; i < kPetSlots; i++) {
    slots[i] = kPetAuto;
    if (!(look.custom & (1u << i))) continue;
    const uint32_t c = look.rgb[i];
    const uint32_t r = c >> 11, g = (c >> 5) & 63, b = c & 31;
    slots[i] = ((r << 3 | r >> 2) << 16 | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2)) + 1;
  }
}

// The look after the pet byte, each part only if it fits whole (the accessories first).
static void putLook(const FriendLook& l, uint8_t* out, size_t cap, size_t& n) {
  const bool colours = l.custom || l.eyes;
  if (!colours && !l.accHead && !l.accFace && !l.accNeck) return;  // the default look: nothing
  if (n + 3 > cap) return;
  out[n++] = l.accHead;
  out[n++] = l.accFace;
  out[n++] = l.accNeck;
  uint8_t count = 0;
  for (uint8_t i = 0; i < kPetSlots; i++) count += (l.custom >> i) & 1;
  if (!colours || n + 2 + 2 * (size_t)count > cap) return;
  out[n++] = l.custom;
  out[n++] = l.eyes;
  for (uint8_t i = 0; i < kPetSlots; i++) {
    if (!(l.custom & (1u << i))) continue;
    out[n++] = (uint8_t)(l.rgb[i] >> 8);
    out[n++] = (uint8_t)l.rgb[i];
  }
}

// The look from what follows the pet byte: the parts that are whole and valid.
static FriendLook getLook(const uint8_t* in, size_t len, size_t n) {
  FriendLook l;
  if (n + 3 > len) return l;
  l.accHead = knownAccessory(kAccHead, in[n]);
  l.accFace = knownAccessory(kAccFace, in[n + 1]);
  l.accNeck = knownAccessory(kAccNeck, in[n + 2]);
  n += 3;
  if (n + 2 > len) return l;
  const uint8_t custom = in[n], eyes = in[n + 1];
  n += 2;
  if ((custom & 0x80) || eyes >= kEyeShapes) return l;
  uint8_t count = 0;
  for (uint8_t i = 0; i < kPetSlots; i++) count += (custom >> i) & 1;
  if (n + 2 * (size_t)count > len) return l;
  l.custom = custom;
  l.eyes = eyes;
  for (uint8_t i = 0; i < kPetSlots; i++) {
    if (!(custom & (1u << i))) continue;
    l.rgb[i] = (uint16_t)(in[n] << 8 | in[n + 1]);
    n += 2;
  }
  return l;
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
  if (n < cap) {  // optional: a full packet goes out without it (a cat), and without the look
    out[n++] = p.pet;
    putLook(p.look, out, cap, n);
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

// Invisible or direction-changing characters (zero-width, bidi overrides and isolates, BOM,
// interlinear annotations): a name could hide or reorder text with them.
// Also fillers that draw nothing (soft hyphen, grapheme joiner, Hangul fillers, Mongolian vowel
// separator, variation selectors, tag characters).
static bool formatChar(uint32_t cp) {
  return (cp >= 0x200B && cp <= 0x200F) || (cp >= 0x2028 && cp <= 0x202E) || (cp >= 0x2060 && cp <= 0x206F) ||
         cp == 0xFEFF || (cp >= 0xFFF9 && cp <= 0xFFFB) || cp == 0x00AD || cp == 0x034F || cp == 0x115F ||
         cp == 0x1160 || cp == 0x180E || cp == 0x3164 || cp == 0xFFA0 || (cp >= 0xFE00 && cp <= 0xFE0F) ||
         (cp >= 0xE0000 && cp <= 0xE007F);
}

static bool spaceChar(uint32_t cp) {
  return cp == ' ' || cp == 0x00A0 || cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200A) || cp == 0x202F ||
         cp == 0x205F || cp == 0x3000;
}

// A name: <= 20 characters of well-formed UTF-8 (no overlong forms, surrogates or code points past
// U+10FFFF) without control characters (C0, DEL, C1).
static bool validName(const char* s) {
  if (utf8Length(s) > 20) return false;
  for (const char* p = s; *p;) {
    const char* start = p;
    const uint32_t cp = utf8Next(p);
    const size_t len = (size_t)(p - start);
    const size_t need = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
    if (cp == 0xFFFD || len != need || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
    if (cp < 0x20 || (cp >= 0x7F && cp <= 0x9F)) return false;
  }
  return true;
}

// Removes formatChar()s and the spaces around the name, in place (the name is already valid). A
// name left empty becomes the id's default name ("miblo-4f2a" is "Miblo-4F2A").
static void cleanName(char* s, size_t cap, const char* id) {
  char* w = s;
  char* end = s;  // after the last non-space
  for (const char* p = s; *p;) {
    const char* start = p;
    const uint32_t cp = utf8Next(p);
    if (formatChar(cp) || (w == s && spaceChar(cp))) continue;
    while (start < p) *w++ = *start++;
    if (!spaceChar(cp)) end = w;
  }
  *end = 0;
  if (s[0]) return;
  strncpy(s, id, cap - 1);
  s[cap - 1] = 0;
  if (strncmp(s, "miblo-", 6) != 0) return;
  s[0] = 'M';
  for (char* c = s + 6; *c; c++) {
    if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 'a' + 'A');
  }
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
  if (n < len) {  // absent (an older firmware): the cat, in its preset
    p.pet = knownPet(in[n++]);
    p.look = getLook(in, len, n);
  }
  if (!validId(p.id, false) || !validId(p.to, true) || !validName(p.name)) return false;
  cleanName(p.name, sizeof(p.name), p.id);
  // Addressed to itself, or an invitation to visit its own addressee: nothing sends these.
  if (strcmp(p.id, p.to) == 0 || (p.host[0] && strcmp(p.host, p.to) == 0)) return false;
  // Home with no addressee: "all my guests, go home" (the host's human got back to work).
  if (p.type != FriendPacket::Beacon && p.type != FriendPacket::Who && p.type != FriendPacket::Home && !p.to[0]) {
    return false;
  }
  out = p;  // anything after the known fields is ignored (a future version may add some)
  return true;
}

// ---- FriendPlay ----

void FriendPlay::setSelf(const char* id, const char* name, uint8_t mascot, uint8_t pet, const FriendLook& look) {
  // The name as the other Miblos will read it: one they would refuse (the settings accept any
  // printable bytes; decodeFriendPacket wants valid UTF-8 without C1 controls) goes out as the
  // default name, or every packet of ours would be dropped.
  char clean[sizeof(name_)];
  utf8Copy(clean, sizeof(clean), name, 20);
  if (!validName(clean)) clean[0] = 0;
  cleanName(clean, sizeof(clean), id);
  if (strcmp(id, id_) != 0 || strcmp(clean, name_) != 0 || mascot != mascot_ || pet != pet_ ||
      look != look_) {
    strncpy(id_, id, sizeof(id_) - 1);
    strcpy(name_, clean);
    mascot_ = mascot;
    pet_ = pet;
    look_ = look;
    announce_ = true;
  }
}

FriendPlay::Friend* FriendPlay::find(const char* id) {
  for (Friend& f : friends_) {
    if (f.used && strcmp(f.id, id) == 0) return &f;
  }
  return nullptr;
}

bool FriendPlay::inUse(const char* id) const {
  if (!id[0]) return false;
  if (strcmp(visitWith_, id) == 0 || strcmp(reservedFor_, id) == 0 || (replying_ && strcmp(replyTo_, id) == 0)) {
    return true;
  }
  for (uint8_t i = 0; i < visit_.extra; i++) {
    if (strcmp(extraIds_[i], id) == 0) return true;
  }
  const uint8_t n = membersN_ > invitedN_ ? membersN_ : invitedN_;
  for (uint8_t i = 0; i < n; i++) {
    if (strcmp(members_[i], id) == 0) return true;
  }
  return false;
}

FriendPlay::Friend* FriendPlay::admit(const char* id, uint32_t ip, uint32_t nowMs) {
  // One machine is one Miblo: a new id from an address a live friend holds is someone multiplying
  // itself (every limit is per id). 0 is "unknown" (tests only).
  for (const Friend& c : friends_) {
    if (ip && c.used && c.ip == ip) return nullptr;
  }
  Friend* f = nullptr;
  for (Friend& c : friends_) {
    if (!c.used) {
      f = &c;
      break;
    }
  }
  if (!f) {
    // Table full: a random one makes room (not the oldest: Miblos powered on together announce
    // themselves in the same order, which would always keep the same few and leave out the rest).
    // Never one the visit, the group or a reservation needs. A friend known for a while leaves at
    // most every kEvictGapMs: a flood of new ids churns among its own instead of pushing out the
    // friends around (a big network of real ones still rotates).
    const bool knownMayGo = !evicted_ || nowMs - evictMs_ >= kEvictGapMs;
    const uint32_t r = nextRnd();
    for (uint8_t k = 0; k < kMaxFriends && !f; k++) {
      Friend& c = friends_[(r + k) % kMaxFriends];
      if (inUse(c.id)) continue;
      const bool known = c.seenMs - c.firstMs >= kFriendProvenMs;
      if (known && !knownMayGo) continue;
      if (known) {
        evicted_ = true;
        evictMs_ = nowMs;
      }
      f = &c;
    }
    if (!f) return nullptr;
  }
  *f = Friend();
  f->used = true;
  strcpy(f->id, id);
  f->ip = ip;
  f->firstMs = f->seenMs = f->rateMs = nowMs;
  return f;
}

// A visit already started by this friend lately: not another one yet.
bool FriendPlay::mayStart(const Friend& f, uint32_t nowMs) {
  if (!(f.marks & kMarkStarted)) return true;
  return nowMs - f.startedMs >= (demoOn(nowMs) ? kVisitMs : kVisitStartGapMs);
}

bool FriendPlay::invited(const char* id) const {
  for (uint8_t i = 0; i < invitedN_; i++) {
    if (strcmp(members_[i], id) == 0) return true;
  }
  return false;
}

// Time into a visit from a packet's offset, never past the walk in (current firmware sends 0).
static uint32_t offsetMs(uint8_t offset) {
  const uint32_t ms = (uint32_t)offset * 100;
  return ms < kVisitWalkMs ? ms : kVisitWalkMs;
}

FriendPlay::Out* FriendPlay::queue(FriendPacket::Type type, const char* to, Gift gift) {
  if (outN_ >= kFriendOutMax) return nullptr;
  Out& o = out_[outN_++];
  memset(&o, 0, sizeof(o));
  o.type = type;
  o.flags = (uint8_t)(flags_ | (visit_.role != VisitRole::None ? kFriendBusy : 0));
  if (to) strncpy(o.to, to, sizeof(o.to) - 1);
  o.gift = gift;
  o.chance = 255;
  return &o;
}

bool FriendPlay::nextPacket(FriendPacket& out) {
  if (!outN_) return false;
  const Out& o = out_[0];
  out = FriendPacket();
  out.type = o.type;
  strcpy(out.id, id_);
  strcpy(out.name, name_);
  out.mascot = mascot_;
  out.pet = pet_;
  out.look = look_;
  out.flags = o.flags;
  strcpy(out.to, o.to);
  out.gift = o.gift;
  out.chance = o.chance;
  strcpy(out.host, o.host);
  for (uint8_t i = 1; i < outN_; i++) out_[i - 1] = out_[i];
  outN_--;
  return true;
}

void FriendPlay::demo(uint32_t nowMs, uint32_t untilMs) {
  demo_ = (int32_t)(untilMs - nowMs) > 0;
  demoUntilMs_ = untilMs;
  if (!demo_) return;
  for (Friend& f : friends_) f.greeted = false;
  greetWinN_ = 0;
  if (visit_.role == VisitRole::None) {
    asking_ = polling_ = awaiting_ = false;  // a poll under way would push the demo's first visit back
    membersN_ = invitedN_ = 0;
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
  if (Out* o = queue(FriendPacket::Who, nullptr, Gift::None)) o->chance = (uint8_t)(chance ? chance : 1);
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
  } else {  // we host: wait for the guests (only the members are let in)
    awaiting_ = true;
    anchorMs_ = nowMs;
    hostGift_ = gift;
    invitedN_ = membersN_;
    invitesFrom_ = false;
  }
  for (uint8_t i = 0; i < membersN_; i++) {
    if (Friend* m = find(members_[i])) noteStart(*m, nowMs);
    if (i + 1 == h) continue;
    Out* o = queue(FriendPacket::Invite, members_[i], gift);
    if (o && h) strcpy(o->host, host);
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
  visit_.pet = f.pet;
  visit_.look = f.look;
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
  invitedN_ = 0;
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
    for (Friend& f : friends_) f.marks &= (uint8_t)~kMarkHeard;
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
    membersN_ = invitedN_ = 0;
    return;
  }
  if (!wasRoaming || !scheduled_) firstVisit(nowMs);

  // Say hi to a friend that is in pet mode too (once in a while each).
  if (!greetOn_ || nowMs - greetMs_ >= kGreetShowMs) {
    greetOn_ = false;
    if (greetWinN_ && nowMs - greetWinMs_ >= kGreetWindowMs) greetWinN_ = 0;
    for (Friend& f : friends_) {
      if (greetWinN_ >= kGreetMax) break;  // a crowd of new ids cannot fill the screen with hellos
      if (!f.used || (flags & kFriendNapping) || (f.flags & (kFriendRoaming | kFriendNapping)) != kFriendRoaming || (f.greeted && nowMs - f.greetedMs < kGreetEveryMs)) continue;
      f.greeted = true;
      f.greetedMs = nowMs;
      strcpy(greetName_, f.name);
      greetMs_ = nowMs;
      greetOn_ = true;
      if (!greetWinN_++) greetWinMs_ = nowMs;
      break;
    }
  }

  // Chosen as host but no guest came (they changed their mind, or a packet was lost).
  if (awaiting_ && nowMs - anchorMs_ >= kVisitArriveMs) {
    awaiting_ = false;
    invitedN_ = 0;
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
    membersN_ = invitedN_ = 0;
    polling_ = true;
    pollMs_ = nowMs;
    queuePoll((uint8_t)(groupSize_ - 1));
    scheduled_ = false;  // rescheduled when the visit ends, or when nobody answers
  }
}

void FriendPlay::receive(const FriendPacket& p, uint32_t nowMs, uint32_t fromIp) {
  // Off, or our own broadcast (or our id claimed by someone else).
  if (!enabled_ || !id_[0] || strcmp(p.id, id_) == 0) return;
  Friend* f = find(p.id);
  if (f && f->ip != fromIp) return;  // a friend's id from another address: an impostor
  if (!f) f = admit(p.id, fromIp, nowMs);
  if (f) {  // a few packets per second each; the excess is dropped
    if (nowMs - f->rateMs >= kFriendRateWindowMs) {
      f->rateMs = nowMs;
      f->rateN = 0;
      f->marks &= (uint8_t)~kMarkHomed;
    }
    if (f->rateN >= kFriendRateMax) return;
    f->rateN++;
  }
  if (!f) return;  // no room for a stranger right now
  // Network size (sets our poll's answer chance): each friend once per beacon period.
  if (p.type == FriendPacket::Beacon && !(f->marks & kMarkHeard) && heard_ < 0xFFFF) {
    f->marks |= kMarkHeard;
    heard_++;
  }
  strcpy(f->name, p.name[0] ? p.name : p.id);
  f->mascot = p.mascot;
  f->pet = p.pet;
  f->look = p.look;
  f->flags = p.flags;
  f->seenMs = nowMs;
  if (p.type == FriendPacket::Beacon) return;
  if (p.type == FriendPacket::Who) {  // someone is organising a visit: answer, maybe
    const bool lately = (f->marks & kMarkAnswered) && nowMs - f->whoMs < kWhoGapMs;
    if (free() && !reservedFor_[0] && !replying_ && !lately && mayStart(*f, nowMs) &&
        (nextRnd() & 0xFF) < p.chance) {
      replying_ = true;
      strcpy(replyTo_, p.id);
      replyAtMs_ = nowMs + nextRnd() % kHereSpreadMs;
      f->marks |= kMarkAnswered;
      f->whoMs = nowMs;
    }
    return;
  }
  if (p.type == FriendPacket::Home && !p.to[0]) {  // a host sending its guests home
    turnAway(p.id, nowMs);
    return;
  }
  // Our organiser inviting others to our place (broadcast, so we hear it): they are expected too.
  if (p.type == FriendPacket::Invite && invitesFrom_ && invitedN_ && invitedN_ < kMaxGuests &&
      (awaiting_ || visit_.role == VisitRole::Host) && strcmp(p.host, id_) == 0 &&
      strcmp(members_[0], p.id) == 0 && !invited(p.to)) {
    strcpy(members_[invitedN_++], p.to);
    return;
  }
  if (strcmp(p.to, id_) != 0) return;
  const bool mine = !reservedFor_[0] || strcmp(reservedFor_, p.id) == 0;  // not promised elsewhere
  const bool asked = reservedFor_[0] && strcmp(reservedFor_, p.id) == 0;  // we answered its poll
  switch (p.type) {
    case FriendPacket::Here: {
      // An answer to our poll: one more member; the group forms once it is full.
      if (!polling_ || membersN_ >= kMaxGuests || !mayStart(*f, nowMs)) break;
      bool dup = false;
      for (uint8_t i = 0; i < membersN_; i++) dup |= strcmp(members_[i], p.id) == 0;
      if (dup) break;
      strcpy(members_[membersN_++], p.id);
      if (membersN_ + 1 >= groupSize_) formGroup(nowMs);
      break;
    }
    case FriendPacket::Host:
      // Drawn as the host of a group we answered: wait for the guests on that timeline. The
      // organiser is expected; the others it invites are added as we hear their Invites.
      if (!asked || visit_.role != VisitRole::None || polling_ || !mayStart(*f, nowMs)) break;
      reservedFor_[0] = 0;
      noteStart(*f, nowMs);
      awaiting_ = true;
      anchorMs_ = nowMs - offsetMs(p.offset);
      hostGift_ = p.gift;
      strcpy(members_[0], p.id);
      invitedN_ = 1;
      invitesFrom_ = true;
      break;
    case FriendPacket::Invite: {
      // Go visiting: to the host named, or to the sender (only for the one whose poll we answered).
      if (!asked || visit_.role != VisitRole::None || polling_ || awaiting_ || !mayStart(*f, nowMs)) break;
      const char* hostId = p.host[0] ? p.host : p.id;
      Friend* hf = find(hostId);
      if (!hf) break;  // never heard of it: cannot show where we went
      reservedFor_[0] = 0;
      noteStart(*f, nowMs);
      startVisit(VisitRole::Visitor, *hf, p.gift, nowMs);
      visitMs_ = nowMs - offsetMs(p.offset);
      queue(FriendPacket::VisitOk, hostId, p.gift);
      break;
    }
    case FriendPacket::VisitOk: {
      // A guest is coming: the first starts the visit (on the group's timeline), the next ones join.
      // Only this visit's guests, and 3 at most; anyone else is sent home.
      bool here = visit_.role == VisitRole::Host && strcmp(visitWith_, p.id) == 0;
      for (uint8_t i = 0; i < visit_.extra; i++) here |= strcmp(extraIds_[i], p.id) == 0;
      if (here) break;  // said twice
      if (awaiting_ && invited(p.id)) {
        startVisit(VisitRole::Host, *f, hostGift_, anchorMs_);
      } else if (visit_.role == VisitRole::Host && invited(p.id) && nowMs - visitMs_ < kVisitArriveMs &&
                 visit_.extra < kMaxGuests - 1) {
        strcpy(extraIds_[visit_.extra], p.id);
        visit_.extraPet[visit_.extra] = p.pet;
        visit_.extraLook[visit_.extra] = p.look;
        visit_.extraMascot[visit_.extra++] = p.mascot;
      } else {
        // Too late, too many, or not invited: do not wait. Once per sender per rate window, and
        // never into the queue's last slot (real packets keep their room).
        if (!(f->marks & kMarkHomed) && outN_ + 1 < kFriendOutMax) {
          f->marks |= kMarkHomed;
          queue(FriendPacket::Home, p.id, Gift::None);
        }
      }
      break;
    }
    case FriendPacket::VisitAsk:
      // An older firmware asking to visit us directly: host it 1:1 (it alone is let in).
      if (!free() || !mine || !mayStart(*f, nowMs)) break;
      reservedFor_[0] = 0;
      noteStart(*f, nowMs);
      queue(FriendPacket::VisitOk, p.id, p.gift);
      startVisit(VisitRole::Host, *f, p.gift, nowMs);
      strcpy(members_[0], p.id);
      invitedN_ = 1;
      invitesFrom_ = false;
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
        visit_.pet = visit_.extraPet[0];
        visit_.look = visit_.extraLook[0];
        for (uint8_t j = 1; j < visit_.extra; j++) {
          strcpy(extraIds_[j - 1], extraIds_[j]);
          visit_.extraMascot[j - 1] = visit_.extraMascot[j];
          visit_.extraPet[j - 1] = visit_.extraPet[j];
          visit_.extraLook[j - 1] = visit_.extraLook[j];
        }
        visit_.extra--;
        break;
      }
      for (uint8_t i = 0; i < visit_.extra; i++) {  // an extra guest went home early
        if (strcmp(extraIds_[i], p.id) != 0) continue;
        for (uint8_t j = i + 1; j < visit_.extra; j++) {
          strcpy(extraIds_[j - 1], extraIds_[j]);
          visit_.extraMascot[j - 1] = visit_.extraMascot[j];
          visit_.extraPet[j - 1] = visit_.extraPet[j];
          visit_.extraLook[j - 1] = visit_.extraLook[j];
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
