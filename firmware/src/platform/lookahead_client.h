#pragma once
// ESP8266 only (included from platform.h). The web server's connection type: a WiFiClient that
// can read a request's header block ahead and then replays those bytes, unchanged and in order,
// to ESP8266WebServer's own parser.
//
// Why (1): ESP8266WebServer parses a request with blocking reads, up to 5 s per header line and
// per body: a client that sent half a request and stopped froze the whole loop() (screen, alerts,
// every other client) for ~10 s per connection, minutes with a few dozen connections. So until
// the whole request is here (miblo::pollRequest, which never waits) the connection reports no
// data, and the server waits in its non-blocking state instead (dropping it after 5 s, or 30 ms
// when another client has data). Re-armed after each request (routes.cpp, the not-found handler),
// since a connection may carry a second one.
//
// Why (2): the server's request hook (web.cpp limitPostBody) must judge the whole header block before
// the server parses it (a multipart Content-Type with a huge boundary crashes _parseForm), but
// WiFiClient can only peek at the first received segment (peekBuffer/peekBytes stop at the first
// pbuf). ESP8266WebServerTemplate takes the connection type from its ServerType::ClientType, so
// the server is instantiated with LookaheadServer below; nothing in the core is patched.
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>

#include <memory>

#include "miblo_headers.h"

class LookaheadClient : public WiFiClient {
 public:
  LookaheadClient() = default;
  explicit LookaheadClient(const WiFiClient& c) : WiFiClient(c) {}

  // Holds the next request back until it is all here (see Why (1) above).
  void rearm() { held_ = true; }
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
    if (held_ && !releaseRequest()) return 0;
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
  uint8_t connected() override { return ahead_.pending() ? 1 : WiFiClient::connected(); }
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

  // True once the request is all here. A request that can never be complete (no header block
  // within the buffer, or the peer gone) is answered and closed here, so the server drops it.
  bool releaseRequest() {
    Source src{*this};
    switch (miblo::pollRequest(ahead_, src)) {
      case miblo::RequestReadiness::Ready:
        held_ = false;
        return true;
      case miblo::RequestReadiness::TooLarge: {
        static const char kReply[] PROGMEM =
            "HTTP/1.1 431 Request Header Fields Too Large\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
        WiFiClient::write_P(kReply, sizeof(kReply) - 1);
        stop();
        return false;
      }
      case miblo::RequestReadiness::Closed:
        stop();
        return false;
      case miblo::RequestReadiness::Waiting:
      case miblo::RequestReadiness::NoMemory:
        break;
    }
    return false;
  }

  miblo::HeaderBuffer ahead_;
  bool held_ = true;
};

class LookaheadServer : public WiFiServer {
 public:
  using WiFiServer::WiFiServer;
  using ClientType = LookaheadClient;
  LookaheadClient accept() { return LookaheadClient(WiFiServer::accept()); }
};
