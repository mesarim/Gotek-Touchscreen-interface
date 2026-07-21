# Building the GTi firmware (Arduino IDE)

_Firmware: **Gotek_JC3248 v4.4.0** — OMEGAWARE. Target board: **Guition JC3248W535C** (ESP32-S3-N16R8, 16 MB flash / 8 MB PSRAM, AXS15231B QSPI display + touch)._

Everything below is what a fresh machine needs to compile the sketch from source. Most of it is bundled with the ESP32 Arduino core — only **two** things are separate installs.

---

## 1. Board support (Boards Manager)

**esp32 by Espressif Systems — core 3.0.0 or newer.** (3.x is required: the firmware uses the `ESP32_NOW` C++ wrapper and the current USB/`esp_lcd` APIs that only exist in 3.x.)

In Arduino IDE: *File → Preferences → Additional Boards Manager URLs*:
```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```
Then *Boards Manager → search "esp32" → install*.

---

## 2. External libraries (install these — they are NOT in the core)

| Library | Where to get it | Used for |
|---|---|---|
| **JPEGDEC** (Larry Bank / bitbank2) | Arduino **Library Manager** → search `JPEGDEC` | Decoding cover-art / screensaver JPGs |
| **AXS15231B esp_lcd driver** (`esp_lcd_axs15231b.c` + `.h`) | Panel driver for the JC3248W535 display — e.g. the community driver in **NorthernMan54/JC3248W535EN** (`src/esp_lcd_axs15231b.*`). ⚠️ **Confirm the exact source you used** and install it as a library (or drop the `.c`/`.h` beside the sketch). | The display + capacitive touch controller |

> The AXS15231B driver is the one that trips people up: it is **not** in Library Manager. It must be present on the include path as `esp_lcd_axs15231b.h`. If you get `esp_lcd_axs15231b.h: No such file or directory`, this is the missing piece.

> **TinyUSB is NOT a separate install.** It's built into the ESP32 core — you enable it with the *USB Mode: USB-OTG (TinyUSB)* board setting (see §4), and `USB.h`/`USBMSC.h` come from the core. ⚠️ Do **not** install the "Adafruit TinyUSB Library" from Library Manager — on ESP32-S3 it conflicts with the core's own TinyUSB.

---

## 3. Bundled with the ESP32 core (no separate install — listed so you don't go hunting)

`Arduino.h`, `USB.h`, `USBMSC.h` (TinyUSB mass-storage), `FS.h`, `SD_MMC.h`, `Wire.h`,
`WiFi.h` / `WiFiClient.h` / `ESP32_NOW.h` / `esp_mac.h` (wireless + multicast),
`driver/spi_master.h`, `esp_lcd_panel_io.h` / `_vendor.h` / `_ops.h` / `_interface.h`,
`esp_random.h`, plus C/C++ standard headers (`vector`, `algorithm`, `ctype.h`, `sys/stat.h`).

---

## 4. Board menu settings that matter

Select **ESP32S3 Dev Module** (or a JC3248W535 entry if your setup provides one), then set:

| Setting | Value | Why |
|---|---|---|
| Flash Size | **16MB (128Mb)** | It's an N16R8. |
| PSRAM | **OPI PSRAM** | Octal PSRAM — the 1 MB RAM disk + framebuffers live here. Wrong setting = boot crash / no PSRAM. |
| Partition Scheme | **Huge APP (3MB No OTA/1MB SPIFFS)** | Firmware (~1.3 MB incl. embedded diagnostic ADF) needs the 3 MB app slot. |
| USB Mode | **USB-OTG (TinyUSB)** | The device presents itself as a USB mass-storage disk to the Gotek — this is mandatory. |
| USB CDC On Boot | Disabled | MSC is the USB function; leave the serial-CDC off boot. |

> ⚠️ These reflect the known-good requirements — **verify against your working IDE profile**, since the board-menu wording varies slightly by core version.

---

## 5. Sketch files (all must sit in the same folder)

```
Gotek_JC3248/
  Gotek_JC3248.ino     ← main firmware
  espnow_server.h      ← wireless / multicast
  espnow_server.cpp
  diag_adf.h           ← embedded Amiga Test Kit diagnostic (zero-RLE, ~99 KB, public domain)
```
Open `Gotek_JC3248.ino`, compile, and flash. First compile is a little slower because `diag_adf.h` embeds the ~99 KB diagnostic image.

---

## 6. Deploying to the web flasher

*Sketch → Export Compiled Binary*, then grab the **merged** `.bin` from the build folder for the ESP Web Tools manifest (writes at offset `0x0`). Same process as prior releases.

---

_Credits: Amiga Test Kit © Keir Fraser, released into the public domain (Unlicense) — bundled with thanks. GTi firmware by Mez & Dimmy / OMEGAWARE._
