#pragma once
#include <stddef.h>

// Time zones: the config stores an IANA name ("America/Sao_Paulo"); the device turns it into the
// POSIX TZ rule that configTime() needs through a table in flash (miblo_tz_table, generated).
// Configs saved before the table existed hold a POSIX rule directly ("<-03>3"); those keep working.
namespace miblo {

// Looks up an IANA name (exact, case-sensitive). On success copies the POSIX rule into `out`.
bool tzLookup(const char* iana, char* out, size_t cap);
inline bool tzIsKnown(const char* iana) {
  char tmp[48];
  return tzLookup(iana, tmp, sizeof(tmp));
}
// True when `s` has the shape of a POSIX TZ rule: a zone abbreviation (3+ letters or "<...>")
// followed by an offset ("UTC0", "<-03>3", "WET0WEST,M3.5.0/1,M10.5.0"). Printable ASCII only.
bool tzLooksPosix(const char* s);
// The rule to apply for a stored `tz`: known IANA name → table; POSIX rule → as-is; else "UTC0".
void tzResolve(const char* stored, char* out, size_t cap);

}  // namespace miblo
