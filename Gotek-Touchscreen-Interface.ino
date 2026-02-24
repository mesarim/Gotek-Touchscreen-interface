// ESP32-S3 (Waveshare 2.8") — USB MSC RAM Disk + ADF/DSK Browser + CST328 touch
// Board: Waveshare ESP32-S3-Touch-LCD-2.8
// Version: v0.5.2 (ADF/DSK toggle only on Selection screen; compile fixes)
// --------------------------------------------------------------------------
// UX: Selection → Details; LOAD/UNLOAD with detach-before-copy safety.
// ADF mode lists *.ADF and exports DISK.ADF; DSK mode lists *.DSK and exports DISK.DSK.

#include <Arduino.h>
#include "USB.h"
#include "USBMSC.h"
#include <FS.h>
#include <SD_MMC.h>

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <Wire.h>
#include <vector>
#include <algorithm>
#include <ctype.h>

// ---------- Version ----------
#define FW_VERSION  "v0.5.2"

// ---------- TinyUSB detach/attach ----------
extern "C" {
  bool tud_mounted(void);
  void tud_disconnect(void);
  void tud_connect(void);
}

// ---------- Display ----------
#define LCD_ROTATION  1          // Landscape
static int gW = 240, gH = 320;   // corrected after init/rotation (expect 320x240 at ROT=1)

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
#endif

class LGFX_Local : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel;
  lgfx::Bus_SPI      _bus;
public:
  LGFX_Local() {
    // Waveshare ESP32-S3-Touch-LCD-2.8 default wiring
    { auto cfg = _bus.config();
      cfg.spi_host=SPI3_HOST; cfg.spi_mode=0; cfg.freq_write=40000000; cfg.freq_read=16000000;
      cfg.spi_3wire=true; cfg.use_lock=true; cfg.dma_channel=SPI_DMA_CH_AUTO;
      cfg.pin_sclk=40; cfg.pin_mosi=45; cfg.pin_miso=-1; cfg.pin_dc=41;
      _bus.config(cfg); _panel.setBus(&_bus); }
    { auto cfg = _panel.config();
      cfg.pin_cs=42; cfg.pin_rst=39; cfg.pin_busy=-1;
      cfg.memory_width=240; cfg.memory_height=320; cfg.panel_width=240; cfg.panel_height=320;
      cfg.offset_x=0; cfg.offset_y=0; cfg.readable=false;
      cfg.invert     = true;       // ST7789 typically inverted
      cfg.rgb_order  = false;      // RGB (not BGR)
      cfg.dlen_16bit=false;
      cfg.bus_shared=true;
      _panel.config(cfg);
    }
    setPanel(&_panel);
  }
};
static LGFX_Local tft;

// ------------------------------------------------------------------
// Forward declarations
// ------------------------------------------------------------------
static void drawList();
static void drawDetailsFromNFO(const String& adfPath);
static bool  doLoadSelected(const String& adfPath);
static void  doUnloadInDetails(const String& adfPath);
static bool  listImages(fs::FS &fs, std::vector<String>& out);
static void  buildDisplayNames();
static void  sortByDisplay();
static void  drawModeSwitchButton();

// ---------- UI helpers ----------
static uint16_t uiY=0;
static void uiInit(){
  tft.init();
  tft.setRotation(LCD_ROTATION);
  gW = tft.width();
  gH = tft.height();
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  uiY=0;
  pinMode(5,OUTPUT); digitalWrite(5,HIGH);   // backlight (adjust to your board if needed)
}
static void uiClr(){ tft.fillScreen(TFT_BLACK); uiY=0; }
static void uiLine(uint16_t c,const String& s){ tft.setTextColor(c,TFT_BLACK); tft.setCursor(8,uiY); tft.print(s); uiY+=12; }
static void uiSection(const char* t){ uiLine(TFT_CYAN,"----------------------------------------"); uiLine(TFT_CYAN,t); uiLine(TFT_CYAN,"----------------------------------------"); }
static void uiOK (const String& s){ uiLine(TFT_GREEN, String("OK: ")+s); }
static void uiERR(const String& s){ uiLine(TFT_RED,   String("ERR: ")+s); }

// ---------- MSC RAM disk ----------
USBMSC MSC;

// Global state: "Loaded" means USB MSC is ATTACHED & visible to the host.
static bool g_ram_loaded = false;   // source of truth for UI/logic
static bool g_usb_online = false;   // kept in sync with g_ram_loaded

static const uint16_t SECTOR_SIZE   = 512;
static const uint32_t TOTAL_SECTORS = 2048;   // 1 MiB
static const uint16_t RESERVED_SECTORS = 1;
static const uint16_t SECTORS_PER_FAT  = 6;
static const uint16_t ROOT_DIR_SECTORS = 4;
static const uint8_t  SECTORS_PER_CLUSTER = 1;
static const uint8_t  NUM_FATS = 1;
static const uint16_t ROOT_ENTRIES = 64;

static const uint32_t FAT_LBA  = RESERVED_SECTORS;                       // 1
static const uint32_t ROOT_LBA = FAT_LBA + (NUM_FATS * SECTORS_PER_FAT); // 7
static const uint32_t DATA_LBA = ROOT_LBA + ROOT_DIR_SECTORS;            // 11

// ---- Dynamic image filename and size (per mode) ----
static const uint32_t DATA_SECTORS     = (TOTAL_SECTORS - DATA_LBA);     // 2037
static const uint32_t MAX_FILE_BYTES   = DATA_SECTORS * SECTOR_SIZE;     // 1,042,944
static const uint32_t ADF_DEFAULT_SIZE = 901120;                         // 1760*512

enum DiskMode { MODE_ADF=0, MODE_DSK=1 };
static DiskMode g_mode = MODE_ADF;

static inline const char* getOutputFilename() {
  return (g_mode == MODE_ADF) ? "DISK.ADF" : "DISK.DSK";
}

static uint8_t* g_disk=nullptr;
extern "C" void* ps_malloc(size_t size);

static inline void wr16(uint8_t*p,int o,uint16_t v){ p[o]=(uint8_t)v; p[o+1]=(uint8_t)(v>>8); }
static inline void wr32(uint8_t*p,int o,uint32_t v){ p[o]=(uint8_t)v; p[o+1]=(uint8_t)(v>>8); p[o+2]=(uint8_t)(v>>16); p[o+3]=(uint8_t)(v>>24); }

static void build_boot_sector(uint8_t* bs){
  memset(bs,0,SECTOR_SIZE);
  bs[0]=0xEB; bs[1]=0x3C; bs[2]=0x90; memcpy(&bs[3],"MSDOS5.0",8);
  wr16(bs,11,SECTOR_SIZE);
  bs[13]=SECTORS_PER_CLUSTER;
  wr16(bs,14,RESERVED_SECTORS);
  bs[16]=NUM_FATS;
  wr16(bs,17,ROOT_ENTRIES);
  wr16(bs,19,TOTAL_SECTORS);
  bs[21]=0xF8;
  wr16(bs,22,SECTORS_PER_FAT);
  wr16(bs,24,32);
  wr16(bs,26,64);
  wr32(bs,28,0);
  wr32(bs,32,0);
  bs[36]=0x80; bs[38]=0x29; wr32(bs,39,0x12345678);
  memcpy(&bs[43],"ESP32MSC   ",11);
  memcpy(&bs[54],"FAT12   ",8);
  bs[510]=0x55; bs[511]=0xAA;
}
static void fat12_set(uint8_t* fat, uint16_t cl, uint16_t v){
  uint32_t i=(cl*3)/2;
  if((cl&1)==0){ fat[i]=(uint8_t)(v&0xFF); fat[i+1]=(uint8_t)((fat[i+1]&0xF0)|((v>>8)&0x0F)); }
  else{ fat[i]=(uint8_t)((fat[i]&0x0F)|((v<<4)&0xF0)); fat[i+1]=(uint8_t)((v>>4)&0xFF); }
}
static void build_fat(uint8_t* fat, uint32_t fsz){
  memset(fat,0,SECTORS_PER_FAT*SECTOR_SIZE);
  fat[0]=0xF8; fat[1]=0xFF; fat[2]=0xFF;
  const uint32_t bpc = SECTOR_SIZE * SECTORS_PER_CLUSTER;
  uint32_t need=(fsz + bpc - 1)/bpc;
  for(uint32_t i=0;i<need;i++){
    uint16_t c=(uint16_t)(2+i);
    uint16_t v=(i==(need-1))?0x0FFF:(c+1);
    fat12_set(fat,c,v);
  }
}
static void make_83_name(const char* fn, char n[8], char e[3]){
  memset(n,' ',8); memset(e,' ',3);
  char tmp[32];
  size_t L = strlen(fn);
  if (L >= sizeof(tmp)) L = sizeof(tmp)-1;
  memcpy(tmp, fn, L);
  tmp[L] = '\0';
  for(size_t i=0;i<L;i++) tmp[i]=(char)toupper((unsigned char)tmp[i]);
  const char* dot=strrchr(tmp,'.');
  size_t nl= dot? (size_t)(dot-tmp): strlen(tmp);
  size_t el=(dot&&*(dot+1))? strlen(dot+1):0;
  for(size_t i=0;i<nl&&i<8;i++) n[i]=tmp[i];
  for(size_t i=0;i<el&&i<3;i++) e[i]=dot[1+i];
}
static void build_root(uint8_t* root, const char* name, uint32_t fsz){
  memset(root,0,ROOT_DIR_SECTORS*SECTOR_SIZE);
  uint8_t* de=root; char n[8],e[3]; make_83_name(name,n,e);
  memcpy(&de[0],n,8); memcpy(&de[8],e,3);
  de[11]=0x20;
  wr16(de,26,2);
  wr32(de,28,fsz);
}
// Build volume for the current mode and requested file size
static void build_volume_with_file(const char* outName, uint32_t fsz){
  if (fsz > MAX_FILE_BYTES) fsz = MAX_FILE_BYTES;
  memset(g_disk,0,TOTAL_SECTORS*SECTOR_SIZE);
  build_boot_sector(g_disk);
  uint8_t* fat  = g_disk + (RESERVED_SECTORS)*SECTOR_SIZE;
  uint8_t* root = g_disk + (RESERVED_SECTORS + SECTORS_PER_FAT)*SECTOR_SIZE;
  build_fat(fat, fsz);
  build_root(root, outName, fsz);
}
static int32_t onRead(uint32_t lba,uint32_t off,void*buf,uint32_t n){
  uint32_t s=lba*SECTOR_SIZE+off; if(s+n> TOTAL_SECTORS*SECTOR_SIZE) return 0;
  memcpy(buf,g_disk+s,n); return (int32_t)n;
}
static int32_t onWrite(uint32_t lba,uint32_t off,uint8_t*buf,uint32_t n){
  uint32_t s=lba*SECTOR_SIZE+off; if(s+n> TOTAL_SECTORS*SECTOR_SIZE) return 0;
  memcpy(g_disk+s,buf,n); return (int32_t)n;
}
static void usbEventCallback(void*, esp_event_base_t, int32_t, void*){}

// Hard replug helpers
static uint32_t g_rev_counter = 1;
static void bumpInquiryRevision(){ char rev[8]; snprintf(rev,sizeof(rev),"%lu", (unsigned long)g_rev_counter++); MSC.productRevision(rev); }
static void hardDetach(){ MSC.mediaPresent(false); delay(100); tud_disconnect(); delay(500); g_usb_online=false; g_ram_loaded=false; }
static void hardAttach(){ bumpInquiryRevision(); MSC.mediaPresent(true); delay(50); tud_connect(); delay(200); g_usb_online=true;  g_ram_loaded=true; }

// ---------- TF / SD_MMC ----------
static bool listImages(fs::FS &fs, std::vector<String>& out){
  out.clear(); File root=fs.open("/");
  if(!root||!root.isDirectory()) return false;
  File f=root.openNextFile();
  while(f){
    if(!f.isDirectory()){
      String nm=f.name(); String u=nm; u.toUpperCase();
      if((g_mode==MODE_ADF && u.endsWith(".ADF")) || (g_mode==MODE_DSK && u.endsWith(".DSK")))
        out.push_back(nm);
    }
    f.close();
    f=root.openNextFile();
  }
  return true;
}
static File openNamedImage(const String& path){
  String p=path; if(p.length()==0||p[0]!='/') p="/"+p;
  return SD_MMC.open(p, FILE_READ);
}

// Copy generic stream, padding to 'targetSize' (already used to build FAT/root)
static bool copyImageStream(File& f, uint32_t targetSize){
  uint32_t sz=f.size(); if(sz>targetSize) sz=targetSize;
  uint8_t* dst=g_disk + DATA_LBA*SECTOR_SIZE;

  const size_t BUFSZ=4096;
  uint8_t* buf=(uint8_t*)malloc(BUFSZ);
  if(!buf) return false;

  uiLine(TFT_WHITE,"Loading...");
  int baseY=uiY; int barX=8, barW=gW-16, barH=10, barY=baseY+4;
  tft.drawRect(barX,barY,barW,barH,TFT_DARKGREY);

  uint32_t remain=sz, copied=0; uint8_t* p=dst;
  while(remain){
    size_t n= remain>BUFSZ ? BUFSZ : remain;
    int rd=f.read(buf,n);
    if(rd<=0){ free(buf); return false;}
    memcpy(p,buf,rd); p+=rd; remain-=rd; copied+=rd;

    // Progress relative to requested targetSize (so smaller DSKs reach 100%)
    uint32_t pct=(uint32_t)((copied*100ULL)/targetSize); if(pct>100) pct=100;
    int filled=(int)((barW-2)*(double)pct/100.0);
    tft.fillRect(barX+1,barY+1,filled,barH-2,TFT_GREEN);
    delay(1);
  }
  free(buf);
  if(sz<targetSize) memset(dst+sz,0,targetSize - sz);
  uiOK("Image copied");
  return true;
}

// ---------- NFO/JPG helpers ----------
static String basenameNoExt(const String& p) {
  int s = p.lastIndexOf('/');
  int d = p.lastIndexOf('.');
  String b = (s >= 0) ? p.substring(s + 1) : p;
  if (d > s) b = b.substring(0, d - (s >= 0 ? s + 1 : 0));
  return b;
}

// Case-insensitive existence check strictly in root ("/")
static bool existsCaseInsensitiveInRoot(const String& targetName, String& outPath) {
  String tgt = targetName;
  if (tgt.length() && tgt[0] == '/') tgt.remove(0, 1);
  String tgtLower = tgt; tgtLower.toLowerCase();

  File root = SD_MMC.open("/");
  if (!root || !root.isDirectory()) return false;

  File f = root.openNextFile();
  while (f) {
    if (!f.isDirectory()) {
      String nm = f.name();
      int slash = nm.lastIndexOf('/');
      if (slash >= 0) nm = nm.substring(slash + 1);
      String nmLower = nm; nmLower.toLowerCase();
      if (nmLower == tgtLower) { outPath = "/" + nm; f.close(); root.close(); return true; }
    }
    f.close();
    f = root.openNextFile();
  }
  root.close();
  return false;
}
static bool findNFOFor(const String& adfPath, String& outNFO) {
  String base = basenameNoExt(adfPath);
  String cand = base + ".nfo";  // search in root, case-insensitive filename match
  return existsCaseInsensitiveInRoot(cand, outNFO);
}
static bool findJPGFor(const String& adfPath, String& outJPG) {
  String base = basenameNoExt(adfPath);
  String cand;
  const char* exts[] = {".jpg",".JPG",".Jpg",".jpeg",".JPEG",".Jpeg"};
  for (auto e : exts) {
    cand = "/" + base + e; if (SD_MMC.exists(cand)) { outJPG=cand; return true; }
  }
  return false;
}
static bool findMainMenuLogo(String& outJPG) {
  const char* cands[] = {
    "/amiga.jpg","/amiga.jpeg","/AMIGA.JPG","/AMIGA.JPEG",
    "/logo.jpg","/logo.jpeg","/LOGO.JPG","/LOGO.JPEG"
  };
  for (auto c : cands) { if (SD_MMC.exists(c)) { outJPG = c; return true; } }
  return false;
}

// Banners
static bool findBannerImage(String& outJPG) {
  if (existsCaseInsensitiveInRoot("banner.jpg", outJPG))  return true;
  if (existsCaseInsensitiveInRoot("banner.jpeg", outJPG)) return true;
  return false;
}
// DSK-mode banners: prefer ZX, then CPC, then generic DSK
static bool findDSKBanner(String& outJPG) {
  if (existsCaseInsensitiveInRoot("zx.jpg", outJPG))      return true;
  if (existsCaseInsensitiveInRoot("zx.jpeg", outJPG))     return true;
  if (existsCaseInsensitiveInRoot("cpc.jpg", outJPG))     return true;
  if (existsCaseInsensitiveInRoot("cpc.jpeg", outJPG))    return true;
  if (existsCaseInsensitiveInRoot("dsk.jpg", outJPG))     return true;
  if (existsCaseInsensitiveInRoot("dsk.jpeg", outJPG))    return true;
  return false;
}

// ---------- Word-wrap helpers ----------
static void flushLinePrint(int x, int &cy, int lineH, String &line) {
  tft.setCursor(x, cy);
  tft.print(line);
  cy += lineH;
  line = "";
}
static void drawWrappedText(int x, int y, int w, int h, const String& text, uint16_t color = TFT_WHITE)
{
  tft.setTextColor(color, TFT_BLACK);
  int lineH = (int)tft.fontHeight(); if (lineH <= 0) lineH = 12;
  int cy = y;
  String line="", word="";
  const int len = text.length();
  int i = 0;
  while (i <= len) {
    char c = (i < len) ? text[i] : ' ';  ++i;
    if (c == '\n') {
      if (word.length()) {
        String candidate = line.length() ? (line + " " + word) : word;
        if (tft.textWidth(candidate) > (uint16_t)w && line.length()) {
          flushLinePrint(x, cy, lineH, line);
          if (cy >= y + h) return;
          line = word;
        } else { line = candidate; }
        word = "";
      }
      flushLinePrint(x, cy, lineH, line);
      if (cy >= y + h) return;
      continue;
    }
    if (c == ' ' || c == '\t') {
      if (word.length()) {
        String candidate = line.length() ? (line + " " + word) : word;
        if (tft.textWidth(candidate) > (uint16_t)w && line.length()) {
          flushLinePrint(x, cy, lineH, line);
          if (cy >= y + h) return;
          line = word;
        } else { line = candidate; }
        word = "";
      }
    } else {
      word += c;
    }
  }
  if (line.length() && cy < y + h) {
    tft.setCursor(x, cy);
    tft.print(line);
  }
}
static int drawWrappedTextBG(int x, int y, int w, int h, const String& text, uint16_t fg, uint16_t bg)
{
  tft.setTextColor(fg, bg);
  int lineH = (int)tft.fontHeight(); if (lineH <= 0) lineH = 12;
  int cy = y;
  String line="", word="";
  const int len = text.length();
  int i = 0;
  while (i <= len) {
    char c = (i < len) ? text[i] : ' ';  ++i;
    if (c == '\n') {
      if (word.length()) {
        String candidate = line.length()? (line + " " + word) : word;
        if (tft.textWidth(candidate) > (uint16_t)w && line.length()){
          flushLinePrint(x, cy, lineH, line);
          if (cy >= y + h) return h;
          line = word;
        } else { line = candidate; }
        word = "";
      }
      flushLinePrint(x, cy, lineH, line);
      if (cy >= y + h) return h;
      continue;
    }
    if (c == ' ' || c == '\t'){
      if (word.length()){
        String candidate = line.length()? (line + " " + word) : word;
        if (tft.textWidth(candidate) > (uint16_t)w && line.length()){
          flushLinePrint(x, cy, lineH, line);
          if (cy >= y + h) return h;
          line = word;
        } else { line = candidate; }
        word = "";
      }
    } else {
      word += c;
    }
  }
  if (line.length() && cy < y + h){
    tft.setCursor(x, cy);
    tft.print(line);
    cy += lineH;
  }
  int used = cy - y;
  if (used > h) used = h;
  return used;
}

// ---------- Touch (CST328) ----------
#define CST328_SDA_PIN  1
#define CST328_SCL_PIN  3
#define CST328_INT_PIN  4
#define CST328_RST_PIN  2
#define CST328_ADDR     0x1A
#define I2C_TCH_FREQ_HZ 400000

#define CST328_REG_NUM      0xD005
#define CST328_REG_XY       0xD000
#define HYN_REG_DBG_MODE    0xD101
#define HYN_REG_NORM_MODE   0xD109
#define HYN_REG_BOOT_TIME   0xD1FC
#define CST328_MAX_POINTS   5

#define TOUCH_RAW_X_MIN     0
#define TOUCH_RAW_X_MAX     240
#define TOUCH_RAW_Y_MIN     0
#define TOUCH_RAW_Y_MAX     320
static bool TOUCH_SWAP_XY = true;
static bool TOUCH_INVERT_X = false;
static bool TOUCH_INVERT_Y = true;

TwoWire &WireT = Wire1;
static bool TCH_Write(uint16_t reg,const uint8_t* data,uint32_t len){
  WireT.beginTransmission(CST328_ADDR);
  WireT.write((uint8_t)(reg>>8)); WireT.write((uint8_t)reg);
  for(uint32_t i=0;i<len;i++) WireT.write(data[i]);
  return WireT.endTransmission(true)==0;
}
static bool TCH_Read(uint16_t reg,uint8_t* data,uint32_t len){
  WireT.beginTransmission(CST328_ADDR);
  WireT.write((uint8_t)(reg>>8)); WireT.write((uint8_t)reg);
  if(WireT.endTransmission(true)!=0) return false;
  uint32_t rcv=WireT.requestFrom((int)CST328_ADDR,(int)len);
  if(rcv!=len) return false;
  for(uint32_t i=0;i<len;i++) data[i]=WireT.read();
  return true;
}
static void CST328_Reset(){
  pinMode(CST328_RST_PIN,OUTPUT);
  digitalWrite(CST328_RST_PIN,HIGH); delay(50);
  digitalWrite(CST328_RST_PIN,LOW);  delay(5);
  digitalWrite(CST328_RST_PIN,HIGH); delay(50);
}
static bool CST328_Begin(){
  WireT.begin(CST328_SDA_PIN,CST328_SCL_PIN,I2C_TCH_FREQ_HZ);
  pinMode(CST328_INT_PIN,INPUT_PULLUP);
  CST328_Reset();
  uint8_t d=0;
  TCH_Write(HYN_REG_DBG_MODE,nullptr,0);
  TCH_Read (HYN_REG_BOOT_TIME,&d,1);
  TCH_Write(HYN_REG_NORM_MODE,nullptr,0);
  return true;
}

struct TouchData { uint8_t points; uint16_t rawX, rawY, strength; } gTouch={0,0,0,0};
static bool Touch_ReadFrame(){
  uint8_t cnt=0; if(!TCH_Read(CST328_REG_NUM,&cnt,1)) return false;
  uint8_t touches=cnt & 0x0F;
  if(touches==0){
    uint8_t clr=0; TCH_Write(CST328_REG_NUM,&clr,1);
    gTouch.points=0; return false;
  }
  if (touches > CST328_MAX_POINTS){
    uint8_t clr=0; TCH_Write(CST328_REG_NUM,&clr,1);
    return false;
  }
  uint8_t buf[32]={0};
  if(!TCH_Read(CST328_REG_XY,&buf[1],27)) return false;
  uint8_t clr=0; TCH_Write(CST328_REG_NUM,&clr,1);
  int num=0;
  uint16_t rx=((uint16_t)buf[2+num]<<4)|((buf[4+num]&0xF0)>>4);
  uint16_t ry=((uint16_t)buf[3+num]<<4)|( buf[4+num]&0x0F);
  uint16_t str=buf[5+num];
  gTouch.points=touches; gTouch.rawX=rx; gTouch.rawY=ry; gTouch.strength=str;
  return true;
}
static bool getTouchXY_Configurable(uint16_t* x,uint16_t* y){
  if(gTouch.points==0) return false;
  uint16_t rawX=gTouch.rawX, rawY=gTouch.rawY;
  uint16_t mx,my;
  if (TOUCH_SWAP_XY){
    mx=(uint16_t)map((long)rawY,TOUCH_RAW_Y_MIN,TOUCH_RAW_Y_MAX,0,gW-1);
    my=(uint16_t)map((long)rawX,TOUCH_RAW_X_MIN,TOUCH_RAW_X_MAX,0,gH-1);
  } else {
    mx=(uint16_t)map((long)rawX,TOUCH_RAW_X_MIN,TOUCH_RAW_X_MAX,0,gW-1);
    my=(uint16_t)map((long)rawY,TOUCH_RAW_Y_MIN,TOUCH_RAW_Y_MAX,0,gH-1);
  }
  if(TOUCH_INVERT_X) mx=(gW-1)-mx;
  if(TOUCH_INVERT_Y) my=(gH-1)-my;
  *x=constrain(mx,0,(uint16_t)(gW-1));
  *y=constrain(my,0,(uint16_t)(gH-1));
  return true;
}

// ---------- Screens / State ----------
enum ScreenMode { SCR_SELECTION=0, SCR_DETAILS=1 };
static ScreenMode g_screen = SCR_SELECTION;

// ---------- Buttons ----------
static const int BTN_H = 42;

// ---------- Status Indicator (small circle, top-right) ----------
static const int IND_RADIUS = 5;
static const int IND_MARGIN = 5;

// Draw small circle indicator: RED = LOADED, GREEN = UNLOADED
static void drawRamdiskIndicatorCircle() {
  int cx = gW - IND_MARGIN - IND_RADIUS;
  int cy = IND_MARGIN + IND_RADIUS;
  uint16_t color = g_ram_loaded ? TFT_RED : TFT_GREEN;
  tft.fillCircle(cx, cy, IND_RADIUS, color);
  tft.drawCircle(cx, cy, IND_RADIUS, TFT_BLACK);
}

// Mode switch button (middle-right) — SELECTION SCREEN ONLY
static void drawModeSwitchButton(){
  int w=40, h=26;
  int x = gW - w - 4;
  int y = (gH / 2) - (h / 2);
  uint16_t col = (g_mode == MODE_ADF) ? TFT_ORANGE : TFT_CYAN;
  tft.drawRect(x, y, w, h, TFT_DARKGREY);
  tft.fillRect(x+1, y+1, w-2, h-2, col);
  tft.setTextColor(TFT_BLACK, col);
  tft.setCursor(x + 6, y + 8);
  tft.print((g_mode == MODE_ADF) ? "ADF" : "DSK");
}

// ---------- Buttons drawing ----------
static void drawButtons_Select(){
  int x0=0, w0=gW/2-1, x1=gW/2, w1=gW/2;
  tft.drawRect(x0,gH-BTN_H,w0,BTN_H,TFT_DARKGREY);
  tft.drawRect(x1,gH-BTN_H,w1,BTN_H,TFT_DARKGREY);
  // Left: SELECT
  tft.fillRect(x0+1,gH-BTN_H+1,w0-2,BTN_H-2,TFT_ORANGE);
  tft.setTextColor(TFT_BLACK,TFT_ORANGE);
  tft.setCursor(x0+8,gH-BTN_H+13); tft.print("SELECT");
  // Right: OPEN
  tft.fillRect(x1+1,gH-BTN_H+1,w1-2,BTN_H-2,TFT_BLUE);
  tft.setTextColor(TFT_WHITE,TFT_BLUE);
  tft.setCursor(x1+8,gH-BTN_H+13); tft.print("OPEN");
}

static void drawButtons_Details(){
  int x0=0, w0=gW/2-1, x1=gW/2, w1=gW/2;
  tft.drawRect(x0,gH-BTN_H,w0,BTN_H,TFT_DARKGREY);
  tft.drawRect(x1,gH-BTN_H,w1,BTN_H,TFT_DARKGREY);
  // Left: BACK
  tft.fillRect(x0+1,gH-BTN_H+1,w0-2,BTN_H-2,TFT_ORANGE);
  tft.setTextColor(TFT_BLACK,TFT_ORANGE);
  tft.setCursor(x0+8,gH-BTN_H+13); tft.print("BACK");
  // Right: LOAD / UNLOAD (toggle)
  if (g_ram_loaded){
    tft.fillRect(x1+1,gH-BTN_H+1,w1-2,BTN_H-2,TFT_RED);
    tft.setTextColor(TFT_WHITE,TFT_RED);
    tft.setCursor(x1+8,gH-BTN_H+13); tft.print("UNLOAD");
  } else {
    tft.fillRect(x1+1,gH-BTN_H+1,w1-2,BTN_H-2,TFT_GREEN);
    tft.setTextColor(TFT_BLACK,TFT_GREEN);
    tft.setCursor(x1+8,gH-BTN_H+13); tft.print("LOAD");
  }
}

// ---------- File chooser list + display names ----------
static std::vector<String> g_adf;   // raw filenames (path)
static std::vector<String> g_disp;  // display names (Title or filename)
static int g_sel=0;

static String filenameOnly(const String& p){ int s=p.lastIndexOf('/'); return (s>=0)? p.substring(s+1): p; }

// Pixel-accurate truncation with "..."
static String truncateToWidth(const String& s, int maxW) {
  if (tft.textWidth(s) <= (uint16_t)maxW) return s;
  const String ell = "...";
  int ellW = tft.textWidth(ell);
  if (ellW >= maxW) return "";
  int lo = 0, hi = s.length(); String out;
  while (lo < hi) {
    int mid = (lo + hi) / 2;
    String cand = s.substring(0, mid) + ell;
    if (tft.textWidth(cand) <= (uint16_t)maxW) { out = cand; lo = mid + 1; }
    else hi = mid;
  }
  if (out.length() == 0) out = ell;
  return out;
}
static String readSmallTextFile(const String& path, size_t maxBytes = 4096) {
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) return "";
  String s; s.reserve(min((size_t)f.size(), maxBytes));
  while (f.available() && s.length() < (int)maxBytes) s += (char)f.read();
  f.close();
  s.replace("\r\n","\n"); s.replace("\r","\n");
  return s;
}
static void parseNFO(const String& txt, String& outTitle, String& outBlurb) {
  outTitle = ""; outBlurb = "";
  if (!txt.length()) return;
  std::vector<String> lines; int pos=0;
  while (pos < txt.length()) {
    int nl = txt.indexOf('\n', pos);
    if (nl < 0) nl = txt.length();
    String L = txt.substring(pos, nl);
    L.trim();
    lines.push_back(L);
    pos = nl + 1;
  }
  bool gotT=false, gotB=false;
  for (size_t i=0; i<lines.size(); ++i) {
    const String& L = lines[i];
    if (!gotT && L.startsWith("Title:")) { outTitle=L.substring(6); outTitle.trim(); gotT=true; continue; }
    if (!gotB && (L.startsWith("Blurb:") || L.startsWith("Description:"))) {
      outBlurb = L.substring(L.indexOf(':')+1); outBlurb.trim();
      for (size_t j=i+1; j<lines.size(); ++j) {
        const String& Lj = lines[j];
        if (Lj.indexOf(':') > 0) break;
        if (Lj.length()) { if (outBlurb.length()) outBlurb += "\n"; outBlurb += Lj; }
      }
      gotB=true;
    }
  }
  if (!gotT) { for (auto &L:lines){ if (L.length()){ outTitle=L; break; } } }
  if (!gotB) {
    bool afterFirst=false;
    for (auto &L:lines){
      if (L.length()){
        if (!afterFirst){ afterFirst=true; continue; }
        if (outBlurb.length()) outBlurb += "\n";
        outBlurb += L;
      }
    }
  }
  outTitle.trim(); outBlurb.trim();
}

// ---------- Main Menu (Selection) background logo ----------
static const int BTN_H_BAR = BTN_H; // alias
static void drawMainMenuLogoIfAny(){
  String logo;
  if (!findMainMenuLogo(logo)) return;
  int maxH = gH - BTN_H_BAR; if (maxH < 0) maxH = 0;
  tft.drawJpgFile(SD_MMC, logo.c_str(), 0, 0, gW, maxH);
}

// Draw top banner if present (returns banner height used)
static int drawTopBannerByMode(){
  String banner; bool have=false;
  if (g_mode == MODE_ADF) have = findBannerImage(banner);
  else                    have = findDSKBanner(banner);
  if (!have) return 0;
  const int BANNER_H = 40; // tuned for rotation=1 (landscape)
  tft.fillRect(0, 0, gW, BANNER_H, TFT_BLACK); // clear area
  tft.drawJpgFile(SD_MMC, banner.c_str(), 0, 0, gW, BANNER_H);
  return BANNER_H;
}

// ---------- List screen ----------
static void buildDisplayNames(){
  g_disp.clear(); g_disp.reserve(g_adf.size());
  for (const auto& p : g_adf){
    String nfo, title, blurb;
    if (findNFOFor(p, nfo)) {
      String txt = readSmallTextFile(nfo, 4096);
      parseNFO(txt, title, blurb);
    }
    g_disp.push_back(title.length()? title : filenameOnly(p));
  }
}
// Sort by display text (case-insensitive)
struct CiLessDisp {
  const std::vector<String>* disp;
  bool operator()(size_t a, size_t b) const {
    String A = (*disp)[a];
    String B = (*disp)[b];
    A.toUpperCase(); B.toUpperCase();
    return A.compareTo(B) < 0;
  }
};
static void sortByDisplay(){
  const size_t n = g_adf.size();
  std::vector<size_t> idx(n);
  for (size_t i=0;i<n;++i) idx[i]=i;
  std::sort(idx.begin(), idx.end(), CiLessDisp{ &g_disp });

  std::vector<String> adf2, disp2;
  adf2.reserve(n); disp2.reserve(n);
  for (size_t i=0;i<n;++i){ adf2.push_back(g_adf[idx[i]]); disp2.push_back(g_disp[idx[i]]); }
  g_adf.swap(adf2); g_disp.swap(disp2);
}

static void drawList(){
  uiClr();
  drawMainMenuLogoIfAny();           // background logo first

  // Top area: try to draw banner; if none, draw text header
  int bannerH = drawTopBannerByMode();
  if (bannerH > 0) {
    uiY = bannerH + 4;               // start list just below banner
  } else {
    uiSection((g_mode==MODE_ADF) ? "ADF Browser" : "DSK Browser");   // fallback title
  }

  // Status indicator circle (top-right), drawn after banner/header
  drawRamdiskIndicatorCircle();

  // Mode switch button ONLY on Selection
  drawModeSwitchButton();

  if (g_adf.empty()){ uiERR((g_mode==MODE_ADF) ? "No .ADF in /" : "No .DSK in /"); drawButtons_Select(); return; }

  const int rowW = gW - 16;  // margins
  int first=max(0,g_sel-6), last=min((int)g_adf.size()-1, first+11);
  for(int i=first;i<=last;i++){
    uint16_t col=(i==g_sel)?TFT_YELLOW:TFT_WHITE;
    int prefixW = (i==g_sel) ? tft.textWidth("> ") : tft.textWidth("  ");
    String fitted = truncateToWidth(g_disp[i], rowW - prefixW);
    uiLine(col, String(i==g_sel?"> ":"  ") + fitted);
  }
  drawButtons_Select();
}

// ---------- Details (NFO-backed; wrapped title + filename) + CENTERED JPG ----------
static void drawDetailsFromNFO(const String& adfPath) {
  tft.fillScreen(TFT_BLACK);

  String fn   = filenameOnly(adfPath);
  String base = basenameNoExt(adfPath);

  String title, blurb, nfoPath;
  if (findNFOFor(adfPath, nfoPath)) {
    String txt = readSmallTextFile(nfoPath, 4096);
    parseNFO(txt, title, blurb);
  }
  if (!title.length()) title = base;
  if (!blurb.length()) blurb = "No NFO found.";

  // Header band — reserve a small area at the right for the indicator circle
  int lineH = (int)tft.fontHeight(); if (lineH <= 0) lineH = 12;
  const int padTop = 4, padSide = 6, padMid = 2, padBottom = 4;
  const int maxTitleLines = 2;
  const int titleBoxH = maxTitleLines * lineH;
  const int fileLineH = lineH;
  const int headerH = padTop + titleBoxH + padMid + fileLineH + padBottom;

  const int indDiam = (IND_RADIUS*2);
  const int rightReserved = IND_MARGIN + indDiam + IND_MARGIN;

  tft.fillRect(0, 0, gW, headerH, TFT_DARKGREY);

  int usableW = max(0, gW - 2*padSide - rightReserved);
  drawWrappedTextBG(padSide, padTop, usableW, titleBoxH, title, TFT_YELLOW, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setCursor(padSide, padTop + titleBoxH + padMid);
  String fileLine = "(" + fn + ")";
  while (tft.textWidth(fileLine) > (uint16_t)usableW && fileLine.length() > 5) {
    fileLine.remove(fileLine.length()-2);
    fileLine += ")";
  }
  tft.print(fileLine);

  // Status circle only; no mode toggle here to avoid confusion
  drawRamdiskIndicatorCircle();

  // Body: centered image zone + guaranteed blurb space
  const int bodyY  = headerH;
  const int bodyH  = gH - headerH - BTN_H;
  const int gap    = 6;

  const int minBlurbH = 60;                  // always reserve space for blurb
  int imgZoneH = bodyH - minBlurbH; if (imgZoneH < 0) imgZoneH = 0;

  String jpg; bool hasJpg = findJPGFor(adfPath, jpg);
  const int recommendedImgH = 152;           // typical 320x152 art
  int targetImgH = hasJpg ? min(imgZoneH, recommendedImgH) : 0;

  int imgYCentered = bodyY + (imgZoneH > 0 ? (imgZoneH - targetImgH)/2 : 0);
  if (imgYCentered < bodyY) imgYCentered = bodyY;

  if (hasJpg && targetImgH >= 40) {
    tft.drawJpgFile(SD_MMC, jpg.c_str(), 0, imgYCentered, gW, targetImgH);
  }

  // Blurb below image zone
  int blurbTop = bodyY + imgZoneH + gap;
  int blurbH   = gH - blurbTop - BTN_H - gap;
  if (blurbH > 0) {
    drawWrappedText(6, blurbTop, gW - 12, blurbH, blurb, TFT_WHITE);
  }

  drawButtons_Details();
}

// ---------- LOAD/UNLOAD actions (Details screen) ----------
static bool doLoadSelected(const String& adfPath){
  uiClr();
  uiSection("Load Selected");
  uiLine(TFT_WHITE, adfPath);

  File f=openNamedImage(adfPath);
  if(!f){ uiERR("open failed"); delay(800); return false; }
  // Compute target size: actual file size clamped to MAX_FILE_BYTES
  uint32_t fsz = f.size();
  if (fsz == 0) { uiERR("empty file"); f.close(); delay(800); return false; }
  if (fsz > MAX_FILE_BYTES) fsz = MAX_FILE_BYTES;

  // Rebuild FAT12 volume with correct filename and size for this mode
  build_volume_with_file(getOutputFilename(), fsz);
  bool ok = copyImageStream(f, fsz);
  f.close();
  if(!ok){ uiERR("copy failed"); delay(800); return false; }

  hardAttach();                // attach after copying
  drawDetailsFromNFO(adfPath);
  return true;
}
static void doUnloadInDetails(const String& adfPath){
  hardDetach();
  drawDetailsFromNFO(adfPath);
}

// ---------- Debounced bottom bar ----------
static const uint32_t BTN_COOLDOWN_MS = 220;
static const int      BTN_MOVE_TOL_PX = 18;

static bool     bbTouchDown=false;
static uint32_t bbLastActionMs=0;
static int      bbStartX=0, bbStartY=0;
static bool     bbStartIsLeftHalf=false;

// ---------------- SETUP ----------------
void setup(){
  Serial.begin(115200); delay(200);
  uiInit();

  // Banner
  uiSection("ADF/DSK Browser");
  uiLine(TFT_WHITE, String("Firmware ")+FW_VERSION);

  uiSection("Build Volume");
  g_disk=(uint8_t*)ps_malloc((size_t)TOTAL_SECTORS*SECTOR_SIZE); if(!g_disk) g_disk=(uint8_t*)malloc((size_t)TOTAL_SECTORS*SECTOR_SIZE);
  if(!g_disk){ uiERR("RAM alloc failed"); while(true) delay(1000); }
  // Initialize volume with a small placeholder (so MSC has valid media immediately)
  build_volume_with_file(getOutputFilename(), (g_mode==MODE_ADF)? ADF_DEFAULT_SIZE : 64);
  uiOK("FAT12 ready");

  uiSection("TF Card");
  SD_MMC.setPins(14,17,16,-1,-1,21); delay(100);
  bool sdok=SD_MMC.begin("/sdcard", true); if(!sdok){ delay(200); sdok=SD_MMC.begin("/sdcard", true);} 
  if(!sdok) uiERR("SD mount");
  else {
    uiOK("SD OK");
    if(!listImages(SD_MMC, g_adf)) uiERR("list / failed");
    buildDisplayNames(); // titles for list
    sortByDisplay();     // sort alphabetically by display text
    g_sel = 0;
  }

  uiSection("Touch");
  if(CST328_Begin()) uiOK("CST328 init"); else uiERR("Touch init");

  uiSection("USB MSC");
  USB.onEvent(usbEventCallback);
  MSC.vendorID("ESP32"); MSC.productID("RAMDISK"); MSC.productRevision("1.0");
  MSC.onRead(onRead); MSC.onWrite(onWrite); MSC.mediaPresent(true);  // media prepared
  MSC.begin(TOTAL_SECTORS, SECTOR_SIZE); USB.begin();
  // Start in UNLOADED state (detached); user must LOAD explicitly in Details
  hardDetach();
  uiOK("MSC ready (UNLOADED)");

  g_screen = SCR_SELECTION;
  drawList();
}

// --------------- LOOP ---------------
void loop(void){
  static uint32_t last=0; 
  if(millis()-last<16){ delay(1); return; } 
  last=millis();

  bool frame = Touch_ReadFrame();
  uint16_t px=0, py=0; bool haveTouch = frame && getTouchXY_Configurable(&px,&py);

  // Mode switch hit-test (middle-right) — ENABLED ONLY ON SELECTION
  int ms_w=40, ms_h=26;
  int ms_x = gW - ms_w - 4;
  int ms_y = (gH / 2) - (ms_h / 2);
  bool inModeSwitch = haveTouch && (g_screen == SCR_SELECTION) &&
                      (px >= ms_x && px < ms_x+ms_w && py >= ms_y && py < ms_y+ms_h);

  // Bottom bar: fire once on press start, with cooldown & drift hysteresis
  const int barY = gH - BTN_H;
  bool inBottomBar = haveTouch && (py >= barY);
  uint32_t nowMs = millis();

  if (!bbTouchDown) {
    // Handle mode toggle first (Selection screen only)
    if (inModeSwitch && (nowMs - bbLastActionMs >= BTN_COOLDOWN_MS)) {
      bbTouchDown = true;
      // Toggle ADF <-> DSK
      g_mode = (g_mode == MODE_ADF) ? MODE_DSK : MODE_ADF;
      // Refresh list per new extension
      if(!listImages(SD_MMC, g_adf)) { g_adf.clear(); }
      buildDisplayNames();
      sortByDisplay();
      g_sel = 0;
      // Refresh current screen (always Selection here)
      drawList();
      bbLastActionMs = nowMs;
    }
    else if (inBottomBar && (nowMs - bbLastActionMs >= BTN_COOLDOWN_MS)) {
      bbTouchDown = true;
      bbStartX = (int)px; bbStartY = (int)py;
      bbStartIsLeftHalf = (bbStartX < (gW/2));

      if (g_screen == SCR_SELECTION) {
        if (bbStartIsLeftHalf) {
          // SELECT
          if (!g_adf.empty()) { g_sel=(g_sel+1)%(int)g_adf.size(); }
          drawList();
        } else {
          // OPEN
          if (!g_adf.empty()) { g_screen = SCR_DETAILS; drawDetailsFromNFO(g_adf[g_sel]); }
        }
      } else { // SCR_DETAILS
        const String currentADF = g_adf.empty()? String("") : g_adf[g_sel];
        if (bbStartIsLeftHalf) {
          // BACK
          g_screen = SCR_SELECTION; drawList();
        } else {
          // TOGGLE: LOAD / UNLOAD
          if (g_ram_loaded) {
            doUnloadInDetails(currentADF); // becomes UNLOADED
          } else {
            if (currentADF.length()) {
              doLoadSelected(currentADF);   // copy + attach (becomes LOADED)
            }
          }
        }
      }
      bbLastActionMs = nowMs;
    }
  } else {
    int dx = abs((int)px - bbStartX);
    int dy = abs((int)py - bbStartY);
    bool movedTooMuch = (dx > BTN_MOVE_TOL_PX) || (dy > BTN_MOVE_TOL_PX);
    if (!haveTouch || !inBottomBar || movedTooMuch) bbTouchDown = false;
    // If user released after hitting mode switch, reset touch state too
    if (!haveTouch && inModeSwitch) bbTouchDown = false;
  }

  delay(1);
}
