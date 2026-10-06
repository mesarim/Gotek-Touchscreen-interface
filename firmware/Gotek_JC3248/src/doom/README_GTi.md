# src/doom — PrBoom inside the GTi (JC3248 only)

Hidden easter egg. **GPL-2** (see COPYING.GPL2, AUTHORS). The GTi's own code is MIT; a JC3248
firmware binary built with this folder is a combined work and is distributed under the GPL-2 —
its complete source is this repository.

Source: [espressif/esp32-doom](https://github.com/espressif/esp32-doom) @ `085f21b` (2017-07-04) — PrBoom 2.5.0
ported to the ESP32 by Jeroen Domburg / Espressif. Files are flattened into one folder so the
Arduino IDE builds them without extra include paths.

## GTi changes
- `i_system.c` rewritten: the WAD is the `doom` flash partition (type 0x40, subtype 0x06),
  memory-mapped once with the IDF-5 API; `I_Read` advances the offset like POSIX `read()`.
- `i_video.c` rewritten: frames go to `gti_doom_present()` in the sketch (scaled onto the
  480x320 canvas); input is the touch screen (d-pad left, fire/use right, menu/enter/weapon
  middle, hold top-left 2 s to quit). No sound (stubs).
- `gti_doom_alloc.c` (new) + ~25 arrays in the engine turned from static arrays into heap
  pointers (marked `GTi: heap`) — 55 KB less static DRAM for every GTi boot.
- `*.inl` renamed `*_inl.h` (the Arduino builder does not copy .inl files).
- Dropped: `spi_lcd.c`, `psxcontroller.c`, `gamepad.c`, `w_memcache.c.disabled`.

Host-tested (x86, -m32) with doom1-cut.wad: title → new game via touch → walk, fire, turn, 600 frames.
