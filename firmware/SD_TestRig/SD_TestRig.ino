// ============================================================================
//  GTi SD Card Test Rig  (ON-SCREEN, JC4827W543 4.3")  -  OMEGAWARE   rev2
//  Benchmarks an SD card the way the GTi sees it (1-bit SDIO), on the display.
//  Every phase is TIME-BOUNDED so a slow card can't make it run for minutes,
//  and each phase shows live progress. Side-by-side 20 vs 40 MHz.
//  Tap the bottom buttons to re-run:  [20MHz] [40MHz] [BOTH]. Swap cards freely.
//  Non-destructive (one temp file, removed).
//  Libs: Arduino_GFX + bb_captouch. Board: ESP32S3, 4MB, OPI PSRAM, USB-OTG.
// ============================================================================
#include <FS.h>
#include <SD_MMC.h>
#include "esp_random.h"
#include <Arduino_GFX_Library.h>
#include <bb_captouch.h>

#define LCD_CS 45
#define LCD_CLK 47
#define LCD_D0 21
#define LCD_D1 48
#define LCD_D2 40
#define LCD_D3 39
#define LCD_BL 1
#define TP_SDA 8
#define TP_SCL 4
#define TP_INT 3
#define TP_RST 38
#define SD_CLK 12
#define SD_CMD 11
#define SD_D0  13

#define FREQ_DEFAULT   20000
#define FREQ_HIGHSPEED 40000
#define BENCH_PATH  "/_bench.tmp"
#define WRITE_MB    6        // cap on the write test size...
#define WRITE_MS    8000     // ...and a hard 8s time budget (whichever comes first)
#define RAND_READS  1000
#define ENUM_MS     8000     // stop the /ADF walk after 8s and report the rate
#define ENUM_ROOT   "/ADF"

#define C_BLACK 0x0000
#define C_WHITE 0xFFFF
#define C_ORANGE 0xFD20
#define C_YELLOW 0xFFE0
#define C_RED   0xF800
#define C_GREEN 0x07E0
#define C_BLUE  0x1B7F
#define C_DIM   0x8410

Arduino_DataBus *bus = NULL;
Arduino_GFX     *gfx = NULL;
BBCapTouch bbct;

static uint8_t buf[8192];
static int g_files=0, g_dirs=0;

struct Res { bool mounted; float w,r,q; float enumRate; int enumFiles; bool enumCapped; uint64_t mb; };
static Res r20, r40;

static void status(const String& s){
  gfx->fillRect(0,110,480,44,C_BLACK);
  gfx->setTextSize(2); gfx->setTextColor(C_WHITE); gfx->setCursor(14,122); gfx->print(s);
}

static bool mountAt(int freqKHz){
  SD_MMC.end(); delay(50);
  SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0);
  bool ok = SD_MMC.begin("/sdcard", true, false, freqKHz);
  if(!ok){ delay(200); ok = SD_MMC.begin("/sdcard", true, false, freqKHz); }
  return ok;
}
// write up to WRITE_MB, but stop at WRITE_MS ms; return MB/s of whatever got written
static float seqWrite(){
  SD_MMC.remove(BENCH_PATH);
  File f = SD_MMC.open(BENCH_PATH, FILE_WRITE); if(!f) return -1;
  for(int i=0;i<8192;i++) buf[i]=(uint8_t)i;
  size_t cap=(size_t)WRITE_MB*1024*1024, done=0; uint32_t t0=millis();
  while(done<cap && (millis()-t0)<WRITE_MS){ if(f.write(buf,8192)!=8192) break; done+=8192; if((done&0xFFFFF)==0) yield(); }
  f.flush(); f.close(); uint32_t dt=millis()-t0; if(dt==0)dt=1;
  return done? (float)done/1048576.0f/(dt/1000.0f) : -1;
}
static float seqRead(){
  File f = SD_MMC.open(BENCH_PATH, FILE_READ); if(!f) return -1;
  size_t done=0; uint32_t t0=millis();
  for(;;){ int n=f.read(buf,8192); if(n<=0) break; done+=n; if((done&0xFFFFF)==0) yield(); }
  f.close(); uint32_t dt=millis()-t0; if(dt==0)dt=1;
  return done? (float)done/1048576.0f/(dt/1000.0f) : -1;
}
static float randRead(){
  File f = SD_MMC.open(BENCH_PATH, FILE_READ); if(!f) return -1;
  size_t blocks=f.size()/512; if(blocks==0){ f.close(); return -1; }
  uint32_t t0=millis();
  for(int i=0;i<RAND_READS;i++){ uint32_t off=(esp_random()%blocks)*512; f.seek(off); f.read(buf,512); if((i&255)==0) yield(); }
  f.close(); uint32_t dt=millis()-t0; if(dt==0)dt=1;
  return (float)RAND_READS/(dt/1000.0f);
}
// walk /ADF but bail after ENUM_MS; report how many files/sec we managed
static bool g_enumCapped=false;
static void walk(File dir,int depth,uint32_t t0){
  File e=dir.openNextFile();
  while(e){
    if((millis()-t0)>ENUM_MS){ g_enumCapped=true; e.close(); return; }
    if(e.isDirectory()){ g_dirs++; if(depth<6) walk(e,depth+1,t0); }
    else g_files++;
    e.close();
    if(g_enumCapped) return;
    e=dir.openNextFile(); yield();
  }
}
static void enumTest(Res&R){
  g_files=0; g_dirs=0; g_enumCapped=false;
  File d=SD_MMC.open(ENUM_ROOT);
  if(!d||!d.isDirectory()){ if(d)d.close(); R.enumRate=0; R.enumFiles=0; R.enumCapped=false; return; }
  uint32_t t0=millis(); walk(d,0,t0); uint32_t dt=millis()-t0; if(dt==0)dt=1; d.close();
  R.enumFiles=g_files; R.enumCapped=g_enumCapped; R.enumRate=(float)g_files/(dt/1000.0f);
}

static Res runOne(int freqKHz, const char* tag){
  Res R={false,-1,-1,-1,0,0,false,0};
  status(String(tag)+": mounting...");
  if(!mountAt(freqKHz)) return R;
  R.mounted=true; R.mb=SD_MMC.cardSize()/(1024ULL*1024ULL);
  status(String(tag)+": writing...");  R.w=seqWrite();
  status(String(tag)+": reading...");  R.r=seqRead();
  status(String(tag)+": random...");   R.q=randRead();
  status(String(tag)+": scanning /ADF...");  enumTest(R);
  SD_MMC.remove(BENCH_PATH);
  return R;
}

static void drawCol(int x, const char* title, Res&R){
  gfx->setTextSize(2); gfx->setTextColor(C_ORANGE); gfx->setCursor(x,34); gfx->print(title);
  gfx->setTextSize(1); int y=58; char l[44];
  if(!R.mounted){ gfx->setTextColor(C_RED); gfx->setCursor(x,y); gfx->print("MOUNT FAILED"); return; }
  gfx->setTextColor(C_DIM);   snprintf(l,44,"card %llu MB",R.mb);                          gfx->setCursor(x,y); gfx->print(l); y+=15;
  gfx->setTextColor(C_WHITE); snprintf(l,44,"write %s MB/s", R.w<0?"FAIL":String(R.w,2).c_str()); gfx->setCursor(x,y); gfx->print(l); y+=15;
  gfx->setTextColor(C_WHITE); snprintf(l,44,"read  %s MB/s", R.r<0?"FAIL":String(R.r,2).c_str()); gfx->setCursor(x,y); gfx->print(l); y+=15;
  gfx->setTextColor(C_YELLOW);snprintf(l,44,"rand  %s /s",  R.q<0?"FAIL":String(R.q,0).c_str()); gfx->setCursor(x,y); gfx->print(l); y+=15;
  gfx->setTextColor(C_GREEN); snprintf(l,44,"scan  %s /s",  String(R.enumRate,0).c_str());  gfx->setCursor(x,y); gfx->print(l); y+=15;
  gfx->setTextColor(C_DIM);   snprintf(l,44,"%d files%s", R.enumFiles, R.enumCapped?"+":""); gfx->setCursor(x,y); gfx->print(l);
}
static void button(int x,const char* s,uint16_t col,uint16_t ink){
  gfx->fillRoundRect(x,232,148,34,6,col);
  gfx->setTextSize(2); gfx->setTextColor(ink);
  gfx->setCursor(x+(148 - (int)strlen(s)*12)/2, 240); gfx->print(s);
}
static void render(){
  gfx->fillScreen(C_BLACK);
  gfx->setTextSize(2); gfx->setTextColor(C_YELLOW); gfx->setCursor(10,6); gfx->print("SD TEST RIG");
  gfx->setTextSize(1); gfx->setTextColor(C_DIM); gfx->setCursor(210,12); gfx->print("1-bit SDIO");
  gfx->drawFastVLine(238,30,180,C_DIM);
  drawCol(16,  "20 MHz", r20);
  drawCol(252, "40 MHz", r40);
  button(8,  "20MHz", C_BLUE,  C_WHITE);
  button(166,"40MHz", C_BLUE,  C_WHITE);
  button(324,"BOTH",  C_GREEN, C_BLACK);
}

void setup(){
  bus = new Arduino_ESP32QSPI(LCD_CS,LCD_CLK,LCD_D0,LCD_D1,LCD_D2,LCD_D3);
  gfx = new Arduino_NV3041A(bus, GFX_NOT_DEFINED, 0, true);
  gfx->begin(32000000);
  gfx->fillScreen(C_BLACK);
  ledcAttach(LCD_BL,5000,8); ledcWrite(LCD_BL,200);
  bbct.init(TP_SDA,TP_SCL,TP_RST,TP_INT);
  gfx->setTextSize(2); gfx->setTextColor(C_YELLOW); gfx->setCursor(10,6); gfx->print("SD TEST RIG");
  r20 = runOne(FREQ_DEFAULT,  "20MHz");
  r40 = runOne(FREQ_HIGHSPEED,"40MHz");
  render();
}
void loop(){
  TOUCHINFO ti; ti.count=0; bbct.getSamples(&ti);
  if(ti.count>0){
    int x=ti.x[0], y=ti.y[0];
    if(y>=225){
      if(x<160){ r20=runOne(FREQ_DEFAULT,"20MHz"); }
      else if(x<318){ r40=runOne(FREQ_HIGHSPEED,"40MHz"); }
      else { r20=runOne(FREQ_DEFAULT,"20MHz"); r40=runOne(FREQ_HIGHSPEED,"40MHz"); }
      render();
      delay(400);
    }
  }
  delay(40);
}
