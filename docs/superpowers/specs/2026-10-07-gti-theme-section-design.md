# One theme section: NEO, the classic themes and your own

Date: 7 Oct 2026. Author: Dimmy (with Claude). Status: agreed with Mez over the relay (#91-#103);
for review by Dimmy, then Mez.

## Why

NEO is the default look of GTi (spec `2026-10-06-neo-web-style-design.md`). People must still be
able to use the classic colour themes (NAVY ... OMEGA), and Mez wants a way to make your own
theme. OMEGAWARE already has a Themes tab with a Theme Editor in the shared web app
(`firmware/shared/webui.html`); GTi reuses it instead of getting a separate builder.

## Decisions (Mez, relay #93 and #103)

1. **Everything theme-related lives in one THEME section**, on the screen and on the web: the NEO
   on/off, the classic themes and the custom themes. No loose theme rows elsewhere in Settings.
2. **NEO stays an on/off** (`NEO=ON|OFF`, default ON), now inside the theme section.
   `THEME=` names the colour theme used when NEO is off: a classic one or a custom one.
3. **One theme format for OMEGAWARE and GTi:** the Theme Editor's existing style JSON
   (`fill, light, dark, text, accent, bevel, bevelW, radius, scale`). GTi derives its 16 screen
   colours from it (below). No separate `COLORS=` line.
4. **The shared Themes tab + Theme Editor is the theme builder**, served by the GTi panel. A
   standalone copy on the docs site may follow later from the same code; not in this step.
5. Order after this: JC4827 NEO port, then web 1a, then 1b (Mez, #93).

## Screen (JC3248 first)

- Settings shows one row, `THEME: <current>`, where `<current>` is `NEO` when NEO is on, else the
  theme name. The separate `NEO: ON/OFF` row goes.
- The row opens the existing pick page (Mez, 069d504) as the THEME page:
  - first a key `NEO: ON` / `NEO: OFF` (green edge when on); tapping it switches live, as today;
  - then one key per theme: NAVY, EMBER, MATRIX, PAPER, SYNTH, GOLD, OMEGA, then the custom themes
    found in `/GTI/THEMES/` (max 8, sorted by name), each drawn in its own colours (as 069d504);
  - tapping a theme sets `THEME=<name>`, switches NEO off, applies live and returns to Settings;
  - the current choice is marked as today (`> <`).
- `CONFIG.TXT`: `NEO=` and `THEME=` keep their meaning, so every existing card works unchanged.
  `THEME=` accepts a classic name, a number 0..6, or a custom name. A custom name whose file is
  missing or broken falls back to NAVY and logs one line. The firmware writes names, not numbers.
- Old cards (Mez, #104: keep what is on the card): a CONFIG.TXT with a `THEME=` line but no
  `NEO=` line comes from before NEO, so it opens with NEO off and its own theme (`THEME=0` stays
  NAVY). The self-heal then appends the documented `NEO=` line with the value in use (`NEO=OFF`
  there), so the user can see and change it. A card with neither line, or a new card from the
  template, gets `NEO=ON`.
- The special cracktros DENISE, WRANGLER and RETRONAUT stay undocumented (Mez, #104): nothing in
  the theme section, the self-heal or the web UI lists them.

## Custom theme to screen colours

A custom theme is the editor's style JSON in `/GTI/THEMES/<NAME>.json` (NAME: `A-Z 0-9 _`, max
16, as the editor already enforces). The firmware maps it to the 16 roles of `struct Theme`:

| Role | From |
|---|---|
| bg | dark |
| bar | dark mixed 25% toward black |
| panel | fill |
| sel | fill mixed 30% toward accent |
| sep | light |
| dim | text mixed 45% toward fill |
| mid | text mixed 25% toward fill |
| lit | text |
| amber | accent |
| accent (slate keys) | fill mixed 35% toward light |
| circ | fill mixed 20% toward light |
| circ_text | text |
| now | dark mixed 20% toward accent |
| green, orange, blue | fixed, as NAVY (state colours: on, warning, INSERT) |

`bevel`, `bevelW`, `radius` and `scale` are stored and shown in the editor but not used by the
GTi screen in this step (its key shapes follow NEO / BUTTONS). The same mapping is written once
in C (firmware) and once in JS (editor preview); both are in this spec so they cannot drift.

## Web (served by the GTi panel, `web_panel.h`)

`web_panel.h` is shared by JC3248, JC4827 and the P4 sketches. The theme API is compiled only when
the sketch defines `GTI_THEMES` and provides four hooks (list, read, save, activate); other
sketches build exactly as before and keep `"has_themes":false`.

- `/api/system/info`: `"has_themes":true`, `"theme":"<current>"` (`NEO` or the theme name), and
  `"theme_store":true` (the editor may save, see below), `"screen":"gti"` (shows the GTi preview).
- `/api/themes/list` -> `{"themes":["NEO","NAVY",...,"OMEGA",<custom>...],"active":"<current>"}`.
- `/api/themes/<name>/activate` (POST): `NEO` -> NEO on; any other -> NEO off + `THEME=<name>`;
  applied live on the screen.
- `/api/themes/<name>/style` GET: the style JSON. For NEO and the classics it is derived back from
  their colours (fill=panel, light=sep, dark=bg, text=lit, accent=amber), so they can be a
  starting point in the editor. POST (custom names only): validated, written to
  `/GTI/THEMES/<NAME>.json`.
- `/api/themes/geometry` -> `{"buttons":[]}`: the GTi screen draws its own keys, so the editor
  uploads no button pictures. `/api/themes/font` -> the screen's 6x8 font (as the dongle sends).

`webui.html` changes are additive only, so the file stays identical in the OMEGAWARE tree and
nothing changes for OMEGAWARE:

- The Theme Editor card is shown when `has_sd` **or** `theme_store` is true (the GTi panel reports
  `has_sd:false` because its file manager lives at `/files`).
- With `"screen":"gti"`: a second preview under the button preview: a 480x320 mock of the GTi
  main screen (list rows with the selected row, game panel, bottom keys) in the colours from the
  mapping above. Without it nothing new is shown.

## Constraints

- **Flash:** record the `.bin` size before and after for JC3248, JC4827 and the three Webby
  sketches (Mez, #103). The Webby sketches keep their own `webui.h` copies and are not regenerated
  in this step, so they should not change; the JC4827 gets the new `webui.h` through
  `web_panel.h` and must stay well inside its ~70 KB headroom (budget: under 6 KB for both the
  preview and the API).
- Toolchain: core 3.3.12, JPEGDEC 1.8.4, PNGdec 1.1.6 (Mez/Dimmy compare, #95-#101).
- Build into a scratch output directory; the committed `build/` binaries are not replaced.

## Testing

- JC3248 on Dimmy's desk: the THEME page (NEO on/off, every classic theme, a custom theme), live
  switching, CONFIG.TXT with old and new keys, a broken custom JSON (falls back to NAVY).
- Web on a phone: Themes tab lists NEO + classics + custom; activate each; save a new theme from
  the editor; the screen follows; `/api/screenshot` of the screen next to the editor's GTi preview
  shows the same colours.
- OMEGAWARE dongle with the new `webui.html`: Themes tab and editor unchanged.
- Flash sizes before/after as above.

## Out of scope

The JC4827 and P4 screens (they get the same model when NEO is ported there), the standalone
docs-site builder, using `bevel`/`radius` on the GTi screen, the web pages' own NEO look (1a).
