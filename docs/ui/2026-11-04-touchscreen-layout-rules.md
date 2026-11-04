# GTi touchscreen (JC3248, 480 x 320): layout rules and audit

Status: draft, 4 Nov 2026 (Dimitri + Claude), for discussion with Mez before anything is built. The look should come
closer to the web page's NEO style (dark cards with a thin border, a small grey label above a large value, one accent
colour), and the same grid must serve the skins (PR #29).

## The reference: FluxDrive's own page

Dimitri (4 Nov): FluxDrive's web page (http://fluxdrive-xxxx.local/) is "very clear in structure and choices, the look
and feel is very nice". What it does, and the panel should copy:

- A header with the logo, the device name and one grey line of status; then two or three info rows (label left,
  value right).
- Tabs for the two or three main areas (`Disk` | `Wi-Fi`), not pages of mixed keys.
- One card per drive: a small caption (`DF0`), a disk icon, the disk name large, one status line under it
  (`Stored · 880 KB · saving...`), and ONE action at the right (`Eject`). Nothing else competes in the card.
- Settings as rows: label left, a toggle switch right, a short grey sentence under it saying what it does. A setting
  never looks like an action.
- One accent colour for the active state (the toggle, the selected tab); the rest grey on dark.

## The rules (Dimitri, 4 Nov)

1. **Touch targets:** every interactive element has a touch area of at least 48 x 48 px, even when what is drawn is
   smaller (the hit rectangle is larger than the drawing).
2. **No hover:** feedback comes only from a pressed state and a clear change after the tap.
3. **Flow:** prefer one vertical column per area. Destructive or confirming actions (EJECT, RESTART, START FRESH,
   delete) sit far from navigation (BACK, page arrows, tabs).
4. **Type:** one font family, at most 3 sizes on a screen.
5. **Spacing:** an 8 / 16 px grid for padding and gaps.

On this screen: 48 px is 7.4 mm at about 165 ppi (Apple's minimum is 7 mm). 320 px of height holds at most six 48 px
rows, so every screen has to choose what is on it; anything else goes one tap deeper.

## Audit of today's screens (screenshots of 4 Nov, A600-lab2i-fd4)

| Screen | What breaks a rule |
|---|---|
| List (landscape) | A-Z column: 26 cells of about 10 x 14 px (rule 1, far off). Disk keys D1 D2 D3 about 46 x 20 (rule 1). Bottom keys about 32 high (rule 1). At least 4 text sizes: the status bar's tiny text, the side info, the keys, the list rows (rule 4). The NOW PLAYING bar lies over the last list row (rule 5). Two rows of look-alike keys (disk D1-D3, drive DF0 / DF1) next to each other. |
| Carousel | Disk keys 1 2 3 about 30 x 20 (rule 1). DF0 / DF1 sit 8 px under them. Otherwise the calmest screen. |
| Settings 1-3 | Keys about 34 high (rule 1). Three text sizes plus some keys with small text (FLEET, HOME WIFI, CRACKTRO STYLE, LIST IMAGE, LAST GAME) (rule 4). A setting and an action look the same (WEB UI: ON vs WIFI TEST). RESTART (destructive) sits between TEST TOOLS and SD ACCESS (rule 3). Mixed languages ("SAVED WIFI", "THUIS-WIFI"). |
| Drive info | Close to the rules: EJECT keys 92 x 32 should be 48 high; BACK at the bottom, away from the EJECTs (good). Disk names green and large, labels tiny. |
| Status bar | Version, mDNS name, battery and DISK in tiny text: information, not controls, but unreadable at arm's length. |

## Proposal (to be drawn as mockups next)

- **Grid:** 8 px base. Screen margin 8, gaps 8, card padding 8, between groups 16.
- **Three sizes:** small 16 px (labels, the status line), medium 24 px (keys, list rows), large 32 px (the one thing
  the screen is about: the selected game's name, a drive's disk). One family (the existing pixel font at 1x/2x/3x,
  or one smooth font if the size budget allows).
- **Keys:** 48 px high everywhere. A bottom bar of at most 4 keys at 48 px.
- **List:** rows 48 px; the letter jump becomes one key ("A-Z") that opens a letter grid (6 x 5 cells of 64 x 48)
  instead of the 26-cell column.
- **Disk and drive:** one row. The disk choice as a stepper (`<  Disk 2 of 3  >`, arrows 48 x 48) and below it the
  drive keys `DF0 | DF1` as the screen's primary action; never two rows of look-alike small keys.
- **NOW PLAYING:** a 48 px card that is part of the layout (the list ends above it), tap opens DRIVE INFO.
- **Settings:** two kinds of row, drawn differently: a setting (label left, value right, a chevron) and an action
  (a key). Destructive actions (RESTART, SD reset, FW update) on their own last page or behind a confirmation, never
  next to the page arrows.
- **Cards like the web page:** dark fill, 1 px border in the theme's line colour, small grey label on top, value
  below. The same card is a skin slot (PANEL), so skins keep working.

Open for Mez: whether the compact layout and portrait keep their own variants or follow the same grid; the font.
