# Gotek Touchscreen Interface

**The GTi** — *"The Floppy Flinger Thinger."*

A touchscreen front-end for [Gotek](https://en.wikipedia.org/wiki/Gotek_Floppy_Emulator)-style
floppy emulators. It browses `.ADF` (Amiga) and `.DSK` (ZX / CPC) disk images from an SD card,
shows cover art and game info, and presents the selected image to the host machine as a USB
floppy — wrapped in a demoscene-flavoured UI (cracktro splash, copper bars, scroller, six themes).

Built by **Mez** and **Dimmy** (Dimitri Hilverda) — **OMEGAWARE**.

---

**Current release: A.5.0.0** — the save-game era, proven on a real Amiga and now the recommended stable build.

## What it is (and what it isn't)

The GTi doesn't replace FlashFloppy — it makes it nicer to live with. FlashFloppy and HxC
already do floppy emulation brilliantly; what they leave you with is a 0.9" OLED, a rotary
encoder, and a thousand files named things like `Turrican2[cr FLT].adf`. The GTi is the
missing front-end: browse by cover art, read the blurb, tap INSERT.

- **Keep your existing setup.** Nothing on the Gotek is removed or reflashed — the GTi simply
  presents itself as the USB stick. Your OLED, encoder and current workflow all still work
  the moment you unplug it.
- **Wireless = invisible installs.** With a dongle tucked inside the machine there's no USB
  stick protruding from your Amiga at all; the screen feeds it disks over WiFi from across
  the room.
- **Easier on the eyes.** Big type on a real screen (three font sizes on the JC build) and
  six themes including a high-contrast paper-white mode — kinder than a tiny OLED to eyes
  that, like the hardware, have been going since 1987.
- **One dumb endpoint, many faces.** The dongle does exactly one thing: receive an image,
  become a floppy. The 3.5" screen, the 7" beta and future clients all feed that same simple
  endpoint — the system grows without ever touching the Gotek again.

Built for FlashFloppy; anything that boots images from a USB stick should be equally happy,
though FlashFloppy is what gets bench-tested here.

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
- **A-Z jump bar**, now-playing bar.
- **Six boot cracktros** — COPPER, STARFIELD, RAINBOW, PLASMA, BOING BALL, SYNTHWAVE. Pick one
  with `CRACKTRO=1..6` in `CONFIG.TXT`, or `CRACKTRO=0` for a random one each boot.
- **Built-in Amiga Test Kit** — a green **LOAD DIAG** button (INFO panel) mounts Keir Fraser's
  public-domain diagnostic disk straight from firmware. Works with **no SD card at all** — a
  bare GTi can still health-check your Amiga out of the box.
- **Favourites** — tap a game's letter circle to star it. Stars and play counts live in a small
  file on the SD card, so they survive reboots, rescans and even reflashes.
- **STANDALONE** (direct USB) or **WIRELESS** (Super Mini dongle) transfer.
- Auto-generates a commented `CONFIG.TXT` on a blank card, plus a **SAMPLE folder** in `/ADF`
  showing exactly how to lay out a game (disk + cover + `.nfo`) — the browser ignores it; it
  exists purely to be copied.

### SD card layout
```
/ADF/<Game Name>/<Game Name>.adf      (+ optional .nfo and .jpg alongside)
/DSK/<Game Name>/<Game Name>.dsk
/CONFIG.TXT                            (auto-created if missing)
```
Multi-disk: name files `<Game>-1.adf`, `<Game>-2.adf`, ... (digits after the final dash).

### The `.nfo` file (optional game title + info)

Plain text, same folder and basename as the game (`GameName.nfo`). Two formats, both supported:

**Simple** — no labels needed:
```
Turrican II
1991 - Rainbow Arts - Run and gun
Sprawling run-and-gun with huge weapons and legendary music.
```
First non-empty line = the display title (shown **instead of the file name**); everything after it = the info blurb shown on the cover panel.

**Labelled** — labels are case-insensitive (`Title:`, `TITLE:`, `title:` all work):
```
Title: Turrican II
Blurb: 1991 - Rainbow Arts - Run and gun
Sprawling run-and-gun with huge weapons and legendary music.
```
`Title:` overrides the display name regardless of the file/folder name. `Blurb:` (or `Description:`) starts the info text; lines that follow are appended until a line containing a `:` appears. Keep blurbs short — a line or two reads best on the cover panel.

### First-time setup
Insert a blank **FAT32** SD card, flash the board, and reboot. The firmware creates the folder
structure and a sample `CONFIG.TXT`. Populate the card from your PC, then reboot again.

To reboot into programming mode: hold **BOOT** and **RESET**, release **RESET**, then release
**BOOT** — the board re-enumerates on its COM port.

---

## Generic mode (GEN) — use the GTi with (almost) any Gotek machine

The GTi grew up on the Amiga, but it never actually *talks* to the Amiga — it hands disk **images**
to the Gotek, and **FlashFloppy** does the floppy emulation to whatever machine it's fitted in. So
the GTi can front-end a Gotek in an Atari ST, Amstrad, PC, or a classic **sampler / synth** (Akai,
Roland, Yamaha, Ensoniq, Korg…) — anything on FlashFloppy's host list.

Tap **GEN** on the mode line (beside ADF / DSK) to switch the browser to a **`/GENERIC/`** folder.
It uses the same folder + sidecar layout as the Amiga library, but mounts **any** image and keeps
its real name and extension so FlashFloppy detects the format:

```
/GENERIC/<Name>/<Name>.<ext>      the disk image — ANY format (.hfe, .img, .st, .adf, .dsk …)
/GENERIC/<Name>/ff.cfg            OPTIONAL FlashFloppy config (see below)
/GENERIC/<Name>/<Name>.jpg|.png   OPTIONAL cover art
/GENERIC/<Name>/<Name>.nfo        OPTIONAL title / blurb
/GENERIC/<Name>/<Name>.rtfm       OPTIONAL manual (opens in the RTFM reader)
```

The image is simply "the one file that isn't a known sidecar." Cover art, the `.nfo` info panel and
the RTFM reader all work in GEN exactly as they do for Amiga games.

**Do I need an `ff.cfg`?** Only for *raw, ambiguous* images. FlashFloppy auto-detects self-describing
formats — **`.hfe`** (universal flux; works for any machine), `.adf`, a standard `.st` — so those
need no config. A raw **`.img` / `.ima`** carries no geometry inside it, so drop an `ff.cfg` (with the
right `host =` line for your machine) beside it and it rides onto the mounted disk. If a folder has an
`ff.cfg`, GEN includes it; if not, FlashFloppy uses its defaults. Easiest universal route: convert
your disks to **`.hfe`** (e.g. with the HxC tools) — then no config is needed at all.

**Notes:** GEN is for **standalone / cable** (and the XIAO wireless dongle) — like HD, the 2 MB Super
Mini dongle can't hold larger generic images. Amiga-only extras (save-writeback, user-disks) are off
in GEN. Currently in **beta** — feedback very welcome via GitHub Issues/Discussions.

## Firmware updates over the SD card (JC3248)

From **5.3.0-beta**, the JC3248 can flash a new firmware build straight from its SD card — no
cable, no opening the case. Pair it with **SD Access** and the whole loop is: plug into a PC, drop
the update file on, unplug, tap a button.

How to update:

1. Put the app image on the SD card root as **`GTi_update.bin`** (the `Gotek_JC3248.ino.bin`
   from *Sketch → Export Compiled Binary* — the ~1.4 MB app file, **not** the 16 MB `.merged.bin`).
   Copy it over with **SD Access** if you don't want to pop the card.
2. On the GTi: **INFO → FW UPDATE**. A confirm screen shows the file size and verifies it's a real
   GTi-JC image (a Super Mini / XIAO bin is refused). Tap **FLASH**.
3. Watch the progress bar; it reboots into the new build. Done.

**Safety:** the image is fully hash-verified before the boot pointer is switched, so a corrupt or
half-copied file — or a power cut mid-flash — can't take over; your current firmware simply boots
again. Updates land in a spare OTA slot, leaving the previous build intact.

**One-time transition:** self-update needs a dual-slot (A/B OTA) partition layout, which older units
don't have. Flashing **5.3.0-beta** through the web flasher once moves your unit onto that layout —
that's the **last USB session it ever needs**; every update after is a file on the card. Your games
are untouched (they live on the SD; the disk image lives in PSRAM).

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
- **Retronaut** ([Chris Thomas](https://www.youtube.com/@RetronautTech)) — feature suggestions: the
  in-game **.rtfm** manual reader and **user-disks** (create-your-own save disks); and the first
  video feature of the GTi
- **Claude** (Anthropic) — AI development collaborator: firmware pair-programming (save era, RTFM
  reader, user-disks, SD Access), the web flasher &amp; demo, and documentation
- OMEGAWARE

Amiga Test Kit © Keir Fraser, public domain (Unlicense) — bundled as the built-in diagnostic.

Libraries: [JPEGDEC](https://github.com/bitbank2/JPEGDEC) (Larry Bank), LovyanGFX (Waveshare 2.8"
build), ESP32 Arduino core.

## Support

The GTi firmware is free and open-source — flash it, fork it, keep it. If it's useful to you, you can drop Mez a tip at **[ko-fi.com/mesarim](https://ko-fi.com/mesarim)** ☕ (no pressure; the code stays free). The full roll-call lives on the [contributors page](docs/contributors.html).

## Licence

MIT — see [`LICENSE`](LICENSE).

