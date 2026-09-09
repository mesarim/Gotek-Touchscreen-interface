---
name: gotek-docs
description: >-
  Keep the Gotek Touchscreen Interface firmware documentation in sync with the
  firmware itself, in the project's house style. Use this whenever a Gotek
  build ships or a feature changes and the docs need to follow — the download
  page (docs/index.html), the manifests, the guide/help pages, the CONFIG.TXT
  key reference, the README, and the shop flasher page. Trigger it when the
  user says the docs are stale, "every build I redo the documentation", "update
  the docs for this release", "regenerate the guide", "did the config reference
  change", or after any version bump / new config key / new theme / new
  cracktro / new mode. It reads the firmware source as the source of truth,
  finds where the docs have drifted, rewrites the text-shaped parts itself, and
  flags anything only a human can make (screenshots of new screens, photos of
  new hardware) so nothing visual is faked or silently skipped.
---

# Gotek documentation sync

Mez maintains the Gotek docs by hand, and every build it drifts: version
numbers, feature lists, the CONFIG.TXT key reference, the guide. That is slow
and it goes wrong. This skill makes the **firmware the source of truth** and
rebuilds the text-shaped documentation from it — so a build's docs become a
review, not a rewrite — while sending the genuinely visual work (screenshots,
photos) back to the human as a precise checklist.

Read `references/doc-map.md` before doing anything: it maps every fact to the
files that repeat it, and states the house style. It is short and it is the
repo-specific part you cannot guess.

## The loop

### 1. Extract the facts
Run the extractor against the tree you are documenting (it finds sketches by
walking for `*.ino`):

```
python scripts/extract_firmware_facts.py <firmware_root> --out facts.json
```

`<firmware_root>` is `firmware/` in the mesarim tree, or the repo root in the
OMEGAWARE tree. The result carries, per sketch: `version`, the documented
`config_keys` (key + default + explanation), the `parsed_keys` the firmware
truly reads, the **`undocumented_keys`** drift list, plus `themes` and
`languages`. Read it before touching a doc — it is the ground truth every
edit is checked against.

### 2. Find the drift
For each doc target in `references/doc-map.md`, compare what it currently
claims against the facts. The things that reliably rot:
- **Versions** — the number in `docs/index.html` `ver:` strings, in each
  `manifest.json`, and in every `-update.bin` path segment.
- **The config reference** — keys the firmware now reads that the docs never
  explain (that is `undocumented_keys`; a non-empty list is real work).
- **Feature summaries** — the `—`-separated blurb on the download page, and
  the guide's steps, when a build added a mode / theme / cracktro / toggle.

Prefer any stamping helper the repo already ships (e.g.
`update-firmware.sh <ver>` on the flasher side) over hand-editing the same
numbers — those exist precisely so the version lives in one place.

### 3. Rewrite the text-shaped docs, in house style
Update the drifted parts. Match the surrounding tone (warm, hobbyist, British
English, `<code>` around keys and labels — see the doc-map). Two rules that
keep this trustworthy:
- **Never claim a capability the extractor doesn't show.** Version strings and
  feature lists must be checkable against `facts.json`. If unsure whether a
  build really added something, look at what changed in the source, don't
  guess.
- **Fix drift at its source when you can.** An `undocumented_key` is best
  fixed by adding its `#` explanation to the firmware's CONFIG template (so it
  self-documents next time), not only by describing it in a side file. Surface
  these to the user; propose the template line.

For a full, current key reference, generate `CONFIG-REFERENCE.md` from
`config_keys` (a table of key / default / meaning, in template order). This
is the single doc that most repays being generated rather than typed.

### 4. Flag what only a human can make
The skill cannot screenshot a running device or photograph hardware. When a
build adds something whose documentation should *show* it — a new theme or
cracktro (people pick these by look), a new screen or mode, new hardware, a
layout change words can't carry — append a precise entry to
`DOCS-TODO-HUMAN.md` at the repo root instead of skipping it or approximating
it in prose. Each entry says **what** to capture, **why** (which feature it
documents), and **where** it will go. Keep this list honest and short: only
truly visual assets belong on it; everything text-shaped you write yourself.

### 5. Hand back a summary
Tell the user, in a few lines: which versions/feature strings you synced,
which config keys were undocumented (and the template lines you propose),
which docs you rewrote, and what is now waiting on them in
`DOCS-TODO-HUMAN.md`. That summary is the whole point — it turns "redo the
docs" into "approve these diffs and take these four screenshots".

## Notes
- The extractor only reads text; it compiles nothing and is safe to run on
  every build, including from CI.
- This works on both trees. When a change touches a file that is a
  byte-identical shared twin between the trees (see the project's shared-module
  rule), the same edit has to land in both — flag it rather than editing one.
- Don't over-reach: the guide teaches, it doesn't enumerate; the generated
  reference enumerates, it doesn't teach. Keep each doing its job.
