#pragma once
#include <stddef.h>
#include <stdint.h>

// Miblos on the same network find each other and play while in pet mode (an easter egg): a
// "hi" when another one shows up, visits (the mascot walks off one screen and into the other's),
// a coffee for a friend whose limits are running low, and naps in sync.
//
// Each gadget broadcasts a small UDP packet (kFriendPort) every kBeaconEveryMs with its id, name,
// mascot colour and a few flags (pet mode, napping, tired). Nothing about sessions, limits or the
// owner travels: "tired" only says its limits are past 80%. Packets only ever start animations.
namespace miblo {

constexpr uint16_t kFriendPort = 47757;
constexpr size_t kFriendPacketMax = 120;
constexpr uint8_t kMaxFriends = 8;  // when more are around, a random one makes room (no cliques)
constexpr uint32_t kBeaconEveryMs = 30000;
constexpr uint32_t kBeaconMinGapMs = 2000;   // a state change is announced at once, but not faster
constexpr uint32_t kFriendTtlMs = 95000;     // three beacons missed: gone
constexpr uint32_t kGreetEveryMs = 30UL * 60000;  // a "hi" per friend at most this often
constexpr uint32_t kGreetShowMs = 5000;
constexpr uint32_t kVisitAskMs = 4000;       // an unanswered visit request is dropped
constexpr uint32_t kPollMs = 1800;           // how long "who is free?" collects answers
constexpr uint32_t kHereSpreadMs = 1400;     // answers are spread over this, not all at once
constexpr uint8_t kPollWanted = 4;           // answers wanted back (about 4 random Miblos), any network size
constexpr uint32_t kReserveMs = kHereSpreadMs + kVisitAskMs;  // after answering: only that one may ask
constexpr uint32_t kFirstVisitMinMs = 60000;  // after pet mode starts: the first visit comes in 1-3 min
constexpr uint32_t kFirstVisitSpanMs = 120000;
constexpr uint32_t kNextVisitMinMs = 6UL * 60000;  // then every 6-12 min
constexpr uint32_t kNextVisitSpanMs = 6UL * 60000;
// A visit, the same timeline on both gadgets from the moment it starts: the visitor walks off
// its own screen (to the right), walks into the host's (from the left), they play, it walks
// back out (to the left) and home again (from the right).
constexpr uint32_t kDemoFirstVisitMs = 8000;   // + up to 4 s
constexpr uint32_t kDemoNextVisitMs = 20000;   // + up to 20 s (the last host asks in the first half)
constexpr uint32_t kVisitWalkMs = 3000;
constexpr uint32_t kVisitStayMs = 18000;
constexpr uint32_t kVisitArriveMs = 2 * kVisitWalkMs;              // guest fully in on the host
constexpr uint32_t kVisitPartMs = kVisitArriveMs + kVisitStayMs;   // guest starts to leave
constexpr uint32_t kVisitMs = kVisitPartMs + 2 * kVisitWalkMs;      // visitor back home

enum : uint8_t { kFriendRoaming = 1, kFriendNapping = 2, kFriendTired = 4, kFriendBusy = 8 };
// What a visit is about (chosen by the visitor at random, a coffee more likely for a tired friend):
// programmer things. Unknown values from a newer firmware decode as None.
enum class Gift : uint8_t {
  None,        // just a visit (hearts)
  Coffee,      // brings a coffee
  Duck,        // rubber duck debugging
  Pair,        // pair programming on a tiny laptop
  Review,      // code review: holds up an "LGTM" sign
  Bug,         // hunting a bug together
  Deploy,      // deploying (a rocket takes off)
  HighFive,    // high five: paws meet in the middle with a spark
  PingPong,    // ping-pong: a ball goes back and forth
  Dance,       // a little dance, notes rising
  Pizza,       // pizza: a slice each
  Cake,        // release cake: blow out the candle, confetti
  Merge,       // merge conflict: tug of war over a rope
  Standup,     // daily standup: sticky notes on a tiny board
  Hackathon,   // hackathon: a laptop each, typing fast
  Selfie,      // selfie: pose, flash, look at the photo
  Chess,       // chess on a tiny board
  Game,        // video game: a controller each
  Gossip,      // gossip: a whisper, wide eyes, laughs
  Toast,       // a toast: mugs clink
  Movie,       // movie time: a tiny screen and popcorn
  Blocks,      // stacking blocks until the tower falls
  Brainstorm,  // brainstorm: light bulbs over both
  Pomodoro,    // pomodoro: work until the tomato timer rings
  Hotfix,      // hotfix: a smoking server, put out with an extinguisher
  Tests,       // tests passing: green checks one by one
  NotFound,    // 404: searching with a magnifier, nothing found
  ShipIt,      // ship it: a paper boat slides across
  Sprint,      // sprint: running in place to a finish flag
  Origami,     // origami: a sheet folded into a paper plane
  Nostalgia,   // nostalgia: the dusty Q&A site everyone used before AI ("Stack Underflow")
  Panic,       // kernel panic: an alarm, both shaking
  Picnic,      // picnic: a checkered cloth and a sandwich
  Fishing,     // fishing: a fish bites, pulled in together
  Umbrella,    // rain: one umbrella over both
  CanPhone,    // tin can phone: a cup each and a string
  Kite,        // flying a kite
  Count
};

struct FriendPacket {
  // Who: "who is free for a visit?" (broadcast; `chance` says how likely each one should answer, so
  // a network of any size sends back only a handful). Here: "I am" (to the one who asked).
  // A visit is organised by whoever's turn it is: it asks the network who is free (Who, answered
  // with Here), forms a group of 2 to 4 with the first answers, and draws the HOST among all of
  // them, itself included. Host: the chosen host is told to wait for guests. Invite: each other
  // member is told to go (to `host`, or to the sender when empty). Guests tell the host they are
  // coming with VisitOk. `offset` is how far into the visit it is (100 ms steps): one timeline.
  enum Type : uint8_t {
    Beacon = 1, VisitAsk = 2, VisitOk = 3, Home = 4, Who = 5, Here = 6, Invite = 7, Host = 8
  };
  Type type = Beacon;
  char id[16] = "";    // sender, "miblo-4f2a"
  char name[64] = "";  // sender's name (<= 20 characters)
  uint8_t mascot = 0;  // sender's mascot colour
  uint8_t flags = 0;   // kFriend*
  char to[16] = "";    // addressee (empty for beacons and Who)
  Gift gift = Gift::None;
  uint8_t chance = 255;  // Who only: each free Miblo answers with probability chance/255
  uint8_t offset = 0;    // Invite/Host: time into the visit, in 100 ms steps
  char host[16] = "";    // Invite: the host (empty: the sender itself)
};

// "MBLO", version, type, mascot, flags, gift, then id, name and to as length-prefixed strings.
// encode returns the length (0 if it does not fit); decode rejects anything malformed.
size_t encodeFriendPacket(const FriendPacket& p, uint8_t* out, size_t cap);
bool decodeFriendPacket(const uint8_t* in, size_t len, FriendPacket& out);

enum class VisitRole : uint8_t { None, Visitor, Host };
constexpr uint8_t kMaxGuests = 3;  // a visit is 1:1, 1:2 or 1:3 (the host draws which)
struct VisitView {
  VisitRole role = VisitRole::None;
  uint32_t ms = 0;     // time into the visit (see kVisit*)
  char name[64] = "";  // the other gadget (for a host: the first guest)
  uint8_t mascot = 0;
  Gift gift = Gift::None;
  bool turnedAway = false;                  // visitor: the host got busy, coming back early
  uint8_t extra = 0;                        // host: more guests besides the first (0..kMaxGuests-1)
  uint8_t extraMascot[kMaxGuests - 1] = {};  // their colours
};

class FriendPlay {
 public:
  // Our own identity (cheap to call every frame; a change is announced).
  void setSelf(const char* id, const char* name, uint8_t mascot);
  // Every frame. `enabled`: the setting is on and the network is up (off: everything is
  // forgotten and nothing is sent). `flags`: kFriendRoaming | kFriendNapping | kFriendTired.
  // `rnd`: any random number (timing and choice of friend).
  void update(uint32_t nowMs, bool enabled, uint8_t flags, uint32_t rnd);
  void receive(const FriendPacket& p, uint32_t nowMs);
  // The next packet to send, if any.
  bool nextPacket(FriendPacket& out);
  // Demo (/miblo:demo) until `untilMs`: friends are greeted again, the first visit comes within
  // kDemoFirstVisitMs and the next ones every kDemoNextVisitMs or so, instead of minutes apart.
  // Call after update() has seen pet mode start. untilMs == nowMs ends it.
  void demo(uint32_t nowMs, uint32_t untilMs);

  VisitView visit(uint32_t nowMs) const;
  // The friend being greeted right now ("Hi, Nina!"), or nullptr.
  const char* greeting(uint32_t nowMs) const;
  // A friend napping too while we nap, or nullptr.
  const char* napBuddy() const;
  uint8_t count() const;
#ifdef PIO_UNIT_TESTING
  Gift chooseGiftForTest(bool tired, uint32_t rnd) const { return chooseGift(tired, rnd); }
#endif

 private:
  struct Friend {
    bool used = false;
    char id[16] = "";
    char name[64] = "";
    uint8_t mascot = 0;
    uint8_t flags = 0;
    uint32_t seenMs = 0;
    bool greeted = false;
    uint32_t greetedMs = 0;
    uint8_t lastRole = 0;  // our role in the last visit with this friend: 0 none, 1 visitor, 2 host
  };
  Friend* find(const char* id);
  Friend* remember(const FriendPacket& p, uint32_t nowMs);
  void queue(FriendPacket::Type type, const char* to, Gift gift);
  void startVisit(VisitRole role, Friend& f, Gift gift, uint32_t nowMs);
  void endVisit(uint32_t nowMs);
  void scheduleVisit(uint32_t nowMs, uint32_t minMs, uint32_t spanMs);
  bool demoOn(uint32_t nowMs);
  // The first visit after pet mode starts (or when nobody was around to visit).
  void firstVisit(uint32_t nowMs);
  // The next one after a visit: the one that just hosted asks in the first half of the window,
  // so the two take turns going out.
  void nextVisit(uint32_t nowMs, bool hosted);
  Gift chooseGift(bool friendTired, uint32_t rnd) const;
  void queuePoll(uint8_t wanted);
  void turnAway(const char* hostId, uint32_t nowMs);
  void formGroup(uint32_t nowMs);
  bool free() const;
  uint32_t nextRnd();

  Friend friends_[kMaxFriends];
  char id_[16] = "";
  char name_[64] = "";
  uint8_t mascot_ = 0;
  uint8_t flags_ = 0;
  bool enabled_ = false;
  uint32_t rnd_ = 0;
  // outgoing
  FriendPacket out_[3];
  uint8_t outN_ = 0;
  bool announce_ = true;
  bool beaconed_ = false;
  uint32_t beaconMs_ = 0;
  // visits
  VisitView visit_;
  uint32_t visitMs_ = 0;
  char visitWith_[16] = "";
  bool asking_ = false;
  char askTo_[16] = "";
  Gift askGift_ = Gift::None;
  uint32_t askMs_ = 0;
  bool scheduled_ = false;
  uint32_t nextVisitMs_ = 0;
  bool demo_ = false;
  uint32_t demoUntilMs_ = 0;
  // "Who is free?": the first answer to arrive gets the visit (answers come after random delays,
  // so the first one is a fair draw).
  bool polling_ = false;
  uint32_t pollMs_ = 0;
  // Hosting a group: guests still wanted, the invitations out, and the extra guests that came.
  // Organising: the group size drawn (2..4 with us) and the members that answered so far.
  uint8_t groupSize_ = 0;
  char members_[kMaxGuests][16] = {};
  uint8_t membersN_ = 0;
  // Chosen as host: waiting for the guests' VisitOk (the visit starts on the first).
  bool awaiting_ = false;
  uint32_t anchorMs_ = 0;
  Gift hostGift_ = Gift::None;
  char extraIds_[kMaxGuests - 1][16] = {};
  // After answering someone's poll we are reserved for them until they ask (or give up): nobody
  // else can take us meanwhile. During a visit we are locked by the visit itself.
  char reservedFor_[16] = "";
  uint32_t reservedUntilMs_ = 0;
  // Our pending "Here" answer (sent after a random delay).
  bool replying_ = false;
  char replyTo_[16] = "";
  uint32_t replyAtMs_ = 0;
  // Beacons heard per 30 s: about how many Miblos share the network (sets the answer chance).
  uint16_t heard_ = 0;
  uint16_t heardLast_ = 0;
  uint32_t heardAtMs_ = 0;
  // greeting
  char greetName_[64] = "";
  uint32_t greetMs_ = 0;
  bool greetOn_ = false;
};

}  // namespace miblo
