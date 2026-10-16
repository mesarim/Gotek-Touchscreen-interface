# Webby: pull firmware from GitHub, and the dongle MODE / fallback

Proposal from Dimmy's session, 16 Oct 2026, for Mez to decide. Based on `webby-ota-guard` @31df90f
(Webby-1.6.14). Nothing here is built yet except what is marked **exists**.

## Part B - the screen pulls the right dongle firmware from GitHub

### What exists

- Dongle: `POST /api/system/ota` (multipart), owner-only when locked. With 1.6.14 it refuses another chip, flash
  size or board kind: `400 wrong_image` (readable `err`), `403 locked`, `500 update_failed`, `200 ok:true`.
- Board kind on the wire: pad[2] of PAIR_REPLY/hello and `board_id` in `/api/system/info` and `/api/status`:
  1 SuperMini, 2 Zero, 3 XIAO (reserved, XIAO is not guarded yet).
- `docs/firmware/versions.json` is read only by `docs/index.html` (the web flasher). Webby entries point at
  full merged images (`"full": ".../manifest-*.json"`). The screen cannot use those: OTA takes the **app** image.
  There are no `size` or `sha256` fields anywhere.

### versions.json: fields per Webby build

Add to each OTA-able build entry (device keys `supermini`, `zero`, `xiao` stay as they are):

```json
{
  "v": "Webby-1.6.14-supermini",
  "ch": "stable",
  "date": "2026-10-16",
  "full": "firmware/supermini-beta/manifest-1.6.14-supermini.json",
  "app": "firmware/supermini-beta/Webby-1.6.14-supermini-update.bin",
  "size": 1177136,
  "sha256": "<64 hex>",
  "board_id": 1,
  "guard": true,
  "note": "..."
}
```

- `app` = the app image (what `Gotek_*_Webby.ino.bin` is; byte-identical to the app region of the merged bin).
- `size`, `sha256` = of that file. One small script (`tools/` or part of the release build) writes the three
  fields, so nobody types a hash by hand.
- `board_id` = the kind the entry is for. The screen only offers entries whose `board_id` equals what the dongle
  reported. Put it once per device key if you prefer; per entry makes a mismatched copy-paste visible.
- `guard: true` = the image carries the `GTIKIND:<board>;` marker, so a 1.6.14+ dongle accepts it. Entries
  without `app` or without `guard` are shown as "needs USB" (below 1.6.14 there is no OTA path: a pre-guard
  image has no marker and is refused at the end of the upload).
- The screen takes the newest entry with `ch == "stable"` (or any, if the user picks beta), matching `board_id`,
  `guard`, `app`. "Update available" = that entry's `v` differs from the dongle's `fw`.

### URL and trust

- Base: `https://mesarim.github.io/Gotek-Touchscreen-interface/` (docs/ is the Pages root, same origin as
  `versions.json`). Fetch `firmware/versions.json`, then `<base><app>`.
- TLS with the ESP32 core certificate bundle (no `setInsecure`). The sha256 comes from the same origin, so it
  guards against corruption and truncation, not against a compromised repo; that trust is the repo's, same as
  the web flasher.
- Before pushing, the screen checks the downloaded file itself: size == `size`, sha256 == `sha256`, byte0 0xE9,
  chip id @12 == 0x0009, flash-size nibble matches the dongle (it knows `board_id`), and the `GTIKIND:` marker
  matches the board name. The dongle repeats this anyway; the point is a clear message before any push.

### Flow (SD first, as the S9 pattern)

1. Screen on home WiFi, sees "update available" for a dongle (by `fw` vs versions.json).
2. Download to `/FIRMWARE/<v>.bin.part` on the SD card while hashing; on match rename to `<v>.bin`. The same
   file serves every dongle of that kind (fleet), and survives offline. No resume needed (1.2 MB).
3. Push:
   - **Dongle in home-WiFi mode** (`mode:"wifi"`): it is on the same LAN and, with no AP of its own, has no lock
     gate (`lockActive()` needs `g_webmode == 0`). The screen stays on home WiFi and POSTs to the dongle's IP.
     No switching at all.
   - **Dongle in ESP-NOW/AP mode:** the screen has to be on the dongle AP as an owner station (this is what
     `webOwnerOk` checks), so: download first, then join the AP, then POST, then rejoin home.
4. Result: `200` then the connection drops (reboot, AP/LAN back after ~10 s). The screen polls
   `/api/system/info` for up to 40 s and requires `fw` == the pushed `v` and the same `board_id`. `400` shows
   `err` verbatim; `403` shows "this dongle belongs to another screen"; `500` shows `err` and keeps the old image.
5. Safety: the inactive slot only becomes bootable at `Update.end()`, so every refusal leaves the dongle as it was.
   A new image that crashes on boot is **not** rolled back (no app-rollback in this build); recovery is BOOT +
   USB. Worth stating on the screen before the push ("do not remove power"), as the web page already does.

### Open choices for Mez

- Per entry or per device `board_id`; whether beta builds are offered by default (suggest no).
- Whether the release build writes `size`/`sha256` itself (suggest yes, it already makes the bins).
- XIAO: needs the guard ported first (it is 1.6.4, S3R8, 8 MB flash, OPI PSRAM). Until then it stays "USB only".

## Part A2 - MODE and fallback

### What exists (read from the code)

- `WEBBY.TXT` has `MODE=WIFI|ESPNOW`, `SSID=`, `PASS=`. Boot: with `MODE=WIFI` and an SSID the dongle tries
  the home join twice (12 s each). If that fails it starts its own AP + ESP-NOW (`g_fallback_espnow`, "pairing
  needs BOOT 5 s") and, while idle (no disk loaded, nothing dirty, no AP station), restarts to try home again
  every 3 min (`WIFI_RETRY_MS`).
- **There is no runtime fallback.** Once joined (`g_webmode == 1`), if the router goes away the dongle stays in
  STA mode with no AP and no ESP-NOW until someone power-cycles it. That is the gap behind "fallback is REQUIRED".

### Modes (stored as `PREF=`, per dongle)

| PREF | Name | Behaviour |
|---|---|---|
| `auto` (default) | Home WiFi with fallback | Needs an SSID. Join home at boot as today. If the link is lost at runtime, fall back to own AP + ESP-NOW automatically, and come back to home when it returns. |
| `standalone` | Standalone | Own AP + ESP-NOW only, never touches home WiFi (SSID/PASS kept). This is today's `MODE=ESPNOW`. |
| `link` | Screen link | ESP-NOW to the paired screen only; own AP hidden and no web page. Only BOOT 5 s opens it up again. **My reading of Dimmy's "Screen link" - to be confirmed or corrected by Dimmy.** |

Migration: `MODE=WIFI`+SSID reads as `auto`; `MODE=ESPNOW` or no SSID reads as `standalone`. Old firmware ignores
`PREF=` and keeps working from `MODE=`; the writer keeps both lines in step.

### Fallback timings (`auto`)

- **Detect:** STA lost (`ARDUINO_EVENT_WIFI_STA_DISCONNECTED` and `WiFi.status() != WL_CONNECTED`) continuously
  for **20 s** (the Arduino auto-reconnect gets its chance first; short router blips do not flip modes).
- **Switch:** drop STA, bring up AP + ESP-NOW on the fixed ESP-NOW channel (6) **without a restart** whenever a
  disk image is loaded or saves are pending, because the RAM disk lives in PSRAM and a restart loses it. When
  idle, the existing restart path is fine and simpler. Either way the dongle ends in the same state as
  `g_fallback_espnow` today, within ~5 s of the 20 s timer.
- **Return:** unchanged rule, every 3 min and only while idle (no disk, nothing dirty, no AP station): try the
  home join; success moves back to `g_webmode = 1`. A fling or an open web session postpones it.
- Status for the screen: `/api/status` and `/api/system/info` gain `mode_pref` (`auto|standalone|link`),
  `mode_now` (`wifi|espnow`), `fallback` (bool) and `fallback_s` (seconds in fallback).
- The owner lock stays as it is in fallback (pairing needs BOOT 5 s), so a fallback does not open the dongle.

### How the screen sets the mode

- New `POST /api/mode` with `mode=auto|standalone|link`, owner-gated like the other mutating routes
  (`webOwnerOk`). Reply `{"ok":true,"mode":"auto","applies":"now"|"after_eject"}`; `applies` is `after_eject`
  when a disk is loaded (the change is written to `WEBBY.TXT` at once but applied when the dongle goes idle, so
  the RAM disk is not lost). `400 {"ok":false,"error":"no_ssid"}` for `auto` without an SSID.
- The screen reaches it the same way as the OTA push: over the LAN for a dongle in home-WiFi mode, over the
  dongle AP (as owner) otherwise. `/api/wifi/save` and `/api/espnow-web` stay and map onto this.
- The DONGLE SETUP switch on the screen is Mez's; it only needs `mode_pref` to show the current value and
  `POST /api/mode` to change it.

### Risks to check on hardware

- False fallback on a flaky router (hence 20 s, tunable); fallback loops if the router is up but the password
  changed (the 3-min retry only costs idle dongles a restart).
- STA and AP/ESP-NOW channel: the AP must come up on channel 6, so STA is dropped first, not run alongside.
- Re-registering ESP-NOW peers and the mDNS/fleet election after a runtime switch in either direction.
- `link` mode with a hidden AP: pairing and the 2-minute BOOT window must still work.

## Not in this document

Reserved-name suffix (CON/PRN...) for Webby 8.3 names, the W5 retry test and the LOCK-TEST.md run are separate
items from Mez's list of 15 Oct.
