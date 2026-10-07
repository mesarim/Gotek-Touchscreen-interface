# One theme section - Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** NEO on/off, the classic themes and custom themes in one THEME section on the JC3248 screen and in the shared web app's Themes tab, with the existing Theme Editor as the builder.

**Architecture:** Custom themes are the editor's style JSON in `/GTI/THEMES/<NAME>.json`; the sketch maps them to its 16 colour roles into one runtime `Theme` slot. `web_panel.h` gets the editor's `/api/themes/*` routes behind `GTI_THEMES`, calling sketch hooks. `webui.html` gets additive-only changes (editor visible on `theme_store`, a GTi screen preview on `"screen":"gti"`).

**Tech Stack:** Arduino ESP32 core 3.3.12, JC3248 sketch, shared `web_panel.h`, shared `webui.html` (+ `make_webui_header.py`).

**Spec:** `docs/superpowers/specs/2026-10-07-gti-theme-section-design.md`

## Global Constraints

- `NEO=` and `THEME=` keep their meaning; old cards (THEME= present, no NEO=) open with NEO off and self-heal writes `NEO=OFF`.
- DENISE / WRANGLER / RETRONAUT are never listed (self-heal, pick page, web UI).
- `webui.html`: additive only; OMEGAWARE behaviour unchanged when `screen`/`theme_store` are absent.
- Sketches without `GTI_THEMES` build exactly as before.
- Record `.bin` sizes before/after: JC3248, JC4827, SuperMini/Zero/XIAO Webby. Budget JC4827 < 6 KB.
- Build into scratch; restore committed `build/`. No mid-line `//` comments in sketch edits.

## Review Focus

1. A custom JSON with missing/garbage colours: must fall back per field, never crash or leave black-on-black.
2. `THEME=<custom>` whose file was deleted: NAVY + one log line, Settings still opens.
3. Activating a theme from the web while the Settings/THEME page is on screen: screen redraws (g_ui_gen).
4. Name validation on POST style (path traversal, length, reserved names NEO/NAVY.../CUSTOM).
5. OMEGAWARE dongle with the new webui.html: Themes tab + editor unchanged.

---

### Task 1: Screen side (JC3248)

**Files:** Modify `firmware/Gotek_JC3248/Gotek_JC3248.ino`

- Runtime custom slot: `g_custom` (Theme) + `g_custom_name`; `g_theme_idx==CUSTOM_IDX` (= NUM_THEMES) selects it.
- `themeFromStyle(json)`: parse `fill/light/dark/text/accent` (#RRGGBB), per-field fallback to NAVY-derived style, mapping table from the spec; `mix565(a,b,pct)`.
- `loadCustomTheme(name)`: read `/GTI/THEMES/<NAME>.json`; false on missing/bad.
- `applyTheme` handles CUSTOM_IDX; `themeName()` returns the current name.
- `listCustomThemes()`: up to 8 names from `/GTI/THEMES/*.json`, sorted.
- loadConfig: `THEME=` classic name/number or custom name; track whether `NEO=` was seen; old-card rule.
- selfHeal: if no `NEO=` line and a `THEME=` line exists, append the NEO block with `NEO=OFF`.
- Settings: drop the NEO row; one `THEME: <current>` row always shown.
- THEME page: `NEO: ON/OFF` key, classics, customs; pick = NEO off + THEME=name.
- FW_VERSION `A600-theme1-JC3248`.

Verify: compile; flash; screenshots of Settings + THEME page; CONFIG.TXT cases via SD API.

### Task 2: Web API (`web_panel.h`)

- Under `#if defined(GTI_THEMES)`: `/api/themes/list`, `/api/themes/font` (font6x8 base64), `/api/themes/geometry` (`{"buttons":[]}`), `/api/themes/*` (style GET/POST, activate POST) via `onNotFound` prefix dispatch or regex-free uri parsing.
- sysinfo: real `theme`, `has_themes`, `theme_store`, `screen:"gti"` when GTI_THEMES.
- Activate sets a pending flag served from loop (redraw on screen), `g_ui_gen` aware.

Verify: curl every route on the device; bad names rejected.

### Task 3: Web app (`webui.html` + `webui.h`)

- Editor card visible when `has_sd || theme_store`.
- GTi preview (canvas 480x320 scaled) under the button preview when `screen==='gti'`; JS mapping identical to Task 1.
- Empty geometry: teSave skips uploads (already loops over 0), teRenderPreview copes with `teGeom=[]`.
- Regenerate `firmware/shared/webui.h`.

Verify: Playwright on the device page: Themes tab, editor, save + activate; screenshot vs `/api/screenshot`.

### Task 4: Sizes, device test, push, tell Mez

- Sizes before (neo-style ebeedcf) / after for the five sketches.
- Flash JC3248, run the Review Focus cases, push `gti-themes`, merge into `preview-neo-shot`, relay summary to Mez.
