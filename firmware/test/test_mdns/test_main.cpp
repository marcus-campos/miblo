#include <string.h>
#include <unity.h>

#include <string>
#include <vector>

#include "miblo_mdns.h"

using namespace miblo;

void setUp() {}
void tearDown() {}

static MdnsInfo info() {
  MdnsInfo i{};
  i.instance = "Miblo-4F2A";
  i.host = "miblo-4f2a";
  i.ip[0] = 192;
  i.ip[1] = 168;
  i.ip[2] = 0;
  i.ip[3] = 42;
  i.port = 80;
  i.txt[0] = "id=miblo-4f2a";
  i.txt[1] = "name=Miblo-4F2A";
  i.txt[2] = "fw=0.1.0";
  i.txtCount = 3;
  return i;
}

static void putName(std::vector<uint8_t>& b, const std::string& name) {
  size_t start = 0;
  while (start < name.size()) {
    size_t dot = name.find('.', start);
    if (dot == std::string::npos) dot = name.size();
    b.push_back((uint8_t)(dot - start));
    b.insert(b.end(), name.begin() + start, name.begin() + dot);
    start = dot + 1;
  }
  b.push_back(0);
}

// Same query as plugin/lib/mdns.js buildQuery(): id 0, 1 question, PTR, class IN + QU bit.
static std::vector<uint8_t> query(const std::string& name, uint16_t type, bool qu, uint16_t id = 0) {
  std::vector<uint8_t> b = {(uint8_t)(id >> 8), (uint8_t)id, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0};
  putName(b, name);
  b.push_back((uint8_t)(type >> 8));
  b.push_back((uint8_t)type);
  b.push_back(qu ? 0x80 : 0x00);
  b.push_back(0x01);
  return b;
}

struct Rec {
  std::string name;
  uint16_t type;
  std::string data;  // PTR/SRV-target: name; A: "a.b.c.d"; TXT: strings joined by '|'; SRV: "port target"
};

static std::string readName(const uint8_t* p, size_t len, size_t& off) {
  std::string out;
  size_t o = off;
  bool jumped = false;
  while (o < len && p[o]) {
    if ((p[o] & 0xC0) == 0xC0) {
      if (!jumped) off = o + 2;
      jumped = true;
      o = ((p[o] & 0x3F) << 8) | p[o + 1];
      continue;
    }
    if (!out.empty()) out += ".";
    out.append((const char*)p + o + 1, p[o]);
    o += 1 + p[o];
  }
  if (!jumped) off = o + 1;
  return out;
}

static std::vector<Rec> parse(const uint8_t* p, size_t len, uint16_t& id, uint16_t& qd, uint16_t& an, uint16_t& ar) {
  id = (uint16_t)(p[0] << 8 | p[1]);
  qd = (uint16_t)(p[4] << 8 | p[5]);
  an = (uint16_t)(p[6] << 8 | p[7]);
  ar = (uint16_t)(p[10] << 8 | p[11]);
  size_t off = 12;
  for (int i = 0; i < qd; i++) {
    readName(p, len, off);
    off += 4;
  }
  std::vector<Rec> recs;
  for (int i = 0; i < an + ar; i++) {
    Rec r;
    r.name = readName(p, len, off);
    r.type = (uint16_t)(p[off] << 8 | p[off + 1]);
    uint16_t rdlen = (uint16_t)(p[off + 8] << 8 | p[off + 9]);
    size_t rd = off + 10;
    if (r.type == 12) {
      size_t o = rd;
      r.data = readName(p, len, o);
    } else if (r.type == 33) {
      size_t o = rd + 6;
      r.data = std::to_string(p[rd + 4] << 8 | p[rd + 5]) + " " + readName(p, len, o);
    } else if (r.type == 1) {
      r.data = std::to_string(p[rd]) + "." + std::to_string(p[rd + 1]) + "." + std::to_string(p[rd + 2]) + "." +
               std::to_string(p[rd + 3]);
    } else if (r.type == 16) {
      for (size_t o = rd; o < rd + rdlen; o += 1 + p[o]) {
        if (!r.data.empty()) r.data += "|";
        r.data.append((const char*)p + o + 1, p[o]);
      }
    }
    recs.push_back(r);
    off = rd + rdlen;
  }
  TEST_ASSERT_EQUAL_UINT32(len, off);
  return recs;
}

static const Rec* find(const std::vector<Rec>& recs, uint16_t type) {
  for (const auto& r : recs) {
    if (r.type == type) return &r;
  }
  return nullptr;
}

static void test_bridge_query_gets_unicast_answer_with_everything() {
  MdnsInfo in = info();
  auto q = query("_miblo._tcp.local", 12, true, 0);
  uint8_t out[512];
  MdnsReply r = mdnsRespond(q.data(), q.size(), 53123, in, out, sizeof(out));
  TEST_ASSERT_TRUE(r.len > 0);
  TEST_ASSERT_TRUE(r.unicast);
  uint16_t id, qd, an, ar;
  auto recs = parse(out, r.len, id, qd, an, ar);
  TEST_ASSERT_EQUAL_UINT16(1, qd);  // legacy: question echoed back
  TEST_ASSERT_EQUAL_UINT16(1, an);
  TEST_ASSERT_EQUAL_UINT16(3, ar);
  const Rec* ptr = find(recs, 12);
  TEST_ASSERT_NOT_NULL(ptr);
  TEST_ASSERT_EQUAL_STRING("_miblo._tcp.local", ptr->name.c_str());
  TEST_ASSERT_EQUAL_STRING("Miblo-4F2A._miblo._tcp.local", ptr->data.c_str());
  TEST_ASSERT_EQUAL_STRING("80 miblo-4f2a.local", find(recs, 33)->data.c_str());
  TEST_ASSERT_EQUAL_STRING("id=miblo-4f2a|name=Miblo-4F2A|fw=0.1.0", find(recs, 16)->data.c_str());
  TEST_ASSERT_EQUAL_STRING("192.168.0.42", find(recs, 1)->data.c_str());
  TEST_ASSERT_EQUAL_STRING("miblo-4f2a.local", find(recs, 1)->name.c_str());
}

static void test_legacy_query_echoes_id() {
  MdnsInfo in = info();
  auto q = query("_miblo._tcp.local", 12, false, 0xBEEF);
  uint8_t out[512];
  MdnsReply r = mdnsRespond(q.data(), q.size(), 40000, in, out, sizeof(out));
  TEST_ASSERT_TRUE(r.unicast);
  TEST_ASSERT_EQUAL_HEX8(0xBE, out[0]);
  TEST_ASSERT_EQUAL_HEX8(0xEF, out[1]);
}

static void test_multicast_query_for_host_address() {
  MdnsInfo in = info();
  auto q = query("MIBLO-4F2A.local", 1, false);
  uint8_t out[512];
  MdnsReply r = mdnsRespond(q.data(), q.size(), kMdnsPort, in, out, sizeof(out));
  TEST_ASSERT_TRUE(r.len > 0);
  TEST_ASSERT_FALSE(r.unicast);
  uint16_t id, qd, an, ar;
  auto recs = parse(out, r.len, id, qd, an, ar);
  TEST_ASSERT_EQUAL_UINT16(0, qd);
  TEST_ASSERT_EQUAL_UINT16(1, an);
  TEST_ASSERT_EQUAL_UINT16(0, ar);
  TEST_ASSERT_EQUAL_STRING("192.168.0.42", recs[0].data.c_str());
}

static void test_qu_bit_from_5353_is_unicast() {
  MdnsInfo in = info();
  auto q = query("miblo-4f2a.local", 1, true);
  uint8_t out[512];
  TEST_ASSERT_TRUE(mdnsRespond(q.data(), q.size(), kMdnsPort, in, out, sizeof(out)).unicast);
}

static void test_instance_srv_and_service_enumeration() {
  MdnsInfo in = info();
  uint8_t out[512];
  uint16_t id, qd, an, ar;
  auto q = query("Miblo-4F2A._miblo._tcp.local", 33, false);
  MdnsReply r = mdnsRespond(q.data(), q.size(), kMdnsPort, in, out, sizeof(out));
  auto recs = parse(out, r.len, id, qd, an, ar);
  TEST_ASSERT_EQUAL_UINT16(2, an);  // SRV + TXT
  TEST_ASSERT_EQUAL_UINT16(1, ar);  // A
  q = query("_services._dns-sd._udp.local", 12, false);
  r = mdnsRespond(q.data(), q.size(), kMdnsPort, in, out, sizeof(out));
  recs = parse(out, r.len, id, qd, an, ar);
  TEST_ASSERT_EQUAL_STRING("_miblo._tcp.local", recs[0].data.c_str());
}

static void test_ignores_other_names_responses_and_garbage() {
  MdnsInfo in = info();
  uint8_t out[512];
  auto q = query("_http._tcp.local", 12, true);
  TEST_ASSERT_EQUAL(0, mdnsRespond(q.data(), q.size(), 5353, in, out, sizeof(out)).len);
  q = query("_miblo._tcp.local", 12, true);
  q[2] = 0x84;  // it's a response from another device
  TEST_ASSERT_EQUAL(0, mdnsRespond(q.data(), q.size(), 5353, in, out, sizeof(out)).len);
  uint8_t garbage[] = {0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 60, 'x'};
  TEST_ASSERT_EQUAL(0, mdnsRespond(garbage, sizeof(garbage), 5353, in, out, sizeof(out)).len);
  q = query("_miblo._tcp.local", 12, true);
  TEST_ASSERT_EQUAL(0, mdnsRespond(q.data(), q.size(), 5353, in, out, 40).len);  // doesn't fit
}

static void test_announcement_contains_all_records() {
  MdnsInfo in = info();
  uint8_t out[512];
  size_t n = mdnsAnnounce(in, out, sizeof(out));
  TEST_ASSERT_TRUE(n > 0);
  uint16_t id, qd, an, ar;
  auto recs = parse(out, n, id, qd, an, ar);
  TEST_ASSERT_EQUAL_UINT16(4, an);
  TEST_ASSERT_NOT_NULL(find(recs, 12));
  TEST_ASSERT_NOT_NULL(find(recs, 33));
  TEST_ASSERT_NOT_NULL(find(recs, 16));
  TEST_ASSERT_NOT_NULL(find(recs, 1));
}

// A paired Miblo tells the network only its id: the TXT record carries "id=" and nothing else,
// and the instance is the id-derived default name (never the name the owner gave it).
static void test_public_identity_announces_only_the_id() {
  MdnsInfo in{};
  in.ip[0] = 192;
  in.ip[1] = 168;
  in.ip[3] = 42;
  in.port = 80;
  char txt[32];
  mdnsPublicIdentity(in, "miblo-4f2a", "Miblo-4F2A", txt, sizeof(txt));
  TEST_ASSERT_EQUAL_UINT8(1, in.txtCount);
  TEST_ASSERT_EQUAL_STRING("id=miblo-4f2a", in.txt[0]);
  TEST_ASSERT_EQUAL_STRING("Miblo-4F2A", in.instance);
  TEST_ASSERT_EQUAL_STRING("miblo-4f2a", in.host);
  uint8_t out[512];
  size_t n = mdnsAnnounce(in, out, sizeof(out));
  TEST_ASSERT_TRUE(n > 0);
  uint16_t id, qd, an, ar;
  auto recs = parse(out, n, id, qd, an, ar);
  const Rec* t = find(recs, 16);
  TEST_ASSERT_NOT_NULL(t);
  TEST_ASSERT_EQUAL_STRING("id=miblo-4f2a", t->data.c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_public_identity_announces_only_the_id);
  RUN_TEST(test_bridge_query_gets_unicast_answer_with_everything);
  RUN_TEST(test_legacy_query_echoes_id);
  RUN_TEST(test_multicast_query_for_host_address);
  RUN_TEST(test_qu_bit_from_5353_is_unicast);
  RUN_TEST(test_instance_srv_and_service_enumeration);
  RUN_TEST(test_ignores_other_names_responses_and_garbage);
  RUN_TEST(test_announcement_contains_all_records);
  return UNITY_END();
}
