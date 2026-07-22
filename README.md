# Gotek Touchscreen Interface

A touchscreen front-end for [Gotek](https://en.wikipedia.org/wiki/Gotek_Floppy_Emulator)-style
floppy emulators. It browses `.ADF` (Amiga) and `.DSK` (ZX / CPC) disk images from an SD card,
shows cover art and game info, and presents the selected image to the host machine as a USB
floppy — wrapped in a demoscene-flavoured UI (cracktro splash, copper bars, scroller, six themes).

Built by **Mez** and **Dimmy** (Dimitri Hilverda) — **OMEGAWARE**.

---

## Which board do I have?

| Your hardware | Use | Status |
|---|---|---|
| **Guition JC3248W535C** (3.5", 480x320) | [`Gotek_JC3248/`](firmware/Gotek_JC3248) | **Recommended — current build** |
| Waveshare **ESP32-S3-Touch-LCD-7** (7", 800x480) | [`Waveshare_7inch/`](firmware/Waveshare_7inch) | **BETA — public testing (v4.6.2)** |
| ESP32-S3 **Super Mini** (wireless dongle) | [`Gotek_SuperMini/`](firmware/SuperMini-S3) | Companion to the interface (optional) |
| Waveshare **ESP32-S3-Touch-LCD-2.8** | [`Gotek_Waveshare28/`](firmware/Waveshare_28) | **Legacy / out of date** |
| `Version 0.5.2/` | archive | **Legacy / out of date** |

Each firmware is a separate Arduino sketch and must live in a folder whose name matches its `.ino`
(an Arduino requirement). Don't mix board settings between sections — the PSRAM and partition
options differ per board, and the wrong ones will fail to boot or won't be seen by the Gotek.

---

## JC3248 — 3.5" interface (recommended)

The most developed build: fast cached boot, cover art, six themes, multi-disk grouping with a
paginated disk selector, A-Z jump, and optional wireless loading via a Super Mini dongle.

### Files
Flash `Gotek_JC3248/Gotek_JC3248.ino`; keep the other four files in the folder beside it:
`esp_lcd_axs15231b.c` / `.h` (display driver) and `espnow_server.cpp` / `.h` (wireless layer).

### Hardware
- **Board:** Guition JC3248W535C (ESP32-S3) https://a.aliexpress.com/_EyxPAFg
- **Display:** AXS15231B 480x320, QSPI (driven by the included driver)
- **Touch:** CST816 capacitive (I2C)
- **SD:** SD_MMC, 1-bit
- Presents the chosen image to the host over **USB Mass Storage** (TinyUSB)

### Build settings (Arduino IDE — select "ESP32S3 Dev Module")
| Setting | Value |
|---|---|
| USB Mode | USB-OTG (TinyUSB) |
| **USB CDC On Boot** | **Disabled** (required, or the Gotek won't see the drive) |
| USB Firmware MSC On Boot | Disabled |
| PSRAM | **OPI PSRAM** |
| Flash Size | 16MB |
| Flash Mode | QIO 120MHz |
| Partition Scheme | Huge APP (3MB No OTA / 1MB SPIFFS) |
| CPU Frequency | 240MHz |

**Library:** `JPEGDEC` by Larry Bank (Library Manager) for cover-art decoding.
**Do not** add the ESP-IDF `esp_lcd_touch.c/.h` files — they crash the Arduino build; touch is
handled in the `.ino`.

---

## Waveshare 7" — big-screen interface (BETA)

The JC3248 experience on a 7" 800x480 panel. Runs the **"K" rendering engine** — raw `esp_lcd`
with a double framebuffer and VSYNC page-flip, **no display library required** (LovyanGFX was
removed entirely in v4.5.0). Tear-free UI, drag-to-scroll with flick inertia, cover art, the
6-style cracktro, six themes, the built-in Amiga Test Kit diagnostic, and wireless mode.
Landscape only (0 / 180 degrees via the FLIP button — this RGB panel has no hardware portrait).

**Status: beta.** Rendering, touch, scrolling and the USB virtual disk are verified on real
hardware (USB tested against a PC; Gotek/FlashFloppy end-to-end verification in progress).
Testers welcome — see the full build guide, usage notes and **testing checklist** in
[`firmware/Waveshare_7inch/README.md`](firmware/Waveshare_7inch/README.md).

### Files
Flash `Waveshare_7inch/Gotek_7inch.ino`; keep `espnow_server.cpp` / `.h` and `diag_adf.h`
in the folder beside it.

### Hardware
- **Board:** Waveshare ESP32-S3-Touch-LCD-7 (ESP32-S3, 800x480 ST7262 RGB panel)
- **Touch:** GT911 capacitive (I2C; firmware auto-detects its address)
- **SD:** SD_MMC, 1-bit — FAT32
- Presents the chosen image to the host over **USB Mass Storage** (TinyUSB)

### Build settings (Arduino IDE — select "ESP32S3 Dev Module")
| Setting | Value |
|---|---|
| USB Mode | USB-OTG (TinyUSB) |
| **USB CDC On Boot** | **Disabled** (required, or the Gotek won't see the drive) |
| PSRAM | **OPI PSRAM** |
| Flash Size | 16MB (128Mb) |
| Partition Scheme | Huge APP (3MB No OTA / 1MB SPIFFS) |
| CPU Frequency | 240MHz |

**Library:** just `JPEGDEC` (Library Manager) — nothing else. Do **not** install LovyanGFX or the
Adafruit TinyUSB library for this build.

> Note: the firmware boots with USB **detached** — the USB disk only appears once you INSERT a
> game (or LOAD DIAG). A silent USB port at idle is normal. Reflashing always needs the manual
> BOOT-hold + RESET into download mode, because the running firmware has no COM port.

---

## Super Mini — wireless dongle (optional)

Plugs into the Gotek's USB and receives disk images from the interface over WiFi, so the screen
need not be tethered to the host. Only needed if you want **WIRELESS** mode.

- **Board:** generic ESP32-S3 Super Mini (**ESP32-S3FH4R2**, 4MB flash / 2MB PSRAM) https://a.aliexpress.com/_Ejwh2cS
- Flash `Gotek_SuperMini/Gotek_SuperMini.ino`

### Build settings (select "ESP32S3 Dev Module")
| Setting | Value |
|---|---|
| USB Mode | USB-OTG (TinyUSB) |
| **USB CDC On Boot** | **Disabled** |
| USB Firmware MSC On Boot | Disabled |
| PSRAM | **QSPI PSRAM** (NOT OPI — the FH4R2 has 2MB quad PSRAM; OPI = dead boot) |
| Flash Size | 4MB |
| Flash Mode | QIO 80MHz |
| Partition Scheme | Default 4MB with spiffs (1.2MB APP / 1.5MB SPIFFS) |
| CPU Frequency | 240MHz |

Two optional internal diagnostic LEDs (red on GP1, blue on GP2) show pair/transfer state; the
dongle runs fine without them fitted. The interface and dongle share SSID `GotekOMEGA` — if you
change it, change it in both places or wireless won't connect.

---

## Waveshare 2.8" — legacy / out of date

The original build for the Waveshare ESP32-S3-Touch-LCD-2.8. It is **superseded by the JC3248
build** and is kept here for existing users. It uses a different display library (LovyanGFX) and
includes an experimental web-server UI that was never fully verified.

If you have a Waveshare 2.8", the files are in [`Gotek_Waveshare28/`](Gotek_Waveshare28/).

### Build settings (select "Waveshare ESP32-S3-Touch-LCD-2.8")
| Setting | Value |
|---|---|
| USB CDC On Boot | Disabled |
| CPU Frequency | 240MHz (WiFi) |
| Flash Mode | QIO 80MHz |
| Flash Size | 16MB (128Mb) |
| Partition Scheme | 16M Flash (3MB APP / 9.9MB FATFS) |
| PSRAM | Enabled |
| USB Mode | USB-OTG (TinyUSB) |

**Libraries:** ESP32 core 3.3.7, LovyanGFX 1.2.19.

> The bundled web-server interface is included but **not fully tested** — no guarantee it works on
> the Waveshare.

---

## Features (JC3248)

- **Index + game caches** on the SD card — first boot of a large collection is slow (it reads every
  NFO/cover once); later boots read the cache and are fast. Caches self-heal if the card changes.
- **Lazy loading** — cover art / NFO read only when a game is selected, not for the whole list.
- **Multi-disk grouping** — `Game-1.adf ... Game-N.adf` collapse into one entry with a paginated
  disk selector (6 per page; handles 10+ disk sets such as Monkey Island 2).
- **Six themes** (NAVY, EMBER, MATRIX, PAPER, SYNTH, GOLD), cycled on-device, saved to `CONFIG.TXT`.
- **A-Z jump bar**, now-playing bar, cracktro splash.
- **STANDALONE** (direct USB) or **WIRELESS** (Super Mini dongle) transfer.
- Auto-generates a commented `CONFIG.TXT` on a blank card.

### SD card layout
```
/ADF/<Game Name>/<Game Name>.adf      (+ optional .nfo and .jpg alongside)
/DSK/<Game Name>/<Game Name>.dsk
/CONFIG.TXT                            (auto-created if missing)
```
Multi-disk: name files `<Game>-1.adf`, `<Game>-2.adf`, ... (digits after the final dash).

### First-time setup
Insert a blank **FAT32** SD card, flash the board, and reboot. The firmware creates the folder
structure and a sample `CONFIG.TXT`. Populate the card from your PC, then reboot again.

To reboot into programming mode: hold **BOOT** and **RESET**, release **RESET**, then release
**BOOT** — the board re-enumerates on its COM port.

---

## Tested / not tested

Being straight so nobody gets surprised:

- **JC3248 interface** (works) — fast cached boot, 1000+ games, image loading, themes, cover art,
  multi-disk pagination: exercised on real hardware.
- **Waveshare 7"** (beta) — rendering, touch, drag-scroll and the USB virtual disk verified on
  real hardware against a PC; **Gotek/FlashFloppy end-to-end test still in progress**. That's why
  it's a beta — testers wanted (checklist in
  [`firmware/Waveshare_7inch/README.md`](firmware/Waveshare_7inch/README.md)).
- **Single-dongle wireless** (works) — interface to one Super Mini, pair and load.
- **Multi-dongle switching** (beta) — code to drive several dongles from one screen (BSSID-targeted
  WiFi) is implemented but **not yet hardware-verified with two or more dongles**.
- **Waveshare 2.8" / Version 0.5.2** (legacy) — not maintained against the current feature set.

---

## Demo

[`demo/index.html`](demo/) is a self-contained browser preview of the interface — open it locally
or host it (e.g. GitHub Pages). It uses mock game data to show the look and feel; it does **not**
talk to hardware and contains none of the firmware's SD/USB/wireless logic.

---

## Credits

- **Mez** — UI, firmware
- **Dimmy** (Dimitri Hilverda) — hardware layer, display driver, original ESP-NOW/USB work
  ([fork](https://github.com/dimitrihilverda/Gotek-Touchscreen-interface))
- OMEGAWARE

Amiga Test Kit © Keir Fraser, public domain (Unlicense) — bundled as the built-in diagnostic.

Libraries: [JPEGDEC](https://github.com/bitbank2/JPEGDEC) (Larry Bank), LovyanGFX (Waveshare 2.8"
build), ESP32 Arduino core.

## Licence

MIT — see [`LICENSE`](LICENSE).
