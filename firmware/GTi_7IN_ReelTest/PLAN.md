# GTi 7" Reel Test — what it is and what to read off it

Bench sketch for the **Waveshare 7" 800x480 (ESP32-S3)** — the board
`firmware/Gotek_7inch` targets, and the one the web flasher serves as
**v5.0.1-7IN-beta**. Not firmware. It draws the same covers four different ways
and puts the millisecond cost of each on screen.

## The bug it exists to measure

`Gotek_7inch` v5.0.1's 3x2 cover grid does this, per tile, on every page draw:

```c
if(coverIsPng(gm.jpg_path)) drawPngFit (gm.jpg_path, ...);
else                        drawJpegFit(gm.jpg_path, ...);
```

Six full JPEG/PNG decodes per page turn — read the whole file off the card,
`ps_malloc` a full-size RGB565 buffer, **decode at 1:1 with no downscale**, then
nearest-neighbour shrink it into a ~150 px box and throw all of it away.

Meanwhile `carTile()`, `carLoadThumb()`, `carSaveThumb()`, `carDecodeTile()`,
`carThumbPath()` and a 16-slot LRU are **already in that same file**, ported
from the JC3248, and the grid calls none of them. That is the "5.5.5 had slow
draw on several images".

`Gotek_7B` (1024x600) has the identical grid code and the identical bug.

## The second finding: the 7A is behind its own sibling

`Gotek_7B` already has a **dirty-rect flush engine** (`Gotek_7B.ino` ~line 298):
`markDirty(x0,y0,x1,y1)`, a previous-frame box for two-buffer coherence, an AUX
detached box, and `g_perf_copy_us` / `g_perf_flip_us` timers. Its flasher blurb
even says so — *"dirty-rect perf engine"*.

`Gotek_7inch` has none of it. Its `display()` is:

```c
memcpy(dst, cb, (size_t)KLCD_W*KLCD_H*2);    // 768,000 bytes. Every frame.
```

So the 800x480 board pays a full-screen PSRAM-to-PSRAM copy per frame while the
1024x600 board copies only what changed. The bench's DIRTY mode is written to
the **same shape as the 7B's engine on purpose**, so whatever it measures
transfers straight into a back-port rather than being a one-off experiment.

## Modes — tap [MODE] to cycle

| # | Mode | What it does |
|---|------|--------------|
| 1 | GRID-OLD | 3x2 grid, per-tile `drawJpegFit`/`drawPngFit` — **what ships today** |
| 2 | GRID-TILE slow blit | tile cache, but the old per-pixel `carBlit` |
| 3 | GRID-TILE fast blit | tile cache + the lab3 row-pointer blitter |
| 4 | REEL | moving 5-up coverflow, tile cache + fast blitter, free-running |

Grid modes auto-advance one page per frame, so you are watching a continuous
page-turn — which is how flicking through a library actually feels. In GRID-OLD
on a cold cache expect *seconds* per frame. That is the demonstration.

The split 1 -> 2 is the **decode** saving. 2 -> 3 is the **blit** saving.
Mode 4 answers whether the JC's moving reel ports over.

## [FLUSH] — FULL vs DIRTY RECT

FULL is today's 768,000-byte copy. DIRTY copies only this frame's dirty box
unioned with the previous frame's — two, not one, because the page-flip means
the buffer being written to last displayed the frame *before* last, so anything
that changed in either of the last two frames is stale in it.

The status and button bars repaint only on demand. If they repainted every frame
they would dirty rows 0-26 and 428-480 and DIRTY would degenerate to FULL,
measuring nothing.

## A 1000-game card: read the cache, never walk the tree

The 7B P4 measured **577 s to enumerate 1741 files / 1005 games**. That is the
card seeking through a flat FAT directory with the CPU idle; no firmware-layer
fix exists. So the bench loads its library in this order:

1. `/ADF/.filelist` + `/ADF/.gamecache` (and the `/DSK` pair) — the caches
   **`Gotek_7inch` itself writes**. If 5.0.1 has browsed this card they already
   exist and already carry the resolved cover path per game, so boot is seconds.
2. `/.reelbench` — the bench's own index, from a previous run.
3. Only then, a tree walk — and it writes `/.reelbench` afterwards so it never
   happens twice. **The firmware's caches are never overwritten.**

Covers that the cache does not already know resolve **lazily**, once per game,
cached — and the probe cost gets its own HUD field (`cover?`), because
`findJPGFor` is up to 24 `SD_MMC.exists()` calls against a flat directory and
that is the ~1 s per game the caches exist to avoid.

All three strings per game live in one **PSRAM bump arena**, not in Arduino
Strings. 1005 games x 3 Strings is ~180 KB of small heap blocks, and the ESP32
core only routes allocations >= 4096 bytes to PSRAM — every one of them would
have landed in the ~320 KB of internal RAM.

## HUD

```
fill  cover?  tiles  blit  text  copy (Nkpx)  vsync
frame  avg  min  max  FPS  hit/ld/dec/none
```

* **fill** — clearing the content band
* **cover?** — `SD_MMC.exists()` probing for a cover path not in the cache
* **tiles** — getting the artwork: cache hit, `.tnl` thumb read, or full decode
* **blit** — scaling the tile onto the compose buffer
* **copy** — compose -> framebuffer, with the pixel count actually copied
* **vsync** — waiting for the flip to latch. At 16 MHz pclk with these porches
  the panel frame is **~32 ms** (976 x 528 clocks), so a wait up to ~32 ms here
  is the panel, not the code. That is the ceiling any reel has to live under.
* **hit / ld / dec / none** — cache hits, thumb loads, full decodes, no art

Tap anywhere above the bottom bar to zero the counters. Same numbers go to
Serial every 30 frames at 115200.

## Buttons

`[MODE]` `[FLUSH]` then, in grid modes `[< PREV]` `[NEXT >]`, in reel mode
`[SLOWER]` `[FASTER]` (0.5 - 12 games/s).

## Thumb interop

Tiles are 200x200 RGB565 `.tnl` files under `/ADF/.thumbs` or `/DSK/.thumbs`,
same djb2-xor filename hash as `Gotek_7inch`. Byte-identical — tiles this bench
builds are read straight back by the shipping firmware, and vice versa.

## What gets decided from the numbers

1. If **GRID-OLD -> GRID-TILE** is the big drop, the fix to ship is the
   three-line swap in `drawCarousel`: `carTile()` + `carBlit()`, with
   `drawJpegFit`/`drawPngFit` kept only as the no-tile fallback. Same change
   applies verbatim to `Gotek_7B`.
2. If **slow -> fast blit** is small, the 7"'s row-major compose buffer was
   never the problem the JC's rotated one was (there, `g_rot=0` put consecutive
   writes 640 bytes apart in a 307 KB PSRAM buffer — a cache miss per pixel),
   and the old blitter can stay.
3. If **copy** turns out to matter, back-port the 7B's dirty-rect engine into
   `Gotek_7inch` — it is written, shipping, and this bench measures it in the
   same shape.
4. If **REEL** holds a usable frame time with copy and vsync included, the JC's
   moving reel ports to the 7" as-is.

## Host verification already done

* `blitTileFast` vs `blitTileSlow` (the shipping per-pixel form): **6,048 cases,
  pixel-identical** — sizes 1-799, square and non-square, all three dim levels,
  null tiles, every edge and corner clip case. Dirty box always covers at least
  what the reference touched.
* Dirty-rect union flush vs full copy: **8,000 random frames, zero stale
  pixels**, with page flipping. On that synthetic load it copied 34% of a full
  frame.
* `.filelist` / `.gamecache` parsers against files generated exactly the way
  `writeFileList()` / `writeGameCache()` produce them, including `?` (checked,
  no art) vs empty (never checked), a 3-disk set, a subfolder path, a 44-char
  name, and no trailing newline.
* Line splitter edge cases — and it **caught a real bug**: the terminator was
  written over the `\n` before the end-of-buffer test read it, so any LF-only
  index file would have loaded exactly one game.
