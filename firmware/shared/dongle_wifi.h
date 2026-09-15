#pragma once
#include <WiFi.h>

inline bool gotekDongleSSID(const String& ssid) {
  return ssid == "GotekOMEGA" || ssid.startsWith("GotekOMEGA-");
}

inline String gotekSSIDForBSSID(const uint8_t* mac) {
  String ssid = "GotekOMEGA"; // legacy fallback if the scan misses a beacon
  int n = WiFi.scanNetworks(false, true, false, 300, 6);
  for (int i = 0; i < n; ++i) {
    if (WiFi.BSSID(i) && !memcmp(WiFi.BSSID(i), mac, 6) &&
        gotekDongleSSID(WiFi.SSID(i))) {
      ssid = WiFi.SSID(i); break;
    }
  }
  WiFi.scanDelete();
  return ssid;
}

inline void gotekBeginDongle(const uint8_t* mac, const char* pass) {
  String ssid = gotekSSIDForBSSID(mac);
  WiFi.begin(ssid.c_str(), pass, 6, mac);
}
