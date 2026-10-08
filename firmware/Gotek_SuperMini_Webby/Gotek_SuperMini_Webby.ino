// Gotek_SuperMini_Webby.ino  "Webby-0.1"  (WiFi / web edition of the Super Mini dongle)
// ============================================================================
// SIBLING of Gotek_SuperMini.ino (the base, v3.5.x). SAME proven core  USB-MSC
// ramdisk, build_volume, hardDetach/hardAttach, ESP-NOW + owner-lock, TCP app
// protocol  all reused verbatim. Webby ADDS a WiFi/web layer on top:
//
//    Two modes, flipped from the web page or a config file:
//       - WIFI   : joins your home WiFi (STA). Web page + app over the LAN.
//       - ESPNOW : own AP on ch6 + ESP-NOW (base behaviour). GTi can drive it.
//                  The web page is still served on the AP at 192.168.4.1.
//     "Revert to ESP-NOW" == "back to AP mode" (ESP-NOW lives on the AP channel).
//    A dependency-free web front end (ESP32 core WebServer / ESPmDNS / DNSServer):
//       GET  /            -> the "Load Image" + Wi-Fi page
//       GET  /status      -> {loaded,name,size,mode,ip}   (polled for live sync)
//       POST /upload      -> multipart ADF -> ramdisk -> re-insert to the Amiga
//       POST /eject       -> eject
//       POST /savewifi    -> save home creds, mode=WIFI, reboot
//       POST /espnow      -> mode=ESPNOW, reboot (back to AP + ESP-NOW)
//    Owner-lock is NOT always-on here (Webby = home-LAN trust). The base's
//     owner-lock code is still compiled and usable in ESPNOW mode, but a fresh
//     Webby is unlocked; locking is meant to be enrolled from the GTi later.
//
// *** UNTESTED  compiled/flashed by the owner. No external libraries needed. ***
//
// Board / IDE settings (identical to the base):
//   Board          : ESP32S3 Dev Module   (Super Mini, ESP32-S3FH4R2)
//   USB Mode       : USB-OTG (TinyUSB)     USB CDC on Boot: DISABLED   MSC on Boot: Disabled
//   PSRAM          : QSPI PSRAM (2MB quad  NOT OPI)
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
#include "esp_heap_caps.h"   // HD test: internal-heap readout
#include <LittleFS.h>
#include <Update.h>   // OMEGAWARE: web OTA
#include <WebServer.h>     // WEBBY: built-in (ESP32 core)  no external lib
#include <ESPmDNS.h>       // WEBBY: gotekomega.local
#include <mdns.h>          // 1.6.11: mdns_delegate_hostname_add - the leader keeps its own name too
#include <DNSServer.h>     // WEBBY: captive portal in AP mode
#include <WiFiUdp.h>       // FLEET: UDP discovery beacon (home-WiFi only)
#include "webui.h"       // PANEL: Dimmy's shared SPA (gzipped) + OMEGA_DARK preset

// 1.6.8: ONE source, TWO builds - the SuperMini and the Waveshare S3-Zero differ only in their status light.
//   0 = SuperMini  : two plain LEDs, red GPIO1 + blue GPIO2. GPIO21 is never touched.   (THIS sketch)
//   1 = S3-Zero    : one WS2812 colour LED on GPIO21. GPIO1/GPIO2 are left alone.        (../Gotek_Zero_Webby)
// Leave this at 0. The Zero build is its own sketch folder, Gotek_Zero_Webby, GENERATED from this file by
// its make_zero.py (only this line differs) - so each board compiles in its own folder and the two merged
// .bin files never overwrite each other. Edit here, then regenerate the Zero sketch.
// The version string says which one it is (Webby-1.6.8-supermini / Webby-1.6.8-zero), on the OLED, the web
// page and /api - so a dongle always tells you which image it runs.
#ifndef WEBBY_ZERO
#define WEBBY_ZERO     0
#endif
#if WEBBY_ZERO
#define WEBBY_BOARD    "zero"
#else
#define WEBBY_BOARD    "supermini"
#endif
#define FW_VERSION     "Webby-1.6.11-" WEBBY_BOARD   // 1.6.11: the fleet leader answers to gotekomega.local AND its own gotekomega-xxxx.local | /status has "ap_mac" (the MAC a paired screen knows) | the phone's "sign in to network" window shows a short signpost (keep the network, then open 192.168.4.1 in the browser) instead of the app, which half-worked there - no disk upload, and it closed when you kept the network | 1.6.10: the dongle's page in Polish and Czech (PL, CS in the language bar; a browser set to either gets it automatically) | 1.6.9: the dongle's own page (192.168.4.1) fixed - real version, any disk image up to 1.75 MB, header fits a phone, accents back, every line translated, the real Wi-Fi name, links to the full interface / flasher / help | 1.6.8: SuperMini / S3-Zero builds from one source (WEBBY_ZERO); the Wi-Fi name really is unique now - GotekOMEGA-XXXX came out as GotekOMEGA-0000 on every dongle (the MAC was read before the radio had started) | 1.6.7: take-over check moved to TCP command 0x0A (0x07 is ENROLL in the fleet contract) | 1.6.6: two screens can share this dongle - SHARE from an owner screen opens pairing for one more screen (2 min); the dongle remembers which screen sent the disk, tells scanning screens, and asks before another screen takes it over (refuses while that screen's saves are not handed back, unless forced); save reports go to the screen that sent the disk | 1.6.5: deleting the dongle on its GTi puts it back to "looking for a new owner"; a dongle with no owner always accepts pairing (was: shut after the first pairing, never reopened, and shut 6 min after power-on)
#define ESPNOW_CHANNEL 6
//  Board profile 
// Runs on ANY ESP32-S3 with: >=2MB PSRAM (the RAM disk lives there), the native
// USB broken out to a usable connector (it IS the USB drive), and >=4MB flash.
// The SuperMini is just the cheapest board that packages those three. To port to
// another S3, override these pins for that board (an unused GPIO is fine  the
// LEDs are optional status, not required). Defaults = SuperMini.
// --- Status LEDs -----------------------------------------------------------
// 1.6.8: chosen by WEBBY_ZERO (top of the file). SuperMini build = two discrete LEDs (red GPIO1,
// blue GPIO2) only; S3-Zero build = the WS2812 on GPIO21 only. (Before 1.6.8 one image drove both, and
// the WS2812 output was switched off for everyone in 1.6.2 because it starved the SuperMini's ESP-NOW -
// so the Zero had no status light at all.)
#ifndef LED_RED
#define LED_RED        1      // SuperMini red
#endif
#ifndef LED_BLUE
#define LED_BLUE       2      // SuperMini blue
#endif
#ifndef LED_NP_PIN
#define LED_NP_PIN     21     // Zero WS2812 data
#endif
#ifndef LED_NP_SWAP_RG
#define LED_NP_SWAP_RG 1      // this Zero's pixel reads bytes R,G,B (green<->red)
#endif
#ifndef LED_NP_BRIGHT
#define LED_NP_BRIGHT  28     // 0..255, keep low
#endif
#ifndef LED_NP_ENABLE
#define LED_NP_ENABLE  WEBBY_ZERO     // 1.6.8: on in the Zero build only | 1.6.2: WS2812/RMT OFF by default - driving the pixel starved the ESP-NOW radio on the SuperMini (pairing died). Discrete red/blue LEDs unaffected. Set 1 only where you accept the wireless risk.
#endif
#ifndef BOOT_PIN
#define BOOT_PIN       0
#endif

#define AP_SSID     "GotekOMEGA"
#define AP_PASS     "gotek1234"
#define AP_IP       "192.168.4.1"
#define TCP_PORT    3333
#define MDNS_NAME   "gotekomega"  // FLEET master name -> gotekomega.local

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
#define PKT_SHARE       0x1A   // 1.6.6: an owner screen lets ONE more screen pair (pairing open 2 min)

#define SAVE_PROTO_VER  1
#define SAVE_SETTLE_MS  3000
#define SAVE_BEACON_MS  10000
#define STATUS_BEACON_MS 2500
#define TCP_CMD_ESCAPE  0xFFFFFFFFUL
#define CMD_GET_SAVE    0x01
#define CMD_GET_STATUS  0x02
#define CMD_EJECT       0x03
#define CMD_EJECT_FORCE 0x04
#define CMD_CLAIM       0x0A   // 1.6.7: was 0x07 = ENROLL in the fleet wire contract (#24). 1.6.6: a screen claims the dongle before sending a disk (mac[6], flags bit0=take over, len, name)
#define CMD_SET_NAME    0x06   // #24: set the pretty display name for the NEXT flung disk (g_loaded_name only; FAT12 stays DISK.ADF)
//  FLEET: UDP discovery beacon (shared port: dongle, app, JC, browser-master) 
#define GTI_DISCO_PORT   51703
#define ALIVE_BEACON_MS  12000   // "I'm alive" cadence, home-WiFi only

#pragma pack(push,1)
struct PktHello  { uint8_t type; uint8_t mac[6]; char ip[16]; uint8_t pad[227]; };
struct PktSimple { uint8_t type; uint8_t pad[249]; };
struct PktDirty  { uint8_t type; uint32_t load_id; uint16_t dirty_count;
                   uint32_t image_size; uint32_t age_ms; uint8_t flags; uint8_t pad[234]; };
struct PktStatus { uint8_t type; uint8_t loaded; uint32_t load_id; uint32_t image_size; uint8_t pad[240]; };
#pragma pack(pop)

// FAT12 geometry
#define SECTOR_SIZE      512
#define TOTAL_SECTORS    3584   // HD: 1.75MB RAM disk (holds a 1.76MB HD ADF + FAT); leaves ~256KB PSRAM free on a 2MB board. Do NOT set to the full 2MB (4096)  ps_malloc can't take 100% of PSRAM.
#define RESERVED_SECTORS 1
#define SECTORS_PER_FAT  6
#define NUM_FATS         1
#define SECTORS_PER_CLUSTER 2   // HD: 1KB clusters -> ~2042 clusters, safely FAT12
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

// --- LED status HAL -------------------------------------------------------
// The rest of the firmware only calls setLeds()/ledRed()/ledBlue()/ledActivity();
// this maps the (red,blue) status onto whatever the board has  two discrete LEDs,
// or the Zero's single WS2812. LED=OFF in WEBBY.TXT silences it on either board.
static bool     g_led_enabled = true;
static bool     g_led_red = false, g_led_blue = false;
static uint32_t g_led_act_until = 0;   // activity pulse end (millis)

static void ledRender() {
  bool actv = ((int32_t)(g_led_act_until - millis()) > 0);
  // Discrete pair (SuperMini): the exact rule it always used.
  bool dred  = g_led_enabled && g_led_red;
  bool dblue = g_led_enabled && (g_led_blue || actv);
  // WS2812 (Zero): richer colour map with a distinct activity colour.
  uint8_t r=0,g=0,b=0;
  if (g_led_enabled) {
    if      (actv)                    { r=180; g=0;  b=200; }  // activity = magenta pulse (ADF arriving)
    else if (g_led_red && g_led_blue) { r=200; g=90; b=0;   }  // both  = amber (busy / unsaved writes)
    else if (g_led_red)               { r=220; g=0;  b=0;   }  // red   = fault / not paired
    else if (g_led_blue)              { r=0;   g=0;  b=220; }  // blue  = ready / disk loaded
    else                              { r=0;   g=16; b=0;   }  // idle  = dim green
  }
  uint8_t R=(uint16_t)r*LED_NP_BRIGHT/255, G=(uint16_t)g*LED_NP_BRIGHT/255, B=(uint16_t)b*LED_NP_BRIGHT/255;
  // Only touch the hardware when something changed (covers both outputs).
  uint32_t sig=((uint32_t)R<<16)|((uint32_t)G<<8)|B|((uint32_t)(dred?1:0)<<25)|((uint32_t)(dblue?1:0)<<24);
  static uint32_t last=0xFFFFFFFFu; if(sig==last) return; last=sig;
#if !WEBBY_ZERO
  digitalWrite(LED_RED,  dred ? HIGH : LOW);
  digitalWrite(LED_BLUE, dblue? HIGH : LOW);
#else
  (void)dred; (void)dblue;
#endif
#if LED_NP_ENABLE
#if LED_NP_SWAP_RG
  neopixelWrite(LED_NP_PIN, G, R, B);   // R/G swapped for this Zero's pixel
#else
  neopixelWrite(LED_NP_PIN, R, G, B);
#endif
#endif
}
static inline void ledRed(bool on){ g_led_red = on; ledRender(); }
static inline void ledBlue(bool on){ g_led_blue = on; ledRender(); }
static inline void setLeds(bool red, bool blue){ g_led_red = red; g_led_blue = blue; ledRender(); }
static inline void ledActivity(uint16_t ms=350){ g_led_act_until = millis() + ms; ledRender(); }
static inline void ledTick(){ ledRender(); }   // call each loop so time-based states refresh
static void ledInit(){
#if !WEBBY_ZERO
  pinMode(LED_RED, OUTPUT); pinMode(LED_BLUE, OUTPUT);   // WS2812 needs no pinMode
#endif
  ledRender();
}
static void oledStatus(const String& l0, const String& l1, const String& l2, const String& l3) {
  (void)l1;(void)l2;(void)l3;
  if (l0.startsWith("**") || l0.startsWith("Receiving") || l0.startsWith("Gotek") || l0.startsWith("LOADED")) setLeds(false,true);
  else if (l0.startsWith("Not paired")) setLeds(true,false);
}
static void oledProgress(uint32_t done, uint32_t total) {
  (void)total; static bool t=false; t=!t; ledBlue(t); (void)done;
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
  wr16(bs,11,SECTOR_SIZE);bs[13]=SECTORS_PER_CLUSTER;wr16(bs,14,RESERVED_SECTORS);bs[16]=NUM_FATS;
  wr16(bs,17,ROOT_ENTRIES);wr16(bs,19,TOTAL_SECTORS);bs[21]=0xF8;
  wr16(bs,22,SECTORS_PER_FAT);wr16(bs,24,32);wr16(bs,26,64);
  bs[36]=0x80;bs[38]=0x29;wr32(bs,39,0x12345678);
  memcpy(&bs[43],"ESP32MSC   ",11);memcpy(&bs[54],"FAT12   ",8);
  bs[510]=0x55;bs[511]=0xAA;
  uint8_t* fat=g_disk+RESERVED_SECTORS*SECTOR_SIZE;
  fat[0]=0xF8;fat[1]=0xFF;fat[2]=0xFF;
  uint32_t clb=(uint32_t)SECTORS_PER_CLUSTER*512; uint32_t need=(fsz+clb-1)/clb;
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

// #24: the pretty display name for the NEXT flung disk (set-next-name escape),
// consumed once when the disk lands. FAT12 root name stays constant (DISK.ADF).
static String g_next_name = "";
// 1.6.6: which screen put the current disk in (set by CMD_CLAIM just before a fling; all zero = unknown:
// an older screen, the web page, or nothing loaded). Other screens see it in the pairing reply ("in use by").
static uint8_t g_loader_mac[6]  = {0};
static char    g_loader_name[25] = {0};
static uint8_t g_claim_mac[6]   = {0};
static char    g_claim_name[25] = {0};
static bool    g_claim_pending  = false;
static uint32_t g_claim_ms      = 0;      // a claim only counts for the fling that follows it (20 s)
static void loaderClear(){ memset(g_loader_mac,0,6); g_loader_name[0]=0; }

// #24: a valid, legal 8.3 FAT name from any display string (upper alnum only,
// <=8 chars) + a constant .ADF  the FAT name is cosmetic (nothing reads it),
// so this keeps a long/odd upload filename from producing a garbage root entry.
static String to83(const String& in){
  const char* s=in.c_str(); const char* dot=strrchr(s,'.');
  size_t nl = dot ? (size_t)(dot-s) : in.length();
  String base;
  for(size_t i=0;i<nl && base.length()<8;i++){ char c=s[i];
    if((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')) base += (char)toupper(c); }
  if(base.length()==0) base="OMEGA";
  return base + ".ADF";
}

// Wireless DSK fix (1.6.3): like to83() but PRESERVES the real extension
// (ADF/DSK/IMG/DSD/...) so the FAT12 root advertises the correct format to
// FlashFloppy. A flung CPC .dsk used to be named DISK.ADF -> FF Error 34.
// Base is upper-alnum, <=8 chars; extension is upper-alnum, <=3 chars; ADF fallback.
static String to83keepext(const String& in){
  const char* s=in.c_str(); const char* dot=strrchr(s,'.');
  size_t nl = dot ? (size_t)(dot-s) : in.length();
  String base;
  for(size_t i=0;i<nl && base.length()<8;i++){ char c=s[i];
    if((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')) base += (char)toupper(c); }
  if(base.length()==0) base="OMEGA";
  String ext;
  if(dot){ for(size_t i=1;i<=3 && dot[i] && dot[i]!='.'; i++){ char c=dot[i];
    if((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')) ext += (char)toupper(c); } }
  if(ext.length()==0) ext="ADF";
  return base + "." + ext;
}

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
struct RxPkt { uint8_t data[RX_PKT_SIZE]; int len; uint8_t src[6]; };   // 1.6.4 (#24): src = the REAL sender MAC (radio header), not the payload claim
static QueueHandle_t _rxQueue = nullptr;
static void queuePacket(const uint8_t* data, int len, const uint8_t* src) {
  if (!_rxQueue) return;
  RxPkt pkt; int n = min(len, RX_PKT_SIZE);
  memcpy(pkt.data, data, n); pkt.len = n;
  if (src) memcpy(pkt.src, src, 6); else memset(pkt.src, 0, 6);   // 1.6.4: sender from the radio header
  xQueueSendFromISR(_rxQueue, &pkt, nullptr);
}
static void handleESPNOW(const uint8_t* data, int len, const uint8_t* src);

class XiaoPeer : public ESP_NOW_Peer {
public:
  XiaoPeer(const uint8_t* mac, uint8_t ch, wifi_interface_t iface, const uint8_t* lmk)
    : ESP_NOW_Peer(mac, ch, iface, lmk) {}
  ~XiaoPeer() { remove(); }
  bool add_peer() { return add(); }
  bool send_pkt(const uint8_t* d, size_t l) { return send(d, l); }
  void onReceive(const uint8_t* d, size_t l, bool b) override { queuePacket(d, (int)l, addr()); }   // 1.6.4: sender = this peer
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

static void handleESPNOW(const uint8_t* data, int len, const uint8_t* src) {
  if (len < 1) return;
  uint8_t type = data[0];
  // 1.6.4 (#24): owner decisions use the REAL sender (src, from the radio header),
  // never p->mac (a payload field anyone can fill in). The panel sends from its STA
  // interface and puts WiFi.macAddress() in p->mac, so for a genuine GTi the two are
  // equal and existing pairings carry over unchanged.
  static const uint8_t ZERO6[6] = {0,0,0,0,0,0};
  const bool haveSrc = src && memcmp(src, ZERO6, 6) != 0;
  if (type == PKT_PAIR_HELLO) {
    PktHello hp = {}; memcpy(&hp, data, min((size_t)len, sizeof(hp)));   // copy, then overwrite the claimed MAC with the real one
    if (haveSrc) memcpy(hp.mac, src, 6);
    const PktHello* p = &hp;
    // WEBBY note: Webby ships unlocked, so with no enrolled owners any GTi may pair
    // (g_enroll_open is forced true at boot in ESPNOW mode when _owner_count==0).
    bool known = isOwner(p->mac);
    if (!known) {
      // 1.6.5: an UNCLAIMED dongle (no owners) is always open - same rule as EJECT below (JFW).
      // Before, the door shut when the first GTi paired and was never reopened when that GTi
      // unpaired (deleted the dongle), and shut 6 minutes after power-on even with no owner, so
      // the dongle went silent to every scan until it was power-cycled or BOOT was held 5 s.
      // Once an owner exists the door is closed as before: a SECOND screen still needs BOOT 5 s.
      if (!g_enroll_open && _owner_count > 0) return;
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
    // 1.6.6: who has a disk in me (pad[7] marker 0xA5, [8] loaded, [9..14] screen MAC, [15] len, [16..39] name).
    // pad[1..6] stay free for the parked HD-capability fields.
    reply.pad[7] = 0xA5; reply.pad[8] = g_disk_loaded ? 1 : 0; memcpy(reply.pad+9, g_loader_mac, 6);
    { uint8_t L = (uint8_t)strlen(g_loader_name); if (L > 24) L = 24; reply.pad[15] = L; memcpy(reply.pad+16, g_loader_name, L); }
    XiaoPeer* dst = _wavePeer ? _wavePeer : _bcastPeer;
    if (dst) dst->send_pkt((uint8_t*)&reply, sizeof(reply));
    oledStatus("Gotek OMEGA " FW_VERSION, known?"Reconnected":"Owner added", macToStr(_wave_mac), String(_owner_count)+" owner(s)");
    return;
  }
  if (type == PKT_SHARE) {
    // 1.6.6: only an existing owner can open the door, and only for one more screen (the door closes again
    // as soon as that screen pairs - see PAIR_HELLO) or after 2 minutes.
    if (haveSrc && isOwner(src)) {
      g_enroll_open = true; g_enroll_until = millis() + 2UL*60UL*1000UL;
      oledStatus("Gotek OMEGA " FW_VERSION, "Sharing", "2 min to pair", "one more screen");
    }
    return;
  }
  if (type == PKT_UNPAIR) {
    PktHello up = {}; memcpy(&up, data, min((size_t)len, sizeof(up)));
    if (haveSrc) memcpy(up.mac, src, 6);   // 1.6.4: a screen can only unpair ITSELF
    const PktHello* p = &up;
    if (removeOwner(p->mac)) {
      saveOwners();
      if (memcmp(_wave_mac, p->mac, 6)==0) {
        if (_wavePeer) { delete _wavePeer; _wavePeer=nullptr; }
        if (_owner_count) memcpy(_wave_mac, _owners[0], 6); else memset(_wave_mac,0,6);
      }
      _paired = (_owner_count>0);
      // 1.6.5: the last owner deleted this dongle -> back to "looking for a new owner", exactly like a
      // fresh dongle at power-on (pairing open, pairing blink). The rule in PAIR_HELLO keeps it open
      // for as long as it has no owner, so it never goes silent to a scan.
      if (_owner_count == 0) { g_enroll_open = true; g_enroll_until = millis() + 6UL*60UL*1000UL; }
      oledStatus("Gotek OMEGA " FW_VERSION, "Unpaired", String(_owner_count)+" owner(s)", "");
    }
    return;
  }
  if (type == PKT_DISK_EJECT) {
    // 1.6.4 (#24): an unclaimed dongle stays open (JFW); once an owner exists, only an owner may eject.
    if (_owner_count > 0 && !(haveSrc && isOwner(src))) return;
    if (g_disk_loaded) { hardDetach(); g_disk_loaded=false; }
    dirtyReset(); ledBlue(false); loaderClear();
    oledStatus("Gotek OMEGA " FW_VERSION, "Ejected", "", "Ready");
    return;
  }
}
static void onNewPeer(const esp_now_recv_info_t* info, const uint8_t* data, int len, void* arg) { queuePacket(data, len, info ? info->src_addr : nullptr); }   // 1.6.4: keep src_addr

// Owner config load (base)
// size-trim: "aa:bb:cc:dd:ee:ff" -> 6 bytes; returns how many fields it read (stops at the first bad one,
// like the sscanf("%hhx:...") it replaces, whose libc scanner cost 8.7 KB of flash).
static int parseMac(const char* s, uint8_t m[6]) {
  int n = 0;
  while (n < 6) {
    char* e; unsigned long v = strtoul(s, &e, 16);
    if (e == s || v > 255) break;
    m[n++] = (uint8_t)v;
    if (*e != ':') break;
    s = e + 1;
  }
  return n;
}

static void loadConfig() {
  if (!LittleFS.begin(true)) return;
  File f = LittleFS.open("/XIAO_CONFIG.TXT", "r"); if (!f) return;
  _owner_count = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n'); line.trim();
    if (line.startsWith("WAVE_MAC=")) {
      uint8_t m[6]={0}; String mac = line.substring(9);
      if (parseMac(mac.c_str(), m) == 6) {   // size-trim: was sscanf, which pulled in 8.7 KB of libc
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
static String g_devname = "";   // #name: user-set device name (mDNS/hostname-safe), from the portal; empty = the mac-based default
// #name: defined here (before statusJson/handleSaveWifi) so there is NO forward-ref
// -> arduino's ctags prototype generator stays off and can't choke on the raw-string JS.
static String sanitizeName(const String& in){   // -> valid lowercase mDNS label (a-z 0-9 + single hyphens, <=24)
  String o; char last=0;
  for(size_t i=0;i<in.length() && o.length()<24;i++){ char c=in[i];
    if(c>='A'&&c<='Z') c=c-'A'+'a';
    if((c>='a'&&c<='z')||(c>='0'&&c<='9')){ o+=c; last=c; }
    else if(o.length() && last!='-'){ o+='-'; last='-'; } }
  while(o.length() && o[o.length()-1]=='-') o.remove(o.length()-1);
  return o;
}
static String discoName(){
  if(g_devname.length()) return g_devname;   // #name: user-set (sanitized on save) -> beacon roster + slave mDNS
  uint8_t m[6]; WiFi.macAddress(m);
  char b[24]; snprintf(b,sizeof(b),"gotekomega-%02x%02x",m[4],m[5]); return String(b); }
static int    g_webmode = 0;
static bool   g_join_failed = false;   // resolved at boot: 0 = ESPNOW/AP, 1 = WIFI/STA
static String g_loaded_name = "";

static void loadWifiCfg() {
  if (!LittleFS.begin(true)) return;
  File f = LittleFS.open("/WEBBY.TXT", "r"); if (!f) return;
  while (f.available()) {
    String line = f.readStringUntil('\n'); line.trim();
    if      (line.startsWith("SSID=")) g_ssid    = line.substring(5);
    else if (line.startsWith("PASS=")) g_pass    = line.substring(5);
    else if (line.startsWith("MODE=")) g_modeStr = line.substring(5);
    else if (line.startsWith("NAME=")) g_devname = line.substring(5);
    else if (line.startsWith("LED="))  { String v=line.substring(4); v.trim(); v.toUpperCase(); g_led_enabled = !(v=="OFF"||v=="0"||v=="NO"); }
  }
  f.close();
}
static void saveWifiCfg(const String& ssid, const String& pass) {
  if (!LittleFS.begin(true)) return;
  File f = LittleFS.open("/WEBBY.TXT", "w"); if (!f) return;
  f.printf("SSID=%s\n", ssid.c_str());
  f.printf("PASS=%s\n", pass.c_str());
  f.printf("MODE=WIFI\n");
  f.printf("NAME=%s\n", g_devname.c_str());   // #name: kept alongside the creds
  f.close();
}
static void setModeEspnow() {
  if (!LittleFS.begin(true)) return;
  // keep any saved SSID/PASS, just flip MODE=ESPNOW
  File f = LittleFS.open("/WEBBY.TXT", "w"); if (!f) return;
  f.printf("SSID=%s\n", g_ssid.c_str());
  f.printf("PASS=%s\n", g_pass.c_str());
  f.printf("MODE=ESPNOW\n");
  f.printf("NAME=%s\n", g_devname.c_str());   // #name: preserved across a mode flip
  f.close();
}
// WEBBY: full wipe of the saved home-Wi-Fi credentials + force ESP-NOW. Used by the
// long BOOT-hold; the plain flip (setModeEspnow) keeps SSID/PASS for a one-tap rejoin.
static void wipeWifiCreds() {
  if (!LittleFS.begin(true)) return;
  File f = LittleFS.open("/WEBBY.TXT", "w"); if (!f) return;
  f.printf("SSID=\n");
  f.printf("PASS=\n");
  f.printf("MODE=ESPNOW\n");
  f.close();
  g_ssid = ""; g_pass = ""; g_modeStr = "ESPNOW";
}

//  base TCP save/eject/status handlers (unchanged) 
static WiFiServer _tcpServer(TCP_PORT);
static WiFiUDP    _disco;                // FLEET: discovery beacon socket
static uint32_t   g_next_alive_ms = 0;   // FLEET: next "I'm alive" broadcast
static inline void wrLE32(uint8_t*p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static inline void wrLE16(uint8_t*p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void doEject(WiFiClient& client, bool force){
  if (!force && g_dirty_count > 0) { client.write((uint8_t)0x02); client.flush(); return; }
  if (g_disk_loaded) { hardDetach(); g_disk_loaded = false; }
  dirtyReset(); g_loaded_name=""; ledBlue(false); loaderClear();
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

// 1.6.12: a transfer that fails after the old disk was detached must leave the dongle
// cleanly EMPTY: no stale load_id (the screen would fetch "saves" from a wiped image),
// no stale dirty map (LED amber, EJECT refused), no old name on the page.
static void markDiskEmpty(const char* why) {
  if (g_disk_loaded) { hardDetach(); g_disk_loaded = false; }
  dirtyReset(); g_loaded_name = ""; g_image_size = 0; g_load_id++; g_next_name = "";
  loaderClear(); ledBlue(false); g_next_status_ms = 0;
  Serial.printf("[DISK] empty: %s\n", why);
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
    else if (cmd == CMD_CLAIM) {      // 1.6.6: mac[6] + flags + len + name. Reply 0x01 go ahead / 0x03 in use / 0x02 unsaved saves
      uint8_t in[8]; int got=0; uint32_t tb=millis();
      while(got<8 && millis()-tb<2000){ if(!client.connected())break; int c=client.read(); if(c<0){delay(1);continue;} in[got++]=(uint8_t)c; }
      if (got<8) { client.write((uint8_t)0x00); return; }
      int len = in[7] > 24 ? 24 : in[7]; char nb[25]; int ng=0; tb=millis();
      while(ng<in[7] && millis()-tb<2000){ if(!client.connected())break; int c=client.read(); if(c<0){delay(1);continue;} if(ng<len)nb[ng]=(char)c; ng++; }
      nb[ng<len?ng:len]=0;
      static const uint8_t Z6[6]={0,0,0,0,0,0};
      bool force   = (in[6] & 1) != 0;
      bool other   = g_disk_loaded && memcmp(g_loader_mac, in, 6) != 0;
      bool unknown = memcmp(g_loader_mac, Z6, 6) == 0;
      // Refuse (unless forced) when another screen's disk is in, or anyone's unsaved saves are waiting.
      uint8_t verdict = 0x01;
      if (other && !force) { if (g_dirty_count > 0) verdict = 0x02; else if (!unknown) verdict = 0x03; }
      if (verdict != 0x01) {
        const char* who = g_loader_name[0] ? g_loader_name : "another screen";
        uint8_t wl = (uint8_t)strlen(who); if (wl > 24) wl = 24;
        client.write(verdict); client.write(wl); client.write((const uint8_t*)who, wl);
        return;
      }
      memcpy(g_claim_mac, in, 6); memcpy(g_claim_name, nb, 25); g_claim_pending = true; g_claim_ms = millis();
      client.write((uint8_t)0x01);
    }
    else if (cmd == CMD_SET_NAME) {   // #24: 1-byte length + name bytes -> g_next_name
      uint32_t tn=millis(); while(client.available()<1 && millis()-tn<2000){ if(!client.connected())break; delay(1); }
      int len = client.available()>=1 ? client.read() : 0;
      char nb[129]; int got=0; uint32_t tb=millis();
      while(got<len && millis()-tb<2000){ if(!client.connected())break; int c=client.read(); if(c<0){delay(1);continue;} if(got<128)nb[got]=(char)c; got++; tb=millis(); }
      nb[got<128?got:128]=0; g_next_name=String(nb);
      client.write((uint8_t)0x01);
    }
    else client.write((uint8_t)0x00);
    return;
  }
  if (size == 0 || size > MAX_FILE_BYTES) { client.write((uint8_t)0x00); return; }
  // Wireless DSK fix (1.6.3): name the FAT12 root with the flung disk's REAL
  // extension so FlashFloppy detects the format. The panel sends the real
  // filename+ext via CMD_SET_NAME immediately before the fling; absent that
  // (older panel), fall back to the historic DISK.ADF.
  String fatName = g_next_name.length() ? to83keepext(g_next_name) : String("DISK.ADF");
  // 1.6.4 (#24): if a disk is already attached, detach FIRST - otherwise the host stays
  // mounted on a volume we are rewriting underneath it for the whole transfer.
  // (The browser-upload path already did this; the TCP path now matches.)
  // 1.6.12: nothing is touched until the first payload bytes are here. A header with no
  // body (port scan, a screen that gives up) used to detach the Amiga's disk, zero the
  // whole ramdisk and block loop() for 30 s. The FAT/root metadata is laid down at the
  // END (as the browser upload path does), so only the data region is streamed into.
  uint8_t* dst = g_disk + DATA_LBA * SECTOR_SIZE;
  uint32_t received = 0; const size_t BUF = 4096;
  uint8_t* buf = (uint8_t*)malloc(BUF); if (!buf) { client.write((uint8_t)0x00); return; }
  t0 = millis();
  while (client.available() <= 0 && client.connected() && millis()-t0 < 5000) delay(1);
  if (client.available() <= 0) { free(buf); client.write((uint8_t)0x00); client.stop(); oledStatus("No data", "", "", "disk untouched"); return; }
  if (g_disk_loaded) { hardDetach(); g_disk_loaded = false; }
  t0 = millis();
  while (received < size && millis()-t0 < 30000) {   // 30s = max STALL (no progress), not total  a slow-but-steady fling completes; t0 resets on every read below
    if (!client.connected()) break;
    int avail = client.available(); if (avail <= 0) { delay(1); continue; }
    size_t toRead = min((size_t)avail, min(BUF, (size_t)(size-received)));
    int rd = client.read(buf, toRead);
    if (rd > 0) { memcpy(dst + received, buf, rd); received += rd; oledProgress(received, size); t0 = millis(); }
  }
  free(buf);
  if (received == size) {
    build_volume_ex(fatName.c_str(), size, false);   // 1.6.12: metadata last, the data is already in place
    g_load_id++; g_image_size = size; dirtyReset();
    { String pretty = g_next_name.length() ? g_next_name : String("DISK.ADF");   // #24/1.6.3: pretty display name (extension stripped)
      int d = pretty.lastIndexOf('.'); if (d > 0) pretty = pretty.substring(0, d);
      g_loaded_name = pretty; }
    g_next_name = "";   // consume it  the next fling must set its own name
    uint8_t ack[5]; ack[0]=0x01; wrLE32(ack+1,g_load_id); client.write(ack,5); client.flush(); delay(100); client.stop();
    if (g_disk_loaded) hardDetach(); hardAttach(); g_disk_loaded = true; g_next_status_ms = 0; ledBlue(true); ledActivity();
    // 1.6.6: remember which screen sent it; if that screen is an owner, its save reports go to it from now on
    if (g_claim_pending && millis() - g_claim_ms < 20000) {
      memcpy(g_loader_mac, g_claim_mac, 6); memcpy(g_loader_name, g_claim_name, 25); g_claim_pending = false;
      if (isOwner(g_loader_mac) && memcmp(_wave_mac, g_loader_mac, 6) != 0) {
        memcpy(_wave_mac, g_loader_mac, 6); _paired = true;
        if (_wavePeer) { delete _wavePeer; _wavePeer = nullptr; }
        _wavePeer = new XiaoPeer(_wave_mac, ESPNOW_CHANNEL, WIFI_IF_STA, nullptr);
        if (!_wavePeer->add_peer()) { delete _wavePeer; _wavePeer = nullptr; }
      }
    } else loaderClear();   // an older screen (no claim): unknown
    g_claim_pending = false;
    oledStatus("LOADED!", "", "USB: attached", "Gotek ready");
    sendSimple(PKT_XIAO_DONE);
  } else {
    client.write((uint8_t)0x00); client.flush(); delay(100); client.stop();
    markDiskEmpty(received ? "fling stalled or dropped" : "fling: no data");
    oledStatus("TRANSFER ERROR", "", "dongle is empty", "send again"); sendSimple(PKT_XIAO_ERROR);
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
static char g_ap_name[24] = "GotekOMEGA";   // 1.6.9: the real AP name, shown on the page
static String statusJson(){
  String ip = (g_webmode==1) ? WiFi.localIP().toString() : String(AP_IP);
  String s = "{";
  s += "\"fw\":\""; s += FW_VERSION; s += "\",";
  s += "\"loaded\":"; s += (g_disk_loaded?"true":"false");
  s += ",\"name\":\""; s += jsonEsc(g_loaded_name); s += "\"";
  s += ",\"size\":"; s += String(g_image_size);
  s += ",\"mode\":\""; s += (g_webmode==1 ? "wifi" : "espnow"); s += "\"";
  s += ",\"ssid\":\""; s += jsonEsc(g_ssid); s += "\"";
  s += ",\"devname\":\""; s += jsonEsc(discoName()); s += "\"";   // #name: current device name (custom or mac-based) for portal pre-fill + the fleet
  { uint8_t am[6] = {0}; esp_read_mac(am, ESP_MAC_WIFI_SOFTAP); char ab[20]; snprintf(ab, sizeof(ab), "%02x:%02x:%02x:%02x:%02x:%02x", am[0],am[1],am[2],am[3],am[4],am[5]); s += ",\"ap_mac\":\""; s += ab; s += "\""; }   // 1.6.11: the MAC a paired screen knows (PKT_PAIR_REPLY), so it can tell this dongle exactly on home Wi-Fi (#110)
  s += ",\"join_failed\":"; s += (g_join_failed ? "true" : "false");
  s += ",\"ip\":\""; s += ip; s += "\"";
  s += ",\"heap\":"; s += String((unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));   // HD test
  s += ",\"psram\":"; s += String((unsigned)ESP.getFreePsram());
  s += ",\"disk\":"; s += String((unsigned)((uint32_t)TOTAL_SECTORS*SECTOR_SIZE));
  s += ",\"max\":"; s += String((unsigned)MAX_FILE_BYTES);   // 1.6.9: biggest image the page may send
  s += ",\"ap\":\""; s += jsonEsc(String(g_ap_name)); s += "\"";   // 1.6.9: real AP name for the page
  s += "}";
  return s;
}

// Finalize a browser upload: lay metadata over the streamed data, re-insert.
static void webFinishLoad(){
  uint32_t size = g_up_recv;
  loaderClear(); strcpy(g_loader_name, "web page");   // 1.6.6: loaded from the browser, not a screen
  build_volume_ex(to83keepext(g_up_name).c_str(), size, false);   // #24/1.6.3: 8.3-mangle keeping the real extension so FF detects DSK/ADF/etc; full name kept in g_loaded_name (below)
  g_image_size = size; g_load_id++; dirtyReset();
  if (g_disk_loaded) hardDetach();
  hardAttach(); g_disk_loaded = true; g_next_status_ms = 0; ledBlue(true); ledActivity();
  oledStatus("LOADED!", "", "USB: attached", "Gotek ready");
  sendSimple(PKT_XIAO_DONE);   // harmless if no ESP-NOW peer
}
static void handleUpload(){
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    g_up_recv = 0; g_up_overflow = false;
    g_up_name = up.filename; if (g_up_name.length()==0) g_up_name = "DISK.ADF";
  } else if (up.status == UPLOAD_FILE_WRITE) {
    // 1.6.12: detach on the FIRST data chunk, not at START - an upload that dies before any
    // data (or a 0-byte file) leaves the Amiga's disk alone
    if (g_up_recv == 0 && up.currentSize > 0 && g_disk_loaded) { hardDetach(); g_disk_loaded = false; ledBlue(false); }
    if (!g_up_overflow && g_up_recv + up.currentSize <= MAX_FILE_BYTES) {
      memcpy(g_disk + DATA_LBA*SECTOR_SIZE + g_up_recv, up.buf, up.currentSize);
      g_up_recv += up.currentSize;
    } else { g_up_overflow = true; }
  } else if (up.status == UPLOAD_FILE_END) {
    // finalized by the POST responder below
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (g_up_recv > 0) markDiskEmpty("browser upload aborted");   // 1.6.12: the data region is half new, half old
    g_up_recv = 0; g_up_overflow = false;
  }
}
static void handleUploadDone(){
  if (g_up_overflow) { markDiskEmpty("browser upload too big"); server.send(413,"application/json","{\"ok\":false,\"err\":\"image too big - this dongle holds up to 1.75 MB\"}"); return; }
  if (g_up_recv == 0) { server.send(400,"application/json","{\"ok\":false,\"err\":\"empty upload\"}"); return; }
  g_loaded_name = g_up_name;
  webFinishLoad();
  server.send(200,"application/json", statusJson());
}
static void handleEjectWeb(){
  if (g_disk_loaded) { hardDetach(); g_disk_loaded = false; }
  dirtyReset(); g_loaded_name=""; ledBlue(false); loaderClear();
  oledStatus("Gotek OMEGA " FW_VERSION, "Ejected (web)", "", "Ready");
  server.send(200,"application/json", statusJson());
}
static void handleScan(){
  // A scan needs the STA interface. In ESP-NOW/AP-only mode there is no
  // STA, so scanNetworks returns -2 and the portal shows "scan failed" 
  // exactly what a user hits on a fresh dongle. Add STA for the scan.
  wifi_mode_t pm = WiFi.getMode();
  if (!(pm & WIFI_MODE_STA)) WiFi.mode((wifi_mode_t)(pm | WIFI_MODE_STA));
  int n = WiFi.scanNetworks(false, true, false, 200);
  String jj = "{\"networks\":[";
  for (int i = 0; i < n && i < 30; i++) {
    if (i) jj += ",";
    jj += "{\"ssid\":\""; jj += jsonEsc(WiFi.SSID(i));
    jj += "\",\"rssi\":"; jj += String(WiFi.RSSI(i));
    jj += ",\"enc\":"; jj += (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true");
    jj += "}";
    yield();
  }
  jj += "]}";
  WiFi.scanDelete();
  server.send(200, "application/json", jj);
}

static void handleSaveWifi(){
  String ssid = server.arg("ssid"); String pass = server.arg("pass");
  if (ssid.length()==0) { server.send(400,"application/json","{\"ok\":false,\"err\":\"no SSID\"}"); return; }
  if (server.hasArg("name")) g_devname = sanitizeName(server.arg("name"));   // #name: set at first setup so it joins already-named (no default-name clash)
  saveWifiCfg(ssid, pass);
  bool saved = false;
  if (LittleFS.begin(true)) { File rf = LittleFS.open("/WEBBY.TXT","r"); if (rf) { String want = "SSID=" + ssid; while (rf.available()) { String l = rf.readStringUntil('\n'); l.trim(); if (l == want) { saved = true; break; } } rf.close(); } }
  server.send(200,"application/json", String("{\"ok\":true,\"saved\":") + (saved?"true":"false") + "}");
  if (saved) { delay(500); ESP.restart(); }
}
static void handleEspnowWeb(){
  setModeEspnow();
  server.send(200,"application/json","{\"ok\":true}");
  delay(400); ESP.restart();
}

// The page (self-contained; talks to the real endpoints above)
// size-trim: the dongle's own page lives in dongle_page.html; make_dongle_page.py stores it gzipped in
// dongle_page.h (dongle_page_gz). The footer version is filled in from /status by the page itself.
#include "dongle_page.h"

//  PANEL: Dimmy shared-UI adapter 
// Serves webui.h (his single-page app) and answers the /api/* calls its
// card-less "has_sd:false" dongle surface makes, mapped onto our Webby state.
// This is a thin shim  NOT web_panel.h  so the JC-only globals stay out.
static String g_active_theme = "OMEGA_DARK";  // PANEL: which theme the gallery has activated
static void saveTheme(const String& t){
  if(!LittleFS.begin(true)) return;
  File f=LittleFS.open("/THEME.TXT","w"); if(!f) return;
  f.print(t); f.close();
}
static void loadTheme(){
  if(!LittleFS.begin(true)) return;
  File f=LittleFS.open("/THEME.TXT","r"); if(!f) return;
  String t=f.readStringUntil('\n'); t.trim(); f.close();
  if(t.length()) g_active_theme=t;
}
//  Web OTA (added by OMEGAWARE): flash a new firmware over WiFi into the
// inactive OTA slot. Uses the WebServer upload stream (Webby is not our
// client-parser firmware). First byte must be 0xE9 (an ESP32 image).
static bool g_ota_ok=false, g_ota_run=false, g_ota_first=false;
static void onOtaUpload(){
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    g_ota_ok=false; g_ota_first=true;
    if (Update.isRunning()) Update.abort();   // 1.6.12: a previous aborted upload must not block this one
    g_ota_run = Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH);
  } else if (up.status == UPLOAD_FILE_WRITE && g_ota_run) {
    if (g_ota_first) { g_ota_first=false; if (up.currentSize>0 && up.buf[0]!=0xE9) { Update.abort(); g_ota_run=false; return; } }
    if (Update.write(up.buf, up.currentSize) != up.currentSize) { Update.abort(); g_ota_run=false; }   // 1.6.12: release the slot
  } else if (up.status == UPLOAD_FILE_END && g_ota_run) {
    g_ota_ok = Update.end(true);
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (g_ota_run) Update.abort(); g_ota_run=false; g_ota_ok=false;   // 1.6.12: a dropped upload left Update "running" until a power cycle
  }
}
static void onOtaDone(){
  server.send(200, "application/json", g_ota_ok ? "{\"status\":\"ok\"}" : "{\"error\":\"firmware update failed - not an ESP32 image?\"}");
  if (g_ota_ok) { delay(600); ESP.restart(); }
}

static void apiSystemInfo(){
  String ip = (g_webmode==1) ? WiFi.localIP().toString() : String(AP_IP);
  String j = "{";
  j += "\"firmware\":\""; j += FW_VERSION; j += "\",";
  j += "\"heap_free\":"; j += String((unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL)); j += ",";
  j += "\"psram_free\":"; j += String((unsigned)ESP.getFreePsram()); j += ",";
  j += "\"sd_used_mb\":0,\"sd_total_mb\":0,";
  j += "\"game_count\":0,\"file_count\":0,";
  j += "\"loaded_game\":\""; j += (g_disk_loaded ? jsonEsc(g_loaded_name) : String("none")); j += "\",";
  j += "\"mode\":\""; j += (g_webmode==1 ? "WIFI" : "ESPNOW"); j += "\",";
  j += "\"theme\":\""; j += g_active_theme; j += "\",";
  j += "\"wifi_ip\":\""; j += ip; j += "\",";
  j += "\"wifi_clients\":"; j += String((unsigned)WiFi.softAPgetStationNum()); j += ",";
  j += "\"internet\":"; j += (g_webmode==1 ? "true" : "false"); j += ",";
  j += "\"internet_ssid\":\""; j += (g_webmode==1 ? jsonEsc(g_ssid) : String("")); j += "\",";
  j += "\"internet_ip\":\""; j += (g_webmode==1 ? ip : String("")); j += "\",";
  j += "\"has_sd\":false,\"has_display\":false,\"has_ota\":true,";
  j += "\"ftp_enabled\":false,\"dav_enabled\":false,\"log_enabled\":false";
  j += "}";
  server.send(200,"application/json", j);
}
static void apiDiskStatus(){
  String j = "{";
  j += "\"loaded\":"; j += (g_disk_loaded?"true":"false"); j += ",";
  j += "\"file\":\""; j += jsonEsc(g_loaded_name); j += "\",";
  j += "\"name\":\""; j += jsonEsc(g_loaded_name); j += "\",";
  j += "\"game\":\""; j += jsonEsc(g_loaded_name); j += "\",";
  j += "\"path\":\"\",\"source\":\""; j += (g_disk_loaded?"USB":""); j += "\",";
  j += "\"disk_num\":"; j += String(g_disk_loaded?1:0); j += ",";
  j += "\"disk_total\":"; j += String(g_disk_loaded?1:0);
  j += "}";
  server.send(200,"application/json", j);
}
static void apiDiskUnload(){
  if (g_disk_loaded) { hardDetach(); g_disk_loaded = false; }
  dirtyReset(); g_loaded_name=""; ledBlue(false); loaderClear();
  server.send(200,"application/json","{\"status\":\"ok\"}");
}
static void apiGamesUploadDone(){
  if (g_up_overflow) { markDiskEmpty("browser upload too big"); server.send(413,"application/json","{\"error\":\"image too big for the HD ramdisk\"}"); return; }
  if (g_up_recv == 0) { server.send(400,"application/json","{\"error\":\"empty upload\"}"); return; }
  g_loaded_name = g_up_name;
  webFinishLoad();
  String j = "{\"name\":\""; j += jsonEsc(g_up_name); j += "\",\"bytes\":"; j += String((unsigned)g_up_recv); j += "}";
  server.send(200,"application/json", j);
}
static void apiWifiStatus(){
  bool sta = (WiFi.status()==WL_CONNECTED);
  String ip = (g_webmode==1) ? WiFi.localIP().toString() : String(AP_IP);
  String j = "{\"sta\":"; j += (sta?"true":"false");
  j += ",\"ip\":\""; j += ip; j += "\",\"sta_ssid\":\""; j += jsonEsc(g_ssid); j += "\"}";
  server.send(200,"application/json", j);
}
static void apiConfig(){
  String j = "{";
  j += "\"DISPLAY\":\"SUPERMINI\",\"LASTMODE\":\"ADF\",";
  j += "\"MDNS_NAME\":\"" MDNS_NAME "\",";
  j += "\"WIFI_ENABLED\":\"1\",";
  j += "\"WIFI_CLIENT_ENABLED\":\""; j += (g_webmode==1?"1":"0"); j += "\",";
  j += "\"WIFI_CLIENT_SSID\":\""; j += jsonEsc(g_ssid); j += "\",";
  j += "\"WIFI_CLIENT_PASS\":\"\",";
  j += "\"FTP_ENABLED\":\"0\",\"DAV_ENABLED\":\"0\"";
  j += "}";
  server.send(200,"application/json", j);
}
static void apiConfigSave(){
  String ssid = server.arg("WIFI_CLIENT_SSID");
  String pass = server.arg("WIFI_CLIENT_PASS");
  // 1.6.12: the page posts the whole form with the password MASKED (empty). Empty = keep the
  // stored one, and only a real change saves + reboots. Save on the Config tab used to write
  // PASS= blank and strand the dongle on its own AP.
  if (pass.length() == 0) pass = g_pass;
  const bool changed = ssid.length() && (ssid != g_ssid || pass != g_pass);
  if (changed) { saveWifiCfg(ssid, pass); server.send(200,"application/json","{\"status\":\"ok\",\"reboot\":true}"); delay(400); ESP.restart(); return; }
  server.send(200,"application/json","{\"status\":\"ok\"}");
}
static void apiReboot(){ server.send(200,"application/json","{\"status\":\"ok\"}"); delay(300); ESP.restart(); }
static const char* const THEME_NAMES[] = { "NEO","AMIGA_WB2","AMIGA_WB13","PAPER_WHITE","MIDNIGHT","PHOSPHOR","OMEGA_DARK" };   // 1.6.4: one list for /list and /activate | 1.6.11: + NEO (the GTi look; needs the shared webui.h with the NEO preset)
static bool themeKnown(const String& n){ for (auto t : THEME_NAMES) if (n == t) return true; return false; }
static void apiThemesList(){
  String j = "{\"active\":\""; j += g_active_theme;
  j += "\",\"themes\":[";
  for (size_t i = 0; i < sizeof(THEME_NAMES)/sizeof(THEME_NAMES[0]); i++) { if (i) j += ","; j += "\""; j += THEME_NAMES[i]; j += "\""; }
  j += "]}";
  server.send(200,"application/json", j);
}

static void sendDonglePage(){   // size-trim: stored gzipped (dongle_page.h)
  server.sendHeader("Content-Encoding","gzip");
  server.send_P(200, "text/html", (PGM_P)dongle_page_gz, dongle_page_gz_len);
}
static void handleWebUI(){
  // In AP/setup mode (not yet on home WiFi) serve the focused setup portal
  // with the WiFi scan + feedback, not the rich SPA  the SPA is for once
  // the dongle is on the LAN. Fixes "/" opening the wrong page during setup.
  if (g_webmode != 1) { sendDonglePage(); return; }
  server.sendHeader("Content-Encoding","gzip");
  server.send_P(200, "text/html", (PGM_P)webui_gz, webui_gz_len);
}

static void handleRoot(){ sendDonglePage(); }

//  FLEET: per-device identity from the STA MAC 
static String discoId(){ uint8_t m[6] = {0}; esp_read_mac(m, ESP_MAC_WIFI_STA);   // 1.6.8: same value as WiFi.macAddress(), but valid before the radio starts
  char b[13]; snprintf(b,sizeof(b),"%02X%02X%02X%02X%02X%02X",m[0],m[1],m[2],m[3],m[4],m[5]); return String(b); }
// #name: sanitizeName + discoName are defined up top (before statusJson) so
// there is no forward-reference  that avoided arduino's prototype generator
// running and mis-emitting prototypes for the raw-string portal JS.
//  FLEET: "I'm alive" discovery beacon  home-WiFi (STA) only, every 12 s 
// JSON per the Fleet Discovery design; consumed by the app (direct listen) and
// the browser-master (/api/fleet). NOT sent in AP/ESP-NOW mode (no LAN to serve).
static void sendAliveBeacon(){
  if (g_webmode != 1 || WiFi.status() != WL_CONNECTED) return;
  IPAddress ip = WiFi.localIP();
  String j = "{\"gti\":1";
  j += ",\"id\":\"";    j += discoId();      j += "\"";
  j += ",\"name\":\"";  j += discoName();    j += "\"";
  j += ",\"ip\":\"";    j += ip.toString();  j += "\"";
  j += ",\"board\":\"" WEBBY_BOARD "\"";   // 1.6.8: "supermini" or "zero"
  j += ",\"fw\":\"";    j += FW_VERSION;     j += "\"";
  j += ",\"hd\":true";
  j += ",\"port\":80";
  j += ",\"tcp\":";     j += String(TCP_PORT);
  j += ",\"loaded\":";  j += (g_disk_loaded?"true":"false");
  j += ",\"disk\":\"";  j += jsonEsc(g_loaded_name); j += "\"";
  j += "}";
  IPAddress sub = ip; sub[3] = 255;   // subnet-directed + global broadcast (APs vary)
  _disco.beginPacket(sub, GTI_DISCO_PORT);                         _disco.write((const uint8_t*)j.c_str(), j.length()); _disco.endPacket();
  _disco.beginPacket(IPAddress(255,255,255,255), GTI_DISCO_PORT);  _disco.write((const uint8_t*)j.c_str(), j.length()); _disco.endPacket();
}

//  FLEET: peer roster (from beacons) + lowest-MAC master election 
struct FleetPeer { String id, name, ip, fw; bool hd; bool loaded; bool isPanel; uint32_t seen; };   // #rule: isPanel = a screen; a screen always leads, so dongles defer
static FleetPeer g_peers[16]; static int g_peer_n = 0;
static bool      g_is_master = false;
static uint32_t  g_next_elect_ms = 0;
#define FLEET_STALE_MS 40000   // drop a peer unheard for >40 s (~3 missed beacons)

static String jf(const String& s, const char* key){   // tiny "key":"val" / "key":val extractor
  String k = String("\"") + key + "\":";
  int i = s.indexOf(k); if (i < 0) return "";
  i += k.length(); if (i >= (int)s.length()) return "";
  if (s[i]=='"'){ int e=s.indexOf('"', i+1); return e<0 ? String("") : s.substring(i+1, e); }
  int e=i; while(e<(int)s.length() && s[e]!=',' && s[e]!='}') e++;
  return s.substring(i, e);
}
static void fleetUpsert(const String& id,const String& name,const String& ip,const String& fw,bool hd,bool loaded,bool isPanel){
  for(int i=0;i<g_peer_n;i++) if(g_peers[i].id==id){ g_peers[i].name=name; g_peers[i].ip=ip; g_peers[i].fw=fw; g_peers[i].hd=hd; g_peers[i].loaded=loaded; g_peers[i].isPanel=isPanel; g_peers[i].seen=millis(); return; }
  if(g_peer_n<16){ g_peers[g_peer_n].id=id; g_peers[g_peer_n].name=name; g_peers[g_peer_n].ip=ip; g_peers[g_peer_n].fw=fw; g_peers[g_peer_n].hd=hd; g_peers[g_peer_n].loaded=loaded; g_peers[g_peer_n].isPanel=isPanel; g_peers[g_peer_n].seen=millis(); g_peer_n++; }
}
static void fleetPrune(){
  uint32_t now=millis(); int w=0;
  for(int i=0;i<g_peer_n;i++) if(now-g_peers[i].seen < FLEET_STALE_MS){ if(w!=i) g_peers[w]=g_peers[i]; w++; }
  g_peer_n=w;
}
static void pollDisco(){   // read sibling beacons off the discovery socket
  for(int guard=0; guard<8; guard++){
    int sz=_disco.parsePacket(); if(sz<=0) return;
    char buf[600]; int n=_disco.read((uint8_t*)buf, sizeof(buf)-1); if(n<=0) return; buf[n]=0;
    String s(buf); if(s.indexOf("\"gti\":1")<0) continue;
    String id=jf(s,"id"); if(id.length()==0 || id==discoId()) continue;   // ignore self
    fleetUpsert(id, jf(s,"name"), jf(s,"ip"), jf(s,"fw"), jf(s,"hd")=="true", jf(s,"loaded")=="true", jf(s,"role")=="panel");
  }
}
// 1.6.11: the leader answers to gotekomega.local AND keeps its own gotekomega-<mac>.local (a delegated mDNS
// host on the same IP), so a bookmark or a paired screen's lookup of the own name keeps working.
static void mdnsKeepOwnName(){
  if (WiFi.status() != WL_CONNECTED) return;
  mdns_ip_addr_t a = {}; a.addr.type = ESP_IPADDR_TYPE_V4; a.addr.u_addr.ip4.addr = (uint32_t)WiFi.localIP(); a.next = nullptr;
  if (mdns_delegate_hostname_add(discoName().c_str(), &a) == ESP_OK) Serial.printf("[FLEET] also %s.local\n", discoName().c_str());
}
static void doElection(){   // a screen always leads; otherwise the lowest MAC wears the gotekomega hat
  fleetPrune();
  String me=discoId(); bool master=true;
  for(int i=0;i<g_peer_n;i++) if(g_peers[i].isPanel){ master=false; break; }   // #rule: a panel (screen) is present -> it is the leader, dongles defer (stay gotekomega-<mac>.local)
  if(master) for(int i=0;i<g_peer_n;i++) if(g_peers[i].id < me){ master=false; break; }
  if(master!=g_is_master){
    g_is_master=master;
    MDNS.end();
    if(master) MDNS.begin(MDNS_NAME); else MDNS.begin(discoName().c_str());
    MDNS.addService("http","tcp",80);
    if(master) mdnsKeepOwnName();   // 1.6.11: both names on the leader
    Serial.printf("[FLEET] now %s -> %s.local\n", master?"MASTER":"slave", master?MDNS_NAME:discoName().c_str());
  }
}
static void apiFleet(){
  fleetPrune();
  String ip = (g_webmode==1) ? WiFi.localIP().toString() : String(AP_IP);
  String j = "{\"master\":\""; j += (g_is_master ? discoId() : String("")); j += "\",\"self\":\""; j += discoId(); j += "\",\"devices\":[";
  j += "{\"id\":\""; j+=discoId(); j+="\",\"name\":\""; j+=discoName(); j+="\",\"ip\":\""; j+=ip;
  j += "\",\"fw\":\""; j+=FW_VERSION; j+="\",\"hd\":true,\"self\":true,\"master\":"; j+=(g_is_master?"true":"false"); j+="}";
  for(int i=0;i<g_peer_n;i++){
    j += ",{\"id\":\""; j+=g_peers[i].id; j+="\",\"name\":\""; j+=jsonEsc(g_peers[i].name);
    j += "\",\"ip\":\""; j+=g_peers[i].ip; j+="\",\"fw\":\""; j+=jsonEsc(g_peers[i].fw);
    j += "\",\"hd\":"; j+=(g_peers[i].hd?"true":"false"); j+=",\"self\":false,\"master\":false}";
  }
  j += "]}";
  server.send(200,"application/json", j);
}

// 1.6.11: the phone's own "sign in to network" window (captive portal) is a limited browser - no file
// upload, and it closes as soon as you choose to keep the network. A request for any host other than
// this dongle (the phone's internet check: generate_204, hotspot-detect, allawnos, msftconnecttest ...)
// gets a signpost page instead of the app: keep the network, then open 192.168.4.1 in the real browser.
static const char CAPTIVE_HTML[] PROGMEM = R"CAPHTML(<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Gotek OMEGA</title><style>
body{margin:0;background:#0b0e22;color:#e9ecf5;font-family:system-ui,sans-serif;display:flex;justify-content:center}
.c{max-width:420px;margin:32px 16px;padding:22px;background:#181c31;border:1px solid #3b4570;border-radius:16px}
h1{font-size:20px;margin:0 0 4px}.ap{color:#3fe0e8;font-weight:700}p{color:#9aa3c0;line-height:1.45}
ol{padding-left:20px;line-height:1.6}li{margin:8px 0}
.u{display:block;margin:14px 0 6px;padding:14px;border-radius:12px;background:#0b0e22;border:1px solid #3fe0e8;
color:#3fe0e8;font-size:24px;font-weight:700;text-align:center;text-decoration:none;letter-spacing:.5px}
.n{font-size:12px;color:#7d86a6;text-align:center}
</style></head><body><div class="c">
<h1 id="h">Connected</h1><div class="ap" id="ap">GotekOMEGA</div>
<ol><li id="s1">Tap &#8942; / Menu and choose "Use this network as is" (or "Keep Wi-Fi").</li>
<li id="s2">Open your normal browser and go to:</li></ol>
<a class="u" href="http://192.168.4.1/" target="_blank" rel="noopener">192.168.4.1</a>
<p class="n" id="n">This small sign-in window cannot upload disks. Everything works in the browser.</p>
</div><script>
var T={en:["Connected","Tap ⋮ / Menu and choose \"Use this network as is\" (or \"Keep Wi-Fi\").","Open your normal browser and go to:","This small sign-in window cannot upload disks. Everything works in the browser."],
nl:["Verbonden","Tik op ⋮ / Menu en kies \"Netwerk gebruiken zoals het is\" (of \"Wi-Fi behouden\").","Open je gewone browser en ga naar:","Dit kleine inlogvenster kan geen disks uploaden. In de browser werkt alles."],
de:["Verbunden","Tippe auf ⋮ / Menü und wähle \"Netzwerk unverändert nutzen\" (oder \"WLAN behalten\").","Öffne deinen normalen Browser und gehe zu:","Dieses kleine Anmeldefenster kann keine Disketten hochladen. Im Browser geht alles."],
fr:["Connecté","Touchez ⋮ / Menu et choisissez \"Utiliser ce réseau tel quel\" (ou \"Garder le Wi-Fi\").","Ouvrez votre navigateur habituel et allez à :","Cette petite fenêtre de connexion ne peut pas envoyer de disques. Tout marche dans le navigateur."],
it:["Connesso","Tocca ⋮ / Menu e scegli \"Usa questa rete così com'è\" (o \"Mantieni Wi-Fi\").","Apri il tuo browser e vai a:","Questa piccola finestra di accesso non può caricare dischi. Nel browser funziona tutto."],
es:["Conectado","Toca ⋮ / Menú y elige \"Usar esta red tal cual\" (o \"Mantener Wi-Fi\").","Abre tu navegador normal y ve a:","Esta pequeña ventana de acceso no puede subir discos. En el navegador funciona todo."],
pl:["Połączono","Dotknij ⋮ / Menu i wybierz \"Użyj tej sieci bez zmian\" (lub \"Zachowaj Wi-Fi\").","Otwórz zwykłą przeglądarkę i wejdź na:","To małe okno logowania nie wyśle dysków. W przeglądarce działa wszystko."],
cs:["Připojeno","Klepněte na ⋮ / Menu a zvolte \"Použít síť tak, jak je\" (nebo \"Ponechat Wi-Fi\").","Otevřete běžný prohlížeč a přejděte na:","Toto malé přihlašovací okno neumí nahrát disky. V prohlížeči funguje vše."]};
var L=(localStorage.getItem("lang")||navigator.language||"en").slice(0,2).toLowerCase();var t=T[L]||T.en;
document.getElementById("h").textContent=t[0];document.getElementById("s1").textContent=t[1];
document.getElementById("s2").textContent=t[2];document.getElementById("n").textContent=t[3];
document.getElementById("ap").textContent="%AP%";
</script></body></html>)CAPHTML";
static bool isPortalProbe(){   // AP mode only; our own address and *.local names get the real pages
  if (!g_dns_up) return false;
  String h = server.hostHeader(); int c = h.indexOf(':'); if (c >= 0) h = h.substring(0, c);
  h.toLowerCase();
  return h.length() && h != "192.168.4.1" && !h.endsWith(".local");
}
static void sendCaptive(){
  String pg = FPSTR(CAPTIVE_HTML); pg.replace("%AP%", g_ap_name);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html", pg);
}

static void startWebServer(){
  // PANEL: Dimmy's SPA is the front door; the old simple page stays at /classic.
  server.on("/", HTTP_GET, [](){ if (isPortalProbe()) { sendCaptive(); return; } handleWebUI(); });   // 1.6.11: signpost in the phone's sign-in window
  server.on("/classic", HTTP_GET, handleRoot);
  server.on("/app", HTTP_GET, [](){ server.sendHeader("Content-Encoding","gzip"); server.send_P(200, "text/html", (PGM_P)webui_gz, webui_gz_len); });   // 1.6.9: the full shared interface in any mode (linked from the setup page)
  // legacy simple endpoints (kept  the /classic page and any old clients use them)
  server.on("/status", HTTP_GET, [](){ server.send(200,"application/json", statusJson()); });
  server.on("/upload", HTTP_POST, handleUploadDone, handleUpload);
  server.on("/eject", HTTP_POST, handleEjectWeb);
  server.on("/scan",     HTTP_GET,  handleScan);
  server.on("/savewifi", HTTP_POST, handleSaveWifi);
  server.on("/espnow", HTTP_POST, handleEspnowWeb);
  // PANEL: /api/* shim the SPA calls (has_sd:false dongle surface)
  server.on("/api/system/info",  HTTP_GET,  apiSystemInfo);
  server.on("/api/disk/status",  HTTP_GET,  apiDiskStatus);
  server.on("/api/disk/unload",  HTTP_POST, apiDiskUnload);
  server.on("/api/games/upload", HTTP_POST, apiGamesUploadDone, handleUpload);
  server.on("/api/games/list",   HTTP_GET,  [](){ server.send(200,"application/json","{\"games\":[]}"); });
  server.on("/api/wifi/status",  HTTP_GET,  apiWifiStatus);
  server.on("/api/config",       HTTP_GET,  apiConfig);
  server.on("/api/config",       HTTP_POST, apiConfigSave);
  server.on("/api/system/reboot",HTTP_POST, apiReboot);
  server.on("/api/system/ota",   HTTP_POST, onOtaDone, onOtaUpload);   // OMEGAWARE: web OTA
  // PANEL: theme gallery (built-in presets, no card needed)
  server.on("/api/themes/list", HTTP_GET,  apiThemesList);
  server.on("/api/fleet",       HTTP_GET,  apiFleet);        // FLEET roster
  // API misses -> clean 404 JSON (SPA tolerates it); everything else -> captive portal to /
  server.onNotFound([](){
    String u = server.uri();
    // theme gallery activate: /api/themes/<name>/activate  - 1.6.4 (#24): POST only + whitelist
    // (was: any method, any name -> LittleFS write on a GET)
    if (u.startsWith("/api/themes/") && u.endsWith("/activate")) {
      if (server.method() != HTTP_POST) { server.send(405,"application/json","{\"error\":\"POST only\"}"); return; }
      int a = 12; int b = u.lastIndexOf("/activate");   // 12 = strlen("/api/themes/")
      String nm = (b > a) ? u.substring(a, b) : String("");
      if (!themeKnown(nm)) { server.send(400,"application/json","{\"error\":\"unknown theme\"}"); return; }
      g_active_theme = nm; saveTheme(g_active_theme);   // PANEL: remember across reboots
      server.send(200,"application/json","{\"status\":\"ok\"}"); return;
    }
    if (u.startsWith("/api/")) { server.send(404,"application/json","{\"error\":\"not found\"}"); return; }
    if (isPortalProbe()) { sendCaptive(); return; }   // 1.6.11: the phone's internet check -> signpost page
    server.sendHeader("Location","/"); server.send(302,"text/plain","");   // captive portal
  });
  server.begin();
}

//  base BOOT-button owner-lock gesture (unchanged) 
#define BOOT_PAIR_MS   5000
#define BOOT_WIPE_MS   15000
#define BOOT_REVERT_MS   3000    // WEBBY: in Wi-Fi mode, hold BOOT >=3s to revert to ESP-NOW (keeps creds)
#define BOOT_WIFIWIPE_MS 10000   // WEBBY: hold BOOT >=10s to also wipe saved SSID/PASS
#define ENROLL_WIN_MS  30000
static bool serviceBootButton(){
  uint32_t now = millis();
  static bool held_prev = false; static uint32_t held_t0 = 0; static uint8_t upCount = 8;
  bool raw = (digitalRead(BOOT_PIN) == LOW);
  if (raw) upCount = 0; else if (upCount < 250) upCount++;
  bool down = (upCount < 8); bool phase = (now / 180) & 1; static bool wipedHold = false;

  // WEBBY: when configured for home Wi-Fi, BOOT-hold is the physical escape back to
  // ESP-NOW (mirrors the web "Disconnect Wi-Fi -> ESP-NOW" button). >=3s flips to
  // ESP-NOW keeping the saved creds; >=10s also wipes SSID/PASS. Owner-lock pairing
  // is an ESP-NOW-mode activity, so it keeps BOOT only while already in ESP-NOW mode.
  if (g_modeStr == "WIFI") {
    static uint32_t wrt0 = 0; static bool wrPrev = false;
    if (down) {
      if (!wrPrev) { wrPrev = true; wrt0 = now; }
      uint32_t wheld = now - wrt0;
      if      (wheld >= BOOT_WIFIWIPE_MS) setLeds(true, false);    // solid red = wipe armed
      else if (wheld >= BOOT_REVERT_MS)   setLeds(phase, false);   // red blink  = flip armed
      else                                setLeds(false, phase);   // blue blink = holding
      return true;
    }
    if (wrPrev) {
      uint32_t wheld = now - wrt0; wrPrev = false; setLeds(false, false);
      if (wheld >= BOOT_WIFIWIPE_MS) {
        wipeWifiCreds();
        oledStatus("Gotek OMEGA " FW_VERSION, "Wi-Fi WIPED", "Creds cleared", "Rebooting...");
        delay(600); ESP.restart();
      } else if (wheld >= BOOT_REVERT_MS) {
        setModeEspnow();
        oledStatus("Gotek OMEGA " FW_VERSION, "Wi-Fi OFF", "Back to ESP-NOW", "Rebooting...");
        delay(600); ESP.restart();
      }
      return true;
    }
    return false;
  }

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
  // Unique AP name per device: two dongles in one room both broadcasting
  // "GotekOMEGA" is impossible to tell apart (you configure the wrong one).
  // 1.6.8: WiFi.macAddress() asks the station interface, which only exists once the radio's
  // start event has run - straight after WiFi.mode() it doesn't yet, the call fails and apm stayed
  // zero, so EVERY dongle was "GotekOMEGA-0000". esp_read_mac() reads the chip's own number and needs
  // no radio. SoftAP MAC = the BSSID the dongle broadcasts = its identity (Wire Protocol Registry).
  uint8_t apm[6] = {0}; esp_read_mac(apm, ESP_MAC_WIFI_SOFTAP);
  char apid[24]; snprintf(apid, sizeof(apid), "%s-%02X%02X", AP_SSID, apm[4], apm[5]);
  strncpy(g_ap_name, apid, sizeof(g_ap_name)-1);
  char apline[32]; snprintf(apline, sizeof(apline), "AP: %s", apid);
  WiFi.softAP(apid, AP_PASS, ESPNOW_CHANNEL);
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
  oledStatus("Gotek OMEGA " FW_VERSION, apline, WiFi.softAPIP().toString(), "Broadcasting...");
  uint32_t t0 = millis();
  while (millis()-t0 < (uint32_t)(_paired ? 8000 : 3000)) {
    PktHello hello = {}; hello.type = PKT_PAIR_HELLO; WiFi.softAPmacAddress(hello.mac);
    strncpy(hello.ip, AP_IP, 15); hello.pad[0] = SAVE_PROTO_VER;
    if (_bcastPeer) _bcastPeer->send_pkt((uint8_t*)&hello, sizeof(hello));
    if (_wavePeer)  _wavePeer->send_pkt((uint8_t*)&hello, sizeof(hello));
    RxPkt pkt; while (xQueueReceive(_rxQueue, &pkt, 0) == pdTRUE) handleESPNOW(pkt.data, pkt.len, pkt.src);
    WiFiClient c = _tcpServer.accept(); if (c) handleTCPClient(c);
    server.handleClient(); dnsServer.processNextRequest();
    delay(120);
    if (_paired && _wavePeer) break;
  }
  setLeds(false, g_disk_loaded);
}

void setup() {
  Serial.begin(115200); delay(200);
  ledInit(); pinMode(BOOT_PIN, INPUT_PULLUP);
  _rxQueue = xQueueCreate(32, sizeof(RxPkt));

  // PSRAM ramdisk
  g_disk = (uint8_t*)ps_malloc((size_t)TOTAL_SECTORS*SECTOR_SIZE);
  if (!g_disk) g_disk = (uint8_t*)malloc((size_t)TOTAL_SECTORS*SECTOR_SIZE);
  if (!g_disk) { Serial.println("[FATAL] RAM disk alloc failed \u2014 enable PSRAM (QSPI) in the board menu, or the disk is too big for this board's PSRAM"); oledStatus("FATAL: no RAM","Set PSRAM=QSPI","in board menu",""); while(true){ledRed(true);delay(200);ledRed(false);delay(200);} }
  build_volume("DISK.ADF", ADF_DEFAULT_SIZE);

  // USB MSC
  USB.onEvent(usbEventCb);
  MSC.vendorID("ESP32"); MSC.productID("GOTEK"); MSC.productRevision("1.0");
  MSC.onRead(onRead); MSC.onWrite(onWrite); MSC.mediaPresent(true);
  MSC.begin(TOTAL_SECTORS, SECTOR_SIZE); USB.begin();
  hardDetach();

  loadConfig();       // enrolled owners (used in ESPNOW mode)
  loadWifiCfg();      // WEBBY: home creds + mode
  loadTheme();        // PANEL: last-chosen web theme

  // WEBBY: pick the radio mode.
  bool tryWifi = (g_modeStr == "WIFI" && g_ssid.length() > 0);
  g_webmode = 0;
  if (tryWifi) {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);   // OMEGAWARE: kill modem power-save  as the disk RECEIVER, a sleeping STA delays TCP ACKs and drops LAN flings mid-transfer; off = reliable flings
    WiFi.setHostname(discoName().c_str());   // FLEET: router table shows gotek-xxxx, not "espressif"
    // Two attempts: ESP32 STA frequently misses the first join and takes the retry.
    for (int attempt = 0; attempt < 2 && WiFi.status() != WL_CONNECTED; attempt++) {
      if (attempt) { WiFi.disconnect(true); delay(400); }
      WiFi.begin(g_ssid.c_str(), g_pass.c_str());
      uint32_t t0 = millis();
      while (WiFi.status() != WL_CONNECTED && millis()-t0 < 12000) { ledRed(((millis()/200)&1)); delay(50); }
    }
    ledRed(false);
    g_join_failed = (WiFi.status() != WL_CONNECTED);
    if (WiFi.status() == WL_CONNECTED) {
      g_webmode = 1;
      g_is_master = false;
      if (MDNS.begin(discoName().c_str())) MDNS.addService("http","tcp",80);   // start as self; election may promote to gotekomega.local
      _tcpServer.begin();        // app can reach us over the LAN too
      _disco.begin(GTI_DISCO_PORT);   // FLEET: open discovery socket
      g_next_alive_ms = 0;            // FLEET: beacon on the next loop tick
      g_next_elect_ms = millis() + 2000;   // FLEET: first election shortly after join
      setLeds(false, true);      // blue = connected/ready
      Serial.printf("[WIFI] joined %s  IP %s  host %s  (gotekomega.local)\n", g_ssid.c_str(), WiFi.localIP().toString().c_str(), discoName().c_str());
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
  ledTick();
  server.handleClient();
  if (g_dns_up) dnsServer.processNextRequest();

  // ESP-NOW control queue (only meaningful in AP/ESP-NOW mode; harmless otherwise)
  RxPkt pkt; while (xQueueReceive(_rxQueue, &pkt, 0) == pdTRUE) handleESPNOW(pkt.data, pkt.len, pkt.src);

  // TCP app transfers (begun in both modes)
  WiFiClient client = _tcpServer.accept();
  if (client) handleTCPClient(client);

  bool dirtyWaiting = (g_dirty_count > 0 && g_last_write_ms && (millis()-g_last_write_ms) > SAVE_SETTLE_MS);
  if (dirtyWaiting && millis() > g_next_beacon_ms) { sendDirtyBeacon(); g_next_beacon_ms = millis()+SAVE_BEACON_MS; }
  if (millis() > g_next_status_ms) { sendStatusBeacon(); g_next_status_ms = millis()+STATUS_BEACON_MS; }
  if (g_webmode==1 && millis() > g_next_alive_ms) { sendAliveBeacon(); g_next_alive_ms = millis()+ALIVE_BEACON_MS; }   // FLEET beacon
  if (g_webmode==1) pollDisco();                                                                                       // FLEET listen
  if (g_webmode==1 && millis() > g_next_elect_ms) { doElection(); g_next_elect_ms = millis()+4000; }                    // FLEET elect

  if (!serviceBootButton()) {
    if (dirtyWaiting)          setLeds(true, true);
    else if (g_webmode==0 && !_paired) setLeds(false, (millis()/1000)&1);
  }
  delay(2);
}
