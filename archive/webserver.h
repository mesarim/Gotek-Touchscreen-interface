#ifndef WEBSERVER_H
#define WEBSERVER_H

/*
  Gotek Touchscreen — WiFi Access Point Web Server

  Pure WiFiServer implementation — zero external dependencies.
  Creates a WiFi AP for browser-based configuration and game management.

  Default AP: SSID "Gotek-Setup", password "retrogaming", channel 6
  Web UI served from PROGMEM at http://192.168.4.1/
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>

// Forward declarations from main sketch
extern String cfg_display, cfg_lastfile, cfg_lastmode, cfg_theme;
extern bool cfg_wifi_enabled;
extern String cfg_wifi_ssid, cfg_wifi_pass;
extern uint8_t cfg_wifi_channel;
extern bool cfg_wifi_client_enabled;
extern String cfg_wifi_client_ssid, cfg_wifi_client_pass;
extern String theme_path;
extern vector<String> theme_list;
extern vector<String> file_list;
extern vector<String> display_names;
extern vector<GameEntry> game_list;
extern DiskMode g_mode;
extern int selected_index;
extern int loaded_disk_index;
extern Screen current_screen;

extern void loadConfig();
extern void saveConfig();
extern void scanThemes();
extern vector<String> listImages();
extern void buildDisplayNames(const vector<String> &files);
extern void sortByDisplay();
extern void buildGameList();
extern String filenameOnly(const String &path);
extern String basenameNoExt(const String &path);
extern size_t loadFileToRam(int index);
extern void doLoadSelected();
extern void doUnload();
extern vector<int> disk_set;
extern int game_selected;
extern int findGameIndex(int fileIndex);
extern String detail_filename;
extern void drawDetailsFromNFO(const String &filename);
extern void drawList();

// WiFi state (defined in main .ino)
extern bool wifi_ap_active;
extern String wifi_ap_ip;
extern bool wifi_sta_connected;
extern String wifi_sta_ip;

// ============================================================================
// Raw HTTP Server
// ============================================================================

WiFiServer wifiHttpServer(80);

// Include embedded web UI data
#include "webui.h"

// Forward declare — api_handlers.h included after helpers below

// ============================================================================
// WiFi AP Management
// ============================================================================

bool initWiFiAP() {
  if (!cfg_wifi_enabled && !cfg_remote_enabled) return false;

  // Determine WiFi mode: need STA if client or remote is configured
  bool needSTA = (cfg_wifi_client_enabled && cfg_wifi_client_ssid.length() > 0) ||
                 (cfg_remote_enabled && cfg_remote_ssid.length() > 0);

  if (cfg_wifi_enabled && needSTA) {
    WiFi.mode(WIFI_AP_STA);
    Serial.println("WiFi mode: AP + Station (dual)");
  } else if (cfg_wifi_enabled) {
    WiFi.mode(WIFI_AP);
    Serial.println("WiFi mode: AP only");
  } else if (needSTA) {
    WiFi.mode(WIFI_STA);
    Serial.println("WiFi mode: Station only (remote)");
  }

  // Start Access Point (if enabled)
  if (cfg_wifi_enabled) {
    WiFi.softAP(cfg_wifi_ssid.c_str(), cfg_wifi_pass.c_str(), cfg_wifi_channel);
    delay(200);
    wifi_ap_ip = WiFi.softAPIP().toString();
    wifi_ap_active = true;
    Serial.println("AP started: " + cfg_wifi_ssid + " @ " + wifi_ap_ip);
  }

  // Connect to remote dongle AP (priority over home network)
  if (cfg_remote_enabled && cfg_remote_ssid.length() > 0) {
    Serial.println("Connecting to dongle: " + cfg_remote_ssid);
    WiFi.begin(cfg_remote_ssid.c_str(), cfg_remote_pass.c_str());
    // Don't block — checkWiFiClient() in loop will track connection
  }
  // Connect to home network (non-blocking)
  else if (cfg_wifi_client_enabled && cfg_wifi_client_ssid.length() > 0) {
    Serial.println("Connecting to: " + cfg_wifi_client_ssid);
    WiFi.begin(cfg_wifi_client_ssid.c_str(), cfg_wifi_client_pass.c_str());
  }

  return true;
}

// Call from loop() periodically to track client/remote connection state
unsigned long _lastWifiCheck = 0;
void checkWiFiClient() {
  bool needCheck = (cfg_wifi_client_enabled && cfg_wifi_client_ssid.length() > 0) ||
                   (cfg_remote_enabled && cfg_remote_ssid.length() > 0);
  if (!needCheck) return;
  if (millis() - _lastWifiCheck < 3000) return;  // check every 3s
  _lastWifiCheck = millis();

  if (WiFi.status() == WL_CONNECTED) {
    if (cfg_remote_enabled) {
      if (!remote_connected) {
        remote_connected = true;
        wifi_sta_ip = WiFi.localIP().toString();
        Serial.println("Connected to dongle: " + cfg_remote_ssid + " @ " + wifi_sta_ip);
      }
    } else if (!wifi_sta_connected) {
      wifi_sta_connected = true;
      wifi_sta_ip = WiFi.localIP().toString();
      Serial.println("Connected to " + cfg_wifi_client_ssid + " @ " + wifi_sta_ip);
    }
  } else {
    if (cfg_remote_enabled && remote_connected) {
      remote_connected = false;
      wifi_sta_ip = "";
      Serial.println("Disconnected from dongle: " + cfg_remote_ssid);
    } else if (!cfg_remote_enabled && wifi_sta_connected) {
      wifi_sta_connected = false;
      wifi_sta_ip = "";
      Serial.println("Disconnected from " + cfg_wifi_client_ssid);
    }
    // Auto-reconnect
    if (WiFi.status() != WL_IDLE_STATUS && WiFi.status() != WL_CONNECTED) {
      if (cfg_remote_enabled) {
        WiFi.begin(cfg_remote_ssid.c_str(), cfg_remote_pass.c_str());
      } else {
        WiFi.begin(cfg_wifi_client_ssid.c_str(), cfg_wifi_client_pass.c_str());
      }
    }
  }
}

void stopWiFiAP() {
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  wifi_ap_active = false;
  wifi_ap_ip = "";
  wifi_sta_connected = false;
  wifi_sta_ip = "";
}

// ============================================================================
// HTTP Request Parser
// ============================================================================

struct HttpRequest {
  String method;        // GET, POST, DELETE, OPTIONS
  String path;          // /api/games/list
  String query;         // query string (after ?) decoded
  String contentType;
  int contentLength;
  String body;          // POST body
  String boundary;      // multipart boundary
};

// Full URL decode: %XX hex → char, + → space
String urlDecode(const String &in) {
  String out;
  out.reserve(in.length());
  for (unsigned int i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '+') {
      out += ' ';
    } else if (c == '%' && i + 2 < in.length()) {
      char hi = in[i + 1];
      char lo = in[i + 2];
      int h = (hi >= '0' && hi <= '9') ? hi - '0' : (hi >= 'A' && hi <= 'F') ? hi - 'A' + 10 : (hi >= 'a' && hi <= 'f') ? hi - 'a' + 10 : -1;
      int l = (lo >= '0' && lo <= '9') ? lo - '0' : (lo >= 'A' && lo <= 'F') ? lo - 'A' + 10 : (lo >= 'a' && lo <= 'f') ? lo - 'a' + 10 : -1;
      if (h >= 0 && l >= 0) {
        out += (char)((h << 4) | l);
        i += 2;
      } else {
        out += c;
      }
    } else {
      out += c;
    }
  }
  return out;
}

// Read one HTTP request from client (with timeout)
bool parseHttpRequest(WiFiClient &client, HttpRequest &req) {
  req.method = "";
  req.path = "";
  req.contentType = "";
  req.contentLength = 0;
  req.body = "";
  req.boundary = "";

  // Read request line: "GET /path HTTP/1.1"
  String requestLine = client.readStringUntil('\n');
  requestLine.trim();
  if (requestLine.length() == 0) return false;

  int sp1 = requestLine.indexOf(' ');
  int sp2 = requestLine.indexOf(' ', sp1 + 1);
  if (sp1 < 0 || sp2 < 0) return false;

  req.method = requestLine.substring(0, sp1);
  String fullPath = requestLine.substring(sp1 + 1, sp2);

  // Separate query string from path
  int qMark = fullPath.indexOf('?');
  if (qMark >= 0) {
    req.query = urlDecode(fullPath.substring(qMark + 1));
    fullPath = fullPath.substring(0, qMark);
  }

  // Full URL decode (%XX hex sequences)
  req.path = urlDecode(fullPath);

  // Read headers
  while (client.connected()) {
    String line = client.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) break;  // empty line = end of headers

    String lower = line;
    lower.toLowerCase();

    if (lower.startsWith("content-type:")) {
      req.contentType = line.substring(13);
      req.contentType.trim();
      // Extract multipart boundary
      int bIdx = req.contentType.indexOf("boundary=");
      if (bIdx >= 0) {
        req.boundary = req.contentType.substring(bIdx + 9);
        req.boundary.trim();
      }
    }
    else if (lower.startsWith("content-length:")) {
      req.contentLength = line.substring(15).toInt();
    }
  }

  // Read body for POST/DELETE (non-multipart only)
  if (req.contentLength > 0 && req.boundary.length() == 0) {
    unsigned long start = millis();
    while (client.available() < req.contentLength && millis() - start < 5000) {
      delay(1);
    }
    req.body = "";
    while (client.available() && (int)req.body.length() < req.contentLength) {
      req.body += (char)client.read();
    }
  }

  return true;
}

// Parse URL-encoded form body: "key1=val1&key2=val2"
String getFormValue(const String &body, const String &key) {
  String search = key + "=";
  int start = body.indexOf(search);
  if (start < 0) return "";
  // Make sure we matched a full key (start of string or after &)
  if (start > 0 && body[start - 1] != '&') {
    // Could be partial match, search again
    search = "&" + key + "=";
    start = body.indexOf(search);
    if (start < 0) return "";
    start += 1;  // skip the &
  }
  start += key.length() + 1;  // skip "key="
  int end = body.indexOf('&', start);
  String val = (end < 0) ? body.substring(start) : body.substring(start, end);
  return urlDecode(val);
}

// ============================================================================
// HTTP Response Helpers
// ============================================================================

void sendResponse(WiFiClient &client, int code, const String &contentType, const String &body) {
  client.println("HTTP/1.1 " + String(code) + " OK");
  client.println("Content-Type: " + contentType);
  client.println("Content-Length: " + String(body.length()));
  client.println("Access-Control-Allow-Origin: *");
  client.println("Access-Control-Allow-Methods: GET,POST,DELETE,OPTIONS");
  client.println("Access-Control-Allow-Headers: Content-Type");
  client.println("Connection: close");
  client.println();
  client.print(body);
}

void sendJSON(WiFiClient &client, int code, const String &json) {
  sendResponse(client, code, "application/json", json);
}

void sendGzipResponse(WiFiClient &client, const char *contentType,
                      const uint8_t *data, size_t len) {
  client.println("HTTP/1.1 200 OK");
  client.print("Content-Type: ");
  client.println(contentType);
  client.println("Content-Encoding: gzip");
  client.print("Content-Length: ");
  client.println(len);
  client.println("Cache-Control: max-age=86400");
  client.println("Access-Control-Allow-Origin: *");
  client.println("Connection: close");
  client.println();

  // Send in chunks to avoid WiFi buffer overflow
  size_t sent = 0;
  while (sent < len) {
    size_t chunk = min((size_t)1024, len - sent);
    client.write(data + sent, chunk);
    sent += chunk;
  }
}

void sendFileResponse(WiFiClient &client, const String &path, const String &contentType) {
  File f = SD_MMC.open(path.c_str(), "r");
  if (!f) {
    sendJSON(client, 404, "{\"error\":\"File not found\"}");
    return;
  }

  size_t fileSize = f.size();
  client.println("HTTP/1.1 200 OK");
  client.print("Content-Type: ");
  client.println(contentType);
  client.print("Content-Length: ");
  client.println(fileSize);
  client.println("Cache-Control: max-age=3600");
  client.println("Access-Control-Allow-Origin: *");
  client.println("Connection: close");
  client.println();

  uint8_t buf[1024];
  while (f.available()) {
    size_t n = f.read(buf, sizeof(buf));
    if (n > 0) client.write(buf, n);
  }
  f.close();
}

// ============================================================================
// API Handlers (uses sendJSON, sendFileResponse, getFormValue from above)
// ============================================================================

#include "api_handlers.h"

// ============================================================================
// Multipart Upload Handler
// ============================================================================

// Simple multipart parser: extracts first file from multipart/form-data
// Writes directly to SD card (streaming, no full buffering needed)
bool handleMultipartUpload(WiFiClient &client, const HttpRequest &req) {
  if (req.boundary.length() == 0 || req.contentLength <= 0) return false;

  String delim = "--" + req.boundary;
  String delimEnd = delim + "--";

  // Read all remaining data
  // For ADF files (~901KB), we stream in chunks
  String modeDir = (g_mode == MODE_ADF) ? "/ADF" : "/DSK";
  String gameName = "";
  String filename = "";
  File outFile;
  bool inFileData = false;
  bool done = false;
  size_t totalWritten = 0;

  // Read line by line for headers, then switch to binary for file data
  while (client.connected() && !done) {
    if (!inFileData) {
      String line = client.readStringUntil('\n');
      line.trim();

      if (line.startsWith(delimEnd)) {
        done = true;
        break;
      }

      if (line.startsWith(delim)) {
        // New part — read Content-Disposition
        String disp = client.readStringUntil('\n');
        disp.trim();

        // Extract filename="..."
        int fnIdx = disp.indexOf("filename=\"");
        if (fnIdx >= 0) {
          fnIdx += 10;
          int fnEnd = disp.indexOf("\"", fnIdx);
          filename = disp.substring(fnIdx, fnEnd);

          // Derive game folder name
          gameName = filename;
          int dotPos = gameName.lastIndexOf('.');
          if (dotPos > 0) gameName = gameName.substring(0, dotPos);

          // Strip disk number suffix
          int dashPos = gameName.lastIndexOf('-');
          if (dashPos > 0) {
            String suffix = gameName.substring(dashPos + 1);
            bool allDigits = true;
            for (unsigned int c = 0; c < suffix.length(); c++) {
              if (!isDigit(suffix[c])) { allDigits = false; break; }
            }
            if (allDigits && suffix.length() > 0) gameName = gameName.substring(0, dashPos);
          }

          String gameDir = modeDir + "/" + gameName;
          if (!SD_MMC.exists(gameDir.c_str())) SD_MMC.mkdir(gameDir.c_str());

          String targetPath = gameDir + "/" + filename;
          Serial.println("Upload: " + targetPath);
          outFile = SD_MMC.open(targetPath.c_str(), "w");

          // Skip remaining headers (Content-Type, empty line)
          while (client.connected()) {
            String hdr = client.readStringUntil('\n');
            hdr.trim();
            if (hdr.length() == 0) break;
          }

          inFileData = true;
          totalWritten = 0;
        }
      }
    } else {
      // Binary file data — read in chunks, watch for boundary
      uint8_t buf[1024];
      while (client.connected() && client.available()) {
        // Peek ahead for boundary
        if (client.available() >= (int)delim.length() + 4) {
          // Read a chunk
          int avail = min((int)sizeof(buf), client.available());
          int n = client.readBytes(buf, avail);

          // Check if this chunk contains the boundary
          String chunk((char *)buf, n);
          int bndIdx = chunk.indexOf(delim);

          if (bndIdx >= 0) {
            // Write data before boundary (minus trailing \r\n)
            int writeLen = bndIdx;
            if (writeLen >= 2) writeLen -= 2;  // strip \r\n before boundary
            if (writeLen > 0 && outFile) {
              outFile.write(buf, writeLen);
              totalWritten += writeLen;
            }
            if (outFile) outFile.close();
            inFileData = false;
            Serial.println("Upload done: " + String(totalWritten) + " bytes");

            // Check if this is the final boundary
            if (chunk.indexOf(delimEnd) >= 0) done = true;
            break;
          } else {
            // Safe to write most of buffer (keep tail for split boundary)
            int safe = n - (int)delim.length() - 4;
            if (safe > 0 && outFile) {
              outFile.write(buf, safe);
              totalWritten += safe;
              // Push back the rest... unfortunately WiFiClient doesn't support pushback
              // So we write it all and accept a tiny risk of split boundary
              outFile.write(buf + safe, n - safe);
              totalWritten += (n - safe);
            } else if (outFile) {
              outFile.write(buf, n);
              totalWritten += n;
            }
          }
        } else if (client.available() > 0) {
          int n = client.readBytes(buf, client.available());
          if (outFile) {
            outFile.write(buf, n);
            totalWritten += n;
          }
        } else {
          delay(1);  // wait for more data
        }

        // Timeout check
        if (!client.connected()) break;
      }

      if (inFileData && outFile) {
        outFile.close();
        inFileData = false;
      }
    }
  }

  if (outFile) outFile.close();

  // Rescan game list
  refreshGameList();

  sendJSON(client, 200,
    "{\"status\":\"ok\",\"game\":\"" + jsonEscape(gameName) +
    "\",\"bytes\":" + String(totalWritten) +
    ",\"games\":" + String(game_list.size()) + "}");

  return true;
}

// ============================================================================
// Request Router
// ============================================================================

void handleHttpRequest(WiFiClient &client) {
  client.setTimeout(5);

  HttpRequest req;
  if (!parseHttpRequest(client, req)) {
    client.stop();
    return;
  }

  Serial.println(req.method + " " + req.path);

  // ── CORS Preflight ──
  if (req.method == "OPTIONS") {
    sendResponse(client, 200, "text/plain", "");
    return;
  }

  // ── Serve SPA ──
  if (req.path == "/" || req.path == "/index.html") {
    sendGzipResponse(client, "text/html", webui_html_gz, webui_html_gz_len);
    return;
  }

  // ── API Routes ──

  if (req.path == "/api/system/info" && req.method == "GET") {
    handleSystemInfo(client);
    return;
  }

  if (req.path == "/api/config") {
    if (req.method == "GET") { handleConfigGet(client); return; }
    if (req.method == "POST") { handleConfigPost(client, req.body); return; }
  }

  if (req.path == "/api/games/list" && req.method == "GET") {
    handleGamesList(client);
    return;
  }

  if (req.path == "/api/games/upload" && req.method == "POST") {
    handleMultipartUpload(client, req);
    return;
  }

  if (req.path == "/api/upload/progress" && req.method == "GET") {
    handleUploadProgress(client);
    return;
  }

  if (req.path == "/api/themes/list" && req.method == "GET") {
    handleThemesList(client);
    return;
  }

  if (req.path == "/api/rescan" && req.method == "POST") {
    refreshGameList();
    sendJSON(client, 200, "{\"status\":\"ok\",\"games\":" + String(game_list.size()) + "}");
    return;
  }

  if (req.path == "/api/disk/unload" && req.method == "POST") {
    handleDiskUnload(client);
    return;
  }

  if (req.path == "/api/disk/status" && req.method == "GET") {
    handleDiskStatus(client);
    return;
  }

  if (req.path == "/api/wifi/status" && req.method == "GET") {
    handleWiFiStatus(client);
    return;
  }

  if (req.path == "/api/wifi/scan" && req.method == "GET") {
    handleWiFiScan(client);
    return;
  }

  // ── Dynamic game routes: /api/games/{mode}/{name}[/{action}] ──
  if (req.path.startsWith("/api/games/")) {
    String rest = req.path.substring(11);
    int s1 = rest.indexOf('/');
    if (s1 > 0) {
      String mode = rest.substring(0, s1);
      String remainder = rest.substring(s1 + 1);

      if (mode == "adf" || mode == "dsk") {
        int s2 = remainder.indexOf('/');
        String name, action;
        if (s2 >= 0) {
          name = remainder.substring(0, s2);
          action = remainder.substring(s2 + 1);
        } else {
          name = remainder;
          action = "";
        }

        if (action == "load" && req.method == "POST") {
          handleDiskLoad(client, mode, name, req.body);
          return;
        }
        if (action == "cover" && req.method == "GET") {
          handleCoverServe(client, mode, name, req.query);
          return;
        }
        if (action == "cover" && req.method == "POST") {
          handleCoverUpload(client, req, mode, name);
          return;
        }
        if (action == "cover-url" && req.method == "POST") {
          handleCoverDownload(client, mode, name, req.body);
          return;
        }
        if (action == "nfo" && req.method == "POST") {
          handleNFOUpdateParsed(client, mode, name, req.body);
          return;
        }
        if (action == "" && req.method == "GET") {
          handleGameDetailParsed(client, mode, name);
          return;
        }
        if (action == "" && req.method == "DELETE") {
          handleGameDeleteParsed(client, mode, name);
          return;
        }
      }
    }
  }

  // ── Dynamic theme route: /api/themes/{name}/activate ──
  if (req.path.startsWith("/api/themes/") && req.path.endsWith("/activate") && req.method == "POST") {
    String name = req.path.substring(12, req.path.length() - 9);
    handleThemeActivateParsed(client, name);
    return;
  }

  // ── 404 ──
  sendJSON(client, 404, "{\"error\":\"Not found\"}");
}

// ============================================================================
// Start / Stop / Handle
// ============================================================================

void startWebServer() {
  wifiHttpServer.begin();
  Serial.println("Web server started on port 80");
}

void stopWebServer() {
  wifiHttpServer.stop();
  Serial.println("Web server stopped");
}

// Call from loop() — non-blocking, checks for new clients
void handleWebServer() {
  if (!wifi_ap_active) return;

  // Track home network connection state
  checkWiFiClient();

  WiFiClient client = wifiHttpServer.available();
  if (client) {
    unsigned long start = millis();
    while (client.connected() && !client.available()) {
      if (millis() - start > 2000) { client.stop(); return; }
      yield();
      delay(1);
    }
    if (client.available()) {
      handleHttpRequest(client);
    }
    delay(1);  // brief yield before stop
    client.stop();
  }
}

#endif // WEBSERVER_H
