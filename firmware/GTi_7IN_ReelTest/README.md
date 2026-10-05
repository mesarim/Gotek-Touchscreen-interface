# GTi_7IN_ReelTest — folder contents

```
GTi_7IN_ReelTest.ino   the bench
partitions.csv         pins 3MB APP / 9.9MB FATFS (overrides the Tools menu)
PLAN.md                what it measures and how to read it
README.md              this file
```

## Why there are no driver .c/.h files here

`GTi_AliasTest` needed `esp_lcd_axs15231b.c/.h` and `esp_lcd_touch.c/.h` copied
in, because the JC3248's panel driver is **sketch-local** — it is not an
installed library.

The 7" is different. Its panel is an RGB parallel panel driven by
`esp_lcd_panel_rgb.h`, which ships **inside the ESP32 Arduino core**, and its
GT911 touch controller is driven by raw I2C in the sketch itself. Checked
against the real folders:

```
firmware/Gotek_7inch/   Gotek_7inch.ino  diag_adf.h  espnow_server.cpp/.h   (no .csv, no driver files)
firmware/Gotek_7B/      Gotek_7B.ino     diag_adf.h  espnow_server.cpp/.h   (no .csv, no driver files)
```

`diag_adf.h` is the embedded Amiga Test Kit ADF and `espnow_server.*` is the
wireless stack — this bench has neither a USB RAM disk nor a radio, so neither
is included.

Every `#include` in the bench also appears in `Gotek_7inch.ino`, so if that
sketch compiles on this machine, this one has everything it needs:

```
Arduino.h  FS.h  SD_MMC.h  Wire.h  math.h  sys/stat.h  vector  algorithm  ctype.h
esp_heap_caps.h  esp_lcd_panel_ops.h  esp_lcd_panel_rgb.h     (core)
JPEGDEC.h  PNGdec.h                                           (installed libraries)
```

## Before you compile

Set the board options (a new sketch folder gets Arduino's **defaults**, not the
ones you set for `Gotek_7inch` — this is what cost us the blank screen on the
alias test):

```
Board            ESP32S3 Dev Module
PSRAM            OPI PSRAM            <-- default is Disabled; it will not boot
Flash Size       16MB (128Mb)         <-- required for partitions.csv to fit
Flash Mode       QIO 120MHz
USB CDC On Boot  Enabled              (no MSC here, so Serial stays available)
USB Mode         Hardware CDC and JTAG
CPU Frequency    240MHz
Partition        (ignored — partitions.csv in this folder wins)
```

**Close the sketch tab and reopen it before every compile.** Arduino IDE 2.x
auto-saves its editor buffer over the file on disk when you hit compile.

## What it writes to the card

Only two things:

* `/ADF/.thumbs/*.tnl` and `/DSK/.thumbs/*.tnl` — 200x200 RGB565 tiles in the
  byte-identical format `Gotek_7inch` already uses, same djb2-xor filename hash.
  Tiles built here are read straight back by the firmware and vice versa.
* `/.reelbench` — the bench's own library index, and **only** if the card has no
  `.filelist`/`.gamecache` to read. The firmware's caches are never overwritten.

Nothing else on the card is touched.
