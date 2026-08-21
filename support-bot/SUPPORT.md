# OMEGAWARE GTi — Support Knowledge Base

> This file IS the bot. Everything it knows comes from here, and it is instructed to refuse
> to answer anything that isn't. Edit, `wrangler deploy`, done.
>
> **Anything marked `TODO` is a gap.** The bot will say "not documented, ask on Discord" for
> anything missing — the correct failure, but the fewer the better.
>
> Keep it public-safe. No pricing, no margins, no customer names.

---

## What the GTi is

The GTi is a touchscreen interface for a Gotek floppy emulator in an Amiga or other retro
machine. It shows your disk library with cover art and game info, and hands the selected disk
image to the Gotek. It replaces hunting through numbered folders on a three-digit LED display.

Two ways it connects to the Gotek:

- **Standalone** — the GTi plugs into the Gotek's USB port by cable and drives it directly.
- **Wireless** — a small dongle plugs into the Gotek instead, and the GTi sends disk images to
  it over the air.

The GTi works in both roles; it's a setting, not a different product.

### Boards

The standard kit is the **Guition JC3248**, a 3.5" 480×320 screen. Firmware also exists for
the **Guition JC4827W543** (4.3"), the **Waveshare 7"** (800×480), the **Waveshare 7B**
(1024×600), the **Waveshare 2.8"**, and the **CYD** as a wireless remote. Anything other than
the JC3248 is a build-it-yourself board flashed from the web flasher, not something that ships
as a kit.

Wireless dongles: the **Super Mini** (the recommended one — the wider ESP32-S3 board with two
side buttons) and the **XIAO** (the smaller Seeed board with the metal-shielded module).

### Key links

- **Web flasher, firmware downloads and current version numbers:**
  https://mesarim.github.io/Gotek-Touchscreen-interface/
- **Interactive guide** — the on-screen interface, annotated, in your browser:
  https://mesarim.github.io/Gotek-Touchscreen-interface/guide.html
- **Discord:** https://discord.gg/7gY4PKUnnf
- **GitHub issues:** https://github.com/mesarim/Gotek-Touchscreen-interface/issues
- **Pairing guide:**
  https://github.com/mesarim/Gotek-Touchscreen-interface/blob/main/docs/PAIRING.md
- **Flashing on a Mac:**
  https://mesarim.github.io/Gotek-Touchscreen-interface/FLASHING-MAC.md

**Version numbers in this document may be out of date.** The flasher page above is always
right about what the current build is. Point people there rather than quoting a version.

---

## What ships today

Unless the owner says otherwise, assume they have:

- **GTi (JC3248):** stable **5.5.5**
- **Super Mini dongle:** **3.2.0**

Betas exist and testers run them, but the shipped kit is the above. Answer for stable first,
and only bring up beta behaviour if the person says they're on a beta.

## Menu names change between builds

The menus have been reworked more than once, so the same control has different labels on
different firmware. If someone can't find a button, this is usually why — check their version
before assuming the feature is missing.

| Control | 5.5.5 (stable) | 5.8.2 (beta) |
|---|---|---|
| Settings screen | **INFO** | **CONFIG** |
| Find/pair a dongle | **SCAN DONGLES** | **RE-PAIR** |

TODO — confirm the 5.8.2 column and fill in anything else that was renamed. Add a row every
time a control changes name, and keep the old names in the table forever — old builds stay in
the field and old videos stay on YouTube.

**When giving steps, name both labels the first time** ("tap INFO, labelled CONFIG on newer
builds") so it's findable either way. This document uses the 5.5.5 names.

When 5.8.2 becomes stable, the flasher page, the interactive guide and this file all need the
new names — the labels live in three places and drift apart easily.

---

## What's in the box

- The GTi — a 3.5" JC3248 touchscreen, pre-flashed with current firmware.
- A **512MB SD card**, already prepared, already fitted.
- A **Super Mini** wireless dongle in its own 3D-printed case, also pre-flashed.
- A 3D-printed case, either **portrait** or **landscape**.
- Four screws, if you'd rather mount the unit than case it.
- A USB-A to USB-C data cable.

Everything arrives flashed and ready. There is no firmware to install before you start.

### Fitting the case

- **Portrait** is a snap fit. Nothing to remove.
- **Landscape** needs the rear plastic plate taken off the screen module first. If you'd
  rather keep the plate on, the printed case can be modified to clear it instead.

---

## First-time setup — adding your games

The GTi ships working. The only thing to do is put your own disk images on the card, and you
do that over USB from a PC. The card never has to come out.

1. **Connect the GTi to your PC** with the supplied cable. It must be a **data** cable — a
   charge-only cable will do nothing.
2. On the first screen, tap **INFO** (labelled **CONFIG** on newer builds).
3. Tap **SD ACCESS**. The GTi hands its SD card to the computer, which mounts it as a normal
   removable drive.
4. Open the drive and go to **`/ADF`**. There's a **`sample`** folder already there — open it.
   It contains example files showing exactly what each file type should look like. Keep it;
   it's your reference.
5. **Create a folder in `/ADF` named after each game** and put its files inside. See the
   layout below.
6. **Eject the drive properly** from the PC when you're done. The GTi restarts and scans the
   card for the new games.
7. If it doesn't pick them up, go to **INFO → Rescan SD**. That rebuilds the game list and the
   cover thumbnails.

TODO — roughly how long the first scan takes on the supplied 512MB card with, say, a hundred
games. People need to know whether to wait or worry.

TODO — the three things people most often get wrong on day one.

---

## How games must be laid out on the card

**One folder per game. Files dropped loose in `/ADF` are not picked up at all.**

```
/ADF/
  Bard's Tale/
    Bard's Tale-1.adf
    Bard's Tale-2.adf
    Bard's Tale.jpg          <- cover art (optional)
    Bard's Tale.nfo          <- info text (optional)
    Bard's Tale.rtfm         <- manual (optional)
  Turrican/
    Turrican.adf
    Turrican.jpg
```

- The folder name is the title shown on screen. The browser sorts and groups by folder.
- **The disk image is the only required file.** Everything else is optional.
- **Multi-disk games:** put every disk in the one folder, named `Game Name-1.adf`,
  `Game Name-2.adf` and so on. They collapse into a single entry with a disk selector.
- `.jpg` or `.png` cover art goes beside the disk images, named to match.
- `.nfo` is plain-text game info. Follow the layout in `sample.nfo` — the fields are
  positional, so copy the sample rather than inventing a format.
- `.rtfm` is a plain-text manual, opened by the book button on the cover. It has its own
  section requirements, shown in `sample.rtfm`.

**The `/ADF/sample` folder on the supplied card is the reference for all of this.**

### "I dumped my whole collection into /ADF and nothing works"

That's why. Loose files in the root of `/ADF` are ignored — the GTi organises by folder, so a
file with no folder has no title to belong to. Give each game its own folder and rescan.

TODO — name a PC-side tool or script for bulk-reorganising an existing flat collection, if you
have one to recommend.

### Categories

Optional, for large collections. Top-level folders that contain **only other folders and no
disk images** (`Games/`, `Applications/`, `OS/`) are browsed as categories. A folder that
contains disk images is always a title, never a category.

Switched on with **CATEGORIES: ON/OFF** — in CONFIG.TXT, or in the on-device settings menu on
newer builds.

---

## Connecting to the Gotek — wired

The simple setup, and the one to start with.

1. Connect the GTi to the **Gotek's USB port** with the supplied cable.
2. The screen lights up. It's powered from the Gotek, so there's nothing else to plug in.
3. Power on the retro machine, tap a game, tap **INSERT**. The machine sees a freshly loaded
   floppy.

There is no configuration and no FlashFloppy settings to change. That's the whole procedure.

---

## Connecting wirelessly — the dongle

Wireless swaps the cable for the dongle, so the GTi doesn't have to sit next to the machine.

1. **Unplug the GTi** from the Gotek and plug the **dongle** into the Gotek's USB port
   instead. Power the Gotek on.
2. **Power the GTi separately.** It was drawing power from the Gotek before, so it now needs
   its own 5V over USB-C — a second cable to a PC, a USB battery bank, or a lithium battery
   bought for the purpose.
3. On the GTi, open **INFO** and set **MODE** from **STANDALONE** to **WIRELESS**.
4. Tap **SCAN DONGLES** (labelled **RE-PAIR** on some builds). The screen changes to a search
   view.
5. One device should appear. **Tap the device, then tap USE.** The screen returns to the game
   menu.
6. Select a game and tap **INSERT**. The GTi shows **LOADING** while the disk image transfers,
   and the game starts on the Amiga.

**There is no button to press on the dongle.** The shipped dongle firmware (3.2.0) pairs
automatically — plug it in, scan, tap, USE. If someone is holding buttons on the dongle and
getting nowhere, they're following instructions for the beta firmware they don't have.

### Owner-lock pairing — beta dongle firmware only

**Not on the shipped 3.2.0 dongle.** This applies only to Super Mini 3.5+ and XIAO 3.6.1+,
which testers may be running. Those builds only obey GTis you deliberately enrol, so nobody at
a show can pair with or walk off with your dongle. Up to four GTis per dongle.

- **Pair:** hold the dongle's `BOOT` button for about **5 seconds** and release. That opens a
  30-second enrolment window — now do **INFO → SCAN DONGLES → USE** on the GTi.
- **Forget one:** on the GTi, **SCAN DONGLES → select → DEL**.
- **Wipe all owners:** hold the dongle's `BOOT` for about **15 seconds**. It fires
  automatically at 15 seconds; no release needed.
- **Rename:** **SCAN DONGLES → RENAME** on JC screens.

**If a beta dongle doesn't appear in SCAN DONGLES, it's locked to another GTi.** BOOT-hold it
~5 seconds to open enrolment, then scan again.

### "No lights on the dongle"

Most dongle boards have no status LEDs and no screen, so **go by the timings above, not by
colours**. Some Super Mini boards do show a red power LED when plugged in; its absence isn't
necessarily a fault.

TODO — Mez reports a red LED on the shipped Super Mini. Confirm whether that's reliable across
the boards you ship, so this can be stated one way or the other.

### XIAO dongles

**Plug the external WiFi antenna in before powering on.** Without it, range is very poor.

### Range

TODO — realistic distance, and whether the printed case or the Amiga's shielding affects it.

---

## High-density (HD) disks

An HD Amiga disk image is 1.76MB against a normal DD disk's 880K.

- **Over the cable:** HD works.
- **Over wireless with a Super Mini:** DD only. The Super Mini holds the image in its own
  memory and hasn't room for an HD one. You'll get a size error rather than a load.
- **Over wireless with a XIAO:** HD is supported on newer XIAO firmware, which carries a
  larger ramdisk.
- **HD needs an HD-capable machine** — an A3000 or A4000 class Amiga. An A500 cannot read HD
  disks regardless of the GTi. Builds that show HD disks mark them with an HD badge and a
  "no A500" marker.

If an HD image won't load wirelessly: use the cable, or use a DD version. Most Amiga software
is DD.

---

## Save games

Newer builds persist what the Amiga writes back to the floppy. The write lands on the GTi's SD
card as **`GameName.sav.adf`**, beside the untouched original.

- When a save exists, **INSERT** boots the save automatically. The cover shows a green floppy
  badge.
- **To factory-reset a game**, delete its `.sav.adf` file.
- Controlled by **`SAVES=OFF/COPY/OVERWRITE`** in CONFIG.TXT. Default is `COPY`.

TODO — confirm what OFF, COPY and OVERWRITE each actually do, in one line apiece.

---

## Non-Amiga machines

**GEN mode** is a generic library that mounts any disk image, for machines other than the
Amiga — CPC, Spectrum and so on. Disk images go in **`/DSK`** instead of `/ADF`, laid out the
same way: one folder per title.

TODO — how GEN mode is switched on, and anything machine-specific worth knowing.

---

## Updating the firmware

**You cannot brick a GTi with a bad flash.** Short of an actual hardware fault there is always
a way back — see Recovery below. Say this first to anyone who sounds worried.

Current version numbers and download links are always on the flasher page:
https://mesarim.github.io/Gotek-Touchscreen-interface/

### Method 1 — over the SD card (no cable)

The easy path, if you're already on **5.3.0 or later**.

1. Download the `-update.bin` for your board from the flasher page.
2. Drop it on the **root of the SD card**. On 5.5.3 and later it's detected automatically; on
   older builds, rename it to **`GTi_update.bin`** first.
3. On the GTi, tap **INFO → FW UPDATE**.

No USB, no driver, no PC beyond copying the file across. You can put the file on the card via
**SD ACCESS** without opening anything.

### Method 2 — the web flasher (also the recovery method)

Flash over USB from the browser. This works even when the unit is in a bad state, which is why
it's the fallback for everything else.

1. Open https://mesarim.github.io/Gotek-Touchscreen-interface/ in **Chrome or Edge**. Web
   Serial doesn't exist in Safari or Firefox, so those won't work.
2. Select your device from the cards on the page.
3. Connect it to the computer with a **data** cable, not a charge-only one.
4. If prompted, put it in bootloader mode: **hold `BOOT`, tap `RESET`, release `BOOT`**.
5. Click **Install** and pick the right COM port.
6. Flashing takes about 30 to 60 seconds.

Afterwards, insert a FAT32 SD card and power on. The device generates `CONFIG.TXT` and creates
the `/ADF` and `/DSK` folders by itself.

### "Nothing appears in the COM port list"

Almost always a missing USB-to-serial driver rather than a fault. These boards use a
**CH340 / CH341** chip; if the driver isn't loaded, no port exists for the flasher to see.
Install it once, unplug and replug the board, try again.

- **Windows:** https://www.wch-ic.com/downloads/CH341SER_ZIP.html — run it, click Install,
  reboot if asked.
- **macOS:** https://www.wch-ic.com/downloads/CH34XSER_MAC_ZIP.html — recent macOS often has
  this built in; only install it if the port still won't appear.

If you're already running a GTi build, the **SD-card update** needs no USB and no driver at
all — that path keeps working regardless.

### Recovery

If an update goes wrong: **hold `BOOT`, tap `RESET`, release `BOOT`.** That puts the unit back
in bootloader mode where the web flasher can reach it. Then flash again from the browser.

If that doesn't recover it, it's a hardware fault rather than a firmware one — ask on Discord
or open an issue.

### Coming from a very old build

Units on the old **A.5.0.0** build need **one** web flash over USB to move onto the OTA
partition layout. After that, SD updates work forever and the cable is never needed again.

### After any update

Check the version string on screen to confirm the new firmware took.

TODO — say exactly where the version is displayed.

### Building from source

**USB CDC On Boot must be Disabled.** It's already correct in the pre-built firmware, but if
you rebuild it yourself and get this wrong, the Gotek won't recognise the device.

---

## "My library takes forever to scan"

Almost always the SD card, not the GTi.

The GTi reads a library the way a card is worst at: thousands of tiny reads and directory
lookups. That's *random access*. The big MB/s figure on the front of the card is *sequential*
speed and tells you nothing useful here. On real cards, a slow one enumerated at under ten
files per second where a good one managed several hundred — a thirty-fold gap on identical
firmware.

1. **First boot is always the slow one.** Let it finish; it builds a cache and the next boot is
   quick. Adding or removing games triggers a rescan.
2. **Try `SDSPEED=40` in CONFIG.TXT.** Default is 20, which is safe on every card. 40 is
   roughly 1.7× faster on cards that can take it, and falls back to 20 by itself if the card
   can't.
3. **Try a different card.** Look for A2 rather than A1, and Extreme/Pro tier rather than
   entry tier. Capacity isn't the issue — a small good card beats a large cheap one. The
   supplied 512MB card is fast precisely because it's simple. Cards above 64GB aren't
   recommended and very large ones may not enumerate at all.
4. **Update the firmware.** Older builds probed the card far harder during a scan than current
   ones do.

---

## Touch not working, or landing in the wrong place

There's a diagnostic build that settles whether it's the panel or the firmware. It draws four
corner targets and a crosshair wherever the panel thinks your finger is.

Flash **JC3248 — Touch Test** from the flasher page's Troubleshooting section, then tap each
corner:

- Crosshair lands under your finger and the box turns green → **the touch hardware is fine**,
  so the problem is in the firmware.
- Crosshair jumps, sticks, shows junk, or you get **I2C ERR** → the touch controller or its
  flex cable is **faulty**.

The diagnostic replaces your GTi build temporarily and leaves the SD card untouched. Web-flash
the normal build again when you're done.

---

## Messages on screen

TODO — list every status and error string the firmware can show, with a line each on what it
means. These are the highest-value entries in the file: someone reads a message they don't
understand, types it in, and gets a straight answer. Starting points:

- `SD MOUNT FAILED` — the card wasn't readable at boot. Reseat it, reformat as FAT32, or try
  another card.
- `TOO BIG` / `SIZE ERR` — the disk image is larger than the dongle can hold. See the HD
  section.
- `HD - NO WIRELESS` — an HD image was selected on a DD-only wireless link.
- TODO — the rest.

---

## CONFIG.TXT reference

`CONFIG.TXT` is created automatically on first boot, and new keys are appended when you update
firmware, so an old card gains new options without being reformatted.

Known keys:

| Key | Values | Default | What it does |
|---|---|---|---|
| `SDSPEED` | `20` / `40` | `20` | SD clock speed. 40 is faster on cards that support it, and falls back to 20 automatically. |
| `SAVES` | `OFF` / `COPY` / `OVERWRITE` | `COPY` | How writes from the Amiga are persisted back to the card. |
| `CATEGORIES` | `ON` / `OFF` | `OFF` | Browse the library by top-level category folders. |

TODO — complete this table from a current CONFIG.TXT. The bot is forbidden from inventing
config keys, so any key missing here is a key it will refuse to discuss. Worth being
exhaustive.

---

## Still stuck

**Discord is the fastest route:** https://discord.gg/7gY4PKUnnf

For bugs, open an issue:
https://github.com/mesarim/Gotek-Touchscreen-interface/issues

Either way, include:

- Firmware version, from the screen.
- Which board and screen size.
- Standalone or wireless, and which dongle if wireless.
- SD card make, model and size.
- What you did, what you expected, what happened.
- A photo of the screen if there's something on it.
