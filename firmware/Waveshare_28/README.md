# GTi — Waveshare 2.8" port (legacy build)

Compact standalone controller on the **Waveshare ESP32-S3-Touch-LCD-2.8**
(ST7789 SPI display, CST328 touch). Full GTi feature set of its era: cover-art
browser, USB disk to the Gotek, wireless FLING to a dongle.

> **Status: legacy.** This is the last GTi build using LovyanGFX (the JC3248 and
> 7" ports moved to custom rendering engines). It works, but gets fixes only.

## Build (Arduino IDE)

**Board support:** esp32 by Espressif Systems, core 3.x (Boards Manager).

**Board menu — `waveshare_esp32_s3_touch_lcd_28` (or ESP32S3 Dev Module):**

| Setting | Value |
|---|---|
| Flash | 16MB, QIO 120MHz |
| PSRAM | **OPI PSRAM** |
| Partition Scheme | 16M Flash (3MB APP / 9.9MB FATFS) |
| USB Mode | **USB-OTG (TinyUSB)** |
| USB CDC On Boot | **Disabled** — required for Gotek/FlashFloppy compatibility |
| CPU Frequency | 240MHz |

**Libraries (Library Manager):**
- **LovyanGFX** — ⚠️ this library moves fast and old builds are version-sensitive.
  If you hit compile errors *inside* LovyanGFX headers, try a different release
  and please open an issue telling us which version worked for you.
- **JPEGDEC** (Larry Bank) if prompted (cover art decode).

## Flashing / reflashing

CDC is off, so the COM port **disappears once the firmware boots** (the device
presents a USB disk instead). To reflash: hold **BOOT**, tap **RESET**, release —
the download COM port appears. Every time. Normal, not a brick.

## Pins (for reference / hackers)

SPI display (ST7789): SCK=40 MOSI=45 DC=41 CS=42 RST=39 BL=5 ·
SD_MMC 1-bit: CLK=14 CMD=17 D0=16 · Touch I2C (CST328): SDA=1 SCL=3 INT=4 RST=2

_GTi firmware by Mez & Dimmy & Claude / OMEGAWARE._
