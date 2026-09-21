# GTi — Alias Disk Test Sketch — Plan of Action

**Sketch:** `firmware/GTi_AliasTest/GTi_AliasTest.ino` (standalone, not part of the GTi build)
**Board:** JC3248W535 — same settings as the main firmware (ESP32S3 Dev Module, USB-OTG/TinyUSB, CDC disabled, OPI PSRAM, 240 MHz)

## The claim being tested

A file that lives on the SD card can be presented over USB as an ordinary FAT12 volume **without copying it anywhere**. The GTi builds ~6.5 KB of FAT12 metadata in RAM; the volume's data area is an *alias* for the file's real sectors on the card. Not a symlink — FAT has no such thing and FlashFloppy wouldn't follow one. A block-level remap, the same idea as a Linux loop device or a VM raw-device map.

Already proven on the host against an independent FAT driver: contiguous and 25-extent fragmented files, 880 KB to 12 MB, sequential + 6,000 random reads, all byte-identical. What the host cannot prove is whether it works on *this silicon, this SD driver, this USB stack*.

## Why a separate sketch, and why the PC first

No covers, no NFO, no caches, no reel, no wireless, no saves, no OTA. If something breaks it is the alias layer, not three years of accumulated firmware.

**PC first, Gotek second.** A PC can checksum the file and tell us byte-for-byte whether the mapping is right. A Gotek can only tell us "didn't boot", which is useless for debugging. Once the PC says the bytes are perfect, the Gotek is just a different USB host.

## What the sketch does

1. Boot → panel + touch + SD (1-bit, 20 MHz) — scaffolding lifted verbatim from the main firmware
2. Walk `/GENERIC` and `/ADF` one level deep. **Names and sizes only** — no sidecars, no covers, no `.index`, no `.gamecache`
3. Plain scrolling list, tap to select
4. On tap: resolve the file to card sectors → build the wrapper → attach USB
5. Live screen: filename, size, extent count, sectors served, MB/s
6. EJECT → detach → back to the list

## The only genuinely new code

| Function | Job |
|---|---|
| `mbrFind()` | sector 0 → partition 1 start LBA |
| `bpbParse()` | FAT32 BPB → sec/cluster, FAT LBA, data LBA, root cluster |
| `pathResolve()` | walk `/GENERIC/Game/Game.hfe` through directory clusters, reconstructing long filenames |
| `chainToExtents()` | FAT cluster chain → runs of contiguous clusters → `(cardLBA, sectorCount)` |
| `wrapBuild()` | FAT12 boot sector + FAT + root into a 6.5 KB RAM buffer *(already written and verified)* |
| `aliasRead()` | `onRead(lba)` → metadata from RAM, or extent lookup → `SD_MMC.readRAW()` |

Everything else is transplanted from working code.

## Test protocol

1. Plug the GTi into a PC. A small volume appears with one file on it.
2. Copy that file off. `sha256sum` (or `certutil -hashfile ... SHA256`) against the original on the card.
3. Repeat for: an 880 KB ADF, a 1.76 MB HD ADF, a >5 MB HFE, and one deliberately fragmented file.
4. Watch the **extent count** on screen. `1` = contiguous (easy case). `>1` = the case that matters.

To force a fragmented file for step 4: fill the card near full, delete every other file, then copy the big image on.

## Success criteria

- SHA256 matches on every file, every size
- The fragmented file matches
- The list stays responsive while the PC is reading — no deadlock

## The three unknowns this actually answers

1. **Does `readRAW` in the USB callback coexist with FATFS in the main loop?** The board has no tasks today, so nothing has ever exercised this. The IDF sdmmc driver *should* serialise it. This is the one that could sink the whole approach.
2. **Is the card FAT32 or exFAT?** Cards over 32 GB ship exFAT, which this sketch does not parse. It will say so on screen rather than misbehave.
3. **Throughput** over 1-bit SD, and whether a USB host times out waiting.

## Explicitly out of scope

Writes, exFAT, saves, the Gotek itself, anything over 32 MB, and any change to the shipping firmware. Nothing here touches `Gotek_JC3248.ino`.

## If it passes

The payload stops needing PSRAM at all, which means `g_disk` shrinks from ~2.9 MB to 6.5 KB and hands that memory straight back to the 1000-game problem. Loading becomes instant at any size, because there is no copy. And `DISKMAXKB` stops being a ceiling.
