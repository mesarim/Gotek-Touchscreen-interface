#pragma once
#include <WiFi.h>
#include <ESPmDNS.h>

namespace GotekHome {
static bool enabled = false, borrowed = false, mdnsOwned = false;
static String ssid, pass, ip;
inline void configure(bool on, const String& network, const String& password,
                      const String& address) {
  enabled=on; ssid=network; pass=password; ip=address;
}
inline bool begin(String& address) {
  borrowed=WiFi.status()==WL_CONNECTED && WiFi.SSID()==ssid;
  mdnsOwned=false;
  if (!ssid.length()) return false;
  if (!borrowed) {
    WiFi.mode(WIFI_STA); WiFi.persistent(false); WiFi.setAutoReconnect(false);
    WiFi.disconnect(false,true); delay(100);
    WiFi.begin(ssid.c_str(),pass.c_str());
    uint32_t start=millis();
    while(WiFi.status()!=WL_CONNECTED && millis()-start<15000)delay(50);
    if(WiFi.status()!=WL_CONNECTED)return false;
  }
  address=ip;
  // Respect an explicitly selected address in a fleet. Use mDNS for discovery
  // only when no address is configured; leave the web server's mDNS alone.
  if(!address.length()) {
    if(!borrowed)mdnsOwned=MDNS.begin("gti-remote");
    IPAddress found=MDNS.queryHost("gotekomega",2500);
    if((uint32_t)found)address=ip=found.toString();
  }
  return address.length()>0;
}
inline void end(bool keepRadio = false) {
  if(mdnsOwned)MDNS.end();
  // P4 keeps its hosted C6 radio initialized across transport changes.
  if(!borrowed){WiFi.disconnect();if(!keepRadio)WiFi.mode(WIFI_OFF);}
}
}
