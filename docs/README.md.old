# Gotek Touchscreen Interface

**A touchscreen-driven disk image browser for retro computing.**  
Load Amiga ADF, ZX Spectrum and Amstrad CPC DSK files from an SD card onto a FAT12 USB RAM disk — presented directly to a Gotek/FlashFloppy as a standard USB stick. No PC needed. No USB swapping. Just tap and play.

> **Coded by Mez and Dimmy of OmegaWare · 2026**  
> *Keep the scene alive.*

---

## What is this?

A Gotek floppy emulator normally needs a USB stick full of disk images. You pull it out, plug it into a PC, copy files, put it back. Every time.

This project replaces that USB stick with an ESP32-S3 that **pretends to be a USB stick** — but one that you control wirelessly from a touchscreen. Pick a game, tap INSERT, and your retro machine sees a freshly loaded floppy in under a second.

The key trick: the ESP32-S3 presents a **FAT12 RAM disk over TinyUSB** directly to the Gotek's USB port. No Linux. No Raspberry Pi. No boot delay. Instant on.

---

## Hardware Variants

| Variant | Board | Role | Display | Status |
|---|---|---|---|---|
| **Waveshare 7"** | Waveshare ESP32-S3-Touch-LCD-7 | Standalone touchscreen controller | 800×480 RGB parallel, GT911 | ✅ Supported |
| **XIAO Dongle** | Seeed XIAO ESP32-S3 | Headless USB dongle (phone/web control) | Optional SSD1306 OLED | ✅ Supported |
| **CYD** | ESP32-2432S028R ("Cheap Yellow Display") | Wireless touchscreen remote | 320×240 ILI9341, XPT2046 | ✅ Supported |
| **Waveshare 2.8"** | Waveshare ESP32-S3-Touch-LCD-2.8 | Compact standalone controller | 320×240 ST7789, CST328 | ✅ Supported |
| **Waveshare 2.8B** | Waveshare ESP32-S3-Touch-LCD-2.8B | Compact standalone controller | 320×240 ST7789 variant | 🔜 Coming soon |

> The CYD is widely available for under £10, making this one of the cheapest possible setups.

---

## Features

- **FAT12 USB RAM disk** — ESP32-S3 presents a TinyUSB mass storage device directly to the Gotek. FlashFloppy sees a standard USB stick and needs no modification whatsoever.
- **Touchscreen game browser** — scrollable list with cover art (JPEG), game names, and multi-disk grouping
- **Detail view** — full cover art, `.nfo` game info, INSERT/EJECT buttons, disk selector for multi-disk titles
- **Six built-in themes** — NAVY, EMBER, MATRIX, PAPER, SYNTHWAVE, GOLD — cycle with a tap
- **ADF and DSK modes** — switch between Amiga (ADF/IMG/ADZ) and ZX Spectrum/Amstrad CPC (DSK) from the UI
- **WiFi web server** — built-in access point with browser-based game manager (upload, delete, configure)
- **ESP-NOW + WiFi TCP wireless mode** — touchscreen variants can wirelessly control the XIAO dongle over ESP-NOW for pairing and TCP for fast disk transfers
- **Auto-resume** — last loaded disk and theme saved to `CONFIG.TXT` and restored on boot
- **Cracktro boot screen** — animated starfield, copper bars, and scrolling text on the 7" variant (touch to skip)

---

## How It Works

### Standalone mode (Waveshare 7" / CYD with SD card)

```
SD Card ──► ESP32-S3 ──► TinyUSB FAT12 RAM disk ──► Gotek USB port ──► Retro machine
                │
            Touchscreen UI
            WiFi web server
```

The ESP32 reads the disk image from SD, builds a FAT12 filesystem in PSRAM, and presents it to the Gotek as a USB mass storage device. When you swap disks, it detaches the USB, rebuilds the RAM disk with the new image, and reattaches — FlashFloppy sees a new disk inserted.

### Wireless mode (Touchscreen → XIAO Dongle)

```
SD Card ──► Waveshare/CYD ──[ESP-NOW pairing]──► XIAO ──► Gotek USB port
               Touchscreen UI          └─[WiFi TCP data]─┘
```

The XIAO plugs permanently into the Gotek. The touchscreen pairs with it over ESP-NOW, then transfers disk images over WiFi TCP (fast, reliable). The XIAO presents the RAM disk to the Gotek. The touchscreen shows status and handles all UI.

---

## SD Card Layout

```
SD Card Root/
├── CONFIG.TXT              ← auto-generated on first boot
├── ADF/                    ← Amiga disk images
│   ├── Speedball 2/
│   │   ├── Speedball 2.adf
│   │   ├── Speedball 2.jpg     ← cover art (any JPEG)
│   │   └── Speedball 2.nfo     ← plain text: line 1=title, rest=description
│   ├── Cannon Fodder/
│   │   ├── Cannon Fodder-1.adf ← multi-disk: suffix -1, -2, -3 ...
│   │   ├── Cannon Fodder-2.adf
│   │   ├── Cannon Fodder-3.adf
│   │   ├── Cannon Fodder.jpg
│   │   └── Cannon Fodder.nfo
│   └── Worms.adf               ← flat layout also works
└── DSK/                    ← ZX Spectrum / Amstrad CPC disk images
    └── Dizzy/
        ├── Dizzy.dsk
        └── Dizzy.jpg
```

**File naming rules:**
- Single-disk games: `GameName.adf` / `GameName.dsk`
- Multi-disk games: `GameName-1.adf`, `GameName-2.adf`, etc.
- Cover art: `GameName.jpg` or `GameName.jpeg` (any resolution, displayed scaled)
- Game info: `GameName.nfo` (plain text — line 1 is the title, subsequent lines are description)
- Supported image formats: `.adf`, `.img`, `.adz` (ADF mode) · `.dsk` (DSK mode)

---

## CONFIG.TXT

Auto-generated on first boot. Edit manually or via the web UI.

```ini
MODE=STANDALONE        # STANDALONE or WIRELESS
THEME=0                # 0=NAVY 1=EMBER 2=MATRIX 3=PAPER 4=SYNTHWAVE 5=GOLD
LASTFILE=Worms.adf     # restored on boot
LOOP=0                 # 1=loop cracktro boot screen forever
XIAO_MAC=AA:BB:CC:DD:EE:FF   # auto-written when XIAO pairs
XIAO_IP=192.168.4.1          # auto-written when XIAO pairs
```

---

## WiFi Web Server

When WiFi is enabled, the ESP32 creates an access point:

- **SSID:** `GotekSetup` (configurable)
- **Password:** `retrogaming`
- **URL:** `http://192.168.4.1`

The web UI lets you upload ADF/DSK files, cover art and NFO files, manage your library, and edit `CONFIG.TXT` — all from a phone or laptop without touching the SD card.

---

## Building & Flashing

> **⚡ A one-click web flasher is coming.** For now, use Arduino IDE.

### Waveshare 7" (Gotek_7inch.ino)

**Libraries:**
- `LovyanGFX` by lovyan03
- `ESP32 by Espressif` board package

**Arduino IDE board settings:**

| Setting | Value |
|---|---|
| Board | ESP32S3 Dev Module |
| USB Mode | USB-OTG (TinyUSB) |
| USB CDC On Boot | **Disabled** ← critical |
| PSRAM | OPI PSRAM |
| Flash Size | 16MB (128Mb) |
| Partition Scheme | 3MB APP / 9.9MB FATFS |
| CPU Frequency | 240MHz |
| Flash Mode | QIO 120MHz |

### XIAO Dongle (Gotek_XIAO.ino)

**Libraries:**
- `Adafruit SSD1306` (optional — for status OLED)
- `Adafruit GFX`

**Arduino IDE board settings:**

| Setting | Value |
|---|---|
| Board | XIAO_ESP32S3 |
| USB Mode | USB-OTG (TinyUSB) |
| USB CDC On Boot | **Disabled** ← critical |
| PSRAM | OPI PSRAM |
| Flash Size | 8MB |
| Partition Scheme | Default with SPIFFS |

> **Important:** Plug in the external WiFi antenna before use.

### CYD — Cheap Yellow Display (Gotek_CYD.ino)

**Libraries:**
- `TFT_eSPI` by Bodmer
- `XPT2046_Touchscreen` by Paul Stoffregen
- `TJpg_Decoder` by Bodmer

**You must copy the provided `User_Setup.h`** into your TFT_eSPI library folder before compiling.

**Arduino IDE board settings:**

| Setting | Value |
|---|---|
| Board | ESP32 Dev Module |
| Partition Scheme | Huge APP (3MB No OTA) |
| Flash Frequency | 40MHz |
| CPU Frequency | 240MHz |
| PSRAM | Disabled |

---

## Pairing the XIAO Dongle

1. Flash and connect the XIAO to the Gotek USB port
2. Flash the touchscreen variant (Waveshare 7" or CYD)
3. On the touchscreen, go to the **Info screen** and tap **PAIR**
4. The devices pair automatically over ESP-NOW — MAC address and IP are saved to `CONFIG.TXT`
5. Switch to **WIRELESS** mode — the touchscreen now sends disk images to the XIAO over WiFi TCP

Pairing is persistent. Once paired, power cycling both devices will restore the connection automatically.

---

## Wiring

### Waveshare 7" — no external wiring needed
The display, touch, SD card, and USB OTG are all onboard.

### XIAO Dongle
| Connection | Detail |
|---|---|
| USB port → Gotek | Standard USB-A cable |
| Optional OLED | SSD1306 128×32 on I2C (SDA/SCL) |
| WiFi antenna | External antenna — **must be connected** |

### CYD
All onboard. SD card uses HSPI (SCK=18, MISO=19, MOSI=23, CS=5). No external wiring.

---

## Roadmap

- [ ] Waveshare 2.8" and 2.8B variants
- [ ] ESP Web Tools one-click browser flasher
- [ ] PC companion app (Windows/macOS)
- [ ] iPhone / Android app
- [ ] OTA firmware update via web UI
- [ ] More disk formats (HFE, ST, D64)

---

## Known Issues:
Massive load times when indexing around 1000 disks.
Currently working on resolving this.

---

## Credits & Acknowledgements

- **mesarim** — original concept, FAT12 USB RAM disk core, 7" Waveshare port, XIAO dongle, CYD variant
- **dimitrihilverda** — fork, JC3248 support, WiFi web server, theme engine
- **Keir Fraser** — [FlashFloppy](https://github.com/keirf/flashfloppy) firmware
- **Jean-François Del Nero** — [HxC Floppy Emulator](https://hxc2001.com) — original Gotek community pioneer
- **Jeff (HxC author)** — first to demonstrate Pi Zero W as USB mass storage for Gotek
- **lovyan03** — [LovyanGFX](https://github.com/lovyan03/LovyanGFX)

---

## Licence

MIT — see [LICENSE](LICENSE) for details.
