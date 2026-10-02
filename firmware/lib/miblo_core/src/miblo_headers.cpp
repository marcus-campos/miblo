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
