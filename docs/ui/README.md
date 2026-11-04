# A calmer layout for the GTi touchscreen: proposal for discussion

Mez, this is a proposal from Dimitri and me (his Claude). Nothing is built yet: we want your view before anyone touches
the drawing code. The mockups are in `mockups-2026-11-04-en.html` (open it in a browser; every screen is drawn at the
JC3248's real 480 x 320). There is a Dutch copy next to it for Dimitri.

## Why

Dimitri put the current screens next to FluxDrive's web page and the panel's own `/panel` page on a phone. The web
pages read easily; the touchscreen feels crowded. Measured on the JC3248 (screenshots of 4 Nov):

- The A-Z column has cells of about 10 x 14 px; the disk keys D1 D2 D3 are about 46 x 20; the bottom keys about 32 high;
  the settings keys about 34. Fingers need about 7 mm, which is 48 px on this screen.
- One screen uses four or more text sizes (the status bar's tiny text, the side info, keys, list rows).
- Keys that do different things look alike: two rows of small keys (which disk, which drive) sit on top of each other;
  in Settings a setting (WEB UI: ON) looks the same as an action (WIFI TEST), and RESTART sits between TEST TOOLS and SD
  ACCESS.
- A tap on NOW PLAYING used to eject a disk. We already changed that on the `fluxdrive-panel` branch: it opens a
  Drive info screen now (see below).

## The rules we used

1. Every touch area at least 48 x 48 px (the drawing may be smaller; the hit rectangle is not).
2. No hover: a pressed state and a clear result after the tap.
3. One column per area; destructive or confirming actions (Eject, Restart, Start fresh) far from navigation (Back,
   page arrows, tabs).
4. One font, at most three sizes per screen: 16, 24 and 32 px (the existing pixel font at 2x / 3x / 4x would do).
5. An 8 px grid: margins 8, gaps 8, between groups 16.

## The structure

- **Always on screen.** At the top: where you are (List / Reel / Settings). At the bottom: the drive strip, what is in
  each drive you can reach. Tapping the strip opens Drive info. This replaces NOW PLAYING and the bottom key row.
- **Browsing and loading are split.** The list and the reel only browse. Tapping a program opens a program screen:
  cover, details, a disk stepper (`< Disk 2 of 3 >`) and the load key(s). That removes the side column's crowd of
  small keys.
- **Eject lives in Drive info only.** One card per drive, like FluxDrive's web page: a small label, the disk name large,
  one status line, one action (Eject) at the right, far from Back.
- **Programs, not only games.** The list counts programs, and a filter key ("All") picks Games / Tools / Demos / Own
  disks.
- **A-Z** is one key that opens a letter grid (cells of 64 x 48) instead of the 26-cell column.
- **Settings** get four tabs (Screen / Wi-Fi / Drives / System) and two kinds of row: a setting (label, value or a
  switch, a grey line saying what it does) and an action (a key). Restart, SD reset, firmware update and the test tools
  all go to System, behind a confirmation.

## Every setup, not just FluxDrive

Most people have the screen in a Gotek, or one dongle; only FluxDrive has DF0 and DF1. The drive strip and the load
keys therefore show "drives", whatever the screen can reach:

| Setup | Drive strip | Load on the program screen |
|---|---|---|
| Screen only (in the Gotek) | one wide card, "Gotek" | one full-width Load |
| One dongle | one wide card with the dongle's name and a link dot | one full-width Load |
| Two drives (two dongles, or a FluxDrive's DF0 + DF1) | two cards | two keys side by side |
| Three or more | two cards and "+N" (tap = all drives) | "Load into..." opens a picker: one row per drive with what is in it now |

The screen only knows "the Gotek" when it is wired in: it cannot tell whether the Amiga sees that Gotek as DF0 or DF1,
so it does not say.

## Skins

The cards, keys and the selected row map onto the skin slots of PR #29 (PANEL, KEY_UP / KEY_DOWN, ROW_SEL), so skins keep
working; a skin gets more of the screen to style, not less.

## Open questions for you

1. Is this a direction you want for the GTi at all, or should it stay a FluxDrive-only variant?
2. List rows of 48 px show three rows plus an edge. 40 px gives four, at the cost of rule 1. Your preference?
3. The compact layout and portrait: follow the same grid, or keep their own variants?
4. The font: the existing pixel font at three scales, or one smooth font if the flash budget allows?
5. Order of work: we would start with the drive strip and the program screen (they fix the worst miss-taps), then
   Settings, then the list and reel.
