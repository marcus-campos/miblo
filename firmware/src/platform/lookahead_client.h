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
// before allocating so a fragmented heap is not even tried) the request is answered 503
// {"error":"busy"} at once, which the plugin retries, rather than left waiting silently until the
// server drops it. See miblo_headers.h pollRequest. Re-armed
// after each request (routes.cpp, the not-found handler), since a connection may carry a second one.
//
// Why (2): the server's request hook (web.cpp limitPostBody) must judge the whole header block before
// the server parses it (a multipart Content-Type with a huge boundary crashes _parseForm), but
// WiFiClient can only peek at the first received segment (peekBuffer/peekBytes stop at the first
// pbuf). ESP8266WebServerTemplate takes the connection type from its ServerType::ClientType, so
// the server is instantiated with LookaheadServer below; nothing in the core is patched.
//
// Why (3): fairness. The short body wait blocks loop(): many clients that send their headers and
// then stall the body each held everyone else up for kBodyWaitMs, seconds in all. So while another
// client is waiting for the server (WiFiServer::hasClientData / LookaheadServer::hasMaxPendingClients) a request
// gets one short wait (miblo::kBodyWaitContendedMs, enough for a body a segment behind), then
// reports no data, and the server's non-blocking state decides (its 30 ms rule drops it while the
// other client has data). See miblo::bodyWaitPlan. Alone, it waits kBodyWaitMs as above.
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>

#include <lwip/tcp.h>

#include <memory>

#include "miblo_headers.h"
#include "miblo_policy.h"

inline bool heapLowForRequest(uint32_t needBytes);  // platform.h, after this file
class LookaheadServer;
inline bool othersWaiting(LookaheadServer* server);  // below LookaheadServer

class LookaheadClient : public WiFiClient {
 public:
  LookaheadClient() = default;
  // `server`: the listening server, asked whether other clients are waiting (Why (3) above).
  explicit LookaheadClient(const WiFiClient& c, LookaheadServer* server = nullptr) : WiFiClient(c), server_(server) {}

  // Holds the next request back until it is ready for the server (see Why (1) above).
  void rearm() {
    held_ = true;
    contendedWait_ = false;
  }
  // Low-memory guard (app.cpp, miblo::HeapGuard): while on, every request still held back is
  // answered 503 {"error":"busy"} and closed at once (the plugin retries it), and so is an idle
  // kept-alive connection, so their buffers go back to the heap the Wi-Fi SDK needs.
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
    // Why (3): a small body still to come is waited for in full only when no other client waits.
    const bool others = server_ && othersWaiting(server_);
    const miblo::BodyWait wait = miblo::bodyWaitPlan(others, contendedWait_);
    if (shedding_) {
      r = miblo::RequestReadiness::NoMemory;
    } else {
      // Usually the whole header block came in the first segment: judged in place, no heap buffer
      // (a 2 KB allocation on every request fragmented the heap the TCP sender needs).
      if (!ahead_.pending()) {
        r = miblo::requestInPlace(WiFiClient::peekBuffer(), WiFiClient::peekAvailable(), src, wait.budgetMs);
      }
      if (r == miblo::RequestReadiness::Waiting) {
        const bool heapLow = !ahead_.allocated() && heapLowForRequest(miblo::HeaderBuffer::kCap);
        r = miblo::pollRequest(ahead_, src, wait.budgetMs, heapLow);
      }
      if (r == miblo::RequestReadiness::BodyTimeout && !wait.refuseOnTimeout) contendedWait_ = true;
      r = miblo::settleBodyWait(r, wait);
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
      case miblo::RequestReadiness::NoMemory: {  // the plugin retries a 503 busy
        static const char kReply[] PROGMEM =
            "HTTP/1.1 503 Service Unavailable\r\nContent-Type: application/json\r\nConnection: close\r\n"
            "Content-Length: 16\r\n\r\n{\"error\":\"busy\"}";
        static_assert(sizeof("{\"error\":\"busy\"}") - 1 == 16, "Content-Length");
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
        break;
    }
    return false;
  }

  // Answers a request that can never be served and closes the connection for good.
  void refuse(PGM_P reply, size_t len) {
    WiFiClient::write_P(reply, len);
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
  LookaheadServer* server_ = nullptr;  // the listening server (outlives its clients); none: never contended
  bool held_ = true;
  bool refused_ = false;  // answered and closed by refuse(): no data, not connected
  bool contendedWait_ = false;  // this request already had its short wait while others waited
  static inline bool shedding_ = false;
};

// The listening side. The firmware is built with lwIP's "feat" variant (platformio.ini) for its
// listen backlog: without one (the LOW_FLASH variant) lwIP accepted every handshake and WiFiServer
// queued each with a pcb, a ClientContext and its received bytes, so a burst of idle or half-sent
// connections took the heap from 28 KB to 17 KB and the Wi-Fi SDK then failed an allocation. Now
// at most miblo::kPendingConnections wait (handshakes in progress included); lwIP ignores further
// SYNs and the clients retry them. The accept callback is wrapped (miblo::admitConnection): a
// connection lwIP could not allocate (it calls with no pcb, which the core would wrap in a
// ClientContext) or one arriving while the low-memory guard is on is refused, and lwIP resets it.
class LookaheadServer : public WiFiServer {
 public:
  using WiFiServer::WiFiServer;
  using ClientType = LookaheadClient;
  LookaheadClient accept() { return LookaheadClient(WiFiServer::accept(), this); }
  // ESP8266WebServer calls these by the server type.
  void begin() { begin(_port); }
  void begin(uint16_t port) {
    WiFiServer::begin(port, miblo::kPendingConnections);
    if (_listen_pcb) tcp_accept(_listen_pcb, &LookaheadServer::admit);  // its arg stays this server
  }
  // The core compares with MAX_PENDING_CLIENTS_PER_PORT (5), never reached with our backlog.
  bool hasMaxPendingClients() const {
    return _listen_pcb && reinterpret_cast<const tcp_pcb_listen*>(_listen_pcb)->accepts_pending >= miblo::kPendingConnections;
  }
  // Low-memory guard (app.cpp, miblo::HeapGuard), every loop pass.
  static void shed(bool on) { heapLow_ = on; }
  static uint32_t refused() { return refused_; }  // connections refused since boot (/api/info)

 private:
  static err_t admit(void* arg, tcp_pcb* pcb, err_t err) {
    if (!miblo::admitConnection(pcb != nullptr && err == ERR_OK, heapLow_)) {
      refused_++;
      return ERR_MEM;  // lwIP aborts (resets) a pcb whose accept failed
    }
    return WiFiServer::_s_accept(arg, pcb, err);
  }
  static inline bool heapLow_ = false;
  static inline uint32_t refused_ = 0;
};

inline bool othersWaiting(LookaheadServer* server) { return server->hasClientData() || server->hasMaxPendingClients(); }
