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
  RUN_TEST(test_body_decisions);
  RUN_TEST(test_replay_and_copy);
  return UNITY_END();
}
