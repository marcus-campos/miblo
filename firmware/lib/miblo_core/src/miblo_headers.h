#pragma once
#include <stddef.h>
#include <stdint.h>

#include "miblo_security.h"

// Seeing a request's whole header block before ESP8266WebServer parses it.
//
// The server's request hook runs right after the request line and can only peek at the first
// received TCP segment (536 B with lwIP "low memory"); a real browser's headers often span two or
// three segments. To judge the block as a whole (the last Content-Type wins in the server), the
// connection reads it ahead into a HeaderBuffer and replays those bytes, in order and unchanged,
// to the server's own parser. See src/platform/lookahead_client.h and web.cpp limitPostBody.
namespace miblo {

// Bytes read ahead from a connection, served back before anything still in the socket. Heap
// buffer of kCap bytes, allocated on first use and freed as soon as the last byte is read (or on
// clear()), so an idle connection holds nothing. Copies are deep.
class HeaderBuffer {
 public:
  static constexpr size_t kCap = 2048;

  HeaderBuffer() = default;
  HeaderBuffer(const HeaderBuffer& o);
  HeaderBuffer& operator=(const HeaderBuffer& o);
  ~HeaderBuffer() { clear(); }

  size_t pending() const { return len_ - pos_; }
  const char* data() const { return buf_ ? buf_ + pos_ : nullptr; }
  bool allocated() const { return buf_ != nullptr; }
  int readByte();
  int peekByte() const { return pending() ? (uint8_t)buf_[pos_] : -1; }
  size_t read(uint8_t* dst, size_t n);
  size_t peek(uint8_t* dst, size_t n) const;
  void consume(size_t n);
  void clear();

  // Room to append: moves the pending bytes to the front (allocating the buffer on first use) and
  // returns where `room` more bytes go, or nullptr if the allocation failed. Then commit(n).
  char* reserveTail(size_t& room);
  void commit(size_t n) { len_ += n; }
  size_t append(const char* src, size_t n);

  // The pending bytes judged as a header block (checkRequestHeaders).
  HeaderVerdict verdict() const { return checkRequestHeaders(data(), pending()); }

 private:
  char* buf_ = nullptr;
  size_t pos_ = 0;
  size_t len_ = 0;
};

// Where gatherHeaders reads from: the connection, plus a clock and a short wait (delay(1) on the
// device, so Wi-Fi keeps running).
struct ByteSource {
  virtual size_t available() = 0;
  virtual size_t read(char* dst, size_t n) = 0;
  virtual bool connected() = 0;
  virtual uint32_t nowMs() = 0;
  virtual void wait() = 0;
};

// How long a request's headers may take to arrive once its request line did. Bounds the hook's
// added latency. A browser's segments come back to back, milliseconds apart, but one lost on a weak
// link only comes back after a retransmission timeout (hundreds of ms): 1 s covers that.
constexpr uint32_t kHeaderWaitMs = 1000;

// The lingering close after refusing an upload: the reply is written, then the body the client
// is still sending is read and dropped for up to kLingerMs / kLingerMaxBytes (whichever comes
// first, or until the client closes), so closing does not reset the connection before the client
// has read the reply. Returns the bytes dropped.
constexpr uint32_t kLingerMs = 1000;
constexpr size_t kLingerMaxBytes = 32768;
size_t drainInput(ByteSource& src, uint32_t budgetMs, size_t maxBytes);

enum class GatherResult : uint8_t { Ready, TooLarge, TimedOut, Closed, NoMemory };

// Reads from `src` into `buf` until the pending bytes hold a complete header block (Ready: verdict
// is no longer Incomplete), HeaderBuffer::kCap bytes are pending without one (TooLarge), the
// budget runs out (TimedOut) or the peer closes (Closed). May read past the block into the body:
// those bytes stay in `buf` and are replayed too.
GatherResult gatherHeaders(HeaderBuffer& buf, ByteSource& src, uint32_t budgetMs);

// Whether a connection's request is ready for ESP8266WebServer, whose parser reads every header
// line, and then the body, with a blocking wait of up to 5 s each: one client that sends half a
// request and stops froze the whole gadget (screen, alerts, other clients) for ~10 s per
// connection, minutes with a few dozen. See lookahead_client.h.
//
// Two steps. (1) Until the header block (through its blank line) is all here the connection reports
// no data and the server waits without blocking (its HC_WAIT_READ state, which drops the client
// after 5 s, or after 30 ms when another client has data): never waits here. (2) Once it is, a small
// body (<= kBodyHoldMax) not all here yet is waited for with a short blocking wait, kBodyWaitMs,
// counting the socket's unread bytes without reading them: a real client sends it within
// milliseconds of its headers, often in the next TCP segment. Holding it back in the server's
// non-blocking state instead let the 30 ms rule drop a request whose body was a segment behind
// (the bridge's snapshots while a page loaded). A body that does not come in time is answered 408:
// the residual cost of a client that stalls mid-body is kBodyWaitMs per connection, not 5 s.
enum class RequestReadiness : uint8_t {
  Ready,        // the header block, and a small body all here (or a large one that is not held)
  Waiting,      // the header block is not all here: ask again later
  TooLarge,     // no header block within HeaderBuffer::kCap: refuse (431)
  BodyTimeout,  // a small body did not arrive within the wait: refuse (408)
  Closed,       // the peer closed before sending it all
  NoMemory,     // no heap for the buffer: ask again later
};
// A body larger than this is not held back: the TCP window (lwIP low memory: 4 x 536 B) cannot
// hold more unread, so waiting for it could never end. The server reads it with its own 5 s wait
// (web.cpp largeBodyRefusal admits that only for a paired computer's snapshot).
constexpr size_t kBodyHoldMax = 1536;
// How long a complete header block's small body may take to follow it.
constexpr uint32_t kBodyWaitMs = 200;

// Waits (src.wait()) until `src` has at least `want` unread bytes (never reads them): Ready, or
// Closed if the peer closes first, or BodyTimeout after budgetMs.
RequestReadiness waitForBytes(ByteSource& src, size_t want, uint32_t budgetMs);
// Reads what has arrived (never waits) into `buf` until it holds the header block, then waits up to
// bodyWaitMs for a small body still to come. Body bytes are counted in the socket, not read.
RequestReadiness pollRequest(HeaderBuffer& buf, ByteSource& src, uint32_t bodyWaitMs);
// The same on the bytes p[0..n) judged in place (the first received segment, no copy; `src`
// counts them among its unread bytes): Waiting when they do not hold the whole header block (then
// use pollRequest), else as pollRequest. The usual request never touches the heap.
RequestReadiness requestInPlace(const char* p, size_t n, ByteSource& src, uint32_t bodyWaitMs);

// The body length of a complete header block (the largest Content-Length), 0 when the block has
// not all arrived or has none.
size_t requestBodyLength(const char* p, size_t n);
// The value of header `lowerName` (lowercase, without the colon) in a complete header block, the
// last one if repeated (as the server reads it), trimmed, into `out`. False if absent, if the
// block is incomplete, or if the value does not fit (never cut).
bool findHeader(const char* p, size_t n, const char* lowerName, char* out, size_t cap);

// What the request hook does with a body request (POST, PUT, PATCH, DELETE).
enum class BodyAction : uint8_t {
  Continue,           // let the server parse it
  CheckSize,          // not multipart: apply the body size limits, then continue
  BadRequest,         // 400: malformed headers, or multipart anywhere but POST /update
  BadBoundary,        // 400: multipart POST /update without a single 1..70 character boundary
  NotOpen,            // 400: multipart POST /update with no upload window open
  Locked,             // 429: same, while the update code is locked out
  HeadersTooLarge,    // 431: no complete header block within HeaderBuffer::kCap
  HeadersIncomplete,  // 400: the header block did not arrive within kHeaderWaitMs
  Busy,               // 503: no memory to read the headers ahead
};

// Decision on a complete (or never completed) header block. A multipart body is only ever the
// firmware upload: POST /update, while the upload window is open.
BodyAction decideBody(HeaderVerdict v, bool isPost, bool isUpdatePath, bool armed, bool locked);
// Decision when gathering did not end with a header block (Ready → Continue: call decideBody).
BodyAction decideGather(GatherResult r);

}  // namespace miblo
