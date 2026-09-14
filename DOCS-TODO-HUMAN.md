# Docs — human-only capture list

_Maintained by the `gotek-docs` skill. These are the documentation assets the skill **cannot** make — screenshots of a running device, photos of hardware. Everything text-shaped (config keys, versions, feature blurbs) the skill writes itself; only genuinely visual items land here. Delete an entry once its asset is shot and placed._

## JC3248 / JC4827 — 5.9.7

- **OMEGA theme (`THEME=6`, the house default)** — a screenshot of the library running the OMEGA theme. Why: it's now the default reskin and the 7th theme; people pick themes by look, not name. Where: theme gallery in `docs/guide.html` / the download-page feature blurb.
- **`CRACKTRO=7` "OMEGAWARE" boot intro** — a screenshot (or short clip still) of the 1991 OMEGAWARE cracktro. Why: new cracktro style; the reader chooses cracktros by look. Where: alongside the other cracktro examples in the guide.
- **Web panel (`WEBUI=ON`)** — a screenshot of the shared web page served from the panel over home WiFi (the dashboard + upload/OTA view). Why: brand-new user-facing surface with no visual in the docs. Where: a "browse to your GTi" section in `docs/guide.html`.

## Fleet console + WiFi owner-lock (branches `panel-fleet-alwaysweb` / `webby-lock-161`)

- **The Fleet manager screen** — a photo or screenshot of `INFO → FLEET` with two or three
  dongles listed, at least one row checked and one row showing its **CLAIM** button. Why: it is
  the whole multi-select "play this on several Amigas" idea in one picture, and the claim lives
  on the row rather than in a pop-up — words make that sound more complicated than it looks.
  Where: a fleet section in `docs/guide.html`, and next to the claim steps in `docs/PAIRING.md`.
- **A refused fling** — the screen's "locked to another screen" message. Why: it is the one
  moment a user meets the lock, and seeing the exact wording saves a support question.
  Where: `docs/PAIRING.md`, Troubleshooting.
- **`INFO → SAVED WIFI`** — the remembered-networks list with the network you are on marked and a
  **FORGET** button visible. Why: new screen; explains the club-day/home/friend's-house story at
  a glance. Where: `docs/guide.html`, WiFi section.
- **The dongle's "AP security" card** — the portal card where you set a custom AP name and
  password. Why: new user-facing surface on the dongle's own page. Where: `docs/guide.html`.
## Flasher board cards (still SVG placeholders)

- **JC4827 (4.3″)**, **P4 (JC4880)**, **CYD** — a plain-background board photo each, dropped in `docs/img/` as `jc4827.jpg` / `p4.jpg` / `cyd.jpg`. Why: the other cards show real boards; these fall back to the schematic SVG. (JC3248, Super Mini, XIAO, Waveshare 7/7B already have photos.)
