#include "miblo_tz.h"

#include <ctype.h>
#include <string.h>

#include "miblo_rom.h"
#include "miblo_tz_table.h"

namespace miblo {

bool tzLookup(const char* iana, char* out, size_t cap) {
  if (!iana || !iana[0] || !out || cap == 0) return false;
  for (const char* q = iana; *q; q++) {
    if (*q < 0x21 || *q > 0x7E) return false;  // also keeps '\n' from matching across lines
  }
  const char* p = kTzNames;
  size_t index = 0;
  bool found = false;
  while (index < kTzCount) {
    // Compare this line with `iana` byte by byte, then skip to the next line.
    size_t i = 0;
    while (iana[i] && mibloRomByte(p + i) == (uint8_t)iana[i]) i++;
    uint8_t c = mibloRomByte(p + i);
    if (!iana[i] && c == '\n') {
      found = true;
      break;
    }
    while (mibloRomByte(p) != '\n') p++;
    p++;
    index++;
  }
  if (!found) return false;
  const uint8_t rule = mibloRomByte((const char*)&kTzRule[index]);
  const char* r = kTzRules;
  for (size_t k = 0; k < rule; k++) {
    while (mibloRomByte(r) != '\n') r++;
    r++;
  }
  size_t len = 0;
  while (mibloRomByte(r + len) != '\n') len++;
  if (len + 1 > cap) return false;
  for (size_t k = 0; k < len; k++) out[k] = (char)mibloRomByte(r + k);
  out[len] = 0;
  return true;
}

bool tzLooksPosix(const char* s) {
  if (!s || !s[0]) return false;
  for (const char* q = s; *q; q++) {
    if (*q < 0x21 || *q > 0x7E) return false;
  }
  const char* p = s;
  if (*p == '<') {
    const char* end = strchr(p, '>');
    if (!end || end == p + 1) return false;
    p = end + 1;
  } else {
    int n = 0;
    while (isalpha((unsigned char)*p)) {
      p++;
      n++;
    }
    if (n < 3) return false;
  }
  if (*p == '+' || *p == '-') p++;
  return isdigit((unsigned char)*p) != 0;
}

void tzResolve(const char* stored, char* out, size_t cap) {
  if (!out || cap == 0) return;
  if (tzLookup(stored, out, cap)) return;
  if (tzLooksPosix(stored) && strlen(stored) < cap) {
    strcpy(out, stored);
    return;
  }
  strncpy(out, "UTC0", cap - 1);
  out[cap - 1] = 0;
}

}  // namespace miblo
