// base64url decoding for the skin upload (POST /api/skins/<NAME>/chunk, data=<base64url>).
// Header-only, no Arduino includes, shared by the firmware and the host tests (tools/test_skin_b64.cpp).
// Alphabet A-Z a-z 0-9 '-' '_' (RFC 4648 section 5). Padding '=' is optional; when present it must fill
// the last group of 4. Anything else (whitespace, '+', '/', NUL) makes the whole input invalid.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace skin {

inline int b64urlVal(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '-') return 62;
  if (c == '_') return 63;
  return -1;
}

// Decodes s[0..n) into out (cap bytes). Returns the decoded length, or -1 for invalid input or cap too small.
inline int b64urlDecode(const char* s, size_t n, uint8_t* out, size_t cap) {
  if (n == 0) return 0;
  if (!s) return -1;
  size_t m = n;                                   // length without padding
  if (s[m - 1] == '=') {
    if (n % 4 != 0) return -1;
    --m;
    if (s[m - 1] == '=') --m;
  }
  if (m % 4 == 1) return -1;                      // one character carries only 6 bits: never valid
  if (m == 0) return -1;                          // padding alone
  const size_t len = (m / 4) * 3 + ((m % 4) ? (m % 4) - 1 : 0);
  if (len > cap || len > 0x7FFFFFFF || !out) return -1;
  size_t o = 0;
  uint32_t acc = 0;
  int bits = 0;
  for (size_t i = 0; i < m; ++i) {
    const int v = b64urlVal(s[i]);
    if (v < 0) return -1;
    acc = (acc << 6) | (uint32_t)v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out[o++] = (uint8_t)(acc >> bits);
      acc &= (1u << bits) - 1;
    }
  }
  return (int)o;
}

}  // namespace skin
