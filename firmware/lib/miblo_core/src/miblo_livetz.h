#pragma once
#include <stddef.h>
#include <stdint.h>

#include "miblo_snapshot.h"

// Live time zone offsets. The firmware's table (miblo_tz_table) is frozen at build time, and
// countries change their rules every year; the bridge works out, from the computer's tz database
// (kept current by its updates), each of the gadget's zones' offset now, its next change and the
// offset after it (snapshot "tz"). While those are fresh they win over the table, for the main
// clock (liveRule) and the second clock (zoneHHMM).
namespace miblo {

// How long live offsets are trusted after the snapshot that carried them; then it's the table
// again (a computer gone for two weeks may have missed a rule change).
constexpr uint32_t kLiveTzMaxAgeSec = 14 * 86400;

class LiveTz {
 public:
  // Takes the zones of a parsed snapshot. One without any (an older bridge, the alerts-only one,
  // another computer) keeps the last ones: they stay valid on their own until they go stale.
  void observe(const Snapshot& s);
  // Seconds east of UTC in `zone` (an IANA name) at `epoch`: `off` until `next`, `noff` from then
  // on. False when there is nothing live for that zone, the values are stale, or epoch is 0.
  bool offset(const char* zone, uint32_t epoch, int32_t& east) const;

 private:
  LiveZone zones_[kMaxLiveZones];
  uint8_t count_ = 0;
  uint32_t at_ = 0;  // the snapshot's `now` (epoch s) when they arrived
};

// The POSIX TZ rule for a fixed offset: "<-03>3", "<+0545>-5:45", "<+00>0" (cap >= 14).
void fixedRule(int32_t eastSec, char* out, size_t cap);
// The rule the gadget's clock runs on at `epoch` for the stored zone `stored` (Config::tz): its live
// offset as a fixed rule while there is one, else the table's rule (tzResolve).
void liveRule(const char* stored, const LiveTz& live, uint32_t epoch, char* out, size_t cap);

}  // namespace miblo
