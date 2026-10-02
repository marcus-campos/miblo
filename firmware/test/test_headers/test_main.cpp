// The request-header read-ahead (miblo_headers): gathering a header block that spans several TCP
// segments, replaying it to the server, and the body guard's decision.
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include <string>
#include <vector>

#include "miblo_headers.h"

using namespace miblo;

void setUp() {}
void tearDown() {}

// A connection that delivers `segments`, one each `gapMs` of fake time, then idles (or closes).
struct FakeSource : ByteSource {
  std::vector<std::string> segments;
  uint32_t gapMs = 2;
  bool closeAtEnd = false;
  uint32_t now = 0;
  uint32_t startedAt = 0;
  size_t next = 0;      // next segment to arrive
  std::string rx;       // arrived, not read yet
  uint32_t waits = 0;

  void arrive() {
    while (next < segments.size() && now - startedAt >= gapMs * next) rx += segments[next++];
  }
  size_t available() override {
    arrive();
    return rx.size();
  }
  size_t read(char* dst, size_t n) override {
    arrive();
    if (n > rx.size()) n = rx.size();
    memcpy(dst, rx.data(), n);
    rx.erase(0, n);
    return n;
  }
  bool connected() override { return !(closeAtEnd && next == segments.size() && rx.empty()); }
  uint32_t nowMs() override { return now; }
  void wait() override {
    now++;
    waits++;
  }
};

// Drains the buffer the way the server reads it (byte by byte, then the peek API).
static std::string drain(HeaderBuffer& b) {
  std::string out;
  int c;
  while (b.pending() > 10 && (c = b.readByte()) >= 0) out += (char)c;
  out.append(b.data(), b.pending());
  b.consume(b.pending());
  return out;
}

// What desktop Chrome sends for a fetch() POST from the settings page, with a long
// Accept-Language and a cookie left on the address by another device: 1.1+ KB.
static std::string chromeHeaders(const char* contentType) {
  std::string h;
  h += "Host: 192.168.1.47\r\n";
  h += "Connection: keep-alive\r\n";
  h += "Cache-Control: no-cache\r\n";
  h += "Content-Length: 812\r\n";
  h += "sec-ch-ua-platform: \"macOS\"\r\n";
  h += "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) "
       "Chrome/131.0.0.0 Safari/537.36\r\n";
  h += "sec-ch-ua: \"Google Chrome\";v=\"131\", \"Chromium\";v=\"131\", \"Not_A Brand\";v=\"24\"\r\n";
  h += std::string("Content-Type: ") + contentType + "\r\n";
  h += "sec-ch-ua-mobile: ?0\r\n";
  h += "X-Miblo-Web: 1\r\n";
  h += "Accept: */*\r\n";
  h += "Origin: http://192.168.1.47\r\n";
  h += "Sec-Fetch-Site: same-origin\r\n";
  h += "Sec-Fetch-Mode: cors\r\n";
  h += "Sec-Fetch-Dest: empty\r\n";
  h += "Referer: http://192.168.1.47/\r\n";
  h += "Accept-Encoding: gzip, deflate\r\n";
  h += "Accept-Language: pt-BR,pt;q=0.9,en-US;q=0.8,en;q=0.7,es;q=0.6,fr;q=0.5,de;q=0.4,it;q=0.3,ja;q=0.2\r\n";
  h += "Cookie: _ga=GA1.1.1234567890.1700000000; _ga_ABCDEF1234=GS1.1.1700000000.3.1.1700000500.0.0.0; "
       "sessionid=8f14e45fceea167a5a36dedd4bea2543c9f0f895fb98ab9159f51fd0297e236d; csrftoken=Zm9vYmFyYmF6cXV4"
       "cXV1eGNvcmdlZ3JhdWx0Z2FycGx5d2FsZG9mcmVk; theme=dark; lang=pt-BR; tz=America%2FSao_Paulo; "
       "router_admin_seen=1; last_tab=wireless; consent=%7B%22analytics%22%3Atrue%2C%22ads%22%3Afalse%7D; "
       "prefs=eyJ1bml0cyI6Im1ldHJpYyIsImRlbnNpdHkiOiJjb21wYWN0In0\r\n";
  h += "\r\n";
  return h;
}

// Splits `s` into TCP segments of the ESP8266's MSS (536 B).
static std::vector<std::string> segments(const std::string& s, size_t mss = 536) {
  std::vector<std::string> out;
  for (size_t i = 0; i < s.size(); i += mss) out.push_back(s.substr(i, mss));
  return out;
}

static void test_chrome_headers_across_three_segments_are_gathered() {
  const std::string h = chromeHeaders("application/json");
  TEST_ASSERT_GREATER_OR_EQUAL(1100, h.size());
  TEST_ASSERT_LESS_THAN(HeaderBuffer::kCap, h.size());
  const std::string body(812, '{');
  FakeSource src;
  src.segments = segments(h + body);
  TEST_ASSERT_EQUAL(4, src.segments.size());  // 3 segments of headers, the body after them
  HeaderBuffer b;
  TEST_ASSERT_EQUAL(GatherResult::Ready, gatherHeaders(b, src, kHeaderWaitMs));
  TEST_ASSERT_EQUAL(HeaderVerdict::Plain, b.verdict());
  TEST_ASSERT_LESS_OR_EQUAL(kHeaderWaitMs, src.now);
  // Replayed byte for byte: the server reads exactly what the client sent.
  std::string all = drain(b);
  char rest[2048];
  size_t n;
  while ((n = src.read(rest, sizeof(rest))) > 0) all.append(rest, n);
  while (src.next < src.segments.size()) {  // segments still on the way
    src.wait();
    while ((n = src.read(rest, sizeof(rest))) > 0) all.append(rest, n);
  }
  TEST_ASSERT_TRUE(all == h + body);
  TEST_ASSERT_FALSE(b.allocated());  // freed once drained
}

static void test_complete_first_segment_needs_no_buffer() {
  FakeSource src;
  src.segments = {"Host: x\r\nContent-Length: 2\r\n\r\n{}"};
  HeaderBuffer b;
  // Nothing read ahead yet: the caller judges the first segment in place and gathers only when
  // it is not enough.
  TEST_ASSERT_EQUAL(HeaderVerdict::Incomplete, b.verdict());
  TEST_ASSERT_EQUAL(GatherResult::Ready, gatherHeaders(b, src, kHeaderWaitMs));
  TEST_ASSERT_EQUAL(HeaderVerdict::Plain, b.verdict());
}

static void test_padding_bypass_with_a_late_multipart_content_type_is_seen() {
  // A harmless Content-Type first, 700 B of padding, then the multipart one (the server keeps the
  // last): the first segment alone looks plain-ish; the whole block does not.
  std::string h = "Host: x\r\nContent-Type: application/json\r\nX-Pad: " + std::string(700, 'a') +
                  "\r\nContent-Type: multipart/form-data; boundary=" + std::string(400, 'B') + "\r\n\r\n";
  FakeSource src;
  src.segments = segments(h);
  HeaderBuffer b;
  TEST_ASSERT_EQUAL(GatherResult::Ready, gatherHeaders(b, src, kHeaderWaitMs));
  TEST_ASSERT_EQUAL(HeaderVerdict::BadMultipart, b.verdict());
  TEST_ASSERT_EQUAL(BodyAction::BadRequest, decideBody(b.verdict(), true, false, false, false));  // POST /settings
  TEST_ASSERT_EQUAL(BodyAction::BadBoundary, decideBody(b.verdict(), true, true, true, false));  // even armed
}

static void test_slowloris_is_refused_within_the_wait() {
  FakeSource src;
  src.segments = {"Host: x\r\n", "X-A: 1\r\n", "X-B: 2\r\n"};
  src.gapMs = 400;  // a line every 400 ms, never the blank line
  HeaderBuffer b;
  TEST_ASSERT_EQUAL(GatherResult::TimedOut, gatherHeaders(b, src, kHeaderWaitMs));
  TEST_ASSERT_LESS_OR_EQUAL(kHeaderWaitMs + 1, src.now);
  TEST_ASSERT_EQUAL(BodyAction::HeadersIncomplete, decideGather(GatherResult::TimedOut));
  b.clear();
  TEST_ASSERT_FALSE(b.allocated());
}

static void test_a_retransmitted_segment_still_arrives_in_time() {
  // The second segment was lost on a weak link and comes back after a retransmission timeout
  // (lwIP's minimum RTO is in the hundreds of ms): the wait covers it.
  FakeSource src;
  src.segments = segments(chromeHeaders("application/json"));
  src.gapMs = 600;
  HeaderBuffer b;
  TEST_ASSERT_GREATER_OR_EQUAL(1000, kHeaderWaitMs);
  TEST_ASSERT_LESS_OR_EQUAL(2000, kHeaderWaitMs);  // still bounded
  src.segments = {src.segments[0], src.segments[1] + src.segments[2]};
  TEST_ASSERT_EQUAL(GatherResult::Ready, gatherHeaders(b, src, kHeaderWaitMs));
  TEST_ASSERT_EQUAL(HeaderVerdict::Plain, b.verdict());
}

// drainInput: the lingering close after a refused upload.
static void test_drain_input_is_bounded() {
  FakeSource src;  // a body that keeps coming: 64 KB, 1 KB per ms
  for (int i = 0; i < 64; i++) src.segments.push_back(std::string(1024, 'x'));
  src.gapMs = 1;
  TEST_ASSERT_EQUAL(32768, drainInput(src, kLingerMs, kLingerMaxBytes));  // stops at the byte cap
  FakeSource slow;  // a trickle: stops at the time cap
  for (int i = 0; i < 64; i++) slow.segments.push_back("y");
  slow.gapMs = 100;
  const size_t n = drainInput(slow, kLingerMs, kLingerMaxBytes);
  TEST_ASSERT_LESS_OR_EQUAL(kLingerMs + 1, slow.now);
  TEST_ASSERT_LESS_OR_EQUAL(11, n);
  FakeSource closing;  // the client gives up: stops at once
  closing.segments = {"abc"};
  closing.closeAtEnd = true;
  TEST_ASSERT_EQUAL(3, drainInput(closing, kLingerMs, kLingerMaxBytes));
  TEST_ASSERT_LESS_OR_EQUAL(1, closing.now);
}

static void test_oversized_header_block_is_refused() {
  FakeSource src;
  src.segments = segments("Host: x\r\nX-Pad: " + std::string(4000, 'a') + "\r\n\r\n");
  HeaderBuffer b;
  TEST_ASSERT_EQUAL(GatherResult::TooLarge, gatherHeaders(b, src, kHeaderWaitMs));
  TEST_ASSERT_EQUAL(BodyAction::HeadersTooLarge, decideGather(GatherResult::TooLarge));
  TEST_ASSERT_EQUAL(HeaderBuffer::kCap, b.pending());  // never more than the cap
}

static void test_closed_connection_is_refused() {
  FakeSource src;
  src.segments = {"Host: x\r\n"};
  src.closeAtEnd = true;
  HeaderBuffer b;
  TEST_ASSERT_EQUAL(GatherResult::Closed, gatherHeaders(b, src, kHeaderWaitMs));
  TEST_ASSERT_EQUAL(BodyAction::HeadersIncomplete, decideGather(GatherResult::Closed));
}

// pollRequest: the connection is handed to ESP8266WebServer (whose parser blocks on every line
// for up to 5 s) only once the whole request it will read is there. Never waits.
static void test_poll_waits_without_blocking_for_the_header_block() {
  FakeSource src;
  src.segments = {"GET /api/info HTTP/1.1\r\n", "Host: x\r\n", "\r\n"};
  src.gapMs = 300;
  HeaderBuffer b;
  TEST_ASSERT_EQUAL(RequestReadiness::Waiting, pollRequest(b, src, kBodyWaitMs));
  TEST_ASSERT_EQUAL(0, src.waits);  // never waited
  TEST_ASSERT_EQUAL(0, src.now);
  src.now = 300;
  TEST_ASSERT_EQUAL(RequestReadiness::Waiting, pollRequest(b, src, kBodyWaitMs));
  src.now = 600;
  TEST_ASSERT_EQUAL(RequestReadiness::Ready, pollRequest(b, src, kBodyWaitMs));
  TEST_ASSERT_EQUAL(0, src.waits);
  TEST_ASSERT_EQUAL_STRING_LEN("GET /api/info HTTP/1.1\r\nHost: x\r\n\r\n", b.data(), b.pending());
}

static void test_poll_a_stalled_request_never_becomes_ready() {
  FakeSource src;  // the request line and one header, then nothing: the attack
  src.segments = {"GET /api/info HTTP/1.1\r\nHost: x\r\n"};
  HeaderBuffer b;
  for (int i = 0; i < 100; i++) {
    src.now += 100;
    TEST_ASSERT_EQUAL(RequestReadiness::Waiting, pollRequest(b, src, kBodyWaitMs));
  }
  TEST_ASSERT_EQUAL(0, src.waits);
  FakeSource bare;  // no request line end at all
  bare.segments = {"GET /api/info HTTP/1.1"};
  HeaderBuffer b2;
  TEST_ASSERT_EQUAL(RequestReadiness::Waiting, pollRequest(b2, bare, kBodyWaitMs));
}

// Once the header block is here, a small body still to come is waited for with a short blocking
// wait (kBodyWaitMs): released in the server's non-blocking state, a request whose body was one
// segment behind was dropped by its 30 ms rule whenever another client had data.
static void test_poll_waits_briefly_for_a_small_body() {
  FakeSource src;  // headers in one segment, the body in two more, a few ms apart (Node's fetch)
  src.segments = {"POST /api/say HTTP/1.1\r\nContent-Length: 12\r\n\r\n", "{\"text\":", "\"a\"}"};
  src.gapMs = 3;
  HeaderBuffer b;
  TEST_ASSERT_EQUAL(RequestReadiness::Ready, pollRequest(b, src, kBodyWaitMs));
  TEST_ASSERT_EQUAL(6, src.now);  // waited for the last segment only
  // The body is counted in the socket, not read into the buffer.
  TEST_ASSERT_EQUAL_STRING_LEN("POST /api/say HTTP/1.1\r\nContent-Length: 12\r\n\r\n", b.data(), b.pending());
  TEST_ASSERT_EQUAL(12, src.rx.size());

  // A client using Nagle (Java, Arduino HTTPClient) writes the body only once its headers are
  // ACKed; judged in place, nothing is ACKed early, so lwIP's delayed ACK (up to 250 ms) comes first.
  FakeSource nagle;
  nagle.segments = {"POST /api/say HTTP/1.1\r\nContent-Length: 12\r\n\r\n", "{\"text\":\"a\"}"};
  nagle.gapMs = 250;
  HeaderBuffer bn;
  TEST_ASSERT_EQUAL(RequestReadiness::Ready, pollRequest(bn, nagle, kBodyWaitMs));

  FakeSource stalled;  // the body never comes: the attack costs kBodyWaitMs, then 408
  stalled.segments = {"POST /api/say HTTP/1.1\r\ncontent-length: 50\r\n\r\n{\"te"};
  HeaderBuffer b2;
  TEST_ASSERT_EQUAL(RequestReadiness::BodyTimeout, pollRequest(b2, stalled, kBodyWaitMs));
  TEST_ASSERT_EQUAL(kBodyWaitMs, stalled.now);

  FakeSource gone;  // the peer closes mid-body
  gone.segments = {"POST /api/say HTTP/1.1\r\nContent-Length: 50\r\n\r\n{"};
  gone.closeAtEnd = true;
  HeaderBuffer b3;
  TEST_ASSERT_EQUAL(RequestReadiness::Closed, pollRequest(b3, gone, kBodyWaitMs));
  TEST_ASSERT_EQUAL(0, gone.waits);

  // Duplicated Content-Length: the largest counts.
  FakeSource dup;
  dup.segments = {"POST /x HTTP/1.1\r\nContent-Length: 2\r\nContent-Length: 9\r\n\r\nab"};
  HeaderBuffer b4;
  TEST_ASSERT_EQUAL(RequestReadiness::BodyTimeout, pollRequest(b4, dup, kBodyWaitMs));
}

// A slow header block is still never waited for: a half-sent header block costs nothing here.
static void test_poll_does_not_wait_for_headers_after_the_body_rule() {
  FakeSource src;
  src.segments = {"POST /api/say HTTP/1.1\r\nContent-Len"};
  HeaderBuffer b;
  TEST_ASSERT_EQUAL(RequestReadiness::Waiting, pollRequest(b, src, kBodyWaitMs));
  TEST_ASSERT_EQUAL(0, src.waits);
}

static void test_poll_a_large_body_is_not_held_back() {
  // Larger than what the TCP window can hold unread: waiting for it would never end. The server
  // reads it with its own (bounded) wait.
  FakeSource src;
  src.segments = {"POST /api/state HTTP/1.1\r\nContent-Length: 6000\r\n\r\n{"};
  HeaderBuffer b;
  TEST_ASSERT_GREATER_THAN(kBodyHoldMax, 6000);
  TEST_ASSERT_EQUAL(RequestReadiness::Ready, pollRequest(b, src, kBodyWaitMs));
  FakeSource none;  // no Content-Length: no body
  none.segments = {"POST /api/find HTTP/1.1\r\nHost: x\r\n\r\n"};
  HeaderBuffer b2;
  TEST_ASSERT_EQUAL(RequestReadiness::Ready, pollRequest(b2, none, kBodyWaitMs));
}

static void test_poll_closed_too_large_and_body_counts_socket_bytes() {
  FakeSource closed;
  closed.segments = {"GET / HTTP/1.1\r\nHost"};
  closed.closeAtEnd = true;
  HeaderBuffer b;
  TEST_ASSERT_EQUAL(RequestReadiness::Closed, pollRequest(b, closed, kBodyWaitMs));
  FakeSource big;
  big.segments = segments("GET / HTTP/1.1\r\nX-Pad: " + std::string(4000, 'a') + "\r\n\r\n");
  big.gapMs = 0;
  HeaderBuffer b2;
  TEST_ASSERT_EQUAL(RequestReadiness::TooLarge, pollRequest(b2, big, kBodyWaitMs));
  TEST_ASSERT_EQUAL(HeaderBuffer::kCap, b2.pending());
  // A body already complete in the socket while the buffer is full of headers: ready, without
  // reading past the buffer.
  std::string head = "POST /api/say HTTP/1.1\r\nX-Pad: " + std::string(1960, 'p') + "\r\nContent-Length: 100\r\n\r\n";
  TEST_ASSERT_LESS_THAN(HeaderBuffer::kCap, head.size());
  TEST_ASSERT_GREATER_THAN(HeaderBuffer::kCap, head.size() + 100);
  FakeSource full;
  full.segments = {head + std::string(100, 'b')};
  HeaderBuffer b3;
  TEST_ASSERT_EQUAL(RequestReadiness::Ready, pollRequest(b3, full, kBodyWaitMs));
}

// The hook reads two things from the gathered block before a large body is read: its length and
// the caller's credentials.
static void test_request_body_length_and_header_lookup() {
  const std::string h = "Host: x\r\ncontent-LENGTH:  6000\r\nAuthorization: Bearer abc123\r\n"
                        "authorization: Bearer last\r\n\r\n{\"s";
  TEST_ASSERT_EQUAL(6000, requestBodyLength(h.data(), h.size()));
  char v[40];
  TEST_ASSERT_TRUE(findHeader(h.data(), h.size(), "authorization", v, sizeof(v)));
  TEST_ASSERT_EQUAL_STRING("Bearer last", v);  // the last one wins, as in the server
  TEST_ASSERT_FALSE(findHeader(h.data(), h.size(), "x-miblo-web", v, sizeof(v)));
  char tiny[4];
  TEST_ASSERT_FALSE(findHeader(h.data(), h.size(), "authorization", tiny, sizeof(tiny)));  // never cut
  const std::string partial = "Host: x\r\nContent-Length: 9\r\n";  // no blank line yet
  TEST_ASSERT_EQUAL(0, requestBodyLength(partial.data(), partial.size()));
  const std::string body = "Host: x\r\n\r\nAuthorization: Bearer fake\r\n";  // past the block: body
  TEST_ASSERT_FALSE(findHeader(body.data(), body.size(), "authorization", v, sizeof(v)));
  const std::string huge = "Content-Length: 99999999999999999999\r\n\r\n";
  TEST_ASSERT_GREATER_THAN(kBodyHoldMax, requestBodyLength(huge.data(), huge.size()));
}

// The first received segment judged in place (no heap buffer); the source counts it among its
// unread bytes, as WiFiClient::available() does.
static RequestReadiness inPlace(const std::string& first, FakeSource& src) {
  src.segments.insert(src.segments.begin(), first);
  return requestInPlace(first.data(), first.size(), src, kBodyWaitMs);
}

static void test_request_in_place() {
  FakeSource a;
  TEST_ASSERT_EQUAL(RequestReadiness::Ready, inPlace("GET / HTTP/1.1\r\nHost: x\r\n\r\n", a));
  TEST_ASSERT_EQUAL(0, a.waits);
  FakeSource b;  // blank line missing: not judged here (pollRequest gathers it)
  TEST_ASSERT_EQUAL(RequestReadiness::Waiting, inPlace("GET / HTTP/1.1\r\nHost: x\r\n", b));
  TEST_ASSERT_EQUAL(0, b.waits);
  FakeSource none;
  TEST_ASSERT_EQUAL(RequestReadiness::Waiting, requestInPlace(nullptr, 0, none, kBodyWaitMs));
  const std::string post = "POST /api/say HTTP/1.1\r\nContent-Length: 10\r\n\r\nabcd";
  FakeSource c;  // the rest of the body is already in the socket
  c.segments = {"efghij"};
  c.gapMs = 0;
  TEST_ASSERT_EQUAL(RequestReadiness::Ready, inPlace(post, c));
  TEST_ASSERT_EQUAL(0, c.waits);
  FakeSource d;  // it follows 5 ms later
  d.segments = {"efghij"};
  d.gapMs = 5;
  TEST_ASSERT_EQUAL(RequestReadiness::Ready, inPlace(post, d));
  TEST_ASSERT_EQUAL(5, d.now);
  FakeSource e;  // one byte short, for good
  e.segments = {"efghi"};
  e.gapMs = 0;
  TEST_ASSERT_EQUAL(RequestReadiness::BodyTimeout, inPlace(post, e));
  TEST_ASSERT_EQUAL(kBodyWaitMs, e.now);
  FakeSource f;  // a large body is never held back
  TEST_ASSERT_EQUAL(RequestReadiness::Ready, inPlace("POST /api/state HTTP/1.1\r\nContent-Length: 6000\r\n\r\n", f));
  TEST_ASSERT_EQUAL(0, f.waits);
}

static void test_body_decisions() {
  char h[256];
  snprintf(h, sizeof(h), "Content-Type: multipart/form-data; boundary=%040d\r\n\r\n", 1);
  const HeaderVerdict b40 = checkRequestHeaders(h, strlen(h));
  snprintf(h, sizeof(h), "Content-Type: multipart/form-data; boundary=%071d\r\n\r\n", 1);
  const HeaderVerdict b71 = checkRequestHeaders(h, strlen(h));
  TEST_ASSERT_EQUAL(HeaderVerdict::Multipart, b40);
  TEST_ASSERT_EQUAL(HeaderVerdict::BadMultipart, b71);
  // decideBody(verdict, isPost, isUpdatePath, armed, locked)
  TEST_ASSERT_EQUAL(BodyAction::Continue, decideBody(b40, true, true, true, false));  // armed /update, 40 chars
  TEST_ASSERT_EQUAL(BodyAction::BadBoundary, decideBody(b71, true, true, true, false));
  TEST_ASSERT_EQUAL(BodyAction::NotOpen, decideBody(b40, true, true, false, false));
  TEST_ASSERT_EQUAL(BodyAction::Locked, decideBody(b40, true, true, false, true));
  TEST_ASSERT_EQUAL(BodyAction::BadRequest, decideBody(b40, false, true, true, false));  // PUT /update
  TEST_ASSERT_EQUAL(BodyAction::BadRequest, decideBody(b40, true, false, true, false));  // POST elsewhere
  TEST_ASSERT_EQUAL(BodyAction::CheckSize, decideBody(HeaderVerdict::Plain, true, false, false, false));
  TEST_ASSERT_EQUAL(BodyAction::BadRequest, decideBody(HeaderVerdict::Malformed, true, false, false, false));
  TEST_ASSERT_EQUAL(BodyAction::HeadersIncomplete, decideBody(HeaderVerdict::Incomplete, true, false, false, false));
  TEST_ASSERT_EQUAL(BodyAction::Continue, decideGather(GatherResult::Ready));  // judged by decideBody next
}

static void test_replay_and_copy() {
  HeaderBuffer b;
  TEST_ASSERT_EQUAL(0, b.pending());
  TEST_ASSERT_EQUAL(-1, b.readByte());
  TEST_ASSERT_EQUAL(-1, b.peekByte());
  TEST_ASSERT_EQUAL(5, b.append("hello", 5));
  HeaderBuffer c(b);  // deep copy
  TEST_ASSERT_EQUAL('h', b.peekByte());
  TEST_ASSERT_EQUAL('h', b.readByte());
  uint8_t out[8];
  TEST_ASSERT_EQUAL(2, b.peek(out, 2));
  TEST_ASSERT_EQUAL_MEMORY("el", out, 2);
  TEST_ASSERT_EQUAL(4, b.read(out, sizeof(out)));
  TEST_ASSERT_EQUAL_MEMORY("ello", out, 4);
  TEST_ASSERT_FALSE(b.allocated());
  TEST_ASSERT_EQUAL(5, c.pending());
  TEST_ASSERT_EQUAL_MEMORY("hello", c.data(), 5);
  HeaderBuffer d;
  d = c;
  c.clear();
  TEST_ASSERT_EQUAL_MEMORY("hello", d.data(), 5);
  d = d;  // self-assignment keeps the bytes
  TEST_ASSERT_EQUAL(5, d.pending());
  // Appending after a partial read keeps the order (pending bytes move to the front).
  d.consume(3);
  TEST_ASSERT_EQUAL(3, d.append("abc", 3));
  TEST_ASSERT_EQUAL(5, d.pending());
  TEST_ASSERT_EQUAL_MEMORY("loabc", d.data(), 5);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_chrome_headers_across_three_segments_are_gathered);
  RUN_TEST(test_complete_first_segment_needs_no_buffer);
  RUN_TEST(test_padding_bypass_with_a_late_multipart_content_type_is_seen);
  RUN_TEST(test_slowloris_is_refused_within_the_wait);
  RUN_TEST(test_a_retransmitted_segment_still_arrives_in_time);
  RUN_TEST(test_drain_input_is_bounded);
  RUN_TEST(test_oversized_header_block_is_refused);
  RUN_TEST(test_closed_connection_is_refused);
  RUN_TEST(test_poll_waits_without_blocking_for_the_header_block);
  RUN_TEST(test_poll_a_stalled_request_never_becomes_ready);
  RUN_TEST(test_poll_waits_briefly_for_a_small_body);
  RUN_TEST(test_poll_does_not_wait_for_headers_after_the_body_rule);
  RUN_TEST(test_poll_a_large_body_is_not_held_back);
  RUN_TEST(test_poll_closed_too_large_and_body_counts_socket_bytes);
  RUN_TEST(test_request_body_length_and_header_lookup);
  RUN_TEST(test_request_in_place);
  RUN_TEST(test_body_decisions);
  RUN_TEST(test_replay_and_copy);
  return UNITY_END();
}
