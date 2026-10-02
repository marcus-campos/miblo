#pragma once
// ESP8266 only (included from platform.h). The web server's connection type: a WiFiClient that
// can read a request's header block ahead and then replays those bytes, unchanged and in order,
// to ESP8266WebServer's own parser.
//
// Why: the server's request hook (web.cpp limitPostBody) must judge the whole header block before
// the server parses it (a multipart Content-Type with a huge boundary crashes _parseForm), but
// WiFiClient can only peek at the first received segment (peekBuffer/peekBytes stop at the first
// pbuf). ESP8266WebServerTemplate takes the connection type from its ServerType::ClientType, so
// the server is instantiated with LookaheadServer below; nothing in the core is patched.
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>

#include "miblo_headers.h"

class LookaheadClient : public WiFiClient {
 public:
  LookaheadClient() = default;
  explicit LookaheadClient(const WiFiClient& c) : WiFiClient(c) {}
  LookaheadClient(const LookaheadClient&) = default;
  LookaheadClient& operator=(const LookaheadClient&) = default;
  ~LookaheadClient() override = default;

  // Reads from the socket until the pending bytes hold a complete header block (see
  // miblo::gatherHeaders), waiting at most budgetMs with delay(1). The bytes are replayed below.
  miblo::GatherResult gatherHeaders(uint32_t budgetMs) {
    Source src{*this};
    return miblo::gatherHeaders(ahead_, src, budgetMs);
  }
  // The bytes read ahead and not consumed yet, as one header block.
  miblo::HeaderVerdict aheadVerdict() const { return ahead_.verdict(); }
  bool hasAhead() const { return ahead_.pending() > 0; }

  // Reading: the read-ahead bytes first, then the socket.
  int available() override { return (int)ahead_.pending() + WiFiClient::available(); }
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
  void stop() override {
    ahead_.clear();
    WiFiClient::stop();
  }

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
    bool connected() override { return c.WiFiClient::connected(); }
    uint32_t nowMs() override { return millis(); }
    void wait() override { delay(1); }
    LookaheadClient& c;
  };

  miblo::HeaderBuffer ahead_;
};

class LookaheadServer : public WiFiServer {
 public:
  using WiFiServer::WiFiServer;
  using ClientType = LookaheadClient;
  LookaheadClient accept() { return LookaheadClient(WiFiServer::accept()); }
};
