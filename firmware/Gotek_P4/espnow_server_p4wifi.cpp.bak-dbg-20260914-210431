// espnow_server_p4wifi.cpp — ESP32-P4 WiFi-direct transport (replaces the p4stub).
// ---------------------------------------------------------------------------------
// The P4 has no radio of its own; WiFi runs on the companion ESP32-C6 over SDIO
// (esp-hosted). Once the C6 is on 2.12.13 (self-updated from the SD card), all of
// standard Arduino WiFi.h works: scan, STA join, WiFiClient TCP, ESPmDNS.
//
// ESP-NOW does NOT link on Arduino-P4 (nm confirms esp_now_* is absent from
// libespressif__esp_wifi_remote.a). But ESP-NOW was only ever the *control* plane
// (pairing + eject + lock). The disk DATA has always gone over WiFi TCP-3333. So
// this file keeps the JC's proven WiFi TCP push VERBATIM and swaps ESP-NOW discovery
// for a WiFi AP scan: every dongle's SoftAP is SSID "GotekOMEGA" with a unique BSSID,
// which maps 1:1 onto the existing scan/select UI (BSSID == the dongle's identity).
//
// What works here: SCAN dongles, single-dongle load (AP-direct 192.168.4.1),
//   home-WiFi load (STA + mDNS gotekomega.local), hivemind fan-out (sequential
//   AP-hop), and save-writeback fetch (escape 0x01 GET_SAVE).
// What is stubbed (needs ESP-NOW, or a dongle HTTP call — flagged v2):
//   eject-to-dongle, lock/unlock/unpair, live "online" heartbeat, board-caps (HD)
//   detection. Loading is the critical path and is fully functional.
//
// Build: use EITHER this file OR espnow_server_p4stub.cpp, never both (duplicate
// symbols). Rename the stub to .bak and drop this in.

#include "espnow_server.h"
#include <Arduino.h>
#include "WiFi.h"
#include <WiFiClient.h>
#include <ESPmDNS.h>
#include <SD_MMC.h>

#define DONGLE_AP_CHANNEL 6   // dongles' SoftAP sits on ch6 (legacy ESPNOW_CHANNEL)

// ---------- State (same globals the UI reads) ----------
volatile bool g_espnow_paired                = false;
volatile bool g_espnow_xiao_ready            = false;
volatile bool g_espnow_xiao_done             = false;
volatile bool g_espnow_xiao_error            = false;
volatile bool g_espnow_link_just_established = false;
volatile uint32_t g_espnow_xiao_last_seen    = 0;
volatile bool     g_dongle_loaded            = false;
volatile uint32_t g_dongle_load_id           = 0;
volatile uint32_t g_dongle_img_size          = 0;
volatile uint8_t  g_espnow_dongle_caps  = 0;
volatile uint8_t  g_espnow_dongle_board = 0;   // stays 0 on P4 (no PAIR_REPLY) = DD-safe default
volatile uint32_t g_espnow_load_id      = 0;
volatile bool     g_espnow_dirty        = false;
volatile uint32_t g_espnow_dirty_loadid = 0;
volatile uint16_t g_espnow_dirty_count  = 0;
volatile uint32_t g_espnow_dirty_size   = 0;

// ---------- Identity of the selected dongle ----------
static uint8_t _dongle_mac[6] = {0};   // the dongle's AP BSSID (WiFi identity on P4)
static String  _dongle_ip     = "";    // home-mode LAN IP; AP-mode is always 192.168.4.1

// CRC32 (IEEE, bitwise — identical to the dongle side) — used by save fetch.
static uint32_t crc32sw(uint32_t crc, const uint8_t* p, size_t n) {
  crc = ~crc;
  while (n--) { crc ^= *p++; for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320UL & (uint32_t)(-(int32_t)(crc & 1))); }
  return ~crc;
}

// ---------- Multi-dongle scan (WiFi AP scan for SSID "GotekOMEGA") ----------
#define MAX_SCANNED 64
struct ScannedDongle { uint8_t mac[6]; char ip[16]; };
static ScannedDongle _scanned[MAX_SCANNED];
static int  _scanned_count = 0;
static int  _scanCap = 32;
void espnowSetScanCap(int n){ if(n<1)n=1; if(n>MAX_SCANNED)n=MAX_SCANNED; _scanCap=n; }

// ---------- CONFIG.TXT persistence (same XIAO_MAC / XIAO_IP keys as the JC) ----------
static void loadConfig() {
  File f = SD_MMC.open("/CONFIG.TXT", FILE_READ);
  if (!f) return;
  while (f.available()) {
    String line = f.readStringUntil('\n'); line.trim();
    if (line.startsWith("#")) continue;
    if (line.startsWith("XIAO_MAC=")) {
      String mac = line.substring(9);
      sscanf(mac.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
             &_dongle_mac[0],&_dongle_mac[1],&_dongle_mac[2],
             &_dongle_mac[3],&_dongle_mac[4],&_dongle_mac[5]);
      bool isZero = true; for (int i=0;i<6;i++) if(_dongle_mac[i]){ isZero=false; break; }
      if (!isZero) g_espnow_paired = true;
    }
    if (line.startsWith("XIAO_IP=")) _dongle_ip = line.substring(8);
  }
  f.close();
}
static void saveConfig() {
  char macStr[18];
  snprintf(macStr,sizeof(macStr),"%02X:%02X:%02X:%02X:%02X:%02X",
           _dongle_mac[0],_dongle_mac[1],_dongle_mac[2],_dongle_mac[3],_dongle_mac[4],_dongle_mac[5]);
  String lines=""; bool w1=false,w2=false;
  File fr=SD_MMC.open("/CONFIG.TXT",FILE_READ);
  if(fr){ while(fr.available()){ String line=fr.readStringUntil('\n'); line.trim();
    if(line.startsWith("XIAO_MAC=")){ lines+="XIAO_MAC="+String(macStr)+"\n"; w1=true; }
    else if(line.startsWith("XIAO_IP=")){ lines+="XIAO_IP="+_dongle_ip+"\n"; w2=true; }
    else lines+=line+"\n"; } fr.close(); }
  if(!w1) lines+="XIAO_MAC="+String(macStr)+"\n";
  if(!w2 && _dongle_ip.length()) lines+="XIAO_IP="+_dongle_ip+"\n";
  File fw=SD_MMC.open("/CONFIG.TXT",FILE_WRITE);
  if(fw){ fw.print(lines); fw.close(); }
}

// ---------- API ----------
void espnowBegin() {
  // C6 already alive (self-updated). Just bring WiFi up in STA and load the saved dongle.
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  uint32_t t0=millis(); while(!WiFi.STA.started() && millis()-t0<3000) delay(50);
  loadConfig();
  Serial.printf("[P4WIFI] begin; paired=%d ip=%s\n", g_espnow_paired, _dongle_ip.c_str());
}

// Blocking WiFi scan for the dongle's GotekOMEGA AP on ch6. BLOCKING, not async: on the P4
// the radio is remote (C6 via esp-hosted) and the async scan-complete event does NOT propagate
// reliably — scanComplete() never reports done, so an async scan harvests nothing. The P4
// radio-check proved a *blocking* scanNetworks() works here (it found many APs). Each call
// blocks ~0.3-0.5s (single channel); the UI's ~4s scan loop calls this several times,
// accumulating unique dongle BSSIDs.
static void blockingScanHarvest() {
  int n = WiFi.scanNetworks(false /*blocking*/, false /*show_hidden*/, false /*passive*/, 300, DONGLE_AP_CHANNEL);
  if (n <= 0) { WiFi.scanDelete(); return; }
  for (int i = 0; i < n && _scanned_count < _scanCap; i++) {
    if (WiFi.SSID(i) != String(DONGLE_AP_SSID)) continue;
    uint8_t* b = WiFi.BSSID(i);
    if (!b) continue;
    bool dup=false;
    for (int j=0;j<_scanned_count;j++) if(memcmp(_scanned[j].mac,b,6)==0){ dup=true; break; }
    if (dup) continue;
    memcpy(_scanned[_scanned_count].mac, b, 6);
    strncpy(_scanned[_scanned_count].ip, DONGLE_AP_IP, 15);   // AP-mode dongles all serve .4.1
    _scanned[_scanned_count].ip[15]=0;
    _scanned_count++;
    g_espnow_xiao_last_seen = millis();
  }
  WiFi.scanDelete();
}

// ---------- Multi-dongle scan API ----------
// UI: espnowScanBegin(), then loop { espnowBroadcastHello(); espnowScanCount(); } ~4s, then End().
void espnowScanBegin()      { _scanned_count = 0; blockingScanHarvest(); }  // first pass immediately
void espnowBroadcastHello() { blockingScanHarvest(); }                      // each poll = one rescan
void espnowScanEnd()        { WiFi.scanDelete(); }
int  espnowScanCount()      { return _scanned_count; }

String espnowScanGetMac(int i) {
  if (i<0 || i>=_scanned_count) return "";
  char buf[18];
  snprintf(buf,sizeof(buf),"%02X:%02X:%02X:%02X:%02X:%02X",
           _scanned[i].mac[0],_scanned[i].mac[1],_scanned[i].mac[2],
           _scanned[i].mac[3],_scanned[i].mac[4],_scanned[i].mac[5]);
  return String(buf);
}
void espnowScanMacBytes(int i, uint8_t* out){
  if(out && i>=0 && i<_scanned_count) memcpy(out, _scanned[i].mac, 6);
  else if(out) for(int k=0;k<6;k++) out[k]=0;
}

bool espnowScanSelect(int i) {
  if (i<0 || i>=_scanned_count) return false;
  memcpy(_dongle_mac, _scanned[i].mac, 6);
  _dongle_ip = String(_scanned[i].ip);
  g_espnow_paired = true;
  g_espnow_link_just_established = true;
  g_espnow_xiao_last_seen = millis();
  saveConfig();
  return true;
}

bool   espnowIsPaired()       { return g_espnow_paired; }
String espnowGetSSIDLabel()   { return "WiFi-direct"; }
String espnowGetXiaoMac() {
  char buf[18];
  snprintf(buf,sizeof(buf),"%02X:%02X:%02X:%02X:%02X:%02X",
           _dongle_mac[0],_dongle_mac[1],_dongle_mac[2],
           _dongle_mac[3],_dongle_mac[4],_dongle_mac[5]);
  return String(buf);
}
// No ESP-NOW heartbeat on P4; "online" == we have a selected dongle. (v2: quick TCP /status probe.)
bool espnowXiaoOnline() { return g_espnow_paired; }

bool espnowSendNotify(const String&, const String&, uint32_t) {
  g_espnow_xiao_ready = false; g_espnow_xiao_done = false; g_espnow_xiao_error = false;
  return true;
}

// ---------- Core AP-direct push: join a dongle's SoftAP by BSSID, stream over TCP-3333 ----------
static bool sendDiskCore(const uint8_t* mac, const char* ipc, uint32_t size, uint32_t connectTimeoutMs) {
  String ip = String(ipc && ipc[0] ? ipc : DONGLE_AP_IP);

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, true);     // drop any sticky assoc so we target THIS BSSID
  delay(200);

  bool haveMac = false; for (int i=0;i<6;i++) if(mac[i]){ haveMac=true; break; }
  if (haveMac) WiFi.begin(DONGLE_AP_SSID, DONGLE_AP_PASS, DONGLE_AP_CHANNEL, (uint8_t*)mac);
  else         WiFi.begin(DONGLE_AP_SSID, DONGLE_AP_PASS);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis()-t0 < connectTimeoutMs) { delay(200); Serial.print("."); }
  Serial.println();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[P4WIFI] AP join failed");
    WiFi.disconnect(); g_espnow_xiao_error = true; return false;
  }
  Serial.printf("[P4WIFI] joined, IP %s -> dongle %s:%d\n", WiFi.localIP().toString().c_str(), ip.c_str(), DONGLE_TCP_PORT);

  WiFiClient client;
  if (!client.connect(ip.c_str(), DONGLE_TCP_PORT)) {
    Serial.println("[P4WIFI] TCP connect failed");
    WiFi.disconnect(); g_espnow_xiao_error = true; return false;
  }

  uint8_t* src = g_disk + ESPNOW_DATA_LBA * ESPNOW_SECTOR_SIZE;
  uint8_t hdr[4] = { (uint8_t)(size>>24),(uint8_t)(size>>16),(uint8_t)(size>>8),(uint8_t)size };
  client.write(hdr, 4);
  uint32_t sent = 0; const size_t BUF = 4096;
  while (sent < size) { size_t n = min((uint32_t)BUF, size-sent); size_t w = client.write(src+sent, n); if(!w){ Serial.println("[P4WIFI] write err"); break; } sent += w; }
  client.clear();
  Serial.printf("[P4WIFI] sent %lu bytes\n",(unsigned long)sent);

  bool ok = false;
  t0 = millis(); while (!client.available() && millis()-t0 < 10000) delay(10);
  if (client.available()) {
    ok = (client.read() == 0x01);
    if (ok) {
      uint32_t tid=millis(); uint8_t lid[4]; int got=0;
      while (got<4 && millis()-tid<300) { if(client.available()) lid[got++]=client.read(); else delay(5); }
      g_espnow_load_id = (got==4) ? ((uint32_t)lid[0]|((uint32_t)lid[1]<<8)|((uint32_t)lid[2]<<16)|((uint32_t)lid[3]<<24)) : 0;
    }
  }
  client.stop();
  WiFi.disconnect(); delay(50);

  if (ok) g_espnow_xiao_done = true; else g_espnow_xiao_error = true;
  return ok;
}

// Single paired dongle — AP-direct, 15 s window, learned BSSID.
bool espnowSendDisk(uint32_t size) {
  return sendDiskCore(_dongle_mac, _dongle_ip.length()?_dongle_ip.c_str():DONGLE_AP_IP, size, 15000);
}
// Hivemind fan-out — one dongle by BSSID; shorter window so a powered-off member doesn't stall.
bool espnowSendDiskTo(const uint8_t* mac, uint32_t size) {
  return sendDiskCore(mac, DONGLE_AP_IP, size, 6000);
}

// Home-WiFi transport: join the router (STA/DHCP), resolve the dongle via mDNS
// gotekomega.local (or cached ioIp), push over TCP-3333. ioIp receives the resolved IP.
bool espnowSendDiskHome(const String& ssid, const String& pass, String& ioIp, uint32_t size) {
  if (ssid.length() == 0) { g_espnow_xiao_error = true; return false; }
  Serial.printf("[P4WIFI/HOME] joining '%s'\n", ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false); WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, true); delay(200);
  WiFi.begin(ssid.c_str(), pass.c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis()-t0 < 15000) { delay(200); Serial.print("."); }
  Serial.println();

  bool ok = false;
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[P4WIFI/HOME] joined, IP %s\n", WiFi.localIP().toString().c_str());
    String ip = ioIp;
    if (MDNS.begin("gti-remote")) {
      IPAddress r = MDNS.queryHost("gotekomega", 2500);
      if ((uint32_t)r != 0) { ip = r.toString(); ioIp = ip; Serial.printf("[P4WIFI/HOME] gotekomega.local -> %s\n", ip.c_str()); }
      MDNS.end();
    }
    if (ip.length() > 0) {
      WiFiClient client;
      if (client.connect(ip.c_str(), DONGLE_TCP_PORT)) {
        uint8_t* src = g_disk + ESPNOW_DATA_LBA * ESPNOW_SECTOR_SIZE;
        uint8_t hdr[4] = { (uint8_t)(size>>24),(uint8_t)(size>>16),(uint8_t)(size>>8),(uint8_t)size };
        client.write(hdr, 4);
        uint32_t sent = 0; const size_t BUF = 4096;
        while (sent < size) { size_t n = min((uint32_t)BUF, size-sent); size_t w = client.write(src+sent, n); if(!w) break; sent += w; }
        client.clear();
        t0 = millis(); while (!client.available() && millis()-t0 < 10000) delay(10);
        if (client.available()) {
          ok = (client.read() == 0x01);
          if (ok) { uint32_t tid=millis(); uint8_t lid[4]; int got=0;
            while (got<4 && millis()-tid<300){ if(client.available()) lid[got++]=client.read(); else delay(5); }
            g_espnow_load_id = (got==4) ? ((uint32_t)lid[0]|((uint32_t)lid[1]<<8)|((uint32_t)lid[2]<<16)|((uint32_t)lid[3]<<24)) : 0;
          }
        }
        client.stop();
        Serial.printf("[P4WIFI/HOME] sent %lu bytes ack=%s\n",(unsigned long)sent, ok?"OK":"ERR");
      } else Serial.println("[P4WIFI/HOME] TCP connect failed");
    } else Serial.println("[P4WIFI/HOME] dongle not found (mDNS + no cached IP)");
  } else Serial.println("[P4WIFI/HOME] home join failed");

  WiFi.disconnect(); delay(50);
  if (ok) g_espnow_xiao_done = true; else g_espnow_xiao_error = true;
  return ok;
}

void espnowSendEject() {
  // v2: eject-to-dongle needs the TCP escape (FF FF FF FF 03) which means an AP join —
  // too heavy to fire on UI navigation. Loading auto-replaces the disk, so eject is a
  // convenience only; leave it a no-op on P4 for now (eject also available on the
  // dongle's own web page).
}

// ── Lock/unlock/unpair are ESP-NOW control — unavailable on P4 WiFi-only (v2: dongle HTTP). ──
void espnowSendUnpair(const uint8_t*){}
void espnowSendLock(const uint8_t*){}
void espnowSendUnlock(const uint8_t*){}
void espnowForgetActive(const uint8_t* mac){
  if (memcmp(_dongle_mac, mac, 6)!=0) return;
  g_espnow_paired=false; memset(_dongle_mac,0,6); _dongle_ip="";
  String lines=""; File fr=SD_MMC.open("/CONFIG.TXT",FILE_READ);
  if(fr){ while(fr.available()){ String line=fr.readStringUntil('\n'); line.trim();
    if(line.startsWith("XIAO_MAC=")||line.startsWith("XIAO_IP=")) continue; lines+=line+"\n"; } fr.close(); }
  File fw=SD_MMC.open("/CONFIG.TXT",FILE_WRITE); if(fw){ fw.print(lines); fw.close(); }
}

// ── Save writeback fetch — AP-direct join + escape 0x01 GET_SAVE (WiFi part of the JC path) ──
static bool readFull(WiFiClient& c, uint8_t* buf, uint32_t len, uint32_t timeoutMs) {
  uint32_t got=0, t0=millis();
  while (got<len && millis()-t0<timeoutMs) {
    if (!c.connected() && !c.available()) return false;
    int avail=c.available(); if(avail<=0){ delay(1); continue; }
    int rd=c.read(buf+got, min((uint32_t)avail, len-got));
    if(rd>0){ got+=rd; t0=millis(); }
  }
  return got==len;
}
bool espnowFetchSave(SavePersistCb persist) {
  if (!g_espnow_paired || !persist) return false;
  String ip = _dongle_ip.length() ? _dongle_ip : String(DONGLE_AP_IP);

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false); WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, true); delay(200);
  bool haveMac=false; for(int i=0;i<6;i++) if(_dongle_mac[i]){ haveMac=true; break; }
  if (haveMac) WiFi.begin(DONGLE_AP_SSID, DONGLE_AP_PASS, DONGLE_AP_CHANNEL, (uint8_t*)_dongle_mac);
  else         WiFi.begin(DONGLE_AP_SSID, DONGLE_AP_PASS);
  uint32_t t0=millis(); while(WiFi.status()!=WL_CONNECTED && millis()-t0<15000) delay(200);
  if (WiFi.status()!=WL_CONNECTED) { Serial.println("[P4WIFI/SAVE] join failed"); WiFi.disconnect(); return false; }

  WiFiClient client;
  bool okAll=false; uint8_t* mapBuf=nullptr; uint8_t* packed=nullptr;
  if (client.connect(ip.c_str(), DONGLE_TCP_PORT)) {
    uint8_t esc[5]={0xFF,0xFF,0xFF,0xFF,0x01};    // escape + GET_SAVE
    client.write(esc,5);
    uint8_t hdr[14];
    if (readFull(client,hdr,14,5000) && hdr[0]=='S' && hdr[1]=='V' && hdr[2]=='1') {
      uint32_t loadId =(uint32_t)hdr[4]|((uint32_t)hdr[5]<<8)|((uint32_t)hdr[6]<<16)|((uint32_t)hdr[7]<<24);
      uint32_t imgSz  =(uint32_t)hdr[8]|((uint32_t)hdr[9]<<8)|((uint32_t)hdr[10]<<16)|((uint32_t)hdr[11]<<24);
      uint16_t mapLen =(uint16_t)hdr[12]|((uint16_t)hdr[13]<<8);
      if (mapLen>0 && mapLen<=256) {
        mapBuf=(uint8_t*)malloc(mapLen);
        if (mapBuf && readFull(client,mapBuf,mapLen,5000)) {
          uint32_t nSec=0; for(uint32_t i=0;i<(uint32_t)mapLen*8;i++) if((mapBuf[i>>3]>>(i&7))&1) nSec++;
          bool dataOk=true;
          if (nSec>0) { packed=(uint8_t*)ps_malloc(nSec*512); if(!packed) packed=(uint8_t*)malloc(nSec*512);
            dataOk = packed && readFull(client,packed,nSec*512,30000); }
          uint8_t crcb[4];
          if (dataOk && readFull(client,crcb,4,5000)) {
            uint32_t crcRx=(uint32_t)crcb[0]|((uint32_t)crcb[1]<<8)|((uint32_t)crcb[2]<<16)|((uint32_t)crcb[3]<<24);
            uint32_t crc=crc32sw(0,mapBuf,mapLen); if(nSec>0) crc=crc32sw(crc,packed,nSec*512);
            if (crc==crcRx) {
              bool saved=persist(loadId,imgSz,mapBuf,mapLen,packed,nSec);
              uint8_t ack=saved?0x01:0x00; client.write(&ack,1); client.flush(); delay(100);
              okAll=saved;
            } else { uint8_t ack=0x00; client.write(&ack,1); client.flush(); }
          }
        }
      }
    }
    client.stop();
  }
  if (mapBuf) free(mapBuf);
  if (packed) free(packed);
  WiFi.disconnect(); delay(50);
  if (okAll) g_espnow_dirty=false;
  return okAll;
}
