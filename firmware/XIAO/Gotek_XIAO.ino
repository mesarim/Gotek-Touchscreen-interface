// Gotek_XIAO.ino — XIAO ESP32-S3 ramdisk bridge for Gotek
// ESP-NOW: pairing and control only
// WiFi SoftAP + TCP: reliable disk data transfer
//
// Board          : XIAO_ESP32S3
// USB Mode       : USB-OTG (TinyUSB)
// USB CDC on Boot: *** DISABLED ***
// PSRAM          : OPI PSRAM
// Flash          : 8MB, Partition: Default with spiffs
// ANTENNA        : *** PLUG IT IN ***

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
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define FW_VERSION     "v3.2.0-xiao"
#define ESPNOW_CHANNEL 6
#define LED_PIN        21

// AP settings — XIAO hosts this for file transfer
#define AP_SSID     "GotekXIAO"
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

#pragma pack(push,1)
struct PktHello  { uint8_t type; uint8_t mac[6]; char ip[16]; uint8_t pad[227]; };
struct PktSimple { uint8_t type; uint8_t pad[249]; };
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

// OLED
static Adafruit_SSD1306 oled(128, 32, &Wire, -1);
static bool g_oled_ok = false;

static void oledStatus(const String& l0, const String& l1,
                        const String& l2, const String& l3) {
  if (!g_oled_ok) return;
  oled.clearDisplay(); oled.setTextColor(SSD1306_WHITE); oled.setTextSize(1);
  auto pr = [&](uint8_t line, const String& s) {
    oled.setCursor(0, line*8);
    String t = s; if (t.length() > 21) t = t.substring(0,21); oled.print(t);
  };
  pr(0,l0); pr(1,l1); pr(2,l2); pr(3,l3); oled.display();
}
static void oledProgress(uint32_t done, uint32_t total) {
  if (!g_oled_ok) return;
  oled.fillRect(0,24,128,8,SSD1306_BLACK);
  int w = total>0 ? (int)((float)done/total*128) : 0;
  oled.fillRect(0,27,w,4,SSD1306_WHITE);
  oled.drawRect(0,27,128,4,SSD1306_WHITE);
  oled.display();
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
  memcpy(g_disk+s, buf, n); return (int32_t)n;
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

    // Reply with our MAC and AP IP
    PktHello reply = {};
    reply.type = PKT_PAIR_REPLY;
    WiFi.softAPmacAddress(reply.mac);  // use AP MAC
    strncpy(reply.ip, AP_IP, 15);
    XiaoPeer* dst = _wavePeer ? _wavePeer : _bcastPeer;
    if (dst) dst->send_pkt((uint8_t*)&reply, sizeof(reply));

    // Save config
    if (LittleFS.begin(true)) {
      File f = LittleFS.open("/XIAO_CONFIG.TXT", "w");
      if (f) { f.printf("WAVE_MAC=%s\n", macToStr(_wave_mac).c_str()); f.close(); }
    }
    oledStatus("Gotek XIAO " FW_VERSION, "Paired!", macToStr(_wave_mac), "AP: " AP_SSID);
    return;
  }

  if (type == PKT_DISK_EJECT) {
    if (g_disk_loaded) { hardDetach(); g_disk_loaded=false; }
    digitalWrite(LED_PIN, LOW);
    oledStatus("Gotek XIAO " FW_VERSION, "Ejected", "", "Ready");
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
static bool       _apRunning = false;

static void startAP() {
  if (_apRunning) return;
  // Stop ESP-NOW WiFi first, start AP
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS, ESPNOW_CHANNEL);
  delay(200);
  _tcpServer.begin();
  _apRunning = true;
  Serial.printf("[AP] Started: %s  IP: %s  Port: %d\n", AP_SSID, AP_IP, TCP_PORT);
  oledStatus("Gotek XIAO " FW_VERSION, "WiFi AP ready", AP_SSID, "waiting for WS...");
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
  Serial.printf("[TCP] Expecting %u bytes\n", size);

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

  Serial.printf("[TCP] Received %u / %u bytes\n", received, size);

  if (received == size) {
    // Send OK response
    client.write((uint8_t)0x01);
    client.flush();
    delay(100);
    client.stop();

    // Attach to Gotek
    if (g_disk_loaded) hardDetach();
    hardAttach();
    g_disk_loaded = true;
    digitalWrite(LED_PIN, HIGH);

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
    Serial.printf("[TCP] Transfer incomplete: %u/%u\n", received, size);
    sendSimple(PKT_XIAO_ERROR);
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Wire.begin();
  _rxQueue = xQueueCreate(32, sizeof(RxPkt));
  g_oled_ok = oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (g_oled_ok) {
    oled.clearDisplay(); oled.setTextSize(1); oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(0,0); oled.print("Gotek XIAO " FW_VERSION);
    oled.setCursor(0,8); oled.print("Booting...");
    oled.display();
  }

  // PSRAM ramdisk
  g_disk = (uint8_t*)ps_malloc((size_t)TOTAL_SECTORS*SECTOR_SIZE);
  if (!g_disk) g_disk = (uint8_t*)malloc((size_t)TOTAL_SECTORS*SECTOR_SIZE);
  if (!g_disk) {
    oledStatus("FATAL: no RAM","PSRAM=OPI PSRAM","check settings","");
    while(true){digitalWrite(LED_PIN,HIGH);delay(200);digitalWrite(LED_PIN,LOW);delay(200);}
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
  _apRunning = true;
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
  oledStatus("Gotek XIAO " FW_VERSION, "AP: " AP_SSID, WiFi.softAPIP().toString(), "Broadcasting...");

  uint32_t t0 = millis();
  int dots = 0;
  while (millis()-t0 < 30000) {
    PktHello hello = {};
    hello.type = PKT_PAIR_HELLO;
    WiFi.softAPmacAddress(hello.mac);
    strncpy(hello.ip, AP_IP, 15);
    if (_bcastPeer) _bcastPeer->send_pkt((uint8_t*)&hello, sizeof(hello));
    if (_wavePeer)  _wavePeer->send_pkt((uint8_t*)&hello, sizeof(hello));
    dots++;

    // Drain ESP-NOW queue
    RxPkt pkt;
    while (xQueueReceive(_rxQueue, &pkt, 0) == pdTRUE)
      handleESPNOW(pkt.data, pkt.len);

    // Check for TCP clients while pairing
    WiFiClient client = _tcpServer.available();
    if (client) handleTCPClient(client);

    if (dots % 5 == 0)
      oledStatus("Gotek XIAO " FW_VERSION,
                 "AP: " AP_SSID,
                 WiFi.softAPIP().toString(),
                 "ping " + String(dots));
    delay(500);

    if (_paired && _wavePeer) {
      PktHello confirm = {};
      confirm.type = PKT_PAIR_REPLY;
      WiFi.softAPmacAddress(confirm.mac);
      strncpy(confirm.ip, AP_IP, 15);
      _wavePeer->send_pkt((uint8_t*)&confirm, sizeof(confirm));
      break;
    }
  }

  if (_paired) {
    oledStatus("** PAIRED **", macToStr(_wave_mac), "AP: " AP_SSID, "Ready for disk");
    digitalWrite(LED_PIN, HIGH);
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
  WiFiClient client = _tcpServer.available();
  if (client) handleTCPClient(client);

  // LED blink if not paired
  if (!_paired) digitalWrite(LED_PIN, (millis()/1000)&1 ? HIGH : LOW);

  delay(5);
}
