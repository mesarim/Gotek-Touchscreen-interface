# Gotek docs: source-of-truth → doc-target map

This is the repo-specific knowledge the skill needs: where each fact is born
in the firmware, which documents repeat it, and the house style those
documents are written in. When the firmware and the docs disagree, **the
firmware wins** — it is what actually runs on the device.

There are two trees, both documented:
- **mesarim tree** (`Gotek-Touchscreen-interface`, Mez): sketches under
  `firmware/<Sketch>/`, docs under `docs/`.
- **OMEGAWARE tree** (Dimitri): sketches at the repo root
  (`Gotek_Touchscreen/`, `Gotek_Dongle/`), plus the shop flasher living in a
  separate repo (`retro-shop/public/tools/gotek-flasher/`).

The extractor (`scripts/extract_firmware_facts.py`) finds sketches by walking
for `*.ino`, so point it at whichever tree's sketch root.

## The facts the extractor emits (per sketch)

- `version` — the `FW_VERSION` / `DONGLE_VERSION` define (e.g. `5.9.2-JC3248`).
- `internal` — `FW_INTERNAL` build tag if present.
- `config_keys` — the **documented** keys, each `{key, default, doc}`, pulled
  from the self-documenting template table
  `{"KEY", "\n# explanation\nKEY=default\n"}`.
- `parsed_keys` — every key the firmware actually reads (`if(k=="KEY")`).
- `undocumented_keys` — **`parsed_keys` minus `config_keys`**. This is the
  doc-drift list: keys the firmware honours that the template never explains.
  A non-empty list here is the single most useful signal in the whole run —
  it is exactly the work a human forgets on every build.
- `themes`, `languages` — enumerated features that tend to grow build-to-build.

## Doc targets and what to sync into each

### mesarim `docs/index.html` — the download/landing page (highest churn)
Per board it holds `stable:{...}` and `beta:{...}` objects:
```js
beta:{man:'firmware/jc3248-beta/manifest.json',
      ver:'Firmware 5.9.1-beta — on-screen Home-WiFi setup, fast reel, ...',
      sd:'firmware/jc3248-beta/GTi-JC35-5.9.1-update.bin'}
```
**The page documents what the visitor will FLASH — the bin behind its
`manifest.json` — not the bleeding-edge source.** So there are two different
kinds of drift, and they must not be conflated:

1. **Page string ≠ its own manifest.** The `ver:` string and the `sd:` path's
   version segment must match the `version` in the `manifest.json` that same
   entry points at. When they disagree it is a plain documentation error —
   fix it directly to match the manifest. (The feature summary after the `—`
   should still name that build's headline capabilities.)
2. **Manifest/bin behind the source.** When a sketch's `version` is ahead of
   the manifest it ships (a release was not re-baked), **do not** rewrite the
   page to the source version — the bin really is the older one, and claiming
   otherwise makes the flasher lie. Instead report it: "these channels ship a
   bin older than source; re-bake needed" — that is a build step for the
   human, not a documentation edit. Only after the bin is re-baked and the
   manifest bumped does the page string follow.

In short: sync the page to the manifest; flag manifest-behind-source
separately. Never invent a version the downloadable bin can't back up.

### mesarim `docs/firmware/<variant>/manifest.json`
`name` (carries the version in parentheses) and `version`. Both track the
sketch `version`. There is already a stamping helper on Mez's side; prefer it
when present, otherwise edit the two fields directly.

### mesarim `docs/guide.html` / `docs/help.html`
The on-boarding tutorial and help. House style: friendly, retro-enthusiast,
concise British English, `<code>` around config keys and on-screen labels,
em-dashes for asides. When a build adds a user-facing feature (a new screen,
a new mode, a new config toggle), the guide gains or updates the step that
teaches it. Do NOT mechanically dump the config table here — this file
teaches, it does not enumerate.

### A generated config reference
Neither tree keeps a full, current `CONFIG.TXT` key reference — that gap is
half of Mez's pain. Generate/refresh one Markdown file (suggest
`docs/CONFIG-REFERENCE.md`) straight from `config_keys`: a table of key,
default, and explanation, ordered as the template orders them, with a note
listing any `undocumented_keys` so they get a real explanation added to the
firmware template (fixing the drift at its source, not papering over it).

### A generated API reference
Neither tree documents the endpoint surface at all, and that is the fact that
hurts most when it is wrong, because it is what somebody builds a client
against. Generate `docs/API-REFERENCE.md` with
`scripts/render_api_reference.py` from the `endpoints` block the extractor
emits: the HTTP route table per surface, the TCP escape opcodes, the UDP
beacons and the protocol-facing constants.

Three things make this reference different from the config one:

- **It needs the includes, not just the `.ino`.** The dongles register their
  routes in the sketch, but the panel's routes are *all* in
  `shared/web_panel.h` and none are in the `.ino`. `sketch_sources()` follows
  quoted `#include`s for exactly this reason.
- **Name each surface's checkout explicitly.** Both worktrees hold every
  sketch and the two branches disagree about the same sketch — one carries the
  ported dongle, the other the ported panel. The renderer therefore takes
  `--surface "Label=facts.json:Sketch"` per surface and stamps branch and
  commit into the document. Do not let it guess.
- **The refusals are derived, never remembered.** Which routes a claim closes
  comes from the lock gate the handler body calls (`webDenyLocked` /
  `webDenyLockedCfg` / `webWriteAllowed`), and mode refusals (`wpFleetOff`) get
  their own column because they answer a different question. This is the
  drift that already bit us: a route list given out by hand was wrong within
  days of the lock landing.

Drift signal: a route in the source that the table lacks, or a table row whose
gate column disagrees with the handler. Both mean re-run the pair rather than
editing the Markdown.

### OMEGAWARE side
`README.md` (WiFi/credentials/config examples), and the shop flasher
`retro-shop/public/tools/gotek-flasher/index.html` + `manifest-*.json`
(version strings + the on-page manual). The flasher already has
`update-firmware.sh <ver>` to stamp manifests — use it; then reconcile the
prose (feature list, portal login, troubleshooting) with the facts.

## House style, in one breath
Warm but not chatty; the reader is a hobbyist who likes their Amiga. Prefer
showing the exact key/label in `<code>` over describing it. Keep version and
feature claims verifiable against the extractor output — never invent a
capability the firmware doesn't have. Dutch (`LANG=NL`) and the other
languages are UI-only; docs stay in English unless a file is already bilingual.

## What only a human can make → flag it, do not fake it
The skill cannot photograph hardware or screenshot a running device. When a
build introduces something whose documentation would normally *show* it,
record a precise request in `DOCS-TODO-HUMAN.md` at the repo root instead of
skipping or approximating it. Each entry: what to capture, why (which new
feature it documents), and where it will be placed. Triggers include:
- a **new cracktro style / theme** → a screenshot of it running (the reader
  chooses themes by look, not name);
- a **new screen or mode** (e.g. a new CONFIG page, SD-access, a fleet card)
  → a screenshot of that screen;
- **new hardware** (a new board, a new dongle, a wiring detail like the
  speaker) → a photo;
- a **UI layout change** a text description can't carry → a before/after shot.
Everything text-shaped (config keys, version strings, feature lists, steps)
the skill writes itself; only genuinely visual assets go on the human list.
