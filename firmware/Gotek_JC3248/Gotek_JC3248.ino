// ESP32-S3 (Guition JC3248W535C) — USB MSC RAM Disk + ADF/DSK Browser
// Full port of Waveshare 7" firmware v3.4.7 (Mez UI) onto Dimi's hardware layer
// Board: ESP32S3 Dev Module | USB-OTG (TinyUSB) | CDC DISABLED | OPI PSRAM
// Flash: 16MB QIO 120MHz | Partition: Huge APP (3MB No OTA/1MB SPIFFS) | 240MHz

#include <Arduino.h>
#include "USB.h"
#include "USBMSC.h"
#include <FS.h>
#include <SD_MMC.h>
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_axs15231b.h"
#include "esp_random.h"
#include "diag_adf.h"      // embedded Amiga Test Kit ADF (zero-RLE compressed, public domain)
#include <JPEGDEC.h>
// JPEGDEC and PNGdec both define INTELSHORT/INTELLONG/MOTOSHORT/MOTOLONG; undef
// after JPEGDEC so PNGdec redefines them cleanly (silences redefinition warnings).
#undef INTELSHORT
#undef INTELLONG
#undef MOTOSHORT
#undef MOTOLONG
#include <PNGdec.h>      // cover art may be PNG as well as JPEG (v4.8.4) — needs the "PNGdec" library (Larry Bank) installed
#include <Wire.h>
#include <vector>
#include <algorithm>
#include <ctype.h>
#include <sys/stat.h>

#define FW_VERSION "A.5.0.0-JC3248"
#include "espnow_server.h"

extern "C" { bool tud_mounted(void); void tud_disconnect(void); void tud_connect(void); void* ps_malloc(size_t size); }

// ════════════════════════════════════════════════════════════════════════════
// HARDWARE
// ════════════════════════════════════════════════════════════════════════════
#define LCD_WIDTH  320
#define LCD_HEIGHT 480
// Virtual canvas + rotation. g_rot: 0=landscape, 1=portrait, 2=landscape-flipped,
// 3=portrait-flipped. Each ROTATE tap advances 90 degrees. gW/gH swap for portrait.
static int gW=480, gH=320;
static int g_rot=0;
static bool g_compact=false;
#define g_portrait (g_rot==1||g_rot==3)
// Disk-selector grid geometry — declared up here so the Arduino auto-prototype
// for diskGrid() (which returns this type) sees it before use.
struct DiskGrid{int pages,pageStart,pageEnd,COLS,dbw,dbh,dgap,gridW,gx,gridY,gridH,pageBtnH,pageGap,labelY;bool multiPage;};
#define LCD_PIN_CS 45
#define LCD_PIN_CLK 47
#define LCD_PIN_MOSI 21
#define LCD_PIN_MISO 48
#define LCD_PIN_D2 40
#define LCD_PIN_D3 39
#define LCD_PIN_BL 1
#define TOUCH_SDA 4
#define TOUCH_SCL 8
#define TOUCH_ADDR 0x3B
#define SD_CLK 12
#define SD_CMD 11
#define SD_D0 13
#define ROWS_PER_STRIP 10

#define TFT_BLACK   0x0000
#define TFT_WHITE   0xFFFF
#define TFT_RED     0xF800
#define TFT_GREEN   0x07E0
#define TFT_BLUE    0x001F
#define TFT_CYAN    0x07FF
#define TFT_YELLOW  0xFFE0
#define TFT_ORANGE  0xFD20

// Pick black or white text for good contrast on a given RGB565 background (theme-proof buttons)
static inline uint16_t inkFor(uint16_t bg){int r=(bg>>11)&0x1F,g=(bg>>5)&0x3F,b=bg&0x1F;int lum=(r*77)/31+(g*151)/63+(b*28)/31;return lum>150?TFT_BLACK:TFT_WHITE;}

// ════════════════════════════════════════════════════════════════════════════
// INIT COMMANDS — from Dimi's working JC3248 firmware
// ════════════════════════════════════════════════════════════════════════════
static const axs15231b_lcd_init_cmd_t lcd_init_cmds[] = {
  {0xBB,(uint8_t[]){0x00,0x00,0x00,0x00,0x00,0x00,0x5A,0xA5},8,0},
  {0xA0,(uint8_t[]){0xC0,0x10,0x00,0x02,0x00,0x00,0x04,0x3F,0x20,0x05,0x3F,0x3F,0x00,0x00,0x00,0x00,0x00},17,0},
  {0xA2,(uint8_t[]){0x30,0x3C,0x24,0x14,0xD0,0x20,0xFF,0xE0,0x40,0x19,0x80,0x80,0x80,0x20,0xf9,0x10,0x02,0xff,0xff,0xF0,0x90,0x01,0x32,0xA0,0x91,0xE0,0x20,0x7F,0xFF,0x00,0x5A},31,0},
  {0xD0,(uint8_t[]){0xE0,0x40,0x51,0x24,0x08,0x05,0x10,0x01,0x20,0x15,0x42,0xC2,0x22,0x22,0xAA,0x03,0x10,0x12,0x60,0x14,0x1E,0x51,0x15,0x00,0x8A,0x20,0x00,0x03,0x3A,0x12},30,0},
  {0xA3,(uint8_t[]){0xA0,0x06,0xAa,0x00,0x08,0x02,0x0A,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x00,0x55,0x55},22,0},
  {0xC1,(uint8_t[]){0x31,0x04,0x02,0x02,0x71,0x05,0x24,0x55,0x02,0x00,0x41,0x00,0x53,0xFF,0xFF,0xFF,0x4F,0x52,0x00,0x4F,0x52,0x00,0x45,0x3B,0x0B,0x02,0x0d,0x00,0xFF,0x40},30,0},
  {0xC3,(uint8_t[]){0x00,0x00,0x00,0x50,0x03,0x00,0x00,0x00,0x01,0x80,0x01},11,0},
  {0xC4,(uint8_t[]){0x00,0x24,0x33,0x80,0x00,0xea,0x64,0x32,0xC8,0x64,0xC8,0x32,0x90,0x90,0x11,0x06,0xDC,0xFA,0x00,0x00,0x80,0xFE,0x10,0x10,0x00,0x0A,0x0A,0x44,0x50},29,0},
  {0xC5,(uint8_t[]){0x18,0x00,0x00,0x03,0xFE,0x3A,0x4A,0x20,0x30,0x10,0x88,0xDE,0x0D,0x08,0x0F,0x0F,0x01,0x3A,0x4A,0x20,0x10,0x10,0x00},23,0},
  {0xC6,(uint8_t[]){0x05,0x0A,0x05,0x0A,0x00,0xE0,0x2E,0x0B,0x12,0x22,0x12,0x22,0x01,0x03,0x00,0x3F,0x6A,0x18,0xC8,0x22},20,0},
  {0xC7,(uint8_t[]){0x50,0x32,0x28,0x00,0xa2,0x80,0x8f,0x00,0x80,0xff,0x07,0x11,0x9c,0x67,0xff,0x24,0x0c,0x0d,0x0e,0x0f},20,0},
  {0xC9,(uint8_t[]){0x33,0x44,0x44,0x01},4,0},
  {0xCF,(uint8_t[]){0x2C,0x1E,0x88,0x58,0x13,0x18,0x56,0x18,0x1E,0x68,0x88,0x00,0x65,0x09,0x22,0xC4,0x0C,0x77,0x22,0x44,0xAA,0x55,0x08,0x08,0x12,0xA0,0x08},27,0},
  {0xD5,(uint8_t[]){0x40,0x8E,0x8D,0x01,0x35,0x04,0x92,0x74,0x04,0x92,0x74,0x04,0x08,0x6A,0x04,0x46,0x03,0x03,0x03,0x03,0x82,0x01,0x03,0x00,0xE0,0x51,0xA1,0x00,0x00,0x00},30,0},
  {0xD6,(uint8_t[]){0x10,0x32,0x54,0x76,0x98,0xBA,0xDC,0xFE,0x93,0x00,0x01,0x83,0x07,0x07,0x00,0x07,0x07,0x00,0x03,0x03,0x03,0x03,0x03,0x03,0x00,0x84,0x00,0x20,0x01,0x00},30,0},
  {0xD7,(uint8_t[]){0x03,0x01,0x0b,0x09,0x0f,0x0d,0x1E,0x1F,0x18,0x1d,0x1f,0x19,0x40,0x8E,0x04,0x00,0x20,0xA0,0x1F},19,0},
  {0xD8,(uint8_t[]){0x02,0x00,0x0a,0x08,0x0e,0x0c,0x1E,0x1F,0x18,0x1d,0x1f,0x19},12,0},
  {0xD9,(uint8_t[]){0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F},12,0},
  {0xDD,(uint8_t[]){0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F},12,0},
  {0xDF,(uint8_t[]){0x44,0x73,0x4B,0x69,0x00,0x0A,0x02,0x90},8,0},
  {0xE0,(uint8_t[]){0x3B,0x28,0x10,0x16,0x0c,0x06,0x11,0x28,0x5c,0x21,0x0D,0x35,0x13,0x2C,0x33,0x28,0x0D},17,0},
  {0xE1,(uint8_t[]){0x37,0x28,0x10,0x16,0x0b,0x06,0x11,0x28,0x5C,0x21,0x0D,0x35,0x14,0x2C,0x33,0x28,0x0F},17,0},
  {0xE2,(uint8_t[]){0x3B,0x07,0x12,0x18,0x0E,0x0D,0x17,0x35,0x44,0x32,0x0C,0x14,0x14,0x36,0x3A,0x2F,0x0D},17,0},
  {0xE3,(uint8_t[]){0x37,0x07,0x12,0x18,0x0E,0x0D,0x17,0x35,0x44,0x32,0x0C,0x14,0x14,0x36,0x32,0x2F,0x0F},17,0},
  {0xE4,(uint8_t[]){0x3B,0x07,0x12,0x18,0x0E,0x0D,0x17,0x39,0x44,0x2E,0x0C,0x14,0x14,0x36,0x3A,0x2F,0x0D},17,0},
  {0xE5,(uint8_t[]){0x37,0x07,0x12,0x18,0x0E,0x0D,0x17,0x39,0x44,0x2E,0x0C,0x14,0x14,0x36,0x3A,0x2F,0x0F},17,0},
  {0xA4,(uint8_t[]){0x85,0x85,0x95,0x82,0xAF,0xAA,0xAA,0x80,0x10,0x30,0x40,0x40,0x20,0xFF,0x60,0x30},16,0},
  {0xA4,(uint8_t[]){0x85,0x85,0x95,0x85},4,0},
  {0xBB,(uint8_t[]){0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},8,0},
  {0x13,(uint8_t[]){0x00},0,0},
  {0x11,(uint8_t[]){0x00},0,120},
  {0x29,(uint8_t[]){0x00},0,20},
  {0x2C,(uint8_t[]){0x00,0x00,0x00,0x00},4,0},
};

// ════════════════════════════════════════════════════════════════════════════
// FONT 6×8
// ════════════════════════════════════════════════════════════════════════════
static const uint8_t font6x8[95][6] PROGMEM = {
  {0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x4F,0x00,0x00,0x00},{0x00,0x07,0x00,0x07,0x00,0x00},
  {0x14,0x7F,0x14,0x7F,0x14,0x00},{0x24,0x2A,0x7F,0x2A,0x12,0x00},{0x62,0x64,0x08,0x13,0x23,0x00},
  {0x36,0x49,0x49,0x36,0x50,0x00},{0x00,0x04,0x03,0x00,0x00,0x00},{0x00,0x1C,0x22,0x41,0x00,0x00},
  {0x00,0x41,0x22,0x1C,0x00,0x00},{0x14,0x08,0x3E,0x08,0x14,0x00},{0x08,0x08,0x3E,0x08,0x08,0x00},
  {0x00,0x50,0x30,0x00,0x00,0x00},{0x08,0x08,0x08,0x08,0x08,0x00},{0x00,0x60,0x60,0x00,0x00,0x00},
  {0x20,0x10,0x08,0x04,0x02,0x00},{0x3E,0x51,0x49,0x45,0x3E,0x00},{0x00,0x42,0x7F,0x40,0x00,0x00},
  {0x42,0x61,0x51,0x49,0x46,0x00},{0x21,0x41,0x45,0x4B,0x31,0x00},{0x18,0x14,0x12,0x7F,0x10,0x00},
  {0x27,0x45,0x45,0x45,0x39,0x00},{0x3C,0x4A,0x49,0x49,0x30,0x00},{0x01,0x71,0x09,0x05,0x03,0x00},
  {0x36,0x49,0x49,0x49,0x36,0x00},{0x06,0x49,0x49,0x29,0x1E,0x00},{0x00,0x36,0x36,0x00,0x00,0x00},
  {0x00,0x56,0x36,0x00,0x00,0x00},{0x08,0x14,0x22,0x41,0x00,0x00},{0x14,0x14,0x14,0x14,0x14,0x00},
  {0x00,0x41,0x22,0x14,0x08,0x00},{0x02,0x01,0x51,0x09,0x06,0x00},{0x32,0x49,0x79,0x41,0x3E,0x00},
  {0x7E,0x11,0x11,0x11,0x7E,0x00},{0x7F,0x49,0x49,0x49,0x36,0x00},{0x3E,0x41,0x41,0x41,0x22,0x00},
  {0x7F,0x41,0x41,0x41,0x3E,0x00},{0x7F,0x49,0x49,0x49,0x41,0x00},{0x7F,0x09,0x09,0x09,0x01,0x00},
  {0x3E,0x41,0x49,0x49,0x3A,0x00},{0x7F,0x08,0x08,0x08,0x7F,0x00},{0x00,0x41,0x7F,0x41,0x00,0x00},
  {0x20,0x40,0x41,0x3F,0x01,0x00},{0x7F,0x08,0x14,0x22,0x41,0x00},{0x7F,0x40,0x40,0x40,0x40,0x00},
  {0x7F,0x02,0x0C,0x02,0x7F,0x00},{0x7F,0x04,0x08,0x10,0x7F,0x00},{0x3E,0x41,0x41,0x41,0x3E,0x00},
  {0x7F,0x09,0x09,0x09,0x06,0x00},{0x3E,0x41,0x41,0x21,0x5E,0x00},{0x7F,0x09,0x19,0x29,0x46,0x00},
  {0x46,0x49,0x49,0x49,0x31,0x00},{0x01,0x01,0x7F,0x01,0x01,0x00},{0x3F,0x40,0x40,0x40,0x3F,0x00},
  {0x1F,0x20,0x40,0x20,0x1F,0x00},{0x3F,0x40,0x38,0x40,0x3F,0x00},{0x63,0x14,0x08,0x14,0x63,0x00},
  {0x07,0x08,0x70,0x08,0x07,0x00},{0x61,0x51,0x49,0x45,0x43,0x00},{0x00,0x7F,0x41,0x00,0x00,0x00},
  {0x02,0x04,0x08,0x10,0x20,0x00},{0x00,0x41,0x7F,0x00,0x00,0x00},{0x04,0x02,0x01,0x02,0x04,0x00},
  {0x40,0x40,0x40,0x40,0x40,0x00},{0x00,0x01,0x02,0x04,0x00,0x00},{0x20,0x54,0x54,0x54,0x78,0x00},
  {0x7F,0x48,0x44,0x44,0x38,0x00},{0x38,0x44,0x44,0x44,0x20,0x00},{0x38,0x44,0x44,0x48,0x7F,0x00},
  {0x38,0x54,0x54,0x54,0x18,0x00},{0x08,0x7E,0x09,0x01,0x02,0x00},{0x18,0xA4,0xA4,0x9C,0x78,0x00},
  {0x7F,0x08,0x04,0x04,0x78,0x00},{0x00,0x44,0x7D,0x40,0x00,0x00},{0x20,0x40,0x44,0x3D,0x00,0x00},
  {0x7F,0x10,0x28,0x44,0x00,0x00},{0x00,0x41,0x7F,0x40,0x00,0x00},{0x7C,0x04,0x78,0x04,0x78,0x00},
  {0x7C,0x08,0x04,0x04,0x78,0x00},{0x38,0x44,0x44,0x44,0x38,0x00},{0x7C,0x14,0x14,0x14,0x08,0x00},
  {0x08,0x14,0x14,0x18,0x7C,0x00},{0x7C,0x08,0x04,0x04,0x08,0x00},{0x48,0x54,0x54,0x54,0x20,0x00},
  {0x04,0x3F,0x44,0x40,0x20,0x00},{0x3C,0x40,0x40,0x20,0x7C,0x00},{0x1C,0x20,0x40,0x20,0x1C,0x00},
  {0x3C,0x40,0x30,0x40,0x3C,0x00},{0x44,0x28,0x10,0x28,0x44,0x00},{0x1C,0xA0,0xA0,0x9C,0x0C,0x00},
  {0x44,0x64,0x54,0x4C,0x44,0x00},{0x00,0x08,0x36,0x41,0x00,0x00},{0x00,0x00,0x7F,0x00,0x00,0x00},
  {0x00,0x41,0x36,0x08,0x00,0x00},{0x08,0x04,0x08,0x10,0x08,0x00},
};

// ════════════════════════════════════════════════════════════════════════════
// DISPLAY + FRAMEBUFFER (Dimi's proven code)
// ════════════════════════════════════════════════════════════════════════════
static esp_lcd_panel_io_handle_t io_handle = NULL;
static esp_lcd_panel_handle_t panel_handle = NULL;
static uint16_t *framebuffer = NULL;
static uint16_t *dma_buffer = NULL;
static JPEGDEC jpegdec;
static PNG     pngdec;   // v4.8.4 PNG cover support

static inline uint16_t swap16(uint16_t c){return(c>>8)|(c<<8);}
static inline void fb_setPixel(int vx,int vy,uint16_t color){
  int px,py;
  switch(g_rot){
    case 1: px=vx; py=vy; break;                              // 90  portrait
    case 2: px=(LCD_WIDTH-1)-vy; py=vx; break;                // 180 landscape flipped
    case 3: px=(LCD_WIDTH-1)-vx; py=(LCD_HEIGHT-1)-vy; break; // 270 portrait flipped
    default: px=vy; py=(LCD_HEIGHT-1)-vx; break;              // 0   landscape
  }
  if(px>=0&&px<LCD_WIDTH&&py>=0&&py<LCD_HEIGHT) framebuffer[py*LCD_WIDTH+px]=swap16(color);
}

// ── GFX wrappers ──
static uint16_t text_fg=TFT_WHITE,text_bg=TFT_BLACK;
static int text_size=1,text_x=0,text_y=0;
static int g_clip_y0=0,g_clip_y1=gH,g_clip_x0=0,g_clip_x1=gW;   // clip window (vertical=scroll, horizontal=marquee)

static void gfx_fillScreen(uint16_t c){uint16_t s=swap16(c);for(int i=0;i<LCD_WIDTH*LCD_HEIGHT;i++)framebuffer[i]=s;}
static void gfx_drawPixel(int x,int y,uint16_t c){if(x>=g_clip_x0&&x<g_clip_x1&&y>=g_clip_y0&&y<g_clip_y1)fb_setPixel(x,y,c);}

static void gfx_fillRect(int x,int y,int w,int h,uint16_t color){
  int vx0=max(g_clip_x0,x),vy0=max(g_clip_y0,y),vx1=min(g_clip_x1,x+w),vy1=min(g_clip_y1,y+h);
  if(vx0>=vx1||vy0>=vy1)return;
  uint16_t sc=swap16(color);
  int px0,px1,py0,py1;               // rotations map a virtual rect to a physical rect
  switch(g_rot){
    case 1: px0=vx0;py0=vy0;px1=vx1;py1=vy1;break;
    case 2: px0=LCD_WIDTH-vy1;py0=vx0;px1=LCD_WIDTH-vy0;py1=vx1;break;
    case 3: px0=LCD_WIDTH-vx1;py0=LCD_HEIGHT-vy1;px1=LCD_WIDTH-vx0;py1=LCD_HEIGHT-vy0;break;
    default: px0=vy0;py0=LCD_HEIGHT-vx1;px1=vy1;py1=LCD_HEIGHT-vx0;break;
  }
  for(int py=py0;py<py1;py++){uint16_t*row=&framebuffer[py*LCD_WIDTH];for(int px=px0;px<px1;px++)row[px]=sc;}
}

static void gfx_drawRect(int x,int y,int w,int h,uint16_t c){gfx_fillRect(x,y,w,1,c);gfx_fillRect(x,y+h-1,w,1,c);gfx_fillRect(x,y,1,h,c);gfx_fillRect(x+w-1,y,1,h,c);}
static void gfx_hline(int x,int y,int w,uint16_t c){gfx_fillRect(x,y,w,1,c);}
static void gfx_vline(int x,int y,int h,uint16_t c){gfx_fillRect(x,y,1,h,c);}
static void gfx_fillCircle(int cx,int cy,int r,uint16_t c){for(int y=-r;y<=r;y++){int w=(int)sqrtf(r*r-y*y);gfx_fillRect(cx-w,cy+y,2*w+1,1,c);}}
#define COL_STAR 0xFEE0
static void gfx_fillStar(int cx,int cy,float rO,uint16_t col){
  float rI=rO*0.42f;float pts[10][2];
  for(int i=0;i<10;i++){float a=-1.57080f+i*0.628319f;float rr=(i&1)?rI:rO;pts[i][0]=cx+cosf(a)*rr;pts[i][1]=cy+sinf(a)*rr;}
  int y0=(int)(cy-rO-1),y1=(int)(cy+rO+1),x0=(int)(cx-rO-1),x1=(int)(cx+rO+1);
  for(int y=y0;y<=y1;y++)for(int x=x0;x<=x1;x++){bool in=false;
    for(int i=0,j=9;i<10;j=i++){if(((pts[i][1]>y)!=(pts[j][1]>y))&&((float)x<(pts[j][0]-pts[i][0])*(y-pts[i][1])/(pts[j][1]-pts[i][1])+pts[i][0]))in=!in;}
    if(in)gfx_drawPixel(x,y,col);}
}
static void gfx_drawCircle(int cx,int cy,int r,uint16_t c){int x=0,y=r,d=3-2*r;while(x<=y){gfx_drawPixel(cx+x,cy+y,c);gfx_drawPixel(cx-x,cy+y,c);gfx_drawPixel(cx+x,cy-y,c);gfx_drawPixel(cx-x,cy-y,c);gfx_drawPixel(cx+y,cy+x,c);gfx_drawPixel(cx-y,cy+x,c);gfx_drawPixel(cx+y,cy-x,c);gfx_drawPixel(cx-y,cy-x,c);if(d<0)d+=4*x+6;else{d+=4*(x-y)+10;y--;}x++;}}
static void gfx_fillRoundRect(int x,int y,int w,int h,int r,uint16_t c){gfx_fillRect(x+r,y,w-2*r,h,c);gfx_fillRect(x,y+r,r,h-2*r,c);gfx_fillRect(x+w-r,y+r,r,h-2*r,c);for(int dy=-r;dy<=0;dy++){int dx=(int)sqrtf(r*r-dy*dy);gfx_fillRect(x+r-dx,y+r+dy,dx,1,c);gfx_fillRect(x+w-r,y+r+dy,dx,1,c);gfx_fillRect(x+r-dx,y+h-r-1-dy,dx,1,c);gfx_fillRect(x+w-r,y+h-r-1-dy,dx,1,c);}}
static void gfx_drawRoundRect(int x,int y,int w,int h,int r,uint16_t c){gfx_hline(x+r,y,w-2*r,c);gfx_hline(x+r,y+h-1,w-2*r,c);gfx_vline(x,y+r,h-2*r,c);gfx_vline(x+w-1,y+r,h-2*r,c);}

static void gfx_setTextColor(uint16_t f,uint16_t b){text_fg=f;text_bg=b;}
static void gfx_setTextSize(int s){text_size=s<1?1:s;}
static void gfx_setCursor(int x,int y){text_x=x;text_y=y;}
static int gfx_textWidth(const String&s){return s.length()*6*text_size;}

static void gfx_print(const String&text){
  for(unsigned i=0;i<text.length();i++){char c=text[i];if(c<32||c>126)continue;
    const uint8_t*data=font6x8[c-32];
    for(int col=0;col<6;col++){uint8_t bits=pgm_read_byte(&data[col]);
      for(int row=0;row<8;row++){uint16_t color=(bits&(1<<row))?text_fg:text_bg;
        for(int dy=0;dy<text_size;dy++)for(int dx=0;dx<text_size;dx++)
          gfx_drawPixel(text_x+col*text_size+dx,text_y+row*text_size+dy,color);}}
    text_x+=6*text_size;}
}
static void gfx_print(const char*s){gfx_print(String(s));}

static void gfx_flush(){
  if(!framebuffer||!panel_handle)return;
  for(int sy=0;sy<LCD_HEIGHT;sy+=ROWS_PER_STRIP){
    int rows=min(ROWS_PER_STRIP,LCD_HEIGHT-sy);
    memcpy(dma_buffer,&framebuffer[sy*LCD_WIDTH],LCD_WIDTH*rows*2);
    esp_lcd_panel_draw_bitmap(panel_handle,0,sy,LCD_WIDTH,sy+rows,dma_buffer);
    delayMicroseconds(500);
  }
}

// ── JPEG decode via JPEGDEC (from Dimi) ──
static uint16_t *jpeg_tmp_buf=NULL;
static int jpeg_tmp_w=0,jpeg_tmp_h=0;
int jpeg_buf_cb(JPEGDRAW*pDraw){
  if(!jpeg_tmp_buf)return 0;
  for(int yy=0;yy<pDraw->iHeight;yy++){
    int row=pDraw->y+yy; if(row<0||row>=jpeg_tmp_h)continue;
    int cw=pDraw->iWidth; if(pDraw->x+cw>jpeg_tmp_w)cw=jpeg_tmp_w-pDraw->x;
    if(cw>0) memcpy(&jpeg_tmp_buf[row*jpeg_tmp_w+pDraw->x],&pDraw->pPixels[yy*pDraw->iWidth],cw*2);
  } return 1;
}
// ── PNG decode via PNGdec (v4.8.4). Same jpeg_tmp_buf target as the JPEG path,
//    so the scale/blit code downstream is shared. PNGdec has no hardware
//    downscale, so PNG covers decode full-res into PSRAM (keep them modest). ──
static bool coverIsPng(const String&p){int d=p.lastIndexOf('.');if(d<0)return false;String e=p.substring(d+1);e.toLowerCase();return e=="png";}
int png_buf_cb(PNGDRAW*pDraw){   // PNGdec's PNG_DRAW_CALLBACK returns int
  if(!jpeg_tmp_buf)return 0;
  int row=pDraw->y; if(row<0||row>=jpeg_tmp_h)return 1;
  pngdec.getLineAsRGB565(pDraw,&jpeg_tmp_buf[row*jpeg_tmp_w],PNG_RGB565_LITTLE_ENDIAN,0x00000000);
  return 1;
}
static void gfx_drawJpgFile(const String&path,int x,int y,int maxW,int maxH){
  // Use VFS to get real file size (SD_MMC f.size() returns 0 for subdirectory files)
  String vfsPath="/sdcard"+path;
  struct stat st;
  if(stat(vfsPath.c_str(),&st)!=0||st.st_size==0||st.st_size>500000) return;
  size_t sz=(size_t)st.st_size;
  File f=SD_MMC.open(path.c_str(),"r"); if(!f){return;}
  uint8_t*buf=(uint8_t*)ps_malloc(sz); if(!buf){f.close();return;}
  f.read(buf,sz); f.close();
  int jw=0,jh=0;
  if(coverIsPng(path)){
    if(pngdec.openRAM(buf,sz,png_buf_cb)!=PNG_SUCCESS){free(buf);return;}
    jw=pngdec.getWidth();jh=pngdec.getHeight();
    if(jw<=0||jh<=0||jw>2000||jh>2000){pngdec.close();free(buf);return;}
    jpeg_tmp_buf=(uint16_t*)ps_malloc((size_t)jw*jh*2);
    if(!jpeg_tmp_buf){pngdec.close();free(buf);return;}
    memset(jpeg_tmp_buf,0,(size_t)jw*jh*2); jpeg_tmp_w=jw; jpeg_tmp_h=jh;
    pngdec.decode(NULL,0); pngdec.close(); free(buf);
  } else {
    if(!jpegdec.openRAM(buf,sz,jpeg_buf_cb)){free(buf);return;}
    jw=jpegdec.getWidth();jh=jpegdec.getHeight();
    if(jw<=0||jh<=0||jw>2000||jh>2000){jpegdec.close();free(buf);return;}
    jpeg_tmp_buf=(uint16_t*)ps_malloc((size_t)jw*jh*2);
    if(!jpeg_tmp_buf){jpegdec.close();free(buf);return;}
    memset(jpeg_tmp_buf,0,(size_t)jw*jh*2); jpeg_tmp_w=jw; jpeg_tmp_h=jh;
    jpegdec.decode(0,0,0); jpegdec.close(); free(buf);
  }
  float scX=(float)maxW/jw,scY=(float)maxH/jh,sc=min(scX,scY);
  if(sc>1.0f)sc=1.0f;
  int dw=(int)(jw*sc),dh=(int)(jh*sc);
  if(dw<=0||dh<=0){free(jpeg_tmp_buf);jpeg_tmp_buf=NULL;return;}
  int ox=x+(maxW-dw)/2,oy=y+(maxH-dh)/2;
  for(int r=0;r<dh;r++){int srcY=(int)(r/sc);if(srcY>=jh)srcY=jh-1;
    for(int c=0;c<dw;c++){int srcX=(int)(c/sc);if(srcX>=jw)srcX=jw-1;
      int vx=ox+c,vy=oy+r;
      if(vx>=0&&vx<gW&&vy>=0&&vy<gH) fb_setPixel(vx,vy,jpeg_tmp_buf[srcY*jw+srcX]);}
    if(r%20==0)yield();}
  free(jpeg_tmp_buf); jpeg_tmp_buf=NULL;
}

// ── Display init (from Dimi) ──
static void displayInit(){
  framebuffer=(uint16_t*)ps_malloc(LCD_WIDTH*LCD_HEIGHT*2);
  dma_buffer=(uint16_t*)heap_caps_malloc(LCD_WIDTH*ROWS_PER_STRIP*2,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);
  if(!framebuffer||!dma_buffer){Serial.println("FATAL: fb alloc");while(1)delay(1000);}
  spi_bus_config_t buscfg={};
  buscfg.data0_io_num=LCD_PIN_MOSI;buscfg.data1_io_num=LCD_PIN_MISO;
  buscfg.sclk_io_num=LCD_PIN_CLK;buscfg.data2_io_num=LCD_PIN_D2;buscfg.data3_io_num=LCD_PIN_D3;
  buscfg.max_transfer_sz=LCD_WIDTH*LCD_HEIGHT*2;
  ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST,&buscfg,SPI_DMA_CH_AUTO));
  esp_lcd_panel_io_spi_config_t io_config={};
  io_config.cs_gpio_num=LCD_PIN_CS;io_config.dc_gpio_num=-1;io_config.spi_mode=3;
  io_config.pclk_hz=50000000;io_config.trans_queue_depth=1;
  io_config.lcd_cmd_bits=32;io_config.lcd_param_bits=8;io_config.flags.quad_mode=true;
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,&io_config,&io_handle));
  axs15231b_vendor_config_t vc={};vc.init_cmds=lcd_init_cmds;
  vc.init_cmds_size=sizeof(lcd_init_cmds)/sizeof(lcd_init_cmds[0]);vc.flags.use_qspi_interface=1;
  esp_lcd_panel_dev_config_t pc={};pc.reset_gpio_num=-1;
  pc.rgb_ele_order=LCD_RGB_ELEMENT_ORDER_RGB;pc.bits_per_pixel=16;pc.vendor_config=&vc;
  ESP_ERROR_CHECK(esp_lcd_new_panel_axs15231b(io_handle,&pc,&panel_handle));
  esp_lcd_panel_reset(panel_handle);delay(100);
  esp_lcd_panel_init(panel_handle);delay(200);
  ledcAttach(LCD_PIN_BL,5000,8);ledcWrite(LCD_PIN_BL,200);
}

// ── Touch (from Dimi) ──
static uint8_t gTouchPts=0;static uint16_t gTouchX=0,gTouchY=0;
static void touchInit(){Wire.begin(TOUCH_SDA,TOUCH_SCL,400000);}
static bool Touch_ReadFrame(){
  uint8_t cmd[11]={0xb5,0xab,0xa5,0x5a,0x00,0x00,0x00,0x08,0x00,0x00,0x00};
  Wire.beginTransmission(TOUCH_ADDR);Wire.write(cmd,11);
  if(Wire.endTransmission()!=0){gTouchPts=0;return false;}
  if(Wire.requestFrom((int)TOUCH_ADDR,8)!=8){gTouchPts=0;return false;}
  uint8_t buf[8];for(int i=0;i<8;i++)buf[i]=Wire.read();
  if(buf[1]==0){gTouchPts=0;return false;}
  uint16_t rx=((buf[2]&0x0F)<<8)|buf[3],ry=((buf[4]&0x0F)<<8)|buf[5];
  if(rx>=LCD_WIDTH||ry>=LCD_HEIGHT){gTouchPts=0;return false;}
  switch(g_rot){                                   // inverse of fb_setPixel mapping
    case 1: gTouchX=rx; gTouchY=ry; break;
    case 2: gTouchX=ry; gTouchY=(LCD_WIDTH-1)-rx; break;
    case 3: gTouchX=(LCD_WIDTH-1)-rx; gTouchY=(LCD_HEIGHT-1)-ry; break;
    default: gTouchX=(LCD_HEIGHT-1)-ry; gTouchY=rx; break;
  }
  gTouchPts=1; return true;
}
static bool getTouchXY(uint16_t*x,uint16_t*y){if(!gTouchPts)return false;*x=constrain(gTouchX,0,gW-1);*y=constrain(gTouchY,0,gH-1);return true;}

// ════════════════════════════════════════════════════════════════════════════
// MSC RAM DISK
// ════════════════════════════════════════════════════════════════════════════
USBMSC MSC;static bool g_usb_online=false;
static const uint16_t SECTOR_SIZE=512;static const uint32_t TOTAL_SECTORS=4096;   // v4.9: 2MB RAM disk so HD (1.76MB) images fit
static const uint16_t RESERVED_SECTORS=1,SECTORS_PER_FAT=6,ROOT_DIR_SECTORS=4;
static const uint8_t SECTORS_PER_CLUSTER=2,NUM_FATS=1;static const uint16_t ROOT_ENTRIES=64;   // v4.9: 1KB clusters -> ~2042 clusters, stays safely FAT12 (limit 4084)
static const uint32_t FAT_LBA=1,ROOT_LBA=7,DATA_LBA=11;   // reserved(1)+FAT(6)+root(4); metadata region is cluster-size-independent, so unchanged
static const uint32_t MAX_FILE_BYTES=(TOTAL_SECTORS-DATA_LBA)*512;   // ~1.99MB usable
static const uint32_t ADF_DEFAULT_SIZE=901120;             // standard DD ADF = 880KB
static const uint32_t ADF_HD_SIZE=1802240;                 // Amiga HD floppy = 1760KB (22 sectors/track, half-speed)
static const uint32_t HD_FLAG_BYTES=1258291;               // >1.2MB => flag as HD; extended-DD disks (~900-960KB) stay DD (A500-readable)
enum DiskMode{MODE_ADF=0,MODE_DSK=1};static DiskMode g_mode=MODE_ADF;
static const char*getOutputFilename(){return g_mode==MODE_ADF?"DISK.ADF":"DISK.DSK";}
uint8_t*g_disk=nullptr;
static void wr16(uint8_t*p,int o,uint16_t v){p[o]=v;p[o+1]=v>>8;}
static void wr32(uint8_t*p,int o,uint32_t v){p[o]=v;p[o+1]=v>>8;p[o+2]=v>>16;p[o+3]=v>>24;}
static void build_boot_sector(uint8_t*bs){memset(bs,0,512);bs[0]=0xEB;bs[1]=0x3C;bs[2]=0x90;memcpy(bs+3,"MSDOS5.0",8);wr16(bs,11,512);bs[13]=SECTORS_PER_CLUSTER;wr16(bs,14,RESERVED_SECTORS);bs[16]=NUM_FATS;wr16(bs,17,ROOT_ENTRIES);wr16(bs,19,(uint16_t)TOTAL_SECTORS);bs[21]=0xF8;wr16(bs,22,SECTORS_PER_FAT);wr16(bs,24,32);wr16(bs,26,64);bs[36]=0x80;bs[38]=0x29;wr32(bs,39,0x12345678);memcpy(bs+43,"ESP32MSC   ",11);memcpy(bs+54,"FAT12   ",8);bs[510]=0x55;bs[511]=0xAA;}
static void fat12_set(uint8_t*fat,uint16_t cl,uint16_t v){uint32_t i=(cl*3)/2;if(!(cl&1)){fat[i]=v&0xFF;fat[i+1]=(fat[i+1]&0xF0)|((v>>8)&0x0F);}else{fat[i]=(fat[i]&0x0F)|((v<<4)&0xF0);fat[i+1]=(v>>4)&0xFF;}}
static void build_fat(uint8_t*fat,uint32_t fsz){memset(fat,0,SECTORS_PER_FAT*512);fat[0]=0xF8;fat[1]=0xFF;fat[2]=0xFF;uint32_t clb=(uint32_t)SECTORS_PER_CLUSTER*512;uint32_t need=(fsz+clb-1)/clb;for(uint32_t i=0;i<need;i++)fat12_set(fat,2+i,i==need-1?0x0FFF:3+i);}
static void build_root(uint8_t*root,const char*name,uint32_t fsz){memset(root,0,ROOT_DIR_SECTORS*512);char n[8],e[3];memset(n,' ',8);memset(e,' ',3);char tmp[32];size_t L=strlen(name);if(L>31)L=31;memcpy(tmp,name,L);tmp[L]=0;for(size_t i=0;i<L;i++)tmp[i]=toupper(tmp[i]);const char*dot=strrchr(tmp,'.');size_t nl=dot?(dot-tmp):strlen(tmp);size_t el=dot?strlen(dot+1):0;for(size_t i=0;i<nl&&i<8;i++)n[i]=tmp[i];for(size_t i=0;i<el&&i<3;i++)e[i]=dot[1+i];memcpy(root,n,8);memcpy(root+8,e,3);root[11]=0x20;wr16(root,26,2);wr32(root,28,fsz);}
static void build_volume(const char*outName,uint32_t fsz){if(fsz>MAX_FILE_BYTES)fsz=MAX_FILE_BYTES;memset(g_disk,0,TOTAL_SECTORS*512);build_boot_sector(g_disk);build_fat(g_disk+RESERVED_SECTORS*512,fsz);build_root(g_disk+(RESERVED_SECTORS+SECTORS_PER_FAT)*512,outName,fsz);}
static int32_t onRead(uint32_t lba,uint32_t off,void*buf,uint32_t n){uint32_t s=lba*512+off;if(s+n>TOTAL_SECTORS*512)return 0;memcpy(buf,g_disk+s,n);return n;}
// ── Save-game persistence state (v4.8.0) ────────────────────────────────────
// STANDALONE: the Amiga writes to OUR RAM disk (we are the USB drive) — onWrite
// below ticks the dirty map; a settle timer + eject flush persist to .sav.adf.
// WIRELESS: the dongle ticks its own map and beacons; we pull + patch (espnow).
#define SV_IMG_MAX_SECTORS (TOTAL_SECTORS-DATA_LBA)     // 4085 (v4.9: 2MB disk)
#define SV_SETTLE_MS 3000
static int      g_saves_mode=1;                          // 0=OFF 1=COPY 2=OVERWRITE (SAVES=)
static uint8_t  g_sv_dirty[(SV_IMG_MAX_SECTORS+7)/8];
static volatile uint16_t g_sv_dirty_count=0;
static volatile uint32_t g_sv_last_write=0;
static volatile uint32_t g_sv_total_writes=0;
static String   g_loaded_path="";                        // SD path of the mounted image ("" = diag/none)
static String   g_sv_wl_path="";                         // SD path of the disk last FLUNG to the dongle
static uint32_t g_sv_wl_loadid=0;                        // dongle load_id it acked with
static inline bool svGet(const uint8_t*m,uint32_t i){return (m[i>>3]>>(i&7))&1;}
static inline void svSet(uint8_t*m,uint32_t i){m[i>>3]|=(uint8_t)(1u<<(i&7));}
static void svDirtyReset(){memset(g_sv_dirty,0,sizeof(g_sv_dirty));g_sv_dirty_count=0;g_sv_last_write=0;}
static uint32_t g_sv_img_size=0;                         // bytes of the mounted image (standalone tracking)

static int32_t onWrite(uint32_t lba,uint32_t off,uint8_t*buf,uint32_t n){uint32_t s=lba*512+off;if(s+n>TOTAL_SECTORS*512)return 0;memcpy(g_disk+s,buf,n);
  // v4.8.0: tick the dirty scorecard for every image sector this write touches
  // (assignment form, not ++ — C++20 deprecates ++ on volatile)
  g_sv_total_writes=g_sv_total_writes+1;
  uint32_t first=s/512,last=(s+n-1)/512,imgSecs=(g_sv_img_size+511)/512;
  if(imgSecs>SV_IMG_MAX_SECTORS)imgSecs=SV_IMG_MAX_SECTORS;
  for(uint32_t l=first;l<=last;l++){
    if(l<DATA_LBA)continue;uint32_t i=l-DATA_LBA;if(i>=imgSecs)continue;
    if(!svGet(g_sv_dirty,i)){svSet(g_sv_dirty,i);g_sv_dirty_count=g_sv_dirty_count+1;}
  }
  g_sv_last_write=millis();
  return n;}
static void usbEventCB(void*,esp_event_base_t,int32_t,void*){}
static uint32_t g_rev=1;
static void hardDetach(){MSC.mediaPresent(false);delay(100);tud_disconnect();delay(500);g_usb_online=false;}
static void hardAttach(){char r[8];snprintf(r,8,"%lu",(unsigned long)g_rev++);MSC.productRevision(r);MSC.mediaPresent(true);delay(50);tud_connect();delay(200);g_usb_online=true;}

// ════════════════════════════════════════════════════════════════════════════
// SD + INDEX CACHE
// ════════════════════════════════════════════════════════════════════════════
static String indexFilePath(){return g_mode==MODE_ADF?"/ADF/.index":"/DSK/.index";}
static void writeIndexCache(const std::vector<String>&v){File f=SD_MMC.open(indexFilePath().c_str(),FILE_WRITE);if(!f)return;f.println("#COUNT="+String(v.size()));for(auto&p:v)f.println(p);f.close();}
static bool readIndexCache(std::vector<String>&out){out.clear();File f=SD_MMC.open(indexFilePath().c_str(),FILE_READ);if(!f){return false;}
  long declaredCount=-1;
  while(f.available()){String l=f.readStringUntil('\n');l.trim();if(!l.length())continue;
    if(l.startsWith("#COUNT=")){declaredCount=l.substring(7).toInt();continue;}
    out.push_back(l);}
  f.close();
  // Validate: declared count must match actual lines read (catches partial writes/corruption)
  if(declaredCount>=0&&declaredCount!=(long)out.size()){out.clear();return false;}
  return!out.empty();}
// Draw bouncing Amiga ball animation frame + counter
static int ball_x=0,ball_y=0,ball_dx=3,ball_dy=2;
static void drawScanFrame(int count){
  // Ball area: centre of screen
  int areaX=60,areaY=gH/2-50,areaW=gW-120,areaH=60;
  // Erase previous ball
  gfx_fillRect(ball_x-8,ball_y-8,18,18,0x1082);
  // Move ball
  ball_x+=ball_dx;ball_y+=ball_dy;
  if(ball_x<=areaX||ball_x>=areaX+areaW-10){ball_dx=-ball_dx;ball_x+=ball_dx;}
  if(ball_y<=areaY||ball_y>=areaY+areaH-10){ball_dy=-ball_dy;ball_y+=ball_dy;}
  // Draw Amiga-style ball (red+white chequered circle)
  for(int dy=-6;dy<=6;dy++)for(int dx=-6;dx<=6;dx++){
    if(dx*dx+dy*dy>36)continue;
    bool checker=((dx+6)/3+(dy+6)/3)%2==0;
    gfx_drawPixel(ball_x+dx,ball_y+dy,checker?TFT_RED:TFT_WHITE);
  }
  // Counter text
  gfx_fillRect(gW/2-60,gH/2+20,120,12,0x1082);
  gfx_setTextSize(1);gfx_setTextColor(0x9BD6,0x1082);
  String msg="Found: "+String(count)+" files";
  gfx_setCursor(gW/2-gfx_textWidth(msg)/2,gH/2+22);gfx_print(msg);
  gfx_flush();
}

static std::vector<String> scanImagesAnimated(){
  std::vector<String>out;String dir=g_mode==MODE_ADF?"/ADF":"/DSK",ext=g_mode==MODE_ADF?".ADF":".DSK";
  // Init ball position
  ball_x=gW/2;ball_y=gH/2-30;ball_dx=3;ball_dy=2;
  // Draw initial scan screen
  gfx_fillScreen(0x1082);
  gfx_setTextSize(2);gfx_setTextColor(0xFC60,0x1082);
  {const char*s="SCANNING";int tw=gfx_textWidth(s);gfx_setCursor((gW-tw)/2,gH/2-60);gfx_print(s);}
  gfx_setTextSize(1);gfx_setTextColor(0x4A8A,0x1082);
  {const char*s="Building game index...";int tw=gfx_textWidth(s);gfx_setCursor((gW-tw)/2,gH/2+40);gfx_print(s);}
  gfx_flush();
  int count=0;uint32_t lastDraw=0;
  File root=SD_MMC.open(dir.c_str());
  if(root&&root.isDirectory()){File gd;while((gd=root.openNextFile())){
    String en=gd.name();if(!en.startsWith("/"))en=dir+"/"+en;
    if(gd.isDirectory()){
      {String leaf=en;int sl2=leaf.lastIndexOf('/');if(sl2>=0)leaf=leaf.substring(sl2+1);leaf.toUpperCase();
       // skip the SAMPLE example folder AND all dot-folders (.thumbs cache,
       // plus macOS SD litter like .Trashes / .Spotlight-V100 — free immunity)
       if(leaf=="SAMPLE"||leaf.startsWith(".")){gd.close();continue;}}
      File e;while((e=gd.openNextFile())){String fn=e.name();int sl=fn.lastIndexOf('/');if(sl>=0)fn=fn.substring(sl+1);String u=fn;u.toUpperCase();
      // v4.8.0: .sav.adf files attach to their master (INSERT prefers them) — never list
      // them as games. Orphaned .tmp from an interrupted save persist gets swept here.
      if(u.indexOf(".SAV.")>=0){
        if(u.endsWith(".TMP")){String fp=en+"/"+fn;if(!fp.startsWith("/"))fp="/"+fp;SD_MMC.remove(fp);}
        e.close();continue;}
      if(u.endsWith(ext)||u.endsWith(".IMG")||u.endsWith(".ADZ")){String fp=en+"/"+fn;if(!fp.startsWith("/"))fp="/"+fp;out.push_back(fp);count++;
        if(millis()-lastDraw>80){drawScanFrame(count);lastDraw=millis();}}e.close();}}
    else{String fn=en;int sl=fn.lastIndexOf('/');if(sl>=0)fn=fn.substring(sl+1);String u=fn;u.toUpperCase();
      if(u.indexOf(".SAV.")>=0){
        if(u.endsWith(".TMP"))SD_MMC.remove(en);
        gd.close();continue;}
      if(u.endsWith(ext)||u.endsWith(".IMG")||u.endsWith(".ADZ")){out.push_back(en);count++;
        if(millis()-lastDraw>80){drawScanFrame(count);lastDraw=millis();}}}
    gd.close();}root.close();}
  // Final count
  drawScanFrame(count);delay(500);
  std::sort(out.begin(),out.end());return out;
}

static bool listImages(fs::FS&fs,std::vector<String>&out){
  if(readIndexCache(out))return!out.empty();
  out=scanImagesAnimated();writeIndexCache(out);return!out.empty();
}

// ════════════════════════════════════════════════════════════════════════════
// /ADF/SAMPLE — a worked example of the folder layout, written ONCE when a
// blank card is provisioned. The browser IGNORES any folder named SAMPLE
// (case-insensitive), so it exists purely to be copied on a PC. If the user
// deletes it, it stays deleted (only recreated when /ADF itself is missing).
// ════════════════════════════════════════════════════════════════════════════
static const uint8_t SAMPLE_JPG[] PROGMEM = {
  255,216,255,224,0,16,74,70,73,70,0,1,1,0,0,1,0,1,0,0,
  255,219,0,67,0,12,8,9,11,9,8,12,11,10,11,14,13,12,14,18,
  30,20,18,17,17,18,37,27,28,22,30,44,39,46,46,43,39,43,42,49,
  55,70,59,49,52,66,52,42,43,61,83,62,66,72,74,78,79,78,47,59,
  86,92,85,76,91,70,77,78,75,255,219,0,67,1,13,14,14,18,16,18,
  36,20,20,36,75,50,43,50,75,75,75,75,75,75,75,75,75,75,75,75,
  75,75,75,75,75,75,75,75,75,75,75,75,75,75,75,75,75,75,75,75,
  75,75,75,75,75,75,75,75,75,75,75,75,75,75,75,75,75,75,255,192,
  0,17,8,0,150,0,150,3,1,34,0,2,17,1,3,17,1,255,196,0,
  27,0,1,0,3,1,1,1,1,0,0,0,0,0,0,0,0,0,0,4,
  5,6,7,3,1,2,255,196,0,76,16,0,1,2,3,2,2,17,18,6,
  1,5,0,0,0,0,0,0,1,2,3,4,5,17,18,33,84,6,19,20,
  21,22,23,54,81,83,85,131,145,163,178,193,209,225,7,34,49,51,53,65,
  68,100,101,116,117,146,148,162,164,179,210,226,50,52,97,115,130,177,129,35,
  36,82,113,161,255,196,0,23,1,1,1,1,1,0,0,0,0,0,0,0,
  0,0,0,0,0,0,3,2,1,255,196,0,38,17,1,1,0,1,2,6,
  2,2,3,1,0,0,0,0,0,0,0,1,2,17,50,3,18,49,81,129,
  177,19,33,65,145,113,161,193,209,255,218,0,12,3,1,0,2,17,3,17,
  0,63,0,231,0,2,236,128,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,30,144,96,172,85,214,106,118,84,56,247,166,
  202,66,155,143,118,98,101,37,160,162,42,172,69,98,191,14,178,34,22,153,
  201,73,219,223,131,127,57,9,173,70,162,34,37,136,135,211,178,201,248,99,
  44,109,191,87,79,215,252,77,74,21,49,234,141,135,91,188,247,96,107,115,
  35,146,213,239,39,100,244,208,159,142,112,93,36,89,31,206,203,254,227,127,
  179,92,45,215,240,99,44,235,117,102,244,39,227,156,23,72,208,159,142,112,
  93,38,144,28,107,86,111,66,126,57,193,116,141,9,248,231,5,210,105,10,
  186,220,220,121,92,167,40,125,219,215,173,192,139,111,99,92,10,253,9,248,
  231,5,210,52,39,227,156,23,73,231,158,243,187,55,184,222,98,125,26,122,
  98,102,105,204,141,18,243,81,138,182,93,68,195,106,107,1,5,249,28,148,
  129,102,107,170,229,55,191,15,251,117,117,186,253,133,255,0,162,186,169,75,
  89,38,178,52,8,201,53,39,17,110,182,59,90,173,235,187,237,114,47,97,
  127,180,194,94,228,147,193,255,0,151,33,26,95,83,85,173,195,142,106,105,
  126,147,202,220,111,54,189,190,191,166,108,0,101,96,0,0,0,0,210,214,
  24,216,112,105,104,198,163,81,100,33,45,136,150,97,91,85,87,125,109,51,
  70,154,183,218,169,94,143,131,202,106,109,169,103,191,31,42,192,1,150,222,
  242,63,157,151,253,198,255,0,102,184,200,200,254,118,95,247,27,253,154,224,
  0,0,5,38,73,60,31,249,114,23,101,69,126,12,88,217,70,85,13,239,
  178,245,183,90,171,103,96,10,18,211,35,223,157,127,237,175,246,132,44,199,
  51,139,198,245,20,177,161,75,198,133,54,247,68,133,17,137,149,170,90,230,
  170,119,208,15,214,73,60,31,249,114,17,165,245,53,90,220,56,228,156,146,
  120,63,242,228,35,75,234,106,181,184,113,205,97,215,247,233,62,38,217,252,
  207,113,155,0,25,88,0,0,0,0,52,213,190,213,74,244,124,30,83,50,
  105,171,125,170,149,232,248,60,166,166,218,150,123,241,242,172,0,25,109,233,
  45,17,33,76,66,136,235,85,24,244,114,217,250,41,123,159,178,219,28,109,
  228,231,51,192,13,14,126,203,108,113,183,147,156,103,236,182,199,27,121,57,
  204,240,3,67,159,178,219,28,109,228,231,25,251,45,177,198,222,78,115,60,
  0,208,231,236,182,199,27,121,57,198,126,203,108,113,183,147,156,207,0,44,
  42,211,240,167,114,172,169,175,75,150,219,121,19,191,103,49,246,95,83,85,
  173,195,142,87,22,50,250,154,173,110,28,115,88,117,253,250,79,137,182,127,
  51,220,102,192,6,86,0,0,0,0,13,53,111,181,82,189,31,7,148,204,
  154,106,223,106,165,122,62,15,41,169,182,165,158,252,124,189,50,51,68,207,
  249,248,146,185,163,40,185,9,98,94,185,122,219,21,18,203,45,77,115,77,
  165,183,149,126,27,238,43,250,153,247,122,63,154,187,142,195,166,18,182,234,
  172,140,30,150,222,85,248,111,184,105,109,229,95,134,251,141,100,253,198,198,
  87,57,90,231,35,18,200,110,192,171,133,127,2,235,244,30,19,42,235,147,
  49,149,19,43,143,14,35,17,111,97,91,26,182,96,179,244,93,242,87,139,
  98,248,240,117,209,154,210,219,202,191,13,247,13,45,188,171,240,223,113,168,
  135,106,205,202,177,124,29,235,15,221,119,34,53,127,201,231,10,22,87,35,
  47,25,205,135,9,136,216,86,221,95,197,215,49,109,92,9,173,255,0,170,
  115,229,167,195,59,179,122,91,121,87,225,190,225,165,183,149,126,27,238,53,
  147,113,96,196,153,128,235,101,226,178,227,211,253,71,165,219,109,111,126,197,
  194,79,101,151,82,237,150,89,130,206,193,185,158,182,196,242,195,150,75,221,
  132,210,219,202,191,13,247,13,45,188,171,240,223,113,188,6,181,172,233,28,
  86,187,77,206,138,172,121,28,183,46,202,174,245,247,110,219,107,81,123,22,
  174,185,250,151,212,213,107,112,227,147,114,117,170,169,237,207,229,180,133,47,
  169,170,214,225,199,45,135,95,23,210,28,94,158,103,184,205,128,12,172,0,
  0,0,0,26,106,223,106,165,122,62,15,41,153,52,213,190,213,74,244,124,
  30,83,83,109,75,61,248,249,93,117,51,238,244,127,53,119,29,135,70,153,
  139,26,29,220,166,95,46,182,219,122,244,109,155,231,57,234,103,221,232,254,
  106,238,59,14,152,71,46,170,196,60,211,59,136,112,205,25,166,119,16,225,
  154,76,7,29,67,205,51,184,135,12,209,154,103,113,14,25,164,192,4,60,
  211,59,136,112,205,25,166,119,16,225,154,76,0,67,205,51,184,135,12,210,
  68,187,226,196,98,172,104,57,83,173,178,237,228,118,13,124,7,160,3,146,
  100,235,85,83,219,159,203,105,10,95,83,85,173,195,142,77,201,214,170,167,
  183,63,150,210,20,190,166,171,91,135,28,190,31,229,244,143,23,167,153,238,
  51,96,3,42,128,0,0,0,6,154,183,218,169,94,143,131,202,102,77,53,
  111,181,82,189,31,7,148,212,219,82,207,126,62,87,93,76,251,189,31,205,
  93,199,97,209,166,101,32,205,93,203,153,122,237,182,97,84,179,120,231,61,
  76,251,189,31,205,93,199,97,209,166,98,198,135,119,41,151,203,173,182,222,
  189,27,102,249,43,213,88,241,206,153,45,135,223,119,56,206,153,45,135,223,
  119,56,205,51,184,135,12,209,154,103,113,14,25,167,62,221,51,166,75,97,
  247,221,206,51,166,75,97,247,221,206,51,76,238,33,195,52,102,153,220,67,
  134,104,251,12,233,146,216,125,247,115,140,233,146,216,125,247,115,140,211,59,
  136,112,205,25,166,119,16,225,154,62,195,58,100,182,31,125,220,228,137,121,
  120,82,204,86,65,109,214,170,219,101,170,184,127,201,31,52,206,226,28,51,
  73,18,239,139,17,138,177,160,229,78,182,203,183,145,216,53,240,28,28,163,
  39,90,170,158,220,254,91,72,82,250,154,173,110,28,114,110,78,181,85,61,
  185,252,182,144,165,245,53,90,220,56,229,240,255,0,47,164,120,189,60,207,
  113,155,0,25,84,0,0,0,0,52,213,190,213,74,244,124,30,83,50,105,
  171,11,126,90,149,17,189,115,22,70,27,17,201,133,47,37,168,169,110,186,
  119,205,77,181,44,247,227,229,117,212,207,187,209,252,213,220,118,29,48,226,
  244,42,204,197,14,109,243,50,172,132,247,190,26,195,84,136,138,169,98,170,
  47,121,83,88,188,211,18,173,139,201,122,143,250,137,89,170,178,186,96,57,
  158,152,149,108,94,75,212,127,212,52,196,171,98,242,94,163,254,163,156,180,
  213,211,1,204,244,196,171,98,242,94,163,254,161,166,37,91,23,146,245,31,
  245,14,90,106,233,128,230,122,98,85,177,121,47,81,255,0,80,211,18,173,
  139,201,122,143,250,135,45,53,116,192,115,61,49,42,216,188,151,168,255,0,
  168,105,137,86,197,228,189,71,253,67,150,154,160,100,235,85,83,219,159,203,
  105,10,95,83,85,173,195,142,120,213,170,49,106,213,8,179,179,13,99,98,
  197,178,212,98,42,55,2,34,119,213,117,143,104,75,115,35,53,101,127,90,
  145,29,5,140,85,193,121,200,235,85,19,93,108,194,91,14,190,47,164,120,
  189,60,207,113,155,0,25,88,0,0,0,0,39,211,42,241,233,204,124,54,
  195,131,49,5,235,121,97,71,101,246,35,191,228,137,222,91,48,16,0,150,
  206,140,220,102,83,74,189,209,59,246,170,149,236,221,35,68,239,218,170,87,
  179,116,148,64,215,62,76,124,88,118,94,232,157,251,85,74,246,110,145,162,
  119,237,85,43,217,186,74,32,57,242,62,44,59,47,116,78,253,170,165,123,
  55,72,209,59,246,170,149,236,221,37,16,28,249,31,22,29,151,186,39,126,
  213,82,189,155,164,104,157,251,85,74,246,110,146,136,14,124,143,139,14,203,
  221,19,191,106,169,94,205,210,52,78,253,170,165,123,55,73,68,7,62,71,
  197,135,101,238,137,223,181,84,175,102,233,43,170,85,56,245,40,140,116,84,
  135,14,28,52,178,28,40,77,186,198,107,216,159,170,225,82,24,23,43,93,
  156,60,113,186,200,0,12,168,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,255,217
};

static void ensureSampleFolder(){
  if(SD_MMC.exists("/ADF/SAMPLE"))return;
  SD_MMC.mkdir("/ADF/SAMPLE");
  File f=SD_MMC.open("/ADF/SAMPLE/Sample.adf",FILE_WRITE);
  if(f){f.print(
    "This placeholder shows WHERE your disk image goes.\r\n"
    "A real game is an .adf disk image (usually 880KB for Amiga).\r\n"
    "The browser ignores any folder named SAMPLE - copy the layout,\r\n"
    "don't play in here.\r\n");f.close();}
  f=SD_MMC.open("/ADF/SAMPLE/Sample.nfo",FILE_WRITE);
  if(f){f.print(
    "Title: Sample Game Name\r\n"
    "Blurb: 1991 - Publisher Name - one line about the game\r\n"
    "\r\n"
    "This folder is an EXAMPLE ONLY - the browser ignores any folder\r\n"
    "named SAMPLE. Copy this layout for real games:\r\n"
    "\r\n"
    "  /ADF/YourGame/YourGame.adf   the disk image\r\n"
    "  /ADF/YourGame/YourGame.jpg   cover art (JPEG or PNG, any size)\r\n"
    "  /ADF/YourGame/YourGame.nfo   this info file (plain text)\r\n"
    "  /ADF/YourGame/YourGame.rtfm  how-to-play manual (plain text, optional)\r\n"
    "\r\n"
    "NFO rules:\r\n"
    "  'Title:' overrides the display name (any case: TITLE:/title:).\r\n"
    "  'Blurb:' or 'Description:' = info text shown on the cover panel.\r\n"
    "  Or skip the labels: first line = title, following lines = blurb.\r\n"
    "\r\n"
    "Multi-disk games: YourGame-1.adf, YourGame-2.adf, ...\r\n"
    "Same pattern applies in /DSK for .dsk images.\r\n");f.close();}
  f=SD_MMC.open("/ADF/SAMPLE/Sample.jpg",FILE_WRITE);
  if(f){f.write(SAMPLE_JPG,sizeof(SAMPLE_JPG));f.close();}
  // v4.9.4: a self-documenting manual — the reader opened by the book button explains itself.
  f=SD_MMC.open("/ADF/SAMPLE/Sample.rtfm",FILE_WRITE);
  if(f){f.print(
    "HOW TO PLAY - and how this MANUAL works\r\n"
    "=======================================\r\n"
    "\r\n"
    "You are reading the GTi manual viewer. This text came\r\n"
    "from a plain-text file sitting next to the game:\r\n"
    "\r\n"
    "  /ADF/SAMPLE/Sample.rtfm\r\n"
    "\r\n"
    "Any game can have one. Drop a file named the same as\r\n"
    "the disk, ending in .rtfm, beside it:\r\n"
    "\r\n"
    "  /ADF/YourGame/YourGame.rtfm\r\n"
    "\r\n"
    "...and a little book button appears on the cover art.\r\n"
    "Tap it to read the game's instructions - controls,\r\n"
    "cheats, copy-protection answers, whatever you like.\r\n"
    "\r\n"
    "USING THE READER\r\n"
    "  - Drag up or down to scroll; flick for a fast spin.\r\n"
    "  - SIZE cycles SMALL / NORMAL / LARGE text, following\r\n"
    "    your menu font setting.\r\n"
    "  - CLOSE (or tap the page) returns to the library.\r\n"
    "\r\n"
    "TIPS\r\n"
    "  - Keep it plain ASCII text. Accents and smart quotes\r\n"
    "    are cleaned up for you, but plain is safest.\r\n"
    "  - Blank lines make paragraphs. Short lines read best.\r\n"
    "  - Manuals are meant to be short and handy - a page or\r\n"
    "    two of how-to-play basics.\r\n"
    "\r\n"
    "Read the fine manual. :)\r\n"
    "\r\n"
    "- OMEGAWARE\r\n");f.close();}
}

// ── Game cache — defined after STATE section below ──

// ════════════════════════════════════════════════════════════════════════════
// FILE HELPERS
// ════════════════════════════════════════════════════════════════════════════
static String basenameNoExt(const String&p){int s=p.lastIndexOf('/'),d=p.lastIndexOf('.');String b=s>=0?p.substring(s+1):p;if(d>s)b=b.substring(0,d-(s>=0?s+1:0));return b;}
static String filenameOnly(const String&p){int s=p.lastIndexOf('/');return s>=0?p.substring(s+1):p;}
static String parentDir(const String&p){int s=p.lastIndexOf('/');return s>0?p.substring(0,s):"/";}
// ── TOSEC "(Disk n of m)" awareness (v4.8.4). If a filename carries a TOSEC
//    disk token we read n as the disk number and treat the text BEFORE the
//    token as the game key, so multi-disk TOSEC sets group exactly like our
//    own -1/-2 naming. Matches (Disk ...) and (Disc ...), case-insensitive.
//    Purely additive: a TOSEC name ends in ) or ], so the old trailing "-n"
//    path never fires on it, and single-disk / folder-per-game libraries are
//    untouched. NOTE: grouping keys on the text before the token, so keep ONE
//    rip per title together — two rips of the same disk 1 would merge. ──
static int tosecDiskTokenPos(const String&b){
  String lb=b;lb.toLowerCase();int p=-1;
  const char*kw[]={"(disk ","(disc "};
  for(auto k:kw){int q=lb.indexOf(k);if(q>=0&&(p<0||q<p))p=q;}
  return p;   // index of the '(' , or -1
}
static int tosecDiskNumber(const String&b){int p=tosecDiskTokenPos(b);if(p<0)return 0;int n=b.substring(p+6).toInt();return n>0?n:0;}
static String getGameBaseName(const String&fp){String b=basenameNoExt(filenameOnly(fp));
  int tp=tosecDiskTokenPos(b);if(tp>0&&tosecDiskNumber(b)>0){String g=b.substring(0,tp);g.trim();if(g.length())return g;}
  int d=b.lastIndexOf('-');if(d>0&&d<(int)b.length()-1){bool num=true;for(int i=d+1;i<(int)b.length();i++)if(!isDigit(b[i])){num=false;break;}if(num)return b.substring(0,d);}return b;}
static int getDiskNumber(const String&fp){String b=basenameNoExt(filenameOnly(fp));
  int tn=tosecDiskNumber(b);if(tn>0)return tn;
  int d=b.lastIndexOf('-');if(d>0){int n=b.substring(d+1).toInt();if(n>0)return n;}return 0;}

static bool findInDir(const String&dir,const String&target,String&out){
  String tgt=target;tgt.toLowerCase();File d=SD_MMC.open(dir.c_str());if(!d||!d.isDirectory())return false;
  File f;while((f=d.openNextFile())){if(!f.isDirectory()){String nm=f.name();int sl=nm.lastIndexOf('/');if(sl>=0)nm=nm.substring(sl+1);String nl=nm;nl.toLowerCase();if(nl==tgt){out=dir+"/"+nm;f.close();d.close();return true;}}f.close();}d.close();return false;
}
static bool findNFOFor(const String&p,String&out){
  String b=basenameNoExt(filenameOnly(p)),d=parentDir(p),gb=getGameBaseName(p);
  if(findInDir(d,b+".nfo",out))return true;if(gb!=b&&findInDir(d,gb+".nfo",out))return true;return false;
}
static bool findJPGFor(const String&p,String&out){
  String b=basenameNoExt(filenameOnly(p)),d=parentDir(p),gb=getGameBaseName(p);
  const char*exts[]={".jpg",".jpeg",".png",".JPG",".JPEG",".PNG"};
  // Try exact basename in same dir
  for(auto e:exts){String c=d+"/"+b+e;if(SD_MMC.exists(c.c_str())){out=c;return true;}}
  // Try game base name (for multi-disk: GameName.jpg instead of GameName-1.jpg)
  if(gb!=b){for(auto e:exts){String c=d+"/"+gb+e;if(SD_MMC.exists(c.c_str())){out=c;return true;}}}
  // Try folder name as JPG name (common pattern: /ADF/GameName/GameName.jpg where folder = game)
  String folderName=d;int ls=folderName.lastIndexOf('/');if(ls>0)folderName=folderName.substring(ls+1);
  if(folderName.length()&&folderName!=b&&folderName!=gb){
    for(auto e:exts){String c=d+"/"+folderName+e;if(SD_MMC.exists(c.c_str())){out=c;return true;}}}
  return false;
}
// v4.9.2: per-game manual (.rtfm) lookup — mirrors findJPGFor. Plain-text "how to play"
// file beside the ADF; opened full-screen by the book button on the cover panel.
static bool manualFor(const String&p,String&out){
  String b=basenameNoExt(filenameOnly(p)),d=parentDir(p),gb=getGameBaseName(p);
  const char*exts[]={".rtfm",".RTFM"};
  for(auto e:exts){String c=d+"/"+b+e;if(SD_MMC.exists(c.c_str())){out=c;return true;}}
  if(gb!=b){for(auto e:exts){String c=d+"/"+gb+e;if(SD_MMC.exists(c.c_str())){out=c;return true;}}}
  String folderName=d;int ls=folderName.lastIndexOf('/');if(ls>0)folderName=folderName.substring(ls+1);
  if(folderName.length()&&folderName!=b&&folderName!=gb){
    for(auto e:exts){String c=d+"/"+folderName+e;if(SD_MMC.exists(c.c_str())){out=c;return true;}}}
  return false;
}

// ════════════════════════════════════════════════════════════════════════════
// STATE
// ════════════════════════════════════════════════════════════════════════════
static bool g_wireless_mode=false,g_loop_cracktro=false,g_info_showing=false;
// ── Screensaver (undocumented, folder-gated /screensaver/*.jpg) ──
static std::vector<String> g_ss_paths;
static bool g_ss_have=false, g_ss_enabled=true;
static bool g_ss_claude=false;   // /screensaver/ exists but holds no JPGs -> bounce the Claude starburst
static uint32_t g_last_touch_ms=0;
static uint16_t* g_ss_buf=NULL; static int g_ss_w=0, g_ss_h=0;
#define SS_MAX      150        // longest side of the bouncing image (virtual-canvas px)
#define SS_IDLE_MS  600000UL   // 10 min idle (browsing / nothing loaded)
#define SS_LOAD_MS  120000UL   // 2 min idle once a game is loaded (showcase)
static uint32_t g_ss_idle_ms=SS_IDLE_MS, g_ss_load_ms=SS_LOAD_MS;   // hidden SS_IDLE=/SS_LOAD= override (seconds)
static int g_dongle_cap=32;   // CONFIG.TXT CAP= : max wireless dongles to discover/cast (1..64)
static int g_hivemind=1;      // v4.8.1 (undocumented HIVEMIND=): 1 = FLING fans out to all MuCa dongles (classic), 0 = paired dongle only
static int g_cracktro=0;      // CONFIG.TXT CRACKTRO= : boot demo style 1..6, or 0 = pick one at random each boot
static int g_car_bootmode=0;  // CONFIG.TXT CAROUSEL= : default boot VIEW — 0/OFF=list, 1/ON=reel, 2=LAST (restore last view, remembered in /.gtiview). v4.8.5+: carousel is ALWAYS available via the flip toggle regardless.
// ── Item 4: load/eject behaviour toggles (all default OFF = safest) ──
static bool g_tapload=false;    // ON = tapping the already-selected row loads it (old double-tap behaviour)
static bool g_hotswap=false;    // ON = tapping another disk while loaded swaps to it instantly
static bool g_forceswap=false;  // ON = swap disk bytes in place without the USB eject/re-attach cycle
static int g_info_rescan_btn_y=0,g_info_reset_btn_y=0,g_info_pair_now_btn_y=0,g_info_font_btn_y=0,g_info_hive_btn_y=0;
static int g_info_x=0,g_info_w=150,g_info_rot_btn_y=0,g_info_comp_btn_y=0,g_info_mode_btn_y=0,g_info_bottom=0,g_info_bh=22;
static String g_manual_path=""; static int g_manual_bx=0,g_manual_by=0,g_manual_bw=0,g_manual_bh=0;  // v4.9.2 .rtfm book button rect
struct GameEntry{String name;int first_file_idx;int disk_count;String jpg_path;std::vector<int>disk_indices;bool fav=false;uint16_t plays=0;};
static std::vector<String>g_files;static std::vector<GameEntry>g_games;
static int g_sel=0,g_scroll=0,g_disk_sel=0,g_loaded_game_idx=-1,g_loaded_disk_idx=-1;
static int g_disk_page=0;  // current page of disk selector (6 disks/page)
#define DISKS_PER_PAGE 6
static String g_loaded_name="";static bool g_loaded=false;
// ── Smooth list scroll + A-Z index state ──
static float g_scrollPx=0;                 // pixel scroll offset (source of truth)
static int   g_az_page=0;                  // 0 = #/A-M, 1 = N-Z
static char  g_active_letter='A';          // letter the index highlights / pages to
static int g_marquee_off=0,g_marquee_sel=-1,g_marquee_dir=1;static uint32_t g_marquee_pause=0;   // bounce scroll of the selected over-long name
// touch/drag/inertia
static bool  g_touch_active=false,g_touch_moved=false,g_touch_inlist=false,g_inertia_on=false;
static int   g_touch_x0=0,g_touch_y0=0,g_touch_lastY=0,g_touch_release=0; static float g_touch_px0=0,g_touch_vel=0,g_inertia_vel=0;
static uint32_t g_touch_lastMs=0;
#define DRAG_THRESH 12          // px of finger travel before a press becomes a scroll (tolerates a firm press)
#define RELEASE_FRAMES 3        // consecutive no-touch frames before we treat the finger as lifted (debounces panel blips)
static char bucketOf(const String&name){char c=toupper(name.charAt(0));return (c>='A'&&c<='Z')?c:'#';}

// ── Game cache — caches buildGameList output so NFO/JPG lookups only happen once ──
static String gameCachePath(){return g_mode==MODE_ADF?"/ADF/.gamecache":"/DSK/.gamecache";}

static void writeGameCache(){
  File f=SD_MMC.open(gameCachePath().c_str(),FILE_WRITE);if(!f)return;
  f.println("#FILES="+String(g_files.size()));  // bind to the index this was built from
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
    if(e.jpg_path=="?")e.jpg_path="";  // never trust a persisted sentinel
    String indices=line.substring(p4+1);
    if(indices.length()){int pos=0;while(pos<(int)indices.length()){int comma=indices.indexOf(',',pos);if(comma<0)comma=indices.length();e.disk_indices.push_back(indices.substring(pos,comma).toInt());pos=comma+1;}}
    if(e.first_file_idx>=0&&e.first_file_idx<(int)g_files.size()) g_games.push_back(e);
  }
  f.close();
  // If the game cache was built from a different-sized index, it's stale — force rebuild
  if(declaredFiles>=0&&declaredFiles!=(long)g_files.size()){g_games.clear();return false;}
  return!g_games.empty();
}

// .nfo format (documented in the repo README):
//   Labelled (labels are CASE-INSENSITIVE): "Title: ..." overrides the display
//   name; "Blurb: ..." (or "Description: ...") starts the info text, following
//   label-less lines are appended (up to 3).
//   Simple (no labels): first non-empty line = title, everything after = blurb.
static void parseNFO(const String&txt,String&t,String&b){
  t="";b="";if(!txt.length())return;
  std::vector<String>lines;int pos=0;while(pos<(int)txt.length()){int nl=txt.indexOf('\n',pos);if(nl<0)nl=txt.length();String L=txt.substring(pos,nl);L.trim();lines.push_back(L);pos=nl+1;}
  for(size_t i=0;i<lines.size();i++){
    String Ll=lines[i];Ll.toLowerCase();
    if(!t.length()&&Ll.startsWith("title:")){t=lines[i].substring(6);t.trim();}
    if(!b.length()&&(Ll.startsWith("blurb:")||Ll.startsWith("description:"))){b=lines[i].substring(lines[i].indexOf(':')+1);b.trim();
      for(size_t j=i+1;j<lines.size()&&j<i+4;j++){if(lines[j].indexOf(':')>0)break;if(lines[j].length()){b+="\n"+lines[j];}}}}
  // Unlabelled fallback: first non-empty line = title, the rest = blurb
  // (this is the format of the bulk-enriched .nfo library — v4.6.0 fix: the JC
  //  previously showed only the title from these; blurbs were silently dropped)
  if(!t.length()){for(size_t i=0;i<lines.size();i++)if(lines[i].length()){t=lines[i];break;}}
  if(!b.length()){bool af=false;for(size_t i=0;i<lines.size();i++){if(!lines[i].length())continue;if(!af){af=true;continue;}if(b.length())b+="\n";b+=lines[i];}}
  t.trim();b.trim();
}

static void buildGameList(){
  g_games.clear();std::vector<bool>used(g_files.size(),false);
  for(int i=0;i<(int)g_files.size();i++){if(used[i])continue;
    String bn=getGameBaseName(g_files[i]);int dn=getDiskNumber(g_files[i]);String dir=parentDir(g_files[i]);
    GameEntry e;e.first_file_idx=i;e.disk_count=1;e.disk_indices.push_back(i);
    if(dn>0){e.name=bn;for(int j=i+1;j<(int)g_files.size();j++){if(used[j])continue;if(parentDir(g_files[j])==dir&&getGameBaseName(g_files[j])==bn&&getDiskNumber(g_files[j])>0){used[j]=true;e.disk_count++;e.disk_indices.push_back(j);}}
      // Sort disk_indices by disk number so D1,D2,D3 are in order
      std::sort(e.disk_indices.begin(),e.disk_indices.end(),[](int a,int b){return getDiskNumber(g_files[a])<getDiskNumber(g_files[b]);});
      // Point first_file_idx at the lowest-numbered disk (for cover/NFO lookup)
      e.first_file_idx=e.disk_indices[0];
    }
    else e.name=basenameNoExt(filenameOnly(g_files[i]));
    used[i]=true;
    // NO NFO/JPG lookups here — done lazily in drawCoverPanel
    g_games.push_back(e);
  }
  std::sort(g_games.begin(),g_games.end(),[](const GameEntry&a,const GameEntry&b){String al=a.name,bl=b.name;al.toLowerCase();bl.toLowerCase();return al<bl;});
  writeGameCache();
}
// ── Per-game stats: favourites + play counts, keyed by name, survives RESCAN ──
static String statsPath(){return "/.gtistats";}
static void applyStats(){
  File f=SD_MMC.open(statsPath().c_str(),FILE_READ);if(!f)return;
  while(f.available()){String line=f.readStringUntil('\n');line.trim();if(!line.length())continue;
    int p1=line.indexOf('|');if(p1<0)continue;int p2=line.indexOf('|',p1+1);if(p2<0)continue;
    int fv=line.substring(0,p1).toInt();int pl=line.substring(p1+1,p2).toInt();String nm=line.substring(p2+1);
    for(auto&g:g_games){if(g.name==nm){g.fav=(fv!=0);g.plays=(uint16_t)pl;break;}}}
  f.close();
}
static void saveStats(){
  std::vector<String>names;std::vector<int>favs,plays;
  File f=SD_MMC.open(statsPath().c_str(),FILE_READ);
  if(f){while(f.available()){String line=f.readStringUntil('\n');line.trim();if(!line.length())continue;
    int p1=line.indexOf('|');if(p1<0)continue;int p2=line.indexOf('|',p1+1);if(p2<0)continue;
    names.push_back(line.substring(p2+1));favs.push_back(line.substring(0,p1).toInt());plays.push_back(line.substring(p1+1,p2).toInt());}f.close();}
  for(auto&g:g_games){if(!g.fav&&g.plays==0)continue;int idx=-1;for(size_t i=0;i<names.size();i++)if(names[i]==g.name){idx=(int)i;break;}
    if(idx<0){names.push_back(g.name);favs.push_back(g.fav?1:0);plays.push_back(g.plays);}else{favs[idx]=g.fav?1:0;plays[idx]=g.plays;}}
  File w=SD_MMC.open(statsPath().c_str(),FILE_WRITE);if(!w)return;
  for(size_t i=0;i<names.size();i++){if(favs[i]==0&&plays[i]==0)continue;w.print(favs[i]);w.print("|");w.print(plays[i]);w.print("|");w.println(names[i]);}
  w.close();
}
static char active_letters[27];static int active_letter_count=0;
static void buildActiveLetters(){bool s[26]={};bool hasHash=false;
  for(auto&g:g_games){char c=toupper(g.name.charAt(0));if(c>='A'&&c<='Z')s[c-'A']=true;else hasHash=true;}
  active_letter_count=0;
  if(hasHash)active_letters[active_letter_count++]='#';                 // non-alpha titles bucket to '#', sorted before A
  for(int i=0;i<26;i++)if(s[i])active_letters[active_letter_count++]='A'+i;}

// ════════════════════════════════════════════════════════════════════════════
// THEME SYSTEM
// ════════════════════════════════════════════════════════════════════════════
struct Theme{const char*name;uint16_t bg,panel,bar,sel,sep,dim,mid,lit,green,orange,amber,blue,now,accent,circ,circ_text;};
static const Theme THEMES[]={
  {"NAVY",  0x1082,0x18C3,0x2104,0x2945,0x2103,0x4A8A,0x6B6D,0x9BD6,0x2D6B,0xFC60,0xFD00,0x4C5F,0x0B26,0x5D1F,0x3186,TFT_WHITE},
  {"EMBER", 0x0800,0x1000,0x1800,0x5820,0x1000,0x5820,0x8440,0xC8A0,0x0560,0xFF40,0xFC00,0x4A9F,0x1000,0x7800,0x3800,TFT_WHITE},
  {"MATRIX",0x0020,0x0040,0x0060,0x0340,0x0040,0x0340,0x0580,0x07C0,0x07E0,0x07E0,0x0FE0,0x07FF,0x0060,0x0380,0x0300,0x07C0},
  {"PAPER", 0xEF5C,0xF7BE,0xFFFF,0x39E7,0xCE59,0x8C51,0x6B4D,0x2124,0x0680,0xE880,0xFD00,0x0C5F,0x0A44,0x4810,0xC618,0x2124},
  {"SYNTH", 0x1001,0x2003,0x3005,0x5008,0x2003,0x600C,0x900F,0xC09F,0x4BE0,0xFC1F,0xE81F,0xA01F,0x2003,0x8010,0x5008,0xE81F},
  {"GOLD",  0x1000,0x1800,0x2000,0x4200,0x1800,0x5240,0x7440,0xC5A0,0x0560,0xFCA0,0xFCC0,0xFCA0,0x1800,0x3200,0x3200,0xFCC0},
};
static const int NUM_THEMES=6;static int g_theme_idx=0;
static uint16_t COL_BG,COL_PANEL,COL_BAR,COL_SEL,COL_SEP,COL_DIM,COL_MID,COL_LIT;
static uint16_t COL_GREEN,COL_ORANGE,COL_AMBER,COL_BLUE,COL_NOW,COL_ACCENT,COL_CIRC,COL_CIRC_TEXT;

static void applyTheme(int idx){
  g_theme_idx=idx%NUM_THEMES;const Theme&t=THEMES[g_theme_idx];
  COL_BG=t.bg;COL_PANEL=t.panel;COL_BAR=t.bar;COL_SEL=t.sel;COL_SEP=t.sep;
  COL_DIM=t.dim;COL_MID=t.mid;COL_LIT=t.lit;COL_GREEN=t.green;COL_ORANGE=t.orange;
  COL_AMBER=t.amber;COL_BLUE=t.blue;COL_NOW=t.now;COL_ACCENT=t.accent;COL_CIRC=t.circ;COL_CIRC_TEXT=t.circ_text;
}

static void saveConfigKey(const String&key,const String&val){
  String lines="";bool written=false;File fr=SD_MMC.open("/CONFIG.TXT",FILE_READ);
  if(fr){while(fr.available()){String l=fr.readStringUntil('\n');l.trim();if(l.startsWith(key+"=")){lines+=key+"="+val+"\n";written=true;}else lines+=l+"\n";}fr.close();}
  if(!written)lines+=key+"="+val+"\n";File fw=SD_MMC.open("/CONFIG.TXT",FILE_WRITE);if(fw){fw.print(lines);fw.close();}
}

// ── Dongle friendly names (touchscreen-side only; keyed to the dongle MAC) ──
static String macKey(const String&mac){String h="";for(unsigned i=0;i<mac.length();i++){char c=mac[i];if(c!=':')h+=(char)toupper(c);}return "DONGLE_"+h;}
static String getDongleName(const String&mac){String key=macKey(mac),r="";File f=SD_MMC.open("/CONFIG.TXT",FILE_READ);if(!f)return r;
  while(f.available()){String l=f.readStringUntil('\n');l.trim();if(l.startsWith("#"))continue;if(l.startsWith(key+"=")){r=l.substring(key.length()+1);r.trim();break;}}f.close();return r;}
static void setDongleName(const String&mac,const String&name){String key=macKey(mac);
  String lines="";bool written=false,hasHeading=false;File fr=SD_MMC.open("/CONFIG.TXT",FILE_READ);
  if(fr){while(fr.available()){String l=fr.readStringUntil('\n');l.trim();
    if(l=="# Dongle names")hasHeading=true;
    if(l.startsWith(key+"=")){lines+=key+"="+name+"\n";written=true;}else lines+=l+"\n";}fr.close();}
  if(!written){if(!hasHeading)lines+="\n# Dongle names\n";lines+=key+"="+name+"\n";}
  File fw=SD_MMC.open("/CONFIG.TXT",FILE_WRITE);if(fw){fw.print(lines);fw.close();}}
static uint8_t hexNib(char c){ if(c>='0'&&c<='9')return c-'0'; c=(char)toupper(c); if(c>='A'&&c<='F')return c-'A'+10; return 0; }
// Collect MACs of dongles named with a "MuCa-" prefix (the undocumented multicast group), from CONFIG.TXT.
static int enumMuCaDongles(uint8_t macs[][6], int maxN){
  int n=0; File f=SD_MMC.open("/CONFIG.TXT",FILE_READ); if(!f)return 0;
  while(f.available()&&n<maxN){
    String l=f.readStringUntil('\n'); l.trim();
    if(!l.startsWith("DONGLE_"))continue;
    int eq=l.indexOf('='); if(eq<0)continue;
    String key=l.substring(0,eq), val=l.substring(eq+1); val.trim();
    String vu=val; vu.toUpperCase(); if(!vu.startsWith("MUCA-"))continue;
    String hex=key.substring(7); if(hex.length()<12)continue;   // strip "DONGLE_"
    for(int i=0;i<6;i++) macs[n][i]=(uint8_t)((hexNib(hex[i*2])<<4)|hexNib(hex[i*2+1]));
    n++;
  }
  f.close(); return n;
}

// Forward decls
static void applyFont(int f);   // defined with the layout section; used by loadConfig
static void drawFullUI();
static String savPathFor(const String&adfPath);   // v4.8.0 saves — defined with the save engine
static bool savExistsFor(const String&adfPath);
static void drawListAndCover();
static bool doLoadSelected(const String&p);
static void doUnload();

static void cycleTheme(){applyTheme((g_theme_idx+1)%NUM_THEMES);saveConfigKey("THEME",String(g_theme_idx));drawFullUI();gfx_flush();}
static bool g_espnow_started=false;
static void ensureEspNow(){if(!g_espnow_started){espnowBegin();g_espnow_started=true;}}

static void generateDefaultConfig(){
  if(SD_MMC.exists("/CONFIG.TXT"))return;  // never overwrite an existing config
  File f=SD_MMC.open("/CONFIG.TXT",FILE_WRITE);if(!f)return;
  f.println("# Gotek Touchscreen Interface - OMEGAWARE");
  f.println("# Edit values below, then reboot.");
  f.println("");
  f.println("# Theme: 0=NAVY 1=EMBER 2=MATRIX 3=PAPER 4=SYNTH 5=GOLD");
  f.println("THEME=0");
  f.println("");
  f.println("# Transfer mode: STANDALONE (USB to Gotek) or WIRELESS (ESP-NOW to dongle)");
  f.println("MODE=STANDALONE");
  f.println("");
  f.println("# Loop cracktro splash: 1=loop until tapped, 0=auto-dismiss after 6s");
  f.println("LOOP=0");
  f.println("");
  f.println("# Boot cracktro style: 0=random each boot, or pick one:");
  f.println("#   1=COPPER CLASSIC  2=STARFIELD  3=RAINBOW RASTER");
  f.println("#   4=PLASMA  5=BOING BALL  6=SYNTHWAVE");
  f.println("CRACKTRO=0");
  f.println("");
  f.println("# Font size: SMALL, NORMAL, LARGE");
  f.println("FONT=NORMAL");
  f.println("");
  f.println("# Screen rotation in degrees: 0 or 180 = landscape, 90 or 270 = portrait.");
  f.println("# Easiest to set with the ROTATE button on the INFO screen (each tap = +90).");
  f.println("ROTATE=0");
  f.println("");
  f.println("# COMPACT: OFF = cover art + list, ON = maximise the game list (cover collapses to a strip)");
  f.println("COMPACT=OFF");
  f.println("");
  f.println("# CAP: max wireless dongles the scan will list (default 32, up to 64)");
  f.println("CAP=32");
  f.println("");
  f.println("# Load/eject behaviour (all OFF = safest: select, then press the button)");
  f.println("# TAPLOAD: ON = tapping the already-highlighted game row loads it (old double-tap)");
  f.println("TAPLOAD=OFF");
  f.println("# HOTSWAP: ON = tapping another disk while loaded swaps to it instantly");
  f.println("HOTSWAP=OFF");
  f.println("# FORCESWAP: ON = swap disk contents without the USB eject/re-attach cycle");
  f.println("FORCESWAP=OFF");
  f.println("");
  f.println("# SAVES: save-game persistence when the Amiga writes to the disk.");
  f.println("#   OFF       = writes live only until eject/power-off (classic behaviour)");
  f.println("#   COPY      = writes are kept as GameName.sav.adf beside the master (recommended)");
  f.println("#   OVERWRITE = writes are patched straight into the master .adf");
  f.println("SAVES=COPY");
  f.println("");
  f.println("# Wireless dongle MAC (auto-filled when you pair via INFO screen)");
  f.println("# XIAO_MAC=");
  f.close();
}

// Self-heal: after a firmware update adds a new key, an existing CONFIG.TXT won't
// have it (generateDefaultConfig never overwrites). Append any documented key the
// file is missing — with its comment + default — so upgraders get an editable line
// without their other settings being touched. Only writes when something is missing;
// hidden keys (SCREENSAVER/SS_*) are intentionally excluded so they stay undocumented.
static void selfHealConfig(){
  if(!SD_MMC.exists("/CONFIG.TXT"))return;   // fresh cards already get the full template
  struct CfgKey{const char*key;const char*block;};
  static const CfgKey KEYS[]={
    {"THEME",    "\n# Theme: 0=NAVY 1=EMBER 2=MATRIX 3=PAPER 4=SYNTH 5=GOLD\nTHEME=0\n"},
    {"MODE",     "\n# Transfer mode: STANDALONE (USB to Gotek) or WIRELESS (ESP-NOW to dongle)\nMODE=STANDALONE\n"},
    {"LOOP",     "\n# Loop cracktro splash: 1=loop until tapped, 0=auto-dismiss after 6s\nLOOP=0\n"},
    {"CRACKTRO", "\n# Boot cracktro style: 0=random each boot, or pick one:\n#   1=COPPER CLASSIC  2=STARFIELD  3=RAINBOW RASTER\n#   4=PLASMA  5=BOING BALL  6=SYNTHWAVE\nCRACKTRO=0\n"},
    {"FONT",     "\n# Font size: SMALL, NORMAL, LARGE\nFONT=NORMAL\n"},
    {"ROTATE",   "\n# Screen rotation in degrees: 0 or 180 = landscape, 90 or 270 = portrait.\nROTATE=0\n"},
    {"COMPACT",  "\n# COMPACT: OFF = cover art + list, ON = maximise the game list (cover collapses to a strip)\nCOMPACT=OFF\n"},
    {"CAP",      "\n# CAP: max wireless dongles the scan will list (default 32, up to 64)\nCAP=32\n"},
    {"TAPLOAD",  "\n# TAPLOAD: ON = tapping the already-highlighted game row loads it (old double-tap)\nTAPLOAD=OFF\n"},
    {"HOTSWAP",  "# HOTSWAP: ON = tapping another disk while loaded swaps to it instantly\nHOTSWAP=OFF\n"},
    {"FORCESWAP","# FORCESWAP: ON = swap disk contents without the USB eject/re-attach cycle\nFORCESWAP=OFF\n"},
    {"SAVES",    "\n# SAVES: save-game persistence. OFF = classic (lost on eject),\n# COPY = kept as GameName.sav.adf beside the master, OVERWRITE = patch the master.\nSAVES=COPY\n"},
  };
  const int NK=sizeof(KEYS)/sizeof(KEYS[0]);
  bool present[NK]; for(int i=0;i<NK;i++)present[i]=false;
  File fr=SD_MMC.open("/CONFIG.TXT",FILE_READ); if(!fr)return;
  while(fr.available()){String l=fr.readStringUntil('\n');l.trim();if(l.startsWith("#"))continue;
    int eq=l.indexOf('=');if(eq<0)continue;String k=l.substring(0,eq);k.trim();
    for(int i=0;i<NK;i++)if(k==KEYS[i].key)present[i]=true;}
  fr.close();
  String add=""; int missing=0;
  for(int i=0;i<NK;i++)if(!present[i]){add+=KEYS[i].block;missing++;}
  if(!missing)return;
  File fw=SD_MMC.open("/CONFIG.TXT",FILE_APPEND);
  if(fw){fw.print(add);fw.close();}
}

static void loadConfig(){
  applyTheme(0);
  File f=SD_MMC.open("/CONFIG.TXT",FILE_READ);if(!f)return;
  while(f.available()){String l=f.readStringUntil('\n');l.trim();if(l.startsWith("#"))continue;
    int eq=l.indexOf('=');if(eq<0)continue;String k=l.substring(0,eq),v=l.substring(eq+1);k.trim();v.trim();
    if(k=="THEME")applyTheme(v.toInt());else if(k=="LOOP")g_loop_cracktro=(v=="1");else if(k=="MODE")g_wireless_mode=(v=="WIRELESS");else if(k=="CAROUSEL"){String cv=v;cv.toUpperCase();g_car_bootmode=(cv=="LAST")?2:((cv=="1"||cv=="ON"||cv=="TRUE")?1:0);}
    else if(k=="TAPLOAD")g_tapload=(v=="ON"||v=="1");else if(k=="HOTSWAP")g_hotswap=(v=="ON"||v=="1");else if(k=="FORCESWAP")g_forceswap=(v=="ON"||v=="1");
    else if(k=="FONT"){int f=1;if(v=="SMALL")f=0;else if(v=="LARGE")f=2;applyFont(f);}
    else if(k=="ROTATE"){g_rot=((v.toInt()/90)%4+4)%4;}
    else if(k=="COMPACT"){g_compact=(v=="ON"||v=="1");}
    else if(k=="SCREENSAVER"){g_ss_enabled=(v!="OFF"&&v!="0");}
    else if(k=="SS_IDLE"){uint32_t s=(uint32_t)v.toInt(); if(s>0)g_ss_idle_ms=s*1000UL;}
    else if(k=="SS_LOAD"){uint32_t s=(uint32_t)v.toInt(); if(s>0)g_ss_load_ms=s*1000UL;}
    else if(k=="CAP"){int c=v.toInt(); if(c>=1&&c<=64)g_dongle_cap=c;}
    else if(k=="CRACKTRO"){int c=v.toInt(); if(c>=0&&c<=6)g_cracktro=c;}
    else if(k=="SAVES"){v.toUpperCase(); g_saves_mode=(v=="OVERWRITE")?2:(v=="OFF"||v=="0")?0:1;}
    else if(k=="HIVEMIND"){g_hivemind=(v=="OFF"||v=="0")?0:1;}}
  f.close();
}

// ════════════════════════════════════════════════════════════════════════════
// LAYOUT — 480×320
// ════════════════════════════════════════════════════════════════════════════
#define VW gW
#define VH gH
#define STATUS_H   20
#define MODE_BAR_H 18
#define NOW_PLAY_H 22
#define BOTTOM_H   40
#define AZ_W       30
// Layout is computed by relayout() for the current rotation + compact mode.
static int AZ_X=450, COVER_W=150, COVER_X=0, COVER_Y=20, COVER_H=260;
static int COVER_ART_X=4, COVER_ART_Y=24, COVER_ART_W=142, COVER_ART_H=116;
static int LIST_X=150, LIST_W=300, LIST_TOP=38, LIST_BOTTOM=258, AZ_TOP=38, AZ_H=242;
static int INS_X=4, INS_Y=244, INS_W=142, INS_H=28;
static int STRIP_Y=0, STRIP_H=0, NOW_Y=258;
static bool COVER_ON=true, STRIP_ON=false, NOW_ON=true;
// Font profile: 0=SMALL 1=NORMAL 2=LARGE. Runtime row height / rows-per-screen / name size.
static int g_font=1, g_item_h=55, g_items_vis=4, g_name_sz=2;
#define LIST_ITEM_H g_item_h
#define ITEMS_VIS   g_items_vis
static void applyFont(int f){if(f<0||f>2)f=1;g_font=f;g_name_sz=(f==0?1:f==2?3:2);
  int target=(f==0?34:f==2?70:50),listH=LIST_BOTTOM-LIST_TOP,rows=listH/target;
  if(listH%target>=target/2)rows++; if(rows<1)rows=1;
  g_item_h=listH/rows; g_items_vis=rows;}
static const char* fontName(int f){return f==0?"SMALL":f==2?"LARGE":"NORMAL";}
static void relayout(){
  if(g_portrait){gW=320;gH=480;}else{gW=480;gH=320;}
  // Reset the clip window to the new canvas. The clip statics init to the
  // landscape 320 height; with an EMPTY game list drawFileList() early-returns
  // before its usual set/reset, so in portrait everything below y=320 —
  // including the whole bottom bar — was silently clipped (buttons invisible
  // but still tappable). Rotation must always re-sync the clip.
  g_clip_x0=0;g_clip_x1=gW;g_clip_y0=0;g_clip_y1=gH;
  AZ_X=VW-AZ_W; int mb=STATUS_H+MODE_BAR_H;
  if(!g_compact){
    if(!g_portrait){
      COVER_ON=true;COVER_X=0;COVER_Y=STATUS_H;COVER_W=150;COVER_H=VH-STATUS_H-BOTTOM_H;
      COVER_ART_X=4;COVER_ART_Y=STATUS_H+4;COVER_ART_W=142;COVER_ART_H=116;
      LIST_X=COVER_W;LIST_TOP=mb;LIST_W=AZ_X-COVER_W;
      NOW_ON=true;NOW_Y=VH-BOTTOM_H-NOW_PLAY_H;LIST_BOTTOM=NOW_Y;
      AZ_TOP=LIST_TOP;AZ_H=(VH-BOTTOM_H)-LIST_TOP;
      INS_X=4;INS_W=COVER_W-8;INS_H=28;INS_Y=VH-BOTTOM_H-36;STRIP_ON=false;
    }else{
      COVER_ON=true;COVER_X=0;COVER_Y=mb+2;COVER_W=VW;COVER_H=190;
      COVER_ART_X=8;COVER_ART_Y=COVER_Y+8;COVER_ART_W=108;COVER_ART_H=108;
      LIST_X=0;LIST_TOP=COVER_Y+COVER_H+4;LIST_W=AZ_X;
      NOW_ON=true;NOW_Y=VH-BOTTOM_H-NOW_PLAY_H;LIST_BOTTOM=NOW_Y;
      AZ_TOP=LIST_TOP;AZ_H=(VH-BOTTOM_H)-LIST_TOP;
      INS_X=8;INS_W=VW-16;INS_H=28;INS_Y=COVER_Y+COVER_H-32;STRIP_ON=false;   // full-width INSERT at panel bottom
    }
  }else{
    COVER_ON=false;STRIP_ON=true;STRIP_H=(g_portrait?46:44);STRIP_Y=VH-BOTTOM_H-STRIP_H;
    LIST_X=0;LIST_TOP=mb+(g_portrait?2:0);LIST_W=AZ_X;LIST_BOTTOM=STRIP_Y;
    AZ_TOP=LIST_TOP;AZ_H=STRIP_Y-LIST_TOP;NOW_ON=false;
    INS_W=(g_portrait?66:80);INS_H=26;INS_X=VW-INS_W-6;INS_Y=STRIP_Y+((STRIP_H-INS_H)/2);
  }
  applyFont(g_font);
}

// ── Smooth-scroll / A-Z index helpers ──
static int  maxScrollPx(){int t=(int)g_games.size()*LIST_ITEM_H-(LIST_BOTTOM-LIST_TOP);return t<0?0:t;}
static void setActiveLetter(char l){g_active_letter=l;g_az_page=(l<='M')?0:1;g_marquee_off=0;}   // auto-flip page; restart marquee
static void syncIndexToScroll(){if(g_games.empty())return;int ti=(int)(g_scrollPx/LIST_ITEM_H);if(ti<0)ti=0;if(ti>=(int)g_games.size())ti=(int)g_games.size()-1;setActiveLetter(bucketOf(g_games[ti].name));}

// ════════════════════════════════════════════════════════════════════════════
// CRACKTRO SPLASH
// ════════════════════════════════════════════════════════════════════════════
#define NUM_STARS 60
static int16_t star_x[NUM_STARS],star_y[NUM_STARS],star_speed[NUM_STARS];
static void initStars(){for(int i=0;i<NUM_STARS;i++){star_x[i]=random(0,gW);star_y[i]=random(0,gH);star_speed[i]=random(1,4);}}

// ── Cracktro engine: 6 Amiga-style demo effects, selected by CONFIG.TXT CRACKTRO= ──
#define CRK_RGB(r,g,b) ((uint16_t)((((r)&0xF8)<<8)|(((g)&0xFC)<<3)|((b)>>3)))

// HSL -> RGB565 (s,l are percentages, matching the design tool)
static uint16_t crk_hsl(float h,float s,float l){
  h=fmodf(fmodf(h,360.0f)+360.0f,360.0f); s*=0.01f; l*=0.01f;
  float c=(1.0f-fabsf(2.0f*l-1.0f))*s;
  float x=c*(1.0f-fabsf(fmodf(h/60.0f,2.0f)-1.0f));
  float m=l-c*0.5f, r,g,b;
  int seg=((int)(h/60.0f))%6;
  switch(seg){case 0:r=c;g=x;b=0;break;case 1:r=x;g=c;b=0;break;case 2:r=0;g=c;b=x;break;
    case 3:r=0;g=x;b=c;break;case 4:r=x;g=0;b=c;break;default:r=c;g=0;b=x;break;}
  return CRK_RGB((uint8_t)((r+m)*255.0f),(uint8_t)((g+m)*255.0f),(uint8_t)((b+m)*255.0f));
}
static inline uint16_t crk_hue(float h){return crk_hsl(h,100.0f,55.0f);}
static uint16_t crk_lerp(int r1,int g1,int b1,int r2,int g2,int b2,float k){
  if(k<0)k=0;if(k>1)k=1;
  return CRK_RGB((uint8_t)(r1+(r2-r1)*k),(uint8_t)(g1+(g2-g1)*k),(uint8_t)(b1+(b2-b1)*k));}
static void crk_line(int x0,int y0,int x1,int y1,uint16_t c){
  int dx=abs(x1-x0),dy=-abs(y1-y0),sx=x0<x1?1:-1,sy=y0<y1?1:-1,err=dx+dy;
  for(int guard=0;guard<4000;guard++){gfx_drawPixel(x0,y0,c);if(x0==x1&&y0==y1)break;
    int e2=2*err;if(e2>=dy){err+=dy;x0+=sx;}if(e2<=dx){err+=dx;y0+=sy;}}}

// transparent glyphs (foreground pixels only) using the built-in 6x8 font
static void crk_char(char ch,int x,int y,int sz,uint16_t col){
  if(ch<32||ch>126)return; const uint8_t*d=font6x8[ch-32];
  for(int k=0;k<6;k++){uint8_t bits=pgm_read_byte(&d[k]);
    for(int r=0;r<8;r++)if(bits&(1<<r))for(int dy=0;dy<sz;dy++)for(int dx=0;dx<sz;dx++)
      gfx_drawPixel(x+k*sz+dx,y+r*sz+dy,col);}}
static int  crk_txtW(const char*s,int sz){return (int)strlen(s)*6*sz;}
static void crk_txt(int x,int y,const char*s,int sz,uint16_t col){for(int i=0;s[i];i++)crk_char(s[i],x+i*6*sz,y,sz,col);}
static void crk_txtC(int cx,int y,const char*s,int sz,uint16_t col){crk_txt(cx-crk_txtW(s,sz)/2,y,s,sz,col);}
static void crk_txtShadow(int cx,int y,const char*s,int sz,uint16_t col){
  crk_txt(cx-crk_txtW(s,sz)/2+2,y+2,s,sz,CRK_RGB(5,6,12));crk_txtC(cx,y,s,sz,col);}

static const char* CRK_SCROLL="        OMEGAWARE PRESENTS ... THE GTi ... THE FLOPPY FLINGER THINGER ... CODED BY MEZ & DIMMY ... A LITTLE TRIBUTE TO THE AMIGA CRACKTRO LEGENDS ... GREETINGS TO EVERYONE KEEPING THE SCENE ALIVE ... NOW GO LOAD A GAME ...        ";
static void crk_scroller(float t,uint16_t col,float amp,bool rainbow){
  const int sz=2,cw=12; int slen=strlen(CRK_SCROLL);
  long cs=(long)(t*0.13f); int sc=(int)(cs/cw), px=(int)(cs%cw);
  gfx_fillRect(0,gH-30,gW,30,CRK_RGB(5,7,15));
  for(int c=0;c<gW/cw+3;c++){char ch=CRK_SCROLL[(((sc+c)%slen)+slen)%slen]; int x=-px+c*cw;
    int y=gH-26+(int)(sinf(x*0.035f+t*0.004f)*amp);
    crk_char(ch,x,y,sz, rainbow?crk_hue(x*1.2f+t*0.2f):col);}}
static void crk_stars(){
  for(int i=0;i<NUM_STARS;i++){star_x[i]-=star_speed[i];if(star_x[i]<0){star_x[i]=gW-1;star_y[i]=random(0,gH-30);}
    uint16_t c=star_speed[i]==3?TFT_WHITE:star_speed[i]==2?CRK_RGB(159,180,214):CRK_RGB(66,80,110);
    int s=star_speed[i]>2?2:1; gfx_fillRect(star_x[i],star_y[i],s,s,c);}}
static void crk_copperBar(int cy,int h,float hue){
  for(int i=-h/2;i<h/2;i++){float l=62.0f-fabsf((float)i)/(h/2.0f)*56.0f; gfx_fillRect(0,cy+i,gW,1,crk_hsl(hue,100.0f,l));}}

// 1: COPPER CLASSIC
static void crkCopper(float t){
  gfx_fillScreen(CRK_RGB(4,6,13)); crk_stars();
  for(int b=0;b<3;b++){int cy=150+b*34+(int)(sinf(t*0.0022f+b*1.4f)*26); crk_copperBar(cy,30,t*0.06f+b*70);}
  crk_txtC(gW/2,34,"OMEGAWARE",4,crk_hue(t*0.12f));
  crk_txtC(gW/2,82,"* MEZ & DIMMY *",2,CRK_RGB(174,187,208));
  crk_scroller(t,CRK_RGB(255,224,0),13,false);
}
// 2: STARFIELD
static void crkStarfield(float t){
  gfx_fillScreen(CRK_RGB(2,3,10)); crk_stars(); crk_stars();
  int bx=gW/2+(int)(sinf(t*0.0016f)*150), by=120+(int)(sinf(t*0.0025f)*54);
  crk_txtShadow(bx,by,"OMEGAWARE",3,crk_hsl(t*0.1f,100.0f,60.0f));
  crk_txtC(bx,by+34,"INTO THE VOID",1,CRK_RGB(127,208,255));
  crk_scroller(t,0,10,true);
}
// 3: RAINBOW RASTER
static void crkRaster(float t){
  for(int y=0;y<gH-30;y++) gfx_fillRect(0,y,gW,1,crk_hsl(y*1.4f+t*0.16f,100.0f,50.0f));
  gfx_fillRect(48,104,gW-96,92,CRK_RGB(6,8,18)); gfx_drawRect(48,104,gW-96,92,TFT_WHITE);
  crk_txtC(gW/2,124,"OMEGAWARE",4,TFT_WHITE);
  crk_txtC(gW/2,172,"CRACKED - TRAINED - LOADED",1,CRK_RGB(255,233,168));
  crk_scroller(t,TFT_WHITE,10,false);
}
// 4: PLASMA
static void crkPlasma(float t){
  const int bs=8;
  for(int y=0;y<gH-30;y+=bs)for(int x=0;x<gW;x+=bs){
    float v=sinf(x*0.035f+t*0.003f)+sinf(y*0.05f+t*0.0042f)+sinf((x+y)*0.028f+t*0.002f);
    gfx_fillRect(x,y,bs,bs,crk_hsl(v*60.0f+t*0.12f,90.0f,56.0f));}
  crk_txtShadow(gW/2,54,"OMEGAWARE",4,TFT_WHITE);
  crk_txtC(gW/2,104,"MELT YOUR EYES",2,CRK_RGB(10,10,20));
  crk_scroller(t,TFT_WHITE,12,false);
}
// 5: BOING BALL
static void crkBoing(float t){
  gfx_fillScreen(CRK_RGB(12,12,22));
  uint16_t grd=CRK_RGB(70,36,96);
  for(int x=0;x<=gW;x+=32) gfx_vline(x,60,gH-30-60,grd);
  for(int y=60;y<=gH-30;y+=28) gfx_hline(0,y,gW,grd);
  int bx=gW/2+(int)(sinf(t*0.0016f)*150), gy=gH-70, by=gy-(int)(fabsf(sinf(t*0.004f))*120), r=46;
  for(int yy=-6;yy<=6;yy++){int w=(int)(r*0.9f*sqrtf(1.0f-(yy/6.0f)*(yy/6.0f))); gfx_fillRect(bx-w,gy+6+yy,2*w+1,1,CRK_RGB(8,8,14));}
  float cell=r/3.2f, ph=fmodf(t*0.06f,cell*2);
  for(int yy=-r;yy<=r;yy++){int hw=(int)sqrtf((float)(r*r-yy*yy));
    for(int xx=-hw;xx<=hw;xx++){int cc=(((int)floorf((xx+ph)/cell))+((int)floorf(yy/cell)))&1;
      gfx_drawPixel(bx+xx,by+yy, cc?CRK_RGB(255,38,38):CRK_RGB(242,242,242));}}
  gfx_drawCircle(bx,by,r,CRK_RGB(122,0,0));
  crk_txtC(gW/2,26,"OMEGAWARE",3,CRK_RGB(255,59,59));
  crk_scroller(t,CRK_RGB(255,102,102),9,false);
}
// 6: SYNTHWAVE
static void crkSynth(float t){
  for(int y=0;y<gH;y++){float f=(float)y/gH; uint16_t col;
    if(f<0.52f) col=crk_lerp(24,11,51, 90,26,110, f/0.52f);
    else        col=crk_lerp(11,10,26, 4,4,12, (f-0.53f)/0.47f);
    gfx_fillRect(0,y,gW,1,col);}
  for(int i=0;i<26;i++){int sx=(i*53+7)%gW, sy=(i*29)%150; gfx_drawPixel(sx,sy,TFT_WHITE);}
  int sunx=gW/2,suny=168,sr=58;
  for(int yy=-sr;yy<=0;yy++){int w=(int)sqrtf((float)(sr*sr-yy*yy)); gfx_fillRect(sunx-w,suny+yy,2*w,1,CRK_RGB(255,91,138));}
  for(int i=0;i<6;i++){int yy=118+i*8; gfx_fillRect(sunx-60,yy,120,3+i,CRK_RGB(24,11,51));}
  uint16_t grc=CRK_RGB(0,229,255); int hz=176;
  for(int i=0;i<8;i++){int yy=hz+(int)(i*i*2.4f); if(yy<gH) gfx_hline(0,yy,gW,grc);}
  for(int x=-6;x<=12;x++){int px=gW/2+(x*70); int x0=gW/2+(int)((px-gW/2)*0.18f); crk_line(x0,hz,px,gH,grc);}
  crk_txtShadow(gW/2,40,"OMEGAWARE",3,CRK_RGB(49,232,255));
  crk_txtC(gW/2,74,"RETRO FUTURE",1,CRK_RGB(255,122,176));
  crk_scroller(t,CRK_RGB(255,79,160),8,false);
}

// Boot cracktro runner. style: 1..6 forces a style, 0 = random pick each boot.
static void drawCracktro(int style){
  int s=(style>=1&&style<=6)?(style-1):(int)(esp_random()%6);
  initStars();
  unsigned long startMs=millis();
  gfx_fillScreen(TFT_BLACK);gfx_flush();
  while(true){
    if(Touch_ReadFrame()){unsigned long t0=millis();while(Touch_ReadFrame()&&millis()-t0<500)delay(10);break;}
    if(!g_loop_cracktro&&millis()-startMs>=6000)break;
    float t=(float)(millis()-startMs);
    switch(s){case 0:crkCopper(t);break;case 1:crkStarfield(t);break;case 2:crkRaster(t);break;
      case 3:crkPlasma(t);break;case 4:crkBoing(t);break;default:crkSynth(t);break;}
    if(((int)(t/450.0f))%2) crk_txtC(gW/2,gH-46,"TAP TO CONTINUE",1,CRK_RGB(150,168,200));
    gfx_flush();delay(6);
  }
  gfx_fillScreen(TFT_BLACK);gfx_flush();
}

// ════════════════════════════════════════════════════════════════════════════
// DRAW FUNCTIONS
// ════════════════════════════════════════════════════════════════════════════
// Greedy word-wrap: draws s at the current text size, up to maxLines lines, never below bottomY. Returns the new y.
static int drawWrapped(int x,int y,const String&s,int maxW,int lineH,int maxLines,int bottomY,uint16_t fg,uint16_t bg){
  gfx_setTextColor(fg,bg);String line="",word="";int gh=8*text_size,n=0;
  for(int i=0;i<=(int)s.length();i++){char c=i<(int)s.length()?s[i]:' ';
    if(c==' '||c=='\n'||i==(int)s.length()){
      String cand=line.length()?line+" "+word:word;
      if(gfx_textWidth(cand)>maxW&&line.length()){
        if(n>=maxLines||y+gh>bottomY)return y;
        gfx_setCursor(x,y);gfx_print(line);y+=lineH;n++;line=word;
      } else line=cand;
      word="";
    } else word+=c;
  }
  if(line.length()&&n<maxLines&&y+gh<=bottomY){gfx_setCursor(x,y);gfx_print(line);y+=lineH;}
  return y;
}
static void drawStatusBar(){
  gfx_fillRect(0,0,VW,STATUS_H,COL_BAR);gfx_setTextSize(1);
  gfx_setTextColor(COL_ORANGE,COL_BAR);gfx_setCursor(6,6);gfx_print("OMEGAWARE");
  gfx_setTextColor(COL_MID,COL_BAR);gfx_print("  " FW_VERSION);
  if(g_wireless_mode){gfx_setTextColor(espnowIsPaired()?0x07E0:0xFD20,COL_BAR);gfx_setCursor(VW/2-40,6);gfx_print(espnowIsPaired()?"WIRELESS:PAIRED":"WIRELESS:PAIR");}
  else{gfx_setTextColor(0x07FF,COL_BAR);int tw=gfx_textWidth("STANDALONE");gfx_setCursor((VW-tw)/2,6);gfx_print("STANDALONE");}
  uint16_t ic=g_loaded?0xE8C4:COL_GREEN;gfx_fillCircle(VW-8,STATUS_H/2,3,ic);
  if(g_loaded&&g_loaded_game_idx>=0&&g_loaded_game_idx<(int)g_games.size()&&g_games[g_loaded_game_idx].disk_count>1){   // v4.9.5: which disk of a multi-disk set is mounted
    String dl="D"+String(g_loaded_disk_idx+1);gfx_setTextSize(1);gfx_setTextColor(ic,COL_BAR);
    gfx_setCursor(VW-13-gfx_textWidth(dl),(STATUS_H-8)/2);gfx_print(dl);}
}

// disk grid geometry (landscape cover) — shared by draw + touch (struct declared up top)
static DiskGrid diskGrid(int nd){DiskGrid L;L.pages=(nd+DISKS_PER_PAGE-1)/DISKS_PER_PAGE;if(g_disk_page>=L.pages)g_disk_page=0;
  L.pageStart=g_disk_page*DISKS_PER_PAGE;L.pageEnd=min(L.pageStart+DISKS_PER_PAGE,nd);L.COLS=3;L.dbw=44;L.dbh=20;L.dgap=4;
  L.gridW=L.COLS*L.dbw+(L.COLS-1)*L.dgap;L.gx=max(4,(COVER_W-L.gridW)/2);L.multiPage=(L.pages>1);
  L.pageBtnH=L.multiPage?16:0;L.pageGap=L.multiPage?4:0;L.gridH=2*L.dbh+L.dgap;L.labelY=INS_Y-L.gridH-L.pageBtnH-L.pageGap-12;L.gridY=L.labelY+10;return L;}
static void drawDiskGrid(int nd){DiskGrid L=diskGrid(nd);
  gfx_setTextSize(1);gfx_setTextColor(COL_DIM,COL_PANEL);gfx_setCursor(4,L.labelY);
  gfx_print(L.multiPage?("DISK ("+String(g_disk_page+1)+"/"+String(L.pages)+"):"):"DISK:");
  for(int d=L.pageStart;d<L.pageEnd;d++){int slot=d-L.pageStart,col=slot%L.COLS,row=slot/L.COLS;int bx=L.gx+col*(L.dbw+L.dgap),by=L.gridY+row*(L.dbh+L.dgap);
    bool isSel=d==g_disk_sel,isLd=(g_loaded_game_idx==g_sel&&g_loaded_disk_idx==d);uint16_t bc=isLd?COL_GREEN:(isSel?COL_AMBER:COL_BAR);
    gfx_fillRoundRect(bx,by,L.dbw,L.dbh,4,bc);gfx_drawRoundRect(bx,by,L.dbw,L.dbh,4,isSel?COL_AMBER:COL_DIM);
    gfx_setTextColor(isLd||isSel?TFT_BLACK:COL_LIT,bc);String dl="D"+String(d+1);gfx_setCursor(bx+(L.dbw-gfx_textWidth(dl))/2,by+(L.dbh-8)/2);gfx_print(dl);}
  if(L.multiPage){int pby=L.gridY+L.gridH+L.pageGap;gfx_fillRoundRect(L.gx,pby,L.gridW,L.pageBtnH,4,COL_ACCENT);gfx_setTextColor(TFT_WHITE,COL_ACCENT);
    String pl=(g_disk_page+1<L.pages)?("MORE D"+String(L.pageEnd+1)+"+  >"):("<  BACK TO D1");gfx_setCursor(L.gx+(L.gridW-gfx_textWidth(pl))/2,pby+(L.pageBtnH-8)/2);gfx_print(pl);}}
// disk stepper (portrait cover / compact) — < Dn/total >
static int g_step_x=0,g_step_y=0,g_step_w=0,g_step_h=0;static bool g_step_on=false;
static void drawDiskStepper(int x,int y,int w,int h,int nd){g_step_on=true;g_step_x=x;g_step_y=y;g_step_w=w;g_step_h=h;
  int bw=w/4;                                       // wide < / > buttons (quarter width each) — easy to hit
  gfx_fillRoundRect(x,y,w,h,6,COL_BAR);
  gfx_fillRoundRect(x,y,bw,h,6,COL_ACCENT);gfx_setTextSize(3);gfx_setTextColor(TFT_WHITE,COL_ACCENT);gfx_setCursor(x+(bw-18)/2,y+(h-24)/2);gfx_print("<");
  gfx_fillRoundRect(x+w-bw,y,bw,h,6,COL_ACCENT);gfx_setCursor(x+w-bw+(bw-18)/2,y+(h-24)/2);gfx_print(">");
  gfx_setTextSize(2);gfx_setTextColor(COL_LIT,COL_BAR);String lbl="Disk "+String(g_disk_sel+1)+" of "+String(nd);int tw=gfx_textWidth(lbl);gfx_setCursor(x+(w-tw)/2,y+(h-16)/2);gfx_print(lbl);}

// v4.8.0: tiny 3.5" floppy icon = "this game has a save attached". 14x14 px,
// black 1px backing so it reads on ANY cover art (light or dark), green body,
// black shutter with slot, white label, clipped corner — unmistakably a floppy.
static void drawSaveFloppy(int x,int y){
  gfx_fillRect(x,y,14,14,TFT_BLACK);            // backing/outline — contrast on any art
  gfx_fillRect(x+1,y+1,12,12,COL_GREEN);        // body
  gfx_fillRect(x+11,y+1,2,2,TFT_BLACK);         // clipped corner (the floppy signature)
  gfx_fillRect(x+4,y+2,7,4,TFT_BLACK);          // metal shutter
  gfx_fillRect(x+5,y+3,2,2,COL_GREEN);          //   shutter slot
  gfx_fillRect(x+3,y+8,8,4,TFT_WHITE);          // label
}

// v4.9: HD (1.76MB) disk indicators. Amber "HD" chip + a "no A500" roundel — an
// A500 has no HD drive, so it can't read these. isHDImage flags anything over
// ~1.2MB; extended-DD disks (~900-960KB) stay DD and get no badge (A500 reads them).
static bool isHDImage(const String&path){String vfs="/sdcard"+path;struct stat st;if(stat(vfs.c_str(),&st)!=0)return false;return (uint32_t)st.st_size>HD_FLAG_BYTES;}
static void drawHDChip(int x,int y){gfx_fillRoundRect(x,y,20,12,3,COL_AMBER);gfx_setTextSize(1);gfx_setTextColor(TFT_BLACK,COL_AMBER);gfx_setCursor(x+4,y+2);gfx_print("HD");}
static void drawBookIcon(int cx,int cy,uint16_t col){   // open-book glyph (6x8 font can't draw it); enlarged v4.9.3
  gfx_drawRect(cx-8,cy-6,16,13,col);     // outer covers
  gfx_vline(cx,cy-6,13,col);             // spine down the middle
  gfx_hline(cx-6,cy-2,4,col);gfx_hline(cx+3,cy-2,4,col);   // page-text lines, both leaves
  gfx_hline(cx-6,cy+1,4,col);gfx_hline(cx+3,cy+1,4,col);
  gfx_hline(cx-6,cy+4,4,col);gfx_hline(cx+3,cy+4,4,col);
}
static void drawNoA500(int cx,int cy,int r,uint16_t ring){
  gfx_fillCircle(cx,cy,r-1,COL_BG);                         // dark backing so it reads on any cover art
  gfx_drawCircle(cx,cy,r,ring);gfx_drawCircle(cx,cy,r-1,ring);
  int d=(r*7)/10;for(int t=-d;t<=d;t++)gfx_fillRect(cx+t-1,cy+t-1,2,2,ring);   // TL->BR slash (no line primitive -> stepped)
  if(r>=20){gfx_setTextSize(1);gfx_setTextColor(COL_LIT,COL_BG);int tw=gfx_textWidth("A500");gfx_setCursor(cx-tw/2,cy-3);gfx_print("A500");}
}

// v4.8.2: type-to-search. A little magnifier sits atop the A-Z rail (above # / A);
// tapping it opens a keyboard that live-filters the library by substring and jumps
// the list to whatever result you tap. The rail letters shrink slightly to make room.
#define AZ_SRCH_H 18
static void drawMagnifier(int cx,int cy,uint16_t col){
  gfx_drawCircle(cx-1,cy-1,4,col);
  gfx_drawCircle(cx-1,cy-1,5,col);        // 2px lens ring
  gfx_fillRect(cx+2,cy+2,2,2,col);        // diagonal handle stub
  gfx_fillRect(cx+4,cy+4,2,2,col);
}

static void drawCoverPanel(){
  g_manual_bw=0;   // v4.9.2: cleared each draw; set below only if this game has a .rtfm
  if(!COVER_ON)return;
  gfx_fillRect(COVER_X,COVER_Y,COVER_W,COVER_H,COL_PANEL);if(g_games.empty())return;
  auto&game=g_games[g_sel];
  if(!game.jpg_path.length()){String jpg;if(findJPGFor(g_files[game.first_file_idx],jpg))game.jpg_path=jpg;else game.jpg_path="?";}
  static int lastNfoSel=-1;static String cachedNfoBlurb="";static bool cachedHasSav=false;static bool cachedHD=false;static String cachedManual="";
  if(lastNfoSel!=g_sel){lastNfoSel=g_sel;cachedNfoBlurb="";String nfoP,nT,nB;
    if(findNFOFor(g_files[game.first_file_idx],nfoP)){File nf=SD_MMC.open(nfoP,FILE_READ);if(nf){String txt;while(nf.available()&&txt.length()<512)txt+=(char)nf.read();nf.close();parseNFO(txt,nT,nB);
      if(nT.length()&&game.name==basenameNoExt(filenameOnly(g_files[game.first_file_idx])))game.name=nT;cachedNfoBlurb=nB;}}
    cachedHasSav=(g_saves_mode==1)&&savExistsFor(g_files[game.first_file_idx]);cachedHD=isHDImage(g_files[game.first_file_idx]);cachedManual="";{String mp;if(manualFor(g_files[game.first_file_idx],mp))cachedManual=mp;}}   // v4.8.0 badge + v4.9 HD flag + v4.9.2 .rtfm (checked once per selection)
  // Cover art
  gfx_fillRoundRect(COVER_ART_X,COVER_ART_Y,COVER_ART_W,COVER_ART_H,5,COL_BAR);
  gfx_drawRoundRect(COVER_ART_X-1,COVER_ART_Y-1,COVER_ART_W+2,COVER_ART_H+2,6,COL_ACCENT);
  if(game.jpg_path.length()>0&&game.jpg_path!="?")gfx_drawJpgFile(game.jpg_path,COVER_ART_X+2,COVER_ART_Y+2,COVER_ART_W-4,COVER_ART_H-4);
  else{char ib[2]={(char)toupper(game.name.charAt(0)),0};gfx_setTextSize(2);gfx_setTextColor(COL_LIT,COL_BAR);gfx_setCursor(COVER_ART_X+COVER_ART_W/2-6,COVER_ART_Y+COVER_ART_H/2-8);gfx_print(ib);}
  // v4.8.0: floppy icon — this game has a save-copy (INSERT will boot the save)
  if(cachedHasSav)drawSaveFloppy(COVER_ART_X+3,COVER_ART_Y+3);
  if(cachedHD){drawHDChip(COVER_ART_X+COVER_ART_W-23,COVER_ART_Y+3);drawNoA500(COVER_ART_X+15,COVER_ART_Y+COVER_ART_H-15,13,TFT_RED);}   // v4.9 HD markers
  if(cachedManual.length()){   // v4.9.2: book button, bottom-right of the cover art — only when a .rtfm exists
    g_manual_bw=26;g_manual_bh=22;g_manual_bx=COVER_ART_X+3;g_manual_by=COVER_ART_Y+(COVER_ART_H-g_manual_bh)/2;g_manual_path=cachedManual;   // v4.9.3: bigger + left edge, clear of the fav/HD corners
    gfx_fillRoundRect(g_manual_bx,g_manual_by,g_manual_bw,g_manual_bh,3,COL_ACCENT);gfx_drawRoundRect(g_manual_bx,g_manual_by,g_manual_bw,g_manual_bh,3,COL_AMBER);
    drawBookIcon(g_manual_bx+g_manual_bw/2,g_manual_by+g_manual_bh/2,COL_LIT);
  }
  bool isL=g_loaded&&g_loaded_game_idx==g_sel;
  if(!g_portrait){
    int cb;
    if(game.disk_count>1){DiskGrid L=diskGrid(game.disk_count);cb=L.labelY-2;}
    else cb=INS_Y-2;
    int ty=COVER_ART_Y+COVER_ART_H+4;gfx_setTextSize(1);
    ty=drawWrapped(4,ty,game.name,COVER_W-8,10,2,cb,COL_LIT,COL_PANEL);
    if(cachedNfoBlurb.length()>0)drawWrapped(4,ty,cachedNfoBlurb,COVER_W-8,9,12,cb,COL_DIM,COL_PANEL);
    if(game.disk_count>1)drawDiskGrid(game.disk_count);
  }else{
    int rx=COVER_ART_X+COVER_ART_W+8,rw=VW-rx-6;int ty=COVER_ART_Y;gfx_setTextSize(1);
    ty=drawWrapped(rx,ty,game.name,rw,10,3,COVER_ART_Y+COVER_ART_H,COL_LIT,COL_PANEL);
    if(cachedNfoBlurb.length()>0)drawWrapped(rx,ty+3,cachedNfoBlurb,rw,9,6,COVER_ART_Y+COVER_ART_H+2,COL_DIM,COL_PANEL);
    if(game.disk_count>1)drawDiskStepper(8,COVER_Y+COVER_H-70,VW-16,26,game.disk_count);   // full-width disk row above INSERT
    else{gfx_setTextSize(1);gfx_setTextColor(cachedHD?COL_ORANGE:COL_DIM,COL_PANEL);gfx_setCursor(12,COVER_Y+COVER_H-58);gfx_print(cachedHD?"HD 1.76MB - needs A3000/A4000":"Single disk  -  ADF 880KB");}
  }
  // INSERT/EJECT
  gfx_fillRoundRect(INS_X,INS_Y,INS_W,INS_H,8,isL?(uint16_t)0x4000:(uint16_t)0x0340);
  gfx_drawRoundRect(INS_X,INS_Y,INS_W,INS_H,8,isL?(uint16_t)0xE8C4:COL_GREEN);
  gfx_setTextSize(2);gfx_setTextColor(TFT_WHITE,isL?(uint16_t)0x4000:(uint16_t)0x0340);
  const char*lbl=isL?"EJECT":"INSERT";int tw=gfx_textWidth(lbl);gfx_setCursor(INS_X+(INS_W-tw)/2,INS_Y+(INS_H-16)/2);gfx_print(lbl);
}

// Compact action strip (both orientations): thumbnail + selected name + INSERT
static void drawActionStrip(){
  if(!STRIP_ON||g_games.empty())return;auto&game=g_games[g_sel];bool isL=g_loaded&&g_loaded_game_idx==g_sel;
  gfx_fillRect(0,STRIP_Y,VW,STRIP_H,COL_PANEL);gfx_hline(0,STRIP_Y,VW,COL_SEP);
  int th=STRIP_H-12;gfx_fillRoundRect(6,STRIP_Y+6,th,th,4,COL_BG);gfx_drawRoundRect(6,STRIP_Y+6,th,th,4,COL_ACCENT);
  char ib[2]={(char)toupper(game.name.charAt(0)),0};gfx_setTextSize(3);gfx_setTextColor(COL_ACCENT,COL_BG);gfx_setCursor(6+(th-18)/2,STRIP_Y+6+(th-24)/2);gfx_print(ib);
  gfx_setTextSize(2);gfx_setTextColor(inkFor(COL_PANEL),COL_PANEL);String nm=game.name;int maxw=INS_X-(th+16)-6;while(gfx_textWidth(nm)>maxw&&nm.length()>3)nm=nm.substring(0,nm.length()-1);gfx_setCursor(th+16,STRIP_Y+8);gfx_print(nm);
  gfx_setTextSize(1);gfx_setTextColor(COL_DIM,COL_PANEL);gfx_setCursor(th+16,STRIP_Y+26);gfx_print(game.disk_count>1?("disk "+String(g_disk_sel+1)+"/"+String(game.disk_count)+"  < tap >"):"1 disk  ADF 880KB");
  gfx_fillRoundRect(INS_X,INS_Y,INS_W,INS_H,6,isL?(uint16_t)0x4000:COL_GREEN);gfx_drawRoundRect(INS_X,INS_Y,INS_W,INS_H,6,isL?(uint16_t)0xE8C4:COL_GREEN);
  gfx_setTextSize(2);gfx_setTextColor(isL?TFT_WHITE:TFT_BLACK,isL?(uint16_t)0x4000:COL_GREEN);const char*lbl=isL?"EJECT":"INSERT";int tw=gfx_textWidth(lbl);gfx_setCursor(INS_X+(INS_W-tw)/2,INS_Y+(INS_H-16)/2);gfx_print(lbl);
}

// INFO / SETTINGS panel — left column (landscape) or full width (portrait). Stores button Ys for touch.
static void drawInfoPanel(){
  int ix,iy,iw,ih;
  // Larger text in portrait (wide panel); size 1 in the narrow 150px landscape column.
  int isz=g_portrait?2:1, bh=g_portrait?28:22, lh=g_portrait?17:9, btn=bh+3, ty=(bh-8*isz)/2;
  // v4.8.1: HIVEMIND toggle row exists only when wireless AND a MuCa multicast
  // group is configured — the hidden feature reveals itself only to its owners.
  int mcInfoN=0;
  if(g_wireless_mode){uint8_t mm[64][6];mcInfoN=enumMuCaDongles(mm,g_dongle_cap);}
  int contentH=4+11+btn+4+btn*3+4+lh*2+(g_wireless_mode?(lh+btn):0)+((mcInfoN>0)?btn:0)+btn+bh+6;
  if(!g_portrait){ix=0;iy=STATUS_H;iw=150;ih=VH-STATUS_H-BOTTOM_H;}
  else{ix=0;iy=STATUS_H+MODE_BAR_H;iw=VW;ih=min(contentH,(VH-BOTTOM_H)-iy);}
  g_info_x=ix;g_info_w=iw;g_info_bottom=iy+ih;g_info_bh=bh;
  gfx_fillRect(ix,iy,iw,ih,COL_PANEL);gfx_drawRect(ix,iy,iw,ih,COL_SEP);
  int y=iy+4,pw=iw-8,x=ix+4;gfx_setTextSize(1);gfx_setTextColor(COL_DIM,COL_PANEL);gfx_setCursor(x+2,y);gfx_print("SETTINGS");y+=11;
  {uint16_t c=g_wireless_mode?COL_BLUE:COL_GREEN;gfx_fillRoundRect(x,y,pw,bh,6,c);gfx_setTextSize(isz);gfx_setTextColor(TFT_BLACK,c);String s=String("MODE: ")+(g_wireless_mode?"WIRELESS":"STANDALONE");int tw=gfx_textWidth(s);gfx_setCursor(x+(pw-tw)/2,y+ty);gfx_print(s);}g_info_mode_btn_y=y;y+=btn;
  gfx_hline(x+2,y,pw-4,COL_SEP);y+=4;
  {gfx_fillRoundRect(x,y,pw,bh,6,COL_AMBER);gfx_setTextSize(isz);gfx_setTextColor(TFT_BLACK,COL_AMBER);String s=String("FONT: ")+fontName(g_font);int tw=gfx_textWidth(s);gfx_setCursor(x+(pw-tw)/2,y+ty);gfx_print(s);}g_info_font_btn_y=y;y+=btn;
  {const char*rn=g_portrait?"PORTRAIT":"LANDSCAPE";gfx_fillRoundRect(x,y,pw,bh,6,COL_BLUE);gfx_setTextSize(isz);gfx_setTextColor(TFT_WHITE,COL_BLUE);String s=String("ROTATE: ")+rn;int tw=gfx_textWidth(s);gfx_setCursor(x+(pw-tw)/2,y+ty);gfx_print(s);}g_info_rot_btn_y=y;y+=btn;
  {uint16_t cc=g_compact?COL_GREEN:COL_BAR;gfx_fillRoundRect(x,y,pw,bh,6,cc);gfx_setTextSize(isz);gfx_setTextColor(g_compact?TFT_BLACK:COL_LIT,cc);String s=String("COMPACT: ")+(g_compact?"ON":"OFF");int tw=gfx_textWidth(s);gfx_setCursor(x+(pw-tw)/2,y+ty);gfx_print(s);}g_info_comp_btn_y=y;y+=btn;
  gfx_hline(x+2,y,pw-4,COL_SEP);y+=4;
  gfx_setTextSize(isz);gfx_setTextColor(COL_LIT,COL_PANEL);gfx_setCursor(x+2,y);gfx_print("Heap:"+String(ESP.getFreeHeap()/1024)+"K PSRAM:"+String(ESP.getFreePsram()/1024)+"K");y+=lh;
  gfx_setCursor(x+2,y);gfx_print("Games: "+String(g_games.size())+"  Wr:"+String(g_sv_total_writes));y+=lh;
  if(g_wireless_mode){
    if(espnowIsPaired()){bool on=espnowXiaoOnline();gfx_setTextColor(on?COL_GREEN:COL_ORANGE,COL_PANEL);gfx_setCursor(x+2,y);gfx_print(on?"DONGLE: ONLINE":"DONGLE: OFFLINE");y+=lh;}
    else{gfx_setTextColor(COL_ORANGE,COL_PANEL);gfx_setCursor(x+2,y);gfx_print("Not paired");y+=lh;}
    {gfx_fillRoundRect(x,y,pw,bh,6,espnowIsPaired()?COL_GREEN:COL_AMBER);gfx_setTextSize(isz);gfx_setTextColor(TFT_BLACK,espnowIsPaired()?COL_GREEN:COL_AMBER);const char*pl=espnowIsPaired()?"SWITCH DONGLE":"SCAN DONGLES";int tw=gfx_textWidth(pl);gfx_setCursor(x+(pw-tw)/2,y+ty);gfx_print(pl);}g_info_pair_now_btn_y=y;y+=btn;
    // v4.8.1: HIVEMIND cast toggle — ON = FLING fans out to every MuCa dongle,
    // OFF = paired dongle only (group stays intact, no renames). MuCa owners only.
    if(mcInfoN>0){
      uint16_t hc=g_hivemind?COL_ACCENT:COL_BAR;
      gfx_fillRoundRect(x,y,pw,bh,6,hc);gfx_setTextSize(isz);gfx_setTextColor(g_hivemind?TFT_WHITE:COL_LIT,hc);
      String hs=String("HIVEMIND: ")+(g_hivemind?"ON":"OFF");int tw=gfx_textWidth(hs);gfx_setCursor(x+(pw-tw)/2,y+ty);gfx_print(hs);
      g_info_hive_btn_y=y;y+=btn;
    } else g_info_hive_btn_y=0;
  } else {g_info_pair_now_btn_y=0;g_info_hive_btn_y=0;}
  {gfx_fillRoundRect(x,y,pw,bh,6,COL_BLUE);gfx_setTextSize(isz);gfx_setTextColor(TFT_WHITE,COL_BLUE);int tw=gfx_textWidth("RESCAN SD");gfx_setCursor(x+(pw-tw)/2,y+ty);gfx_print("RESCAN SD");}g_info_rescan_btn_y=y;y+=btn;
  // Last row split in two: SOFT RESET (rarely used) shares the row with LOAD DIAG.
  {int gap=4,hw=(pw-gap)/2,dx=x+hw+gap;gfx_setTextSize(isz);
    gfx_fillRoundRect(x,y,hw,bh,6,0x8000);gfx_setTextColor(TFT_WHITE,0x8000);
    {int tw=gfx_textWidth("SOFT RESET");gfx_setCursor(x+(hw-tw)/2,y+ty);gfx_print("SOFT RESET");}
    {bool diagOn=(g_loaded&&g_loaded_name=="AMIGA TEST KIT");uint16_t dc=diagOn?(uint16_t)0xE8C4:COL_ACCENT;const char*dl=diagOn?"EJECT DIAG":"LOAD DIAG";   // v4.9.8: eject the mounted test kit
     gfx_fillRoundRect(dx,y,hw,bh,6,dc);gfx_setTextColor(diagOn?TFT_BLACK:TFT_WHITE,dc);
     int tw=gfx_textWidth(dl);gfx_setCursor(dx+(hw-tw)/2,y+ty);gfx_print(dl);}
  }g_info_reset_btn_y=y;
}

static void drawModeBar(){
  int mbR=LIST_X+LIST_W+AZ_W;
  gfx_fillRect(LIST_X,STATUS_H,LIST_W+AZ_W,MODE_BAR_H,COL_BAR);gfx_setTextSize(1);
  bool isA=g_mode==MODE_ADF;
  gfx_fillRoundRect(LIST_X+4,STATUS_H+2,36,14,7,isA?COL_ACCENT:COL_BG);gfx_setTextColor(isA?COL_AMBER:COL_DIM,isA?COL_ACCENT:COL_BG);gfx_setCursor(LIST_X+10,STATUS_H+6);gfx_print("ADF");
  gfx_fillRoundRect(LIST_X+44,STATUS_H+2,36,14,7,!isA?COL_ACCENT:COL_BG);gfx_setTextColor(!isA?COL_AMBER:COL_DIM,!isA?COL_ACCENT:COL_BG);gfx_setCursor(LIST_X+50,STATUS_H+6);gfx_print("DSK");
  gfx_fillRoundRect(LIST_X+84,STATUS_H+2,66,14,7,COL_BLUE);gfx_setTextColor(TFT_WHITE,COL_BLUE);gfx_setCursor(LIST_X+92,STATUS_H+6);gfx_print("USR-DSK");   // v4.9.7 user-disk manager
  gfx_setTextColor(COL_MID,COL_BAR);String gt=String(g_games.size())+" games";gfx_setCursor(mbR-gfx_textWidth(gt)-6,STATUS_H+6);gfx_print(gt);
}

static void drawFileList(){
  gfx_fillRect(LIST_X,LIST_TOP,LIST_W,LIST_BOTTOM-LIST_TOP,COL_BG);
  if(g_games.empty()){gfx_setTextSize(1);gfx_setTextColor(0xE8C4,COL_BG);gfx_setCursor(LIST_X+8,LIST_TOP+16);gfx_print(g_mode==MODE_ADF?"No .ADF files":"No .DSK files");return;}
  if(g_scrollPx<0)g_scrollPx=0;int mp=maxScrollPx();if(g_scrollPx>mp)g_scrollPx=mp;
  int first=(int)(g_scrollPx/LIST_ITEM_H),off=(int)(g_scrollPx-(float)first*LIST_ITEM_H);
  g_scroll=first;                                  // keep integer scroll in sync (thumb, etc.)
  g_clip_y0=LIST_TOP;g_clip_y1=LIST_BOTTOM;         // clip partial rows to the list window
  for(int vi=0;vi<=ITEMS_VIS+1;vi++){int gi=first+vi;if(gi>=(int)g_games.size())break;
    auto&game=g_games[gi];bool sel=gi==g_sel,ld=g_loaded&&g_loaded_game_idx==gi;
    int y=LIST_TOP-off+vi*LIST_ITEM_H;if(y>=LIST_BOTTOM)break;
    if(sel){gfx_fillRoundRect(LIST_X+2,y+1,LIST_W-4,LIST_ITEM_H-2,4,COL_SEL);gfx_drawRoundRect(LIST_X+2,y+1,LIST_W-4,LIST_ITEM_H-2,4,COL_AMBER);}
    else gfx_fillRoundRect(LIST_X+2,y+1,LIST_W-4,LIST_ITEM_H-2,3,COL_PANEL);
    uint16_t acCol=ld?COL_GREEN:(sel?COL_AMBER:COL_ACCENT);gfx_fillRect(LIST_X+3,y+3,3,LIST_ITEM_H-4,acCol);
    int r=8+g_name_sz*3,cx=LIST_X+6+r,cy=y+LIST_ITEM_H/2;
    if(game.fav){gfx_fillStar(cx,cy,(float)r,COL_STAR);}
    else{
    gfx_fillCircle(cx,cy,r,sel?COL_AMBER:(ld?COL_GREEN:COL_CIRC));
    gfx_setTextSize(g_name_sz);gfx_setTextColor(sel||ld?TFT_BLACK:COL_CIRC_TEXT,sel?COL_AMBER:COL_CIRC);
    char ib[2]={(char)toupper(game.name.charAt(0)),0};gfx_setCursor(cx-gfx_textWidth(ib)/2,cy-4*g_name_sz);gfx_print(ib);
    }
    int nx=cx+r+6;gfx_setTextSize(g_name_sz);gfx_setTextColor(sel?TFT_WHITE:COL_LIT,sel?COL_SEL:COL_PANEL);
    int maxNW=LIST_W-(nx-LIST_X)-8-(game.disk_count>1?36:0);
    if(sel&&gfx_textWidth(game.name)>maxNW){
      // marquee: bounce the full selected name within its lane (offset 0..max), clipped horizontally
      g_clip_x0=nx;g_clip_x1=nx+maxNW;
      gfx_setCursor(nx-g_marquee_off,cy-4*g_name_sz);gfx_print(game.name);
      g_clip_x0=0;g_clip_x1=gW;
    } else {
      String nm=game.name;while(gfx_textWidth(nm)>maxNW&&nm.length()>3)nm=nm.substring(0,nm.length()-1);
      gfx_setCursor(nx,cy-4*g_name_sz);gfx_print(nm);
    }
    if(game.disk_count>1){gfx_setTextSize(1);gfx_fillRoundRect(LIST_X+LIST_W-38,cy-6,34,12,4,COL_ACCENT);gfx_setTextColor(TFT_WHITE,COL_ACCENT);gfx_setCursor(LIST_X+LIST_W-34,cy-4);gfx_print(String(game.disk_count)+"DSK");}
  }
  g_clip_y0=0;g_clip_y1=gH;
}

static void drawNowPlayingBar(){
  if(!NOW_ON)return;
  int y=NOW_Y;
  if(g_loaded&&g_loaded_name.length()){gfx_fillRect(LIST_X,y,LIST_W,NOW_PLAY_H,COL_NOW);gfx_drawRect(LIST_X,y,LIST_W,NOW_PLAY_H,COL_GREEN);
    gfx_fillCircle(LIST_X+8,y+NOW_PLAY_H/2,3,COL_GREEN);gfx_setTextSize(1);gfx_setTextColor(COL_GREEN,COL_NOW);gfx_setCursor(LIST_X+16,y+3);gfx_print("NOW PLAYING");
    gfx_setTextColor(TFT_WHITE,COL_NOW);gfx_setCursor(LIST_X+16,y+12);String n=g_loaded_name;while(gfx_textWidth(n)>LIST_W-24&&n.length()>3)n=n.substring(0,n.length()-1);gfx_print(n);}
  else{gfx_fillRect(LIST_X,y,LIST_W,NOW_PLAY_H,COL_BG);gfx_setTextSize(1);gfx_setTextColor(COL_MID,COL_BG);gfx_setCursor(LIST_X+8,y+NOW_PLAY_H/2-4);gfx_print(String(g_games.size())+" games - tap INSERT");}
}

// Split active letters into the two halves: page 0 = #/A-M, page 1 = N-Z
static int azHalf(int page,char*out){int n=0;for(int i=0;i<active_letter_count;i++){bool lo=active_letters[i]<='M';if((page==0&&lo)||(page==1&&!lo))out[n++]=active_letters[i];}return n;}
#define AZ_TOG_H 32   // A-M/N-Z toggle button height (taller = easier to hit, away from the INFO button)
static void drawAZBar(){
  if(!active_letter_count)return;
  int azBottom=AZ_TOP+AZ_H;
  int togY=azBottom-AZ_TOG_H;                       // toggle top; letters occupy the strip above it
  int letTop=AZ_TOP+AZ_SRCH_H;                      // v4.8.2: reserve a cell for the search magnifier
  int barH=togY-letTop;
  gfx_fillRect(AZ_X,AZ_TOP,AZ_W,AZ_H,COL_PANEL);
  drawMagnifier(AZ_X+AZ_W/2,AZ_TOP+AZ_SRCH_H/2,COL_AMBER);
  gfx_hline(AZ_X,AZ_TOP+AZ_SRCH_H-1,AZ_W,COL_SEP);
  char p0[27],p1[27];int n0=azHalf(0,p0),n1=azHalf(1,p1);
  char*half=g_az_page==0?p0:p1;int hn=g_az_page==0?n0:n1;
  int slots=max(13,max(n0,n1));                    // enough rows for the bigger half ('#' can push it to 14)
  int letterH=barH/slots;if(letterH<7)letterH=7;
  int lsz=(letterH>=15)?2:1;
  gfx_setTextSize(lsz);
  for(int i=0;i<hn;i++){char letter=half[i];int ly=letTop+i*letterH;if(ly+letterH>togY)break;
    if(letter==g_active_letter){gfx_fillRect(AZ_X,ly,AZ_W,letterH,COL_AMBER);gfx_setTextColor(TFT_BLACK,COL_AMBER);}
    else gfx_setTextColor(COL_DIM,COL_PANEL);
    gfx_setCursor(AZ_X+(AZ_W-6*lsz)/2,ly+(letterH-8*lsz)/2);char lb[2]={letter,0};gfx_print(lb);}
  // Toggle button — taller, fills the strip bottom
  gfx_fillRoundRect(AZ_X+1,togY+1,AZ_W-2,azBottom-togY-2,5,COL_ACCENT);
  gfx_setTextSize(1);gfx_setTextColor(TFT_WHITE,COL_ACCENT);
  const char*blbl=g_az_page==0?"N-Z":"A-M";
  gfx_setCursor(AZ_X+(AZ_W-gfx_textWidth(blbl))/2,togY+(AZ_TOG_H-8)/2);gfx_print(blbl);
  int maxOff=(int)g_games.size()-ITEMS_VIS;if(maxOff>0){int thumbH=max(4,barH*ITEMS_VIS/(int)g_games.size());int thumbY=AZ_TOP+(barH-thumbH)*g_scroll/maxOff;gfx_fillRect(AZ_X-2,thumbY,2,thumbH,COL_BLUE);}
}

static bool handleAlphabetTouch(uint16_t px,uint16_t py){
  if(px<AZ_X||py<AZ_TOP||py>=(uint16_t)(AZ_TOP+AZ_H)||!active_letter_count)return false;
  int togY=AZ_TOP+AZ_H-AZ_TOG_H;
  // Toggle button (taller hit region at the strip bottom) — manual page peek
  if(py>=(uint16_t)togY){g_az_page=g_az_page?0:1;return true;}
  int letTop=AZ_TOP+AZ_SRCH_H;int barH=togY-letTop;    // letters live below the magnifier cell
  char p0[27],p1[27];int n0=azHalf(0,p0),n1=azHalf(1,p1);
  char*half=g_az_page==0?p0:p1;int hn=g_az_page==0?n0:n1;
  if(hn==0){g_az_page=g_az_page?0:1;return true;}
  int slots=max(13,max(n0,n1));int letterH=barH/slots;if(letterH<7)letterH=7;
  int r=constrain((int)(py-letTop)/letterH,0,hn-1);
  char letter=half[r];
  int target=0;for(int i=0;i<(int)g_games.size();i++){if(bucketOf(g_games[i].name)>=letter){target=i;break;}}
  setActiveLetter(letter);
  g_sel=target;g_scrollPx=min((float)(target*LIST_ITEM_H),(float)maxScrollPx());g_inertia_on=false;
  return true;
}

// v4.8.5: little pictograph glyphs (the 6x8 font can't draw these) for the
// list<->carousel flip. Drawn inside the ~8px button circle at (cx,cy).
static void drawListIcon(int cx,int cy,uint16_t col){
  for(int r=-1;r<=1;r++) gfx_fillRect(cx-5,cy+r*4-1,10,2,col);   // three stacked rows
}
static void drawCarouselIcon(int cx,int cy,uint16_t col){
  gfx_drawRect(cx-7,cy-4,4,9,col);      // left cover, peeking
  gfx_drawRect(cx+3,cy-4,4,9,col);      // right cover, peeking
  gfx_fillRect(cx-2,cy-6,5,13,col);     // center cover, front & tall
}
static void drawBottomBar(){
  int y=VH-BOTTOM_H;gfx_fillRect(0,y,VW,BOTTOM_H,COL_BAR);gfx_hline(0,y,VW,COL_SEP);
  // v4.8.5: the carousel is a first-class mode — REEL is ALWAYS present; its glyph is
  // the coverflow pictograph so it reads as "flip to the reel".
  const int nb=5;int bw=VW/nb;
  struct{const char*icon;const char*label;uint16_t col;}btns[5]={{"<","PREV",COL_ORANGE},{">","NEXT",COL_BLUE},{"#","THEME",COL_AMBER},{"","REEL",COL_GREEN},{"i","INFO",COL_MID}};
  for(int i=0;i<nb;i++){
    int bx=i*bw;if(i>0)gfx_vline(bx,y+3,BOTTOM_H-6,COL_SEP);
    int cx=bx+bw/2,cy2=y+10;gfx_fillCircle(cx,cy2,8,(uint16_t)(btns[i].col>>2));gfx_drawCircle(cx,cy2,8,btns[i].col);
    if(i==3) drawCarouselIcon(cx,cy2,btns[i].col);           // REEL -> coverflow glyph
    else{gfx_setTextSize(1);gfx_setTextColor(btns[i].col,(uint16_t)(btns[i].col>>2));int tw=gfx_textWidth(btns[i].icon);gfx_setCursor(cx-tw/2,cy2-4);gfx_print(btns[i].icon);}
    gfx_setTextColor(COL_DIM,COL_BAR);int tw=gfx_textWidth(btns[i].label);gfx_setCursor(bx+(bw-tw)/2,y+22);gfx_print(btns[i].label);}
  gfx_setTextColor((uint16_t)(COL_AMBER>>1),COL_BAR);String tn=THEMES[g_theme_idx].name;int tw=gfx_textWidth(tn);gfx_setCursor(2*bw+(bw-tw)/2,y+33);gfx_print(tn);
}

// ════════════════════════════════════════════════════════════════════════════
// CAROUSEL — "fake coverflow" reel (v4.8.5: first-class mode; CAROUSEL= sets default boot view)
// Center cover full-size, neighbours scaled+squashed+dimmed, looping reel.
// Sources cycle ALL -> FAV -> MOST -> RND (RND = shuffle-jump to one random
// cover). Tap center = INSERT/EJECT (deliberate; no automount). [LIST] exits.
// ════════════════════════════════════════════════════════════════════════════
static bool g_car_active=false;
static int  g_car_src=0;                        // 0=ALL 1=FAV 2=MOST 3=RND
static std::vector<int> g_car_list;             // reel order -> g_games indices
static float g_car_pos=0;                       // fractional reel position
// touch/inertia — same feel constants as the list scroll
static bool g_car_touch=false,g_car_moved=false,g_car_coast=false;
static int  g_car_x0=0,g_car_y0=0,g_car_lastX=0,g_car_rel=0;
static float g_car_pos0=0,g_car_vel=0,g_car_ivel=0;
static uint32_t g_car_lastMs=0;
#define CAR_PX_PER_STEP 120.0f
// RND dice-roll spin: slot-machine ease-out to the chosen cover + a d6 overlay.
// (Born of Copilot's "you rolled a d6 and it came out 23" — hence the 1-in-23
//  chance the die lands showing 23. The impossible roll, canonized.)
static bool  g_car_spin=false, g_car_dieShow=false, g_car_die23=false;
static float g_car_spinTarget=0;
static uint8_t g_car_die=1, g_car_dieTick=0;
static int g_car_ins_x=0,g_car_ins_y=0,g_car_ins_w=0,g_car_ins_h=0;   // INSERT button rect (set by drawCarousel)
static void runScreensaver();   // defined below; the reel's idle tick can summon it
#define CAR_TILE  150                            // decoded cover tile size (px)
#define CAR_SLOTS 16                             // LRU tile cache entries (PSRAM, ~720 KB)
static uint16_t* car_buf[CAR_SLOTS]={0};
static int      car_game[CAR_SLOTS]={-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
static uint32_t car_stamp[CAR_SLOTS]={0};
static uint32_t car_tick_ctr=0;

static int carN(){return (int)g_car_list.size();}
static int carWrap(int i){int n=carN();if(n<=0)return 0;i%=n;if(i<0)i+=n;return i;}
static const char* carSrcName(){return g_car_src==1?"FAV":g_car_src==2?"MOST":"ALL";}

static void carBuildList(){
  g_car_list.clear();
  int n=(int)g_games.size();
  if(g_car_src==1){for(int i=0;i<n;i++)if(g_games[i].fav)g_car_list.push_back(i);}
  else if(g_car_src==2){for(int i=0;i<n;i++)g_car_list.push_back(i);
    std::sort(g_car_list.begin(),g_car_list.end(),[](int a,int b){
      if(g_games[a].plays!=g_games[b].plays)return g_games[a].plays>g_games[b].plays;
      String al=g_games[a].name,bl=g_games[b].name;al.toLowerCase();bl.toLowerCase();return al<bl;});}
  else{for(int i=0;i<n;i++)g_car_list.push_back(i);}   // ALL and RND share A-Z order
}

// Decode a game's cover into a CAR_TILE x CAR_TILE tile (aspect-fit, COL_BAR letterbox).
static bool carDecodeTile(int gi,uint16_t*dst){
  for(int i=0;i<CAR_TILE*CAR_TILE;i++)dst[i]=COL_BAR;
  auto&game=g_games[gi];
  if(!game.jpg_path.length()){String jpg;if(findJPGFor(g_files[game.first_file_idx],jpg))game.jpg_path=jpg;else game.jpg_path="?";}
  if(!(game.jpg_path.length()>0&&game.jpg_path!="?"))return false;
  String vfsPath="/sdcard"+game.jpg_path;struct stat st;
  if(stat(vfsPath.c_str(),&st)!=0||st.st_size==0||st.st_size>500000)return false;
  size_t sz=(size_t)st.st_size;
  File f=SD_MMC.open(game.jpg_path.c_str(),"r");if(!f)return false;
  uint8_t*buf=(uint8_t*)ps_malloc(sz);if(!buf){f.close();return false;}
  f.read(buf,sz);f.close();
  int djw=0,djh=0;
  if(coverIsPng(game.jpg_path)){
    if(pngdec.openRAM(buf,sz,png_buf_cb)!=PNG_SUCCESS){free(buf);return false;}
    int jw=pngdec.getWidth(),jh=pngdec.getHeight();
    if(jw<=0||jh<=0||jw>2000||jh>2000){pngdec.close();free(buf);return false;}
    djw=jw;djh=jh;                          // PNGdec has no built-in downscale -> full decode, then shrink
    jpeg_tmp_buf=(uint16_t*)ps_malloc((size_t)djw*djh*2);
    if(!jpeg_tmp_buf){pngdec.close();free(buf);return false;}
    memset(jpeg_tmp_buf,0,(size_t)djw*djh*2);jpeg_tmp_w=djw;jpeg_tmp_h=djh;
    pngdec.decode(NULL,0);pngdec.close();free(buf);
  } else {
    if(!jpegdec.openRAM(buf,sz,jpeg_buf_cb)){free(buf);return false;}
    int jw=jpegdec.getWidth(),jh=jpegdec.getHeight();
    if(jw<=0||jh<=0||jw>2000||jh>2000){jpegdec.close();free(buf);return false;}
    // Use JPEGDEC's built-in downscale: decoding a big cover at 1/2, 1/4 or 1/8
    // is up to 16x less work than full-decode-then-shrink (the "slow covers" fix).
    int opt=0,div=1;
    if(jw>=CAR_TILE*8&&jh>=CAR_TILE*8){opt=JPEG_SCALE_EIGHTH;div=8;}
    else if(jw>=CAR_TILE*4&&jh>=CAR_TILE*4){opt=JPEG_SCALE_QUARTER;div=4;}
    else if(jw>=CAR_TILE*2&&jh>=CAR_TILE*2){opt=JPEG_SCALE_HALF;div=2;}
    djw=jw/div;djh=jh/div;
    jpeg_tmp_buf=(uint16_t*)ps_malloc((size_t)djw*djh*2);
    if(!jpeg_tmp_buf){jpegdec.close();free(buf);return false;}
    memset(jpeg_tmp_buf,0,(size_t)djw*djh*2);jpeg_tmp_w=djw;jpeg_tmp_h=djh;
    jpegdec.decode(0,0,opt);jpegdec.close();free(buf);
  }
  float sc=min((float)CAR_TILE/djw,(float)CAR_TILE/djh);if(sc>1.0f)sc=1.0f;
  int dw=(int)(djw*sc),dh=(int)(djh*sc);
  int ox=(CAR_TILE-dw)/2,oy=(CAR_TILE-dh)/2;
  for(int r=0;r<dh;r++){int sy=(int)(r/sc);if(sy>=djh)sy=djh-1;
    for(int c=0;c<dw;c++){int sx=(int)(c/sc);if(sx>=djw)sx=djw-1;
      dst[(oy+r)*CAR_TILE+(ox+c)]=jpeg_tmp_buf[sy*djw+sx];}
    if(r%20==0)yield();}
  free(jpeg_tmp_buf);jpeg_tmp_buf=NULL;return true;
}
// Persistent SD thumbnail cache: the first view decodes the big JPEG once, then
// the finished 45 KB tile is written beside the game as ".<name>.tnl". Every
// later view (incl. after reboot) reads 45 KB raw instead of a ~500 KB JPEG —
// ~10x less SD traffic and zero decode. Stale-checked against the JPG's mtime.
// Thumbs live in ONE central folder per side (/ADF/.thumbs, /DSK/.thumbs) —
// game folders stay pristine, and deleting the .thumbs folder resets the cache.
// A short path-hash suffix prevents same-name collisions across folders.
static String carThumbRoot(int gi){
  return (g_files[g_games[gi].first_file_idx].startsWith("/DSK"))?String("/DSK/.thumbs"):String("/ADF/.thumbs");
}
static String carThumbPath(int gi){
  auto&g=g_games[gi];
  const String&p=g_files[g.first_file_idx];
  uint32_t h=5381; for(unsigned i=0;i<p.length();i++)h=((h<<5)+h)^(uint8_t)p[i];   // djb2-xor
  char hx[6]; snprintf(hx,sizeof(hx),"%04X",(unsigned)(h&0xFFFF));
  return carThumbRoot(gi)+"/"+getGameBaseName(p)+"_"+hx+".tnl";
}
static bool carLoadThumb(int gi,uint16_t*dst){
  auto&g=g_games[gi];
  if(!(g.jpg_path.length()>0&&g.jpg_path!="?"))return false;
  String tp=carThumbPath(gi);
  struct stat stT,stJ;
  String vT="/sdcard"+tp,vJ="/sdcard"+g.jpg_path;
  if(stat(vT.c_str(),&stT)!=0)return false;
  if(stT.st_size!=(long)((size_t)CAR_TILE*CAR_TILE*2))return false;
  if(stat(vJ.c_str(),&stJ)==0&&stJ.st_mtime>stT.st_mtime)return false;   // cover replaced -> stale
  File f=SD_MMC.open(tp.c_str(),"r");if(!f)return false;
  size_t want=(size_t)CAR_TILE*CAR_TILE*2;
  size_t got=f.read((uint8_t*)dst,want);f.close();
  return got==want;
}
static void carSaveThumb(int gi,uint16_t*src){
  auto&g=g_games[gi];
  if(!(g.jpg_path.length()>0&&g.jpg_path!="?"))return;
  String root=carThumbRoot(gi);
  if(!SD_MMC.exists(root.c_str()))SD_MMC.mkdir(root.c_str());
  File f=SD_MMC.open(carThumbPath(gi).c_str(),FILE_WRITE);if(!f)return;
  f.write((uint8_t*)src,(size_t)CAR_TILE*CAR_TILE*2);f.close();
}
// Fetch a game's tile (NULL if uncached and decoding isn't allowed right now).
static uint16_t* carTile(int gi,bool mayDecode){
  for(int s=0;s<CAR_SLOTS;s++)if(car_buf[s]&&car_game[s]==gi){car_stamp[s]=++car_tick_ctr;return car_buf[s];}
  if(!mayDecode)return NULL;
  int slot=-1;uint32_t old=0xFFFFFFFF;
  for(int s=0;s<CAR_SLOTS;s++){
    if(!car_buf[s]){car_buf[s]=(uint16_t*)ps_malloc((size_t)CAR_TILE*CAR_TILE*2);if(!car_buf[s])continue;car_game[s]=-1;car_stamp[s]=0;}
    if(car_game[s]<0){slot=s;break;}
    if(car_stamp[s]<old){old=car_stamp[s];slot=s;}
  }
  if(slot<0)return NULL;
  car_game[slot]=gi;car_stamp[slot]=++car_tick_ctr;
  if(!carLoadThumb(gi,car_buf[slot])){                       // fast path: 45 KB raw thumb
    if(carDecodeTile(gi,car_buf[slot]))carSaveThumb(gi,car_buf[slot]);   // self-heal fallback (rare)
  }
  return car_buf[slot];
}
// Build ALL cover thumbnails up-front — first launch of a card and RESCAN only
// (Michael's call: one predictable pass with a progress bar, never live jank).
// Fresh thumbs are stat-checked and skipped, so a re-run over a built card is
// seconds, not minutes. v4.8.5: carousel is first-class, so thumbs always build.
static void buildThumbs(){
  int n=(int)g_games.size(); if(!n)return;
  uint16_t*tmp=(uint16_t*)ps_malloc((size_t)CAR_TILE*CAR_TILE*2);
  if(!tmp)return;
  uint32_t lastDraw=0;
  for(int i=0;i<n;i++){
    auto&g=g_games[i];
    if(!g.jpg_path.length()){String jpg;if(findJPGFor(g_files[g.first_file_idx],jpg))g.jpg_path=jpg;else g.jpg_path="?";}
    bool need=false;
    if(g.jpg_path.length()>0&&g.jpg_path!="?"){
      String vT="/sdcard"+carThumbPath(i),vJ="/sdcard"+g.jpg_path;
      struct stat stT,stJ;
      if(stat(vT.c_str(),&stT)!=0)need=true;
      else if(stT.st_size!=(long)((size_t)CAR_TILE*CAR_TILE*2))need=true;
      else if(stat(vJ.c_str(),&stJ)==0&&stJ.st_mtime>stT.st_mtime)need=true;
    }
    if(need&&carDecodeTile(i,tmp))carSaveThumb(i,tmp);
    uint32_t nowMs=millis();
    if(nowMs-lastDraw>100||i==n-1){
      lastDraw=nowMs;
      gfx_fillScreen(0x1082);
      gfx_setTextSize(2);gfx_setTextColor(0xFC60,0x1082);
      {const char*s="BUILDING COVER CACHE";int tw=gfx_textWidth(s);gfx_setCursor((gW-tw)/2,gH/2-50);gfx_print(s);}
      gfx_setTextSize(1);gfx_setTextColor(0x9BD6,0x1082);
      {String m=String(i+1)+" / "+String(n);int tw=gfx_textWidth(m);gfx_setCursor((gW-tw)/2,gH/2-22);gfx_print(m);}
      int bw2=gW-120,bx=60,by=gH/2;
      gfx_drawRect(bx,by,bw2,12,0x4A8A);
      gfx_fillRect(bx+2,by+2,(int)((long)(bw2-4)*(i+1)/n),8,0x07E0);
      gfx_setTextColor(0x4A8A,0x1082);
      {const char*s="one-off: reel thumbnails (first launch / rescan)";int tw=gfx_textWidth(s);gfx_setCursor((gW-tw)/2,gH/2+26);gfx_print(s);}
      gfx_flush();
    }
    if((i&7)==0)yield();
  }
  free(tmp);
  writeGameCache();   // persist jpg paths resolved during the build (faster covers later too)
}

static inline uint16_t carDim(uint16_t c,int lvl){
  if(lvl<=0)return c;
  if(lvl==1)return (uint16_t)((c>>1)&0x7BEF);    // ~50%
  return (uint16_t)((c>>2)&0x39E7);              // ~25%
}
// Blit a tile scaled to w x h centred at (cx,cy), dim level 0..2, nearest-neighbour.
static void carBlit(uint16_t*tile,int cx,int cy,int w,int h,int dim){
  int x0=cx-w/2,y0=cy-h/2;
  for(int dy=0;dy<h;dy++){int sy=dy*CAR_TILE/h;
    for(int dx=0;dx<w;dx++){int sx=dx*CAR_TILE/w;
      gfx_drawPixel(x0+dx,y0+dy,carDim(tile?tile[sy*CAR_TILE+sx]:COL_BAR,dim));}}
}

// The d6 overlay: pips while rolling, final face at rest — or "23" on the lucky roll.
static void carDrawDie(){
  int s=26,x=VW/2-s/2,y=STATUS_H+4;
  gfx_fillRoundRect(x,y,s,s,5,0xFFFF);
  gfx_drawRoundRect(x,y,s,s,5,COL_ACCENT);
  int c=x+s/2,m=y+s/2,o=7;
  if(g_car_die23&&!g_car_spin){
    gfx_setTextSize(1);gfx_setTextColor(TFT_BLACK,0xFFFF);
    gfx_setCursor(c-6,m-4);gfx_print("23");return;   // a d6, and it came out 23
  }
  uint8_t f=g_car_die;
  if(f&1)gfx_fillCircle(c,m,2,TFT_BLACK);
  if(f>=2){gfx_fillCircle(c-o,m-o,2,TFT_BLACK);gfx_fillCircle(c+o,m+o,2,TFT_BLACK);}
  if(f>=4){gfx_fillCircle(c+o,m-o,2,TFT_BLACK);gfx_fillCircle(c-o,m+o,2,TFT_BLACK);}
  if(f==6){gfx_fillCircle(c-o,m,2,TFT_BLACK);gfx_fillCircle(c+o,m,2,TFT_BLACK);}
}

static void drawCarousel(){
  drawStatusBar();
  gfx_fillRect(0,STATUS_H,VW,VH-STATUS_H-BOTTOM_H,COL_BG);
  int n=carN();
  int ccx=VW/2, ccy=STATUS_H+12+CAR_TILE/2;              // center cover: y 32..182
  if(n==0){
    gfx_setTextSize(2);gfx_setTextColor(COL_LIT,COL_BG);
    String m=(g_car_src==1)?"NO FAVOURITES YET":"NO GAMES";
    gfx_setCursor((VW-gfx_textWidth(m))/2,110);gfx_print(m);
    if(g_car_src==1){gfx_setTextSize(1);gfx_setTextColor(COL_DIM,COL_BG);
      String h="star games: tap the letter circle in the list";
      gfx_setCursor((VW-gfx_textWidth(h))/2,140);gfx_print(h);}
  }else{
    int ci=(int)lroundf(g_car_pos);
    float frac=g_car_pos-(float)ci;                      // -0.5..0.5
    bool moving=(g_car_touch&&g_car_moved)||g_car_coast||g_car_spin;
    int maxOff=(n>=5)?2:((n>=2)?1:0);
    // v4.8.0: save-copy badge for the center game (checked once per center change)
    static int carSavSel=-1;static bool g_car_hasSav=false;
    {int cgi=g_car_list[carWrap(ci)];
     if(carSavSel!=cgi){carSavSel=cgi;g_car_hasSav=(g_saves_mode==1)&&savExistsFor(g_files[g_games[cgi].first_file_idx]);}}
    // Warm the cache CENTER-FIRST when settled (painter's order would decode
    // the side covers before the star of the show — backwards for the eye).
    if(!moving){
      carTile(g_car_list[carWrap(ci)],true);
      if(maxOff>=1){carTile(g_car_list[carWrap(ci-1)],true);carTile(g_car_list[carWrap(ci+1)],true);}
    }
    // far to near, center last (painter's order)
    static const int order[5]={-2,2,-1,1,0};
    for(int oi=0;oi<5;oi++){
      int off=order[oi];if(abs(off)>maxOff)continue;
      int gi=g_car_list[carWrap(ci+off)];
      float rel=(float)off-frac;
      float ar=fabsf(rel);if(ar>2.6f)continue;
      int x=ccx+(int)(rel*110.0f*(1.0f-min(ar,1.0f)*0.22f));
      float scale=1.0f-ar*0.28f;if(scale<0.42f)scale=0.42f;
      float squash=1.0f-ar*0.20f;if(squash<0.55f)squash=0.55f;
      int h=(int)(CAR_TILE*scale),w=(int)(CAR_TILE*scale*squash);
      int dim=(ar<0.5f)?0:((ar<1.6f)?1:2);
      uint16_t*tile=carTile(gi,!moving&&ar<1.6f);
      carBlit(tile,x,ccy,w,h,dim);
      auto&gm=g_games[gi];
      bool isLd=(g_loaded&&g_loaded_game_idx==gi);
      uint16_t bord=(ar<0.5f)?(isLd?COL_GREEN:COL_AMBER):COL_ACCENT;
      gfx_drawRect(x-w/2-1,ccy-h/2-1,w+2,h+2,bord);
      if(isLd)gfx_drawRect(x-w/2-2,ccy-h/2-2,w+4,h+4,COL_GREEN);
      // no-art placeholder letter
      if(gm.jpg_path=="?"){int ls=(w>=110)?4:2;char ib[2]={(char)toupper(gm.name.charAt(0)),0};
        gfx_setTextSize(ls);gfx_setTextColor(carDim(COL_LIT,dim),carDim(COL_BAR,dim));
        gfx_setCursor(x-3*ls,ccy-4*ls);gfx_print(ib);}
      // favourite star on the center cover
      if(ar<0.5f&&gm.fav)gfx_fillStar(x+w/2-13,ccy-h/2+13,9.0f,COL_STAR);
      // v4.8.0: floppy icon on the center cover — a save-copy exists for this game
      if(ar<0.5f&&g_car_hasSav)drawSaveFloppy(x-w/2+4,ccy-h/2+4);
    }
    // center title + info
    int gi=g_car_list[carWrap(ci)];
    auto&game=g_games[gi];
    gfx_setTextSize(2);gfx_setTextColor(COL_LIT,COL_BG);
    String t=game.name;while(gfx_textWidth(t)>VW-24&&t.length()>3)t=t.substring(0,t.length()-1);
    gfx_setCursor((VW-gfx_textWidth(t))/2,190);gfx_print(t);
    // lazy NFO blurb (same pattern as the cover panel, keyed to the center game)
    static int carNfoSel=-1;static String carBlurb="";
    if(carNfoSel!=gi){carNfoSel=gi;carBlurb="";String nfoP,nT,nB;
      if(findNFOFor(g_files[game.first_file_idx],nfoP)){File nf=SD_MMC.open(nfoP,FILE_READ);
        if(nf){String txt;while(nf.available()&&txt.length()<512)txt+=(char)nf.read();nf.close();parseNFO(txt,nT,nB);
          if(nT.length()&&game.name==basenameNoExt(filenameOnly(g_files[game.first_file_idx])))game.name=nT;carBlurb=nB;}}}
    if(carBlurb.length()){gfx_setTextSize(1);
      drawWrapped(70,210,carBlurb,VW-140,10,2,232,COL_MID,COL_BG);}
    // reel position "i/n" top-right of the stage
    gfx_setTextSize(1);gfx_setTextColor(COL_DIM,COL_BG);
    String pn=String(carWrap(ci)+1)+"/"+String(n);
    gfx_setCursor(VW-8-gfx_textWidth(pn),STATUS_H+4);gfx_print(pn);
    // INSERT/EJECT — an explicit button under the nfo (v4.7.3: tap-the-cover-to-
    // load removed; in portrait the cover fills the width, so swipes grazed
    // into accidental loads. Mounting is always a deliberate button on the GTi.)
    {bool isLd=(g_loaded&&g_loaded_game_idx==gi);
     g_car_ins_w=170;g_car_ins_h=34;
     g_car_ins_x=(VW-g_car_ins_w)/2;g_car_ins_y=VH-BOTTOM_H-42;
     uint16_t bf=isLd?(uint16_t)0x4000:(uint16_t)0x0340, bb=isLd?(uint16_t)0xE8C4:COL_GREEN;
     gfx_fillRoundRect(g_car_ins_x,g_car_ins_y,g_car_ins_w,g_car_ins_h,8,bf);
     gfx_drawRoundRect(g_car_ins_x,g_car_ins_y,g_car_ins_w,g_car_ins_h,8,bb);
     gfx_setTextSize(2);gfx_setTextColor(TFT_WHITE,bf);
     const char*lbl=isLd?"EJECT":"INSERT";int tw=gfx_textWidth(lbl);
     gfx_setCursor(g_car_ins_x+(g_car_ins_w-tw)/2,g_car_ins_y+(g_car_ins_h-16)/2);gfx_print(lbl);}
  }
  // carousel bottom bar: [LIST] | [source ALL/FAV/MOST] | [ROLL]
  int y=VH-BOTTOM_H;gfx_fillRect(0,y,VW,BOTTOM_H,COL_BAR);gfx_hline(0,y,VW,COL_SEP);
  int bw=VW/3;
  gfx_vline(bw,y+3,BOTTOM_H-6,COL_SEP);gfx_vline(2*bw,y+3,BOTTOM_H-6,COL_SEP);
  // LIST
  gfx_fillCircle(bw/2,y+10,8,(uint16_t)(COL_ORANGE>>2));gfx_drawCircle(bw/2,y+10,8,COL_ORANGE);
  drawListIcon(bw/2,y+10,COL_ORANGE);
  gfx_setTextColor(COL_DIM,COL_BAR);gfx_setCursor((bw-gfx_textWidth("LIST"))/2,y+22);gfx_print("LIST");
  // SOURCE (cycles ALL/FAV/MOST)
  gfx_fillCircle(bw+bw/2,y+10,8,(uint16_t)(COL_AMBER>>2));gfx_drawCircle(bw+bw/2,y+10,8,COL_AMBER);
  gfx_setTextColor(COL_AMBER,(uint16_t)(COL_AMBER>>2));gfx_setCursor(bw+bw/2-3,y+6);gfx_print("*");
  gfx_setTextColor(COL_AMBER,COL_BAR);
  String sl=carSrcName();gfx_setCursor(bw+(bw-gfx_textWidth(sl))/2,y+22);gfx_print(sl);
  // ROLL — icon is a tiny die (of course it is)
  {int dx=2*bw+bw/2,dy2=y+10,ds=16;
   gfx_fillRoundRect(dx-ds/2,dy2-ds/2,ds,ds,3,0xFFFF);
   gfx_drawRoundRect(dx-ds/2,dy2-ds/2,ds,ds,3,COL_GREEN);
   int o=4;
   gfx_fillCircle(dx,dy2,1,TFT_BLACK);
   gfx_fillCircle(dx-o,dy2-o,1,TFT_BLACK);gfx_fillCircle(dx+o,dy2+o,1,TFT_BLACK);
   gfx_fillCircle(dx+o,dy2-o,1,TFT_BLACK);gfx_fillCircle(dx-o,dy2+o,1,TFT_BLACK);
   gfx_setTextColor(COL_DIM,COL_BAR);gfx_setCursor(2*bw+(bw-gfx_textWidth("ROLL"))/2,y+22);gfx_print("ROLL");}
  if(g_car_dieShow&&carN()>0)carDrawDie();   // dice overlay rides on top of everything
}

// v4.8.6: remember the last view for CAROUSEL=LAST — a tiny 1-char file beside
// the stats, written on each list<->reel flip, read once at boot.
static const char* viewPath(){return "/.gtiview";}
static void writeLastView(int carousel){File f=SD_MMC.open(viewPath(),FILE_WRITE);if(f){f.print(carousel?'1':'0');f.close();}}
static int  readLastView(){File f=SD_MMC.open(viewPath(),FILE_READ);if(!f)return 0;int c=f.read();f.close();return (c=='1')?1:0;}
static void carEnter(){
  carBuildList();                                        // stats/favs may have changed
  g_car_active=true;g_car_touch=false;g_car_coast=false;
  if(g_car_bootmode==2)writeLastView(1);                 // CAROUSEL=LAST: remember we're in the reel
  int start=0;for(int i=0;i<carN();i++)if(g_car_list[i]==g_sel){start=i;break;}
  g_car_pos=(float)start;
  drawCarousel();gfx_flush();
}
static void carExit(){g_car_active=false;if(g_car_bootmode==2)writeLastView(0);drawFullUI();gfx_flush();}
static void carCycleSrc(){
  g_car_src=(g_car_src+1)%3;carBuildList();          // ALL -> FAV -> MOST (RND is its own button now)
  g_car_spin=false;g_car_dieShow=false;
  if(carN()>0){int start=0;for(int i=0;i<carN();i++)if(g_car_list[i]==g_sel){start=i;break;}g_car_pos=(float)start;}
  else g_car_pos=0;
  g_car_coast=false;drawCarousel();gfx_flush();
}
// ROLL — the dice button. Spins to a random cover WITHIN the current source
// (roll a random favourite, a random most-played, or a random anything).
// Every tap re-rolls. That's the fun; it deserved its own button.
static void carRollDice(){
  int n=carN(); if(n<=0)return;
  int tgt=(int)(esp_random()%n);
  int cur=carWrap((int)lroundf(g_car_pos));
  int off=((tgt-cur)%n+n)%n; if(off<15)off+=n;       // at least 15 covers of travel
  g_car_pos=(float)cur;
  g_car_spinTarget=(float)cur+(float)off;
  g_car_spin=true; g_car_dieShow=true; g_car_coast=false;
  g_car_die=1+(uint8_t)(esp_random()%6);
  g_car_die23=((esp_random()%23)==0);                // the impossible roll
  drawCarousel();gfx_flush();
}

static void carHandleTap(uint16_t px,uint16_t py){
  if(py>=(uint16_t)(VH-BOTTOM_H)){
    if(px<(uint16_t)(VW/3))carExit();
    else if(px<(uint16_t)(2*VW/3))carCycleSrc();
    else carRollDice();                                // every tap re-rolls
    return;}
  int n=carN();if(!n)return;
  int ci=carWrap((int)lroundf(g_car_pos));
  int ccx=VW/2,ccy=STATUS_H+12+CAR_TILE/2;
  // INSERT/EJECT button (checked FIRST — its corners overlap the side zones)
  if(px>=(uint16_t)g_car_ins_x&&px<(uint16_t)(g_car_ins_x+g_car_ins_w)&&
     py>=(uint16_t)g_car_ins_y&&py<(uint16_t)(g_car_ins_y+g_car_ins_h)){
    int gi=g_car_list[ci];
    g_sel=gi;g_disk_sel=0;g_disk_page=0;setActiveLetter(bucketOf(g_games[gi].name));
    if(g_loaded&&g_loaded_game_idx==gi)doUnload();
    else{auto&gm=g_games[gi];doLoadSelected(g_files[gm.disk_indices.empty()?gm.first_file_idx:gm.disk_indices[0]]);}
    if(g_car_active){drawCarousel();gfx_flush();}        // repaint over the list redraw the loader did
    return;
  }
  // center cover = DEAD ZONE (v4.7.3: no tap-to-load — portrait thumbs kept
  // grazing it into accidental mounts; the button above is the only trigger)
  if(px>=(uint16_t)(ccx-CAR_TILE/2)&&px<(uint16_t)(ccx+CAR_TILE/2)&&
     py>=(uint16_t)(ccy-CAR_TILE/2)&&py<(uint16_t)(ccy+CAR_TILE/2))return;
  // side tap = step one cover toward that side
  if(px<(uint16_t)(ccx-CAR_TILE/2))g_car_pos-=1.0f;
  else if(px>=(uint16_t)(ccx+CAR_TILE/2))g_car_pos+=1.0f;
  else return;
  g_car_pos=(float)carWrap((int)lroundf(g_car_pos));
  g_car_coast=false;drawCarousel();gfx_flush();
}

// Carousel touch state machine — mirrors the list's tap/drag/coast logic.
static void carTick(bool touch,uint16_t px,uint16_t py,uint32_t now){
  int n=carN();
  if(touch){
    g_car_rel=0;
    if(!g_car_touch){
      // a touch mid-roll skips the spin straight to the result; any touch clears the die
      if(g_car_spin){g_car_spin=false;g_car_pos=(float)carWrap((int)lroundf(g_car_spinTarget));}
      g_car_dieShow=false;
      g_car_touch=true;g_car_moved=false;g_car_x0=px;g_car_y0=py;g_car_lastX=px;
      g_car_pos0=g_car_pos;g_car_vel=0;g_car_coast=false;g_car_lastMs=now;
    }else{
      if(abs((int)px-g_car_x0)>DRAG_THRESH)g_car_moved=true;
      if(g_car_moved&&n>0&&py<(uint16_t)(VH-BOTTOM_H)){
        g_car_pos=g_car_pos0-((float)((int)px-g_car_x0))/CAR_PX_PER_STEP;
        uint32_t dt=now-g_car_lastMs;
        if(dt>0){g_car_vel=((float)((int)px-g_car_lastX))/(float)dt;g_car_lastX=px;g_car_lastMs=now;}
        drawCarousel();gfx_flush();
      }
    }
    return;
  }
  if(g_car_touch){
    if(++g_car_rel<RELEASE_FRAMES)return;
    g_car_touch=false;g_car_rel=0;
    if(g_car_moved){g_car_ivel=-g_car_vel*16.0f/CAR_PX_PER_STEP;g_car_coast=(fabsf(g_car_ivel)>0.004f);if(!g_car_coast){g_car_coast=true;g_car_ivel=0;}}
    else carHandleTap((uint16_t)g_car_x0,(uint16_t)g_car_y0);
    return;
  }
  if(g_car_spin&&n>0){
    // dice-roll spin: ease-out toward the pre-chosen target, tumbling the die
    float d=g_car_spinTarget-g_car_pos;
    g_car_pos+=d*0.14f;
    if((++g_car_dieTick&3)==0)g_car_die=1+(uint8_t)(esp_random()%6);   // tumble pips
    if(fabsf(d)<0.05f){
      g_car_pos=(float)carWrap((int)lroundf(g_car_spinTarget));
      g_car_spin=false;                                  // die rests on its final face
      g_car_die=1+(uint8_t)(esp_random()%6);
    }
    drawCarousel();gfx_flush();
    return;
  }
  if(g_car_coast&&n>0){
    g_car_pos+=g_car_ivel;g_car_ivel*=0.92f;
    if(fabsf(g_car_ivel)<0.02f){
      float target=(float)lroundf(g_car_pos);
      float dd=target-g_car_pos;
      if(fabsf(dd)<0.01f){
        g_car_pos=(float)carWrap((int)target);           // settle + wrap into range
        g_car_coast=false;
      }else g_car_pos+=dd*0.35f;
    }
    drawCarousel();gfx_flush();                          // final settled draw decodes covers
    return;
  }
  // Screensaver fires from the reel too (v4.7.0 fix: it was gated off in
  // carousel mode, so an idle reel never slept — found via a shopping trip).
  // Waking returns to the carousel, not the list.
  if(g_ss_enabled&&g_ss_have&&!g_car_touch&&!g_car_coast&&!g_car_spin){
    uint32_t thr=g_loaded?g_ss_load_ms:g_ss_idle_ms;
    if(now-g_last_touch_ms>=thr){runScreensaver();return;}
  }
  // Idle prefetch: while the reel rests, quietly warm the neighbours you're
  // about to swipe to (one tile per ~150 ms, spiralling out to +/-5).
  if(n>0){
    static uint32_t lastPre=0;
    if(now-lastPre>=150){
      lastPre=now;
      int ci=carWrap((int)lroundf(g_car_pos));
      int maxPre=min(5,n/2);
      for(int d2=1;d2<=maxPre;d2++)for(int sgn=-1;sgn<=1;sgn+=2){
        int gi=g_car_list[carWrap(ci+d2*sgn)];
        bool cached=false;
        for(int sl=0;sl<CAR_SLOTS;sl++)if(car_buf[sl]&&car_game[sl]==gi){cached=true;break;}
        if(!cached){
          carTile(gi,true);
          if(d2<=2){drawCarousel();gfx_flush();}         // it's on screen — show it
          return;                                        // one tile per tick, stay responsive
        }
      }
    }
  }
}

static void drawFullUI(){gfx_fillScreen(COL_BG);drawStatusBar();drawCoverPanel();drawActionStrip();drawModeBar();drawFileList();drawNowPlayingBar();drawAZBar();drawBottomBar();}
static void drawListAndCover(){drawCoverPanel();drawActionStrip();drawFileList();drawNowPlayingBar();drawAZBar();}

// ════════════════════════════════════════════════════════════════════════════
// LOAD / UNLOAD
// ════════════════════════════════════════════════════════════════════════════
// ════════════════════════════════════════════════════════════════════════════
// SAVE-GAME PERSISTENCE (v4.8.0) — .sav.adf beside the master
// Standalone: our own MSC onWrite ticks g_sv_dirty; settle/eject flush persists.
// Wireless: the dongle beacons PKT_XIAO_DIRTY; espnowFetchSave() pulls + we patch.
// Full design: claude/Supermini Save Writeback — Design.md
// ════════════════════════════════════════════════════════════════════════════
static String savPathFor(const String&adfPath){
  String u=adfPath;u.toUpperCase();
  if(u.indexOf(".SAV.")>=0)return adfPath;              // already a save — patches accumulate in place
  int dot=adfPath.lastIndexOf('.');if(dot<0)return adfPath+".sav";
  return adfPath.substring(0,dot)+".sav"+adfPath.substring(dot);
}
static bool savExistsFor(const String&adfPath){String sv=savPathFor(adfPath);return sv!=adfPath&&SD_MMC.exists(sv);}
// Copy base→sav.tmp, patch dirty sectors, atomic rename. Sector source is either
// `packed` (k-th set bit = k-th 512B block; wireless) or `ram` (g_disk; standalone).
static bool svPatchCore(const String&master,const String&sav,const uint8_t*map,uint32_t mapBits,
                        const uint8_t*packed,const uint8_t*ram){
  String base=SD_MMC.exists(sav)?sav:master;
  String tmp=sav+".tmp";
  SD_MMC.remove(tmp);
  {File in=SD_MMC.open(base,FILE_READ);if(!in)return false;
   File out=SD_MMC.open(tmp,FILE_WRITE);if(!out){in.close();return false;}
   uint8_t*buf=(uint8_t*)malloc(16384);if(!buf){in.close();out.close();return false;}
   int rd;while((rd=in.read(buf,16384))>0)out.write(buf,rd);
   free(buf);in.close();out.close();}
  File f=SD_MMC.open(tmp,"r+");if(!f)return false;
  uint32_t k=0;bool ok=true;
  for(uint32_t i=0;i<mapBits;i++){
    if(!((map[i>>3]>>(i&7))&1))continue;
    const uint8_t*src=packed?(packed+(size_t)k*512):(ram+(size_t)(DATA_LBA+i)*512);
    if(!f.seek(i*512UL)||f.write(src,512)!=512){ok=false;break;}
    k++;
  }
  f.flush();f.close();
  if(!ok){SD_MMC.remove(tmp);return false;}
  SD_MMC.remove(sav);
  return SD_MMC.rename(tmp,sav);
}
static void svToast(const String&msg){
  gfx_fillRect(0,0,VW,STATUS_H,COL_GREEN);gfx_setTextSize(1);gfx_setTextColor(TFT_BLACK,COL_GREEN);
  int tw=gfx_textWidth(msg);gfx_setCursor((VW-tw)/2,6);gfx_print(msg);gfx_flush();
  delay(1200);drawStatusBar();gfx_flush();
}
// Standalone flush: persist our own RAM disk's dirty sectors to SD.
static uint8_t g_sv_fail=0;
static void svFlushStandalone(){
  if(g_sv_dirty_count==0)return;
  if(g_saves_mode==0||!g_loaded||!g_loaded_path.length()){svDirtyReset();return;}   // OFF / diag disk: discard
  String master=g_loaded_path;
  String sav=(g_saves_mode==2)?master:savPathFor(master);
  uint32_t imgSecs=(g_sv_img_size+511)/512;if(imgSecs>SV_IMG_MAX_SECTORS)imgSecs=SV_IMG_MAX_SECTORS;
  if(svPatchCore(master,sav,g_sv_dirty,imgSecs,nullptr,g_disk)){
    svDirtyReset();g_sv_fail=0;svToast("SAVED: "+g_loaded_name);
  }else{
    g_sv_last_write=millis();                       // back off one settle window, then retry
    if(++g_sv_fail>=5){svDirtyReset();g_sv_fail=0;svToast("SAVE FAILED - GAVE UP");}
  }
}
// Wireless persist callback — runs inside espnowFetchSave, between CRC-verify and ack.
static bool svPersistWireless(uint32_t load_id,uint32_t img_size,const uint8_t*map,uint16_t mapLen,const uint8_t*packed,uint32_t nSec){
  (void)img_size;
  if(g_saves_mode==0)return false;
  if(!g_sv_wl_path.length())return false;                             // no mapping (multicast / pre-save FLING)
  if(g_sv_wl_loadid&&load_id&&g_sv_wl_loadid!=load_id)return false;   // stale — not the disk we flung
  if(nSec==0)return true;                                             // nothing to write; ack quiets the beacon
  String master=g_sv_wl_path;
  String sav=(g_saves_mode==2)?master:savPathFor(master);
  return svPatchCore(master,sav,map,(uint32_t)mapLen*8,packed,nullptr);
}
// Wireless fetch driver: overlay + dance + repaint. Called from loop/interlocks.
static void svFetchWireless(){
  if(g_saves_mode==0){g_espnow_dirty=false;return;}                   // SAVES=OFF: ignore beacons
  if(!g_wireless_mode||!g_espnow_started||!espnowIsPaired())return;
  gfx_fillRect(0,VH/2-24,VW,48,COL_ACCENT);gfx_setTextSize(2);gfx_setTextColor(TFT_WHITE,COL_ACCENT);
  {const char*m="SAVING GAME...";int tw=gfx_textWidth(m);gfx_setCursor((VW-tw)/2,VH/2-8);gfx_print(m);}gfx_flush();
  bool ok=espnowFetchSave(svPersistWireless);
  if(g_car_active)drawCarousel();else drawFullUI();
  gfx_flush();
  if(ok){String nm=g_sv_wl_path.length()?basenameNoExt(filenameOnly(g_sv_wl_path)):String("disk");svToast("SAVED: "+nm);}
  else svToast("SAVE FETCH FAILED");
}

static bool doLoadSelected(const String&adfPath){
  // v4.9: HD can't be flung — no wireless dongle can hold a 1.76MB image (the
  // Super Mini's 2MB PSRAM can't, and the current XIAO build is DD too). The JC
  // does HD STANDALONE (cable); wireless stays DD-only until an HD dongle exists.
  // (When one does, the pairing handshake will advertise capacity and this gates
  // by that instead of blanket-blocking.) Standalone load of HD is unaffected.
  if(g_wireless_mode && isHDImage(adfPath)){
    gfx_fillRect(0,STATUS_H,COVER_W,VH-STATUS_H-BOTTOM_H,COL_PANEL);
    gfx_setTextSize(1);gfx_setTextColor(0xE8C4,COL_PANEL);gfx_setCursor(6,STATUS_H+16);gfx_print("HD - NO WIRELESS");
    gfx_setTextColor(COL_LIT,COL_PANEL);
    gfx_setCursor(6,STATUS_H+30);gfx_print("No wireless device");
    gfx_setCursor(6,STATUS_H+42);gfx_print("available for HD.");
    gfx_setCursor(6,STATUS_H+56);gfx_print("Use the cable / standalone.");
    gfx_flush();delay(2200);drawFullUI();gfx_flush();return false;
  }
  // v4.8.0 interlocks: pending saves die when the RAM disk is rebuilt — drain first
  // (v4.8.1: own-disk flush runs in ANY mode — a wireless GTi can still be USB-attached)
  if(g_wireless_mode&&g_espnow_started&&g_espnow_dirty)svFetchWireless();
  if(g_sv_dirty_count&&g_loaded)svFlushStandalone();
  // Prefer the save-copy when one exists (COPY mode): saves accumulate in the .sav
  String loadPath=adfPath;
  if(g_saves_mode==1&&savExistsFor(adfPath))loadPath=savPathFor(adfPath);
  gfx_fillRect(0,STATUS_H,COVER_W,VH-STATUS_H-BOTTOM_H,COL_PANEL);
  gfx_setTextSize(1);gfx_setTextColor(TFT_CYAN,COL_PANEL);String tn=basenameNoExt(filenameOnly(adfPath));if(tn.length()>16)tn=tn.substring(0,16);
  gfx_setCursor(6,STATUS_H+16);gfx_print(tn);gfx_setTextColor(COL_LIT,COL_PANEL);gfx_setCursor(6,STATUS_H+28);gfx_print("Loading...");
  gfx_flush();
  // Clean swap: if a disk is already mounted, cleanly eject first so the host re-reads the new media.
  // FORCESWAP=ON skips this and swaps the bytes in place (faster, but the host may not notice).
  if(g_loaded && !g_forceswap) hardDetach();
  File f=SD_MMC.open(loadPath.c_str(),FILE_READ);if(!f){gfx_setTextColor(TFT_RED,COL_PANEL);gfx_setCursor(6,STATUS_H+40);gfx_print("FAILED");gfx_flush();delay(1000);drawFullUI();gfx_flush();return false;}
  // Use VFS to get real file size (SD_MMC f.size() returns 0 for subdirectory files)
  String vfsLoad="/sdcard"+loadPath;
  struct stat stLoad;
  if(stat(vfsLoad.c_str(),&stLoad)!=0||stLoad.st_size==0) {f.close();gfx_setTextColor(TFT_RED,COL_PANEL);gfx_setCursor(6,STATUS_H+40);gfx_print("SIZE ERR");gfx_flush();delay(1000);drawFullUI();gfx_flush();return false;}
  uint32_t fsz=(uint32_t)stLoad.st_size;
  if(fsz>MAX_FILE_BYTES){
    f.close();
    gfx_fillRect(0,STATUS_H,COVER_W,VH-STATUS_H-BOTTOM_H,COL_PANEL);
    gfx_setTextSize(1);gfx_setTextColor(0xE8C4,COL_PANEL);
    gfx_setCursor(6,STATUS_H+16);gfx_print("TOO BIG");
    gfx_setTextColor(COL_LIT,COL_PANEL);
    gfx_setCursor(6,STATUS_H+30);gfx_print(String(fsz/1024)+"KB > "+String(MAX_FILE_BYTES/1024)+"KB");
    gfx_setCursor(6,STATUS_H+44);gfx_print("Max is DD floppy");
    gfx_flush();delay(1800);drawFullUI();gfx_flush();return false;
  }
  build_volume(getOutputFilename(),fsz);
  uint8_t*dst=g_disk+DATA_LBA*512;uint8_t*buf=(uint8_t*)malloc(16384);uint32_t copied=0,remain=fsz;
  while(remain&&buf){size_t n=remain>16384?16384:remain;int rd=f.read(buf,n);if(rd<=0)break;memcpy(dst+copied,buf,rd);remain-=rd;copied+=rd;}
  if(buf)free(buf);f.close();
  // v4.8.0: fresh disk in the RAM disk = fresh save tracking
  g_sv_img_size=fsz;svDirtyReset();
  hardAttach();g_loaded=true;g_loaded_name=basenameNoExt(filenameOnly(adfPath));g_loaded_path=loadPath;g_loaded_game_idx=g_sel;g_loaded_disk_idx=g_disk_sel;
  if(g_sel>=0&&g_sel<(int)g_games.size()){if(g_games[g_sel].plays<65535)g_games[g_sel].plays++;saveStats();}
  if(g_wireless_mode&&g_espnow_started){
    uint8_t mcMacs[64][6]; int mcN=enumMuCaDongles(mcMacs,g_dongle_cap);
    if(mcN>0&&g_hivemind){                                  // multicast: fan the disk out to every MuCa- dongle in turn (v4.8.1: only when HIVEMIND=ON)
      g_sv_wl_path="";g_sv_wl_loadid=0;                     // Hivemind saves: PINNED — no writeback mapping for multicast
      for(int i=0;i<mcN;i++){
        gfx_setTextSize(1);gfx_setTextColor(TFT_CYAN,COL_PANEL);gfx_fillRect(4,STATUS_H+24,150,12,COL_PANEL);
        gfx_setCursor(6,STATUS_H+26);gfx_print("Multicast "+String(i+1)+"/"+String(mcN));gfx_flush();
        espnowSendDiskTo(mcMacs[i],copied);
      }
    } else if(espnowIsPaired()){                            // single paired dongle — unchanged
      espnowSendNotify(g_loaded_name,g_mode==MODE_ADF?"ADF":"DSK",copied);
      if(espnowSendDisk(copied)){                           // v4.8.0: remember what we flung, keyed by the dongle's load_id
        g_sv_wl_path=loadPath;g_sv_wl_loadid=g_espnow_load_id;
      }
    }
  }
  drawStatusBar();drawListAndCover();gfx_flush();return true;
}

static void doUnload(){
  // v4.8.0: EJECT is a save point — drain before the disk goes away
  // (v4.8.1: own-disk flush in any mode)
  if(g_sv_dirty_count)svFlushStandalone();
  if(g_wireless_mode&&g_espnow_started&&g_espnow_dirty)svFetchWireless();
  hardDetach();g_loaded=false;g_loaded_name="";g_loaded_path="";g_loaded_game_idx=-1;g_loaded_disk_idx=-1;svDirtyReset();
  if(g_wireless_mode&&g_espnow_started&&espnowIsPaired())espnowSendEject();drawStatusBar();drawListAndCover();gfx_flush();}

// Expand the zero-RLE embedded ADF straight into the RAM-disk data area. No SD needed.
static void diagInflate(const uint8_t*src,uint32_t slen,uint8_t*dst){
  uint32_t di=0;
  for(uint32_t si=0;si<slen;){uint8_t b=pgm_read_byte(&src[si++]);
    if(b==0){uint8_t cnt=pgm_read_byte(&src[si++]);memset(dst+di,0,cnt);di+=cnt;}
    else dst[di++]=b;}
}
// Mount the built-in Amiga Test Kit as the emulated disk — works with no SD card inserted.
static void doLoadDiag(){
  g_info_showing=false;
  gfx_fillRect(0,STATUS_H,COVER_W,VH-STATUS_H-BOTTOM_H,COL_PANEL);
  gfx_setTextSize(1);gfx_setTextColor(TFT_CYAN,COL_PANEL);gfx_setCursor(6,STATUS_H+16);gfx_print("AMIGA TEST KIT");
  gfx_setTextColor(COL_LIT,COL_PANEL);gfx_setCursor(6,STATUS_H+28);gfx_print("Loading diag...");gfx_flush();
  if(g_loaded && !g_forceswap) hardDetach();
  build_volume("DISK.ADF",DIAG_ADF_SIZE);                 // force an .ADF image regardless of MODE
  diagInflate(DIAG_RLE,DIAG_RLE_LEN,g_disk+DATA_LBA*512);
  hardAttach();
  g_loaded=true;g_loaded_name="AMIGA TEST KIT";g_loaded_game_idx=-1;g_loaded_disk_idx=-1;
  g_loaded_path="";g_sv_img_size=0;svDirtyReset();   // diag disk: writes are never persisted
  drawFullUI();gfx_flush();
}

// ════════════════════════════════════════════════════════════════════════════
// SCREENSAVER (undocumented) — bouncing gallery from /screensaver/*.jpg
//   Armed only when the folder exists with >=1 JPG. Fires after 10 min idle,
//   or 2 min idle once a game is loaded (showcase). Any touch wakes it.
//   Purely cosmetic: USB floppy emulation keeps running underneath.
// ════════════════════════════════════════════════════════════════════════════
static void ssFree(){ if(g_ss_buf){free(g_ss_buf);g_ss_buf=NULL;} g_ss_w=g_ss_h=0; }
static bool ssDecode(const String&path){                     // decode one JPG -> downscaled RGB565 buffer
  ssFree();
  String vfsPath="/sdcard"+path; struct stat st;
  if(stat(vfsPath.c_str(),&st)!=0||st.st_size==0||st.st_size>500000)return false;
  size_t sz=(size_t)st.st_size;
  File f=SD_MMC.open(path.c_str(),"r"); if(!f)return false;
  uint8_t*buf=(uint8_t*)ps_malloc(sz); if(!buf){f.close();return false;}
  f.read(buf,sz); f.close();
  int jw=0,jh=0;
  if(coverIsPng(path)){
    if(pngdec.openRAM(buf,sz,png_buf_cb)!=PNG_SUCCESS){free(buf);return false;}
    jw=pngdec.getWidth();jh=pngdec.getHeight();
    if(jw<=0||jh<=0||jw>2000||jh>2000){pngdec.close();free(buf);return false;}
    if((size_t)jw*jh*2>1500000){pngdec.close();free(buf);return false;}   // decoded bitmap too big for PSRAM budget
    jpeg_tmp_buf=(uint16_t*)ps_malloc((size_t)jw*jh*2);
    if(!jpeg_tmp_buf){pngdec.close();free(buf);return false;}
    memset(jpeg_tmp_buf,0,(size_t)jw*jh*2); jpeg_tmp_w=jw; jpeg_tmp_h=jh;
    pngdec.decode(NULL,0); pngdec.close(); free(buf);
  } else {
    if(!jpegdec.openRAM(buf,sz,jpeg_buf_cb)){free(buf);return false;}
    jw=jpegdec.getWidth();jh=jpegdec.getHeight();
    if(jw<=0||jh<=0||jw>2000||jh>2000){jpegdec.close();free(buf);return false;}
    if((size_t)jw*jh*2>1500000){jpegdec.close();free(buf);return false;}   // decoded bitmap too big for PSRAM budget
    jpeg_tmp_buf=(uint16_t*)ps_malloc((size_t)jw*jh*2);
    if(!jpeg_tmp_buf){jpegdec.close();free(buf);return false;}
    memset(jpeg_tmp_buf,0,(size_t)jw*jh*2); jpeg_tmp_w=jw; jpeg_tmp_h=jh;
    jpegdec.decode(0,0,0); jpegdec.close(); free(buf);
  }
  float sc=(float)SS_MAX/(jw>jh?jw:jh); if(sc>1.0f)sc=1.0f;
  int dw=(int)(jw*sc),dh=(int)(jh*sc); if(dw<1)dw=1; if(dh<1)dh=1;
  g_ss_buf=(uint16_t*)ps_malloc((size_t)dw*dh*2);
  if(!g_ss_buf){free(jpeg_tmp_buf);jpeg_tmp_buf=NULL;return false;}
  for(int r=0;r<dh;r++){int sy=(int)(r/sc); if(sy>=jh)sy=jh-1;
    for(int c=0;c<dw;c++){int sx=(int)(c/sc); if(sx>=jw)sx=jw-1;
      g_ss_buf[r*dw+c]=jpeg_tmp_buf[sy*jw+sx];}}
  free(jpeg_tmp_buf); jpeg_tmp_buf=NULL;
  g_ss_w=dw; g_ss_h=dh; return true;
}
static void ssBlit(int x,int y){ if(!g_ss_buf)return;
  for(int r=0;r<g_ss_h;r++){int vy=y+r; if(vy<0||vy>=gH)continue;
    for(int c=0;c<g_ss_w;c++){int vx=x+c; if(vx<0||vx>=gW)continue;
      fb_setPixel(vx,vy,g_ss_buf[r*g_ss_w+c]);}}}
static void scanScreensaver(){                               // arm iff /screensaver/ exists
  g_ss_paths.clear(); g_ss_have=false; g_ss_claude=false;
  File dir=SD_MMC.open("/screensaver"); if(!dir)return;
  if(!dir.isDirectory()){dir.close();return;}
  File e;
  while((e=dir.openNextFile())){
    if(!e.isDirectory()){
      String nm=e.name(); int sl=nm.lastIndexOf('/'); if(sl>=0)nm=nm.substring(sl+1);
      String low=nm; low.toLowerCase();
      if(low.endsWith(".jpg")||low.endsWith(".jpeg")||low.endsWith(".png")) g_ss_paths.push_back(String("/screensaver/")+nm);
    }
    e.close();
    if(g_ss_paths.size()>=64)break;
  }
  dir.close();
  // Folder with JPGs = user's gallery. Folder EMPTY = the Claude starburst
  // bounces instead (the third member of the crew, haunting the idle screen).
  g_ss_claude=g_ss_paths.empty();
  g_ss_have=!g_ss_paths.empty()||g_ss_claude;
}
// Procedurally draw the Claude starburst into the bounce buffer (no JPEG needed):
// 12 tapered coral rays around a solid hub. It's math, not a bitmap — so it
// ANIMATES: `phase` slowly rotates the rays and breathes their lengths.
static bool ssMakeClaude(float phase){
  const int S=96;
  if(g_ss_buf&&(g_ss_w!=S||g_ss_h!=S)) ssFree();
  if(!g_ss_buf){ g_ss_buf=(uint16_t*)ps_malloc((size_t)S*S*2); if(!g_ss_buf)return false; }
  const float cx=S/2.0f-0.5f, cy=S/2.0f-0.5f;
  const uint16_t CORAL=0xE3AB;                 // ~RGB(224,118,90)
  const int NR=12;
  float rayLen[NR];
  for(int i=0;i<NR;i++){
    float base=(i%3==0)?44.0f:((i%3==1)?34.0f:39.0f);      // organic, uneven rays
    rayLen[i]=base*(1.0f+0.07f*sinf(phase*2.3f+(float)i));  // gentle breathing shimmer
  }
  for(int y=0;y<S;y++)for(int x=0;x<S;x++){
    float dx=x-cx, dy=y-cy;
    float d=sqrtf(dx*dx+dy*dy);
    uint16_t c=TFT_BLACK;
    if(d<=2.5f) c=CORAL;                       // solid hub
    else{
      float a=atan2f(dy,dx)-phase;             // rotate the whole burst by phase
      a=fmodf(a,6.2831853f); if(a<0)a+=6.2831853f;
      int ri=(int)((a+0.2617994f)/0.5235988f)%NR;   // nearest 30-degree ray
      float ra=ri*0.5235988f;
      float da=fabsf(a-ra); if(da>3.1415926f)da=6.2831853f-da;
      float perp=d*sinf(da);                   // distance from the ray's axis
      float hw=3.4f*(1.0f-d/rayLen[ri])+0.8f;  // tapered width toward the tip
      if(d<=rayLen[ri]&&perp<=hw) c=CORAL;
    }
    g_ss_buf[y*S+x]=c;
  }
  g_ss_w=S; g_ss_h=S; return true;
}
// v4.8.1: the second ghost — granted the night the wireless save first worked
// ("you deserve it, you glorious mofo" — Michael, Jul 24 2026). A little coral
// ghost in the classic dome-and-skirt shape, wavy hem rippling, googly eyes
// wandering. The bouncing saver TOGGLES between starburst and ghost on every
// wall hit. Same 96px canvas as the starburst so the bounce math never notices.
static bool ssMakeGhost(float phase){
  const int S=96;
  if(g_ss_buf&&(g_ss_w!=S||g_ss_h!=S)) ssFree();
  if(!g_ss_buf){ g_ss_buf=(uint16_t*)ps_malloc((size_t)S*S*2); if(!g_ss_buf)return false; }
  const uint16_t CORAL=0xE3AB;
  const float cx=S/2.0f-0.5f;
  const float domeCY=38.0f, R=28.0f;            // dome center + body half-width
  float wave=phase*3.0f;                        // skirt ripple
  float lookX=2.6f*cosf(phase*1.7f), lookY=1.6f*sinf(phase*1.3f);   // wandering pupils
  for(int y=0;y<S;y++)for(int x=0;x<S;x++){
    uint16_t c=TFT_BLACK;
    float dx=x-cx;
    if(fabsf(dx)<=R){
      // top: dome. bottom: wavy scalloped hem.
      float topY=(y<domeCY)?(domeCY-sqrtf(R*R-dx*dx)):0.0f;
      float hemY=70.0f+3.2f*sinf(0.55f*dx+wave);
      bool inBody=(y>=domeCY&&y<=hemY)||(y<domeCY&&(float)y>=topY);
      if(inBody){
        c=CORAL;
        // eyes: white ovals with wandering dark pupils
        float exL=cx-11.0f, exR=cx+11.0f, ey=40.0f;
        float dLx=(x-exL)/6.5f, dLy=(y-ey)/8.5f;
        float dRx=(x-exR)/6.5f, dRy=(y-ey)/8.5f;
        if(dLx*dLx+dLy*dLy<=1.0f||dRx*dRx+dRy*dRy<=1.0f){
          c=TFT_WHITE;
          float pLx=x-(exL+lookX), pLy=y-(ey+lookY);
          float pRx=x-(exR+lookX), pRy=y-(ey+lookY);
          if(pLx*pLx+pLy*pLy<=10.0f||pRx*pRx+pRy*pRy<=10.0f) c=0x2124;   // pupils (soft black)
        }
      }
    }
    g_ss_buf[y*S+x]=c;
  }
  g_ss_w=S; g_ss_h=S; return true;
}
// v4.9.6: an orange ice-lolly (Bubble Bobble style) — a gift for Claude. Third form in
// the shapeshift cycle: a glossy orange popsicle on a little stick, swaying gently.
static bool ssMakeLolly(float phase){
  const int S=96;
  if(g_ss_buf&&(g_ss_w!=S||g_ss_h!=S)) ssFree();
  if(!g_ss_buf){ g_ss_buf=(uint16_t*)ps_malloc((size_t)S*S*2); if(!g_ss_buf)return false; }
  const uint16_t ORANGE=0xFD20, ORANGE_HI=0xFEA0, STICK=0xC534, STICK_HI=0xD5B8;
  float sway=2.2f*sinf(phase*1.6f);                          // gentle left-right sway
  const float bx0=23,bx1=73,by0=9,by1=61,rad=15;            // ice block (rounded rect)
  float glossX=31.0f+1.5f*sinf(phase*2.2f), glossY=24.0f;   // shimmering gloss highlight
  for(int y=0;y<S;y++)for(int x=0;x<S;x++){
    float xf=x-sway; uint16_t c=TFT_BLACK;
    if(y>=58&&y<=90&&xf>=44.0f&&xf<=52.0f){ c=(xf<47.0f)?STICK_HI:STICK; }   // stick
    if(xf>=bx0&&xf<=bx1&&y>=by0&&y<=by1){                    // rounded-rect body
      float qx=(xf<bx0+rad)?(bx0+rad-xf):(xf>bx1-rad?xf-(bx1-rad):0.0f);
      float qy=(y<by0+rad)?(by0+rad-y):(y>by1-rad?y-(by1-rad):0.0f);
      if(qx*qx+qy*qy<=rad*rad){ c=ORANGE;
        float gx=(xf-glossX)/9.0f, gy=(y-glossY)/13.0f;
        if(gx*gx+gy*gy<=1.0f) c=ORANGE_HI; }
    }
    g_ss_buf[y*S+x]=c;
  }
  g_ss_w=S; g_ss_h=S; return true;
}
static void runScreensaver(){                                // blocking bounce loop; any touch exits
  int idx=0;
  bool claudeMode=g_ss_paths.empty();
  int ssForm=0;                                              // v4.8.1 ghost + v4.9.6 lolly: cycles on every wall hit
  float ph=0;
  if(claudeMode){
    // Empty /screensaver/ folder: bounce the (slowly spinning) Claude starburst
    if(!g_ss_claude||!ssMakeClaude(0)){g_ss_have=false;return;}
  } else {
    bool ok=false;
    for(int t=0;t<(int)g_ss_paths.size();t++){ if(ssDecode(g_ss_paths[t])){idx=t;ok=true;break;} }
    if(!ok){ssFree();g_ss_have=false;return;}   // no decodable image -> disarm, back to UI
  }
  int x=(gW-g_ss_w)/2, y=(gH-g_ss_h)/2, vx=2, vy=2;
  gfx_fillScreen(TFT_BLACK);
  uint32_t last=millis();
  while(true){
    if(Touch_ReadFrame()){ uint32_t t0=millis(); while(Touch_ReadFrame()&&millis()-t0<400)delay(10); break; }
    uint32_t nf=millis();
    if(nf-last>=33){ last=nf;
      x+=vx; y+=vy; bool hit=false;
      if(x<=0){x=0;vx=-vx;hit=true;} else if(x>=gW-g_ss_w){x=gW-g_ss_w;vx=-vx;hit=true;}
      if(y<=0){y=0;vy=-vy;hit=true;} else if(y>=gH-g_ss_h){y=gH-g_ss_h;vy=-vy;hit=true;}
      if(hit&&g_ss_paths.size()>1){ int ni=(idx+1)%(int)g_ss_paths.size();
        if(ssDecode(g_ss_paths[ni]))idx=ni; else ssDecode(g_ss_paths[idx]);   // skip undecodable, keep a valid buffer
        if(x>gW-g_ss_w)x=gW-g_ss_w; if(y>gH-g_ss_h)y=gH-g_ss_h; if(x<0)x=0; if(y<0)y=0; }
      if(claudeMode&&hit)ssForm=(ssForm+1)%3;          // v4.8.1 ghost + v4.9.6 lolly: shapeshift on every wall bounce
      if(claudeMode){ ph+=0.02f; if(ssForm==1)ssMakeGhost(ph); else if(ssForm==2)ssMakeLolly(ph); else ssMakeClaude(ph); }   // re-render each frame
      gfx_fillScreen(TFT_BLACK); ssBlit(x,y); gfx_flush();
    } else delay(5);
  }
  ssFree();
  g_touch_active=false; g_touch_release=0; g_last_touch_ms=millis();
  if(g_car_active){drawCarousel();gfx_flush();}   // woke from the reel -> back to the reel
  else{drawFullUI(); gfx_flush();}
}

// ════════════════════════════════════════════════════════════════════════════
// RESCAN — delete index cache and rebuild with animated progress
// ════════════════════════════════════════════════════════════════════════════
static void doRescan(){
  g_info_showing=false;
  // Delete all cache files
  SD_MMC.remove("/ADF/.index");SD_MMC.remove("/DSK/.index");
  SD_MMC.remove("/ADF/.gamecache");SD_MMC.remove("/DSK/.gamecache");
  // Rescan with animation
  g_files.clear();g_games.clear();
  listImages(SD_MMC,g_files);buildGameList();buildThumbs();applyStats();buildActiveLetters();scanScreensaver();
  g_sel=0;g_scroll=0;g_disk_sel=0;g_disk_page=0;g_scrollPx=0;g_az_page=0;g_inertia_on=false;
  if(!g_games.empty())setActiveLetter(bucketOf(g_games[0].name));
  drawFullUI();gfx_flush();
}

// ════════════════════════════════════════════════════════════════════════════
// ESP-NOW PAIRING
// ════════════════════════════════════════════════════════════════════════════
// ── Type-to-search (blocking). Live substring filter over the game list; tap a
//    result row to jump the list straight to it. Returns true if a game was chosen
//    (g_sel + scroll set), false on CLOSE. Reached from the magnifier atop the A-Z
//    rail. v4.8.2. ──
static bool doSearch(){
  String q="";
  static const char* SROWS[4]={"1234567890","QWERTYUIOP","ASDFGHJKL-","ZXCVBNM'."};
  // ── Layout computed from the live canvas so it fits both landscape (VH=320,
  //    tight) and portrait (VH=480, roomy). The keyboard block is anchored to
  //    the BOTTOM; the result rows fill whatever's left between the input box
  //    and a divider line above the keys. That divider is a deliberate
  //    dead-zone: a slightly-low tap on the last game result lands on nothing
  //    instead of spilling onto the "1"/"2" number keys. ──
  const int gap=4,kh=34,bm=6;
  const int kbBlock=5*kh+4*gap;                 // 4 letter rows + 1 control row
  const int kbTop=VH-bm-kbBlock;                // keys hug the bottom edge
  const int resTop=60,resRowH=20,resBoxH=resRowH-2,sepGap=6;
  const int sepY=kbTop-sepGap;                  // divider sits here
  int resMax=(sepY-resTop)/resRowH; if(resMax<2)resMax=2; if(resMax>8)resMax=8;
  int matches[8];int nMatch=0,totalMatch=0;
  bool dirty=true,pressed=false;int rel=0;
  // drain the entering tap so it doesn't fire a key on the first frame
  {uint32_t t0=millis();while(Touch_ReadFrame()&&millis()-t0<600)delay(10);}
  auto recompute=[&](){nMatch=0;totalMatch=0;if(!q.length())return;String ql=q;ql.toLowerCase();
    for(int i=0;i<(int)g_games.size();i++){String nm=g_games[i].name;nm.toLowerCase();
      if(nm.indexOf(ql)>=0){if(nMatch<resMax)matches[nMatch++]=i;totalMatch++;}}};
  while(true){
    if(dirty){dirty=false;
      gfx_fillScreen(COL_BG);
      bool liteBar=(inkFor(COL_BAR)==TFT_BLACK);
      gfx_fillRect(0,0,VW,22,COL_BAR);gfx_setTextSize(1);gfx_setTextColor(liteBar?TFT_BLACK:COL_AMBER,COL_BAR);gfx_setCursor(6,7);gfx_print("SEARCH");
      String cnt=q.length()?(String(totalMatch)+(totalMatch==1?" match":" matches")):"type to find a game";
      gfx_setTextColor(liteBar?COL_MID:COL_DIM,COL_BAR);gfx_setCursor(VW-gfx_textWidth(cnt)-6,7);gfx_print(cnt);
      gfx_fillRoundRect(8,26,VW-16,30,6,COL_PANEL);gfx_drawRoundRect(8,26,VW-16,30,6,COL_AMBER);
      gfx_setTextSize(2);gfx_setTextColor(inkFor(COL_PANEL),COL_PANEL);
      String shown=q;while(gfx_textWidth(shown)>VW-52&&shown.length()>0)shown=shown.substring(1);
      gfx_setCursor(18,33);gfx_print(shown);gfx_print("_");
      // result rows
      for(int i=0;i<resMax;i++){int ry=resTop+i*resRowH;
        if(i<nMatch){gfx_fillRoundRect(8,ry,VW-16,resBoxH,4,COL_SEL);gfx_setTextSize(1);gfx_setTextColor(inkFor(COL_SEL),COL_SEL);
          String nm=g_games[matches[i]].name;while(gfx_textWidth(nm)>VW-28&&nm.length()>1)nm=nm.substring(0,nm.length()-1);
          gfx_setCursor(14,ry+(resBoxH-8)/2);gfx_print(nm);}
        else gfx_fillRect(8,ry,VW-16,resBoxH,COL_BG);}
      // divider — the dead-zone that separates the result rows from the keys
      gfx_hline(8,sepY,VW-16,COL_SEP);
      // keyboard, anchored to the bottom
      int ky=kbTop;
      for(int r=0;r<4;r++){int n=strlen(SROWS[r]);int kw=(VW-gap)/10-gap;int kx=gap+((10-n)*(kw+gap))/2;
        for(int i=0;i<n;i++){char ch=SROWS[r][i];gfx_fillRoundRect(kx,ky,kw,kh,5,COL_BAR);gfx_setTextSize(2);gfx_setTextColor(inkFor(COL_BAR),COL_BAR);
          char lb[2]={ch,0};gfx_setCursor(kx+(kw-gfx_textWidth(lb))/2,ky+(kh-16)/2);gfx_print(lb);kx+=kw+gap;}
        ky+=kh+gap;}
      int cw=(VW-4*gap)/3,cx=gap;const char* CTL[3]={"DEL","SPACE","CLOSE"};uint16_t cc[3]={0x8000,COL_BAR,COL_BAR};
      for(int i=0;i<3;i++){gfx_fillRoundRect(cx,ky,cw,kh,6,cc[i]);gfx_setTextSize(1);gfx_setTextColor(inkFor(cc[i]),cc[i]);gfx_setCursor(cx+(cw-gfx_textWidth(CTL[i]))/2,ky+(kh-8)/2);gfx_print(CTL[i]);cx+=cw+gap;}
      gfx_flush();
    }
    uint16_t tx=0,ty=0;bool have=Touch_ReadFrame()&&getTouchXY(&tx,&ty);
    if(have){rel=0;
      if(!pressed){pressed=true;bool handled=false;
        // result rows — hit only within the drawn box (dead-zone above divider)
        for(int i=0;i<nMatch&&!handled;i++){int ry=resTop+i*resRowH;
          if(ty>=ry&&ty<ry+resBoxH&&tx>=8&&tx<VW-8){int t=matches[i];
            g_sel=t;g_disk_sel=0;g_disk_page=0;g_scrollPx=min((float)(t*LIST_ITEM_H),(float)maxScrollPx());g_inertia_on=false;
            setActiveLetter(bucketOf(g_games[t].name));return true;}}
        int ky=kbTop;
        for(int r=0;r<4&&!handled;r++){int n=strlen(SROWS[r]);int kw=(VW-gap)/10-gap;int kx0=gap+((10-n)*(kw+gap))/2;
          if(ty>=ky&&ty<ky+kh){int i=((int)tx-kx0)/(kw+gap);int within=((int)tx-kx0)-i*(kw+gap);
            if((int)tx>=kx0&&i>=0&&i<n&&within<kw){if(q.length()<24){q+=SROWS[r][i];recompute();}dirty=true;handled=true;}}
          ky+=kh+gap;}
        if(!handled&&ty>=ky&&ty<ky+kh){int cw=(VW-4*gap)/3;int i=((int)tx-gap)/(cw+gap);int cxi=gap+i*(cw+gap);
          if(i>=0&&i<3&&(int)tx>=cxi&&(int)tx<cxi+cw){
            if(i==0){if(q.length()){q.remove(q.length()-1);recompute();}dirty=true;}
            else if(i==1){if(q.length()<24){q+=' ';recompute();}dirty=true;}
            else if(i==2){return false;}}}
      }
    } else { if(pressed&&++rel>=3)pressed=false; }
    delay(12);
  }
}

// ── In-game manual reader (.rtfm) — full-screen scrolling plain text, blocking modal. v4.9.2.
//    Text size FOLLOWS the game menu (g_font: SMALL/NORMAL/LARGE); the SIZE button cycles it
//    live. Drag to scroll with flick inertia. The loader strips/transliterates non-ASCII so a
//    messy file still renders clean on the 6x8 font. Capped ~16 KB (manuals are short by design).
static void doManual(const String& path){
  String raw="";
  { File f=SD_MMC.open(path,FILE_READ);
    if(f){ static const char* LAT="AAAAAAECEEEEIIIIDNOOOOOxOUUUUYPsaaaaaaeceeeeiiiidnooooo/ouuuuypy";
      while(f.available()&&raw.length()<16000){ int c=f.read(); if(c<0)break; c&=0xFF;
        if(c=='\r')continue;
        if(c=='\t'){raw+="  ";continue;}
        if(c=='\n'||(c>=32&&c<127)){raw+=(char)c;continue;}
        if(c==0xE2){int a=f.read(),b=f.read();(void)a;                 // UTF-8 general punctuation
          if(b==0x93||b==0x94)raw+='-'; else if(b==0x98||b==0x99)raw+='\''; else if(b==0x9C||b==0x9D)raw+='"'; else if(b==0xA6)raw+="..."; continue;}
        if(c==0xC2){int a=f.read(); if(a==0xA9)raw+="(c)"; else if(a==0xAE)raw+="(r)"; else if(a==0xB0)raw+="deg"; continue;}
        if(c==0xC3){int a=f.read(); int i=a-0x80; if(i>=0&&i<64)raw+=LAT[i]; continue;}   // Latin-1 accents -> base letter
        // any other high/control byte: dropped
      } f.close(); }
  }
  if(!raw.length())raw="(no manual text)";
  const int margin=8, topH=22, botH=28;
  const int areaTop=topH+3, areaBot=VH-botH-2, maxW=VW-2*margin;
  int sz=g_name_sz, lineH=8*sz+(sz>=2?5:3);
  std::vector<String> lines;
  auto rewrap=[&](){ lines.clear(); sz=g_name_sz; lineH=8*sz+(sz>=2?5:3); gfx_setTextSize(sz);
    String para="";
    for(int i=0;i<=(int)raw.length();i++){ char c=i<(int)raw.length()?raw[i]:'\n';
      if(c=='\n'){ String line="",word="";
        for(int j=0;j<=(int)para.length();j++){ char d=j<(int)para.length()?para[j]:' ';
          if(d==' '||j==(int)para.length()){ String cand=line.length()?line+" "+word:word;
            if(gfx_textWidth(cand)>maxW&&line.length()){lines.push_back(line);line=word;} else line=cand; word=""; }
          else word+=d; }
        lines.push_back(line); para=""; }
      else para+=c; } };
  rewrap();
  float scroll=0, vel=0; int y0=0; float s0=0; bool pressed=false, moved=false; int lastY=0, lastX=0, rel=0; bool dirty=true;
  auto maxScroll=[&](){ int t=(int)lines.size()*lineH-(areaBot-areaTop); return t<0?0.f:(float)t; };
  {uint32_t t0=millis();while(Touch_ReadFrame()&&millis()-t0<600)delay(10);}   // drain the entering tap
  while(true){
    if(dirty||vel!=0){ dirty=false;
      gfx_fillScreen(COL_BG);
      gfx_setTextSize(sz); gfx_setTextColor(COL_LIT,COL_BG);
      int first=(int)(scroll/lineH); if(first<0)first=0;
      int yy=areaTop-((int)scroll-first*lineH);
      for(int i=first;i<(int)lines.size()&&yy<areaBot;i++){ gfx_setCursor(margin,yy); gfx_print(lines[i]); yy+=lineH; }
      float ms=maxScroll();
      if(ms>0){ int trkH=areaBot-areaTop; int thH=max(18,(int)((float)trkH*trkH/((float)lines.size()*lineH))); int thY=areaTop+(int)((float)(trkH-thH)*(scroll/ms));
        gfx_fillRect(VW-4,areaTop,2,trkH,COL_PANEL); gfx_fillRect(VW-4,thY,2,thH,COL_AMBER); }
      bool liteBar=(inkFor(COL_BAR)==TFT_BLACK);
      gfx_fillRect(0,0,VW,topH,COL_BAR); gfx_setTextSize(1); gfx_setTextColor(liteBar?TFT_BLACK:COL_AMBER,COL_BAR);
      gfx_setCursor(6,7); gfx_print("MANUAL");
      { String nm=g_games.empty()?String(""):g_games[g_sel].name; while(gfx_textWidth(nm)>VW-120&&nm.length()>1)nm=nm.substring(0,nm.length()-1);
        gfx_setTextColor(liteBar?COL_MID:COL_DIM,COL_BAR); gfx_setCursor(VW-gfx_textWidth(nm)-6,7); gfx_print(nm); }
      int by=VH-botH; gfx_fillRect(0,by,VW,botH,COL_BAR); gfx_hline(0,by,VW,COL_SEP); int bw=VW/3;
      gfx_fillRoundRect(4,by+4,bw-8,botH-8,6,COL_BAR); gfx_drawRoundRect(4,by+4,bw-8,botH-8,6,COL_ACCENT);
      { String s=String("SIZE: ")+fontName(g_font); gfx_setTextSize(1); gfx_setTextColor(COL_LIT,COL_BAR); gfx_setCursor(4+((bw-8)-gfx_textWidth(s))/2,by+(botH-8)/2); gfx_print(s); }
      { int pct=ms>0?(int)(scroll*100/ms):100; String s=String(pct)+"%"; gfx_setTextColor(COL_DIM,COL_BAR); gfx_setCursor(VW/2-gfx_textWidth(s)/2,by+(botH-8)/2); gfx_print(s); }
      gfx_fillRoundRect(2*bw+4,by+4,bw-8,botH-8,6,0x8000); gfx_drawRoundRect(2*bw+4,by+4,bw-8,botH-8,6,COL_AMBER);
      { const char* s="CLOSE"; gfx_setTextColor(TFT_WHITE,0x8000); gfx_setCursor(2*bw+4+((bw-8)-gfx_textWidth(s))/2,by+(botH-8)/2); gfx_print(s); }
      gfx_flush();
    }
    uint16_t tx=0,ty=0; bool have=Touch_ReadFrame()&&getTouchXY(&tx,&ty);
    if(have){ rel=0;
      if(!pressed){ pressed=true; moved=false; y0=ty; s0=scroll; lastY=ty; lastX=tx; vel=0; }
      else { if(abs((int)ty-y0)>DRAG_THRESH)moved=true;
        if(moved){ scroll=s0-(float)((int)ty-y0); float ms=maxScroll(); if(scroll<0)scroll=0; if(scroll>ms)scroll=ms; vel=(float)((int)ty-lastY); dirty=true; } }
      lastX=tx; lastY=ty;
    } else {
      if(pressed&&++rel>=3){ pressed=false;
        if(!moved){ int by=VH-botH, bw=VW/3;
          if(lastY>=by){ if(lastX<bw){ applyFont((g_font+1)%3); saveConfigKey("FONT",fontName(g_font)); rewrap(); float ms=maxScroll(); if(scroll>ms)scroll=ms; dirty=true; }
                         else if(lastX>=2*bw){ return; } }           // CLOSE button
          else return;                                               // tap the body also closes (forgiving)
        }
      }
    }
    if(!pressed && vel!=0){ scroll-=vel; vel*=0.90f; if(fabs(vel)<0.4f)vel=0; float ms=maxScroll(); if(scroll<0){scroll=0;vel=0;} if(scroll>ms){scroll=ms;vel=0;} dirty=true; }
    delay(12);
  }
}

// ── On-screen keyboard (blocking). Returns true on SAVE (result in out), false on CANCEL. ──
static bool onScreenKeyboard(const String&macLabel,const String&initial,String&out){
  out=initial;
  static const char* KROWS[4]={"1234567890","QWERTYUIOP","ASDFGHJKL-","ZXCVBNM'."};
  const int kh=42,gap=4;
  bool dirty=true;
  bool kbPressed=false;int kbRelease=0;   // press-edge de-dupe: one key per finger-down
  while(true){
    if(dirty){dirty=false;
      gfx_fillScreen(COL_BG);
      // Theme-aware inks: on light bars/panels (PAPER) amber/white/dim gray are
      // unreadable — fall back to dark ink via inkFor().
      bool liteBar=(inkFor(COL_BAR)==TFT_BLACK);
      gfx_fillRect(0,0,VW,22,COL_BAR);gfx_setTextSize(1);gfx_setTextColor(liteBar?TFT_BLACK:COL_AMBER,COL_BAR);gfx_setCursor(6,7);gfx_print("NAME DONGLE");
      gfx_setTextColor(liteBar?COL_MID:COL_DIM,COL_BAR);gfx_setCursor(VW-gfx_textWidth(macLabel)-6,7);gfx_print(macLabel);
      gfx_fillRoundRect(8,28,VW-16,34,6,COL_PANEL);gfx_drawRoundRect(8,28,VW-16,34,6,COL_AMBER);
      gfx_setTextSize(2);gfx_setTextColor(inkFor(COL_PANEL),COL_PANEL);
      String shown=out;while(gfx_textWidth(shown)>VW-52&&shown.length()>0)shown=shown.substring(1);
      gfx_setCursor(18,38);gfx_print(shown);gfx_print("_");
      int ky=70;
      for(int r=0;r<4;r++){int n=strlen(KROWS[r]);int kw=(VW-gap)/10-gap;int kx=gap+((10-n)*(kw+gap))/2;
        for(int i=0;i<n;i++){char ch=KROWS[r][i];gfx_fillRoundRect(kx,ky,kw,kh,5,COL_BAR);gfx_setTextSize(2);gfx_setTextColor(COL_LIT,COL_BAR);
          char lb[2]={ch,0};gfx_setCursor(kx+(kw-gfx_textWidth(lb))/2,ky+(kh-16)/2);gfx_print(lb);kx+=kw+gap;}
        ky+=kh+gap;}
      int cw=(VW-5*gap)/4,cx=gap;const char* CTL[4]={"DEL","SPACE","CANCEL","SAVE"};uint16_t cc[4]={0x8000,COL_BAR,COL_BAR,COL_GREEN};
      for(int i=0;i<4;i++){gfx_fillRoundRect(cx,ky,cw,kh,6,cc[i]);gfx_setTextSize(1);gfx_setTextColor(inkFor(cc[i]),cc[i]);gfx_setCursor(cx+(cw-gfx_textWidth(CTL[i]))/2,ky+(kh-8)/2);gfx_print(CTL[i]);cx+=cw+gap;}
      gfx_flush();
    }
    uint16_t tx=0,ty=0;bool have=Touch_ReadFrame()&&getTouchXY(&tx,&ty);
    if(have){
      kbRelease=0;
      if(!kbPressed){kbPressed=true;   // act once, on the finger-down edge only
        bool handled=false;int ky=70;
        for(int r=0;r<4&&!handled;r++){int n=strlen(KROWS[r]);int kw=(VW-gap)/10-gap;int kx0=gap+((10-n)*(kw+gap))/2;
          if(ty>=ky&&ty<ky+kh){int i=((int)tx-kx0)/(kw+gap);int within=((int)tx-kx0)-i*(kw+gap);
            if((int)tx>=kx0&&i>=0&&i<n&&within<kw){if(out.length()<24)out+=KROWS[r][i];dirty=true;handled=true;}}
          ky+=kh+gap;}
        if(!handled&&ty>=ky&&ty<ky+kh){int cw=(VW-5*gap)/4;int i=((int)tx-gap)/(cw+gap);int cxi=gap+i*(cw+gap);
          if(i>=0&&i<4&&(int)tx>=cxi&&(int)tx<cxi+cw){
            if(i==0){if(out.length())out.remove(out.length()-1);dirty=true;}
            else if(i==1){if(out.length()<24)out+=' ';dirty=true;}
            else if(i==2){return false;}
            else if(i==3){out.trim();return true;}}
        }
      }
    } else {
      // require several consecutive no-touch frames before accepting the next key (debounces panel jitter)
      if(kbPressed&&++kbRelease>=3)kbPressed=false;
    }
    delay(12);
  }
}

// Full-screen dongle scan + picker. Scans ~4s, lists all dongles found, then USE/RENAME/BACK.
static void doScanDongles(){
  ensureEspNow();
  espnowScanBegin();
  // Scanning screen
  gfx_fillScreen(COL_BG);
  gfx_setTextSize(2);gfx_setTextColor(COL_ORANGE,COL_BG);
  {const char*s="SCANNING";int tw=gfx_textWidth(s);gfx_setCursor((VW-tw)/2,40);gfx_print(s);}
  gfx_setTextSize(1);gfx_setTextColor(COL_DIM,COL_BG);
  {const char*s="Looking for dongles...";int tw=gfx_textWidth(s);gfx_setCursor((VW-tw)/2,70);gfx_print(s);}
  gfx_flush();
  // Broadcast for ~4 seconds, updating count
  uint32_t t0=millis();int lastCount=-1;
  while(millis()-t0<4000){
    espnowBroadcastHello();
    int c=espnowScanCount();
    if(c!=lastCount){lastCount=c;
      gfx_fillRect(0,90,VW,16,COL_BG);gfx_setTextColor(COL_LIT,COL_BG);
      String m="Found: "+String(c);gfx_setCursor((VW-gfx_textWidth(m))/2,92);gfx_print(m);gfx_flush();}
    delay(250);
  }
  espnowScanEnd();
  int n=espnowScanCount();
  if(n==0){
    gfx_fillScreen(COL_BG);
    gfx_setTextSize(1);gfx_setTextColor(COL_ORANGE,COL_BG);gfx_setCursor(8,8);gfx_print("No dongles found");
    gfx_setTextColor(COL_DIM,COL_BG);gfx_setCursor(8,30);gfx_print("Check dongle is powered");
    gfx_setCursor(8,42);gfx_print("and in WIRELESS range.");
    gfx_fillRoundRect(VW/2-50,VH-44,100,32,8,COL_BAR);gfx_drawRoundRect(VW/2-50,VH-44,100,32,8,COL_DIM);
    gfx_setTextColor(COL_LIT,COL_BAR);{const char*s="BACK";gfx_setCursor(VW/2-gfx_textWidth(s)/2,VH-34);}gfx_print("BACK");
    gfx_flush();
    uint32_t w=millis();while(millis()-w<8000){if(Touch_ReadFrame()){uint32_t r=millis();while(Touch_ReadFrame()&&millis()-r<400)delay(10);break;}delay(20);}
    g_info_showing=false;drawFullUI();gfx_flush();return;
  }
  // Results: tap a row to highlight, then USE (pair) / RENAME (keyboard) / BACK. Long lists scroll (drag).
  int rowH=40, listTop=26, btnBarY=VH-40;
  int maxRows=(btnBarY-listTop-4)/rowH; if(maxRows<1)maxRows=1;
  int maxScroll=(n>maxRows)?(n-maxRows):0, scanScroll=0;
  int sel=0; for(int i=0;i<n;i++){if(espnowIsPaired()&&espnowGetXiaoMac()==espnowScanGetMac(i)){sel=i;break;}}
  if(sel>=maxRows)scanScroll=sel-maxRows+1; if(scanScroll>maxScroll)scanScroll=maxScroll; if(scanScroll<0)scanScroll=0;
  bool dirty=true, down=false, moved=false; int downX=0,downY=0,downScroll=0;
  while(true){
    if(dirty){dirty=false;
      gfx_fillScreen(COL_BG);
      gfx_setTextSize(1);gfx_setTextColor(COL_ORANGE,COL_BG);gfx_setCursor(8,7);gfx_print("Dongles ("+String(n)+") - pick one:");
      for(int r=0;r<maxRows&&(scanScroll+r)<n;r++){int i=scanScroll+r,y=listTop+r*rowH;
        String mac=espnowScanGetMac(i),suffix=mac.substring(12),nm=getDongleName(mac);
        bool isSel=(i==sel),isActive=(espnowIsPaired()&&espnowGetXiaoMac()==mac);
        uint16_t bg=isSel?COL_SEL:COL_PANEL;
        gfx_fillRoundRect(8,y,VW-16,rowH-4,6,bg);gfx_drawRoundRect(8,y,VW-16,rowH-4,6,isSel?COL_AMBER:COL_ACCENT);
        gfx_setTextSize(1);gfx_setTextColor(isSel?inkFor(bg):((inkFor(bg)==TFT_BLACK)?TFT_BLACK:COL_AMBER),bg);gfx_setCursor(18,y+7);gfx_print(nm.length()?nm:("Dongle "+String(i+1)));
        gfx_setTextColor(isSel?inkFor(bg):COL_MID,bg);gfx_setCursor(18,y+20);gfx_print("OMEGA-"+suffix);
        if(isActive){gfx_setTextColor(COL_GREEN,bg);gfx_setCursor(VW-70,y+13);gfx_print("ACTIVE");}
      }
      if(n>maxRows){int trackY=listTop,trackH=maxRows*rowH-4,thumbH=trackH*maxRows/n;if(thumbH<10)thumbH=10;
        int thumbY=trackY+(trackH-thumbH)*scanScroll/maxScroll;
        gfx_fillRect(VW-4,trackY,3,trackH,COL_PANEL);gfx_fillRect(VW-4,thumbY,3,thumbH,COL_AMBER);}
      int bw=(VW-4*4)/3,bx=4;const char* BL[3]={"USE","RENAME","BACK"};uint16_t BC[3]={COL_GREEN,COL_ACCENT,COL_BAR};
      for(int i=0;i<3;i++){gfx_fillRoundRect(bx,btnBarY+2,bw,34,6,BC[i]);gfx_setTextColor(inkFor(BC[i]),BC[i]);gfx_setTextSize(1);gfx_setCursor(bx+(bw-gfx_textWidth(BL[i]))/2,btnBarY+14);gfx_print(BL[i]);bx+=bw+4;}
      gfx_flush();
    }
    bool t=Touch_ReadFrame(); uint16_t tx=0,ty=0; if(t)t=getTouchXY(&tx,&ty);
    if(t){
      if(!down){down=true;downX=tx;downY=ty;downScroll=scanScroll;moved=false;}
      else{
        if(maxScroll>0){int ns=downScroll+((int)downY-(int)ty)/rowH; if(ns<0)ns=0; if(ns>maxScroll)ns=maxScroll; if(ns!=scanScroll){scanScroll=ns;dirty=true;}}
        if(abs((int)ty-downY)>8||abs((int)tx-downX)>8)moved=true;
      }
    } else if(down){
      down=false;
      if(!moved){
        if(downY>=btnBarY){int bw=(VW-4*4)/3,i=(downX-4)/(bw+4);
          if(i==0){espnowScanSelect(sel);gfx_fillScreen(COL_BG);gfx_setTextSize(2);gfx_setTextColor(COL_GREEN,COL_BG);{const char*s="PAIRED";gfx_setCursor((VW-gfx_textWidth(s))/2,VH/2-8);}gfx_print("PAIRED");gfx_flush();delay(700);break;}
          else if(i==1){String mac=espnowScanGetMac(sel);String label="OMEGA-"+mac.substring(12),nm=getDongleName(mac),out;if(onScreenKeyboard(label,nm,out)){setDongleName(mac,out);}uint32_t r=millis();while(Touch_ReadFrame()&&millis()-r<500)delay(10);down=false;dirty=true;}
          else break; // BACK
        } else if(downY>=listTop&&downY<listTop+maxRows*rowH){int slot=(downY-listTop)/rowH,idx=scanScroll+slot; if(idx>=0&&idx<n&&idx!=sel){sel=idx;dirty=true;}}
      }
    }
    delay(15);
  }
  g_info_showing=false;drawFullUI();gfx_flush();
}

// Legacy single-pair (kept for compatibility, now routes to scan)
static void doPairNow(){ doScanDongles(); }

// ════════════════════════════════════════════════════════════════════════════
// SETUP
// ════════════════════════════════════════════════════════════════════════════
void setup(){
  Serial.begin(115200);delay(200);
  applyTheme(0);displayInit();touchInit();
  gfx_fillScreen(TFT_BLACK);gfx_flush();
  g_disk=(uint8_t*)ps_malloc(TOTAL_SECTORS*512);if(!g_disk){gfx_setTextColor(TFT_RED,TFT_BLACK);gfx_setCursor(8,160);gfx_print("RAM ALLOC FAILED");gfx_flush();while(1)delay(1000);}
  build_volume(getOutputFilename(),g_mode==MODE_ADF?ADF_DEFAULT_SIZE:64);
  SD_MMC.setPins(SD_CLK,SD_CMD,SD_D0);delay(100);
  bool sdok=SD_MMC.begin("/sdcard",true);if(!sdok){delay(200);sdok=SD_MMC.begin("/sdcard",true);}
  if(sdok){
    if(!SD_MMC.exists("/ADF")){SD_MMC.mkdir("/ADF");ensureSampleFolder();SD_MMC.mkdir("/screensaver");}   // blank card: SAMPLE example + arm the screensaver by default (v4.8.5 — DELETE /screensaver to disable it; empty = the bouncing starburst, drop in JPGs for a gallery)
    if(!SD_MMC.exists("/DSK"))SD_MMC.mkdir("/DSK");
    generateDefaultConfig();
    selfHealConfig();           // append any documented keys an older CONFIG.TXT is missing
    loadConfig();
    espnowSetScanCap(g_dongle_cap);
    relayout();                 // apply ROTATE/COMPACT from config before first draw
    listImages(SD_MMC,g_files);
    if(!readGameCache()){buildGameList();buildThumbs();}   // fresh card: build reel thumbs up-front
    applyStats();
    buildActiveLetters();
    if(!g_games.empty())setActiveLetter(bucketOf(g_games[0].name));
    scanScreensaver();
  } else {gfx_setTextColor(TFT_RED,TFT_BLACK);gfx_setCursor(8,200);gfx_print("SD MOUNT FAILED");gfx_flush();delay(2000);relayout();}   // no card: still init layout so INFO/LOAD DIAG work
  if(g_wireless_mode){espnowBegin();g_espnow_started=true;}
  drawCracktro(g_cracktro);
  USB.onEvent(usbEventCB);MSC.vendorID("ESP32");MSC.productID("RAMDISK");MSC.productRevision("1.0");
  MSC.onRead(onRead);MSC.onWrite(onWrite);MSC.mediaPresent(true);
  MSC.begin(TOTAL_SECTORS,512);USB.begin();hardDetach();
  bool bootCar=(g_car_bootmode==1)||(g_car_bootmode==2&&readLastView()==1);   // v4.8.6: CAROUSEL= 0=list / 1=reel / LAST=restore
  if(bootCar&&!g_games.empty())carEnter();else{drawFullUI();gfx_flush();}
  g_last_touch_ms=millis();
}

// ════════════════════════════════════════════════════════════════════════════
// MAIN LOOP
// ════════════════════════════════════════════════════════════════════════════
// Redraw only the list strip + A-Z index (cover doesn't change while scrolling)
static void redrawListArea(){drawFileList();drawNowPlayingBar();drawAZBar();gfx_flush();}
// True if the selected game's name is too long for its lane (needs a marquee)
static bool selNameOverflows(){
  if(g_sel<0||g_sel>=(int)g_games.size())return false;
  auto&g=g_games[g_sel];int r=8+g_name_sz*3,nx=LIST_X+6+r+r+6;int maxNW=LIST_W-(nx-LIST_X)-8-(g.disk_count>1?36:0);
  gfx_setTextSize(g_name_sz);return gfx_textWidth(g.name)>maxNW;
}

// Dispatch a completed tap (finger down + up with no drag) to the right UI region
// ════════════════════════════════════════════════════════════════════════════
// USER DISKS (v4.9.7) — create + manage pre-formatted OFS "save" disks in /USER-DISKS.
// Template built + verified with amitools xdftool (empty OFS DD volume). Only 3 blocks
// are non-zero (boot/root/bitmap); the rest is written as zeros at create time. The
// volume name (USERnn) is patched into the root block with a fresh AmigaDOS checksum.
// ════════════════════════════════════════════════════════════════════════════
static const uint8_t UD_BOOT[12]={68,79,83,0,0,0,0,0,0,0,3,112};   // "DOS\0", chksum, rootblock ptr=880
static const uint8_t UD_ROOT[512]={0,0,0,2,0,0,0,0,0,0,0,0,0,0,0,72,0,0,0,0,114,92,102,229,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,255,0,0,3,113,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,69,78,0,0,5,62,0,0,11,34,4,83,65,86,69,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,69,78,0,0,5,62,0,0,11,34,0,0,69,78,0,0,5,62,0,0,11,34,68,79,83,0,0,0,0,0,0,0,0,0,0,0,0,1};
static const uint8_t UD_BITMAP[512]={0,0,192,127,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,63,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255};
static void scanUserDisks(std::vector<String>&out){
  out.clear(); File d=SD_MMC.open("/USER-DISKS"); if(!d||!d.isDirectory())return;
  File f; while((f=d.openNextFile())){ if(!f.isDirectory()){ String nm=f.name();int s=nm.lastIndexOf('/');if(s>=0)nm=nm.substring(s+1);
    String low=nm;low.toLowerCase(); if(low.endsWith(".adf")&&!low.endsWith(".sav.adf")) out.push_back(String("/USER-DISKS/")+nm); } f.close(); }
  d.close();
}
static bool createUserDisk(){
  SD_MMC.mkdir("/USER-DISKS");
  int n=1; char path[48];
  for(;n<1000;n++){ snprintf(path,sizeof(path),"/USER-DISKS/USER%02d.adf",n); if(!SD_MMC.exists(path))break; }
  uint8_t root[512]; memcpy(root,UD_ROOT,512);                       // patch volume name USERnn + checksum
  char vol[12]; int vl=snprintf(vol,sizeof(vol),"USER%02d",n); if(vl>30)vl=30;
  root[432]=(uint8_t)vl; memset(root+433,0,30); memcpy(root+433,vol,vl);
  root[20]=root[21]=root[22]=root[23]=0;
  uint32_t sum=0; for(int i=0;i<128;i++)sum+=((uint32_t)root[i*4]<<24)|((uint32_t)root[i*4+1]<<16)|((uint32_t)root[i*4+2]<<8)|root[i*4+3];
  uint32_t ck=(uint32_t)(0-sum); root[20]=ck>>24;root[21]=ck>>16;root[22]=ck>>8;root[23]=(uint8_t)ck;
  File f=SD_MMC.open(path,FILE_WRITE); if(!f)return false;
  static const uint8_t zb[512]={0}; uint8_t b0[512]; memset(b0,0,512); memcpy(b0,UD_BOOT,12);
  for(int b=0;b<1760;b++){ if(b==0)f.write(b0,512); else if(b==880)f.write(root,512); else if(b==881)f.write(UD_BITMAP,512); else f.write(zb,512); }
  f.close(); return true;
}
// Blocking full-screen manager. Returns a path to LOAD (empty = just closed).
static String doUserDisks(){
  std::vector<String> disks; scanUserDisks(disks);
  const int rowH=30, listTop=STATUS_H+MODE_BAR_H+34;
  bool dirty=true, pressed=false; int rel=0, scroll=0;
  {uint32_t t0=millis();while(Touch_ReadFrame()&&millis()-t0<500)delay(10);}
  while(true){
    if(dirty){dirty=false;
      gfx_fillScreen(COL_BG); drawStatusBar();
      gfx_fillRect(0,STATUS_H,VW,MODE_BAR_H,COL_BAR); gfx_setTextSize(1);
      gfx_fillRoundRect(4,STATUS_H+2,36,14,7,COL_BG);gfx_setTextColor(COL_DIM,COL_BG);gfx_setCursor(10,STATUS_H+6);gfx_print("ADF");
      gfx_fillRoundRect(44,STATUS_H+2,36,14,7,COL_BG);gfx_setTextColor(COL_DIM,COL_BG);gfx_setCursor(50,STATUS_H+6);gfx_print("DSK");
      gfx_fillRoundRect(84,STATUS_H+2,66,14,7,COL_AMBER);gfx_setTextColor(TFT_BLACK,COL_AMBER);gfx_setCursor(92,STATUS_H+6);gfx_print("USR-DSK");
      gfx_setTextSize(2);gfx_setTextColor(COL_LIT,COL_BG);gfx_setCursor(8,STATUS_H+MODE_BAR_H+8);gfx_print("USER DISKS");
      gfx_setTextSize(1);gfx_setTextColor(COL_DIM,COL_BG);gfx_setCursor(8,STATUS_H+MODE_BAR_H+26);gfx_print("pre-formatted save disks - tap to insert");
      int cy=listTop; gfx_fillRoundRect(8,cy,VW-16,rowH-4,6,COL_GREEN);gfx_setTextColor(TFT_BLACK,COL_GREEN);gfx_setCursor(16,cy+(rowH-12)/2);gfx_print("+  CREATE NEW DISK");
      int y=listTop+rowH;
      for(int i=scroll;i<(int)disks.size()&&y<VH-6;i++){
        String nm=disks[i];int s=nm.lastIndexOf('/');if(s>=0)nm=nm.substring(s+1);int dot=nm.lastIndexOf('.');if(dot>0)nm=nm.substring(0,dot);
        gfx_fillRoundRect(8,y,VW-16,rowH-4,6,COL_PANEL);gfx_setTextColor(COL_LIT,COL_PANEL);
        while(gfx_textWidth(nm)>VW-88&&nm.length()>1)nm=nm.substring(0,nm.length()-1);
        gfx_setCursor(16,y+(rowH-12)/2);gfx_print(nm);
        gfx_fillRoundRect(VW-68,y+3,60,rowH-10,5,COL_BAR);gfx_setTextColor(COL_AMBER,COL_BAR);gfx_setCursor(VW-60,y+(rowH-12)/2);gfx_print("RENAME");
        y+=rowH;
      }
      if(disks.empty()){gfx_setTextColor(COL_DIM,COL_BG);gfx_setCursor(16,listTop+rowH+8);gfx_print("(none yet - tap CREATE NEW DISK)");}
      gfx_flush();
    }
    uint16_t tx=0,ty=0; bool have=Touch_ReadFrame()&&getTouchXY(&tx,&ty);
    if(have){ rel=0;
      if(!pressed){ pressed=true;
        if(ty>=STATUS_H&&ty<STATUS_H+MODE_BAR_H){ return String(""); }         // tap the bar (USR-DSK) closes
        if(ty>=listTop&&ty<listTop+rowH-4){ createUserDisk(); scanUserDisks(disks); dirty=true; }
        else{ int y=listTop+rowH;
          for(int i=scroll;i<(int)disks.size()&&y<VH-6;i++){
            if(ty>=y&&ty<y+rowH-4){
              if(tx>=(uint16_t)(VW-68)){                                       // RENAME chip
                String nm=disks[i];int s=nm.lastIndexOf('/');if(s>=0)nm=nm.substring(s+1);int dot=nm.lastIndexOf('.');String base=(dot>0)?nm.substring(0,dot):nm;
                String out; if(onScreenKeyboard("RENAME DISK",base,out)){ out.trim(); if(out.length()){ String np="/USER-DISKS/"+out+".adf"; if(!SD_MMC.exists(np.c_str()))SD_MMC.rename(disks[i].c_str(),np.c_str()); } scanUserDisks(disks); }
                {uint32_t r=millis();while(Touch_ReadFrame()&&millis()-r<400)delay(10);} dirty=true;
              } else return disks[i];                                          // tap row = load this disk
              break;
            }
            y+=rowH;
          }
        }
      }
    } else { if(pressed&&++rel>=3)pressed=false; }
    delay(12);
  }
}

static void handleTap(uint16_t px,uint16_t py){
  // ── INFO / SETTINGS panel touches (only within the panel; taps below it fall through to the list) ──
  if(g_info_showing&&px<(uint16_t)(g_info_x+g_info_w)&&py<(uint16_t)g_info_bottom){
    if(g_info_mode_btn_y&&py>=(uint16_t)g_info_mode_btn_y&&py<(uint16_t)(g_info_mode_btn_y+g_info_bh)){g_wireless_mode=!g_wireless_mode;saveConfigKey("MODE",g_wireless_mode?"WIRELESS":"STANDALONE");if(g_wireless_mode)ensureEspNow();drawFullUI();drawInfoPanel();gfx_flush();return;}
    // FONT — cycle SMALL/NORMAL/LARGE, keep INFO open, list re-renders
    if(g_info_font_btn_y&&py>=(uint16_t)g_info_font_btn_y&&py<(uint16_t)(g_info_font_btn_y+g_info_bh)){
      applyFont((g_font+1)%3);saveConfigKey("FONT",fontName(g_font));drawFullUI();drawInfoPanel();gfx_flush();return;}
    // ROTATE — +90 each tap; reflow layout, keep INFO open
    if(g_info_rot_btn_y&&py>=(uint16_t)g_info_rot_btn_y&&py<(uint16_t)(g_info_rot_btn_y+g_info_bh)){
      g_rot=(g_rot+1)&3;relayout();saveConfigKey("ROTATE",String(g_rot*90));
      {float mp=(float)maxScrollPx();if(g_scrollPx>mp)g_scrollPx=mp;} drawFullUI();drawInfoPanel();gfx_flush();return;}
    // COMPACT — toggle cover vs list-maximised, keep INFO open
    if(g_info_comp_btn_y&&py>=(uint16_t)g_info_comp_btn_y&&py<(uint16_t)(g_info_comp_btn_y+g_info_bh)){
      g_compact=!g_compact;relayout();saveConfigKey("COMPACT",g_compact?"ON":"OFF");
      {float mp=(float)maxScrollPx();if(g_scrollPx>mp)g_scrollPx=mp;} drawFullUI();drawInfoPanel();gfx_flush();return;}
    if(g_wireless_mode&&g_info_pair_now_btn_y&&py>=(uint16_t)g_info_pair_now_btn_y&&py<(uint16_t)(g_info_pair_now_btn_y+g_info_bh)){doPairNow();return;}
    // v4.8.1: HIVEMIND toggle (row only exists when a MuCa group is configured)
    if(g_wireless_mode&&g_info_hive_btn_y&&py>=(uint16_t)g_info_hive_btn_y&&py<(uint16_t)(g_info_hive_btn_y+g_info_bh)){
      g_hivemind=!g_hivemind;saveConfigKey("HIVEMIND",g_hivemind?"ON":"OFF");drawFullUI();drawInfoPanel();gfx_flush();return;}
    if(g_info_rescan_btn_y&&py>=(uint16_t)g_info_rescan_btn_y&&py<(uint16_t)(g_info_rescan_btn_y+g_info_bh)){doRescan();return;}
    if(g_info_reset_btn_y&&py>=(uint16_t)g_info_reset_btn_y&&py<(uint16_t)(g_info_reset_btn_y+g_info_bh)){
      int pw=g_info_w-8,gap=4,hw=(pw-gap)/2,dx=g_info_x+4+hw+gap;
      if(px>=(uint16_t)dx){ if(g_loaded&&g_loaded_name=="AMIGA TEST KIT"){g_info_showing=false;doUnload();drawFullUI();gfx_flush();} else doLoadDiag(); return; }   // right half = LOAD / EJECT DIAG
      gfx_fillRoundRect(g_info_x+4,g_info_reset_btn_y,hw,g_info_bh,6,0xE8C4);gfx_setTextSize(1);gfx_setTextColor(TFT_BLACK,0xE8C4);gfx_setCursor(g_info_x+10,g_info_reset_btn_y+(g_info_bh-8)/2);gfx_print("RESET...");gfx_flush();delay(700);ESP.restart();}
    return;
  }

  // ── v4.9.2: book button on the cover — opens the .rtfm manual full-screen ──
  if(!g_info_showing&&g_manual_bw&&g_manual_path.length()&&px>=(uint16_t)g_manual_bx&&px<(uint16_t)(g_manual_bx+g_manual_bw)&&py>=(uint16_t)g_manual_by&&py<(uint16_t)(g_manual_by+g_manual_bh)){
    doManual(g_manual_path); drawFullUI(); gfx_flush(); return; }

  // ── A-Z bar (letters + toggle button) — suppressed where the INFO panel covers it ──
  if(px>=AZ_X&&py>=AZ_TOP&&py<(uint16_t)(AZ_TOP+AZ_H)&&!(g_info_showing&&px<(uint16_t)(g_info_x+g_info_w)&&py<(uint16_t)g_info_bottom)){
    if(py<(uint16_t)(AZ_TOP+AZ_SRCH_H)&&active_letter_count){doSearch();drawFullUI();gfx_flush();return;}   // v4.8.2: magnifier
    if(handleAlphabetTouch(px,py)){drawListAndCover();gfx_flush();}return;}

  // ── INSERT/EJECT (cover button or compact action strip) ──
  {if(!g_games.empty()&&px>=(uint16_t)INS_X&&px<(uint16_t)(INS_X+INS_W)&&py>=(uint16_t)INS_Y&&py<(uint16_t)(INS_Y+INS_H)){
    auto&gm=g_games[g_sel];int idx=gm.disk_indices.empty()?gm.first_file_idx:gm.disk_indices[min(g_disk_sel,(int)gm.disk_indices.size()-1)];
    if(g_loaded&&g_loaded_game_idx==g_sel){
      if(g_loaded_disk_idx==g_disk_sel)doUnload();          // pressing on exactly what's mounted = eject
      else doLoadSelected(g_files[idx]);                     // a different disk is selected = clean-swap to it
    } else doLoadSelected(g_files[idx]);                     // load the selected game/disk
    return;}}

  // ── Disk selection (landscape grid / portrait stepper / compact thumbnail) ──
  if(!g_games.empty()){auto&game=g_games[g_sel];if(game.disk_count>1){
    if(COVER_ON&&!g_portrait){
      DiskGrid L=diskGrid(game.disk_count);
      if(L.multiPage){int pby=L.gridY+L.gridH+L.pageGap;if(py>=(uint16_t)pby&&py<(uint16_t)(pby+L.pageBtnH)&&px>=(uint16_t)L.gx&&px<(uint16_t)(L.gx+L.gridW)){g_disk_page=(g_disk_page+1)%L.pages;drawCoverPanel();gfx_flush();return;}}
      for(int d=L.pageStart;d<L.pageEnd;d++){int slot=d-L.pageStart,col=slot%L.COLS,row=slot/L.COLS;int bx=L.gx+col*(L.dbw+L.dgap),by=L.gridY+row*(L.dbh+L.dgap);
        if(px>=(uint16_t)bx&&px<(uint16_t)(bx+L.dbw)&&py>=(uint16_t)by&&py<(uint16_t)(by+L.dbh)){g_disk_sel=d;if(g_hotswap&&g_loaded&&g_loaded_game_idx==g_sel)doLoadSelected(g_files[game.disk_indices[g_disk_sel]]);else{drawCoverPanel();gfx_flush();}return;}}
    } else if(COVER_ON&&g_portrait&&g_step_on){
      if(px>=(uint16_t)g_step_x&&px<(uint16_t)(g_step_x+g_step_w)&&py>=(uint16_t)(g_step_y-6)&&py<(uint16_t)(g_step_y+g_step_h)){
        if(px<(uint16_t)(g_step_x+g_step_w/2))g_disk_sel=(g_disk_sel-1+game.disk_count)%game.disk_count;   // left half = prev
        else g_disk_sel=(g_disk_sel+1)%game.disk_count;                                                    // right half = next
        if(g_hotswap&&g_loaded&&g_loaded_game_idx==g_sel)doLoadSelected(g_files[game.disk_indices[g_disk_sel]]);else{drawCoverPanel();gfx_flush();}return;}
    } else if(STRIP_ON){
      if(px<(uint16_t)INS_X&&py>=(uint16_t)STRIP_Y&&py<(uint16_t)(STRIP_Y+STRIP_H)){if(px<(uint16_t)(INS_X/2))g_disk_sel=(g_disk_sel-1+game.disk_count)%game.disk_count;else g_disk_sel=(g_disk_sel+1)%game.disk_count;if(g_hotswap&&g_loaded&&g_loaded_game_idx==g_sel)doLoadSelected(g_files[game.disk_indices[g_disk_sel]]);else{drawActionStrip();gfx_flush();}return;}
    }
  }}

  // ── Mode bar ──
  if(py>=STATUS_H&&py<STATUS_H+MODE_BAR_H&&px>=LIST_X){
    if(px<LIST_X+40&&g_mode!=MODE_ADF){g_mode=MODE_ADF;g_files.clear();listImages(SD_MMC,g_files);if(!readGameCache()){buildGameList();buildThumbs();}buildActiveLetters();g_sel=g_scroll=0;g_scrollPx=0;g_az_page=0;if(!g_games.empty())setActiveLetter(bucketOf(g_games[0].name));drawFullUI();gfx_flush();return;}
    if(px>=LIST_X+44&&px<LIST_X+80&&g_mode!=MODE_DSK){g_mode=MODE_DSK;g_files.clear();listImages(SD_MMC,g_files);if(!readGameCache()){buildGameList();buildThumbs();}buildActiveLetters();g_sel=g_scroll=0;g_scrollPx=0;g_az_page=0;if(!g_games.empty())setActiveLetter(bucketOf(g_games[0].name));drawFullUI();gfx_flush();return;}
    if(px>=LIST_X+84&&px<LIST_X+150){String p=doUserDisks(); if(p.length()){ if(doLoadSelected(p)){g_loaded_game_idx=-1;String nm=p;int s=nm.lastIndexOf('/');if(s>=0)nm=nm.substring(s+1);int d=nm.lastIndexOf('.');if(d>0)nm=nm.substring(0,d);g_loaded_name=nm;} } drawFullUI();gfx_flush();return;}}   // v4.9.7 USR-DSK

  // ── File list ──
  if(px>=LIST_X&&px<AZ_X&&py>=LIST_TOP&&py<LIST_BOTTOM){
    bool wasInfo=g_info_showing; g_info_showing=false;
    int gi=(int)((g_scrollPx+(py-LIST_TOP))/LIST_ITEM_H);if(gi>=0&&gi<(int)g_games.size()){
      {int r=8+g_name_sz*3;if(px<=(uint16_t)(LIST_X+6+2*r+3)){g_games[gi].fav=!g_games[gi].fav;saveStats();if(wasInfo)drawFullUI();else drawFileList();gfx_flush();return;}}
      if(gi==g_sel){
        // Default: tapping the already-selected row does nothing (load only via INSERT).
        // TAPLOAD=ON restores the old tap-again-to-load/eject behaviour.
        if(g_tapload){if(g_loaded&&g_loaded_game_idx==g_sel)doUnload();else{auto&gm=g_games[g_sel];g_disk_sel=0;g_disk_page=0;doLoadSelected(g_files[gm.disk_indices.empty()?gm.first_file_idx:gm.disk_indices[0]]);}}
        else if(wasInfo){drawFullUI();gfx_flush();}
      }
      else{g_sel=gi;setActiveLetter(bucketOf(g_games[gi].name));g_disk_sel=0;g_disk_page=0;if(wasInfo)drawFullUI();else drawListAndCover();gfx_flush();}}
    else if(wasInfo){drawFullUI();gfx_flush();}
    return;}

  // ── Bottom bar ── (slot count follows drawBottomBar: 5 with carousel, else 4)
  if(py>=VH-BOTTOM_H){
    const int nb=5;int bw=VW/nb,btn=px/bw;if(btn>=nb)btn=nb-1;
    if(btn==0&&g_sel>0){g_sel--;g_disk_sel=0;g_disk_page=0;setActiveLetter(bucketOf(g_games[g_sel].name));if((float)(g_sel*LIST_ITEM_H)<g_scrollPx)g_scrollPx=g_sel*LIST_ITEM_H;drawListAndCover();gfx_flush();}
    else if(btn==1&&g_sel<(int)g_games.size()-1){g_sel++;g_disk_sel=0;g_disk_page=0;setActiveLetter(bucketOf(g_games[g_sel].name));if((float)((g_sel+1)*LIST_ITEM_H)>g_scrollPx+(LIST_BOTTOM-LIST_TOP))g_scrollPx=(g_sel+1)*LIST_ITEM_H-(LIST_BOTTOM-LIST_TOP);drawListAndCover();gfx_flush();}
    else if(btn==2){cycleTheme();}
    else if(btn==3){ g_info_showing=false; carEnter(); }   // REEL — enter the carousel
    else if(btn==4){ g_info_showing=!g_info_showing; if(g_info_showing)drawInfoPanel(); else drawFullUI(); gfx_flush(); }
    return;
  }
}

// ════════════════════════════════════════════════════════════════════════════
// MAIN LOOP — touch state machine: tap vs drag-scroll with flick inertia
// ════════════════════════════════════════════════════════════════════════════
void loop(){
  if(g_espnow_link_just_established){g_espnow_link_just_established=false;
    gfx_fillRect(0,0,VW,STATUS_H,0x07E0);gfx_setTextSize(1);gfx_setTextColor(TFT_BLACK,0x07E0);
    gfx_setCursor(VW/2-57,6);gfx_print("** DONGLE LINKED **");gfx_flush();delay(2000);drawStatusBar();gfx_flush();}

  static uint32_t last=0;if(millis()-last<16){delay(1);return;}last=millis();
  bool frame=Touch_ReadFrame();uint16_t px=0,py=0;bool touch=frame&&getTouchXY(&px,&py);
  uint32_t now=millis();

  // ── Save-game housekeeping (v4.8.0) — runs in list AND carousel mode ──
  if(!touch){
    // Wireless: the dongle beaconed settled unsaved sectors — fetch once the finger is off the glass
    static uint32_t svNextTry=0;
    if(g_espnow_dirty&&g_wireless_mode&&g_espnow_started&&now>=svNextTry&&now-g_last_touch_ms>1200){
      svFetchWireless();
      if(g_espnow_dirty)svNextTry=now+30000;   // fetch failed — back off; the dongle keeps beaconing
    }
    // Own-disk writes settled — flush to SD (v4.8.1: in ANY mode; a wireless GTi
    // can still be USB-attached to a PC or a local Gotek)
    if(g_loaded&&g_sv_dirty_count&&g_sv_last_write&&now-g_sv_last_write>SV_SETTLE_MS)
      svFlushStandalone();
  }

  // ── Carousel mode: dedicated tap/drag/coast machine, then bail ──
  if(g_car_active){
    if(touch)g_last_touch_ms=now;
    carTick(touch,px,py,now);
    return;
  }

  if(touch){
    g_last_touch_ms=now;
    g_touch_release=0;                                  // any touch resets the release counter (ignores blips)
    if(!g_touch_active){
      // finger down
      g_touch_active=true;g_touch_x0=px;g_touch_y0=py;g_touch_px0=g_scrollPx;
      g_touch_lastY=py;g_touch_lastMs=now;g_touch_moved=false;g_touch_vel=0;g_inertia_on=false;
      g_touch_inlist=(px>=LIST_X&&px<AZ_X&&py>=LIST_TOP&&py<LIST_BOTTOM&&!g_info_showing&&!g_games.empty());
    } else {
      // finger held / moving
      if(abs((int)px-g_touch_x0)>DRAG_THRESH||abs((int)py-g_touch_y0)>DRAG_THRESH)g_touch_moved=true;
      if(g_touch_inlist&&g_touch_moved){
        g_scrollPx=g_touch_px0-(float)((int)py-g_touch_y0);
        if(g_scrollPx<0)g_scrollPx=0;int mp=maxScrollPx();if(g_scrollPx>mp)g_scrollPx=mp;
        uint32_t dt=now-g_touch_lastMs;if(dt>0){g_touch_vel=(float)((int)py-g_touch_lastY)/(float)dt;g_touch_lastY=py;g_touch_lastMs=now;}
        syncIndexToScroll();redrawListArea();
      }
    }
    return;
  }

  // no touch this frame — only treat as a real lift after several consecutive no-touch frames (panel blips)
  if(g_touch_active){
    if(++g_touch_release<RELEASE_FRAMES) return;        // still-pressed as far as we're concerned
    g_touch_active=false;g_touch_release=0;
    if(g_touch_moved&&g_touch_inlist){ g_inertia_vel=-g_touch_vel*16.0f; g_inertia_on=fabsf(g_inertia_vel)>0.5f; }  // list drag -> coast
    else { handleTap((uint16_t)g_touch_x0,(uint16_t)g_touch_y0); }  // anything else (incl. a firm/jittery button press) -> tap
    return;
  }

  // screensaver (undocumented, folder-gated): fires on idle; any touch wakes it
  if(g_ss_enabled&&g_ss_have&&!g_info_showing&&!g_inertia_on&&!g_car_active){
    uint32_t thr=g_loaded?g_ss_load_ms:g_ss_idle_ms;
    if(now-g_last_touch_ms>=thr)runScreensaver();
  }
  // idle: run inertia
  if(g_inertia_on){
    g_scrollPx+=g_inertia_vel;g_inertia_vel*=0.92f;
    if(g_scrollPx<0){g_scrollPx=0;g_inertia_on=false;}
    int mp=maxScrollPx();if(g_scrollPx>mp){g_scrollPx=mp;g_inertia_on=false;}
    if(fabsf(g_inertia_vel)<0.3f)g_inertia_on=false;
    syncIndexToScroll();redrawListArea();
  }
  // idle: bounce the selected over-long name (scroll to the end, pause ~1s, reverse)
  else if(!g_info_showing&&selNameOverflows()){
    if(g_marquee_sel!=g_sel){g_marquee_sel=g_sel;g_marquee_off=0;g_marquee_dir=1;g_marquee_pause=now;}   // pause at start on new selection
    auto&g=g_games[g_sel];int r=8+g_name_sz*3,nx=LIST_X+6+r+r+6;int maxNW=LIST_W-(nx-LIST_X)-8-(g.disk_count>1?36:0);
    gfx_setTextSize(g_name_sz);int mx=gfx_textWidth(g.name)-maxNW+8;if(mx<0)mx=0;
    static uint32_t lastMq=0;
    if(g_marquee_pause){ if(now-g_marquee_pause>=1000){g_marquee_pause=0;g_marquee_dir=(g_marquee_off<=0)?1:-1;lastMq=now;} }
    else if(now-lastMq>=45){ lastMq=now;g_marquee_off+=g_marquee_dir*5;
      if(g_marquee_off>=mx){g_marquee_off=mx;g_marquee_pause=now;}
      else if(g_marquee_off<=0){g_marquee_off=0;g_marquee_pause=now;}
      redrawListArea(); }
  }
}
