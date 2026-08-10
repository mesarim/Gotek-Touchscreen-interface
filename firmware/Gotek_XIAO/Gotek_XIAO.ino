// Gotek_XIAO.ino — Seeed XIAO ESP32-S3 ramdisk bridge for Gotek
// v3.5.0-xiao: brought up to the current save-era protocol so it pairs with the
// JC again. Wire-compatible with the Super Mini v3.4.0 — SAME ESP-NOW/USB/TCP
// protocol, byte-for-byte; only the board header + version differ.
// DOUBLE-DENSITY ONLY — HD (1.76MB) is intentionally NOT supported on the dongle.
//
// Board          : XIAO_ESP32S3
// USB Mode       : USB-OTG (TinyUSB)
// USB CDC on Boot: *** DISABLED ***
// USB MSC on Boot: Disabled
// PSRAM          : *** OPI PSRAM ***  (XIAO ESP32-S3 = ESP32-S3R8, 8MB octal PSRAM)
// Flash Size     : 8MB
// Partition      : Default 4MB with spiffs (or an 8MB scheme) — needs ~1.2MB app
// CPU            : 240MHz
// ANTENNA        : *** PLUG IT IN ***  (XIAO uses an external u.FL antenna)
//
// Status LEDs (internal/diagnostic, optional):
//   RED  on GP1 : slow blink = NOT paired; fast blink = FATAL (no RAM)
//   BLUE on GP2 : solid = disk loaded/presented; brief flash = transferring
//   (Wire LED + resistor from pin to GND, or repoint LED_RED/LED_BLUE to the
//    onboard LED on GP21. Ignore if not fitted — the dongle works without them.)

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

#define FW_VERSION     "v3.6.0-xiao"   // v3.6.0: HD-capable — 2MB ramdisk (2-sector clusters, like the JC) + pad[1]=1 HD marker so the GTi may fling 1.76MB. Wire-compatible with the Super Mini (which stays DD).
#define ESPNOW_CHANNEL 6
#define LED_RED        1   // GP1 — status/attention
#define LED_BLUE       2   // GP2 — activity

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

// ── Save writeback (v3.2.0) ─────────────────────────────────────────────────
// The Gotek writes save-game sectors onto our RAM disk via USB MSC. We tick a
// dirty bitmap per written sector (we never parse or diff anything — doctrine),
// and once writes settle we beacon PKT_XIAO_DIRTY until the GTi pulls the dirty
// sectors over TCP (command escape below) and acks. Bits only clear on ack.
#define SAVE_PROTO_VER  1        // advertised in HELLO/REPLY pad[0]
#define SAVE_SETTLE_MS  3000     // quiet time after last write before beaconing
#define SAVE_BEACON_MS  10000    // beacon repeat until serviced
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
#pragma pack(pop)

// FAT12 geometry
#define SECTOR_SIZE      512
#define TOTAL_SECTORS    4096          // v3.6.0: 2MB RAM disk so HD (1.76MB) images fit
#define RESERVED_SECTORS 1
#define SECTORS_PER_FAT  6
#define SECTORS_PER_CLUSTER 2          // v3.6.0: 1KB clusters -> ~2042 clusters, stays FAT12 (limit 4084), keeps DATA_LBA=11
#define NUM_FATS         1
#define ROOT_ENTRIES     64
#define ROOT_DIR_SECTORS 4
#define DATA_LBA         11
#define MAX_FILE_BYTES   ((uint32_t)(TOTAL_SECTORS-DATA_LBA)*SECTOR_SIZE)
#define ADF_DEFAULT_SIZE 901120

static uint8_t* g_disk = nullptr;

// ── Save writeback state ────────────────────────────────────────────────────
#define IMG_MAX_SECTORS (TOTAL_SECTORS - DATA_LBA)      // 4085 (v3.6.0: 2MB disk)
static uint8_t  g_dirty[(IMG_MAX_SECTORS+7)/8];         // live map (filled by onWrite)
static uint8_t  g_snap [(IMG_MAX_SECTORS+7)/8];         // snapshot streamed to the GTi
static volatile uint16_t g_dirty_count   = 0;
static volatile uint32_t g_last_write_ms = 0;
static volatile uint32_t g_total_writes  = 0;           // includes FAT noise (INFO counter)
static uint32_t g_load_id    = 0;                        // bumped per received disk
static uint32_t g_image_size = ADF_DEFAULT_SIZE;         // size of the mounted image
static uint32_t g_next_beacon_ms = 0;
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
  wr16(bs,11,SECTOR_SIZE);bs[13]=SECTORS_PER_CLUSTER;wr16(bs,14,RESERVED_SECTORS);bs[16]=NUM_FATS;
  wr16(bs,17,ROOT_ENTRIES);wr16(bs,19,TOTAL_SECTORS);bs[21]=0xF8;
  wr16(bs,22,SECTORS_PER_FAT);wr16(bs,24,32);wr16(bs,26,64);
  bs[36]=0x80;bs[38]=0x29;wr32(bs,39,0x12345678);
  memcpy(&bs[43],"ESP32MSC   ",11);memcpy(&bs[54],"FAT12   ",8);
  bs[510]=0x55;bs[511]=0xAA;
  uint8_t* fat=g_disk+RESERVED_SECTORS*SECTOR_SIZE;
  fat[0]=0xF8;fat[1]=0xFF;fat[2]=0xFF;
  uint32_t clb=(uint32_t)SECTORS_PER_CLUSTER*512; uint32_t need=(fsz+clb-1)/clb;
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
static uint8_t _wave_mac[6] = {0};
static bool    _paired       = false;

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

// Handle ESP-NOW control packets
static void handleESPNOW(const uint8_t* data, int len) {
  if (len < 1) return;
  uint8_t type = data[0];

  if (type == PKT_PAIR_HELLO) {
    const PktHello* p = (const PktHello*)data;
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
    reply.pad[1] = 1;                  // v3.6.0: 1 = HD-capable (2MB ramdisk) — GTi may fling 1.76MB HD
    XiaoPeer* dst = _wavePeer ? _wavePeer : _bcastPeer;
    if (dst) dst->send_pkt((uint8_t*)&reply, sizeof(reply));

    // Save config
    if (LittleFS.begin(true)) {
      File f = LittleFS.open("/XIAO_CONFIG.TXT", "w");
      if (f) { f.printf("WAVE_MAC=%s\n", macToStr(_wave_mac).c_str()); f.close(); }
    }
    oledStatus("Gotek OMEGA " FW_VERSION, "Paired!", macToStr(_wave_mac), "AP: " AP_SSID);
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
  while (f.available()) {
    String line = f.readStringUntil('\n'); line.trim();
    if (line.startsWith("WAVE_MAC=")) {
      String mac = line.substring(9);
      sscanf(mac.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
             &_wave_mac[0],&_wave_mac[1],&_wave_mac[2],
             &_wave_mac[3],&_wave_mac[4],&_wave_mac[5]);
      bool isZero=true;
      for(int i=0;i<6;i++) if(_wave_mac[i]) { isZero=false; break; }
      if (!isZero) _paired=true;
    }
  }
  f.close();
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
  while (millis()-t0 < 30000) {
    PktHello hello = {};
    hello.type = PKT_PAIR_HELLO;
    WiFi.softAPmacAddress(hello.mac);
    strncpy(hello.ip, AP_IP, 15);
    hello.pad[0] = SAVE_PROTO_VER;
    hello.pad[1] = 1;                  // v3.6.0: HD-capable marker
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
      confirm.pad[1] = 1;              // v3.6.0: HD-capable marker
      _wavePeer->send_pkt((uint8_t*)&confirm, sizeof(confirm));
      break;
    }
  }

  if (_paired) {
    oledStatus("** PAIRED **", macToStr(_wave_mac), "AP: " AP_SSID, "Ready for disk");
    digitalWrite(LED_RED, LOW);
    digitalWrite(LED_BLUE, HIGH);
  } else {
    oledStatus("Not paired", "AP: " AP_SSID, "Tap PAIR NOW", "on Waveshare");
  }
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
  if (g_dirty_count > 0 && g_last_write_ms &&
      (millis() - g_last_write_ms) > SAVE_SETTLE_MS &&
      millis() > g_next_beacon_ms) {
    sendDirtyBeacon();
    g_next_beacon_ms = millis() + SAVE_BEACON_MS;
    // Red+blue together = unsaved data waiting (distinct from either alone)
    setLeds(true, true);
  }

  // LED blink if not paired
  if (!_paired) digitalWrite(LED_RED, (millis()/1000)&1 ? HIGH : LOW);

  delay(5);
}
