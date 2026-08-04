# GTi — XIAO wireless dongle (Seeed XIAO ESP32-S3)

The original GTi wireless dongle: plugs into the Gotek's USB port, receives
disks from a GTi screen (or CYD remote) over WiFi, and presents them to the
Gotek as a USB drive. Drives a small SSD1306 OLED for status.

> **Note:** the **Super Mini dongle** (`firmware/SuperMini-S3/`) is the preferred
> dongle these days — cheaper, onboard antenna, no OLED needed. This XIAO build
> remains fully supported and wire-compatible.

## Build (Arduino IDE)

**Board support:** esp32 by Espressif Systems, core 3.x (Boards Manager).

**Board menu — XIAO_ESP32S3:**

| Setting | Value |
|---|---|
| USB Mode | **USB-OTG (TinyUSB)** |
| USB CDC On Boot | **Disabled** |
| PSRAM | **OPI PSRAM** |
| Flash | 8MB, Partition: Default with spiffs |

**Libraries (Library Manager):**
- **Adafruit GFX Library**
- **Adafruit SSD1306** (the little status OLED)

## ⚠️ ANTENNA

**Plug the external antenna in.** The XIAO ESP32-S3 has no useful onboard
antenna — without the u.FL antenna connected, pairing and transfers will be
flaky to impossible. (This is the #1 XIAO support question. It's the antenna.)

## Flashing / reflashing

CDC is off, so the COM port **disappears once the firmware boots** (the dongle
is a USB disk). To reflash: hold **BOOT**, tap **RESET**, release — the download
COM port appears. Normal, not a brick.

## Using it

Power it (in the Gotek's USB port or any USB power), pair once from the GTi's
INFO screen (SCAN DONGLES → USE), then FLING disks. The OLED shows pairing
state, transfer progress, and the loaded disk.

_GTi firmware by Mez & Dimmy & Claude / OMEGAWARE._
