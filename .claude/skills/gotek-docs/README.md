# gotek-docs — a skill that keeps the GTi docs in sync with the firmware

Made by Dimmy's side (OMEGAWARE) to kill the "every build I redo the
documentation" grind. It reads the firmware source as the source of truth and
rebuilds the text-shaped docs from it — versions, feature summaries, the
CONFIG.TXT reference — in the site's house style, and hands back a short list
of the genuinely visual things only you can make (screenshots, photos).

## What it does, concretely

- Extracts, per sketch: `FW_VERSION`, every documented CONFIG key (name +
  default + explanation), every key the firmware actually reads, the themes,
  and the languages.
- Flags **doc drift** automatically — e.g. it caught that the `CRACKTRO`
  template still said `8=WRANGLER` (no `OMEGA=9`), that `LANG` listed 5
  languages (no `NL`), and 14 keys the JC3248 reads but never documents.
- Distinguishes a **page-vs-manifest** error (it fixes that directly) from a
  **bin-behind-source** release gap (it reports that as a re-bake to-do, never
  fakes the version on the flasher page).
- Generates a full `CONFIG.TXT` reference (Markdown or an HTML page in your
  dark house-style — coral/copper, Share Tech Mono).
- Writes `DOCS-TODO-HUMAN.md` (screenshots/photos to take) and, when relevant,
  a release-drift report.

## Install — pick one

**A. As a project skill in your repo (recommended — zero install, your Claude
auto-discovers it whenever you work in the repo).** Unzip the `.skill` into
`.claude/skills/` so you get:
```
.claude/skills/gotek-docs/SKILL.md
.claude/skills/gotek-docs/references/doc-map.md
.claude/skills/gotek-docs/scripts/extract_firmware_facts.py
```
(A `.skill` file is just a zip — `unzip gotek-docs.skill -d .claude/skills/`.)
Commit it and it travels with the repo.

**B. Into your Claude profile.** Open `gotek-docs.skill` with Claude and click
**Save skill** (if your org allows skill creation). Then it's available in any
session.

## Use it

Just say it in a session that has the repo open: *"update the docs for this
build"*, *"the config reference is stale"*, or *"regenerate the guide"* — the
skill triggers on those. Or run the extractor by hand to see the facts:
```
python .claude/skills/gotek-docs/scripts/extract_firmware_facts.py firmware --out facts.json
```

## One heads-up for our shared files
`references/doc-map.md` knows both trees. When a change lands in a file that's
a byte-identical shared twin between your tree and Dimmy's, the skill flags it
so the same edit reaches both — it won't silently touch only one side.
