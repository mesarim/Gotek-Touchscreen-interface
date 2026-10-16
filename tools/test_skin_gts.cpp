#include "skin_gts.h"
#include <cstdio>
#include <cstring>
#include <vector>

static int g_fail = 0, g_run = 0;
#define CHECK(cond) do { \
  ++g_run; \
  if (!(cond)) { ++g_fail; \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

typedef std::vector<uint8_t> Buf;
static const uint8_t PNG[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

static Buf png(size_t extra) {
  Buf b(PNG, PNG + 8);
  for (size_t i = 0; i < extra; ++i) b.push_back((uint8_t)(i * 7 + 1));
  return b;
}
static void u16(Buf& b, uint32_t v) { b.push_back(v & 255); b.push_back((v >> 8) & 255); }
static void u32(Buf& b, uint32_t v) { u16(b, v & 0xFFFF); u16(b, v >> 16); }
static void app(Buf& b, const Buf& s) { b.insert(b.end(), s.begin(), s.end()); }

// Builds a file by hand: header, manifest, atlas, then nBg entries.
static Buf build(const Buf& man, const Buf& atlas, const std::vector<uint8_t>& ids,
                 const Buf& bgpng, uint8_t reserved = 0) {
  Buf b = {'G', 'T', 'S', '1', reserved, 1};
  u16(b, (uint32_t)man.size()); app(b, man);
  u32(b, (uint32_t)atlas.size()); app(b, atlas);
  b.push_back((uint8_t)ids.size());
  for (uint8_t id : ids) { b.push_back(id); u32(b, (uint32_t)bgpng.size()); app(b, bgpng); }
  return b;
}
static Buf simple() { return build(Buf{'{', '}'}, png(10), {1, 3}, png(5)); }

static void test_roundtrip() {
  gts::View v = {};
  v.slotVer = 1;
  Buf man = {'a', 'b', 'c'}, at = png(20), b1 = png(4), b2 = png(9);
  v.manifest = {man.data(), (uint32_t)man.size()};
  v.atlas = {at.data(), (uint32_t)at.size()};
  v.nBg = 2;
  v.bgId[0] = 5; v.bg[0] = {b1.data(), (uint32_t)b1.size()};
  v.bgId[1] = 2; v.bg[1] = {b2.data(), (uint32_t)b2.size()};
  uint8_t out[512];
  size_t n = gts::pack(v, out, sizeof out);
  CHECK(n > 0);
  CHECK(gts::pack(v, out, n - 1) == 0);
  CHECK(gts::pack(v, out, n) == n);
  gts::View r = {};
  CHECK(gts::parse(out, n, r) == gts::OK);
  CHECK(r.slotVer == 1);
  CHECK(r.manifest.n == 3 && std::memcmp(r.manifest.p, man.data(), 3) == 0);
  CHECK(r.atlas.n == at.size() && std::memcmp(r.atlas.p, at.data(), at.size()) == 0);
  CHECK(r.nBg == 2);
  CHECK(r.bgId[0] == 5 && r.bg[0].n == b1.size() && std::memcmp(r.bg[0].p, b1.data(), b1.size()) == 0);
  CHECK(r.bgId[1] == 2 && r.bg[1].n == b2.size() && std::memcmp(r.bg[1].p, b2.data(), b2.size()) == 0);
}

static void test_empty_and_short() {
  gts::View v;
  CHECK(gts::parse(nullptr, 0, v) == gts::TOO_SHORT);
  Buf b = simple();
  for (size_t n = 0; n < 8; ++n) CHECK(gts::parse(b.data(), n, v) == gts::TOO_SHORT);
}

static void test_wrong_magic() {
  Buf b = simple(); b[3] = '2';
  gts::View v;
  CHECK(gts::parse(b.data(), b.size(), v) == gts::BAD_MAGIC);
}

static void test_reserved_byte_nonzero_is_ok() {
  Buf b = build(Buf{'x'}, png(3), {}, Buf(), 0x7F);
  gts::View v;
  CHECK(gts::parse(b.data(), b.size(), v) == gts::OK);
  CHECK(v.nBg == 0);
}

static void test_manifest_len_past_end() {
  Buf b = simple(); b[6] = 0xFF; b[7] = 0x03;  // 1023: inside MAX_MANIFEST, past end of file
  gts::View v;
  CHECK(gts::parse(b.data(), b.size(), v) == gts::BAD_LENGTH);
  b[6] = 0x01; b[7] = 0x04;  // 1025 > MAX_MANIFEST
  CHECK(gts::parse(b.data(), b.size(), v) == gts::BAD_LENGTH);
}

static void test_atlas_len_overflow_uint32() {
  Buf b = simple();
  size_t off = 8 + 2;  // manifest is 2 bytes
  b[off] = 0xF0; b[off + 1] = 0xFF; b[off + 2] = 0xFF; b[off + 3] = 0xFF;
  gts::View v;
  CHECK(gts::parse(b.data(), b.size(), v) == gts::BAD_LENGTH);
}

static void test_atlas_not_png() {
  Buf at = png(10); at[1] = 'X';
  Buf b = build(Buf{'{', '}'}, at, {}, Buf());
  gts::View v;
  CHECK(gts::parse(b.data(), b.size(), v) == gts::BAD_PNG);
  Buf tiny = {0x89, 'P'};
  b = build(Buf{'{', '}'}, tiny, {}, Buf());
  CHECK(gts::parse(b.data(), b.size(), v) == gts::BAD_PNG);
}

static void test_too_big() {
  Buf b(gts::MAX_FILE + 1, 0);
  gts::View v;
  CHECK(gts::parse(b.data(), b.size(), v) == gts::TOO_BIG);
}

static void test_bg_count_over_max() {
  Buf b = build(Buf{'x'}, png(3), {1, 2, 3, 4, 5, 1, 2}, png(1));
  gts::View v;
  CHECK(gts::parse(b.data(), b.size(), v) == gts::TOO_MANY_BG);
  // exactly MAX_BG distinct is not possible (only 5 ids), so 5 must be fine
  b = build(Buf{'x'}, png(3), {1, 2, 3, 4, 5}, png(1));
  CHECK(gts::parse(b.data(), b.size(), v) == gts::OK && v.nBg == 5);
}

static void test_bg_unknown_id() {
  gts::View v;
  Buf b = build(Buf{'x'}, png(3), {0}, png(1));
  CHECK(gts::parse(b.data(), b.size(), v) == gts::BAD_BGID);
  b = build(Buf{'x'}, png(3), {6}, png(1));
  CHECK(gts::parse(b.data(), b.size(), v) == gts::BAD_BGID);
}

static void test_bg_duplicate() {
  Buf b = build(Buf{'x'}, png(3), {3, 3}, png(1));
  gts::View v;
  CHECK(gts::parse(b.data(), b.size(), v) == gts::DUP_BG);
}

static void test_trailing_bytes_rejected() {
  Buf b = simple(); b.push_back(0);
  gts::View v;
  CHECK(gts::parse(b.data(), b.size(), v) == gts::BAD_LENGTH);
  Buf ok = simple();
  CHECK(gts::parse(ok.data(), ok.size(), v) == gts::OK);
  // truncated last bg
  CHECK(gts::parse(ok.data(), ok.size() - 1, v) == gts::BAD_LENGTH);
}

static void test_bgIdFor() {
  CHECK(gts::bgIdFor(480, 320) == 1);
  CHECK(gts::bgIdFor(320, 480) == 2);
  CHECK(gts::bgIdFor(480, 272) == 3);
  CHECK(gts::bgIdFor(272, 480) == 4);
  CHECK(gts::bgIdFor(100, 100) == 0);
}

static void test_errText() {
  CHECK(std::strlen(gts::errText(gts::OK)) > 0);
  CHECK(std::strlen(gts::errText(gts::DUP_BG)) > 0);
  CHECK(std::strlen(gts::errText((gts::Err)200)) > 0);
}

int main() {
  test_roundtrip();
  test_empty_and_short();
  test_wrong_magic();
  test_reserved_byte_nonzero_is_ok();
  test_manifest_len_past_end();
  test_atlas_len_overflow_uint32();
  test_atlas_not_png();
  test_too_big();
  test_bg_count_over_max();
  test_bg_unknown_id();
  test_bg_duplicate();
  test_trailing_bytes_rejected();
  test_bgIdFor();
  test_errText();
  std::printf("%d checks, %d failed\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
