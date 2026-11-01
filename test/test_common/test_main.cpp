// Host-side tests for code shared between boards. Run: pio test -e native
#include <unity.h>

#include "ipv4.h"
#include "rover_protocol.h"

void setUp() {}
void tearDown() {}

static ipv4::Result v(const char* s, char out[16]) { return ipv4::validate(s, strlen(s), out); }
static ipv4::Result x(const char* s, char out[16]) { return ipv4::extractUnique(s, strlen(s), out); }

void test_ipv4_strict_accepts_valid() {
  char out[16] = {0};
  TEST_ASSERT_EQUAL(ipv4::Result::kOk, v("192.168.1.10", out));
  TEST_ASSERT_EQUAL_STRING("192.168.1.10", out);
  TEST_ASSERT_EQUAL(ipv4::Result::kOk, v("  10.0.0.5\r\n", out));
  TEST_ASSERT_EQUAL_STRING("10.0.0.5", out);
  TEST_ASSERT_EQUAL(ipv4::Result::kOk, v("1.0.0.0", out));
  TEST_ASSERT_EQUAL(ipv4::Result::kOk, v("223.255.255.254", out));
}

void test_ipv4_strict_rejects_bad_syntax() {
  char out[16] = {0};
  const char* bad[] = {"", "1.2.3", "1.2.3.4.5", "256.1.1.1", "01.2.3.4", "1.2.3.04",
                       "1..2.3", "1.2.3.", "a.b.c.d", "1.2.3.4a", "+1.2.3.4", "1 .2.3.4",
                       "1234.1.1.1", "192.168.001.1"};
  for (const char* s : bad) {
    TEST_ASSERT_EQUAL_MESSAGE(ipv4::Result::kSyntax, v(s, out), s);
  }
}

void test_ipv4_rejects_non_unicast() {
  char out[16] = {0};
  TEST_ASSERT_EQUAL(ipv4::Result::kNotUnicast, v("0.0.0.0", out));
  TEST_ASSERT_EQUAL(ipv4::Result::kNotUnicast, v("127.0.0.1", out));
  TEST_ASSERT_EQUAL(ipv4::Result::kNotUnicast, v("224.0.0.1", out));
  TEST_ASSERT_EQUAL(ipv4::Result::kNotUnicast, v("255.255.255.255", out));
}

void test_ipv4_extract_from_text() {
  char out[16] = {0};
  TEST_ASSERT_EQUAL(ipv4::Result::kOk, x("Ping server: 10.20.30.40.", out));
  TEST_ASSERT_EQUAL_STRING("10.20.30.40", out);
  TEST_ASSERT_EQUAL(ipv4::Result::kOk, x("ip=192.168.4.2 ip=192.168.4.2", out));
  TEST_ASSERT_EQUAL(ipv4::Result::kAmbiguous, x("10.0.0.1 or 10.0.0.2", out));
  TEST_ASSERT_EQUAL(ipv4::Result::kNone, x("hello world 42", out));
  TEST_ASSERT_EQUAL(ipv4::Result::kSyntax, x("v1.2.3", out));
  TEST_ASSERT_EQUAL(ipv4::Result::kNotUnicast, x("go to 127.0.0.1", out));
  TEST_ASSERT_EQUAL(ipv4::Result::kOk, x(".10.0.0.9", out));
  TEST_ASSERT_EQUAL_STRING("10.0.0.9", out);
}

void test_link_frame_roundtrip() {
  const char* json = "{\"v\":1,\"t\":\"hb\",\"s\":7}";
  char buf[boardlink::kMaxLine + 2];
  const size_t n = boardlink::frame(buf, sizeof(buf), json, strlen(json));
  TEST_ASSERT_EQUAL(strlen(json) + 3, n);
  size_t jl = 0;
  TEST_ASSERT_TRUE(boardlink::unframe(buf, n, &jl));
  TEST_ASSERT_EQUAL(strlen(json), jl);
  buf[n] = '\r';
  TEST_ASSERT_TRUE(boardlink::unframe(buf, n + 1, &jl));
  buf[5] ^= 0x01;  // corrupt one byte
  TEST_ASSERT_FALSE(boardlink::unframe(buf, n, &jl));
}

void test_link_rejects_garbage_and_overflow() {
  size_t jl = 0;
  TEST_ASSERT_FALSE(boardlink::unframe("garbage", 7, &jl));
  TEST_ASSERT_FALSE(boardlink::unframe("{}*ZZ", 5, &jl));
  boardlink::LineAssembler la;
  for (size_t i = 0; i < boardlink::kMaxLine + 50; ++i) TEST_ASSERT_FALSE(la.push('x'));
  TEST_ASSERT_FALSE(la.push('\n'));  // over-long line is dropped
  TEST_ASSERT_EQUAL_UINT32(1, la.overflows());
  const char* ok = "{}*00";
  for (const char* p = ok; *p; ++p) la.push(*p);
  TEST_ASSERT_TRUE(la.push('\n'));
  TEST_ASSERT_EQUAL_STRING(ok, la.line());
}

void test_pan_names_roundtrip_and_fail_to_hold() {
  using boardlink::PanMode;
  const PanMode modes[] = {PanMode::Hold, PanMode::Goto, PanMode::Scan, PanMode::Track, PanMode::Pulse};
  for (PanMode m : modes) {
    TEST_ASSERT_TRUE(boardlink::parsePanMode(boardlink::panModeName(m)) == m);
  }
  TEST_ASSERT_TRUE(boardlink::parsePanMode("spin") == PanMode::Hold);
  TEST_ASSERT_TRUE(boardlink::parsePanMode(nullptr) == PanMode::Hold);
  TEST_ASSERT_TRUE(boardlink::parsePanState("settled") == boardlink::PanState::Settled);
  TEST_ASSERT_TRUE(boardlink::parsePanState("bogus") == boardlink::PanState::Absent);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_ipv4_strict_accepts_valid);
  RUN_TEST(test_ipv4_strict_rejects_bad_syntax);
  RUN_TEST(test_ipv4_rejects_non_unicast);
  RUN_TEST(test_ipv4_extract_from_text);
  RUN_TEST(test_link_frame_roundtrip);
  RUN_TEST(test_link_rejects_garbage_and_overflow);
  RUN_TEST(test_pan_names_roundtrip_and_fail_to_hold);
  return UNITY_END();
}
