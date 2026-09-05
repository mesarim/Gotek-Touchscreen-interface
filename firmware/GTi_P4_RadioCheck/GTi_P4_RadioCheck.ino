// GTi_P4_RadioCheck v3 — proves the P4→C6 WiFi path end to end, ON SCREEN (CDC off).
// Answers, in order, the three questions that decide whether the WiFi-direct transport works:
//   1. Does a BLOCKING WiFi scan see the dongle's "GotekOMEGA" AP?  (lists every AP; flags it)
//   2. Can the P4 JOIN that AP over esp-hosted and get an IP?       (WiFi.begin -> DHCP)
//   3. Can it open the TCP disk socket at 192.168.4.1:3333?         (WiFiClient.connect)
// Whichever step fails is exactly where the transport breaks. Reuses the proven p4_display.
//
// Board menu: ESP32P4 Dev Module | 16MB flash | PSRAM Enabled | 360 MHz | USB-OTG(TinyUSB) | CDC off.

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include "esp_chip_info.h"
#include "esp_system.h"
#include "p4_display.h"
#include "font6x8.h"
#if __has_include(<esp_hosted.h>)
  #include <esp_hosted.h>
  #define HAVE_HOSTED 1
#endif

#define DONGLE_SSID  "GotekOMEGA"
#define DONGLE_PASS  "gotek1234"
#define DONGLE_IP    "192.168.4.1"
#define DONGLE_PORT  3333

// ── tiny landscape text layer (800x480 virtual -> 480x800 panel) ──
#define LCD_W 480
#define LCD_H 800
static uint16_t* fb=nullptr;
static int cx=0, cy=0, ts=1; static uint16_t fg=0xFFFF, bg=0x0000;
static inline void px(int vx,int vy,uint16_t c){ int x=vy, y=(LCD_H-1)-vx; if(x>=0&&x<LCD_W&&y>=0&&y<LCD_H) fb[y*LCD_W+x]=c; }
static void clr(uint16_t c){ for(int i=0;i<LCD_W*LCD_H;i++) fb[i]=c; }
static void putc_(char ch){ if(ch>=32&&ch<=126){ const uint8_t*g=font6x8[ch-32];
    for(int col=0;col<6;col++){ uint8_t b=pgm_read_byte(&g[col]);
      for(int row=0;row<8;row++){ uint16_t c=(b&(1<<row))?fg:bg;
        for(int dy=0;dy<ts;dy++)for(int dx=0;dx<ts;dx++) px(cx+col*ts+dx,cy+row*ts+dy,c);}}}
  cx+=6*ts; }
static void print_(const char*s){ while(*s) putc_(*s++); }
static int Y=6;
static void line(uint16_t col,int size,const char*fmt,...){ char buf[120]; va_list ap; va_start(ap,fmt); vsnprintf(buf,sizeof buf,fmt,ap); va_end(ap);
  ts=size; fg=col; bg=0x0000; cx=6; cy=Y; print_(buf); Y += size*8 + 3; }
static void flush(){ p4disp_present(fb); }

void setup(){
  fb=(uint16_t*)ps_malloc((size_t)LCD_W*LCD_H*2);
  if(!fb) while(1) delay(1000);
  p4disp_init(); p4disp_backlight(true);
  clr(0x0000);

  line(0xFD20,2,"P4 RADIO CHECK v3");
  Y+=2;

  // ── C6 version (confirm the self-update stuck) ──
  WiFi.mode(WIFI_STA);
  uint8_t mac[6]={0}; WiFi.macAddress(mac);
  line(0x07FF,1,"P4 STA MAC %02X:%02X:%02X:%02X:%02X:%02X", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
#ifdef HAVE_HOSTED
  { esp_hosted_coprocessor_fwver_t v;
    if(esp_hosted_get_coprocessor_fwversion(&v)==ESP_OK)
      line(0x07E0,1,"C6 hosted FW: v%d.%d.%d  (want 2.12.13)", v.major1, v.minor1, v.patch1);
    else line(0xFC00,1,"C6 hosted FW: query error"); }
#endif
  Y+=2;

  // ── Q1: BLOCKING full scan, list every AP, flag GotekOMEGA ──
  line(0xFD20,1,"[1] BLOCKING scan (all channels)...");
  flush();
  int n = WiFi.scanNetworks(false /*blocking*/, false, false, 350, 0 /*all ch*/);
  if(n<=0){ line(0xF800,1,"  scan returned %d - NOTHING SEEN", n); }
  int omega=0, omegaCh=0; uint8_t omegaB[6]={0};
  for(int i=0;i<n && i<14;i++){
    bool hit = (WiFi.SSID(i)==String(DONGLE_SSID));
    uint8_t* b=WiFi.BSSID(i);
    if(hit){ omega++; omegaCh=WiFi.channel(i); if(b) memcpy(omegaB,b,6); }
    line(hit?0x07E0:0x9BD6,1,"  %-16.16s ch%-2d %ddBm %02X%02X",
         WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i),
         b?b[4]:0, b?b[5]:0);
  }
  if(n>14) line(0x8410,1,"  ...(%d more)", n-14);
  if(omega>0) line(0x07E0,1,"  => GotekOMEGA FOUND: ch%d bssid ..%02X%02X", omegaCh, omegaB[4], omegaB[5]);
  else        line(0xFC00,1,"  => GotekOMEGA NOT in scan list");
  Y+=2; flush();

  // ── Q2: JOIN the dongle AP ──
  line(0xFD20,1,"[2] Joining %s ...", DONGLE_SSID); flush();
  WiFi.persistent(false); WiFi.setAutoReconnect(false);
  WiFi.disconnect(false,true); delay(200);
  WiFi.begin(DONGLE_SSID, DONGLE_PASS);            // plain SSID join (no bssid/ch hint)
  uint32_t t0=millis();
  while(WiFi.status()!=WL_CONNECTED && millis()-t0<15000) delay(200);
  bool joined = (WiFi.status()==WL_CONNECTED);
  if(joined) line(0x07E0,1,"  JOINED. P4 IP %s", WiFi.localIP().toString().c_str());
  else       line(0xFC00,1,"  JOIN FAILED (status=%d)", WiFi.status());
  Y+=2; flush();

  // ── Q3: TCP to the disk server ──
  if(joined){
    line(0xFD20,1,"[3] TCP %s:%d ...", DONGLE_IP, DONGLE_PORT); flush();
    WiFiClient c;
    if(c.connect(DONGLE_IP, DONGLE_PORT)){ line(0x07E0,2,"  TCP OK - transport works!"); c.stop(); }
    else                                  line(0xFC00,1,"  TCP connect FAILED");
  } else {
    line(0x8410,1,"[3] skipped (no join)");
  }
  flush();
}

void loop(){ flush(); delay(1000); }
