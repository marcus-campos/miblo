// The settings page's "Preview on Miblo" (miblo_preview.h): the unsaved look, validated as a
// settings patch, shown for kShowMs, at most one every kEveryMs.
#include <unity.h>

#include <string>

#include "miblo_preview.h"

using namespace miblo;

void setUp() {}
void tearDown() {}

// `bad` points into the JSON document (as on the device, where the document outlives its use):
// copied out here, since this helper's document dies when it returns.
static std::string badField;
static bool parse(const Config& cfg, const char* json, PreviewLook& out, const char** bad = nullptr) {
  StaticJsonDocument<512> doc;
  deserializeJson(doc, json);
  Config scratch;  // the device allocates it on the heap (web.cpp handlePreview)
  scratch.accHead = 7;  // whatever it held before is overwritten with `cfg`
  const char* field = nullptr;
  const bool ok = previewFromJson(cfg, scratch, doc.as<JsonObjectConst>(), out, &field);
  badField = field ? field : "";
  if (bad) *bad = field ? badField.c_str() : nullptr;
  return ok;
}

static void test_look_from_the_form() {
  Config cfg;
  cfg.pet = 3;
  cfg.accNeck = 16;
  PreviewLook l;
  TEST_ASSERT_TRUE(parse(cfg,
                         "{\"pet\":9,\"mascot\":2,\"petEyes\":1,\"petColors\":\"ff0000,,,,,,00ff00\","
                         "\"accHead\":5,\"accFace\":20}",
                         l));
  TEST_ASSERT_EQUAL_UINT8(9, l.pet);
  TEST_ASSERT_EQUAL_UINT8(2, l.mascot);
  TEST_ASSERT_EQUAL_UINT8(1, l.petEyes);
  TEST_ASSERT_EQUAL_UINT32(0xFF0000 + 1, l.petColors[0]);
  TEST_ASSERT_EQUAL_UINT32(kPetAuto, l.petColors[1]);
  TEST_ASSERT_EQUAL_UINT32(0x00FF00 + 1, l.petColors[6]);
  TEST_ASSERT_EQUAL_UINT8(5, l.accHead);
  TEST_ASSERT_EQUAL_UINT8(20, l.accFace);
  TEST_ASSERT_EQUAL_UINT8(16, l.accNeck);  // absent: the saved one
  // Nothing is saved.
  TEST_ASSERT_EQUAL_UINT8(3, cfg.pet);
  TEST_ASSERT_EQUAL_UINT8(0, cfg.accHead);
}

static void test_invalid_look_is_refused() {
  Config cfg;
  PreviewLook l;
  const char* const bad[][2] = {{"{\"accFace\":13}", "accFace"}, {"{\"accHead\":11}", "accHead"},
                                {"{\"pet\":200}", "pet"},        {"{\"petColors\":\"zz\"}", "petColors"},
                                {"{\"petEyes\":9}", "petEyes"}};
  for (const auto& b : bad) {
    const char* field = nullptr;
    TEST_ASSERT_FALSE_MESSAGE(parse(cfg, b[0], l, &field), b[0]);
    TEST_ASSERT_EQUAL_STRING(b[1], field);
  }
}

static void test_shown_for_a_while_and_rate_limited() {
  LookPreview p;
  PreviewLook l;
  l.pet = 4;
  TEST_ASSERT_FALSE(p.active(0));
  TEST_ASSERT_TRUE(p.start(l, 1000));
  TEST_ASSERT_TRUE(p.active(1000));
  TEST_ASSERT_EQUAL_UINT8(4, p.look().pet);
  TEST_ASSERT_EQUAL_UINT32(500, p.elapsed(1500));
  // Another within kEveryMs: refused, the first stays.
  l.pet = 5;
  TEST_ASSERT_FALSE(p.start(l, 1000 + LookPreview::kEveryMs - 1));
  TEST_ASSERT_EQUAL_UINT8(4, p.look().pet);
  // After it: the new look, from the start again.
  TEST_ASSERT_TRUE(p.start(l, 1000 + LookPreview::kEveryMs));
  TEST_ASSERT_EQUAL_UINT8(5, p.look().pet);
  const uint32_t t0 = 1000 + LookPreview::kEveryMs;
  TEST_ASSERT_TRUE(p.active(t0 + LookPreview::kShowMs - 1));
  TEST_ASSERT_FALSE(p.active(t0 + LookPreview::kShowMs));
  // An alert ends it at once.
  TEST_ASSERT_TRUE(p.start(l, t0 + 60000));
  p.end();
  TEST_ASSERT_FALSE(p.active(t0 + 60001));
  // Across the millis() wrap.
  LookPreview w;
  TEST_ASSERT_TRUE(w.start(l, 0xFFFFF000u));
  TEST_ASSERT_TRUE(w.active(0x00000100u));
  TEST_ASSERT_FALSE(w.active(0xFFFFF000u + LookPreview::kShowMs));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_look_from_the_form);
  RUN_TEST(test_invalid_look_is_refused);
  RUN_TEST(test_shown_for_a_while_and_rate_limited);
  return UNITY_END();
}
