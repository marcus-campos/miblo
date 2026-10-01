#include "miblo_i18n.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "miblo_rom.h"

namespace miblo {

static const char* const kCodes[] = {"en", "pt-BR", "pt-PT", "es", "fr", "it", "de", "ru", "zh"};
static const char* const kNames[] = {"English",  "Português (Brasil)", "Português (Portugal)",
                                     "Español",  "Français",           "Italiano",
                                     "Deutsch",  "Русский",            "中文"};

const char* langCode(Lang lang) {
  return lang < Lang::Count ? kCodes[(int)lang] : kCodes[0];
}

const char* langName(Lang lang) {
  return lang < Lang::Count ? kNames[(int)lang] : kNames[0];
}

bool langFromCode(const char* code, Lang& out) {
  if (!code) return false;
  for (int i = 0; i < (int)Lang::Count; i++) {
    if (strcasecmp(code, kCodes[i]) == 0) {
      out = (Lang)i;
      return true;
    }
  }
  return false;
}

// Maps a language tag (e.g. "pt-br", "zh-Hans-CN", "fr") to Lang.
static bool matchTag(const char* tag, size_t len, Lang& out) {
  char buf[16];
  if (len == 0 || len >= sizeof(buf)) return false;
  for (size_t i = 0; i < len; i++) buf[i] = (char)tolower((unsigned char)tag[i]);
  buf[len] = 0;
  char primary[4] = {0};
  size_t p = 0;
  while (p < len && p < 3 && buf[p] != '-' && buf[p] != '_') {
    primary[p] = buf[p];
    p++;
  }
  if (p < len && buf[p] != '-' && buf[p] != '_') return false;  // primary tag longer than 3 letters
  const char* region = (p < len) ? buf + p + 1 : "";
  if (strcmp(primary, "pt") == 0) {
    out = (region[0] == 0 || strcmp(region, "br") == 0) ? Lang::PtBR : Lang::PtPT;
    return true;
  }
  // Two-letter tags and their language, in flash (MIBLO_ROM).
  static const char kSimple[][3] MIBLO_ROM = {{'e', 'n', (char)Lang::En}, {'e', 's', (char)Lang::Es},
                                              {'f', 'r', (char)Lang::Fr}, {'i', 't', (char)Lang::It},
                                              {'d', 'e', (char)Lang::De}, {'r', 'u', (char)Lang::Ru},
                                              {'z', 'h', (char)Lang::Zh}};
  if (!primary[0] || !primary[1] || primary[2]) return false;
  for (const auto& s : kSimple) {
    if (primary[0] == (char)mibloRomByte(s) && primary[1] == (char)mibloRomByte(s + 1)) {
      out = (Lang)mibloRomByte(s + 2);
      return true;
    }
  }
  return false;
}

// "q=0.8" weight x 1000 (rounded to 3 decimals), without atof: pulling the libc float parser
// in for this costs ~4 KB of flash.
static int qValue(const char* s) {
  while (*s == ' ') s++;
  int v = 0;
  while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
  v *= 1000;
  if (*s == '.') {
    s++;
    for (int scale = 100; scale > 0 && *s >= '0' && *s <= '9'; scale /= 10) v += (*s++ - '0') * scale;
    if (*s >= '5' && *s <= '9') v++;  // 4th decimal: round half up
  }
  return v;
}

Lang negotiateLang(const char* header) {
  Lang best = Lang::En;
  int bestQ = -1;
  if (!header) return best;
  const char* p = header;
  while (*p) {
    while (*p == ' ' || *p == ',') p++;
    const char* tag = p;
    while (*p && *p != ';' && *p != ',' && *p != ' ') p++;
    size_t tagLen = (size_t)(p - tag);
    int q = 1000;
    while (*p == ' ') p++;
    if (*p == ';') {
      const char* qs = strstr(p, "q=");
      const char* next = strchr(p, ',');
      if (qs && (!next || qs < next)) q = qValue(qs + 2);
    }
    while (*p && *p != ',') p++;
    Lang l;
    if (tagLen > 0 && q > bestQ && matchTag(tag, tagLen, l)) {
      best = l;
      bestQ = q;
    }
  }
  return best;
}

void tr(Lang lang, S id, char* out, size_t cap) {
  if (cap == 0) return;
  if (lang >= Lang::Count) lang = Lang::En;
  if (id >= S::Count) {
    out[0] = 0;
    return;
  }
  const char* p;
  mibloRomCopy(&p, &kLangTables[(int)lang], sizeof(p));  // the table itself is in flash too
  for (int i = 0; i < (int)id; i++) {
    while (mibloRomByte(p)) p++;
    p++;
  }
  size_t len = 0;
  while (mibloRomByte(p + len)) len++;
  if (len >= cap) {
    len = cap - 1;
    // don't cut in the middle of a UTF-8 sequence
    while (len > 0 && (mibloRomByte(p + len) & 0xC0) == 0x80) len--;
  }
  for (size_t i = 0; i < len; i++) out[i] = (char)mibloRomByte(p + i);
  out[len] = 0;
}

Lang pageLanguage(bool paired, bool langSet, Lang stored, Lang browser, bool& store) {
  store = false;
  if (paired) return browser;
  if (langSet) return stored;
  store = browser != stored;
  return browser;
}

}  // namespace miblo
