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

// Shared ramdisk — defined in main .ino
extern uint8_t* g_disk;
#define ESPNOW_SECTOR_SIZE  512
#define ESPNOW_DATA_LBA     11

// XIAO WiFi AP settings (fixed)
#define XIAO_AP_SSID     "GotekXIAO"
#define XIAO_AP_PASS     "gotek1234"
#define XIAO_AP_IP       "192.168.4.1"
#define XIAO_TCP_PORT    3333

// State flags
extern volatile bool g_espnow_paired;
extern volatile bool g_espnow_xiao_ready;
extern volatile bool g_espnow_xiao_done;
extern volatile bool g_espnow_xiao_error;
extern volatile bool g_espnow_link_just_established;
extern volatile uint32_t g_espnow_xiao_last_seen; // millis() of last packet from XIAO

bool espnowXiaoOnline(); // true if heard from XIAO in last 30s

// API
void   espnowBegin();
void   espnowBroadcastHello();
bool   espnowIsPaired();
String espnowGetXiaoMac();
String espnowGetSSIDLabel();

// Transfer — sends via WiFi TCP, uses ESP-NOW only for DONE/ERROR reply
bool   espnowSendNotify(const String& name, const String& mode, uint32_t size);
bool   espnowSendDisk(uint32_t size);
void   espnowSendEject();
