#pragma once
// ESP8266 only (included from platform.h). The web server's connection type: a WiFiClient that
// can read a request's header block ahead and then replays those bytes, unchanged and in order,
// to ESP8266WebServer's own parser.
//
// Why (1): ESP8266WebServer parses a request with blocking reads, up to 5 s per header line and
// per body: a client that sent half a request and stopped froze the whole loop() (screen, alerts,
// every other client) for ~10 s per connection, minutes with a few dozen connections. So until the
// whole header block is here the connection reports no data, and the server waits in its
// non-blocking state instead (dropping it after 5 s, or 30 ms when another client has data): a
// half-sent header block costs nothing. Once it is here, a small body still to come is waited for
// with a short blocking wait (miblo::kBodyWaitMs, 350 ms: a real client sends it within
// milliseconds, or after a delayed ACK), then released; a body that does not come is answered 408. The residual cost of a
// client that stalls mid-body is that wait, per connection. A body too large to hold back
// (miblo::kBodyHoldMax) is read by the server itself, admitted only for a paired computer's
// snapshot (web.cpp largeBodyRefusal). Holding the body in the non-blocking state too was tried
// and dropped: a request whose body was one segment behind its headers (the bridge's snapshots)
// fell to the 30 ms rule whenever a page was loading. A header block that spans segments is read
// into a 2 KB heap buffer; when the heap cannot spare it (platform.h heapLowForRequest, checked
// before allocating so a fragmented heap is not even tried) the request is held, writing nothing,
// as in 1.12.0: a reply needs heap for its segment too, and a write that cannot get it waits in the
// loop while more connections pile up (a 1.14.0 build answered 503 there and fell over under a
// burst). See miblo_headers.h pollRequest. Re-armed
// after each request (routes.cpp, the not-found handler), since a connection may carry a second one.
//
// Why (2): the server's request hook (web.cpp limitPostBody) must judge the whole header block before
// the server parses it (a multipart Content-Type with a huge boundary crashes _parseForm), but
// WiFiClient can only peek at the first received segment (peekBuffer/peekBytes stop at the first
// pbuf). ESP8266WebServerTemplate takes the connection type from its ServerType::ClientType, so
// the server is instantiated with LookaheadServer below; nothing in the core is patched.
//
// The body wait is the same whether or not other clients wait (as in 1.12.0): a stalled body is
// always answered 408 and closed by us within kBodyWaitMs. A shorter wait under contention that
// then left the request to the server's non-blocking state (dropped silently, or held) was tried
// and dropped: a 1.14 test build held a burst of connections (heap down to ~17 KB, then a Wi-Fi
// SDK failure) that 1.12.0 closed within seconds.
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>

#include <lwip/tcp.h>

#include <memory>

#include "miblo_headers.h"
#include "miblo_policy.h"

inline bool heapLowForRequest(uint32_t needBytes);  // platform.h, after this file

class LookaheadClient : public WiFiClient {
 public:
  LookaheadClient() = default;
  explicit LookaheadClient(const WiFiClient& c) : WiFiClient(c) {}

  // Holds the next request back until it is ready for the server (see Why (1) above).
  void rearm() { held_ = true; }
  // Low-memory guard (app.cpp, miblo::HeapGuard): while on, every request still held back is
  // closed at once without a reply (writing one would need the heap that is missing), so its
  // buffers go back to the heap the Wi-Fi SDK needs. The plugin retries.
  static void shed(bool on) { shedding_ = on; }
  LookaheadClient(const LookaheadClient&) = default;
  LookaheadClient& operator=(const LookaheadClient&) = default;
  ~LookaheadClient() override = default;

  // Reads from the socket until the pending bytes hold a complete header block (see
  // miblo::gatherHeaders), waiting at most budgetMs with delay(1). The bytes are replayed below.
  miblo::GatherResult gatherHeaders(uint32_t budgetMs) {
    Source src{*this};
    return miblo::gatherHeaders(ahead_, src, budgetMs);
  }
  // Drops what the client still sends (the read-ahead bytes, then the socket) for a bounded time:
  // the lingering close after a refused upload (miblo::drainInput).
  void discardInput(uint32_t budgetMs, size_t maxBytes) {
    const size_t ahead = ahead_.pending();
    ahead_.clear();
    if (ahead >= maxBytes) return;
    Source src{*this};
    miblo::drainInput(src, budgetMs, maxBytes - ahead);
  }
  // The bytes read ahead and not consumed yet, as one header block.
  miblo::HeaderVerdict aheadVerdict() const { return ahead_.verdict(); }
  bool hasAhead() const { return ahead_.pending() > 0; }

  // Reading: the read-ahead bytes first, then the socket.
  int available() override {
    if (refused_ || (held_ && !releaseRequest())) return 0;
    return (int)ahead_.pending() + WiFiClient::available();
  }
  int read() override { return ahead_.pending() ? ahead_.readByte() : WiFiClient::read(); }
  int read(uint8_t* buf, size_t size) override {
    return ahead_.pending() ? (int)ahead_.read(buf, size) : WiFiClient::read(buf, size);
  }
  int read(char* buf, size_t size) { return read((uint8_t*)buf, size); }  // WiFiClient's is not virtual
  int peek() override { return ahead_.pending() ? ahead_.peekByte() : WiFiClient::peek(); }
  size_t peekBytes(uint8_t* buf, size_t size) override {
    return ahead_.pending() ? ahead_.peek(buf, size) : WiFiClient::peekBytes(buf, size);
  }
  size_t peekBytes(char* buf, size_t size) { return peekBytes((uint8_t*)buf, size); }
  bool hasPeekBufferAPI() const override { return true; }
  size_t peekAvailable() override { return ahead_.pending() ? ahead_.pending() : WiFiClient::peekAvailable(); }
  const char* peekBuffer() override { return ahead_.pending() ? ahead_.data() : WiFiClient::peekBuffer(); }
  void peekConsume(size_t n) override {
    if (ahead_.pending()) ahead_.consume(n);
    else WiFiClient::peekConsume(n);
  }
  uint8_t connected() override {
    if (refused_) return 0;
    return ahead_.pending() ? 1 : WiFiClient::connected();
  }
  void stop() override { (void)stop(0); }
  bool stop(unsigned int maxWaitMs) {  // WiFiClient's is not virtual: keep both in step
    ahead_.clear();
    return WiFiClient::stop(maxWaitMs);
  }
  // A copy carries the bytes still to replay (HeaderBuffer copies are deep).
  std::unique_ptr<WiFiClient> clone() const override { return std::unique_ptr<WiFiClient>(new LookaheadClient(*this)); }
  using WiFiClient::flush;  // output only: nothing to do with the read-ahead

 private:
  // gatherHeaders' view of the socket itself (never the read-ahead bytes).
  struct Source : miblo::ByteSource {
    explicit Source(LookaheadClient& c) : c(c) {}
    size_t available() override {
      const int n = c.WiFiClient::available();
      return n > 0 ? (size_t)n : 0;
    }
    size_t read(char* dst, size_t n) override {
      const int r = c.WiFiClient::read((uint8_t*)dst, n);
      return r > 0 ? (size_t)r : 0;
    }
    // The socket itself: WiFiClient::connected() would count the read-ahead bytes (it calls the
    // virtual available()), hiding a peer that closed mid-headers.
    bool connected() override { return c.status() == ESTABLISHED || c.WiFiClient::available() > 0; }
    uint32_t nowMs() override { return millis(); }
    void wait() override { delay(1); }
    LookaheadClient& c;
  };

  // True once the request is ready for the server (Why (1) above). One that can never be (no
  // header block within the buffer, a small body that does not come, no heap for the buffer) is
  // answered and closed here.
  bool releaseRequest() {
    Source src{*this};
    miblo::RequestReadiness r = miblo::RequestReadiness::Waiting;
    if (shedding_) {
      // Closed without a reply: writing one needs heap for its segment, and a write that cannot
      // get it waits in the loop (up to the client's timeout) while more connections pile up.
      refuse(nullptr, 0);
      return false;
    } else {
      // Usually the whole header block came in the first segment: judged in place, no heap buffer
      // (a 2 KB allocation on every request fragmented the heap the TCP sender needs).
      if (!ahead_.pending()) {
        r = miblo::requestInPlace(WiFiClient::peekBuffer(), WiFiClient::peekAvailable(), src, miblo::kBodyWaitMs);
      }
      if (r == miblo::RequestReadiness::Waiting) {
        const bool heapLow = !ahead_.allocated() && heapLowForRequest(miblo::HeaderBuffer::kCap);
        r = miblo::pollRequest(ahead_, src, miblo::kBodyWaitMs, heapLow);
      }
    }
    switch (r) {
      case miblo::RequestReadiness::Ready:
        held_ = false;
        return true;
      case miblo::RequestReadiness::TooLarge: {
        static const char kReply[] PROGMEM =
            "HTTP/1.1 431 Request Header Fields Too Large\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
        refuse(kReply, sizeof(kReply) - 1);
        return false;
      }
      case miblo::RequestReadiness::BodyTimeout: {
        static const char kReply[] PROGMEM =
            "HTTP/1.1 408 Request Timeout\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
        refuse(kReply, sizeof(kReply) - 1);
        return false;
      }
      case miblo::RequestReadiness::BadLength: {  // H1: a length the server would read otherwise
        static const char kReply[] PROGMEM =
            "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
        refuse(kReply, sizeof(kReply) - 1);
        return false;
      }
      case miblo::RequestReadiness::Closed:
        // Not stop(): its flush can wait up to 300 ms in the loop. Reporting no data is enough,
        // the server drops the connection itself.
        ahead_.clear();
        return false;
      case miblo::RequestReadiness::Waiting:
      // No heap for the header buffer: held as in 1.12.0, writing nothing (a reply needs heap too,
      // and a write that cannot get it waits in the loop). The server's 30 ms rule drops it while
      // other clients wait, or its own timeout does.
      case miblo::RequestReadiness::NoMemory:
        break;
    }
    return false;
  }

  // Answers a request that can never be served (no reply: len 0) and closes the connection for good.
  void refuse(PGM_P reply, size_t len) {
    if (len) WiFiClient::write_P(reply, len);
    ahead_.clear();
    // Unread bytes would make lwIP reset the connection, losing the reply: drop them first.
    while (size_t k = WiFiClient::peekAvailable()) WiFiClient::peekConsume(k);
    // Not stop(): its flush waits up to 300 ms more for a client that already stalled.
    (void)WiFiClient::stop(1);
    // The closed socket may still show bytes (ClientContext keeps its receive buffer): never judge
    // them again, or each of the server's later available() calls would wait kBodyWaitMs once more.
    refused_ = true;
  }

  miblo::HeaderBuffer ahead_;
  bool held_ = true;
  bool refused_ = false;  // answered and closed by refuse(): no data, not connected
  static inline bool shedding_ = false;
};

class LookaheadServer : public WiFiServer {
 public:
  using WiFiServer::WiFiServer;
  using ClientType = LookaheadClient;
  LookaheadClient accept() { return LookaheadClient(WiFiServer::accept()); }
};

