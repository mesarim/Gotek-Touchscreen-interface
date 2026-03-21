// espnow_server.cpp — CYD (2432S028) side
// Identical protocol to Waveshare version.
// Change from Waveshare: SD_MMC → SD (SPI), and ESP32 Arduino core WiFi / ESP-NOW API.
//
// NOTE: Uses the classic esp_now C API (esp_now.h) directly.
//       Arduino core 3.x (ESP-IDF 5.x) changed both callback signatures.
//       We use compile-time detection to handle both core 2.x and 3.x.

#include "espnow_server.h"
#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <SD.h>
#include <WiFiClient.h>

#define ESPNOW_CHANNEL 6

// ── State ─────────────────────────────────────────────────────────────────────
volatile bool     g_espnow_paired                = false;
volatile bool     g_espnow_xiao_ready            = false;
volatile bool     g_espnow_xiao_done             = false;
volatile bool     g_espnow_xiao_error            = false;
volatile bool     g_espnow_link_just_established = false;
volatile uint32_t g_espnow_xiao_last_seen        = 0;
volatile bool     g_espnow_pair_waiting          = false;

static uint8_t _xiao_mac[6] = {0};
static String  _xiao_ip     = XIAO_AP_IP;
static String  _xiao_name   = "";
static bool    _accept_next = false;

static uint8_t _pending_mac[6] = {0};
static String  _pending_name   = "";

// SD CS pin for CYD — see main sketch
extern int g_sd_cs_pin;

// ── Packet structs ────────────────────────────────────────────────────────────
#pragma pack(push,1)
struct PktHello {
  uint8_t type;
  uint8_t mac[6];
  char    ip[16];
  char    name[12];
  uint8_t pad[215];
};
struct PktConfirm {
  uint8_t type;
  uint8_t wave_mac[6];
  char    xiao_name[12];
  uint8_t pad[231];
};
struct PktSimple { uint8_t type; uint8_t pad[249]; };
#pragma pack(pop)

// ── Forward declarations ──────────────────────────────────────────────────────
static void handleIncoming(const uint8_t* data, int len, const uint8_t* src_mac);
static void sendConfirmTo(const uint8_t* mac, const char* name);
static void saveConfig();
static void restoreEspNow();

// ── Classic esp_now callbacks — core 2.x vs 3.x signature detection ──────────
// Core 3.x (ESP-IDF 5.x) changed recv cb: mac arg → esp_now_recv_info_t*
// Core 3.x changed send cb: mac arg → wifi_tx_info_t*
// ESP_IDF_VERSION is defined by the framework and lets us detect this cleanly.
#include <esp_idf_version.h>

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
// ── Core 3.x / IDF 5.x signatures ────────────────────────────────────────────
static void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  const uint8_t* mac = info ? info->src_addr : nullptr;
  handleIncoming(data, len, mac);
}
static void onSent(const wifi_tx_info_t*, esp_now_send_status_t) {}
#else
// ── Core 2.x / IDF 4.x signatures ────────────────────────────────────────────
static void onRecv(const uint8_t* mac, const uint8_t* data, int len) {
  handleIncoming(data, len, mac);
}
static void onSent(const uint8_t*, esp_now_send_status_t) {}
#endif

// ── Incoming handler ──────────────────────────────────────────────────────────
static void handleIncoming(const uint8_t* data, int len, const uint8_t* src_mac) {
  if (!data || len < 1) return;
  uint8_t type = data[0];

  if (type == PKT_PAIR_HELLO) {
    const PktHello* p = (const PktHello*)data;
    String inName = String(p->name);
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             p->mac[0],p->mac[1],p->mac[2],p->mac[3],p->mac[4],p->mac[5]);
    Serial.printf("[NOW] HELLO from %s name=%s\n", macStr, inName.c_str());

    if (g_espnow_paired && inName == _xiao_name) {
      g_espnow_xiao_last_seen = millis();
      sendConfirmTo(p->mac, inName.c_str());
      return;
    }

    if (_accept_next) {
      _accept_next = false;
      memcpy(_xiao_mac, p->mac, 6);
      _xiao_ip   = String(p->ip);
      _xiao_name = inName;
      g_espnow_paired                = true;
      g_espnow_link_just_established = true;
      g_espnow_xiao_last_seen        = millis();
      g_espnow_pair_waiting          = false;
      _pending_name = "";
      memset(_pending_mac, 0, 6);

      // Register as peer
      esp_now_peer_info_t pi = {};
      memcpy(pi.peer_addr, _xiao_mac, 6);
      pi.channel = ESPNOW_CHANNEL;
      pi.encrypt = false;
      if (!esp_now_is_peer_exist(_xiao_mac)) esp_now_add_peer(&pi);

      saveConfig();
      sendConfirmTo(_xiao_mac, inName.c_str());
      Serial.printf("[NOW] Paired: %s %s\n", inName.c_str(), macStr);
      return;
    }

    memcpy(_pending_mac, p->mac, 6);
    _pending_name         = inName;
    g_espnow_pair_waiting = true;
    Serial.printf("[NOW] Pending: %s — tap PAIR NOW\n", inName.c_str());
    return;
  }

  if (type == PKT_PAIR_REPLY) {
    g_espnow_xiao_last_seen = millis();
    Serial.println("[NOW] PAIR_REPLY — XIAO session locked");
    return;
  }

  if (g_espnow_paired) {
    if (src_mac && memcmp(src_mac, _xiao_mac, 6) != 0) return;
    if (type == PKT_XIAO_READY) { g_espnow_xiao_last_seen=millis(); g_espnow_xiao_ready=true; return; }
    if (type == PKT_XIAO_DONE)  { g_espnow_xiao_last_seen=millis(); g_espnow_xiao_done=true;  g_espnow_xiao_error=false; return; }
    if (type == PKT_XIAO_ERROR) { g_espnow_xiao_last_seen=millis(); g_espnow_xiao_error=true; g_espnow_xiao_done=false;  return; }
  }
}

// ── Send PKT_PAIR_CONFIRM ─────────────────────────────────────────────────────
static void sendConfirmTo(const uint8_t* mac, const char* name) {
  if (!esp_now_is_peer_exist(mac)) {
    esp_now_peer_info_t pi = {};
    memcpy(pi.peer_addr, mac, 6);
    pi.channel = ESPNOW_CHANNEL;
    pi.encrypt = false;
    esp_now_add_peer(&pi);
  }
  PktConfirm pkt = {};
  pkt.type = PKT_PAIR_CONFIRM;
  WiFi.macAddress(pkt.wave_mac);
  strncpy(pkt.xiao_name, name, 11);
  esp_now_send(mac, (uint8_t*)&pkt, sizeof(pkt));
}

// ── Config (SD SPI) ───────────────────────────────────────────────────────────
static void saveConfig() {
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           _xiao_mac[0],_xiao_mac[1],_xiao_mac[2],
           _xiao_mac[3],_xiao_mac[4],_xiao_mac[5]);

  String lines = "";
  bool macW=false, nameW=false, ipW=false;
  File fr = SD.open("/CONFIG.TXT", FILE_READ);
  if (fr) {
    while (fr.available()) {
      String line = fr.readStringUntil('\n'); line.trim();
      if      (line.startsWith("XIAO_MAC="))  { lines += "XIAO_MAC="  + String(macStr) + "\n"; macW=true; }
      else if (line.startsWith("XIAO_NAME=")) { lines += "XIAO_NAME=" + _xiao_name     + "\n"; nameW=true; }
      else if (line.startsWith("XIAO_IP="))   { lines += "XIAO_IP="   + _xiao_ip       + "\n"; ipW=true; }
      else { lines += line + "\n"; }
    }
    fr.close();
  }
  if (!macW)  lines += "XIAO_MAC="  + String(macStr) + "\n";
  if (!nameW) lines += "XIAO_NAME=" + _xiao_name     + "\n";
  if (!ipW)   lines += "XIAO_IP="   + _xiao_ip       + "\n";
  SD.remove("/CONFIG.TXT");
  File fw = SD.open("/CONFIG.TXT", FILE_WRITE);
  if (fw) { fw.print(lines); fw.close(); }
  Serial.printf("[NOW] Saved: MAC=%s NAME=%s\n", macStr, _xiao_name.c_str());
}

static void loadConfig() {
  File f = SD.open("/CONFIG.TXT", FILE_READ);
  if (!f) return;
  while (f.available()) {
    String line = f.readStringUntil('\n'); line.trim();
    if (line.startsWith("#")) continue;
    if (line.startsWith("XIAO_MAC=")) {
      String mac = line.substring(9);
      sscanf(mac.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
             &_xiao_mac[0],&_xiao_mac[1],&_xiao_mac[2],
             &_xiao_mac[3],&_xiao_mac[4],&_xiao_mac[5]);
      bool z=true; for(int i=0;i<6;i++) if(_xiao_mac[i]) { z=false; break; }
      if (!z) g_espnow_paired = true;
    }
    if (line.startsWith("XIAO_NAME=")) _xiao_name = line.substring(10);
    if (line.startsWith("XIAO_IP="))   _xiao_ip   = line.substring(8);
  }
  f.close();
  if (g_espnow_paired)
    Serial.printf("[NOW] Loaded: NAME=%s IP=%s\n", _xiao_name.c_str(), _xiao_ip.c_str());
}

// ── espnowBegin ───────────────────────────────────────────────────────────────
void espnowBegin() {
  WiFi.mode(WIFI_STA);
  // Fix channel for ESP-NOW
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) { Serial.println("[NOW] FAILED"); return; }
  esp_now_register_recv_cb(onRecv);
  esp_now_register_send_cb(onSent);

  // Broadcast peer
  esp_now_peer_info_t pi = {};
  memset(pi.peer_addr, 0xFF, 6);
  pi.channel = ESPNOW_CHANNEL;
  pi.encrypt = false;
  esp_now_add_peer(&pi);

  loadConfig();
  if (g_espnow_paired) {
    esp_now_peer_info_t xp = {};
    memcpy(xp.peer_addr, _xiao_mac, 6);
    xp.channel = ESPNOW_CHANNEL;
    xp.encrypt = false;
    if (!esp_now_is_peer_exist(_xiao_mac)) esp_now_add_peer(&xp);
  }
  Serial.println("[NOW] begin OK");
}

// ── espnowListenForPair ───────────────────────────────────────────────────────
void espnowListenForPair() {
  if (_pending_name.length() > 0) {
    _accept_next = true;
    uint8_t fake[sizeof(PktHello)] = {};
    PktHello* p = (PktHello*)fake;
    p->type = PKT_PAIR_HELLO;
    memcpy(p->mac, _pending_mac, 6);
    strncpy(p->ip,   XIAO_AP_IP,       15);
    strncpy(p->name, _pending_name.c_str(), 11);
    handleIncoming(fake, sizeof(PktHello), _pending_mac);
  } else {
    _accept_next = true;
    Serial.println("[NOW] Armed — waiting for XIAO hello");
  }
}

// ── espnowClearPairing ────────────────────────────────────────────────────────
void espnowClearPairing() {
  if (g_espnow_paired && esp_now_is_peer_exist(_xiao_mac))
    esp_now_del_peer(_xiao_mac);

  g_espnow_paired         = false;
  g_espnow_pair_waiting   = false;
  g_espnow_xiao_done      = false;
  g_espnow_xiao_error     = false;
  g_espnow_xiao_last_seen = 0;
  memset(_xiao_mac, 0, 6);
  _xiao_name = "";
  _xiao_ip   = XIAO_AP_IP;
  _accept_next = false;
  memset(_pending_mac, 0, 6);
  _pending_name = "";

  String lines = "";
  File fr = SD.open("/CONFIG.TXT", FILE_READ);
  if (fr) {
    while (fr.available()) {
      String line = fr.readStringUntil('\n'); line.trim();
      if (line.startsWith("XIAO_MAC=") ||
          line.startsWith("XIAO_NAME=") ||
          line.startsWith("XIAO_IP=")) continue;
      lines += line + "\n";
    }
    fr.close();
  }
  SD.remove("/CONFIG.TXT");
  File fw = SD.open("/CONFIG.TXT", FILE_WRITE);
  if (fw) { fw.print(lines); fw.close(); }
  Serial.println("[NOW] Pairing cleared");
}

// ── API ───────────────────────────────────────────────────────────────────────
bool   espnowIsPaired()       { return g_espnow_paired; }
String espnowGetPendingName() { return _pending_name; }
String espnowGetXiaoMac() {
  char buf[18];
  snprintf(buf,sizeof(buf),"%02X:%02X:%02X:%02X:%02X:%02X",
           _xiao_mac[0],_xiao_mac[1],_xiao_mac[2],
           _xiao_mac[3],_xiao_mac[4],_xiao_mac[5]);
  return String(buf);
}
String espnowGetXiaoName()  { return _xiao_name.length() ? _xiao_name : "?"; }
String espnowGetSSIDLabel() { return "WiFi+NOW"; }
bool   espnowXiaoOnline()   {
  if (!g_espnow_paired || g_espnow_xiao_last_seen==0) return false;
  return (millis()-g_espnow_xiao_last_seen) < 30000;
}
bool espnowSendNotify(const String&, const String&, uint32_t) {
  g_espnow_xiao_ready=false; g_espnow_xiao_done=false; g_espnow_xiao_error=false;
  return true;
}

// ── Restore ESP-NOW after TCP ─────────────────────────────────────────────────
static void restoreEspNow() {
  esp_now_deinit();
  WiFi.disconnect(); delay(200);
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_now_init();
  esp_now_register_recv_cb(onRecv);
  esp_now_register_send_cb(onSent);

  esp_now_peer_info_t pi = {};
  memset(pi.peer_addr, 0xFF, 6);
  pi.channel = ESPNOW_CHANNEL;
  pi.encrypt = false;
  if (!esp_now_is_peer_exist(pi.peer_addr)) esp_now_add_peer(&pi);

  if (g_espnow_paired) {
    esp_now_peer_info_t xp = {};
    memcpy(xp.peer_addr, _xiao_mac, 6);
    xp.channel = ESPNOW_CHANNEL;
    xp.encrypt = false;
    if (!esp_now_is_peer_exist(_xiao_mac)) esp_now_add_peer(&xp);
  }
  Serial.println("[NOW] restored");
}

// ── espnowSendDisk ────────────────────────────────────────────────────────────
// CYD build: streams direct from SD (no PSRAM — can't buffer full ADF in RAM).
// g_queued_adf_path / g_queued_adf_size are set by Gotek_CYD.ino before calling.
extern String   g_queued_adf_path;
extern uint32_t g_queued_adf_size;

bool espnowSendDisk(uint32_t size) {
  String ip = (_xiao_ip.length()>0) ? _xiao_ip : String(XIAO_AP_IP);
  Serial.printf("[TCP] %s:%d\n", ip.c_str(), XIAO_TCP_PORT);

  esp_now_deinit(); delay(100);
  WiFi.mode(WIFI_STA); delay(100);
  WiFi.begin(XIAO_AP_SSID, XIAO_AP_PASS);
  uint32_t t0=millis();
  while (WiFi.status()!=WL_CONNECTED && millis()-t0<15000) { delay(200); Serial.print("."); }
  Serial.println();
  if (WiFi.status()!=WL_CONNECTED) { Serial.println("[TCP] WiFi fail"); restoreEspNow(); return false; }

  WiFiClient client;
  if (!client.connect(ip.c_str(), XIAO_TCP_PORT)) { Serial.println("[TCP] connect fail"); restoreEspNow(); return false; }

  uint8_t hdr[4]={(uint8_t)(size>>24),(uint8_t)(size>>16),(uint8_t)(size>>8),(uint8_t)size};
  client.write(hdr,4);

  uint32_t sent=0;
  const size_t BUF=4096;

  if (g_queued_adf_path.length() > 0) {
    // ── Stream direct from SD card ────────────────────────────────────────────
    File sdFile = SD.open(g_queued_adf_path.c_str(), FILE_READ);
    if (!sdFile) {
      Serial.println("[TCP] SD open fail");
      client.stop(); restoreEspNow(); return false;
    }
    uint8_t* buf = (uint8_t*)malloc(BUF);
    if (!buf) { sdFile.close(); client.stop(); restoreEspNow(); return false; }
    while (sent < size && sdFile.available()) {
      size_t n = min((uint32_t)BUF, size-sent);
      int rd = sdFile.read(buf, n);
      if (rd <= 0) break;
      size_t w = client.write(buf, (size_t)rd);
      if (!w) { Serial.println("[TCP] write err"); break; }
      sent += (uint32_t)w;
    }
    // Zero-pad to declared size if file shorter
    if (sent < size) {
      memset(buf, 0, BUF);
      while (sent < size) {
        size_t n = min((uint32_t)BUF, size-sent);
        client.write(buf, n); sent += (uint32_t)n;
      }
    }
    free(buf);
    sdFile.close();
  } else {
    // ── Fallback: send from g_disk buffer (won't hold full ADF on CYD) ───────
    uint8_t* src = g_disk + ESPNOW_DATA_LBA*ESPNOW_SECTOR_SIZE;
    while (sent < size) {
      size_t n = min((uint32_t)BUF, size-sent);
      size_t w = client.write(src+sent, n);
      if (!w) { Serial.println("[TCP] write err"); break; }
      sent += (uint32_t)w;
    }
  }

  client.clear();  // flush deprecated in core 3.x
  Serial.printf("[TCP] sent %lu\n",(unsigned long)sent);
  t0=millis();
  while (!client.available()&&millis()-t0<10000) delay(10);
  bool ok=false;
  if (client.available()) { uint8_t r=client.read(); ok=(r==0x01); Serial.printf("[TCP] resp 0x%02X\n",r); }
  client.stop();
  restoreEspNow();
  if (ok) g_espnow_xiao_done=true; else g_espnow_xiao_error=true;
  return ok;
}

void espnowSendEject() {
  static const uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
  const uint8_t* dst = g_espnow_paired ? _xiao_mac : bcast;
  PktSimple pkt={}; pkt.type=PKT_DISK_EJECT;
  esp_now_send(dst, (uint8_t*)&pkt, sizeof(pkt));
}
