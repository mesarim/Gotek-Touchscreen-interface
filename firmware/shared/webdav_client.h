#ifndef WEBDAV_CLIENT_H
#define WEBDAV_CLIENT_H

/*
  Gotek Touchscreen — Lightweight WebDAV Client
  Uses WiFiClientSecure for HTTPS WebDAV (e.g. Stackstorage, Nextcloud, etc.)
  PROPFIND for directory listing, GET for file download.
  No external library dependencies.
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
// The SD download path pulls its own dependency rather than assuming the
// sketch already included it — this header must work in any include order.
#if !defined(HAS_SD) || HAS_SD
  #include <FS.h>
  #include <SD_MMC.h>
#endif
#include <vector>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// ============================================================================
// Configuration surface
// ============================================================================
//
// This file used to read seven cfg_dav_* globals owned by whichever sketch
// included it, and log through a free function called sdLog(). That worked
// with two consumers and became the porting cost with five: every new board
// had to define the exact globals with the exact names. Now the sketch hands
// over a struct and a log callback once, via configure(), and this file
// touches nothing it was not given. That is the entire contract.
struct DavConfig {
  String   host;                 // bare hostname; configure() strips scheme/path
  uint16_t port     = 443;
  bool     https    = true;
  String   user;
  String   pass;
  String   basePath = "/";       // remote root every path is joined to
  bool     enabled  = false;
};

using DavLogFn = void (*)(const String &msg);

// Module-level, not a class member, because PsramAlloc below logs allocation
// failures and lives outside the class. Set by GotekDAV::configure().
static DavLogFn g_davLog = nullptr;
static inline void davLogLine(const String &m) { if (g_davLog) g_davLog(m); }

// ============================================================================
// WebDAV Types
// ============================================================================

// ---------------------------------------------------------------------------
// One entry in a listing.
//
// The name is a fixed char buffer rather than a String, and this is the single
// most important detail in the file. arduino-esp32 3.3.11 builds with
// CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096, which means allocations LARGER
// than 4 KB prefer PSRAM while everything SMALLER is forced into the ~300 KB
// internal SRAM. Arduino String stores up to 14 characters inline and heap-
// allocates beyond that, so 3000 folder names cost ~60 bytes of internal SRAM
// each (rounded capacity + TLSF header + light poisoning) — roughly 180 KB,
// held for the whole session, on a device that has ~150 KB free after WiFi,
// TLS, SD and USB are up.
//
// It got worse during growth: String's move constructor is not noexcept, so
// std::vector could not move-on-realloc and copied instead, briefly holding
// two full sets of name strings. That transient spike at the 2048 -> 4096
// boundary is what actually tipped the device over, and because the toolchain
// enables C++ exceptions with no emergency pool, the resulting bad_alloc
// aborted — reported as ESP_RST_PANIC rather than anything mentioning memory.
//
// With the name inline, an entry is plain-old-data: the whole array lives in
// PSRAM, growth is a memcpy, and a listing costs zero internal heap.
//
// 96 bytes fits any real disk-image folder name; longer ones are truncated
// for display but still address correctly because paths are rebuilt from the
// server's own listing.
// ---------------------------------------------------------------------------
struct DAVFileEntry {
  char     nameBuf[96];
  uint32_t size;
  bool     isDir;
  bool     hasCover;   // directory: contains a cover. file: IS a cover.
  bool     hasNfo;     // directory: contains an NFO. file: IS an NFO.

  void setName(const char *s) {
    strncpy(nameBuf, s ? s : "", sizeof(nameBuf) - 1);
    nameBuf[sizeof(nameBuf) - 1] = 0;
  }
  void setName(const String &s) { setName(s.c_str()); }
  const char *cname() const { return nameBuf; }
  String name() const { return String(nameBuf); }
  bool nameEquals(const char *s) const { return strcmp(nameBuf, s) == 0; }
};

// ---------------------------------------------------------------------------
// PSRAM-backed vector for entry lists. 3000 entries is ~310 KB, which the
// 8 MB of OPI PSRAM absorbs without noticing but the internal heap cannot.
//
// allocate() must THROW on failure, never return null: std::vector does not
// null-check, so a null return means elements get constructed at address 0 —
// a StoreProhibited panic that looks exactly like the OOM we are trying to
// report. Throwing gives callers something they can catch and turn into a
// readable message.
// ---------------------------------------------------------------------------
template <class T>
struct PsramAlloc {
  using value_type = T;
  PsramAlloc() noexcept {}
  template <class U> PsramAlloc(const PsramAlloc<U> &) noexcept {}
  T *allocate(size_t n) {
    const size_t bytes = n * sizeof(T);
    void *p = ps_malloc(bytes);
    if (!p) p = malloc(bytes);   // no PSRAM (or exhausted) — try internal
    if (!p) {
      davLogLine("DAV: alloc FAILED " + String((unsigned)bytes) +
                 " B (psram=" + String(ESP.getFreePsram()) +
                 " heap=" + String(ESP.getFreeHeap()) + ")");
      throw std::bad_alloc();
    }
    return (T *)p;
  }
  void deallocate(T *p, size_t) noexcept { free(p); }
  template <class U> bool operator==(const PsramAlloc<U> &) const noexcept { return true; }
  template <class U> bool operator!=(const PsramAlloc<U> &) const noexcept { return false; }
};

// Entry list type used everywhere a DAV listing is passed around. Declared as
// a typedef so call sites (`[]`, `.size()`, `.push_back()`, range-for) are
// unchanged — only the declarations needed touching.
using DAVEntryList = std::vector<DAVFileEntry, PsramAlloc<DAVFileEntry>>;

// Full radio for the duration of one network operation.
//
// The firmware runs WIFI_PS_MAX_MODEM at rest, deliberately: the radio sleeps
// between beacons and steady-state current drops from ~120 to ~15-30 mA, which
// is what keeps an Amiga 5V rail alive. But every TCP round trip then waits
// for the radio to wake, and a TLS transfer is thousands of round trips — that
// one setting is the difference between the 5-second loads this project used
// to have and 47 KB/s today.
//
// So: wake fully on entry, restore whatever was set on exit. Capture-restore
// rather than hardcoding MAX_MODEM back, because the dongle shares this file
// and never opted into modem sleep. A transfer is transient and the backlight
// is already dipped during it; the steady-state saving stays.
struct DavRadioWake {
  wifi_ps_type_t prev;
  DavRadioWake() {
    prev = WiFi.getSleep();
    if (prev != WIFI_PS_NONE) WiFi.setSleep(WIFI_PS_NONE);
  }
  ~DavRadioWake() {
    if (prev != WIFI_PS_NONE) WiFi.setSleep(prev);
  }
};

// ============================================================================
// WebDAV Client Class
// ============================================================================

class GotekDAV {
public:
  GotekDAV() : _connected(false), _lastError(""), _debugLog(""),
               _byteProgressCb(nullptr) {}

  String lastError() { return _lastError; }
  String lastDebug() { return _debugLog; }

  // Drop any pooled socket — after a settings change it may point at the old
  // host, and after a disconnect it should simply not exist.
  void closeIdle() { _dropPool(); }

  // A parked TLS connection pins ~50 KB of mbedTLS context in INTERNAL heap
  // for as long as it lives. That is a fine price during a burst of listings
  // (each reuse saves a 300-900 ms handshake) and a terrible one at rest:
  // with the pool warm plus a wallpaper decode, the heap-minimum log line
  // read min=0K. Call this from loop(); after a quiet spell the pool lets go
  // and the ~50 KB comes back. The next operation pays one handshake.
  void dropIdle(uint32_t idleMs = 15000) {
    if (_pool && millis() - _poolLastUseMs > idleMs) {
      _log("DAV: pooled connection idle, releasing its ~50 KB");
      _dropPool();
    }
  }
  bool isConnected() { return _connected; }

  // True if the last streamToBuffer() filled its destination before the
  // response ended. Callers loading a disk image must treat that as a
  // failure — a half-read ADF is indistinguishable from a good one by size
  // alone. Callers reading NFO text can happily use what they got.
  bool lastTruncated() const { return _lastTruncated; }

  // Progress callback — invoked from within _readHTTPBody / streamToBuffer
  // as bytes arrive, throttled to ~10 Hz so it can't slow the transfer.
  // `total` is 0 if Content-Length was not advertised. Passing nullptr
  // clears any previous callback. Runs on the same task as the DAV call
  // (loop task); no threading concerns.
  using ByteProgressCb = void (*)(size_t received, size_t total);
  void setProgressCallback(ByteProgressCb cb) { _byteProgressCb = cb; }

  // Hand over settings and a logger. Call at boot and again after the user
  // edits the DAV settings; a change of server or credentials also drops the
  // pooled connection, which would otherwise keep talking to the old one.
  void configure(const DavConfig &c, DavLogFn logFn) {
    _cfg = c;
    g_davLog = logFn;
    // Accept what people paste: full URLs. Store what the code needs: a host.
    if (_cfg.host.startsWith("https://")) _cfg.host = _cfg.host.substring(8);
    if (_cfg.host.startsWith("http://"))  _cfg.host = _cfg.host.substring(7);
    const int slash = _cfg.host.indexOf('/');
    if (slash > 0) _cfg.host = _cfg.host.substring(0, slash);
    _cfg.host.trim();
    _dropPool();
    _connected = false;
  }
  const DavConfig &config() const { return _cfg; }

  // Connect to WebDAV server (just validate connectivity)
  bool connect() {
    _lastError = "";
    if (_cfg.host.length() == 0) {
      _lastError = "No WebDAV host configured";
      _log("DAV connect: " + _lastError);
      return false;
    }
    if (WiFi.status() != WL_CONNECTED) {
      _lastError = "WiFi not connected (status=" + String(WiFi.status()) + ")";
      _log("DAV connect: " + _lastError);
      return false;
    }

    _log("DAV: testing connection to " + String(_cfg.https ? "https://" : "http://") +
         _cfg.host + ":" + String(_cfg.port));

    // Test with a PROPFIND on the base path
    DAVEntryList test;
    if (listDir("/", test)) {
      _connected = true;
      _log("DAV: connected OK (" + String(test.size()) + " entries in root)");
      return true;
    }
    // _lastError already set by listDir
    return false;
  }

  void disconnect() {
    _connected = false;
    _log("DAV: disconnected");
  }

  // List directory contents via PROPFIND
  bool listDir(const String &path, DAVEntryList &entries) {
    DavRadioWake _wake;
    entries.clear();
    _lastError = "";

    // Build full path
    String fullPath = _cfg.basePath;
    if (!fullPath.endsWith("/")) fullPath += "/";
    if (path.length() > 0 && path != "/") {
      if (path.startsWith("/")) fullPath += path.substring(1);
      else fullPath += path;
    }
    if (!fullPath.endsWith("/")) fullPath += "/";

    // URL-encode spaces in path but keep slashes
    String encodedPath = _urlEncodePath(fullPath);

    _log("DAV: listDir enter path='" + encodedPath + "' heap=" +
         String(ESP.getFreeHeap()) + " psram=" + String(ESP.getFreePsram()));

    // Create HTTPS or HTTP client on heap
    // One connection, kept between requests when the last body was fully
    // read. attempt 1 forces a fresh one if a pooled socket turned out dead.
    bool reused = false;
    WiFiClient *tcp = _acquire(false, 15000, reused);
    if (!tcp) return false;

    // Build PROPFIND request
    String auth = _basicAuth(_cfg.user, _cfg.pass);
    String body = "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                  "<D:propfind xmlns:D=\"DAV:\">"
                  "<D:prop><D:resourcetype/><D:getcontentlength/><D:displayname/></D:prop>"
                  "</D:propfind>";

    _log("DAV: -> PROPFIND " + encodedPath + " Host: " + _cfg.host);

    long contentLength = -1;
    bool chunked = false;
    for (int attempt = 0; ; attempt++) {
    tcp->println("PROPFIND " + encodedPath + " HTTP/1.1");
    tcp->println("Host: " + _cfg.host);
    tcp->println("Authorization: Basic " + auth);
    tcp->println("Depth: 1");
    tcp->println("Content-Type: application/xml");
    tcp->println("Content-Length: " + String(body.length()));
    tcp->println("Connection: keep-alive");
    tcp->println();
    tcp->print(body);
    _log("DAV: PROPFIND sent, awaiting headers...");

    // Read just the headers — the document itself is consumed by the
    // streaming parser below, which never materialises it in RAM.
    _readHTTPHeaders(tcp, contentLength, chunked);
    // A pooled socket the server had already closed answers nothing at all.
    // That is the one failure worth retrying, and only once.
    if (_httpStatus != 0 || attempt > 0 || !reused) break;
    _log("DAV: pooled connection was stale, retrying on a fresh one");
    _dropPool();
    tcp = _acquire(true, 15000, reused);
    if (!tcp) { _lastError = "Reconnect failed"; return false; }
    }
    _log("DAV: hdrs HTTP " + String(_httpStatus) + " len=" + String(contentLength) +
         (chunked ? " chunked" : "") + " heap=" + String(ESP.getFreeHeap()));

    if (_httpStatus >= 400) {
      // Read a small excerpt of the error body for the web UI, capped so a
      // verbose error page can't blow the heap on the way to reporting it.
      char errBuf[161];
      size_t n = 0;
      unsigned long t0 = millis();
      while (n < sizeof(errBuf) - 1 && millis() - t0 < 2000) {
        if (tcp->available()) errBuf[n++] = (char)tcp->read();
        else if (!tcp->connected()) break;
        else delay(1);
      }
      errBuf[n] = 0;
      for (size_t i = 0; i < n; i++) {
        if (errBuf[i] == '"')  errBuf[i] = '\'';
        if (errBuf[i] == '\n' || errBuf[i] == '\r') errBuf[i] = ' ';
      }
      _release(tcp, false);   // error path: never reuse
      _lastError = "HTTP " + String(_httpStatus) + ": " + String(errBuf);
      _log("DAV: " + _lastError);
      return false;
    }

    // Reserve up front so the listing isn't spent reallocating. Nextcloud and
    // Stack answer PROPFIND with chunked encoding and no Content-Length, so
    // this must NOT be conditional on having a length — that is precisely the
    // case that matters, and the one a `contentLength > 0` guard would skip.
    // ~230 bytes of XML per <response> is typical for the properties we ask
    // for; over-reserving slightly is far cheaper than a realloc storm.
    {
      size_t est = (contentLength > 0) ? (size_t)(contentLength / 230) + 16 : 512;
      if (est > 8192) est = 8192;
      entries.reserve(est);
    }

    const bool ok = _streamParsePropfind(tcp, contentLength, chunked, entries);
    // A parse that ran to the end leaves the socket at the document boundary,
    // which is the one state in which it can safely serve the next request.
    _release(tcp, ok);

    if (!ok) return false;
    if (entries.empty() && _httpStatus == 0) {
      _lastError = "Empty response from server";
      _log("DAV: " + _lastError);
      return false;
    }

    _log("DAV: listed " + String(entries.size()) + " entries in " + fullPath);
    _connected = true;
    return true;
  }

#if !defined(HAS_SD) || HAS_SD
  // Download a file via GET, straight to a file on the card.
  // Returns bytes written, or -1 on error.
  //
  // Guarded because this is the client's only tie to storage: a screenless
  // dongle has no card, and streamToBuffer() below is what it uses instead.
  // Boards that never define HAS_SD keep the method, so nothing existing moves.
  long downloadFile(const String &remotePath, const String &localPath) {
    DavRadioWake _wake;
    _lastError = "";

    // Ensure parent directory exists on SD
    int lastSlash = localPath.lastIndexOf('/');
    if (lastSlash > 0) {
      String parentDir = localPath.substring(0, lastSlash);
      SD_MMC.mkdir(parentDir.c_str());
    }

    // Build full remote path
    String fullRemote = _cfg.basePath;
    if (!fullRemote.endsWith("/")) fullRemote += "/";
    if (remotePath.startsWith("/")) fullRemote += remotePath.substring(1);
    else fullRemote += remotePath;

    String encodedPath = _urlEncodePath(fullRemote);

    _log("DAV: GET " + encodedPath);

    // Create HTTPS or HTTP client on heap
    // One connection, kept between requests when the last body was fully
    // read. attempt 1 forces a fresh one if a pooled socket turned out dead.
    bool reused = false;
    WiFiClient *tcp = _acquire(false, 30000, reused);
    if (!tcp) return -1;

    String auth = _basicAuth(_cfg.user, _cfg.pass);

    tcp->println("GET " + encodedPath + " HTTP/1.1");
    tcp->println("Host: " + _cfg.host);
    tcp->println("Authorization: Basic " + auth);
    tcp->println("Connection: keep-alive");
    tcp->println();

    long contentLength = -1;
    bool chunked = false;
    _readHTTPHeaders(tcp, contentLength, chunked);
    if (_httpStatus >= 400) {
      _lastError = "HTTP " + String(_httpStatus);
      _release(tcp, false);   // error path: never reuse
      return -1;
    }

    File outFile = SD_MMC.open(localPath.c_str(), "w");
    if (!outFile) {
      _release(tcp, false);   // error path: never reuse
      _lastError = "Cannot create local file";
      return -1;
    }

    bool complete = false, truncated = false;
    long totalBytes = _pumpBody(tcp, contentLength, chunked,
        [&outFile](const uint8_t *d, size_t n) -> size_t {
          return outFile.write(d, n);
        },
        &complete, &truncated);

    outFile.close();
    _release(tcp, complete);

    // A short write means the card is full or failing. A partial download
    // that lands at a plausible size is worse than no download at all: it
    // sits on the card looking valid and only fails when the machine tries
    // to boot it.
    if (totalBytes <= 0 || !complete || truncated) {
      SD_MMC.remove(localPath.c_str());
      if (_lastError.length() == 0) {
        _lastError = (totalBytes <= 0) ? "Zero bytes received"
                                       : "Download incomplete (" + String(totalBytes) + " B)";
      }
      _log("DAV: " + _lastError);
      return -1;
    }

    _log("DAV: downloaded " + fullRemote + " -> " + localPath + " (" + String(totalBytes) + " bytes)");
    return totalBytes;
  }
#endif  // HAS_SD

  // Stream a file directly into a memory buffer via GET
  // Returns bytes written, or -1 on error
  // allowReuse=false forces a fresh connection. A pooled socket followed by a
  // large body correlates exactly with a panic on the dongle: the small
  // follow-ups (cover, notes) reuse happily, a 900 KB image does not. Until
  // that is understood, the caller decides — and a disk image is the one
  // transfer where a crash costs the user a re-flash rather than a retry.
  long streamToBuffer(const String &remotePath, uint8_t *buf, size_t bufSize,
                      bool allowReuse = true) {
    DavRadioWake _wake;
    _lastError = "";

    // Build full remote path
    String fullRemote = _cfg.basePath;
    if (!fullRemote.endsWith("/")) fullRemote += "/";
    if (remotePath.startsWith("/")) fullRemote += remotePath.substring(1);
    else fullRemote += remotePath;

    String encodedPath = _urlEncodePath(fullRemote);
    _log("DAV: GET->RAM " + encodedPath + " bufSize=" + String(bufSize));

    // Create HTTPS or HTTP client on heap
    // One connection, kept between requests when the last body was fully
    // read. attempt 1 forces a fresh one if a pooled socket turned out dead.
    bool reused = false;
    WiFiClient *tcp = _acquire(!allowReuse, 30000, reused);
    if (!tcp) return -1;

    String auth = _basicAuth(_cfg.user, _cfg.pass);

    long contentLength = -1;
    bool chunked = false;
    for (int attempt = 0; ; attempt++) {
    tcp->println("GET " + encodedPath + " HTTP/1.1");
    tcp->println("Host: " + _cfg.host);
    tcp->println("Authorization: Basic " + auth);
    tcp->println("Connection: keep-alive");
    tcp->println();

    _readHTTPHeaders(tcp, contentLength, chunked);
    // A pooled socket the server had already closed answers nothing at all.
    // That is the one failure worth retrying, and only once.
    if (_httpStatus != 0 || attempt > 0 || !reused) break;
    _log("DAV: pooled connection was stale, retrying on a fresh one");
    _dropPool();
    tcp = _acquire(true, 30000, reused);
    if (!tcp) { _lastError = "Reconnect failed"; return -1; }
    }
    if (_httpStatus >= 400) {
      _lastError = "HTTP " + String(_httpStatus);
      _release(tcp, false);   // error path: never reuse
      return -1;
    }
    _log("DAV: stream contentLen=" + String(contentLength) + (chunked ? " chunked" : ""));

    size_t written = 0;
    bool complete = false, truncated = false;
    _pumpBody(tcp, contentLength, chunked,
        [buf, bufSize, &written](const uint8_t *d, size_t n) -> size_t {
          size_t space = bufSize - written;
          size_t take  = (n < space) ? n : space;
          if (take) { memcpy(buf + written, d, take); written += take; }
          return take;
        },
        &complete, &truncated);

    // Only pooled if the body was read to its end; anything else would leave
    // the next request reading the tail of this one.
    _release(tcp, complete);

    long totalBytes = (long)written;
    _lastTruncated = truncated;

    if (totalBytes == 0) {
      if (_lastError.length() == 0) _lastError = "Zero bytes received";
      return -1;
    }
    if (truncated) {
      // Report it but still hand back what we got: NFO text is genuinely
      // useful truncated, whereas a disk image is not — so the caller
      // decides, via lastTruncated().
      _lastError = "Response exceeds buffer (" + String((unsigned)bufSize) + " B)";
      _log("DAV: " + _lastError);
    } else if (!complete) {
      _lastError = "Transfer incomplete (" + String(totalBytes) + " B)";
      _log("DAV: " + _lastError);
    }

    _log("DAV: streamed " + String(totalBytes) + " bytes to RAM");
    return totalBytes;
  }

private:
  bool   _connected;
  String _lastError;

  // ── Connection pooling ─────────────────────────────────────────────────
  //
  // Every request used to open a fresh TCP connection and do a full TLS
  // handshake, then send Connection: close. Measured against a real server that
  // is about 300 ms of pure setup per request — enough that fetching a 104-byte
  // NFO took 326 ms, and listing a five-entry folder took 300-480 ms of which
  // the two kilobytes of XML were the cheap part.
  //
  // So one connection is kept between requests. The rule that makes this safe:
  // a connection is only ever pooled if the previous response was read to its
  // end. A half-drained body would leave the next request reading the tail of
  // the last one, which on a disk image is silent corruption rather than an
  // error — the worst kind of bug to trade for latency.
  //
  // And it always falls back: a pooled connection that turns out to be dead
  // costs one wasted attempt, after which the request is retried on a fresh
  // one. The worst case is exactly the old behaviour.
  DavConfig _cfg;
  WiFiClient *_pool = nullptr;
  uint32_t _poolLastUseMs = 0;   // for dropIdle(): when the pool last earned its keep
  bool _connSecure = false;   // what _newConnection() actually allocated

  WiFiClient *_newConnection() {
    WiFiClient *tcp = nullptr;
    _connSecure = _cfg.https;
    if (_cfg.https) {
      WiFiClientSecure *secure = new WiFiClientSecure();
      if (!secure) { _lastError = "Out of memory"; return nullptr; }
      secure->setInsecure();   // no CA store on an ESP32
      // setTimeout is MILLISECONDS on ESP32-Arduino. It once read 15, which is
      // 15 ms and therefore no timeout at all.
      secure->setTimeout(15000);
      tcp = secure;
    } else {
      tcp = new WiFiClient();
      if (!tcp) { _lastError = "Out of memory"; return nullptr; }
      tcp->setTimeout(15000);
    }
    // Explicit connect timeout: setTimeout() bounds reads only, and a session
    // log once showed a handshake blocking the loop task for 362 seconds.
    if (!tcp->connect(_cfg.host.c_str(), _cfg.port, 10000)) {
      _lastError = "TCP connect failed to " + _cfg.host + ":" + String(_cfg.port);
      _log("DAV: " + _lastError);
      delete tcp;
      return nullptr;
    }
    _log("DAV: TCP up heap=" + String(ESP.getFreeHeap()));
    return tcp;
  }

  // reused says whether the caller got a pooled connection, so it knows a
  // failure might just be a stale socket and is worth one retry.
  WiFiClient *_acquire(bool forceFresh, uint32_t readTimeoutMs, bool &reused) {
    reused = false;
    WiFiClient *tcp = nullptr;
    if (!forceFresh && _pool) {
      if (_pool->connected()) {
        reused = true;
        tcp = _pool;
        _log("DAV: reusing connection");
      } else {
        delete _pool;
        _pool = nullptr;
      }
    }
    if (!tcp) tcp = _newConnection();
    // Set per use: a socket opened for a PROPFIND gets reused for a whole-ADF
    // GET, which needs the longer one.
    if (tcp) tcp->setTimeout(readTimeoutMs);
    return tcp;
  }

  // Hand the connection back. Pooled only if the body was read to the end.
  // Close a connection and free it properly.
  //
  // Client has no virtual destructor, so deleting a WiFiClientSecure through a
  // WiFiClient* never runs ~NetworkClientSecure() and quietly leaks the mbedTLS
  // context it owns. Delete through the type we actually allocated.
  //
  // One flag covers it: every connection is made by _newConnection() from the
  // same _cfg.https, and changing that setting drops the pool.
  void _destroy(WiFiClient *tcp) {
    if (!tcp) return;
    tcp->stop();
    if (_connSecure) delete static_cast<WiFiClientSecure *>(tcp);
    else             delete tcp;
  }

  void _release(WiFiClient *tcp, bool bodyComplete) {
    if (!tcp) return;

    // Reusable: park it, replacing whatever was parked before.
    if (bodyComplete && tcp->connected()) {
      if (_pool && _pool != tcp) _destroy(_pool);
      _pool = tcp;
      _poolLastUseMs = millis();
      return;
    }

    // Not reusable: close it and let it go.
    //
    // This line used to read _release(tcp, false), which re-entered with the
    // same arguments, failed the same test, and called itself again until the
    // stack canary fired. Every transfer the server closed instead of keeping
    // alive ended in a PANIC at the very last moment, with the heap and the
    // stack both looking perfectly healthy right up to it.
    if (_pool == tcp) _pool = nullptr;
    _destroy(tcp);
  }

  void _dropPool() {
    if (!_pool) return;
    _destroy(_pool);
    _pool = nullptr;
  }

  String _debugLog;
  int    _httpStatus;
  bool   _resynced = false;      // parser had to drop data to recover
  bool   _lastTruncated = false; // last streamToBuffer filled its destination
  ByteProgressCb _byteProgressCb;

  // Fire progress callback (if set) at most once per 100 ms so the UI
  // gets a live counter without slowing the transfer to render speed.
  void _fireProgress(size_t received, size_t total) {
    if (!_byteProgressCb) return;
    static uint32_t lastFireMs = 0;
    uint32_t now = millis();
    if (now - lastFireMs < 100 && received != total) return;
    lastFireMs = now;
    _byteProgressCb(received, total);
  }

  void _log(const String &msg) {
    _debugLog += msg + "\n";
    // Keep last 2KB only
    if (_debugLog.length() > 2048) {
      _debugLog = _debugLog.substring(_debugLog.length() - 1500);
    }
    davLogLine(msg);
  }

  // Base64 encode for HTTP Basic Auth
  String _basicAuth(const String &user, const String &pass) {
    String credentials = user + ":" + pass;
    int len = credentials.length();
    const char *data = credentials.c_str();

    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    String result = "";
    result.reserve((len + 2) / 3 * 4);

    for (int i = 0; i < len; i += 3) {
      uint32_t n = ((uint8_t)data[i]) << 16;
      if (i + 1 < len) n |= ((uint8_t)data[i + 1]) << 8;
      if (i + 2 < len) n |= ((uint8_t)data[i + 2]);

      result += b64[(n >> 18) & 0x3F];
      result += b64[(n >> 12) & 0x3F];
      result += (i + 1 < len) ? b64[(n >> 6) & 0x3F] : '=';
      result += (i + 2 < len) ? b64[n & 0x3F] : '=';
    }
    return result;
  }

  // URL-encode path segments (keep slashes, encode spaces etc.)
  String _urlEncodePath(const String &path) {
    String result = "";
    for (int i = 0; i < (int)path.length(); i++) {
      char c = path.charAt(i);
      if (c == '/' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
        result += c;
      } else {
        char hex[4];
        snprintf(hex, sizeof(hex), "%%%02X", (uint8_t)c);
        result += hex;
      }
    }
    return result;
  }

  // Consume the HTTP status line + headers, leaving the socket positioned at
  // the first body byte. Sets _httpStatus and reports framing to the caller.
  // Split out of _readHTTPBody so the streaming parser can take over from
  // here without the body ever being buffered.
  void _readHTTPHeaders(WiFiClient *tcp, long &contentLength, bool &chunked) {
    _httpStatus   = 0;
    contentLength = -1;
    chunked       = false;
    unsigned long timeout = millis();
    while (tcp->connected() && millis() - timeout < 15000) {
      if (!tcp->available()) { delay(1); continue; }
      String line = tcp->readStringUntil('\n');
      line.trim();
      if (line.startsWith("HTTP/")) {
        int sp = line.indexOf(' ');
        if (sp > 0) _httpStatus = line.substring(sp + 1, sp + 4).toInt();
      }
      // Match header names properly. Testing the whole line for "chunked"
      // (as this used to) flips the client into chunked mode for any
      // response whose ETag or Content-Disposition happens to contain that
      // substring — after which the first "chunk size line" is actually
      // payload and the transfer collapses in a way that is baffling to
      // debug.
      String lower = line;
      lower.toLowerCase();
      if (lower.startsWith("content-length:")) {
        contentLength = line.substring(line.indexOf(':') + 1).toInt();
      }
      if (lower.startsWith("transfer-encoding:") && lower.indexOf("chunked") >= 0) {
        chunked = true;
      }
      if (line.length() == 0) break;      // blank line ends the header section
      timeout = millis();
    }
    // RFC 9112: if both are present, chunked wins and Content-Length is
    // ignored. Leaving it set would make the pump stop short.
    if (chunked) contentLength = -1;
  }

  // ==========================================================================
  // Shared HTTP body pump
  //
  // Handles chunked and Content-Length framing once, so the download paths
  // don't each reimplement it — they used to, and two of the three copies
  // were wrong in different ways: downloadFile ignored chunked entirely and
  // wrote the hex chunk headers into the middle of .adf files, and
  // streamToBuffer credited bytes it never read when its destination filled,
  // desynchronising the framing and silently truncating.
  //
  // `sink(data, len)` returns how many bytes it accepted. Accepting fewer
  // than offered means "full": the pump keeps draining the socket so framing
  // stays in sync, but stops delivering and reports truncation.
  //
  // Returns bytes delivered to the sink, or -1 if the response never started.
  // ==========================================================================
  template <typename Sink>
  long _pumpBody(WiFiClient *tcp, long contentLength, bool chunked,
                 Sink sink, bool *outComplete, bool *outTruncated) {
    // 4 KB, not 1 KB: a TLS record carries up to 16 KB, and draining it in
    // 1 KB sips pays the WiFiClientSecure call overhead sixteen times per
    // record. Still on the stack — the loop task has 16 KB and its high-water
    // mark shows ~11 KB never touched, measured during a transfer.
    uint8_t scratch[4096];
    long delivered = 0;
    long consumed  = 0;          // body bytes taken off the socket
    bool truncated = false;
    bool complete  = false;
    long chunkRemaining = chunked ? 0 : -1;
    unsigned long timeout = millis();

    while (millis() - timeout < 30000) {
      // ---- chunk header -------------------------------------------------
      if (chunked && chunkRemaining == 0) {
        if (!tcp->available()) {
          if (!tcp->connected()) break;      // died mid-stream
          delay(1);
          continue;
        }
        String sizeLine = tcp->readStringUntil('\n');
        sizeLine.trim();
        if (sizeLine.length() == 0) { timeout = millis(); continue; }
        chunkRemaining = strtol(sizeLine.c_str(), nullptr, 16);
        timeout = millis();
        if (chunkRemaining <= 0) { complete = true; break; }   // terminal chunk
      }

      // ---- how much may we take right now -------------------------------
      size_t want = sizeof(scratch);
      if (chunked) {
        if ((long)want > chunkRemaining) want = (size_t)chunkRemaining;
      } else if (contentLength > 0) {
        long remain = contentLength - consumed;
        if (remain <= 0) { complete = true; break; }
        if ((long)want > remain) want = (size_t)remain;
      }

      if (!tcp->available()) {
        if (!tcp->connected()) {
          // Connection-close framing has no length; EOF IS the end.
          if (!chunked && contentLength < 0) complete = true;
          break;
        }
        delay(1);
        continue;
      }

      size_t got = tcp->readBytes((char *)scratch, want);
      if (got == 0) { delay(1); continue; }
      consumed += got;
      timeout = millis();

      if (chunked) {
        chunkRemaining -= got;
        if (chunkRemaining == 0) {
          // Consume the CRLF terminating the chunk body.
          if (tcp->available()) tcp->read();
          if (tcp->available()) tcp->read();
        }
      }

      // Deliver. Once full we keep reading (above) but stop handing over,
      // which is what keeps chunk framing aligned.
      if (!truncated) {
        size_t accepted = sink(scratch, got);
        delivered += (long)accepted;
        if (accepted < got) truncated = true;
      }

      _fireProgress((size_t)delivered, contentLength > 0 ? (size_t)contentLength : 0);
      yield();
    }

    if (outComplete)  *outComplete  = complete;
    if (outTruncated) *outTruncated = truncated;
    return delivered;
  }


  // ==========================================================================
  // Streaming PROPFIND parser
  //
  // The old path read the whole multistatus document into an Arduino String
  // and then walked it with indexOf/substring. For a 3000-folder library that
  // document is 600-800 KB, which does not fit in the ESP32-S3's ~300 KB
  // internal heap — the listing died with an OOM that surfaced as a PANIC
  // reboot. Nothing about the format requires random access, so we now parse
  // as the bytes arrive and never hold more than one <response> block.
  //
  // Peak memory is the 8 KB sliding window below, regardless of library size.
  // ==========================================================================

  static const size_t PARSE_WIN = 8192;   // sliding window over the XML stream

  // Case-insensitive scan for an XML tag, tolerating any namespace prefix
  // (<D:response>, <d:response>, <lp1:response>, <response>). Returns the
  // index of the opening '<', or -1.
  //
  // A tag whose delimiter would fall outside the buffer is reported as
  // not-found so the caller pulls more bytes and retries — that is what makes
  // it safe to run over a partial window.
  static int _scanTag(const char *p, size_t len, const char *tagName, bool closing) {
    const size_t tlen = strlen(tagName);
    if (len < tlen + 2) return -1;
    for (size_t i = 0; i + tlen + 2 <= len; i++) {
      if (p[i] != '<') continue;
      size_t j = i + 1;
      if (closing) {
        if (p[j] != '/') continue;
        j++;
      } else if (p[j] == '/') {
        continue;
      }
      // Optional namespace prefix: [A-Za-z0-9_-]{1,12} ':'
      size_t nsStart = j;
      while (j < len && (j - nsStart) < 12 &&
             (isalnum((unsigned char)p[j]) || p[j] == '-' || p[j] == '_')) j++;
      if (j < len && p[j] == ':') j++;   // prefix consumed
      else                        j = nsStart;   // no prefix — rewind
      if (j + tlen >= len) return -1;    // tag + delimiter not fully buffered
      if (strncasecmp(p + j, tagName, tlen) != 0) continue;
      const char d = p[j + tlen];
      if (d != '>' && d != ' ' && d != '/' && d != '\t' && d != '\r' && d != '\n') continue;
      return (int)i;
    }
    return -1;
  }

  // Decode the XML entities WebDAV servers actually emit, in place. Without
  // this a game called "Tom & Jerry" listed as "Tom &amp; Jerry" — a
  // long-standing display bug in the old parser, which never decoded at all.
  static void _decodeEntities(char *s) {
    char *r = s, *w = s;
    while (*r) {
      if (*r != '&') { *w++ = *r++; continue; }
      if      (!strncmp(r, "&amp;",  5)) { *w++ = '&';  r += 5; }
      else if (!strncmp(r, "&lt;",   4)) { *w++ = '<';  r += 4; }
      else if (!strncmp(r, "&gt;",   4)) { *w++ = '>';  r += 4; }
      else if (!strncmp(r, "&quot;", 6)) { *w++ = '"';  r += 6; }
      else if (!strncmp(r, "&apos;", 6)) { *w++ = '\''; r += 6; }
      else if (r[1] == '#') {
        // Numeric entity. Anything above U+00FF can't be shown by the 8-bit
        // font, so it becomes '?' rather than mangled bytes.
        int base = (r[2] == 'x' || r[2] == 'X') ? 16 : 10;
        const char *numStart = r + (base == 16 ? 3 : 2);
        char *endp = nullptr;
        long cp = strtol(numStart, &endp, base);
        if (endp && *endp == ';' && cp > 0) {
          *w++ = (cp < 256) ? (char)cp : '?';
          r = endp + 1;
        } else {
          *w++ = *r++;
        }
      } else {
        *w++ = *r++;
      }
    }
    *w = 0;
  }

  // Extract the text content of <ns:tag>...</ns:tag> from a response block
  // into `out`. Returns true if the tag was present (an empty or self-closing
  // tag counts as present with an empty value).
  static bool _extractTagBuf(const char *p, size_t len, const char *tagName,
                             char *out, size_t outCap) {
    out[0] = 0;
    int s = _scanTag(p, len, tagName, false);
    if (s < 0) return false;
    // Advance to the end of the opening tag
    size_t gt = (size_t)s;
    while (gt < len && p[gt] != '>') gt++;
    if (gt >= len) return false;
    if (p[gt - 1] == '/') return true;          // self-closing: present, empty
    size_t vStart = gt + 1;
    int e = _scanTag(p + vStart, len - vStart, tagName, true);
    if (e < 0) return true;                     // unterminated: treat as empty
    size_t vLen = (size_t)e;
    if (vLen >= outCap) vLen = outCap - 1;
    memcpy(out, p + vStart, vLen);
    out[vLen] = 0;
    _decodeEntities(out);
    return true;
  }

  // Percent-decode a URL path in place (href values arrive encoded).
  static void _urlDecodeBuf(char *s) {
    char *r = s, *w = s;
    while (*r) {
      if (*r == '%' && isxdigit((unsigned char)r[1]) && isxdigit((unsigned char)r[2])) {
        char hex[3] = { r[1], r[2], 0 };
        *w++ = (char)strtol(hex, nullptr, 16);
        r += 3;
      } else if (*r == '+') {
        *w++ = ' '; r++;
      } else {
        *w++ = *r++;
      }
    }
    *w = 0;
  }

  // Turn one <response>...</response> block into an entry and append it.
  // `skip` is set for the very first block, which is always the collection
  // being listed rather than one of its children.
  void _parseResponseBlock(const char *blk, size_t len, bool skip,
                           DAVEntryList &entries) {
    if (skip) return;

    char nameBuf[192];
    // displayname is authoritative when present; otherwise fall back to the
    // last path segment of href.
    bool haveName = _extractTagBuf(blk, len, "displayname", nameBuf, sizeof(nameBuf));
    if (!haveName || nameBuf[0] == 0) {
      char hrefBuf[320];
      if (!_extractTagBuf(blk, len, "href", hrefBuf, sizeof(hrefBuf))) return;
      _urlDecodeBuf(hrefBuf);
      size_t hl = strlen(hrefBuf);
      while (hl > 0 && hrefBuf[hl - 1] == '/') hrefBuf[--hl] = 0;
      const char *slash = strrchr(hrefBuf, '/');
      const char *base  = slash ? slash + 1 : hrefBuf;
      strncpy(nameBuf, base, sizeof(nameBuf) - 1);
      nameBuf[sizeof(nameBuf) - 1] = 0;
    }
    if (nameBuf[0] == 0) return;
    if (!strcmp(nameBuf, ".") || !strcmp(nameBuf, "..")) return;

    const bool isDir = (_scanTag(blk, len, "collection", false) >= 0);

    uint32_t fileSize = 0;
    char sizeBuf[24];
    if (_extractTagBuf(blk, len, "getcontentlength", sizeBuf, sizeof(sizeBuf)) && sizeBuf[0]) {
      fileSize = (uint32_t)strtoul(sizeBuf, nullptr, 10);
    }

    bool isCover = false, isNfo = false;
    if (!isDir) {
      // Classify by extension; anything that isn't a disk image, cover or NFO
      // is noise as far as the browser is concerned. Compared on the raw
      // buffer so a listing never allocates a String per entry.
      const bool isDisk = _hasExt(nameBuf, kDiskExts);
      isCover = _hasExt(nameBuf, kCoverExts);
      isNfo   = _hasExt(nameBuf, kNfoExts);
      if (!isDisk && !isCover && !isNfo) return;
    }

    // Construct in place — a push_back of a local would copy the 104-byte
    // entry an extra time for every row in the library.
    entries.emplace_back();
    DAVFileEntry &entry = entries.back();
    entry.setName(nameBuf);
    entry.isDir    = isDir;
    entry.size     = fileSize;
    entry.hasCover = isCover;
    entry.hasNfo   = isNfo;
  }

  // Case-insensitive suffix match against a NULL-terminated extension table.
  // Operates on the raw char buffer specifically to avoid a String temporary
  // per entry — at 3000 entries those add up to real internal-heap churn.
  static bool _hasExt(const char *name, const char *const *exts) {
    const size_t nlen = strlen(name);
    for (const char *const *e = exts; *e; ++e) {
      const size_t elen = strlen(*e);
      if (nlen > elen && strcasecmp(name + nlen - elen, *e) == 0) return true;
    }
    return false;
  }

  // Disk-image extensions we surface. Kept in one table so adding another
  // platform's format is a one-line change rather than a hunt through the
  // parser. Covers the machines a Gotek actually gets fitted to, not just
  // the Amiga.
  static constexpr const char *kDiskExts[] = {
    ".adf", ".adz", ".dms",            // Amiga
    ".dsk", ".cpc", ".edsk",           // Amstrad CPC / Spectrum +3
    ".st",  ".msa", ".stx",            // Atari ST
    ".img", ".ima", ".dmf",            // PC / generic raw
    ".hfe",                            // HxC / FlashFloppy native
    ".d64", ".d81",                    // Commodore
    ".mgt", ".sad",                    // SAM Coupe
    ".fdi", ".scp",                    // generic flux / disk image
    ".zip",                            // archives (handled downstream)
    nullptr
  };
  static constexpr const char *kCoverExts[] = { ".jpg", ".jpeg", ".png", nullptr };
  static constexpr const char *kNfoExts[]   = { ".nfo", ".txt", nullptr };

  // Pull the multistatus document off the socket, emitting entries as blocks
  // complete. Handles both chunked and Content-Length framing.
  bool _streamParsePropfind(WiFiClient *tcp, long contentLength, bool chunked,
                            DAVEntryList &entries) {
    char *win = (char *)malloc(PARSE_WIN + 1);
    if (!win) { _lastError = "Out of memory (parse window)"; return false; }

    size_t used = 0;             // bytes currently in the window
    size_t totalBytes = 0;       // bytes pulled off the socket overall
    size_t lastLogged = 0;
    bool   firstBlock = true;    // first <response> is the collection itself
    long   chunkRemaining = chunked ? 0 : -1;   // -1 = not chunked
    bool   eof = false;
    bool   complete = false;    // false unless we reach a clean end of stream
    unsigned long timeout = millis();
    _resynced = false;

    // Loop until the socket is drained AND the window has been parsed out.
    // EOF must never short-circuit straight to the exit: the final reads
    // almost always leave one or more complete <response> blocks sitting in
    // the window, and breaking early would silently drop the last entries of
    // every listing.
    while (millis() - timeout < 30000) {
      // ---- refill -------------------------------------------------------
      if (!eof && used < PARSE_WIN) {
        size_t want = PARSE_WIN - used;
        bool   canRead = true;

        if (chunked) {
          if (chunkRemaining == 0) {
            // Between chunks: read the hex size line.
            if (tcp->available()) {
              String sizeLine = tcp->readStringUntil('\n');
              sizeLine.trim();
              if (sizeLine.length() > 0) {
                chunkRemaining = strtol(sizeLine.c_str(), nullptr, 16);
                if (chunkRemaining <= 0) eof = true;   // terminal chunk
                timeout = millis();
              }
              canRead = false;   // re-evaluate framing on the next pass
            } else if (!tcp->connected()) {
              eof = true;
              canRead = false;
            } else {
              delay(1);
              canRead = false;
            }
          }
          if (canRead && (long)want > chunkRemaining) want = (size_t)chunkRemaining;
        } else if (contentLength > 0) {
          size_t remain = (size_t)contentLength - totalBytes;
          if (remain == 0) { eof = true; canRead = false; }
          else if (want > remain) want = remain;
        }

        if (canRead && !eof) {
          if (tcp->available()) {
            size_t got = tcp->readBytes(win + used, want);
            if (got > 0) {
              used       += got;
              totalBytes += got;
              if (chunked) {
                chunkRemaining -= got;
                if (chunkRemaining == 0) {
                  // Consume the CRLF that terminates the chunk body.
                  if (tcp->available()) tcp->read();
                  if (tcp->available()) tcp->read();
                }
              }
              timeout = millis();
              _fireProgress(totalBytes, contentLength > 0 ? (size_t)contentLength : 0);
              if (totalBytes - lastLogged >= 65536) {
                lastLogged = totalBytes;
                _log("DAV: parsed @" + String((unsigned)(totalBytes / 1024)) +
                     "K entries=" + String(entries.size()) +
                     " heap=" + String(ESP.getFreeHeap()));
              }
            }
          } else if (!tcp->connected()) {
            eof = true;
          } else {
            delay(1);
          }
        }
      }

      // ---- drain complete <response> blocks -----------------------------
      size_t consumed = 0;
      while (true) {
        int s = _scanTag(win + consumed, used - consumed, "response", false);
        if (s < 0) break;
        size_t bs = consumed + (size_t)s;
        int e = _scanTag(win + bs, used - bs, "response", true);
        if (e < 0) break;                       // block not complete yet
        size_t be = bs + (size_t)e;
        _parseResponseBlock(win + bs, be - bs, firstBlock, entries);
        firstBlock = false;
        // Skip past the closing tag so the next scan starts cleanly.
        size_t after = be;
        while (after < used && win[after] != '>') after++;
        consumed = (after < used) ? after + 1 : used;
        yield();
      }

      // ---- compact ------------------------------------------------------
      if (consumed > 0) {
        memmove(win, win + consumed, used - consumed);
        used -= consumed;
      } else if (used >= PARSE_WIN) {
        // A single response block larger than the window. Real servers never
        // do this; rather than stall forever, resync on the last opening tag
        // we can see and drop everything before it.
        int last = -1, from = 0;
        while (true) {
          int s = _scanTag(win + from, used - from, "response", false);
          if (s < 0) break;
          last = from + s;
          from = last + 1;
        }
        if (last > 0) {
          memmove(win, win + last, used - last);
          used -= last;
        } else {
          used = 0;    // nothing recognisable — start over
        }
        _resynced = true;   // data was dropped — the listing is not trustworthy
        _log("DAV: parse window overflow, resynced");
      }

      // ---- termination --------------------------------------------------
      // Once the socket is drained, keep going only while the drain step is
      // still making progress. `consumed == 0` at EOF means what's left in
      // the window is a trailing fragment (</multistatus>, whitespace) with
      // no further complete block in it.
      if (eof && consumed == 0) { complete = true; break; }
      yield();
    }

    free(win);

    // Content-Length framing gives us an exact expectation — hold it to that,
    // otherwise a connection that dies at 90 % looks identical to success.
    if (complete && contentLength > 0 && totalBytes < (size_t)contentLength) {
      complete = false;
    }

    _log("DAV: stream parse " + String(complete ? "ok" : "TRUNCATED") + ", " +
         String(entries.size()) + " entries, " +
         String((unsigned)(totalBytes / 1024)) + " KB, heap=" +
         String(ESP.getFreeHeap()) + " psram=" + String(ESP.getFreePsram()));

    if (!complete) {
      // Reporting success here would be worse than failing: davBrowsePath
      // writes the result straight to the SD listing cache, so one bad
      // transfer would leave a permanently half-missing library that never
      // refetches.
      _lastError = "Listing truncated after " + String(entries.size()) + " entries";
    } else if (_resynced) {
      _lastError = "Listing incomplete (oversized entry skipped)";
      complete = false;
    }
    return complete;
  }

};

// Global WebDAV client instance
GotekDAV davClient;

#endif // WEBDAV_CLIENT_H
