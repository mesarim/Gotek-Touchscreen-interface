# GTi — Waveshare 7″ port (BETA — v4.6.2-7IN)

**OMEGAWARE Gotek Touchscreen interface** for the **Waveshare ESP32-S3-Touch-LCD-7**
(ESP32-S3, 800×480 ST7262 RGB panel, GT911 touch, CH422G/TCA9554 I/O expander).

> **Status: BETA for testing.** Verified on the bench: tear-free rendering, touch,
> drag-scroll with inertia, cover art, cracktro, themes, and the USB virtual disk
> (tested against Windows). **Gotek/FlashFloppy end-to-end test still in progress.**
> Feedback wanted — see "What to test" below.

This port runs the **K engine**: raw `esp_lcd` with a double framebuffer and
VSYNC page-flip — **no LovyanGFX** (no display library to install or version-pin).
Landscape only (`ROTATE=0` or `180`; portrait is not supported on this RGB panel).

---

## Build (Arduino IDE)

**Board support:** esp32 by Espressif Systems, **core 3.x** (Boards Manager).

**Libraries:** just **JPEGDEC** (Library Manager, by Larry Bank). Nothing else.
Do **not** install the Adafruit TinyUSB library — TinyUSB comes with the core.

**Sketch folder — all four files together:**

```
Waveshare_7inch/
  Gotek_7inch.ino      ← main firmware
  espnow_server.h      ← wireless / multicast
  espnow_server.cpp
  diag_adf.h           ← embedded Amiga Test Kit (public domain, ~99 KB compressed)
```

**Board menu — ESP32S3 Dev Module:**

| Setting | Value |
|---|---|
| Flash Size | 16MB (128Mb) |
| PSRAM | **OPI PSRAM** (wrong setting = boot crash) |
| Partition Scheme | Huge APP (3MB No OTA/1MB SPIFFS) |
| USB Mode | **USB-OTG (TinyUSB)** |
| USB CDC On Boot | **Disabled** |
| CPU Frequency | 240MHz |

## Flashing

The firmware presents USB as a **mass-storage disk** (CDC off), so its COM port
**disappears once it boots**. To reflash: hold **BOOT**, tap **RESET**, release
BOOT → the download-mode COM port appears → upload. Every time. This is normal.

## SD card

FAT32. Games in `/ADF/GameName/GameName.adf` (multi-disk: `-1`, `-2`, …), optional
`GameName.jpg` cover + `GameName.nfo` text in the same folder. A blank card
self-provisions folders + a commented `CONFIG.TXT` on first boot.

## Using it

- **Tap a game** = select. **INSERT** = mount to the Gotek. **EJECT** = unmount.
  (Tapping a game never mounts it — INSERT is the only trigger.)
- **Drag the list** to scroll; **flick** for momentum. A-Z bar jumps by letter.
- **INFO** (bottom-right) opens the settings panel; tap INFO again to close.
  The list still scrolls while INFO is open.
- **LOAD DIAG** (in INFO) mounts the built-in Amiga Test Kit — works with no SD card.
- **THEME** cycles 6 colour themes. **FLIP** (in INFO) rotates 180° for upside-down mounting.
- The USB disk only appears after INSERT — a silent USB port at idle is correct.

## What to test / report

1. INSERT a game → does the Gotek (FlashFloppy) see the disk and boot it?
2. Multi-disk games: disk selector buttons, swapping mid-game.
3. Scroll feel on large libraries (100+ games): smoothness, tap accuracy.
4. Any image corruption (lines wrapping top/bottom, right-edge noise) — say what
   you were doing when it happened (SD load? WiFi? USB attached?).
5. Touch reliability after long sessions (taps landing wrong / going dead).
6. Cover art JPEGs: wrong colours or missing art (note the file's dimensions).

Please report with: board revision (check the expander chip: CH422G vs TCA9554
if known), SD card size/class, FlashFloppy version, and what the screen showed.

---

## Known board gotcha (already handled in firmware, FYI for hackers)

The CH422G expander's **bit5 is USB_SEL** — it routes the USB data lines between
the ESP32 and the CAN transceiver. Drive it high and the USB disk vanishes while
everything else keeps working. The firmware keeps it low; if you modify the
expander code, keep it that way.

---

_GTi firmware by Mez & Dimmy / OMEGAWARE. Amiga Test Kit © Keir Fraser, public
domain (Unlicense) — bundled with thanks._
