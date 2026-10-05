# Gotek_P4_Round — Waveshare ESP32-P4-WIFI6-Touch-LCD-3.4C (3.4" round, 800×800)

**5.9.41-lab15n-P4R — bring-up build (30 Sep 2026).** Source = the 4.3" P4 `5.9.41-lab15n-P4` + the 3.4C board layer.
The normal square GTi UI is drawn **inside the circle** (a 624×464 landscape canvas in the middle of the glass).
The round layout (reel home screen, A–Z around the rim) is a later build, after a mock-up is approved (R9).

## Board layer (vs the 4.3" P4)
| Area | 4.3" P4 (JC4880) | 3.4C round (this sketch) |
|---|---|---|
| Panel | ST7701 480×800 portrait | **JD9365 800×800** (only the inscribed circle is glass) |
| DSI | 2 lanes | 2 lanes @ **1500 Mbps**, DPI **80 MHz**, h 20/20/40 v 4/12/24 |
| Reset / backlight | RST 5 / BL 23 | **RST 27 / BL 26 (active-low)** |
| Compose | 480×800 = whole panel | **464×624** (same portrait layout) → turned upright into the middle, rest black |
| Touch | GT911 SDA7/SCL8 | **GT9271** (GT911 protocol) SDA7/SCL8, 0x5D/0x14; ring touches ignored |
| microSD | 1-bit CLK12/CMD11/D0 13 | **4-bit** CLK43/CMD44/D0–D3 39–42, 1-bit fallback |
| SD update tag | `P4` / `OMEGAWARE.GTi.P4.fw` | **`P4R` / `OMEGAWARE.GTi.P4R.fw`** (never installs the other's .bin) |

Panel data: Waveshare BSP `waveshare/esp32_p4_wifi6_touch_lcd_xc` 3.0.1 (3.4" option) and their Arduino example —
same 197-command JD9365 init list in both. `esp_lcd_jd9365.c/.h` = Espressif's component v2.0.2 (Apache-2.0),
vendored; only the version log line is a literal.

## Tools ▸ menu
| Setting | Value |
|---|---|
| Board | ESP32P4 Dev Module |
| **Chip Variant** | **must match the chip**: esptool prints "Chip is ESP32-P4 (revision v1.x / v3.x)" when it connects. v1.x → "Before v3.00"; v3.x → "v3.00 or newer". Wrong one = won't boot. |
| Flash Size | 16MB (the board has 32 MB; 16 is fine) |
| Partition Scheme | Custom (sketch-local partitions.csv) |
| PSRAM | Enabled |
| USB Mode | USB-OTG (TinyUSB) |
| USB CDC On Boot | Disabled |
| Debug level | None |

## P4R-2 — 5.9.41-lab15o-P4R: the ROUND REEL (30 Sep 2026)
The reel now uses the whole glass (800×800) and is the home screen. The list and Settings are still the square canvas.
- `REELSTYLE=MOON` (default): covers ride an arc across the top, the one at 12 o'clock is selected. `REELSTYLE=FLAT`: one big cover, neighbours at the sides. Settings ▸ REEL STYLE.
- Status round the top rim; title, one line of the description, disk circles (up to 7 shown, `<` `>` when there are more), INSERT/EJECT.
- Bottom rim buttons: ROLL · LIST · CONFIG · ALL/FAV/MOST. Tap the cover or the title = the whole .nfo.
- `ROUNDUI=OFF` = the old square reel (Settings ▸ ROUND UI). `ROUNDHOME=LIST` = boot into the list instead of the reel.
- Settings ▸ TEST TOOLS ▸ **RIM TOUCH TEST**: tap 21 dots (12 on the rim, 8 inside, centre); gti.log gets how far each touch landed.
- How: round screens draw into their own 800×800 buffer (`roundBegin()`), `gfx_flush()` shows it with `p4disp_present_full()`;
  touch follows the last screen shown (round = 800×800 coordinates, ring included; square = the 624×464 canvas).

## P4R-2b/2c/3 — 5.9.41-lab15p/q/r-P4R (30 Sep 2026)
- 15p: a square screen after the round reel clears the ring (no reel leftovers round the edge).
- 15q: toasts (saves, dongle linked, SAVING GAME), load/eject endings, web-UI loads and the take-over question return to / show on the round screen.
- 15r **ROUND LIST**: the games as a wheel (middle row selected with a small cover, rows follow the edge and fade), A–Z round the
  right rim (tap a letter or slide a thumb along it - a big letter shows where you are), rim buttons INSERT/EJECT · REEL · CONFIG ·
  LIB (ADF→DSK→GEN). Tap a row = bring it to the middle; tap the middle row = the reel on that game. The reel's LIST button opens it.
  `drawFullUI()` now lands on the round reel/list whenever the round UI is on (leaving Settings, rescan, library switch, screensaver) -
  the square list is not used with ROUND UI on. `ROUNDHOME=LIST` boots into the round list.
- Still square: Settings, the .nfo reader, keyboard, dialogs, load progress, SD ACCESS, screensaver, boot screens (next builds).

## P4R-4 — 5.9.41-lab15s-P4R: ROUND SETTINGS (30 Sep 2026)
- Settings is round: the same rows as before, as pills down the middle of the glass, each as wide as the circle allows at its
  height (up to 7 per page). `<` and `>` circles at 9 and 3 o'clock turn the page, **CLOSE** is the rim button at 6 o'clock,
  "SETTINGS" and the page number run round the top rim. TEST TOOLS is round too. BTNSTYLE PILL/FLAT is followed.
- Toasts (dongle linked, saves) and "Loading ADF/DSK/GEN library..." now show on the round screens (reel, list, Settings)
  instead of flashing the square canvas.
- Sub-screens opened from Settings (keyboard, pairing, WiFi check, SD soak, firmware update) are still square; they come
  back to the round Settings.
- Still square: the .nfo/manual reader, keyboard, dialogs, load progress, SD ACCESS, screensaver, boot screens.

## P4R-4b — 5.9.41-lab15t-P4R: ROUND LIST AS PILLS (30 Sep 2026)
- Mez picked "B": every row of the round list is a pill with a small cover, like Settings. Rows get dimmer away from the
  middle, widths follow the circle (kept clear of the A-Z letters and the rim buttons), the loaded game has a green edge,
  favourites a star at the right end. The middle row is as before (big pill, amber edge, bigger cover, NDSK badge).
- While the list moves the side rows use the small quick covers; when it stops, the rows near the middle get their full
  covers (a short pause the first time, then they come from the cache). Touch, drag, A-Z and the rim buttons are unchanged.

## P4R-6a — 5.9.41-lab15u-P4R: ROUND SCREENSAVER + ROUND SCAN/BUILD SCREENS (30 Sep 2026)
- All three screensavers use the whole round glass:
  - BOUNCE (sprites, names, /screensaver pictures, Denise/Wrangler/Retronaut): bounces off the circle's edge (a small random
    turn on each bounce so it never runs one line through the middle); pictures up to 260 px, names text size 5.
  - MATRIX rain fills the circle.
  - SLIDES: each picture decoded bigger (its own decode, not the cover cache) and fitted into a 640x640 box in the middle;
    fade / dissolve / slide work on the round buffer.
- SCANNING, BUILDING (cover tiles) and "Preparing covers" are drawn on the round glass (library switch ADF/DSK/GEN, RESCAN,
  first boot on a new card). Mez: "switching from adf to dsk to gen gives a square".

## P4R-6b — 5.9.41-lab15v-P4R: polish from Mez's photos (30 Sep 2026)
- Round list: the side rows' pills were invisible on his theme (COL_PANEL is almost the background) - they are now the
  background lifted towards the text colour, so they show on every theme (brighter near the middle).
- Rim button labels (reel, list, Settings CLOSE) size 3 - size 2 read faint on the glass. The NDSK badge on the middle row size 2.

## P4R-6c — 5.9.41-lab15w-P4R: the same rim buttons on the reel and the list (30 Sep 2026)
- Mez: "The list and reel buttons seem randomly located... Standardise it?" Both screens now use the list's four slots, same
  job and colour per slot, from 6 o'clock round to 9: action (ROLL / INSERT, green) - view switch (LIST / REEL, blue) -
  CONFIG (accent) - filter (ALL-FAV-MOST / ADF-DSK-GEN, amber). The list is unchanged; the reel's buttons moved (the right
  side of the reel is now free, the list keeps A-Z there).
- The reel draws its rim buttons last, so the small far cover of the MOON arc at 8 o'clock tucks under the filter button.

## P4R-6d — 5.9.41-lab15x-P4R (30 Sep 2026)
- Rim button labels 10% smaller (2.7x instead of 3x; new fractional text size `gfx_setTextSizeT`) - Mez: CONFIG a touch too big.
- Settings hides COMPACT, REEL BORDER and LIST TILE while ROUND UI is on: they only change the square list/reel. The CONFIG.TXT
  keys still work and the items come back with ROUND UI OFF.

## P4R-6e — 5.9.41-lab15y-P4R (30 Sep 2026)
- Rim button labels back to whole 3x letters (even strokes - the 2.7x of 15x looked uneven on the glass), set 2 px apart
  instead of 3, so CONFIG is 100 px wide instead of 108. Mez picked this ("option 2"). `rTextCAdv()`; RIM_SZ 3, RIM_ADV 17.

## P4R-6f — 5.9.41-lab15z-P4R: curved rim labels (30 Sep 2026)
- Mez: "no change on the config... maybe curve it?" The rim button labels (reel, list, Settings CLOSE) now CURVE along the
  rim like the writing round a coin: each letter turned to the circle at its own angle, tops towards the centre, reading left
  to right, edges smoothed (4x4 samples per pixel against the button colour). 3x letters, normal spacing - they sit inside their
  buttons now. `rArcLabel()`. Flat whole-size text elsewhere is unchanged.

## P4R-6g — 5.9.41-lab16a-P4R (30 Sep 2026)
- The boot screen (OMEGAWARE / GTi, shown while the library loads) has the firmware version in small grey under "GTi" (Mez),
  and is drawn on the round glass.

## P4R-7 — 5.9.41-lab16b-P4R: AMIGA THEME (30 Sep 2026)
- New THEME: **AMIGA** (Settings > THEME, or `THEME=AMIGA`): Workbench 1.3 blue background, white, black and orange.
  Rim buttons: action orange, view black, CONFIG white, filter light orange.
- With the **ADF** library (and ROUND UI on), the round reel, list and Settings sit on a big faint Boing ball. It is drawn
  once into its own 800x800 picture (1.3 MB PSRAM, first use only) and copied in at the start of each frame, so scrolling
  costs the same; text over it is see-through. DSK / GEN: plain Workbench blue.
- The screensaver in the AMIGA theme with ADF is the **Boing demo**: grey room, purple grid wall and floor, the red/white
  ball spinning and bouncing (it follows the round edge), with its shadow. Replaces SSMODE while that theme + ADF is on.

## P4R-5 — 5.9.41-lab16c-P4R: ROUND READER + red outline + calmer AMIGA blue (30 Sep 2026)
- The .nfo / manual reader (tap the cover or title on the reel) is round: the text in a 540 px column in the middle of the
  glass (it slides in and out at the top and bottom of the window), the title, game and % read round the top, a dotted arc
  on the right rim shows where you are. Rim buttons: TOP (right), CLOSE (6 o'clock, as in Settings), SIZE, JUMP (sections,
  manuals only). Drag to scroll; a tap on the text still closes it. The JUMP list itself is still square.
- Settings: the last option you tapped has a red outline (round and square), cleared when Settings opens again.
- AMIGA theme: a deeper, calmer blue (#1E3A5F) - Mez: "not so blue". The Boing background follows it.

## P4R-7b — 5.9.41-lab16d-P4R (30 Sep 2026)
- AMIGA theme: the background ball is a solid red/white Boing ball (no blue tint - Mez: "red and white, like in B"), resting
  low on the right (centre 605,555, r 92) with a shadow on the floor - clear of the title, INSERT, the rim buttons and the A-Z.
  Still drawn once, still only with the ADF library.

## P4R-6h — 5.9.41-lab16e-P4R: INSERT on the round screens (30 Sep 2026)
- "Loading", "too big", "failed", "size error", the multicast count and LOAD DIAG are a round card in the middle of the screen
  you were on (reel, list or Settings stay underneath) instead of the square panel. `rLoadCard()`.
- The dongle take-over question ("DONGLE IN USE") is a round card with TAKE OVER / CANCEL.

## P4R-7c — 5.9.41-lab16f-P4R (30 Sep 2026)
- AMIGA theme with the **DSK** library: a Spectrum-style rainbow stripe (red, yellow, green, cyan; low on the right, kept inside
  r 350 so the list's A-Z stays clear) and CPC-style colour keys (red, green, blue) beside it - DSK can be either machine, so
  both (Mez). The looks of those machines, not the makers' logos. ADF keeps the Boing ball; GEN stays plain. The Boing
  screensaver stays ADF-only. The background is rebuilt when the library changes.

## P4R-7d — 5.9.41-lab16g-P4R (30 Sep 2026)
- DSK art fix (Mez's photo of 16f showed only a red sliver): the rainbow stripe moved inside the circle (x 530-750, y 460-690,
  all four bands visible), the colour keys 40 px left (x 430-550) so they don't touch it.

## P4R-7e — 5.9.41-lab16h-P4R (30 Sep 2026)
- DSK art (Mez): the rainbow stripe moved left of centre, below INSERT (x 262-382), level with the colour keys right of centre
  (x 430-550), both y 576-658 - a matching pair.

## Hidden CONFIG.TXT keys for bring-up
- `PANELTURN=0|90|180|270` — turns the whole picture **and** touch, if the panel's "up" isn't the board's "up".
- `ROTATE=` works as on the 4.3" P4 (landscape / portrait / flipped).

## What to check (report back, with gti.log)
1. Picture appears, upright, centred, nothing cut off by the round edge; backlight on.
2. Touch: taps land where you touch. gti.log has `[touch] raw x,y` for the first 16 touches.
3. `[sd] clock 20000 kHz, 4-bit` (or 1-bit) in gti.log; library lists; covers.
4. Cable load → the PC sees DISK.ADF. SD ACCESS works.
5. Soak test (Settings ▸ TEST TOOLS) once.
