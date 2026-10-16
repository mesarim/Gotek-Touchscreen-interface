// GTS1 skin container: parser and packer. Header-only, no Arduino includes,
// shared by the firmware and the host tests.
// Layout (little endian): "GTS1", u8 reserved, u8 slotVer, u16 manifestLen,
// manifest, u32 atlasLen, atlas PNG, u8 nBg, nBg x (u8 bgId, u32 len, PNG).
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace gts {

constexpr uint32_t MAX_FILE = 262144;
constexpr uint8_t MAX_BG = 6;
constexpr uint16_t MAX_MANIFEST = 1024;

enum Err : uint8_t { OK, TOO_SHORT, BAD_MAGIC, BAD_SLOTVER, BAD_LENGTH, TOO_BIG,
                     BAD_PNG, TOO_MANY_BG, BAD_BGID, DUP_BG };

struct Chunk { const uint8_t* p; uint32_t n; };
struct View {
  uint8_t slotVer;
  Chunk manifest;
  Chunk atlas;
  uint8_t nBg;
  uint8_t bgId[MAX_BG];
  Chunk bg[MAX_BG];
};

enum BgId : uint8_t { BG_480x320 = 1, BG_320x480 = 2, BG_480x272 = 3, BG_272x480 = 4, BG_TILE = 5 };

inline const char* errText(Err e) {
  switch (e) {
    case OK: return "ok";
    case TOO_SHORT: return "file too short";
    case BAD_MAGIC: return "not a GTS1 skin file";
    case BAD_SLOTVER: return "unsupported slot version";
    case BAD_LENGTH: return "bad chunk length";
    case TOO_BIG: return "file too big";
    case BAD_PNG: return "chunk is not a PNG";
    case TOO_MANY_BG: return "too many backgrounds";
    case BAD_BGID: return "unknown background id";
    case DUP_BG: return "duplicate background id";
  }
  return "unknown error";
}

inline uint8_t bgIdFor(int w, int h) {
  if (w == 480 && h == 320) return BG_480x320;
  if (w == 320 && h == 480) return BG_320x480;
  if (w == 480 && h == 272) return BG_480x272;
  if (w == 272 && h == 480) return BG_272x480;
  return 0;
}

namespace detail {
inline bool isPng(const Chunk& c) {
  static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  return c.n >= 8 && memcmp(c.p, sig, 8) == 0;
}
inline uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
inline uint32_t rd32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
}  // namespace detail

// Every length is checked against the remaining bytes by subtraction, so a
// hostile u32 can never wrap an offset.
inline Err parse(const uint8_t* d, size_t n, View& v) {
  using namespace detail;
  memset(&v, 0, sizeof v);
  if (n > MAX_FILE) return TOO_BIG;
  if (n < 8) return TOO_SHORT;
  if (memcmp(d, "GTS1", 4) != 0) return BAD_MAGIC;
  v.slotVer = d[5];
  uint16_t mlen = rd16(d + 6);
  size_t off = 8, rem = n - 8;
  if (mlen > MAX_MANIFEST || mlen > rem) return BAD_LENGTH;
  v.manifest = {d + off, mlen};
  off += mlen; rem -= mlen;
  if (rem < 4) return BAD_LENGTH;
  uint32_t alen = rd32(d + off);
  off += 4; rem -= 4;
  if (alen > rem) return BAD_LENGTH;
  v.atlas = {d + off, alen};
  if (!isPng(v.atlas)) return BAD_PNG;
  off += alen; rem -= alen;
  if (rem < 1) return BAD_LENGTH;
  uint8_t nbg = d[off];
  off += 1; rem -= 1;
  if (nbg > MAX_BG) return TOO_MANY_BG;
  for (uint8_t i = 0; i < nbg; ++i) {
    if (rem < 5) return BAD_LENGTH;
    uint8_t id = d[off];
    uint32_t len = rd32(d + off + 1);
    off += 5; rem -= 5;
    if (id < BG_480x320 || id > BG_TILE) return BAD_BGID;
    for (uint8_t j = 0; j < i; ++j) if (v.bgId[j] == id) return DUP_BG;
    if (len > rem) return BAD_LENGTH;
    v.bgId[i] = id;
    v.bg[i] = {d + off, len};
    if (!isPng(v.bg[i])) return BAD_PNG;
    off += len; rem -= len;
    v.nBg = (uint8_t)(i + 1);
  }
  if (rem != 0) return BAD_LENGTH;
  return OK;
}

// Serialises v. Returns the byte count, or 0 if cap is too small or v does not
// fit the format (manifest over MAX_MANIFEST, more than MAX_BG backgrounds).
inline size_t pack(const View& v, uint8_t* out, size_t cap) {
  if (v.manifest.n > MAX_MANIFEST || v.nBg > MAX_BG) return 0;
  size_t need = 8 + (size_t)v.manifest.n + 4 + (size_t)v.atlas.n + 1;
  for (uint8_t i = 0; i < v.nBg; ++i) need += 5 + (size_t)v.bg[i].n;
  if (need > cap) return 0;
  uint8_t* w = out;
  auto p32 = [&](uint32_t x) { for (int s = 0; s < 32; s += 8) *w++ = (uint8_t)(x >> s); };
  memcpy(w, "GTS1", 4); w += 4;
  *w++ = 0;
  *w++ = v.slotVer;
  *w++ = (uint8_t)(v.manifest.n & 255);
  *w++ = (uint8_t)(v.manifest.n >> 8);
  if (v.manifest.n) memcpy(w, v.manifest.p, v.manifest.n);
  w += v.manifest.n;
  p32(v.atlas.n);
  if (v.atlas.n) memcpy(w, v.atlas.p, v.atlas.n);
  w += v.atlas.n;
  *w++ = v.nBg;
  for (uint8_t i = 0; i < v.nBg; ++i) {
    *w++ = v.bgId[i];
    p32(v.bg[i].n);
    if (v.bg[i].n) memcpy(w, v.bg[i].p, v.bg[i].n);
    w += v.bg[i].n;
  }
  return (size_t)(w - out);
}

}  // namespace gts
