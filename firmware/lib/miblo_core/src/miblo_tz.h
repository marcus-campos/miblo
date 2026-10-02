#pragma once
#include <stddef.h>

#include "miblo_tz_table.h"

// Time zones: the config stores an IANA name ("America/Sao_Paulo"); the device turns it into the
// POSIX TZ rule that configTime() needs through a table in flash (miblo_tz_table, generated).
// Configs saved before the table existed hold a POSIX rule directly ("<-03>3"); those keep working.
namespace miblo {

// The IANA names of the table, in order (they are stored front-coded, see gen_tz_table.py):
//   TzNames n; while (n.next()) use(n.name);
struct TzNames {
  // Set member by member: a default member initializer would make the compiler copy a 40-byte
  // template from .rodata (RAM on the ESP8266).
  TzNames() : index((size_t)-1), p_(kTzNames) { name[0] = 0; }
  char name[kTzNameMax + 1];
  size_t index;  // of `name` in the table
  // Moves to the next name; false after the last one.
  bool next();

 private:
  const char* p_;
};

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
