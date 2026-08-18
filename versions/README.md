# GTi — Version Archive (evolution of the product)

The complete history of the Gotek Touchscreen Interface firmware: the full
`.ino` source for **every version** of every board, plus the SD-update binaries
we retained. Nothing here is needed to *build* the current firmware — it's the
product's memory, kept so any past release can be read, diffed, or re-flashed.

Each board folder has: `src/` (one `.ino` per version), `bin/` (retained update
binaries), and `CHANGELOG.md` (the dated timeline with links).

| Board | Versions | Span | Binaries kept |
|---|---|---|---|
| [Guition JC3248 (3.5")](jc3248/CHANGELOG.md) | 31 | 2026-06-17 → 2026-08-14 | 7 |
| [Guition JC4827W543 (4.3")](jc4827/CHANGELOG.md) | 15 | 2026-08-03 → 2026-08-14 | 6 |
| [Waveshare 7" (7A, 800×480)](waveshare7/CHANGELOG.md) | 9 | 2026-08-05 → 2026-08-14 | 2 |
| [Waveshare 7B (1024×600)](waveshare7b/CHANGELOG.md) | 8 | 2026-08-05 → 2026-08-14 | 2 |
| [Super Mini dongle](supermini/CHANGELOG.md) | 2 | 2026-08-07 → 2026-08-13 | 0 |
| [XIAO dongle](xiao/CHANGELOG.md) | 3 | 2026-08-04 → 2026-08-13 | 0 |

## Two ways in
* **git tags** — `tags.sh` recreates all 68 versions as tags. Once pushed
  (`git push origin --tags`), every release is a one-click download in GitHub's
  **Tags/Releases** view, with a full diff between any two.
* **the folders** — just open `versions/<board>/` and read or download any
  version's source and binary directly, no git needed.

## Going forward
Tag every release at build time (`git tag <version>`) so source and binary always
pair cleanly — this archive is the one-time backfill of everything before that habit.
