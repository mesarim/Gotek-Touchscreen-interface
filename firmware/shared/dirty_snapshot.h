#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Call while holding the same lock used by the USB write callback. Writes
// after begin() go to a new live map, even if that sector is in the snapshot.
namespace GotekDirty {
inline void begin(uint8_t* live, uint8_t* snapshot, size_t bytes,
                  volatile uint16_t& count) {
  memcpy(snapshot, live, bytes);
  memset(live, 0, bytes);
  count = 0;
}
inline void finish(uint8_t* live, const uint8_t* snapshot, size_t bytes,
                   volatile uint16_t& count, bool committed) {
  if (committed) return;
  for (size_t i = 0; i < bytes; ++i) {
    uint8_t missing = snapshot[i] & ~live[i];
    for (uint8_t b = missing; b; b &= b - 1) count = count + 1;
    live[i] |= missing;
  }
}
}
