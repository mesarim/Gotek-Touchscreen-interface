// ESP32-S3 (Waveshare 7" RGB Touch LCD) — USB MSC RAM Disk + ADF/DSK Browser + GT911 touch
// Board: ESP32S3 Dev Module
// USB Mode     : USB-OTG (TinyUSB)
// USB CDC      : *** DISABLED *** (REQUIRED for FlashFloppy/Gotek compatibility)
// PSRAM        : OPI PSRAM
// Partition    : 3MB APP / 9MB FATFS
// Flash        : 16MB qio120
// CPU          : 240MHz
//
// Derived from: ESP32_S3_ADF_DSK_Browser_v0_5_2 (2.8" SPI/CST328)
// Converted for: 800x480 RGB parallel panel, GT911 touch via LovyanGFX
//
// Pin notes:
//   RGB bus: 0,1,2,3,5,7,10,14,17,18,21,38,39,40,41,42,45,46,47,48
//   SD_MMC 1-bit: CLK=12, CMD=11, D0=13
//   I2C (expander + GT911): SDA=8, SCL=9
//   Touch INT: GPIO4

#include <Arduino.h>
#include "USB.h"
#include "USBMSC.h"
#include <FS.h>
#include <SD_MMC.h>

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <Wire.h>
#include <vector>
#include <algorithm>
#include <ctype.h>

// ---------- Version ----------
#define FW_VERSION  "v3.4.7"

// ---------- ESP-NOW server (peer-to-peer, no WiFi AP needed) ----------
#include "espnow_server.h"

// ---------- TinyUSB detach/attach ----------
extern "C" {
  bool tud_mounted(void);
  void tud_disconnect(void);
  void tud_connect(void);
}

// ---------- I2C Expander ----------
#define EXPANDER_I2C_SDA   8
#define EXPANDER_I2C_SCL   9
#define EXPANDER_I2C_ADDR  0x20

static void initExpander() {
  Wire.begin(EXPANDER_I2C_SDA, EXPANDER_I2C_SCL);
  Wire.beginTransmission(EXPANDER_I2C_ADDR);
  Wire.write(0x03); Wire.write(0x00);
  Wire.endTransmission();
  Wire.beginTransmission(EXPANDER_I2C_ADDR);
  Wire.write(0x01); Wire.write(0xFF);
  Wire.endTransmission();
  delay(100);
}

// ---------- Display colours ----------
#ifndef TFT_BLACK
  #define TFT_BLACK     0x0000
  #define TFT_WHITE     0xFFFF
  #define TFT_RED       0xF800
  #define TFT_GREEN     0x07E0
  #define TFT_BLUE      0x001F
  #define TFT_CYAN      0x07FF
  #define TFT_DARKGREY  0x7BEF
  #define TFT_YELLOW    0xFFE0
  #define TFT_ORANGE    0xFD20
  #define TFT_GREY      0x8410
  #define WB_ORANGE     0xFD20
#endif

#define LCD_ROTATION  0
static int gW = 800, gH = 480;

// ---------- LovyanGFX: 7" RGB panel + GT911 touch ----------
class LGFX_Local : public lgfx::LGFX_Device {
public:
  lgfx::Bus_RGB     _bus_instance;
  lgfx::Panel_RGB   _panel_instance;
  lgfx::Light_PWM   _light_instance;
  lgfx::Touch_GT911 _touch_instance;

  LGFX_Local() {
    {
      auto cfg = _panel_instance.config();
      cfg.memory_width  = 800; cfg.memory_height = 480;
      cfg.panel_width   = 800; cfg.panel_height  = 480;
      cfg.offset_x = 0; cfg.offset_y = 0;
      _panel_instance.config(cfg);
    }
    {
      auto cfg = _panel_instance.config_detail();
      cfg.use_psram = 2;  // double buffer — eliminates all tearing
      _panel_instance.config_detail(cfg);
    }
    {
      auto cfg = _bus_instance.config();
      cfg.panel = &_panel_instance;
      cfg.pin_d0  = 14; cfg.pin_d1  = 38; cfg.pin_d2  = 18; cfg.pin_d3  = 17; cfg.pin_d4  = 10;
      cfg.pin_d5  = 39; cfg.pin_d6  = 0;  cfg.pin_d7  = 45; cfg.pin_d8  = 48; cfg.pin_d9  = 47; cfg.pin_d10 = 21;
      cfg.pin_d11 = 1;  cfg.pin_d12 = 2;  cfg.pin_d13 = 42; cfg.pin_d14 = 41; cfg.pin_d15 = 40;
      cfg.pin_henable = 5; cfg.pin_vsync = 3; cfg.pin_hsync = 46; cfg.pin_pclk = 7;
      cfg.freq_write        = 16000000;
      cfg.hsync_polarity    = 0; cfg.hsync_front_porch = 40; cfg.hsync_pulse_width = 48; cfg.hsync_back_porch  = 88;
      cfg.vsync_polarity    = 0; cfg.vsync_front_porch = 13; cfg.vsync_pulse_width =  3; cfg.vsync_back_porch  = 32;
      cfg.pclk_active_neg   = 1; cfg.de_idle_high = 0; cfg.pclk_idle_high = 0;
      _bus_instance.config(cfg);
    }
    _panel_instance.setBus(&_bus_instance);
    {
      auto cfg = _light_instance.config();
      cfg.pin_bl = -1; cfg.invert = false; cfg.freq = 44100; cfg.pwm_channel = 7;
      _light_instance.config(cfg);
    }
    _panel_instance.setLight(&_light_instance);
    {
      auto cfg = _touch_instance.config();
      cfg.x_min = 0; cfg.x_max = 799; cfg.y_min = 0; cfg.y_max = 479;
      cfg.pin_int = 4; cfg.pin_rst = -1;
      cfg.i2c_port = I2C_NUM_0; cfg.i2c_addr = 0x5D;
      cfg.pin_sda = 8; cfg.pin_scl = 9; cfg.freq = 400000;
      cfg.offset_rotation = 0;
      _touch_instance.config(cfg);
    }
    _panel_instance.setTouch(&_touch_instance);
    setPanel(&_panel_instance);
  }
};
static LGFX_Local tft;

// ---------- Forward declarations ----------


static void drawFullUI();
static void drawListAndCover();
static bool doLoadSelected(const String& adfPath);
static void doUnload();






// ---------- UI helpers ----------
static uint16_t uiY = 0;
static void uiInit() {
  tft.init();
  tft.setRotation(LCD_ROTATION);
  gW = tft.width(); gH = tft.height();
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.setFont(&lgfx::fonts::DejaVu9);
  uiY = 0;
  tft.setBrightness(220);
}
// Simple error display for boot failures
static void uiERR(const String& s){
  tft.setFont(&lgfx::fonts::DejaVu9);
  tft.setTextSize(1);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.setCursor(8, 200);
  tft.print("ERR: " + s);
}

// ---------- MSC RAM disk (0.5.2 geometry — proven working) ----------
USBMSC MSC;

static bool g_usb_online = false;

static const uint16_t SECTOR_SIZE         = 512;
static const uint32_t TOTAL_SECTORS       = 2048;
static const uint16_t RESERVED_SECTORS    = 1;
static const uint16_t SECTORS_PER_FAT     = 6;
static const uint16_t ROOT_DIR_SECTORS    = 4;
static const uint8_t  SECTORS_PER_CLUSTER = 1;
static const uint8_t  NUM_FATS            = 1;
static const uint16_t ROOT_ENTRIES        = 64;

static const uint32_t FAT_LBA  = RESERVED_SECTORS;
static const uint32_t ROOT_LBA = FAT_LBA  + (NUM_FATS * SECTORS_PER_FAT);
static const uint32_t DATA_LBA = ROOT_LBA + ROOT_DIR_SECTORS;

static const uint32_t DATA_SECTORS    = (TOTAL_SECTORS - DATA_LBA);
static const uint32_t MAX_FILE_BYTES  = DATA_SECTORS * SECTOR_SIZE;
static const uint32_t ADF_DEFAULT_SIZE = 901120;

enum DiskMode { MODE_ADF=0, MODE_DSK=1 };
static DiskMode g_mode = MODE_ADF;

static inline const char* getOutputFilename() {
  return (g_mode == MODE_ADF) ? "DISK.ADF" : "DISK.DSK";
}

uint8_t* g_disk = nullptr;
extern "C" void* ps_malloc(size_t size);

static inline void wr16(uint8_t* p, int o, uint16_t v) { p[o]=(uint8_t)v; p[o+1]=(uint8_t)(v>>8); }
static inline void wr32(uint8_t* p, int o, uint32_t v) { p[o]=(uint8_t)v; p[o+1]=(uint8_t)(v>>8); p[o+2]=(uint8_t)(v>>16); p[o+3]=(uint8_t)(v>>24); }

static void build_boot_sector(uint8_t* bs) {
  memset(bs,0,SECTOR_SIZE);
  bs[0]=0xEB; bs[1]=0x3C; bs[2]=0x90; memcpy(&bs[3],"MSDOS5.0",8);
  wr16(bs,11,SECTOR_SIZE); bs[13]=SECTORS_PER_CLUSTER;
  wr16(bs,14,RESERVED_SECTORS); bs[16]=NUM_FATS;
  wr16(bs,17,ROOT_ENTRIES); wr16(bs,19,TOTAL_SECTORS);
  bs[21]=0xF8; wr16(bs,22,SECTORS_PER_FAT);
  wr16(bs,24,32); wr16(bs,26,64); wr32(bs,28,0); wr32(bs,32,0);
  bs[36]=0x80; bs[38]=0x29; wr32(bs,39,0x12345678);
  memcpy(&bs[43],"ESP32MSC   ",11); memcpy(&bs[54],"FAT12   ",8);
  bs[510]=0x55; bs[511]=0xAA;
}
static void fat12_set(uint8_t* fat, uint16_t cl, uint16_t v) {
  uint32_t i=(cl*3)/2;
  if((cl&1)==0){ fat[i]=(uint8_t)(v&0xFF); fat[i+1]=(uint8_t)((fat[i+1]&0xF0)|((v>>8)&0x0F)); }
  else{ fat[i]=(uint8_t)((fat[i]&0x0F)|((v<<4)&0xF0)); fat[i+1]=(uint8_t)((v>>4)&0xFF); }
}
static void build_fat(uint8_t* fat, uint32_t fsz) {
  memset(fat,0,SECTORS_PER_FAT*SECTOR_SIZE);
  fat[0]=0xF8; fat[1]=0xFF; fat[2]=0xFF;
  const uint32_t bpc=SECTOR_SIZE*SECTORS_PER_CLUSTER;
  uint32_t need=(fsz+bpc-1)/bpc;
  for(uint32_t i=0;i<need;i++){
    uint16_t c=(uint16_t)(2+i);
    uint16_t v=(i==(need-1))?0x0FFF:(c+1);
    fat12_set(fat,c,v);
  }
}
static void make_83_name(const char* fn, char n[8], char e[3]) {
  memset(n,' ',8); memset(e,' ',3);
  char tmp[32]; size_t L=strlen(fn);
  if(L>=sizeof(tmp)) L=sizeof(tmp)-1;
  memcpy(tmp,fn,L); tmp[L]='\0';
  for(size_t i=0;i<L;i++) tmp[i]=(char)toupper((unsigned char)tmp[i]);
  const char* dot=strrchr(tmp,'.');
  size_t nl=dot?(size_t)(dot-tmp):strlen(tmp);
  size_t el=(dot&&*(dot+1))?strlen(dot+1):0;
  for(size_t i=0;i<nl&&i<8;i++) n[i]=tmp[i];
  for(size_t i=0;i<el&&i<3;i++) e[i]=dot[1+i];
}
static void build_root(uint8_t* root, const char* name, uint32_t fsz) {
  memset(root,0,ROOT_DIR_SECTORS*SECTOR_SIZE);
  uint8_t* de=root; char n[8],e[3]; make_83_name(name,n,e);
  memcpy(&de[0],n,8); memcpy(&de[8],e,3);
  de[11]=0x20; wr16(de,26,2); wr32(de,28,fsz);
}
static void build_volume_with_file(const char* outName, uint32_t fsz) {
  if(fsz>MAX_FILE_BYTES) fsz=MAX_FILE_BYTES;
  memset(g_disk,0,TOTAL_SECTORS*SECTOR_SIZE);
  build_boot_sector(g_disk);
  uint8_t* fat  = g_disk+(RESERVED_SECTORS)*SECTOR_SIZE;
  uint8_t* root = g_disk+(RESERVED_SECTORS+SECTORS_PER_FAT)*SECTOR_SIZE;
  build_fat(fat,fsz); build_root(root,outName,fsz);
}
static int32_t onRead(uint32_t lba,uint32_t off,void* buf,uint32_t n) {
  uint32_t s=lba*SECTOR_SIZE+off; if(s+n>TOTAL_SECTORS*SECTOR_SIZE) return 0;
  memcpy(buf,g_disk+s,n); return (int32_t)n;
}
static int32_t onWrite(uint32_t lba,uint32_t off,uint8_t* buf,uint32_t n) {
  uint32_t s=lba*SECTOR_SIZE+off; if(s+n>TOTAL_SECTORS*SECTOR_SIZE) return 0;
  memcpy(g_disk+s,buf,n); return (int32_t)n;
}
static void usbEventCallback(void*,esp_event_base_t,int32_t,void*){}

static uint32_t g_rev_counter=1;
static void bumpInquiryRevision(){
  char rev[8]; snprintf(rev,sizeof(rev),"%lu",(unsigned long)g_rev_counter++);
  MSC.productRevision(rev);
}
static void hardDetach(){
  MSC.mediaPresent(false); delay(100); tud_disconnect(); delay(500);
  g_usb_online=false;
}
static void hardAttach(){
  bumpInquiryRevision(); MSC.mediaPresent(true); delay(50); tud_connect(); delay(200);
  g_usb_online=true;
}

// ---------- SD card (SD_MMC 1-bit) ----------
#define SD_MOSI 11
#define SD_CLK  12
#define SD_MISO 13

static bool listImages(fs::FS& fs, std::vector<String>& out) {
  out.clear();
  String modeDir = (g_mode == MODE_ADF) ? "/ADF" : "/DSK";
  String ext1    = (g_mode == MODE_ADF) ? ".ADF" : ".DSK";

  // Primary scan: /ADF/<GameFolder>/<file>.adf  or  /ADF/<file>.adf
  File root = SD_MMC.open(modeDir.c_str());
  if (root && root.isDirectory()) {
    File gameDir;
    while ((gameDir = root.openNextFile())) {
      String entryName = gameDir.name();
      if (!entryName.startsWith("/")) entryName = modeDir + "/" + entryName;

      if (gameDir.isDirectory()) {
        // Subfolder: /ADF/GameName/GameName.adf
        File entry;
        while ((entry = gameDir.openNextFile())) {
          String fname = entry.name();
          int slash = fname.lastIndexOf('/');
          if (slash >= 0) fname = fname.substring(slash + 1);
          String upper = fname; upper.toUpperCase();
          if (upper.endsWith(ext1) || upper.endsWith(".IMG") || upper.endsWith(".ADZ")) {
            String fullPath = entryName + "/" + fname;
            if (!fullPath.startsWith("/")) fullPath = "/" + fullPath;
            out.push_back(fullPath);
          }
          entry.close();
        }
      } else {
        // Flat: /ADF/GameName.adf
        String fname = entryName;
        int slash = fname.lastIndexOf('/');
        if (slash >= 0) fname = fname.substring(slash + 1);
        String upper = fname; upper.toUpperCase();
        if (upper.endsWith(ext1) || upper.endsWith(".IMG") || upper.endsWith(".ADZ")) {
          out.push_back(entryName);
        }
      }
      gameDir.close();
    }
    root.close();
  }

  // Fallback: also scan root for any .ADF/.DSK files (legacy / flat SD layout)
  // Only add if no file with the same basename already exists from subfolder scan
  File rootDir = SD_MMC.open("/");
  if (rootDir) {
    File entry;
    while ((entry = rootDir.openNextFile())) {
      if (!entry.isDirectory()) {
        String fname = entry.name();
        int slash = fname.lastIndexOf('/');
        if (slash >= 0) fname = fname.substring(slash + 1);
        String upper = fname; upper.toUpperCase();
        if (upper.endsWith(ext1) || upper.endsWith(".IMG")) {
          String fullPath = "/" + fname;
          // Check for duplicate by basename (catches same file in both root and subfolder)
          String baseName = fname; baseName.toLowerCase();
          bool dup = false;
          for (const auto& e : out) {
            String existBase = e; existBase.toLowerCase();
            int s = existBase.lastIndexOf('/');
            if (s >= 0) existBase = existBase.substring(s + 1);
            if (existBase == baseName) { dup = true; break; }
          }
          if (!dup) out.push_back(fullPath);
        }
      }
      entry.close();
    }
    rootDir.close();
  }


  std::sort(out.begin(), out.end());
  return !out.empty();
}
static File openNamedImage(const String& path){
  String p=path; if(p.length()==0||p[0]!='/') p="/"+p;
  return SD_MMC.open(p,FILE_READ);
}

// ---------- NFO/JPG helpers ----------
static String basenameNoExt(const String& p){
  int s=p.lastIndexOf('/'); int d=p.lastIndexOf('.');
  String b=(s>=0)?p.substring(s+1):p;
  if(d>s) b=b.substring(0,d-(s>=0?s+1:0));
  return b;
}
static bool existsCaseInsensitiveInDir(const String& dir, const String& targetName, String& outPath){
  String tgtLower = targetName; tgtLower.toLowerCase();
  File d = SD_MMC.open(dir.c_str());
  if(!d || !d.isDirectory()) return false;
  File f = d.openNextFile();
  while(f){
    if(!f.isDirectory()){
      String nm = f.name();
      int slash = nm.lastIndexOf('/');
      if(slash >= 0) nm = nm.substring(slash+1);
      String nmLower = nm; nmLower.toLowerCase();
      if(nmLower == tgtLower){ outPath = dir + "/" + nm; f.close(); d.close(); return true; }
    }
    f.close(); f = d.openNextFile();
  }
  d.close(); return false;
}
static bool existsCaseInsensitiveInRoot(const String& targetName, String& outPath){
  return existsCaseInsensitiveInDir("/", targetName, outPath);
}
static bool findNFOFor(const String& adfPath, String& outNFO){
  String base = basenameNoExt(filenameOnly(adfPath));
  String dir  = parentDir(adfPath);
  // Try exact basename first (e.g. Cannon-Fodder-1.nfo)
  if(existsCaseInsensitiveInDir(dir, base+".nfo", outNFO)) return true;
  // Try game base name without disk number (e.g. Cannon-Fodder.nfo)
  String gameBase = getGameBaseName(adfPath);
  if(gameBase != base){
    if(existsCaseInsensitiveInDir(dir, gameBase+".nfo", outNFO)) return true;
  }
  // Fallback: root with exact name
  if(existsCaseInsensitiveInRoot(base+".nfo", outNFO)) return true;
  // Fallback: root with game base name
  if(gameBase != base)
    return existsCaseInsensitiveInRoot(gameBase+".nfo", outNFO);
  return false;
}
static bool findJPGFor(const String& adfPath, String& outJPG){
  String base = basenameNoExt(filenameOnly(adfPath));
  String dir  = parentDir(adfPath);
  const char* exts[] = {".jpg",".jpeg",".JPG",".JPEG",".Jpg",".Jpeg"};
  // Look in same folder as the ADF first
  for(auto e : exts){
    String cand = dir + "/" + base + e;
    if(SD_MMC.exists(cand.c_str())){ outJPG = cand; return true; }
  }
  // Fallback to root
  for(auto e : exts){
    String cand = "/" + base + e;
    if(SD_MMC.exists(cand.c_str())){ outJPG = cand; return true; }
  }
  return false;
}

// ---------- Word-wrap helpers ----------

// ---------- Touch: GT911 via LovyanGFX ----------
struct TouchData { uint8_t points; uint16_t rawX,rawY,strength; } gTouch={0,0,0,0};

static bool Touch_ReadFrame(){
  int32_t rx,ry;
  if(!tft.getTouch(&rx,&ry)){ gTouch.points=0; return false; }
  gTouch.points=1; gTouch.rawX=(uint16_t)rx; gTouch.rawY=(uint16_t)ry; gTouch.strength=128;
  return true;
}
static bool getTouchXY(uint16_t* x,uint16_t* y){
  if(gTouch.points==0) return false;
  *x=constrain(gTouch.rawX,0,(uint16_t)(gW-1));
  *y=constrain(gTouch.rawY,0,(uint16_t)(gH-1));
  return true;
}

static bool  g_info_showing    = false;
static int   g_info_pair_btn_y = 0;
static void  doPairNow();  // defined after layout constants

#define NUM_STARS 80
static int16_t star_x[NUM_STARS], star_y[NUM_STARS], star_speed[NUM_STARS];
static bool g_loop_cracktro  = false;  // set by LOOP=1 in CONFIG.TXT
static bool g_wireless_mode  = false;  // set by MODE=WIRELESS in CONFIG.TXT

static void initStars() {
  for(int i=0;i<NUM_STARS;i++){
    star_x[i]=random(0,gW); star_y[i]=random(0,gH); star_speed[i]=random(1,4);
  }
}


static void drawCracktroSplash() {
  initStars();

  const char* scrollText =
    "       GOTEK TOUCHSCREEN INTERFACE  *  CODED BY MEZ AND DIMMY OF OMEGAWARE  *  "
    "THE ULTIMATE RETRO DISK LOADER FOR AMIGA AND CPC  ...  "
    "WIFI WEB INTERFACE - THEME ENGINE - FAT12 RAM DISK  ...  "
    "GREETINGS TO THE GREENFORD COMPUTER CLUB  ...  "
    "KEEP THE SCENE ALIVE  ...  OMEGAWARE 2026  *  "
    "       ";
  const int scrollLen = strlen(scrollText);
  int scrollPos = 0;
  const int charW = 12;

  uint16_t copperColors[] = {
    0xF800,0xF920,0xFAA0,0xFC00,0xFDE0,0xEFE0,0x87E0,0x07E0,
    0x07F0,0x07FF,0x041F,0x001F,0x801F,0xF81F,0xF810,0xF800
  };
  uint16_t sineColors[] = {
    0xF800,0xFBE0,0xFFE0,0x07E0,0x07FF,0x001F,0xF81F,0xF800
  };
  const int numCopper=16, numSineColors=8;
  int frame=0;

  const unsigned long CRACKTRO_MS = 6000;
  unsigned long startMs = millis();

  // use_psram=2 gives double buffering — draw to back buffer, display() flips atomically
  tft.fillScreen(TFT_BLACK);
  tft.display();

  while(true){
    if(Touch_ReadFrame()){
      unsigned long t0=millis();
      while(Touch_ReadFrame()&&millis()-t0<500) delay(10);
      break;
    }
    if(!g_loop_cracktro && millis()-startMs>=CRACKTRO_MS) break;

    int copperY=gH/2-40+(int)(55.0f*sinf((float)frame*0.05f));
    int prevCopperY=gH/2-40+(int)(55.0f*sinf((float)(frame-1)*0.05f));

    // Stars — erase old pixel, move, draw new
    for(int i=0;i<NUM_STARS;i++){
      tft.drawPixel(star_x[i],star_y[i],TFT_BLACK);
      star_x[i]-=star_speed[i];
      if(star_x[i]<0){ star_x[i]=gW-1; star_y[i]=random(0,gH-40); }
      uint16_t col=(star_speed[i]==3)?TFT_WHITE:
                   (star_speed[i]==2)?(uint16_t)0x7BEF:(uint16_t)0x4208;
      tft.drawPixel(star_x[i],star_y[i],col);
    }

    // Copper bars — erase previous band fringe, draw new
    tft.fillRect(0,max(0,prevCopperY-3),gW,3,TFT_BLACK);
    tft.fillRect(0,min(gH-43,prevCopperY+numCopper*4),gW,3,TFT_BLACK);
    for(int i=0;i<numCopper;i++){
      int by=copperY+i*4;
      if(by>=0&&by<gH-40) tft.fillRect(0,by,gW,3,copperColors[i]);
    }

    // Text — erase at PREVIOUS copperY, draw at CURRENT copperY
    tft.setTextSize(4); tft.setTextWrap(false);
    { const char* s="MEZ & DIMMY"; int tw=strlen(s)*24;
      int pty=prevCopperY-68; if(pty<0)pty=4;
      int ty=copperY-68; if(ty<0)ty=4;
      tft.setTextColor(TFT_BLACK); tft.setCursor((gW-tw)/2,pty); tft.print(s);
      tft.setTextColor(sineColors[(frame/4)%numSineColors]);
      tft.setCursor((gW-tw)/2,ty); tft.print(s); }

    tft.setTextSize(3);
    { const char* s="- OMEGAWARE -"; int tw=strlen(s)*18;
      tft.setTextColor(TFT_BLACK); tft.setCursor((gW-tw)/2,prevCopperY+68); tft.print(s);
      tft.setTextColor((frame%40<20)?TFT_CYAN:TFT_WHITE);
      tft.setCursor((gW-tw)/2,copperY+68); tft.print(s); }

    tft.setTextSize(2);
    { const char* s="GOTEK TOUCHSCREEN INTERFACE"; int tw=strlen(s)*12;
      tft.setTextColor(TFT_BLACK); tft.setCursor((gW-tw)/2,prevCopperY+94); tft.print(s);
      tft.setTextColor(0x7BEF); tft.setCursor((gW-tw)/2,copperY+94); tft.print(s); }

    { const char* s="TAP TO CONTINUE"; int tw=strlen(s)*12;
      tft.setTextSize(2);
      tft.setTextColor((frame/15)%2==0 ? (uint16_t)0x4A8A : TFT_BLACK);
      tft.setCursor((gW-tw)/2,gH-72); tft.print(s); }

    // Scroll bar
    tft.fillRect(0,gH-36,gW,36,0x0010);
    tft.setTextSize(2); tft.setTextColor(TFT_YELLOW); tft.setTextWrap(false);
    { int sc=scrollPos/charW, px=scrollPos%charW;
      tft.setCursor(-px,gH-36+4);
      for(int c=0;c<(gW/charW)+3;c++){
        char buf[2]={scrollText[(sc+c)%scrollLen],0}; tft.print(buf); } }

    // Atomic buffer flip
    // Atomic buffer flip — panel reads from front while we drew to back
    tft.display();

    scrollPos+=2; frame++;
    delay(33);
  }

  tft.fillScreen(TFT_BLACK);
  tft.display();
}

// ============================================================================
#define LCD_WIDTH    800
#define LCD_HEIGHT   480
#define STATUS_H     24
#define COVER_W      200
#define COVER_ART_X  8
#define COVER_ART_Y  (STATUS_H + 6)
#define COVER_ART_W  184
#define COVER_ART_H  150
#define AZ_W         20
#define AZ_X         (LCD_WIDTH - AZ_W)
#define LIST_X       COVER_W
#define LIST_W       (LCD_WIDTH - COVER_W - AZ_W)
#define MODE_BAR_H   24
#define LIST_TOP     (STATUS_H + MODE_BAR_H)
#define NOW_PLAY_H   32
#define BOTTOM_H     52
#define LIST_BOTTOM  (LCD_HEIGHT - BOTTOM_H - NOW_PLAY_H)
#define LIST_ITEM_H  ((LIST_BOTTOM - LIST_TOP) / 8)
#define ITEMS_VIS    8

// ============================================================================
// THEME SYSTEM — runtime cycling colours
// ============================================================================
struct Theme {
  const char* name;
  uint16_t bg, panel, bar, sel, sep, dim, mid, lit;
  uint16_t green, orange, amber, blue, now, accent;
  uint16_t circ;       // unselected circle fill
  uint16_t circ_text;  // text on unselected circle
};

static const Theme THEMES[] = {
  // NAVY
  { "NAVY",
    0x1082, 0x18C3, 0x2104, 0x2945, 0x2103, 0x4A8A, 0x6B6D, 0x9BD6,
    0x2D6B, 0xFC60, 0xFD00, 0x4C5F, 0x0B26, 0x5D1F,
    0x3186, TFT_WHITE },
  // EMBER
  { "EMBER",
    0x0800, 0x1000, 0x1800, 0x5820, 0x1000, 0x5820, 0x8440, 0xC8A0,
    0x0560, 0xFF40, 0xFC00, 0x4A9F, 0x1000, 0x7800,
    0x3800, TFT_WHITE },
  // MATRIX
  { "MATRIX",
    0x0020, 0x0040, 0x0060, 0x0340, 0x0040, 0x0340, 0x0580, 0x07C0,
    0x07E0, 0x07E0, 0x0FE0, 0x07FF, 0x0060, 0x0380,
    0x0300, 0x07C0 },
  // PAPER
  { "PAPER",
    0xEF5C, 0xF7BE, 0xFFFF, 0xC618, 0xCE59, 0x8C51, 0x6B4D, 0x2124,
    0x0680, 0xE880, 0xFD00, 0x0C5F, 0xCE59, 0x4810,
    0xC618, TFT_WHITE },
  // SYNTHWAVE
  { "SYNTHWAVE",
    0x1001, 0x2003, 0x3005, 0x5008, 0x2003, 0x600C, 0x900F, 0xC09F,
    0x4BE0, 0xFC1F, 0xE81F, 0xA01F, 0x2003, 0x8010,
    0x5008, 0xE81F },
  // GOLD
  { "GOLD",
    0x1000, 0x1800, 0x2000, 0x4200, 0x1800, 0x5240, 0x7440, 0xC5A0,
    0x0560, 0xFCA0, 0xFCC0, 0xFCA0, 0x1800, 0x3200,
    0x3200, 0xFCC0 },
};
static const int NUM_THEMES = 6;
static int g_theme_idx = 0;

// Runtime colour vars — updated when theme changes
static uint16_t COL_BG, COL_PANEL, COL_BAR, COL_SEL, COL_SEP;
static uint16_t COL_DIM, COL_MID, COL_LIT;
static uint16_t COL_GREEN, COL_ORANGE, COL_AMBER, COL_BLUE, COL_NOW, COL_ACCENT;
static uint16_t COL_CIRC, COL_CIRC_TEXT;

static void applyTheme(int idx){
  g_theme_idx = idx % NUM_THEMES;
  const Theme& t = THEMES[g_theme_idx];
  COL_BG=t.bg; COL_PANEL=t.panel; COL_BAR=t.bar; COL_SEL=t.sel;
  COL_SEP=t.sep; COL_DIM=t.dim; COL_MID=t.mid; COL_LIT=t.lit;
  COL_GREEN=t.green; COL_ORANGE=t.orange; COL_AMBER=t.amber;
  COL_BLUE=t.blue; COL_NOW=t.now; COL_ACCENT=t.accent;
  COL_CIRC=t.circ; COL_CIRC_TEXT=t.circ_text;
}
static void cycleTheme(){
  applyTheme((g_theme_idx+1) % NUM_THEMES);
  // Save to CONFIG.TXT
  // Read existing config first, then rewrite with updated THEME line
  String lines = "";
  bool themeWritten = false;
  File fr = SD_MMC.open("/CONFIG.TXT", FILE_READ);
  if(fr){
    while(fr.available()){
      String line = fr.readStringUntil('\n'); line.trim();
      if(line.startsWith("THEME=")){
        lines += "THEME=" + String(g_theme_idx) + "\n";
        themeWritten = true;
      } else {
        lines += line + "\n";
      }
    }
    fr.close();
  }
  if(!themeWritten) lines += "THEME=" + String(g_theme_idx) + "\n";
  File fw = SD_MMC.open("/CONFIG.TXT", FILE_WRITE);
  if(fw){ fw.print(lines); fw.close(); }
  drawFullUI();
}
static void saveMode() {
  String lines = ""; bool written = false;
  File fr = SD_MMC.open("/CONFIG.TXT", FILE_READ);
  if (fr) {
    while (fr.available()) {
      String line = fr.readStringUntil('\n'); line.trim();
      if (line.startsWith("MODE=")) {
        lines += "MODE=" + String(g_wireless_mode ? "WIRELESS" : "STANDALONE") + "\n";
        written = true;
      } else { lines += line + "\n"; }
    }
    fr.close();
  }
  if (!written) lines += "MODE=" + String(g_wireless_mode ? "WIRELESS" : "STANDALONE") + "\n";
  File fw = SD_MMC.open("/CONFIG.TXT", FILE_WRITE);
  if (fw) { fw.print(lines); fw.close(); }
}

static void setWirelessMode(bool wireless) {
  g_wireless_mode = wireless;
  saveMode();
  drawFullUI();
}

static void ensureConfig(){
  // If CONFIG.TXT is missing or empty, write defaults so the user
  // always has a starting point and WiFi works out of the box.
  File f = SD_MMC.open("/CONFIG.TXT", FILE_READ);
  bool needsWrite = true;
  if(f){
    if(f.size() > 4) needsWrite = false;
    f.close();
  }
  if(needsWrite){
    File fw = SD_MMC.open("/CONFIG.TXT", FILE_WRITE);
    if(fw){
      fw.print(
        "# Gotek Touchscreen Interface — Configuration\n"
        "# Edit this file to customise behaviour.\n"
        "#\n"
        "THEME=0\n"
        "LOOP=0\n"
        "MODE=STANDALONE\n"
        "LASTMODE=ADF\n"
        "#\n"
        "# WiFi AP settings (XIAO connects to this)\n"
        "WIFI_SSID=GotekWifi\n"
        "WIFI_PASS=gotek1234\n"
        "WIFI_CHANNEL=6\n"
        "WIFI_IP=192.168.4.1\n"
      );
      fw.close();
    }
  }

  // Create /ADF directory and README if missing
  if(!SD_MMC.exists("/ADF")){
    SD_MMC.mkdir("/ADF");
    File r = SD_MMC.open("/ADF/README.TXT", FILE_WRITE);
    if(r){
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
        "OPTIONAL EXTRAS (same folder as the .adf files):\r\n"
        "  GameName.jpg  — cover art (JPEG, any size)\r\n"
        "  GameName.nfo  — info/description (plain text)\r\n"
        "\r\n"
        "NOTES:\r\n"
        "  - Folder name becomes the game title in the browser\r\n"
        "  - Multi-disk games are grouped automatically\r\n"
        "  - Disk numbering: -1 -2 -3 or _1 _2 _3 or (1) (2) (3)\r\n"
        "  - Supported formats: .adf .img .adz\r\n"
      );
      r.close();
    }
  }

  // Create /DSK directory and README if missing
  if(!SD_MMC.exists("/DSK")){
    SD_MMC.mkdir("/DSK");
    File r = SD_MMC.open("/DSK/README.TXT", FILE_WRITE);
    if(r){
      r.print(
        "GOTEK TOUCHSCREEN — DSK GAMES FOLDER\r\n"
        "=====================================\r\n"
        "\r\n"
        "Place Amstrad CPC disk images (.dsk) in this folder.\r\n"
        "\r\n"
        "SINGLE DISK GAME:\r\n"
        "  /DSK/GameName/GameName.dsk\r\n"
        "\r\n"
        "MULTI DISK GAME:\r\n"
        "  /DSK/GameName/GameName-1.dsk\r\n"
        "  /DSK/GameName/GameName-2.dsk\r\n"
        "\r\n"
        "OPTIONAL EXTRAS (same folder as the .dsk files):\r\n"
        "  GameName.jpg  — cover art (JPEG, any size)\r\n"
        "  GameName.nfo  — info/description (plain text)\r\n"
        "\r\n"
        "NOTES:\r\n"
        "  - Same folder structure as ADF\r\n"
        "  - Switch between ADF and DSK mode using the\r\n"
        "    ADF/DSK buttons in the mode bar\r\n"
        "  - Supported formats: .dsk .img\r\n"
      );
      r.close();
    }
  }
}

static void loadTheme(){
  File f = SD_MMC.open("/CONFIG.TXT", FILE_READ);
  if(!f){ applyTheme(0); return; }
  while(f.available()){
    String line = f.readStringUntil('\n'); line.trim();
    if(line.startsWith("#")) continue;
    int eq = line.indexOf('='); if(eq<0) continue;
    String key = line.substring(0,eq); key.trim();
    String val = line.substring(eq+1); val.trim();
    if(key == "THEME") applyTheme(val.toInt());
    else if(key == "LOOP") g_loop_cracktro = (val == "1" || val == "true");
    else if(key == "MODE") g_wireless_mode = (val == "WIRELESS");
  }
  f.close();
}


// ============================================================================
// GAME ENTRY — groups multi-disk games
// ============================================================================
struct GameEntry {
  String name;            // display name (from NFO or basename)
  int    first_file_idx;  // index into g_files[] for disk 1
  int    disk_count;      // 1 = single, 2+ = multi-disk
  String jpg_path;        // cover art path (may be empty)
  std::vector<int> disk_indices; // all disk indices in order
};

// ============================================================================
// STATE
// ============================================================================
static std::vector<String>    g_files;    // all raw file paths
static std::vector<GameEntry> g_games;    // grouped game list
static int    g_sel       = 0;            // selected game index
static int    g_scroll    = 0;            // top of visible window
static int    g_disk_sel  = 0;            // selected disk within multi-disk game
static String g_loaded_name = "";
static bool   g_loaded    = false;
static int    g_loaded_game_idx = -1;     // which game is loaded
static int    g_loaded_disk_idx = -1;     // which disk of that game

// ============================================================================
// FILE HELPERS
// ============================================================================
static String filenameOnly(const String& p){
  int s=p.lastIndexOf('/'); return (s>=0)?p.substring(s+1):p;
}
static String parentDir(const String& p){
  int s=p.lastIndexOf('/'); return (s>0)?p.substring(0,s):"/";
}
static String getGameBaseName(const String& fullPath){
  String base=basenameNoExt(filenameOnly(fullPath));
  int dash=base.lastIndexOf('-');
  if(dash>0&&dash<(int)base.length()-1){
    String suffix=base.substring(dash+1);
    bool isNum=true;
    for(int i=0;i<(int)suffix.length();i++) if(!isDigit(suffix[i])){ isNum=false; break; }
    if(isNum) return base.substring(0,dash);
  }
  return base;
}
static int getDiskNumber(const String& fullPath){
  String base=basenameNoExt(filenameOnly(fullPath));
  int dash=base.lastIndexOf('-');
  if(dash>0&&dash<(int)base.length()-1){
    String suffix=base.substring(dash+1);
    int num=suffix.toInt();
    if(num>0) return num;
  }
  return 0;
}

// ============================================================================
// NFO PARSING
// ============================================================================
static String readSmallTextFile(const String& path, size_t maxBytes=512){
  File f=SD_MMC.open(path,FILE_READ); if(!f) return "";
  String s; s.reserve(min((size_t)f.size(),maxBytes));
  while(f.available()&&s.length()<(int)maxBytes) s+=(char)f.read();
  f.close(); s.replace("\r\n","\n"); s.replace("\r","\n"); return s;
}
static void parseNFO(const String& txt,String& outTitle,String& outBlurb){
  outTitle=""; outBlurb=""; if(!txt.length()) return;
  std::vector<String> lines; int pos=0;
  while(pos<(int)txt.length()){
    int nl=txt.indexOf('\n',pos); if(nl<0) nl=txt.length();
    String L=txt.substring(pos,nl); L.trim(); lines.push_back(L); pos=nl+1;
  }
  bool gotT=false,gotB=false;
  for(size_t i=0;i<lines.size();++i){
    const String& L=lines[i];
    if(!gotT&&L.startsWith("Title:")){ outTitle=L.substring(6); outTitle.trim(); gotT=true; continue; }
    if(!gotB&&(L.startsWith("Blurb:")||L.startsWith("Description:"))){
      outBlurb=L.substring(L.indexOf(':')+1); outBlurb.trim();
      for(size_t j=i+1;j<lines.size();++j){
        const String& Lj=lines[j]; if(Lj.indexOf(':')>0) break;
        if(Lj.length()){ if(outBlurb.length()) outBlurb+="\n"; outBlurb+=Lj; }
      }
      gotB=true;
    }
  }
  if(!gotT){ for(auto& L:lines){ if(L.length()){ outTitle=L; break; } } }
  if(!gotB){ bool af=false;
    for(auto& L:lines){ if(L.length()){ if(!af){ af=true; continue; }
      if(outBlurb.length()) outBlurb+="\n"; outBlurb+=L; } } }
  outTitle.trim(); outBlurb.trim();
}

// ============================================================================
// GAME LIST BUILDER — groups multi-disk games, finds cover art
// ============================================================================
static void buildGameList(){
  g_games.clear();
  std::vector<bool> used(g_files.size(), false);

  for(int i=0;i<(int)g_files.size();i++){
    if(used[i]) continue;
    String baseName=getGameBaseName(g_files[i]);
    int diskNum=getDiskNumber(g_files[i]);
    String dir=parentDir(g_files[i]);

    GameEntry entry;
    entry.first_file_idx=i;
    entry.disk_count=1;
    entry.disk_indices.push_back(i);

    if(diskNum>0){
      // Multi-disk: find all related disks
      entry.name=baseName;
      for(int j=i+1;j<(int)g_files.size();j++){
        if(used[j]) continue;
        if(parentDir(g_files[j])==dir &&
           getGameBaseName(g_files[j])==baseName &&
           getDiskNumber(g_files[j])>0){
          used[j]=true;
          entry.disk_count++;
          entry.disk_indices.push_back(j);
          // Keep first_file_idx pointing to disk 1
          if(getDiskNumber(g_files[j])<getDiskNumber(g_files[entry.first_file_idx]))
            entry.first_file_idx=j;
        }
      }
      // Sort disk_indices by disk number
      for(int a=0;a<(int)entry.disk_indices.size();a++)
        for(int b=a+1;b<(int)entry.disk_indices.size();b++)
          if(getDiskNumber(g_files[entry.disk_indices[b]])<getDiskNumber(g_files[entry.disk_indices[a]]))
            std::swap(entry.disk_indices[a],entry.disk_indices[b]);
    } else {
      entry.name=basenameNoExt(filenameOnly(g_files[i]));
    }
    used[i]=true;

    // Try NFO for title
    String nfoPath,nfoTitle,nfoBlurb;
    if(findNFOFor(g_files[entry.first_file_idx],nfoPath)){
      String txt=readSmallTextFile(nfoPath,512);
      parseNFO(txt,nfoTitle,nfoBlurb);
      if(nfoTitle.length()) entry.name=nfoTitle;
    }

    // Cover art
    String jpg;
    if(findJPGFor(g_files[entry.first_file_idx],jpg)) entry.jpg_path=jpg;
    else {
      // Try base name in folder
      String tryBase=dir+"/"+baseName;
      for(const char* ext:{".jpg",".jpeg",".png"}){
        String tryPath=tryBase+ext;
        if(SD_MMC.exists(tryPath.c_str())){ entry.jpg_path=tryPath; break; }
      }
    }

    g_games.push_back(entry);
  }

  // Sort alphabetically
  for(int i=0;i<(int)g_games.size();i++)
    for(int j=i+1;j<(int)g_games.size();j++){
      String a=g_games[i].name; String b=g_games[j].name;
      a.toLowerCase(); b.toLowerCase();
      if(a.compareTo(b)>0) std::swap(g_games[i],g_games[j]);
    }
}

// ============================================================================
// A-Z BAR
// ============================================================================
static char active_letters[26];
static int  active_letter_count=0;

static void buildActiveLetters(){
  bool seen[26]={false};
  for(auto& g:g_games){
    char c=toupper(g.name.charAt(0));
    if(c>='A'&&c<='Z') seen[c-'A']=true;
  }
  active_letter_count=0;
  for(int i=0;i<26;i++) if(seen[i]) active_letters[active_letter_count++]='A'+i;
}
static String _gameName(int i){ return g_games[i].name; }
static int findFirstWithLetter(char letter,int count,String(*fn)(int)){
  letter=toupper(letter);
  for(int i=0;i<count;i++) if(toupper(fn(i).charAt(0))>=letter) return i;
  return count-1;
}
static void drawAZBar(){
  if(active_letter_count==0) return;
  int barH=LIST_BOTTOM-LIST_TOP;
  tft.fillRect(AZ_X,LIST_TOP,AZ_W,barH+NOW_PLAY_H,COL_PANEL);
  char curLetter='A';
  if(g_scroll>=0&&g_scroll<(int)g_games.size())
    curLetter=toupper(g_games[g_scroll].name.charAt(0));
  int letterH=barH/active_letter_count; if(letterH<8) letterH=8;
  tft.setFont(&lgfx::fonts::Font0);
  tft.setTextSize(1);
  for(int i=0;i<active_letter_count;i++){
    char letter=active_letters[i];
    int ly=LIST_TOP+i*letterH;
    if(ly+letterH>LIST_BOTTOM) break;
    if(letter==curLetter){
      tft.fillRect(AZ_X,ly,AZ_W,letterH,COL_AMBER);
      tft.setTextColor(TFT_BLACK,COL_AMBER);
    } else {
      tft.setTextColor(COL_DIM,COL_PANEL);
    }
    tft.setCursor(AZ_X+(AZ_W-6)/2,ly+(letterH-8)/2);
    tft.print(String(letter));
  }
  int maxOff=(int)g_games.size()-ITEMS_VIS;
  if(maxOff>0){
    int thumbH=max(6,barH*ITEMS_VIS/(int)g_games.size());
    int thumbY=LIST_TOP+(barH-thumbH)*g_scroll/maxOff;
    tft.fillRect(AZ_X-3,thumbY,2,thumbH,COL_BLUE);
  }
}
static bool handleAlphabetTouch(uint16_t px,uint16_t py){
  if(px<AZ_X||py<LIST_TOP||py>=LIST_BOTTOM||active_letter_count==0) return false;
  int barH=LIST_BOTTOM-LIST_TOP;
  int letterH=barH/active_letter_count; if(letterH<8) letterH=8;
  int idx=(py-LIST_TOP)/letterH;
  if(idx<0) idx=0; if(idx>=active_letter_count) idx=active_letter_count-1;
  int target=findFirstWithLetter(active_letters[idx],(int)g_games.size(),_gameName);
  g_sel=target; g_scroll=target;
  int maxOff=(int)g_games.size()-ITEMS_VIS; if(maxOff<0) maxOff=0;
  if(g_scroll>maxOff) g_scroll=maxOff;
  return true;
}

// ============================================================================
// STATUS BAR
// ============================================================================
// ============================================================================
// ESP-NOW manual pair — broadcasts hellos for 15s from main thread
// Must be defined after COVER_W, COL_* are available
// ============================================================================
static void doPairNow() {
  tft.fillRoundRect(8, g_info_pair_btn_y, COVER_W-16, 28, 6, COL_AMBER);
  tft.setFont(&lgfx::fonts::DejaVu12);
  tft.setTextColor(TFT_BLACK, COL_AMBER);
  tft.setCursor(18, g_info_pair_btn_y+8);
  tft.print("PAIRING...");

  uint32_t t0 = millis();
  int count = 0;
  while (!g_espnow_paired && millis() - t0 < 15000) {
    espnowBroadcastHello();
    delay(300);
    count++;
    tft.setFont(&lgfx::fonts::DejaVu9);
    tft.setTextColor(COL_LIT, COL_PANEL);
    tft.setCursor(8, g_info_pair_btn_y - 14);
    tft.print("tx: " + String(count) + "   ");
  }

  uint16_t btnCol = g_espnow_paired ? COL_GREEN : 0xE8C4;
  tft.fillRoundRect(8, g_info_pair_btn_y, COVER_W-16, 28, 6, btnCol);
  tft.setFont(&lgfx::fonts::DejaVu12);
  tft.setTextColor(TFT_BLACK, btnCol);
  tft.setCursor(18, g_info_pair_btn_y+8);
  tft.print(g_espnow_paired ? "PAIRED OK!" : "PAIR FAILED");
  delay(1500);
  drawFullUI();
  g_info_showing = false;
}

static void drawStatusBar(){
  tft.fillRect(0,0,LCD_WIDTH,STATUS_H,COL_BAR);
  tft.setFont(&lgfx::fonts::DejaVu9);
  tft.setTextColor(COL_ORANGE,COL_BAR);
  tft.setCursor(10,8); tft.print("OMEGAWARE");
  tft.setTextColor(COL_MID,COL_BAR);
  tft.print("  " FW_VERSION);

  // Mode indicator (centre)
  if (g_wireless_mode) {
    if(espnowIsPaired()){
      tft.setTextColor(0x07E0, COL_BAR);
      String label = "WIRELESS:PAIRED";
      int tw = tft.textWidth(label);
      tft.setCursor((LCD_WIDTH/2)-(tw/2), 8);
      tft.print(label);
    } else {
      tft.setTextColor(0xFD20, COL_BAR);
      String label = "WIRELESS:PAIRING";
      int tw = tft.textWidth(label);
      tft.setCursor((LCD_WIDTH/2)-(tw/2), 8);
      tft.print(label);
    }
  } else {
    tft.setTextColor(0x07FF, COL_BAR);
    String label = "STANDALONE";
    int tw = tft.textWidth(label);
    tft.setCursor((LCD_WIDTH/2)-(tw/2), 8);
    tft.print(label);
  }

  // Right side: battery widget + LOADED/READY text
  int batX = LCD_WIDTH - 80, batY = (STATUS_H - 12) / 2;
  drawBatteryWidget(batX, batY);

  // LOADED / READY indicator dot
  uint16_t indCol = g_loaded ? 0xE8C4 : COL_GREEN;
  tft.fillCircle(LCD_WIDTH-8, STATUS_H/2, 4, indCol);
}

// ============================================================================
// DRAW HELPERS — floppy icon, gradient bg
// ============================================================================

// ── Floppy disk icon — clean 24x24 pixel bitmap ──────────────────────────
// Each row is 24 bits, stored as 3 bytes, MSB first.
// Designed on a grid: square body, top-right notch, label area with 3 lines,
// centred metal shutter with hub hole.
static const uint8_t FLOPPY_24[] PROGMEM = {
  // Row 0-1: top edge, notch cut-out top-right (cols 18-23 open)
  0xFF, 0xFF, 0xC0,  // 1111 1111 1111 1111 1100 0000  <- right edge is notch gap
  0xFF, 0xFF, 0xC0,
  // Row 2-7: upper body — label area (cols 2-15), notch right side
  0xFF, 0xFF, 0xFF,
  0xFD, 0x55, 0xFF,  // label: col 2-15 filled, col 1=body border, lines inside
  0xFD, 0x55, 0xFF,
  0xFD, 0x55, 0xFF,
  0xFD, 0x55, 0xFF,
  0xFF, 0xFF, 0xFF,
  // Row 8-9: divider between label and shutter zone
  0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF,
  // Row 10-19: lower body — metal shutter centred cols 7-16, hub hole col 11-12
  0xFF, 0xFF, 0xFF,
  0xFF, 0x01, 0xFF,  // shutter outer left/right walls
  0xFF, 0x01, 0xFF,
  0xFF, 0x01, 0xFF,
  0xFF, 0x19, 0xFF,  // hub hole: cols 11-12 open inside shutter
  0xFF, 0x19, 0xFF,
  0xFF, 0x01, 0xFF,
  0xFF, 0x01, 0xFF,
  0xFF, 0x01, 0xFF,
  0xFF, 0xFF, 0xFF,
  // Row 20-23: bottom edge
  0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF,
};

// Draw floppy at (x,y), 24x24, 2-colour: bodyCol for set bits, bgCol for clear
static void drawFloppyIcon(int x, int y, int s, uint16_t bodyCol, uint16_t labelCol, uint16_t shutterCol) {
  // s parameter sets size. Draw proportionally using direct primitives.
  int bx = x, by = y, bw = s, bh = s;
  // Body
  tft.fillRect(bx, by, bw, bh, bodyCol);
  // Top-right write-protect notch (corner cutout ~1/5 width, 1/6 height)
  int nw = bw*4/18, nh = bh*3/18;
  tft.fillRect(bx+bw-nw, by, nw, nh, shutterCol);
  // Label area: top 40% of body, inset 1px each side except right edge
  int lx = bx+2, ly = by+2, lw = bw-nw-3, lh = bh*9/22;
  tft.fillRect(lx, ly, lw, lh, labelCol);
  // Three clean label lines
  uint16_t llineCol = (labelCol > 0x4000)
    ? (uint16_t)((labelCol & 0xF7DE) >> 1)   // darken
    : (uint16_t)(labelCol | 0x2104);           // lighten
  int lineW = lw - 6;
  for (int i = 0; i < 3; i++) {
    int lineY = ly + 3 + i * (lh/4);
    tft.fillRect(lx+3, lineY, lineW, 2, llineCol);
  }
  // Metal shutter: centred, lower 50% of body
  int mx = bx + bw/4, my = by + bh*11/20;
  int mw = bw/2,      mh = bh*8/20;
  tft.fillRect(mx, my, mw, mh, shutterCol);
  // Shutter border
  tft.drawRect(mx, my, mw, mh, (uint16_t)(shutterCol >> 1 | 0x0821));
  // Hub hole: small centred rectangle inside shutter
  int hx = mx + mw/2 - mw/6, hy = my + mh/2 - mh/4;
  int hw = mw/3,              hh = mh/2;
  tft.fillRect(hx, hy, hw, hh, bodyCol);
}

// ── Battery — reserved for future I2C fuel gauge (e.g. MAX17048 @ 0x36) ──
// IO20 is ADC2 which is killed by WiFi/ESP-NOW. Ripped out until proper HW added.
static void batInit() {}
static void batUpdate() __attribute__((unused));
static void batUpdate() {}
static void drawBatteryWidget(int x, int y) {}

// Draw vertical gradient (dark top → slightly lighter bottom)
static void drawGradientBg(int x, int y, int w, int h, uint16_t colTop, uint16_t colBot) {
  // Decompose colours
  int r1=(colTop>>11)&0x1F, g1=(colTop>>5)&0x3F, b1=colTop&0x1F;
  int r2=(colBot>>11)&0x1F, g2=(colBot>>5)&0x3F, b2=colBot&0x1F;
  int steps = min(h, 32); // max 32 gradient bands for speed
  int bandH = max(1, h/steps);
  for (int i=0; i<steps; i++) {
    int r = r1 + (r2-r1)*i/steps;
    int g = g1 + (g2-g1)*i/steps;
    int b = b1 + (b2-b1)*i/steps;
    uint16_t col = ((r&0x1F)<<11)|((g&0x3F)<<5)|(b&0x1F);
    int iy = y + i*bandH;
    int ih = (i==steps-1) ? (y+h-iy) : bandH;
    tft.fillRect(x, iy, w, ih, col);
  }
}


// Read JPEG dimensions from file header (SOF0 marker FFC0)
static bool getJpegSize(const String& path, int& w, int& h){
  w=0; h=0;
  File f=SD_MMC.open(path.c_str(),FILE_READ);
  if(!f) return false;
  uint8_t buf[4];
  // Check SOI marker
  if(f.read(buf,2)!=2||buf[0]!=0xFF||buf[1]!=0xD8){ f.close(); return false; }
  while(f.available()>4){
    if(f.read(buf,1)!=1||buf[0]!=0xFF){ f.close(); return false; }
    while(buf[0]==0xFF&&f.read(buf,1)==1); // skip padding
    uint8_t marker=buf[0];
    if(f.read(buf,2)!=2){ f.close(); return false; }
    int segLen=(buf[0]<<8)|buf[1];
    // SOF markers: FFC0-FFCF except FFC4 FFC8 FFCC
    if(marker>=0xC0&&marker<=0xCF&&marker!=0xC4&&marker!=0xC8&&marker!=0xCC){
      if(f.read(buf,1)!=1){ f.close(); return false; } // precision
      uint8_t dim[4]; if(f.read(dim,4)!=4){ f.close(); return false; }
      h=(dim[0]<<8)|dim[1];
      w=(dim[2]<<8)|dim[3];
      f.close(); return true;
    }
    // Skip segment
    f.seek(f.position()+segLen-2);
  }
  f.close(); return false;
}

static void drawCoverPanel(){
  // Gradient background for cover panel
  uint16_t bgTop = (uint16_t)(COL_PANEL);
  uint16_t bgBot = (uint16_t)(COL_BG);
  drawGradientBg(0, STATUS_H, COVER_W, LCD_HEIGHT-STATUS_H-BOTTOM_H, bgTop, bgBot);

  if(g_games.empty()) return;
  const GameEntry& game=g_games[g_sel];
  const int maxW=COVER_W-12;

  // Cover art box — rounded with subtle glow border
  int artX=COVER_ART_X, artY=COVER_ART_Y, artW=COVER_ART_W, artH=COVER_ART_H;
  // Outer glow
  tft.drawRoundRect(artX-1, artY-1, artW+2, artH+2, 8, COL_ACCENT);
  tft.fillRoundRect(artX, artY, artW, artH, 7, COL_BAR);

  if(game.jpg_path.length()>0){
    int jw=0,jh=0;
    getJpegSize(game.jpg_path,jw,jh);
    int boxW=artW-4, boxH=artH-4;
    if(jw>0&&jh>0){
      float scaleW=(float)boxW/jw, scaleH=(float)boxH/jh;
      float scale=min(scaleW,scaleH);
      if(scale>1.0f) scale=1.0f;
      int dw=(int)(jw*scale), dh=(int)(jh*scale);
      int ox=artX+2+(boxW-dw)/2, oy=artY+2+(boxH-dh)/2;
      tft.drawJpgFile(SD_MMC,game.jpg_path.c_str(),ox,oy,0,0,0,0,scale,scale);
    } else {
      tft.drawJpgFile(SD_MMC,game.jpg_path.c_str(),artX+2,artY+2,boxW,boxH);
    }
  } else {
    // No art — draw floppy icon as placeholder
    int is = min(artW, artH) * 3 / 4;
    int ix = artX + (artW-is)/2, iy = artY + (artH-is)/2;
    drawFloppyIcon(ix, iy, is, COL_ACCENT, COL_MID, COL_BAR);
    tft.setFont(&lgfx::fonts::DejaVu9);
    tft.setTextColor(COL_DIM, COL_BAR);
    char initial=toupper(game.name.charAt(0));
    // Draw initial over label area of floppy
    tft.setFont(&lgfx::fonts::DejaVu18);
    tft.setTextColor(COL_LIT, COL_MID);
    char ibuf[2]={initial,0};
    int tw=tft.textWidth(ibuf);
    tft.setCursor(artX+(artW-tw)/2, artY+artH/4);
    tft.print(ibuf);
  }

  // Flow content downward
  int ty=artY+artH+6;

  // Title
  tft.setFont(&lgfx::fonts::DejaVu12);
  tft.setTextColor(COL_LIT, COL_PANEL);
  tft.setTextSize(1);
  String title=game.name;
  if(tft.textWidth(title)<=maxW){
    tft.setCursor(6,ty); tft.print(title); ty+=16;
  } else {
    int breakAt=0;
    for(int i=1;i<=(int)title.length();i++){
      if(i==(int)title.length()||title[i]==' '){
        if(tft.textWidth(title.substring(0,i))<=maxW) breakAt=i;
        else break;
      }
    }
    if(breakAt>0){
      tft.setCursor(6,ty); tft.print(title.substring(0,breakAt)); ty+=16;
      String rest=title.substring(breakAt+1);
      while(tft.textWidth(rest)>maxW&&rest.length()>3) rest=rest.substring(0,rest.length()-1);
      tft.setCursor(6,ty); tft.print(rest); ty+=16;
    } else {
      while(tft.textWidth(title)>maxW&&title.length()>3) title=title.substring(0,title.length()-1);
      tft.setCursor(6,ty); tft.print(title); ty+=16;
    }
  }
  ty+=2;

  // NFO blurb
  String nfoPath,nfoTitle,nfoBlurb;
  if(findNFOFor(g_files[game.first_file_idx],nfoPath)){
    String txt=readSmallTextFile(nfoPath,512);
    parseNFO(txt,nfoTitle,nfoBlurb);
  }
  if(nfoBlurb.length()>0){
    tft.setFont(&lgfx::fonts::DejaVu9);
    tft.setTextColor(COL_LIT, COL_PANEL);
    String line="",word=""; int lines=0;
    for(int i=0;i<=(int)nfoBlurb.length()&&lines<4;i++){
      char c=(i<(int)nfoBlurb.length())?nfoBlurb[i]:' ';
      if(c==' '||c=='\n'||i==(int)nfoBlurb.length()){
        String cand=line.length()?line+" "+word:word;
        if(tft.textWidth(cand)>maxW&&line.length()){
          tft.setCursor(6,ty); tft.print(line); ty+=11; lines++; line=word;
        } else line=cand;
        word="";
      } else word+=c;
    }
    if(line.length()&&lines<4){ tft.setCursor(6,ty); tft.print(line); ty+=11; }
    ty+=3;
  }

  // Disk selector
  int btnY=LCD_HEIGHT-BOTTOM_H-44;
  if(game.disk_count>1){
    int diskY=btnY-38;
    tft.setFont(&lgfx::fonts::DejaVu9);
    tft.setTextColor(COL_DIM, COL_PANEL);
    tft.setCursor(6,diskY); tft.print("DISK:"); diskY+=12;
    int btnW=26,btnH=20,gap=3;
    int totalW=game.disk_count*(btnW+gap)-gap;
    int startX=max(6,(COVER_W-totalW)/2);
    for(int d=0;d<game.disk_count&&d<8;d++){
      int bx=startX+d*(btnW+gap);
      bool isSel=(d==g_disk_sel);
      bool isLoaded=(g_loaded_game_idx==g_sel&&g_loaded_disk_idx==d);
      uint16_t bc=isLoaded?COL_GREEN:(isSel?COL_AMBER:COL_BAR);
      tft.fillRoundRect(bx,diskY,btnW,btnH,4,bc);
      tft.drawRoundRect(bx,diskY,btnW,btnH,4,isSel?COL_AMBER:COL_DIM);
      tft.setTextColor(isLoaded||isSel?TFT_BLACK:COL_LIT,bc);
      tft.setCursor(bx+(btnW-6)/2,diskY+(btnH-9)/2);
      tft.print(String(d+1));
    }
  }

  // INSERT / EJECT button with floppy icon
  bool isLoaded=(g_loaded&&g_loaded_game_idx==g_sel);
  uint16_t btnFill = isLoaded ? (uint16_t)0x4000 : (uint16_t)0x0340;
  uint16_t btnBord = isLoaded ? (uint16_t)0xE8C4 : COL_GREEN;
  int bh = 36;
  tft.fillRoundRect(6, btnY, COVER_W-12, bh, 10, btnFill);
  tft.drawRoundRect(6, btnY, COVER_W-12, bh, 10, btnBord);

  // Floppy icon — small, left side of button
  int iconSize = 22;
  int iconX = 12, iconY = btnY + (bh-iconSize)/2;
  drawFloppyIcon(iconX, iconY, iconSize,
    isLoaded ? (uint16_t)0x8000 : COL_GREEN,
    isLoaded ? (uint16_t)0x6000 : COL_ACCENT,
    isLoaded ? (uint16_t)0x4000 : COL_BAR);

  // Label
  tft.setFont(&lgfx::fonts::DejaVu12);
  tft.setTextColor(TFT_WHITE, btnFill);
  const char* btnLabel = isLoaded ? "EJECT" : "INSERT";
  int tw2 = tft.textWidth(btnLabel);
  // Centre label in remaining space after icon
  int labelX = iconX + iconSize + 4 + ((COVER_W-12 - (iconX-6+iconSize+4) - tw2)/2);
  tft.setCursor(labelX, btnY + (bh-13)/2);
  tft.print(btnLabel);
}

// ============================================================================
static void drawModeBar(){
  tft.fillRect(LIST_X,STATUS_H,LIST_W+AZ_W,MODE_BAR_H,COL_BAR);
  tft.setFont(&lgfx::fonts::DejaVu9);
  bool isADF=(g_mode==MODE_ADF);
  int by=STATUS_H+3, bh=18;
  // ADF pill
  if(isADF){
    tft.fillRoundRect(LIST_X+4,by,44,bh,9,COL_ACCENT);
    tft.drawRoundRect(LIST_X+4,by,44,bh,9,COL_AMBER);
    tft.setTextColor(COL_AMBER,COL_ACCENT);
  } else {
    tft.fillRoundRect(LIST_X+4,by,44,bh,9,COL_BG);
    tft.setTextColor(COL_DIM,COL_BG);
  }
  tft.setCursor(LIST_X+12,by+5); tft.print("ADF");
  // DSK pill
  if(!isADF){
    tft.fillRoundRect(LIST_X+52,by,44,bh,9,COL_ACCENT);
    tft.drawRoundRect(LIST_X+52,by,44,bh,9,COL_AMBER);
    tft.setTextColor(COL_AMBER,COL_ACCENT);
  } else {
    tft.fillRoundRect(LIST_X+52,by,44,bh,9,COL_BG);
    tft.setTextColor(COL_DIM,COL_BG);
  }
  tft.setCursor(LIST_X+60,by+5); tft.print("DSK");
  // Count
  tft.setTextColor(COL_MID,COL_BAR);
  tft.setCursor(LIST_X+102,by+5);
  tft.print(String(g_games.size())+" games");
}

// ============================================================================
// FILE LIST
// ============================================================================
static void drawFileList(){
  // Gradient background for list area
  drawGradientBg(LIST_X, LIST_TOP, LIST_W, LIST_BOTTOM-LIST_TOP, COL_BG, (uint16_t)(COL_BG >> 1));

  if(g_games.empty()){
    tft.setFont(&lgfx::fonts::DejaVu12);
    tft.setTextColor(0xE8C4, COL_BG);
    tft.setCursor(LIST_X+10, LIST_TOP+20);
    tft.print(g_mode==MODE_ADF?"No .ADF files":"No .DSK files"); return;
  }
  int maxOff=(int)g_games.size()-ITEMS_VIS; if(maxOff<0) maxOff=0;
  if(g_scroll>maxOff) g_scroll=maxOff; if(g_scroll<0) g_scroll=0;

  for(int vi=0;vi<ITEMS_VIS;vi++){
    int gi=g_scroll+vi; if(gi>=(int)g_games.size()) break;
    const GameEntry& game=g_games[gi];
    bool isSel=(gi==g_sel);
    bool isLoaded=(g_loaded&&g_loaded_game_idx==gi);
    int y=LIST_TOP+vi*LIST_ITEM_H;
    int cardH = LIST_ITEM_H - 2;
    int cardY = y + 1;

    // Card background
    if(isSel){
      tft.fillRoundRect(LIST_X+2, cardY, LIST_W-4, cardH, 6, COL_SEL);
      tft.drawRoundRect(LIST_X+2, cardY, LIST_W-4, cardH, 6, COL_AMBER);
    } else {
      tft.fillRect(LIST_X, y, LIST_W, LIST_ITEM_H, COL_BG); // clear first
      tft.fillRoundRect(LIST_X+2, cardY, LIST_W-4, cardH, 4, (uint16_t)(COL_PANEL));
    }

    // Left accent bar
    uint16_t accentCol = isLoaded ? COL_GREEN : (isSel ? COL_AMBER : COL_ACCENT);
    tft.fillRoundRect(LIST_X+4, cardY+3, 4, cardH-6, 2, accentCol);

    // Initial circle
    int cx=LIST_X+24, cy=y+LIST_ITEM_H/2;
    uint16_t circCol=isSel?COL_AMBER:(isLoaded?COL_GREEN:COL_CIRC);
    tft.fillCircle(cx, cy, 13, circCol);
    tft.setFont(&lgfx::fonts::DejaVu12);
    tft.setTextColor(isSel||isLoaded?TFT_BLACK:COL_CIRC_TEXT, circCol);
    char initial=toupper(game.name.charAt(0));
    char ibuf[2]={initial,0};
    int iw=tft.textWidth(ibuf);
    tft.setCursor(cx-iw/2, cy-7); tft.print(ibuf);

    // Game name
    int textX=LIST_X+46;
    tft.setFont(&lgfx::fonts::DejaVu12);
    uint16_t textBg = isSel ? COL_SEL : (uint16_t)COL_PANEL;
    tft.setTextColor(isSel?TFT_WHITE:COL_LIT, textBg);
    String name=game.name;
    int maxNameW=LIST_W-60-(game.disk_count>1?44:0);
    while(tft.textWidth(name)>maxNameW&&name.length()>3)
      name=name.substring(0,name.length()-1);
    tft.setCursor(textX, cy-7); tft.print(name);

    // Disk count badge
    if(game.disk_count>1){
      tft.setFont(&lgfx::fonts::DejaVu9);
      uint16_t badgeCol=isLoaded?COL_GREEN:COL_ACCENT;
      int bx=LIST_X+LIST_W-46;
      int bh2=16, bby=cy-8;
      tft.fillRoundRect(bx,bby,42,bh2,5,badgeCol);
      tft.setTextColor(TFT_WHITE,badgeCol);
      String dc=String(game.disk_count)+"DSK";
      tft.setCursor(bx+(42-tft.textWidth(dc))/2,bby+4);
      tft.print(dc);
    }

    // Loaded indicator — small floppy icon
    if(isLoaded){
      drawFloppyIcon(LIST_X+LIST_W-20, cy-8, 16, COL_GREEN, COL_ACCENT, COL_BAR);
    }
  }
}

// ============================================================================
// NOW PLAYING BAR
// ============================================================================
static void drawNowPlayingBar(){
  int y=LIST_BOTTOM;
  if(g_loaded&&g_loaded_name.length()>0){
    tft.fillRect(LIST_X,y,LIST_W,NOW_PLAY_H,COL_NOW);
    tft.drawRect(LIST_X,y,LIST_W,NOW_PLAY_H,COL_GREEN);
    tft.fillCircle(LIST_X+10,y+NOW_PLAY_H/2,4,COL_GREEN);
    tft.setFont(&lgfx::fonts::DejaVu9);
    tft.setTextColor(COL_GREEN,COL_NOW);
    tft.setCursor(LIST_X+20,y+5); tft.print("NOW PLAYING");
    tft.setFont(&lgfx::fonts::DejaVu12);
    tft.setTextColor(TFT_WHITE,COL_NOW);
    tft.setCursor(LIST_X+20,y+17);
    String name=g_loaded_name;
    while(tft.textWidth(name)>LIST_W-30&&name.length()>3)
      name=name.substring(0,name.length()-1);
    tft.print(name);
  } else {
    tft.fillRect(LIST_X,y,LIST_W,NOW_PLAY_H,COL_BG);
    tft.setFont(&lgfx::fonts::DejaVu9);
    tft.setTextColor(COL_MID,COL_BG);
    tft.setCursor(LIST_X+10,y+NOW_PLAY_H/2-4);
    tft.print(String((int)g_games.size())+" games  —  tap INSERT to load");
  }
}

// ============================================================================
// BOTTOM BAR
// ============================================================================
static void drawBottomBar(){
  int y=LCD_HEIGHT-BOTTOM_H;
  drawGradientBg(0, y, LCD_WIDTH, BOTTOM_H, COL_BAR, COL_PANEL);
  tft.drawFastHLine(0, y, LCD_WIDTH, COL_SEP);
  int bw=LCD_WIDTH/4;

  struct BtnDef { const char* icon; const char* label; uint16_t col; };
  BtnDef btns[4] = {
    { "<", "PREV", COL_ORANGE },
    { ">", "NEXT", COL_BLUE },
    { "#", "THEME", COL_AMBER },
    { "i", "INFO", COL_MID },
  };

  for(int i=0;i<4;i++){
    int bx = i*bw;
    if(i>0) tft.drawFastVLine(bx, y+4, BOTTOM_H-8, COL_SEP);

    // Icon circle — positioned higher to leave room for labels
    int cx = bx + bw/2, cy = y + 13;
    tft.fillCircle(cx, cy, 10, (uint16_t)(btns[i].col >> 2));
    tft.drawCircle(cx, cy, 10, btns[i].col);
    tft.setFont(&lgfx::fonts::DejaVu12);
    tft.setTextColor(btns[i].col, (uint16_t)(btns[i].col >> 2));
    int tw = tft.textWidth(btns[i].icon);
    tft.setCursor(cx-tw/2, cy-7); tft.print(btns[i].icon);

    // Label
    tft.setFont(&lgfx::fonts::DejaVu9);
    tft.setTextColor(COL_DIM, COL_PANEL);
    tw = tft.textWidth(btns[i].label);
    tft.setCursor(bx+(bw-tw)/2, y+26); tft.print(btns[i].label);
  }

  // Theme name
  tft.setFont(&lgfx::fonts::DejaVu9);
  tft.setTextColor((uint16_t)(COL_AMBER>>1), COL_PANEL);
  String tn=THEMES[g_theme_idx].name;
  int tw=tft.textWidth(tn);
  tft.setCursor(2*bw+(bw-tw)/2, y+38); tft.print(tn);
}

// ============================================================================
// FULL REDRAW + PARTIAL REFRESH
// ============================================================================
static void drawFullUI(){
  tft.setTextSize(1);
  tft.setFont(&lgfx::fonts::DejaVu12);
  // Full screen gradient background
  drawGradientBg(0, 0, LCD_WIDTH, LCD_HEIGHT, (uint16_t)(COL_BAR), (uint16_t)(COL_BG>>1));
  drawStatusBar();
  drawCoverPanel();
  drawModeBar();
  drawFileList();
  drawNowPlayingBar();
  drawAZBar();
  drawBottomBar();
}
static void drawListAndCover(){
  tft.setTextSize(1);
  tft.setFont(&lgfx::fonts::DejaVu12);
  // Use flat fills here (not gradients) to avoid stutter on partial refresh
  tft.fillRect(0, STATUS_H, COVER_W, LCD_HEIGHT-STATUS_H-BOTTOM_H, COL_PANEL);
  tft.fillRect(LIST_X, LIST_TOP, LIST_W, LIST_BOTTOM-LIST_TOP, COL_BG);
  drawCoverPanel();
  drawFileList();
  drawNowPlayingBar();
  drawAZBar();
  tft.fillCircle(LCD_WIDTH-14,STATUS_H/2,5,g_loaded?0xE8C4:COL_GREEN);
  tft.setFont(&lgfx::fonts::DejaVu9);
  tft.setTextColor(g_loaded?0xE8C4:COL_GREEN,COL_BAR);
  tft.setCursor(LCD_WIDTH-78,8); tft.print(g_loaded?"LOADED ":"READY  ");
}

// ============================================================================
// LOAD / UNLOAD
// ============================================================================
static bool doLoadSelected(const String& adfPath){
  // Loading overlay in cover panel
  tft.fillRect(0,STATUS_H,COVER_W,LCD_HEIGHT-STATUS_H-BOTTOM_H,COL_PANEL);
  tft.setFont(&lgfx::fonts::DejaVu12);
  tft.setTextColor(TFT_CYAN,COL_PANEL);
  String title=basenameNoExt(filenameOnly(adfPath));
  if(title.length()>13) title=title.substring(0,13);
  tft.setCursor(8,STATUS_H+20); tft.print(title);
  tft.setFont(&lgfx::fonts::DejaVu9);
  tft.setTextColor(COL_LIT,COL_PANEL);
  tft.setCursor(8,STATUS_H+38); tft.print("Loading...");

  File f=openNamedImage(adfPath);
  if(!f){
    tft.setTextColor(0xE8C4,COL_PANEL); tft.setCursor(8,STATUS_H+56);
    tft.print("Open failed"); delay(1000); drawListAndCover(); return false;
  }
  uint32_t fsz=f.size();
  if(fsz==0){ f.close(); drawListAndCover(); return false; }
  if(fsz>MAX_FILE_BYTES) fsz=MAX_FILE_BYTES;
  build_volume_with_file(getOutputFilename(),fsz);

  int barX=8,barY=STATUS_H+58,barW=COVER_W-16,barH=14;
  tft.drawRoundRect(barX,barY,barW,barH,4,COL_DIM);

  uint32_t copied=0;
  uint8_t* dst=g_disk+DATA_LBA*SECTOR_SIZE;
  const size_t BUFSZ=4096;
  uint8_t* buf=(uint8_t*)malloc(BUFSZ);
  if(!buf){ f.close(); return false; }
  uint32_t remain=fsz;
  while(remain){
    size_t n=remain>BUFSZ?BUFSZ:remain;
    int rd=f.read(buf,n); if(rd<=0) break;
    memcpy(dst+copied,buf,rd); remain-=rd; copied+=rd;
    int fill=(int)((barW-4)*((float)copied/fsz));
    tft.fillRoundRect(barX+2,barY+2,fill,barH-4,3,COL_GREEN);
  }
  free(buf);
  if(fsz>copied) memset(dst+copied,0,fsz-copied);
  f.close();

  tft.setFont(&lgfx::fonts::DejaVu9);
  tft.setTextColor(COL_GREEN,COL_PANEL);
  tft.setCursor(8,barY+18); tft.print("OK  "+String(copied/1024)+"KB");
  delay(400);

  hardAttach();
  g_loaded=true;
  g_loaded_name=basenameNoExt(filenameOnly(adfPath));
  g_loaded_game_idx=g_sel;
  g_loaded_disk_idx=g_disk_sel;

  // Send to XIAO via WiFi TCP if in wireless mode
  if (g_wireless_mode && espnowIsPaired()) {
    String modeName = (g_mode==MODE_ADF) ? "ADF" : "DSK";
    espnowSendNotify(g_loaded_name, modeName, copied);
    espnowSendDisk(copied);
  }
  // In standalone mode, hardAttach() already connected USB directly to Gotek

  drawStatusBar();
  drawListAndCover();
  return true;
}

static void doUnload(){
  hardDetach();
  g_loaded=false; g_loaded_name="";
  g_loaded_game_idx=-1; g_loaded_disk_idx=-1;
  if (g_wireless_mode && espnowIsPaired()) espnowSendEject();
  drawStatusBar();
  drawListAndCover();
}

// ============================================================================
// DEBOUNCED TOUCH STATE
// ============================================================================
static const uint32_t BTN_COOLDOWN_MS=200;
static const int      BTN_MOVE_TOL_PX=20;
static bool     bbTouchDown=false;
static uint32_t bbLastActionMs=0;
static int      bbStartX=0,bbStartY=0;

void setup(){
  applyTheme(0);  // default colours before SD is available
  Serial.begin(115200); delay(200);

  initExpander();
  uiInit();

  // Silent boot — no on-screen log
  g_disk=(uint8_t*)ps_malloc((size_t)TOTAL_SECTORS*SECTOR_SIZE);
  if(!g_disk) g_disk=(uint8_t*)malloc((size_t)TOTAL_SECTORS*SECTOR_SIZE);
  if(!g_disk){ uiERR("RAM alloc failed"); while(true) delay(1000); }
  build_volume_with_file(getOutputFilename(),(g_mode==MODE_ADF)?ADF_DEFAULT_SIZE:64);

  // Mount SD first so config is available before WiFi starts
  SD_MMC.setPins(SD_CLK,SD_MOSI,SD_MISO,-1,-1,-1);
  delay(100);
  bool sdok=SD_MMC.begin("/sdcard",true);
  if(!sdok){ delay(200); sdok=SD_MMC.begin("/sdcard",true); }
  if(sdok){
    ensureConfig();      // create CONFIG.TXT with defaults if missing or empty
    listImages(SD_MMC,g_files);
    buildGameList();
    buildActiveLetters();
    g_sel=0; g_scroll=0; g_disk_sel=0;
  } else {
    uiERR("SD mount failed");
    delay(2000);
  }

  // ESP-NOW starts after SD so XIAO_MAC can be loaded from CONFIG.TXT
  espnowBegin();

  // Battery ADC init AFTER SD_MMC — SD_MMC may have touched IO20
  batInit();

  loadTheme();         // must be before cracktro so LOOP= and THEME= are loaded
  drawCracktroSplash();

  USB.onEvent(usbEventCallback);
  MSC.vendorID("ESP32"); MSC.productID("RAMDISK"); MSC.productRevision("1.0");
  MSC.onRead(onRead); MSC.onWrite(onWrite); MSC.mediaPresent(true);
  MSC.begin(TOTAL_SECTORS,SECTOR_SIZE); USB.begin();
  hardDetach();
  drawFullUI();
}

void loop(void){
  // Flash LINK ESTABLISHED message once when XIAO pairs
  if(g_espnow_link_just_established){
    g_espnow_link_just_established=false;
    // Brief green flash across status bar
    tft.fillRect(0,0,LCD_WIDTH,STATUS_H,0x07E0);
    tft.setFont(&lgfx::fonts::DejaVu9);
    tft.setTextColor(TFT_BLACK,0x07E0);
    tft.setCursor(LCD_WIDTH/2 - 72, 8);
    tft.print("** XIAO LINK ESTABLISHED **");
    delay(2000);
    drawStatusBar();
  }

  static uint32_t last=0;
  if(millis()-last<16){ delay(1); return; }
  last=millis();

  bool frame=Touch_ReadFrame();
  uint16_t px=0,py=0;
  bool haveTouch=frame&&getTouchXY(&px,&py);
  uint32_t nowMs=millis();

  if(!haveTouch){ bbTouchDown=false; delay(1); return; }
  if(nowMs-bbLastActionMs<BTN_COOLDOWN_MS){ delay(1); return; }
  if(bbTouchDown){
    int dx=abs((int)px-bbStartX),dy=abs((int)py-bbStartY);
    if(dx>BTN_MOVE_TOL_PX||dy>BTN_MOVE_TOL_PX) bbTouchDown=false;
    delay(1); return;
  }
  bbTouchDown=true; bbStartX=px; bbStartY=py; bbLastActionMs=nowMs;

  // ── A-Z bar ────────────────────────────────────────────────────────────────
  if(px>=AZ_X&&py>=LIST_TOP&&py<LIST_BOTTOM){
    if(handleAlphabetTouch(px,py)) drawListAndCover();
    return;
  }

  // ── Cover panel: INFO button touches ─────────────────────────────────────
  if(g_info_showing && px < COVER_W) {
    int pw = COVER_W - 12;
    int saY = g_info_pair_btn_y;          // STANDALONE button Y
    int wiY = saY + 52;                    // WIRELESS button Y (48px + 4px gap)
    int pairBtnY = LCD_HEIGHT-BOTTOM_H-116;

    // STANDALONE button (48px tall)
    if(py >= (uint16_t)saY && py < (uint16_t)(saY+48) && px >= 6 && px < COVER_W-6) {
      if(g_wireless_mode) setWirelessMode(false);
      return;
    }
    // WIRELESS button (48px tall)
    if(py >= (uint16_t)wiY && py < (uint16_t)(wiY+48) && px >= 6 && px < COVER_W-6) {
      if(!g_wireless_mode) setWirelessMode(true);
      return;
    }
    // SOFT RESET button
    int resetBtnY = LCD_HEIGHT - BOTTOM_H - 78;
    if(py >= (uint16_t)resetBtnY && py < (uint16_t)(resetBtnY+30) &&
       px >= 6 && px < COVER_W-6) {
      // Flash the button red briefly then restart
      tft.fillRoundRect(6, resetBtnY, pw, 30, 8, (uint16_t)0xE8C4);
      tft.setFont(&lgfx::fonts::DejaVu12);
      tft.setTextColor(TFT_BLACK, (uint16_t)0xE8C4);
      { int tw=tft.textWidth("RESTARTING...");
        tft.setCursor(6+(pw-tw)/2, resetBtnY+9); }
      tft.print("RESTARTING...");
      delay(800);
      ESP.restart();
      return;
    }

    // PAIR NOW button (only in wireless mode, 36px tall)
    if(g_wireless_mode && py >= (uint16_t)pairBtnY && py < (uint16_t)(pairBtnY+36) &&
       px >= 6 && px < COVER_W-6) {
      doPairNow();
      return;
    }
  }
  {
    int btnY=LCD_HEIGHT-BOTTOM_H-40;
    if(px<COVER_W&&py>=btnY&&py<LCD_HEIGHT-BOTTOM_H&&!g_games.empty()){
      bool isLoaded=(g_loaded&&g_loaded_game_idx==g_sel);
      if(isLoaded){
        doUnload();
      } else {
        // Load the selected disk of the selected game
        const GameEntry& game=g_games[g_sel];
        int diskFileIdx=game.disk_indices.empty()?
                        game.first_file_idx:
                        game.disk_indices[min(g_disk_sel,(int)game.disk_indices.size()-1)];
        doLoadSelected(g_files[diskFileIdx]);
      }
      return;
    }
  }

  // ── Cover panel: disk selector buttons ─────────────────────────────────────
  if(px<COVER_W&&!g_games.empty()){
    const GameEntry& game=g_games[g_sel];
    if(game.disk_count>1){
      int btnY=LCD_HEIGHT-BOTTOM_H-40;
      int diskH=34;
      int diskY=btnY-diskH-4+13;  // matches draw position
      int btnW=26,btnH=20,gap=3;
      int totalW=game.disk_count*(btnW+gap)-gap;
      int startX=max(6,(COVER_W-totalW)/2);
      if(py>=(uint16_t)diskY&&py<(uint16_t)(diskY+btnH)){
        int hitBtn=(px-startX)/(btnW+gap);
        if(hitBtn>=0&&hitBtn<game.disk_count){
          g_disk_sel=hitBtn;
          if(g_loaded&&g_loaded_game_idx==g_sel){
            int diskFileIdx=game.disk_indices[g_disk_sel];
            doLoadSelected(g_files[diskFileIdx]);
          } else {
            drawCoverPanel();
          }
        }
        return;
      }
    }
  }

  // ── Mode bar ADF/DSK ───────────────────────────────────────────────────────
  if(py>=STATUS_H&&py<STATUS_H+MODE_BAR_H&&px>=LIST_X){
    if(px>=LIST_X+6&&px<LIST_X+44&&g_mode!=MODE_ADF){
      g_mode=MODE_ADF;
      if(!listImages(SD_MMC,g_files)) g_files.clear();
      buildGameList(); buildActiveLetters(); g_sel=0; g_scroll=0; g_disk_sel=0;
      drawFullUI(); return;
    }
    if(px>=LIST_X+50&&px<LIST_X+88&&g_mode!=MODE_DSK){
      g_mode=MODE_DSK;
      if(!listImages(SD_MMC,g_files)) g_files.clear();
      buildGameList(); buildActiveLetters(); g_sel=0; g_scroll=0; g_disk_sel=0;
      drawFullUI(); return;
    }
  }

  // ── File list tap ──────────────────────────────────────────────────────────
  if(px>=LIST_X&&px<AZ_X&&py>=LIST_TOP&&py<LIST_BOTTOM){
    g_info_showing=false;
    int vi=(py-LIST_TOP)/LIST_ITEM_H;
    int gi=g_scroll+vi;
    if(gi>=0&&gi<(int)g_games.size()){
      if(gi==g_sel){
        // Second tap = load disk 1 (or eject if loaded)
        bool isLoaded=(g_loaded&&g_loaded_game_idx==g_sel);
        if(isLoaded){ doUnload(); }
        else {
          const GameEntry& game=g_games[g_sel];
          int diskFileIdx=game.disk_indices.empty()?
                          game.first_file_idx:
                          game.disk_indices[0];
          g_disk_sel=0;
          doLoadSelected(g_files[diskFileIdx]);
        }
      } else {
        g_sel=gi; g_disk_sel=0;
        drawListAndCover();
      }
    }
    return;
  }

  // ── Bottom bar ─────────────────────────────────────────────────────────────
  if(py>=LCD_HEIGHT-BOTTOM_H){
    int bw=LCD_WIDTH/4;
    int btn=px/bw;
    if(btn==0){
      if(g_sel>0){
        g_sel--; g_disk_sel=0;
        if(g_sel<g_scroll) g_scroll=g_sel;
        drawListAndCover();
      }
    } else if(btn==1){
      if(g_sel<(int)g_games.size()-1){
        g_sel++; g_disk_sel=0;
        if(g_sel>=g_scroll+ITEMS_VIS) g_scroll=g_sel-ITEMS_VIS+1;
        int maxOff=(int)g_games.size()-ITEMS_VIS; if(maxOff<0) maxOff=0;
        if(g_scroll>maxOff) g_scroll=maxOff;
        drawListAndCover();
      }
    } else if(btn==2){
      // THEME — cycle to next
      cycleTheme();
    } else {
      // INFO panel in cover area
      tft.fillRect(0,STATUS_H,COVER_W,LCD_HEIGHT-STATUS_H-BOTTOM_H,COL_PANEL);
      int y = STATUS_H + 8;
      int pw = COVER_W - 12;  // panel usable width

      // --- TRANSFER MODE label ---
      tft.setFont(&lgfx::fonts::DejaVu9);
      tft.setTextColor(COL_DIM, COL_PANEL);
      tft.setCursor(8, y); tft.print("TRANSFER MODE"); y += 13;

      // STANDALONE button — double height (48px)
      uint16_t saCol = !g_wireless_mode ? COL_GREEN : COL_BAR;
      uint16_t saBrd = !g_wireless_mode ? COL_GREEN : COL_DIM;
      tft.fillRoundRect(6, y, pw, 48, 8, saCol);
      tft.drawRoundRect(6, y, pw, 48, 8, saBrd);
      tft.setFont(&lgfx::fonts::DejaVu18);
      tft.setTextColor(!g_wireless_mode ? TFT_BLACK : COL_DIM, saCol);
      { int tw=tft.textWidth("STANDALONE");
        tft.setCursor(6+(pw-tw)/2, y+14); }
      tft.print("STANDALONE");
      tft.setFont(&lgfx::fonts::DejaVu9);
      tft.setTextColor(!g_wireless_mode ? TFT_BLACK : COL_DIM, saCol);
      { int tw=tft.textWidth("direct USB to Gotek");
        tft.setCursor(6+(pw-tw)/2, y+34); }
      tft.print("direct USB to Gotek");
      int saY = y;
      y += 52;

      // WIRELESS button — double height (48px)
      uint16_t wiCol = g_wireless_mode ? COL_BLUE : COL_BAR;
      uint16_t wiBrd = g_wireless_mode ? COL_BLUE : COL_DIM;
      tft.fillRoundRect(6, y, pw, 48, 8, wiCol);
      tft.drawRoundRect(6, y, pw, 48, 8, wiBrd);
      tft.setFont(&lgfx::fonts::DejaVu18);
      tft.setTextColor(g_wireless_mode ? TFT_WHITE : COL_DIM, wiCol);
      { int tw=tft.textWidth("WIRELESS");
        tft.setCursor(6+(pw-tw)/2, y+14); }
      tft.print("WIRELESS");
      tft.setFont(&lgfx::fonts::DejaVu9);
      tft.setTextColor(g_wireless_mode ? TFT_WHITE : COL_DIM, wiCol);
      { int tw=tft.textWidth("send via WiFi to XIAO");
        tft.setCursor(6+(pw-tw)/2, y+34); }
      tft.print("send via WiFi to XIAO");
      y += 56;

      // Store saY for touch handler
      g_info_pair_btn_y = saY;

      // Divider
      tft.drawFastHLine(8, y, pw, COL_SEP); y += 6;

      // --- SYSTEM INFO ---
      tft.setFont(&lgfx::fonts::DejaVu9);
      tft.setTextColor(COL_LIT, COL_PANEL);
      tft.setCursor(8,y); tft.print("Heap: "+String(ESP.getFreeHeap()/1024)+"KB  PSRAM: "+String(ESP.getFreePsram()/1024)+"KB"); y+=12;
      tft.setCursor(8,y); tft.print("Games: "+String(g_games.size())+"  FW: " FW_VERSION); y+=14;

      // --- WIRELESS STATUS (only in wireless mode) ---
      if (g_wireless_mode) {
        tft.drawFastHLine(8, y, pw, COL_SEP); y+=6;
        tft.setFont(&lgfx::fonts::DejaVu9);
        if(espnowIsPaired()){
          bool online = espnowXiaoOnline();
          tft.setTextColor(online ? COL_GREEN : COL_ORANGE, COL_PANEL);
          tft.setCursor(8,y); tft.print(online ? "XIAO: ONLINE" : "XIAO: OFFLINE"); y+=11;
          tft.setTextColor(COL_LIT, COL_PANEL);
          String mac = espnowGetXiaoMac();
          tft.setCursor(8,y); tft.print(mac); y+=11;
          tft.setTextColor(COL_DIM, COL_PANEL);
          tft.setCursor(8,y); tft.print("AP: GotekXIAO"); y+=11;
          if(g_espnow_xiao_done){
            tft.setTextColor(COL_GREEN,COL_PANEL);
            tft.setCursor(8,y); tft.print("Status: loaded OK"); y+=11;
          } else if(g_espnow_xiao_error){
            tft.setTextColor(0xE8C4,COL_PANEL);
            tft.setCursor(8,y); tft.print("Status: error"); y+=11;
          } else {
            tft.setTextColor(COL_LIT,COL_PANEL);
            tft.setCursor(8,y); tft.print("Status: idle"); y+=11;
          }
        } else {
          tft.setTextColor(COL_ORANGE, COL_PANEL);
          tft.setCursor(8,y); tft.print("Not paired — tap PAIR NOW"); y+=11;
        }
        y += 4;

        // PAIR NOW button — anchored at bottom of panel
        int pairBtnY = LCD_HEIGHT - BOTTOM_H - 116;
        uint16_t pairCol = espnowIsPaired() ? COL_GREEN : COL_AMBER;
        tft.fillRoundRect(6, pairBtnY, pw, 36, 8, pairCol);
        tft.drawRoundRect(6, pairBtnY, pw, 36, 8, pairCol);
        tft.setFont(&lgfx::fonts::DejaVu18);
        tft.setTextColor(TFT_BLACK, pairCol);
        const char* pairLabel = espnowIsPaired() ? "RE-PAIR" : "PAIR NOW";
        { int tw=tft.textWidth(pairLabel);
          tft.setCursor(6+(pw-tw)/2, pairBtnY+10); }
        tft.print(pairLabel);
      }

      // SOFT RESET button — positioned safely above INSERT/EJECT area
      int resetBtnY = LCD_HEIGHT - BOTTOM_H - 78;
      tft.fillRoundRect(6, resetBtnY, pw, 30, 8, (uint16_t)0x8000);
      tft.drawRoundRect(6, resetBtnY, pw, 30, 8, (uint16_t)0xE8C4);
      tft.setFont(&lgfx::fonts::DejaVu12);
      tft.setTextColor(TFT_WHITE, (uint16_t)0x8000);
      { int tw=tft.textWidth("SOFT RESET");
        tft.setCursor(6+(pw-tw)/2, resetBtnY+9); }
      tft.print("SOFT RESET");

      g_info_showing = true;
    }
    return;
  }
}
