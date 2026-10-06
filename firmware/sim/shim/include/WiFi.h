#pragma once
#include <Arduino.h>
#include "IPAddress.h"
// sim: no radio. Home Wi-Fi never connects (the web panel / WebDAV / home-WiFi sends say so, as on a board out of range).
typedef enum { WL_NO_SHIELD = 255, WL_IDLE_STATUS = 0, WL_NO_SSID_AVAIL, WL_SCAN_COMPLETED, WL_CONNECTED, WL_CONNECT_FAILED, WL_CONNECTION_LOST, WL_DISCONNECTED } wl_status_t;
typedef enum { WIFI_OFF = 0, WIFI_STA, WIFI_AP, WIFI_AP_STA } wifi_mode_t;
#define WIFI_MODE_NULL WIFI_OFF
#define WIFI_MODE_STA WIFI_STA
#define WIFI_MODE_AP WIFI_AP
#define WIFI_MODE_APSTA WIFI_AP_STA
class WiFiClass {
  wifi_mode_t _m = WIFI_OFF;
public:
  wl_status_t status() { return WL_DISCONNECTED; }
  bool mode(wifi_mode_t m) { _m = m; return true; }
  wifi_mode_t getMode() { return _m; }
  bool persistent(bool) { return true; }
  bool setAutoReconnect(bool) { return true; }
  wl_status_t begin(const char *, const char * = NULL, int32_t = 0, const uint8_t * = NULL, bool = true) { return WL_DISCONNECTED; }
  bool disconnect(bool = false, bool = false) { return true; }
  IPAddress localIP() { return IPAddress(0,0,0,0); }
  IPAddress softAPIP() { return IPAddress(192,168,4,1); }
  int16_t scanNetworks(bool = false, bool = false, bool = false, uint32_t = 300, uint8_t = 0, const char * = nullptr, const uint8_t * = nullptr);
  void scanDelete() {}
  String SSID(uint8_t i);
  String SSID() { return String(""); }
  int32_t RSSI(uint8_t i);
  int32_t RSSI() { return 0; }
  String macAddress() { return String("3C:84:27:C0:58:B0"); }
  String softAPmacAddress() { return String("3C:84:27:C0:58:B1"); }
  int32_t channel() { return 1; }
  bool softAP(const char *, const char * = NULL, int = 1, int = 0, int = 4) { return true; }
  bool softAPdisconnect(bool = false) { return true; }
  bool setHostname(const char *) { return true; }
};
extern WiFiClass WiFi;
