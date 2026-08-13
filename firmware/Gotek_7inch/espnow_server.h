// espnow_server.h — ESP-NOW pairing + WiFi TCP disk transfer, Waveshare side
#pragma once
#include <Arduino.h>

// ESP-NOW packet types (control only — no data transfer)
#define PKT_PAIR_HELLO   0x05  // Waveshare → broadcast: I exist
#define PKT_PAIR_REPLY   0x14  // XIAO → Waveshare: here's my MAC + IP
#define PKT_DISK_EJECT   0x02  // Waveshare → XIAO: eject disk
#define PKT_XIAO_READY   0x10  // XIAO → Waveshare: WiFi AP up, ready for TCP
#define PKT_XIAO_DONE    0x12  // XIAO → Waveshare: disk loaded to Gotek
#define PKT_XIAO_ERROR   0x13  // XIAO → Waveshare: error
#define PKT_XIAO_DIRTY   0x15  // XIAO → Waveshare: settled unsaved writes exist (v4.8.0 saves)
#define PKT_UNPAIR       0x16  // Waveshare → dongle: forget me (drop this GTi from the owner list)

// Shared ramdisk — defined in main .ino
extern uint8_t* g_disk;
#define ESPNOW_SECTOR_SIZE  512
#define ESPNOW_DATA_LBA     11

// XIAO WiFi AP settings (fixed)
#define DONGLE_AP_SSID   "GotekOMEGA"
#define DONGLE_AP_PASS   "gotek1234"
#define DONGLE_AP_IP     "192.168.4.1"
#define DONGLE_TCP_PORT  3333

// State flags
extern volatile bool g_espnow_paired;
extern volatile bool g_espnow_xiao_ready;
extern volatile bool g_espnow_xiao_done;
extern volatile bool g_espnow_xiao_error;
extern volatile bool g_espnow_link_just_established;
extern volatile uint32_t g_espnow_xiao_last_seen; // millis() of last packet from XIAO

bool espnowXiaoOnline(); // true if heard from XIAO in last 30s

// ── Save writeback (v4.8.0) ─────────────────────────────────────────────────
extern volatile uint8_t  g_espnow_dongle_caps;   // pad[0] of PAIR_REPLY: save proto version (0 = old dongle)
extern volatile uint8_t  g_espnow_dongle_board;  // pad[1] of PAIR_REPLY: 1 = HD-capable (XIAO 8MB, 2MB ramdisk); 0 = DD-only / old dongle
extern volatile uint32_t g_espnow_load_id;       // load_id from the last FLING's TCP ack (0 = old dongle)
extern volatile bool     g_espnow_dirty;         // dongle beaconed unsaved writes
extern volatile uint32_t g_espnow_dirty_loadid;  // which load the dirty sectors belong to
extern volatile uint16_t g_espnow_dirty_count;   // how many sectors
extern volatile uint32_t g_espnow_dirty_size;    // image size on the dongle
// Fetch dirty sectors over TCP (same radio dance as a FLING). The persist
// callback runs between CRC-verify and the ack: return true only once the save
// is safely on SD — that is what lets the dongle clear its dirty bits.
// packed = the dirty sectors in ascending-LBA order (k-th set bit in map = k-th sector).
typedef bool (*SavePersistCb)(uint32_t load_id, uint32_t img_size,
                              const uint8_t* map, uint16_t mapLen,
                              const uint8_t* packed, uint32_t nSectors);
bool espnowFetchSave(SavePersistCb persist);

// API
void   espnowBegin();
void   espnowBroadcastHello();
bool   espnowIsPaired();
String espnowGetXiaoMac();
String espnowGetSSIDLabel();

// Multi-dongle scan (Path 1)
void   espnowScanBegin();
void   espnowScanEnd();
int    espnowScanCount();
String espnowScanGetMac(int i);
bool   espnowScanSelect(int i);
void   espnowSetScanCap(int n);   // runtime cap on discovered dongles (CONFIG.TXT CAP=)
void   espnowScanMacBytes(int i, uint8_t* out);   // raw 6-byte MAC of a scanned dongle
void   espnowSendUnpair(const uint8_t* mac);       // tell a dongle to forget this GTi (unicast)
void   espnowForgetActive(const uint8_t* mac);     // clear local pairing if mac is the active dongle

// Transfer — sends via WiFi TCP, uses ESP-NOW only for DONE/ERROR reply
bool   espnowSendNotify(const String& name, const String& mode, uint32_t size);
bool   espnowSendDisk(uint32_t size);
bool   espnowSendDiskTo(const uint8_t* mac, uint32_t size);   // multicast: send to one dongle by MAC
void   espnowSendEject();
