# GTi API reference

_Generated — do not hand-edit._ Every table below is read out of the firmware by
`.claude/skills/gotek-docs/scripts/extract_firmware_facts.py` and rendered by
`render_api_reference.py`. When the firmware and this file disagree, the firmware is
right and this file is stale; re-run the pair rather than correcting it by hand.

This file **enumerates**; it does not teach. For how pairing and the owner-lock
actually work, read `PAIRING.md`; for the CONFIG.TXT keys, `CONFIG-REFERENCE.md`.

## Where these facts came from

All surfaces below were read from the same checkout, named per row. Worth stating,
because every worktree of this repo contains every sketch and two branches can
disagree about the same one — so these tables describe this commit, nothing else.

| Surface | Sketch | Version | Branch | Commit |
|---|---|---|---|---|
| Webby dongle - Super Mini | `Gotek_SuperMini_Webby` | `Webby-1.6.3` | `gotek-docs-api-reference` | `8beac4e` |
| Webby dongle - XIAO | `Gotek_XIAO_Webby` | `Webby-1.5-xiao` | `gotek-docs-api-reference` | `8beac4e` |
| Touchscreen panel - JC3248 | `Gotek_JC3248` | `5.9.38-lab8-JC3248` | `gotek-docs-api-reference` | `8beac4e` |

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
| `POST` | `/upload` | `handleUploadDone`, `handleUpload` | no |
| `POST` | `/eject` | `handleEjectWeb` | no |
| `GET` | `/scan` | `handleScan` | no |
| `POST` | `/savewifi` | `handleSaveWifi` | no |
| `POST` | `/espnow` | `handleEspnowWeb` | no |
| `GET` | `/api/system/info` | `apiSystemInfo` | no |
| `GET` | `/api/disk/status` | `apiDiskStatus` | no |
| `POST` | `/api/disk/unload` | `apiDiskUnload` | no |
| `POST` | `/api/games/upload` | `apiGamesUploadDone`, `handleUpload` | no |
| `GET` | `/api/games/list` | `<lambda>` | no |
| `GET` | `/api/wifi/status` | `apiWifiStatus` | no |
| `GET` | `/api/config` | `apiConfig` | no |
| `POST` | `/api/config` | `apiConfigSave` | no |
| `POST` | `/api/system/reboot` | `apiReboot` | no |
| `POST` | `/api/system/ota` | `onOtaDone`, `onOtaUpload` | no |
| `GET` | `/api/themes/list` | `apiThemesList` | no |
| `GET` | `/api/fleet` | `apiFleet` | no |
| `ANY` | `(onNotFound fallback)` <br>matches `/api/themes/`, `/activate`, `/api/` | `<lambda>` | no |

21 routes, 0 of which a claim closes. Served from `Gotek_SuperMini_Webby.ino`.

### Webby dongle - XIAO

| Method | Path | Handler(s) | Closed by a claim |
|---|---|---|---|
| `GET` | `/` | `handleWebUI` | no |
| `GET` | `/classic` | `handleRoot` | no |
| `GET` | `/status` | `<lambda>` | no |
| `POST` | `/upload` | `handleUploadDone`, `handleUpload` | no |
| `POST` | `/eject` | `handleEjectWeb` | no |
| `GET` | `/scan` | `handleScan` | no |
| `POST` | `/savewifi` | `handleSaveWifi` | no |
| `POST` | `/espnow` | `handleEspnowWeb` | no |
| `GET` | `/api/system/info` | `apiSystemInfo` | no |
| `GET` | `/api/disk/status` | `apiDiskStatus` | no |
| `POST` | `/api/disk/unload` | `apiDiskUnload` | no |
| `POST` | `/api/games/upload` | `apiGamesUploadDone`, `handleUpload` | no |
| `GET` | `/api/games/list` | `<lambda>` | no |
| `GET` | `/api/wifi/status` | `apiWifiStatus` | no |
| `GET` | `/api/config` | `apiConfig` | no |
| `POST` | `/api/config` | `apiConfigSave` | no |
| `POST` | `/api/system/reboot` | `apiReboot` | no |
| `POST` | `/api/system/ota` | `onOtaDone`, `onOtaUpload` | no |
| `GET` | `/api/themes/list` | `apiThemesList` | no |
| `GET` | `/api/fleet` | `apiFleet` | no |
| `ANY` | `(onNotFound fallback)` <br>matches `/api/themes/`, `/activate`, `/api/` | `<lambda>` | no |

21 routes, 0 of which a claim closes. Served from `Gotek_XIAO_Webby.ino`.

### Touchscreen panel - JC3248

| Method | Path | Handler(s) | Closed by a claim |
|---|---|---|---|
| `GET` | `/` | `hRoot` | no |
| `GET` | `/index.html` | `hRoot` | no |
| `GET` | `/panel` | `hPanel` | no |
| `GET` | `/api/system/info` | `hSysInfo` | no |
| `GET` | `/api/config` | `hConfigGet` | no |
| `POST` | `/api/config` | `hConfigPost` | no |
| `GET` | `/api/games/list` | `hGamesList` | no |
| `GET` | `/api/disk/status` | `hDiskStatus` | no |
| `POST` | `/api/disk/unload` | `hDiskUnload` | no |
| `POST` | `/api/system/reboot` | `hReboot` | no |
| `POST` | `/api/system/ota` | `otaDone`, `otaUpload` | no |
| `POST` | `/api/games/upload` | `guDone`, `guUpload` | no |
| `GET` | `/api/wifi/status` | `hWifiStatus` | no |
| `GET` | `/api/dav/status` | `hDavStatus` | no |
| `POST` | `/api/dav/connect` | `hDavConnect` | no |
| `GET` | `/api/dav/list` | `hDavList` | no |
| `GET` | `/api/dav/rowmeta` | `hDavRowmeta` | no |
| `GET` | `/api/dav/nfo` | `hDavNfo` | no |
| `POST` | `/api/dav/load` | `hDavLoad` | no |
| `GET` | `/api/sd/list` | `hSdList` | no |
| `GET` | `/api/sd/get` | `hSdGet` | no |
| `POST` | `/api/sd/upload` | `suDone`, `suUpload` | no |
| `POST` | `/api/sd/delete` | `hSdDelete` | no |
| `POST` | `/api/sd/mkdir` | `hSdMkdir` | no |
| `GET` | `/files` | `hFiles` | no |
| `ANY` | `(onNotFound fallback)` | `<lambda>` | no |

26 routes, 0 of which a claim closes. Served from `../shared/web_panel.h`.

## TCP control port

A disk arrives as a 4-byte big-endian size followed by the image. The escape
`0xFFFFFFFF` in that size field means the next byte is a command instead.
**Needs AUTH first** is derived from whether the opcode is dispatched above or
below the refusal line inside `handleTCPClient`, so it follows the code.

### Webby dongle - Super Mini — port 3333

| Byte | Name | Note from the source | Needs AUTH first | Dispatched here |
|---|---|---|---|---|
| `0x01` | `CMD_GET_SAVE` | — | — | yes |
| `0x02` | `CMD_GET_STATUS` | — | — | yes |
| `0x03` | `CMD_EJECT` | — | — | yes |
| `0x04` | `CMD_EJECT_FORCE` | — | — | yes |
| `0x06` | `CMD_SET_NAME` | #24: set the pretty display name for the NEXT flung disk (g_loaded_name only; FAT12 stays DISK.ADF) | — | yes |

### Webby dongle - XIAO — port 3333

| Byte | Name | Note from the source | Needs AUTH first | Dispatched here |
|---|---|---|---|---|
| `0x01` | `CMD_GET_SAVE` | — | — | yes |
| `0x02` | `CMD_GET_STATUS` | — | — | yes |
| `0x03` | `CMD_EJECT` | — | — | yes |
| `0x04` | `CMD_EJECT_FORCE` | — | — | yes |
| `0x06` | `CMD_SET_NAME` | #24: set the pretty display name for the NEXT flung disk (g_loaded_name only; FAT12 stays DISK.ADF) | — | yes |

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
| `sendAliveBeacon` | `gti`, `id`, `name`, `ip`, `board`, `fw`, `hd`, `port`, `tcp`, `loaded`, `disk` | `ALIVE_BEACON_MS` = 12000 ms (call site) | `sub`, `IPAddress(255,255,255,255)` |

### Webby dongle - XIAO

| Builder | JSON fields | Cadence | Sent to |
|---|---|---|---|
| `sendAliveBeacon` | `gti`, `id`, `name`, `ip`, `board`, `fw`, `hd`, `port`, `tcp`, `loaded`, `disk` | `ALIVE_BEACON_MS` = 12000 ms (call site) | `sub`, `IPAddress(255,255,255,255)` |

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
| `FLEET_STALE_MS` | 40000 | 40000 | — |
| `GTI_DISCO_PORT` | 51703 | 51703 | — |
| `MAX_OWNERS` | 4 | 4 | — |
| `RX_PKT_SIZE` | 250 | 250 | — |
| `SAVE_BEACON_MS` | 10000 | 10000 | — |
| `SAVE_SETTLE_MS` | 3000 | 3000 | — |
| `STATUS_BEACON_MS` | 2500 | 2500 | — |
| `SV_SETTLE_MS` | — | — | 3000 |
| `TCP_PORT` | 3333 | 3333 | — |

