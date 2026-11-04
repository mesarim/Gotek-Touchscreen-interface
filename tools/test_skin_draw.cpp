#include "skin_draw.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

static int g_fail = 0, g_run = 0;
#define CHECK(cond) do { \
  ++g_run; \
  if (!(cond)) { ++g_fail; \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static void test_slice9_covers_exactly() {
  const int sizes[4][2] = {{48, 48}, {100, 30}, {7, 7}, {1, 1}};
  for (int i = 0; i < 4; ++i) {
    int dw = sizes[i][0], dh = sizes[i][1];
    skin::Piece p[9];
    int n = skin::slice9(skin::SLOTS[skin::S_KEY_UP], dw, dh, p);
    CHECK(n >= 1 && n <= 9);
    long sum = 0;
    std::vector<int> grid((size_t)dw * dh, 0);
    for (int k = 0; k < n; ++k) {
      CHECK(p[k].dw > 0 && p[k].dh > 0);
      sum += (long)p[k].dw * p[k].dh;
      for (int y = p[k].dy; y < p[k].dy + p[k].dh; ++y)
        for (int x = p[k].dx; x < p[k].dx + p[k].dw; ++x) {
          CHECK(x >= 0 && y >= 0 && x < dw && y < dh);
          if (x >= 0 && y >= 0 && x < dw && y < dh) grid[(size_t)y * dw + x]++;
        }
    }
    CHECK(sum == (long)dw * dh);
    for (size_t k = 0; k < grid.size(); ++k) CHECK(grid[k] == 1);
  }
}

static void test_slice9_corners_unscaled() {
  const skin::Slot& s = skin::SLOTS[skin::S_PANEL];
  skin::Piece p[9];
  int n = skin::slice9(s, 100, 60, p);
  CHECK(n == 9);
  int corners = 0;
  for (int k = 0; k < n; ++k) {
    bool l = p[k].dx == 0, r = p[k].dx + p[k].dw == 100;
    bool t = p[k].dy == 0, b = p[k].dy + p[k].dh == 60;
    if ((l || r) && (t || b)) {
      ++corners;
      CHECK(p[k].dw == s.border && p[k].dh == s.border);
      CHECK(p[k].sw == s.border && p[k].sh == s.border);
    }
  }
  CHECK(corners == 4);
  // Borderless slot: single piece.
  n = skin::slice9(skin::SLOTS[skin::S_TILE], 30, 20, p);
  CHECK(n == 1);
}

static int chan(uint16_t c, int sh, int bits) { return (c >> sh) & ((1 << bits) - 1); }

static void test_blend_edges_and_mid() {
  const uint16_t red = 0xF800, blue = 0x001F;
  CHECK(skin::blend565(red, 0, blue) == blue);
  CHECK(skin::blend565(red, 255, blue) == red);
  CHECK(skin::blend565(0x1234, 0, 0xABCD) == 0xABCD);
  CHECK(skin::blend565(0x1234, 255, 0xABCD) == 0x1234);
  uint16_t m = skin::blend565(red, 128, blue);
  CHECK(std::abs(chan(m, 11, 5) - 16) <= 1);
  CHECK(chan(m, 5, 6) == 0);
  CHECK(std::abs(chan(m, 0, 5) - 15) <= 1);
}

static void test_iconScale() {
  CHECK(skin::iconScale(480, 320) == 1);
  CHECK(skin::iconScale(1024, 600) == 2);
  CHECK(skin::iconScale(600, 1024) == 2);
  CHECK(skin::iconScale(799, 799) == 1);
  CHECK(skin::iconScale(800, 480) == 2);
}

static void test_drawSlice_matches_manual() {
  uint16_t rgb[16];
  uint8_t a[16];
  for (int y = 0; y < 4; ++y)
    for (int x = 0; x < 4; ++x) {
      rgb[y * 4 + x] = (uint16_t)(((x + y) & 1) ? 0xFFFF : 0x0000) | (uint16_t)(y * 4 + x) << 1;
      a[y * 4 + x] = (uint8_t)(10 * (y * 4 + x) + 1);
    }
  skin::Atlas at;
  at.w = 4; at.h = 4; at.rgb = rgb; at.a = a;
  skin::Slot s = {0, 0, 4, 4, 0};

  uint16_t o1[16]; uint8_t oa1[16];
  int calls = 0;
  skin::drawSlice(at, s, 0, 0, 4, 4, [&](int x, int y, uint16_t c, uint8_t al) {
    ++calls; o1[y * 4 + x] = c; oa1[y * 4 + x] = al;
  });
  CHECK(calls == 16);
  for (int i = 0; i < 16; ++i) { CHECK(o1[i] == rgb[i]); CHECK(oa1[i] == a[i]); }

  uint16_t o2[64]; uint8_t oa2[64];
  calls = 0;
  skin::drawSlice(at, s, 0, 0, 8, 8, [&](int x, int y, uint16_t c, uint8_t al) {
    ++calls; o2[y * 8 + x] = c; oa2[y * 8 + x] = al;
  });
  CHECK(calls == 64);
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 8; ++x) {
      CHECK(o2[y * 8 + x] == rgb[(y / 2) * 4 + x / 2]);
      CHECK(oa2[y * 8 + x] == a[(y / 2) * 4 + x / 2]);
    }

  // Offset destination, and null alpha means opaque.
  at.a = nullptr;
  int minx = 99, miny = 99;
  skin::drawSlice(at, s, 5, 7, 4, 4, [&](int x, int y, uint16_t, uint8_t al) {
    if (x < minx) minx = x;
    if (y < miny) miny = y;
    CHECK(al == 255);
  });
  CHECK(minx == 5 && miny == 7);
}

static void test_slots_inside_atlas_and_disjoint() {
  CHECK(skin::ATLAS_W == 256 && skin::ATLAS_H == 192 && skin::SLOT_VER == 1);
  CHECK(skin::S_COUNT == 7);
  for (int i = 0; i < skin::S_COUNT; ++i) {
    const skin::Slot& a = skin::SLOTS[i];
    CHECK(a.w > 0 && a.h > 0);
    CHECK(a.x + a.w <= skin::ATLAS_W && a.y + a.h <= skin::ATLAS_H);
    CHECK(a.border * 2 <= a.w && a.border * 2 <= a.h);
    for (int j = i + 1; j < skin::S_COUNT; ++j) {
      const skin::Slot& b = skin::SLOTS[j];
      bool sep = a.x + a.w <= b.x || b.x + b.w <= a.x ||
                 a.y + a.h <= b.y || b.y + b.h <= a.y;
      CHECK(sep);
    }
  }
  CHECK(skin::SLOTS[skin::S_ROW_SEL].w == 48 && skin::SLOTS[skin::S_ROW_SEL].h == 32);
  CHECK(skin::SLOTS[skin::S_ROW_SEL].border == 8);
  CHECK(skin::SLOTS[skin::S_ICONS_2X].y == 128 && skin::SLOTS[skin::S_ICONS_2X].w == 256);
}

// Copy of fb_setPixel's switch in Gotek_JC3248.ino (physical panel 320x480, portrait).
static void fbSwitch(int rot, int vx, int vy, int& px, int& py) {
  const int W = 320, H = 480;
  switch (rot) {
    case 1: px = vx; py = vy; break;
    case 2: px = (W - 1) - vy; py = vx; break;
    case 3: px = (W - 1) - vx; py = (H - 1) - vy; break;
    default: px = vy; py = (H - 1) - vx; break;
  }
}

static void test_rotMap_bijective_and_matches_firmware() {
  const int W = 320, H = 480;
  for (int rot = 0; rot < 4; ++rot) {
    const int lw = (rot & 1) ? W : H, lh = (rot & 1) ? H : W;
    const skin::RotMap m = skin::rotMap(rot, W, H);
    std::vector<int> hit((size_t)W * H, 0);
    int bad = 0, out = 0;
    for (int y = 0; y < lh; ++y)
      for (int x = 0; x < lw; ++x) {
        int px, py, qx, qy;
        skin::mapRot(rot, W, H, x, y, px, py);
        fbSwitch(rot, x, y, qx, qy);
        if (px != qx || py != qy) ++bad;
        long i = (long)m.base + (long)x * m.sx + (long)y * m.sy;
        if (i < 0 || i >= (long)W * H) { ++out; continue; }
        if (i != (long)qy * W + qx) ++bad;
        hit[(size_t)i]++;
      }
    CHECK(bad == 0);
    CHECK(out == 0);
    int notOnce = 0;
    for (size_t k = 0; k < hit.size(); ++k) if (hit[k] != 1) ++notOnce;
    CHECK(notOnce == 0);
    CHECK(m.sx == 1 || m.sx == -1 || m.sy == 1 || m.sy == -1);
  }
}

static void test_drawSliceColumns_same_pixels() {
  std::vector<uint16_t> rgb((size_t)skin::ATLAS_W * skin::ATLAS_H);
  std::vector<uint8_t> a(rgb.size());
  for (size_t i = 0; i < rgb.size(); ++i) { rgb[i] = (uint16_t)(i * 2654435761u >> 7); a[i] = (uint8_t)(i * 31); }
  skin::Atlas at; at.w = skin::ATLAS_W; at.h = skin::ATLAS_H; at.rgb = rgb.data(); at.a = a.data();
  const int sizes[3][2] = {{100, 30}, {7, 50}, {48, 48}};
  for (int s = 0; s < 3; ++s) {
    const int dw = sizes[s][0], dh = sizes[s][1], dx = 3, dy = 5;
    std::vector<uint32_t> r1((size_t)(dw + dx) * (dh + dy), 0), r2(r1.size(), 0);
    int c1 = 0, c2 = 0, lastX = -1, colBreaks = 0, lastY = -1;
    skin::drawSlice(at, skin::SLOTS[skin::S_PANEL], dx, dy, dw, dh, [&](int x, int y, uint16_t c, uint8_t al) {
      ++c1; r1[(size_t)y * (dw + dx) + x] = ((uint32_t)al << 16 | c) + 1;
    });
    skin::drawSliceColumns(at, skin::SLOTS[skin::S_PANEL], dx, dy, dw, dh, [&](int x, int y, uint16_t c, uint8_t al) {
      ++c2; r2[(size_t)y * (dw + dx) + x] = ((uint32_t)al << 16 | c) + 1;
      if (x == lastX && y != lastY + 1) ++colBreaks;
      lastX = x; lastY = y;
    });
    CHECK(c1 == dw * dh);
    CHECK(c2 == dw * dh);
    CHECK(r1 == r2);
    CHECK(colBreaks == 0);
  }
}

int main() {
  test_rotMap_bijective_and_matches_firmware();
  test_drawSliceColumns_same_pixels();
  test_slice9_covers_exactly();
  test_slice9_corners_unscaled();
  test_blend_edges_and_mid();
  test_iconScale();
  test_drawSlice_matches_manual();
  test_slots_inside_atlas_and_disjoint();
  std::printf("skin_draw: %d checks, %d failed\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
