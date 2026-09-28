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

// Mapeia uma tag de idioma (ex.: "pt-br", "zh-Hans-CN", "fr") para Lang.
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
  if (p < len && buf[p] != '-' && buf[p] != '_') return false;  // primária com > 3 letras
  const char* region = (p < len) ? buf + p + 1 : "";
  if (strcmp(primary, "pt") == 0) {
    out = (region[0] == 0 || strcmp(region, "br") == 0) ? Lang::PtBR : Lang::PtPT;
    return true;
  }
  static const struct {
    const char* primary;
    Lang lang;
  } simple[] = {{"en", Lang::En}, {"es", Lang::Es}, {"fr", Lang::Fr}, {"it", Lang::It},
                {"de", Lang::De}, {"ru", Lang::Ru}, {"zh", Lang::Zh}};
  for (const auto& s : simple) {
    if (strcmp(primary, s.primary) == 0) {
      out = s.lang;
      return true;
    }
  }
  return false;
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
      if (qs && (!next || qs < next)) q = (int)(atof(qs + 2) * 1000.0 + 0.5);
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
  const char* p = kLangTables[(int)lang];
  for (int i = 0; i < (int)id; i++) {
    while (mibloRomByte(p)) p++;
    p++;
  }
  size_t len = 0;
  while (mibloRomByte(p + len)) len++;
  if (len >= cap) {
    len = cap - 1;
    // não cortar no meio de uma sequência UTF-8
    while (len > 0 && (mibloRomByte(p + len) & 0xC0) == 0x80) len--;
  }
  for (size_t i = 0; i < len; i++) out[i] = (char)mibloRomByte(p + i);
  out[len] = 0;
}

}  // namespace miblo
