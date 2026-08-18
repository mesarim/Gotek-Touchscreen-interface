// Gotek_SuperMini.ino — Generic ESP32-S3 Super Mini ramdisk bridge for Gotek
// Ported from Gotek_XIAO.ino — same ESP-NOW/USB/TCP protocol (wire-compatible).
//
// Board          : ESP32S3 Dev Module  (generic Super Mini, ESP32-S3FH4R2)
// USB Mode       : USB-OTG (TinyUSB)
// USB CDC on Boot: *** DISABLED ***
// USB MSC on Boot: Disabled
// PSRAM          : *** QSPI PSRAM ***  (NOT OPI, NOT QIO — FH4R2 has 2MB quad PSRAM)
// Flash Size     : 4MB (32Mb)
// Flash Mode     : QIO 80MHz
// Partition      : Default 4MB with spiffs (1.2MB APP / 1.5MB SPIFFS)
// CPU            : 240MHz
//
// Status LEDs (internal/diagnostic, optional):
//   RED  on GP1 : slow blink = NOT paired; fast blink = FATAL (no RAM)
//   BLUE on GP2 : solid = disk loaded/presented; brief flash = transferring
//   (Wire LED + resistor from pin to GND. Ignore if not fitted.)

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

#define FW_VERSION     "v3.5.3-mini"   // 3.5.0: OWNER LOCK — dongle only obeys enrolled GTis; BOOT-hold to pair/wipe. 3.4.0 = TCP EJECT line.
#define ESPNOW_CHANNEL 6
#define LED_RED        1   // GP1 — status/attention
#define LED_BLUE       2   // GP2 — activity
#define BOOT_PIN       0   // GP0 = BOOT button — owner-lock gesture (hold 5-15s pair, 15s+ wipe)

// AP settings — XIAO hosts this for file transfer
#define AP_SSID     "GotekOMEGA"
#define AP_PASS     "gotek1234"
#define AP_IP       "192.168.4.1"
#define TCP_PORT    3333

// ESP-NOW packet types
#define PKT_PAIR_HELLO  0x05
#define PKT_PAIR_REPLY  0x14
#define PKT_DISK_EJECT  0x02
#define PKT_XIAO_READY  0x10
#define PKT_XIAO_DONE   0x12
#define PKT_XIAO_ERROR  0x13
#define PKT_XIAO_DIRTY  0x15   // v3.2.0: settled unsaved writes exist — GTi, come fetch
#define PKT_XIAO_STATUS 0x17   // v3.5.3: periodic load-state heartbeat {loaded,load_id,image_size}
#define PKT_UNPAIR      0x16   // v3.5.2: GTi -> dongle: forget me (drop the sender from the owner list)

// ── Save writeback (v3.2.0) ─────────────────────────────────────────────────
// The Gotek writes save-game sectors onto our RAM disk via USB MSC. We tick a
// dirty bitmap per written sector (we never parse or diff anything — doctrine),
// and once writes settle we beacon PKT_XIAO_DIRTY until the GTi pulls the dirty
// sectors over TCP (command escape below) and acks. Bits only clear on ack.
#define SAVE_PROTO_VER  1        // advertised in HELLO/REPLY pad[0]
#define SAVE_SETTLE_MS  3000     // quiet time after last write before beaconing
#define SAVE_BEACON_MS  10000    // beacon repeat until serviced
#define STATUS_BEACON_MS 2500    // v3.5.3: load-state heartbeat cadence
#define TCP_CMD_ESCAPE  0xFFFFFFFFUL  // size header value that means "command, not disk"
#define CMD_GET_SAVE    0x01
#define CMD_GET_STATUS  0x02
#define CMD_EJECT       0x03   // v3.4.0: app eject — REFUSES (reply 0x02) if unsaved writes pending
#define CMD_EJECT_FORCE 0x04   // v3.4.0: app eject, no questions — drops unsaved writes

#pragma pack(push,1)
struct PktHello  { uint8_t type; uint8_t mac[6]; char ip[16]; uint8_t pad[227]; };
struct PktSimple { uint8_t type; uint8_t pad[249]; };
struct PktDirty  { uint8_t type; uint32_t load_id; uint16_t dirty_count;
                   uint32_t image_size; uint32_t age_ms; uint8_t flags; uint8_t pad[234]; };
struct PktStatus { uint8_t type; uint8_t loaded; uint32_t load_id; uint32_t image_size; uint8_t pad[240]; };  // v3.5.3
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

// ── Save writeback state ────────────────────────────────────────────────────
#define IMG_MAX_SECTORS (TOTAL_SECTORS - DATA_LBA)      // 2037
static uint8_t  g_dirty[(IMG_MAX_SECTORS+7)/8];         // live map (filled by onWrite)
static uint8_t  g_snap [(IMG_MAX_SECTORS+7)/8];         // snapshot streamed to the GTi
static volatile uint16_t g_dirty_count   = 0;
static volatile uint32_t g_last_write_ms = 0;
static volatile uint32_t g_total_writes  = 0;           // includes FAT noise (INFO counter)
static uint32_t g_load_id    = 0;                        // bumped per received disk
static uint32_t g_image_size = ADF_DEFAULT_SIZE;         // size of the mounted image
static uint32_t g_next_beacon_ms = 0;
static uint32_t g_next_status_ms = 0;   // v3.5.3: load-state heartbeat timer
static inline bool dGet(const uint8_t*m,uint32_t i){return (m[i>>3]>>(i&7))&1;}
static inline void dSet(uint8_t*m,uint32_t i){m[i>>3]|=(uint8_t)(1u<<(i&7));}
static inline void dClr(uint8_t*m,uint32_t i){m[i>>3]&=(uint8_t)~(1u<<(i&7));}
static void dirtyReset(){memset(g_dirty,0,sizeof(g_dirty));g_dirty_count=0;g_last_write_ms=0;g_next_beacon_ms=0;}
// CRC32 (IEEE, bitwise — identical implementation on the GTi side)
static uint32_t crc32sw(uint32_t crc,const uint8_t*p,size_t n){
  crc=~crc;
  while(n--){crc^=*p++;for(int k=0;k<8;k++)crc=(crc>>1)^(0xEDB88320UL&(uint32_t)(-(int32_t)(crc&1)));}
  return ~crc;
}

// Two-LED diagnostic status (replaces OLED). Call sites kept as oledStatus()/oledProgress()
// so the rest of the firmware is unchanged — they just drive LEDs now.
static void setLeds(bool red, bool blue){ digitalWrite(LED_RED, red?HIGH:LOW); digitalWrite(LED_BLUE, blue?HIGH:LOW); }

// oledStatus is called with 4 status lines throughout. We can't show text on LEDs,
// so we infer state from the first line keyword and set the LEDs accordingly.
static void oledStatus(const String& l0, const String& l1,
                        const String& l2, const String& l3) {
  (void)l1;(void)l2;(void)l3;
  // Activity/“ready”: blue solid.  Pairing/idle: handled by loop blink.  Errors: red handled inline.
  if (l0.startsWith("**") || l0.startsWith("Receiving") || l0.startsWith("Gotek")) {
    // paired/ready or transferring — show blue, red off
    setLeds(false,true);
  } else if (l0.startsWith("Not paired")) {
    setLeds(true,false);  // red on = attention/not paired
  }
}
static void oledProgress(uint32_t done, uint32_t total) {
  // Blink blue to show transfer activity
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
  // v3.2.0: tick the dirty scorecard for every image sector this write touches.
  // Writes below DATA_LBA are FAT/dir housekeeping — counted but never mapped.
  // (assignment form, not ++ — C++20 deprecates ++ on volatile)
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
static void hardDetach() {
  MSC.mediaPresent(false); delay(100); tud_disconnect(); delay(500); g_usb_online=false;
}
static void hardAttach() {
  char rev[8]; snprintf(rev,sizeof(rev),"%lu",(unsigned long)g_rev_counter++);
  MSC.productRevision(rev); MSC.mediaPresent(true); delay(50); tud_connect(); delay(200);
  g_usb_online=true;
}

// FAT12
static inline void wr16(uint8_t*p,int o,uint16_t v){p[o]=(uint8_t)v;p[o+1]=(uint8_t)(v>>8);}
static inline void wr32(uint8_t*p,int o,uint32_t v){p[o]=(uint8_t)v;p[o+1]=(uint8_t)(v>>8);p[o+2]=(uint8_t)(v>>16);p[o+3]=(uint8_t)(v>>24);}
static void fat12_set(uint8_t*fat,uint16_t cl,uint16_t v){
  uint32_t i=(cl*3)/2;
  if((cl&1)==0){fat[i]=(uint8_t)(v&0xFF);fat[i+1]=(uint8_t)((fat[i+1]&0xF0)|((v>>8)&0x0F));}
  else{fat[i]=(uint8_t)((fat[i]&0x0F)|((v<<4)&0xF0));fat[i+1]=(uint8_t)((v>>4)&0xFF);}
}
static void build_volume(const char* outName, uint32_t fsz) {
  if (fsz>MAX_FILE_BYTES) fsz=MAX_FILE_BYTES;
  memset(g_disk,0,TOTAL_SECTORS*SECTOR_SIZE);
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
  for(uint32_t i=0;i<need;i++){
    uint16_t c=(uint16_t)(2+i); fat12_set(fat,c,(i==(need-1))?0x0FFF:(c+1));
  }
  uint8_t* root=fat+SECTORS_PER_FAT*SECTOR_SIZE;
  char n[8],e[3]; memset(n,' ',8); memset(e,' ',3);
  const char* dot=strrchr(outName,'.');
  size_t nl=dot?(size_t)(dot-outName):strlen(outName);
  for(size_t i=0;i<nl&&i<8;i++) n[i]=toupper(outName[i]);
  if(dot) for(size_t i=0;i<3&&dot[1+i];i++) e[i]=toupper(dot[1+i]);
  memcpy(&root[0],n,8);memcpy(&root[8],e,3);
  root[11]=0x20;wr16(root,26,2);wr32(root,28,fsz);
}

// MAC helper
static String macToStr(const uint8_t* mac) {
  char buf[18];
  snprintf(buf,sizeof(buf),"%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
  return String(buf);
}

// State
static uint8_t _wave_mac[6] = {0};   // currently-active owner (last GTi we handshook)
static bool    _paired       = false;

// ── Owner lock (v3.5.0) — the dongle only obeys enrolled GTis ────────────────
#define MAX_OWNERS 4
static uint8_t  _owners[MAX_OWNERS][6] = {{0}};
static uint8_t  _owner_count   = 0;
static bool     g_enroll_open  = false;   // a BOOT-hold opened a pairing window
static uint32_t g_enroll_until = 0;       // ms deadline for that window

// ESP-NOW receive queue (FreeRTOS)
#define RX_PKT_SIZE 250
struct RxPkt { uint8_t data[RX_PKT_SIZE]; int len; };
static QueueHandle_t _rxQueue = nullptr;

static void queuePacket(const uint8_t* data, int len) {
  if (!_rxQueue) return;
  RxPkt pkt;
  int n = min(len, RX_PKT_SIZE);
  memcpy(pkt.data, data, n); pkt.len = n;
  xQueueSendFromISR(_rxQueue, &pkt, nullptr);
}

// Forward declare
static void handleESPNOW(const uint8_t* data, int len);

// ESP-NOW peer class
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

// ── Owner-lock helpers (v3.5.0) ─────────────────────────────────────────────
static bool isOwner(const uint8_t* mac){
  for(int i=0;i<_owner_count;i++) if(memcmp(_owners[i],mac,6)==0) return true;
  return false;
}
static bool addOwner(const uint8_t* mac){
  if(isOwner(mac)) return true;
  if(_owner_count>=MAX_OWNERS) return false;
  memcpy(_owners[_owner_count],mac,6); _owner_count++;
  return true;
}
static bool removeOwner(const uint8_t* mac){
  for(int i=0;i<_owner_count;i++) if(memcmp(_owners[i],mac,6)==0){
    for(int j=i;j<_owner_count-1;j++) memcpy(_owners[j],_owners[j+1],6);
    _owner_count--; return true;
  }
  return false;
}
static void saveOwners(){
  if(!LittleFS.begin(true)) return;
  File f=LittleFS.open("/XIAO_CONFIG.TXT","w");
  if(!f) return;
  for(int i=0;i<_owner_count;i++) f.printf("WAVE_MAC=%s\n", macToStr(_owners[i]).c_str());
  f.close();
}
static void wipeOwners(){
  _owner_count=0; _paired=false; memset(_wave_mac,0,6);
  if(_wavePeer){ delete _wavePeer; _wavePeer=nullptr; }
  if(LittleFS.begin(true)) LittleFS.remove("/XIAO_CONFIG.TXT");
  oledStatus("Gotek OMEGA " FW_VERSION,"** WIPED **","All owners cleared","Hold BOOT to pair");
}

// Handle ESP-NOW control packets
static void handleESPNOW(const uint8_t* data, int len) {
  if (len < 1) return;
  uint8_t type = data[0];

  if (type == PKT_PAIR_HELLO) {
    const PktHello* p = (const PktHello*)data;
    // Owner lock (v3.5.0): only obey enrolled GTis. An unknown GTi is enrolled
    // ONLY while a BOOT-hold pairing window is open — otherwise ignored silently.
    bool known = isOwner(p->mac);
    if (!known) {
      if (!g_enroll_open) return;                 // locked — stranger ignored
      if (!addOwner(p->mac)) {                     // allowlist full
        oledStatus("Gotek OMEGA " FW_VERSION,"OWNERS FULL","Hold BOOT 15s","to wipe & re-pair");
        return;
      }
      saveOwners();
      g_enroll_open = false;                        // this hold enrolled one GTi
    }

    memcpy(_wave_mac, p->mac, 6);
    _paired = true;

    if (_wavePeer) { delete _wavePeer; _wavePeer = nullptr; }
    _wavePeer = new XiaoPeer(_wave_mac, ESPNOW_CHANNEL, WIFI_IF_STA, nullptr);
    if (!_wavePeer->add_peer()) { delete _wavePeer; _wavePeer = nullptr; }

    // Reply with our MAC and AP IP (+ save-protocol capability in the pad —
    // old GTi firmware ignores pad bytes, new firmware reads pad[0])
    PktHello reply = {};
    reply.type = PKT_PAIR_REPLY;
    WiFi.softAPmacAddress(reply.mac);  // use AP MAC
    strncpy(reply.ip, AP_IP, 15);
    reply.pad[0] = SAVE_PROTO_VER;
    XiaoPeer* dst = _wavePeer ? _wavePeer : _bcastPeer;
    if (dst) dst->send_pkt((uint8_t*)&reply, sizeof(reply));

    oledStatus("Gotek OMEGA " FW_VERSION, known?"Reconnected":"Owner added",
               macToStr(_wave_mac), String(_owner_count)+" owner(s)");
    return;
  }

  if (type == PKT_UNPAIR) {                        // v3.5.2: an owner GTi asked to be forgotten
    const PktHello* p = (const PktHello*)data;
    if (removeOwner(p->mac)) {                     // only ever removes a MAC that was enrolled
      saveOwners();
      if (memcmp(_wave_mac, p->mac, 6)==0) {        // that was the active link — re-point or clear
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
    // A new-firmware GTi drains saves BEFORE ejecting, so any bits left here are
    // either already fetched or from an old GTi that can't fetch — drop them.
    dirtyReset();
    digitalWrite(LED_BLUE, LOW);
    oledStatus("Gotek OMEGA " FW_VERSION, "Ejected", "", "Ready");
    return;
  }
}

static void onNewPeer(const esp_now_recv_info_t* info, const uint8_t* data, int len, void* arg) {
  queuePacket(data, len);
}

// Load saved config
static void loadConfig() {
  if (!LittleFS.begin(true)) return;
  File f = LittleFS.open("/XIAO_CONFIG.TXT", "r");
  if (!f) return;
  _owner_count = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n'); line.trim();
    if (line.startsWith("WAVE_MAC=")) {          // one line per enrolled owner
      uint8_t m[6]={0};
      String mac = line.substring(9);
      if (sscanf(mac.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                 &m[0],&m[1],&m[2],&m[3],&m[4],&m[5]) == 6) {
        bool isZero=true;
        for(int i=0;i<6;i++) if(m[i]) { isZero=false; break; }
        if (!isZero) addOwner(m);
      }
    }
  }
  f.close();
  _paired = (_owner_count > 0);
  if (_paired) memcpy(_wave_mac, _owners[0], 6);   // default active = first owner
}

// WiFi AP + TCP server
static WiFiServer _tcpServer(TCP_PORT);
// (_apRunning removed in 3.3.0 with startAP() — the AP is unconditionally started in setup())

// (startAP() removed in 3.3.0 — dead code since the AP became always-on in setup())

// ── Save writeback: TCP command handlers (v3.2.0) ───────────────────────────
static inline void wrLE32(uint8_t*p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static inline void wrLE16(uint8_t*p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}

// EJECT (v3.4.0): the ESP-NOW eject's TCP twin, for the GTi app (which can't speak
// ESP-NOW). The GTi screen drains saves BEFORE ejecting; the app can't yet — so the
// plain eject REFUSES when unsaved writes are pending, and the app must confirm with
// EJECT_FORCE. Replies: 0x01 = ejected ("nothing loaded" is also success),
// 0x02 = refused, unsaved writes pending (0x03 only). Old dongles reply 0x00 (unknown).
static void doEject(WiFiClient& client, bool force){
  if (!force && g_dirty_count > 0) {
    Serial.printf("[TCP] Eject refused — %u dirty sectors pending\n",(unsigned)g_dirty_count);
    client.write((uint8_t)0x02); client.flush();
    return;
  }
  if (g_disk_loaded) { hardDetach(); g_disk_loaded = false; }
  // Forced (or clean) eject: same policy as an old-GTi ESP-NOW eject — drop the bits.
  dirtyReset();
  digitalWrite(LED_BLUE, LOW);
  oledStatus("Gotek OMEGA " FW_VERSION, "Ejected (app)", "", "Ready");
  client.write((uint8_t)0x01); client.flush();
}
// GET_STATUS: 'S','T',ver | load_id u32 | dirty_count u16 | age_ms u32 | total_writes u32 | image_size u32
static void doGetStatus(WiFiClient& client){
  uint8_t r[21]; r[0]='S'; r[1]='T'; r[2]=SAVE_PROTO_VER;
  wrLE32(r+3,g_load_id); wrLE16(r+7,g_dirty_count);
  wrLE32(r+9,g_last_write_ms?(millis()-g_last_write_ms):0xFFFFFFFFUL);
  wrLE32(r+13,g_total_writes); wrLE32(r+17,g_image_size);
  client.write(r,21); client.flush();
}

// GET_SAVE: stream snapshot bitmap + dirty sectors + CRC32; clear bits only on ack.
// Response: 'S','V','1' | flags u8 | load_id u32 | image_size u32 | mapLen u16 |
//           map[mapLen] | <dirty sectors ascending> | crc32 u32 (over map+sectors)
static void doGetSave(WiFiClient& client){
  uint32_t imgSecs=(g_image_size+SECTOR_SIZE-1)/SECTOR_SIZE;
  if(imgSecs>IMG_MAX_SECTORS)imgSecs=IMG_MAX_SECTORS;
  uint16_t mapLen=(uint16_t)((imgSecs+7)/8);
  memcpy(g_snap,g_dirty,mapLen);           // snapshot: writes during transfer stay dirty in the live map
  uint8_t hdr[14]; hdr[0]='S';hdr[1]='V';hdr[2]='1';hdr[3]=0;
  wrLE32(hdr+4,g_load_id); wrLE32(hdr+8,g_image_size); wrLE16(hdr+12,mapLen);
  client.write(hdr,14);
  uint32_t crc=crc32sw(0,g_snap,mapLen);
  client.write(g_snap,mapLen);
  uint32_t sent=0;
  for(uint32_t i=0;i<imgSecs;i++){
    if(!dGet(g_snap,i))continue;
    uint8_t* sec=g_disk+(DATA_LBA+i)*SECTOR_SIZE;
    client.write(sec,SECTOR_SIZE);
    crc=crc32sw(crc,sec,SECTOR_SIZE);
    sent++; oledProgress(sent,g_dirty_count);
  }
  uint8_t cb[4]; wrLE32(cb,crc); client.write(cb,4); client.flush();
  Serial.printf("[SAVE] Sent %lu dirty sectors (load %lu)\n",(unsigned long)sent,(unsigned long)g_load_id);
  // Await GTi ack: 0x01 = persisted to SD → clear the snapshot's bits from the live map
  uint32_t t0=millis(); while(!client.available()&&millis()-t0<10000)delay(5);
  bool ok=(client.available()&&client.read()==0x01);
  if(ok){
    for(uint32_t i=0;i<imgSecs;i++)
      if(dGet(g_snap,i)&&dGet(g_dirty,i)){dClr(g_dirty,i);if(g_dirty_count)g_dirty_count=g_dirty_count-1;}
    g_next_beacon_ms=0;
    Serial.println("[SAVE] GTi persisted — bits cleared");
    if(g_dirty_count==0) setLeds(false,g_disk_loaded);
  } else {
    Serial.println("[SAVE] No ack — keeping dirty bits for retry");
  }
}

// Beacon: settled unsaved writes exist — repeated every SAVE_BEACON_MS until fetched.
// Doubles as a keepalive so the GTi's DONGLE:ONLINE indicator stays fresh.
static void sendDirtyBeacon(){
  PktDirty pkt={}; pkt.type=PKT_XIAO_DIRTY;
  pkt.load_id=g_load_id; pkt.dirty_count=g_dirty_count;
  pkt.image_size=g_image_size;
  pkt.age_ms=g_last_write_ms?(millis()-g_last_write_ms):0;
  pkt.flags=0;
  XiaoPeer* dst=_wavePeer?_wavePeer:_bcastPeer;
  if(dst)dst->send_pkt((uint8_t*)&pkt,sizeof(pkt));
}

// v3.5.3: periodic load-state heartbeat to the paired GTi so its disk indicator
// reflects reality (loaded / empty / offline). Sent only to the active GTi.
static void sendStatusBeacon(){
  if(!_wavePeer)return;
  PktStatus pkt={}; pkt.type=PKT_XIAO_STATUS;
  pkt.loaded=g_disk_loaded?1:0; pkt.load_id=g_load_id; pkt.image_size=g_image_size;
  _wavePeer->send_pkt((uint8_t*)&pkt,sizeof(pkt));
}

// Handle incoming TCP disk transfer
static void handleTCPClient(WiFiClient& client) {
  Serial.printf("[TCP] Client connected from %s\n", client.remoteIP().toString().c_str());
  oledStatus("Receiving...", "TCP connected", "", "");

  // Read 4-byte size header
  uint32_t t0 = millis();
  while (client.available() < 4 && millis()-t0 < 5000) delay(1);
  if (client.available() < 4) {
    Serial.println("[TCP] No size header");
    client.write((uint8_t)0x00);
    return;
  }
  uint8_t hdr[4];
  client.read(hdr, 4);
  uint32_t size = ((uint32_t)hdr[0]<<24)|((uint32_t)hdr[1]<<16)|
                  ((uint32_t)hdr[2]<<8)|(uint32_t)hdr[3];

  // v3.2.0 command escape: a real disk size is always <= MAX_FILE_BYTES, so
  // 0xFFFFFFFF unmistakably means "command follows", never a disk.
  if (size == TCP_CMD_ESCAPE) {
    t0 = millis();
    while (client.available() < 1 && millis()-t0 < 3000) delay(1);
    if (!client.available()) { client.write((uint8_t)0x00); return; }
    uint8_t cmd = client.read();
    Serial.printf("[TCP] Command 0x%02X\n", cmd);
    if      (cmd == CMD_GET_SAVE)    doGetSave(client);
    else if (cmd == CMD_GET_STATUS)  doGetStatus(client);
    else if (cmd == CMD_EJECT)       doEject(client,false);
    else if (cmd == CMD_EJECT_FORCE) doEject(client,true);
    else client.write((uint8_t)0x00);
    return;
  }

  Serial.printf("[TCP] Expecting %lu bytes\n", (unsigned long)size);

  if (size == 0 || size > MAX_FILE_BYTES) {
    Serial.println("[TCP] Invalid size");
    client.write((uint8_t)0x00);
    return;
  }

  // Determine filename from size
  const char* outName = (size == 901120) ? "DISK.ADF" : 
                        (size <= MAX_FILE_BYTES) ? "DISK.ADF" : "DISK.DSK";
  build_volume(outName, size);

  // Receive data directly into ramdisk
  uint8_t* dst = g_disk + DATA_LBA * SECTOR_SIZE;
  uint32_t received = 0;
  const size_t BUF = 4096;
  uint8_t* buf = (uint8_t*)malloc(BUF);
  if (!buf) { client.write((uint8_t)0x00); return; }

  t0 = millis();
  while (received < size && millis()-t0 < 30000) {
    if (!client.connected()) break;
    int avail = client.available();
    if (avail <= 0) { delay(1); continue; }
    size_t toRead = min((size_t)avail, min(BUF, (size_t)(size-received)));
    int rd = client.read(buf, toRead);
    if (rd > 0) {
      memcpy(dst + received, buf, rd);
      received += rd;
      oledProgress(received, size);
    }
  }
  free(buf);

  Serial.printf("[TCP] Received %lu / %lu bytes\n", (unsigned long)received, (unsigned long)size);

  if (received == size) {
    // New disk = new save identity: bump load_id, wipe the old scorecard.
    g_load_id++;
    g_image_size = size;
    dirtyReset();

    // Send OK response + load_id (old GTi reads only the first byte — compatible;
    // new GTi reads 4 more bytes to learn which load this ack belongs to)
    uint8_t ack[5]; ack[0]=0x01; wrLE32(ack+1,g_load_id);
    client.write(ack,5);
    client.flush();
    delay(100);
    client.stop();

    // Attach to Gotek
    if (g_disk_loaded) hardDetach();
    hardAttach();
    g_disk_loaded = true;
    g_next_status_ms = 0;                 // v3.5.3: beacon the new loaded state immediately
    digitalWrite(LED_BLUE, HIGH);

    String dispName = (size == 901120) ? "ADF 880KB" : String(size/1024) + "KB";
    oledStatus("LOADED!", dispName, "USB: attached", "Gotek ready");
    Serial.println("[TCP] Disk loaded to Gotek OK");

    // Signal Waveshare via ESP-NOW
    sendSimple(PKT_XIAO_DONE);
  } else {
    client.write((uint8_t)0x00);
    client.flush();
    delay(100);
    client.stop();
    String rxStr = "rx:" + String(received/1024) + "k/" + String(size/1024) + "k";
    oledStatus("TRANSFER ERROR", rxStr, "tap INSERT again", "");
    Serial.printf("[TCP] Transfer incomplete: %lu/%lu\n", (unsigned long)received, (unsigned long)size);
    sendSimple(PKT_XIAO_ERROR);
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  pinMode(BOOT_PIN, INPUT_PULLUP);   // owner-lock gesture button
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_BLUE, LOW);

  _rxQueue = xQueueCreate(32, sizeof(RxPkt));

  // PSRAM ramdisk
  g_disk = (uint8_t*)ps_malloc((size_t)TOTAL_SECTORS*SECTOR_SIZE);
  if (!g_disk) g_disk = (uint8_t*)malloc((size_t)TOTAL_SECTORS*SECTOR_SIZE);
  if (!g_disk) {
    oledStatus("FATAL: no RAM","Set PSRAM=QSPI","in board menu","");
    while(true){digitalWrite(LED_RED,HIGH);delay(200);digitalWrite(LED_RED,LOW);delay(200);}
  }
  build_volume("DISK.ADF", ADF_DEFAULT_SIZE);

  // USB MSC
  USB.onEvent(usbEventCb);
  MSC.vendorID("ESP32"); MSC.productID("GOTEK"); MSC.productRevision("1.0");
  MSC.onRead(onRead); MSC.onWrite(onWrite); MSC.mediaPresent(true);
  MSC.begin(TOTAL_SECTORS, SECTOR_SIZE); USB.begin();
  hardDetach();

  loadConfig();

  // Start WiFi AP immediately — always on
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS, ESPNOW_CHANNEL);
  delay(300);
  _tcpServer.begin();
  Serial.printf("[AP] Started on channel %d, IP: %s\n", ESPNOW_CHANNEL, AP_IP);

  // Start ESP-NOW (compatible with AP_STA mode)
  if (!ESP_NOW.begin()) {
    oledStatus("ESP-NOW FAILED","","",""); return;
  }
  ESP_NOW.onNewPeer(onNewPeer, nullptr);

  _bcastPeer = new XiaoPeer(ESP_NOW.BROADCAST_ADDR, ESPNOW_CHANNEL, WIFI_IF_STA, nullptr);
  if (!_bcastPeer->add_peer()) { delete _bcastPeer; _bcastPeer=nullptr; }

  if (_paired) {
    _wavePeer = new XiaoPeer(_wave_mac, ESPNOW_CHANNEL, WIFI_IF_STA, nullptr);
    if (!_wavePeer->add_peer()) { delete _wavePeer; _wavePeer=nullptr; }
  }

  // Broadcast hello for 30s so Waveshare can find us
  oledStatus("Gotek OMEGA " FW_VERSION, "AP: " AP_SSID, WiFi.softAPIP().toString(), "Broadcasting...");

  uint32_t t0 = millis();
  int dots = 0;
  // Owners reconnect during this window; an unowned dongle bails out fast so the
  // BOOT-hold pairing gesture (serviced in loop) becomes responsive right away.
  while (millis()-t0 < (uint32_t)(_paired ? 30000 : 3000)) {
    PktHello hello = {};
    hello.type = PKT_PAIR_HELLO;
    WiFi.softAPmacAddress(hello.mac);
    strncpy(hello.ip, AP_IP, 15);
    hello.pad[0] = SAVE_PROTO_VER;
    if (_bcastPeer) _bcastPeer->send_pkt((uint8_t*)&hello, sizeof(hello));
    if (_wavePeer)  _wavePeer->send_pkt((uint8_t*)&hello, sizeof(hello));
    dots++;

    // Drain ESP-NOW queue
    RxPkt pkt;
    while (xQueueReceive(_rxQueue, &pkt, 0) == pdTRUE)
      handleESPNOW(pkt.data, pkt.len);

    // Check for TCP clients while pairing
    WiFiClient client = _tcpServer.accept();
    if (client) handleTCPClient(client);

    if (dots % 5 == 0)
      oledStatus("Gotek OMEGA " FW_VERSION,
                 "AP: " AP_SSID,
                 WiFi.softAPIP().toString(),
                 "ping " + String(dots));
    delay(500);

    if (_paired && _wavePeer) {
      PktHello confirm = {};
      confirm.type = PKT_PAIR_REPLY;
      WiFi.softAPmacAddress(confirm.mac);
      strncpy(confirm.ip, AP_IP, 15);
      confirm.pad[0] = SAVE_PROTO_VER;
      _wavePeer->send_pkt((uint8_t*)&confirm, sizeof(confirm));
      break;
    }
  }

  if (_paired) {
    oledStatus("** PAIRED **", macToStr(_wave_mac), "AP: " AP_SSID, "Ready for disk");
    digitalWrite(LED_RED, LOW);
    digitalWrite(LED_BLUE, HIGH);
  } else {
    oledStatus("Not paired", "Hold BOOT ~5s", "until BLUE blinks,", "then PAIR on GTi");
  }
}

// ── BOOT-button owner-lock gesture (v3.5.0) ─────────────────────────────────
// Hold BOOT, commit on release. The LEDs show what's armed:
//   0-5s   RED blink  -> release = cancel (accidental-brush guard)
//   5-15s  BLUE blink -> release = open pairing window (enroll the next GTi)
//   15s+   RED solid  -> release = WIPE all owners
// Abort an armed action by unplugging or resetting instead of releasing.
// Returns true while it owns the LEDs (button held, or a pairing window is open).
#define BOOT_PAIR_MS   5000
#define BOOT_WIPE_MS   15000
#define ENROLL_WIN_MS  30000
static bool serviceBootButton(){
  uint32_t now = millis();
  static bool     held_prev = false;
  static uint32_t held_t0   = 0;
  static uint8_t  upCount   = 8;     // consecutive "released" reads (debounce); starts released
  bool raw   = (digitalRead(BOOT_PIN) == LOW);
  // Debounce: a real release needs ~40ms of continuous HIGH. A brief contact blip
  // mid-hold no longer commits early — that blip past 5s was committing PAIR and
  // resetting the timer, which made a clean 15s WIPE hold nearly impossible.
  if (raw) upCount = 0; else if (upCount < 250) upCount++;
  bool down  = (upCount < 8);        // stays down through brief (<~40ms) contact blips
  bool phase = (now / 180) & 1;                 // blink phase
  static bool wipedHold = false;     // wipe already fired during this hold

  if (g_enroll_open && now > g_enroll_until) g_enroll_open = false;   // window timed out

  if (down) {
    if (!held_prev) { held_prev = true; held_t0 = now; wipedHold = false; }
    uint32_t held = now - held_t0;
    // This board has no screen and maybe no LEDs, so we can't ask you to judge a
    // release moment. WIPE auto-fires at 15s WHILE STILL HELD: just hold BOOT ~15s
    // and it wipes (verify from the GTi — the dongle drops its owner). PAIR still
    // commits on release in the 5-15s window.
    if (held >= BOOT_WIPE_MS && !wipedHold) { wipeOwners(); wipedHold = true; }
    if      (wipedHold)            setLeds(true,  false);   // solid red = wiped (let go to finish)
    else if (held >= BOOT_PAIR_MS) setLeds(false, phase);   // blue blink = release now to PAIR
    else                          setLeds(phase, false);    // red blink  = holding
    return true;
  }

  if (held_prev) {                               // release edge
    uint32_t held = now - held_t0;
    held_prev = false;
    setLeds(false, false);
    if (!wipedHold && held >= BOOT_PAIR_MS) {    // 5-15s release = open the pairing window
      g_enroll_open = true; g_enroll_until = now + ENROLL_WIN_MS;
    }
    return true;                                 // (<5s, and post-wipe releases, are no-ops)
  }

  if (g_enroll_open) { setLeds(false, phase); return true; }   // BLUE blink = pair window open
  return false;                                                 // idle — let loop drive LEDs
}

void loop() {
  // Drain ESP-NOW control queue
  RxPkt pkt;
  while (xQueueReceive(_rxQueue, &pkt, 0) == pdTRUE)
    handleESPNOW(pkt.data, pkt.len);

  // Handle TCP disk transfers
  WiFiClient client = _tcpServer.accept();
  if (client) handleTCPClient(client);

  // v3.2.0: unsaved writes settled? Raise a hand until the GTi collects them.
  bool dirtyWaiting = (g_dirty_count > 0 && g_last_write_ms &&
                       (millis() - g_last_write_ms) > SAVE_SETTLE_MS);
  if (dirtyWaiting && millis() > g_next_beacon_ms) {
    sendDirtyBeacon();
    g_next_beacon_ms = millis() + SAVE_BEACON_MS;
  }
  // v3.5.3: steady load-state heartbeat (independent of dirty writes)
  if (millis() > g_next_status_ms) {
    sendStatusBeacon();
    g_next_status_ms = millis() + STATUS_BEACON_MS;
  }

  // The BOOT owner-lock gesture owns the LEDs while held or while a pairing
  // window is open; otherwise fall back to status indication.
  if (!serviceBootButton()) {
    if (dirtyWaiting)   setLeds(true, true);                  // red+blue = unsaved data waiting
    else if (!_paired)  setLeds(false, (millis()/1000)&1);    // slow blue blink = unowned ("pair me")
  }

  delay(5);
}
