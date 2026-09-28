#include <string.h>
#include <unity.h>

#include <string>
#include <vector>

#include "miblo_activity.h"
#include "miblo_i18n.h"
#include "miblo_utf8.h"

using namespace miblo;

void setUp() {}
void tearDown() {}

static std::string T(Lang l, S id) {
  char b[160];
  tr(l, id, b, sizeof(b));
  return b;
}

static std::vector<std::string> markers(const std::string& s) {
  std::vector<std::string> out;
  for (size_t i = 0; i + 1 < s.size(); i++) {
    if (s[i] == '%') out.push_back(s.substr(i, 2));
  }
  return out;
}

static void test_every_language_has_every_string_with_same_placeholders() {
  for (int l = 0; l < (int)Lang::Count; l++) {
    // the table has exactly S::Count entries: the entry after the last one is the empty string
    // the compiler puts at the end of the literal (the final "\0" plus the implicit terminator).
    const char* p = kLangTables[l];
    for (int i = 0; i < (int)S::Count; i++) {
      TEST_ASSERT_TRUE_MESSAGE(strlen(p) > 0, langCode((Lang)l));
      p += strlen(p) + 1;
    }
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, (uint8_t)*p, langCode((Lang)l));
    for (int i = 0; i < (int)S::Count; i++) {
      TEST_ASSERT_TRUE_MESSAGE(markers(T(Lang::En, (S)i)) == markers(T((Lang)l, (S)i)), langCode((Lang)l));
    }
    // web.cpp's tr() copies into a 256-byte buffer: no entry may be cut there.
    p = kLangTables[l];
    for (int i = 0; i < (int)S::Count; i++) {
      TEST_ASSERT_TRUE_MESSAGE(strlen(p) < 256, langCode((Lang)l));
      p += strlen(p) + 1;
    }
  }
}

static void test_known_strings() {
  TEST_ASSERT_EQUAL_STRING("NEEDS YOU", T(Lang::En, S::NeedsYou).c_str());
  TEST_ASSERT_EQUAL_STRING("PRECISA DE VOCÊ", T(Lang::PtBR, S::NeedsYou).c_str());
  TEST_ASSERT_EQUAL_STRING("Pediu permissão", T(Lang::PtBR, S::AskedPermission).c_str());
  TEST_ASSERT_EQUAL_STRING("Palavra-passe incorreta", T(Lang::PtPT, S::WrongPassword).c_str());
  TEST_ASSERT_EQUAL_STRING("Неверный пароль", T(Lang::Ru, S::WrongPassword).c_str());
  TEST_ASSERT_EQUAL_STRING("密码错误", T(Lang::Zh, S::WrongPassword).c_str());
  TEST_ASSERT_EQUAL_STRING("Firmware version", T(Lang::En, S::WebVersion).c_str());
  TEST_ASSERT_EQUAL_STRING("qui", T(Lang::PtBR, S::WdThu).c_str());
}

static void test_tr_truncates_on_utf8_boundary() {
  char b[8];
  tr(Lang::Zh, S::WaitingComputer, b, sizeof(b));  // "等待电脑连接": 3 bytes per character
  TEST_ASSERT_EQUAL_STRING("等待", b);
  tr(Lang::En, S::Count, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("", b);
}

static void test_lang_codes_roundtrip() {
  for (int l = 0; l < (int)Lang::Count; l++) {
    Lang out;
    TEST_ASSERT_TRUE(langFromCode(langCode((Lang)l), out));
    TEST_ASSERT_EQUAL((int)l, (int)out);
  }
  Lang out;
  TEST_ASSERT_TRUE(langFromCode("PT-br", out));
  TEST_ASSERT_EQUAL(Lang::PtBR, out);
  TEST_ASSERT_FALSE(langFromCode("ja", out));
  TEST_ASSERT_FALSE(langFromCode(nullptr, out));
  TEST_ASSERT_EQUAL_STRING("Português (Brasil)", langName(Lang::PtBR));
}

static void test_negotiate_accept_language() {
  const struct {
    const char* header;
    Lang expected;
  } cases[] = {
      {nullptr, Lang::En},
      {"", Lang::En},
      {"ja-JP,ko;q=0.8", Lang::En},
      {"pt-BR,pt;q=0.9,en;q=0.8", Lang::PtBR},
      {"pt", Lang::PtBR},
      {"pt-PT,pt;q=0.9", Lang::PtPT},
      {"pt-AO", Lang::PtPT},
      {"zh-CN,zh;q=0.9", Lang::Zh},
      {"zh-Hant-TW", Lang::Zh},
      {"en-US,en;q=0.9,fr;q=0.8", Lang::En},
      {"ja;q=1.0, de;q=0.7, fr;q=0.9", Lang::Fr},
      {"it-IT", Lang::It},
      {"es-419,es;q=0.9", Lang::Es},
      {"ru-RU,ru;q=0.9,en-US;q=0.8", Lang::Ru},
      {"de-CH;q=0.5, en;q=0.4", Lang::De},
      {"fr;q=0.8, es;q=0.8", Lang::Fr},
      {"*;q=0.5", Lang::En},
  };
  for (const auto& c : cases) {
    TEST_ASSERT_EQUAL_MESSAGE((int)c.expected, (int)negotiateLang(c.header), c.header ? c.header : "null");
  }
}

static std::string act(Lang l, const char* tool, const char* det, bool discreet = false) {
  char b[200];
  activityText(l, tool, det, discreet, b, sizeof(b));
  return b;
}

static void test_activity_text() {
  TEST_ASSERT_EQUAL_STRING("Editando Header.tsx", act(Lang::PtBR, "Edit", "Header.tsx").c_str());
  TEST_ASSERT_EQUAL_STRING("Editando", act(Lang::PtBR, "Edit", "Header.tsx", true).c_str());
  TEST_ASSERT_EQUAL_STRING("Reading y.md", act(Lang::En, "Read", "y.md").c_str());
  TEST_ASSERT_EQUAL_STRING("Searching TODO", act(Lang::En, "Grep", "TODO").c_str());
  TEST_ASSERT_EQUAL_STRING("Web search", act(Lang::En, "WebSearch", "").c_str());
  TEST_ASSERT_EQUAL_STRING("Agent Find usages", act(Lang::En, "Task", "Find usages").c_str());
  TEST_ASSERT_EQUAL_STRING("Bash · npm test", act(Lang::PtBR, "Bash", "npm test").c_str());
  TEST_ASSERT_EQUAL_STRING("Bash", act(Lang::PtBR, "Bash", "npm test", true).c_str());
  TEST_ASSERT_EQUAL_STRING("create_issue", act(Lang::En, "create_issue", "").c_str());
  TEST_ASSERT_EQUAL_STRING("Trabalhando", act(Lang::PtBR, "", "").c_str());
  TEST_ASSERT_EQUAL_STRING("工作中", act(Lang::Zh, nullptr, nullptr).c_str());
}

static void test_session_line() {
  SessionRow r{};
  strcpy(r.tool, "Bash");
  strcpy(r.det, "npm run migrate");
  char b[200];
  r.st = SessionState::Perm;
  sessionLine(Lang::PtBR, r, false, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("permissão · Bash", b);
  r.st = SessionState::Question;
  sessionLine(Lang::PtBR, r, false, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("pergunta", b);
  r.st = SessionState::Done;
  sessionLine(Lang::En, r, false, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("finished", b);
  r.st = SessionState::Idle;
  sessionLine(Lang::En, r, false, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("idle", b);
  r.st = SessionState::Running;
  sessionLine(Lang::En, r, true, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("Bash", b);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_every_language_has_every_string_with_same_placeholders);
  RUN_TEST(test_known_strings);
  RUN_TEST(test_tr_truncates_on_utf8_boundary);
  RUN_TEST(test_lang_codes_roundtrip);
  RUN_TEST(test_negotiate_accept_language);
  RUN_TEST(test_activity_text);
  RUN_TEST(test_session_line);
  return UNITY_END();
}
