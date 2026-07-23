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
// v4.5.0 "K": LovyanGFX REMOVED. Display now runs on esp_lcd directly:
//   * num_fbs=2 driver framebuffers + bounce buffer (anti-glitch under SD/USB load)
//   * all UI draws go to a PSRAM compose buffer (JC3248-style gfx architecture)
//   * flush = copy compose -> hidden framebuffer, VSYNC page-flip, wait for latch
//   * GT911 touch raw I2C @ 0x14 (bench-proven in K_DISPLAYTEST v3 — tear-free)
// Landscape only (ROTATE=0/180). No LovyanGFX version pin needed anymore.
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

// K display stack: esp_lcd RGB panel (double framebuffer) — no LovyanGFX
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include <Wire.h>
#include <JPEGDEC.h>
#include <sys/stat.h>
#include <vector>
#include <algorithm>
#include <ctype.h>
#include "esp_random.h"
#include "diag_adf.h"      // embedded Amiga Test Kit ADF (zero-RLE compressed, public domain)

// ---------- Version ----------
#define FW_VERSION  "v4.6.4-7IN"

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

#define GT911_INT_PIN 4

// Expander bit map (Waveshare 7"): bit1=TP_RST, bit2=DISP/backlight, bit3=LCD_RST,
// bit4=SD_CS, bit5=USB_SEL. *** USB_SEL MUST STAY LOW *** — high routes the USB
// data lines to the CAN transceiver and the MSC virtual disk vanishes (v4.5.0 bug:
// writing 0xFF killed the USB key while the screen kept working).
#define EXP_ALL_ON   0xDF   // everything high EXCEPT USB_SEL (bit5)
#define EXP_TPRST_LO 0xDD   // same, with TP_RST (bit1) pulled low for the GT911 reset

// Write the expander output register on BOTH possible chips (CH422G 0x38 /
// TCA9554 0x20:reg1) — only the one actually fitted will care.
static void expanderOut(uint8_t val){
  Wire.beginTransmission(0x38); Wire.write(val); Wire.endTransmission();
  Wire.beginTransmission(EXPANDER_I2C_ADDR); Wire.write(0x01); Wire.write(val); Wire.endTransmission();
}

static void initExpander() {
  Wire.begin(EXPANDER_I2C_SDA, EXPANDER_I2C_SCL, 400000);
  // CH422G (this board rev): 0x24 = system/config, 0x38 = output register.
  Wire.beginTransmission(0x24); Wire.write(0x01); Wire.endTransmission();  // outputs enabled
  // TCA9554 fallback (older revs): all pins -> outputs
  Wire.beginTransmission(EXPANDER_I2C_ADDR);
  Wire.write(0x03); Wire.write(0x00);
  Wire.endTransmission();

  // ── GT911 deterministic reset ─────────────────────────────────────────────
  // The GT911 latches its I2C address AT RESET from the INT pin: LOW -> 0x5D,
  // HIGH -> 0x14. LovyanGFX used to do this dance (that's why the old firmware
  // lived at 0x5D); without it the address is a per-boot coin toss. Hold INT
  // LOW, pulse TP_RST (expander bit1) low->high, release INT: address = 0x5D.
  pinMode(GT911_INT_PIN, OUTPUT);
  digitalWrite(GT911_INT_PIN, LOW);
  expanderOut(EXP_TPRST_LO);   // TP_RST low; backlight/LCD high; USB_SEL kept LOW
  delay(15);
  expanderOut(EXP_ALL_ON);     // TP_RST high — GT911 samples INT (LOW) => addr 0x5D
  delay(10);
  pinMode(GT911_INT_PIN, INPUT);   // release INT back to the GT911
  delay(80);           // GT911 needs >50ms after reset before I2C
  // Wire stays OPEN: we drive the GT911 ourselves now.
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

// ============================================================================
// K DISPLAY ENGINE — esp_lcd RGB panel, double framebuffer, VSYNC page-flip.
// Bench-proven in K_DISPLAYTEST v3 (tear-free). All UI code below draws through
// the KGfx shim, which keeps the LovyanGFX-style API (setCursor/print/fillRect/
// textWidth/...) so the existing 300+ draw call sites compile unchanged, but
// renders into a PSRAM compose buffer — JC3248 architecture, unified codebase.
// ============================================================================
#define KLCD_W 800
#define KLCD_H 480
// GT911 address is probed at boot (0x5D after our deterministic reset; 0x14 fallback)

// 6x8 bitmap font, ASCII 32..126 (also used by the cracktro as crk_font).
static const uint8_t crk_font[95][6] PROGMEM = {
  {0x00,0x00,0x00,0x00,0x00,0x00},
  {0x00,0x00,0x4F,0x00,0x00,0x00},
  {0x00,0x07,0x00,0x07,0x00,0x00},
  {0x14,0x7F,0x14,0x7F,0x14,0x00},
  {0x24,0x2A,0x7F,0x2A,0x12,0x00},
  {0x62,0x64,0x08,0x13,0x23,0x00},
  {0x36,0x49,0x49,0x36,0x50,0x00},
  {0x00,0x04,0x03,0x00,0x00,0x00},
  {0x00,0x1C,0x22,0x41,0x00,0x00},
  {0x00,0x41,0x22,0x1C,0x00,0x00},
  {0x14,0x08,0x3E,0x08,0x14,0x00},
  {0x08,0x08,0x3E,0x08,0x08,0x00},
  {0x00,0x50,0x30,0x00,0x00,0x00},
  {0x08,0x08,0x08,0x08,0x08,0x00},
  {0x00,0x60,0x60,0x00,0x00,0x00},
  {0x20,0x10,0x08,0x04,0x02,0x00},
  {0x3E,0x51,0x49,0x45,0x3E,0x00},
  {0x00,0x42,0x7F,0x40,0x00,0x00},
  {0x42,0x61,0x51,0x49,0x46,0x00},
  {0x21,0x41,0x45,0x4B,0x31,0x00},
  {0x18,0x14,0x12,0x7F,0x10,0x00},
  {0x27,0x45,0x45,0x45,0x39,0x00},
  {0x3C,0x4A,0x49,0x49,0x30,0x00},
  {0x01,0x71,0x09,0x05,0x03,0x00},
  {0x36,0x49,0x49,0x49,0x36,0x00},
  {0x06,0x49,0x49,0x29,0x1E,0x00},
  {0x00,0x36,0x36,0x00,0x00,0x00},
  {0x00,0x56,0x36,0x00,0x00,0x00},
  {0x08,0x14,0x22,0x41,0x00,0x00},
  {0x14,0x14,0x14,0x14,0x14,0x00},
  {0x00,0x41,0x22,0x14,0x08,0x00},
  {0x02,0x01,0x51,0x09,0x06,0x00},
  {0x32,0x49,0x79,0x41,0x3E,0x00},
  {0x7E,0x11,0x11,0x11,0x7E,0x00},
  {0x7F,0x49,0x49,0x49,0x36,0x00},
  {0x3E,0x41,0x41,0x41,0x22,0x00},
  {0x7F,0x41,0x41,0x41,0x3E,0x00},
  {0x7F,0x49,0x49,0x49,0x41,0x00},
  {0x7F,0x09,0x09,0x09,0x01,0x00},
  {0x3E,0x41,0x49,0x49,0x3A,0x00},
  {0x7F,0x08,0x08,0x08,0x7F,0x00},
  {0x00,0x41,0x7F,0x41,0x00,0x00},
  {0x20,0x40,0x41,0x3F,0x01,0x00},
  {0x7F,0x08,0x14,0x22,0x41,0x00},
  {0x7F,0x40,0x40,0x40,0x40,0x00},
  {0x7F,0x02,0x0C,0x02,0x7F,0x00},
  {0x7F,0x04,0x08,0x10,0x7F,0x00},
  {0x3E,0x41,0x41,0x41,0x3E,0x00},
  {0x7F,0x09,0x09,0x09,0x06,0x00},
  {0x3E,0x41,0x41,0x21,0x5E,0x00},
  {0x7F,0x09,0x19,0x29,0x46,0x00},
  {0x46,0x49,0x49,0x49,0x31,0x00},
  {0x01,0x01,0x7F,0x01,0x01,0x00},
  {0x3F,0x40,0x40,0x40,0x3F,0x00},
  {0x1F,0x20,0x40,0x20,0x1F,0x00},
  {0x3F,0x40,0x38,0x40,0x3F,0x00},
  {0x63,0x14,0x08,0x14,0x63,0x00},
  {0x07,0x08,0x70,0x08,0x07,0x00},
  {0x61,0x51,0x49,0x45,0x43,0x00},
  {0x00,0x7F,0x41,0x00,0x00,0x00},
  {0x02,0x04,0x08,0x10,0x20,0x00},
  {0x00,0x41,0x7F,0x00,0x00,0x00},
  {0x04,0x02,0x01,0x02,0x04,0x00},
  {0x40,0x40,0x40,0x40,0x40,0x00},
  {0x00,0x01,0x02,0x04,0x00,0x00},
  {0x20,0x54,0x54,0x54,0x78,0x00},
  {0x7F,0x48,0x44,0x44,0x38,0x00},
  {0x38,0x44,0x44,0x44,0x20,0x00},
  {0x38,0x44,0x44,0x48,0x7F,0x00},
  {0x38,0x54,0x54,0x54,0x18,0x00},
  {0x08,0x7E,0x09,0x01,0x02,0x00},
  {0x18,0xA4,0xA4,0x9C,0x78,0x00},
  {0x7F,0x08,0x04,0x04,0x78,0x00},
  {0x00,0x44,0x7D,0x40,0x00,0x00},
  {0x20,0x40,0x44,0x3D,0x00,0x00},
  {0x7F,0x10,0x28,0x44,0x00,0x00},
  {0x00,0x41,0x7F,0x40,0x00,0x00},
  {0x7C,0x04,0x78,0x04,0x78,0x00},
  {0x7C,0x08,0x04,0x04,0x78,0x00},
  {0x38,0x44,0x44,0x44,0x38,0x00},
  {0x7C,0x14,0x14,0x14,0x08,0x00},
  {0x08,0x14,0x14,0x18,0x7C,0x00},
  {0x7C,0x08,0x04,0x04,0x08,0x00},
  {0x48,0x54,0x54,0x54,0x20,0x00},
  {0x04,0x3F,0x44,0x40,0x20,0x00},
  {0x3C,0x40,0x40,0x20,0x7C,0x00},
  {0x1C,0x20,0x40,0x20,0x1C,0x00},
  {0x3C,0x40,0x30,0x40,0x3C,0x00},
  {0x44,0x28,0x10,0x28,0x44,0x00},
  {0x1C,0xA0,0xA0,0x9C,0x0C,0x00},
  {0x44,0x64,0x54,0x4C,0x44,0x00},
  {0x00,0x08,0x36,0x41,0x00,0x00},
  {0x00,0x00,0x7F,0x00,0x00,0x00},
  {0x00,0x41,0x36,0x08,0x00,0x00},
  {0x08,0x04,0x08,0x10,0x08,0x00}
};

// Font "handles" — keeps the existing UG->setFont(&lgfx::fonts::DejaVu12) call
// sites compiling. Each maps to a scale of the 6x8 font (fractional = nearest
// neighbour): Font0/DejaVu9 -> 1.0 (6x8), DejaVu12 -> 1.5 (9x12), DejaVu18 -> 2.0 (12x16).
namespace lgfx { namespace fonts {
  static const uint8_t Font0=0, DejaVu9=1, DejaVu12=2, DejaVu18=3;
}}

// VSYNC handshake (free functions — the callback runs in ISR context)
static volatile uint32_t k_vsync_count = 0;
static bool IRAM_ATTR kOnVsync(esp_lcd_panel_handle_t p,
                               const esp_lcd_rgb_panel_event_data_t* e, void* u){
  k_vsync_count++; return false;
}
static void kWaitVsync(){
  uint32_t s=k_vsync_count; uint32_t t0=millis();
  while(k_vsync_count==s && (millis()-t0)<50){ delayMicroseconds(200); }  // gentle wait — don't hammer the bus
}

class KGfx {
public:
  esp_lcd_panel_handle_t panel=NULL;
  uint16_t *fb0=NULL,*fb1=NULL;   // driver-owned framebuffers (num_fbs=2)
  uint16_t *cb=NULL;              // compose buffer (PSRAM) — ALL drawing lands here
  uint8_t  backIdx=0;
  bool     ok=false, flip180=false;
  // text state
  int   tx=0, ty=0, tsize=1;
  float fscale=1.0f;
  uint16_t tfg=0xFFFF, tbg=0x0000;
  // touch
  uint8_t gtAddr=0x5D;   // set by touchProbe()
  // clip window (used by the scrolling list so partial rows don't bleed out)
  int clx0=0, cly0=0, clx1=KLCD_W, cly1=KLCD_H;

  bool init(){
    esp_lcd_rgb_panel_config_t cfg = {};
    cfg.clk_src = LCD_CLK_SRC_DEFAULT;
    cfg.timings.pclk_hz            = 16000000;   // proven on this panel
    cfg.timings.h_res              = KLCD_W;
    cfg.timings.v_res              = KLCD_H;
    cfg.timings.hsync_pulse_width  = 48;
    cfg.timings.hsync_back_porch   = 88;
    cfg.timings.hsync_front_porch  = 40;
    cfg.timings.vsync_pulse_width  = 3;
    cfg.timings.vsync_back_porch   = 32;
    cfg.timings.vsync_front_porch  = 13;
    cfg.timings.flags.pclk_active_neg = 1;
    cfg.data_width            = 16;
    cfg.bits_per_pixel        = 16;
    cfg.num_fbs               = 2;                // double buffer -> VSYNC page-flip
    cfg.bounce_buffer_size_px = KLCD_W * 20;      // 20 lines (~1.2ms stall tolerance):
                                                  // SD/JPEG/WiFi PSRAM bursts starved the
                                                  // LCD DMA at 10 -> image drift/wrap
    cfg.psram_trans_align     = 64;
    cfg.hsync_gpio_num = 46;
    cfg.vsync_gpio_num = 3;
    cfg.de_gpio_num    = 5;
    cfg.pclk_gpio_num  = 7;
    cfg.disp_gpio_num  = -1;
    int dpins[16] = {14,38,18,17,10, 39,0,45,48,47,21, 1,2,42,41,40};
    for(int i=0;i<16;i++) cfg.data_gpio_nums[i] = dpins[i];
    cfg.flags.fb_in_psram = 1;
    if(esp_lcd_new_rgb_panel(&cfg,&panel)!=ESP_OK) return false;
    esp_lcd_rgb_panel_event_callbacks_t cbs = {};
    cbs.on_vsync = kOnVsync;
    cbs.on_bounce_frame_finish = kOnVsync;   // bounce-buffer builds signal here instead
    esp_lcd_rgb_panel_register_event_callbacks(panel,&cbs,NULL);
    if(esp_lcd_panel_reset(panel)!=ESP_OK) return false;
    if(esp_lcd_panel_init(panel)!=ESP_OK) return false;
    if(esp_lcd_rgb_panel_get_frame_buffer(panel,2,(void**)&fb0,(void**)&fb1)!=ESP_OK
       || !fb0 || !fb1) return false;
    cb=(uint16_t*)heap_caps_malloc((size_t)KLCD_W*KLCD_H*2,MALLOC_CAP_SPIRAM);
    if(!cb) return false;
    ok=true;
    touchProbe();          // lock onto the GT911's actual address (0x5D expected)
    fillScreen(0x0000); display();
    return true;
  }
  // -- LovyanGFX-compatible surface ------------------------------------------
  void setRotation(int r){ flip180=(r==2||r==3); }
  int  width()  const { return KLCD_W; }
  int  height() const { return KLCD_H; }
  void setBrightness(uint8_t){ /* backlight is expander-driven, always on */ }

  void setClip(int x0,int y0,int x1,int y1){
    clx0=max(0,x0); cly0=max(0,y0); clx1=min(KLCD_W,x1); cly1=min(KLCD_H,y1);
  }
  void resetClip(){ clx0=0; cly0=0; clx1=KLCD_W; cly1=KLCD_H; }
  inline void px(int x,int y,uint16_t c){
    if(x<clx0||x>=clx1||y<cly0||y>=cly1||!cb) return;
    cb[y*KLCD_W+x]=c;
  }
  void fillScreen(uint16_t c){ if(!cb)return; for(int i=0;i<KLCD_W*KLCD_H;i++) cb[i]=c; }
  void fillRect(int x,int y,int w,int h,uint16_t c){
    if(!cb)return;
    int x0=max(clx0,x),y0=max(cly0,y),x1=min(clx1,x+w),y1=min(cly1,y+h);
    for(int yy=y0;yy<y1;yy++){ uint16_t* r=cb+yy*KLCD_W; for(int xx=x0;xx<x1;xx++) r[xx]=c; }
  }
  void drawRect(int x,int y,int w,int h,uint16_t c){
    fillRect(x,y,w,1,c); fillRect(x,y+h-1,w,1,c); fillRect(x,y,1,h,c); fillRect(x+w-1,y,1,h,c);
  }
  void drawPixel(int x,int y,uint16_t c){ px(x,y,c); }
  void drawFastHLine(int x,int y,int w,uint16_t c){ fillRect(x,y,w,1,c); }
  void drawFastVLine(int x,int y,int h,uint16_t c){ fillRect(x,y,1,h,c); }
  void fillCircle(int cx,int cy,int r,uint16_t c){
    for(int dy=-r;dy<=r;dy++){ int hw=(int)sqrtf((float)(r*r-dy*dy)); fillRect(cx-hw,cy+dy,2*hw+1,1,c); }
  }
  void drawCircle(int cx,int cy,int r,uint16_t c){
    int x=0,y=r,d=3-2*r;
    while(x<=y){
      px(cx+x,cy+y,c);px(cx-x,cy+y,c);px(cx+x,cy-y,c);px(cx-x,cy-y,c);
      px(cx+y,cy+x,c);px(cx-y,cy+x,c);px(cx+y,cy-x,c);px(cx-y,cy-x,c);
      if(d<0)d+=4*x+6; else {d+=4*(x-y)+10;y--;} x++;
    }
  }
  void fillRoundRect(int x,int y,int w,int h,int r,uint16_t c){
    if(r<=0){ fillRect(x,y,w,h,c); return; }
    fillRect(x+r,y,w-2*r,h,c);
    fillRect(x,y+r,r,h-2*r,c);
    fillRect(x+w-r,y+r,r,h-2*r,c);
    for(int dy=-r;dy<=0;dy++){
      int dx=(int)sqrtf((float)(r*r-dy*dy));
      fillRect(x+r-dx,y+r+dy,dx,1,c);       fillRect(x+w-r,y+r+dy,dx,1,c);
      fillRect(x+r-dx,y+h-r-1-dy,dx,1,c);   fillRect(x+w-r,y+h-r-1-dy,dx,1,c);
    }
  }
  void drawRoundRect(int x,int y,int w,int h,int r,uint16_t c){
    drawFastHLine(x+r,y,w-2*r,c); drawFastHLine(x+r,y+h-1,w-2*r,c);
    drawFastVLine(x,y+r,h-2*r,c); drawFastVLine(x+w-1,y+r,h-2*r,c);
    // corner arcs — without these the outline renders as 4 floating lines
    int cx0=x+r, cy0=y+r, cx1=x+w-1-r, cy1=y+h-1-r;
    int xx=0, yy=r, d=3-2*r;
    while(xx<=yy){
      px(cx1+xx,cy1+yy,c); px(cx0-xx,cy1+yy,c); px(cx1+xx,cy0-yy,c); px(cx0-xx,cy0-yy,c);
      px(cx1+yy,cy1+xx,c); px(cx0-yy,cy1+xx,c); px(cx1+yy,cy0-xx,c); px(cx0-yy,cy0-xx,c);
      if(d<0)d+=4*xx+6; else {d+=4*(xx-yy)+10; yy--;}
      xx++;
    }
  }
  // -- text -------------------------------------------------------------------
  void setFont(const uint8_t* f){
    switch(f?*f:1){
      case 2:  fscale=1.5f; break;   // DejaVu12 -> 9x12
      case 3:  fscale=2.0f; break;   // DejaVu18 -> 12x16
      default: fscale=1.0f; break;   // Font0 / DejaVu9 -> 6x8
    }
  }
  void setTextSize(int s){ tsize=(s<1)?1:s; }
  void setTextColor(uint16_t f,uint16_t b){ tfg=f; tbg=b; }
  void setTextColor(uint16_t f){ tfg=f; }
  void setCursor(int x,int y){ tx=x; ty=y; }
  int  charW() const { return (int)(6.0f*fscale*tsize+0.5f); }
  int  charH() const { return (int)(8.0f*fscale*tsize+0.5f); }
  int  textWidth(const String& s){ return (int)s.length()*charW(); }
  int  textWidth(const char* s){ return (int)strlen(s)*charW(); }
  void drawGlyph(char ch){
    if(ch<32||ch>126) ch='?';
    const uint8_t* d=crk_font[ch-32];
    float s=fscale*tsize;
    int cw=charW(), chh=charH();
    for(int dy=0;dy<chh;dy++){
      int row=(int)(dy/s); if(row>7)row=7;
      for(int dx=0;dx<cw;dx++){
        int col=(int)(dx/s); if(col>5)col=5;
        uint8_t bits=pgm_read_byte(&d[col]);
        px(tx+dx,ty+dy,(bits&(1<<row))?tfg:tbg);
      }
    }
    tx+=cw;
  }
  void print(const String& s){ for(unsigned int i=0;i<s.length();i++) drawGlyph(s[i]); }
  void print(const char* s){ for(int i=0;s[i];i++) drawGlyph(s[i]); }
  // -- image blit -------------------------------------------------------------
  void pushImage(int x,int y,int w,int h,const uint16_t* img){
    if(!cb||!img) return;
    for(int yy=0;yy<h;yy++){
      int dy=y+yy; if(dy<cly0||dy>=cly1) continue;
      int sx=0, dx=x, cw=w;
      if(dx<clx0){ sx=clx0-dx; cw-=sx; dx=clx0; }
      if(dx+cw>clx1) cw=clx1-dx;
      if(cw>0) memcpy(cb+dy*KLCD_W+dx, img+yy*w+sx, (size_t)cw*2);
    }
  }
  // -- touch: raw GT911 -------------------------------------------------------
  void touchProbe(){
    // After the reset dance the GT911 should sit at 0x5D; probe both to be safe.
    const uint8_t cands[2]={0x5D,0x14};
    for(int i=0;i<2;i++){
      Wire.beginTransmission(cands[i]);
      if(Wire.endTransmission()==0){ gtAddr=cands[i]; return; }
    }
    gtAddr=0x5D;   // nothing ACKed (shouldn't happen) — keep the expected default
  }
  bool gtRead(uint16_t reg,uint8_t* buf,uint8_t len){
    Wire.beginTransmission(gtAddr);
    Wire.write((uint8_t)(reg>>8)); Wire.write((uint8_t)(reg&0xFF));
    if(Wire.endTransmission(false)!=0) return false;
    Wire.requestFrom((int)gtAddr,(int)len);
    uint8_t i=0; while(Wire.available()&&i<len) buf[i++]=Wire.read();
    return i==len;
  }
  void gtWrite8(uint16_t reg,uint8_t v){
    Wire.beginTransmission(gtAddr);
    Wire.write((uint8_t)(reg>>8)); Wire.write((uint8_t)(reg&0xFF)); Wire.write(v);
    Wire.endTransmission();
  }
  bool getTouch(int32_t* x,int32_t* y){
    uint8_t status=0;
    if(!gtRead(0x814E,&status,1)) return false;
    if(!(status&0x80)) return false;
    uint8_t n=status&0x0F; bool got=false;
    if(n>0){
      uint8_t p[8];
      // Register map from 0x8150: [0]=Xlo [1]=Xhi [2]=Ylo [3]=Yhi [4..5]=size.
      // (v4.5.0 bug: offsets were shifted one byte -> every tap clamped to the
      //  bottom-right corner = phantom INFO presses, "touch doesn't work".)
      if(gtRead(0x8150,p,8)){
        int32_t rx=(int32_t)(p[0]|(p[1]<<8));
        int32_t ry=(int32_t)(p[2]|(p[3]<<8));
        if(flip180){ rx=KLCD_W-1-rx; ry=KLCD_H-1-ry; }
        if(rx<0)rx=0; if(rx>=KLCD_W)rx=KLCD_W-1;
        if(ry<0)ry=0; if(ry>=KLCD_H)ry=KLCD_H-1;
        *x=rx; *y=ry; got=true;
      }
    }
    gtWrite8(0x814E,0);
    return got;
  }
  // -- flush: compose -> hidden framebuffer -> VSYNC page-flip ---------------
  void display(){
    if(!ok) return;
    uint16_t* dst = backIdx ? fb1 : fb0;
    if(!flip180){
      memcpy(dst, cb, (size_t)KLCD_W*KLCD_H*2);
    } else {
      // 180° flip during the copy (ROTATE=180: screen mounted upside-down)
      for(int y=0;y<KLCD_H;y++){
        const uint16_t* s=cb+y*KLCD_W;
        uint16_t* d=dst+(KLCD_H-1-y)*KLCD_W+(KLCD_W-1);
        for(int x=0;x<KLCD_W;x++) *d-- = *s++;
      }
    }
    esp_lcd_panel_draw_bitmap(panel,0,0,KLCD_W,KLCD_H,dst);  // driver FB ptr -> true page-flip
    kWaitVsync();                                            // wait for the flip to LATCH
    backIdx^=1;
  }
  void init_ok_or_halt(){
    if(!init()){ while(true){ delay(1000); } }   // panel bring-up failed — nothing we can show
  }
};
static KGfx tft;

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
  tft.display();   // K: compose buffer must be flushed to be visible
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
static bool findNFOFor(const String& adfPath, String& outNFO){
  String base = basenameNoExt(filenameOnly(adfPath));
  String dir  = parentDir(adfPath);
  String gameBase = getGameBaseName(adfPath);
  const char* exts[] = {".nfo",".NFO",".Nfo"};
  // Direct exists() checks (fast — no directory enumeration). Same approach as findJPGFor.
  for(auto e : exts){ String c=dir+"/"+base+e;     if(SD_MMC.exists(c.c_str())){ outNFO=c; return true; } }
  if(gameBase != base) for(auto e : exts){ String c=dir+"/"+gameBase+e; if(SD_MMC.exists(c.c_str())){ outNFO=c; return true; } }
  for(auto e : exts){ String c="/"+base+e;          if(SD_MMC.exists(c.c_str())){ outNFO=c; return true; } }
  if(gameBase != base) for(auto e : exts){ String c="/"+gameBase+e;     if(SD_MMC.exists(c.c_str())){ outNFO=c; return true; } }
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

// ---------- Touch: GT911 raw I2C @ 0x14 (KGfx::getTouch) ----------
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

#define NUM_STARS 120
static int16_t star_x[NUM_STARS], star_y[NUM_STARS], star_speed[NUM_STARS];
static bool g_loop_cracktro  = false;  // set by LOOP=1 in CONFIG.TXT
static bool g_wireless_mode  = false;  // set by MODE=WIRELESS in CONFIG.TXT
static int  g_rot            = 0;      // CONFIG.TXT ROTATE= : LovyanGFX rotation. 0=landscape, 2=180 flip. (90/270 portrait = stage 2)

static void initStars() {
  for(int i=0;i<NUM_STARS;i++){
    star_x[i]=random(0,gW); star_y[i]=random(0,gH); star_speed[i]=random(1,4);
  }
}


// (crk_font 6x8 table moved up into the K display engine — shared with KGfx text)
#define CRK_RGB(r,g,b) ((uint16_t)((((r)&0xF8)<<8)|(((g)&0xFC)<<3)|((b)>>3)))
#define CRK_SB 45
static const char* CRK_SCROLL="        OMEGAWARE PRESENTS ... THE GTi ... THE FLOPPY FLINGER THINGER ... CODED BY MEZ & DIMMY ... A LITTLE TRIBUTE TO THE AMIGA CRACKTRO LEGENDS ... GREETINGS TO EVERYONE KEEPING THE SCENE ALIVE ... NOW GO LOAD A GAME ...        ";
static int g_cracktro = 0;   // CONFIG.TXT CRACKTRO= : boot demo style 1..6, or 0 = random each boot
// ── K rendering targets ─────────────────────────────────────────────────────
// Everything (UI and cracktro) draws into the ONE KGfx compose buffer; a flush
// copies it to the hidden driver framebuffer and page-flips at VSYNC. The old
// LovyanGFX sprite machinery is gone — the flip IS the double buffer now.
static KGfx* crkG = &tft;                  // cracktro draw target
static KGfx* UG   = &tft;                  // UI draw target
static int   ui_depth = 0;                 // nesting depth so only the outermost redraw pushes

static inline void uiFlush(){ tft.display(); }
// RAII guard: wrap each top-level redraw. Nested draws share the frame; the
// outermost one flushes on scope exit -> one atomic tear-free flip per redraw.
struct UiFrame { UiFrame(){ ui_depth++; } ~UiFrame(){ if(--ui_depth==0) uiFlush(); } };

static uint16_t crk_hsl(float h,float s,float l){
  h=fmodf(fmodf(h,360.0f)+360.0f,360.0f); s*=0.01f; l*=0.01f;
  float c=(1.0f-fabsf(2.0f*l-1.0f))*s;
  float x=c*(1.0f-fabsf(fmodf(h/60.0f,2.0f)-1.0f));
  float m=l-c*0.5f,r,g,b; int seg=((int)(h/60.0f))%6;
  switch(seg){case 0:r=c;g=x;b=0;break;case 1:r=x;g=c;b=0;break;case 2:r=0;g=c;b=x;break;
    case 3:r=0;g=x;b=c;break;case 4:r=x;g=0;b=c;break;default:r=c;g=0;b=x;break;}
  return CRK_RGB((uint8_t)((r+m)*255.0f),(uint8_t)((g+m)*255.0f),(uint8_t)((b+m)*255.0f));
}
static inline uint16_t crk_hue(float h){return crk_hsl(h,100.0f,55.0f);}
static uint16_t crk_lerp(int r1,int g1,int b1,int r2,int g2,int b2,float k){
  if(k<0)k=0; if(k>1)k=1;
  return CRK_RGB((uint8_t)(r1+(r2-r1)*k),(uint8_t)(g1+(g2-g1)*k),(uint8_t)(b1+(b2-b1)*k));
}
static void crk_line(int x0,int y0,int x1,int y1,uint16_t c){
  int dx=abs(x1-x0),dy=-abs(y1-y0),sx=x0<x1?1:-1,sy=y0<y1?1:-1,err=dx+dy;
  for(int gd=0;gd<6000;gd++){crkG->drawPixel(x0,y0,c);if(x0==x1&&y0==y1)break;
    int e2=2*err;if(e2>=dy){err+=dy;x0+=sx;}if(e2<=dx){err+=dx;y0+=sy;}}
}
// transparent pixel glyphs (foreground only), scaled 6x8 font
static void crk_char(char ch,int x,int y,int sz,uint16_t col){
  if(ch<32||ch>126)return; const uint8_t*d=crk_font[ch-32];
  for(int k=0;k<6;k++){uint8_t bits=pgm_read_byte(&d[k]);
    for(int r=0;r<8;r++) if(bits&(1<<r)) crkG->fillRect(x+k*sz,y+r*sz,sz,sz,col);}
}
static int  crk_txtW(const char*s,int sz){return (int)strlen(s)*6*sz;}
static void crk_txt(int x,int y,const char*s,int sz,uint16_t col){for(int i=0;s[i];i++)crk_char(s[i],x+i*6*sz,y,sz,col);}
static void crk_txtC(int cx,int y,const char*s,int sz,uint16_t col){crk_txt(cx-crk_txtW(s,sz)/2,y,s,sz,col);}
static void crk_txtSh(int cx,int y,const char*s,int sz,uint16_t col){int x=cx-crk_txtW(s,sz)/2;crk_txt(x+2,y+2,s,sz,CRK_RGB(5,6,12));crk_txt(x,y,s,sz,col);}
static void crk_stars(){
  for(int i=0;i<NUM_STARS;i++){star_x[i]-=star_speed[i]; if(star_x[i]<0){star_x[i]=gW-1; star_y[i]=random(0,gH-CRK_SB);}
    uint16_t c=star_speed[i]==3?TFT_WHITE:star_speed[i]==2?CRK_RGB(159,180,214):CRK_RGB(66,80,110);
    int z=star_speed[i]>2?3:2; crkG->fillRect(star_x[i],star_y[i],z,z,c);}
}
static void crk_bar(int cy,int h,float hue){
  for(int i=-h/2;i<h/2;i++){float l=62.0f-fabsf((float)i)/(h/2.0f)*56.0f; crkG->fillRect(0,cy+i,gW,1,crk_hsl(hue,100.0f,l));}
}
static void crk_scroller(float t,uint16_t col,float amp,bool rainbow){
  const int sz=3,cw=18; int slen=strlen(CRK_SCROLL);
  long cs=(long)(t*0.16f); int sc=(int)(cs/cw),pxo=(int)(cs%cw);
  crkG->fillRect(0,gH-CRK_SB,gW,CRK_SB,CRK_RGB(5,7,15));
  for(int c=0;c<gW/cw+3;c++){char ch=CRK_SCROLL[(((sc+c)%slen)+slen)%slen]; int x=-pxo+c*cw;
    int y=gH-CRK_SB+8+(int)(sinf(x*0.02f+t*0.004f)*amp);
    crk_char(ch,x,y,sz, rainbow?crk_hue(x*0.9f+t*0.2f):col);}
}

// 1: COPPER CLASSIC
static void crkCopper(float t){
  crkG->fillScreen(CRK_RGB(4,6,13)); crk_stars();
  for(int b=0;b<3;b++){int cy=(int)(225+b*52+sinf(t*0.0022f+b*1.4f)*40); crk_bar(cy,45,t*0.06f+b*70);}
  crk_txtC(gW/2,50,"OMEGAWARE",7,crk_hue(t*0.12f));
  crk_txtC(gW/2,130,"* MEZ & DIMMY *",3,CRK_RGB(174,187,208));
  crk_scroller(t,CRK_RGB(255,224,0),18,false);
}
// 2: STARFIELD
static void crkStar(float t){
  crkG->fillScreen(CRK_RGB(2,3,10)); crk_stars(); crk_stars();
  int bx=(int)(gW/2+sinf(t*0.0016f)*250), by=(int)(180+sinf(t*0.0025f)*80);
  crk_txtSh(bx,by,"OMEGAWARE",5,crk_hsl(t*0.1f,100.0f,60.0f));
  crk_txtC(bx,by+52,"INTO THE VOID",2,CRK_RGB(127,208,255));
  crk_scroller(t,0,15,true);
}
// 3: RAINBOW RASTER
static void crkRaster(float t){
  { int st=(gH>600)?2:1; for(int y=0;y<gH-CRK_SB;y+=st) crkG->fillRect(0,y,gW,st,crk_hsl(y*0.9f+t*0.16f,100.0f,50.0f)); }
  crkG->fillRect(80,160,gW-160,140,CRK_RGB(6,8,18)); crkG->drawRect(80,160,gW-160,140,TFT_WHITE);
  crk_txtC(gW/2,190,"OMEGAWARE",7,TFT_WHITE);
  crk_txtC(gW/2,262,"CRACKED - TRAINED - LOADED",2,CRK_RGB(255,233,168));
  crk_scroller(t,TFT_WHITE,15,false);
}
// 4: PLASMA
static void crkPlasma(float t){
  const int bs=(gH>600)?16:10;
  for(int y=0;y<gH-CRK_SB;y+=bs)for(int x=0;x<gW;x+=bs){
    float v=sinf(x*0.02f+t*0.003f)+sinf(y*0.03f+t*0.0042f)+sinf((x+y)*0.017f+t*0.002f);
    crkG->fillRect(x,y,bs,bs,crk_hsl(v*60.0f+t*0.12f,90.0f,56.0f));}
  crk_txtSh(gW/2,85,"OMEGAWARE",7,TFT_WHITE);
  crk_txtC(gW/2,165,"MELT YOUR EYES",3,CRK_RGB(10,10,20));
  crk_scroller(t,TFT_WHITE,18,false);
}
// 5: BOING BALL
static void crkBoing(float t){
  crkG->fillScreen(CRK_RGB(12,12,22));
  uint16_t grd=CRK_RGB(70,36,96);
  for(int x=0;x<=gW;x+=52) crkG->fillRect(x,90,1,gH-CRK_SB-90,grd);
  for(int y=90;y<=gH-CRK_SB;y+=42) crkG->fillRect(0,y,gW,1,grd);
  int bx=(int)(gW/2+sinf(t*0.0016f)*250), gy=gH-CRK_SB-30, by=(int)(gy-fabsf(sinf(t*0.004f))*180), r=70;
  for(int yy=-8;yy<=8;yy++){int w=(int)(r*0.9f*sqrtf(1.0f-(yy/8.0f)*(yy/8.0f))); crkG->fillRect(bx-w,gy+8+yy,2*w+1,1,CRK_RGB(8,8,14));}
  float cell=r/3.2f, ph=fmodf(t*0.06f,cell*2);
  for(int yy=-r;yy<=r;yy++){int hw=(int)sqrtf((float)(r*r-yy*yy));
    for(int xx=-hw;xx<=hw;xx++){int cc=(((int)floorf((xx+ph)/cell))+((int)floorf((float)yy/cell)))&1;
      crkG->drawPixel(bx+xx,by+yy, cc?CRK_RGB(255,38,38):CRK_RGB(242,242,242));}}
  crkG->drawCircle(bx,by,r,CRK_RGB(122,0,0));
  crk_txtC(gW/2,40,"OMEGAWARE",5,CRK_RGB(255,59,59));
  crk_scroller(t,CRK_RGB(255,102,102),13,false);
}
// 6: SYNTHWAVE
static void crkSynth(float t){
  { int st=(gH>600)?2:1; for(int y=0;y<gH;y+=st){float f=(float)y/gH; uint16_t col;
    if(f<0.52f) col=crk_lerp(24,11,51,90,26,110,f/0.52f);
    else        col=crk_lerp(11,10,26,4,4,12,(f-0.53f)/0.47f);
    crkG->fillRect(0,y,gW,st,col);} }
  for(int i=0;i<40;i++) crkG->drawPixel((i*53+7)%gW,(i*29)%230,TFT_WHITE);
  int sunx=gW/2,suny=250,sr=88;
  for(int yy=-sr;yy<=0;yy++){int w=(int)sqrtf((float)(sr*sr-yy*yy)); crkG->fillRect(sunx-w,suny+yy,2*w,1,CRK_RGB(255,91,138));}
  for(int i=0;i<7;i++){int yy=178+i*12; crkG->fillRect(sunx-90,yy,180,4+i,CRK_RGB(24,11,51));}
  uint16_t grc=CRK_RGB(0,229,255); int hz=264;
  for(int i=0;i<9;i++){int yy=hz+(int)(i*i*3.6f); if(yy<gH) crkG->fillRect(0,yy,gW,1,grc);}
  for(int x=-8;x<=16;x++){int p2=gW/2+x*90; int x0=gW/2+(int)((p2-gW/2)*0.18f); crk_line(x0,hz,p2,gH,grc);}
  crk_txtSh(gW/2,60,"OMEGAWARE",5,CRK_RGB(49,232,255));
  crk_txtC(gW/2,112,"RETRO FUTURE",2,CRK_RGB(255,122,176));
  crk_scroller(t,CRK_RGB(255,79,160),13,false);
}

// Boot cracktro runner. style 1..6 forces a style, 0 = random each boot.
// Renders each frame into an off-screen PSRAM sprite, then blits it whole — the
// per-pixel work happens off-panel so the RGB display never shows a half-drawn frame (no flicker).
static void drawCracktro(int style){
  int s=(style>=1&&style<=6)?(style-1):(int)(esp_random()%6);
  initStars();
  unsigned long startMs=millis();
  UG->fillScreen(TFT_BLACK); tft.display();
  // K: no sprite needed — every frame renders into the compose buffer, then
  // display() page-flips it at VSYNC. Atomic by construction.
  while(true){
    if(Touch_ReadFrame()){unsigned long t0=millis();while(Touch_ReadFrame()&&millis()-t0<500)delay(10);break;}
    if(!g_loop_cracktro && millis()-startMs>=6000) break;
    float t=(float)(millis()-startMs);
    switch(s){case 0:crkCopper(t);break;case 1:crkStar(t);break;case 2:crkRaster(t);break;
      case 3:crkPlasma(t);break;case 4:crkBoing(t);break;default:crkSynth(t);break;}
    if(((int)(t/450.0f))%2) crk_txtC(gW/2,gH-CRK_SB-26,"TAP TO CONTINUE",2,CRK_RGB(150,168,200));
    tft.display();
    delay(2);
  }
  UG->fillScreen(TFT_BLACK); tft.display();
}

// ============================================================================
// ── Layout geometry — now runtime variables (not #defines) so relayout() can
//    reflow the whole UI per rotation. Same names, so existing draw code is unchanged. ──
static int LCD_WIDTH=800, LCD_HEIGHT=480;
static int STATUS_H=24, MODE_BAR_H=24, NOW_PLAY_H=32, BOTTOM_H=52;
static int COVER_W=200, COVER_H=404, COVER_ART_X=8, COVER_ART_Y=30, COVER_ART_W=184, COVER_ART_H=150;
static int AZ_W=20, AZ_X=780;
static int LIST_X=200, LIST_W=580, LIST_TOP=48, LIST_BOTTOM=396, LIST_ITEM_H=43, ITEMS_VIS=8;
static bool g_portrait_mode=false;   // true when g_rot is 1 or 3 (90/270)

static void relayout(){
  LCD_WIDTH=gW; LCD_HEIGHT=gH;
  g_portrait_mode = (g_rot==1||g_rot==3);
  STATUS_H=24; MODE_BAR_H=24; NOW_PLAY_H=32; BOTTOM_H=52; AZ_W=20;
  AZ_X=LCD_WIDTH-AZ_W;
  if(!g_portrait_mode){
    // ── Landscape (0/2): original layout, byte-identical to the old #defines ──
    COVER_W=200; COVER_H=LCD_HEIGHT-STATUS_H-BOTTOM_H;
    COVER_ART_X=8; COVER_ART_Y=STATUS_H+6; COVER_ART_W=184; COVER_ART_H=150;
    LIST_X=COVER_W; LIST_W=LCD_WIDTH-COVER_W-AZ_W;
    LIST_TOP=STATUS_H+MODE_BAR_H;
    LIST_BOTTOM=LCD_HEIGHT-BOTTOM_H-NOW_PLAY_H;
    ITEMS_VIS=8; LIST_ITEM_H=(LIST_BOTTOM-LIST_TOP)/ITEMS_VIS;
  } else {
    // ── Portrait (1/3): cover full-width on top, list full-width below ──
    COVER_W=LCD_WIDTH; COVER_H=196;
    COVER_ART_X=8; COVER_ART_Y=STATUS_H+MODE_BAR_H+8; COVER_ART_W=150; COVER_ART_H=150;
    LIST_X=0; LIST_W=LCD_WIDTH-AZ_W;
    LIST_TOP=STATUS_H+MODE_BAR_H+COVER_H;
    LIST_BOTTOM=LCD_HEIGHT-BOTTOM_H-NOW_PLAY_H;
    int rowH=58; ITEMS_VIS=(LIST_BOTTOM-LIST_TOP)/rowH; if(ITEMS_VIS<1)ITEMS_VIS=1;
    LIST_ITEM_H=(LIST_BOTTOM-LIST_TOP)/ITEMS_VIS;
  }
}

// (uiSpriteSetup removed in K — the esp_lcd double framebuffer IS the double buffer.)

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

// Write ROTATE=<deg> to CONFIG.TXT (0/90/180/270), then caller reboots to apply.
static void saveRotate(int deg){
  String lines=""; bool written=false;
  File fr=SD_MMC.open("/CONFIG.TXT",FILE_READ);
  if(fr){ while(fr.available()){ String line=fr.readStringUntil('\n'); line.trim();
      if(line.startsWith("ROTATE=")){ lines+="ROTATE="+String(deg)+"\n"; written=true; }
      else lines+=line+"\n"; }
    fr.close(); }
  if(!written) lines+="ROTATE="+String(deg)+"\n";
  File fw=SD_MMC.open("/CONFIG.TXT",FILE_WRITE);
  if(fw){ fw.print(lines); fw.close(); }
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
        "# Boot cracktro style: 0=random each boot, 1=COPPER 2=STARFIELD\n"
        "# 3=RAINBOW 4=PLASMA 5=BOING 6=SYNTHWAVE\n"
        "CRACKTRO=0\n"
        "# Screen rotation (reboot to apply): 0=landscape, 180=flipped (mount upside-down). 90/270 portrait coming soon.\n"
        "ROTATE=0\n"
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
        "NFO FORMAT (GameName.nfo, plain text):\r\n"
        "  Line 1:  game title (shown INSTEAD of the file name)\r\n"
        "  Line 2+: short info - year, publisher, description\r\n"
        "  Or use labels (any case):\r\n"
        "    Title: Turrican II\r\n"
        "    Blurb: 1991 - Rainbow Arts - legendary run and gun\r\n"
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
        "NFO FORMAT (GameName.nfo, plain text):\r\n"
        "  Line 1:  game title (shown INSTEAD of the file name)\r\n"
        "  Line 2+: short info - year, publisher, description\r\n"
        "  Or use labels (any case):\r\n"
        "    Title: Head Over Heels\r\n"
        "    Blurb: 1987 - Ocean - isometric puzzle classic\r\n"
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

// After a firmware update adds a new key, an existing CONFIG.TXT won't have it.
// Append CRACKTRO= (with legend) if missing, so upgraders get an editable line
// without their other settings being touched. Only writes when it's actually absent.
static void selfHealConfig(){
  File fr = SD_MMC.open("/CONFIG.TXT", FILE_READ);
  if(!fr) return;
  bool hasCracktro=false; String all="";
  while(fr.available()){ String line=fr.readStringUntil('\n'); all+=line+"\n";
    String t=line; t.trim(); if(t.startsWith("CRACKTRO=")) hasCracktro=true; }
  fr.close();
  if(hasCracktro) return;
  File fw = SD_MMC.open("/CONFIG.TXT", FILE_WRITE);
  if(fw){ fw.print(all);
    fw.print("# Boot cracktro style: 0=random each boot, 1=COPPER 2=STARFIELD\n"
             "# 3=RAINBOW 4=PLASMA 5=BOING 6=SYNTHWAVE\n"
             "CRACKTRO=0\n");
    fw.close(); }
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
    else if(key == "CRACKTRO"){ int c=val.toInt(); if(c>=0&&c<=6) g_cracktro=c; }
    else if(key == "ROTATE"){ int d=((val.toInt()/90)%4+4)%4; if(d==1||d==3) d=(d==1)?0:2; g_rot=d; }
    // LANDSCAPE-LOCKED: this RGB panel has no HW portrait; 90/270 is a slow SW transpose (~1-2fps),
    // so portrait is snapped to the nearest landscape (90->0, 270->180). Only 0/180 are allowed.
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
  String jpg_path;        // cover art path (may be empty; "?" = checked, none found)
  std::vector<int> disk_indices; // all disk indices in order
  bool nfo_checked=false; // lazy: have we looked for an NFO title yet?
  String blurb;           // runtime cache of NFO blurb (not persisted to .gamecache)
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

// ── Smooth list scroll + drag/inertia state (ported from the JC3248 engine) ──
static float g_scrollPx=0;                 // pixel scroll offset (source of truth)
static bool  g_touch_active=false,g_touch_moved=false,g_touch_inlist=false,g_inertia_on=false;
static int   g_touch_x0=0,g_touch_y0=0,g_touch_lastY=0,g_touch_release=0;
static float g_touch_px0=0,g_touch_vel=0,g_inertia_vel=0;
static uint32_t g_touch_lastMs=0;
#define DRAG_THRESH 14          // px of finger travel before a press becomes a scroll
#define RELEASE_FRAMES 5        // consecutive no-touch frames = real lift (~80ms; 7" GT911 blips more than the JC's)
static int maxScrollPx(){ int m=(int)g_games.size()*LIST_ITEM_H-(LIST_BOTTOM-LIST_TOP); return m>0?m:0; }

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
    String Ll=L; Ll.toLowerCase();   // labels are case-insensitive (Title:/TITLE:/title:)
    if(!gotT&&Ll.startsWith("title:")){ outTitle=L.substring(6); outTitle.trim(); gotT=true; continue; }
    if(!gotB&&(Ll.startsWith("blurb:")||Ll.startsWith("description:"))){
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
// ── Game cache — caches buildGameList output so NFO/JPG lookups only happen once ──
static String gameCachePath(){return g_mode==MODE_ADF?"/ADF/.gamecache":"/DSK/.gamecache";}

static void writeGameCache(){
  File f=SD_MMC.open(gameCachePath().c_str(),FILE_WRITE);if(!f)return;
  f.println("#FILES="+String(g_files.size()));
  for(auto&g:g_games){
    f.print(g.name);f.print("|");f.print(g.first_file_idx);f.print("|");
    f.print(g.disk_count);f.print("|");f.print(g.jpg_path);f.print("|");
    for(int i=0;i<(int)g.disk_indices.size();i++){if(i>0)f.print(",");f.print(g.disk_indices[i]);}
    f.println();
  }
  f.close();
}

static bool readGameCache(){
  g_games.clear();
  File f=SD_MMC.open(gameCachePath().c_str(),FILE_READ);
  if(!f){return false;}
  long declaredFiles=-1;
  while(f.available()){
    String line=f.readStringUntil('\n');line.trim();if(!line.length())continue;
    if(line.startsWith("#FILES=")){declaredFiles=line.substring(7).toInt();continue;}
    int p1=line.indexOf('|');if(p1<0)continue;
    int p2=line.indexOf('|',p1+1);if(p2<0)continue;
    int p3=line.indexOf('|',p2+1);if(p3<0)continue;
    int p4=line.indexOf('|',p3+1);if(p4<0)continue;
    GameEntry e;
    e.name=line.substring(0,p1);
    e.first_file_idx=line.substring(p1+1,p2).toInt();
    e.disk_count=line.substring(p2+1,p3).toInt();
    e.jpg_path=line.substring(p3+1,p4);
    if(e.jpg_path=="?")e.jpg_path="";
    String indices=line.substring(p4+1);
    if(indices.length()){int pos=0;while(pos<(int)indices.length()){int comma=indices.indexOf(',',pos);if(comma<0)comma=indices.length();e.disk_indices.push_back(indices.substring(pos,comma).toInt());pos=comma+1;}}
    if(e.first_file_idx>=0&&e.first_file_idx<(int)g_files.size()) g_games.push_back(e);
  }
  f.close();
  if(declaredFiles>=0&&declaredFiles!=(long)g_files.size()){g_games.clear();return false;}
  return!g_games.empty();
}

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
      entry.name=baseName;
      for(int j=i+1;j<(int)g_files.size();j++){
        if(used[j]) continue;
        if(parentDir(g_files[j])==dir && getGameBaseName(g_files[j])==baseName && getDiskNumber(g_files[j])>0){
          used[j]=true; entry.disk_count++; entry.disk_indices.push_back(j);
        }
      }
      std::sort(entry.disk_indices.begin(),entry.disk_indices.end(),[](int a,int b){return getDiskNumber(g_files[a])<getDiskNumber(g_files[b]);});
      entry.first_file_idx=entry.disk_indices[0];
    } else {
      entry.name=basenameNoExt(filenameOnly(g_files[i]));
    }
    used[i]=true;
    // NO NFO/JPG lookups here — done lazily in drawCoverPanel (huge boot speedup)
    g_games.push_back(entry);
  }
  std::sort(g_games.begin(),g_games.end(),[](const GameEntry&a,const GameEntry&b){String al=a.name,bl=b.name;al.toLowerCase();bl.toLowerCase();return al<bl;});
  writeGameCache();
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
static void drawAZBar(){ UiFrame _uf;
  if(active_letter_count==0) return;
  int barH=LIST_BOTTOM-LIST_TOP;
  UG->fillRect(AZ_X,LIST_TOP,AZ_W,barH+NOW_PLAY_H,COL_PANEL);
  char curLetter='A';
  if(g_scroll>=0&&g_scroll<(int)g_games.size())
    curLetter=toupper(g_games[g_scroll].name.charAt(0));
  int letterH=barH/active_letter_count; if(letterH<8) letterH=8;
  UG->setFont(&lgfx::fonts::Font0);
  UG->setTextSize(1);
  for(int i=0;i<active_letter_count;i++){
    char letter=active_letters[i];
    int ly=LIST_TOP+i*letterH;
    if(ly+letterH>LIST_BOTTOM) break;
    if(letter==curLetter){
      UG->fillRect(AZ_X,ly,AZ_W,letterH,COL_AMBER);
      UG->setTextColor(TFT_BLACK,COL_AMBER);
    } else {
      UG->setTextColor(COL_DIM,COL_PANEL);
    }
    UG->setCursor(AZ_X+(AZ_W-6)/2,ly+(letterH-8)/2);
    UG->print(String(letter));
  }
  int maxOff=(int)g_games.size()-ITEMS_VIS;
  if(maxOff>0){
    int thumbH=max(6,barH*ITEMS_VIS/(int)g_games.size());
    int thumbY=LIST_TOP+(barH-thumbH)*g_scroll/maxOff;
    UG->fillRect(AZ_X-3,thumbY,2,thumbH,COL_BLUE);
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
  g_scrollPx=(float)(g_scroll*LIST_ITEM_H); g_inertia_on=false;   // keep pixel scroll in sync
  return true;
}

// ============================================================================
// STATUS BAR
// ============================================================================
// ============================================================================
// ESP-NOW manual pair — broadcasts hellos for 15s from main thread
// Must be defined after COVER_W, COL_* are available
// ============================================================================
// RESCAN — delete cache files and rebuild the game list from a fresh SD scan.
// Use after adding/removing games on the card so the cached list updates.
static void doRescan(){
  g_info_showing=false;
  SD_MMC.remove("/ADF/.index");   SD_MMC.remove("/DSK/.index");
  SD_MMC.remove("/ADF/.gamecache");SD_MMC.remove("/DSK/.gamecache");
  g_files.clear(); g_games.clear();
  listImages(SD_MMC,g_files);
  buildGameList();           // no cache now → full rebuild, writes fresh cache
  buildActiveLetters();
  g_sel=0; g_scroll=0; g_disk_sel=0; g_scrollPx=0; g_inertia_on=false;
  drawFullUI();
}

static void doPairNow() {
  UG->fillRoundRect(8, g_info_pair_btn_y, COVER_W-16, 28, 6, COL_AMBER);
  UG->setFont(&lgfx::fonts::DejaVu12);
  UG->setTextColor(TFT_BLACK, COL_AMBER);
  UG->setCursor(18, g_info_pair_btn_y+8);
  UG->print("PAIRING...");

  uint32_t t0 = millis();
  int count = 0;
  while (!g_espnow_paired && millis() - t0 < 15000) {
    espnowBroadcastHello();
    delay(300);
    count++;
    UG->setFont(&lgfx::fonts::DejaVu9);
    UG->setTextColor(COL_LIT, COL_PANEL);
    UG->setCursor(8, g_info_pair_btn_y - 14);
    UG->print("tx: " + String(count) + "   ");
  }

  uint16_t btnCol = g_espnow_paired ? COL_GREEN : 0xE8C4;
  UG->fillRoundRect(8, g_info_pair_btn_y, COVER_W-16, 28, 6, btnCol);
  UG->setFont(&lgfx::fonts::DejaVu12);
  UG->setTextColor(TFT_BLACK, btnCol);
  UG->setCursor(18, g_info_pair_btn_y+8);
  UG->print(g_espnow_paired ? "PAIRED OK!" : "PAIR FAILED");
  delay(1500);
  drawFullUI();
  g_info_showing = false;
}

static void drawStatusBar(){ UiFrame _uf;
  UG->fillRect(0,0,LCD_WIDTH,STATUS_H,COL_BAR);
  UG->setFont(&lgfx::fonts::DejaVu9);
  UG->setTextColor(COL_ORANGE,COL_BAR);
  UG->setCursor(10,8); UG->print("OMEGAWARE");
  UG->setTextColor(COL_MID,COL_BAR);
  UG->print("  " FW_VERSION);

  // Mode indicator (centre)
  if (g_wireless_mode) {
    if(espnowIsPaired()){
      UG->setTextColor(0x07E0, COL_BAR);
      String label = "WIRELESS:PAIRED";
      int tw = UG->textWidth(label);
      UG->setCursor((LCD_WIDTH/2)-(tw/2), 8);
      UG->print(label);
    } else {
      UG->setTextColor(0xFD20, COL_BAR);
      String label = "WIRELESS:PAIRING";
      int tw = UG->textWidth(label);
      UG->setCursor((LCD_WIDTH/2)-(tw/2), 8);
      UG->print(label);
    }
  } else {
    UG->setTextColor(0x07FF, COL_BAR);
    String label = "STANDALONE";
    int tw = UG->textWidth(label);
    UG->setCursor((LCD_WIDTH/2)-(tw/2), 8);
    UG->print(label);
  }

  // Right side: battery widget + LOADED/READY text
  int batX = LCD_WIDTH - 80, batY = (STATUS_H - 12) / 2;
  drawBatteryWidget(batX, batY);

  // LOADED / READY indicator dot
  uint16_t indCol = g_loaded ? 0xE8C4 : COL_GREEN;
  UG->fillCircle(LCD_WIDTH-8, STATUS_H/2, 4, indCol);
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
  UG->fillRect(bx, by, bw, bh, bodyCol);
  // Top-right write-protect notch (corner cutout ~1/5 width, 1/6 height)
  int nw = bw*4/18, nh = bh*3/18;
  UG->fillRect(bx+bw-nw, by, nw, nh, shutterCol);
  // Label area: top 40% of body, inset 1px each side except right edge
  int lx = bx+2, ly = by+2, lw = bw-nw-3, lh = bh*9/22;
  UG->fillRect(lx, ly, lw, lh, labelCol);
  // Three clean label lines
  uint16_t llineCol = (labelCol > 0x4000)
    ? (uint16_t)((labelCol & 0xF7DE) >> 1)   // darken
    : (uint16_t)(labelCol | 0x2104);           // lighten
  int lineW = lw - 6;
  for (int i = 0; i < 3; i++) {
    int lineY = ly + 3 + i * (lh/4);
    UG->fillRect(lx+3, lineY, lineW, 2, llineCol);
  }
  // Metal shutter: centred, lower 50% of body
  int mx = bx + bw/4, my = by + bh*11/20;
  int mw = bw/2,      mh = bh*8/20;
  UG->fillRect(mx, my, mw, mh, shutterCol);
  // Shutter border
  UG->drawRect(mx, my, mw, mh, (uint16_t)(shutterCol >> 1 | 0x0821));
  // Hub hole: small centred rectangle inside shutter
  int hx = mx + mw/2 - mw/6, hy = my + mh/2 - mh/4;
  int hw = mw/3,              hh = mh/2;
  UG->fillRect(hx, hy, hw, hh, bodyCol);
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
    UG->fillRect(x, iy, w, ih, col);
  }
}


// Read JPEG dimensions from file header (SOF0 marker FFC0)
static JPEGDEC jpegdec;
static uint16_t *jpeg_tmp_buf=NULL;
static int jpeg_tmp_w=0,jpeg_tmp_h=0;
static int jpeg_buf_cb(JPEGDRAW*pDraw){
  if(!jpeg_tmp_buf)return 0;
  for(int yy=0;yy<pDraw->iHeight;yy++){
    int row=pDraw->y+yy; if(row<0||row>=jpeg_tmp_h)continue;
    int cw=pDraw->iWidth; if(pDraw->x+cw>jpeg_tmp_w)cw=jpeg_tmp_w-pDraw->x;
    if(cw>0) memcpy(&jpeg_tmp_buf[row*jpeg_tmp_w+pDraw->x],&pDraw->pPixels[yy*pDraw->iWidth],cw*2);
  } return 1;
}
// Decode JPEG from SD via JPEGDEC, scale to fit maxW x maxH, blit with LovyanGFX pushImage.
// Replaces UG->drawJpgFile(SD_MMC,...) which fails to compile on newer LovyanGFX+core (SDMMCFS abstract).
static void drawJpegFit(const String&path,int boxX,int boxY,int maxW,int maxH){
  File f=SD_MMC.open(path.c_str(),FILE_READ); if(!f) return;
  size_t sz=f.size();
  // SD_MMC f.size() can be 0 for subdir files — fall back to VFS stat
  if(sz==0){ f.close(); struct stat st; String vp="/sdcard"+path; if(stat(vp.c_str(),&st)==0) sz=st.st_size; f=SD_MMC.open(path.c_str(),FILE_READ); if(!f) return; }
  if(sz==0||sz>500000){ f.close(); return; }
  uint8_t*buf=(uint8_t*)ps_malloc(sz); if(!buf){ buf=(uint8_t*)malloc(sz); } if(!buf){ f.close(); return; }
  f.read(buf,sz); f.close();
  if(!jpegdec.openRAM(buf,sz,jpeg_buf_cb)){ free(buf); return; }
  jpegdec.setPixelType(RGB565_LITTLE_ENDIAN);  // K: esp_lcd framebuffer is native little-endian RGB565 (big-endian was a LovyanGFX pushImage quirk)
  int jw=jpegdec.getWidth(),jh=jpegdec.getHeight();
  if(jw<=0||jh<=0||jw>2000||jh>2000){ jpegdec.close(); free(buf); return; }
  jpeg_tmp_buf=(uint16_t*)ps_malloc((size_t)jw*jh*2);
  if(!jpeg_tmp_buf){ jpegdec.close(); free(buf); return; }
  memset(jpeg_tmp_buf,0,(size_t)jw*jh*2); jpeg_tmp_w=jw; jpeg_tmp_h=jh;
  jpegdec.decode(0,0,0); jpegdec.close(); free(buf);
  float scX=(float)maxW/jw,scY=(float)maxH/jh,sc=min(scX,scY);
  if(sc>1.0f)sc=1.0f;
  int dw=(int)(jw*sc),dh=(int)(jh*sc);
  if(dw<=0||dh<=0){ free(jpeg_tmp_buf); jpeg_tmp_buf=NULL; return; }
  int ox=boxX+(maxW-dw)/2,oy=boxY+(maxH-dh)/2;
  if(sc>=0.999f){
    // 1:1 — push the decoded buffer directly
    UG->pushImage(ox,oy,jw,jh,jpeg_tmp_buf);
  } else {
    // nearest-neighbour downscale into a row buffer, push row by row
    uint16_t*rowbuf=(uint16_t*)malloc(dw*2);
    if(rowbuf){
      for(int r=0;r<dh;r++){ int srcY=(int)(r/sc); if(srcY>=jh)srcY=jh-1;
        for(int c=0;c<dw;c++){ int srcX=(int)(c/sc); if(srcX>=jw)srcX=jw-1;
          rowbuf[c]=jpeg_tmp_buf[srcY*jw+srcX]; }
        UG->pushImage(ox,oy+r,dw,1,rowbuf);
        if(r%20==0)yield();
      }
      free(rowbuf);
    }
  }
  free(jpeg_tmp_buf); jpeg_tmp_buf=NULL;
}


static void drawCoverPanel(){ UiFrame _uf;
  // Flat fill cover panel background (portrait = full-width top block; landscape = left column)
  if(g_portrait_mode) UG->fillRect(0, STATUS_H+MODE_BAR_H, LCD_WIDTH, COVER_H, COL_PANEL);
  else                UG->fillRect(0, STATUS_H, COVER_W, LCD_HEIGHT-STATUS_H-BOTTOM_H, COL_PANEL);

  if(g_games.empty()) return;
  GameEntry& game=g_games[g_sel];
  const int maxW=COVER_W-12;

  // Lazy resolve cover art on first view of this game (moved out of buildGameList for fast boot)
  if(!game.jpg_path.length()){
    String jpg;
    if(findJPGFor(g_files[game.first_file_idx],jpg)) game.jpg_path=jpg;
    else {
      String tryBase=parentDir(g_files[game.first_file_idx])+"/"+getGameBaseName(g_files[game.first_file_idx]);
      bool found=false;
      for(const char* ext:{".jpg",".jpeg",".png"}){
        String tryPath=tryBase+ext;
        if(SD_MMC.exists(tryPath.c_str())){ game.jpg_path=tryPath; found=true; break; }
      }
      if(!found) game.jpg_path="?";   // mark checked so we don't search SD again
    }
  }
  // Lazy resolve NFO title+blurb on first view (read the NFO file ONCE, cache both)
  if(!game.nfo_checked){
    game.nfo_checked=true;
    String nfoPath,nfoTitle,nfoBlurb;
    if(findNFOFor(g_files[game.first_file_idx],nfoPath)){
      String txt=readSmallTextFile(nfoPath,512);
      parseNFO(txt,nfoTitle,nfoBlurb);
      if(nfoTitle.length()) game.name=nfoTitle;
      game.blurb=nfoBlurb;
    }
  }
  bool hasJpg = (game.jpg_path.length()>0 && game.jpg_path!="?");

  // Cover art box — rounded with subtle glow border
  int artX=COVER_ART_X, artY=COVER_ART_Y, artW=COVER_ART_W, artH=COVER_ART_H;
  // Outer glow
  UG->drawRoundRect(artX-1, artY-1, artW+2, artH+2, 8, COL_ACCENT);
  UG->fillRoundRect(artX, artY, artW, artH, 7, COL_BAR);

  if(hasJpg){
    int boxW=artW-4, boxH=artH-4;
    drawJpegFit(game.jpg_path, artX+2, artY+2, boxW, boxH);
  } else {
    // No art — draw floppy icon as placeholder
    int is = min(artW, artH) * 3 / 4;
    int ix = artX + (artW-is)/2, iy = artY + (artH-is)/2;
    drawFloppyIcon(ix, iy, is, COL_ACCENT, COL_MID, COL_BAR);
    UG->setFont(&lgfx::fonts::DejaVu9);
    UG->setTextColor(COL_DIM, COL_BAR);
    char initial=toupper(game.name.charAt(0));
    // Draw initial over label area of floppy
    UG->setFont(&lgfx::fonts::DejaVu18);
    UG->setTextColor(COL_LIT, COL_MID);
    char ibuf[2]={initial,0};
    int tw=UG->textWidth(ibuf);
    UG->setCursor(artX+(artW-tw)/2, artY+artH/4);
    UG->print(ibuf);
  }

  // ── PORTRAIT: title/blurb to the RIGHT of the art, INSERT full-width at block bottom ──
  if(g_portrait_mode){
    int rx=artX+artW+12, rw=LCD_WIDTH-rx-12, ry=artY+4;
    // Title (up to 2 lines, big)
    UG->setFont(&lgfx::fonts::DejaVu18); UG->setTextColor(COL_LIT,COL_PANEL);
    { String t=game.name, line="", word=""; int nl=0;
      for(int i=0;i<=(int)t.length()&&nl<2;i++){ char c=(i<(int)t.length())?t[i]:' ';
        if(c==' '||i==(int)t.length()){ String cand=line.length()?line+" "+word:word;
          if(UG->textWidth(cand)>rw&&line.length()){ UG->setCursor(rx,ry); UG->print(line); ry+=22; nl++; line=word; }
          else line=cand; word=""; } else word+=c; }
      if(line.length()&&nl<2){ UG->setCursor(rx,ry); UG->print(line); ry+=22; } }
    ry+=4;
    // Blurb (up to 5 lines)
    if(game.blurb.length()){
      UG->setFont(&lgfx::fonts::DejaVu9); UG->setTextColor(COL_MID,COL_PANEL);
      String b=game.blurb, line="", word=""; int nl=0;
      for(int i=0;i<=(int)b.length()&&nl<5;i++){ char c=(i<(int)b.length())?b[i]:' ';
        if(c==' '||c=='\n'||i==(int)b.length()){ String cand=line.length()?line+" "+word:word;
          if(UG->textWidth(cand)>rw&&line.length()){ UG->setCursor(rx,ry); UG->print(line); ry+=12; nl++; line=word; }
          else line=cand; word=""; } else word+=c; }
      if(line.length()&&nl<5){ UG->setCursor(rx,ry); UG->print(line); } }
    // INSERT / EJECT — full-width at the bottom of the cover block
    int coverBottom=STATUS_H+MODE_BAR_H+COVER_H, pbtnY=coverBottom-42, pbh=34, pbx=8, pbw=LCD_WIDTH-16;
    bool isLd=(g_loaded&&g_loaded_game_idx==g_sel);
    uint16_t bf=isLd?(uint16_t)0x4000:(uint16_t)0x0340, bb=isLd?(uint16_t)0xE8C4:COL_GREEN;
    UG->fillRoundRect(pbx,pbtnY,pbw,pbh,10,bf); UG->drawRoundRect(pbx,pbtnY,pbw,pbh,10,bb);
    UG->setFont(&lgfx::fonts::DejaVu12); UG->setTextColor(TFT_WHITE,bf);
    const char* pl=isLd?"EJECT":"INSERT"; int ptw=UG->textWidth(pl);
    UG->setCursor(pbx+(pbw-ptw)/2, pbtnY+(pbh-13)/2); UG->print(pl);
    // Multi-disk selector — one centred row just above INSERT
    if(game.disk_count>1){
      int nd=game.disk_count, bw2=28, bh2=18, gap=4;
      int per=max(1,(pbw)/(bw2+gap)); int show=min(nd,per);
      int rowW=show*(bw2+gap)-gap, sx=(LCD_WIDTH-rowW)/2, dy=pbtnY-bh2-6;
      UG->setFont(&lgfx::fonts::DejaVu9);
      for(int d=0; d<show; d++){
        int bx=sx+d*(bw2+gap);
        bool sel=(d==g_disk_sel), ld=(g_loaded_game_idx==g_sel&&g_loaded_disk_idx==d);
        uint16_t bc=ld?COL_GREEN:(sel?COL_AMBER:COL_BAR);
        UG->fillRoundRect(bx,dy,bw2,bh2,4,bc); UG->drawRoundRect(bx,dy,bw2,bh2,4,sel?COL_AMBER:COL_DIM);
        UG->setTextColor(ld||sel?TFT_BLACK:COL_LIT,bc);
        String dl=String(d+1); UG->setCursor(bx+(bw2-UG->textWidth(dl))/2,dy+(bh2-9)/2); UG->print(dl);
      }
    }
    return;
  }

  // Flow content downward
  int ty=artY+artH+6;

  // Title
  UG->setFont(&lgfx::fonts::DejaVu12);
  UG->setTextColor(COL_LIT, COL_PANEL);
  UG->setTextSize(1);
  String title=game.name;
  if(UG->textWidth(title)<=maxW){
    UG->setCursor(6,ty); UG->print(title); ty+=16;
  } else {
    int breakAt=0;
    for(int i=1;i<=(int)title.length();i++){
      if(i==(int)title.length()||title[i]==' '){
        if(UG->textWidth(title.substring(0,i))<=maxW) breakAt=i;
        else break;
      }
    }
    if(breakAt>0){
      UG->setCursor(6,ty); UG->print(title.substring(0,breakAt)); ty+=16;
      String rest=title.substring(breakAt+1);
      while(UG->textWidth(rest)>maxW&&rest.length()>3) rest=rest.substring(0,rest.length()-1);
      UG->setCursor(6,ty); UG->print(rest); ty+=16;
    } else {
      while(UG->textWidth(title)>maxW&&title.length()>3) title=title.substring(0,title.length()-1);
      UG->setCursor(6,ty); UG->print(title); ty+=16;
    }
  }
  ty+=2;

  // NFO blurb (use cached value from the single NFO read above — no re-read per draw)
  String nfoBlurb=game.blurb;
  if(nfoBlurb.length()>0){
    UG->setFont(&lgfx::fonts::DejaVu9);
    UG->setTextColor(COL_LIT, COL_PANEL);
    String line="",word=""; int lines=0;
    for(int i=0;i<=(int)nfoBlurb.length()&&lines<4;i++){
      char c=(i<(int)nfoBlurb.length())?nfoBlurb[i]:' ';
      if(c==' '||c=='\n'||i==(int)nfoBlurb.length()){
        String cand=line.length()?line+" "+word:word;
        if(UG->textWidth(cand)>maxW&&line.length()){
          UG->setCursor(6,ty); UG->print(line); ty+=11; lines++; line=word;
        } else line=cand;
        word="";
      } else word+=c;
    }
    if(line.length()&&lines<4){ UG->setCursor(6,ty); UG->print(line); ty+=11; }
    ty+=3;
  }

  // Disk selector — wraps to as many rows as needed (7 per row). Tested layout
  // fits 3 rows (21 disks) above INSERT without colliding with the blurb — covers
  // every floppy-playable Amiga game (Beneath a Steel Sky=15, Biiing!=19, etc).
  int btnY=LCD_HEIGHT-BOTTOM_H-44;
  if(game.disk_count>1){
    const int PER_ROW=7;
    int btnW=23,btnH=18,gap=3;
    int nd=game.disk_count;
    int rows=(nd+PER_ROW-1)/PER_ROW;
    int rowH=btnH+gap;
    // Reserve vertical space above INSERT for label + all rows
    int diskY=btnY - 12 - rows*rowH - 4;
    UG->setFont(&lgfx::fonts::DejaVu9);
    UG->setTextColor(COL_DIM, COL_PANEL);
    UG->setCursor(6,diskY); UG->print("DISK:"); diskY+=12;
    for(int d=0;d<nd;d++){
      int col=d%PER_ROW, row=d/PER_ROW;
      int inRow=min(PER_ROW, nd-row*PER_ROW);          // buttons in THIS row (last row may be short)
      int rowW=inRow*(btnW+gap)-gap;
      int startX=max(4,(COVER_W-rowW)/2);
      int bx=startX+col*(btnW+gap);
      int by=diskY+row*rowH;
      bool isSel=(d==g_disk_sel);
      bool isLoaded=(g_loaded_game_idx==g_sel&&g_loaded_disk_idx==d);
      uint16_t bc=isLoaded?COL_GREEN:(isSel?COL_AMBER:COL_BAR);
      UG->fillRoundRect(bx,by,btnW,btnH,4,bc);
      UG->drawRoundRect(bx,by,btnW,btnH,4,isSel?COL_AMBER:COL_DIM);
      UG->setTextColor(isLoaded||isSel?TFT_BLACK:COL_LIT,bc);
      String dl=String(d+1);
      UG->setCursor(bx+(btnW-UG->textWidth(dl))/2,by+(btnH-9)/2);
      UG->print(dl);
    }
  }

  // INSERT / EJECT button with floppy icon
  bool isLoaded=(g_loaded&&g_loaded_game_idx==g_sel);
  uint16_t btnFill = isLoaded ? (uint16_t)0x4000 : (uint16_t)0x0340;
  uint16_t btnBord = isLoaded ? (uint16_t)0xE8C4 : COL_GREEN;
  int bh = 36;
  UG->fillRoundRect(6, btnY, COVER_W-12, bh, 10, btnFill);
  UG->drawRoundRect(6, btnY, COVER_W-12, bh, 10, btnBord);

  // Floppy icon — small, left side of button
  int iconSize = 22;
  int iconX = 12, iconY = btnY + (bh-iconSize)/2;
  drawFloppyIcon(iconX, iconY, iconSize,
    isLoaded ? (uint16_t)0x8000 : COL_GREEN,
    isLoaded ? (uint16_t)0x6000 : COL_ACCENT,
    isLoaded ? (uint16_t)0x4000 : COL_BAR);

  // Label
  UG->setFont(&lgfx::fonts::DejaVu12);
  UG->setTextColor(TFT_WHITE, btnFill);
  const char* btnLabel = isLoaded ? "EJECT" : "INSERT";
  int tw2 = UG->textWidth(btnLabel);
  // Centre label in remaining space after icon
  int labelX = iconX + iconSize + 4 + ((COVER_W-12 - (iconX-6+iconSize+4) - tw2)/2);
  UG->setCursor(labelX, btnY + (bh-13)/2);
  UG->print(btnLabel);
}

// ============================================================================
static void drawModeBar(){ UiFrame _uf;
  UG->fillRect(LIST_X,STATUS_H,LIST_W+AZ_W,MODE_BAR_H,COL_BAR);
  UG->setFont(&lgfx::fonts::DejaVu9);
  bool isADF=(g_mode==MODE_ADF);
  int by=STATUS_H+3, bh=18;
  // ADF pill
  if(isADF){
    UG->fillRoundRect(LIST_X+4,by,44,bh,9,COL_ACCENT);
    UG->drawRoundRect(LIST_X+4,by,44,bh,9,COL_AMBER);
    UG->setTextColor(COL_AMBER,COL_ACCENT);
  } else {
    UG->fillRoundRect(LIST_X+4,by,44,bh,9,COL_BG);
    UG->setTextColor(COL_DIM,COL_BG);
  }
  UG->setCursor(LIST_X+12,by+5); UG->print("ADF");
  // DSK pill
  if(!isADF){
    UG->fillRoundRect(LIST_X+52,by,44,bh,9,COL_ACCENT);
    UG->drawRoundRect(LIST_X+52,by,44,bh,9,COL_AMBER);
    UG->setTextColor(COL_AMBER,COL_ACCENT);
  } else {
    UG->fillRoundRect(LIST_X+52,by,44,bh,9,COL_BG);
    UG->setTextColor(COL_DIM,COL_BG);
  }
  UG->setCursor(LIST_X+60,by+5); UG->print("DSK");
  // Count
  UG->setTextColor(COL_MID,COL_BAR);
  UG->setCursor(LIST_X+102,by+5);
  UG->print(String(g_games.size())+" games");
}

// ============================================================================
// FILE LIST
// ============================================================================
static void drawFileList(){ UiFrame _uf;
  // Flat fill list background — was a 32-band gradient redrawn on every scroll/select (slow on RGB+single-buffer)
  UG->fillRect(LIST_X, LIST_TOP, LIST_W, LIST_BOTTOM-LIST_TOP, COL_BG);

  if(g_games.empty()){
    UG->setFont(&lgfx::fonts::DejaVu12);
    UG->setTextColor(0xE8C4, COL_BG);
    UG->setCursor(LIST_X+10, LIST_TOP+20);
    UG->print(g_mode==MODE_ADF?"No .ADF files":"No .DSK files"); return;
  }
  // Pixel-smooth scroll: g_scrollPx is the source of truth; rows draw at a
  // fractional offset inside a clip window so partial rows slide cleanly.
  { int mp=maxScrollPx();
    if(g_scrollPx<0) g_scrollPx=0;
    if(g_scrollPx>(float)mp) g_scrollPx=(float)mp; }
  int first=(int)(g_scrollPx/LIST_ITEM_H);
  int off=(int)(g_scrollPx-(float)first*LIST_ITEM_H);
  g_scroll=first;   // keep the integer scroll in sync (A-Z thumb, PREV/NEXT)
  UG->setClip(LIST_X, LIST_TOP, LIST_X+LIST_W, LIST_BOTTOM);

  for(int vi=0;vi<=ITEMS_VIS+1;vi++){
    int gi=first+vi; if(gi>=(int)g_games.size()) break;
    const GameEntry& game=g_games[gi];
    bool isSel=(gi==g_sel);
    bool isLoaded=(g_loaded&&g_loaded_game_idx==gi);
    int y=LIST_TOP-off+vi*LIST_ITEM_H;
    if(y>=LIST_BOTTOM) break;
    int cardH = LIST_ITEM_H - 2;
    int cardY = y + 1;

    // Card background
    if(isSel){
      UG->fillRoundRect(LIST_X+2, cardY, LIST_W-4, cardH, 6, COL_SEL);
      UG->drawRoundRect(LIST_X+2, cardY, LIST_W-4, cardH, 6, COL_AMBER);
    } else {
      UG->fillRect(LIST_X, y, LIST_W, LIST_ITEM_H, COL_BG); // clear first
      UG->fillRoundRect(LIST_X+2, cardY, LIST_W-4, cardH, 4, (uint16_t)(COL_PANEL));
    }

    // Left accent bar
    uint16_t accentCol = isLoaded ? COL_GREEN : (isSel ? COL_AMBER : COL_ACCENT);
    UG->fillRoundRect(LIST_X+4, cardY+3, 4, cardH-6, 2, accentCol);

    // Initial circle
    int cx=LIST_X+24, cy=y+LIST_ITEM_H/2;
    uint16_t circCol=isSel?COL_AMBER:(isLoaded?COL_GREEN:COL_CIRC);
    UG->fillCircle(cx, cy, 13, circCol);
    UG->setFont(&lgfx::fonts::DejaVu12);
    UG->setTextColor(isSel||isLoaded?TFT_BLACK:COL_CIRC_TEXT, circCol);
    char initial=toupper(game.name.charAt(0));
    char ibuf[2]={initial,0};
    int iw=UG->textWidth(ibuf);
    UG->setCursor(cx-iw/2, cy-7); UG->print(ibuf);

    // Game name
    int textX=LIST_X+46;
    UG->setFont(&lgfx::fonts::DejaVu12);
    uint16_t textBg = isSel ? COL_SEL : (uint16_t)COL_PANEL;
    UG->setTextColor(isSel?TFT_WHITE:COL_LIT, textBg);
    String name=game.name;
    int maxNameW=LIST_W-60-(game.disk_count>1?44:0);
    while(UG->textWidth(name)>maxNameW&&name.length()>3)
      name=name.substring(0,name.length()-1);
    UG->setCursor(textX, cy-7); UG->print(name);

    // Disk count badge
    if(game.disk_count>1){
      UG->setFont(&lgfx::fonts::DejaVu9);
      uint16_t badgeCol=isLoaded?COL_GREEN:COL_ACCENT;
      int bx=LIST_X+LIST_W-46;
      int bh2=16, bby=cy-8;
      UG->fillRoundRect(bx,bby,42,bh2,5,badgeCol);
      UG->setTextColor(TFT_WHITE,badgeCol);
      String dc=String(game.disk_count)+"DSK";
      UG->setCursor(bx+(42-UG->textWidth(dc))/2,bby+4);
      UG->print(dc);
    }

    // Loaded indicator — small floppy icon
    if(isLoaded){
      drawFloppyIcon(LIST_X+LIST_W-20, cy-8, 16, COL_GREEN, COL_ACCENT, COL_BAR);
    }
  }
  UG->resetClip();
}

// ============================================================================
// NOW PLAYING BAR
// ============================================================================
static void drawNowPlayingBar(){ UiFrame _uf;
  int y=LIST_BOTTOM;
  if(g_loaded&&g_loaded_name.length()>0){
    UG->fillRect(LIST_X,y,LIST_W,NOW_PLAY_H,COL_NOW);
    UG->drawRect(LIST_X,y,LIST_W,NOW_PLAY_H,COL_GREEN);
    UG->fillCircle(LIST_X+10,y+NOW_PLAY_H/2,4,COL_GREEN);
    UG->setFont(&lgfx::fonts::DejaVu9);
    UG->setTextColor(COL_GREEN,COL_NOW);
    UG->setCursor(LIST_X+20,y+5); UG->print("NOW PLAYING");
    UG->setFont(&lgfx::fonts::DejaVu12);
    UG->setTextColor(TFT_WHITE,COL_NOW);
    UG->setCursor(LIST_X+20,y+17);
    String name=g_loaded_name;
    while(UG->textWidth(name)>LIST_W-30&&name.length()>3)
      name=name.substring(0,name.length()-1);
    UG->print(name);
  } else {
    UG->fillRect(LIST_X,y,LIST_W,NOW_PLAY_H,COL_BG);
    UG->setFont(&lgfx::fonts::DejaVu9);
    UG->setTextColor(COL_MID,COL_BG);
    UG->setCursor(LIST_X+10,y+NOW_PLAY_H/2-4);
    UG->print(String((int)g_games.size())+" games  -  tap INSERT to load");
  }
}

// Redraw only the list strip + now-playing + A-Z index during scroll/inertia —
// the cover panel is untouched (no SD/JPEG churn mid-drag). One flip per frame.
static void redrawListArea(){ UiFrame _uf; drawFileList(); drawNowPlayingBar(); drawAZBar(); }

// ============================================================================
// BOTTOM BAR
// ============================================================================
static void drawBottomBar(){ UiFrame _uf;
  int y=LCD_HEIGHT-BOTTOM_H;
  drawGradientBg(0, y, LCD_WIDTH, BOTTOM_H, COL_BAR, COL_PANEL);
  UG->drawFastHLine(0, y, LCD_WIDTH, COL_SEP);
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
    if(i>0) UG->drawFastVLine(bx, y+4, BOTTOM_H-8, COL_SEP);

    // Icon circle — positioned higher to leave room for labels
    int cx = bx + bw/2, cy = y + 13;
    UG->fillCircle(cx, cy, 10, (uint16_t)(btns[i].col >> 2));
    UG->drawCircle(cx, cy, 10, btns[i].col);
    UG->setFont(&lgfx::fonts::DejaVu12);
    UG->setTextColor(btns[i].col, (uint16_t)(btns[i].col >> 2));
    int tw = UG->textWidth(btns[i].icon);
    UG->setCursor(cx-tw/2, cy-7); UG->print(btns[i].icon);

    // Label
    UG->setFont(&lgfx::fonts::DejaVu9);
    UG->setTextColor(COL_DIM, COL_PANEL);
    tw = UG->textWidth(btns[i].label);
    UG->setCursor(bx+(bw-tw)/2, y+26); UG->print(btns[i].label);
  }

  // Theme name
  UG->setFont(&lgfx::fonts::DejaVu9);
  UG->setTextColor((uint16_t)(COL_AMBER>>1), COL_PANEL);
  String tn=THEMES[g_theme_idx].name;
  int tw=UG->textWidth(tn);
  UG->setCursor(2*bw+(bw-tw)/2, y+38); UG->print(tn);
}

// ============================================================================
// FULL REDRAW + PARTIAL REFRESH
// ============================================================================
static void drawFullUI(){ UiFrame _uf;
  UG->setTextSize(1);
  UG->setFont(&lgfx::fonts::DejaVu12);
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
static void drawListAndCover(){ UiFrame _uf;
  UG->setTextSize(1);
  UG->setFont(&lgfx::fonts::DejaVu12);
  // drawCoverPanel() and drawFileList() now flat-fill their own backgrounds,
  // so no pre-fill needed here (was double-painting both areas every redraw).
  drawCoverPanel();
  drawFileList();
  drawNowPlayingBar();
  drawAZBar();
  UG->fillCircle(LCD_WIDTH-14,STATUS_H/2,5,g_loaded?0xE8C4:COL_GREEN);
  UG->setFont(&lgfx::fonts::DejaVu9);
  UG->setTextColor(g_loaded?0xE8C4:COL_GREEN,COL_BAR);
  UG->setCursor(LCD_WIDTH-78,8); UG->print(g_loaded?"LOADED ":"READY  ");
}

// ============================================================================
// LOAD / UNLOAD
// ============================================================================
static bool doLoadSelected(const String& adfPath){
  // Loading overlay in cover panel
  UG->fillRect(0,STATUS_H,COVER_W,LCD_HEIGHT-STATUS_H-BOTTOM_H,COL_PANEL);
  UG->setFont(&lgfx::fonts::DejaVu12);
  UG->setTextColor(TFT_CYAN,COL_PANEL);
  String title=basenameNoExt(filenameOnly(adfPath));
  if(title.length()>13) title=title.substring(0,13);
  UG->setCursor(8,STATUS_H+20); UG->print(title);
  UG->setFont(&lgfx::fonts::DejaVu9);
  UG->setTextColor(COL_LIT,COL_PANEL);
  UG->setCursor(8,STATUS_H+38); UG->print("Loading...");
  uiFlush();   // portrait: show the loading overlay (progress bar below repaints at the end)

  File f=openNamedImage(adfPath);
  if(!f){
    UG->setTextColor(0xE8C4,COL_PANEL); UG->setCursor(8,STATUS_H+56);
    UG->print("Open failed"); delay(1000); drawListAndCover(); return false;
  }
  uint32_t fsz=f.size();
  if(fsz==0){ f.close(); drawListAndCover(); return false; }
  if(fsz>MAX_FILE_BYTES) fsz=MAX_FILE_BYTES;
  build_volume_with_file(getOutputFilename(),fsz);

  int barX=8,barY=STATUS_H+58,barW=COVER_W-16,barH=14;
  UG->drawRoundRect(barX,barY,barW,barH,4,COL_DIM);

  uint32_t copied=0;
  uint8_t* dst=g_disk+DATA_LBA*SECTOR_SIZE;
  const size_t BUFSZ=4096;
  uint8_t* buf=(uint8_t*)malloc(BUFSZ);
  if(!buf){ f.close(); return false; }
  uint32_t remain=fsz;
  int _pcTick=0;
  while(remain){
    size_t n=remain>BUFSZ?BUFSZ:remain;
    int rd=f.read(buf,n); if(rd<=0) break;
    memcpy(dst+copied,buf,rd); remain-=rd; copied+=rd;
    int fill=(int)((barW-4)*((float)copied/fsz));
    UG->fillRoundRect(barX+2,barY+2,fill,barH-4,3,COL_GREEN);
    if((++_pcTick & 7)==0) uiFlush();   // throttled flip so the progress bar animates
  }
  free(buf);
  if(fsz>copied) memset(dst+copied,0,fsz-copied);
  f.close();

  UG->setFont(&lgfx::fonts::DejaVu9);
  UG->setTextColor(COL_GREEN,COL_PANEL);
  UG->setCursor(8,barY+18); UG->print("OK  "+String(copied/1024)+"KB");
  uiFlush();
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

// Expand the zero-RLE embedded ADF straight into the RAM-disk data area. No SD needed.
static void diagInflate(const uint8_t* src, uint32_t slen, uint8_t* dst){
  uint32_t di=0;
  for(uint32_t si=0; si<slen; ){ uint8_t b=pgm_read_byte(&src[si++]);
    if(b==0){ uint8_t cnt=pgm_read_byte(&src[si++]); memset(dst+di,0,cnt); di+=cnt; }
    else dst[di++]=b; }
}
// Mount the built-in Amiga Test Kit as the emulated disk — works with no SD card.
static void doLoadDiag(){
  g_info_showing=false;
  if(g_loaded) hardDetach();
  build_volume_with_file("DISK.ADF", DIAG_ADF_SIZE);          // force an .ADF image regardless of MODE
  diagInflate(DIAG_RLE, DIAG_RLE_LEN, g_disk+DATA_LBA*SECTOR_SIZE);
  hardAttach();
  g_loaded=true; g_loaded_name="AMIGA TEST KIT";
  g_loaded_game_idx=-1; g_loaded_disk_idx=-1;
  drawFullUI();
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
static uint32_t bbLastActionMs=0;
static void handleTap(uint16_t px,uint16_t py);   // tap dispatcher (below loop)

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
    selfHealConfig();    // append CRACKTRO= to an older CONFIG.TXT that predates it
    listImages(SD_MMC,g_files);
    if(!readGameCache()) buildGameList();
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

  loadTheme();         // must be before cracktro so LOOP=, THEME=, CRACKTRO=, ROTATE= are loaded
  tft.setRotation(g_rot);                   // apply ROTATE= (K: 180 = flip during flush + touch remap)
  gW = tft.width(); gH = tft.height();
  relayout();
  drawCracktro(g_cracktro);   // 6-style boot cracktro (CRACKTRO= config). K engine — no LovyanGFX version pin needed.

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
    UG->fillRect(0,0,LCD_WIDTH,STATUS_H,0x07E0);
    UG->setFont(&lgfx::fonts::DejaVu9);
    UG->setTextColor(TFT_BLACK,0x07E0);
    UG->setCursor(LCD_WIDTH/2 - 72, 8);
    UG->print("** XIAO LINK ESTABLISHED **");
    uiFlush();
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

  // ── Touch state machine: tap vs drag-scroll with flick inertia (JC engine) ──
  if(haveTouch){
    g_touch_release=0;                                  // any touch resets the lift counter
    if(!g_touch_active){
      // finger down
      g_touch_active=true; g_touch_x0=px; g_touch_y0=py; g_touch_px0=g_scrollPx;
      g_touch_lastY=py; g_touch_lastMs=nowMs; g_touch_moved=false; g_touch_vel=0; g_inertia_on=false;
      // NOTE: unlike the JC (whose info panel overlays the list), the 7" info
      // panel lives in the cover column — the list stays scrollable beside it.
      g_touch_inlist=(px>=LIST_X&&px<AZ_X&&py>=LIST_TOP&&py<LIST_BOTTOM&&!g_games.empty());
    } else {
      // finger held / moving
      if(abs((int)px-g_touch_x0)>DRAG_THRESH||abs((int)py-g_touch_y0)>DRAG_THRESH) g_touch_moved=true;
      if(g_touch_inlist&&g_touch_moved){
        g_scrollPx=g_touch_px0-(float)((int)py-g_touch_y0);
        if(g_scrollPx<0) g_scrollPx=0;
        { int mp=maxScrollPx(); if(g_scrollPx>(float)mp) g_scrollPx=(float)mp; }
        uint32_t dt=nowMs-g_touch_lastMs;
        if(dt>0){ g_touch_vel=(float)((int)py-g_touch_lastY)/(float)dt; g_touch_lastY=py; g_touch_lastMs=nowMs; }
        redrawListArea();
      }
    }
    return;
  }

  // no touch this frame — only a real lift after several consecutive empty frames
  if(g_touch_active){
    if(++g_touch_release<RELEASE_FRAMES){ delay(1); return; }
    g_touch_active=false; g_touch_release=0;
    if(g_touch_moved){
      // ANY moved gesture arms the tap cooldown: if the GT911 blips mid-drag,
      // the split-off "second touch" can't fire a phantom tap (the LOAD DIAG bug)
      bbLastActionMs=nowMs;
      if(g_touch_inlist){
        // list drag released -> coast with flick inertia
        g_inertia_vel=-g_touch_vel*16.0f;
        g_inertia_on=fabsf(g_inertia_vel)>0.5f;
      }
    } else if(!g_touch_moved){
      // clean tap (no drag) -> dispatch at the DOWN position
      if(nowMs-bbLastActionMs>=BTN_COOLDOWN_MS){
        bbLastActionMs=nowMs;
        handleTap((uint16_t)g_touch_x0,(uint16_t)g_touch_y0);
      }
    }
    return;
  }

  // idle: run inertia
  if(g_inertia_on){
    g_scrollPx+=g_inertia_vel; g_inertia_vel*=0.92f;
    if(g_scrollPx<0){ g_scrollPx=0; g_inertia_on=false; }
    { int mp=maxScrollPx(); if(g_scrollPx>(float)mp){ g_scrollPx=(float)mp; g_inertia_on=false; } }
    if(fabsf(g_inertia_vel)<0.3f) g_inertia_on=false;
    redrawListArea();
    return;
  }
  delay(1);
}

// ── Tap dispatcher — a completed tap (down + up, no drag) lands here ─────────
static void handleTap(uint16_t px,uint16_t py){
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
    int pairBtnY = LCD_HEIGHT-BOTTOM_H-156;

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
    // RESCAN SD (left half) + ROTATE (right half)
    int rescanBtnY = LCD_HEIGHT - BOTTOM_H - 114;
    if(py >= (uint16_t)rescanBtnY && py < (uint16_t)(rescanBtnY+30) &&
       px >= 6 && px < COVER_W-6) {
      int hw=(pw-6)/2, dx=6+hw+6;
      if(px >= (uint16_t)dx){
        // right half = FLIP: toggle landscape 0 <-> 180 (portrait disabled on this RGB panel)
        int nextDeg = (g_rot==0) ? 180 : 0;
        UG->fillRoundRect(dx, rescanBtnY, hw, 30, 8, COL_BLUE);
        UG->setFont(&lgfx::fonts::DejaVu12); UG->setTextColor(TFT_WHITE, COL_BLUE);
        { int tw=UG->textWidth("SAVING..."); UG->setCursor(dx+(hw-tw)/2, rescanBtnY+9); }
        UG->print("SAVING...");
        uiFlush();
        saveRotate(nextDeg);
        delay(600);
        ESP.restart();
        return;
      }
      // left half = RESCAN SD
      UG->fillRoundRect(6, rescanBtnY, hw, 30, 8, COL_GREEN);
      UG->setFont(&lgfx::fonts::DejaVu12);
      UG->setTextColor(TFT_BLACK, COL_GREEN);
      { int tw=UG->textWidth("SCANNING"); UG->setCursor(6+(hw-tw)/2, rescanBtnY+9); }
      UG->print("SCANNING");
      uiFlush();
      doRescan();
      return;
    }
    // SOFT RESET (left half) + LOAD DIAG (right half) share one row
    int resetBtnY = LCD_HEIGHT - BOTTOM_H - 78;
    if(py >= (uint16_t)resetBtnY && py < (uint16_t)(resetBtnY+30) &&
       px >= 6 && px < COVER_W-6) {
      int hw=(pw-6)/2, dx=6+hw+6;
      if(px >= (uint16_t)dx){ doLoadDiag(); return; }   // right half = LOAD DIAG
      // left half = SOFT RESET
      UG->fillRoundRect(6, resetBtnY, hw, 30, 8, (uint16_t)0xE8C4);
      UG->setFont(&lgfx::fonts::DejaVu12);
      UG->setTextColor(TFT_BLACK, (uint16_t)0xE8C4);
      { int tw=UG->textWidth("RESET");
        UG->setCursor(6+(hw-tw)/2, resetBtnY+9); }
      UG->print("RESET");
      uiFlush();
      delay(700);
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
    bool insHit;
    if(g_portrait_mode){ int cb=STATUS_H+MODE_BAR_H+COVER_H, pb=cb-42;
      insHit = (px>=8&&px<LCD_WIDTH-8&&py>=(uint16_t)pb&&py<(uint16_t)(pb+34)); }
    else { int btnY=LCD_HEIGHT-BOTTOM_H-40;
      insHit = (px<COVER_W&&py>=(uint16_t)btnY&&py<(uint16_t)(LCD_HEIGHT-BOTTOM_H)); }
    if(insHit && !g_games.empty()){
      bool isLoaded=(g_loaded&&g_loaded_game_idx==g_sel);
      if(isLoaded){
        doUnload();
      } else {
        const GameEntry& game=g_games[g_sel];
        int diskFileIdx=game.disk_indices.empty()?
                        game.first_file_idx:
                        game.disk_indices[min(g_disk_sel,(int)game.disk_indices.size()-1)];
        doLoadSelected(g_files[diskFileIdx]);
      }
      return;
    }
  }

  // ── Cover panel: disk selector buttons (must mirror draw) ──
  if(!g_games.empty()){
    const GameEntry& game=g_games[g_sel];
    if(game.disk_count>1){
      if(g_portrait_mode){
        int cb=STATUS_H+MODE_BAR_H+COVER_H, pbtnY=cb-42, pbw=LCD_WIDTH-16;
        int nd=game.disk_count, bw2=28,bh2=18,gap=4;
        int per=max(1,pbw/(bw2+gap)), show=min(nd,per);
        int rowW=show*(bw2+gap)-gap, sx=(LCD_WIDTH-rowW)/2, dy=pbtnY-bh2-6;
        for(int d=0; d<show; d++){ int bx=sx+d*(bw2+gap);
          if(px>=(uint16_t)bx&&px<(uint16_t)(bx+bw2)&&py>=(uint16_t)dy&&py<(uint16_t)(dy+bh2)){
            g_disk_sel=d;
            if(g_loaded&&g_loaded_game_idx==g_sel) doLoadSelected(g_files[game.disk_indices[g_disk_sel]]);
            else drawCoverPanel();
            return;
          } }
      } else if(px<COVER_W){
        const int PER_ROW=7; int btnW=23,btnH=18,gap=3; int nd=game.disk_count;
        int rows=(nd+PER_ROW-1)/PER_ROW; int rowH=btnH+gap;
        int btnY=LCD_HEIGHT-BOTTOM_H-44; int diskY=btnY - 12 - rows*rowH - 4 + 12;
        for(int d=0;d<nd;d++){
          int col=d%PER_ROW, row=d/PER_ROW; int inRow=min(PER_ROW, nd-row*PER_ROW);
          int rowW=inRow*(btnW+gap)-gap; int startX=max(4,(COVER_W-rowW)/2);
          int bx=startX+col*(btnW+gap); int by=diskY+row*rowH;
          if(px>=(uint16_t)bx&&px<(uint16_t)(bx+btnW)&&py>=(uint16_t)by&&py<(uint16_t)(by+btnH)){
            g_disk_sel=d;
            if(g_loaded&&g_loaded_game_idx==g_sel){ doLoadSelected(g_files[game.disk_indices[g_disk_sel]]); }
            else { drawCoverPanel(); }
            return;
          }
        }
      }
    }
  }

  // ── Mode bar ADF/DSK ───────────────────────────────────────────────────────
  if(py>=STATUS_H&&py<STATUS_H+MODE_BAR_H&&px>=LIST_X){
    if(px>=LIST_X+6&&px<LIST_X+44&&g_mode!=MODE_ADF){
      g_mode=MODE_ADF;
      if(!listImages(SD_MMC,g_files)) g_files.clear();
      if(!readGameCache())buildGameList(); buildActiveLetters(); g_sel=0; g_scroll=0; g_disk_sel=0;
      g_scrollPx=0; g_inertia_on=false;
      drawFullUI(); return;
    }
    if(px>=LIST_X+50&&px<LIST_X+88&&g_mode!=MODE_DSK){
      g_mode=MODE_DSK;
      if(!listImages(SD_MMC,g_files)) g_files.clear();
      if(!readGameCache())buildGameList(); buildActiveLetters(); g_sel=0; g_scroll=0; g_disk_sel=0;
      g_scrollPx=0; g_inertia_on=false;
      drawFullUI(); return;
    }
  }

  // ── File list tap ──────────────────────────────────────────────────────────
  if(px>=LIST_X&&px<AZ_X&&py>=LIST_TOP&&py<LIST_BOTTOM){
    g_info_showing=false;
    int gi=(int)((g_scrollPx+(float)(py-LIST_TOP))/LIST_ITEM_H);   // pixel-accurate row hit
    if(gi>=0&&gi<(int)g_games.size()){
      // Tap = SELECT only (v4.5.3: double-tap-to-insert removed — loading is
      // done exclusively via the INSERT button, so browsing can't mount a disk)
      if(gi!=g_sel){
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
        g_scrollPx=(float)(g_scroll*LIST_ITEM_H); g_inertia_on=false;
        drawListAndCover();
      }
    } else if(btn==1){
      if(g_sel<(int)g_games.size()-1){
        g_sel++; g_disk_sel=0;
        if(g_sel>=g_scroll+ITEMS_VIS) g_scroll=g_sel-ITEMS_VIS+1;
        int maxOff=(int)g_games.size()-ITEMS_VIS; if(maxOff<0) maxOff=0;
        if(g_scroll>maxOff) g_scroll=maxOff;
        g_scrollPx=(float)(g_scroll*LIST_ITEM_H); g_inertia_on=false;
        drawListAndCover();
      }
    } else if(btn==2){
      // THEME — cycle to next
      cycleTheme();
    } else {
      // INFO — second tap toggles back to the regular cover panel
      if(g_info_showing){
        g_info_showing=false;
        drawCoverPanel();
        return;
      }
      UiFrame _uf;   // compose the whole INFO panel off-screen, push once
      UG->fillRect(0,STATUS_H,COVER_W,LCD_HEIGHT-STATUS_H-BOTTOM_H,COL_PANEL);
      int y = STATUS_H + 8;
      int pw = COVER_W - 12;  // panel usable width

      // --- TRANSFER MODE label ---
      UG->setFont(&lgfx::fonts::DejaVu9);
      UG->setTextColor(COL_DIM, COL_PANEL);
      UG->setCursor(8, y); UG->print("TRANSFER MODE"); y += 13;

      // STANDALONE button — double height (48px)
      uint16_t saCol = !g_wireless_mode ? COL_GREEN : COL_BAR;
      uint16_t saBrd = !g_wireless_mode ? COL_GREEN : COL_DIM;
      UG->fillRoundRect(6, y, pw, 48, 8, saCol);
      UG->drawRoundRect(6, y, pw, 48, 8, saBrd);
      UG->setFont(&lgfx::fonts::DejaVu18);
      UG->setTextColor(!g_wireless_mode ? TFT_BLACK : COL_DIM, saCol);
      { int tw=UG->textWidth("STANDALONE");
        UG->setCursor(6+(pw-tw)/2, y+14); }
      UG->print("STANDALONE");
      UG->setFont(&lgfx::fonts::DejaVu9);
      UG->setTextColor(!g_wireless_mode ? TFT_BLACK : COL_DIM, saCol);
      { int tw=UG->textWidth("direct USB to Gotek");
        UG->setCursor(6+(pw-tw)/2, y+34); }
      UG->print("direct USB to Gotek");
      int saY = y;
      y += 52;

      // WIRELESS button — double height (48px)
      uint16_t wiCol = g_wireless_mode ? COL_BLUE : COL_BAR;
      uint16_t wiBrd = g_wireless_mode ? COL_BLUE : COL_DIM;
      UG->fillRoundRect(6, y, pw, 48, 8, wiCol);
      UG->drawRoundRect(6, y, pw, 48, 8, wiBrd);
      UG->setFont(&lgfx::fonts::DejaVu18);
      UG->setTextColor(g_wireless_mode ? TFT_WHITE : COL_DIM, wiCol);
      { int tw=UG->textWidth("WIRELESS");
        UG->setCursor(6+(pw-tw)/2, y+14); }
      UG->print("WIRELESS");
      UG->setFont(&lgfx::fonts::DejaVu9);
      UG->setTextColor(g_wireless_mode ? TFT_WHITE : COL_DIM, wiCol);
      { int tw=UG->textWidth("send via WiFi to XIAO");
        UG->setCursor(6+(pw-tw)/2, y+34); }
      UG->print("send via WiFi to XIAO");
      y += 56;

      // Store saY for touch handler
      g_info_pair_btn_y = saY;

      // Divider
      UG->drawFastHLine(8, y, pw, COL_SEP); y += 6;

      // --- SYSTEM INFO ---
      UG->setFont(&lgfx::fonts::DejaVu9);
      UG->setTextColor(COL_LIT, COL_PANEL);
      UG->setCursor(8,y); UG->print("Heap: "+String(ESP.getFreeHeap()/1024)+"KB  PSRAM: "+String(ESP.getFreePsram()/1024)+"KB"); y+=12;
      UG->setCursor(8,y); UG->print("Games: "+String(g_games.size())+"  FW: " FW_VERSION); y+=14;

      // --- WIRELESS STATUS (only in wireless mode) ---
      if (g_wireless_mode) {
        UG->drawFastHLine(8, y, pw, COL_SEP); y+=6;
        UG->setFont(&lgfx::fonts::DejaVu9);
        if(espnowIsPaired()){
          bool online = espnowXiaoOnline();
          UG->setTextColor(online ? COL_GREEN : COL_ORANGE, COL_PANEL);
          UG->setCursor(8,y); UG->print(online ? "XIAO: ONLINE" : "XIAO: OFFLINE"); y+=11;
          UG->setTextColor(COL_LIT, COL_PANEL);
          String mac = espnowGetXiaoMac();
          UG->setCursor(8,y); UG->print(mac); y+=11;
          UG->setTextColor(COL_DIM, COL_PANEL);
          UG->setCursor(8,y); UG->print("AP: GotekXIAO"); y+=11;
          if(g_espnow_xiao_done){
            UG->setTextColor(COL_GREEN,COL_PANEL);
            UG->setCursor(8,y); UG->print("Status: loaded OK"); y+=11;
          } else if(g_espnow_xiao_error){
            UG->setTextColor(0xE8C4,COL_PANEL);
            UG->setCursor(8,y); UG->print("Status: error"); y+=11;
          } else {
            UG->setTextColor(COL_LIT,COL_PANEL);
            UG->setCursor(8,y); UG->print("Status: idle"); y+=11;
          }
        } else {
          UG->setTextColor(COL_ORANGE, COL_PANEL);
          UG->setCursor(8,y); UG->print("Not paired - tap PAIR NOW"); y+=11;
        }
        y += 4;

        // PAIR NOW button — anchored at bottom of panel
        int pairBtnY = LCD_HEIGHT - BOTTOM_H - 156;
        uint16_t pairCol = espnowIsPaired() ? COL_GREEN : COL_AMBER;
        UG->fillRoundRect(6, pairBtnY, pw, 36, 8, pairCol);
        UG->drawRoundRect(6, pairBtnY, pw, 36, 8, pairCol);
        UG->setFont(&lgfx::fonts::DejaVu18);
        UG->setTextColor(TFT_BLACK, pairCol);
        const char* pairLabel = espnowIsPaired() ? "RE-PAIR" : "PAIR NOW";
        { int tw=UG->textWidth(pairLabel);
          UG->setCursor(6+(pw-tw)/2, pairBtnY+10); }
        UG->print(pairLabel);
      }

      // RESCAN SD (left half) + ROTATE (right half) — above SOFT RESET
      int rescanBtnY = LCD_HEIGHT - BOTTOM_H - 114;
      { int hw=(pw-6)/2, dx=6+hw+6;
        UG->setFont(&lgfx::fonts::DejaVu12);
        // left: RESCAN SD
        UG->fillRoundRect(6, rescanBtnY, hw, 30, 8, COL_ACCENT);
        UG->drawRoundRect(6, rescanBtnY, hw, 30, 8, COL_ACCENT);
        UG->setTextColor(TFT_WHITE, COL_ACCENT);
        { int tw=UG->textWidth("RESCAN"); UG->setCursor(6+(hw-tw)/2, rescanBtnY+9); }
        UG->print("RESCAN");
        // right: FLIP (landscape 0<->180; shows current orientation; tap saves + reboots)
        UG->fillRoundRect(dx, rescanBtnY, hw, 30, 8, COL_BLUE);
        UG->drawRoundRect(dx, rescanBtnY, hw, 30, 8, COL_BLUE);
        UG->setTextColor(TFT_WHITE, COL_BLUE);
        String rl = "ROT " + String(g_rot*90);
        { int tw=UG->textWidth(rl); UG->setCursor(dx+(hw-tw)/2, rescanBtnY+9); }
        UG->print(rl);
      }

      // SOFT RESET (left half) + LOAD DIAG (right half) — share one row
      int resetBtnY = LCD_HEIGHT - BOTTOM_H - 78;
      { int hw=(pw-6)/2, dx=6+hw+6;
        UG->setFont(&lgfx::fonts::DejaVu12);
        // left: SOFT RESET
        UG->fillRoundRect(6, resetBtnY, hw, 30, 8, (uint16_t)0x8000);
        UG->drawRoundRect(6, resetBtnY, hw, 30, 8, (uint16_t)0xE8C4);
        UG->setTextColor(TFT_WHITE, (uint16_t)0x8000);
        { int tw=UG->textWidth("SOFT RESET"); UG->setCursor(6+(hw-tw)/2, resetBtnY+9); }
        UG->print("SOFT RESET");
        // right: LOAD DIAG
        UG->fillRoundRect(dx, resetBtnY, hw, 30, 8, COL_ACCENT);
        UG->drawRoundRect(dx, resetBtnY, hw, 30, 8, COL_ACCENT);
        UG->setTextColor(TFT_WHITE, COL_ACCENT);
        { int tw=UG->textWidth("LOAD DIAG"); UG->setCursor(dx+(hw-tw)/2, resetBtnY+9); }
        UG->print("LOAD DIAG");
      }

      g_info_showing = true;
    }
    return;
  }
}
