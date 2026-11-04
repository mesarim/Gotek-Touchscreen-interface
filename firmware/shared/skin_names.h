// Theme / skin names for the THEME page and the web list. Header-only, no Arduino includes,
// shared by the firmware and the host tests (tools/test_skin_names.cpp).
// A name is what CONFIG.TXT THEME= takes: A-Z 0-9 _, 1..16 characters, not starting with a digit
// (THEME=3 is a theme number). Anything else (lower case, spaces, dots, slashes) is not a theme.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "skin_gts.h"   // gts::MAX_MANIFEST

namespace skin {

constexpr int NAME_LEN = 16;

inline bool validName(const char* s) {
  if (!s || !s[0] || (s[0] >= '0' && s[0] <= '9')) return false;
  int n = 0;
  for (; s[n]; ++n) {
    char c = s[n];
    if (n >= NAME_LEN) return false;
    if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return false;
  }
  return true;
}

// The valid names of both lists, each once, sorted (strcmp), the first maxN of them. Returns the count.
inline int mergeNames(const char* const* a, int na, const char* const* b, int nb,
                      char out[][NAME_LEN + 1], int maxN) {
  int n = 0;
  for (int pass = 0; pass < 2; ++pass) {
    const char* const* l = pass ? b : a;
    int nl = pass ? nb : na;
    for (int i = 0; l && i < nl; ++i) {
      const char* s = l[i];
      if (!validName(s)) continue;
      int k = 0;
      while (k < n && strcmp(out[k], s) < 0) ++k;
      if (k < n && strcmp(out[k], s) == 0) continue;
      if (k >= maxN) continue;              // sorts after every kept name and the list is full
      int last = (n < maxN) ? n : maxN - 1;  // when full, the last name falls off
      for (int m = last; m > k; --m) memcpy(out[m], out[m - 1], NAME_LEN + 1);
      memcpy(out[k], s, strlen(s) + 1);
      if (n < maxN) ++n;
    }
  }
  return n;
}

// The manifest length from the first 8 bytes of a .gts ("GTS1", u8, u8 slotVer, u16 LE length),
// so a list can read just the manifest without the atlas. -1: not a GTS1 header or length over gts::MAX_MANIFEST.
inline int manifestLen(const uint8_t* h, size_t n) {
  if (!h || n < 8 || memcmp(h, "GTS1", 4) != 0) return -1;
  int len = h[6] | (h[7] << 8);
  return len > gts::MAX_MANIFEST ? -1 : len;
}

}  // namespace skin
