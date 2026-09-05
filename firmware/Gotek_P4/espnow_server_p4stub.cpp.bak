// espnow_server_p4stub.cpp — P4 wireless STUB.
// ESP-NOW does NOT link in stock arduino-esp32 for the ESP32-P4 (no radio; needs
// esp_wifi_remote + esp-hosted + updated C6 firmware). To let the JC 5.8.6 UI build
// and run standalone on the P4, every espnow_server API is stubbed to a no-op here.
// Stage 3 replaces this with the WiFi/TCP transport to Webby dongles (compiles today).
#include "espnow_server.h"

// ── State globals (all "no dongle") ──
volatile bool     g_espnow_paired = false;
volatile bool     g_espnow_xiao_ready = false;
volatile bool     g_espnow_xiao_done = false;
volatile bool     g_espnow_xiao_error = false;
volatile bool     g_espnow_link_just_established = false;
volatile uint32_t g_espnow_xiao_last_seen = 0;
volatile bool     g_dongle_loaded = false;
volatile uint32_t g_dongle_load_id = 0;
volatile uint32_t g_dongle_img_size = 0;
volatile uint8_t  g_espnow_dongle_caps = 0;
volatile uint8_t  g_espnow_dongle_board = 0;
volatile uint32_t g_espnow_load_id = 0;
volatile bool     g_espnow_dirty = false;
volatile uint32_t g_espnow_dirty_loadid = 0;
volatile uint16_t g_espnow_dirty_count = 0;
volatile uint32_t g_espnow_dirty_size = 0;

bool   espnowXiaoOnline(){ return false; }
bool   espnowFetchSave(SavePersistCb){ return false; }

void   espnowBegin(){}
void   espnowBroadcastHello(){}
bool   espnowIsPaired(){ return false; }
String espnowGetXiaoMac(){ return String(); }
String espnowGetSSIDLabel(){ return String("(no radio on P4)"); }

void   espnowScanBegin(){}
void   espnowScanEnd(){}
int    espnowScanCount(){ return 0; }
String espnowScanGetMac(int){ return String(); }
bool   espnowScanSelect(int){ return false; }
void   espnowSetScanCap(int){}
void   espnowScanMacBytes(int, uint8_t* out){ if(out) for(int i=0;i<6;i++) out[i]=0; }
void   espnowSendUnpair(const uint8_t*){}
void   espnowSendLock(const uint8_t*){}
void   espnowSendUnlock(const uint8_t*){}
void   espnowForgetActive(const uint8_t*){}

bool   espnowSendNotify(const String&, const String&, uint32_t){ return false; }
bool   espnowSendDisk(uint32_t){ return false; }
bool   espnowSendDiskTo(const uint8_t*, uint32_t){ return false; }
bool   espnowSendDiskHome(const String&, const String&, String&, uint32_t){ return false; }
void   espnowSendEject(){}
