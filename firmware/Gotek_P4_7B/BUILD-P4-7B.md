# Gotek_P4_7B — Arduino build settings (Waveshare ESP32-P4-WIFI6-Touch-LCD-7B)

7-inch (1024×600) variant of `Gotek_P4`. Same UI/engine as `5.9.13-P4`; only the
**display backend, resolution, SD pins and touch mapping** changed. **Radio is dormant**
(no C6/WiFi/ESP-NOW bring-up) — Michael asked for a display-first build; wireless can be
folded back in later (see "Bringing the radio back" below). On-screen identity: **`5.9.13-P4-7B`**.

> **NOT yet run on hardware.** This is the first bring-up. Structural review done; no compile
> was run here (owner compiles, per our standing rule).

## What's different vs Gotek_P4 (4.3" JC4880P443C)
| Area | 4.3" (JC4880) | 7B (this build) |
|---|---|---|
| Panel | ST7701, 480×800, native **portrait** | **EK79007, 1024×600, native landscape** |
| DSI | 2-lane @ 500 Mbps, DPI 34 MHz | 2-lane @ **1000 Mbps**, DPI **52 MHz** |
| Porches | h 12/42/42 · v 2/8/166 | h **10/160/160** · v **1/23/12** |
| Reset / Backlight | RST 5 / BL 23 | **RST 33 / BL 32 (ACTIVE-LOW)** |
| Rotation | landscape UI rotated 90° into portrait FB | **identity** (native landscape, no rotate) |
| microSD (1-bit) | CLK12/CMD11/D0 13 | **CLK43/CMD44/D0 39** |
| Touch | GT911 SDA7/SCL8 @0x5D | **same** (SDA7/SCL8 @0x5D) |
| Radio | C6 WiFi/ESP-NOW | **dormant** (C6 self-update skipped) |

## Files in this sketch
- `Gotek_P4_7B.ino` — the port (edited: resolution, gW/gH, fb_setPixel + gfx_fillRect + touch transforms for a landscape-native panel, SD pins, backlight, version, radio skip).
- `p4_display.c` / `p4_display.h` — **new EK79007 1024×600 backend** (replaces the ST7701 one).
- `esp_lcd_ek79007.c` / `esp_lcd_ek79007.h` — **vendored** Espressif driver v2.0.2 (the version-log line was patched to literals since the IDF component manager isn't generating a version header in an Arduino build). This is the same "vendor the panel driver locally" pattern we used for ST7701.
- `espnow_server.h` / `espnow_server_p4wifi.cpp` — unchanged (compiled but never armed; radio dormant).
- `diag_adf.h`, `retro_assets.h`, `omega_logo.h`, `partitions.csv` — unchanged from Gotek_P4.
- **Dropped:** `esp_lcd_st7701*` (4.3"-only), `bake_c6.py` (no C6 baking needed).

## Tools ▸ menu selections (same as Gotek_P4)
| Setting | Value |
|---|---|
| Board | **ESP32P4 Dev Module** |
| Flash Size | **16MB** |
| PSRAM | **Enabled** (framebuffer 1.23 MB + ramdisk live here) |
| CPU Frequency | **360 MHz** — do NOT force 400 (v1.3 silicon unstable) |
| USB Mode | **USB-OTG (TinyUSB)** |
| USB CDC On Boot | **Disabled** |
| Partition Scheme | sketch-local `partitions.csv` (dual-app OTA) overrides the menu |
| Upload Speed | 460800 |

Requires **arduino-esp32 core 3.x** (lists *ESP32P4 Dev Module*) with the managed `esp_lcd`
MIPI-DSI API in core — same core that already builds Gotek_P4.

## Flash (esptool)
```
esptool --chip esp32p4 -p <PORT> -b 460800 write_flash 0x0 <merged>.bin
```
Won't connect? Hold **BOOT**, tap **RESET**, release **BOOT**; close the serial monitor first;
use the USB-C that enumerates as USB-Serial-JTAG (on the 7B it's typically the port nearest the
USB-UART/JTAG label — try both if unsure). Supply ≥600 mA.

## First-run expectations
- **UI should render** at 1024×600 landscape, reflowed by `relayout()`. A boot **TOUCH CHECK**
  screen appears first (tap the 4 corners) — it prints raw GT911 coords and drops a marker at the
  identity transform. If the marker tracks your finger, touch mapping is correct.
- **Layout polish is a known TODO.** Many layout constants were tuned for the 320×480 / 480×800
  boards; at 1024×600 everything reflows and is usable, but spacing/reel sizing will want a tuning
  pass once we see it on the panel. That's cosmetic, not a rewrite.
- **USB-MSC** presents the PSRAM ramdisk to the Gotek/PC — confirm it mounts (try both USB-C ports).
- **microSD** browse should work on CLK43/CMD44/D0 39. If the card won't mount, the SDMMC IO bank
  (GPIO39-45) is powered by the board's **VO4** rail — the Waveshare Arduino SD demo mounts with
  just `setPins`+`begin`, so VO4 should be on by default; flag it if the card is dead and I'll add
  an explicit power-enable.

## Things to verify on the bench (report back)
1. Panel lights up + image is correct 1024×600 (not torn/garbled/half-screen).
2. Backlight actually turns on (it's active-low — if the screen shows an image but is dark, the BL polarity is the thing to flip).
3. Touch: marker tracks finger in the TOUCH CHECK screen; corners map right.
4. microSD mounts and the library lists.
5. USB-MSC enumerates on the Gotek.

## Bringing the radio back (later)
The 7B has a real ESP32-C6, so WiFi via esp-hosted is possible. To re-enable: uncomment the
`c6SelfUpdate()` line in `setup()` (search "no-radio build") and make sure a `c6fw` image is
available (SD override `c6_network_adapter_*.bin` or a baked `c6fw` partition). ESP-NOW to the
SuperMini/XIAO dongles is a separate lift (doesn't link on P4 core; needs the hybrid path).

## Tuning knobs if bring-up misbehaves
- **Nothing on screen at all** → first suspect is the **DSI-PHY LDO** (channel 3 @ 2.5 V) — that's the #1 P4 bring-up trap; it's set in `p4_display.c`.
- **Image but wrong geometry / tearing** → DPI clock (52 MHz) or lane rate (set to 1000 Mbps; the driver default is 900 — try 900 if 1000 is marginal).
- **Screen renders then reboots** → suspect 400 MHz on v1.3 silicon; confirm CPU is pinned to 360.
