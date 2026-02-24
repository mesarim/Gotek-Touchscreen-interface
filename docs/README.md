
# ESP32‑S3 ADF/DSK Browser — Waveshare ESP32‑S3‑Touch‑LCD‑2.8
**Version:** v0.5.2  
**Board:** Waveshare ESP32‑S3‑Touch‑LCD‑2.8 (ST7789 + CST328)

This fork adds an on‑screen **ADF/DSK** toggle (Selection screen only) so you can browse either **`.ADF`** (Amiga) or **`.DSK`** (ZX/CPC) and export a single file to a **1 MiB FAT12 RAM disk** over **USB MSC**. Safety: copy happens while **detached**; attach only after a successful load.

## Quick start
1. Open `ESP32_S3_ADF_DSK_Browser_v0_5_2.ino` in Arduino IDE.
2. Select an ESP32‑S3 board (TinyUSB MSC enabled). PSRAM recommended.
3. Flash to the Waveshare ESP32‑S3‑Touch‑LCD‑2.8.
4. SD card root should contain your images:
   - ADF mode → `*.ADF`
   - DSK mode → `*.DSK`
   - Optional per‑title: `<name>.nfo` and `<name>.jpg`
   - Optional banners:
     - ADF: `/banner.jpg` (or `.jpeg`)
     - DSK: `/zx.jpg` → ZX, else `/cpc.jpg` → CPC, else `/dsk.jpg` → generic
5. On boot you land in **Selection**. Tap **ADF/DSK** (middle‑right) to switch mode. Tap **OPEN**; on **Details** use **LOAD/UNLOAD**.

## Notes
- The exported filename is **`DISK.ADF`** or **`DISK.DSK`** depending on mode.
- The FAT12 image is rebuilt to the **actual file size** (up to ~1.043 MiB), so the progress bar reaches 100% even for small DSKs.
- The **ADF/DSK toggle is hidden in Details** to avoid confusion.

## Pinout assumptions (from v0.4.4)
- ST7789: SCLK=40, MOSI=45, DC=41, CS=42, RST=39.  
- CST328 touch: SDA=1, SCL=3, INT=4, RST=2, I²C addr 0x1A.  
- SD_MMC 1‑bit: `setPins(14,17,16,-1,-1,21)`.

## Credits
Original ADF Browser by Michael. This fork extends it with DSK support and UI/UX refinements.
