#include "miblo_headers.h"

#include <stdlib.h>
#include <string.h>

namespace miblo {

HeaderBuffer::HeaderBuffer(const HeaderBuffer& o) { append(o.data(), o.pending()); }

HeaderBuffer& HeaderBuffer::operator=(const HeaderBuffer& o) {
  if (this == &o) return *this;
  clear();
  append(o.data(), o.pending());
  return *this;
}

int HeaderBuffer::readByte() {
  if (!pending()) return -1;
  const int c = (uint8_t)buf_[pos_];
  consume(1);
  return c;
}

size_t HeaderBuffer::peek(uint8_t* dst, size_t n) const {
  if (n > pending()) n = pending();
  if (n) memcpy(dst, buf_ + pos_, n);
  return n;
}

size_t HeaderBuffer::read(uint8_t* dst, size_t n) {
  n = peek(dst, n);
  consume(n);
  return n;
}

void HeaderBuffer::consume(size_t n) {
  pos_ += n < pending() ? n : pending();
  if (!pending()) clear();  // drained: give the memory back at once
}

void HeaderBuffer::clear() {
  free(buf_);
  buf_ = nullptr;
  pos_ = len_ = 0;
}

char* HeaderBuffer::reserveTail(size_t& room) {
  room = 0;
  if (!buf_) {
    buf_ = static_cast<char*>(malloc(kCap));
    if (!buf_) return nullptr;
    pos_ = len_ = 0;
  }
  if (pos_) {
    memmove(buf_, buf_ + pos_, pending());
    len_ -= pos_;
    pos_ = 0;
  }
  room = kCap - len_;
  return buf_ + len_;
}

size_t HeaderBuffer::append(const char* src, size_t n) {
  if (!n) return 0;
  size_t room;
  char* tail = reserveTail(room);
  if (!tail) return 0;
  if (n > room) n = room;
  memcpy(tail, src, n);
  commit(n);
  return n;
}

GatherResult gatherHeaders(HeaderBuffer& buf, ByteSource& src, uint32_t budgetMs) {
  const uint32_t start = src.nowMs();
  for (;;) {
    if (buf.verdict() != HeaderVerdict::Incomplete) return GatherResult::Ready;
    if (buf.pending() >= HeaderBuffer::kCap) return GatherResult::TooLarge;
    const size_t avail = src.available();
    if (avail) {
      size_t room;
      char* tail = buf.reserveTail(room);
      if (!tail) return GatherResult::NoMemory;
      buf.commit(src.read(tail, avail < room ? avail : room));
      continue;
    }
    if (!src.connected()) return GatherResult::Closed;
    if (src.nowMs() - start >= budgetMs) return GatherResult::TimedOut;
    src.wait();
  }
}

namespace {

// Where the header block ends (just past its blank line), or 0 when it has not all arrived.
size_t headerBlockEnd(const char* p, size_t n) {
  for (size_t i = 3; i < n; i++) {
    if (p[i] == '\n' && p[i - 1] == '\r' && p[i - 2] == '\n' && p[i - 3] == '\r') return i + 1;
  }
  return 0;
}

bool lowerIs(const char* p, const char* word, size_t n) {
  for (size_t i = 0; i < n; i++) {
    char c = p[i];
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    if (c != word[i]) return false;
  }
  return true;
}

// The body length the server will read: the largest Content-Length in the block (0 if none).
// Saturates instead of overflowing.
size_t contentLength(const char* p, size_t end) {
  static const char kName[] = "content-length:";
  const size_t kLen = sizeof(kName) - 1;
  size_t best = 0;
  for (size_t line = 0; line < end;) {
    size_t eol = line;
    while (eol < end && p[eol] != '\n') eol++;
    if (eol - line > kLen && lowerIs(p + line, kName, kLen)) {
      size_t i = line + kLen;
      while (i < eol && (p[i] == ' ' || p[i] == '\t')) i++;
      size_t v = 0;
      for (; i < eol && p[i] >= '0' && p[i] <= '9'; i++) v = v > 100000000 ? v : v * 10 + (size_t)(p[i] - '0');
      if (v > best) best = v;
    }
    line = eol + 1;
  }
  return best;
}

}  // namespace

size_t requestBodyLength(const char* p, size_t n) {
  const size_t end = p ? headerBlockEnd(p, n) : 0;
  return end ? contentLength(p, end) : 0;
}

bool findHeader(const char* p, size_t n, const char* lowerName, char* out, size_t cap) {
  const size_t end = p ? headerBlockEnd(p, n) : 0;
  const size_t nameLen = strlen(lowerName);
  bool found = false;
  for (size_t line = 0; line < end;) {
    size_t eol = line;
    while (eol < end && p[eol] != '\n') eol++;
    if (eol - line > nameLen && p[line + nameLen] == ':' && lowerIs(p + line, lowerName, nameLen)) {
      size_t a = line + nameLen + 1, b = eol;
      while (a < b && (p[a] == ' ' || p[a] == '\t')) a++;
      while (b > a && (p[b - 1] == '\r' || p[b - 1] == ' ' || p[b - 1] == '\t')) b--;
      if (b - a >= cap) {
        found = false;  // the last one counts: one that does not fit hides an earlier one
      } else {
        memcpy(out, p + a, b - a);
        out[b - a] = 0;
        found = true;
      }
    }
    line = eol + 1;
  }
  if (!found && cap) out[0] = 0;
  return found;
}

namespace {
// Body bytes beyond p[0..n) (which starts with a header block ending at `end`) the request still
// needs: 0 when all here, or when the body is larger than kBodyHoldMax (not held back).
size_t bodyToCome(const char* p, size_t end, size_t n) {
  const size_t body = contentLength(p, end);
  if (body > kBodyHoldMax) return 0;
  const size_t have = n - end;
  return have >= body ? 0 : body - have;
}
}  // namespace

RequestReadiness waitForBytes(ByteSource& src, size_t want, uint32_t budgetMs) {
  const uint32_t start = src.nowMs();
  for (;;) {
    if (src.available() >= want) return RequestReadiness::Ready;
    if (!src.connected()) return RequestReadiness::Closed;
    if (src.nowMs() - start >= budgetMs) return RequestReadiness::BodyTimeout;
    src.wait();
  }
}

RequestReadiness requestInPlace(const char* p, size_t n, ByteSource& src, uint32_t bodyWaitMs) {
  const size_t end = p ? headerBlockEnd(p, n) : 0;
  if (!end) return RequestReadiness::Waiting;
  return waitForBytes(src, n + bodyToCome(p, end, n), bodyWaitMs);
}

RequestReadiness pollRequest(HeaderBuffer& buf, ByteSource& src, uint32_t bodyWaitMs, bool heapLow) {
  for (;;) {
    const size_t end = headerBlockEnd(buf.data(), buf.pending());
    if (end) return waitForBytes(src, bodyToCome(buf.data(), end, buf.pending()), bodyWaitMs);
    if (buf.pending() >= HeaderBuffer::kCap) return RequestReadiness::TooLarge;
    const size_t avail = src.available();
    if (!avail) return src.connected() ? RequestReadiness::Waiting : RequestReadiness::Closed;
    if (heapLow && !buf.allocated()) return RequestReadiness::NoMemory;
    size_t room;
    char* tail = buf.reserveTail(room);
    if (!tail) return RequestReadiness::NoMemory;
    buf.commit(src.read(tail, avail < room ? avail : room));
  }
}

size_t drainInput(ByteSource& src, uint32_t budgetMs, size_t maxBytes) {
  char sink[64];
  size_t total = 0;
  const uint32_t start = src.nowMs();
  while (total < maxBytes && src.nowMs() - start < budgetMs) {
    size_t k = src.available();
    if (k) {
      if (k > sizeof(sink)) k = sizeof(sink);
      if (k > maxBytes - total) k = maxBytes - total;
      total += src.read(sink, k);
      continue;
    }
    if (!src.connected()) break;
    src.wait();
  }
  return total;
}

BodyAction decideBody(HeaderVerdict v, bool isPost, bool isUpdatePath, bool armed, bool locked) {
  switch (v) {
    case HeaderVerdict::Plain:
      return BodyAction::CheckSize;
    case HeaderVerdict::Incomplete:
      return BodyAction::HeadersIncomplete;
    case HeaderVerdict::Malformed:
      return BodyAction::BadRequest;
    case HeaderVerdict::Multipart:
    case HeaderVerdict::BadMultipart:
      break;
  }
  if (!isPost || !isUpdatePath) return BodyAction::BadRequest;
  if (!armed) return locked ? BodyAction::Locked : BodyAction::NotOpen;
  return v == HeaderVerdict::Multipart ? BodyAction::Continue : BodyAction::BadBoundary;
}

BodyAction decideGather(GatherResult r) {
  switch (r) {
    case GatherResult::Ready:
      return BodyAction::Continue;
    case GatherResult::TooLarge:
      return BodyAction::HeadersTooLarge;
    case GatherResult::NoMemory:
      return BodyAction::Busy;
    case GatherResult::TimedOut:
    case GatherResult::Closed:
      break;
  }
  return BodyAction::HeadersIncomplete;
}

}  // namespace miblo
