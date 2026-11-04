# Skins: your own look, as one file you can upload and share

Date: 16 Oct 2026. Author: Dimmy (with Claude). Status: draft for review by Dimmy, then Mez.
Origin: a community member asked for something like Winamp's skins ("Skinner Atlas": one file with all the
pieces, used everywhere). Builds on `2026-10-07-gti-theme-section-design.md` (one THEME section, colour
themes as style JSON).

## Why

A GTi theme today is colours only: nine style fields from which the screen derives its 16 colours. The screen
draws all keys and panels itself, so a user cannot change the shapes, the textures or the icons. The goal is
that **anyone can make their own look and put it on their screen with little effort**, and swap it with others.

What made Winamp skinning easy, and what we copy (not the file format, the experience):

- You start from a **template** and change only what you care about.
- You edit with **ordinary tools**: a PNG in any paint program. No code, no special editor.
- A skin is **one file** you can share.
- A broken or half-finished skin **cannot hurt**: it falls back to the colour theme.

Winamp's own design (a fixed list of slots, positions known to the player, pixels supplied by the skin) is the
model for the contract between skin and firmware. We deliberately do not copy its limits (fixed sizes).

## Goals

1. A user can download a **starter skin**, edit it in a paint program, **upload** it in the web panel, and see
   it live on the screen.
2. A skin is **one file** (`.gts`) that can be posted in Discord, mailed, or dropped on the SD card.
3. Existing colour themes (style JSON in `/GTI/THEMES/<NAME>.json`) and every `CONFIG.TXT` keep working
   unchanged.
4. Any problem with a skin (broken file, wrong size, missing slot) degrades to the colour theme for that
   piece. Nothing a user uploads can leave the screen unusable.

## Non-goals

- No layout editing (moving or adding elements). No scripting. (The Winamp 3/5 "modern skin" trap: it made
  skins hard to make and hard to support.)
- The JC4827 and P4 screens in the first step (same order as NEO: JC3248 first).
- A new standalone skin builder. The existing Theme Editor in the shared web app grows an atlas section.

## Anti-goals

A skin system that works but needs a manual to use. A skin format only the firmware understands, so users
cannot inspect or edit what someone else shared.

## How it works

### The skin file (`.gts`)

One small binary container, written and read by the web page (JS) and the firmware (C). Layout, little endian:

| Bytes | Content |
|---|---|
| 4 | magic `GTS1` (version is part of the magic; a later format is `GTS2`) |
| 1 | reserved, `0` (no screen class: one skin serves every screen, see "One skin for all screens") |
| 1 | slot list version the atlas follows (see below; the slot table in `skin_draw.h` is its definition) |
| 2 | manifest length N |
| N | manifest: JSON, the existing style fields (`fill, light, dark, text, accent, ...`) plus `name`, `author` |
| 4 | PNG length M |
| M | the atlas, a plain PNG (256x192, RGBA) |
| 1 | number of backgrounds B (0..4; the loader accepts up to 6) |
| B x | background: 1 byte id (1 = 480x320, 2 = 320x480, 3 = 480x272, 4 = 272x480), 4 byte length, a baseline JPEG of exactly that size |

There is no tile chunk: the tile is the atlas `TILE` slot (64x64), used when no background picture matches the
screen's size and orientation. Backgrounds are JPEG, not PNG: JPEGDEC decodes any row width, PNGdec 1.1.6 does
not fit a 480 px row.

Why our own container instead of a zip: no new library on the ESP32; PNGdec is already in the toolchain. Why
not data inside the PNG: paint programs drop custom chunks when you save, so the manifest would be lost on the
first edit. The user edits a plain PNG; the web page does the packing.

Stored at `/GTI/THEMES/<NAME>.gts` (name rules as today: `A-Z 0-9 _`, max 16). If `<NAME>.json` and
`<NAME>.gts` both exist, the `.gts` wins (my call). `THEME=<NAME>` in `CONFIG.TXT` selects it, as for a colour
theme. The theme list on the screen and the web lists both kinds.

### The atlas and the slot list (the contract)

The atlas is one PNG. The slot list is fixed per slot-list version: each slot has a name, a rectangle in the
atlas, a size, and how it is drawn. The starter skin is the atlas with every slot filled with a clearly
labelled placeholder (the Skinner Atlas idea: the template is the documentation).

Proposed slots for slot-list version 1 (**proposal; the exact list and rectangles are settled in the plan from
what the JC3248 draws today**):

- `KEY_UP`, `KEY_DOWN`: the key face, normal and pressed. **9-slice**: corners stay, edges and centre stretch,
  so one picture serves every key size (the part Winamp did not have).
- `PANEL`: cover/info panel background, 9-slice.
- `ROW_SEL`: the selected list row, 9-slice.
- `TILE`: a 64x64 tileable pattern in the atlas that fits any screen by repeating. Full-screen backgrounds are
  not atlas slots: they are JPEG chunks of the container (see "The skin file"), one optional per screen size
  and orientation (the list grows as screens are added).
- `ICONS`: a fixed grid of small icons (insert, eject, folder, favourite, settings, battery, wifi...) drawn
  once on a 16x16 pixel grid. Which icons exactly: from the firmware, in the plan.

### One skin for all screens (Dimmy, 16 Oct)

There is no template per screen size. A skin declares what it supplies, and each screen uses what applies:

- The 9-slice art (keys, panel, selected row) and the colours apply on every screen.
- The background: the screen uses the slot for its own size and orientation if the skin has it, else
  the atlas `TILE` slot, else the colour theme's background.
- Icons are scaled by whole numbers (1x on the small screens, 2x on the large ones), so they stay sharp.
  A skin may add a second icon set for large screens (`ICONS_2X`); it is used when present.
- A skin made for one screen therefore looks right on the others, except that a missing background falls back
  to colours. The starter skin is NEO and works on every screen; only the background needs a size choice.

Not in version 1 (my call): the 6x8 screen font, text colours beyond the existing mapping, per-screen
layouts. A later slot-list version can add them; a skin built for version 1 keeps working.

Transparency: a slot may be partly transparent. PNGdec's RGB565 line path drops alpha, so either its alpha
mask is used or a reserved colour key (magenta `FF00FF`) means "transparent". **To decide in the plan after a
test on the device**; the user-facing rule must be one sentence either way.

### NEO is the default skin and the starter (Dimmy, 16 Oct)

The starter skin is **NEO itself**: the atlas the user downloads contains NEO's keys, panel, selected row and
icons, so a new skin starts as a copy of the default look and the user changes what they want. The labelled
slot map (the Skinner Atlas part) is a separate overlay image in the docs, not baked into the starter, so the
starter looks right on the screen.

- NEO stays the built-in default (`NEO=ON` as today). The firmware keeps drawing NEO in code; NEO is not
  loaded from a file. The starter atlas is a **rendering of NEO into the slots**, drawn at runtime in the
  browser by `gtsDrawStarter` in the web page (not made at build time, not checked in as a binary), so the
  code-drawn NEO and the starter look the same.
- NEO is see-through (translucent keys over the background picture). A skin that copies it needs **real
  alpha**, not a colour key: the transparency method is therefore decided as PNG alpha with blending, unless
  the device test shows blending a slot is too slow (then the starter gets pre-blended opaque slots and the
  colour key stays as a fallback). This replaces the open question in the section above.
- The web Theme Editor's "NEO" starting point (style derived from NEO's colours) stays; "Download starter
  skin" now gives the NEO atlas with those colours.

### Loading and drawing

- The atlas is decoded once when the skin is activated, into PSRAM as RGB565, and kept while the skin is
  active. Each slot is then drawn by copying its rectangle (9-slice for keys, panels and the selected row).
- Maximum atlas size and file size are limits to be **measured on the device**, not guessed. Starting
  proposal: file at most 256 KB; the atlas size follows from free PSRAM. Several full-screen backgrounds
  will not fit one small atlas, so a skin carries backgrounds as extra JPEG chunks in the container, loaded
  only for the screen in use (only the background that applies is decoded). The upload says which limit was hit.
- The colour fields in the manifest still define the 16 screen colours, exactly as the style JSON does today
  (the mapping table in the theme-section spec applies unchanged). A skin therefore always has a complete
  colour theme underneath it.
- A slot that is missing, fully transparent, or outside the atlas is drawn the classic way (the colour
  theme and the `BTNSTYLE=` setting). A `.gts` that does not parse at all is treated like a broken custom theme: fall back to NAVY, log
  one line.

### Drawing details (settled after the builder check)

- **Draw path:** the JC3248 draws into one full 480x320 RGB565 framebuffer in PSRAM (`framebuffer`, about
  300 KB) that is pushed to the panel by DMA. A slot is therefore a PSRAM-to-PSRAM rectangle copy, with
  9-slice done as nine copies/stretches. Stretching is done at draw time, no per-size caches (my call);
  if a redraw of the busiest page measures slower than today's, the plan adds a cache for the few key sizes
  actually in use.
- **Skin and NEO:** picking a skin is picking a theme, exactly as in the theme-section spec: it sets
  `THEME=<name>` and switches NEO off. The skin's slots replace the key/panel shapes; any slot the skin
  leaves out is drawn the classic way (the `BTNSTYLE=` pill/flat setting and the colour theme), not the NEO way.
- **States:** the state markers the firmware draws today (green edge for on, `> <` for the current choice,
  the INSERT/EJECT colours) are drawn **over** the skin art, so skins cannot remove them and version 1 needs
  only `KEY_UP` and `KEY_DOWN`.
- **Text on keys:** labels use the manifest's `text` colour (already one of the style fields). A skin author who
  draws a light key sets a dark `text`. No new manifest fields.
- **Theme page list:** entries are drawn in the skin's colour fields only; the skin's own `KEY_UP` is shown
  only for the active skin. Listing never decodes an atlas.
- **Preview in the editor:** indicative, not pixel-accurate: it shows the main-screen mock with the atlas
  slots, not every page.
- **Starter PNG hosting:** none needed: the web page draws the starter and the labelled slot map itself
  (`gtsDrawStarter`), so it works on a phone without internet. The docs site only holds a saved copy of the
  overlay and the README (`docs/skins/`).
- **Size limit:** the 256 KB limit counts the whole `.gts` file. Free PSRAM on the JC3248 with a cover
  decoded is measured first (the firmware already logs `psram=` at scan time); the atlas limit follows from
  that number.

### Web panel (served by `web_panel.h`)

Same compile switch as the theme API (`GTI_THEMES`), additive only, so other sketches build as before.

- **Starter download:** button in the Theme Editor, "Download starter skin". The page builds the `.gts` in JS
  from the starter the page draws itself (256x192) and the current colours; no file is served by the firmware
  or fetched from the docs site.
- **Import:** the user picks an edited PNG (and, in the editor, the colours); the page packs a `.gts` and
  POSTs it. Dropping a ready-made `.gts` works too.
- **Validate on upload, in plain words**, in the page before sending and again in the firmware: for example
  "atlas is 300 px wide, expected 512", "slot KEY_UP is empty, it will use the default look". A rejected file
  never reaches the screen's loader.
- **Export:** the page unpacks a stored skin into PNG + colours so it can be edited, and can pack it as a
  `.gts` to share.
- Endpoints (names are a starting point): `/api/skins/upload` (POST), `/api/skins/<name>` (GET the `.gts`),
  `/api/skins/<name>/delete`. Activate goes through the existing `/api/themes/<name>/activate`.
- The editor gets an atlas section next to the existing colour fields and a live preview. The editor changes
  are additive so `webui.html` stays identical in the OMEGAWARE tree.

### Screen (JC3248 first)

No new row. Skins appear in the THEME page next to the custom colour themes (max 8 custom entries in total as
today, colour themes and skins together). A skin's key in the list is drawn with the skin's own `KEY_UP` when
it has one.

## Constraints

- **Flash:** record the `.bin` size before and after for JC3248 (the only sketch with skin drawing in step 1).
  The new web code goes through `web_panel.h` and `webui.h`; the JC4827 and P4 sketches must stay well inside
  their headroom (the JC4827 has ~70 KB), so the web-side addition is budgeted and measured, not assumed.
- **PSRAM:** the atlas buffer is allocated at activation and freed on switching. No allocation per frame.
- **Safety:** the firmware validates every length field against the file size before reading. A skin never
  writes outside its buffer, whatever the file contains.
- Toolchain: core 3.3.12, PNGdec 1.1.6, JPEGDEC 1.8.4. Build into a scratch output directory; the committed
  `build/` binaries are not replaced.

## Testing

- JC3248 on Dimmy's desk: activate the starter skin, a skin with only `KEY_UP` changed, a skin with the atlas
  missing a slot, a corrupt `.gts` (falls back to NAVY, one log line), a `.gts` with lying length fields,
  an atlas over the size limit (rejected with the right message).
- Web on a phone and a laptop: download starter, edit the PNG, import, see it live; export and re-import;
  upload a `.gts` posted from Discord.
- Old cards: a card with only `.json` themes and a `CONFIG.TXT` from before skins behaves exactly as before.
- Flash and PSRAM numbers before and after, as above.

## Decided by Dimmy

- The goal is user-made skins, easy to change, uploaded by the user (16 Oct).
- One shareable file, packed by the web page; the user edits a plain PNG (16 Oct, "go" on the proposal).

- NEO is the default skin and the starter; one skin file for all screen sizes, backgrounds per screen size,
  icons scaled by whole numbers (16 Oct).

## My calls (check these)

- Container is our own `GTS1` format, not a zip and not data inside the PNG.
- `.gts` wins over `.json` of the same name.
- Version 1 slots are keys, panel, selected row, background and icons; the font is left for later.
- Starting limits (256x192 atlas, 256 KB) are as built; the PSRAM numbers are still to be measured on the device.
- Transparency: PNG alpha with blending (NEO is translucent); colour key only as a fallback after a device test.
- The starter is a rendering of NEO drawn in the browser; NEO itself stays drawn in code.
- JC3248 only in step 1 (the format is already screen-independent, see "One skin for all screens").
- The "Drawing details" section: picking a skin switches NEO off, state markers are drawn over the skin,
  stretching at draw time, indicative preview, starter drawn in the page.

## Left to the builder

The exact slot list and rectangles (from what the JC3248 draws), 9-slice border widths, the icon set, the
alpha method, the starter art, limits after measuring, endpoint details, and the test skins.

## Out of scope

JC4827 / P4 skins, a custom font slot, layout changes, a skin gallery or any online service, signing or
moderation of shared skins.

## Changes made during the build

Rulings taken while building (16 Oct), which replace the text above where they differ:

- Backgrounds in a `.gts` are baseline JPEG, exactly 480x320, 320x480, 480x272 or 272x480, not PNG.
- The container carries no tile chunk: the tile is the atlas `TILE` slot (64x64), used when no background
  matches the screen's size and orientation. The screen picks the background for the orientation current when
  the skin is loaded and reloads it when the screen is rotated.
- The starter skin and the slot map are drawn in the web page (`gtsDrawStarter`); nothing is made at build time
  or hosted. The slot list is version 1 as in `firmware/shared/skin_draw.h`.
- Icons are parked: the starter has the icon area (`ICONS`, `ICONS_2X`) but the screen does not draw icons from
  a skin yet; the cells are reserved for a later version.
- A `.gts` is inspected in the page before anything is sent (sizes, PNG type, baseline JPEG); what can be fixed
  is re-encoded, the rest is refused in plain words.
- Skin keys reach every key on the screen.
- NEO is off while a skin is active.
