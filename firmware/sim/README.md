# GTi simulator (3.5″ JC3248, firmware A600)

The real GTi firmware running in a web browser. `Gotek_JC3248.ino` is compiled **unchanged** to WebAssembly;
only the hardware around it is simulated. Live at `docs/demo/` (GitHub Pages: `/demo/`).

| On the board | In the simulator |
|---|---|
| AXS15231B 320×480 QSPI panel | `esp_lcd_panel_draw_bitmap` → a canvas (the page turns it to landscape like the case) |
| AXS15231B touch on I²C 0x3B | `Wire.requestFrom` builds the same 8-byte touch report from the mouse / finger |
| microSD (4-bit SDMMC) | a virtual FAT32 card: FAT, directories and boot sectors are generated from a file list, file bytes are read on demand, every sector the firmware writes goes into an overlay. FatFs R0.15, the SD guard, the raw FAT walker and the alias mount all run on real sectors. |
| USB MSC to the Gotek / PC | `USBMSC` callbacks are called by the page: the "Gotek" reads the presented FAT12 volume, "Amiga saves a game" writes into it; in SD ACCESS a "PC" reads the card and can eject |
| ESP-NOW + TCP to Webby dongles | `espnow_server.h` implemented by `shim/src/sim_espnow.cpp`: three simulated dongles (scan, pair, send, eject, heartbeat, dirty beacon, save fetch) |
| Wi-Fi web page, WebDAV, home Wi-Fi | not available (stubs say so) |
| FreeRTOS, PSRAM, OTA, RTC NOINIT | one thread (asyncify: every `delay()` yields to the browser), 8 MB PSRAM accounting (malloc is wrapped), OTA counts bytes, NOINIT survives `ESP.restart()` |

The only edit to the firmware text is in `tools/sim_sketch.py`: the single `try/catch` around the library build
becomes a plain block (no C++ exceptions in this wasm build; out-of-memory aborts instead).

## Build

```
python3 -m pip install ziglang==0.16.0      # clang + libc++ for wasm32-wasi
npm i -g binaryen                           # wasm-opt (asyncify)
# arduino-cli with esp32:esp32 3.3.12, JPEGDEC and PNGdec installed (same as the firmware build)
./build.sh                                   # -> build/gti.wasm and web/gti.wasm
PUBLISH=../../docs/demo ./build.sh           # also copy index.html / gti_core.js / gti_worker.js / gti.wasm to the site
```

`build.sh` asks `arduino-cli compile --preprocess` for the sketch exactly as the IDE compiles it (with its generated
prototypes), so the simulator follows whatever firmware is in `../Gotek_JC3248`.

## Demo library

`tools/make_library.py` builds `web/library/` (copied to `docs/demo/library/`) from freely distributable games:
Nippon Safes Inc. (ScummVM / Dynabyte free licence), Enemy 1 + 2 (Anachronia freeware, non-commercial), MegaBall 4
(Apache 2.0), Gravity Force 2 (CC BY-SA), Solid Gold (freeware / PD), L'Abbaye des Morts (GPL port, CC BY original),
Uwol and Sir Ababol (CC BY-NC-SA 3.0), cyberTRON (freeware). Each game's licence / readme sits next to its disks.
Sources (downloaded into `lib_src/`, not committed here):
scummvm.org Nippon Safes zips, auralis.ch Anachronia ADFs, aminet.net (MegaBall4, SolidGold, LAbbayeDesMorts,
Uwol-Quest_for_money, SirAbabol, cyberTRON), lysator.liu.se/~jensa/gf2 (gf2-adf.zip), covers from locomalito.com,
mojontwins.com and the Nippon Safes box scan; the other covers are drawn by the script.

## Tests

- `node tools/node_run.js script.json` boots the firmware headless, plays taps/drags and saves screenshots.
- `node tools/pw_test.js steps.json` drives the web page in headless Chromium (serve `web/` on :8766).

## Things the simulator found

- PNGdec only buffers rows up to ~426 RGB pixels (`PNG_MAX_BUFFERED_PIXELS = (320*4+1)*2`): a wider PNG (cover or
  screensaver slide) stops after two rows and is skipped. Slides here are 424 px wide.
