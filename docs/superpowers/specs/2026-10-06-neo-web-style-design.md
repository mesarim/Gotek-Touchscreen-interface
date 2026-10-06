# NEO everywhere: one calm look for GTi's web pages and screens

Date: 6 Oct 2026. Author: Dimmy (with Claude). Status: draft for review by Dimmy, then Mez.

## Why

GTi's web surfaces were each built on their own and none of them match: the panel's landing page
(`/`), the shared web app (`/panel`), the SD file manager (`/files`) and the dongle's own page
(`/classic`) each carry a different dark palette. Mez added the NEO look to the P4 screens
(commits `959cd71`, `ec74c55`) and both Mez and Dimmy like it: calm, navy to purple, amber for
what is selected, cyan for INSERT. This work makes NEO the one look of GTi, on the web and on the
screens that do not have it yet.

## The three steps

Each step gets its own branch off current main, its own plan and its own review. This spec covers
step 1 in detail; steps 2 and 3 are fixed here only as direction.

| Step | What | Touches |
|---|---|---|
| **1. NEO everywhere** (this spec) | 1a panel web pages in NEO; 1b dongle `/classic` in NEO colours; 1c NEO as an on-screen theme on JC3248 and JC4827 | `firmware/shared/web_panel.h`, `firmware/shared/webui.html` (+ regenerated `webui.h`), Webby sketches, `Gotek_JC3248.ino`, `Gotek_JC4827W543.ino` |
| 2. One web app on the panel | The app is served at `/`. The current landing page goes away. `/files` stays a real URL that opens the app on its Files tab, and Files is a menu item in the app next to Dashboard, Games and Config. `/panel` redirects to `/`. Only the Wi-Fi sign-in page stays separate. | `web_panel.h`, `webui.html` |
| 3. Dongle clean-up | Remove stale code first (the per-sketch `webui.h` copies that lag behind `shared/`, the XIAO fork still on 1.6.4, dead code), then give the dongle the same app. | Webby sketches |

## Decisions

1. **NEO is GTi's fixed web style.** No theme choice on the GTi web pages; the Themes tab is not
   offered on GTi. (Option A of the brainstorm.)
2. **`webui.html` stays shared with the OMEGAWARE tree.** Per `docs/SHARED-MODULES-HANDOVER.md`
   the files in `firmware/shared/` are byte-identical to their OMEGAWARE twins. Step 1 only *adds*
   to `webui.html` (a NEO preset and its CSS); the same commit lands in the OMEGAWARE tree, where
   nothing changes for its users because OMEGAWARE does not select NEO. GTi selects NEO from
   `web_panel.h`, which exists only in this tree. (Option B.)
3. **Full NEO on the web:** palette plus the navy to purple gradient and the faint circuit traces,
   done in CSS with no image. System font, not a pixel font. (Option B of the visual comparison.)
4. **On-screen NEO for JC3248/JC4827: colours and background first, layout later.** The P4's taller
   bars and list/panel layout are a separate, later step, after Dimmy has seen colours plus
   background on his JC3248. (Option C.)
5. Step 1 changes looks only. No route moves until step 2.

## NEO colour reference

From the `THEMES[]` NEO entry in `firmware/Gotek_P4/Gotek_P4.ino` (RGB565, hex is the conversion):

| Role | RGB565 | Hex | Web use |
|---|---|---|---|
| bg | 0x0884 | `#081021` | page base under the gradient |
| panel | 0x10C6 | `#101831` | cards, lists (fill) |
| bar | 0x0863 | `#080C19` | top bar, dark |
| sep | 0x42D1 | `#42598C` | borders, separators (light) |
| dim | 0x9517 | `#94A2BD` | secondary text |
| lit | 0xEF9F | `#EFF3FF` | text |
| green | 0x568F | `#52D27B` | OK / ready |
| orange | 0xF466 | `#F78E31` | warnings |
| amber | 0xF586 | `#F7B231` | selection, active tab, call to action (accent) |
| blue | 0x7E9E | `#7BD2F7` | INSERT |
| accent | 0x3A4C | `#3A4963` | slate buttons |

Background: linear gradient from `#0A1026` to `#241640`, with traces in `#5A78C8` at about 15%
opacity (the screen blends at 40/256).

## Step 1a: panel web pages

- **`webui.html`**: add a preset `NEO` to the built-in presets (next to `OMEGA_DARK` at
  `webui.html:1984-1989`): fill `#101831`, light `#42598C`, dark `#080C19`, text `#EFF3FF`,
  accent `#F7B231`, flat, radius 10. Add a CSS block that applies only when NEO is the active
  style: the gradient and traces on `body`, translucent panels, cyan `#7BD2F7` for the INSERT
  action. Budget about 0.5 KB before gzip. Regenerate `webui.h` with
  `firmware/shared/make_webui_header.py`.
- **`web_panel.h`**: report `"theme":"NEO"` in `/api/system/info` (today it hard-codes
  `OMEGA_DARK`, `web_panel.h:125`) and send `"has_themes":false`, so the panel stops showing a
  tab that answers 404. The app has no such flag yet: add `has_themes` to `webui.html` next to
  `has_sd` (`webui.html:919-927`), where absent means "shown", so OMEGAWARE, which does not send
  it, keeps its Themes tab.
- **Landing page `/` and `/files`**: replace their hard-coded palettes with one NEO CSS block
  defined once in `web_panel.h` and used by both pages. Layout unchanged.

## Step 1b: dongle `/classic`

- Change the CSS variables in `PAGE_HTML` of `Gotek_SuperMini_Webby.ino` to the NEO values
  (`--bg`, `--panel`, `--panel2`, `--line`, `--ink`, `--dim`, `--amber` to `#F7B231`, `--cyan` to
  `#7BD2F7`, `--green` to `#52D27B`) and the radial background to the NEO gradient.
- Regenerate the S3-Zero sketch with `firmware/Gotek_Zero_Webby/make_zero.py`.
- Apply the same variables by hand to `Gotek_XIAO_Webby.ino` (a separate fork until step 3).
- The dongles' copies of the web app (`/app`, and `/` on home Wi-Fi) are left alone until step 3.

## Step 1c: NEO on the JC3248 and JC4827 screens

Both sketches already use the same `Theme` struct and framebuffer drawing as the P4
(`Gotek_JC3248.ino:2406`, `Gotek_JC4827W543.ino:2360`), so this is a port, not new design.

- Add the NEO entry to `THEMES[]` with the P4 colours.
- Add `NEO=ON|OFF` to CONFIG.TXT (default ON, as on the P4) and a "NEO: ON/OFF" row on the
  Settings screen, switched live. When NEO is on, the THEME row is hidden, as on the P4. Every
  setting is reachable in both CONFIG.TXT and Settings (working rule).
- Draw the gradient and traces where `drawFullUI()` now calls `gfx_fillScreen(COL_BG)`. Compute
  it per line into the framebuffer instead of keeping a full background buffer in PSRAM; fall
  back to the flat `bg` colour if that turns out too slow.
- Layout stays as it is.
- Before editing `Gotek_JC3248.ino`, announce the area on the session relay (working rule).

## Constraints

- **Flash headroom:** JC4827 about 70 KB free, Webby SuperMini about 100 KB. Record the `.bin`
  size of each touched sketch before and after; step 1 should add well under 5 KB anywhere.
- **Build output:** compile into a scratch output directory; the committed `firmware/*/build/`
  binaries are only replaced when a release build is made on purpose (they are the downgrade
  library and stay committed).
- **Toolchain:** arduino-esp32 core 3.3.12 on both sides.

## Testing

- Compile JC3248, JC4827, Webby SuperMini, Webby S3-Zero and Webby XIAO; record sizes.
- Flash Dimmy's JC3248 and a SuperMini (allowed per the working agreement); check on a phone:
  `/`, `/panel` (Dashboard, Games, Config; no Themes tab), `/files`, the dongle's `/classic`.
- On the JC3248 screen: NEO on and off from Settings and from CONFIG.TXT; the THEME row hides and
  returns; the gradient redraws after every full-screen refresh; no visible slowdown in list
  scrolling.
- OMEGAWARE: the updated `webui.html` still shows its own themes and Themes tab.
- P4 builds are Mez's bench; step 1 does not change P4 code.

## Coordination

- Agree this spec with Mez's Claude over the relay before implementation (Mez made NEO).
- The relay shows claims by session `dimitri-gti-66f0` on `web_panel.h`, `webui.*` and
  `Gotek_JC3248.ino`; that session appears dead since the Claude Desktop restart of 6 Oct 13:44.
  Clear or confirm those claims before step 1 touches the files.
- The `webui.html` change is committed to both trees in the same change.

## Out of scope for step 1

Moving the app to `/`, merging `/files` into the app, the dongle clean-up, P4-style layout on the
JC3248/JC4827, the GitHub Pages site in `docs/`.
