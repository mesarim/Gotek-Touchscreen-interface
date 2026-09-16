#include "cracktro_gti.h"
#include <cstdio>
#include <cstring>
#include <string>

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

static const char* TWO_PATTERNS =
  "GTICRACK 1\n"
  "NAME=Omegaware\n"
  "AUTHOR=Dimmy\n"
  "\n"
  "[PATTERN]\n"
  "FX=COPPER\n"
  "TIME=4000\n"
  "TITLE=OMEGAWARE\n"
  "SUB=* MEZ & DIMMY *\n"
  "COL=FFE000\n"
  "\n"
  "[PATTERN]\n"
  "FX=LOGO\n"
  "TIME=5000\n"
  "LOGO=1\n"
  "SCROLL=ON\n";

static void test_header_fields() {
  gti::File f;
  CHECK(parseStr(TWO_PATTERNS, f) == gti::OK);
  CHECK(std::strcmp(f.name, "Omegaware") == 0);
  CHECK(std::strcmp(f.author, "Dimmy") == 0);
}

static void test_pattern_count_and_fields() {
  gti::File f;
  CHECK(parseStr(TWO_PATTERNS, f) == gti::OK);
  CHECK(f.patternCount == 2);
  CHECK(std::strcmp(f.patterns[0].fx, "COPPER") == 0);
  CHECK(f.patterns[0].timeMs == 4000u);
  CHECK(std::strcmp(f.patterns[0].title, "OMEGAWARE") == 0);
  CHECK(std::strcmp(f.patterns[0].sub, "* MEZ & DIMMY *") == 0);
  CHECK(f.patterns[0].col == gti::rgb565(0xFF, 0xE0, 0x00));
  CHECK(f.patterns[0].logo == -1);
  CHECK(f.patterns[0].scroll == false);
}

static void test_second_pattern_logo_and_scroll() {
  gti::File f;
  CHECK(parseStr(TWO_PATTERNS, f) == gti::OK);
  CHECK(std::strcmp(f.patterns[1].fx, "LOGO") == 0);
  CHECK(f.patterns[1].logo == 1);
  CHECK(f.patterns[1].scroll == true);
}

static void test_too_many_patterns_is_rejected() {
  std::string src = "GTICRACK 1\n";
  for (int i = 0; i < gti::MAX_PATTERNS + 1; ++i) src += "[PATTERN]\nFX=COPPER\n";
  gti::File f;
  CHECK(gti::parse(src.c_str(), src.size(), f) == gti::ERR_TOO_MANY_PATTERNS);
}

int main() {
  test_empty_is_rejected();
  test_wrong_magic_is_rejected();
  test_magic_alone_has_no_patterns();
  test_comments_and_blanks_before_magic_are_allowed();
  test_rgb565_matches_the_firmware_macro();
  test_header_fields();
  test_pattern_count_and_fields();
  test_second_pattern_logo_and_scroll();
  test_too_many_patterns_is_rejected();
  std::printf("%d checks, %d failed\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
