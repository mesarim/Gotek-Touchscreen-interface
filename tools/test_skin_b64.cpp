#include "skin_b64.h"
#include <cstdio>
#include <cstring>
#include <string>

static int g_fail = 0, g_run = 0;
#define CHECK(cond) do { \
  ++g_run; \
  if (!(cond)) { ++g_fail; \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

// Reference encoder: standard base64 with '+'/'/' swapped to '-'/'_', padding optional.
static std::string enc(const uint8_t* d, size_t n, bool pad) {
  static const char* A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string s;
  for (size_t i = 0; i < n; i += 3) {
    uint32_t v = (uint32_t)d[i] << 16;
    if (i + 1 < n) v |= (uint32_t)d[i + 1] << 8;
    if (i + 2 < n) v |= d[i + 2];
    s += A[(v >> 18) & 63];
    s += A[(v >> 12) & 63];
    s += (i + 1 < n) ? A[(v >> 6) & 63] : (pad ? '=' : '\0');
    s += (i + 2 < n) ? A[v & 63] : (pad ? '=' : '\0');
  }
  std::string out;
  for (char c : s) if (c) out += c;
  return out;
}

static int dec(const std::string& s, uint8_t* out, size_t cap) {
  return skin::b64urlDecode(s.data(), s.size(), out, cap);
}

static void test_roundtrip_0_to_5() {
  const uint8_t src[5] = {0xFB, 0xFF, 0xBF, 0x00, 0x7E};   // 0xFB 0xFF 0xBF encodes to "-_-_": both url characters
  for (size_t n = 0; n <= 5; ++n) {
    for (int pad = 0; pad < 2; ++pad) {
      uint8_t out[8];
      memset(out, 0xAA, sizeof out);
      std::string s = enc(src, n, pad != 0);
      int r = dec(s, out, sizeof out);
      CHECK(r == (int)n);
      CHECK(r == (int)n && memcmp(out, src, n) == 0);
    }
  }
}

static void test_url_chars() {
  uint8_t out[4];
  CHECK(dec("-_-_", out, 4) == 3);
  CHECK(out[0] == 0xFB && out[1] == 0xFF && out[2] == 0xBF);
  CHECK(dec("+/+/", out, 4) == -1);   // standard alphabet is not base64url
}

static void test_invalid() {
  uint8_t out[16];
  CHECK(dec("AB*D", out, 16) == -1);
  CHECK(dec("AB.D", out, 16) == -1);
  CHECK(dec("A", out, 16) == -1);         // one char left over is never valid
  CHECK(dec("ABCDE", out, 16) == -1);
  CHECK(dec("AB=D", out, 16) == -1);      // padding only at the end
  CHECK(dec("A===", out, 16) == -1);
  CHECK(dec("AB=", out, 16) == -1);       // padding must complete a group of 4
  CHECK(dec("ABC==", out, 16) == -1);
  CHECK(dec("====", out, 16) == -1);
  std::string withNul("AB\0D", 4);
  CHECK(dec(withNul, out, 16) == -1);
  CHECK(skin::b64urlDecode(nullptr, 4, out, 16) == -1);
}

static void test_whitespace() {
  uint8_t out[16];
  CHECK(dec("QUJD REVG", out, 16) == -1);
  CHECK(dec("QUJD\nREVG", out, 16) == -1);
  CHECK(dec("QUJDREVG\r\n", out, 16) == -1);
  CHECK(dec(" QUJD", out, 16) == -1);
}

static void test_cap() {
  uint8_t out[8];
  CHECK(dec("QUJDREVG", out, 6) == 6);    // "ABCDEF"
  CHECK(memcmp(out, "ABCDEF", 6) == 0);
  CHECK(dec("QUJDREVG", out, 5) == -1);
  CHECK(dec("QUJDRA", out, 4) == 4);      // "ABCD"
  CHECK(dec("QUJDRA", out, 3) == -1);
  CHECK(dec("QQ", out, 0) == -1);
  CHECK(dec("", out, 0) == 0);
  CHECK(skin::b64urlDecode("", 0, nullptr, 0) == 0);
}

static void test_padding() {
  uint8_t out[8];
  CHECK(dec("QUI=", out, 8) == 2 && memcmp(out, "AB", 2) == 0);
  CHECK(dec("QUI", out, 8) == 2 && memcmp(out, "AB", 2) == 0);
  CHECK(dec("QQ==", out, 8) == 1 && out[0] == 'A');
  CHECK(dec("QQ", out, 8) == 1 && out[0] == 'A');
}

static void test_big() {
  uint8_t src[4096], out[4096];
  for (int i = 0; i < 4096; ++i) src[i] = (uint8_t)(i * 37 + 11);
  std::string s = enc(src, sizeof src, false);
  CHECK(s.size() == 5462);
  CHECK(dec(s, out, sizeof out) == 4096);
  CHECK(memcmp(src, out, 4096) == 0);
}

int main() {
  test_roundtrip_0_to_5();
  test_url_chars();
  test_invalid();
  test_whitespace();
  test_cap();
  test_padding();
  test_big();
  std::printf("test_skin_b64: %d/%d passed\n", g_run - g_fail, g_run);
  return g_fail ? 1 : 0;
}
