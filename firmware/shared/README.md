# firmware/shared/

Modules included by more than one sketch. The rule that makes this folder
worth having: **a file in here is byte-identical to its twin in the
OMEGAWARE tree** (`dimitrihilverda/Gotek-Touchscreen-interface`,
`Gotek_Touchscreen/`). Fixes land in one place and travel whole — the
alternative is the JC3248/JC4827 situation this folder exists to end.

## webdav_client.h

HTTPS/HTTP WebDAV client: PROPFIND directory listing (streaming parser, tested
against a 688 KB response), GET into a caller-supplied buffer, connection
pooling with idle release, per-transfer progress callback. No external
libraries; WiFi.h and WiFiClientSecure only.

Its entire contract with the sketch:

```cpp
DavConfig c;                 // host, port, https, user, pass, basePath, enabled
davClient.configure(c, logFn);   // logFn: void(*)(const String&) — or nullptr
```

It reads no globals and calls nothing back except `logFn`. Include order does
not matter.

Battle scars worth knowing about, all fixed and covered by the host test suite
in the OMEGAWARE tree (`tools/test/`, 325 assertions):

- `_release()` once recursed into itself on any transfer whose connection the
  server closed — a panic at the very last moment of an otherwise perfect
  transfer. Found with per-128KB breadcrumbs and a crash-surviving RTC log.
- A parked (pooled) TLS connection pins ~50 KB of internal heap while it
  lives. Call `davClient.dropIdle()` from loop() — after 15 s of quiet it
  releases; the next request pays one handshake (~300-900 ms).
- `Client` has no virtual destructor, so connections are destroyed through
  the concrete type (`_destroy()`), not the base pointer.
- Arduino `String::indexOf` stops at NUL bytes; the parser scans raw buffers.

## Wired in this branch (step 1, as agreed)

Both panel sketches: `DAV_*` keys in CONFIG.TXT next to `HOME_SSID`, a
`davApplyConfig()`, and `doLoadWebdav(remotePath, showName)` beside the
existing loader — joins HOME_SSID with the same join/leave discipline as
`espnowSendDiskHome`, streams into the RAM disk's data region, builds the FAT
metadata around the bytes, attaches. **No UI yet** (that is the step after
review), and it refuses while the wireless dongle link is up: fetch-over-WiFi
next to ESP-NOW is the coexistence question deliberately parked.

CONFIG.TXT example:

```
DAV=ON
DAV_HOST=stack.example.net
DAV_PORT=443
DAV_HTTPS=ON
DAV_USER=amiga
DAV_PASS=secret
DAV_PATH=/remote.php/webdav/
HOME_SSID=MyNetwork
HOME_PASS=wifipassword
```
