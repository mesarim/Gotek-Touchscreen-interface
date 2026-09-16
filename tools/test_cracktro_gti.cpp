#include "cracktro_gti.h"
#include <cstdio>
#include <cstring>

static int g_fail = 0, g_run = 0;
#define CHECK(cond) do { \
  ++g_run; \
  if (!(cond)) { ++g_fail; \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static gti::Err parseStr(const char* s, gti::File& f) {
  return gti::parse(s, std::strlen(s), f);
}

static void test_empty_is_rejected() {
  gti::File f;
  CHECK(parseStr("", f) == gti::ERR_EMPTY);
}

static void test_wrong_magic_is_rejected() {
  gti::File f;
  CHECK(parseStr("NOTACRACKTRO\n", f) == gti::ERR_MAGIC);
}

static void test_magic_alone_has_no_patterns() {
  gti::File f;
  CHECK(parseStr("GTICRACK 1\n", f) == gti::ERR_NO_PATTERNS);
}

static void test_comments_and_blanks_before_magic_are_allowed() {
  gti::File f;
  const char* src = "; a comment\n\n   \nGTICRACK 1\n";
  CHECK(parseStr(src, f) == gti::ERR_NO_PATTERNS);
}

static void test_rgb565_matches_the_firmware_macro() {
  // CRK_RGB(r,g,b) == ((r&0xF8)<<8)|((g&0xFC)<<3)|(b>>3)
  CHECK(gti::rgb565(0xFF, 0xE0, 0x00) == 0xFF00);
  CHECK(gti::rgb565(0x00, 0x00, 0x00) == 0x0000);
  CHECK(gti::rgb565(0xFF, 0xFF, 0xFF) == 0xFFFF);
}

int main() {
  test_empty_is_rejected();
  test_wrong_magic_is_rejected();
  test_magic_alone_has_no_patterns();
  test_comments_and_blanks_before_magic_are_allowed();
  test_rgb565_matches_the_firmware_macro();
  std::printf("%d checks, %d failed\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
