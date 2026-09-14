#pragma once
#include <stdint.h>

namespace GotekSaveGeometry {
constexpr uint32_t maxImageBytes = (4096u - 11u) * 512u;
inline bool header(uint32_t imageBytes, uint16_t mapBytes) {
  return imageBytes && imageBytes <= maxImageBytes &&
    mapBytes == ((imageBytes + 511) / 512 + 7) / 8;
}
inline bool bitmap(uint32_t imageBytes, const uint8_t* map, uint16_t mapBytes) {
  if (!header(imageBytes, mapBytes)) return false;
  uint32_t sectors = (imageBytes + 511) / 512;
  for (uint32_t i = sectors; i < uint32_t(mapBytes) * 8; ++i)
    if (map[i >> 3] & (1u << (i & 7))) return false;
  return true;
}
}
