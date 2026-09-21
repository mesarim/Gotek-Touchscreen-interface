// ============================================================================
// GTi_7IN_ReelTest — 7" (800x480) ESP32-S3 draw-speed bench
// OMEGAWARE / Gotek Touchscreen Interface
// ----------------------------------------------------------------------------
// WHY THIS SKETCH EXISTS
//   Gotek_7inch v5.0.1 draws its 3x2 cover grid by calling drawJpegFit() /
//   drawPngFit() ONCE PER TILE, ON EVERY PAGE DRAW — six full JPEG/PNG decodes
//   per page turn — while carTile()/carLoadThumb()/carBlit() (the JC3248 tile
//   cache, already ported into that same file) sit completely unused.
//   That is the "slow draw on several images" in 5.5.5.
//
//   This sketch does nothing but measure. It renders the SAME six covers three
//   different ways and puts the millisecond cost of each on screen, so the
//   "can the 7" run a moving reel like the JC?" question gets answered with a
//   number instead of a guess.
//
// MODES (tap [MODE] on the bottom bar)
//   1. GRID-OLD  — 3x2 grid, per-tile drawJpegFit/drawPngFit   (what ships now)
//   2. GRID-NEW  — 3x2 grid, carTile() + fast blit             (the proposed fix)
//   3. REEL      — continuously moving 5-up coverflow, free-running FPS
//
// FLUSH (tap [FLUSH])
//   FULL  — display() memcpy's all 768,000 bytes of the compose buffer
//   DIRTY — display() memcpy's only the rows that changed in the last 2 frames
//           (2 because we page-flip: the back buffer is one frame stale)
//
// The HUD reports, per frame: tile fetch / draw / copy / vsync wait / total.
//
// ----------------------------------------------------------------------------
// BOARD SETTINGS — A NEW SKETCH FOLDER GETS ARDUINO'S DEFAULTS, NOT THE ONES
// YOU SET FOR Gotek_7inch. Set these or it will not boot:
//   Board         : ESP32S3 Dev Module
//   PSRAM         : OPI PSRAM   <-- MUST be set. Default is Disabled, and
//                                   "QSPI PSRAM" fails the same way on this
//                                   N16R8 board. Without it the RGB driver
//                                   cannot allocate its 1.5MB of framebuffers
//                                   and the panel never comes up.
//   Flash Size    : 16MB (128Mb)
//   Flash Mode    : QIO 120MHz
//   Partition     : 16M Flash (3MB APP/9.9MB FATFS)
//   USB CDC on boot : ENABLED   (this bench has no MSC, so leave CDC on for Serial)
//   USB Mode      : Hardware CDC and JTAG
//   CPU Frequency : 240MHz
//
// Reads the card READ-ONLY apart from writing .thumbs tiles — and those are the
// same 200x200 .tnl files Gotek_7inch already uses, so warming them here warms
// the shipping firmware too.
// ============================================================================

#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include <Wire.h>
#include <JPEGDEC.h>
#undef INTELSHORT
#undef INTELLONG
#undef MOTOSHORT
#undef MOTOLONG
#include <PNGdec.h>
#include <sys/stat.h>
#include <math.h>
#include <dirent.h>
#include <vector>
#include <algorithm>
#include <ctype.h>

#define BENCH_VERSION "7IN-REELTEST-1.0"

// ---------- panel geometry (Waveshare 7" 800x480 RGB) ----------
#define KLCD_W 800
#define KLCD_H 480

// ---------- SD (1-bit SDMMC) ----------
#define SD_MOSI 11
#define SD_CLK  12
#define SD_MISO 13

// ---------- I2C expander + GT911 ----------
#define EXPANDER_I2C_SDA   8
#define EXPANDER_I2C_SCL   9
#define EXPANDER_I2C_ADDR  0x20
#define GT911_INT_PIN      4
#define EXP_ALL_ON   0xDF   // all high EXCEPT USB_SEL (bit5) — high kills USB
#define EXP_TPRST_LO 0xDD

// ---------- bench knobs ----------
#define CAR_TILE    200     // tile size — MATCHES Gotek_7inch so .thumbs interop
#define CAR_SLOTS   16      // LRU tile slots in PSRAM (~1.28 MB)
#define MAX_GAMES   1400    // headroom over a 1005-game card
#define GRID_COLS   3
#define GRID_ROWS   2
#define GRID_PER    6
#define REEL_VIS    5       // covers visible in the reel
#define STATUS_H    26
#define BOTTOM_H    52

// ---------- colours ----------
#define C_BLACK  0x0000
#define C_WHITE  0xFFFF
#define C_BG     0x0841
#define C_BAR    0x2124
#define C_SEP    0x4208
#define C_DIM    0x8410
#define C_LIT    0xDEFB
#define C_ACC    0x05BF
#define C_AMBER  0xFD20
#define C_GREEN  0x07E0
#define C_RED    0xF800
#define C_BLUE   0x04FF
#define C_ORANGE 0xFC00

static uint16_t* jpeg_tmp_buf=NULL;
static int jpeg_tmp_w=0, jpeg_tmp_h=0;
static JPEGDEC jpegdec;
static PNG     pngdec;

static void expanderOut(uint8_t val){
  Wire.beginTransmission(0x38); Wire.write(val); Wire.endTransmission();
  Wire.beginTransmission(EXPANDER_I2C_ADDR); Wire.write(0x01); Wire.write(val); Wire.endTransmission();
}
static void initExpander(){
  Wire.begin(EXPANDER_I2C_SDA, EXPANDER_I2C_SCL, 400000);
  Wire.beginTransmission(0x24); Wire.write(0x01); Wire.endTransmission();   // CH422G outputs on
  Wire.beginTransmission(EXPANDER_I2C_ADDR); Wire.write(0x03); Wire.write(0x00); Wire.endTransmission();
  // GT911 latches its I2C address at reset from INT: LOW -> 0x5D.
  pinMode(GT911_INT_PIN, OUTPUT); digitalWrite(GT911_INT_PIN, LOW);
  expanderOut(EXP_TPRST_LO); delay(15);
  expanderOut(EXP_ALL_ON);   delay(10);
  pinMode(GT911_INT_PIN, INPUT); delay(80);
}

// 6x8 bitmap font, ASCII 32..126.
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


// Font handles kept name-compatible with the shipping sketch.
namespace lgfx { namespace fonts {
  static const uint8_t Font0=0, DejaVu9=1, DejaVu12=2, DejaVu18=3;
}}

// ============================================================================
// KGfx — esp_lcd RGB panel, 2 driver framebuffers, PSRAM compose buffer.
// Same architecture as Gotek_7inch, plus:
//   * per-frame dirty-row tracking
//   * micro-second timers split into copy vs vsync-latch
// Landscape only (no 180 flip in the bench — it would hide the copy cost behind
// a per-pixel reversed loop, which is exactly what we are trying to measure).
// ============================================================================
static volatile uint32_t k_vsync_count = 0;
static bool IRAM_ATTR kOnVsync(esp_lcd_panel_handle_t p,
                               const esp_lcd_rgb_panel_event_data_t* e, void* u){
  k_vsync_count = k_vsync_count + 1;   // not ++: compound ops on volatile are deprecated in C++20
  return false;
}
static void kWaitVsync(){
  uint32_t s=k_vsync_count; uint32_t t0=millis();
  while(k_vsync_count==s && (millis()-t0)<50){ delayMicroseconds(200); }
}

class KGfx {
public:
  esp_lcd_panel_handle_t panel=NULL;
  uint16_t *fb0=NULL,*fb1=NULL;
  uint16_t *cb=NULL;              // compose buffer (PSRAM)
  uint16_t *draw=NULL;            // where drawing actually lands: cb, or the back fb
  uint8_t  backIdx=0;
  bool     ok=false;
  // DIRTY-RECT tracking. Deliberately the SAME shape as Gotek_7B's engine
  // (Gotek_7B.ino ~line 298) so whatever this measures transfers straight into
  // a back-port: this frame's box, unioned with the previous frame's, because
  // the page-flip means the buffer being written to last showed the frame
  // before last. Start both FULL so the first two flushes initialise both.
  int  dx0=0, dy0=0, dx1=KLCD_W, dy1=KLCD_H;
  int  pdx0=0, pdy0=0, pdx1=KLCD_W, pdy1=KLCD_H;
  // 0 = FULL   : copy the whole compose buffer every frame (what Gotek_7inch does)
  // 1 = DIRTY  : copy only this frame's box unioned with last frame's (the 7B engine)
  // 2 = DIRECT : no compose buffer at all - draw straight into the back framebuffer
  //              and just flip. Costs nothing to present, but anything not redrawn
  //              every frame must be painted into BOTH buffers (see g_bars_paint).
  uint8_t flushMode=0;
  // measured per display() call
  uint32_t t_copy_us=0, t_vsync_us=0, n_copy_px=0;
  // text state
  int tx=0, ty=0, tsize=1; float fscale=1.0f;
  uint16_t tfg=C_WHITE, tbg=C_BLACK;
  uint8_t gtAddr=0x5D;
  int clx0=0, cly0=0, clx1=KLCD_W, cly1=KLCD_H;

  inline void markDirty(int x0,int y0,int x1,int y1){
    if(x0<0)x0=0; if(y0<0)y0=0; if(x1>KLCD_W)x1=KLCD_W; if(y1>KLCD_H)y1=KLCD_H;
    if(x1<=x0||y1<=y0) return;
    if(x0<dx0)dx0=x0; if(y0<dy0)dy0=y0; if(x1>dx1)dx1=x1; if(y1>dy1)dy1=y1;
  }
  void markDirtyReset(){ dx0=KLCD_W; dy0=KLCD_H; dx1=0; dy1=0; }
  bool init(){
    esp_lcd_rgb_panel_config_t cfg = {};
    cfg.clk_src = LCD_CLK_SRC_DEFAULT;
    cfg.timings.pclk_hz            = 16000000;
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
    cfg.num_fbs               = 2;
    cfg.bounce_buffer_size_px = KLCD_W * 20;
    // psram_trans_align and on_bounce_frame_finish are deprecated in IDF 5.x
    // (dma_burst_size / on_frame_buf_complete replace them). They are set here
    // anyway because they are exactly what Gotek_7inch sets and what is proven
    // on this panel - and on_bounce_frame_finish is load-bearing: with a bounce
    // buffer configured, on_vsync does not fire, so dropping it would leave
    // kWaitVsync() timing out at 50 ms every frame and wreck the measurement.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
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
    cbs.on_bounce_frame_finish = kOnVsync;
#pragma GCC diagnostic pop
    esp_lcd_rgb_panel_register_event_callbacks(panel,&cbs,NULL);
    if(esp_lcd_panel_reset(panel)!=ESP_OK) return false;
    if(esp_lcd_panel_init(panel)!=ESP_OK) return false;
    if(esp_lcd_rgb_panel_get_frame_buffer(panel,2,(void**)&fb0,(void**)&fb1)!=ESP_OK
       || !fb0 || !fb1) return false;
    cb=(uint16_t*)heap_caps_malloc((size_t)KLCD_W*KLCD_H*2,MALLOC_CAP_SPIRAM);
    if(!cb) return false;
    draw=cb;
    ok=true;
    touchProbe();
    fillScreen(C_BLACK); display(); display();
    return true;
  }
  int  width()  const { return KLCD_W; }
  int  height() const { return KLCD_H; }
  void setClip(int x0,int y0,int x1,int y1){
    clx0=max(0,x0); cly0=max(0,y0); clx1=min(KLCD_W,x1); cly1=min(KLCD_H,y1);
  }
  void resetClip(){ clx0=0; cly0=0; clx1=KLCD_W; cly1=KLCD_H; }
  inline void px(int x,int y,uint16_t c){
    if(x<clx0||x>=clx1||y<cly0||y>=cly1||!draw) return;
    draw[y*KLCD_W+x]=c; markDirty(x,y,x+1,y+1);
  }
  void fillScreen(uint16_t c){
    if(!draw)return; for(int i=0;i<KLCD_W*KLCD_H;i++) draw[i]=c; markDirty(0,0,KLCD_W,KLCD_H);
  }
  void fillRect(int x,int y,int w,int h,uint16_t c){
    if(!draw)return;
    int x0=max(clx0,x),y0=max(cly0,y),x1=min(clx1,x+w),y1=min(cly1,y+h);
    if(x0>=x1||y0>=y1)return;
    for(int yy=y0;yy<y1;yy++){ uint16_t* r=draw+yy*KLCD_W; for(int xx=x0;xx<x1;xx++) r[xx]=c; }
    markDirty(x0,y0,x1,y1);
  }
  void drawRect(int x,int y,int w,int h,uint16_t c){
    fillRect(x,y,w,1,c); fillRect(x,y+h-1,w,1,c); fillRect(x,y,1,h,c); fillRect(x+w-1,y,1,h,c);
  }
  void drawPixel(int x,int y,uint16_t c){ px(x,y,c); }
  void drawFastHLine(int x,int y,int w,uint16_t c){ fillRect(x,y,w,1,c); }
  void drawFastVLine(int x,int y,int h,uint16_t c){ fillRect(x,y,1,h,c); }
  void fillRoundRect(int x,int y,int w,int h,int r,uint16_t c){
    if(r<=0){ fillRect(x,y,w,h,c); return; }
    fillRect(x+r,y,w-2*r,h,c); fillRect(x,y+r,r,h-2*r,c); fillRect(x+w-r,y+r,r,h-2*r,c);
    for(int dy=-r;dy<=0;dy++){
      int dx=(int)sqrtf((float)(r*r-dy*dy));
      fillRect(x+r-dx,y+r+dy,dx,1,c);     fillRect(x+w-r,y+r+dy,dx,1,c);
      fillRect(x+r-dx,y+h-r-1-dy,dx,1,c); fillRect(x+w-r,y+h-r-1-dy,dx,1,c);
    }
  }
  void drawRoundRect(int x,int y,int w,int h,int r,uint16_t c){
    drawFastHLine(x+r,y,w-2*r,c); drawFastHLine(x+r,y+h-1,w-2*r,c);
    drawFastVLine(x,y+r,h-2*r,c); drawFastVLine(x+w-1,y+r,h-2*r,c);
  }
  void pushImage(int x,int y,int w,int h,const uint16_t* img){
    if(!draw||!img) return;
    int hx0=KLCD_W,hy0=KLCD_H,hx1=0,hy1=0;
    for(int yy=0;yy<h;yy++){
      int dy=y+yy; if(dy<cly0||dy>=cly1) continue;
      int sx=0, dx=x, cw=w;
      if(dx<clx0){ sx=clx0-dx; cw-=sx; dx=clx0; }
      if(dx+cw>clx1) cw=clx1-dx;
      if(cw>0){ memcpy(draw+dy*KLCD_W+dx, img+yy*w+sx, (size_t)cw*2);
                if(dx<hx0)hx0=dx; if(dx+cw>hx1)hx1=dx+cw;
                if(dy<hy0)hy0=dy; if(dy+1>hy1)hy1=dy+1; }
    }
    markDirty(hx0,hy0,hx1,hy1);
  }
  // -- text --
  void setFont(const uint8_t* f){
    switch(f?*f:1){ case 2: fscale=1.5f; break; case 3: fscale=2.0f; break; default: fscale=1.0f; }
  }
  void setTextSize(int s){ tsize=(s<1)?1:s; }
  void setTextColor(uint16_t f,uint16_t b){ tfg=f; tbg=b; }
  void setCursor(int x,int y){ tx=x; ty=y; }
  int  charW() const { return (int)(6.0f*fscale*tsize+0.5f); }
  int  charH() const { return (int)(8.0f*fscale*tsize+0.5f); }
  int  textWidth(const String& s){ return (int)s.length()*charW(); }
  int  textWidth(const char* s){ return (int)strlen(s)*charW(); }
  void drawGlyph(char ch){
    if(ch<32||ch>126) ch='?';
    const uint8_t* d=crk_font[ch-32];
    float s=fscale*tsize; int cw=charW(), chh=charH();
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
  // -- touch (raw GT911) --
  void touchProbe(){
    const uint8_t cands[2]={0x5D,0x14};
    for(int i=0;i<2;i++){ Wire.beginTransmission(cands[i]); if(Wire.endTransmission()==0){ gtAddr=cands[i]; return; } }
    gtAddr=0x5D;
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
      if(gtRead(0x8150,p,8)){
        int32_t rx=(int32_t)(p[0]|(p[1]<<8));
        int32_t ry=(int32_t)(p[2]|(p[3]<<8));
        if(rx<0)rx=0; if(rx>=KLCD_W)rx=KLCD_W-1;
        if(ry<0)ry=0; if(ry>=KLCD_H)ry=KLCD_H-1;
        *x=rx; *y=ry; got=true;
      }
    }
    gtWrite8(0x814E,0);
    return got;
  }
  // -- flush --------------------------------------------------------------
  // FULL : 768,000 bytes every frame (what Gotek_7inch does today).
  // DIRTY: only the union of this frame's and last frame's touched rows. The
  //        union is required because we PAGE-FLIP: the buffer we are writing
  //        into last showed the frame before last, so anything that changed in
  //        EITHER of the last two frames is stale in it.
  void display(){
    if(!ok) return;
    uint16_t* dst = backIdx ? fb1 : fb0;
    if(flushMode==2){                       // DIRECT: we already drew into dst
      t_copy_us=0; n_copy_px=0;
      uint32_t tv=micros();
      esp_lcd_panel_draw_bitmap(panel,0,0,KLCD_W,KLCD_H,dst);
      kWaitVsync();
      t_vsync_us=micros()-tv;
      backIdx^=1;
      draw = backIdx ? fb1 : fb0;           // next frame paints the other buffer
      pdx0=dx0; pdy0=dy0; pdx1=dx1; pdy1=dy1;
      markDirtyReset();
      return;
    }
    int ux0,uy0,ux1,uy1;
    if(flushMode==0){ ux0=0; uy0=0; ux1=KLCD_W; uy1=KLCD_H; }
    else { ux0=min(dx0,pdx0); uy0=min(dy0,pdy0); ux1=max(dx1,pdx1); uy1=max(dy1,pdy1); }
    uint32_t t0=micros();
    n_copy_px=0;
    if(ux1>ux0 && uy1>uy0){
      size_t rowbytes=(size_t)(ux1-ux0)*2;
      for(int yy=uy0; yy<uy1; yy++)
        memcpy(dst+(size_t)yy*KLCD_W+ux0, cb+(size_t)yy*KLCD_W+ux0, rowbytes);
      n_copy_px=(uint32_t)(ux1-ux0)*(uint32_t)(uy1-uy0);
    }
    t_copy_us=micros()-t0;
    t0=micros();
    esp_lcd_panel_draw_bitmap(panel,0,0,KLCD_W,KLCD_H,dst);
    kWaitVsync();
    t_vsync_us=micros()-t0;
    backIdx^=1;
    pdx0=dx0; pdy0=dy0; pdx1=dx1; pdy1=dy1;
    markDirtyReset();
  }
  // Switching flush modes has to leave both framebuffers and the compose buffer
  // mutually coherent, or the first flip after the change shows half of an old
  // frame. The caller must also force a full repaint (g_repaint=2), because in
  // DIRECT mode anything painted once lands in only one of the two buffers.
  void setFlushMode(uint8_t m){
    if(!ok||m==flushMode) return;
    size_t bytes=(size_t)KLCD_W*KLCD_H*2;
    if(m==2){
      memcpy(fb0,cb,bytes); memcpy(fb1,cb,bytes);   // seed both from the compose buffer
      flushMode=2; draw = backIdx ? fb1 : fb0;
    } else {
      if(flushMode==2) memcpy(cb, backIdx ? fb0 : fb1, bytes);   // the one on screen
      flushMode=m; draw=cb;
      pdx0=0; pdy0=0; pdx1=KLCD_W; pdy1=KLCD_H;     // force a full first copy
      markDirty(0,0,KLCD_W,KLCD_H);
    }
  }
};
static KGfx tft;
static KGfx* UG = &tft;

// ============================================================================
// GAME LIST
// ----------------------------------------------------------------------------
// At 1000+ games two things matter more than anything else in this file:
//
//  1. NEVER WALK THE TREE IF A CACHE EXISTS. The 7B P4 measured 577 s to
//     enumerate 1741 files / 1005 games. That is the card seeking through a
//     flat FAT directory with the CPU idle — no firmware fix exists, so the
//     bench reads the caches Gotek_7inch already writes:
//         /ADF/.filelist    #N=<count> then one path per line
//         /ADF/.gamecache   name|first_file_idx|disk_count|jpg_path|indices
//     If the card has been browsed by 5.0.1 those files are already there,
//     already carry the resolved cover paths, and boot costs seconds.
//     Failing that it walks once and writes its OWN index (/.reelbench) —
//     the firmware's files are never overwritten.
//
//  2. KEEP THE STRINGS OUT OF INTERNAL DRAM. 1005 games x 3 Arduino Strings
//     is ~180 KB of small heap blocks, and the ESP32 core only routes
//     allocations >= 4096 bytes to PSRAM, so every one of them would land in
//     the ~320 KB of internal RAM. Instead all three strings live in one PSRAM
//     bump arena and the record holds plain pointers. Nothing is ever freed —
//     this is a bench, the library is loaded once.
// ============================================================================
#define ARENA_CHUNK (256*1024)
static std::vector<char*> g_arena_chunks;
static char*  g_arena_cur=NULL;
static size_t g_arena_used=0, g_arena_cap=0, g_arena_total=0;

static const char* arenaDup(const char* src,int len){
  if(len<0) len=(int)strlen(src);
  size_t need=(size_t)len+1;
  if(!g_arena_cur || g_arena_used+need>g_arena_cap){
    size_t sz=(need>ARENA_CHUNK)?need:ARENA_CHUNK;
    char* c=(char*)ps_malloc(sz);
    if(!c) return "";
    g_arena_chunks.push_back(c);
    g_arena_cur=c; g_arena_used=0; g_arena_cap=sz; g_arena_total+=sz;
  }
  char* d=g_arena_cur+g_arena_used;
  memcpy(d,src,len); d[len]=0;
  g_arena_used+=need;
  return d;
}
static const char* arenaDupS(const String& s){ return arenaDup(s.c_str(),(int)s.length()); }

struct Game {
  const char* path;   // /ADF/Foo-1.adf
  const char* name;   // Foo
  const char* jpg;    // NULL = not looked up yet, "" = looked up and there is none
};
static std::vector<Game> g_games;
static bool g_sd_ok=false;
static uint32_t g_cover_probe_us=0;   // time spent in findCoverFor this frame

static String filenameOnly(const String& p){ int s=p.lastIndexOf('/'); return (s>=0)?p.substring(s+1):p; }
static String parentDir(const String& p){ int s=p.lastIndexOf('/'); return (s>0)?p.substring(0,s):String("/"); }
static String basenameNoExt(const String& f){ int d=f.lastIndexOf('.'); return (d>0)?f.substring(0,d):f; }
static String lowerExt(const String& p){ int d=p.lastIndexOf('.'); if(d<0)return String(""); String e=p.substring(d+1); e.toLowerCase(); return e; }
static bool coverIsPng(const String& p){ return lowerExt(p)=="png"; }
static bool isDiskImage(const String& p){
  String e=lowerExt(p);
  return e=="adf"||e=="dsk"||e=="img"||e=="hfe"||e=="st"||e=="ima"||e=="adz";
}
// "Emerald Mine 2-1" -> "Emerald Mine 2" (the cover carries no disk number)
static String gameBaseName(const String& fullPath){
  String base=basenameNoExt(filenameOnly(fullPath));
  int dash=base.lastIndexOf('-');
  if(dash>0&&dash<(int)base.length()-1){
    String suffix=base.substring(dash+1);
    bool isNum=true;
    for(int i=0;i<(int)suffix.length();i++) if(!isdigit((unsigned char)suffix[i])){ isNum=false; break; }
    if(isNum) return base.substring(0,dash);
  }
  return base;
}
// Same probe order as Gotek_7inch::findJPGFor, plus .png (the 7" decodes both).
// Up to 24 SD_MMC.exists() calls against a flat directory - this is the ~1 s
// per game the cache exists to avoid, so it is timed separately in the HUD.
static bool findCoverProbe(const String& imgPath, String& out){
  String base  = basenameNoExt(filenameOnly(imgPath));
  String dir   = parentDir(imgPath);
  String gbase = gameBaseName(imgPath);
  const char* exts[] = {".jpg",".jpeg",".JPG",".JPEG",".png",".PNG"};
  for(auto e : exts){ String c=dir+"/"+base+e;  if(SD_MMC.exists(c.c_str())){ out=c; return true; } }
  if(gbase!=base) for(auto e : exts){ String c=dir+"/"+gbase+e; if(SD_MMC.exists(c.c_str())){ out=c; return true; } }
  for(auto e : exts){ String c="/"+base+e;      if(SD_MMC.exists(c.c_str())){ out=c; return true; } }
  if(gbase!=base) for(auto e : exts){ String c="/"+gbase+e;     if(SD_MMC.exists(c.c_str())){ out=c; return true; } }
  return false;
}
static bool findCoverFor(const String& imgPath, String& out){
  uint32_t t0=micros();
  bool r=findCoverProbe(imgPath,out);
  g_cover_probe_us+=micros()-t0;
  return r;
}

static void bootLine(const String& s,int line);  // fwd (plain types only)
static void bootMsg (const String& s,int line);

// ---- whole-file slurp into PSRAM: readStringUntil() on a 1741-line index is
//      thousands of single-byte SD reads. One read + split is seconds faster.
static char* slurp(const String& path,size_t* outLen){
  struct stat st; String v="/sdcard"+path;
  if(stat(v.c_str(),&st)!=0||st.st_size<=0||st.st_size>4000000) return NULL;
  size_t sz=(size_t)st.st_size;
  File f=SD_MMC.open(path.c_str(),"r"); if(!f) return NULL;
  char* buf=(char*)ps_malloc(sz+1); if(!buf){ f.close(); return NULL; }
  size_t got=f.read((uint8_t*)buf,sz); f.close();
  buf[got]=0; if(outLen)*outLen=got;
  return buf;
}
// Walk the slurped buffer line by line, in place (destructive).
static int splitLines(char* buf,std::vector<char*>& out){
  out.clear();
  char* p=buf;
  while(*p){
    char* e=p; while(*e&&*e!='\n') e++;
    bool atEnd=(*e==0);          // MUST be sampled before the terminator is written:
    char* t=e;                   // *t=0 can land on the very '\n' we are testing for
    if(t>p&&*(t-1)=='\r') t--;
    *t=0;
    if(t>p) out.push_back(p);
    if(atEnd) break;
    p=e+1;
  }
  return (int)out.size();
}

// ---- Gotek_7inch's own caches -------------------------------------------
// Returns games added. side is "/ADF" or "/DSK".
static int loadFirmwareCache(const char* side){
  String sd=String(side);
  size_t n1=0; char* fl=slurp(sd+"/.filelist",&n1);
  if(!fl) return 0;
  std::vector<char*> flines; splitLines(fl,flines);
  long declared=-1;
  std::vector<const char*> files; files.reserve(flines.size());
  for(size_t i=0;i<flines.size();i++){
    if(strncmp(flines[i],"#N=",3)==0){ declared=atol(flines[i]+3); continue; }
    files.push_back(arenaDup(flines[i],-1));
  }
  free(fl);
  if(declared>=0 && (long)files.size()!=declared) return 0;
  if(files.empty()) return 0;

  size_t n2=0; char* gc=slurp(sd+"/.gamecache",&n2);
  if(!gc){
    // no game cache — take the file list as-is and collapse multi-disk sets
    int added=0; String prev="";
    for(size_t i=0;i<files.size()&&(int)g_games.size()<MAX_GAMES;i++){
      String gb=gameBaseName(String(files[i]));
      if(gb==prev) continue; prev=gb;
      Game g; g.path=files[i]; g.name=arenaDupS(gb); g.jpg=NULL;
      g_games.push_back(g); added++;
    }
    return added;
  }
  std::vector<char*> glines; splitLines(gc,glines);
  long declFiles=-1; int added=0;
  for(size_t i=0;i<glines.size()&&(int)g_games.size()<MAX_GAMES;i++){
    char* L=glines[i];
    if(strncmp(L,"#FILES=",7)==0){ declFiles=atol(L+7); continue; }
    // name|first_file_idx|disk_count|jpg_path|indices
    char* p1=strchr(L,'|');     if(!p1) continue;
    char* p2=strchr(p1+1,'|');  if(!p2) continue;
    char* p3=strchr(p2+1,'|');  if(!p3) continue;
    char* p4=strchr(p3+1,'|');  if(!p4) continue;
    *p1=0; *p2=0; *p3=0; *p4=0;
    int idx=atoi(p1+1);
    if(idx<0||idx>=(int)files.size()) continue;
    Game g;
    g.path=files[idx];
    g.name=arenaDup(L,-1);
    const char* jp=p3+1;
    if(jp[0]==0)              g.jpg=NULL;      // never looked up by the firmware
    else if(strcmp(jp,"?")==0) g.jpg="";       // looked up, no art
    else                       g.jpg=arenaDup(jp,-1);
    g_games.push_back(g); added++;
  }
  free(gc);
  if(declFiles>=0 && declFiles!=(long)files.size()){ /* stale pairing — still usable for a bench */ }
  return added;
}

// ---- the bench's own index (written only when no firmware cache existed) --
#define BENCH_INDEX "/.reelbench"
static bool readBenchIndex(){
  size_t n=0; char* b=slurp(String(BENCH_INDEX),&n); if(!b) return false;
  std::vector<char*> lines; splitLines(b,lines);
  long declared=-1; int added=0;
  for(size_t i=0;i<lines.size()&&(int)g_games.size()<MAX_GAMES;i++){
    char* L=lines[i];
    if(strncmp(L,"#N=",3)==0){ declared=atol(L+3); continue; }
    char* p1=strchr(L,'|');    if(!p1) continue;
    char* p2=strchr(p1+1,'|'); if(!p2) continue;
    *p1=0; *p2=0;
    Game g; g.path=arenaDup(L,-1); g.name=arenaDup(p1+1,-1);
    const char* jp=p2+1;
    g.jpg = (jp[0]==0) ? NULL : ((strcmp(jp,"?")==0) ? "" : arenaDup(jp,-1));
    g_games.push_back(g); added++;
  }
  free(b);
  if(declared>=0 && added!=(int)declared){ g_games.clear(); return false; }
  return added>0;
}
static void writeBenchIndex(){
  File f=SD_MMC.open(BENCH_INDEX,FILE_WRITE); if(!f) return;
  f.print("#N="); f.println((int)g_games.size());
  for(size_t i=0;i<g_games.size();i++){
    f.print(g_games[i].path); f.print('|');
    f.print(g_games[i].name); f.print('|');
    f.println(g_games[i].jpg ? (g_games[i].jpg[0]?g_games[i].jpg:"?") : "");
  }
  f.close();
}

// ---- last resort: walk the tree -----------------------------------------
// Progress is driven by ENTRIES EXAMINED, not games added. A directory full of
// .jpg and .nfo files advances the game count not at all, so a games-only
// counter sits frozen and the board looks hung - the identical mistake fixed on
// the JC3248 in 5.9.36-lab6 (scanTick per dirent, not per matched image).
#define SCAN_EVERY_N   10        // entries between screen updates
#define SCAN_FLOOR_MS  2000      // ...or this long, whichever comes first
static uint32_t g_scan_seen=0, g_scan_dirs=0, g_scan_lastseen=0;
static uint32_t g_scan_lastdraw=0, g_scan_t0=0, g_scan_lastlog=0;
static String   g_scan_where="/";

static void scanFrame(){
  g_scan_lastseen=g_scan_seen; g_scan_lastdraw=millis();
  uint32_t el=(millis()-g_scan_t0)/1000UL;
  uint32_t rate = el ? (g_scan_seen/el) : 0;
  bootLine(String("scanning: ")+String((unsigned)g_scan_seen)+" entries  "
           +String((int)g_games.size())+" games  "
           +String((unsigned)g_scan_dirs)+" dirs  "+String((unsigned)el)+"s  "
           +String((unsigned)rate)+"/s",1);
  String w=g_scan_where; if(w.length()>60) w=w.substring(w.length()-60);
  bootLine(w,2);
  tft.display();
  if(g_scan_seen-g_scan_lastlog>=100){
    g_scan_lastlog=g_scan_seen;
    Serial.printf("[scan] %u entries  %d games  %u dirs  %us  %u/s  in %s\n",
      (unsigned)g_scan_seen,(int)g_games.size(),(unsigned)g_scan_dirs,
      (unsigned)el,(unsigned)rate,g_scan_where.c_str());
  }
}
static inline bool scanTick(){
  if(g_scan_seen-g_scan_lastseen>=SCAN_EVERY_N) return true;
  return (millis()-g_scan_lastdraw)>SCAN_FLOOR_MS;
}

static bool isCoverFile(const String& p){
  String e=lowerExt(p);
  return e=="jpg"||e=="jpeg"||e=="png";
}
// Pick the cover whose basename matches `base`, preferring jpg/jpeg over png so
// the choice matches findJPGFor's extension order. Case-insensitive, because
// FAT hands back whatever case the file was written with.
static int matchCover(const std::vector<String>& covers,const String& base){
  String want=base; want.toLowerCase();
  int best=-1, bestRank=99;
  for(size_t i=0;i<covers.size();i++){
    String fn=basenameNoExt(filenameOnly(covers[i])); fn.toLowerCase();
    if(fn!=want) continue;
    String e=lowerExt(covers[i]);
    int rank=(e=="jpg")?0:(e=="jpeg")?1:2;
    if(rank<bestRank){ bestRank=rank; best=(int)i; }
  }
  return best;
}
static std::vector<String> g_root_covers;   // "/" fallback, harvested at root

// COVER HARVESTING: while the loop below is enumerating a directory it already
// sees every .jpg and .png in it. Recording them costs nothing and matching
// them afterwards is a memory compare. The alternative - what the shipping
// firmware does - is to come back later and ask the card up to 24 times per
// game whether a file exists. That second pass costs as much as the walk.
//
// WHY readdir() AND NOT openNextFile()
// ---------------------------------------------------------------------------
// Arduino's File::openNextFile() is not a directory read. Look at what it does
// per entry in vfs_api.cpp: readdir(), then it constructs a VFSFileImpl for the
// result, whose constructor stat()s the path and fopen()s it - and the handle
// is closed again a moment later. So listing a directory OPENS EVERY FILE IN
// IT, one at a time, and on a flat FAT directory of ~1700 entries that is where
// the ~0.3 s per entry goes. Measured here: 94 entries in 27 s.
//
// readdir() does the f_readdir and nothing else. d_type tells directory from
// file without touching the entry - and it is trustworthy on this platform for
// the simple reason that Arduino's own openNextFile() filters on it before
// doing anything else. DT_UNKNOWN falls back to stat(), which is still one
// syscall instead of an open/close pair.
//
// This is not just a bench optimisation: the 577 s cold scan recorded on the 7B
// P4 for 1741 files / 1005 games was openNextFile() doing exactly this.
static void scanDir(const String& sdPath,int depth){
  if((int)g_games.size()>=MAX_GAMES || depth>3) return;
  g_scan_where=sdPath.length()?sdPath:String("/");
  g_scan_dirs++;
  scanFrame();
  String vp="/sdcard"+sdPath;
  DIR* d=opendir(vp.c_str());
  if(!d) return;
  std::vector<String> subdirs, imgs, covers;
  struct dirent* de;
  while((de=readdir(d))){
    g_scan_seen++;
    if(scanTick()) scanFrame();          // every entry counts, matched or not
    const char* nm=de->d_name;
    if(!nm||!nm[0]||nm[0]=='.') continue;
    String child=sdPath+"/"+nm;
    bool isDir;
    if(de->d_type==DT_DIR)       isDir=true;
    else if(de->d_type==DT_REG)  isDir=false;
    else {                                // DT_UNKNOWN - ask, but don't open
      struct stat st; String cv="/sdcard"+child;
      if(stat(cv.c_str(),&st)!=0) continue;
      isDir=S_ISDIR(st.st_mode);
    }
    if(isDir){ subdirs.push_back(child); continue; }
    if(isDiskImage(child))      imgs.push_back(child);
    else if(isCoverFile(child)) covers.push_back(child);
    if((g_scan_seen&0x1F)==0) yield();
  }
  closedir(d);

  if(depth==0) g_root_covers=covers;     // root covers are everyone's fallback

  std::sort(imgs.begin(),imgs.end());    // FAT order is arbitrary; sorting puts
                                         // multi-disk sets next to each other
  for(size_t i=0;i<imgs.size();i++){
    if((int)g_games.size()>=MAX_GAMES) break;
    const String& ip=imgs[i];
    String gb=gameBaseName(ip);
    if(!g_games.empty() && strcmp(g_games.back().name,gb.c_str())==0) continue;   // disk 2,3..
    String base=basenameNoExt(filenameOnly(ip));
    int ci=matchCover(covers,base);
    if(ci<0 && gb!=base)  ci=matchCover(covers,gb);
    const String* pick = (ci>=0) ? &covers[ci] : NULL;
    int ri=-1;
    if(!pick){ ri=matchCover(g_root_covers,base);
               if(ri<0 && gb!=base) ri=matchCover(g_root_covers,gb);
               if(ri>=0) pick=&g_root_covers[ri]; }
    Game g;
    g.path=arenaDupS(ip);
    g.name=arenaDupS(gb);
    g.jpg = pick ? arenaDupS(*pick) : "";   // "" = resolved, genuinely no art
    g_games.push_back(g);
    yield();
  }
  scanFrame();                            // show the games this directory added

  std::sort(subdirs.begin(),subdirs.end());
  for(size_t i=0;i<subdirs.size();i++){
    scanDir(subdirs[i],depth+1);
    if((int)g_games.size()>=MAX_GAMES) break;
  }
  g_scan_where=sdPath.length()?sdPath:String("/");
}

// Lazy, cached, once per game — exactly what the shipping grid does, except the
// shipping grid pays it inside the frame it first draws that tile.
static void coverResolve(int gi){
  if(g_games[gi].jpg) return;
  String j;
  if(findCoverFor(String(g_games[gi].path),j)) g_games[gi].jpg=arenaDupS(j);
  else                                          g_games[gi].jpg="";
}
static bool carHasCover(int gi){ coverResolve(gi); return g_games[gi].jpg && g_games[gi].jpg[0]; }

// ---------------------------------------------------------------------------
// Decoder row callbacks. Both decoders write into the SAME scratch buffer so
// the scale/letterbox tail below is shared.
// ---------------------------------------------------------------------------
static int jpeg_row_cb(JPEGDRAW* pDraw){
  if(!jpeg_tmp_buf) return 0;
  for(int yy=0;yy<pDraw->iHeight;yy++){
    int row=pDraw->y+yy; if(row<0||row>=jpeg_tmp_h) continue;
    int cw=pDraw->iWidth; if(pDraw->x+cw>jpeg_tmp_w) cw=jpeg_tmp_w-pDraw->x;
    if(cw>0) memcpy(&jpeg_tmp_buf[row*jpeg_tmp_w+pDraw->x],&pDraw->pPixels[yy*pDraw->iWidth],cw*2);
  }
  return 1;
}
static int png_row_cb(PNGDRAW* pDraw){
  if(!jpeg_tmp_buf) return 0;
  int row=pDraw->y; if(row<0||row>=jpeg_tmp_h) return 1;
  pngdec.getLineAsRGB565(pDraw,&jpeg_tmp_buf[row*jpeg_tmp_w],PNG_RGB565_LITTLE_ENDIAN,0x00000000);
  return 1;
}

// ============================================================================
// TILE CACHE — byte-identical on-card format to Gotek_7inch (200x200 RGB565
// .tnl under /ADF/.thumbs or /DSK/.thumbs), so tiles built here are reused by
// the shipping firmware and vice versa.
// ============================================================================
static uint16_t* car_buf[CAR_SLOTS]={0};
static int       car_game[CAR_SLOTS];
static uint32_t  car_stamp[CAR_SLOTS];
static uint32_t  car_tick=0;
static bool      car_slots_init=false;
// bench counters
static uint32_t g_tile_hits=0, g_tile_loads=0, g_tile_decodes=0, g_tile_fail=0;
// A .tnl thumb is 80,000 bytes. If fetching one is slow, it is either the
// stat+open (a FAT directory scan, O(number of thumbs already in .thumbs)) or
// the read itself (SDMMC cannot DMA straight into PSRAM, so it bounces through
// internal RAM in chunks). Those two want completely different fixes - shard
// the directory vs shrink the tile - so they are timed apart.
static uint32_t t_topen=0, t_tread=0;

// Touch has to be SAMPLED far more often than once a frame. A frame here is
// 163 ms at best and 2.2 s while thumbs load, so a 150 ms tap falls clean
// between two polls and is never seen - which looks exactly like broken touch.
// touchPump() is cheap (one I2C read), gets called from several points inside
// the frame, and LATCHES the result. The tap is acted on at the END of the
// frame, never mid-draw, so a mode change cannot land halfway through a render.
// Release is time-based (60 ms) rather than a frame count, so it behaves the
// same whether the frame took 50 ms or 2 s.
static bool g_t_down=false; static int g_t_x=0,g_t_y=0;
static uint32_t g_t_lastseen=0;
static bool g_tap_pending=false; static int g_tap_x=0,g_tap_y=0;
static int  g_tap_count=0, g_raw_x=-1, g_raw_y=-1;
#define RELEASE_MS 60
static void touchPump();        // fwd: called from inside the render loops

static void carInit(){
  if(car_slots_init) return;
  for(int i=0;i<CAR_SLOTS;i++){ car_buf[i]=NULL; car_game[i]=-1; car_stamp[i]=0; }
  car_slots_init=true;
}
static String carThumbRoot(int gi){
  return (strncmp(g_games[gi].path,"/DSK",4)==0) ? String("/DSK/.thumbs") : String("/ADF/.thumbs");
}
// ---------------------------------------------------------------------------
// WHY THUMB FILENAMES AND FOLDERS CHANGED
//
// Measured on this board, per 80,000-byte thumb:   open 272 ms,  read 28 ms.
// The read is fine - 2.9 MB/s is exactly what 1-bit SDMMC gives. The open is
// 90% of the cost, and it is a FAT directory scan of a single flat .thumbs
// folder. Two things made it quadratic:
//
//  1. carLoadThumb stat()ed the file to check its size and THEN opened it.
//     Two full directory scans per thumb. f.size() on the open handle gives
//     the same answer for free.
//  2. Names like "Conflict Middle East Political Simulator_A3F2.tnl" do not
//     fit 8.3, so VFAT spends 6-7 directory entries on each one. 1000 thumbs
//     in one folder is then ~200 KB of directory to walk, per open.
//
// So: an 8-hex FNV-1a name (8.3 clean, ONE directory entry) inside a shard
// folder picked from the top byte of the same hash - 256 shards, ~4 files
// each. The legacy path is still read as a fallback and migrated on the way
// past, so nothing has to be rebuilt and it gets faster as you browse.
//
// This matters well beyond the bench: the JC3248 uses the identical flat
// .thumbs layout with the same long names, and has the same wall waiting at
// 1000 games.
// ---------------------------------------------------------------------------
static uint32_t carHash(int gi){
  const char* p=g_games[gi].path;
  uint32_t h=2166136261u;                       // FNV-1a
  for(const char* q=p; *q; q++){ h^=(uint8_t)(*q); h*=16777619u; }
  return h;
}
static String carShardDir(int gi){
  char b[4]; snprintf(b,sizeof(b),"%02X",(unsigned)(carHash(gi)>>24));
  return carThumbRoot(gi)+"/"+b;
}
static String carThumbFast(int gi){
  char b[16]; snprintf(b,sizeof(b),"%08X.tnl",(unsigned)carHash(gi));
  return carShardDir(gi)+"/"+b;
}
// legacy flat path - read-only now, kept so existing thumbs are not wasted
static String carThumbPath(int gi){
  const char* p=g_games[gi].path;
  uint32_t h=5381; for(const char* q=p; *q; q++) h=((h<<5)+h)^(uint8_t)(*q);        // djb2-xor
  char hx[6]; snprintf(hx,sizeof(hx),"%04X",(unsigned)(h&0xFFFF));
  return carThumbRoot(gi)+"/"+gameBaseName(String(p))+"_"+hx+".tnl";
}

static uint32_t g_tnl_fast=0, g_tnl_legacy=0;
// ONE directory scan: open, then ask the handle for the size. The stat() that
// used to precede this doubled the cost of the single most expensive operation
// in the whole browse path.
static bool carReadTnl(const String& path,uint16_t* dst){
  uint32_t o0=micros();
  File f=SD_MMC.open(path.c_str(),"r");
  t_topen+=micros()-o0;
  if(!f) return false;
  size_t want=(size_t)CAR_TILE*CAR_TILE*2;
  if((size_t)f.size()!=want){ f.close(); return false; }
  uint32_t r0=micros();
  size_t got=f.read((uint8_t*)dst,want);
  f.close();
  t_tread+=micros()-r0;
  return got==want;
}
static void carSaveThumb(int gi,uint16_t* src);        // fwd: used to migrate
static bool carLoadThumb(int gi,uint16_t* dst){
  if(!carHasCover(gi)) return false;
  if(carReadTnl(carThumbFast(gi),dst)){ g_tnl_fast++; return true; }
  if(carReadTnl(carThumbPath(gi),dst)){                // legacy flat layout
    g_tnl_legacy++;
    carSaveThumb(gi,dst);                              // migrate as we go past
    return true;
  }
  return false;
}
// 256 shard folders. SD_MMC.exists() on each would itself be a scan of the big
// parent, so instead remember which ones this boot has already mkdir'd - mkdir
// on an existing directory just fails, harmlessly.
static uint8_t g_shard_made[32]={0};
static bool    g_thumbroot_made=false;
static void carSaveThumb(int gi,uint16_t* src){
  if(!carHasCover(gi)) return;
  if(!g_thumbroot_made){ SD_MMC.mkdir(carThumbRoot(gi).c_str()); g_thumbroot_made=true; }
  unsigned sh=(unsigned)(carHash(gi)>>24);
  if(!(g_shard_made[sh>>3]&(1u<<(sh&7)))){
    SD_MMC.mkdir(carShardDir(gi).c_str());
    g_shard_made[sh>>3]|=(uint8_t)(1u<<(sh&7));
  }
  File f=SD_MMC.open(carThumbFast(gi).c_str(),FILE_WRITE); if(!f) return;
  f.write((uint8_t*)src,(size_t)CAR_TILE*CAR_TILE*2); f.close();
}
// Decode a cover into a CAR_TILE x CAR_TILE tile (aspect fit, C_BAR letterbox).
static bool carDecodeTile(int gi,uint16_t* dst){
  for(int i=0;i<CAR_TILE*CAR_TILE;i++) dst[i]=C_BAR;
  if(!carHasCover(gi)) return false;
  String cp=String(g_games[gi].jpg);
  struct stat st; String v="/sdcard"+cp;
  if(stat(v.c_str(),&st)!=0||st.st_size==0||st.st_size>2000000) return false;
  size_t sz=(size_t)st.st_size;
  File f=SD_MMC.open(cp.c_str(),"r"); if(!f) return false;
  uint8_t* buf=(uint8_t*)ps_malloc(sz); if(!buf){ f.close(); return false; }
  f.read(buf,sz); f.close();
  int djw=0,djh=0;
  if(coverIsPng(cp)){
    if(pngdec.openRAM(buf,sz,png_row_cb)!=PNG_SUCCESS){ free(buf); return false; }
    int jw=pngdec.getWidth(), jh=pngdec.getHeight();
    if(jw<=0||jh<=0||jw>2000||jh>2000){ pngdec.close(); free(buf); return false; }
    djw=jw; djh=jh;                               // PNGdec has no downscale
    jpeg_tmp_buf=(uint16_t*)ps_malloc((size_t)djw*djh*2);
    if(!jpeg_tmp_buf){ pngdec.close(); free(buf); return false; }
    memset(jpeg_tmp_buf,0,(size_t)djw*djh*2); jpeg_tmp_w=djw; jpeg_tmp_h=djh;
    pngdec.decode(NULL,0); pngdec.close(); free(buf);
  } else {
    if(!jpegdec.openRAM(buf,sz,jpeg_row_cb)){ free(buf); return false; }
    jpegdec.setPixelType(RGB565_LITTLE_ENDIAN);   // 7" framebuffer is native LE
    int jw=jpegdec.getWidth(), jh=jpegdec.getHeight();
    if(jw<=0||jh<=0||jw>2000||jh>2000){ jpegdec.close(); free(buf); return false; }
    int opt=0,div=1;                              // JPEGDEC's free 1/2, 1/4, 1/8 downscale
    if(jw>=CAR_TILE*8&&jh>=CAR_TILE*8){ opt=JPEG_SCALE_EIGHTH; div=8; }
    else if(jw>=CAR_TILE*4&&jh>=CAR_TILE*4){ opt=JPEG_SCALE_QUARTER; div=4; }
    else if(jw>=CAR_TILE*2&&jh>=CAR_TILE*2){ opt=JPEG_SCALE_HALF;  div=2; }
    djw=jw/div; djh=jh/div;
    jpeg_tmp_buf=(uint16_t*)ps_malloc((size_t)djw*djh*2);
    if(!jpeg_tmp_buf){ jpegdec.close(); free(buf); return false; }
    memset(jpeg_tmp_buf,0,(size_t)djw*djh*2); jpeg_tmp_w=djw; jpeg_tmp_h=djh;
    jpegdec.decode(0,0,opt); jpegdec.close(); free(buf);
  }
  float sc=min((float)CAR_TILE/djw,(float)CAR_TILE/djh); if(sc>1.0f) sc=1.0f;
  int dw=(int)(djw*sc), dh=(int)(djh*sc);
  int ox=(CAR_TILE-dw)/2, oy=(CAR_TILE-dh)/2;
  for(int r=0;r<dh;r++){
    int sy=(int)(r/sc); if(sy>=djh) sy=djh-1;
    for(int c=0;c<dw;c++){ int sx=(int)(c/sc); if(sx>=djw) sx=djw-1;
      dst[(oy+r)*CAR_TILE+(ox+c)]=jpeg_tmp_buf[sy*djw+sx]; }
    if((r%20)==0) yield();
  }
  free(jpeg_tmp_buf); jpeg_tmp_buf=NULL; return true;
}
// Fetch a tile.
//   mayLoad   — allowed to pull the 80 KB .tnl thumb off the card (~10-20 ms)
//   mayDecode — allowed to fall back to a full JPEG/PNG decode (~200 ms+)
// Both false = cache-only: returns NULL rather than stalling the frame. The reel
// uses this to spend its budget on the centre cover and never on the edges.
// okOut is a plain out-param on purpose: a user-defined type in a free function's
// signature inside an .ino trips Arduino's auto-prototype generator.
static uint16_t* carTileEx(int gi,bool mayLoad,bool mayDecode,bool* okOut){
  carInit();
  if(okOut)*okOut=false;
  if(gi<0||gi>=(int)g_games.size()) return NULL;
  for(int s=0;s<CAR_SLOTS;s++) if(car_buf[s]&&car_game[s]==gi){
    car_stamp[s]=++car_tick; g_tile_hits++; if(okOut)*okOut=true; return car_buf[s]; }
  if(!mayLoad&&!mayDecode) return NULL;
  int slot=-1; uint32_t oldest=0xFFFFFFFFu;
  for(int s=0;s<CAR_SLOTS;s++){
    if(!car_buf[s]){
      car_buf[s]=(uint16_t*)ps_malloc((size_t)CAR_TILE*CAR_TILE*2);
      if(!car_buf[s]) continue;
      car_game[s]=-1; car_stamp[s]=0;
    }
    if(car_game[s]<0){ slot=s; break; }
    if(car_stamp[s]<oldest){ oldest=car_stamp[s]; slot=s; }
  }
  if(slot<0) return NULL;
  car_game[slot]=gi; car_stamp[slot]=++car_tick;
  bool good=false;
  if(mayLoad&&carLoadThumb(gi,car_buf[slot])){ g_tile_loads++; good=true; }
  else if(mayDecode&&carDecodeTile(gi,car_buf[slot])){ g_tile_decodes++; carSaveThumb(gi,car_buf[slot]); good=true; }
  else {
    g_tile_fail++;
    for(int i=0;i<CAR_TILE*CAR_TILE;i++) car_buf[slot][i]=C_BAR;
    if(!mayDecode) car_game[slot]=-1;      // don't poison the slot with an empty tile
  }
  if(okOut)*okOut=good;
  return car_buf[slot];
}

// ============================================================================
// BLITTERS
// ----------------------------------------------------------------------------
// blitTileSlow() is the blitter Gotek_7inch ships today: one drawPixel() call
// per destination pixel, each doing a clip test and two integer divides.
//
// blitTileFast() is the lab3 form proven on the JC3248, adapted for the 7":
//   * the source-column map is built ONCE per call (w entries, not w*h)
//   * the source row index is computed ONCE per destination row
//   * clipping happens once per row, not once per pixel
//   * the inner loop writes through a raw row pointer into the compose buffer
// On this board the compose buffer is row-major with X contiguous, so the
// inner loop is already walking memory the right way — unlike the JC3248,
// where the rotation made consecutive writes 640 bytes apart.
// ============================================================================
static inline uint16_t carDim(uint16_t c,int lvl){
  if(lvl<=0) return c;
  if(lvl==1) return (uint16_t)((c>>1)&0x7BEF);   // ~50%
  return (uint16_t)((c>>2)&0x39E7);              // ~25%
}
static int g_bl_sx[KLCD_W];

static void blitTileSlow(uint16_t* tile,int cx,int cy,int w,int h,int dim){
  int x0=cx-w/2, y0=cy-h/2;
  for(int dy=0;dy<h;dy++){ int sy=dy*CAR_TILE/h;
    for(int dx=0;dx<w;dx++){ int sx=dx*CAR_TILE/w;
      UG->drawPixel(x0+dx,y0+dy,carDim(tile?tile[sy*CAR_TILE+sx]:C_BAR,dim)); } }
}
static void blitTileFast(uint16_t* tile,int cx,int cy,int w,int h,int dim){
  if(w<=0||h<=0||!tft.draw) return;
  int x0=cx-w/2, y0=cy-h/2;
  int bx0=max(tft.clx0,x0), bx1=min(tft.clx1,x0+w);
  int by0=max(tft.cly0,y0), by1=min(tft.cly1,y0+h);
  if(bx0>=bx1||by0>=by1) return;
  int cw=bx1-bx0;
  for(int i=0,dx=bx0-x0; i<cw; i++,dx++) g_bl_sx[i]=(dx*CAR_TILE)/w;   // once per call
  for(int y=by0;y<by1;y++){
    int sy=((y-y0)*CAR_TILE)/h;                                        // once per row
    uint16_t* drow=tft.draw+(size_t)y*KLCD_W+bx0;
    if(!tile){ uint16_t c=carDim(C_BAR,dim); for(int i=0;i<cw;i++) drow[i]=c; continue; }
    const uint16_t* srow=tile+(size_t)sy*CAR_TILE;
    if(dim<=0)       for(int i=0;i<cw;i++) drow[i]=srow[g_bl_sx[i]];
    else if(dim==1)  for(int i=0;i<cw;i++) drow[i]=(uint16_t)((srow[g_bl_sx[i]]>>1)&0x7BEF);
    else             for(int i=0;i<cw;i++) drow[i]=(uint16_t)((srow[g_bl_sx[i]]>>2)&0x39E7);
  }
  tft.markDirty(bx0,by0,bx1,by1);
}

// The shipping per-tile decode path, kept verbatim in spirit so GRID-OLD is a
// fair measurement of what the firmware does today.
static void drawCoverFitDecode(const String& path,int boxX,int boxY,int maxW,int maxH){
  struct stat st; String v="/sdcard"+path;
  if(stat(v.c_str(),&st)!=0||st.st_size==0||st.st_size>2000000) return;
  size_t sz=(size_t)st.st_size;
  File f=SD_MMC.open(path.c_str(),"r"); if(!f) return;
  uint8_t* buf=(uint8_t*)ps_malloc(sz); if(!buf){ f.close(); return; }
  f.read(buf,sz); f.close();
  int jw=0,jh=0;
  if(coverIsPng(path)){
    if(pngdec.openRAM(buf,sz,png_row_cb)!=PNG_SUCCESS){ free(buf); return; }
    jw=pngdec.getWidth(); jh=pngdec.getHeight();
    if(jw<=0||jh<=0||jw>2000||jh>2000){ pngdec.close(); free(buf); return; }
    jpeg_tmp_buf=(uint16_t*)ps_malloc((size_t)jw*jh*2);
    if(!jpeg_tmp_buf){ pngdec.close(); free(buf); return; }
    memset(jpeg_tmp_buf,0,(size_t)jw*jh*2); jpeg_tmp_w=jw; jpeg_tmp_h=jh;
    pngdec.decode(NULL,0); pngdec.close(); free(buf);
  } else {
    if(!jpegdec.openRAM(buf,sz,jpeg_row_cb)){ free(buf); return; }
    jpegdec.setPixelType(RGB565_LITTLE_ENDIAN);
    jw=jpegdec.getWidth(); jh=jpegdec.getHeight();
    if(jw<=0||jh<=0||jw>2000||jh>2000){ jpegdec.close(); free(buf); return; }
    jpeg_tmp_buf=(uint16_t*)ps_malloc((size_t)jw*jh*2);
    if(!jpeg_tmp_buf){ jpegdec.close(); free(buf); return; }
    memset(jpeg_tmp_buf,0,(size_t)jw*jh*2); jpeg_tmp_w=jw; jpeg_tmp_h=jh;
    jpegdec.decode(0,0,0); jpegdec.close(); free(buf);   // NOTE: no downscale, exactly as shipped
  }
  float sc=min((float)maxW/jw,(float)maxH/jh); if(sc>1.0f) sc=1.0f;
  int dw=(int)(jw*sc), dh=(int)(jh*sc);
  if(dw<=0||dh<=0){ free(jpeg_tmp_buf); jpeg_tmp_buf=NULL; return; }
  int ox=boxX+(maxW-dw)/2, oy=boxY+(maxH-dh)/2;
  if(sc>=0.999f){ UG->pushImage(ox,oy,jw,jh,jpeg_tmp_buf); }
  else {
    uint16_t* rowbuf=(uint16_t*)malloc((size_t)dw*2);
    if(rowbuf){
      for(int r=0;r<dh;r++){
        int sy=(int)(r/sc); if(sy>=jh) sy=jh-1;
        for(int c=0;c<dw;c++){ int sx=(int)(c/sc); if(sx>=jw) sx=jw-1; rowbuf[c]=jpeg_tmp_buf[sy*jw+sx]; }
        UG->pushImage(ox,oy+r,dw,1,rowbuf);
        if((r%20)==0) yield();
      }
      free(rowbuf);
    }
  }
  free(jpeg_tmp_buf); jpeg_tmp_buf=NULL;
}

// ============================================================================
// BENCH STATE + HUD
// ============================================================================
#define MODE_GRID_OLD  0   // per-tile JPEG/PNG decode  — what Gotek_7inch ships
#define MODE_GRID_SLOW 1   // tile cache + the old per-pixel blitter
#define MODE_GRID_FAST 2   // tile cache + the lab3 row-pointer blitter
#define MODE_REEL      3   // moving 5-up coverflow, tile cache + fast blitter
#define MODE_REEL_LEAN 4   // same, but drawing ~40% fewer pixels per frame
#define MODE_COUNT     5
static int      g_mode=MODE_GRID_FAST;
static int      g_page=0;
static float    g_reel_pos=0.0f;      // fractional index into g_games
static float    g_reel_spd=3.0f;      // games per second
static uint32_t g_last_us=0;

// per-frame timers (microseconds)
static uint32_t t_fill=0, t_tiles=0, t_blit=0, t_text=0, t_total=0;
// rolling window
#define WIN 30
static uint32_t win_total[WIN]; static int win_n=0, win_i=0;
static uint32_t g_frames=0;
// The status and button bars never change between frames. Repainting them every
// frame would dirty rows 0..26 and 428..480 and make the DIRTY flush copy the
// whole screen anyway - which would measure nothing. So they repaint on demand.
//
// It is a COUNT, not a flag, because DIRECT mode has no compose buffer: anything
// drawn once lands in one framebuffer only and then flickers on alternate
// frames. Two consecutive paints put it in both. Harmless in the other modes.
static int g_repaint=2;

static void winPush(uint32_t v){ win_total[win_i]=v; win_i=(win_i+1)%WIN; if(win_n<WIN) win_n++; }
static uint32_t winAvg(){ if(!win_n) return 0; uint64_t s=0; for(int i=0;i<win_n;i++) s+=win_total[i]; return (uint32_t)(s/win_n); }
static uint32_t winMax(){ uint32_t m=0; for(int i=0;i<win_n;i++) if(win_total[i]>m) m=win_total[i]; return m; }
static uint32_t winMin(){ if(!win_n) return 0; uint32_t m=0xFFFFFFFFu; for(int i=0;i<win_n;i++) if(win_total[i]<m) m=win_total[i]; return m; }

static const char* modeName(){
  switch(g_mode){
    case MODE_GRID_OLD:  return "1 GRID-OLD  decode per tile";
    case MODE_GRID_SLOW: return "2 GRID-TILE slow blit";
    case MODE_GRID_FAST: return "3 GRID-TILE fast blit";
    case MODE_REEL:      return "4 REEL  5-up, full band";
    default:             return "5 REEL-LEAN  3-up, tight band";
  }
}
static String ms1(uint32_t us){                 // microseconds -> "12.3" ms
  uint32_t t=(us+50)/100; return String(t/10)+"."+String(t%10);
}

static void bootLine(const String& s,int line){      // draw only, no flush
  if(!tft.ok) return;
  UG->fillRect(0,100+line*22,KLCD_W,22,C_BG);
  UG->setFont(&lgfx::fonts::DejaVu12); UG->setTextColor(C_LIT,C_BG);
  UG->setCursor(20,100+line*22); UG->print(s);
}
static void bootMsg(const String& s,int line){ bootLine(s,line); tft.display(); }

static void drawStatusBar(){
  UG->fillRect(0,0,KLCD_W,STATUS_H,C_BAR);
  UG->drawFastHLine(0,STATUS_H-1,KLCD_W,C_SEP);
  UG->setFont(&lgfx::fonts::DejaVu12);
  UG->setTextColor(C_ORANGE,C_BAR); UG->setCursor(8,7); UG->print("GTi 7\" REEL TEST");
  UG->setTextColor(C_LIT,C_BAR);
  String m=String(modeName());
  UG->setCursor((KLCD_W-UG->textWidth(m))/2,7); UG->print(m);
  uint16_t fc = (tft.flushMode==0)?C_AMBER:(tft.flushMode==1)?C_GREEN:C_BLUE;
  UG->setTextColor(fc,C_BAR);
  String fl = (tft.flushMode==0)?String("FLUSH: FULL 384kpx")
            : (tft.flushMode==1)?String("FLUSH: DIRTY RECT")
            :                    String("FLUSH: DIRECT (no compose)");
  UG->setCursor(KLCD_W-8-UG->textWidth(fl),7); UG->print(fl);
}

static void drawHud(){
  int y=KLCD_H-BOTTOM_H-42;
  UG->fillRect(0,y,KLCD_W,42,C_BLACK);
  UG->drawFastHLine(0,y,KLCD_W,C_SEP);
  UG->setFont(&lgfx::fonts::DejaVu9); UG->setTextColor(C_LIT,C_BLACK);
  String l1="fill "+ms1(t_fill)+"  tiles "+ms1(t_tiles)
           +" (open "+ms1(t_topen)+" read "+ms1(t_tread)+")  blit "+ms1(t_blit)
           +"  text "+ms1(t_text)+"  copy "+ms1(tft.t_copy_us)+" ("+String((int)(tft.n_copy_px/1000))+"kpx)  vsync "+ms1(tft.t_vsync_us);
  UG->setCursor(8,y+7); UG->print(l1);
  uint32_t av=winAvg();
  String l2="frame "+ms1(t_total)+"ms   avg "+ms1(av)+"   min "+ms1(winMin())+"   max "+ms1(winMax())
           +"   FPS "+String(av?(int)(1000000UL/av):0)
           +"   hit "+String((int)g_tile_hits)+" ld "+String((int)g_tile_loads)
           +" dec "+String((int)g_tile_decodes)
           +"   tnl fast "+String((int)g_tnl_fast)+" old "+String((int)g_tnl_legacy)
           +"   TOUCH "+String(g_tap_count);
  UG->setTextColor(C_AMBER,C_BLACK);
  UG->setCursor(8,y+24); UG->print(l2);
}

static void drawBottomBar(){
  int y=KLCD_H-BOTTOM_H;
  UG->fillRect(0,y,KLCD_W,BOTTOM_H,C_BAR);
  UG->drawFastHLine(0,y,KLCD_W,C_SEP);
  int q=KLCD_W/4, pad=6, rad=10, rw=q-2*pad, rh=BOTTOM_H-20, ry=y+5;
  const char* lbl[4]={"MODE","FLUSH","<","> "};
  uint16_t col[4]={C_ORANGE,C_BLUE,C_AMBER,C_GREEN};
  for(int i=0;i<4;i++){
    int rx=i*q+pad; uint16_t c=col[i], d=(uint16_t)((c>>2)&0x39E7);
    UG->fillRoundRect(rx,ry,rw,rh,rad,d);
    UG->drawRoundRect(rx,ry,rw,rh,rad,c);
    UG->setFont(&lgfx::fonts::DejaVu12); UG->setTextColor(c,d);
    String s=String(lbl[i]);
    if(i==2) s=(g_mode>=MODE_REEL)?String("SLOWER"):String("< PREV");
    if(i==3) s=(g_mode>=MODE_REEL)?String("FASTER"):String("NEXT >");
    UG->setCursor(rx+(rw-UG->textWidth(s))/2, ry+(rh-12)/2); UG->print(s);
  }
}

// --- one 3x2 grid page -----------------------------------------------------
static void renderGrid(int how){   // how: 0=decode per tile, 1=tile+slow blit, 2=tile+fast blit
  int n=(int)g_games.size();
  int pages=(n+GRID_PER-1)/GRID_PER; if(pages<1) pages=1;
  if(g_page>=pages) g_page=0; if(g_page<0) g_page=pages-1;
  int top=STATUS_H, bot=KLCD_H-BOTTOM_H-42;
  int gridH=bot-top, cellW=KLCD_W/GRID_COLS, cellH=gridH/GRID_ROWS;

  uint32_t a=micros();
  UG->fillRect(0,top,KLCD_W,gridH,C_BG);
  t_fill=micros()-a;
  t_tiles=0; t_blit=0;

  for(int c=0;c<GRID_PER;c++){
    int li=g_page*GRID_PER+c; if(li>=n) break;
    int gcol=c%GRID_COLS, grow=c/GRID_COLS;
    int cx=gcol*cellW+cellW/2, cyTop=top+grow*cellH;
    int box=cellW-18, boxH=cellH-40; if(box>boxH) box=boxH;
    int cyImg=cyTop+6+box/2;
    UG->fillRect(cx-box/2,cyImg-box/2,box,box,C_BAR);
    if(how>0){
      uint32_t t0=micros();
      bool ok=false; uint16_t* tile=carTileEx(li,true,true,&ok);
      t_tiles+=micros()-t0;
      t0=micros();
      if(tile&&ok){
        if(how==1) blitTileSlow(tile,cx,cyImg,box,box,0);
        else       blitTileFast(tile,cx,cyImg,box,box,0);
      }
      t_blit+=micros()-t0;
    } else {
      uint32_t t0=micros();
      if(carHasCover(li))
        drawCoverFitDecode(String(g_games[li].jpg),cx-box/2,cyImg-box/2,box,box);
      t_tiles+=micros()-t0;    // the decode IS the tile fetch on this path
    }
    touchPump();
    UG->drawRect(cx-box/2-1,cyImg-box/2-1,box+2,box+2,C_ACC);
    uint32_t t0=micros();
    UG->setFont(&lgfx::fonts::DejaVu12); UG->setTextColor(C_LIT,C_BG);
    String nm=String(g_games[li].name);
    int budget=cellW-10;
    while(UG->textWidth(nm)>budget&&nm.length()>2) nm=nm.substring(0,nm.length()-1);
    UG->setCursor(cx-UG->textWidth(nm)/2, cyImg+box/2+8); UG->print(nm);
    t_text+=micros()-t0;
  }
  UG->setFont(&lgfx::fonts::DejaVu9); UG->setTextColor(C_DIM,C_BG);
  String pg=String(g_page+1)+"/"+String(pages)+"  ("+String(n)+" games)";
  UG->setCursor(KLCD_W-10-UG->textWidth(pg),top+6); UG->print(pg);
}

// --- one moving-reel frame -------------------------------------------------
// Two geometries, because the bottleneck turned out to be PSRAM bandwidth and
// the only lever left is drawing fewer pixels.
//
//   full : 5 covers, 236 px centre, the whole band cleared every frame
//          = 268,800 fill + ~140,000 blit = ~409,000 pixel writes
//   lean : 3 covers, 200 px centre, and only the rows a cover can occupy are
//          cleared (plus the title strip) - the covers overlap horizontally, so
//          clearing the full band was mostly painting under opaque artwork
//          = ~168,000 fill + ~87,000 blit = ~255,000 pixel writes
//
// Combined with DIRECT flush (which removes the 768,000-byte copy outright),
// lean is the configuration that decides whether a moving reel is possible on
// this panel at all.
static void renderReel(float dt,bool lean){
  int n=(int)g_games.size(); if(n<=0) return;
  g_reel_pos+=g_reel_spd*dt;
  while(g_reel_pos>=n) g_reel_pos-=n;
  while(g_reel_pos<0)  g_reel_pos+=n;

  const int SPACING = lean?206:168;
  const int BIG     = lean?200:236;
  const int STEP    = 46;
  const int MINSZ   = lean?116:112;
  const int VIS     = lean?3:5;

  int top=STATUS_H, bot=KLCD_H-BOTTOM_H-42;
  int bandH=bot-top, cyc=top+bandH/2;

  uint32_t a=micros();
  if(lean){
    int fy0=cyc-BIG/2-3, fy1=cyc+BIG/2+3;          // only where a cover can land
    if(fy0<top) fy0=top; if(fy1>bot-28) fy1=bot-28;
    if(fy1>fy0) UG->fillRect(0,fy0,KLCD_W,fy1-fy0,C_BG);
    UG->fillRect(0,bot-28,KLCD_W,28,C_BG);         // title strip
  } else {
    UG->fillRect(0,top,KLCD_W,bandH,C_BG);         // the whole band, as measured
  }
  t_fill=micros()-a;
  t_tiles=0; t_blit=0;

  int   base=(int)floorf(g_reel_pos);
  float frac=g_reel_pos-base;
  int half=VIS/2;
  // furthest first so the centre cover lands on top
  for(int pass=half; pass>=0; pass--){
    for(int sgn=-1; sgn<=1; sgn+=2){
      int k=pass*sgn; if(pass==0&&sgn>0) continue;
      int idx=base+k; idx%=n; if(idx<0) idx+=n;
      float d=fabsf((float)k-frac);
      int sz=(int)(BIG-STEP*d); if(sz<MINSZ) sz=MINSZ;
      int cx=KLCD_W/2+(int)(((float)k-frac)*SPACING);
      if(cx<-sz||cx>KLCD_W+sz) continue;
      int dim=(d<0.60f)?0:(d<1.60f?1:2);
      uint32_t t0=micros();
      bool ok=false;
      // thumbs for the near covers, a full decode only for the one in the middle
      uint16_t* tile=carTileEx(idx, d<1.60f, d<0.60f, &ok);
      t_tiles+=micros()-t0;
      t0=micros();
      blitTileFast(tile,cx,cyc,sz,sz,dim);
      t_blit+=micros()-t0;
      touchPump();
      if(d<0.60f) UG->drawRect(cx-sz/2-1,cyc-sz/2-1,sz+2,sz+2,C_GREEN);
    }
  }
  uint32_t t0=micros();
  UG->setFont(&lgfx::fonts::DejaVu18); UG->setTextColor(C_LIT,C_BG);
  String nm=String(g_games[base].name);
  while(UG->textWidth(nm)>KLCD_W-40&&nm.length()>2) nm=nm.substring(0,nm.length()-1);
  UG->setCursor((KLCD_W-UG->textWidth(nm))/2, bot-24); UG->print(nm);
  UG->setFont(&lgfx::fonts::DejaVu9); UG->setTextColor(C_DIM,C_BG);
  String sp=String("speed ")+ms1((uint32_t)(g_reel_spd*1000.0f))+" games/s   "
           +String(base+1)+"/"+String(n);
  UG->setCursor(KLCD_W-10-UG->textWidth(sp),top+6); UG->print(sp);
  t_text=micros()-t0;
}

// ============================================================================
// TOUCH (rising-edge tap on the bottom bar)
// ============================================================================

static void resetCounters(){
  g_tile_hits=g_tile_loads=g_tile_decodes=g_tile_fail=0;
  win_n=0; win_i=0; g_frames=0;
}
static void handleTap(int x,int y){
  if(y<KLCD_H-BOTTOM_H){ resetCounters(); g_repaint=2; return; }   // tap above the bar = zero the stats
  int q=KLCD_W/4;
  if(x<q){ g_mode=(g_mode+1)%MODE_COUNT; g_repaint=2; resetCounters(); }
  else if(x<2*q){ tft.setFlushMode((uint8_t)((tft.flushMode+1)%3)); g_repaint=2; resetCounters(); }
  else if(x<3*q){ if(g_mode>=MODE_REEL){ g_reel_spd-=0.5f; if(g_reel_spd<0.5f) g_reel_spd=0.5f; } else g_page--; }
  else          { if(g_mode>=MODE_REEL){ g_reel_spd+=0.5f; if(g_reel_spd>12.0f) g_reel_spd=12.0f; } else g_page++; }
}
static void touchPump(){
  int32_t rx,ry;
  uint32_t now=millis();
  if(tft.getTouch(&rx,&ry)){
    g_raw_x=(int)rx; g_raw_y=(int)ry;
    if(!g_t_down){ g_t_down=true; g_t_x=(int)rx; g_t_y=(int)ry; }
    g_t_lastseen=now;
    return;
  }
  if(g_t_down && (now-g_t_lastseen)>RELEASE_MS){
    g_t_down=false;
    g_tap_pending=true; g_tap_x=g_t_x; g_tap_y=g_t_y; g_tap_count++;
    Serial.printf("[touch] tap %d at %d,%d  (bar starts at y=%d)\n",
      g_tap_count,g_tap_x,g_tap_y,KLCD_H-BOTTOM_H);
  }
}

// ============================================================================
// SETUP / LOOP
// ============================================================================
static void logHeader(){
  Serial.println();
  Serial.println("==== GTi 7\" REEL TEST " BENCH_VERSION " ====");
  Serial.printf("panel %dx%d  compose %u KB  tile %d px  slots %d (%u KB)\n",
    KLCD_W,KLCD_H,(unsigned)((size_t)KLCD_W*KLCD_H*2/1024),CAR_TILE,CAR_SLOTS,
    (unsigned)((size_t)CAR_TILE*CAR_TILE*2*CAR_SLOTS/1024));
  Serial.printf("PSRAM free %u KB  heap free %u KB\n",
    (unsigned)(ESP.getFreePsram()/1024),(unsigned)(ESP.getFreeHeap()/1024));
}

void setup(){
  Serial.begin(115200); delay(300);

  // ---- PSRAM gate -------------------------------------------------------
  // The RGB driver takes its two 800x480x16 framebuffers (1,536,000 bytes)
  // from PSRAM and nowhere else, so with PSRAM off the panel never comes up
  // and the only symptom is an IDF line buried in the boot log:
  //     lcd_rgb_panel_alloc_frame_buffers(181): no mem for frame buffer
  // There is no screen to report that on, so say it here, loudly, forever.
  // Note OPI specifically: this board is N16R8 (octal). Selecting "QSPI PSRAM"
  // leaves the chip uninitialised and looks identical to Disabled.
  size_t psz=ESP.getPsramSize();
  if(psz<2*1024*1024){
    while(true){
      Serial.println();
      Serial.printf("*** PSRAM NOT AVAILABLE (%u bytes) ***\n",(unsigned)psz);
      Serial.println("*** Tools > PSRAM > OPI PSRAM, then recompile.        ***");
      Serial.println("*** A new sketch folder does NOT inherit the board    ***");
      Serial.println("*** settings you use for Gotek_7inch.                 ***");
      Serial.println("*** Also check: Flash Size 16MB, Flash Mode QIO 120MHz ***");
      delay(3000);
    }
  }

  initExpander();
  if(!tft.init()){                                 // panel dead - no screen to say it on
    while(true){
      Serial.println("*** PANEL INIT FAILED (esp_lcd_new_rgb_panel) ***");
      Serial.printf("    PSRAM total %u KB, free %u KB\n",
        (unsigned)(ESP.getPsramSize()/1024),(unsigned)(ESP.getFreePsram()/1024));
      delay(3000);
    }
  }
  logHeader();
  Serial.printf("[touch] GT911 at 0x%02X\n",(unsigned)tft.gtAddr);

  UG->fillScreen(C_BG);
  drawStatusBar();
  UG->setFont(&lgfx::fonts::DejaVu18); UG->setTextColor(C_ORANGE,C_BG);
  UG->setCursor(20,50); UG->print("GTi 7\" REEL TEST");
  UG->setFont(&lgfx::fonts::DejaVu12); UG->setTextColor(C_DIM,C_BG);
  UG->setCursor(20,78); UG->print(BENCH_VERSION);
  tft.display();

  bootMsg("mounting card...",0);
  SD_MMC.setPins(SD_CLK,SD_MOSI,SD_MISO,-1,-1,-1);
  delay(100);
  g_sd_ok=SD_MMC.begin("/sdcard",true);
  if(!g_sd_ok){ delay(200); g_sd_ok=SD_MMC.begin("/sdcard",true); }
  if(!g_sd_ok){
    bootMsg("SD MOUNT FAILED",0);
    Serial.println("[sd] mount failed");
    while(true) delay(1000);
  }
  bootMsg(String("card ok (")+String((int)(SD_MMC.cardSize()/(1024ULL*1024ULL)))+" MB)",0);

  // ---- library: cache, cache, and only then walk --------------------------
  // A 1005-game card takes MINUTES to enumerate (577 s measured on the 7B P4
  // for 1741 files). Never do that twice.
  g_games.clear(); g_games.reserve(MAX_GAMES);
  uint32_t t0=millis();
  const char* src="";
  int n=0;
  n += loadFirmwareCache("/ADF");
  n += loadFirmwareCache("/DSK");
  if(n>0) src="firmware .gamecache";
  if(n==0 && readBenchIndex()){ n=(int)g_games.size(); src="bench index"; }
  if(n==0){
    bootMsg("no cache - walking the card (this is the slow one)",0);
    Serial.println("[lib] no cache found, walking the tree");
    g_scan_t0=millis(); g_scan_lastdraw=0; g_scan_seen=0; g_scan_lastseen=0;
    g_scan_dirs=0; g_scan_lastlog=0;
    scanDir("",0);
    Serial.printf("[scan] done: %u entries, %d games, %u dirs in %u ms\n",
      (unsigned)g_scan_seen,(int)g_games.size(),(unsigned)g_scan_dirs,
      (unsigned)(millis()-g_scan_t0));
    n=(int)g_games.size();
    src="tree walk";
    if(n>0){ bootMsg("writing bench index...",2); writeBenchIndex(); }
  }
  uint32_t libMs=millis()-t0;
  int resolved=0, withArt=0;
  for(size_t i=0;i<g_games.size();i++){
    if(g_games[i].jpg){ resolved++; if(g_games[i].jpg[0]) withArt++; }
  }
  Serial.printf("[lib] %d games from %s in %u ms | covers known %d, with art %d\n",
                n,src,(unsigned)libMs,resolved,withArt);
  Serial.printf("[lib] arena %u KB in %u chunks\n",
                (unsigned)(g_arena_total/1024),(unsigned)g_arena_chunks.size());
  bootMsg(String(n)+" games from "+src+"  ("+String(libMs)+"ms)",1);
  bootMsg(String("covers known ")+String(resolved)+", with art "+String(withArt)
          +(resolved<n?String("  - the rest resolve lazily"):String("")),2);
  if(n==0){
    bootMsg("NO GAMES FOUND - check /ADF or /DSK on the card",3);
    Serial.println("[lib] nothing found");
    while(true) delay(1000);
  }

  carInit();
  Serial.printf("PSRAM free after init: %u KB\n",(unsigned)(ESP.getFreePsram()/1024));
  delay(900);
  UG->fillScreen(C_BG);
  g_repaint=2;
  resetCounters();
  g_last_us=micros();
}

void loop(){
  uint32_t f0=micros();
  float dt=(float)(f0-g_last_us)/1000000.0f; g_last_us=f0;
  if(dt>0.25f) dt=0.25f;

  touchPump();
  t_text=0; g_cover_probe_us=0; t_topen=0; t_tread=0;
  if(g_repaint>0){
    UG->fillScreen(C_BG);   // both frames, so neither framebuffer keeps a ghost
    drawStatusBar(); drawBottomBar(); g_repaint--;
  }
  if(g_mode==MODE_REEL||g_mode==MODE_REEL_LEAN) renderReel(dt,g_mode==MODE_REEL_LEAN);
  else                                          renderGrid(g_mode);
  touchPump();
  drawHud();
  touchPump();
  tft.display();
  touchPump();
  t_total=micros()-f0;
  winPush(t_total);
  g_frames++;

  // Grid modes auto-advance a page per frame: a page turn IS the thing being
  // measured, and flicking through the library is exactly how it gets used.
  if(g_mode!=MODE_REEL&&g_mode!=MODE_REEL_LEAN) g_page++;

  if((g_frames%30)==0){
    Serial.printf("[%s] frame %s ms (avg %s) fill %s tiles %s (open %s read %s) blit %s text %s copy %s (%u px) vsync %s | hit %u ld %u dec %u\n",
      modeName(), ms1(t_total).c_str(), ms1(winAvg()).c_str(),
      ms1(t_fill).c_str(), ms1(t_tiles).c_str(), ms1(t_topen).c_str(), ms1(t_tread).c_str(),
      ms1(t_blit).c_str(), ms1(t_text).c_str(),
      ms1(tft.t_copy_us).c_str(), (unsigned)tft.n_copy_px, ms1(tft.t_vsync_us).c_str(),
      (unsigned)g_tile_hits,(unsigned)g_tile_loads,(unsigned)g_tile_decodes);
    Serial.printf("      thumbs: %u from the sharded layout, %u still legacy (migrating)\n",
      (unsigned)g_tnl_fast,(unsigned)g_tnl_legacy);
  }
  if(g_tap_pending){ g_tap_pending=false; handleTap(g_tap_x,g_tap_y); }
}
