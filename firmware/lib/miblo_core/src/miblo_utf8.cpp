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
  const uint8_t* p = reinterpret_cast<const uint8_t*>(s);
  size_t chars = 0;
  while (*p) {
    const uint8_t b = *p;
    uint32_t cp, least;
    int n;
    if (b < 0x80) cp = b, n = 1, least = 0;
    else if ((b & 0xE0) == 0xC0) cp = b & 0x1F, n = 2, least = 0x80;
    else if ((b & 0xF0) == 0xE0) cp = b & 0x0F, n = 3, least = 0x800;
    else if ((b & 0xF8) == 0xF0) cp = b & 0x07, n = 4, least = 0x10000;
    else return false;  // a lone continuation byte or a 5/6-byte form
    for (int i = 1; i < n; i++) {  // a NUL here (truncated) fails the test too
      if ((p[i] & 0xC0) != 0x80) return false;
      cp = (cp << 6) | (p[i] & 0x3F);
    }
    if (cp < least || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;  // overlong, out of range
    if (cp < 0x20 || (cp >= 0x7F && cp <= 0x9F)) return false;                       // C0, DEL, C1
    p += n;
    if (++chars > maxChars) return false;
  }
  return (size_t)(p - reinterpret_cast<const uint8_t*>(s)) < cap;
}

}  // namespace miblo
