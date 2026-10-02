#pragma once
#include <stddef.h>
#include <stdint.h>

namespace miblo {

// Decodes the next code point from `p` and advances the pointer.
// Invalid byte or truncated sequence → U+FFFD, advancing 1 byte. End of string → 0 (doesn't advance).
uint32_t utf8Next(const char*& p);

// Number of code points.
size_t utf8Length(const char* s);

// Copies at most `maxChars` code points from `src` to `dst` (capacity `cap` bytes, always
// NUL-terminated), never cutting a UTF-8 sequence in the middle. Returns the bytes written.
size_t utf8Copy(char* dst, size_t cap, const char* src, size_t maxChars = (size_t)-1);

// Text typed by a person (a gadget name, a paired computer's label): fits `cap` bytes with its
// NUL, at most `maxChars` code points, no control characters.
bool typedText(const char* s, size_t cap, size_t maxChars);

}  // namespace miblo
