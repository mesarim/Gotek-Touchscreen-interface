// espnow_server_p4wifi.cpp — ESP32-P4 WiFi-direct transport (replaces the p4stub).
// lab15i-P4: brought to the 15i radio API - CMD_CLAIM 0x0A take-over check over TCP, espnowStop (no radio off),
// SHARE / 'in use by' reported as unavailable (ESP-NOW only), ESPNOW_DATA_LBA 13 via the shared espnow_server.h.
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
#include <stdarg.h>
// lab2-P4G7: every [P4WIFI] line goes to the serial port AND to gti.log (p4wifiLog lives in the sketch, = gLog)
void p4wifiLog(const char* s);
static void P4LOG(const char* fmt, ...){ char b[200]; va_list ap; va_start(ap,fmt); vsnprintf(b,sizeof b,fmt,ap); va_end(ap); Serial.print(b); p4wifiLog(b); }

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
static char    _dongle_ssid[33] = "";  // lab3-P4G7: its real Wi-Fi name (GotekOMEGA-XXXX since Webby 1.6.8), kept in CONFIG.TXT XIAO_SSID=
static SavePersistCb _persistCb = nullptr;   // lab3-P4G7: the sketch's save writer
static bool    _diskOut = false;              // lab3-P4G7: this screen sent a disk to _dongle_mac and has not ejected it
volatile int   g_p4_eject_result = -1;
volatile int   g_p4_saved_secs   = 0;
void espnowSetSavePersist(SavePersistCb cb){ _persistCb = cb; }

// CRC32 (IEEE, bitwise — identical to the dongle side) — used by save fetch.
static uint32_t crc32sw(uint32_t crc, const uint8_t* p, size_t n) {
  crc = ~crc;
  while (n--) { crc ^= *p++; for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320UL & (uint32_t)(-(int32_t)(crc & 1))); }
  return ~crc;
}

// ---------- Multi-dongle scan (WiFi AP scan for SSID "GotekOMEGA") ----------
#define MAX_SCANNED 64
struct ScannedDongle { uint8_t mac[6]; char ip[16]; char ssid[33]; };
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
    if (line.startsWith("XIAO_SSID=")) { String v=line.substring(10); strncpy(_dongle_ssid, v.c_str(), 32); _dongle_ssid[32]=0; }   // lab3-P4G7
  }
  f.close();
}
static void saveConfig() {
  char macStr[18];
  snprintf(macStr,sizeof(macStr),"%02X:%02X:%02X:%02X:%02X:%02X",
           _dongle_mac[0],_dongle_mac[1],_dongle_mac[2],_dongle_mac[3],_dongle_mac[4],_dongle_mac[5]);
  String lines=""; bool w1=false,w2=false,w3=false;
  File fr=SD_MMC.open("/CONFIG.TXT",FILE_READ);
  if(fr){ while(fr.available()){ String line=fr.readStringUntil('\n'); line.trim();
    if(line.startsWith("XIAO_MAC=")){ lines+="XIAO_MAC="+String(macStr)+"\n"; w1=true; }
    else if(line.startsWith("XIAO_IP=")){ lines+="XIAO_IP="+_dongle_ip+"\n"; w2=true; }
    else if(line.startsWith("XIAO_SSID=")){ lines+="XIAO_SSID="+String(_dongle_ssid)+"\n"; w3=true; }   // lab3-P4G7
    else lines+=line+"\n"; } fr.close(); }
  if(!w1) lines+="XIAO_MAC="+String(macStr)+"\n";
  if(!w2 && _dongle_ip.length()) lines+="XIAO_IP="+_dongle_ip+"\n";
  if(!w3 && _dongle_ssid[0]) lines+="XIAO_SSID="+String(_dongle_ssid)+"\n";
  File fw=SD_MMC.open("/CONFIG.TXT",FILE_WRITE);
  if(fw){ fw.print(lines); fw.close(); }
}

// ---------- API ----------
void espnowBegin() {
  // C6 already alive (self-updated). Just bring WiFi up in STA and load the saved dongle.
  if(WiFi.getMode()!=WIFI_STA) WiFi.mode(WIFI_STA);   // 5.9.10: c6SelfUpdate leaves STA up in wireless mode; re-initing the hosted radio here crashed it
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  uint32_t t0=millis(); while(!WiFi.STA.started() && millis()-t0<3000) delay(50);
  loadConfig();
  P4LOG("[P4WIFI] begin; paired=%d ip=%s\n", g_espnow_paired, _dongle_ip.c_str());
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
    if (!WiFi.SSID(i).startsWith(DONGLE_AP_SSID)) continue;   // 5.9.11: match GotekOMEGA AND GotekOMEGA-<mac> (unique-SSID dongles)
    uint8_t* b = WiFi.BSSID(i);
    if (!b) continue;
    bool dup=false;
    for (int j=0;j<_scanned_count;j++) if(memcmp(_scanned[j].mac,b,6)==0){ dup=true; break; }
    if (dup) continue;
    memcpy(_scanned[_scanned_count].mac, b, 6);
    strncpy(_scanned[_scanned_count].ip, DONGLE_AP_IP, 15);   // AP-mode dongles all serve .4.1
    _scanned[_scanned_count].ip[15]=0;
    strncpy(_scanned[_scanned_count].ssid, WiFi.SSID(i).c_str(), 32); _scanned[_scanned_count].ssid[32]=0;   // 5.9.11: remember the dongle's actual (unique) SSID for the join
    P4LOG("[P4WIFI] scan: found %s (%02X:%02X:%02X:%02X:%02X:%02X) RSSI %d dBm\n", _scanned[_scanned_count].ssid, b[0],b[1],b[2],b[3],b[4],b[5], (int)WiFi.RSSI(i));   // lab2-P4G7
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
  strncpy(_dongle_ssid, _scanned[i].ssid, 32); _dongle_ssid[32]=0;   // lab3-P4G7: remembered (also in CONFIG.TXT)
  _diskOut = false;
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
// ── Wireless DSK fix (matches Webby 1.6.3) ──────────────────────────────────
// Tell the dongle the flung disk's real filename+extension via the CMD_SET_NAME
// escape just before the disk fling, so a CPC/Spectrum .dsk mounts as DISK.DSK
// (was hardcoded DISK.ADF on the dongle -> FlashFloppy Error 34). Best-effort;
// an older dongle NAKs the escape and the fling still lands as before.
#ifndef CMD_SET_NAME
#define CMD_SET_NAME 0x06
#endif
static String g_fling_name = "";
void espnowSetFlingName(const String& nameWithExt){
  g_fling_name = nameWithExt.length() ? nameWithExt : String("DISK.ADF");
}
static void tcpSendSetName(const char* ip){
  if (g_fling_name.length() == 0) return;
  WiFiClient c;
  if (!c.connect(ip, DONGLE_TCP_PORT)) return;
  uint8_t esc[5] = {0xFF,0xFF,0xFF,0xFF, CMD_SET_NAME};
  c.write(esc, 5);
  uint8_t L = (uint8_t)(g_fling_name.length() > 128 ? 128 : g_fling_name.length());
  c.write(&L, 1);
  c.write((const uint8_t*)g_fling_name.c_str(), L);
  uint32_t t0 = millis();
  while (!c.available() && millis()-t0 < 1000) delay(5);
  if (c.available()) c.read();
  c.stop();
  delay(20);
}

// ── lab15i-P4: take-over check (CMD_CLAIM 0x0A over TCP) - same wire format as the S3's espnow_server.cpp ──
// Escape CMD_CLAIM: my MAC[6], flags (bit0 = take over), name length, name. Webby 1.6.6+ answers 0x01 = go ahead;
// 0x03 = another screen has a disk in me; 0x02 = another screen's saves are not handed back yet (both followed by
// length + that screen's name). An older dongle answers 0x00 (unknown command) = go ahead (old behaviour).
#define CMD_CLAIM 0x0A
static ClaimAskCb _claimAsk = nullptr;
static String     _myName   = "";
static bool       _claimCancelled = false;
void espnowSetClaimAsk(ClaimAskCb cb){ _claimAsk = cb; }
void espnowSetScreenName(const String& n){ _myName = n; _myName.trim(); }
bool espnowClaimCancelled(){ return _claimCancelled; }
static String screenName(){
  if (_myName.length()) return _myName;
  uint8_t m[6]; WiFi.macAddress(m); char b[12]; snprintf(b, sizeof(b), "GTi-%02X%02X", m[4], m[5]); return String(b);
}
static bool tcpClaim(const char* ip){          // true = go ahead and send
  _claimCancelled = false;
  for (int pass = 0; pass < 2; pass++) {
    WiFiClient c;
    if (!c.connect(ip, DONGLE_TCP_PORT)) return true;          // can't ask: old behaviour
    uint8_t my[6]; WiFi.macAddress(my);
    String nm = screenName(); uint8_t L = (uint8_t)(nm.length() > 24 ? 24 : nm.length());
    uint8_t esc[5] = {0xFF,0xFF,0xFF,0xFF, CMD_CLAIM}; uint8_t fl = pass ? 1 : 0;
    c.write(esc, 5); c.write(my, 6); c.write(&fl, 1); c.write(&L, 1); c.write((const uint8_t*)nm.c_str(), L);
    uint32_t t0 = millis(); while (!c.available() && millis()-t0 < 1500) delay(5);
    int r = c.available() ? c.read() : -1;
    if (r != 0x02 && r != 0x03) { c.stop(); delay(20); return true; }
    char who[25] = {0}; t0 = millis(); while (!c.available() && millis()-t0 < 500) delay(2);
    int wl = c.available() ? c.read() : 0; if (wl > 24) wl = 24;
    int got = 0; t0 = millis();
    while (got < wl && millis()-t0 < 500) { int ch = c.read(); if (ch < 0) { delay(1); continue; } who[got++] = (char)ch; }
    c.stop(); delay(20);
    P4LOG("[P4WIFI] dongle %s '%s'\n", r == 0x02 ? "holds unsaved saves from" : "is in use by", who);
    if (pass == 1) return true;
    if (!_claimAsk || !_claimAsk(r == 0x02, who[0] ? who : "another screen")) { _claimCancelled = true; return false; }
  }
  return true;
}
// ESP-NOW-only features: not available over the P4's Wi-Fi-direct link (no ESP-NOW on Arduino-P4).
void   espnowSendShare(const uint8_t*){ P4LOG("[P4WIFI] SHARE needs ESP-NOW - not available on the P4\n"); }
String espnowScanInUseBy(int){ return ""; }        // "in use by" comes in the ESP-NOW pairing reply - unknown here
// MODE switching: the hosted radio must NOT be switched off and on again (5.9.12 crash) - forget the link only.
void   espnowStop(){ WiFi.disconnect(false, true); }

static bool readFullP(WiFiClient& c, uint8_t* buf, uint32_t len, uint32_t timeoutMs) {   // lab3-P4G7 (= readFull below)
  uint32_t got=0, t0=millis();
  while (got<len && millis()-t0<timeoutMs) {
    if (!c.connected() && !c.available()) return false;
    int avail=c.available(); if(avail<=0){ delay(1); continue; }
    int rd=c.read(buf+got, min((uint32_t)avail, len-got));
    if(rd>0){ got+=rd; t0=millis(); }
  }
  return got==len;
}
// ---------- lab3-P4G7: join a dongle's own Wi-Fi ----------
// The P4 never keeps a link: every send / save pull / eject joins the dongle's AP, talks TCP 3333, leaves.
// Since Webby 1.6.8 the AP name is unique (GotekOMEGA-XXXX), so the join needs the REAL name: from this boot's scan,
// else the one remembered for the selected dongle, else a 0.3 s scan of channel 6 for its BSSID (the 3.5" does this
// since lab14s). Two tries; the second one re-learns the name first.
static bool ssidFromScan(const uint8_t* mac, char* out){
  for (int i=0;i<_scanned_count;i++) if(memcmp(_scanned[i].mac,mac,6)==0 && _scanned[i].ssid[0]){ strncpy(out,_scanned[i].ssid,32); out[32]=0; return true; }
  return false;
}
static bool learnSsid(const uint8_t* mac, char* out){
  int n = WiFi.scanNetworks(false, false, false, 300, DONGLE_AP_CHANNEL);
  bool hit=false;
  for (int i=0;i<n;i++){ uint8_t* b=WiFi.BSSID(i); if(b && memcmp(b,mac,6)==0){ strncpy(out,WiFi.SSID(i).c_str(),32); out[32]=0;
      P4LOG("[P4WIFI] dongle %02X:%02X:%02X:%02X:%02X:%02X is '%s', RSSI %d dBm\n",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5],out,(int)WiFi.RSSI(i)); hit=true; break; } }
  WiFi.scanDelete();
  if(!hit) P4LOG("[P4WIFI] dongle %02X:%02X:%02X:%02X:%02X:%02X not heard on channel %d\n",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5],DONGLE_AP_CHANNEL);
  return hit;
}
static bool joinDongle(const uint8_t* mac, uint32_t timeoutMs, const char* why){
  bool haveMac=false; for (int i=0;i<6;i++) if(mac[i]){ haveMac=true; break; }
  char ssid[33]="";
  WiFi.mode(WIFI_STA); WiFi.persistent(false); WiFi.setAutoReconnect(false);
  // lab4: ALWAYS scan channel 6 for the dongle first (0.3 s). gti.log 4 Oct (7", C6 on esp-hosted 2.3.2): every join tried
  // without a fresh scan failed after the full 12-15 s (status 6), every join straight after a scan worked in ~3.8 s.
  if (haveMac && !learnSsid(mac,ssid)) {
    if (!ssidFromScan(mac,ssid) && memcmp(mac,_dongle_mac,6)==0 && _dongle_ssid[0]) { strncpy(ssid,_dongle_ssid,32); ssid[32]=0; }
  }
  if (!ssid[0]) { strncpy(ssid,DONGLE_AP_SSID,32); ssid[32]=0; }
  for (int attempt=1; attempt<=2; attempt++) {
    WiFi.disconnect(false, true); delay(200);
    P4LOG("[P4WIFI] %s: joining '%s' (%s, try %d)\n", why, ssid, haveMac?"BSSID-locked":"any BSSID", attempt);
    if (haveMac) WiFi.begin(ssid, DONGLE_AP_PASS, DONGLE_AP_CHANNEL, (uint8_t*)mac);
    else         WiFi.begin(ssid, DONGLE_AP_PASS);
    uint32_t t0=millis(), lim=(attempt==1)?timeoutMs:10000;   // (a fresh scan comes first, so try 1 normally joins in ~4 s)
    while (WiFi.status()!=WL_CONNECTED && millis()-t0<lim) delay(100);
    if (WiFi.status()==WL_CONNECTED) {
      P4LOG("[P4WIFI] joined in %lu ms, RSSI %d dBm\n",(unsigned long)(millis()-t0),(int)WiFi.RSSI());
      if (haveMac && memcmp(mac,_dongle_mac,6)==0 && strcmp(_dongle_ssid,ssid)!=0) { strncpy(_dongle_ssid,ssid,32); _dongle_ssid[32]=0; saveConfig(); }
      return true;
    }
    P4LOG("[P4WIFI] join failed after %lu ms (status %d)\n",(unsigned long)(millis()-t0),(int)WiFi.status());
    if (attempt==1 && haveMac) { char s2[33]=""; WiFi.disconnect(false,true); delay(100); if (learnSsid(mac,s2)) { strncpy(ssid,s2,32); ssid[32]=0; } }
  }
  WiFi.disconnect();
  return false;
}
// Pull the dongle's game saves over the link that is up (GET_SAVE 0x01). Always answers the dongle (it waits 10 s
// for the ack). Returns the sectors written (0 = nothing to save), or -1 = the pull failed.
static int pullSavesOnLink(const String& ip){
  if (!_persistCb) return 0;
  WiFiClient client; int result=-1;
  if (!client.connect(ip.c_str(), DONGLE_TCP_PORT)) { P4LOG("[P4WIFI/SAVE] TCP connect failed\n"); return -1; }
  uint8_t esc[5]={0xFF,0xFF,0xFF,0xFF,0x01}; client.write(esc,5);
  uint8_t hdr[14]; uint8_t* mapBuf=nullptr; uint8_t* packed=nullptr;
  if (readFullP(client,hdr,14,5000) && hdr[0]=='S' && hdr[1]=='V' && hdr[2]=='1') {
    uint32_t loadId =(uint32_t)hdr[4]|((uint32_t)hdr[5]<<8)|((uint32_t)hdr[6]<<16)|((uint32_t)hdr[7]<<24);
    uint32_t imgSz  =(uint32_t)hdr[8]|((uint32_t)hdr[9]<<8)|((uint32_t)hdr[10]<<16)|((uint32_t)hdr[11]<<24);
    uint16_t mapLen =(uint16_t)hdr[12]|((uint16_t)hdr[13]<<8);
    bool okMap = (mapLen<=1024);
    if (okMap && mapLen>0) { mapBuf=(uint8_t*)malloc(mapLen); okMap = mapBuf && readFullP(client,mapBuf,mapLen,5000); }
    uint32_t nSec=0; if (okMap && mapBuf) for(uint32_t i=0;i<(uint32_t)mapLen*8;i++) if((mapBuf[i>>3]>>(i&7))&1) nSec++;
    bool dataOk = okMap;
    if (dataOk && nSec>0) { packed=(uint8_t*)ps_malloc(nSec*512); if(!packed) packed=(uint8_t*)malloc(nSec*512); dataOk = packed && readFullP(client,packed,nSec*512,30000); }
    uint8_t crcb[4];
    if (dataOk && readFullP(client,crcb,4,5000)) {
      uint32_t crcRx=(uint32_t)crcb[0]|((uint32_t)crcb[1]<<8)|((uint32_t)crcb[2]<<16)|((uint32_t)crcb[3]<<24);
      uint32_t crc=crc32sw(0,mapBuf,mapLen); if(nSec>0) crc=crc32sw(crc,packed,nSec*512);
      bool saved=false;
      if (crc==crcRx) saved = (nSec==0) ? true : _persistCb(loadId,imgSz,mapBuf,mapLen,packed,nSec);
      uint8_t ack=saved?0x01:0x00; client.write(&ack,1); client.flush(); delay(100);
      result = saved ? (int)nSec : -1;
      P4LOG("[P4WIFI/SAVE] %u changed sectors on the dongle (load %lu): %s\n",(unsigned)nSec,(unsigned long)loadId, saved?(nSec?"written back":"nothing to save"):(crc!=crcRx?"CRC mismatch":"write FAILED"));
    } else { uint8_t ack=0x00; client.write(&ack,1); client.flush(); P4LOG("[P4WIFI/SAVE] short read from the dongle\n"); }
  } else P4LOG("[P4WIFI/SAVE] no save header from the dongle\n");
  if (mapBuf) free(mapBuf); if (packed) free(packed);
  client.stop(); delay(20);
  return result;
}

static bool sendDiskCore(const uint8_t* mac, const char* ipc, uint32_t size, uint32_t connectTimeoutMs, bool claim) {
  String ip = String(ipc && ipc[0] ? ipc : DONGLE_AP_IP);

  if (!joinDongle(mac, connectTimeoutMs, "send")) { g_espnow_xiao_error = true; return false; }   // lab3-P4G7: real name + 2 tries
  P4LOG("[P4WIFI] send %lu bytes -> dongle %s:%d\n",(unsigned long)size, ip.c_str(), DONGLE_TCP_PORT);
  if (_diskOut && memcmp(mac,_dongle_mac,6)==0) { int r=pullSavesOnLink(ip); g_p4_saved_secs=r; }   // lab3-P4G7: the disk we sent before may hold saves
  if (claim && !tcpClaim(ip.c_str())) {   // lab15i-P4: another screen's disk is in this dongle and the user said no
    P4LOG("[P4WIFI] not sent - dongle kept for the other screen\n");
    WiFi.disconnect(); return false;
  }
  tcpSendSetName(ip.c_str());   // wireless DSK fix: real filename+ext for the fling
  WiFiClient client;
  if (!client.connect(ip.c_str(), DONGLE_TCP_PORT)) {
    P4LOG("[P4WIFI] TCP connect failed\n");
    WiFi.disconnect(); g_espnow_xiao_error = true; return false;
  }

  uint8_t* src = g_disk + ESPNOW_DATA_LBA * ESPNOW_SECTOR_SIZE;
  uint8_t hdr[4] = { (uint8_t)(size>>24),(uint8_t)(size>>16),(uint8_t)(size>>8),(uint8_t)size };
  client.write(hdr, 4);
  uint32_t sent = 0; const size_t BUF = 4096; uint32_t ts=millis();
  while (sent < size) { size_t n = min((uint32_t)BUF, size-sent); size_t w = client.write(src+sent, n); if(!w){ P4LOG("[P4WIFI] write err at %lu bytes\n",(unsigned long)sent); break; } sent += w; }
  client.clear();
  P4LOG("[P4WIFI] sent %lu of %lu bytes in %lu ms\n",(unsigned long)sent,(unsigned long)size,(unsigned long)(millis()-ts));

  bool ok = false;
  uint32_t t0 = millis(); while (!client.available() && millis()-t0 < 10000) delay(10);
  if (client.available()) {
    ok = (client.read() == 0x01);
    if (ok) {
      uint32_t tid=millis(); uint8_t lid[4]; int got=0;
      while (got<4 && millis()-tid<300) { if(client.available()) lid[got++]=client.read(); else delay(5); }
      g_espnow_load_id = (got==4) ? ((uint32_t)lid[0]|((uint32_t)lid[1]<<8)|((uint32_t)lid[2]<<16)|((uint32_t)lid[3]<<24)) : 0;
    }
  }
  P4LOG("[P4WIFI] dongle answer: %s\n", ok?"OK (disk in)":(millis()-t0>=10000?"none within 10 s":"ERROR"));   // lab2-P4G7
  client.stop();
  WiFi.disconnect(); delay(50);

  if (ok) { g_espnow_xiao_done = true; if (memcmp(mac,_dongle_mac,6)==0) _diskOut = true; } else g_espnow_xiao_error = true;
  return ok;
}

// Single paired dongle — AP-direct, 15 s window, learned BSSID.
bool espnowSendDisk(uint32_t size) {
  return sendDiskCore(_dongle_mac, _dongle_ip.length()?_dongle_ip.c_str():DONGLE_AP_IP, size, 15000, true);   // single dongle: take-over check first
}
// Hivemind fan-out — one dongle by BSSID; shorter window so a powered-off member doesn't stall.
bool espnowSendDiskTo(const uint8_t* mac, uint32_t size) {
  return sendDiskCore(mac, DONGLE_AP_IP, size, 6000, false);   // hivemind fan-out: no take-over question (same as the S3)
}

// Home-WiFi transport: join the router (STA/DHCP), resolve the dongle via mDNS
// gotekomega.local (or cached ioIp), push over TCP-3333. ioIp receives the resolved IP.
bool espnowSendDiskHome(const String& ssid, const String& pass, String& ioIp, uint32_t size) {
  if (ssid.length() == 0) { g_espnow_xiao_error = true; return false; }
  P4LOG("[P4WIFI/HOME] joining '%s'\n", ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false); WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, true); delay(200);
  WiFi.begin(ssid.c_str(), pass.c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis()-t0 < 15000) { delay(200); Serial.print("."); }
  Serial.println();

  bool ok = false;
  if (WiFi.status() == WL_CONNECTED) {
    P4LOG("[P4WIFI/HOME] joined, IP %s\n", WiFi.localIP().toString().c_str());
    String ip = ioIp;
    if (MDNS.begin("gti-remote")) {
      IPAddress r = MDNS.queryHost("gotekomega", 2500);
      if ((uint32_t)r != 0) { ip = r.toString(); ioIp = ip; P4LOG("[P4WIFI/HOME] gotekomega.local -> %s\n", ip.c_str()); }
      MDNS.end();
    }
    if (ip.length() > 0 && !tcpClaim(ip.c_str())) {   // lab15i-P4: take-over check (home Wi-Fi path too, as on the S3)
      P4LOG("[P4WIFI/HOME] not sent - dongle kept for the other screen\n"); ip = "";
    }
    if (ip.length() > 0) {
      tcpSendSetName(ip.c_str());   // wireless DSK fix: real filename+ext for the fling
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
        P4LOG("[P4WIFI/HOME] sent %lu bytes ack=%s\n",(unsigned long)sent, ok?"OK":"ERR");
      } else P4LOG("[P4WIFI/HOME] TCP connect failed\n");
    } else P4LOG("[P4WIFI/HOME] dongle not found (mDNS + no cached IP)\n");
  } else P4LOG("[P4WIFI/HOME] home join failed\n");

  WiFi.disconnect(); delay(50);
  if (ok) g_espnow_xiao_done = true; else g_espnow_xiao_error = true;
  return ok;
}

void espnowSendEject() {
  // lab3-P4G7: real eject over Wi-Fi - join, pull the game saves back, then CMD_EJECT 0x03 (0x04 = force, only after a
  // clean save pull). Was a no-op on the P4 (the dongle kept the disk until the next load replaced it).
  g_p4_eject_result = -1; g_p4_saved_secs = 0;
  if (!g_espnow_paired) return;
  String ip = String(DONGLE_AP_IP);
  if (!joinDongle(_dongle_mac, 12000, "eject")) { g_p4_eject_result = 0; return; }
  int r = _diskOut ? pullSavesOnLink(ip) : 0; g_p4_saved_secs = r;
  WiFiClient c;
  if (c.connect(ip.c_str(), DONGLE_TCP_PORT)) {
    uint8_t esc[5]={0xFF,0xFF,0xFF,0xFF,(uint8_t)(r>=0?0x04:0x03)}; c.write(esc,5);
    uint32_t t0=millis(); while(!c.available() && millis()-t0<3000) delay(5);
    int a = c.available() ? c.read() : -1;
    g_p4_eject_result = (a==0x01) ? 1 : (a==0x02 ? 2 : 0);
    P4LOG("[P4WIFI] eject: dongle answered %s\n", a==0x01?"ejected":(a==0x02?"kept the disk - unsaved writes":"nothing"));
    c.stop(); delay(20);
  } else { P4LOG("[P4WIFI] eject: TCP connect failed\n"); g_p4_eject_result = 0; }
  if (g_p4_eject_result==1) _diskOut = false;
  WiFi.disconnect(); delay(50);
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
  // lab3-P4G7: same join as a send (it used the plain "GotekOMEGA" name, which every Webby 1.6.8+ dongle refuses)
  if (!g_espnow_paired || !persist) return false;
  if (!_persistCb) _persistCb = persist;
  String ip = _dongle_ip.length() ? _dongle_ip : String(DONGLE_AP_IP);
  if (!joinDongle(_dongle_mac, 15000, "save pull")) return false;
  int r = pullSavesOnLink(ip); g_p4_saved_secs = r;
  WiFi.disconnect(); delay(50);
  return r >= 0;
}
