// Deterministic mutation fuzzer (no libFuzzer needed: Apple clang ships without it).
//
//   miblo_fuzz list
//   miblo_fuzz <target|all> [-n ITERATIONS] [-seed N] [-corpus DIR]... [-seeds DIR] [-out DIR] [-timeout SEC]
//   miblo_fuzz <target> -repro FILE...
//
// Each iteration takes an input from the pool (the target's built-in seeds, the files in
// <corpus>/<target>/ for each -corpus, the files in -seeds DIR, and a few earlier mutants that got
// past parsing), applies 1..8 random mutations (bit flips, interesting bytes and numbers,
// dictionary tokens, insert/delete/duplicate/splice, deep nesting) and runs the target. The
// random generator is seeded from -seed and the target's name, so a run is reproducible. On a
// crash, sanitizer report, failed invariant or hang (-timeout, default 5 s per input) the input
// is written to <out>/crash-<target>-<iteration>.bin (timeout-... for a hang); -repro replays it.
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <string>
#include <vector>

#include "fuzz.h"

extern "C" void __sanitizer_set_death_callback(void (*callback)(void)) __attribute__((weak));

namespace fuzz {

// ---- registry and helpers shared with the targets ----

static const Target* g_targets[64];
static size_t g_count = 0;

Registrar::Registrar(const Target& t) {
  if (g_count < sizeof(g_targets) / sizeof(g_targets[0])) g_targets[g_count++] = new Target(t);
}
const Target* const* targets(size_t& count) {
  count = g_count;
  return g_targets;
}

static unsigned long long g_reached = 0;
void reached() { g_reached++; }

void fail(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "\n==fuzz== INVARIANT FAILED: ");
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  va_end(ap);
  abort();
}

ExactBuf::ExactBuf(const void* src, size_t len) : p(static_cast<char*>(malloc(len ? len : 1))), n(len) {
  if (len) memcpy(p, src, len);
}
ExactBuf::~ExactBuf() { free(p); }

CStr::CStr(const void* src, size_t n) {
  const void* nul = memchr(src, 0, n);
  const size_t len = nul ? (size_t)(static_cast<const char*>(nul) - static_cast<const char*>(src)) : n;
  s = static_cast<char*>(malloc(len + 1));
  memcpy(s, src, len);
  s[len] = 0;
}
CStr::~CStr() { free(s); }

bool plainIdent(const char* s) {
  if (!s || !*s) return false;
  size_t n = 0;
  for (; s[n]; n++) {
    const char c = s[n];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return false;
  }
  return n <= 32;
}

bool validUtf8(const char* s) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(s);
  while (*p) {
    const uint8_t b = *p;
    uint32_t cp, least;
    int n;
    if (b < 0x80) cp = b, n = 1, least = 0;
    else if ((b & 0xE0) == 0xC0) cp = b & 0x1F, n = 2, least = 0x80;
    else if ((b & 0xF0) == 0xE0) cp = b & 0x0F, n = 3, least = 0x800;
    else if ((b & 0xF8) == 0xF0) cp = b & 0x07, n = 4, least = 0x10000;
    else return false;
    for (int i = 1; i < n; i++) {
      if ((p[i] & 0xC0) != 0x80) return false;
      cp = (cp << 6) | (p[i] & 0x3F);
    }
    if (cp < least || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
    p += n;
  }
  return true;
}

}  // namespace fuzz

using fuzz::Target;
using Bytes = std::vector<uint8_t>;

// ---- crash capture ----

static const Target* g_cur = nullptr;
static const uint8_t* g_in = nullptr;
static size_t g_inLen = 0;
static unsigned long long g_iter = 0;
static char g_outDir[512] = ".";
static bool g_saving = true;

static void writeInput(const char* kind) {
  if (!g_saving || !g_cur || !g_in) return;
  g_saving = false;  // once (a sanitizer death runs the callback, then raises a signal)
  char path[768];
  snprintf(path, sizeof(path), "%s/%s-%s-%llu.bin", g_outDir, kind, g_cur->name, g_iter);
  const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd >= 0) {
    size_t off = 0;
    while (off < g_inLen) {
      const ssize_t w = write(fd, g_in + off, g_inLen - off);
      if (w <= 0) break;
      off += (size_t)w;
    }
    close(fd);
    fprintf(stderr, "==fuzz== input saved to %s (%zu bytes)\n", path, g_inLen);
  }
}

static void onDeath() { writeInput("crash"); }

static void onSignal(int sig) {
  writeInput(sig == SIGALRM ? "timeout" : "crash");
  if (sig == SIGALRM) {
    static const char msg[] = "==fuzz== TIMEOUT: one input ran longer than the limit\n";
    (void)!write(2, msg, sizeof(msg) - 1);
    _exit(3);
  }
  signal(sig, SIG_DFL);
  raise(sig);
}

static void armTimer(unsigned sec) {
  struct itimerval it = {};
  it.it_value.tv_sec = sec;
  setitimer(ITIMER_REAL, &it, nullptr);
}

// ---- random numbers (xorshift64*) ----

struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed) : s(seed ? seed : 0x9E3779B97F4A7C15ull) {}
  uint64_t next() {
    s ^= s >> 12;
    s ^= s << 25;
    s ^= s >> 27;
    return s * 2685821657736338717ull;
  }
  uint32_t below(uint32_t n) { return n ? (uint32_t)(next() % n) : 0; }
  bool one(uint32_t inN) { return below(inN) == 0; }
};

static uint64_t hashName(const char* s) {
  uint64_t h = 1469598103934665603ull;
  for (; *s; s++) h = (h ^ (uint8_t)*s) * 1099511628211ull;
  return h;
}

// ---- seeds ----

static int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static Bytes seedBytes(const char* s) {
  Bytes b;
  if (strncmp(s, "hex:", 4) == 0) {
    for (const char* p = s + 4; p[0] && p[1];) {
      const int hi = hexVal(p[0]), lo = hexVal(p[1]);
      if (hi < 0 || lo < 0) {
        p++;
        continue;
      }
      b.push_back((uint8_t)(hi << 4 | lo));
      p += 2;
    }
  } else {
    b.assign(s, s + strlen(s));
  }
  return b;
}

static bool readFile(const char* path, Bytes& out) {
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  out.clear();
  uint8_t buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.insert(out.end(), buf, buf + n);
  fclose(f);
  return true;
}

static void loadCorpusDir(const char* dir, std::vector<Bytes>& pool) {
  DIR* d = opendir(dir);
  if (!d) return;
  std::vector<std::string> names;
  while (struct dirent* e = readdir(d)) {
    if (e->d_name[0] != '.') names.push_back(e->d_name);
  }
  closedir(d);
  std::sort(names.begin(), names.end());  // a stable order: the run stays reproducible
  for (const std::string& n : names) {
    Bytes b;
    if (readFile((std::string(dir) + "/" + n).c_str(), b)) pool.push_back(b);
  }
}

// ---- mutations ----

static const char* const kNumbers[] = {
    "0", "-0", "1", "-1", "127", "128", "255", "256", "-129", "32767", "32768", "-32769", "65535", "65536",
    "2147483647", "2147483648", "-2147483649", "4294967295", "4294967296", "9223372036854775807",
    "9223372036854775808", "-9223372036854775809", "18446744073709551616", "1e999", "-1e999", "1e-400",
    "1.7976931348623157e308", "3.4e38", "3.5e38", "-3.5e38", "NaN", "-NaN", "Infinity", "-Infinity", "0.5",
    "99.5", "100.0001", "1439", "1440", "0x10", "1e9", "0.0000001", "123456789012345678901234567890",
    "-0.0", "1E+2", "00", "1.", ".5", "+1", "true", "false", "null"};

static const uint8_t kBytes[] = {0x00, 0x01, 0x0A, 0x0D, 0x1F, 0x20, 0x22, 0x27, 0x2C, 0x2D, 0x2E, 0x30, 0x3A,
                                 0x3C, 0x3E, 0x5B, 0x5C, 0x5D, 0x7B, 0x7D, 0x7F, 0x80, 0xBF, 0xC0, 0xC1, 0xC3,
                                 0xE2, 0xED, 0xEF, 0xF0, 0xF4, 0xF5, 0xF8, 0xFE, 0xFF};

static const char* const kGeneric[] = {
    "\"", "\\", "\\u0000", "\\ud800", "\\udfff", "\\ud83d\\ude00", "\\u00e9", "\xc3\xa9", "\xe9\xa1\xb9",
    "\xf0\x9f\x98\x80", "\xed\xa0\x80", "\xc0\x80", "\xf4\x90\x80\x80", "\xef\xbf\xbd", "\xe2\x80\xa8",
    "\x7f", "\x1b[2J", "{", "}", "[", "]", ":", ",", "{}", "[]", "\"\"", "//", "/*", "*/", " ", "\t",
    "\r\n", "%s%n", "<script>", "' OR 1=1", "../../", "A"};

static void insertAt(Bytes& b, size_t pos, const uint8_t* p, size_t n) { b.insert(b.begin() + pos, p, p + n); }
static void insertStr(Bytes& b, size_t pos, const char* s) {
  insertAt(b, pos, reinterpret_cast<const uint8_t*>(s), strlen(s));
}

static const char* pickToken(Rng& r, const Target& t) {
  size_t nd = 0;
  if (t.dict) {
    while (t.dict[nd]) nd++;
  }
  const size_t ng = sizeof(kGeneric) / sizeof(kGeneric[0]);
  if (nd && !r.one(3)) return t.dict[r.below((uint32_t)nd)];
  return kGeneric[r.below((uint32_t)ng)];
}

// Replaces a run of digits (or anything, if none is near) with an interesting number.
static void mutateNumber(Rng& r, Bytes& b) {
  const char* num = kNumbers[r.below(sizeof(kNumbers) / sizeof(kNumbers[0]))];
  if (b.empty()) {
    insertStr(b, 0, num);
    return;
  }
  size_t start = r.below((uint32_t)b.size());
  size_t i = start;
  while (i < b.size() && !((b[i] >= '0' && b[i] <= '9') || b[i] == '-')) i++;
  if (i == b.size()) {
    insertStr(b, start, num);
    return;
  }
  size_t e = i;
  while (e < b.size() && ((b[e] >= '0' && b[e] <= '9') || b[e] == '-' || b[e] == '.' || b[e] == 'e' || b[e] == 'E' ||
                          b[e] == '+'))
    e++;
  b.erase(b.begin() + i, b.begin() + e);
  insertStr(b, i, num);
}

// JSON-aware: the end of the value that starts at `i` (a string, an object/array, or a scalar).
static size_t valueEnd(const Bytes& b, size_t i) {
  const size_t n = b.size();
  if (i >= n) return n;
  if (b[i] == '"') {
    for (size_t k = i + 1; k < n; k++) {
      if (b[k] == '\\') k++;
      else if (b[k] == '"') return k + 1;
    }
    return n;
  }
  if (b[i] == '{' || b[i] == '[') {
    int depth = 0;
    bool str = false;
    for (size_t k = i; k < n; k++) {
      if (str) {
        if (b[k] == '\\') k++;
        else if (b[k] == '"') str = false;
      } else if (b[k] == '"') str = true;
      else if (b[k] == '{' || b[k] == '[') depth++;
      else if ((b[k] == '}' || b[k] == ']') && --depth == 0) return k + 1;
    }
    return n;
  }
  size_t k = i;
  while (k < n && b[k] != ',' && b[k] != '}' && b[k] != ']' && b[k] != '\n') k++;
  return k;
}

// A JSON value worth trying in place of another one.
static std::string jsonValue(Rng& r, const Target& t) {
  static const char* const fixed[] = {
      "0", "-1", "1", "255", "256", "65535", "65536", "2147483647", "2147483648", "-2147483649", "4294967295",
      "4294967296", "9223372036854775808", "1e999", "-1e999", "1e-400", "0.5", "-0.0", "3.5e38", "1.7976931348623157e308",
      "true", "false", "null", "{}", "[]", "[1,2,3]", "{\"a\":1}", "\"\"", "\" \"", "\"x\"", "\"\\u0000\"",
      "\"\\ud800\"", "\"\\udc00x\"", "\"\\ud83d\\ude00\"", "\"\xff\xfe\"", "\"\xc2\x85\"",
      "\"\xef\xbf\xbd\"", "\"\xe2\x80\xae\"", "\"\xc0\x80\"", "\"\xf4\x90\x80\x80\"", "\"a\\nb\"",
      "\"\\\"\"", "\"09:45\"", "\"23:59\"", "\"24:00\"", "\"2026-02-29\"", "\"2024-02-29\"", "\"02-29\"",
      "\"12-31\"", "\"00-00\""};
  const uint32_t pick = r.below(10);
  if (pick < 6) return fixed[r.below(sizeof(fixed) / sizeof(fixed[0]))];
  if (pick == 6) {  // a long string, of letters or of a multi-byte character
    const char* const unit[] = {"A", "\xe9\xa1\xb9", "\xc3\xa9", "\xf0\x9f\x98\x80", " ", "\\u00e9"};
    const char* u = unit[r.below(6)];
    std::string v = "\"";
    const size_t times = 1 + r.below(r.one(4) ? 400 : 64);
    for (size_t i = 0; i < times; i++) v += u;
    return v + "\"";
  }
  if (pick == 7) {  // nested arrays/objects
    const size_t depth = 1 + r.below(12);
    std::string v;
    for (size_t i = 0; i < depth; i++) v += r.one(2) ? "[" : "{\"k\":";
    v += "1";
    // close in reverse (a mismatch now and then is fine: malformed input is the point)
    for (size_t i = 0; i < depth; i++) v += r.one(2) ? "]" : "}";
    return v;
  }
  // A quoted dictionary token of the target (keys and string values).
  if (t.dict) {
    size_t nd = 0;
    while (t.dict[nd]) nd++;
    if (nd) {
      std::string tok = t.dict[r.below((uint32_t)nd)];
      if (!tok.empty() && tok.back() == ':') tok.pop_back();
      if (!tok.empty() && tok[0] == '"') return tok;
    }
  }
  return "\"x\"";
}

// Replaces the value after a random ':' (or a whole array element after '[' / ',').
static bool mutateJsonValue(Rng& r, Bytes& b, const Target& t) {
  const size_t n = b.size();
  if (!n) return false;
  const size_t start = r.below((uint32_t)n);
  for (size_t k = 0; k < n; k++) {
    const size_t i = (start + k) % n;
    if (b[i] != ':' && b[i] != '[' && b[i] != ',') continue;
    size_t v = i + 1;
    while (v < n && b[v] == ' ') v++;
    const size_t e = valueEnd(b, v);
    const std::string val = jsonValue(r, t);
    b.erase(b.begin() + v, b.begin() + e);
    insertStr(b, v, val.c_str());
    return true;
  }
  return false;
}

// Replaces a key (a string right before ':') with a dictionary key, or duplicates a member.
static bool mutateJsonKey(Rng& r, Bytes& b, const Target& t) {
  const size_t n = b.size();
  if (!n) return false;
  const size_t start = r.below((uint32_t)n);
  for (size_t k = 0; k < n; k++) {
    const size_t i = (start + k) % n;
    if (b[i] != ':' || i == 0 || b[i - 1] != '"') continue;
    size_t q = i - 1;
    while (q > 0 && b[q - 1] != '"') q--;
    if (q == 0) return false;
    const size_t keyStart = q - 1;
    if (r.one(2)) {  // duplicate "key":value right after itself
      const size_t e = valueEnd(b, i + 1);
      Bytes member(b.begin() + keyStart, b.begin() + e);
      member.insert(member.begin(), ',');
      insertAt(b, e, member.data(), member.size());
      return true;
    }
    const char* tok = pickToken(r, t);
    std::string key = tok;
    if (key.empty() || key[0] != '"') return false;
    if (key.back() == ':') key.pop_back();
    b.erase(b.begin() + keyStart, b.begin() + i);
    insertStr(b, keyStart, key.c_str());
    return true;
  }
  return false;
}

static void mutate(Rng& r, Bytes& b, const Target& t, const std::vector<Bytes>& pool) {
  const size_t n = b.size();
  // JSON inputs: most mutations keep the document well-formed, so they reach the logic behind it.
  if (memchr(b.data(), ':', n) && r.below(10) < 6) {
    if (r.below(4) ? mutateJsonValue(r, b, t) : mutateJsonKey(r, b, t)) return;
  }
  switch (r.below(14)) {
    case 0:  // flip a bit
      if (n) b[r.below((uint32_t)n)] ^= (uint8_t)(1u << r.below(8));
      break;
    case 1:  // random byte
      if (n) b[r.below((uint32_t)n)] = (uint8_t)r.next();
      break;
    case 2:  // interesting byte
      if (n) b[r.below((uint32_t)n)] = kBytes[r.below(sizeof(kBytes))];
      break;
    case 3: {  // delete a range
      if (!n) break;
      const size_t at = r.below((uint32_t)n);
      size_t len = 1 + r.below(r.one(4) ? (uint32_t)(n - at) : 8);
      if (at + len > n) len = n - at;
      b.erase(b.begin() + at, b.begin() + at + len);
      break;
    }
    case 4: {  // insert random bytes
      const size_t at = r.below((uint32_t)n + 1);
      const size_t len = 1 + r.below(8);
      for (size_t i = 0; i < len; i++) b.insert(b.begin() + at, (uint8_t)r.next());
      break;
    }
    case 5: {  // duplicate a chunk elsewhere
      if (!n) break;
      const size_t from = r.below((uint32_t)n);
      size_t len = 1 + r.below(32);
      if (from + len > n) len = n - from;
      Bytes chunk(b.begin() + from, b.begin() + from + len);
      insertAt(b, r.below((uint32_t)b.size() + 1), chunk.data(), chunk.size());
      break;
    }
    case 6:
    case 7:  // insert a dictionary token
      insertStr(b, r.below((uint32_t)n + 1), pickToken(r, t));
      break;
    case 8: {  // overwrite with a dictionary token
      const char* tok = pickToken(r, t);
      const size_t len = strlen(tok);
      const size_t at = r.below((uint32_t)n + 1);
      for (size_t i = 0; i < len; i++) {
        if (at + i < b.size()) b[at + i] = (uint8_t)tok[i];
        else b.push_back((uint8_t)tok[i]);
      }
      break;
    }
    case 9: {  // splice with another pool entry
      const Bytes& o = pool[r.below((uint32_t)pool.size())];
      if (o.empty()) break;
      const size_t cut = r.below((uint32_t)n + 1);
      const size_t from = r.below((uint32_t)o.size());
      b.resize(cut);
      b.insert(b.end(), o.begin() + from, o.end());
      break;
    }
    case 10:  // truncate
      if (n) b.resize(r.below((uint32_t)n));
      break;
    case 11:
    case 12:  // an interesting number
      mutateNumber(r, b);
      break;
    case 13: {  // repeat a short chunk many times (deep nesting, long strings, many entries)
      const char* const reps[] = {"[", "{\"a\":", "\"x\",", "{},", "[[", "\xe9\xa1\xb9", "A", "\\\\", "0,"};
      const char* rep = reps[r.below(sizeof(reps) / sizeof(reps[0]))];
      const size_t times = 8 + r.below(r.one(8) ? 2000 : 64);
      const size_t at = r.below((uint32_t)n + 1);
      std::string s;
      for (size_t i = 0; i < times; i++) s += rep;
      insertStr(b, at, s.c_str());
      break;
    }
  }
}

// ---- running ----

static void runOne(const Target& t, const Bytes& in, unsigned timeoutSec) {
  g_in = in.data();
  g_inLen = in.size();
  if (timeoutSec) armTimer(timeoutSec);
  t.run(in.empty() ? reinterpret_cast<const uint8_t*>("") : in.data(), in.size());
  if (timeoutSec) armTimer(0);
}

static int fuzzTarget(const Target& t, unsigned long long iterations, uint64_t seed,
                      const std::vector<const char*>& corpora, const char* flatSeeds, unsigned timeoutSec) {
  g_cur = &t;
  std::vector<Bytes> pool;
  if (t.seeds) {
    for (const char* const* s = t.seeds; *s; s++) pool.push_back(seedBytes(*s));
  }
  for (const char* c : corpora) loadCorpusDir((std::string(c) + "/" + t.name).c_str(), pool);
  if (flatSeeds) loadCorpusDir(flatSeeds, pool);
  if (pool.empty()) pool.push_back(Bytes());
  const size_t fixed = pool.size();
  Rng r(seed ^ hashName(t.name));
  const time_t t0 = time(nullptr);
  size_t maxSeen = 0;
  fuzz::g_reached = 0;

  // Every seed as it is first.
  for (size_t i = 0; i < fixed; i++) {
    g_iter = i;
    runOne(t, pool[i], timeoutSec);
  }
  Bytes in;
  for (g_iter = 0; g_iter < iterations; g_iter++) {
    // Half the time a seed, else any pool entry (seeds and mutants that reached the logic).
    const Bytes& base = pool[r.one(2) ? r.below((uint32_t)fixed) : r.below((uint32_t)pool.size())];
    in = base;
    // Mostly one or two mutations (close to valid input), sometimes a pile of them.
    const uint32_t dice = r.below(10);
    const uint32_t rounds = dice < 5 ? 1 : dice < 8 ? 2 + r.below(2) : 4 + r.below(13);
    for (uint32_t k = 0; k < rounds; k++) mutate(r, in, t, pool);
    // Inputs stay under the target's limit, except one in 512 (oversized/huge input paths): an
    // input that grew past it is mostly cut (keeping its start), else redone with one mutation.
    if (in.size() > t.maxLen && !r.one(512)) {
      if (r.one(2)) in.resize(t.maxLen);
      else {
        in = base;
        mutate(r, in, t, pool);
        if (in.size() > t.maxLen) in.resize(t.maxLen);
      }
    }
    if (in.size() > 256 * 1024) in.resize(256 * 1024);
    if (in.size() > maxSeen) maxSeen = in.size();
    const unsigned long long before = fuzz::g_reached;
    runOne(t, in, timeoutSec);
    // Keep a few mutants that got past parsing, for deeper chains (bounded; the seeds stay).
    if (fuzz::g_reached != before && r.one(8)) {
      if (pool.size() < fixed + 256) pool.push_back(in);
      else pool[fixed + r.below(256)] = in;
    }
  }
  const unsigned long long total = iterations + fixed;
  printf("%-14s %10llu iterations  seeds %3zu  max input %6zu B  deep %3llu%%  %4lds  ok\n", t.name, iterations, fixed,
         maxSeen, total ? fuzz::g_reached * 100 / total : 0, (long)(time(nullptr) - t0));
  fflush(stdout);
  g_cur = nullptr;
  return 0;
}

static const Target* findTarget(const char* name) {
  size_t n;
  const Target* const* all = fuzz::targets(n);
  for (size_t i = 0; i < n; i++) {
    if (strcmp(all[i]->name, name) == 0) return all[i];
  }
  return nullptr;
}

static int usage() {
  fprintf(stderr,
          "usage: miblo_fuzz list\n"
          "       miblo_fuzz <target|all> [-n ITERATIONS] [-seed N] [-corpus DIR]... [-seeds DIR] [-out DIR]\n"
          "                  [-timeout SEC]\n"
          "       miblo_fuzz <target> -repro FILE...\n");
  return 2;
}

#ifndef MIBLO_LIBFUZZER
int main(int argc, char** argv) {
  if (argc < 2) return usage();
  size_t count;
  const Target* const* all = fuzz::targets(count);
  if (strcmp(argv[1], "list") == 0) {
    for (size_t i = 0; i < count; i++) printf("%s\n", all[i]->name);
    return 0;
  }
  unsigned long long iterations = 200000;
  uint64_t seed = 1;
  std::vector<const char*> corpora;
  const char* flatSeeds = nullptr;
  unsigned timeoutSec = 5;
  std::vector<const char*> repro;
  for (int i = 2; i < argc; i++) {
    const bool more = i + 1 < argc;
    if (!strcmp(argv[i], "-n") && more) iterations = strtoull(argv[++i], nullptr, 10);
    else if (!strcmp(argv[i], "-seed") && more) seed = strtoull(argv[++i], nullptr, 10);
    else if (!strcmp(argv[i], "-corpus") && more) corpora.push_back(argv[++i]);
    else if (!strcmp(argv[i], "-seeds") && more) flatSeeds = argv[++i];
    else if (!strcmp(argv[i], "-out") && more) snprintf(g_outDir, sizeof(g_outDir), "%s", argv[++i]);
    else if (!strcmp(argv[i], "-timeout") && more) timeoutSec = (unsigned)atoi(argv[++i]);
    else if (!strcmp(argv[i], "-repro")) {
      while (++i < argc) repro.push_back(argv[i]);
    } else return usage();
  }
  mkdir(g_outDir, 0755);
  if (__sanitizer_set_death_callback) __sanitizer_set_death_callback(onDeath);
  signal(SIGSEGV, onSignal);
  signal(SIGBUS, onSignal);
  signal(SIGABRT, onSignal);
  signal(SIGFPE, onSignal);
  signal(SIGILL, onSignal);
  signal(SIGALRM, onSignal);

  if (!repro.empty()) {
    const Target* t = findTarget(argv[1]);
    if (!t) return usage();
    g_cur = t;
    g_saving = false;
    for (const char* path : repro) {
      Bytes b;
      if (!readFile(path, b)) {
        fprintf(stderr, "cannot read %s\n", path);
        return 2;
      }
      printf("replaying %s (%zu bytes) on %s\n", path, b.size(), t->name);
      fflush(stdout);
      runOne(*t, b, timeoutSec);
    }
    printf("no crash\n");
    return 0;
  }
  if (strcmp(argv[1], "all") == 0) {
    for (size_t i = 0; i < count; i++) fuzzTarget(*all[i], iterations, seed, corpora, nullptr, timeoutSec);
    return 0;
  }
  const Target* t = findTarget(argv[1]);
  if (!t) {
    fprintf(stderr, "unknown target %s (miblo_fuzz list)\n", argv[1]);
    return 2;
  }
  return fuzzTarget(*t, iterations, seed, corpora, flatSeeds, timeoutSec);
}
#else
// libFuzzer build: MIBLO_FUZZ_TARGET names the target (-DMIBLO_FUZZ_TARGET=\"snapshot\").
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static const Target* t = findTarget(MIBLO_FUZZ_TARGET);
  if (t) t->run(data, size);
  return 0;
}
#endif
