# Gotek_P4 — the full JC 5.8.6 interface, ported to ESP32-P4 (JC4880P443C)

**This is the real conversion**, not a probe: the entire `Gotek_JC3248` **5.8.6** firmware
(categories, recurse-flatten library, JPEG/PNG cover art, carousel, save-writeback, SD library,
USB-MSC — the whole feature set) compiled for the ESP32-P4 with the ST7701 MIPI-DSI display and
GT911 touch. Wireless is stubbed (see below). **Compiles clean: 29% flash, 46% RAM. Not yet run on
hardware** — flash it and see.

## Why JC3248 was the right base (answering the RGB question)
The panel interface (RGB / QSPI / DSI) is only the electrical layer, which we replace wholesale. What
matters is the *draw model*, and the JC — like the 7-inch — composes into one PSRAM framebuffer via
its own `gfx_*` layer, then flushes. That maps 1:1 onto a DSI panel. JC3248 wins over the 7-inch
because it's **portrait** and its UI is already **resolution-independent** (`relayout()` reflows from
`gW`/`gH`), so targeting the P4's native 480×800 is "set the canvas dimensions and reflow" rather than
a rewrite.

## What was swapped (the whole port surface)
- **Display:** JC's AXS15231B QSPI panel init → `p4_display.c` (ST7701 480×800 MIPI-DSI, proven
  benchmark config). `gfx_flush()` → `p4disp_present(framebuffer)`.
- **Canvas:** `LCD_WIDTH/HEIGHT` → 480×800; `gW/gH` → 480×800; forced **portrait** (`g_rot=1`), which
  is the JC's existing *identity* pixel map — so `fb_setPixel`/`gfx_fillRect` didn't need rewriting.
- **Byte order:** `swap16()` neutralised (QSPI panel wanted big-endian; DSI is native little-endian).
- **Touch:** AXS integrated touch → **GT911** on I²C SDA7/SCL8 (native 480×800 = our canvas, identity).
- **Wireless:** `espnow_server.cpp` replaced with `espnow_server_p4stub.cpp` — every ESP-NOW call is a
  no-op, because **ESP-NOW doesn't link in stock Arduino for P4**. Stage 3 swaps this for the WiFi/TCP
  transport to Webby dongles (which compiles).

Everything above `gfx_*` — menus, categories, cover loading, the lot — is **unchanged JC 5.8.6 code**.

## Flash
```
esptool --chip esp32p4 -p <PORT> -b 460800 write_flash \
  0x2000 flash_gotek_p4/bootloader.bin  0x8000 flash_gotek_p4/partitions.bin  0x10000 flash_gotek_p4/app.bin
```
(or the single `merged.bin` at `0x0`). Won't connect? Hold BOOT, tap RESET, release BOOT; close the
serial monitor first. The Arduino P4 bootloader is `min_rev v0.0`, so it won't hit the v1.3
illegal-instruction trap your IDF build did.

## What to expect / watch for (first hardware run)
- **The real GTi UI should render** in portrait at 480×800, reflowed by `relayout()`. Some layout
  constants were tuned for 320×480 — expect a few spacing/размер niggles to tidy once you see it, not a
  rewrite.
- **Touch pins (RST22/INT21, addr 0x5D) are community-sourced.** If the UI draws but touch is dead or
  offset, that's the pins/addr — send me the real ones.
- **USB-MSC** presents the 2 MB library ramdisk (same as JC). Confirm the Gotek/PC mounts it; try both
  USB-C ports (only one is the HS OTG, GPIO49/50).
- **SD** (`SD_MMC`) TF-slot pins are unconfirmed on this board — SD browse may need the pins set.
- If it renders then reboots, suspect the 400 MHz-on-v1.3 instability (your recipe) — tell me and I'll
  pin 360 via a build flag.
- **Wireless does nothing** (stubbed) — flinging to a dongle is a no-op until Stage 3 (Webby-over-WiFi).

## Next
Give me: (1) touch working y/n + real pins, (2) which USB-C enumerates, (3) a photo of the UI so I can
tune the 480×800 layout. Then I fold in the WiFi/TCP Webby transport for wireless and tidy the layout.
