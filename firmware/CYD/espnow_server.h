// espnow_server.h — ESP-NOW pairing + WiFi TCP disk transfer, CYD side
// Identical interface to Waveshare version — only the .cpp changes
#pragma once
#include <Arduino.h>

// ── ESP-NOW packet types ────────────────────────────────────────────────────
#define PKT_PAIR_HELLO    0x05  // XIAO→broadcast: "I exist, my name is XIAO-XXXX"
#define PKT_PAIR_CONFIRM  0x06  // Waveshare→XIAO: "I know your name, session locked"
#define PKT_PAIR_REPLY    0x14  // XIAO→Waveshare: "name confirmed, I'm yours"
#define PKT_DISK_EJECT    0x02  // Waveshare→XIAO: eject disk
#define PKT_XIAO_READY    0x10  // XIAO→Waveshare: ready
#define PKT_XIAO_DONE     0x12  // XIAO→Waveshare: disk loaded OK
#define PKT_XIAO_ERROR    0x13  // XIAO→Waveshare: error

// Shared ramdisk — on CYD we use a heap buffer, not USB MSC
extern uint8_t* g_disk;
#define ESPNOW_SECTOR_SIZE  512
#define ESPNOW_DATA_LBA     11

// XIAO WiFi AP (fixed)
#define XIAO_AP_SSID   "GotekXIAO"
#define XIAO_AP_PASS   "gotek1234"
#define XIAO_AP_IP     "192.168.4.1"
#define XIAO_TCP_PORT  3333

// State flags
extern volatile bool     g_espnow_paired;
extern volatile bool     g_espnow_xiao_ready;
extern volatile bool     g_espnow_xiao_done;
extern volatile bool     g_espnow_xiao_error;
extern volatile bool     g_espnow_link_just_established;
extern volatile uint32_t g_espnow_xiao_last_seen;
extern volatile bool     g_espnow_pair_waiting;

// API
void   espnowBegin();
bool   espnowIsPaired();
bool   espnowXiaoOnline();
String espnowGetXiaoMac();
String espnowGetXiaoName();
String espnowGetPendingName();
String espnowGetSSIDLabel();

// Pairing
void   espnowListenForPair();
void   espnowClearPairing();

// Transfer
bool   espnowSendNotify(const String& name, const String& mode, uint32_t size);
bool   espnowSendDisk(uint32_t size);
void   espnowSendEject();
