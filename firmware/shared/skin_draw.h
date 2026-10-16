// Skin atlas slot table, 9-slice geometry and RGB565 blending.
// No Arduino includes: shared by the ESP32 firmware and host tests.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace skin {

constexpr int ATLAS_W = 256;
constexpr int ATLAS_H = 192;
constexpr int SLOT_VER = 1;

enum SlotId : uint8_t {
  S_KEY_UP, S_KEY_DOWN, S_PANEL, S_ROW_SEL, S_TILE, S_ICONS, S_ICONS_2X, S_COUNT
};

struct Slot { uint16_t x, y, w, h; uint8_t border; };

// Icon cells (row-major, first 8 named, rest reserved): INSERT, EJECT, FOLDER,
// STAR, GEAR, BATTERY, WIFI, SEARCH. ICONS: 8x2 cells of 16; ICONS_2X: 8x2 of 32.
constexpr Slot SLOTS[S_COUNT] = {
  {0,   0,   48,  48, 10},  // KEY_UP
  {48,  0,   48,  48, 10},  // KEY_DOWN
  {96,  0,   48,  48, 10},  // PANEL
  {144, 0,   48,  32, 8},   // ROW_SEL
  {0,   64,  64,  64, 0},   // TILE
  {64,  64,  128, 32, 0},   // ICONS
  {0,   128, 256, 64, 0},   // ICONS_2X
};

struct Piece { int16_t sx, sy, sw, sh, dx, dy, dw, dh; };

// Splits slot s into up to 9 pieces covering a dw x dh destination exactly.
// Returns the number of non-empty pieces written to out.
inline int slice9(const Slot& s, int dw, int dh, Piece out[9]) {
  if (dw <= 0 || dh <= 0) return 0;
  int b = s.border;
  int m = (dw < dh ? dw : dh) / 2;
  if (dw < 2 * b || dh < 2 * b) b = m < b ? m : b;
  const int sxs[3] = {0, b, (int)s.w - b};
  const int sws[3] = {b, (int)s.w - 2 * b, b};
  const int sys[3] = {0, b, (int)s.h - b};
  const int shs[3] = {b, (int)s.h - 2 * b, b};
  const int dxs[3] = {0, b, dw - b};
  const int dws[3] = {b, dw - 2 * b, b};
  const int dys[3] = {0, b, dh - b};
  const int dhs[3] = {b, dh - 2 * b, b};
  int n = 0;
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c) {
      if (dws[c] <= 0 || dhs[r] <= 0 || sws[c] <= 0 || shs[r] <= 0) continue;
      Piece& p = out[n++];
      p.sx = (int16_t)(s.x + sxs[c]); p.sy = (int16_t)(s.y + sys[r]);
      p.sw = (int16_t)sws[c];         p.sh = (int16_t)shs[r];
      p.dx = (int16_t)dxs[c];         p.dy = (int16_t)dys[r];
      p.dw = (int16_t)dws[c];         p.dh = (int16_t)dhs[r];
    }
  return n;
}

// Host-order RGB565 (not byte-swapped). a=0 gives dst, a=255 gives src.
inline uint16_t blend565(uint16_t src, uint8_t a, uint16_t dst) {
  if (a == 0) return dst;
  if (a == 255) return src;
  const uint32_t ia = 255u - a;
  uint32_t r = (((src >> 11) & 31u) * a + ((dst >> 11) & 31u) * ia + 127u) / 255u;
  uint32_t g = (((src >> 5) & 63u) * a + ((dst >> 5) & 63u) * ia + 127u) / 255u;
  uint32_t bl = ((src & 31u) * a + (dst & 31u) * ia + 127u) / 255u;
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

inline int iconScale(int screenW, int screenH) {
  return (screenW > screenH ? screenW : screenH) >= 800 ? 2 : 1;
}

struct Atlas { uint16_t w = 0, h = 0; uint16_t* rgb = nullptr; uint8_t* a = nullptr; };

// Calls put(x, y, rgb, a) once per destination pixel (absolute coordinates
// dx+.., dy+..), nearest-neighbour inside stretched pieces. A null alpha plane
// means opaque.
template <class Put>
void drawSlice(const Atlas& at, const Slot& s, int dx, int dy, int dw, int dh, Put put) {
  Piece p[9];
  int n = slice9(s, dw, dh, p);
  for (int k = 0; k < n; ++k) {
    for (int y = 0; y < p[k].dh; ++y) {
      int sy = p[k].sy + (y * p[k].sh) / p[k].dh;
      for (int x = 0; x < p[k].dw; ++x) {
        int sx = p[k].sx + (x * p[k].sw) / p[k].dw;
        size_t i = (size_t)sy * at.w + (size_t)sx;
        put(dx + p[k].dx + x, dy + p[k].dy + y, at.rgb[i], at.a ? at.a[i] : (uint8_t)255);
      }
    }
  }
}

}  // namespace skin
