# Calm layout: implementation plan

Design: `docs/ui/README.md` and the mockups (`mockups-2026-11-04-en.html`). Rules: 48 px touch areas, no hover, destructive
actions far from navigation, the pixel font at 2x / 3x / 4x only (16 / 24 / 32 px), an 8 px grid. Branch `gti-calm-ui`
(release-a600-lab2 + gti-skins + fluxdrive-panel). JC3248 only; the other boards keep their code.

Every phase ends with: host tests green, the sketch compiled, the screen flashed by web OTA, and screenshots of every
screen it touched (both orientations) checked against the mockups. FW_VERSION `A600-calmN-JC3248`, one N per phase.

## Phase 0: base and a test hook
- The merge compiles and runs (skins, DF1, START FRESH, the fleet as before).
- `POST /api/tap?x=&y=` and `POST /api/tap?x=&y=&hold=ms`, only when `GTI_WEB_TAP` is defined (a test build; never in
  a release): it feeds the same handleTap / hold path a finger does. With `/api/screenshot` the screens can be walked
  without someone at the panel.

## Phase 1: the building blocks
- `calm_ui.h` (pure, host-tested where it decides): the grid (margin 8, gap 8, group 16, key 48), the three text sizes,
  rectangle helpers (rows, columns, splits), and the hit-test of a list of rectangles.
- Drawing primitives in the sketch, skin-aware (a skin's KEY_UP / KEY_DOWN / PANEL / ROW_SEL when it has them, NEO
  otherwise, the colour themes as fallback): key (normal, primary, danger, pressed), card, row, chip, toggle, tab bar,
  stepper.
- `gti_targets.h` (pure, host-tested): the drives the screen can reach, as a list: wired Gotek (screen only), each
  dongle, each FluxDrive's DF0 and (with "df1":1) DF1; each with its name, what it holds, link state, wp, storing,
  unsaved. The strip layout (1 wide, 2 halves, 2 + "+N") and the load-key layout (one Load, two keys, "Load into...")
  are functions of that list.

## Phase 2: the frame
- The top bar (List / Reel tabs, the count, Settings) and the drive strip at the bottom, on every browsing screen.
- The drive strip replaces NOW PLAYING and the bottom key row; a tap opens Drive info.
- Drive info for every kind of drive (the DF1 screen from fd4, generalised): one card per drive, Eject far from Back.

## Phase 3: the list
- Search row (search, A-Z, the filter "All / Games / Tools / Demos / Own disks" from the existing categories), rows of
  48 px with the drive chip, the letter grid screen.
- The side cover column goes; a tap on a row opens the program screen.

## Phase 4: the program screen
- Cover, details, the disk stepper, the load keys from `gti_targets.h`, the "Load into..." picker, the "..." key for
  the manual and START FRESH.

## Phase 5: the reel
- Covers only; a tap opens the program screen; the dice in the filter.

## Phase 6: Settings
- Four tabs (Screen / Wi-Fi / Drives / System), setting rows (label, value or toggle, a grey line) and action keys;
  System's destructive actions behind a confirmation.

## Phase 7: portrait and the old paths
- Portrait on the same grid; the compact layout retired (its setting migrates to the new list); dead code removed.

## Phase 8: the skin builder
- The Theme Editor's starter skin and slot map follow the new primitives; docs/skins/README.md updated.

Device checks with Dimitri at the end of phases 2, 4 and 6 (the feel of a real finger); the rest by screenshots.
