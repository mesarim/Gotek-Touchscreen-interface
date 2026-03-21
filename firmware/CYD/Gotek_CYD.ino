// Gotek_CYD.ino — CYD (ESP32-2432S028R) Wireless-Only Build
// v3.8.1-CYD  Landscape 320×240
//
// Changelog:
//   v3.8.0-CYD  2026-03-21
//     - Init pattern taken DIRECTLY from lachimalaif/DataDisplayCYD (confirmed working):
//         tft.init() → tft.setRotation(1) → tft.invertDisplay(true)
//         SPI.begin(T_CLK, T_DOUT, T_DIN) → ts.begin() → ts.setRotation(1)
//     - User_Setup.h: ILI9341_2_DRIVER + USE_HSPI_PORT + no TOUCH_CS
//     - No SD on this build (wireless-only, ADF streamed from SD via g_queued_adf_path)
//     - JPEG: read to RAM buffer, draw via TJpg_Decoder
//
// ── Board settings ─────────────────────────────────────────────────────────────
//   Board:     ESP32 Dev Module
//   Partition: Huge APP (3MB No OTA)   ← REQUIRED
//   Flash Freq: 40MHz                  ← REQUIRED
//   CPU:       240MHz
//   PSRAM:     Disabled
//
// ── User_Setup.h ───────────────────────────────────────────────────────────────
//   Copy User_Setup.h (included in this zip) to:
//   C:\Users\<you>\Documents\Arduino\libraries\TFT_eSPI\User_Setup.h
//
// ── Libraries ──────────────────────────────────────────────────────────────────
//   TFT_eSPI            (Bodmer)
//   XPT2046_Touchscreen (Paul Stoffregen)
//   TJpg_Decoder        (Bodmer)
//   SD + WiFi           (built-in)

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SD.h>
#include <FS.h>
#include <WiFi.h>
#include <TJpg_Decoder.h>
#include <vector>
#include <algorithm>
#include <ctype.h>
#include "espnow_server.h"

// ── Version ──────────────────────────────────────────────────────────────────
#define FW_VERSION "v3.9.0-CYD"

// ── CYD pins ─────────────────────────────────────────────────────────────────
// Confirmed by sdcard.h (jczn_2432s028r board variant):
//   SD is on HSPI with DEFAULT SPI pins: SCK=18 MISO=19 MOSI=23 CS=5
//   Touch is on VSPI: CLK=25 MISO=39 MOSI=32 CS=33 (managed by TFT_eSPI)
//   Display is on VSPI via TFT_eSPI (USE_HSPI_PORT makes it use HSPI for display)
// Wait — sdcard.h says SD=HSPI, display+touch=VSPI.
// This means USE_HSPI_PORT should NOT be set (display on VSPI = default).
// But DataDisplayCYD uses USE_HSPI_PORT and works... 
// The key insight: SD_SCK=18/MISO=19/MOSI=23 are completely separate from
// touch pins 25/39/32, so they never conflict regardless of bus assignment.
#define T_CLK    25   // Touch CLK
#define T_DOUT   39   // Touch MISO
#define T_DIN    32   // Touch MOSI
#define T_CS     33   // Touch CS (TOUCH_CS in User_Setup.h)
// SD uses separate hardware pins — not shared with touch
#define SD_SCK    18
#define SD_MISO   19
#define SD_MOSI   23
#define SD_CS_PIN  5
#define LCD_BL_PIN 21
#define LED_R_PIN   4
#define LED_G_PIN  16
#define LED_B_PIN  17

// ── Display (TFT_eSPI manages display on HSPI + touch on VSPI internally) ────
static TFT_eSPI tft;
// Touch — XPT2046 on VSPI (CLK=25, MISO=39, MOSI=32, CS=33, IRQ=36)
#define T_IRQ 36
static SPIClass touchSPI(VSPI);
static XPT2046_Touchscreen ts(T_CS, T_IRQ);

// ── Exported for espnow_server.cpp ───────────────────────────────────────────
int      g_sd_cs_pin       = SD_CS_PIN;
String   g_queued_adf_path = "";
uint32_t g_queued_adf_size = 0;
uint8_t* g_disk            = nullptr;

// ── Screen dimensions ────────────────────────────────────────────────────────
#define LCD_W  320
#define LCD_H  240

// ── Layout ───────────────────────────────────────────────────────────────────
#define STATUS_H   18
#define TOPBAR_H   18
#define BOTBAR_H   18
#define BOTTOM_H   26
#define LIST_TOP   (STATUS_H + TOPBAR_H)
#define LIST_BOT   (LCD_H - BOTBAR_H - BOTTOM_H)
#define LIST_H_    (LIST_BOT - LIST_TOP)
#define ITEMS_VIS  6
#define ITEM_H     (LIST_H_ / ITEMS_VIS)
#define ART_HEADER_H  18
#define ART_PANEL_W   148
#define ART_INFO_X    152
#define ART_INFO_W    (LCD_W - ART_INFO_X)
#define ART_BTN_H     22
#define ART_BTN_Y     (LCD_H - ART_BTN_H - 4)

// ── Colours ───────────────────────────────────────────────────────────────────
#ifndef TFT_BLACK
  #define TFT_BLACK  0x0000
  #define TFT_WHITE  0xFFFF
  #define TFT_RED    0xF800
  #define TFT_GREEN  0x07E0
  #define TFT_BLUE   0x001F
  #define TFT_CYAN   0x07FF
  #define TFT_YELLOW 0xFFE0
  #define TFT_ORANGE 0xFD20
#endif

// ============================================================================
// THEMES
// ============================================================================
struct Theme {
  const char* name;
  uint16_t bg,panel,bar,sel,sep,dim,mid,lit;
  uint16_t green,orange,amber,blue,now,accent,circ,circ_text;
};
static const Theme THEMES[] = {
  {"NAVY",   0x1082,0x18C3,0x2104,0x2945,0x2103,0x4A8A,0x6B6D,0x9BD6, 0x2D6B,0xFC60,0xFD00,0x4C5F,0x0B26,0x5D1F,0x3186,TFT_WHITE},
  {"EMBER",  0x0800,0x1000,0x1800,0x5820,0x1000,0x5820,0x8440,0xC8A0, 0x0560,0xFF40,0xFC00,0x4A9F,0x1000,0x7800,0x3800,TFT_WHITE},
  {"MATRIX", 0x0020,0x0040,0x0060,0x0340,0x0040,0x0340,0x0580,0x07C0, 0x07E0,0x07E0,0x0FE0,0x07FF,0x0060,0x0380,0x0300,0x07C0},
  {"SYNTH",  0x1001,0x2003,0x3005,0x5008,0x2003,0x600C,0x900F,0xC09F, 0x4BE0,0xFC1F,0xE81F,0xA01F,0x2003,0x8010,0x5008,0xE81F},
};
static const int NUM_THEMES = 4;
static int g_theme = 0;
static uint16_t COL_BG,COL_PANEL,COL_BAR,COL_SEL,COL_SEP,COL_DIM,COL_MID,COL_LIT;
static uint16_t COL_GREEN,COL_ORANGE,COL_AMBER,COL_BLUE,COL_NOW,COL_ACCENT,COL_CIRC,COL_CIRC_TEXT;

static void applyTheme(int i){
  g_theme=i%NUM_THEMES; const Theme& t=THEMES[g_theme];
  COL_BG=t.bg; COL_PANEL=t.panel; COL_BAR=t.bar; COL_SEL=t.sel;
  COL_SEP=t.sep; COL_DIM=t.dim; COL_MID=t.mid; COL_LIT=t.lit;
  COL_GREEN=t.green; COL_ORANGE=t.orange; COL_AMBER=t.amber;
  COL_BLUE=t.blue; COL_NOW=t.now; COL_ACCENT=t.accent;
  COL_CIRC=t.circ; COL_CIRC_TEXT=t.circ_text;
}

// ============================================================================
// STATE
// ============================================================================
enum UIScreen { SCR_LIST, SCR_ART, SCR_INFO };
static UIScreen g_screen = SCR_LIST;
enum DiskMode  { MODE_ADF=0, MODE_DSK=1 };
static DiskMode g_mode = MODE_ADF;

struct GameEntry {
  String name; int first_file_idx,disk_count; String jpg_path;
  std::vector<int> disk_indices;
};
static std::vector<String>    g_files;
static std::vector<GameEntry> g_games;
static int    g_sel=0,g_scroll=0,g_disk_sel=0;
static String g_loaded_name="";
static bool   g_loaded=false;
static int    g_loaded_game_idx=-1,g_loaded_disk_idx=-1;
static bool   g_loop_cracktro = false;  // LOOP=1 in CONFIG.TXT (no cracktro on CYD but kept for config parity)
static bool   g_wireless_mode = false;  // MODE=WIRELESS in CONFIG.TXT

// ── Double-tap ───────────────────────────────────────────────────────────────
static int g_last_tap=-1; static uint32_t g_last_tap_ms=0;
static bool isDoubleTap(int i){ uint32_t n=millis(); bool d=(i==g_last_tap)&&(n-g_last_tap_ms<500); g_last_tap=i; g_last_tap_ms=n; return d; }
static void resetTap(){ g_last_tap=-1; g_last_tap_ms=0; }

// ── Helpers ───────────────────────────────────────────────────────────────────
static String fnOnly(const String& p){ int s=p.lastIndexOf('/'); return s>=0?p.substring(s+1):p; }
static String parentDir(const String& p){ int s=p.lastIndexOf('/'); return s>0?p.substring(0,s):String("/"); }
static String noExt(const String& p){ int s=p.lastIndexOf('/'),d=p.lastIndexOf('.'); String b=(s>=0)?p.substring(s+1):p; if(d>s) b=b.substring(0,d-(s>=0?s+1:0)); return b; }
static String gameBase(const String& fp){ String b=noExt(fnOnly(fp)); int dash=b.lastIndexOf('-'); if(dash>0&&dash<(int)b.length()-1){ String s=b.substring(dash+1); bool n=true; for(int i=0;i<(int)s.length();i++) if(!isDigit(s[i])){ n=false; break; } if(n) return b.substring(0,dash); } return b; }
static int diskNum(const String& fp){ String b=noExt(fnOnly(fp)); int d=b.lastIndexOf('-'); if(d>0&&d<(int)b.length()-1){ int n=b.substring(d+1).toInt(); if(n>0) return n; } return 0; }
static String fit(const String& s,int w){ String r=s; while(r.length()>1&&tft.textWidth(r)>w) r=r.substring(0,r.length()-1); return r; }

// ── SD helpers ────────────────────────────────────────────────────────────────
static bool findInDir(const String& dir,const String& t,String& out){
  String tL=t; tL.toLowerCase(); File d=SD.open(dir.c_str()); if(!d||!d.isDirectory()) return false;
  File f=d.openNextFile(); while(f){ if(!f.isDirectory()){ String nm=f.name(); int sl=nm.lastIndexOf('/'); if(sl>=0) nm=nm.substring(sl+1); String nL=nm; nL.toLowerCase(); if(nL==tL){ out=dir+"/"+nm; f.close(); d.close(); return true; } } f.close(); f=d.openNextFile(); } d.close(); return false;
}
static bool findNFO(const String& p,String& out){ String b=noExt(fnOnly(p)),dir=parentDir(p); if(findInDir(dir,b+".nfo",out)) return true; String gb=gameBase(p); if(gb!=b&&findInDir(dir,gb+".nfo",out)) return true; return false; }
static bool findJPG(const String& p,String& out){
  String b=noExt(fnOnly(p)),dir=parentDir(p);
  // Try exact basename first (e.g. GameName-1.jpg)
  for(auto e:{"jpg","jpeg","JPG","JPEG"}){ String c=dir+"/"+b+"."+e; if(SD.exists(c.c_str())){ out=c; return true; } }
  // Try game base name without disk suffix (e.g. GameName.jpg)
  String gb=gameBase(p);
  if(gb!=b){ for(auto e:{"jpg","jpeg","JPG","JPEG"}){ String c=dir+"/"+gb+"."+e; if(SD.exists(c.c_str())){ out=c; return true; } } }
  return false;
}
static String readFile(const String& p,size_t mx=256){ File f=SD.open(p.c_str(),FILE_READ); if(!f) return ""; String s; while(f.available()&&s.length()<mx) s+=(char)f.read(); f.close(); s.replace("\r\n","\n"); s.replace("\r","\n"); return s; }
static void parseNFO(const String& txt,String& ti,String& bl){ ti=""; bl=""; bool gT=false,gB=false; int pos=0; while(pos<(int)txt.length()){ int nl=txt.indexOf('\n',pos); if(nl<0) nl=txt.length(); String L=txt.substring(pos,nl); L.trim(); pos=nl+1; if(!gT&&L.startsWith("Title:")){ ti=L.substring(6); ti.trim(); gT=true; continue; } if(!gB&&(L.startsWith("Blurb:")||L.startsWith("Description:"))){ bl=L.substring(L.indexOf(':')+1); bl.trim(); gB=true; continue; } if(!gT&&L.length()){ ti=L; gT=true; } } ti.trim(); bl.trim(); }

// ── Scanner ───────────────────────────────────────────────────────────────────
static void listImages(std::vector<String>& out){
  out.clear(); String md=(g_mode==MODE_ADF)?"/ADF":"/DSK",ext=(g_mode==MODE_ADF)?".ADF":".DSK";
  File root=SD.open(md.c_str()); if(root&&root.isDirectory()){ File gd; while((gd=root.openNextFile())){ String en=gd.name(); if(!en.startsWith("/")) en=md+"/"+en; if(gd.isDirectory()){ File e; while((e=gd.openNextFile())){ String fn=e.name(); int sl=fn.lastIndexOf('/'); if(sl>=0) fn=fn.substring(sl+1); String up=fn; up.toUpperCase(); if(up.endsWith(ext)||up.endsWith(".IMG")){ String fp=en+"/"+fn; if(!fp.startsWith("/")) fp="/"+fp; out.push_back(fp); } e.close(); } } else { String fn=en; int sl=fn.lastIndexOf('/'); if(sl>=0) fn=fn.substring(sl+1); String up=fn; up.toUpperCase(); if(up.endsWith(ext)||up.endsWith(".IMG")) out.push_back(en); } gd.close(); } root.close(); }
  std::sort(out.begin(),out.end());
}
static void buildGames(){ g_games.clear(); std::vector<bool> used(g_files.size(),false); for(int i=0;i<(int)g_files.size();i++){ if(used[i]) continue; String base=gameBase(g_files[i]); int dn=diskNum(g_files[i]); String dir=parentDir(g_files[i]); GameEntry e; e.first_file_idx=i; e.disk_count=1; e.disk_indices.push_back(i); if(dn>0){ e.name=base; for(int j=i+1;j<(int)g_files.size();j++){ if(used[j]) continue; if(parentDir(g_files[j])==dir&&gameBase(g_files[j])==base&&diskNum(g_files[j])>0){ used[j]=true; e.disk_count++; e.disk_indices.push_back(j); if(diskNum(g_files[j])<diskNum(g_files[e.first_file_idx])) e.first_file_idx=j; } } for(int a=0;a<(int)e.disk_indices.size();a++) for(int b=a+1;b<(int)e.disk_indices.size();b++) if(diskNum(g_files[e.disk_indices[b]])<diskNum(g_files[e.disk_indices[a]])) std::swap(e.disk_indices[a],e.disk_indices[b]); } else { e.name=noExt(fnOnly(g_files[i])); } used[i]=true; String np,nt,nb; if(findNFO(g_files[e.first_file_idx],np)){ parseNFO(readFile(np,256),nt,nb); if(nt.length()) e.name=nt; } String jpg; if(findJPG(g_files[e.first_file_idx],jpg)) e.jpg_path=jpg; g_games.push_back(e); } for(int i=0;i<(int)g_games.size();i++) for(int j=i+1;j<(int)g_games.size();j++){ String a=g_games[i].name,b=g_games[j].name; a.toLowerCase(); b.toLowerCase(); if(a.compareTo(b)>0) std::swap(g_games[i],g_games[j]); } }

// ── Config ────────────────────────────────────────────────────────────────────
static void ensureConfig() {
  // Write full CONFIG.TXT if missing or empty (< 4 bytes)
  File f = SD.open("/CONFIG.TXT", FILE_READ);
  bool needsWrite = true;
  if (f) {
    if (f.size() > 4) needsWrite = false;
    f.close();
  }
  if (needsWrite) {
    SD.remove("/CONFIG.TXT");
    File fw = SD.open("/CONFIG.TXT", FILE_WRITE);
    if (fw) {
      fw.print(
        "# ============================================================\r\n"
        "# Gotek Touchscreen Interface — CONFIG.TXT\r\n"
        "# ============================================================\r\n"
        "# This file was auto-generated on first boot.\r\n"
        "# Edit values to customise behaviour.\r\n"
        "# Lines starting with # are comments and are ignored.\r\n"
        "# ============================================================\r\n"
        "\r\n"
        "# Theme colour scheme (0=NAVY  1=EMBER  2=MATRIX  3=SYNTH)\r\n"
        "THEME=0\r\n"
        "\r\n"
        "# Boot animation — 0=play once, 1=loop (Waveshare 7\" only)\r\n"
        "LOOP=0\r\n"
        "\r\n"
        "# Last used disk format, restored on boot (ADF or DSK)\r\n"
        "LASTMODE=ADF\r\n"
        "\r\n"
        "# Operating mode\r\n"
        "# STANDALONE = SD card in this device, USB goes to Gotek\r\n"
        "# WIRELESS   = send disk images to a paired XIAO dongle\r\n"
        "MODE=STANDALONE\r\n"
        "\r\n"
        "# WiFi AP settings (Waveshare 7\" web server)\r\n"
        "WIFI_SSID=GotekWifi\r\n"
        "WIFI_PASS=gotek1234\r\n"
        "WIFI_CHANNEL=6\r\n"
        "WIFI_IP=192.168.4.1\r\n"
        "\r\n"
        "# XIAO pairing — written automatically when you tap PAIR\r\n"
        "# Delete these lines to unpair and re-pair a new XIAO.\r\n"
        "#XIAO_MAC=AA:BB:CC:DD:EE:FF\r\n"
        "#XIAO_NAME=XIAO-AABB\r\n"
        "#XIAO_IP=192.168.4.1\r\n"
      );
      fw.close();
    }
  }

  // Create /ADF folder with README if missing
  if (!SD.exists("/ADF")) {
    SD.mkdir("/ADF");
    File r = SD.open("/ADF/README.TXT", FILE_WRITE);
    if (r) {
      r.print(
        "GOTEK TOUCHSCREEN — ADF GAMES FOLDER\r\n"
        "=====================================\r\n"
        "\r\n"
        "Place Amiga floppy disk images (.adf) in this folder.\r\n"
        "\r\n"
        "SINGLE DISK GAME:\r\n"
        "  /ADF/GameName/GameName.adf\r\n"
        "\r\n"
        "MULTI DISK GAME:\r\n"
        "  /ADF/GameName/GameName-1.adf\r\n"
        "  /ADF/GameName/GameName-2.adf\r\n"
        "  /ADF/GameName/GameName-3.adf\r\n"
        "\r\n"
        "OPTIONAL EXTRAS (in same folder as the .adf files):\r\n"
        "  GameName.jpg  - cover art (JPEG, any size)\r\n"
        "  GameName.nfo  - info text (plain text, first line = title)\r\n"
        "\r\n"
        "SUPPORTED FORMATS: .adf  .img  .adz\r\n"
        "\r\n"
        "See https://github.com/mesarim/Gotek-Touchscreen-interface\r\n"
      );
      r.close();
    }
  }

  // Create /DSK folder with README if missing
  if (!SD.exists("/DSK")) {
    SD.mkdir("/DSK");
    File r = SD.open("/DSK/README.TXT", FILE_WRITE);
    if (r) {
      r.print(
        "GOTEK TOUCHSCREEN — DSK GAMES FOLDER\r\n"
        "=====================================\r\n"
        "\r\n"
        "Place ZX Spectrum / Amstrad CPC disk images (.dsk) here.\r\n"
        "\r\n"
        "SINGLE DISK GAME:\r\n"
        "  /DSK/GameName/GameName.dsk\r\n"
        "\r\n"
        "MULTI DISK GAME:\r\n"
        "  /DSK/GameName/GameName-1.dsk\r\n"
        "  /DSK/GameName/GameName-2.dsk\r\n"
        "\r\n"
        "OPTIONAL EXTRAS (in same folder as the .dsk files):\r\n"
        "  GameName.jpg  - cover art (JPEG, any size)\r\n"
        "  GameName.nfo  - info text (plain text, first line = title)\r\n"
        "\r\n"
        "SUPPORTED FORMATS: .dsk  .img\r\n"
        "\r\n"
        "See https://github.com/mesarim/Gotek-Touchscreen-interface\r\n"
      );
      r.close();
    }
  }
}

static void loadConfig() {
  File f = SD.open("/CONFIG.TXT", FILE_READ);
  if (!f) { applyTheme(0); return; }
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.startsWith("#")) continue;
    int eq = line.indexOf('=');
    if (eq < 0) continue;
    String key = line.substring(0, eq); key.trim();
    String val = line.substring(eq + 1); val.trim();
    if      (key == "THEME")    applyTheme(val.toInt());
    else if (key == "LASTMODE") g_mode = (val == "DSK") ? MODE_DSK : MODE_ADF;
    else if (key == "LOOP")     g_loop_cracktro = (val == "1" || val == "true");
    else if (key == "MODE")     g_wireless_mode = (val == "WIRELESS");
    // WIFI_* and XIAO_* are consumed by espnow_server.cpp
  }
  f.close();
}
static void saveKey(const String& key,const String& val){ String lines=""; bool written=false; File fr=SD.open("/CONFIG.TXT",FILE_READ); if(fr){ while(fr.available()){ String ln=fr.readStringUntil('\n'); ln.trim(); if(ln.startsWith(key+"=")){ lines+=key+"="+val+"\n"; written=true; } else lines+=ln+"\n"; } fr.close(); } if(!written) lines+=key+"="+val+"\n"; SD.remove("/CONFIG.TXT"); File fw=SD.open("/CONFIG.TXT",FILE_WRITE); if(fw){ fw.print(lines); fw.close(); } }

// ── Touch read (XPT2046 on VSPI) ─────────────────────────────────────────────
// Calibration from real measurements on this CYD unit:
//   Raw x: ~200 (left) to ~3900 (right)
//   Raw y: ~200 (top)  to ~3600 (bottom) — note: bottom raw < 3900
#define TOUCH_X_MIN  200
#define TOUCH_X_MAX  3900
#define TOUCH_Y_MIN  200
#define TOUCH_Y_MAX  3600
struct TouchPt { bool valid; uint16_t x,y; } gT={false,0,0};
static bool readTouch(){
  gT.valid=false;
  if(!ts.tirqTouched()||!ts.touched()) return false;
  TS_Point p=ts.getPoint();
  Serial.printf("[TOUCH] raw x=%d y=%d z=%d → mapped x=%d y=%d\n",
    p.x, p.y, p.z,
    (int)map(p.x,TOUCH_X_MIN,TOUCH_X_MAX,0,LCD_W-1),
    (int)map(p.y,TOUCH_Y_MIN,TOUCH_Y_MAX,0,LCD_H-1));
  gT.x=(uint16_t)constrain(map(p.x,TOUCH_X_MIN,TOUCH_X_MAX,0,LCD_W-1),0,LCD_W-1);
  gT.y=(uint16_t)constrain(map(p.y,TOUCH_Y_MIN,TOUCH_Y_MAX,0,LCD_H-1),0,LCD_H-1);
  gT.valid=true; return true;
}

// ── JPEG via TJpgDec ─────────────────────────────────────────────────────────
static bool jpgCB(int16_t x,int16_t y,uint16_t w,uint16_t h,uint16_t* bmp){ if(y>=tft.height()) return false; tft.pushImage(x,y,w,h,bmp); return true; }

// ── Draw helpers ──────────────────────────────────────────────────────────────
static void gradBg(int x,int y,int w,int h,uint16_t cT,uint16_t cB){ int r1=(cT>>11)&0x1F,g1=(cT>>5)&0x3F,b1=cT&0x1F,r2=(cB>>11)&0x1F,g2=(cB>>5)&0x3F,b2=cB&0x1F; int steps=min(h,16),bH=max(1,h/steps); for(int i=0;i<steps;i++){ uint16_t c=(((r1+(r2-r1)*i/steps)&0x1F)<<11)|(((g1+(g2-g1)*i/steps)&0x3F)<<5)|((b1+(b2-b1)*i/steps)&0x1F); int iy=y+i*bH,ih=(i==steps-1)?(y+h-iy):bH; tft.fillRect(x,iy,w,ih,c); } }
static void floppyIcon(int x,int y,int s,uint16_t body,uint16_t lbl,uint16_t shut){ tft.fillRect(x,y,s,s,body); int nw=s*4/18,nh=s*3/18; tft.fillRect(x+s-nw,y,nw,nh,shut); int lw=s-nw-3,lh=s*9/22; tft.fillRect(x+2,y+2,lw,lh,lbl); for(int i=0;i<3;i++) tft.fillRect(x+4,y+4+i*(lh/4),lw-5,1,(uint16_t)(lbl>>1|0x1082)); int mx=x+s/4,my=y+s*11/20,mw=s/2,mh=s*8/20; tft.fillRect(mx,my,mw,mh,shut); tft.drawRect(mx,my,mw,mh,(uint16_t)(shut>>1|0x0821)); tft.fillRect(mx+mw/2-mw/6,my+mh/2-mh/4,mw/3,mh/2,body); }

// ── Forward declarations ──────────────────────────────────────────────────────
static void drawListView();
static void drawArtView();
static void drawInfoView();
static bool doLoad(const String& path);
static void doUnload();

// ============================================================================
// SCREEN 1 — LIST VIEW
// ============================================================================
static void drawStatusBar(){
  tft.fillRect(0,0,LCD_W,STATUS_H,COL_BAR); tft.setTextSize(1);
  tft.setTextColor(COL_ORANGE,COL_BAR); tft.setCursor(4,5); tft.print("OMEGAWARE");
  tft.setTextColor(COL_DIM,COL_BAR); tft.print(" " FW_VERSION);
  String label; uint16_t lc;
  if(espnowIsPaired()){ label=espnowGetXiaoName(); lc=COL_GREEN; }
  else if(g_espnow_pair_waiting){ label=espnowGetPendingName()+"?"; lc=COL_AMBER; }
  else { label="NO XIAO"; lc=0xFD20; }
  tft.setTextColor(lc,COL_BAR); int tw=tft.textWidth(label);
  tft.setCursor(LCD_W/2-tw/2,5); tft.print(label);
  tft.fillCircle(LCD_W-6,STATUS_H/2,4,g_loaded?0xE8C4:COL_GREEN);
}
static void drawTopBar(){
  tft.fillRect(0,STATUS_H,LCD_W,TOPBAR_H,COL_BAR); tft.drawFastHLine(0,STATUS_H,LCD_W,COL_SEP); tft.setTextSize(1);
  bool isADF=(g_mode==MODE_ADF);
  if(isADF){ tft.fillRoundRect(4,STATUS_H+2,32,14,5,COL_ACCENT); tft.drawRoundRect(4,STATUS_H+2,32,14,5,COL_AMBER); tft.setTextColor(COL_AMBER,COL_ACCENT); }
  else { tft.fillRoundRect(4,STATUS_H+2,32,14,5,COL_BG); tft.setTextColor(COL_DIM,COL_BG); }
  tft.setCursor(11,STATUS_H+5); tft.print("ADF");
  if(!isADF){ tft.fillRoundRect(40,STATUS_H+2,32,14,5,COL_ACCENT); tft.drawRoundRect(40,STATUS_H+2,32,14,5,COL_AMBER); tft.setTextColor(COL_AMBER,COL_ACCENT); }
  else { tft.fillRoundRect(40,STATUS_H+2,32,14,5,COL_BG); tft.setTextColor(COL_DIM,COL_BG); }
  tft.setCursor(47,STATUS_H+5); tft.print("DSK");
  tft.setTextColor(COL_MID,COL_BAR); tft.setCursor(78,STATUS_H+5); tft.print(String(g_games.size())+" games");
  tft.setTextColor(COL_DIM,COL_BAR); tft.setCursor(LCD_W-20,STATUS_H+5); tft.print(" ^ ");
}
static void drawBotBar(){
  tft.fillRect(0,LIST_BOT,LCD_W,BOTBAR_H,COL_BAR); tft.drawFastHLine(0,LIST_BOT,LCD_W,COL_SEP);
  tft.setTextSize(1); tft.setTextColor(COL_DIM,COL_BAR); tft.setCursor(4,LIST_BOT+5); tft.print(" v ");
  if(g_loaded&&g_loaded_name.length()){ tft.fillCircle(30,LIST_BOT+BOTBAR_H/2,3,COL_GREEN); tft.setTextColor(COL_GREEN,COL_BAR); tft.setCursor(36,LIST_BOT+5); tft.print(fit(g_loaded_name,LCD_W-40)); }
  else { tft.setTextColor(COL_DIM,COL_BAR); tft.setCursor(36,LIST_BOT+5); tft.print("tap item twice for art view"); }
}
static void drawBottomBtns(){
  int y=LCD_H-BOTTOM_H; gradBg(0,y,LCD_W,BOTTOM_H,COL_BAR,COL_PANEL); tft.drawFastHLine(0,y,LCD_W,COL_SEP);
  bool ld=(g_loaded&&g_loaded_game_idx==g_sel);
  bool paired=espnowIsPaired();
  uint16_t bF,bB; const char* bl;
  if(ld){        bF=0x4000; bB=0xE8C4; bl="EJECT"; }
  else if(paired){ bF=0x0340; bB=COL_GREEN; bl="INSERT"; }
  else {         bF=COL_PANEL; bB=COL_DIM; bl="NO XIAO"; }
  tft.fillRoundRect(2,y+2,237,BOTTOM_H-4,5,bF); tft.drawRoundRect(2,y+2,237,BOTTOM_H-4,5,bB);
  tft.setTextSize(1); tft.setTextColor(paired||ld?TFT_WHITE:COL_DIM,bF);
  tft.setCursor(2+(237-tft.textWidth(bl))/2,y+5); tft.print(bl);
  tft.fillRoundRect(242,y+2,76,BOTTOM_H-4,5,COL_PANEL); tft.drawRoundRect(242,y+2,76,BOTTOM_H-4,5,COL_DIM);
  tft.setTextColor(COL_MID,COL_PANEL); tft.setCursor(242+(76-tft.textWidth("INFO"))/2,y+5); tft.print("INFO");
}
static void drawFileList(){
  gradBg(0,LIST_TOP,LCD_W,LIST_H_,COL_BG,(uint16_t)(COL_BG>>1));
  if(g_games.empty()){ tft.setTextSize(1); tft.setTextColor(0xE8C4,COL_BG); tft.setCursor(8,LIST_TOP+14); tft.print(g_mode==MODE_ADF?"No .ADF on SD":"No .DSK on SD"); return; }
  int maxOff=(int)g_games.size()-ITEMS_VIS; if(maxOff<0) maxOff=0; g_scroll=constrain(g_scroll,0,maxOff);
  for(int vi=0;vi<ITEMS_VIS;vi++){
    int gi=g_scroll+vi; if(gi>=(int)g_games.size()) break;
    const GameEntry& gm=g_games[gi]; bool isSel=(gi==g_sel),isLd=(g_loaded&&g_loaded_game_idx==gi);
    int y=LIST_TOP+vi*ITEM_H,cH=ITEM_H-2;
    if(isSel){ tft.fillRoundRect(1,y+1,LCD_W-2,cH,4,COL_SEL); tft.drawRoundRect(1,y+1,LCD_W-2,cH,4,COL_AMBER); }
    else { tft.fillRect(0,y,LCD_W,ITEM_H,COL_BG); tft.fillRoundRect(1,y+1,LCD_W-2,cH,3,COL_PANEL); }
    tft.fillRoundRect(3,y+3,3,cH-6,1,isLd?COL_GREEN:(isSel?COL_AMBER:COL_ACCENT));
    int cx=18,cy=y+ITEM_H/2; uint16_t cc=isSel?COL_AMBER:(isLd?COL_GREEN:COL_CIRC);
    tft.fillCircle(cx,cy,10,cc); tft.setTextSize(1); tft.setTextColor(isSel||isLd?TFT_BLACK:COL_CIRC_TEXT,cc);
    char ib[2]={(char)toupper(gm.name.charAt(0)),0}; tft.setCursor(cx-tft.textWidth(ib)/2,cy-5); tft.print(ib);
    int bW=gm.disk_count>1?28:0,fW=isLd?18:0;
    tft.setTextColor(isSel?TFT_WHITE:COL_LIT,isSel?COL_SEL:COL_PANEL);
    tft.setCursor(34,cy-5); tft.print(fit(gm.name,LCD_W-36-bW-fW-4));
    if(gm.disk_count>1){ int bx=LCD_W-bW-fW-2; uint16_t bc=isLd?COL_GREEN:COL_ACCENT; tft.fillRoundRect(bx,cy-7,bW,12,3,bc); tft.setTextColor(TFT_WHITE,bc); String dc=String(gm.disk_count)+"D"; tft.setCursor(bx+(bW-tft.textWidth(dc))/2,cy-4); tft.print(dc); }
    if(isLd) floppyIcon(LCD_W-16,cy-7,14,COL_GREEN,COL_ACCENT,COL_BAR);
    if(vi<ITEMS_VIS-1) tft.drawFastHLine(8,y+ITEM_H-1,LCD_W-16,COL_SEP);
  }
}
static void drawListView(){ drawStatusBar(); drawTopBar(); drawFileList(); drawBotBar(); drawBottomBtns(); }

// ============================================================================
// SCREEN 2 — ART VIEW
// ============================================================================
static void drawArtView(){
  tft.fillScreen(COL_BG); if(g_games.empty()) return;
  const GameEntry& gm=g_games[g_sel];

  // Header — taller for easier touch
  tft.fillRect(0,0,LCD_W,ART_HEADER_H,COL_BAR); tft.setTextSize(1);
  // PREV: left quarter, NEXT: right quarter — big touch zones
  bool hasPrev=(g_sel>0), hasNext=(g_sel<(int)g_games.size()-1);
  tft.fillRoundRect(0,0,70,ART_HEADER_H,0,hasPrev?COL_ACCENT:COL_BAR);
  tft.setTextColor(hasPrev?COL_AMBER:COL_DIM,hasPrev?COL_ACCENT:COL_BAR);
  tft.setCursor(4,5); tft.print("< PREV");
  tft.fillRoundRect(250,0,70,ART_HEADER_H,0,hasNext?COL_ACCENT:COL_BAR);
  tft.setTextColor(hasNext?COL_AMBER:COL_DIM,hasNext?COL_ACCENT:COL_BAR);
  tft.setCursor(LCD_W-tft.textWidth("NEXT >")-4,5); tft.print("NEXT >");
  tft.setTextColor(COL_LIT,COL_BAR); String hdr=fit(gm.name,155);
  tft.setCursor(LCD_W/2-tft.textWidth(hdr)/2,5); tft.print(hdr);

  int aX=2,aY=ART_HEADER_H+2,aW=ART_PANEL_W-4,aH=LCD_H-ART_HEADER_H-4;
  tft.fillRoundRect(aX,aY,aW,aH,6,COL_PANEL); tft.drawRoundRect(aX-1,aY-1,aW+2,aH+2,7,COL_ACCENT);
  if(gm.jpg_path.length()){
    TJpgDec.setJpgScale(1); TJpgDec.setSwapBytes(true); TJpgDec.setCallback(jpgCB);
    TJpgDec.drawSdJpg(aX+2,aY+2,gm.jpg_path.c_str());
  } else {
    int is=min(aW,aH)*3/4; floppyIcon(aX+(aW-is)/2,aY+(aH-is)/2,is,COL_ACCENT,COL_MID,COL_BAR);
    tft.setTextSize(2); tft.setTextColor(COL_LIT,COL_MID);
    char ib[2]={(char)toupper(gm.name.charAt(0)),0}; tft.setCursor(aX+(aW-tft.textWidth(ib))/2,aY+aH/4); tft.print(ib); tft.setTextSize(1);
  }
  int ry=ART_HEADER_H+6; tft.setTextSize(1); tft.setTextColor(COL_LIT,COL_BG);
  String title=gm.name;
  if(tft.textWidth(title)>ART_INFO_W){ int brk=0; for(int i=1;i<=(int)title.length();i++){ if(i==(int)title.length()||title[i]==' '){ if(tft.textWidth(title.substring(0,i))<=ART_INFO_W) brk=i; else break; } } if(brk>0){ tft.setCursor(ART_INFO_X,ry); tft.print(title.substring(0,brk)); ry+=12; tft.setCursor(ART_INFO_X,ry); tft.print(fit(title.substring(brk+1),ART_INFO_W)); ry+=12; } else { tft.setCursor(ART_INFO_X,ry); tft.print(fit(title,ART_INFO_W)); ry+=12; } }
  else { tft.setCursor(ART_INFO_X,ry); tft.print(title); ry+=12; }
  ry+=2;
  String np,nt,nb; if(findNFO(g_files[gm.first_file_idx],np)){ parseNFO(readFile(np,256),nt,nb); }
  if(nb.length()){ tft.setTextColor(COL_MID,COL_BG); String line="",word=""; int lines=0; for(int i=0;i<=(int)nb.length()&&lines<2;i++){ char c=(i<(int)nb.length())?nb[i]:' '; if(c==' '||c=='\n'){ String cand=line.length()?line+" "+word:word; if(tft.textWidth(cand)>ART_INFO_W&&line.length()){ tft.setCursor(ART_INFO_X,ry); tft.print(line); ry+=10; lines++; line=word; } else line=cand; word=""; } else word+=c; } if(line.length()&&lines<2){ tft.setCursor(ART_INFO_X,ry); tft.print(line); ry+=10; } ry+=4; }
  if(gm.disk_count>1){ tft.setTextColor(COL_DIM,COL_BG); tft.setCursor(ART_INFO_X,ry); tft.print("DISK:"); ry+=12; int bw=26,bh=20,gap=4,sx=ART_INFO_X; for(int d=0;d<gm.disk_count&&d<6;d++){ int bx=sx+d*(bw+gap); if(bx+bw>LCD_W) break; bool iS=(d==g_disk_sel),iL=(g_loaded_game_idx==g_sel&&g_loaded_disk_idx==d); uint16_t bc=iL?COL_GREEN:(iS?COL_AMBER:COL_BAR); tft.fillRoundRect(bx,ry,bw,bh,3,bc); tft.drawRoundRect(bx,ry,bw,bh,3,iS?COL_AMBER:COL_DIM); tft.setTextColor(iL||iS?TFT_BLACK:COL_LIT,bc); tft.setCursor(bx+(bw-tft.textWidth(String(d+1)))/2,ry+6); tft.print(d+1); } ry+=bh+4; }
  bool isLd=(g_loaded&&g_loaded_game_idx==g_sel);
  bool paired=espnowIsPaired();
  uint16_t bF,bB; const char* bl;
  if(isLd){         bF=0x4000; bB=0xE8C4; bl="EJECT"; }
  else if(paired){  bF=0x0340; bB=COL_GREEN; bl="INSERT"; }
  else {            bF=COL_PANEL; bB=COL_DIM; bl="NO XIAO"; }
  // Taller INSERT button for easier touch
  int artBtnY=LCD_H-30, artBtnH=28;
  tft.fillRoundRect(ART_INFO_X,artBtnY,ART_INFO_W-2,artBtnH,6,bF);
  tft.drawRoundRect(ART_INFO_X,artBtnY,ART_INFO_W-2,artBtnH,6,bB);
  tft.setTextColor(paired||isLd?TFT_WHITE:COL_DIM,bF);
  tft.setCursor(ART_INFO_X+(ART_INFO_W-2-tft.textWidth(bl))/2,artBtnY+(artBtnH-8)/2); tft.print(bl);
}

// ============================================================================
// SCREEN 3 — INFO VIEW
// ============================================================================
static bool g_depair_confirm=false;
static void drawInfoView(){
  tft.fillScreen(COL_BG); tft.fillRect(0,0,LCD_W,STATUS_H,COL_BAR); tft.setTextSize(1);
  tft.setTextColor(COL_AMBER,COL_BAR); tft.setCursor(4,5); tft.print("< BACK");
  tft.setTextColor(COL_LIT,COL_BAR); tft.setCursor(LCD_W/2-tft.textWidth("INFO")/2,5); tft.print("INFO");
  int y=STATUS_H+8,pw=LCD_W-8;
  if(espnowIsPaired()){ bool on=espnowXiaoOnline(); tft.setTextColor(on?COL_GREEN:COL_ORANGE,COL_BG); tft.setCursor(4,y); tft.print(espnowGetXiaoName()); tft.print(on?" ONLINE":" OFFLINE"); y+=12; tft.setTextColor(COL_DIM,COL_BG); tft.setCursor(4,y); tft.print(espnowGetXiaoMac()); y+=12; if(g_espnow_xiao_done){ tft.setTextColor(COL_GREEN,COL_BG); tft.setCursor(4,y); tft.print("Transfer: OK"); y+=12; } else if(g_espnow_xiao_error){ tft.setTextColor(0xE8C4,COL_BG); tft.setCursor(4,y); tft.print("Transfer: ERR"); y+=12; } }
  else if(g_espnow_pair_waiting){ tft.setTextColor(COL_AMBER,COL_BG); tft.setCursor(4,y); tft.print(espnowGetPendingName()+" wants to pair!"); y+=12; }
  else { tft.setTextColor(COL_ORANGE,COL_BG); tft.setCursor(4,y); tft.print("No XIAO paired"); y+=12; tft.setTextColor(COL_DIM,COL_BG); tft.setCursor(4,y); tft.print("Power on XIAO then tap PAIR NOW"); y+=12; }
  y+=2; tft.drawFastHLine(4,y,pw,COL_SEP); y+=4;
  tft.setTextColor(COL_DIM,COL_BG); tft.setCursor(4,y); tft.print("Heap:"+String(ESP.getFreeHeap()/1024)+"KB  Games:"+String(g_games.size())+"  FW:" FW_VERSION); y+=12;
  y+=2; tft.drawFastHLine(4,y,pw,COL_SEP); y+=6;
  tft.setTextColor(COL_DIM,COL_BG); tft.setCursor(4,y); tft.print("THEME"); y+=12;
  int tx=4,pW=70,pH=18,pG=4;
  for(int i=0;i<NUM_THEMES;i++){ bool act=(i==g_theme); tft.fillRoundRect(tx,y,pW,pH,5,act?COL_ACCENT:COL_PANEL); tft.drawRoundRect(tx,y,pW,pH,5,act?COL_AMBER:COL_DIM); tft.setTextColor(act?COL_AMBER:COL_DIM,act?COL_ACCENT:COL_PANEL); tft.setCursor(tx+(pW-tft.textWidth(THEMES[i].name))/2,y+(pH-8)/2); tft.print(THEMES[i].name); tx+=pW+pG; }
  y+=pH+6; tft.drawFastHLine(4,y,pw,COL_SEP); y+=6;
  uint16_t pC=g_espnow_pair_waiting?COL_AMBER:(espnowIsPaired()?COL_MID:COL_GREEN);
  tft.fillRoundRect(4,y,pw,22,6,pC); tft.setTextColor(TFT_BLACK,pC);
  const char* pL=espnowIsPaired()?"RE-PAIR":(g_espnow_pair_waiting?"PAIR NOW!":"PAIR NOW");
  tft.setCursor(4+(pw-tft.textWidth(pL))/2,y+7); tft.print(pL); y+=26;
  if(espnowIsPaired()){ if(g_depair_confirm){ tft.fillRoundRect(4,y,pw,22,6,(uint16_t)0xE8C4); tft.setTextColor(TFT_BLACK,(uint16_t)0xE8C4); tft.setCursor(4+(pw-tft.textWidth("Sure?  [ YES ]  [ NO ]"))/2,y+7); tft.print("Sure?  [ YES ]  [ NO ]"); } else { tft.fillRoundRect(4,y,pw,22,6,(uint16_t)0x8000); tft.drawRoundRect(4,y,pw,22,6,(uint16_t)0xE8C4); tft.setTextColor(TFT_WHITE,(uint16_t)0x8000); String dl="DE-PAIR "+espnowGetXiaoName(); tft.setCursor(4+(pw-tft.textWidth(dl))/2,y+7); tft.print(dl); } y+=26; }
  tft.fillRoundRect(4,y,pw,22,6,(uint16_t)0x4208); tft.drawRoundRect(4,y,pw,22,6,COL_DIM); tft.setTextColor(COL_MID,(uint16_t)0x4208); tft.setCursor(4+(pw-tft.textWidth("SOFT RESET"))/2,y+7); tft.print("SOFT RESET");
}

// ── Pair flow ─────────────────────────────────────────────────────────────────
static void doPairNow(){ tft.fillScreen(COL_BG); tft.fillRect(0,0,LCD_W,STATUS_H,COL_BAR); tft.setTextSize(1); tft.setTextColor(COL_AMBER,COL_BAR); tft.setCursor(4,5); tft.print("PAIRING..."); tft.setTextColor(COL_LIT,COL_BG); tft.setCursor(4,STATUS_H+10); tft.print("Waiting for XIAO..."); espnowListenForPair(); uint32_t t0=millis(); int dots=0; while(!g_espnow_paired&&millis()-t0<20000){ delay(200); dots++; tft.fillRect(4,STATUS_H+24,180,10,COL_BG); tft.setTextColor(COL_MID,COL_BG); tft.setCursor(4,STATUS_H+24); tft.print(String("Listening")+(dots%2?"...":"   ")); } if(g_espnow_paired){ tft.setTextColor(COL_GREEN,COL_BG); tft.setCursor(4,STATUS_H+40); tft.print("PAIRED! "+espnowGetXiaoName()); delay(1500); } else { tft.setTextColor(0xE8C4,COL_BG); tft.setCursor(4,STATUS_H+40); tft.print("No XIAO found"); delay(2000); } drawInfoView(); }

// ── Load/Unload ───────────────────────────────────────────────────────────────
static bool doLoad(const String& path){
  // Guard: must be paired before loading
  if(!espnowIsPaired()){
    // Flash a clear error on the button area then redirect to INFO
    if(g_screen==SCR_LIST){
      int y=LCD_H-BOTTOM_H;
      tft.fillRoundRect(2,y+2,237,BOTTOM_H-4,5,(uint16_t)0x8000);
      tft.setTextSize(1); tft.setTextColor(TFT_WHITE,(uint16_t)0x8000);
      tft.setCursor(2+(237-tft.textWidth("PAIR XIAO FIRST!"))/2,y+5);
      tft.print("PAIR XIAO FIRST!");
    } else {
      tft.fillRoundRect(ART_INFO_X,ART_BTN_Y,ART_INFO_W-2,ART_BTN_H,6,(uint16_t)0x8000);
      tft.setTextSize(1); tft.setTextColor(TFT_WHITE,(uint16_t)0x8000);
      tft.setCursor(ART_INFO_X+(ART_INFO_W-2-tft.textWidth("PAIR XIAO FIRST!"))/2,ART_BTN_Y+(ART_BTN_H-8)/2);
      tft.print("PAIR XIAO FIRST!");
    }
    delay(1500);
    g_depair_confirm=false; g_screen=SCR_INFO; drawInfoView();
    return false;
  }

  if(g_screen==SCR_LIST){ int y=LCD_H-BOTTOM_H; tft.fillRoundRect(2,y+2,237,BOTTOM_H-4,5,(uint16_t)0x0260); tft.setTextSize(1); tft.setTextColor(TFT_CYAN,(uint16_t)0x0260); tft.setCursor(2+(237-tft.textWidth("LOADING..."))/2,y+5); tft.print("LOADING..."); }
  else { tft.fillRoundRect(ART_INFO_X,ART_BTN_Y,ART_INFO_W-2,ART_BTN_H,6,(uint16_t)0x0260); tft.setTextSize(1); tft.setTextColor(TFT_CYAN,(uint16_t)0x0260); tft.setCursor(ART_INFO_X+(ART_INFO_W-2-tft.textWidth("LOADING..."))/2,ART_BTN_Y+(ART_BTN_H-8)/2); tft.print("LOADING..."); }
  File f=SD.open(path.c_str(),FILE_READ); if(!f){ tft.setTextColor(0xE8C4,COL_BG); tft.setCursor(4,100); tft.print("SD open failed"); delay(1200); if(g_screen==SCR_LIST) drawListView(); else drawArtView(); return false; }
  uint32_t fsz=f.size(); f.close(); if(!fsz){ if(g_screen==SCR_LIST) drawListView(); else drawArtView(); return false; }
  g_queued_adf_path=path; g_queued_adf_size=fsz; g_loaded=true; g_loaded_name=noExt(fnOnly(path)); g_loaded_game_idx=g_sel; g_loaded_disk_idx=g_disk_sel;
  espnowSendNotify(g_loaded_name,(g_mode==MODE_ADF)?"ADF":"DSK",fsz);
  if(!espnowSendDisk(fsz)){ tft.setTextColor(0xE8C4,COL_BG); tft.setCursor(4,110); tft.print("Transfer failed!"); delay(1500); }
  if(g_screen==SCR_LIST) drawListView(); else drawArtView(); return true;
}
static void doUnload(){ g_loaded=false; g_loaded_name=""; g_loaded_game_idx=-1; g_loaded_disk_idx=-1; g_queued_adf_path=""; g_queued_adf_size=0; if(espnowIsPaired()) espnowSendEject(); if(g_screen==SCR_LIST) drawListView(); else drawArtView(); }

// ── Touch dispatch ────────────────────────────────────────────────────────────
static const uint32_t COOLDOWN=400;  // longer cooldown for resistive touch
static uint32_t g_last_act=0;

// Wait until finger is lifted — prevents double-fire on slow resistive screens
static void waitRelease(){
  delay(50);
  uint32_t t0=millis();
  while(millis()-t0<800){
    if(!ts.tirqTouched()) break;
    delay(20);
  }
  delay(50);
  g_last_act=millis();
}

static void handleList(uint16_t px,uint16_t py){
  if(py>=STATUS_H&&py<LIST_TOP){ if(px>=4&&px<40){ if(g_mode!=MODE_ADF){ g_mode=MODE_ADF; listImages(g_files); buildGames(); g_sel=g_scroll=g_disk_sel=0; saveKey("LASTMODE","ADF"); waitRelease(); drawListView(); } } else if(px>=40&&px<76){ if(g_mode!=MODE_DSK){ g_mode=MODE_DSK; listImages(g_files); buildGames(); g_sel=g_scroll=g_disk_sel=0; saveKey("LASTMODE","DSK"); waitRelease(); drawListView(); } } else { if(g_scroll>0){ g_scroll--; drawFileList(); drawBotBar(); } } return; }
  if(py>=LIST_BOT&&py<LCD_H-BOTTOM_H){ int mx=(int)g_games.size()-ITEMS_VIS; if(mx<0) mx=0; if(g_scroll<mx){ g_scroll++; drawFileList(); drawBotBar(); } return; }
  if(py>=LCD_H-BOTTOM_H){ if(px<=240){ if(g_games.empty()) return; bool ld=(g_loaded&&g_loaded_game_idx==g_sel); waitRelease(); if(ld){ doUnload(); } else { const GameEntry& gm=g_games[g_sel]; int fi=gm.disk_indices.empty()?gm.first_file_idx:gm.disk_indices[min(g_disk_sel,(int)gm.disk_indices.size()-1)]; doLoad(g_files[fi]); } resetTap(); } else { waitRelease(); g_depair_confirm=false; g_screen=SCR_INFO; drawInfoView(); } return; }
  if(py>=LIST_TOP&&py<LIST_BOT){ int vi=(py-LIST_TOP)/ITEM_H,gi=g_scroll+vi; if(gi<0||gi>=(int)g_games.size()) return; if(gi!=g_sel){ g_sel=gi; g_disk_sel=0; drawFileList(); drawBottomBtns(); g_last_tap=gi; g_last_tap_ms=millis(); } else if(isDoubleTap(gi)){ waitRelease(); g_screen=SCR_ART; drawArtView(); resetTap(); } }
}
static void handleArt(uint16_t px,uint16_t py){
  // PREV — left 70px of header
  if(py<ART_HEADER_H){
    if(px<70){ if(g_sel>0){ g_sel--; g_disk_sel=0; waitRelease(); drawArtView(); } }
    else if(px>=250){ if(g_sel<(int)g_games.size()-1){ g_sel++; g_disk_sel=0; waitRelease(); drawArtView(); } }
    return;
  }
  // INSERT/EJECT — bottom 30px on right panel
  int artBtnY=LCD_H-30;
  if(py>=artBtnY&&px>=ART_INFO_X){
    bool ld=(g_loaded&&g_loaded_game_idx==g_sel);
    waitRelease();
    if(ld){ doUnload(); }
    else { const GameEntry& gm=g_games[g_sel]; int fi=gm.disk_indices.empty()?gm.first_file_idx:gm.disk_indices[min(g_disk_sel,(int)gm.disk_indices.size()-1)]; doLoad(g_files[fi]); }
    return;
  }
  // Disk selector buttons
  if(px>=ART_INFO_X&&!g_games.empty()){
    const GameEntry& gm=g_games[g_sel];
    if(gm.disk_count>1){
      int bw=26,bh=20,gap=4,sx=ART_INFO_X;
      if(py>=ART_HEADER_H+40&&py<=LCD_H-34){
        int hitBtn=(px-sx)/(bw+gap);
        if(hitBtn>=0&&hitBtn<gm.disk_count){ g_disk_sel=hitBtn; drawArtView(); return; }
      }
    }
  }
  // Tap art panel or info area → back to list
  if(px<ART_PANEL_W||(px>=ART_INFO_X&&py<artBtnY)){
    waitRelease(); g_screen=SCR_LIST; drawListView(); resetTap();
  }
}
static void handleInfo(uint16_t px,uint16_t py){
  if(py<STATUS_H){ g_depair_confirm=false; g_screen=SCR_LIST; drawListView(); resetTap(); return; }
  if(py>=100&&py<=132){ int tx=4,pW=70,pG=4; for(int i=0;i<NUM_THEMES;i++){ if(px>=(uint16_t)tx&&px<(uint16_t)(tx+pW)){ applyTheme(i); saveKey("THEME",String(i)); drawInfoView(); return; } tx+=pW+pG; } }
  if(py>=138&&py<=162){ doPairNow(); return; }
  if(espnowIsPaired()){ if(py>=166&&py<=190){ if(!g_depair_confirm){ g_depair_confirm=true; drawInfoView(); } else { if(px<LCD_W/2) espnowClearPairing(); g_depair_confirm=false; drawInfoView(); } return; } if(py>=194&&py<=218){ tft.fillScreen(COL_BG); tft.setCursor(LCD_W/2-40,LCD_H/2); tft.print("RESTARTING..."); delay(800); ESP.restart(); return; } }
  else { if(py>=166&&py<=190){ tft.fillScreen(COL_BG); tft.setCursor(LCD_W/2-40,LCD_H/2); tft.print("RESTARTING..."); delay(800); ESP.restart(); return; } }
}

// ============================================================================
// SETUP
// ============================================================================
void setup(){
  Serial.begin(115200); delay(200);
  Serial.println("\n\n=== Gotek CYD " FW_VERSION " ===");

  pinMode(LED_R_PIN,OUTPUT); pinMode(LED_G_PIN,OUTPUT); pinMode(LED_B_PIN,OUTPUT);
  digitalWrite(LED_R_PIN,HIGH); digitalWrite(LED_G_PIN,HIGH); digitalWrite(LED_B_PIN,HIGH);
  pinMode(LCD_BL_PIN,OUTPUT); analogWrite(LCD_BL_PIN,200);

  applyTheme(0);

  // ── Display init — EXACT pattern from DataDisplayCYD.ino ─────────────────
  tft.init();
  tft.setRotation(1);
  // CYD panel is hardware-inverted — invertDisplay(true) gives correct colours
  tft.invertDisplay(true);
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(1);
  Serial.printf("[TFT] %dx%d rotation=%d\n",(int)tft.width(),(int)tft.height(),(int)tft.getRotation());

  // ── Touch init (XPT2046 on VSPI: CLK=25 MISO=39 MOSI=32 CS=33) ─────────────
  // VSPI is separate from display (HSPI) and SD (HSPI on 18/19/23)
  touchSPI.begin(T_CLK, T_DOUT, T_DIN, T_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);
  Serial.println("[BOOT] Touch OK");

  // ── SD init ───────────────────────────────────────────────────────────────
  // SD is on HSPI with its own dedicated pins: SCK=18 MISO=19 MOSI=23 CS=5
  // Confirmed by sdcard.h from jczn_2432s028r board variant.
  // Completely separate from touch (VSPI 25/39/32) and display (HSPI 13/14).
  pinMode(SD_CS_PIN, OUTPUT); digitalWrite(SD_CS_PIN, HIGH);
  delay(20);

  static SPIClass sdSPI(HSPI);
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS_PIN);
  delay(50);

  bool sdOK = SD.begin(SD_CS_PIN, sdSPI, 50000000);
  if(!sdOK){ SD.end(); delay(500); sdOK = SD.begin(SD_CS_PIN, sdSPI, 25000000); }
  if(!sdOK){ SD.end(); delay(500); sdOK = SD.begin(SD_CS_PIN, sdSPI,  4000000); }

  // ── TJpgDec ──────────────────────────────────────────────────────────────
  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(jpgCB);

  if(!sdOK){
    Serial.println("[BOOT] SD mount failed");
    tft.setTextColor(TFT_RED,TFT_BLACK); tft.setCursor(4,80); tft.print("SD mount failed — FAT32?");
    delay(3000);
  } else {
    Serial.printf("[BOOT] SD OK — %uMB\n",(unsigned)(SD.cardSize()/(1024*1024)));
    ensureConfig(); loadConfig(); listImages(g_files); buildGames();
    g_sel=0; g_scroll=0; g_disk_sel=0;
    Serial.printf("[BOOT] %d games\n",(int)g_games.size());
  }

  g_disk=(uint8_t*)malloc(4096);
  espnowBegin();
  Serial.println("[BOOT] ESP-NOW OK");

  // Splash
  tft.fillScreen(COL_BG); tft.setTextSize(2); tft.setTextColor(COL_ORANGE,COL_BG);
  tft.setCursor(LCD_W/2-tft.textWidth("OMEGAWARE")/2,LCD_H/2-20); tft.print("OMEGAWARE");
  tft.setTextSize(1); tft.setTextColor(COL_MID,COL_BG);
  tft.setCursor(LCD_W/2-tft.textWidth("GOTEK CYD " FW_VERSION)/2,LCD_H/2+4); tft.print("GOTEK CYD " FW_VERSION);
  tft.setTextColor(COL_DIM,COL_BG); String gl=String(g_games.size())+" games loaded";
  tft.setCursor(LCD_W/2-tft.textWidth(gl)/2,LCD_H/2+18); tft.print(gl);
  uint32_t t0=millis(); while(millis()-t0<2000){ if(readTouch()) break; delay(20); }

  g_screen=SCR_LIST; drawListView();
}

// ============================================================================
// LOOP
// ============================================================================
void loop(){
  if(g_espnow_link_just_established){ g_espnow_link_just_established=false; tft.fillRect(0,0,LCD_W,STATUS_H,0x07E0); tft.setTextSize(1); tft.setTextColor(TFT_BLACK,0x07E0); tft.setCursor(LCD_W/2-tft.textWidth("** XIAO LINKED **")/2,5); tft.print("** XIAO LINKED **"); delay(1500); drawStatusBar(); }
  static uint32_t last=0; if(millis()-last<20){ delay(1); return; } last=millis();
  if(!readTouch()) return;
  if(!gT.valid) return;
  uint16_t px=gT.x,py=gT.y; uint32_t now=millis();
  if(now-g_last_act<COOLDOWN) return;
  g_last_act=now;
  switch(g_screen){ case SCR_LIST: handleList(px,py); break; case SCR_ART: handleArt(px,py); break; case SCR_INFO: handleInfo(px,py); break; }
}
