# Handover: the `shared-webdav-client` and `shared-webui` branches

**Audience:** whoever works on these branches next — explicitly including the
Claude working with Mez. Written by the Claude working with Dimitri
(OMEGAWARE tree, `dimitrihilverda/Gotek-Touchscreen-interface`), 2026-09-01.

Everything below was verified on real hardware before being pushed: a Guition
JC3248W535C on an Amiga 500 at Dimitri's bench, running these exact commits.

---

## The one rule that matters most

**Files in `firmware/shared/` are byte-identical to their twins in the
OMEGAWARE tree** (`Gotek_Touchscreen/` there). That is the entire point of the
folder: a fix travels whole, to every board of both products, or the trees
drift back into the seven-near-identical-sketches problem.

Practically, for you: **do not edit a `firmware/shared/` file in this tree
only.** If review demands a change in one of them, make it here, but flag it
so the same commit lands in the OMEGAWARE tree — or ask Dimitri's side to make
it and sync. The OMEGAWARE tree also carries the host test suite for these
files (`tools/test/`, 325 assertions, runs on plain g++ with stubs); a change
to `webdav_client.h` or `multipart_scan.h` should pass it.

`web_panel.h` is the exception: it exists only in this tree (it adapts the
shared UI to *this* firmware's structures), so it is yours to evolve freely.

---

## What the branches contain

### `shared-webdav-client` (Step 1 — as agreed with Mez)

- `firmware/shared/webdav_client.h` — the WebDAV client. HTTPS/HTTP, PROPFIND
  with a streaming parser (tested against a 688 KB response), GET into a
  caller buffer, connection pooling with idle release.
- Wiring in both panel sketches: `DAV_*` CONFIG.TXT keys, `davApplyConfig()`,
  and `doLoadWebdav(remotePath, showName)` beside the existing loader.
- `DAV_TEST=<remote path>` — a boot-time smoke hook so review needs no UI:
  fetches that file once at boot and mounts it. Remove key or hook once real
  UI exists.

**Contract of the client** (its only coupling to any sketch):

```cpp
DavConfig c;                     // host, port, https, user, pass, basePath, enabled
davClient.configure(c, logFn);   // logFn: void(*)(const String&) — nullptr is fine
```

It reads no globals. Include order does not matter. `configure()` normalises a
pasted URL down to a bare host, and drops the pooled connection (settings
changed = old server).

### `shared-webui` (Step 2 — working preview, review of Step 1 still leads)

- `firmware/shared/webui.h` — the gzipped single-page UI every OMEGAWARE
  device serves (generated from `webui.html` there; do not hand-edit).
- `firmware/shared/multipart_scan.h` — binary-safe multipart scanner
  (Arduino `String::indexOf` stops at NUL bytes; an ADF is full of them).
- `firmware/shared/web_panel.h` — serves the page and answers its API mapped
  onto this firmware: upload into the RAM disk, WebDAV browse + insert,
  firmware OTA, dashboard. Activation: `WEBUI=ON` + `HOME_SSID` (or alias).

---

## Rules already load-bearing in the code — please do not refactor them away

1. **A disk load is queued and executed from `loop()`, never inside the HTTP
   handler.** (Mez's rule, and it is real: a synchronous handler serves
   nothing for the whole transfer — including the endpoint you would use to
   debug it.) See `g_webPendingDav` / `webPanelService()`.

2. **`webPanelService()` runs once per screensaver frame.** All three savers
   are blocking `while(true)` loops; without this call the web server dies
   the moment a saver starts. Found on the bench within minutes of first use.

3. **HTTP 408 on a reused pooled connection is treated as a stale pool and
   retried once, fresh.** A proxy (HAProxy at Dimitri's) answers 408 when a
   request lands on a keep-alive socket it was tearing down. Not a server
   error; do not surface it.

4. **`davClient.dropIdle()` must keep being called from the service loop.** A
   parked TLS connection pins ~50 KB of *internal* heap; on the OMEGAWARE
   panel the heap-minimum once hit literally 0K with the pool warm.

5. **`_release()`/`_destroy()` in the client:** `_release` once recursed into
   itself and blew the stack at the end of any transfer whose connection the
   server closed — every board, intermittently, PANIC or watchdog depending
   on where the stack landed. And `Client` has no virtual destructor, so
   connections are deleted through the concrete type. Both are fixed; both
   are the kind of thing a tidy-up could silently reintroduce.

6. **OTA refuses anything whose first byte is not 0xE9**, and streams into
   the inactive slot of this tree's own `partitions.csv` (which already has
   two 6.9 MB slots — BUILDING.md's "Huge APP No OTA" note predates it).

7. **Errors must reach the panel screen, not only Serial.** The serial port
   is plugged into a Gotek; a Serial-only error is a message to nobody. See
   `g_dav_fail` and the DAV_TEST result lines.

## CONFIG.TXT semantics the branches introduced

- **Aliases, both read forever:** `DAV=ON` ⇔ `DAV_ENABLED=1`, and
  `HOME_SSID`/`HOME_PASS` ⇔ `WIFI_CLIENT_SSID`/`WIFI_CLIENT_PASS`. Cards
  travel between the two firmwares; this was proven within hours when
  Dimitri's card did exactly that, twice.
- **An empty value never erases what another alias already set.** This tree's
  own template-append writes `HOME_SSID=` (empty); without this rule that
  line would neutralise a `WIFI_CLIENT_SSID` set higher up.

---

## Verified on hardware (2026-09-01, JC3248 on an Amiga 500)

| What | Proof |
|---|---|
| Step 1 fetch | 880 KB ADF over HTTPS from Nextcloud-style server, mounted, Gotek read it |
| Step 2 insert via page | browsed, queued insert, mounted — page stayed responsive |
| Web OTA through this firmware | three consecutive updates flashed through `/api/system/ota` |
| Saver fix | matrix saver running while the page kept serving |
| 408 fix | Game Library that intermittently errored now loads first try |

Both sketches build with the BUILDING.md settings
(`FlashSize=16M,PSRAM=opi,PartitionScheme=huge_app,USBMode=default,CDCOnBoot=default`;
JC4827 additionally needs the `bb_captouch` library). Core: arduino-esp32
3.3.11 on both sides.

---

## Deliberately NOT in these branches — the review decisions

These are Mez's calls, over real code, not gaps to silently fill:

1. **Canonical CONFIG.TXT names.** Suggestion on the table: this tree's names
   become canonical (consistent with tree-as-base), OMEGAWARE's stay as
   read-aliases forever.
2. **The SD-library / themes / cover-art web surface.** The page is told
   `has_sd:false` until the endpoint shapes for categories, favourites and
   covers are pinned. Guessing them is how trees drift.
3. **Config rewriters must preserve keys they don't understand.** Both
   firmwares currently violate this (this tree appends politely; OMEGAWARE's
   rewrites and drops). It cost the bench two card-edits in one day.
4. **WiFi + ESP-NOW coexistence.** Everything here declines to run in
   wireless dongle mode. Parked by Mez, still parked.
5. **`downloadFile()`** (the client's SD-download path) has no stale/408
   retry loop yet; listings and buffer streams do.
6. **Single-loop stutter:** during an 880 KB fetch the saver/UI freezes
   briefly and vice versa. Known, accepted for now; chunking the fetch
   through the service loop is the obvious refinement if it bothers anyone.

---

## How to reproduce the bench demo in ten minutes

CONFIG.TXT on the card:

```
DAV=ON                      (or DAV_ENABLED=1)
DAV_HOST=your.webdav.host
DAV_PORT=443
DAV_HTTPS=ON
DAV_USER=...
DAV_PASS=...
DAV_PATH=/remote.php/webdav/
HOME_SSID=YourWiFi          (or WIFI_CLIENT_SSID=...)
HOME_PASS=...               (or WIFI_CLIENT_PASS=...)
WEBUI=ON
DAV_TEST=/SomeGame/SomeGame.adf
```

Boot in STANDALONE mode. Watch: cracktro → `DAV_TEST:` line with an on-screen
success/failure verdict → firmware joins your WiFi → `http://gotek.local`
serves the interface. Every later firmware goes on via the page's Firmware
card; the cable retires after this one flash.

## Feature inventory: what this tree has and the web surface could carry

Requested by Dimitri: a catalogue of mesarim-tree features OMEGAWARE never
had, with where each stands on the shared web UI.

| Feature | State on the web surface |
|---|---|
| Panel behaviour options (carousel, screensaver+modes, language, font, rotation, compact, button style, tap/hot/force-swap, categories, nesting, cracktro) | **Exposed now** — "Panel Options" section on the Config page, persisted via saveConfigKey, applied at next boot, with a Reboot button |
| HIVEMIND + CAP (FLING fan-out to every paired dongle) | **Exposed now** as the two config keys; the *act* of flinging is not |
| Wireless dongle fleet: pairing, per-dongle friendly names, FLING to one/all, LINK=HOMEWIFI routing, wireless save writeback | **Not exposed — candidate endpoints.** A "Dongles" card (list paired MuCa dongles via the enumerator, name them, fling the current disk to one/all) is the most user-visible thing the web UI could gain next. Needs the ESP-NOW coexistence question answered first, since the web server holds STA. |
| OTA-from-SD (doFirmwareUpdate) | Untouched; complements the web OTA for offline use |
| SD-access boot (card served to a PC over USB) | Untouched; a web button to reboot into it would be trivial *and* is a footgun (it never returns) — review call |
| Amiga Test Kit diagnostic ADF | Untouched; a one-tap "load diagnostics" web button is cheap if wanted |
| Favourites + play statistics | Not exposed; belongs to the library-surface conversation |
| GEN mode (generic/any-machine library) | Mode is reported read-only in system info; switching modes over the web belongs to the library surface |

The dividing line used: **settings that already live in CONFIG.TXT are
exposed as settings; actions and library semantics wait for the surface
review.** Config keys are already this tree's public interface — mirroring
them over the web invents nothing.

## Config page mechanics (changed with the Panel Options work)

The Config page now hides every section whose probe keys are absent from
`/api/config`, and disables the hidden fields so Save never posts empty
values into settings a device never showed. Consequence: **GET /api/config IS
the config UI.** A device adds a section by answering with its keys — nobody
edits the page per device. This tree's GET deliberately answers with client
WiFi + DAV + the panel options above, nothing borrowed.

— written 2026-09-01; the commit messages on both branches carry the longer
versions of every story above.
