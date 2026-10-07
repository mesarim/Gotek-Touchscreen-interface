#pragma once
//
// The GTi web interface, for the panel sketches. (Merge step 2.)
//
// 5.9.16: ported from a hand-rolled raw WiFiServer to the ESP32 core WebServer
// class — the SAME server the Webby dongles use, which is proven reachable over
// home WiFi on this core. The raw WiFiServer/available() model reported "UP"
// but never actually answered incoming connections on core 3.x; WebServer's
// accept()/handleClient() model does. WiFi bring-up is unchanged (STA +
// setSleep(false), identical to the Webby).
//
// Serves the SAME gzipped page the OMEGAWARE touchscreen and dongles serve, and
// answers the API it expects, mapped onto this firmware's own structures. The
// page reports has_sd:false deliberately: upload / WebDAV / OTA / dashboard are
// on, while the SD-library surface stays off until its shape is pinned. The
// WiFi SD file manager is a SEPARATE page at /files (GTI_WEB_SD_FILES).
//
// Include from the sketch AFTER doLoadWebdav() and the disk builders; it uses
// them. Requires WEBUI=ON and HOME_SSID/HOME_PASS to join.

// Bumped on every change to this file or the embedded page, appended to the
// reported firmware string so a device tells you WHICH web build it runs.
#define GTI_WEB_REV "r16"

#include <Update.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <vector>
#include "webui.h"

static WebServer webPanelHttp(80);
// g_web_on is defined by the sketch (WEBUI=ON), parsed long before this include.
static bool   g_web_up          = false;   // server up and serving
static bool   g_web_joining     = false;   // WiFi join in progress; service() finishes bring-up
static bool   g_web_srv_started = false;   // routes registered + begin() done (once)
static bool   g_web_ap          = false;   // lab15p: serving on the GTi's own Wi-Fi (ESP-NOW mode), not the home router
static bool   g_web_mdns        = false;   // lab15p: MDNS.begin() succeeded (only in home-WiFi mode)
static bool   g_webPendingUnload = false;  // lab15p: eject queued for loop() (ESP-NOW mode)
static String g_webPendingDav   = "";      // queued remote path; loop() executes
static String g_webPendingName  = "";
static String g_webDavLoaded    = "";      // remote path of the mounted image, if any

static void webLog(const String &m) { Serial.println(m); }

static String wpJsonEscape(const String &in) {
  String out; out.reserve(in.length() + 16);
  for (unsigned int i = 0; i < in.length(); i++) {
    const char c = in[i];
    if      (c == '"')  out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') { }
    else if ((uint8_t)c < 0x20) { }
    else out += c;
  }
  return out;
}

static String wpArg(const char *k) { return webPanelHttp.hasArg(k) ? webPanelHttp.arg(k) : String(""); }

// ── Page + JSON handlers ──────────────────────────────────────────────────

// size-trim: the landing page (/) and the SD file page (/files) live in panel_landing.html and
// panel_files.html; make_webui_header.py stores them gzipped in panel_pages.h (as webui.h).
#include "panel_pages.h"
static void hRoot() { webPanelHttp.sendHeader("Content-Encoding", "gzip"); webPanelHttp.send_P(200, "text/html", (PGM_P)panel_landing_gz, panel_landing_gz_len); }
static void hPanel() {
  webPanelHttp.sendHeader("Cache-Control", "no-cache");
  webPanelHttp.sendHeader("Content-Encoding", "gzip");
  webPanelHttp.send_P(200, "text/html", (PGM_P)webui_gz, webui_gz_len);
}

static void hSysInfo() {
  const bool sta = (WiFi.status() == WL_CONNECTED);
  String j = "{";
  j += "\"firmware\":\"" + String(FW_VERSION) + " (gti-web " GTI_WEB_REV ")\",";
  j += "\"heap_free\":" + String((uint32_t)ESP.getFreeHeap()) + ",";
  j += "\"psram_free\":" + String((uint32_t)ESP.getFreePsram()) + ",";
  j += "\"sd_used_mb\":0,\"sd_total_mb\":0,";
  j += "\"game_count\":0,\"file_count\":0,";
  j += "\"loaded_game\":\"" + wpJsonEscape(g_loaded ? g_loaded_name : String("none")) + "\",";
  j += "\"mode\":\"" + String(g_mode == MODE_ADF ? "ADF" : g_mode == MODE_DSK ? "DSK" : "GEN") + "\",";
  j += "\"theme\":\"OMEGA_DARK\",";   // 5.9.39: name a preset the shared SPA knows, so it paints OMEGAWARE dark blue from the first request (was "GTI" = unknown = Workbench grey)
  j += "\"wifi_clients\":0,";
  j += "\"wifi_ip\":\"" + WiFi.localIP().toString() + "\",";
  j += "\"internet\":" + String(sta ? "true" : "false") + ",";
  j += "\"internet_ip\":\"" + (sta ? WiFi.localIP().toString() : String("")) + "\",";
  j += "\"internet_ssid\":\"" + wpJsonEscape(g_home_ssid) + "\",";
  j += "\"ftp_enabled\":false,";
  j += "\"dav_enabled\":" + String(g_dav_on ? "true" : "false") + ",";
  j += "\"log_enabled\":false,";
  j += "\"has_sd\":false,\"has_display\":true,\"has_ota\":true,";
  j += "\"max_image_bytes\":" + String((uint32_t)MAX_FILE_BYTES) + ",";
  j += "\"supports_hd\":true";
  j += "}";
  webPanelHttp.send(200, "application/json", j);
}

static void hConfigGet() {
  String j = "{";
  j += "\"WIFI_CLIENT_ENABLED\":\"1\",";
  j += "\"WIFI_CLIENT_SSID\":\"" + wpJsonEscape(g_home_ssid) + "\",";
  j += "\"WIFI_CLIENT_PASS\":\"\",";                                       // 5.9.39 (#24): never echo secrets to the LAN; the form shows blank = unchanged
  j += "\"HAS_WIFI_PASS\":\"" + String(g_home_pass.length() ? "1" : "0") + "\",";
  j += "\"DAV_ENABLED\":\"" + String(g_dav_on ? "1" : "0") + "\",";
  j += "\"DAV_HOST\":\"" + wpJsonEscape(g_dav_host) + "\",";
  j += "\"DAV_PORT\":\"" + String(g_dav_port) + "\",";
  j += "\"DAV_HTTPS\":\"" + String(g_dav_https ? "1" : "0") + "\",";
  j += "\"DAV_USER\":\"" + wpJsonEscape(g_dav_user) + "\",";
  j += "\"DAV_PASS\":\"\",";                                               // 5.9.39 (#24): masked, see above
  j += "\"HAS_DAV_PASS\":\"" + String(g_dav_pass.length() ? "1" : "0") + "\",";
  j += "\"DAV_PATH\":\"" + wpJsonEscape(g_dav_path) + "\",";
  j += "\"CAROUSEL\":\"" + String(g_car_bootmode == 2 ? "LAST" : g_car_bootmode == 1 ? "ON" : "OFF") + "\",";
  j += "\"SCREENSAVER\":\"" + String(g_ss_enabled ? "ON" : "OFF") + "\",";
  j += "\"SSMODE\":\"" + String(g_ss_matrix ? "MATRIX" : g_ss_slides ? "SLIDES" : "BOUNCE") + "\",";
  j += "\"SSTIME\":\"" + String((uint32_t)(g_ss_time_ms / 1000UL)) + "\",";
  j += "\"SSFAV\":\"" + String(g_ss_fav ? "ON" : "OFF") + "\",";
  j += "\"LANG\":\"" + String(LANG_NAMES[g_lang]) + "\",";
  j += "\"FONT\":\"" + String(g_font == 0 ? "SMALL" : g_font == 2 ? "LARGE" : "NORMAL") + "\",";
  j += "\"ROTATE\":\"" + String(g_rot * 90) + "\",";
  j += "\"COMPACT\":\"" + String(g_compact ? "ON" : "OFF") + "\",";
  j += "\"BTNSTYLE\":\"" + String(g_btn_pill ? "PILL" : "FLAT") + "\",";
  j += "\"TAPLOAD\":\"" + String(g_tapload ? "ON" : "OFF") + "\",";
  j += "\"HOTSWAP\":\"" + String(g_hotswap ? "ON" : "OFF") + "\",";
  j += "\"FORCESWAP\":\"" + String(g_forceswap ? "ON" : "OFF") + "\",";
  j += "\"CATEGORIES\":\"" + String(g_categories ? "ON" : "OFF") + "\",";
  j += "\"NESTING\":\"" + String(g_nesting ? "ON" : "OFF") + "\",";
  j += "\"HIVEMIND\":\"" + String(g_hivemind ? "ON" : "OFF") + "\",";
  j += "\"CAP\":\"" + String(g_dongle_cap) + "\",";
  j += "\"CRACKTRO\":\"" + (g_cracktro < 0 ? String("OFF") : String(g_cracktro)) + "\",";   // 5.9.41: OFF is a real value (-1); the page has a dropdown for it now
  j += "\"LOOP\":\"" + String(g_loop_cracktro ? "1" : "0") + "\"";
  j += "}";
  webPanelHttp.send(200, "application/json", j);
}

static void hConfigPost() {
  struct { const char *form; const char *cfg; String *dst; } sv[] = {
    { "DAV_HOST", "DAV_HOST", &g_dav_host },
    { "DAV_USER", "DAV_USER", &g_dav_user },
    { "DAV_PASS", "DAV_PASS", &g_dav_pass },
    { "DAV_PATH", "DAV_PATH", &g_dav_path },
  };
  for (auto &f : sv) {
    if (!webPanelHttp.hasArg(f.form)) continue;
    const String v = webPanelHttp.arg(f.form);
    if (f.dst == &g_dav_pass && v.length() == 0) continue;   // 5.9.39: GET masks the password, so a blank re-submit means "keep it"
    *f.dst = v; saveConfigKey(f.cfg, *f.dst);
  }
  if (webPanelHttp.hasArg("DAV_PORT")) {
    const int p = webPanelHttp.arg("DAV_PORT").toInt();
    g_dav_port = (p > 0 && p < 65536) ? p : 443;
    saveConfigKey("DAV_PORT", String(g_dav_port));
  }
  if (webPanelHttp.hasArg("DAV_HTTPS")) { g_dav_https = (webPanelHttp.arg("DAV_HTTPS") == "1"); saveConfigKey("DAV_HTTPS", g_dav_https ? "ON" : "OFF"); }
  if (webPanelHttp.hasArg("DAV_ENABLED")) { g_dav_on = (webPanelHttp.arg("DAV_ENABLED") == "1"); saveConfigKey("DAV", g_dav_on ? "ON" : "OFF"); }
  if (webPanelHttp.hasArg("WIFI_CLIENT_SSID")) { const String v = webPanelHttp.arg("WIFI_CLIENT_SSID"); if (v.length()) { g_home_ssid = v; saveConfigKey("HOME_SSID", v); } }
  if (webPanelHttp.hasArg("WIFI_CLIENT_PASS")) { const String v = webPanelHttp.arg("WIFI_CLIENT_PASS"); if (v.length()) { g_home_pass = v; saveConfigKey("HOME_PASS", v); } }
  static const char *passThrough[] = {
    "CAROUSEL", "SCREENSAVER", "SSMODE", "SSTIME", "SSFAV", "LANG",
    "FONT", "ROTATE", "COMPACT", "BTNSTYLE", "TAPLOAD", "HOTSWAP",
    "FORCESWAP", "CATEGORIES", "NESTING", "HIVEMIND", "CAP",
    "CRACKTRO", "LOOP"
  };
  for (auto k : passThrough) {
    if (webPanelHttp.hasArg(k)) { const String v = webPanelHttp.arg(k); if (v.length()) saveConfigKey(k, v); }
  }
  davApplyConfig();
  webPanelHttp.send(200, "application/json", "{\"status\":\"ok\"}");
}

static void hGamesList() {
  webPanelHttp.send(200, "application/json",
    "{\"mode\":\"ADF\",\"loaded_game\":\"" + wpJsonEscape(g_loaded ? g_loaded_name : String("")) +
    "\",\"loaded_file\":\"" + wpJsonEscape(g_loaded ? g_loaded_name : String("")) + "\",\"games\":[]}");
}

static void hDiskStatus() {
  String j = "{\"loaded\":" + String(g_loaded ? "true" : "false") + ",";
  j += "\"file\":\"" + wpJsonEscape(g_loaded_name) + "\",\"path\":\"\",";
  j += "\"game\":\"" + wpJsonEscape(g_loaded_name) + "\",";
  j += "\"disk_num\":" + String(g_loaded ? 1 : 0) + ",";
  j += "\"disk_total\":" + String(g_loaded ? 1 : 0) + ",";
  j += "\"source\":\"" + String(g_webDavLoaded.length() ? "DAV" : (g_loaded ? "SD" : "")) + "\",";
  j += "\"name\":\"" + wpJsonEscape(g_loaded_name) + "\",";
  j += "\"np_path\":\"" + wpJsonEscape(g_webDavLoaded) + "\",";
  j += "\"mode\":\"ADF\"}";
  webPanelHttp.send(200, "application/json", j);
}

static void hDiskUnload() {
  // lab15p: on the GTi's own Wi-Fi an eject can fetch the dongle's saves, which takes the radio off to
  // join the dongle - in the middle of this reply. So answer first and eject from loop() right after.
  if (g_web_ap) { g_webPendingUnload = true; webPanelHttp.send(200, "application/json", "{\"status\":\"ok\"}"); return; }
  doUnload();
  g_webDavLoaded = "";
  webPanelHttp.send(200, "application/json", "{\"status\":\"ok\"}");
}

static void hReboot() {
  webPanelHttp.send(200, "application/json", "{\"status\":\"ok\"}");
  delay(250); ESP.restart();
}

static void hWifiStatus() {
  const bool sta = (WiFi.status() == WL_CONNECTED);
  String jj = g_web_ap   // lab15p: report the GTi's own Wi-Fi when that is what we serve on
    ? "{\"ap_active\":true,\"ap_ip\":\"" + WiFi.softAPIP().toString() + "\",\"ap_clients\":" + String((unsigned)WiFi.softAPgetStationNum())
    : String("{\"ap_active\":false,\"ap_ip\":\"\",\"ap_clients\":0");
  jj += ",\"sta_connected\":" + String(sta ? "true" : "false");
  jj += ",\"sta_ip\":\"" + (sta ? WiFi.localIP().toString() : String("")) + "\"";
  jj += ",\"sta_ssid\":\"" + wpJsonEscape(g_home_ssid) + "\"}";
  webPanelHttp.send(200, "application/json", jj);
}

// ── WebDAV (client) ────────────────────────────────────────────────────────

static void hDavStatus() {
  String jj = "{\"enabled\":" + String(g_dav_on ? "true" : "false");
  jj += ",\"host\":\"" + wpJsonEscape(g_dav_host) + "\"";
  jj += ",\"port\":" + String(g_dav_port);
  jj += ",\"user\":\"" + wpJsonEscape(g_dav_user) + "\"";
  jj += ",\"path\":\"" + wpJsonEscape(g_dav_path) + "\"";
  jj += ",\"https\":" + String(g_dav_https ? "true" : "false");
  jj += ",\"connected\":" + String(davClient.isConnected() ? "true" : "false");
  jj += ",\"wifi_connected\":" + String((WiFi.status() == WL_CONNECTED) ? "true" : "false");
  jj += ",\"has_cache\":false";
  const String err = davClient.lastError();
  if (err.length() > 0) jj += ",\"error\":\"" + wpJsonEscape(err) + "\"";
  if (g_loaded) {
    jj += ",\"now_playing\":{\"source\":\"" + String(g_webDavLoaded.length() ? "dav" : "sd") + "\"";
    jj += ",\"name\":\"" + wpJsonEscape(g_loaded_name) + "\"";
    jj += ",\"path\":\"" + wpJsonEscape(g_webDavLoaded) + "\"}";
  }
  jj += "}";
  webPanelHttp.send(200, "application/json", jj);
}

static void hDavConnect() {
  if (!g_dav_on || g_dav_host.length() == 0)      webPanelHttp.send(400, "application/json", "{\"error\":\"WebDAV is not configured yet\"}");
  else if (WiFi.status() != WL_CONNECTED)          webPanelHttp.send(503, "application/json", "{\"error\":\"Not on a network\"}");
  else if (davClient.connect())                    webPanelHttp.send(200, "application/json", "{\"status\":\"ok\"}");
  else                                             webPanelHttp.send(502, "application/json", "{\"error\":\"" + wpJsonEscape(davClient.lastError()) + "\"}");
}

static void hDavList() {
  String want = wpArg("path");
  if (want.length() == 0) want = "/";
  if (WiFi.status() != WL_CONNECTED) { webPanelHttp.send(503, "application/json", "{\"error\":\"Not on a network\"}"); return; }
  DAVEntryList entries;
  bool ok = false;
  try { ok = davClient.listDir(want, entries); } catch (const std::bad_alloc &) { entries.clear(); }
  if (!ok) { webPanelHttp.send(502, "application/json", "{\"error\":\"" + wpJsonEscape(davClient.lastError()) + "\"}"); return; }
  String jj = "{\"path\":\"" + wpJsonEscape(want) + "\",\"entries\":[";
  bool first = true;
  for (size_t i = 0; i < entries.size(); i++) {
    if (!entries[i].isDir && (entries[i].hasCover || entries[i].hasNfo)) continue;
    if (!first) jj += ","; first = false;
    jj += "{\"name\":\"" + wpJsonEscape(entries[i].name()) + "\"";
    jj += ",\"dir\":" + String(entries[i].isDir ? "true" : "false");
    jj += ",\"size\":" + String(entries[i].size) + "}";
    yield();
  }
  jj += "]}";
  webPanelHttp.send(200, "application/json", jj);
}

static void hDavRowmeta() { webPanelHttp.send(200, "application/json", "{\"meta\":[],\"capped\":false}"); }

static void hDavNfo() {
  const String want = wpArg("path");
  static uint8_t nfoBuf[2048];
  const long n = davClient.streamToBuffer(want, nfoBuf, sizeof(nfoBuf) - 1);
  if (n <= 0) webPanelHttp.send(404, "application/json", "{\"error\":\"No notes there\"}");
  else { nfoBuf[n] = 0; webPanelHttp.send(200, "application/json", "{\"nfo\":\"" + wpJsonEscape(String((char *)nfoBuf)) + "\"}"); }
}

static void hDavLoad() {
  const String remote = wpArg("path");
  if (remote.length() == 0)                 webPanelHttp.send(400, "application/json", "{\"error\":\"No path given\"}");
  else if (WiFi.status() != WL_CONNECTED)   webPanelHttp.send(503, "application/json", "{\"error\":\"Not on a network\"}");
  else if (g_webPendingDav.length() > 0)    webPanelHttp.send(409, "application/json", "{\"error\":\"Already loading something\"}");
  else {
    String name = remote;
    const int sl = name.lastIndexOf('/'); if (sl >= 0) name = name.substring(sl + 1);
    g_webPendingDav = remote; g_webPendingName = name;
    webPanelHttp.send(200, "application/json", "{\"status\":\"ok\",\"name\":\"" + wpJsonEscape(name) + "\",\"file\":\"" + wpJsonEscape(remote) + "\"}");
  }
}

// ── Firmware OTA (streamed to the inactive slot) ───────────────────────────

static bool   g_otaBad = false, g_otaFail = false;
static size_t g_otaWrote = 0;
static void otaUpload() {
  HTTPUpload &up = webPanelHttp.upload();
  if (up.status == UPLOAD_FILE_START) {
    g_otaBad = false; g_otaFail = false; g_otaWrote = 0;
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) g_otaFail = true;
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (g_otaFail || g_otaBad) return;
    if (g_otaWrote == 0 && up.currentSize > 0 && up.buf[0] != 0xE9) { g_otaBad = true; Update.abort(); return; }
    if (Update.write(up.buf, up.currentSize) != up.currentSize) g_otaFail = true;
    g_otaWrote += up.currentSize;
  } else if (up.status == UPLOAD_FILE_END) {
    if (!g_otaBad && !g_otaFail) { if (!Update.end(true)) g_otaFail = true; }
  }
}
static void otaDone() {
  if (g_otaBad)                      { webPanelHttp.send(400, "application/json", "{\"error\":\"that is not an ESP32 firmware image\"}"); return; }
  if (g_otaFail || g_otaWrote == 0)  { webPanelHttp.send(500, "application/json", "{\"error\":\"firmware update failed\"}"); return; }
  webPanelHttp.send(200, "application/json", "{\"status\":\"ok\",\"bytes\":" + String((uint32_t)g_otaWrote) + "}");
  delay(300); ESP.restart();
}

// ── Game/image upload (streamed into the RAM disk, then mounted) ────────────

static size_t g_guRecv = 0; static bool g_guOverflow = false; static String g_guName = "";
static void guUpload() {
  HTTPUpload &up = webPanelHttp.upload();
  if (up.status == UPLOAD_FILE_START) {
    hardDetach(); g_guRecv = 0; g_guOverflow = false; g_guName = up.filename;
  } else if (up.status == UPLOAD_FILE_WRITE) {
    uint8_t *dst = g_disk + DATA_LBA * 512;
    if (g_guRecv + up.currentSize <= (size_t)MAX_FILE_BYTES) { memcpy(dst + g_guRecv, up.buf, up.currentSize); g_guRecv += up.currentSize; }
    else g_guOverflow = true;
  }
}
static void guDone() {
  if (g_guOverflow) { hardAttach(); webPanelHttp.send(400, "application/json", "{\"error\":\"Image is larger than this board's volume\"}"); return; }
  if (g_guRecv == 0) { hardAttach(); webPanelHttp.send(400, "application/json", "{\"error\":\"Upload was empty\"}"); return; }
  memset(g_disk, 0, DATA_LBA * 512);
  build_boot_sector(g_disk);
  build_fat(g_disk + RESERVED_SECTORS * 512, (uint32_t)g_guRecv);
  String outn = (g_mode == MODE_GEN) ? g_guName : String(getOutputFilename());
  build_root(g_disk + (RESERVED_SECTORS + SECTORS_PER_FAT) * 512, outn.c_str(), (uint32_t)g_guRecv);
  g_sv_img_size = 0; svDirtyReset();
  hardAttach();
  g_loaded = true;
  String bn = g_guName; const int d = bn.lastIndexOf('.'); if (d > 0) bn = bn.substring(0, d);
  g_loaded_name = bn; g_loaded_path = ""; g_webDavLoaded = "";
  webLog("Web upload mounted: " + g_guName + " (" + String((uint32_t)g_guRecv) + " B)");
  webPanelHttp.send(200, "application/json", "{\"name\":\"" + wpJsonEscape(g_guName) + "\",\"bytes\":" + String((uint32_t)g_guRecv) + "}");
}

#if defined(GTI_WEB_SD_FILES)
// ── SD card file access (WiFi file manager) ────────────────────────────────
static bool wpSdPathOK(const String &p) { return p.indexOf("..") < 0; }
static String wpSdNorm(const String &p) {
  String s = p; if (s.length() == 0) s = "/";
  if (!s.startsWith("/")) s = "/" + s;
  return s;
}

static void hSdList() {
  const String want = wpSdNorm(wpArg("path"));
  if (!wpSdPathOK(want)) { webPanelHttp.send(400, "application/json", "{\"error\":\"bad path\"}"); return; }
  File root = SD_MMC.open(want.c_str());
  if (!root || !root.isDirectory()) { if (root) root.close(); webPanelHttp.send(404, "application/json", "{\"error\":\"no such folder\"}"); return; }
  String jj = "{\"path\":\"" + wpJsonEscape(want) + "\",\"entries\":[";
  bool first = true;
  File e;
  while ((e = root.openNextFile())) {
    String en = e.name();
    const int sl = en.lastIndexOf('/'); if (sl >= 0) en = en.substring(sl + 1);
    if (!first) jj += ","; first = false;
    jj += "{\"name\":\"" + wpJsonEscape(en) + "\",\"dir\":" + String(e.isDirectory() ? "true" : "false");
    jj += ",\"size\":" + String((uint32_t)e.size()) + "}";
    e.close();
    yield();
  }
  root.close();
  jj += "]}";
  webPanelHttp.send(200, "application/json", jj);
}

static void hSdGet() {
  const String want = wpSdNorm(wpArg("path"));
  if (!wpSdPathOK(want)) { webPanelHttp.send(400, "application/json", "{\"error\":\"bad path\"}"); return; }
  File f = SD_MMC.open(want.c_str(), FILE_READ);
  if (!f || f.isDirectory()) { if (f) f.close(); webPanelHttp.send(404, "application/json", "{\"error\":\"no such file\"}"); return; }
  String fn = want; const int sl = fn.lastIndexOf('/'); if (sl >= 0) fn = fn.substring(sl + 1);
  webPanelHttp.sendHeader("Content-Disposition", "attachment; filename=\"" + fn + "\"");
  webPanelHttp.streamFile(f, "application/octet-stream");
  f.close();
}

static File   g_suFile;
static size_t g_suBytes = 0; static bool g_suBad = false; static String g_suName = "";
static void suUpload() {
  HTTPUpload &up = webPanelHttp.upload();
  if (up.status == UPLOAD_FILE_START) {
    g_suBytes = 0; g_suBad = false; g_suName = "";
    if (g_loaded) { g_suBad = true; return; }              // refuse while a disk is mounted
    String dir = wpSdNorm(webPanelHttp.hasArg("path") ? webPanelHttp.arg("path") : String("/"));
    String name = up.filename;
    const int sl = name.lastIndexOf('/');  if (sl >= 0) name = name.substring(sl + 1);
    const int bs = name.lastIndexOf('\\'); if (bs >= 0) name = name.substring(bs + 1);
    if (name.length() == 0 || name.indexOf("..") >= 0 || !wpSdPathOK(dir)) { g_suBad = true; return; }
    g_suName = name;
    String full = dir; if (!full.endsWith("/")) full += "/"; full += name;
    g_suFile = SD_MMC.open(full.c_str(), FILE_WRITE);
    if (!g_suFile) g_suBad = true;
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!g_suBad && g_suFile) { if (g_suFile.write(up.buf, up.currentSize) != up.currentSize) g_suBad = true; else g_suBytes += up.currentSize; }
  } else if (up.status == UPLOAD_FILE_END) {
    if (g_suFile) g_suFile.close();
  }
}
static void suDone() {
  if (g_loaded)                 { webPanelHttp.send(409, "application/json", "{\"error\":\"Unload the mounted disk first - SD writes pause the emulator\"}"); return; }
  if (g_suBad || g_suBytes == 0){ webPanelHttp.send(400, "application/json", "{\"error\":\"upload failed\"}"); return; }
  webLog("SD upload: " + g_suName + " (" + String((uint32_t)g_suBytes) + " B)");
  webPanelHttp.send(200, "application/json", "{\"name\":\"" + wpJsonEscape(g_suName) + "\",\"bytes\":" + String((uint32_t)g_suBytes) + "}");
}

// 5.9.22: recursive delete — remove a folder and everything inside it. Names are
// collected before deleting (deleting while iterating a FAT dir is unsafe).
static bool wpSdRmR(const String &path) {
  File f = SD_MMC.open(path.c_str());
  if (!f) return false;
  if (!f.isDirectory()) { f.close(); return SD_MMC.remove(path.c_str()); }
  std::vector<String> files, dirs;
  File e;
  while ((e = f.openNextFile())) {
    String cn = e.name(); const int sl = cn.lastIndexOf('/'); if (sl >= 0) cn = cn.substring(sl + 1);
    if (e.isDirectory()) dirs.push_back(cn); else files.push_back(cn);
    e.close();
  }
  f.close();
  String base = path; if (!base.endsWith("/")) base += "/";
  bool ok = true;
  for (auto &n : files) { if (!SD_MMC.remove((base + n).c_str())) ok = false; yield(); }
  for (auto &n : dirs)  { if (!wpSdRmR(base + n)) ok = false; yield(); }
  if (!SD_MMC.rmdir(path.c_str())) ok = false;
  return ok;
}

static void hSdDelete() {
  const String want = wpSdNorm(wpArg("path"));
  if (!wpSdPathOK(want) || want == "/") { webPanelHttp.send(400, "application/json", "{\"error\":\"bad path\"}"); return; }
  File f = SD_MMC.open(want.c_str());
  const bool isDir = (f && f.isDirectory());
  if (f) f.close();
  const bool ok = isDir ? wpSdRmR(want) : SD_MMC.remove(want.c_str());
  if (ok) webPanelHttp.send(200, "application/json", "{\"status\":\"ok\"}");
  else    webPanelHttp.send(500, "application/json", "{\"error\":\"could not delete\"}");
}


static void hSdMkdir() {
  if (g_loaded) { webPanelHttp.send(409, "application/json", "{\"error\":\"Unload the mounted disk first\"}"); return; }
  const String want = wpSdNorm(wpArg("path"));
  if (!wpSdPathOK(want) || want == "/") { webPanelHttp.send(400, "application/json", "{\"error\":\"bad path\"}"); return; }
  bool ok = true;                                  // create each level (idempotent)
  for (int i = 1; i <= (int)want.length(); i++) {
    if (i == (int)want.length() || want[i] == '/') {
      String seg = want.substring(0, i);
      if (seg.length() > 1 && !SD_MMC.exists(seg.c_str())) { if (!SD_MMC.mkdir(seg.c_str())) { ok = false; break; } }
    }
  }
  if (ok) webPanelHttp.send(200, "application/json", "{\"status\":\"ok\"}");
  else    webPanelHttp.send(500, "application/json", "{\"error\":\"could not create folder\"}");
}

static void hFiles() { webPanelHttp.sendHeader("Content-Encoding", "gzip"); webPanelHttp.send_P(200, "text/html", (PGM_P)panel_files_gz, panel_files_gz_len); }
#endif  // GTI_WEB_SD_FILES

// ── Route table (registered once, after WiFi is up) ────────────────────────
static void webPanelRegister() {
  webPanelHttp.on("/",               HTTP_GET,  hRoot);
  webPanelHttp.on("/index.html",     HTTP_GET,  hRoot);
  webPanelHttp.on("/panel",          HTTP_GET,  hPanel);   // full SPA
  webPanelHttp.on("/api/system/info",HTTP_GET,  hSysInfo);
  webPanelHttp.on("/api/config",     HTTP_GET,  hConfigGet);
  webPanelHttp.on("/api/config",     HTTP_POST, hConfigPost);
  webPanelHttp.on("/api/games/list", HTTP_GET,  hGamesList);
  webPanelHttp.on("/api/disk/status",HTTP_GET,  hDiskStatus);
  webPanelHttp.on("/api/disk/unload",HTTP_POST, hDiskUnload);
  webPanelHttp.on("/api/system/reboot", HTTP_POST, hReboot);
  webPanelHttp.on("/api/system/ota", HTTP_POST, otaDone, otaUpload);
  webPanelHttp.on("/api/games/upload", HTTP_POST, guDone, guUpload);
  webPanelHttp.on("/api/wifi/status",HTTP_GET,  hWifiStatus);
  webPanelHttp.on("/api/dav/status", HTTP_GET,  hDavStatus);
  webPanelHttp.on("/api/dav/connect",HTTP_POST, hDavConnect);
  webPanelHttp.on("/api/dav/list",   HTTP_GET,  hDavList);
  webPanelHttp.on("/api/dav/rowmeta",HTTP_GET,  hDavRowmeta);
  webPanelHttp.on("/api/dav/nfo",    HTTP_GET,  hDavNfo);
  webPanelHttp.on("/api/dav/load",   HTTP_POST, hDavLoad);
#if defined(GTI_WEB_SD_FILES)
  webPanelHttp.on("/api/sd/list",    HTTP_GET,  hSdList);
  webPanelHttp.on("/api/sd/get",     HTTP_GET,  hSdGet);
  webPanelHttp.on("/api/sd/upload",  HTTP_POST, suDone, suUpload);
  webPanelHttp.on("/api/sd/delete",  HTTP_POST, hSdDelete);
  webPanelHttp.on("/api/sd/mkdir",   HTTP_POST, hSdMkdir);
  webPanelHttp.on("/files",          HTTP_GET,  hFiles);
  // 5.9.39: the shared SPA polls /api/fleet every 15 s (loadFleet) and toasts any non-200 as
  // "Error: Not available on this device". The panel has no fleet roster (that is the GTI_FLEET
  // build), so answer with an empty roster and the SPA simply hides the bar.
  webPanelHttp.on("/api/fleet",      HTTP_GET,  []() { webPanelHttp.send(200, "application/json", "{\"devices\":[]}"); });
#endif
  webPanelHttp.onNotFound([]() { webPanelHttp.send(404, "application/json", "{\"error\":\"Not available on this device\"}"); });
}

// ── Lifecycle, called from the sketch ──────────────────────────────────────

// Kicks a NON-BLOCKING WiFi join; webPanelService() registers routes + begins
// the server once the link is up. WiFi setup is identical to the Webby (STA +
// setSleep(false)); the server is now the same WebServer the Webby uses.
static void webPanelBegin() {
  g_web_on = true;   // 5.9.17: WiFi mode implies the web UI is on (no separate toggle to miss)
  if (g_home_ssid.length() == 0) { webLog("[WEB] HOME_SSID not set - web UI off"); return; }
  g_web_up = false;
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);            // modem sleep drops incoming connections in pure STA
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(g_home_ssid.c_str(), g_home_pass.c_str());
  g_web_joining = true;
  webLog("[WEB] joining " + g_home_ssid + " ...");
}

// Every loop(): finish the join, then service one client + one queued DAV load.
// 5.9.17: stop serving + free the listener so a live MODE switch re-inits cleanly.
static void webPanelStop() {
  if (g_web_srv_started) { webPanelHttp.stop(); if (g_web_mdns) MDNS.end(); }
  g_web_up = false; g_web_joining = false; g_web_srv_started = false; g_web_ap = false; g_web_mdns = false; g_webPendingUnload = false;
}

// lab15p: ESP-NOW mode - serve the same page on the GTi's own Wi-Fi (GTi_Omega-XXXX, 192.168.4.1).
// The radio is already up (espnowBegin started the access point), so there is nothing to join and
// no mDNS. Called by the sketch right after ESP-NOW starts.
__attribute__((unused)) static void webPanelBeginAP() {   // unused on boards whose radio never runs an AP (P4)
  if (!g_web_srv_started) { webPanelRegister(); webPanelHttp.begin(); g_web_srv_started = true; }
  g_web_ap = true; g_web_up = true; g_web_joining = false;
  webLog("[WEB] up on the GTi's own Wi-Fi at http://192.168.4.1/");
}

static void webPanelService() {
  if (g_web_joining && !g_web_up) {
    if (WiFi.status() == WL_CONNECTED) {
      davApplyConfig();
      if (!g_web_srv_started) {                     // register + begin exactly once
        if (MDNS.begin("GTi")) { MDNS.addService("http", "tcp", 80); g_web_mdns = true; }
        webPanelRegister();
        webPanelHttp.begin();
        g_web_srv_started = true;
      }
      g_web_up = true; g_web_joining = false;
      webLog("[WEB] up at http://" + WiFi.localIP().toString() + "/ (GTi.local)");
    }
    return;
  }
  if (!g_web_up) return;
  webPanelHttp.handleClient();
  if (g_webPendingUnload) { g_webPendingUnload = false; doUnload(); g_webDavLoaded = ""; }   // lab15p
  if (g_webPendingDav.length() > 0) {
    const String remote = g_webPendingDav;
    const String name   = g_webPendingName;
    g_webPendingDav = ""; g_webPendingName = "";
    if (doLoadWebdav(remote, name)) g_webDavLoaded = remote;
  }
  davClient.dropIdle();
}
