#pragma once
//
// The GTi web interface, for the panel sketches. (Merge step 2.)
//
// This serves the SAME gzipped page the OMEGAWARE touchscreen and dongles
// serve — one UI across every device — and answers the API it expects, mapped
// onto this firmware's own structures. The page already adapts to what a
// device reports (that is how the dongles hide the SD library), so the first
// increment deliberately reports has_sd:false: upload, WebDAV browsing,
// firmware-over-the-air and the dashboard all work, while the SD-library and
// theme surfaces stay OFF until their exact shape is pinned in review. The
// alternative — guessing endpoints onto categories/favourites/covers — is how
// two trees drift apart.
//
// Rules honoured here, learned the hard way on the other tree:
//  - A disk load is QUEUED and executed from loop(), never inside the HTTP
//    handler. A synchronous handler serves nothing else for the whole seven
//    seconds an HD image takes — including the endpoint you would use to see
//    what went wrong.
//  - OTA streams into the inactive slot (partitions.csv has two) and refuses
//    anything whose first byte is not 0xE9: a panel can be reflashed blind,
//    but rebooting into a slot of garbage means a USB cable and a sad hour.
//
// Include from the sketch AFTER doLoadWebdav() and the disk builders; it uses
// them and nothing else defines them. Requires WEBUI=ON in CONFIG.TXT and
// HOME_SSID/HOME_PASS (or their OMEGAWARE aliases) to join.

#include <Update.h>
#include <ESPmDNS.h>
#include "multipart_scan.h"
#include "webui.h"

static WiFiServer webPanelServer(80);
// g_web_on is defined by the sketch (WEBUI=ON), parsed long before this include.
static bool   g_web_up      = false;   // server actually running
static String g_webPendingDav = "";    // queued remote path; loop() executes
static String g_webPendingName = "";
static String g_webDavLoaded = "";     // remote path of the mounted image, if any

static void webLog(const String &m) { Serial.println(m); }

// ── Request bits (same helpers the dongle uses) ──────────────────────────
static String wpUrlDecode(const String &in) {
  String out; out.reserve(in.length());
  for (unsigned int i = 0; i < in.length(); i++) {
    const char c = in[i];
    if (c == '+') out += ' ';
    else if (c == '%' && i + 2 < in.length()) {
      const char h[3] = { in[i + 1], in[i + 2], 0 };
      out += (char)strtol(h, nullptr, 16); i += 2;
    } else out += c;
  }
  return out;
}

// Whole-key match only, so "path" does not also match "np_path".
static String wpPairValue(const String &blob, const String &key) {
  const String needle = key + "=";
  int at = -1;
  if (blob.startsWith(needle)) at = 0;
  else { const int i = blob.indexOf("&" + needle); if (i >= 0) at = i + 1; }
  if (at < 0) return "";
  const int from = at + needle.length();
  const int amp = blob.indexOf('&', from);
  return wpUrlDecode(amp < 0 ? blob.substring(from) : blob.substring(from, amp));
}

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

static void wpSendHeader(WiFiClient &c, int code, const char *type, int len = -1) {
  c.print("HTTP/1.1 "); c.print(code);
  c.println(code == 200 ? " OK" : (code == 404 ? " Not Found" : " Error"));
  c.print("Content-Type: "); c.println(type);
  if (len >= 0) { c.print("Content-Length: "); c.println(len); }
  c.println("Cache-Control: no-store");
  c.println("Connection: close");
  c.println();
}

static void wpSendJson(WiFiClient &c, int code, const String &body) {
  wpSendHeader(c, code, "application/json", body.length());
  c.print(body);
}

// ── Uploads ──────────────────────────────────────────────────────────────
//
// The image streams straight into the RAM disk's data region while the Gotek
// is detached. Binary-safe: the delimiter scan is MultipartBody's, which is
// host-tested against payloads full of NUL bytes (an ADF is one).
static long wpReceiveImage(WiFiClient &client, const String &boundary, String &nameOut) {
  const String delim = "--" + boundary;
  MultipartBody body;
  if (!body.begin(delim.c_str(), (int)delim.length())) return -1;

  bool inData = false;
  unsigned long deadline = millis() + 10000;
  while (client.connected() && !inData && millis() < deadline) {
    if (!client.available()) { delay(2); continue; }
    String line = client.readStringUntil('\n');
    line.trim();
    const int fn = line.indexOf("filename=\"");
    if (fn >= 0) { const int e = line.indexOf('"', fn + 10); nameOut = line.substring(fn + 10, e); }
    if (line.length() == 0 && nameOut.length() > 0) inData = true;
  }
  if (!inData) return -2;

  hardDetach();
  uint8_t *dst = g_disk + DATA_LBA * 512;
  size_t written = 0; bool overflow = false;
  uint8_t rd[1024];
  unsigned long lastByte = millis();
  while (client.connected()) {
    if (millis() - lastByte > 10000) break;
    const int avail = client.available();
    if (avail <= 0) { delay(2); continue; }
    const int n = client.readBytes(rd, min((int)sizeof(rd), avail));
    if (n <= 0) continue;
    lastByte = millis();
    const bool done = body.feed(rd, n, [&](const uint8_t *p, int len) {
      if (written + len > (size_t)MAX_FILE_BYTES) { overflow = true; return; }
      memcpy(dst + written, p, len);
      written += len;
    });
    if (done || overflow) break;
    yield();
  }
  if (overflow) return -3;
  if (written == 0) return -4;
  return (long)written;
}

// Firmware into the inactive OTA slot. First byte must be 0xE9.
static long wpReceiveFirmware(WiFiClient &client, const String &boundary, String &nameOut) {
  const String delim = "--" + boundary;
  MultipartBody body;
  if (!body.begin(delim.c_str(), (int)delim.length())) return -1;

  bool inData = false;
  unsigned long deadline = millis() + 10000;
  while (client.connected() && !inData && millis() < deadline) {
    if (!client.available()) { delay(2); continue; }
    String line = client.readStringUntil('\n');
    line.trim();
    const int fn = line.indexOf("filename=\"");
    if (fn >= 0) { const int e = line.indexOf('"', fn + 10); nameOut = line.substring(fn + 10, e); }
    if (line.length() == 0 && nameOut.length() > 0) inData = true;
  }
  if (!inData) return -2;

  if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
    webLog("OTA: cannot start: " + String(Update.errorString()));
    Update.abort();
    return -3;
  }
  size_t written = 0; bool failed = false, badMagic = false;
  uint8_t rd[1024];
  unsigned long lastByte = millis();
  while (client.connected()) {
    if (millis() - lastByte > 15000) { failed = true; break; }
    const int avail = client.available();
    if (avail <= 0) { delay(2); continue; }
    const int n = client.readBytes(rd, min((int)sizeof(rd), avail));
    if (n <= 0) continue;
    lastByte = millis();
    const bool done = body.feed(rd, n, [&](const uint8_t *p, int len) {
      if (failed || badMagic) return;
      if (written == 0 && len > 0 && p[0] != 0xE9) { badMagic = true; return; }
      if (Update.write((uint8_t *)p, len) != (size_t)len) { failed = true; return; }
      written += len;
    });
    if (done || failed || badMagic) break;
    yield();
  }
  if (badMagic) { Update.abort(); webLog("OTA: refused, not an ESP32 image"); return -4; }
  if (failed || written == 0) { Update.abort(); webLog("OTA: failed after " + String((uint32_t)written) + " B"); return -5; }
  if (!Update.end(true) || !Update.isFinished()) { webLog("OTA: finalise: " + String(Update.errorString())); return -6; }
  webLog("OTA: wrote " + String((uint32_t)written) + " bytes");
  return (long)written;
}

// ── One HTTP request ─────────────────────────────────────────────────────
static void wpHandleClient(WiFiClient &client) {
  const unsigned long deadline = millis() + 5000;
  while (!client.available() && millis() < deadline) delay(2);
  if (!client.available()) { client.stop(); return; }

  String reqLine = client.readStringUntil('\n');
  reqLine.trim();
  const int sp1 = reqLine.indexOf(' ');
  const int sp2 = reqLine.indexOf(' ', sp1 + 1);
  if (sp1 < 0 || sp2 < 0) { client.stop(); return; }
  const String method = reqLine.substring(0, sp1);
  String path = reqLine.substring(sp1 + 1, sp2);
  String query = "";
  const int qm = path.indexOf('?');
  if (qm >= 0) { query = path.substring(qm + 1); path = path.substring(0, qm); }

  String boundary = "";
  long contentLength = 0;
  while (client.connected()) {
    String h = client.readStringUntil('\n');
    h.trim();
    if (h.length() == 0) break;
    String lower = h; lower.toLowerCase();
    if (lower.startsWith("content-length:")) contentLength = h.substring(15).toInt();
    if (lower.startsWith("content-type:")) {
      const int b = lower.indexOf("boundary=");
      if (b >= 0) boundary = h.substring(b + 9);
      boundary.trim();
    }
  }
  String bodyStr = "";
  if (method == "POST" && boundary.length() == 0 && contentLength > 0 && contentLength < 4096) {
    const unsigned long until = millis() + 3000;
    while ((long)bodyStr.length() < contentLength && millis() < until) {
      if (client.available()) bodyStr += (char)client.read(); else delay(1);
    }
  }

  if (method == "GET" && (path == "/" || path == "/index.html")) {
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html");
    client.println("Content-Encoding: gzip");
    client.print("Content-Length: "); client.println(webui_gz_len);
    client.println("Cache-Control: no-cache");
    client.println("Connection: close");
    client.println();
    uint8_t buf[512];
    for (unsigned int off = 0; off < webui_gz_len; off += sizeof(buf)) {
      const unsigned int n = min((unsigned int)sizeof(buf), webui_gz_len - off);
      memcpy_P(buf, webui_gz + off, n);
      client.write(buf, n);
      yield();
    }
  }
  else if (method == "GET" && path == "/api/system/info") {
    // has_sd:false is deliberate for this increment: the page then hides the
    // SD library and theme surfaces, whose exact endpoint shape (categories,
    // favourites, covers) is the review conversation — not a guess to bake in.
    String j = "{";
    j += "\"firmware\":\"" + String(FW_VERSION) + "\",";
    j += "\"heap_free\":" + String((uint32_t)ESP.getFreeHeap()) + ",";
    j += "\"psram_free\":" + String((uint32_t)ESP.getFreePsram()) + ",";
    j += "\"sd_used_mb\":0,\"sd_total_mb\":0,";
    j += "\"game_count\":0,\"file_count\":0,";
    j += "\"loaded_game\":\"" + wpJsonEscape(g_loaded ? g_loaded_name : String("none")) + "\",";
    j += "\"mode\":\"" + String(g_mode == MODE_ADF ? "ADF" : g_mode == MODE_DSK ? "DSK" : "GEN") + "\",";
    j += "\"theme\":\"GTI\",";
    j += "\"wifi_clients\":0,";
    j += "\"wifi_ip\":\"" + WiFi.localIP().toString() + "\",";
    const bool sta = (WiFi.status() == WL_CONNECTED);
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
    wpSendJson(client, 200, j);
  }
  else if (path == "/api/config") {
    if (method == "POST") {
      // Only fields that arrived are touched; each is persisted through
      // saveConfigKey(), which updates in place or appends.
      struct { const char *form; const char *cfg; String *dst; } sv[] = {
        { "DAV_HOST", "DAV_HOST", &g_dav_host },
        { "DAV_USER", "DAV_USER", &g_dav_user },
        { "DAV_PASS", "DAV_PASS", &g_dav_pass },
        { "DAV_PATH", "DAV_PATH", &g_dav_path },
      };
      for (auto &f : sv) {
        if (bodyStr.indexOf(String(f.form) + "=") >= 0) {
          *f.dst = wpPairValue(bodyStr, f.form);
          saveConfigKey(f.cfg, *f.dst);
        }
      }
      if (bodyStr.indexOf("DAV_PORT=") >= 0) {
        const int p = wpPairValue(bodyStr, "DAV_PORT").toInt();
        g_dav_port = (p > 0 && p < 65536) ? p : 443;
        saveConfigKey("DAV_PORT", String(g_dav_port));
      }
      if (bodyStr.indexOf("DAV_HTTPS=") >= 0) {
        g_dav_https = (wpPairValue(bodyStr, "DAV_HTTPS") == "1");
        saveConfigKey("DAV_HTTPS", g_dav_https ? "ON" : "OFF");
      }
      if (bodyStr.indexOf("DAV_ENABLED=") >= 0) {
        g_dav_on = (wpPairValue(bodyStr, "DAV_ENABLED") == "1");
        saveConfigKey("DAV", g_dav_on ? "ON" : "OFF");
      }
      // Client-WiFi credentials persist under this tree's canonical names.
      // Applied at the next boot: swapping the network out from under the
      // request that carries the change would answer nobody.
      if (bodyStr.indexOf("WIFI_CLIENT_SSID=") >= 0) {
        const String v = wpPairValue(bodyStr, "WIFI_CLIENT_SSID");
        if (v.length()) { g_home_ssid = v; saveConfigKey("HOME_SSID", v); }
      }
      if (bodyStr.indexOf("WIFI_CLIENT_PASS=") >= 0) {
        const String v = wpPairValue(bodyStr, "WIFI_CLIENT_PASS");
        if (v.length()) { g_home_pass = v; saveConfigKey("HOME_PASS", v); }
      }
      // The panel's own options: persisted verbatim through saveConfigKey
      // (update-in-place-or-append), picked up by loadConfig at the next
      // boot. The page says so and offers the reboot button.
      static const char *passThrough[] = {
        "CAROUSEL", "SCREENSAVER", "SSMODE", "SSTIME", "SSFAV", "LANG",
        "FONT", "ROTATE", "COMPACT", "BTNSTYLE", "TAPLOAD", "HOTSWAP",
        "FORCESWAP", "CATEGORIES", "NESTING", "HIVEMIND", "CAP",
        "CRACKTRO", "LOOP"
      };
      for (auto k : passThrough) {
        if (bodyStr.indexOf(String(k) + "=") >= 0) {
          const String v = wpPairValue(bodyStr, k);
          if (v.length()) saveConfigKey(k, v);
        }
      }
      davApplyConfig();
      wpSendJson(client, 200, "{\"status\":\"ok\"}");
    } else {
      // Exactly the keys this firmware supports, nothing borrowed: the page
      // hides every config section whose probe keys are absent, so this list
      // IS the config UI. No THEME/SAVES/AP keys — those sections belong to
      // the OMEGAWARE boards until their surface is pinned.
      String j = "{";
      j += "\"WIFI_CLIENT_ENABLED\":\"1\",";
      j += "\"WIFI_CLIENT_SSID\":\"" + wpJsonEscape(g_home_ssid) + "\",";
      j += "\"WIFI_CLIENT_PASS\":\"" + wpJsonEscape(g_home_pass) + "\",";
      j += "\"DAV_ENABLED\":\"" + String(g_dav_on ? "1" : "0") + "\",";
      j += "\"DAV_HOST\":\"" + wpJsonEscape(g_dav_host) + "\",";
      j += "\"DAV_PORT\":\"" + String(g_dav_port) + "\",";
      j += "\"DAV_HTTPS\":\"" + String(g_dav_https ? "1" : "0") + "\",";
      j += "\"DAV_USER\":\"" + wpJsonEscape(g_dav_user) + "\",";
      j += "\"DAV_PASS\":\"" + wpJsonEscape(g_dav_pass) + "\",";
      j += "\"DAV_PATH\":\"" + wpJsonEscape(g_dav_path) + "\",";
      // The panel's own CONFIG.TXT options, read back from the live globals
      // so the page shows what is actually in effect.
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
      j += "\"CRACKTRO\":\"" + String(g_cracktro) + "\",";
      j += "\"LOOP\":\"" + String(g_loop_cracktro ? "1" : "0") + "\"";
      j += "}";
      wpSendJson(client, 200, j);
    }
  }
  else if (method == "GET" && path == "/api/games/list") {
    wpSendJson(client, 200,
               "{\"mode\":\"ADF\",\"loaded_game\":\"" + wpJsonEscape(g_loaded ? g_loaded_name : String("")) +
               "\",\"loaded_file\":\"" + wpJsonEscape(g_loaded ? g_loaded_name : String("")) + "\",\"games\":[]}");
  }
  else if (method == "GET" && path == "/api/disk/status") {
    String j = "{\"loaded\":" + String(g_loaded ? "true" : "false") + ",";
    j += "\"file\":\"" + wpJsonEscape(g_loaded_name) + "\",\"path\":\"\",";
    j += "\"game\":\"" + wpJsonEscape(g_loaded_name) + "\",";
    j += "\"disk_num\":" + String(g_loaded ? 1 : 0) + ",";
    j += "\"disk_total\":" + String(g_loaded ? 1 : 0) + ",";
    j += "\"source\":\"" + String(g_webDavLoaded.length() ? "DAV" : (g_loaded ? "SD" : "")) + "\",";
    j += "\"name\":\"" + wpJsonEscape(g_loaded_name) + "\",";
    j += "\"np_path\":\"" + wpJsonEscape(g_webDavLoaded) + "\",";
    j += "\"mode\":\"ADF\"}";
    wpSendJson(client, 200, j);
  }
  else if (method == "POST" && path == "/api/disk/unload") {
    doUnload();
    g_webDavLoaded = "";
    wpSendJson(client, 200, "{\"status\":\"ok\"}");
  }
  else if (method == "POST" && path == "/api/system/reboot") {
    // Most panel options apply at boot; this makes "save, reboot" one page.
    wpSendJson(client, 200, "{\"status\":\"ok\"}");
    client.flush(); delay(250); client.stop(); delay(250);
    ESP.restart();
  }
  else if (method == "POST" && path == "/api/system/ota") {
    if (boundary.length() == 0) {
      wpSendJson(client, 400, "{\"error\":\"Not a multipart upload\"}");
    } else {
      String name = "";
      const long n = wpReceiveFirmware(client, boundary, name);
      if (n > 0) {
        wpSendJson(client, 200, "{\"status\":\"ok\",\"bytes\":" + String(n) + "}");
        client.flush(); delay(250); client.stop(); delay(250);
        ESP.restart();
      } else if (n == -4) {
        wpSendJson(client, 400, "{\"error\":\"that is not an ESP32 firmware image\"}");
      } else {
        wpSendJson(client, 500, "{\"error\":\"firmware update failed\"}");
      }
    }
  }
  else if (method == "POST" && path == "/api/games/upload") {
    if (boundary.length() == 0) {
      wpSendJson(client, 400, "{\"error\":\"Not a multipart upload\"}");
    } else {
      String name = "";
      const long n = wpReceiveImage(client, boundary, name);
      if (n > 0) {
        memset(g_disk, 0, DATA_LBA * 512);
        build_boot_sector(g_disk);
        build_fat(g_disk + RESERVED_SECTORS * 512, (uint32_t)n);
        String outn = (g_mode == MODE_GEN) ? name : String(getOutputFilename());
        build_root(g_disk + (RESERVED_SECTORS + SECTORS_PER_FAT) * 512, outn.c_str(), (uint32_t)n);
        g_sv_img_size = 0; svDirtyReset();
        hardAttach();
        g_loaded = true;
        String bn = name; const int d = bn.lastIndexOf('.'); if (d > 0) bn = bn.substring(0, d);
        g_loaded_name = bn; g_loaded_path = ""; g_webDavLoaded = "";
        webLog("Web upload mounted: " + name + " (" + String(n) + " B)");
        wpSendJson(client, 200, "{\"name\":\"" + wpJsonEscape(name) + "\",\"bytes\":" + String(n) + "}");
      } else {
        hardAttach();
        const char *why = n == -3 ? "Image is larger than this board's volume"
                        : n == -2 ? "Could not find the file in the upload"
                        : n == -4 ? "Upload was empty" : "Upload failed";
        wpSendJson(client, 400, String("{\"error\":\"") + why + "\"}");
      }
    }
  }
  else if (method == "GET" && path == "/api/wifi/status") {
    const bool sta = (WiFi.status() == WL_CONNECTED);
    String jj = "{\"ap_active\":false,\"ap_ip\":\"\",\"ap_clients\":0";
    jj += ",\"sta_connected\":" + String(sta ? "true" : "false");
    jj += ",\"sta_ip\":\"" + (sta ? WiFi.localIP().toString() : String("")) + "\"";
    jj += ",\"sta_ssid\":\"" + wpJsonEscape(g_home_ssid) + "\"}";
    wpSendJson(client, 200, jj);
  }
  else if (method == "GET" && path == "/api/dav/status") {
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
    wpSendJson(client, 200, jj);
  }
  else if (method == "POST" && path == "/api/dav/connect") {
    if (!g_dav_on || g_dav_host.length() == 0) {
      wpSendJson(client, 400, "{\"error\":\"WebDAV is not configured yet\"}");
    } else if (WiFi.status() != WL_CONNECTED) {
      wpSendJson(client, 503, "{\"error\":\"Not on a network\"}");
    } else if (davClient.connect()) {
      wpSendJson(client, 200, "{\"status\":\"ok\"}");
    } else {
      wpSendJson(client, 502, "{\"error\":\"" + wpJsonEscape(davClient.lastError()) + "\"}");
    }
  }
  else if (method == "GET" && path == "/api/dav/list") {
    String want = wpPairValue(query, "path");
    if (want.length() == 0) want = "/";
    if (WiFi.status() != WL_CONNECTED) {
      wpSendJson(client, 503, "{\"error\":\"Not on a network\"}");
    } else {
      DAVEntryList entries;
      bool ok = false;
      try { ok = davClient.listDir(want, entries); }
      catch (const std::bad_alloc &) { entries.clear(); }
      if (!ok) {
        wpSendJson(client, 502, "{\"error\":\"" + wpJsonEscape(davClient.lastError()) + "\"}");
      } else {
        String jj = "{\"path\":\"" + wpJsonEscape(want) + "\",\"entries\":[";
        bool first = true;
        for (size_t i = 0; i < entries.size(); i++) {
          if (!entries[i].isDir && (entries[i].hasCover || entries[i].hasNfo)) continue;
          if (!first) jj += ",";
          first = false;
          jj += "{\"name\":\"" + wpJsonEscape(entries[i].name()) + "\"";
          jj += ",\"dir\":" + String(entries[i].isDir ? "true" : "false");
          jj += ",\"size\":" + String(entries[i].size) + "}";
          yield();
        }
        jj += "]}";
        wpSendJson(client, 200, jj);
      }
    }
  }
  else if (method == "GET" && path == "/api/dav/rowmeta") {
    wpSendJson(client, 200, "{\"meta\":[],\"capped\":false}");
  }
  else if (method == "GET" && path == "/api/dav/nfo") {
    const String want = wpPairValue(query, "path");
    static uint8_t nfoBuf[2048];
    const long n = davClient.streamToBuffer(want, nfoBuf, sizeof(nfoBuf) - 1);
    if (n <= 0) wpSendJson(client, 404, "{\"error\":\"No notes there\"}");
    else { nfoBuf[n] = 0; wpSendJson(client, 200, "{\"nfo\":\"" + wpJsonEscape(String((char *)nfoBuf)) + "\"}"); }
  }
  else if (method == "POST" && path == "/api/dav/load") {
    // Queued; executed from loop(). The non-blocking rule is not negotiable —
    // a synchronous load here would freeze this server for the whole transfer.
    const String remote = wpPairValue(bodyStr, "path");
    if (remote.length() == 0) {
      wpSendJson(client, 400, "{\"error\":\"No path given\"}");
    } else if (WiFi.status() != WL_CONNECTED) {
      wpSendJson(client, 503, "{\"error\":\"Not on a network\"}");
    } else if (g_webPendingDav.length() > 0) {
      wpSendJson(client, 409, "{\"error\":\"Already loading something\"}");
    } else {
      String name = remote;
      const int sl = name.lastIndexOf('/');
      if (sl >= 0) name = name.substring(sl + 1);
      g_webPendingDav = remote;
      g_webPendingName = name;
      wpSendJson(client, 200, "{\"status\":\"ok\",\"name\":\"" + wpJsonEscape(name) +
                              "\",\"file\":\"" + wpJsonEscape(remote) + "\"}");
    }
  }
  else {
    wpSendJson(client, 404, "{\"error\":\"Not available on this device\"}");
  }

  client.flush();
  delay(2);
  client.stop();
}

// ── Lifecycle, called from the sketch ────────────────────────────────────

// After setup, once config is loaded. Standalone mode only: the web server
// holds a live STA connection, and how that coexists with ESP-NOW casting is
// the parked question — not answered here by accident.
static void webPanelBegin() {
  if (!g_web_on) return;
  if (g_espnow_started) { webLog("[WEB] wireless dongle mode active - web UI off"); return; }
  if (g_home_ssid.length() == 0) { webLog("[WEB] HOME_SSID not set - web UI off"); return; }
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(g_home_ssid.c_str(), g_home_pass.c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(200);
  if (WiFi.status() != WL_CONNECTED) { webLog("[WEB] WiFi join failed - web UI off"); return; }
  davApplyConfig();
  if (MDNS.begin("gotek")) MDNS.addService("http", "tcp", 80);
  webPanelServer.begin();
  g_web_up = true;
  webLog("[WEB] up at http://" + WiFi.localIP().toString() + "/ (gotek.local)");
}

// Every loop(): serve one client, run one queued DAV load, let the idle pool go.
static void webPanelService() {
  if (!g_web_up) return;
  WiFiClient c = webPanelServer.available();
  if (c) wpHandleClient(c);
  if (g_webPendingDav.length() > 0) {
    const String remote = g_webPendingDav;
    const String name   = g_webPendingName;
    g_webPendingDav = ""; g_webPendingName = "";
    if (doLoadWebdav(remote, name)) g_webDavLoaded = remote;
  }
  davClient.dropIdle();
}
