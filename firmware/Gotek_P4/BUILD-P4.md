# Gotek_P4 — Arduino build settings (Toby)

Full JC **5.8.6** interface ported to the **Guition JC4880P443C (ESP32-P4)**.
On-screen/firmware identity is now **`5.8.6-P4`** (marker `OMEGAWARE.GTi.P4.fw`).
Compiles clean under **arduino-esp32 3.3.11** (`esp32:esp32:esp32p4`): 29% flash, 46% RAM.
**Not yet run on hardware** — this build is the first bring-up.

## Board Manager
- Install/enable **arduino-esp32 core 3.3.11 or newer** (must list *ESP32P4 Dev Module*).

## Tools ▸ menu selections
| Setting | Value |
|---|---|
| Board | **ESP32P4 Dev Module** |
| Flash Size | **16MB** — use 16MB even on a 32MB-flash unit: a 16MB layout boots on a 32MB chip, but a 32MB layout does **not** boot on a 16MB chip. (The module's 32MB is PSRAM, not flash. Confirm the flash chip with `esptool --chip esp32p4 flash_id` if unsure.) |
| PSRAM | **Enabled** — 32 MB HP PSRAM (framebuffers + ramdisk live here) |
| CPU Frequency | **360 MHz** — leave default. ⛔ do **not** force 400 MHz: confirmed unstable on this v1.3 silicon. |
| USB Mode | **USB-OTG (TinyUSB)** — required for the USB-MSC floppy |
| USB CDC On Boot | **Disabled** (flash via BOOT-hold, same as the S3 boards) |
| Partition Scheme | dual-app OTA; see note below (a sketch-local `partitions.csv` overrides the menu) |
| Upload Speed | 460800 |

## Partition sizing — the short version
This firmware uses **no internal flash filesystem**: the game library, cover art and
CONFIG.TXT are all on the **SD card**, and the USB-MSC ramdisk + decode buffers are in
**PSRAM** (`ps_malloc`). Internal flash only needs **NVS + OTA-data + two app slots** (the
second slot is what makes the on-device SD-update / FW-UPDATE work). So partition size is
driven by the **app image + OTA**, *not* by how big the flash chip is — any surplus flash is
simply unused. 3 MB app slots give large headroom (app is ~0.9 MB today).

Drop one of these as `partitions.csv` in this folder (arduino-esp32 uses a sketch-local CSV
when present). Use the **16MB** table below (safe on both 16MB and 32MB flash). Only use the 32MB table if `flash_id` confirms 32MB **and** you want the extra unused FAT reserve:

**16 MB flash** (validated 26-Aug layout):
```
# Name,   Type, SubType, Offset,   Size,    Flags
nvs,      data, nvs,     0x9000,   0x5000,
otadata,  data, ota,     0xe000,   0x2000,
app0,     app,  ota_0,   0x10000,  0x300000,
app1,     app,  ota_1,   0x310000, 0x300000,
ffat,     data, fat,     0x610000, 0x9E0000,
coredump, data, coredump,0xFF0000, 0x10000,
```

**32 MB flash** (same lean apps; the extra space is an unused FAT reserve):
```
# Name,   Type, SubType, Offset,    Size,     Flags
nvs,      data, nvs,     0x9000,    0x5000,
otadata,  data, ota,     0xe000,    0x2000,
app0,     app,  ota_0,   0x10000,   0x300000,
app1,     app,  ota_1,   0x310000,  0x300000,
ffat,     data, fat,     0x610000,  0x19E0000,
coredump, data, coredump,0x1FF0000, 0x10000,
```
(Either boots fine on a bigger chip; only the app+OTA region is actually used.)

## Flash (esptool, any OS)
```
esptool --chip esp32p4 -p <PORT> -b 460800 write_flash 0x0 <merged>.bin
```
Won't connect? **Hold BOOT, tap RESET, release BOOT**; close the serial monitor first;
use the USB port that flashes (USB-Serial-JTAG), supply >=600 mA.

## First-run expectations (bring-up)
- **UI should render** in portrait 480×800 (reflowed by `relayout()`). A few 320×480 layout
  constants may need tidying once we see it — not a rewrite.
- **Touch pins (RST22/INT21, addr 0x5D) are community-sourced.** UI draws but touch dead/offset
  = wrong pins/addr; send the real ones.
- **USB-MSC** presents the PSRAM ramdisk (Arduino P4 MSC is documented flaky) — confirm the
  Gotek/PC mounts it; try both USB-C ports (HS OTG = GPIO49/50).
- **SD** (`SD_MMC`) TF pins unconfirmed on this board — browse may need pins set.
- Renders then reboots = suspect 400 MHz-on-v1.3; tell me and I'll pin 360 by build flag.
- **Wireless is stubbed** (ESP-NOW doesn't link on P4) — flinging is a no-op until the
  WiFi/TCP Webby transport is folded in (Stage 3).

## Report back for the next iteration
1) `flash_id` size, 2) touch working y/n + real pins, 3) which USB-C enumerates, 4) a photo of the UI.

## Board notes — confirmed on hardware (Sep 4)
- **USB-C ports:** the one **furthest from the BOOT/RESET buttons** is the **flashing** port
  (USB-Serial-JTAG). The near port is USB-OTG (the disk / USB-MSC side).
- **Reflashing a running GTi build** needs manual download mode: **hold BOOT, tap RESET, release
  BOOT**, then pick the serial port that appears (CDC is off, so a running build shows as a USB
  drive, not a port, and won't auto-reset).
- **Touch (GT911):** I2C **SDA=7 / SCL=8**; **RST and INT are NOT connected** to the MCU on this
  board (vendor BSP `esp32_p4_function_ev_board`: `BSP_LCD_TOUCH_RST/INT = GPIO_NUM_NC`). Do **not**
  drive GPIO21/22 — that was a wrong community guess. The chip powers up on the I2C bus; address is
  0x5D (0x14 fallback). Fixed in 5.8.6-P4.2.
- Backlight = GPIO23, panel = ST7701 480x800 DSI (matches the benchmark config).
