// Gotek_SuperMini_Webby.ino — "Webby-0.1"  (WiFi / web edition of the Super Mini dongle)
// ============================================================================
// SIBLING of Gotek_SuperMini.ino (the base, v3.5.x). SAME proven core — USB-MSC
// ramdisk, build_volume, hardDetach/hardAttach, ESP-NOW + owner-lock, TCP app
// protocol — all reused verbatim. Webby ADDS a WiFi/web layer on top:
//
//   • Two modes, flipped from the web page or a config file:
//       - WIFI   : joins your home WiFi (STA). Web page + app over the LAN.
//       - ESPNOW : own AP on ch6 + ESP-NOW (base behaviour). GTi can drive it.
//                  The web page is still served on the AP at 192.168.4.1.
//     "Revert to ESP-NOW" == "back to AP mode" (ESP-NOW lives on the AP channel).
//   • A dependency-free web front end (ESP32 core WebServer / ESPmDNS / DNSServer):
//       GET  /            -> the "Load Image" + Wi-Fi page
//       GET  /status      -> {loaded,name,size,mode,ip}   (polled for live sync)
//       POST /upload      -> multipart ADF -> ramdisk -> re-insert to the Amiga
//       POST /eject       -> eject
//       POST /savewifi    -> save home creds, mode=WIFI, reboot
//       POST /espnow      -> mode=ESPNOW, reboot (back to AP + ESP-NOW)
//   • Owner-lock is NOT always-on here (Webby = home-LAN trust). The base's
//     owner-lock code is still compiled and usable in ESPNOW mode, but a fresh
//     Webby is unlocked; locking is meant to be enrolled from the GTi later.
//
// *** UNTESTED — compiled/flashed by the owner. No external libraries needed. ***
//
// Board / IDE settings (identical to the base):
//   Board          : ESP32S3 Dev Module   (Super Mini, ESP32-S3FH4R2)
//   USB Mode       : USB-OTG (TinyUSB)     USB CDC on Boot: DISABLED   MSC on Boot: Disabled
//   PSRAM          : QSPI PSRAM (2MB quad — NOT OPI)
//   Flash Size     : 4MB   Partition: Default 4MB w/ spiffs   CPU: 240MHz
// ============================================================================

#include <Arduino.h>
#include "USB.h"
#include "USBMSC.h"
#include "ESP32_NOW.h"
#include "WiFi.h"
#include <WiFiServer.h>
#include <WiFiClient.h>
#include <esp_mac.h>
#include "esp_wifi.h"
#include <LittleFS.h>
#include <WebServer.h>     // WEBBY: built-in (ESP32 core) — no external lib
#include <ESPmDNS.h>       // WEBBY: gotek.local
#include <DNSServer.h>     // WEBBY: captive portal in AP mode

#define FW_VERSION     "Webby-0.1"
#define ESPNOW_CHANNEL 6
#define LED_RED        1
#define LED_BLUE       2
#define BOOT_PIN       0

#define AP_SSID     "GotekOMEGA"
#define AP_PASS     "gotek1234"
#define AP_IP       "192.168.4.1"
#define TCP_PORT    3333
#define MDNS_NAME   "gotek"      // WEBBY: -> gotek.local

// ESP-NOW packet types (unchanged from base)
#define PKT_PAIR_HELLO  0x05
#define PKT_PAIR_REPLY  0x14
#define PKT_DISK_EJECT  0x02
#define PKT_XIAO_READY  0x10
#define PKT_XIAO_DONE   0x12
#define PKT_XIAO_ERROR  0x13
#define PKT_XIAO_DIRTY  0x15
#define PKT_XIAO_STATUS 0x17
#define PKT_UNPAIR      0x16

#define SAVE_PROTO_VER  1
#define SAVE_SETTLE_MS  3000
#define SAVE_BEACON_MS  10000
#define STATUS_BEACON_MS 2500
#define TCP_CMD_ESCAPE  0xFFFFFFFFUL
#define CMD_GET_SAVE    0x01
#define CMD_GET_STATUS  0x02
#define CMD_EJECT       0x03
#define CMD_EJECT_FORCE 0x04

#pragma pack(push,1)
struct PktHello  { uint8_t type; uint8_t mac[6]; char ip[16]; uint8_t pad[227]; };
struct PktSimple { uint8_t type; uint8_t pad[249]; };
struct PktDirty  { uint8_t type; uint32_t load_id; uint16_t dirty_count;
                   uint32_t image_size; uint32_t age_ms; uint8_t flags; uint8_t pad[234]; };
struct PktStatus { uint8_t type; uint8_t loaded; uint32_t load_id; uint32_t image_size; uint8_t pad[240]; };
#pragma pack(pop)

// FAT12 geometry
#define SECTOR_SIZE      512
#define TOTAL_SECTORS    2048
#define RESERVED_SECTORS 1
#define SECTORS_PER_FAT  6
#define NUM_FATS         1
#define ROOT_ENTRIES     64
#define ROOT_DIR_SECTORS 4
#define DATA_LBA         11
#define MAX_FILE_BYTES   ((uint32_t)(TOTAL_SECTORS-DATA_LBA)*SECTOR_SIZE)
#define ADF_DEFAULT_SIZE 901120

static uint8_t* g_disk = nullptr;

// Save-writeback state (unchanged from base)
#define IMG_MAX_SECTORS (TOTAL_SECTORS - DATA_LBA)
static uint8_t  g_dirty[(IMG_MAX_SECTORS+7)/8];
static uint8_t  g_snap [(IMG_MAX_SECTORS+7)/8];
static volatile uint16_t g_dirty_count   = 0;
static volatile uint32_t g_last_write_ms = 0;
static volatile uint32_t g_total_writes  = 0;
static uint32_t g_load_id    = 0;
static uint32_t g_image_size = ADF_DEFAULT_SIZE;
static uint32_t g_next_beacon_ms = 0;
static uint32_t g_next_status_ms = 0;
static inline bool dGet(const uint8_t*m,uint32_t i){return (m[i>>3]>>(i&7))&1;}
static inline void dSet(uint8_t*m,uint32_t i){m[i>>3]|=(uint8_t)(1u<<(i&7));}
static inline void dClr(uint8_t*m,uint32_t i){m[i>>3]&=(uint8_t)~(1u<<(i&7));}
static void dirtyReset(){memset(g_dirty,0,sizeof(g_dirty));g_dirty_count=0;g_last_write_ms=0;g_next_beacon_ms=0;}
static uint32_t crc32sw(uint32_t crc,const uint8_t*p,size_t n){
  crc=~crc;
  while(n--){crc^=*p++;for(int k=0;k<8;k++)crc=(crc>>1)^(0xEDB88320UL&(uint32_t)(-(int32_t)(crc&1)));}
  return ~crc;
}

static void setLeds(bool red, bool blue){ digitalWrite(LED_RED, red?HIGH:LOW); digitalWrite(LED_BLUE, blue?HIGH:LOW); }
static void oledStatus(const String& l0, const String& l1, const String& l2, const String& l3) {
  (void)l1;(void)l2;(void)l3;
  if (l0.startsWith("**") || l0.startsWith("Receiving") || l0.startsWith("Gotek") || l0.startsWith("LOADED")) setLeds(false,true);
  else if (l0.startsWith("Not paired")) setLeds(true,false);
}
static void oledProgress(uint32_t done, uint32_t total) {
  (void)total; static bool t=false; t=!t; digitalWrite(LED_BLUE, t?HIGH:LOW); (void)done;
}

// TinyUSB
extern "C" { bool tud_mounted(void); void tud_disconnect(void); void tud_connect(void); }
static USBMSC   MSC;
static bool     g_usb_online  = false;
static bool     g_disk_loaded = false;
static uint32_t g_rev_counter = 1;

static int32_t onRead(uint32_t lba, uint32_t off, void* buf, uint32_t n) {
  uint32_t s = lba*SECTOR_SIZE+off;
  if (s+n > TOTAL_SECTORS*SECTOR_SIZE) return 0;
  memcpy(buf, g_disk+s, n); return (int32_t)n;
}
static int32_t onWrite(uint32_t lba, uint32_t off, uint8_t* buf, uint32_t n) {
  uint32_t s = lba*SECTOR_SIZE+off;
  if (s+n > TOTAL_SECTORS*SECTOR_SIZE) return 0;
  memcpy(g_disk+s, buf, n);
  g_total_writes=g_total_writes+1;
  uint32_t first=s/SECTOR_SIZE, last=(s+n-1)/SECTOR_SIZE;
  uint32_t imgSecs=(g_image_size+SECTOR_SIZE-1)/SECTOR_SIZE;
  if(imgSecs>IMG_MAX_SECTORS)imgSecs=IMG_MAX_SECTORS;
  for(uint32_t l=first;l<=last;l++){
    if(l<DATA_LBA)continue;
    uint32_t i=l-DATA_LBA;
    if(i>=imgSecs)continue;
    if(!dGet(g_dirty,i)){dSet(g_dirty,i);g_dirty_count=g_dirty_count+1;}
  }
  g_last_write_ms=millis();
  return (int32_t)n;
}
static void usbEventCb(void*,esp_event_base_t,int32_t,void*) {}
static void hardDetach() { MSC.mediaPresent(false); delay(100); tud_disconnect(); delay(500); g_usb_online=false; }
static void hardAttach() {
  char rev[8]; snprintf(rev,sizeof(rev),"%lu",(unsigned long)g_rev_counter++);
  MSC.productRevision(rev); MSC.mediaPresent(true); delay(50); tud_connect(); delay(200);
  g_usb_online=true;
}

// FAT12 writers
static inline void wr16(uint8_t*p,int o,uint16_t v){p[o]=(uint8_t)v;p[o+1]=(uint8_t)(v>>8);}
static inline void wr32(uint8_t*p,int o,uint32_t v){p[o]=(uint8_t)v;p[o+1]=(uint8_t)(v>>8);p[o+2]=(uint8_t)(v>>16);p[o+3]=(uint8_t)(v>>24);}
static void fat12_set(uint8_t*fat,uint16_t cl,uint16_t v){
  uint32_t i=(cl*3)/2;
  if((cl&1)==0){fat[i]=(uint8_t)(v&0xFF);fat[i+1]=(uint8_t)((fat[i+1]&0xF0)|((v>>8)&0x0F));}
  else{fat[i]=(uint8_t)((fat[i]&0x0F)|((v<<4)&0xF0));fat[i+1]=(uint8_t)((v>>4)&0xFF);}
}
// Write ONLY the boot sector + FAT + root dir (sectors 0..DATA_LBA-1). `wipeAll`
// controls whether the data region is cleared too. WEBBY streams the file straight
// into the data area first, then lays the metadata with wipeAll=false so the bytes
// already written survive. The base path uses wipeAll=true (fresh volume).
static void build_volume_ex(const char* outName, uint32_t fsz, bool wipeAll) {
  if (fsz>MAX_FILE_BYTES) fsz=MAX_FILE_BYTES;
  memset(g_disk, 0, (wipeAll ? (size_t)TOTAL_SECTORS*SECTOR_SIZE : (size_t)DATA_LBA*SECTOR_SIZE));
  uint8_t* bs=g_disk;
  bs[0]=0xEB;bs[1]=0x3C;bs[2]=0x90;memcpy(&bs[3],"MSDOS5.0",8);
  wr16(bs,11,SECTOR_SIZE);bs[13]=1;wr16(bs,14,RESERVED_SECTORS);bs[16]=NUM_FATS;
  wr16(bs,17,ROOT_ENTRIES);wr16(bs,19,TOTAL_SECTORS);bs[21]=0xF8;
  wr16(bs,22,SECTORS_PER_FAT);wr16(bs,24,32);wr16(bs,26,64);
  bs[36]=0x80;bs[38]=0x29;wr32(bs,39,0x12345678);
  memcpy(&bs[43],"ESP32MSC   ",11);memcpy(&bs[54],"FAT12   ",8);
  bs[510]=0x55;bs[511]=0xAA;
  uint8_t* fat=g_disk+RESERVED_SECTORS*SECTOR_SIZE;
  fat[0]=0xF8;fat[1]=0xFF;fat[2]=0xFF;
  uint32_t need=(fsz+511)/512;
  for(uint32_t i=0;i<need;i++){ uint16_t c=(uint16_t)(2+i); fat12_set(fat,c,(i==(need-1))?0x0FFF:(c+1)); }
  uint8_t* root=fat+SECTORS_PER_FAT*SECTOR_SIZE;
  char n[8],e[3]; memset(n,' ',8); memset(e,' ',3);
  const char* dot=strrchr(outName,'.');
  size_t nl=dot?(size_t)(dot-outName):strlen(outName);
  for(size_t i=0;i<nl&&i<8;i++) n[i]=toupper(outName[i]);
  if(dot) for(size_t i=0;i<3&&dot[1+i];i++) e[i]=toupper(dot[1+i]);
  memcpy(&root[0],n,8);memcpy(&root[8],e,3);
  root[11]=0x20;wr16(root,26,2);wr32(root,28,fsz);
}
static void build_volume(const char* outName, uint32_t fsz){ build_volume_ex(outName,fsz,true); }

static String macToStr(const uint8_t* mac) {
  char buf[18];
  snprintf(buf,sizeof(buf),"%02X:%02X:%02X:%02X:%02X:%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
  return String(buf);
}

// State
static uint8_t _wave_mac[6] = {0};
static bool    _paired       = false;

// Owner lock (base)
#define MAX_OWNERS 4
static uint8_t  _owners[MAX_OWNERS][6] = {{0}};
static uint8_t  _owner_count   = 0;
static bool     g_enroll_open  = false;
static uint32_t g_enroll_until = 0;

// ESP-NOW receive queue
#define RX_PKT_SIZE 250
struct RxPkt { uint8_t data[RX_PKT_SIZE]; int len; };
static QueueHandle_t _rxQueue = nullptr;
static void queuePacket(const uint8_t* data, int len) {
  if (!_rxQueue) return;
  RxPkt pkt; int n = min(len, RX_PKT_SIZE);
  memcpy(pkt.data, data, n); pkt.len = n;
  xQueueSendFromISR(_rxQueue, &pkt, nullptr);
}
static void handleESPNOW(const uint8_t* data, int len);

class XiaoPeer : public ESP_NOW_Peer {
public:
  XiaoPeer(const uint8_t* mac, uint8_t ch, wifi_interface_t iface, const uint8_t* lmk)
    : ESP_NOW_Peer(mac, ch, iface, lmk) {}
  ~XiaoPeer() { remove(); }
  bool add_peer() { return add(); }
  bool send_pkt(const uint8_t* d, size_t l) { return send(d, l); }
  void onReceive(const uint8_t* d, size_t l, bool b) override { queuePacket(d, (int)l); }
  void onSent(bool) override {}
};
static XiaoPeer* _bcastPeer = nullptr;
static XiaoPeer* _wavePeer  = nullptr;
static void sendSimple(uint8_t type) {
  PktSimple pkt = {}; pkt.type = type;
  XiaoPeer* dst = _wavePeer ? _wavePeer : _bcastPeer;
  if (dst) dst->send_pkt((uint8_t*)&pkt, sizeof(pkt));
}

// Owner-lock helpers (base)
static bool isOwner(const uint8_t* mac){ for(int i=0;i<_owner_count;i++) if(memcmp(_owners[i],mac,6)==0) return true; return false; }
static bool addOwner(const uint8_t* mac){ if(isOwner(mac)) return true; if(_owner_count>=MAX_OWNERS) return false; memcpy(_owners[_owner_count],mac,6); _owner_count++; return true; }
static bool removeOwner(const uint8_t* mac){
  for(int i=0;i<_owner_count;i++) if(memcmp(_owners[i],mac,6)==0){
    for(int j=i;j<_owner_count-1;j++) memcpy(_owners[j],_owners[j+1],6);
    _owner_count--; return true; }
  return false;
}
static void saveOwners(){
  if(!LittleFS.begin(true)) return;
  File f=LittleFS.open("/XIAO_CONFIG.TXT","w"); if(!f) return;
  for(int i=0;i<_owner_count;i++) f.printf("WAVE_MAC=%s\n", macToStr(_owners[i]).c_str());
  f.close();
}
static void wipeOwners(){
  _owner_count=0; _paired=false; memset(_wave_mac,0,6);
  if(_wavePeer){ delete _wavePeer; _wavePeer=nullptr; }
  if(LittleFS.begin(true)) LittleFS.remove("/XIAO_CONFIG.TXT");
  oledStatus("Gotek OMEGA " FW_VERSION,"** WIPED **","All owners cleared","Hold BOOT to pair");
}

static void handleESPNOW(const uint8_t* data, int len) {
  if (len < 1) return;
  uint8_t type = data[0];
  if (type == PKT_PAIR_HELLO) {
    const PktHello* p = (const PktHello*)data;
    // WEBBY note: Webby ships unlocked, so with no enrolled owners any GTi may pair
    // (g_enroll_open is forced true at boot in ESPNOW mode when _owner_count==0).
    bool known = isOwner(p->mac);
    if (!known) {
      if (!g_enroll_open) return;
      if (!addOwner(p->mac)) { oledStatus("Gotek OMEGA " FW_VERSION,"OWNERS FULL","Hold BOOT 15s","to wipe & re-pair"); return; }
      saveOwners();
      if (_owner_count>0) g_enroll_open = false;   // once a real owner exists, close the door
    }
    memcpy(_wave_mac, p->mac, 6); _paired = true;
    if (_wavePeer) { delete _wavePeer; _wavePeer = nullptr; }
    _wavePeer = new XiaoPeer(_wave_mac, ESPNOW_CHANNEL, WIFI_IF_STA, nullptr);
    if (!_wavePeer->add_peer()) { delete _wavePeer; _wavePeer = nullptr; }
    PktHello reply = {}; reply.type = PKT_PAIR_REPLY;
    WiFi.softAPmacAddress(reply.mac); strncpy(reply.ip, AP_IP, 15); reply.pad[0] = SAVE_PROTO_VER;
    XiaoPeer* dst = _wavePeer ? _wavePeer : _bcastPeer;
    if (dst) dst->send_pkt((uint8_t*)&reply, sizeof(reply));
    oledStatus("Gotek OMEGA " FW_VERSION, known?"Reconnected":"Owner added", macToStr(_wave_mac), String(_owner_count)+" owner(s)");
    return;
  }
  if (type == PKT_UNPAIR) {
    const PktHello* p = (const PktHello*)data;
    if (removeOwner(p->mac)) {
      saveOwners();
      if (memcmp(_wave_mac, p->mac, 6)==0) {
        if (_wavePeer) { delete _wavePeer; _wavePeer=nullptr; }
        if (_owner_count) memcpy(_wave_mac, _owners[0], 6); else memset(_wave_mac,0,6);
      }
      _paired = (_owner_count>0);
      oledStatus("Gotek OMEGA " FW_VERSION, "Unpaired", String(_owner_count)+" owner(s)", "");
    }
    return;
  }
  if (type == PKT_DISK_EJECT) {
    if (g_disk_loaded) { hardDetach(); g_disk_loaded=false; }
    dirtyReset(); digitalWrite(LED_BLUE, LOW);
    oledStatus("Gotek OMEGA " FW_VERSION, "Ejected", "", "Ready");
    return;
  }
}
static void onNewPeer(const esp_now_recv_info_t* info, const uint8_t* data, int len, void* arg) { queuePacket(data, len); }

// Owner config load (base)
static void loadConfig() {
  if (!LittleFS.begin(true)) return;
  File f = LittleFS.open("/XIAO_CONFIG.TXT", "r"); if (!f) return;
  _owner_count = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n'); line.trim();
    if (line.startsWith("WAVE_MAC=")) {
      uint8_t m[6]={0}; String mac = line.substring(9);
      if (sscanf(mac.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",&m[0],&m[1],&m[2],&m[3],&m[4],&m[5]) == 6) {
        bool isZero=true; for(int i=0;i<6;i++) if(m[i]) { isZero=false; break; }
        if (!isZero) addOwner(m);
      }
    }
  }
  f.close();
  _paired = (_owner_count > 0);
  if (_paired) memcpy(_wave_mac, _owners[0], 6);
}

// ============================================================================
// WEBBY: WiFi-mode config (creds + mode) in LittleFS
// ============================================================================
static String g_ssid = "", g_pass = "", g_modeStr = "";   // g_modeStr: "WIFI" or "ESPNOW"
static int    g_webmode = 0;   // resolved at boot: 0 = ESPNOW/AP, 1 = WIFI/STA
static String g_loaded_name = "";

static void loadWifiCfg() {
  if (!LittleFS.begin(true)) return;
  File f = LittleFS.open("/WEBBY.TXT", "r"); if (!f) return;
  while (f.available()) {
    String line = f.readStringUntil('\n'); line.trim();
    if      (line.startsWith("SSID=")) g_ssid    = line.substring(5);
    else if (line.startsWith("PASS=")) g_pass    = line.substring(5);
    else if (line.startsWith("MODE=")) g_modeStr = line.substring(5);
  }
  f.close();
}
static void saveWifiCfg(const String& ssid, const String& pass) {
  if (!LittleFS.begin(true)) return;
  File f = LittleFS.open("/WEBBY.TXT", "w"); if (!f) return;
  f.printf("SSID=%s\n", ssid.c_str());
  f.printf("PASS=%s\n", pass.c_str());
  f.printf("MODE=WIFI\n");
  f.close();
}
static void setModeEspnow() {
  if (!LittleFS.begin(true)) return;
  // keep any saved SSID/PASS, just flip MODE=ESPNOW
  File f = LittleFS.open("/WEBBY.TXT", "w"); if (!f) return;
  f.printf("SSID=%s\n", g_ssid.c_str());
  f.printf("PASS=%s\n", g_pass.c_str());
  f.printf("MODE=ESPNOW\n");
  f.close();
}

// ── base TCP save/eject/status handlers (unchanged) ─────────────────────────
static WiFiServer _tcpServer(TCP_PORT);
static inline void wrLE32(uint8_t*p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static inline void wrLE16(uint8_t*p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void doEject(WiFiClient& client, bool force){
  if (!force && g_dirty_count > 0) { client.write((uint8_t)0x02); client.flush(); return; }
  if (g_disk_loaded) { hardDetach(); g_disk_loaded = false; }
  dirtyReset(); g_loaded_name=""; digitalWrite(LED_BLUE, LOW);
  oledStatus("Gotek OMEGA " FW_VERSION, "Ejected (app)", "", "Ready");
  client.write((uint8_t)0x01); client.flush();
}
static void doGetStatus(WiFiClient& client){
  uint8_t r[21]; r[0]='S'; r[1]='T'; r[2]=SAVE_PROTO_VER;
  wrLE32(r+3,g_load_id); wrLE16(r+7,g_dirty_count);
  wrLE32(r+9,g_last_write_ms?(millis()-g_last_write_ms):0xFFFFFFFFUL);
  wrLE32(r+13,g_total_writes); wrLE32(r+17,g_image_size);
  client.write(r,21); client.flush();
}
static void doGetSave(WiFiClient& client){
  uint32_t imgSecs=(g_image_size+SECTOR_SIZE-1)/SECTOR_SIZE; if(imgSecs>IMG_MAX_SECTORS)imgSecs=IMG_MAX_SECTORS;
  uint16_t mapLen=(uint16_t)((imgSecs+7)/8);
  memcpy(g_snap,g_dirty,mapLen);
  uint8_t hdr[14]; hdr[0]='S';hdr[1]='V';hdr[2]='1';hdr[3]=0;
  wrLE32(hdr+4,g_load_id); wrLE32(hdr+8,g_image_size); wrLE16(hdr+12,mapLen);
  client.write(hdr,14);
  uint32_t crc=crc32sw(0,g_snap,mapLen);
  client.write(g_snap,mapLen);
  uint32_t sent=0;
  for(uint32_t i=0;i<imgSecs;i++){ if(!dGet(g_snap,i))continue; uint8_t* sec=g_disk+(DATA_LBA+i)*SECTOR_SIZE; client.write(sec,SECTOR_SIZE); crc=crc32sw(crc,sec,SECTOR_SIZE); sent++; oledProgress(sent,g_dirty_count); }
  uint8_t cb[4]; wrLE32(cb,crc); client.write(cb,4); client.flush();
  uint32_t t0=millis(); while(!client.available()&&millis()-t0<10000)delay(5);
  bool ok=(client.available()&&client.read()==0x01);
  if(ok){ for(uint32_t i=0;i<imgSecs;i++) if(dGet(g_snap,i)&&dGet(g_dirty,i)){dClr(g_dirty,i);if(g_dirty_count)g_dirty_count=g_dirty_count-1;} g_next_beacon_ms=0; if(g_dirty_count==0) setLeds(false,g_disk_loaded); }
}
static void sendDirtyBeacon(){
  PktDirty pkt={}; pkt.type=PKT_XIAO_DIRTY; pkt.load_id=g_load_id; pkt.dirty_count=g_dirty_count;
  pkt.image_size=g_image_size; pkt.age_ms=g_last_write_ms?(millis()-g_last_write_ms):0; pkt.flags=0;
  XiaoPeer* dst=_wavePeer?_wavePeer:_bcastPeer; if(dst)dst->send_pkt((uint8_t*)&pkt,sizeof(pkt));
}
static void sendStatusBeacon(){
  if(!_wavePeer)return;
  PktStatus pkt={}; pkt.type=PKT_XIAO_STATUS; pkt.loaded=g_disk_loaded?1:0; pkt.load_id=g_load_id; pkt.image_size=g_image_size;
  _wavePeer->send_pkt((uint8_t*)&pkt,sizeof(pkt));
}

static void handleTCPClient(WiFiClient& client) {
  oledStatus("Receiving...", "TCP connected", "", "");
  uint32_t t0 = millis();
  while (client.available() < 4 && millis()-t0 < 5000) delay(1);
  if (client.available() < 4) { client.write((uint8_t)0x00); return; }
  uint8_t hdr[4]; client.read(hdr, 4);
  uint32_t size = ((uint32_t)hdr[0]<<24)|((uint32_t)hdr[1]<<16)|((uint32_t)hdr[2]<<8)|(uint32_t)hdr[3];
  if (size == TCP_CMD_ESCAPE) {
    t0 = millis(); while (client.available() < 1 && millis()-t0 < 3000) delay(1);
    if (!client.available()) { client.write((uint8_t)0x00); return; }
    uint8_t cmd = client.read();
    if      (cmd == CMD_GET_SAVE)    doGetSave(client);
    else if (cmd == CMD_GET_STATUS)  doGetStatus(client);
    else if (cmd == CMD_EJECT)       doEject(client,false);
    else if (cmd == CMD_EJECT_FORCE) doEject(client,true);
    else client.write((uint8_t)0x00);
    return;
  }
  if (size == 0 || size > MAX_FILE_BYTES) { client.write((uint8_t)0x00); return; }
  const char* outName = "DISK.ADF";
  build_volume(outName, size);
  uint8_t* dst = g_disk + DATA_LBA * SECTOR_SIZE;
  uint32_t received = 0; const size_t BUF = 4096;
  uint8_t* buf = (uint8_t*)malloc(BUF); if (!buf) { client.write((uint8_t)0x00); return; }
  t0 = millis();
  while (received < size && millis()-t0 < 30000) {
    if (!client.connected()) break;
    int avail = client.available(); if (avail <= 0) { delay(1); continue; }
    size_t toRead = min((size_t)avail, min(BUF, (size_t)(size-received)));
    int rd = client.read(buf, toRead);
    if (rd > 0) { memcpy(dst + received, buf, rd); received += rd; oledProgress(received, size); }
  }
  free(buf);
  if (received == size) {
    g_load_id++; g_image_size = size; dirtyReset(); g_loaded_name = "DISK.ADF";
    uint8_t ack[5]; ack[0]=0x01; wrLE32(ack+1,g_load_id); client.write(ack,5); client.flush(); delay(100); client.stop();
    if (g_disk_loaded) hardDetach(); hardAttach(); g_disk_loaded = true; g_next_status_ms = 0; digitalWrite(LED_BLUE, HIGH);
    oledStatus("LOADED!", "", "USB: attached", "Gotek ready");
    sendSimple(PKT_XIAO_DONE);
  } else {
    client.write((uint8_t)0x00); client.flush(); delay(100); client.stop();
    oledStatus("TRANSFER ERROR", "", "", ""); sendSimple(PKT_XIAO_ERROR);
  }
}

// ============================================================================
// WEBBY: the web front end
// ============================================================================
static WebServer  server(80);
static DNSServer  dnsServer;
static bool       g_dns_up = false;

// upload streaming state
static uint32_t g_up_recv = 0;
static bool     g_up_overflow = false;
static String   g_up_name = "";

static String jsonEsc(const String& s){
  String o; for(size_t i=0;i<s.length();i++){ char c=s[i]; if(c=='"'||c=='\\'){o+='\\';o+=c;} else if(c>=32) o+=c; } return o;
}
static String statusJson(){
  String ip = (g_webmode==1) ? WiFi.localIP().toString() : String(AP_IP);
  String s = "{";
  s += "\"loaded\":"; s += (g_disk_loaded?"true":"false");
  s += ",\"name\":\""; s += jsonEsc(g_loaded_name); s += "\"";
  s += ",\"size\":"; s += String(g_image_size);
  s += ",\"mode\":\""; s += (g_webmode==1 ? "wifi" : "espnow"); s += "\"";
  s += ",\"ip\":\""; s += ip; s += "\"";
  s += "}";
  return s;
}

// Finalize a browser upload: lay metadata over the streamed data, re-insert.
static void webFinishLoad(){
  uint32_t size = g_up_recv;
  build_volume_ex(g_up_name.c_str(), size, false);   // metadata only — data already streamed in
  g_image_size = size; g_load_id++; dirtyReset();
  if (g_disk_loaded) hardDetach();
  hardAttach(); g_disk_loaded = true; g_next_status_ms = 0; digitalWrite(LED_BLUE, HIGH);
  oledStatus("LOADED!", "", "USB: attached", "Gotek ready");
  sendSimple(PKT_XIAO_DONE);   // harmless if no ESP-NOW peer
}
static void handleUpload(){
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    g_up_recv = 0; g_up_overflow = false;
    g_up_name = up.filename; if (g_up_name.length()==0) g_up_name = "DISK.ADF";
    // detach first so the Amiga isn't reading the disk while we rewrite its data region
    if (g_disk_loaded) { hardDetach(); g_disk_loaded = false; digitalWrite(LED_BLUE, LOW); }
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!g_up_overflow && g_up_recv + up.currentSize <= MAX_FILE_BYTES) {
      memcpy(g_disk + DATA_LBA*SECTOR_SIZE + g_up_recv, up.buf, up.currentSize);
      g_up_recv += up.currentSize;
    } else { g_up_overflow = true; }
  } else if (up.status == UPLOAD_FILE_END) {
    // finalized by the POST responder below
  }
}
static void handleUploadDone(){
  if (g_up_overflow) { server.send(413,"application/json","{\"ok\":false,\"err\":\"image too big for the 1MB ramdisk (DD only)\"}"); return; }
  if (g_up_recv == 0) { server.send(400,"application/json","{\"ok\":false,\"err\":\"empty upload\"}"); return; }
  g_loaded_name = g_up_name;
  webFinishLoad();
  server.send(200,"application/json", statusJson());
}
static void handleEjectWeb(){
  if (g_disk_loaded) { hardDetach(); g_disk_loaded = false; }
  dirtyReset(); g_loaded_name=""; digitalWrite(LED_BLUE, LOW);
  oledStatus("Gotek OMEGA " FW_VERSION, "Ejected (web)", "", "Ready");
  server.send(200,"application/json", statusJson());
}
static void handleSaveWifi(){
  String ssid = server.arg("ssid"); String pass = server.arg("pass");
  if (ssid.length()==0) { server.send(400,"application/json","{\"ok\":false,\"err\":\"no SSID\"}"); return; }
  saveWifiCfg(ssid, pass);
  server.send(200,"application/json","{\"ok\":true}");
  delay(400); ESP.restart();
}
static void handleEspnowWeb(){
  setModeEspnow();
  server.send(200,"application/json","{\"ok\":true}");
  delay(400); ESP.restart();
}

// The page (self-contained; talks to the real endpoints above)
static const char PAGE_HTML[] PROGMEM = R"HTML(<!doctype html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>Webby - Gotek OMEGA</title><style>
:root{--bg:#0f1016;--panel:#181a24;--panel2:#1f2230;--line:#2b2f42;--ink:#e9ecf5;--dim:#8b93ad;--amber:#ffca57;--cyan:#3fe0e8;--green:#54d67e;--red:#ff6b6b}
*{box-sizing:border-box}html,body{margin:0}body{background:radial-gradient(120% 80% at 50% -10%,#1a1d2b,var(--bg) 60%);color:var(--ink);min-height:100vh;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,system-ui,sans-serif;padding:env(safe-area-inset-top) 0 0}
.wrap{max-width:460px;margin:0 auto;padding:18px 16px 40px}
header{display:flex;align-items:center;gap:10px;margin-bottom:16px}
.badge{width:38px;height:38px;border-radius:9px;background:linear-gradient(150deg,#2a2e42,#12131b);border:1px solid var(--line);display:grid;place-items:center;flex:none}
.badge b{font-weight:800;font-size:15px;color:var(--amber)}
.title h1{margin:0;font-size:15px;letter-spacing:.4px}.title span{font-size:11px;color:var(--dim);text-transform:uppercase;letter-spacing:1.4px}
.conn{margin-left:auto;display:flex;align-items:center;gap:7px;font-size:11.5px;color:var(--dim);background:var(--panel);border:1px solid var(--line);padding:6px 10px;border-radius:20px}
.dot{width:8px;height:8px;border-radius:50%;background:var(--green);box-shadow:0 0 8px var(--green)}.dot.ap{background:var(--amber);box-shadow:0 0 8px var(--amber)}
.tabs{display:flex;background:var(--panel);border:1px solid var(--line);border-radius:12px;padding:4px;margin-bottom:16px}
.tabs button{flex:1;border:0;background:transparent;color:var(--dim);font:inherit;font-weight:600;font-size:13px;padding:9px;border-radius:9px;cursor:pointer}
.tabs button.on{background:var(--panel2);color:var(--ink)}
.card{background:var(--panel);border:1px solid var(--line);border-radius:14px;padding:16px;margin-bottom:14px}
.card h2{margin:0 0 12px;font-size:11px;letter-spacing:1.5px;text-transform:uppercase;color:var(--dim)}
.drive{display:flex;align-items:center;gap:14px}
.floppy{width:52px;height:52px;flex:none;display:grid;place-items:center}.floppy svg{width:52px;height:52px;display:block}.floppy.empty svg{filter:grayscale(1) brightness(.85);opacity:.4}
.drive .name{font-weight:700;font-size:15px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.drive .sub{font-size:12px;color:var(--dim);margin-top:2px}.drive .sub.live{color:var(--cyan)}
.btn{border:0;border-radius:10px;font:inherit;font-weight:700;font-size:14px;padding:11px 16px;cursor:pointer}
.btn.ghost{background:var(--panel2);color:var(--ink);border:1px solid var(--line)}.btn.amber{background:linear-gradient(180deg,#ffd06a,#f0a92e);color:#241a02}.btn.wide{width:100%;padding:14px}.btn:disabled{opacity:.4;cursor:default}
.eject{margin-left:auto;flex:none}
.drop{display:flex;flex-direction:column;align-items:center;justify-content:center;margin-top:4px;border:2px dashed #3a4064;border-radius:14px;padding:26px 16px;text-align:center;cursor:pointer;background:#141626}
.drop.hot{border-color:var(--amber);background:#1c1a10}.drop .big{font-size:15px;font-weight:700}.drop .small{font-size:12px;color:var(--dim);margin-top:5px}.drop .plus{font-size:26px;color:var(--amber);margin-bottom:6px}
input[type=file]{display:none}
.prog{height:8px;background:#0e1120;border-radius:6px;overflow:hidden;margin-top:14px;display:none}.prog i{display:block;height:100%;width:0;background:linear-gradient(90deg,var(--cyan),var(--amber));transition:width .12s}
.field{margin-top:12px}.field label{display:block;font-size:12px;color:var(--dim);margin-bottom:6px}
.field input{width:100%;background:#0e1120;border:1px solid var(--line);border-radius:10px;color:var(--ink);font:inherit;font-size:15px;padding:12px}
.hint{font-size:12px;color:var(--dim);margin-top:12px;line-height:1.5}.note{font-size:11.5px;color:var(--dim);text-align:center;margin-top:6px}
.toast{position:fixed;left:50%;bottom:22px;transform:translate(-50%,20px);opacity:0;pointer-events:none;background:#0c1a12;border:1px solid #2f6b45;color:#c9f5d8;padding:11px 16px;border-radius:11px;font-size:13.5px;font-weight:600;transition:.25s;max-width:90%}
.toast.show{opacity:1;transform:translate(-50%,0)}.toast.err{background:#1e0f12;border-color:#6b3030;color:#ffd3d3}
footer{text-align:center;color:#5b6076;font-size:11px;margin-top:22px}.hidden{display:none}
</style></head><body><div class="wrap">
<header><div class="badge"><b>&#937;</b></div><div class="title"><h1>GOTEK&nbsp;OMEGA</h1><span>Webby</span></div>
<div class="conn"><span class="dot" id="condot"></span><span id="conntxt">...</span></div></header>
<div class="tabs"><button class="on" id="tabDisk" onclick="view('disk')">Disk</button><button id="tabWifi" onclick="view('wifi')">Wi-Fi</button></div>
<section id="vDisk">
 <div class="card"><h2>In the drive</h2><div class="drive">
  <div class="floppy empty" id="floppy"><svg viewBox="0 0 52 52" aria-hidden="true">
   <path d="M7 5 H40 L46 11 V45 A2 2 0 0 1 44 47 H9 A2 2 0 0 1 7 45 V5 Z" fill="#262e4a" stroke="#3b4570" stroke-width="1"/>
   <rect x="14" y="5" width="22" height="13" rx="1" fill="#b9c1da"/><rect x="29" y="7" width="5" height="9" rx="1" fill="#2a3150"/>
   <rect x="11" y="25" width="30" height="19" rx="2" fill="#eef1fb"/><rect x="14" y="30" width="22" height="2.4" rx="1" fill="#aeb7d6"/><rect x="14" y="35" width="15" height="2.4" rx="1" fill="#c3ccec"/></svg></div>
  <div class="meta" style="min-width:0"><div class="name" id="dname">-- no disk --</div><div class="sub" id="dsub">Load an ADF to insert it</div></div>
  <button class="btn ghost eject" id="ejectBtn" onclick="ejectDisk()" disabled>Eject</button></div></div>
 <div class="card"><h2>Load image</h2>
  <label class="drop" id="drop" for="file"><div class="plus">+</div><div class="big">Tap to choose an ADF</div><div class="small">or drag a file here &middot; .adf .adz .img</div></label>
  <input type="file" id="file" accept=".adf,.adz,.img" onchange="picked(this.files[0])">
  <div class="prog" id="prog"><i id="bar"></i></div><div class="note">DD image up to ~880&nbsp;KB &middot; one disk at a time</div></div>
</section>
<section id="vWifi" class="hidden">
 <div class="card"><h2>Home Wi-Fi</h2>
  <div class="field"><label>Network (SSID)</label><input id="ssid" placeholder="your home wifi name"></div>
  <div class="field"><label>Password</label><input id="pass" type="password" placeholder="&bull;&bull;&bull;&bull;&bull;&bull;"></div>
  <button class="btn amber wide" style="margin-top:16px" onclick="joinWifi()">Save &amp; Join</button>
  <div class="hint">Saves and reboots onto your home Wi-Fi. After it joins, open <b>gotek.local</b> in any browser on that network.</div></div>
 <div class="card"><h2>ESP-NOW / GTi mode</h2>
  <div class="hint" style="margin-top:0">Switch back to the dongle's own access point + ESP-NOW so a GTi screen can drive it. Reconnect to the <b>GotekOMEGA</b> Wi-Fi afterwards to return here.</div>
  <button class="btn ghost wide" style="margin-top:14px" onclick="toEspnow()">Disconnect Wi-Fi &rarr; ESP-NOW</button></div>
</section>
<footer>Webby-0.1 &middot; OMEGAWARE</footer></div>
<div class="toast" id="toast"></div>
<script>
function fmt(b){return b>=1048576?(b/1048576).toFixed(2)+' MB':Math.round(b/1024)+' KB';}
function toast(m,e){var t=document.getElementById('toast');t.textContent=m;t.classList.toggle('err',!!e);t.classList.add('show');clearTimeout(t._t);t._t=setTimeout(function(){t.classList.remove('show')},2600);}
function view(v){document.getElementById('vDisk').classList.toggle('hidden',v!=='disk');document.getElementById('vWifi').classList.toggle('hidden',v!=='wifi');document.getElementById('tabDisk').classList.toggle('on',v==='disk');document.getElementById('tabWifi').classList.toggle('on',v==='wifi');}
function paint(s){
 var pill=document.getElementById('conntxt'),dot=document.getElementById('condot');
 if(s.mode==='wifi'){pill.textContent=s.ip||'gotek.local';dot.classList.remove('ap');}else{pill.textContent='ESP-NOW / AP';dot.classList.add('ap');}
 var f=document.getElementById('floppy');
 if(s.loaded){f.classList.remove('empty');document.getElementById('dname').textContent=s.name||'disk';var d=document.getElementById('dsub');d.textContent='Inserted &middot; '+fmt(s.size);d.classList.add('live');document.getElementById('ejectBtn').disabled=false;}
 else{f.classList.add('empty');document.getElementById('dname').textContent='-- no disk --';var d=document.getElementById('dsub');d.textContent='Load an ADF to insert it';d.classList.remove('live');document.getElementById('ejectBtn').disabled=true;}
}
function poll(){fetch('/status').then(function(r){return r.json()}).then(paint).catch(function(){});}
function picked(f){
 if(!f)return;
 if(!/\.(adf|adz|img)$/i.test(f.name)){toast('Not an ADF (.adf .adz .img)',1);return;}
 if(f.size>1048576){toast('HD image too big - DD only (~880 KB)',1);return;}
 var fd=new FormData();fd.append('f',f,f.name);
 var x=new XMLHttpRequest();x.open('POST','/upload');
 var prog=document.getElementById('prog'),bar=document.getElementById('bar');prog.style.display='block';bar.style.width='0';
 x.upload.onprogress=function(e){if(e.lengthComputable)bar.style.width=(e.loaded/e.total*100)+'%';};
 x.onload=function(){prog.style.display='none';if(x.status>=200&&x.status<300){toast('Inserted &#10003;');poll();}else{try{toast(JSON.parse(x.responseText).err||'Upload failed',1)}catch(_){toast('Upload failed',1)}}};
 x.onerror=function(){prog.style.display='none';toast('Upload failed',1);};
 x.send(fd);
}
function ejectDisk(){fetch('/eject',{method:'POST'}).then(function(r){return r.json()}).then(function(s){toast('Ejected');paint(s);});}
function joinWifi(){var s=document.getElementById('ssid').value.trim();if(!s){toast('enter a network',1);return;}var b=new URLSearchParams();b.append('ssid',s);b.append('pass',document.getElementById('pass').value);toast('Saving...');fetch('/savewifi',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b.toString()}).then(function(){document.body.innerHTML='<div style="max-width:460px;margin:60px auto;padding:24px;font-family:system-ui;color:#e9ecf5;text-align:center"><h2>Rebooting onto '+s+'</h2><p style="color:#8b93ad">Reconnect your device to your home Wi-Fi, then open <b>gotek.local</b> in a browser.</p></div>';});}
function toEspnow(){toast('Switching...');fetch('/espnow',{method:'POST'}).then(function(){document.body.innerHTML='<div style="max-width:460px;margin:60px auto;padding:24px;font-family:system-ui;color:#e9ecf5;text-align:center"><h2>Back to ESP-NOW / AP mode</h2><p style="color:#8b93ad">Rebooting. Reconnect to the <b>GotekOMEGA</b> Wi-Fi (password gotek1234) and open <b>192.168.4.1</b> to return here.</p></div>';});}
var drop=document.getElementById('drop');
['dragenter','dragover'].forEach(function(e){drop.addEventListener(e,function(ev){ev.preventDefault();drop.classList.add('hot');});});
['dragleave','drop'].forEach(function(e){drop.addEventListener(e,function(ev){ev.preventDefault();drop.classList.remove('hot');});});
drop.addEventListener('drop',function(ev){if(ev.dataTransfer.files[0])picked(ev.dataTransfer.files[0]);});
setInterval(poll,1500);poll();
</script></body></html>)HTML";

static void handleRoot(){ server.send_P(200, "text/html", PAGE_HTML); }

static void startWebServer(){
  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, [](){ server.send(200,"application/json", statusJson()); });
  server.on("/upload", HTTP_POST, handleUploadDone, handleUpload);
  server.on("/eject", HTTP_POST, handleEjectWeb);
  server.on("/savewifi", HTTP_POST, handleSaveWifi);
  server.on("/espnow", HTTP_POST, handleEspnowWeb);
  server.onNotFound([](){ server.sendHeader("Location","/"); server.send(302,"text/plain",""); });
  server.begin();
}

// ── base BOOT-button owner-lock gesture (unchanged) ─────────────────────────
#define BOOT_PAIR_MS   5000
#define BOOT_WIPE_MS   15000
#define ENROLL_WIN_MS  30000
static bool serviceBootButton(){
  uint32_t now = millis();
  static bool held_prev = false; static uint32_t held_t0 = 0; static uint8_t upCount = 8;
  bool raw = (digitalRead(BOOT_PIN) == LOW);
  if (raw) upCount = 0; else if (upCount < 250) upCount++;
  bool down = (upCount < 8); bool phase = (now / 180) & 1; static bool wipedHold = false;
  if (g_enroll_open && now > g_enroll_until) g_enroll_open = false;
  if (down) {
    if (!held_prev) { held_prev = true; held_t0 = now; wipedHold = false; }
    uint32_t held = now - held_t0;
    if (held >= BOOT_WIPE_MS && !wipedHold) { wipeOwners(); wipedHold = true; }
    if (wipedHold) setLeds(true,false); else if (held >= BOOT_PAIR_MS) setLeds(false,phase); else setLeds(phase,false);
    return true;
  }
  if (held_prev) {
    uint32_t held = now - held_t0; held_prev = false; setLeds(false,false);
    if (!wipedHold && held >= BOOT_PAIR_MS) { g_enroll_open = true; g_enroll_until = now + ENROLL_WIN_MS; }
    return true;
  }
  if (g_enroll_open) { setLeds(false, phase); return true; }
  return false;
}

// ============================================================================
// SETUP
// ============================================================================
static void startEspnowApMode(){
  // base radio: own AP on ch6 + ESP-NOW (so a GTi can drive it), TCP app server,
  // captive-portal DNS, and the web page served on 192.168.4.1.
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS, ESPNOW_CHANNEL);
  delay(300);
  _tcpServer.begin();
  dnsServer.start(53, "*", IPAddress(192,168,4,1)); g_dns_up = true;

  if (ESP_NOW.begin()) {
    ESP_NOW.onNewPeer(onNewPeer, nullptr);
    _bcastPeer = new XiaoPeer(ESP_NOW.BROADCAST_ADDR, ESPNOW_CHANNEL, WIFI_IF_STA, nullptr);
    if (!_bcastPeer->add_peer()) { delete _bcastPeer; _bcastPeer=nullptr; }
    if (_paired) {
      _wavePeer = new XiaoPeer(_wave_mac, ESPNOW_CHANNEL, WIFI_IF_STA, nullptr);
      if (!_wavePeer->add_peer()) { delete _wavePeer; _wavePeer=nullptr; }
    }
  }
  // Webby is unlocked by default: with no enrolled owner, hold the enrol window
  // open so a GTi can pair without the BOOT-button dance.
  if (_owner_count == 0) { g_enroll_open = true; g_enroll_until = millis() + 6UL*60UL*1000UL; }

  // Broadcast hello burst so a GTi can discover us (web stays responsive meanwhile).
  oledStatus("Gotek OMEGA " FW_VERSION, "AP: " AP_SSID, WiFi.softAPIP().toString(), "Broadcasting...");
  uint32_t t0 = millis();
  while (millis()-t0 < (uint32_t)(_paired ? 8000 : 3000)) {
    PktHello hello = {}; hello.type = PKT_PAIR_HELLO; WiFi.softAPmacAddress(hello.mac);
    strncpy(hello.ip, AP_IP, 15); hello.pad[0] = SAVE_PROTO_VER;
    if (_bcastPeer) _bcastPeer->send_pkt((uint8_t*)&hello, sizeof(hello));
    if (_wavePeer)  _wavePeer->send_pkt((uint8_t*)&hello, sizeof(hello));
    RxPkt pkt; while (xQueueReceive(_rxQueue, &pkt, 0) == pdTRUE) handleESPNOW(pkt.data, pkt.len);
    WiFiClient c = _tcpServer.accept(); if (c) handleTCPClient(c);
    server.handleClient(); dnsServer.processNextRequest();
    delay(120);
    if (_paired && _wavePeer) break;
  }
  setLeds(false, g_disk_loaded);
}

void setup() {
  Serial.begin(115200); delay(200);
  pinMode(LED_RED, OUTPUT); pinMode(LED_BLUE, OUTPUT); pinMode(BOOT_PIN, INPUT_PULLUP);
  digitalWrite(LED_RED, LOW); digitalWrite(LED_BLUE, LOW);
  _rxQueue = xQueueCreate(32, sizeof(RxPkt));

  // PSRAM ramdisk
  g_disk = (uint8_t*)ps_malloc((size_t)TOTAL_SECTORS*SECTOR_SIZE);
  if (!g_disk) g_disk = (uint8_t*)malloc((size_t)TOTAL_SECTORS*SECTOR_SIZE);
  if (!g_disk) { oledStatus("FATAL: no RAM","Set PSRAM=QSPI","in board menu",""); while(true){digitalWrite(LED_RED,HIGH);delay(200);digitalWrite(LED_RED,LOW);delay(200);} }
  build_volume("DISK.ADF", ADF_DEFAULT_SIZE);

  // USB MSC
  USB.onEvent(usbEventCb);
  MSC.vendorID("ESP32"); MSC.productID("GOTEK"); MSC.productRevision("1.0");
  MSC.onRead(onRead); MSC.onWrite(onWrite); MSC.mediaPresent(true);
  MSC.begin(TOTAL_SECTORS, SECTOR_SIZE); USB.begin();
  hardDetach();

  loadConfig();       // enrolled owners (used in ESPNOW mode)
  loadWifiCfg();      // WEBBY: home creds + mode

  // WEBBY: pick the radio mode.
  bool tryWifi = (g_modeStr == "WIFI" && g_ssid.length() > 0);
  g_webmode = 0;
  if (tryWifi) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(g_ssid.c_str(), g_pass.c_str());
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis()-t0 < 12000) { digitalWrite(LED_RED, ((millis()/200)&1)?HIGH:LOW); delay(50); }
    digitalWrite(LED_RED, LOW);
    if (WiFi.status() == WL_CONNECTED) {
      g_webmode = 1;
      if (MDNS.begin(MDNS_NAME)) MDNS.addService("http","tcp",80);
      _tcpServer.begin();        // app can reach us over the LAN too
      setLeds(false, true);      // blue = connected/ready
      Serial.printf("[WIFI] joined %s  IP %s  (gotek.local)\n", g_ssid.c_str(), WiFi.localIP().toString().c_str());
    }
  }
  if (g_webmode == 0) {
    startEspnowApMode();         // no creds / MODE=ESPNOW / STA join failed -> AP + ESP-NOW
  }

  startWebServer();
}

// ============================================================================
// LOOP
// ============================================================================
void loop() {
  server.handleClient();
  if (g_dns_up) dnsServer.processNextRequest();

  // ESP-NOW control queue (only meaningful in AP/ESP-NOW mode; harmless otherwise)
  RxPkt pkt; while (xQueueReceive(_rxQueue, &pkt, 0) == pdTRUE) handleESPNOW(pkt.data, pkt.len);

  // TCP app transfers (begun in both modes)
  WiFiClient client = _tcpServer.accept();
  if (client) handleTCPClient(client);

  bool dirtyWaiting = (g_dirty_count > 0 && g_last_write_ms && (millis()-g_last_write_ms) > SAVE_SETTLE_MS);
  if (dirtyWaiting && millis() > g_next_beacon_ms) { sendDirtyBeacon(); g_next_beacon_ms = millis()+SAVE_BEACON_MS; }
  if (millis() > g_next_status_ms) { sendStatusBeacon(); g_next_status_ms = millis()+STATUS_BEACON_MS; }

  if (!serviceBootButton()) {
    if (dirtyWaiting)          setLeds(true, true);
    else if (g_webmode==0 && !_paired) setLeds(false, (millis()/1000)&1);
  }
  delay(2);
}
