// Gotek Touchscreen Interface — Waveshare ESP32-S3-Touch-LCD-2.8
// Board: waveshare_esp32_s3_touch_lcd_28  (or ESP32S3 Dev Module)
// USB Mode     : USB-OTG (TinyUSB)
// USB CDC      : *** DISABLED *** (REQUIRED for FlashFloppy/Gotek compatibility)
// PSRAM        : OPI PSRAM
// Partition    : 16M Flash (3MB APP/9.9MB FATFS)
// Flash        : 16MB qio120
// CPU          : 240MHz
//
// Pin notes:
//   SPI display (ST7789): SCK=40 MOSI=45 DC=41 CS=42 RST=39 BL=5
//   SD_MMC 1-bit: CLK=14 CMD=17 D0=16
//   Touch I2C (CST328):  SDA=1 SCL=3 INT=4 RST=2
//   Cracktro: sprite-based double buffer (no native RGB panel flip)

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
#include <math.h>

// ---------- Version ----------
#define FW_VERSION  "v3.4.7-WS28"

// ---------- ESP-NOW server ----------
#include "espnow_server.h"

// ---------- TinyUSB detach/attach ----------
extern "C" {
  bool tud_mounted(void);
  void tud_disconnect(void);
  void tud_connect(void);
}

// ---------- Colours ----------
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

// ---------- LovyanGFX: ST7789 SPI ----------
class LGFX_Local : public lgfx::LGFX_Device {
public:
  lgfx::Panel_ST7789  _panel_instance;
  lgfx::Bus_SPI       _bus_instance;
  lgfx::Light_PWM     _light_instance;
  LGFX_Local() {
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host    = SPI3_HOST;
      cfg.spi_mode    = 0;
      cfg.freq_write  = 40000000;
      cfg.freq_read   = 16000000;
      cfg.spi_3wire   = true;
      cfg.use_lock    = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk    = 40;
      cfg.pin_mosi    = 45;
      cfg.pin_miso    = -1;
      cfg.pin_dc      = 41;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs        = 42;
      cfg.pin_rst       = 39;
      cfg.pin_busy      = -1;
      cfg.memory_width  = 240;
      cfg.memory_height = 320;
      cfg.panel_width   = 240;
      cfg.panel_height  = 320;
      cfg.offset_x = 0; cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.readable   = false;
      cfg.invert     = true;
      cfg.rgb_order  = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = true;
      _panel_instance.config(cfg);
    }
    {
      auto cfg = _light_instance.config();
      cfg.pin_bl      = 5;
      cfg.invert      = false;
      cfg.freq        = 44100;
      cfg.pwm_channel = 7;
      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }
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
static void uiInit() {
  tft.init();
  tft.setRotation(1);  // landscape: 320w x 240h
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.setBrightness(220);
}
static void uiERR(const String& s){
  tft.setTextSize(1);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.setCursor(8, 100);
  tft.print("ERR: " + s);
}

// ---------- Display size (after rotation) ----------
static const int gW = 320;
static const int gH = 240;

// ---------- MSC RAM disk ----------
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
static const uint32_t DATA_SECTORS   = (TOTAL_SECTORS - DATA_LBA);
static const uint32_t MAX_FILE_BYTES = DATA_SECTORS * SECTOR_SIZE;
static const uint32_t ADF_DEFAULT_SIZE = 901120;

enum DiskMode { MODE_ADF=0, MODE_DSK=1 };
static DiskMode g_mode = MODE_ADF;
static inline const char* getOutputFilename() {
  return (g_mode == MODE_ADF) ? "DISK.ADF" : "DISK.DSK";
}

uint8_t* g_disk = nullptr;
extern "C" void* ps_malloc(size_t size);

static inline void wr16(uint8_t* p,int o,uint16_t v){ p[o]=(uint8_t)v; p[o+1]=(uint8_t)(v>>8); }
static inline void wr32(uint8_t* p,int o,uint32_t v){ p[o]=(uint8_t)v; p[o+1]=(uint8_t)(v>>8); p[o+2]=(uint8_t)(v>>16); p[o+3]=(uint8_t)(v>>24); }

static void build_boot_sector(uint8_t* bs){
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
static void fat12_set(uint8_t* fat,uint16_t cl,uint16_t v){
  uint32_t i=(cl*3)/2;
  if((cl&1)==0){ fat[i]=(uint8_t)(v&0xFF); fat[i+1]=(uint8_t)((fat[i+1]&0xF0)|((v>>8)&0x0F)); }
  else{ fat[i]=(uint8_t)((fat[i]&0x0F)|((v<<4)&0xF0)); fat[i+1]=(uint8_t)((v>>4)&0xFF); }
}
static void build_fat(uint8_t* fat,uint32_t fsz){
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
static void make_83_name(const char* fn,char n[8],char e[3]){
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
static void build_root(uint8_t* root,const char* name,uint32_t fsz){
  memset(root,0,ROOT_DIR_SECTORS*SECTOR_SIZE);
  uint8_t* de=root; char n[8],e[3]; make_83_name(name,n,e);
  memcpy(&de[0],n,8); memcpy(&de[8],e,3);
  de[11]=0x20; wr16(de,26,2); wr32(de,28,fsz);
}
static void build_volume_with_file(const char* outName,uint32_t fsz){
  if(fsz>MAX_FILE_BYTES) fsz=MAX_FILE_BYTES;
  memset(g_disk,0,TOTAL_SECTORS*SECTOR_SIZE);
  build_boot_sector(g_disk);
  uint8_t* fat  = g_disk+(RESERVED_SECTORS)*SECTOR_SIZE;
  uint8_t* root = g_disk+(RESERVED_SECTORS+SECTORS_PER_FAT)*SECTOR_SIZE;
  build_fat(fat,fsz); build_root(root,outName,fsz);
}
static int32_t onRead(uint32_t lba,uint32_t off,void* buf,uint32_t n){
  uint32_t s=lba*SECTOR_SIZE+off; if(s+n>TOTAL_SECTORS*SECTOR_SIZE) return 0;
  memcpy(buf,g_disk+s,n); return (int32_t)n;
}
static int32_t onWrite(uint32_t lba,uint32_t off,uint8_t* buf,uint32_t n){
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
// SD_MMC pins — from working v0.5.2
#define SD_CLK   14
#define SD_CMD   17
#define SD_D0    16
#define SD_D1    (-1)
#define SD_D2    (-1)
#define SD_D3    21

// ---------- Touch (CST328) — raw I2C driver, exact copy from v0.5.2 ----------
#define CST328_SDA_PIN  1
#define CST328_SCL_PIN  3
#define CST328_INT_PIN  4
#define CST328_RST_PIN  2
#define CST328_ADDR     0x1A
#define I2C_TCH_FREQ_HZ 400000

#define CST328_REG_NUM   0xD005
#define CST328_REG_XY    0xD000
#define HYN_REG_DBG_MODE 0xD101
#define HYN_REG_NORM_MODE 0xD109
#define HYN_REG_BOOT_TIME 0xD1FC
#define CST328_MAX_POINTS 5

#define TOUCH_RAW_X_MIN  0
#define TOUCH_RAW_X_MAX  240
#define TOUCH_RAW_Y_MIN  0
#define TOUCH_RAW_Y_MAX  320
static bool TOUCH_SWAP_XY   = true;
static bool TOUCH_INVERT_X  = false;
static bool TOUCH_INVERT_Y  = true;

TwoWire &WireT = Wire1;

static bool TCH_Write(uint16_t reg, const uint8_t* data, uint32_t len){
  WireT.beginTransmission(CST328_ADDR);
  WireT.write((uint8_t)(reg>>8)); WireT.write((uint8_t)reg);
  for(uint32_t i=0;i<len;i++) WireT.write(data ? data[i] : 0);
  return WireT.endTransmission(true)==0;
}
static bool TCH_Read(uint16_t reg, uint8_t* data, uint32_t len){
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
  TCH_Read(HYN_REG_BOOT_TIME,&d,1);
  TCH_Write(HYN_REG_NORM_MODE,nullptr,0);
  return true;
}

struct TouchData { uint8_t points; uint16_t rawX,rawY,strength; } gTouch={0,0,0,0};

static bool Touch_ReadFrame(){
  uint8_t cnt=0; if(!TCH_Read(CST328_REG_NUM,&cnt,1)) return false;
  uint8_t touches=cnt&0x0F;
  if(touches==0){ uint8_t clr=0; TCH_Write(CST328_REG_NUM,&clr,1); gTouch.points=0; return false; }
  if(touches>CST328_MAX_POINTS){ uint8_t clr=0; TCH_Write(CST328_REG_NUM,&clr,1); return false; }
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

static bool getTouchXY(uint16_t* x, uint16_t* y){
  if(gTouch.points==0) return false;
  uint16_t rawX=gTouch.rawX, rawY=gTouch.rawY;
  uint16_t mx,my;
  if(TOUCH_SWAP_XY){
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

// ---------- NFO/JPG helpers ----------
static String basenameNoExt(const String& p){
  int s=p.lastIndexOf('/'); int d=p.lastIndexOf('.');
  String b=(s>=0)?p.substring(s+1):p;
  if(d>s) b=b.substring(0,d-(s>=0?s+1:0));
  return b;
}
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
static bool existsCaseInsensitiveInDir(const String& dir,const String& targetName,String& outPath){
  String tgtLower=targetName; tgtLower.toLowerCase();
  File d=SD_MMC.open(dir.c_str());
  if(!d||!d.isDirectory()) return false;
  File f=d.openNextFile();
  while(f){
    if(!f.isDirectory()){
      String nm=f.name(); int slash=nm.lastIndexOf('/');
      if(slash>=0) nm=nm.substring(slash+1);
      String nmLower=nm; nmLower.toLowerCase();
      if(nmLower==tgtLower){ outPath=dir+"/"+nm; f.close(); d.close(); return true; }
    }
    f.close(); f=d.openNextFile();
  }
  d.close(); return false;
}
static bool existsCaseInsensitiveInRoot(const String& targetName,String& outPath){
  return existsCaseInsensitiveInDir("/",targetName,outPath);
}
static bool findNFOFor(const String& adfPath,String& outNFO){
  String base=basenameNoExt(filenameOnly(adfPath));
  String dir=parentDir(adfPath);
  if(existsCaseInsensitiveInDir(dir,base+".nfo",outNFO)) return true;
  String gameBase=getGameBaseName(adfPath);
  if(gameBase!=base){ if(existsCaseInsensitiveInDir(dir,gameBase+".nfo",outNFO)) return true; }
  if(existsCaseInsensitiveInRoot(base+".nfo",outNFO)) return true;
  if(gameBase!=base) return existsCaseInsensitiveInRoot(gameBase+".nfo",outNFO);
  return false;
}
static bool findJPGFor(const String& adfPath,String& outJPG){
  String base=basenameNoExt(filenameOnly(adfPath));
  String dir=parentDir(adfPath);
  const char* exts[]=  {".jpg",".jpeg",".JPG",".JPEG",".Jpg",".Jpeg"};
  for(auto e:exts){ String c=dir+"/"+base+e; if(SD_MMC.exists(c.c_str())){ outJPG=c; return true; } }
  for(auto e:exts){ String c="/"+base+e;     if(SD_MMC.exists(c.c_str())){ outJPG=c; return true; } }
  return false;
}
static String readSmallTextFile(const String& path,size_t maxBytes=512){
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

// ---------- Image scanner ----------
static bool listImages(fs::FS& fs,std::vector<String>& out){
  out.clear();
  String modeDir=(g_mode==MODE_ADF)?"/ADF":"/DSK";
  String ext1=(g_mode==MODE_ADF)?".ADF":".DSK";
  File root=SD_MMC.open(modeDir.c_str());
  if(root&&root.isDirectory()){
    File gameDir;
    while((gameDir=root.openNextFile())){
      String entryName=gameDir.name();
      if(!entryName.startsWith("/")) entryName=modeDir+"/"+entryName;
      if(gameDir.isDirectory()){
        File entry;
        while((entry=gameDir.openNextFile())){
          String fname=entry.name(); int slash=fname.lastIndexOf('/');
          if(slash>=0) fname=fname.substring(slash+1);
          String upper=fname; upper.toUpperCase();
          if(upper.endsWith(ext1)||upper.endsWith(".IMG")||upper.endsWith(".ADZ")){
            String fullPath=entryName+"/"+fname;
            if(!fullPath.startsWith("/")) fullPath="/"+fullPath;
            out.push_back(fullPath);
          }
          entry.close();
        }
      } else {
        String fname=entryName; int slash=fname.lastIndexOf('/');
        if(slash>=0) fname=fname.substring(slash+1);
        String upper=fname; upper.toUpperCase();
        if(upper.endsWith(ext1)||upper.endsWith(".IMG")||upper.endsWith(".ADZ"))
          out.push_back(entryName);
      }
      gameDir.close();
    }
    root.close();
  }
  // Flat root fallback
  File rootDir=SD_MMC.open("/");
  if(rootDir){
    File entry;
    while((entry=rootDir.openNextFile())){
      if(!entry.isDirectory()){
        String fname=entry.name(); int slash=fname.lastIndexOf('/');
        if(slash>=0) fname=fname.substring(slash+1);
        String upper=fname; upper.toUpperCase();
        if(upper.endsWith(ext1)||upper.endsWith(".IMG")){
          String fullPath="/"+fname;
          bool dup=false;
          for(const auto& e:out){ String eb=e; eb.toLowerCase(); int s2=eb.lastIndexOf('/'); if(s2>=0) eb=eb.substring(s2+1); if(eb==fname.c_str()) { dup=true; break; } }
          if(!dup) out.push_back(fullPath);
        }
      }
      entry.close();
    }
    rootDir.close();
  }
  std::sort(out.begin(),out.end());
  return !out.empty();
}
static File openNamedImage(const String& path){
  String p=path; if(p.length()==0||p[0]!='/') p="/"+p;
  return SD_MMC.open(p,FILE_READ);
}

// ============================================================================
// GAME LIST
// ============================================================================
struct GameEntry {
  String name;
  int    first_file_idx;
  int    disk_count;
  String jpg_path;
  std::vector<int> disk_indices;
};

static std::vector<String>    g_files;
static std::vector<GameEntry> g_games;
static int    g_sel=0, g_scroll=0, g_disk_sel=0;
static String g_loaded_name="";
static bool   g_loaded=false;
static int    g_loaded_game_idx=-1, g_loaded_disk_idx=-1;

static void buildGameList(){
  g_games.clear();
  std::vector<bool> used(g_files.size(),false);
  for(int i=0;i<(int)g_files.size();i++){
    if(used[i]) continue;
    String baseName=getGameBaseName(g_files[i]);
    int diskNum=getDiskNumber(g_files[i]);
    String dir=parentDir(g_files[i]);
    GameEntry entry; entry.first_file_idx=i; entry.disk_count=1; entry.disk_indices.push_back(i);
    if(diskNum>0){
      entry.name=baseName;
      for(int j=i+1;j<(int)g_files.size();j++){
        if(used[j]) continue;
        if(parentDir(g_files[j])==dir&&getGameBaseName(g_files[j])==baseName&&getDiskNumber(g_files[j])>0){
          used[j]=true; entry.disk_count++; entry.disk_indices.push_back(j);
          if(getDiskNumber(g_files[j])<getDiskNumber(g_files[entry.first_file_idx])) entry.first_file_idx=j;
        }
      }
      for(int a=0;a<(int)entry.disk_indices.size();a++)
        for(int b=a+1;b<(int)entry.disk_indices.size();b++)
          if(getDiskNumber(g_files[entry.disk_indices[b]])<getDiskNumber(g_files[entry.disk_indices[a]]))
            std::swap(entry.disk_indices[a],entry.disk_indices[b]);
    } else {
      entry.name=basenameNoExt(filenameOnly(g_files[i]));
    }
    used[i]=true;
    String nfoPath,nfoTitle,nfoBlurb;
    if(findNFOFor(g_files[entry.first_file_idx],nfoPath)){
      String txt=readSmallTextFile(nfoPath,512);
      parseNFO(txt,nfoTitle,nfoBlurb);
      if(nfoTitle.length()) entry.name=nfoTitle;
    }
    String jpg;
    if(findJPGFor(g_files[entry.first_file_idx],jpg)) entry.jpg_path=jpg;
    else {
      String tryBase=dir+"/"+baseName;
      for(const char* ext:{".jpg",".jpeg",".png"}){
        String tryPath=tryBase+ext;
        if(SD_MMC.exists(tryPath.c_str())){ entry.jpg_path=tryPath; break; }
      }
    }
    g_games.push_back(entry);
  }
  for(int i=0;i<(int)g_games.size();i++)
    for(int j=i+1;j<(int)g_games.size();j++){
      String a=g_games[i].name; String b=g_games[j].name;
      a.toLowerCase(); b.toLowerCase();
      if(a.compareTo(b)>0) std::swap(g_games[i],g_games[j]);
    }
}

// ============================================================================
// THEME SYSTEM
// ============================================================================
struct Theme {
  const char* name;
  uint16_t bg,panel,bar,sel,sep,dim,mid,lit;
  uint16_t green,orange,amber,blue,now,accent;
  uint16_t circ,circ_text;
};
static const Theme THEMES[] = {
  {"NAVY",    0x1082,0x18C3,0x2104,0x2945,0x2103,0x4A8A,0x6B6D,0x9BD6, 0x2D6B,0xFC60,0xFD00,0x4C5F,0x0B26,0x5D1F, 0x3186,TFT_WHITE},
  {"EMBER",   0x0800,0x1000,0x1800,0x5820,0x1000,0x5820,0x8440,0xC8A0, 0x0560,0xFF40,0xFC00,0x4A9F,0x1000,0x7800, 0x3800,TFT_WHITE},
  {"MATRIX",  0x0020,0x0040,0x0060,0x0340,0x0040,0x0340,0x0580,0x07C0, 0x07E0,0x07E0,0x0FE0,0x07FF,0x0060,0x0380, 0x0300,0x07C0},
  {"PAPER",   0xEF5C,0xF7BE,0xFFFF,0xC618,0xCE59,0x8C51,0x6B4D,0x2124, 0x0680,0xE880,0xFD00,0x0C5F,0xCE59,0x4810, 0xC618,TFT_WHITE},
  {"SYNTH",   0x1001,0x2003,0x3005,0x5008,0x2003,0x600C,0x900F,0xC09F, 0x4BE0,0xFC1F,0xE81F,0xA01F,0x2003,0x8010, 0x5008,0xE81F},
  {"GOLD",    0x1000,0x1800,0x2000,0x4200,0x1800,0x5240,0x7440,0xC5A0, 0x0560,0xFCA0,0xFCC0,0xFCA0,0x1800,0x3200, 0x3200,0xFCC0},
};
static const int NUM_THEMES=6;
static int g_theme_idx=0;
static uint16_t COL_BG,COL_PANEL,COL_BAR,COL_SEL,COL_SEP;
static uint16_t COL_DIM,COL_MID,COL_LIT;
static uint16_t COL_GREEN,COL_ORANGE,COL_AMBER,COL_BLUE,COL_NOW,COL_ACCENT;
static uint16_t COL_CIRC,COL_CIRC_TEXT;

static void applyTheme(int idx){
  g_theme_idx=idx%NUM_THEMES;
  const Theme& t=THEMES[g_theme_idx];
  COL_BG=t.bg; COL_PANEL=t.panel; COL_BAR=t.bar; COL_SEL=t.sel;
  COL_SEP=t.sep; COL_DIM=t.dim; COL_MID=t.mid; COL_LIT=t.lit;
  COL_GREEN=t.green; COL_ORANGE=t.orange; COL_AMBER=t.amber;
  COL_BLUE=t.blue; COL_NOW=t.now; COL_ACCENT=t.accent;
  COL_CIRC=t.circ; COL_CIRC_TEXT=t.circ_text;
}
static void cycleTheme(){
  applyTheme((g_theme_idx+1)%NUM_THEMES);
  String lines=""; bool themeWritten=false;
  File fr=SD_MMC.open("/CONFIG.TXT",FILE_READ);
  if(fr){ while(fr.available()){ String line=fr.readStringUntil('\n'); line.trim();
    if(line.startsWith("THEME=")){ lines+="THEME="+String(g_theme_idx)+"\n"; themeWritten=true; }
    else lines+=line+"\n"; } fr.close(); }
  if(!themeWritten) lines+="THEME="+String(g_theme_idx)+"\n";
  File fw=SD_MMC.open("/CONFIG.TXT",FILE_WRITE);
  if(fw){ fw.print(lines); fw.close(); }
  drawFullUI();
}

// ============================================================================
// CONFIG
// ============================================================================
static bool   g_loop_cracktro = false;
static bool   g_wireless_mode = false;

static void saveMode(){
  String lines=""; bool written=false;
  File fr=SD_MMC.open("/CONFIG.TXT",FILE_READ);
  if(fr){ while(fr.available()){ String line=fr.readStringUntil('\n'); line.trim();
    if(line.startsWith("MODE=")){ lines+="MODE="+String(g_wireless_mode?"WIRELESS":"STANDALONE")+"\n"; written=true; }
    else lines+=line+"\n"; } fr.close(); }
  if(!written) lines+="MODE="+String(g_wireless_mode?"WIRELESS":"STANDALONE")+"\n";
  File fw=SD_MMC.open("/CONFIG.TXT",FILE_WRITE);
  if(fw){ fw.print(lines); fw.close(); }
}
static void setWirelessMode(bool wireless){
  g_wireless_mode=wireless; saveMode(); drawFullUI();
}

static void ensureConfig(){
  File f=SD_MMC.open("/CONFIG.TXT",FILE_READ);
  bool needsWrite=true;
  if(f){ if(f.size()>4) needsWrite=false; f.close(); }
  if(needsWrite){
    File fw=SD_MMC.open("/CONFIG.TXT",FILE_WRITE);
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
  if(!SD_MMC.exists("/ADF")){
    SD_MMC.mkdir("/ADF");
    File r=SD_MMC.open("/ADF/README.TXT",FILE_WRITE);
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
        "\r\n"
        "OPTIONAL EXTRAS (same folder as the .adf files):\r\n"
        "  GameName.jpg  - cover art (JPEG, any size)\r\n"
        "  GameName.nfo  - info/description (plain text)\r\n"
        "\r\n"
        "SUPPORTED FORMATS: .adf .img .adz\r\n"
        "\r\n"
        "See https://github.com/mesarim/Gotek-Touchscreen-interface\r\n"
      );
      r.close();
    }
  }
  if(!SD_MMC.exists("/DSK")){
    SD_MMC.mkdir("/DSK");
    File r=SD_MMC.open("/DSK/README.TXT",FILE_WRITE);
    if(r){
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
        "OPTIONAL EXTRAS (same folder as the .dsk files):\r\n"
        "  GameName.jpg  - cover art (JPEG, any size)\r\n"
        "  GameName.nfo  - info/description (plain text)\r\n"
        "\r\n"
        "SUPPORTED FORMATS: .dsk .img\r\n"
        "\r\n"
        "See https://github.com/mesarim/Gotek-Touchscreen-interface\r\n"
      );
      r.close();
    }
  }
}

static void loadTheme(){
  File f=SD_MMC.open("/CONFIG.TXT",FILE_READ);
  if(!f){ applyTheme(0); return; }
  while(f.available()){
    String line=f.readStringUntil('\n'); line.trim();
    if(line.startsWith("#")) continue;
    int eq=line.indexOf('='); if(eq<0) continue;
    String key=line.substring(0,eq); key.trim();
    String val=line.substring(eq+1); val.trim();
    if(key=="THEME")  applyTheme(val.toInt());
    else if(key=="LOOP")  g_loop_cracktro=(val=="1"||val=="true");
    else if(key=="MODE")  g_wireless_mode=(val=="WIRELESS");
  }
  f.close();
}

// ============================================================================
// LAYOUT — 320x240 compact (like 7" but scaled)
// Cover panel: left 110px, list: right 190px, A-Z bar: last 16px
// ============================================================================
#define LCD_WIDTH    320
#define LCD_HEIGHT   240
#define STATUS_H      20
#define COVER_W      110
#define COVER_ART_X    4
#define COVER_ART_Y   (STATUS_H+4)
#define COVER_ART_W  102
#define COVER_ART_H   90
#define AZ_W          16
#define AZ_X          (LCD_WIDTH-AZ_W)
#define LIST_X        COVER_W
#define LIST_W        (LCD_WIDTH-COVER_W-AZ_W)
#define MODE_BAR_H    18
#define LIST_TOP      (STATUS_H+MODE_BAR_H)
#define NOW_PLAY_H    22
#define BOTTOM_H      38
#define LIST_BOTTOM   (LCD_HEIGHT-BOTTOM_H-NOW_PLAY_H)
#define LIST_ITEM_H   ((LIST_BOTTOM-LIST_TOP)/6)
#define ITEMS_VIS     6

// ============================================================================
// DRAW HELPERS
// ============================================================================
static void drawGradientBg(int x,int y,int w,int h,uint16_t colTop,uint16_t colBot){
  int r1=(colTop>>11)&0x1F,g1=(colTop>>5)&0x3F,b1=colTop&0x1F;
  int r2=(colBot>>11)&0x1F,g2=(colBot>>5)&0x3F,b2=colBot&0x1F;
  int steps=min(h,16); int bandH=max(1,h/steps);
  for(int i=0;i<steps;i++){
    int r=r1+(r2-r1)*i/steps, g=g1+(g2-g1)*i/steps, b=b1+(b2-b1)*i/steps;
    uint16_t col=((r&0x1F)<<11)|((g&0x3F)<<5)|(b&0x1F);
    int iy=y+i*bandH, ih=(i==steps-1)?(y+h-iy):bandH;
    tft.fillRect(x,iy,w,ih,col);
  }
}

static void drawFloppyIcon(int x,int y,int s,uint16_t bodyCol,uint16_t labelCol,uint16_t shutterCol){
  tft.fillRect(x,y,s,s,bodyCol);
  int nw=s*4/18,nh=s*3/18; tft.fillRect(x+s-nw,y,nw,nh,shutterCol);
  int lx=x+2,ly=y+2,lw=s-nw-3,lh=s*9/22; tft.fillRect(lx,ly,lw,lh,labelCol);
  uint16_t llineCol=(labelCol>0x4000)?(uint16_t)((labelCol&0xF7DE)>>1):(uint16_t)(labelCol|0x2104);
  int lineW=lw-6;
  for(int i=0;i<3;i++) tft.fillRect(lx+3,ly+3+i*(lh/4),lineW,1,llineCol);
  int mx=x+s/4,my=y+s*11/20,mw=s/2,mh=s*8/20;
  tft.fillRect(mx,my,mw,mh,shutterCol);
  tft.drawRect(mx,my,mw,mh,(uint16_t)(shutterCol>>1|0x0821));
  tft.fillRect(mx+mw/2-mw/6,my+mh/2-mh/4,mw/3,mh/2,bodyCol);
}

// ============================================================================
// A-Z BAR
// ============================================================================
static char active_letters[26];
static int  active_letter_count=0;

static void buildActiveLetters(){
  bool seen[26]={false};
  for(auto& g:g_games){ char c=toupper(g.name.charAt(0)); if(c>='A'&&c<='Z') seen[c-'A']=true; }
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
  if(g_scroll>=0&&g_scroll<(int)g_games.size()) curLetter=toupper(g_games[g_scroll].name.charAt(0));
  int letterH=barH/active_letter_count; if(letterH<6) letterH=6;
  tft.setTextSize(1);
  for(int i=0;i<active_letter_count;i++){
    char letter=active_letters[i];
    int ly=LIST_TOP+i*letterH; if(ly+letterH>LIST_BOTTOM) break;
    if(letter==curLetter){
      tft.fillRect(AZ_X,ly,AZ_W,letterH,COL_AMBER);
      tft.setTextColor(TFT_BLACK,COL_AMBER);
    } else { tft.setTextColor(COL_DIM,COL_PANEL); }
    tft.setCursor(AZ_X+(AZ_W-6)/2,ly+(letterH-8)/2);
    tft.print(String(letter));
  }
  int maxOff=(int)g_games.size()-ITEMS_VIS; if(maxOff>0){
    int thumbH=max(4,barH*ITEMS_VIS/(int)g_games.size());
    int thumbY=LIST_TOP+(barH-thumbH)*g_scroll/maxOff;
    tft.fillRect(AZ_X-2,thumbY,2,thumbH,COL_BLUE);
  }
}
static bool handleAlphabetTouch(uint16_t px,uint16_t py){
  if(px<AZ_X||py<LIST_TOP||py>=LIST_BOTTOM||active_letter_count==0) return false;
  int barH=LIST_BOTTOM-LIST_TOP;
  int letterH=barH/active_letter_count; if(letterH<6) letterH=6;
  int idx=(py-LIST_TOP)/letterH;
  if(idx<0) idx=0; if(idx>=active_letter_count) idx=active_letter_count-1;
  int target=findFirstWithLetter(active_letters[idx],(int)g_games.size(),_gameName);
  g_sel=target; g_scroll=target;
  int maxOff=(int)g_games.size()-ITEMS_VIS; if(maxOff<0) maxOff=0;
  if(g_scroll>maxOff) g_scroll=maxOff;
  return true;
}

// ============================================================================
// ESP-NOW PAIR
// ============================================================================
static bool  g_info_showing=false;
static int   g_info_pair_btn_y=0;

static void doPairNow(){
  int pw=COVER_W-12;
  tft.fillRoundRect(6,g_info_pair_btn_y,pw,24,5,COL_AMBER);
  tft.setTextColor(TFT_BLACK,COL_AMBER);
  tft.setTextSize(1);
  tft.setCursor(14,g_info_pair_btn_y+8); tft.print("PAIRING...");
  uint32_t t0=millis(); int count=0;
  while(!g_espnow_paired&&millis()-t0<15000){
    espnowBroadcastHello(); delay(300); count++;
    tft.setTextColor(COL_LIT,COL_PANEL);
    tft.setCursor(6,g_info_pair_btn_y-12); tft.print("tx:"+String(count)+"  ");
  }
  uint16_t btnCol=g_espnow_paired?COL_GREEN:0xE8C4;
  tft.fillRoundRect(6,g_info_pair_btn_y,pw,24,5,btnCol);
  tft.setTextColor(TFT_BLACK,btnCol);
  tft.setCursor(14,g_info_pair_btn_y+8);
  tft.print(g_espnow_paired?"PAIRED OK!":"PAIR FAILED");
  delay(1500);
  drawFullUI();
  g_info_showing=false;
}

// ============================================================================
// STATUS BAR
// ============================================================================
static void drawStatusBar(){
  tft.fillRect(0,0,LCD_WIDTH,STATUS_H,COL_BAR);
  tft.setTextSize(1);
  tft.setTextColor(COL_ORANGE,COL_BAR);
  tft.setCursor(4,6); tft.print("OW");
  tft.setTextColor(COL_MID,COL_BAR);
  tft.print(" " FW_VERSION);
  // Mode indicator centre
  if(g_wireless_mode){
    // Show actual comms state, not just pairing state
    bool online=espnowXiaoOnline();
    if(online){ tft.setTextColor(0x07E0,COL_BAR); String l="WIFI: Comm"; int tw=tft.textWidth(l); tft.setCursor((LCD_WIDTH/2)-(tw/2),6); tft.print(l); }
    else       { tft.setTextColor(TFT_RED,COL_BAR); String l="WIFI: No Comm"; int tw=tft.textWidth(l); tft.setCursor((LCD_WIDTH/2)-(tw/2),6); tft.print(l); }
  } else {
    tft.setTextColor(0x07FF,COL_BAR); String l="STANDALONE"; int tw=tft.textWidth(l); tft.setCursor((LCD_WIDTH/2)-(tw/2),6); tft.print(l);
  }
  // Loaded indicator dot
  uint16_t indCol=g_loaded?0xE8C4:COL_GREEN;
  tft.fillCircle(LCD_WIDTH-6,STATUS_H/2,4,indCol);
}

// ============================================================================
// COVER PANEL
// ============================================================================
static void drawCoverPanel(){
  drawGradientBg(0,STATUS_H,COVER_W,LCD_HEIGHT-STATUS_H-BOTTOM_H,COL_PANEL,COL_BG);
  if(g_games.empty()) return;
  const GameEntry& game=g_games[g_sel];
  const int maxW=COVER_W-8;

  // Cover art box
  int artX=COVER_ART_X,artY=COVER_ART_Y,artW=COVER_ART_W,artH=COVER_ART_H;
  tft.drawRoundRect(artX-1,artY-1,artW+2,artH+2,5,COL_ACCENT);
  tft.fillRoundRect(artX,artY,artW,artH,4,COL_BAR);

  if(game.jpg_path.length()>0){
    // Scale-to-fit (contain): read JPEG dimensions, scale uniformly, centre in box
    int jw=0,jh=0;
    { // read JPEG SOF header to get dimensions
      File jf=SD_MMC.open(game.jpg_path.c_str(),FILE_READ);
      if(jf){
        uint8_t b2[2];
        if(jf.read(b2,2)==2&&b2[0]==0xFF&&b2[1]==0xD8){
          while(jf.available()>4){
            if(jf.read(b2,1)!=1||b2[0]!=0xFF) break;
            while(b2[0]==0xFF&&jf.read(b2,1)==1);
            uint8_t marker=b2[0];
            if(jf.read(b2,2)!=2) break;
            int segLen=(b2[0]<<8)|b2[1];
            if(marker>=0xC0&&marker<=0xCF&&marker!=0xC4&&marker!=0xC8&&marker!=0xCC){
              uint8_t prec; jf.read(&prec,1);
              uint8_t dim[4]; jf.read(dim,4);
              jh=(dim[0]<<8)|dim[1]; jw=(dim[2]<<8)|dim[3];
              break;
            }
            jf.seek(jf.position()+segLen-2);
          }
        }
        jf.close();
      }
    }
    int boxW=artW-4, boxH=artH-4;
    if(jw>0&&jh>0){
      float scaleW=(float)boxW/jw, scaleH=(float)boxH/jh;
      float scale=min(scaleW,scaleH);
      if(scale>1.0f) scale=1.0f;
      int dw=(int)(jw*scale), dh=(int)(jh*scale);
      int ox=artX+2+(boxW-dw)/2, oy=artY+2+(boxH-dh)/2;
      tft.drawJpgFile(SD_MMC,game.jpg_path.c_str(),ox,oy,dw,dh,0,0,scale,scale);
    } else {
      // Fallback: let decoder fill the box
      tft.drawJpgFile(SD_MMC,game.jpg_path.c_str(),artX+2,artY+2,boxW,boxH);
    }
  } else {
    int is=min(artW,artH)*3/4;
    int ix=artX+(artW-is)/2,iy=artY+(artH-is)/2;
    drawFloppyIcon(ix,iy,is,COL_ACCENT,COL_MID,COL_BAR);
    tft.setTextSize(2);
    tft.setTextColor(COL_LIT,COL_MID);
    char ibuf[2]={(char)toupper(game.name.charAt(0)),0};
    int tw=tft.textWidth(ibuf);
    tft.setCursor(artX+(artW-tw)/2,artY+artH/4); tft.print(ibuf);
  }

  int ty=artY+artH+4;
  // Title (clipped to panel width)
  tft.setTextSize(1);
  tft.setTextColor(COL_LIT,COL_PANEL);
  String title=game.name;
  while(tft.textWidth(title)>maxW&&title.length()>3) title=title.substring(0,title.length()-1);
  tft.setCursor(4,ty); tft.print(title); ty+=10;
  ty+=2;

  // NFO blurb (2 lines max)
  String nfoPath,nfoTitle,nfoBlurb;
  if(findNFOFor(g_files[game.first_file_idx],nfoPath)){
    String txt=readSmallTextFile(nfoPath,256); parseNFO(txt,nfoTitle,nfoBlurb);
  }
  if(nfoBlurb.length()>0){
    tft.setTextColor(COL_DIM,COL_PANEL);
    String line="",word=""; int lines=0;
    for(int i=0;i<=(int)nfoBlurb.length()&&lines<2;i++){
      char c=(i<(int)nfoBlurb.length())?nfoBlurb[i]:' ';
      if(c==' '||c=='\n'){
        String cand=line.length()?line+" "+word:word;
        if(tft.textWidth(cand)>maxW&&line.length()){ tft.setCursor(4,ty); tft.print(line); ty+=9; lines++; line=word; }
        else line=cand; word="";
      } else word+=c;
    }
    if(line.length()&&lines<2){ tft.setCursor(4,ty); tft.print(line); ty+=9; }
    ty+=2;
  }

  // Disk selector
  int btnY=LCD_HEIGHT-BOTTOM_H-32;
  if(game.disk_count>1){
    int diskY=btnY-28;
    tft.setTextColor(COL_DIM,COL_PANEL); tft.setTextSize(1);
    tft.setCursor(4,diskY); tft.print("DISK:"); diskY+=9;
    int btnW=18,btnH=14,gap=2;
    int totalW=game.disk_count*(btnW+gap)-gap;
    int startX=max(4,(COVER_W-totalW)/2);
    for(int d=0;d<game.disk_count&&d<6;d++){
      int bx=startX+d*(btnW+gap);
      bool isSel=(d==g_disk_sel);
      bool isLoaded=(g_loaded_game_idx==g_sel&&g_loaded_disk_idx==d);
      uint16_t bc=isLoaded?COL_GREEN:(isSel?COL_AMBER:COL_BAR);
      tft.fillRoundRect(bx,diskY,btnW,btnH,3,bc);
      tft.drawRoundRect(bx,diskY,btnW,btnH,3,isSel?COL_AMBER:COL_DIM);
      tft.setTextColor(isLoaded||isSel?TFT_BLACK:COL_LIT,bc);
      tft.setCursor(bx+(btnW-6)/2,diskY+(btnH-8)/2); tft.print(String(d+1));
    }
  }

  // INSERT/EJECT button
  bool isLoaded=(g_loaded&&g_loaded_game_idx==g_sel);
  uint16_t btnFill=isLoaded?(uint16_t)0x4000:(uint16_t)0x0340;
  uint16_t btnBord=isLoaded?(uint16_t)0xE8C4:COL_GREEN;
  int bh=28;
  tft.fillRoundRect(4,btnY,COVER_W-8,bh,7,btnFill);
  tft.drawRoundRect(4,btnY,COVER_W-8,bh,7,btnBord);
  int is2=16; int ix2=8,iy2=btnY+(bh-is2)/2;
  drawFloppyIcon(ix2,iy2,is2,isLoaded?(uint16_t)0x8000:COL_GREEN,isLoaded?(uint16_t)0x6000:COL_ACCENT,isLoaded?(uint16_t)0x4000:COL_BAR);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE,btnFill);
  const char* btnLabel=isLoaded?"EJECT":"INSERT";
  int tw2=tft.textWidth(btnLabel);
  int labelX=ix2+is2+2+((COVER_W-8-(ix2-4+is2+2)-tw2)/2);
  tft.setCursor(labelX,btnY+(bh-8)/2); tft.print(btnLabel);
}

// ============================================================================
// MODE BAR
// ============================================================================
static void drawModeBar(){
  tft.fillRect(LIST_X,STATUS_H,LIST_W+AZ_W,MODE_BAR_H,COL_BAR);
  tft.setTextSize(1);
  bool isADF=(g_mode==MODE_ADF);
  int by=STATUS_H+2,bh=14;
  if(isADF){ tft.fillRoundRect(LIST_X+3,by,30,bh,5,COL_ACCENT); tft.drawRoundRect(LIST_X+3,by,30,bh,5,COL_AMBER); tft.setTextColor(COL_AMBER,COL_ACCENT); }
  else { tft.fillRoundRect(LIST_X+3,by,30,bh,5,COL_BG); tft.setTextColor(COL_DIM,COL_BG); }
  tft.setCursor(LIST_X+8,by+3); tft.print("ADF");
  if(!isADF){ tft.fillRoundRect(LIST_X+36,by,30,bh,5,COL_ACCENT); tft.drawRoundRect(LIST_X+36,by,30,bh,5,COL_AMBER); tft.setTextColor(COL_AMBER,COL_ACCENT); }
  else { tft.fillRoundRect(LIST_X+36,by,30,bh,5,COL_BG); tft.setTextColor(COL_DIM,COL_BG); }
  tft.setCursor(LIST_X+41,by+3); tft.print("DSK");
  tft.setTextColor(COL_MID,COL_BAR);
  tft.setCursor(LIST_X+70,by+3); tft.print(String(g_games.size())+" games");
}

// ============================================================================
// FILE LIST
// ============================================================================
static void drawFileList(){
  drawGradientBg(LIST_X,LIST_TOP,LIST_W,LIST_BOTTOM-LIST_TOP,COL_BG,(uint16_t)(COL_BG>>1));
  if(g_games.empty()){
    tft.setTextSize(1); tft.setTextColor(0xE8C4,COL_BG);
    tft.setCursor(LIST_X+6,LIST_TOP+10);
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
    int cardH=LIST_ITEM_H-2;
    if(isSel){ tft.fillRoundRect(LIST_X+1,y+1,LIST_W-2,cardH,4,COL_SEL); tft.drawRoundRect(LIST_X+1,y+1,LIST_W-2,cardH,4,COL_AMBER); }
    else { tft.fillRect(LIST_X,y,LIST_W,LIST_ITEM_H,COL_BG); tft.fillRoundRect(LIST_X+1,y+1,LIST_W-2,cardH,3,COL_PANEL); }
    uint16_t accentCol=isLoaded?COL_GREEN:(isSel?COL_AMBER:COL_ACCENT);
    tft.fillRoundRect(LIST_X+3,y+3,3,cardH-6,1,accentCol);
    int cx=LIST_X+17,cy=y+LIST_ITEM_H/2;
    uint16_t circCol=isSel?COL_AMBER:(isLoaded?COL_GREEN:COL_CIRC);
    tft.fillCircle(cx,cy,9,circCol);
    tft.setTextSize(1);
    tft.setTextColor(isSel||isLoaded?TFT_BLACK:COL_CIRC_TEXT,circCol);
    char ibuf[2]={(char)toupper(game.name.charAt(0)),0};
    int iw=tft.textWidth(ibuf);
    tft.setCursor(cx-iw/2,cy-4); tft.print(ibuf);
    int textX=LIST_X+30;
    uint16_t textBg=isSel?COL_SEL:(uint16_t)COL_PANEL;
    tft.setTextColor(isSel?TFT_WHITE:COL_LIT,textBg);
    tft.setTextSize(1);
    String name=game.name;
    int maxNameW=LIST_W-38-(game.disk_count>1?28:0);
    while(tft.textWidth(name)>maxNameW&&name.length()>3) name=name.substring(0,name.length()-1);
    tft.setCursor(textX,cy-4); tft.print(name);
    if(game.disk_count>1){
      uint16_t badgeCol=isLoaded?COL_GREEN:COL_ACCENT;
      int bx=LIST_X+LIST_W-30;
      tft.fillRoundRect(bx,cy-6,26,12,3,badgeCol);
      tft.setTextColor(TFT_WHITE,badgeCol);
      String dc=String(game.disk_count)+"D";
      tft.setCursor(bx+(26-tft.textWidth(dc))/2,cy-2); tft.print(dc);
    }
    if(isLoaded){ drawFloppyIcon(LIST_X+LIST_W-14,cy-6,12,COL_GREEN,COL_ACCENT,COL_BAR); }
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
    tft.fillCircle(LIST_X+7,y+NOW_PLAY_H/2,3,COL_GREEN);
    tft.setTextSize(1);
    tft.setTextColor(COL_GREEN,COL_NOW);
    tft.setCursor(LIST_X+14,y+3); tft.print("NOW PLAYING");
    tft.setTextColor(TFT_WHITE,COL_NOW);
    tft.setCursor(LIST_X+14,y+13);
    String name=g_loaded_name;
    while(tft.textWidth(name)>LIST_W-20&&name.length()>3) name=name.substring(0,name.length()-1);
    tft.print(name);
  } else {
    tft.fillRect(LIST_X,y,LIST_W,NOW_PLAY_H,COL_BG);
    tft.setTextSize(1); tft.setTextColor(COL_MID,COL_BG);
    tft.setCursor(LIST_X+6,y+NOW_PLAY_H/2-4);
    tft.print(String((int)g_games.size())+" games — tap INSERT to load");
  }
}

// ============================================================================
// BOTTOM BAR
// ============================================================================
static void drawBottomBar(){
  int y=LCD_HEIGHT-BOTTOM_H;
  drawGradientBg(0,y,LCD_WIDTH,BOTTOM_H,COL_BAR,COL_PANEL);
  tft.drawFastHLine(0,y,LCD_WIDTH,COL_SEP);
  int bw=LCD_WIDTH/4;
  struct BtnDef { const char* icon; const char* label; uint16_t col; };
  BtnDef btns[4]={{"<","PREV",COL_ORANGE},{">","NEXT",COL_BLUE},{"#","THEME",COL_AMBER},{"i","INFO",COL_MID}};
  for(int i=0;i<4;i++){
    int bx=i*bw;
    if(i>0) tft.drawFastVLine(bx,y+3,BOTTOM_H-6,COL_SEP);
    int cx=bx+bw/2,cy=y+10;
    tft.fillCircle(cx,cy,8,(uint16_t)(btns[i].col>>2));
    tft.drawCircle(cx,cy,8,btns[i].col);
    tft.setTextSize(1); tft.setTextColor(btns[i].col,(uint16_t)(btns[i].col>>2));
    int tw=tft.textWidth(btns[i].icon);
    tft.setCursor(cx-tw/2,cy-4); tft.print(btns[i].icon);
    tft.setTextColor(COL_DIM,COL_PANEL);
    tw=tft.textWidth(btns[i].label);
    tft.setCursor(bx+(bw-tw)/2,y+22); tft.print(btns[i].label);
  }
  tft.setTextColor((uint16_t)(COL_AMBER>>1),COL_PANEL);
  String tn=THEMES[g_theme_idx].name;
  int tw=tft.textWidth(tn);
  tft.setCursor(2*bw+(bw-tw)/2,y+30); tft.print(tn);
}

// ============================================================================
// FULL REDRAW
// ============================================================================
static void drawFullUI(){
  tft.setTextSize(1);
  drawGradientBg(0,0,LCD_WIDTH,LCD_HEIGHT,(uint16_t)(COL_BAR),(uint16_t)(COL_BG>>1));
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
  tft.fillRect(0,STATUS_H,COVER_W,LCD_HEIGHT-STATUS_H-BOTTOM_H,COL_PANEL);
  tft.fillRect(LIST_X,LIST_TOP,LIST_W,LIST_BOTTOM-LIST_TOP,COL_BG);
  drawCoverPanel();
  drawFileList();
  drawNowPlayingBar();
  drawAZBar();
  tft.fillCircle(LCD_WIDTH-6,STATUS_H/2,4,g_loaded?0xE8C4:COL_GREEN);
  tft.setTextColor(g_loaded?0xE8C4:COL_GREEN,COL_BAR);
  tft.setTextSize(1);
  tft.setCursor(LCD_WIDTH-50,6); tft.print(g_loaded?"LOADED":"READY ");
}

// ============================================================================
// CRACKTRO — sprite double-buffer (SPI panel has no native flip)
// ============================================================================
#define NUM_STARS 60
static int16_t star_x[NUM_STARS],star_y[NUM_STARS],star_speed[NUM_STARS];
static void initStars(){ for(int i=0;i<NUM_STARS;i++){ star_x[i]=random(0,gW); star_y[i]=random(0,gH); star_speed[i]=random(1,4); } }

static void drawCracktroSplash(){
  initStars();

  // Allocate sprite as back buffer in PSRAM
  LGFX_Sprite spr(&tft);
  bool useSprite = spr.createSprite(gW, gH);

  const char* scrollText =
    "       GOTEK TOUCHSCREEN INTERFACE  *  CODED BY MEZ AND DIMMY OF OMEGAWARE  *  "
    "THE ULTIMATE RETRO DISK LOADER FOR AMIGA AND CPC  ...  "
    "WIFI WEB INTERFACE - THEME ENGINE - FAT12 RAM DISK  ...  "
    "GREETINGS TO THE GREENFORD COMPUTER CLUB  ...  "
    "KEEP THE SCENE ALIVE  ...  OMEGAWARE 2026  *  "
    "       ";
  const int scrollLen=strlen(scrollText);
  int scrollPos=0;
  const int charW=12;

  uint16_t copperColors[]={
    0xF800,0xF920,0xFAA0,0xFC00,0xFDE0,0xEFE0,0x87E0,0x07E0,
    0x07F0,0x07FF,0x041F,0x001F,0x801F,0xF81F,0xF810,0xF800
  };
  uint16_t sineColors[]={0xF800,0xFBE0,0xFFE0,0x07E0,0x07FF,0x001F,0xF81F,0xF800};
  const int numCopper=16,numSineColors=8;
  int frame=0;

  const unsigned long CRACKTRO_MS=6000;
  unsigned long startMs=millis();

  auto spFill  = [&](uint16_t c){ if(useSprite) spr.fillScreen(c); else tft.fillScreen(c); };
  auto spPixel = [&](int x,int y,uint16_t c){ if(useSprite) spr.drawPixel(x,y,c); else tft.drawPixel(x,y,c); };
  auto spRect  = [&](int x,int y,int w,int h,uint16_t c){ if(useSprite) spr.fillRect(x,y,w,h,c); else tft.fillRect(x,y,w,h,c); };

  while(true){
    uint16_t tx,ty;
    if(Touch_ReadFrame()&&getTouchXY(&tx,&ty)){ uint32_t t0=millis(); while(Touch_ReadFrame()&&getTouchXY(&tx,&ty)&&millis()-t0<500) delay(10); break; }
    if(!g_loop_cracktro&&millis()-startMs>=CRACKTRO_MS) break;

    spFill(TFT_BLACK);

    // Stars
    for(int i=0;i<NUM_STARS;i++){
      uint16_t col=(star_speed[i]==3)?TFT_WHITE:(star_speed[i]==2)?(uint16_t)0x7BEF:(uint16_t)0x4208;
      spPixel(star_x[i],star_y[i],col);
      star_x[i]-=star_speed[i];
      if(star_x[i]<0){ star_x[i]=gW-1; star_y[i]=random(0,gH-30); }
    }

    // Copper bars
    int copperY=gH/2-20+(int)(30.0f*sinf((float)frame*0.07f));
    for(int i=0;i<numCopper;i++){
      int by=copperY+i*3; if(by>=0&&by<gH-30) spRect(0,by,gW,2,copperColors[i]);
    }

    // Title text — drawn into sprite
    if(useSprite){
      spr.setTextSize(3); spr.setTextColor(sineColors[(frame/4)%numSineColors]);
      const char* s="MEZ & DIMMY"; int tw=strlen(s)*18;
      spr.setCursor((gW-tw)/2,copperY-36); spr.print(s);
      spr.setTextSize(2); spr.setTextColor((frame%30<15)?TFT_CYAN:TFT_WHITE);
      s="- OMEGAWARE -"; tw=strlen(s)*12;
      spr.setCursor((gW-tw)/2,copperY+40); spr.print(s);
      spr.setTextSize(1); spr.setTextColor(0x7BEF);
      s="GOTEK TOUCHSCREEN INTERFACE"; tw=spr.textWidth(s);
      spr.setCursor((gW-tw)/2,copperY+56); spr.print(s);
      spr.setTextColor((frame/10)%2==0?(uint16_t)0x4A8A:TFT_BLACK);
      s="TAP TO CONTINUE"; tw=spr.textWidth(s);
      spr.setCursor((gW-tw)/2,gH-42); spr.print(s);
      // Scroll bar
      spRect(0,gH-28,gW,28,0x0010);
      spr.setTextSize(2); spr.setTextColor(TFT_YELLOW); spr.setTextWrap(false);
      spr.setClipRect(0,gH-28,gW,28);
      int sc=scrollPos/charW,px2=scrollPos%charW;
      spr.setCursor(-px2,gH-28+4);
      for(int c=0;c<(gW/charW)+3;c++){ char buf[2]={scrollText[(sc+c)%scrollLen],0}; spr.print(buf); }
      spr.clearClipRect();
    } else {
      // Fallback direct draw (no sprite)
      tft.setTextSize(2); tft.setTextColor(sineColors[(frame/4)%numSineColors]);
      const char* s="MEZ & DIMMY"; int tw=strlen(s)*12;
      tft.setCursor((gW-tw)/2,copperY-24); tft.print(s);
      spRect(0,gH-28,gW,28,0x0010);
      tft.setTextSize(1); tft.setTextColor(TFT_YELLOW);
      int sc=scrollPos/charW,px2=scrollPos%charW;
      tft.setCursor(-px2,gH-24);
      for(int c=0;c<(gW/charW)+3;c++){ char buf[2]={scrollText[(sc+c)%scrollLen],0}; tft.print(buf); }
    }

    scrollPos+=2; frame++;

    if(useSprite){ tft.startWrite(); spr.pushSprite(0,0); tft.endWrite(); }
    delay(33);
  }

  if(useSprite) spr.deleteSprite();
  tft.fillScreen(TFT_BLACK);
}

// ============================================================================
// LOAD / UNLOAD
// ============================================================================
static bool doLoadSelected(const String& adfPath){
  tft.fillRect(0,STATUS_H,COVER_W,LCD_HEIGHT-STATUS_H-BOTTOM_H,COL_PANEL);
  tft.setTextSize(1); tft.setTextColor(TFT_CYAN,COL_PANEL);
  String title=basenameNoExt(filenameOnly(adfPath));
  if(title.length()>14) title=title.substring(0,14);
  tft.setCursor(4,STATUS_H+12); tft.print(title);
  tft.setTextColor(COL_LIT,COL_PANEL);
  tft.setCursor(4,STATUS_H+24); tft.print("Loading...");

  File f=openNamedImage(adfPath);
  if(!f){
    tft.setTextColor(0xE8C4,COL_PANEL); tft.setCursor(4,STATUS_H+36);
    tft.print("Open failed"); delay(1000); drawListAndCover(); return false;
  }
  uint32_t fsz=f.size();
  if(fsz==0){ f.close(); drawListAndCover(); return false; }
  if(fsz>MAX_FILE_BYTES) fsz=MAX_FILE_BYTES;
  build_volume_with_file(getOutputFilename(),fsz);

  int barX=4,barY=STATUS_H+40,barW=COVER_W-8,barH=10;
  tft.drawRoundRect(barX,barY,barW,barH,3,COL_DIM);

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
    tft.fillRoundRect(barX+2,barY+2,fill,barH-4,2,COL_GREEN);
  }
  free(buf);
  if(fsz>copied) memset(dst+copied,0,fsz-copied);
  f.close();

  tft.setTextColor(COL_GREEN,COL_PANEL); tft.setTextSize(1);
  tft.setCursor(4,barY+14); tft.print("OK "+String(copied/1024)+"KB");
  delay(400);

  hardAttach();
  g_loaded=true;
  g_loaded_name=basenameNoExt(filenameOnly(adfPath));
  g_loaded_game_idx=g_sel;
  g_loaded_disk_idx=g_disk_sel;

  if(g_wireless_mode&&espnowIsPaired()){
    String modeName=(g_mode==MODE_ADF)?"ADF":"DSK";
    espnowSendNotify(g_loaded_name,modeName,copied);
    espnowSendDisk(copied);
  }

  drawStatusBar(); drawListAndCover();
  return true;
}

static void doUnload(){
  hardDetach();
  g_loaded=false; g_loaded_name="";
  g_loaded_game_idx=-1; g_loaded_disk_idx=-1;
  if(g_wireless_mode&&espnowIsPaired()) espnowSendEject();
  drawStatusBar(); drawListAndCover();
}

// ============================================================================
// DEBOUNCED TOUCH
// ============================================================================
static const uint32_t BTN_COOLDOWN_MS=200;
static const int      BTN_MOVE_TOL_PX=15;
static bool     bbTouchDown=false;
static uint32_t bbLastActionMs=0;
static int      bbStartX=0,bbStartY=0;

// ============================================================================
// SETUP
// ============================================================================
void setup(){
  applyTheme(0);
  Serial.begin(115200); delay(200);

  uiInit();

  g_disk=(uint8_t*)ps_malloc((size_t)TOTAL_SECTORS*SECTOR_SIZE);
  if(!g_disk) g_disk=(uint8_t*)malloc((size_t)TOTAL_SECTORS*SECTOR_SIZE);
  if(!g_disk){ uiERR("RAM alloc failed"); while(true) delay(1000); }
  build_volume_with_file(getOutputFilename(),(g_mode==MODE_ADF)?ADF_DEFAULT_SIZE:64);

  SD_MMC.setPins(SD_CLK,SD_CMD,SD_D0,SD_D1,SD_D2,SD_D3);
  delay(100);
  bool sdok=SD_MMC.begin("/sdcard",true);
  if(!sdok){ delay(200); sdok=SD_MMC.begin("/sdcard",true); }
  if(sdok){
    ensureConfig();
    listImages(SD_MMC,g_files);
    buildGameList();
    buildActiveLetters();
    g_sel=0; g_scroll=0; g_disk_sel=0;
  } else {
    uiERR("SD mount failed"); delay(2000);
  }

  CST328_Begin();
  espnowBegin();
  loadTheme();
  drawCracktroSplash();

  USB.onEvent(usbEventCallback);
  MSC.vendorID("ESP32"); MSC.productID("RAMDISK"); MSC.productRevision("1.0");
  MSC.onRead(onRead); MSC.onWrite(onWrite); MSC.mediaPresent(true);
  MSC.begin(TOTAL_SECTORS,SECTOR_SIZE); USB.begin();
  hardDetach();
  drawFullUI();
}

// ============================================================================
// LOOP
// ============================================================================
void loop(){
  // Flash LINK ESTABLISHED banner once when XIAO pairs
  if(g_espnow_link_just_established){
    g_espnow_link_just_established=false;
    tft.fillRect(0,0,LCD_WIDTH,STATUS_H,0x07E0);
    tft.setTextSize(1); tft.setTextColor(TFT_BLACK,0x07E0);
    int tw=tft.textWidth("** XIAO LINK ESTABLISHED **");
    tft.setCursor((LCD_WIDTH-tw)/2,6); tft.print("** XIAO LINK ESTABLISHED **");
    delay(2000);
    drawStatusBar();
  }

  static uint32_t last=0;
  if(millis()-last<16){ delay(1); return; }
  last=millis();

  uint16_t px=0,py=0;
  bool haveTouch=Touch_ReadFrame()&&getTouchXY(&px,&py);
  uint32_t nowMs=millis();

  if(!haveTouch){ bbTouchDown=false; delay(1); return; }
  if(nowMs-bbLastActionMs<BTN_COOLDOWN_MS){ delay(1); return; }
  if(bbTouchDown){
    int dx=abs((int)px-bbStartX),dy=abs((int)py-bbStartY);
    if(dx>BTN_MOVE_TOL_PX||dy>BTN_MOVE_TOL_PX) bbTouchDown=false;
    delay(1); return;
  }
  bbTouchDown=true; bbStartX=px; bbStartY=py; bbLastActionMs=nowMs;

  // ── A-Z bar ─────────────────────────────────────────────────────────────
  if(px>=AZ_X&&py>=LIST_TOP&&py<LIST_BOTTOM){
    if(handleAlphabetTouch(px,py)) drawListAndCover();
    return;
  }

  // ── Cover panel: INFO button touches ────────────────────────────────────
  if(g_info_showing&&px<COVER_W){
    int pw=COVER_W-12;
    int saY=g_info_pair_btn_y;
    int wiY=saY+26;
    const int maxY=LCD_HEIGHT-BOTTOM_H-32;

    // STANDALONE
    if(py>=(uint16_t)saY&&py<(uint16_t)(saY+22)&&px>=4&&px<COVER_W-4){
      if(g_wireless_mode) setWirelessMode(false); return;
    }
    // WIRELESS
    if(py>=(uint16_t)wiY&&py<(uint16_t)(wiY+22)&&px>=4&&px<COVER_W-4){
      if(!g_wireless_mode) setWirelessMode(true); return;
    }
    // SOFT RESET (standalone mode only)
    if(!g_wireless_mode){
      int resetBtnY=maxY-22;
      if(py>=(uint16_t)resetBtnY&&py<(uint16_t)(resetBtnY+20)&&px>=4&&px<COVER_W-4){
        tft.fillRoundRect(4,resetBtnY,pw,20,5,(uint16_t)0xE8C4);
        tft.setTextSize(1); tft.setTextColor(TFT_BLACK,(uint16_t)0xE8C4);
        { int tw=tft.textWidth("RESTARTING..."); tft.setCursor(4+(pw-tw)/2,resetBtnY+6); } tft.print("RESTARTING...");
        delay(800); ESP.restart(); return;
      }
    }
    // PAIR NOW (wireless mode only)
    if(g_wireless_mode){
      int pairBtnY=maxY-24;
      if(py>=(uint16_t)pairBtnY&&py<(uint16_t)(pairBtnY+20)&&px>=4&&px<COVER_W-4){
        doPairNow(); return;
      }
    }
  }

  // ── INSERT/EJECT button ──────────────────────────────────────────────────
  {
    int btnY=LCD_HEIGHT-BOTTOM_H-30;
    if(px<COVER_W&&py>=(uint16_t)btnY&&py<LCD_HEIGHT-BOTTOM_H&&!g_games.empty()){
      bool isLoaded=(g_loaded&&g_loaded_game_idx==g_sel);
      if(isLoaded){ doUnload(); }
      else {
        const GameEntry& game=g_games[g_sel];
        int diskFileIdx=game.disk_indices.empty()?game.first_file_idx:game.disk_indices[min(g_disk_sel,(int)game.disk_indices.size()-1)];
        doLoadSelected(g_files[diskFileIdx]);
      }
      return;
    }
  }

  // ── Disk selector ────────────────────────────────────────────────────────
  if(px<COVER_W&&!g_games.empty()){
    const GameEntry& game=g_games[g_sel];
    if(game.disk_count>1){
      int btnY=LCD_HEIGHT-BOTTOM_H-30;
      int diskY=btnY-26;
      int btnW=18,btnH=14,gap=2;
      int totalW=game.disk_count*(btnW+gap)-gap;
      int startX=max(4,(COVER_W-totalW)/2);
      if(py>=(uint16_t)diskY&&py<(uint16_t)(diskY+btnH)){
        int hitBtn=(px-startX)/(btnW+gap);
        if(hitBtn>=0&&hitBtn<game.disk_count){
          g_disk_sel=hitBtn;
          if(g_loaded&&g_loaded_game_idx==g_sel){
            int diskFileIdx=game.disk_indices[g_disk_sel];
            doLoadSelected(g_files[diskFileIdx]);
          } else drawCoverPanel();
        }
        return;
      }
    }
  }

  // ── Mode bar ADF/DSK ─────────────────────────────────────────────────────
  if(py>=STATUS_H&&py<STATUS_H+MODE_BAR_H&&px>=LIST_X){
    if(px>=LIST_X+3&&px<LIST_X+33&&g_mode!=MODE_ADF){
      g_mode=MODE_ADF; if(!listImages(SD_MMC,g_files)) g_files.clear();
      buildGameList(); buildActiveLetters(); g_sel=0; g_scroll=0; g_disk_sel=0;
      drawFullUI(); return;
    }
    if(px>=LIST_X+36&&px<LIST_X+66&&g_mode!=MODE_DSK){
      g_mode=MODE_DSK; if(!listImages(SD_MMC,g_files)) g_files.clear();
      buildGameList(); buildActiveLetters(); g_sel=0; g_scroll=0; g_disk_sel=0;
      drawFullUI(); return;
    }
  }

  // ── File list tap ────────────────────────────────────────────────────────
  if(px>=LIST_X&&px<AZ_X&&py>=LIST_TOP&&py<LIST_BOTTOM){
    g_info_showing=false;
    int vi=(py-LIST_TOP)/LIST_ITEM_H;
    int gi=g_scroll+vi;
    if(gi>=0&&gi<(int)g_games.size()){
      if(gi==g_sel){
        bool isLoaded=(g_loaded&&g_loaded_game_idx==g_sel);
        if(isLoaded){ doUnload(); }
        else {
          const GameEntry& game=g_games[g_sel];
          int diskFileIdx=game.disk_indices.empty()?game.first_file_idx:game.disk_indices[0];
          g_disk_sel=0; doLoadSelected(g_files[diskFileIdx]);
        }
      } else { g_sel=gi; g_disk_sel=0; drawListAndCover(); }
    }
    return;
  }

  // ── Bottom bar ───────────────────────────────────────────────────────────
  if(py>=LCD_HEIGHT-BOTTOM_H){
    int bw=LCD_WIDTH/4; int btn=px/bw;
    if(btn==0){
      if(g_sel>0){ g_sel--; g_disk_sel=0; if(g_sel<g_scroll) g_scroll=g_sel; drawListAndCover(); }
    } else if(btn==1){
      if(g_sel<(int)g_games.size()-1){
        g_sel++; g_disk_sel=0;
        if(g_sel>=g_scroll+ITEMS_VIS) g_scroll=g_sel-ITEMS_VIS+1;
        int maxOff=(int)g_games.size()-ITEMS_VIS; if(maxOff<0) maxOff=0;
        if(g_scroll>maxOff) g_scroll=maxOff;
        drawListAndCover();
      }
    } else if(btn==2){
      cycleTheme();
    } else {
      // INFO panel — 110px wide, must stay within cover area
      tft.fillRect(0,STATUS_H,COVER_W,LCD_HEIGHT-STATUS_H-BOTTOM_H,COL_PANEL);
      int y=STATUS_H+4;
      int pw=COVER_W-8;
      // Hard stop: leave room for INSERT/EJECT button at bottom
      const int maxY=LCD_HEIGHT-BOTTOM_H-32;

      // Label
      tft.setTextSize(1); tft.setTextColor(COL_DIM,COL_PANEL);
      tft.setCursor(4,y); tft.print("MODE"); y+=10;

      // STANDALONE button — single line only
      uint16_t saCol=!g_wireless_mode?COL_GREEN:COL_BAR;
      tft.fillRoundRect(4,y,pw,22,5,saCol);
      tft.drawRoundRect(4,y,pw,22,5,!g_wireless_mode?COL_GREEN:COL_DIM);
      tft.setTextColor(!g_wireless_mode?TFT_BLACK:COL_DIM,saCol);
      { int tw=tft.textWidth("STANDALONE"); tft.setCursor(4+(pw-tw)/2,y+7); } tft.print("STANDALONE");
      int saY=y; y+=26;

      // WIRELESS button — single line only
      uint16_t wiCol=g_wireless_mode?COL_BLUE:COL_BAR;
      tft.fillRoundRect(4,y,pw,22,5,wiCol);
      tft.drawRoundRect(4,y,pw,22,5,g_wireless_mode?COL_BLUE:COL_DIM);
      tft.setTextColor(g_wireless_mode?TFT_WHITE:COL_DIM,wiCol);
      { int tw=tft.textWidth("WIRELESS"); tft.setCursor(4+(pw-tw)/2,y+7); } tft.print("WIRELESS");
      g_info_pair_btn_y=saY; y+=26;

      // Divider + stats
      tft.drawFastHLine(4,y,pw,COL_SEP); y+=4;
      tft.setTextColor(COL_LIT,COL_PANEL);
      tft.setCursor(4,y); tft.print(String(g_games.size())+" games"); y+=9;
      tft.setCursor(4,y); tft.print(FW_VERSION); y+=9;

      // Wireless status (only if paired and in wireless mode)
      if(g_wireless_mode&&y<maxY){
        tft.drawFastHLine(4,y,pw,COL_SEP); y+=4;
        if(espnowIsPaired()){
          // Pairing status
          if(y<maxY){ tft.setTextColor(COL_GREEN,COL_PANEL); tft.setCursor(4,y); tft.print("paired"); y+=9; }
          // Comms state
          bool online=espnowXiaoOnline();
          if(y<maxY){
            if(online){ tft.setTextColor(COL_GREEN,COL_PANEL); tft.setCursor(4,y); tft.print("WIFI: Comm"); }
            else       { tft.setTextColor(TFT_RED,COL_PANEL);  tft.setCursor(4,y); tft.print("WIFI: No Comm"); }
            y+=9;
          }
          if(y<maxY){
            if(g_espnow_xiao_done)      { tft.setTextColor(COL_GREEN,COL_PANEL); tft.setCursor(4,y); tft.print("loaded OK"); }
            else if(g_espnow_xiao_error){ tft.setTextColor(0xE8C4,COL_PANEL);    tft.setCursor(4,y); tft.print("error"); }
            else                        { tft.setTextColor(COL_DIM,COL_PANEL);   tft.setCursor(4,y); tft.print("idle"); }
            y+=9;
          }
        } else {
          if(y<maxY){ tft.setTextColor(COL_ORANGE,COL_PANEL); tft.setCursor(4,y); tft.print("not paired"); y+=9; }
        }
      }

      // Buttons anchored from maxY upward
      if(g_wireless_mode){
        int pairBtnY=maxY-24;
        uint16_t pairCol=espnowIsPaired()?COL_GREEN:COL_AMBER;
        tft.fillRoundRect(4,pairBtnY,pw,20,5,pairCol);
        tft.setTextColor(TFT_BLACK,pairCol);
        const char* pl=espnowIsPaired()?"RE-PAIR":"PAIR NOW";
        { int tw=tft.textWidth(pl); tft.setCursor(4+(pw-tw)/2,pairBtnY+6); } tft.print(pl);
      } else {
        int resetBtnY=maxY-22;
        tft.fillRoundRect(4,resetBtnY,pw,20,5,(uint16_t)0x8000);
        tft.drawRoundRect(4,resetBtnY,pw,20,5,(uint16_t)0xE8C4);
        tft.setTextColor(TFT_WHITE,(uint16_t)0x8000);
        { int tw=tft.textWidth("SOFT RESET"); tft.setCursor(4+(pw-tw)/2,resetBtnY+6); } tft.print("SOFT RESET");
      }
      g_info_showing=true;
    }
    return;
  }
}
