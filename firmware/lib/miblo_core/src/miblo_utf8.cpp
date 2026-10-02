#include "miblo_utf8.h"

namespace miblo {

static int seqLen(uint8_t b) {
  if (b < 0x80) return 1;
  if ((b & 0xE0) == 0xC0) return 2;
  if ((b & 0xF0) == 0xE0) return 3;
  if ((b & 0xF8) == 0xF0) return 4;
  return 0;
}

uint32_t utf8Next(const char*& p) {
  const uint8_t* s = reinterpret_cast<const uint8_t*>(p);
  if (s[0] == 0) return 0;
  int n = seqLen(s[0]);
  if (n == 0) {
    p += 1;
    return 0xFFFD;
  }
  if (n == 1) {
    p += 1;
    return s[0];
  }
  uint32_t cp = s[0] & (0xFF >> (n + 1));
  for (int i = 1; i < n; i++) {
    if ((s[i] & 0xC0) != 0x80) {
      p += 1;
      return 0xFFFD;
    }
    cp = (cp << 6) | (s[i] & 0x3F);
  }
  p += n;
  return cp;
}

size_t utf8Length(const char* s) {
  size_t n = 0;
  const char* p = s;
  while (*p) {
    utf8Next(p);
    n++;
  }
  return n;
}

size_t utf8Copy(char* dst, size_t cap, const char* src, size_t maxChars) {
  if (cap == 0) return 0;
  size_t used = 0;
  size_t chars = 0;
  const char* p = src ? src : "";
  while (*p && chars < maxChars) {
    const char* start = p;
    utf8Next(p);
    size_t len = (size_t)(p - start);
    if (used + len + 1 > cap) break;
    for (size_t i = 0; i < len; i++) dst[used + i] = start[i];
    used += len;
    chars++;
  }
  dst[used] = 0;
  return used;
}

bool typedText(const char* s, size_t cap, size_t maxChars) {
  if (!s) return false;
  size_t bytes = 0;
  for (const char* p = s; *p; p++, bytes++) {
    if ((uint8_t)*p < 0x20 || *p == 0x7F) return false;
  }
  return bytes < cap && utf8Length(s) <= maxChars;
}

}  // namespace miblo
