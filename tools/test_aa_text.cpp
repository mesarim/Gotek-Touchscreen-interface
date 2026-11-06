// Host test for firmware/Gotek_JC3248/aa_text.h (the anti-aliased font: decoding, measuring, cutting, blending) and the
// generated aa_font_data.h (sizes and the glyphs the screens rely on). Pure C++. Run with tools/test_aa_text.sh.
#include "aa_font_data.h"
#include <cstdio>
#include <cstring>

static int g_fail = 0, g_run = 0;
#define CHECK(cond) do { \
  ++g_run; \
  if (!(cond)) { ++g_fail; \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static void test_decode() {
  int i = 0;
  CHECK(aaNext("A", &i) == 'A' && i == 1);
  i = 0; CHECK(aaNext("\xc3\xa9", &i) == 0xE9 && i == 2);         // UTF-8 e-acute
  i = 0; CHECK(aaNext("\xe9x", &i) == 0xE9 && i == 1);            // a Latin-1 byte on its own (old SD names): taken as it is
  i = 0; CHECK(aaNext("\xe2\x82\xac", &i) == '?' && i == 3);      // the euro sign: outside the font, one '?' for the whole sequence
  i = 0; CHECK(aaNext("\xc3", &i) == 0xC3 && i == 1);             // a lead byte at the end: no read past the string
  CHECK(aaIndex(' ') == 0 && aaIndex('~') == 94 && aaIndex(0xA0) == 95 && aaIndex(0xFF) == 190);
  CHECK(aaIndex(0x7F) == aaIndex('?') && aaIndex(0x9F) == aaIndex('?') && aaIndex(0x100) == aaIndex('?'));
}

static void test_fonts() {
  const AaFont* f[3] = {&aaInter16, &aaInter24, &aaInter32};
  for (int k = 0; k < 3; k++) {
    CHECK(f[k]->box == 16 + 8 * k);
    for (int c = 0; c < AA_GLYPHS; c++) {
      const AaGlyph& g = f[k]->glyphs[c];
      CHECK(g.adv > 0 && g.adv <= f[k]->box * 2);
      CHECK(g.dy + g.h <= f[k]->box + f[k]->box / 4);             // ink stays near the box: descenders a little below at most
    }
    const AaGlyph& A = f[k]->glyphs[aaIndex('A')];
    CHECK(A.w > 0 && A.h > f[k]->box / 2 && A.dy >= 0);           // capitals sit inside the box
    CHECK(f[k]->glyphs[0].w == 0);                                // the space has no ink
  }
  CHECK(aaFontFor(2) == &aaInter16 && aaFontFor(3) == &aaInter24 && aaFontFor(4) == &aaInter32);
  CHECK(aaFontFor(1) == nullptr && aaFontFor(5) == nullptr);      // other sizes stay on the pixel font
}

static void test_measure_and_cut() {
  const AaFont& f = aaInter16;
  const int wA = f.glyphs[aaIndex('A')].adv, wi = f.glyphs[aaIndex('i')].adv;
  CHECK(aaWidth(f, "") == 0);
  CHECK(aaWidth(f, "Ai") == wA + wi);
  CHECK(aaWidth(f, "\xc3\xa9") == f.glyphs[aaIndex(0xE9)].adv);
  CHECK(wi < wA);                                                  // proportional: an i is narrower than an A
  CHECK(aaWidth(f, "Lemmings - Disk 2") < 17 * 12);               // narrower than the pixel font at 2x
  CHECK(aaFitBytes(f, "Ai", 1000) == 2);
  CHECK(aaFitBytes(f, "Ai", wA) == 1);
  CHECK(aaFitBytes(f, "Ai", wA - 1) == 0);
  CHECK(aaFitBytes(f, "\xc3\xa9x", f.glyphs[aaIndex(0xE9)].adv) == 2);   // never cuts inside a UTF-8 sequence
}

static void test_blend() {
  CHECK(aaBlend565(0x0000, 0xFFFF, 0) == 0x0000);
  CHECK(aaBlend565(0x0000, 0xFFFF, 15) == 0xFFFF);
  const uint16_t mid = aaBlend565(0x0000, 0xFFFF, 8);
  CHECK(((mid >> 11) & 31) >= 15 && ((mid >> 11) & 31) <= 17);
  CHECK(((mid >> 5) & 63) >= 31 && ((mid >> 5) & 63) <= 34);
  CHECK(aaBlend565(0xF800, 0x001F, 15) == 0x001F && aaBlend565(0xF800, 0x001F, 0) == 0xF800);
}

static void test_coverage() {
  // aaCoverage: one pixel of a glyph, from the packed nibbles (two per byte, high first, rows not padded)
  static const uint8_t bits[] = {0xF0, 0x8F};
  const AaGlyph g = {0, 3, 1, 0, 0, 3};
  CHECK(aaCoverage(bits, g, 0, 0) == 15 && aaCoverage(bits, g, 1, 0) == 0 && aaCoverage(bits, g, 2, 0) == 8);
}

int main() {
  test_decode();
  test_fonts();
  test_measure_and_cut();
  test_blend();
  test_coverage();
  std::printf("%d/%d checks passed\n", g_run - g_fail, g_run);
  return g_fail ? 1 : 0;
}
