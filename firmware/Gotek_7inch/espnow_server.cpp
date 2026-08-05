// espnow_server.cpp — Waveshare side
// ESP-NOW: pairing and control signals only
// WiFi TCP: disk data transfer (reliable, fast)

#include "espnow_server.h"
#include <Arduino.h>
#include "ESP32_NOW.h"
#include "WiFi.h"
#include <esp_mac.h>
#include <SD_MMC.h>
#include <WiFiClient.h>

#define ESPNOW_CHANNEL 6

// ---------- State ----------
volatile bool g_espnow_paired                = false;
volatile bool g_espnow_xiao_ready            = false;
volatile bool g_espnow_xiao_done             = false;
volatile bool g_espnow_xiao_error            = false;
volatile bool g_espnow_link_just_established = false;
volatile uint32_t g_espnow_xiao_last_seen    = 0;

bool espnowXiaoOnline() {
  if (!g_espnow_paired) return false;
  if (g_espnow_xiao_last_seen == 0) return false;
  return (millis() - g_espnow_xiao_last_seen) < 30000; // 30s timeout
}

static uint8_t _xiao_mac[6] = {0};
static String  _xiao_ip     = "";

// ---------- Packet structs ----------
#pragma pack(push,1)
struct PktHello  { uint8_t type; uint8_t mac[6]; char ip[16]; uint8_t pad[227]; };
struct PktSimple { uint8_t type; uint8_t pad[249]; };
struct PktEject  { uint8_t type; uint8_t pad[249]; };
#pragma pack(pop)

// ---------- Forward declare ----------
static void handleIncoming(const uint8_t* data, int len);

// ---------- Peer class ----------
class GotekPeer : public ESP_NOW_Peer {
public:
  GotekPeer(const uint8_t* mac, uint8_t ch, wifi_interface_t iface, const uint8_t* lmk)
    : ESP_NOW_Peer(mac, ch, iface, lmk) {}
  ~GotekPeer() { remove(); }
  bool add_peer() { return add(); }
  bool send_pkt(const uint8_t* d, size_t l) { return send(d, l); }
  void onReceive(const uint8_t* d, size_t l, bool b) override { handleIncoming(d, (int)l); }
  void onSent(bool) override {}
};

static GotekPeer* _bcastPeer = nullptr;
static GotekPeer* _xiaoPeer  = nullptr;

// ---------- Incoming handler ----------
static void handleIncoming(const uint8_t* data, int len) {
  if (len < 1) return;
  uint8_t type = data[0];

  if (type == PKT_PAIR_REPLY) {
    const PktHello* p = (const PktHello*)data;
    memcpy(_xiao_mac, p->mac, 6);
    _xiao_ip = String(p->ip);
    g_espnow_paired = true;
    g_espnow_link_just_established = true;
    g_espnow_xiao_last_seen = millis();

    // Register XIAO as direct peer
    if (_xiaoPeer) { delete _xiaoPeer; _xiaoPeer = nullptr; }
    _xiaoPeer = new GotekPeer(_xiao_mac, ESPNOW_CHANNEL, WIFI_IF_STA, nullptr);
    if (!_xiaoPeer->add_peer()) { delete _xiaoPeer; _xiaoPeer = nullptr; }

    // Save to CONFIG.TXT
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             _xiao_mac[0],_xiao_mac[1],_xiao_mac[2],
             _xiao_mac[3],_xiao_mac[4],_xiao_mac[5]);
    String lines = ""; bool written = false;
    File fr = SD_MMC.open("/CONFIG.TXT", FILE_READ);
    if (fr) {
      while (fr.available()) {
        String line = fr.readStringUntil('\n'); line.trim();
        if (line.startsWith("XIAO_MAC=")) { lines += "XIAO_MAC=" + String(macStr) + "\n"; written = true; }
        else if (line.startsWith("XIAO_IP=")) { lines += "XIAO_IP=" + _xiao_ip + "\n"; }
        else { lines += line + "\n"; }
      }
      fr.close();
    }
    if (!written) lines += "XIAO_MAC=" + String(macStr) + "\n";
    File fw = SD_MMC.open("/CONFIG.TXT", FILE_WRITE);
    if (fw) { fw.print(lines); fw.close(); }
    Serial.printf("[NOW] Paired: MAC=%s IP=%s\n", macStr, _xiao_ip.c_str());
    return;
  }

  if (type == PKT_XIAO_READY) { g_espnow_xiao_last_seen = millis(); g_espnow_xiao_ready = true; return; }
  if (type == PKT_XIAO_DONE)  { g_espnow_xiao_last_seen = millis(); g_espnow_xiao_done = true; g_espnow_xiao_error = false; return; }
  if (type == PKT_XIAO_ERROR) { g_espnow_xiao_last_seen = millis(); g_espnow_xiao_error = true; g_espnow_xiao_done = false; return; }
}

static void onNewPeer(const esp_now_recv_info_t* info, const uint8_t* data, int len, void* arg) {
  handleIncoming(data, len);
}

// ---------- Load saved config ----------
static void loadConfig() {
  File f = SD_MMC.open("/CONFIG.TXT", FILE_READ);
  if (!f) return;
  while (f.available()) {
    String line = f.readStringUntil('\n'); line.trim();
    if (line.startsWith("#")) continue;
    if (line.startsWith("XIAO_MAC=")) {
      String mac = line.substring(9);
      sscanf(mac.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
             &_xiao_mac[0],&_xiao_mac[1],&_xiao_mac[2],
             &_xiao_mac[3],&_xiao_mac[4],&_xiao_mac[5]);
      bool isZero = true;
      for (int i=0;i<6;i++) if(_xiao_mac[i]) { isZero=false; break; }
      if (!isZero) g_espnow_paired = true;
    }
    if (line.startsWith("XIAO_IP=")) _xiao_ip = line.substring(8);
  }
  f.close();
  if (g_espnow_paired) Serial.printf("[NOW] Loaded config: IP=%s\n", _xiao_ip.c_str());
}

// ---------- API ----------
void espnowBegin() {
  // Use WIFI_AP_STA — AP mode needed for Waveshare to connect to XIAO's AP later
  WiFi.mode(WIFI_AP_STA);
  WiFi.setChannel(ESPNOW_CHANNEL);
  while (!WiFi.STA.started()) delay(100);

  if (!ESP_NOW.begin()) { Serial.println("[NOW] begin FAILED"); return; }
  Serial.println("[NOW] begin OK");

  ESP_NOW.onNewPeer(onNewPeer, nullptr);

  _bcastPeer = new GotekPeer(ESP_NOW.BROADCAST_ADDR, ESPNOW_CHANNEL, WIFI_IF_STA, nullptr);
  if (!_bcastPeer->add_peer()) { delete _bcastPeer; _bcastPeer = nullptr; }

  loadConfig();

  if (g_espnow_paired) {
    _xiaoPeer = new GotekPeer(_xiao_mac, ESPNOW_CHANNEL, WIFI_IF_STA, nullptr);
    if (!_xiaoPeer->add_peer()) { delete _xiaoPeer; _xiaoPeer = nullptr; }
    Serial.println("[NOW] Restored XIAO peer");
  }
}

void espnowBroadcastHello() {
  if (!_bcastPeer) return;
  PktHello pkt = {};
  pkt.type = PKT_PAIR_HELLO;
  WiFi.macAddress(pkt.mac);
  _bcastPeer->send_pkt((uint8_t*)&pkt, sizeof(pkt));
}

bool   espnowIsPaired()       { return g_espnow_paired; }
String espnowGetSSIDLabel()   { return "WiFi+NOW"; }
String espnowGetXiaoMac() {
  char buf[18];
  snprintf(buf,sizeof(buf),"%02X:%02X:%02X:%02X:%02X:%02X",
           _xiao_mac[0],_xiao_mac[1],_xiao_mac[2],
           _xiao_mac[3],_xiao_mac[4],_xiao_mac[5]);
  return String(buf);
}

bool espnowSendNotify(const String& name, const String& mode, uint32_t size) {
  // For WiFi TCP approach, notify isn't needed — we just connect and send
  // But we signal XIAO to be ready
  g_espnow_xiao_ready = false;
  g_espnow_xiao_done  = false;
  g_espnow_xiao_error = false;
  return true; // proceed straight to sendDisk
}

bool espnowSendDisk(uint32_t size) {
  String ip = (_xiao_ip.length() > 0) ? _xiao_ip : String(XIAO_AP_IP);
  Serial.printf("[TCP] Connecting to XIAO at %s:%d\n", ip.c_str(), XIAO_TCP_PORT);

  // Stop ESP-NOW before switching WiFi mode
  ESP_NOW.end();
  delay(100);

  // Switch to pure STA mode to connect to XIAO's AP
  WiFi.mode(WIFI_STA);
  delay(100);
  WiFi.begin(XIAO_AP_SSID, XIAO_AP_PASS);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis()-t0 < 15000) {
    delay(200);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[TCP] WiFi connect failed — restarting ESP-NOW");
    WiFi.disconnect();
    delay(200);
    WiFi.mode(WIFI_AP_STA);
    WiFi.setChannel(ESPNOW_CHANNEL);
    while (!WiFi.STA.started()) delay(100);
    ESP_NOW.begin();
    ESP_NOW.onNewPeer(onNewPeer, nullptr);
    if (_bcastPeer) { delete _bcastPeer; _bcastPeer = nullptr; }
    _bcastPeer = new GotekPeer(ESP_NOW.BROADCAST_ADDR, ESPNOW_CHANNEL, WIFI_IF_STA, nullptr);
    if (!_bcastPeer->add_peer()) { delete _bcastPeer; _bcastPeer = nullptr; }
    if (g_espnow_paired) {
      if (_xiaoPeer) { delete _xiaoPeer; _xiaoPeer = nullptr; }
      _xiaoPeer = new GotekPeer(_xiao_mac, ESPNOW_CHANNEL, WIFI_IF_STA, nullptr);
      if (!_xiaoPeer->add_peer()) { delete _xiaoPeer; _xiaoPeer = nullptr; }
    }
    return false;
  }
  Serial.printf("[TCP] WiFi connected. IP: %s\n", WiFi.localIP().toString().c_str());

  // Connect TCP
  WiFiClient client;
  if (!client.connect(ip.c_str(), XIAO_TCP_PORT)) {
    Serial.println("[TCP] TCP connect failed");
    WiFi.disconnect();
    delay(100);
    WiFi.mode(WIFI_AP_STA);
    WiFi.setChannel(ESPNOW_CHANNEL);
    return false;
  }
  Serial.printf("[TCP] Connected to XIAO TCP server\n");

  // Protocol: send 4-byte size (big endian), then raw data
  uint8_t* src = g_disk + ESPNOW_DATA_LBA * ESPNOW_SECTOR_SIZE;

  // Send size header
  uint8_t hdr[4] = {
    (uint8_t)(size >> 24), (uint8_t)(size >> 16),
    (uint8_t)(size >> 8),  (uint8_t)(size)
  };
  client.write(hdr, 4);

  // Send data in 4KB chunks
  uint32_t sent = 0;
  const size_t BUF = 4096;
  while (sent < size) {
    size_t n = min((uint32_t)BUF, size - sent);
    size_t written = client.write(src + sent, n);
    if (written == 0) { Serial.println("[TCP] Write error"); break; }
    sent += written;
  }
  client.clear();
  Serial.printf("[TCP] Sent %lu bytes\n", (unsigned long)sent);

  // Wait for XIAO to confirm (single byte: 0x01=OK, 0x00=ERROR)
  t0 = millis();
  while (!client.available() && millis()-t0 < 10000) delay(10);
  bool ok = false;
  if (client.available()) {
    uint8_t resp = client.read();
    ok = (resp == 0x01);
    Serial.printf("[TCP] XIAO response: 0x%02X (%s)\n", resp, ok?"OK":"ERROR");
  } else {
    Serial.println("[TCP] No response from XIAO");
  }
  client.stop();

  // Restore ESP-NOW
  WiFi.disconnect();
  delay(200);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setChannel(ESPNOW_CHANNEL);
  while (!WiFi.STA.started()) delay(100);
  ESP_NOW.begin();
  ESP_NOW.onNewPeer(onNewPeer, nullptr);
  if (_bcastPeer) { delete _bcastPeer; _bcastPeer = nullptr; }
  _bcastPeer = new GotekPeer(ESP_NOW.BROADCAST_ADDR, ESPNOW_CHANNEL, WIFI_IF_STA, nullptr);
  if (!_bcastPeer->add_peer()) { delete _bcastPeer; _bcastPeer = nullptr; }
  if (g_espnow_paired) {
    if (_xiaoPeer) { delete _xiaoPeer; _xiaoPeer = nullptr; }
    _xiaoPeer = new GotekPeer(_xiao_mac, ESPNOW_CHANNEL, WIFI_IF_STA, nullptr);
    if (!_xiaoPeer->add_peer()) { delete _xiaoPeer; _xiaoPeer = nullptr; }
  }
  Serial.println("[NOW] ESP-NOW restored after TCP transfer");

  if (ok) g_espnow_xiao_done = true;
  else    g_espnow_xiao_error = true;
  return ok;
}

void espnowSendEject() {
  GotekPeer* dst = _xiaoPeer ? _xiaoPeer : _bcastPeer;
  if (!dst) return;
  PktEject pkt = {}; pkt.type = PKT_DISK_EJECT;
  dst->send_pkt((uint8_t*)&pkt, sizeof(pkt));
}
