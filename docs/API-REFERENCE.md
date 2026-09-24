# GTi API reference

_Generated — do not hand-edit._ Every table below is read out of the firmware by
`.claude/skills/gotek-docs/scripts/extract_firmware_facts.py` and rendered by
`render_api_reference.py`. When the firmware and this file disagree, the firmware is
right and this file is stale; re-run the pair rather than correcting it by hand.

This file **enumerates**; it does not teach. For how pairing and the owner-lock
actually work, read `PAIRING.md`; for the CONFIG.TXT keys, `CONFIG-REFERENCE.md`.

## Where these facts came from

These surfaces were read from DIFFERENT checkouts, so each one names its own. That
matters here: every worktree of this repo contains every sketch, and two branches
can disagree about the same sketch, so a table without its provenance cannot be
read safely.

| Surface | Sketch | Version | Branch | Commit |
|---|---|---|---|---|
| Webby dongle - Super Mini | `Gotek_SuperMini_Webby` | `Webby-1.6.5-lock` | `webby-lock-161` | `3ce5acc` |
| Webby dongle - XIAO | `Gotek_XIAO_Webby` | `Webby-1.5.4-xiao-lock` | `webby-lock-161` | `3ce5acc` |
| Touchscreen panel - JC3248 | `Gotek_JC3248` | `5.9.23-fleet-JC3248` | `panel-fleet-5923` | `27400a8` |

## HTTP

Routes in registration order. **Closed by a claim** is derived per run from the lock
gate the handler body actually calls — `webDenyLocked` for the routes that change
what a running Amiga sees, `webDenyLockedCfg` for the ones that could take the disk
or the gate away. Both gates only bite when the device has at least one enrolled
owner *and* is on the shared LAN; on its own AP they are inactive.

### Webby dongle - Super Mini

| Method | Path | Handler(s) | Closed by a claim |
|---|---|---|---|
| `GET` | `/` | `handleWebUI` | no |
| `GET` | `/classic` | `handleRoot` | no |
| `GET` | `/status` | `<lambda>` | no |
| `POST` | `/upload` | `handleUploadDone`, `handleUpload` | yes - disk |
| `POST` | `/eject` | `handleEjectWeb` | yes - disk |
| `GET` | `/scan` | `handleScan` | no |
| `POST` | `/savewifi` | `handleSaveWifi` | yes - config |
| `POST` | `/espnow` | `handleEspnowWeb` | yes - config |
| `POST` | `/setap` | `<lambda>` | yes - config |
| `GET` | `/api/system/info` | `apiSystemInfo` | no |
| `GET` | `/api/disk/status` | `apiDiskStatus` | no |
| `POST` | `/api/disk/unload` | `apiDiskUnload` | yes - disk |
| `POST` | `/api/games/upload` | `apiGamesUploadDone`, `handleUpload` | yes - disk |
| `GET` | `/api/games/list` | `<lambda>` | no |
| `GET` | `/api/wifi/status` | `apiWifiStatus` | no |
| `GET` | `/api/config` | `apiConfig` | no |
| `POST` | `/api/config` | `apiConfigSave` | yes - config |
| `POST` | `/api/system/reboot` | `apiReboot` | yes - config |
| `POST` | `/api/system/ota` | `onOtaDone`, `onOtaUpload` | no |
| `GET` | `/api/themes/list` | `apiThemesList` | no |
| `GET` | `/api/fleet` | `apiFleet` | no |
| `ANY` | `(onNotFound fallback)` <br>matches `/api/themes/`, `/activate`, `/api/` | `<lambda>` | no |

22 routes, 9 of which a claim closes. Served from `Gotek_SuperMini_Webby.ino`.

### Webby dongle - XIAO

| Method | Path | Handler(s) | Closed by a claim |
|---|---|---|---|
| `GET` | `/` | `handleWebUI` | no |
| `GET` | `/classic` | `handleRoot` | no |
| `GET` | `/status` | `<lambda>` | no |
| `POST` | `/upload` | `handleUploadDone`, `handleUpload` | yes - disk |
| `POST` | `/eject` | `handleEjectWeb` | yes - disk |
| `GET` | `/scan` | `handleScan` | no |
| `POST` | `/savewifi` | `handleSaveWifi` | yes - config |
| `POST` | `/espnow` | `handleEspnowWeb` | yes - config |
| `POST` | `/setap` | `<lambda>` | yes - config |
| `GET` | `/api/system/info` | `apiSystemInfo` | no |
| `GET` | `/api/disk/status` | `apiDiskStatus` | no |
| `POST` | `/api/disk/unload` | `apiDiskUnload` | yes - disk |
| `POST` | `/api/games/upload` | `apiGamesUploadDone`, `handleUpload` | yes - disk |
| `GET` | `/api/games/list` | `<lambda>` | no |
| `GET` | `/api/wifi/status` | `apiWifiStatus` | no |
| `GET` | `/api/config` | `apiConfig` | no |
| `POST` | `/api/config` | `apiConfigSave` | yes - config |
| `POST` | `/api/system/reboot` | `apiReboot` | yes - config |
| `POST` | `/api/system/ota` | `onOtaDone`, `onOtaUpload` | no |
| `GET` | `/api/themes/list` | `apiThemesList` | no |
| `GET` | `/api/fleet` | `apiFleet` | no |
| `ANY` | `(onNotFound fallback)` <br>matches `/api/themes/`, `/activate`, `/api/` | `<lambda>` | no |

22 routes, 9 of which a claim closes. Served from `Gotek_XIAO_Webby.ino`.

### Touchscreen panel - JC3248

| Method | Path | Handler(s) | Closed by a claim | Refused in the wrong MODE |
|---|---|---|---|---|
| `GET` | `/` | `hRoot` | no | no |
| `GET` | `/index.html` | `hRoot` | no | no |
| `GET` | `/panel` | `hPanel` | no | no |
| `GET` | `/api/system/info` | `hSysInfo` | no | no |
| `GET` | `/api/config` | `hConfigGet` | no | no |
| `POST` | `/api/config` | `hConfigPost` | no | no |
| `GET` | `/api/games/list` | `hGamesList` | no | no |
| `GET` | `/api/disk/status` | `hDiskStatus` | no | no |
| `POST` | `/api/disk/unload` | `hDiskUnload` | no | no |
| `POST` | `/api/system/reboot` | `hReboot` | no | no |
| `POST` | `/api/system/ota` | `otaDone`, `otaUpload` | no | no |
| `POST` | `/api/games/upload` | `guDone`, `guUpload` | no | no |
| `GET` | `/api/wifi/status` | `hWifiStatus` | no | no |
| `GET` | `/api/dav/status` | `hDavStatus` | no | no |
| `POST` | `/api/dav/connect` | `hDavConnect` | no | no |
| `GET` | `/api/dav/list` | `hDavList` | no | no |
| `GET` | `/api/dav/rowmeta` | `hDavRowmeta` | no | no |
| `GET` | `/api/dav/nfo` | `hDavNfo` | no | no |
| `POST` | `/api/dav/load` | `hDavLoad` | no | no |
| `GET` | `/api/fleet` | `hFleet` | no | no |
| `POST` | `/api/fleet/send` | `hFleetSend` | no | `wpFleetOff` |
| `POST` | `/api/fleet/cmd` | `hFleetCmd` | no | `wpFleetOff` |
| `POST` | `/api/fleet/enroll` | `hFleetEnroll` | no | `wpFleetOff` |
| `POST` | `/api/fleet/unenroll` | `hFleetUnenroll` | no | `wpFleetOff` |
| `GET` | `/api/sd/list` | `hSdList` | no | no |
| `GET` | `/api/sd/get` | `hSdGet` | no | no |
| `POST` | `/api/sd/upload` | `suDone`, `suUpload` | no | no |
| `POST` | `/api/sd/delete` | `hSdDelete` | no | no |
| `POST` | `/api/sd/mkdir` | `hSdMkdir` | no | no |
| `GET` | `/files` | `hFiles` | no | no |
| `ANY` | `(onNotFound fallback)` | `<lambda>` | no | no |

`wpFleetOff` answers 409 unless this screen is in the mode that drives
dongles, which is a different refusal from an owner claim: it says *this
device is not doing that job right now*, not *this device belongs to
somebody else*. A client has to handle both.

31 routes, 0 of which a claim closes. Served from `../shared/web_panel.h`.

## TCP control port

A disk arrives as a 4-byte big-endian size followed by the image. The escape
`0xFFFFFFFF` in that size field means the next byte is a command instead.
**Needs AUTH first** is derived from whether the opcode is dispatched above or
below the refusal line inside `handleTCPClient`, so it follows the code.

### Webby dongle - Super Mini — port 3333

| Byte | Name | Note from the source | Needs AUTH first | Dispatched here |
|---|---|---|---|---|
| `0x01` | `CMD_GET_SAVE` | — | yes | yes |
| `0x02` | `CMD_GET_STATUS` | — | no | yes |
| `0x03` | `CMD_EJECT` | — | yes | yes |
| `0x04` | `CMD_EJECT_FORCE` | — | yes | yes |
| `0x06` | `CMD_SET_NAME` | #24: set the pretty display name for the NEXT flung disk (g_loaded_name only; FAT12 stays DISK.ADF) | yes | yes |
| `0x07` | `CMD_ENROLL` | #fleet: [FFFFFFFF][07][16-byte token] -> if the enrol window is open, store the token as an owner | no | yes |
| `0x08` | `CMD_AUTH` | #fleet: [FFFFFFFF][08][16-byte token] preamble before a disk fling -> proves the sender is an enrolled owner | no | yes |
| `0x09` | `CMD_UNENROLL` | #fleet: [FFFFFFFF][09][16-byte token] -> remove that token | yes | yes |

### Webby dongle - XIAO — port 3333

| Byte | Name | Note from the source | Needs AUTH first | Dispatched here |
|---|---|---|---|---|
| `0x01` | `CMD_GET_SAVE` | — | yes | yes |
| `0x02` | `CMD_GET_STATUS` | — | no | yes |
| `0x03` | `CMD_EJECT` | — | yes | yes |
| `0x04` | `CMD_EJECT_FORCE` | — | yes | yes |
| `0x06` | `CMD_SET_NAME` | #24: set the pretty display name for the NEXT flung disk (g_loaded_name only; FAT12 stays DISK.ADF) | yes | yes |
| `0x07` | `CMD_ENROLL` | #lock: [FFFFFFFF][07][16-byte token] -> if the enroll window is open, store the token as an owner | no | yes |
| `0x08` | `CMD_AUTH` | #lock: [FFFFFFFF][08][16-byte token] preamble before a disk fling -> proves the sender is an enrolled owner | no | yes |
| `0x09` | `CMD_UNENROLL` | #lock: [FFFFFFFF][09][16-byte token] -> remove that token (unclaim/release) | yes | yes |

## UDP discovery beacons

Read the cadence as part of the contract: a consumer that reads a flag out of a
beacon is reading state up to one full interval old, because nothing re-sends the
beacon early when that flag changes. **Sent to** is the first argument of each
`beginPacket` call exactly as the source writes it, variable names included.
Builders that send a packed binary struct rather than JSON are not beacons and
are not listed.

### Webby dongle - Super Mini

| Builder | JSON fields | Cadence | Sent to |
|---|---|---|---|
| `sendAliveBeacon` | `gti`, `id`, `name`, `ip`, `board`, `fw`, `hd`, `lk`, `lkd`, `enr`, `port`, `tcp`, `loaded`, `disk` | `ALIVE_BEACON_MS` = 12000 ms (call site) | `sub`, `IPAddress(255,255,255,255)` |

### Webby dongle - XIAO

| Builder | JSON fields | Cadence | Sent to |
|---|---|---|---|
| `sendAliveBeacon` | `gti`, `id`, `name`, `ip`, `board`, `fw`, `hd`, `lk`, `lkd`, `enr`, `port`, `tcp`, `loaded`, `disk` | `ALIVE_BEACON_MS` = 12000 ms (call site) | `sub`, `IPAddress(255,255,255,255)` |

### Touchscreen panel - JC3248

| Builder | JSON fields | Cadence | Sent to |
|---|---|---|---|
| `pfSendBeacon` | `gti`, `role`, `id`, `name`, `mdns`, `ip`, `loaded` | 8000 ms — a literal, no define (builder) | `IPAddress(255, 255, 255, 255)` |

## Ports, timings and limits

Numeric `#define`s whose names mention a port, a beacon, enrolment, a token, an
owner, a peer, staleness, a hello, a channel, a settle time or a packet. That is a
name filter over the protocol-facing constants, not the full define list — a
screensaver timeout is a real define but it is not part of any interface.

| Define | Webby dongle - Super Mini | Webby dongle - XIAO | Touchscreen panel - JC3248 |
|---|---|---|---|
| `ALIVE_BEACON_MS` | 12000 | 12000 | — |
| `DONGLE_TCP_PORT` | — | — | 3333 |
| `ENROLL_WIN_MS` | 30000 | 30000 | — |
| `ESPNOW_CHANNEL` | 6 | 6 | — |
| `ESPNOW_HELLO_MS` | 2000 | 2000 | — |
| `FLEET_STALE_MS` | 40000 | 40000 | — |
| `GTI_DISCO_PORT` | 51703 | 51703 | — |
| `MAX_OWNERS` | 4 | 4 | — |
| `PF_DISCO_PORT` | — | — | 51703 |
| `PF_MAX_PEERS` | — | — | 16 |
| `PF_STALE_MS` | — | — | 40000 |
| `PF_TCP_PORT` | — | — | 3333 |
| `PF_TOKEN_LEN` | — | — | 16 |
| `RX_PKT_SIZE` | 250 | 250 | — |
| `SAVE_BEACON_MS` | 10000 | 10000 | — |
| `SAVE_SETTLE_MS` | 3000 | 3000 | — |
| `STATUS_BEACON_MS` | 2500 | 2500 | — |
| `SV_SETTLE_MS` | — | — | 3000 |
| `TCP_PORT` | 3333 | 3333 | — |
| `WENROLL_WIN_MS` | 60000 | 60000 | — |
| `WOWNER_MAX` | 4 | 4 | — |
| `WTOKEN_LEN` | 16 | 16 | — |

