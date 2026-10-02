#pragma once
// Host fuzz harness for the parsers that take untrusted input (make fuzz; README.md here).
// A target is a function fed arbitrary bytes; it must never crash, hang, touch memory out of
// bounds, trip UBSan or break one of its own invariants (fuzzFail). The driver (driver.cpp) runs
// each target over a seed corpus plus deterministic mutations; the same target functions also
// build as libFuzzer entry points (LLVMFuzzerTestOneInput) where libFuzzer is available.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <string>

namespace fuzz {

struct Target {
  const char* name;
  void (*run)(const uint8_t* data, size_t size);
  const char* const* seeds;  // nullptr-terminated (JSON text, or hex for binary targets: "hex:...")
  const char* const* dict;   // nullptr-terminated tokens the mutator splices in
  size_t maxLen;             // inputs are capped at this many bytes (a few go beyond: "huge" inputs)
};

// Registers a target at start-up (one static Registrar per target).
struct Registrar {
  explicit Registrar(const Target& t);
};
const Target* const* targets(size_t& count);

// The input got past parsing (the driver reports the share of inputs that did: "deep").
void reached();

// An invariant broke: prints the message and aborts (the driver saves the input first).
[[noreturn]] void fail(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

#define FUZZ_CHECK(cond, ...)       \
  do {                              \
    if (!(cond)) ::fuzz::fail(__VA_ARGS__); \
  } while (0)

// Reads structured values off the input, for targets that need numbers and choices besides text.
// Past the end it returns zeros (never reads out of bounds).
class Reader {
 public:
  Reader(const uint8_t* d, size_t n) : d_(d), n_(n) {}
  size_t left() const { return n_ - p_; }
  uint8_t u8() { return p_ < n_ ? d_[p_++] : 0; }
  uint16_t u16() { return (uint16_t)(u8() | (u8() << 8)); }
  uint32_t u32() { return (uint32_t)u16() | ((uint32_t)u16() << 16); }
  // Up to `max` bytes as a std::string (may hold NULs and invalid UTF-8).
  std::string bytes(size_t max) {
    size_t n = u8();
    if (n > max) n = max;
    if (n > left()) n = left();
    std::string s(reinterpret_cast<const char*>(d_ + p_), n);
    p_ += n;
    return s;
  }
  std::string rest() {
    std::string s(reinterpret_cast<const char*>(d_ + p_), n_ - p_);
    p_ = n_;
    return s;
  }

 private:
  const uint8_t* d_;
  size_t n_;
  size_t p_ = 0;
};

// A heap copy of exactly `n` bytes (no terminator): ASan flags any read past the end.
struct ExactBuf {
  ExactBuf(const void* src, size_t n);
  ~ExactBuf();
  ExactBuf(const ExactBuf&) = delete;
  ExactBuf& operator=(const ExactBuf&) = delete;
  char* p;
  size_t n;
};

// A heap copy as a C string: the bytes up to the first NUL, then the terminator.
struct CStr {
  CStr(const void* src, size_t n);
  ~CStr();
  CStr(const CStr&) = delete;
  CStr& operator=(const CStr&) = delete;
  char* s;
};

// True when `s` is a short identifier ([A-Za-z0-9_]+, <= 32): what a handler may put raw into JSON.
bool plainIdent(const char* s);
// True when `s` is valid UTF-8 (no overlongs, surrogates or values past U+10FFFF).
bool validUtf8(const char* s);

}  // namespace fuzz

#define FUZZ_REGISTER(NAME, FN, SEEDS, DICT, MAXLEN) \
  static ::fuzz::Registrar fuzz_reg_##NAME({#NAME, FN, SEEDS, DICT, MAXLEN})
