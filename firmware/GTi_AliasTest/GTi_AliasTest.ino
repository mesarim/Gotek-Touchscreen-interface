// ════════════════════════════════════════════════════════════════════════════
// GTi ALIAS DISK — TEST SKETCH
// ════════════════════════════════════════════════════════════════════════════
// Board: Guition JC3248W535C (ESP32-S3 N16R8)
// IDE:   ESP32S3 Dev Module | USB-OTG (TinyUSB) | USB CDC On Boot: DISABLED
//        Flash 16MB QIO 80MHz | PSRAM: OPI 8MB | 240MHz
//        (no sketch-local partitions.csv needed - any dual-OTA scheme is fine)
//
// WHAT THIS PROVES
//   A disk image that lives on the SD card is presented over USB as an ordinary
//   FAT12 volume WITHOUT BEING COPIED ANYWHERE. We build ~6.5 KB of FAT12
//   metadata in RAM; the volume's data area is an ALIAS for the file's real
//   sectors on the card. Not a symlink - FAT has no such concept and no host
//   would follow one. A block-level remap, like a Linux loop device.
//
//   onRead(lba):  lba <  DATA_LBA  -> serve from the RAM metadata
//                 lba >= DATA_LBA  -> translate to a card sector, SD_MMC.readRAW()
//
// WHY THE PC AND NOT A GOTEK
//   A PC can checksum the file and tell us byte-for-byte whether the mapping is
//   right. A Gotek can only say "didn't boot". Get the bytes perfect first.
//
// DELIBERATELY ABSENT: covers, NFO, caches, reel, wireless, saves, OTA.
//   If this breaks, it is the alias layer.
//
// LIMITS OF THIS BUILD: FAT32 cards only (exFAT is detected and reported, not
//   parsed). Read-only. Images up to ~32 MB (BPB_TotSec16 is 16 bits).
// ════════════════════════════════════════════════════════════════════════════

#include <Arduino.h>
#include "USB.h"
#include "USBMSC.h"
#include <FS.h>
#include <SD_MMC.h>
#include <Wire.h>
#include "sha256_inline.h"
#include <vector>
#include <algorithm>
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_axs15231b.h"

extern "C" { bool tud_mounted(void); void tud_disconnect(void); void tud_connect(void); void* ps_malloc(size_t size); }

#define FW_VERSION "AliasTest-0.1"

// ── hardware (same pins as the shipping firmware) ───────────────────────────
#define LCD_WIDTH  320
#define LCD_HEIGHT 480
static int gW=480, gH=320;      // virtual canvas: landscape
static int g_rot=0;
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

#define TFT_BLACK 0x0000
#define TFT_WHITE 0xFFFF
#define TFT_RED   0xF800
static const uint16_t COL_BG=0x0861,COL_PANEL=0x18E3,COL_BAR=0x2945,COL_SEL=0x0340,
                      COL_SEP=0x4208,COL_DIM=0x8410,COL_MID=0xAD55,COL_LIT=0xFFFF,
                      COL_ACCENT=0x07FF,COL_GREEN=0x07E0,COL_AMBER=0xFD20;
static inline uint16_t inkFor(uint16_t bg){int r=(bg>>11)&0x1F,g=(bg>>5)&0x3F,b=bg&0x1F;int lum=(r*77)/31+(g*151)/63+(b*28)/31;return lum>150?TFT_BLACK:TFT_WHITE;}

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
// DISPLAY + FRAMEBUFFER (lifted verbatim from the shipping firmware)
// ════════════════════════════════════════════════════════════════════════════
static esp_lcd_panel_io_handle_t io_handle = NULL;
static esp_lcd_panel_handle_t panel_handle = NULL;
static uint16_t *framebuffer = NULL;
static uint16_t *dma_buffer = NULL;

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

#define COL_STAR 0xFEE0
static void gfx_fillRoundRect(int x,int y,int w,int h,int r,uint16_t c){gfx_fillRect(x+r,y,w-2*r,h,c);gfx_fillRect(x,y+r,r,h-2*r,c);gfx_fillRect(x+w-r,y+r,r,h-2*r,c);for(int dy=-r;dy<=0;dy++){int dx=(int)sqrtf(r*r-dy*dy);gfx_fillRect(x+r-dx,y+r+dy,dx,1,c);gfx_fillRect(x+w-r,y+r+dy,dx,1,c);gfx_fillRect(x+r-dx,y+h-r-1-dy,dx,1,c);gfx_fillRect(x+w-r,y+h-r-1-dy,dx,1,c);}}

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

// ── BOOT DIAGNOSTICS VIA THE BACKLIGHT ──────────────────────────────────────
// This board's single USB-C is the MSC device, so there is no serial monitor.
// The backlight is the only output that works before the panel is up, so it is
// the bring-up channel: N flashes = reached stage N. A failure sticks on that
// stage's count forever, so a dark screen still tells you exactly how far it got.
//
//   1 = setup() entered            4 = panel handle created
//   2 = PSRAM framebuffer OK       5 = panel reset+init done
//   3 = SPI bus + panel IO OK      6 = SD card mounted
//   ...then the UI draws. Steady backlight with no picture = panel talked, we
//   drew, and the pixels went nowhere - a different problem to a dead boot.
static void blCode(int n){
  ledcWrite(LCD_PIN_BL,0); delay(400);
  for(int i=0;i<n;i++){ ledcWrite(LCD_PIN_BL,255); delay(180); ledcWrite(LCD_PIN_BL,0); delay(180); }
  ledcWrite(LCD_PIN_BL,200);
}
static void blFail(int n){ for(;;){ blCode(n); delay(900); } }

static void displayInit(){
  framebuffer=(uint16_t*)ps_malloc(LCD_WIDTH*LCD_HEIGHT*2);
  if(!framebuffer)blFail(2);                                  // PSRAM not enabled / not present
  dma_buffer=(uint16_t*)heap_caps_malloc(LCD_WIDTH*ROWS_PER_STRIP*2,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);
  if(!dma_buffer)blFail(2);
  spi_bus_config_t buscfg={};
  buscfg.data0_io_num=LCD_PIN_MOSI;buscfg.data1_io_num=LCD_PIN_MISO;
  buscfg.sclk_io_num=LCD_PIN_CLK;buscfg.data2_io_num=LCD_PIN_D2;buscfg.data3_io_num=LCD_PIN_D3;
  buscfg.max_transfer_sz=LCD_WIDTH*LCD_HEIGHT*2;
  if(spi_bus_initialize(SPI2_HOST,&buscfg,SPI_DMA_CH_AUTO)!=ESP_OK)blFail(3);
  esp_lcd_panel_io_spi_config_t io_config={};
  io_config.cs_gpio_num=LCD_PIN_CS;io_config.dc_gpio_num=-1;io_config.spi_mode=3;
  io_config.pclk_hz=50000000;io_config.trans_queue_depth=1;
  io_config.lcd_cmd_bits=32;io_config.lcd_param_bits=8;io_config.flags.quad_mode=true;
  if(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,&io_config,&io_handle)!=ESP_OK)blFail(3);
  axs15231b_vendor_config_t vc={};vc.init_cmds=lcd_init_cmds;
  vc.init_cmds_size=sizeof(lcd_init_cmds)/sizeof(lcd_init_cmds[0]);vc.flags.use_qspi_interface=1;
  esp_lcd_panel_dev_config_t pc={};pc.reset_gpio_num=-1;
  pc.rgb_ele_order=LCD_RGB_ELEMENT_ORDER_RGB;pc.bits_per_pixel=16;pc.vendor_config=&vc;
  if(esp_lcd_new_panel_axs15231b(io_handle,&pc,&panel_handle)!=ESP_OK)blFail(4);
  esp_lcd_panel_reset(panel_handle);delay(100);
  esp_lcd_panel_init(panel_handle);delay(200);
  ledcWrite(LCD_PIN_BL,200);
}

// ── Touch ──
static uint8_t gTouchPts=0;static uint16_t gTouchX=0,gTouchY=0;
static void touchInit(){Wire.begin(TOUCH_SDA,TOUCH_SCL,400000);}
static bool Touch_ReadFrame(){
  uint8_t cmd[11]={0xb5,0xab,0xa5,0x5a,0x00,0x00,0x00,0x08,0x00,0x00,0x00};
  Wire.beginTransmission(TOUCH_ADDR);Wire.write(cmd,11);
  if(Wire.endTransmission()!=0){gTouchPts=0;return false;}
  if(Wire.requestFrom((int)TOUCH_ADDR,8)!=8){gTouchPts=0;return false;}
  uint8_t buf[8];for(int i=0;i<8;i++)buf[i]=Wire.read();
  // AXS15231B idle/no-touch frames come back as 0xCA-fill or 0xFF-fill (b[0]!=0).
  // The vendor driver requires b[0]==0 && b[1]!=0; we only checked b[1], so the idle
  // fill (b[1]=0xCA/0xFF) was mis-read as a phantom touch -> UI flooded ("bouncing").
  if(buf[0]!=0 || buf[1]==0){gTouchPts=0;return false;}
  uint16_t rx=((buf[2]&0x0F)<<8)|buf[3],ry=((buf[4]&0x0F)<<8)|buf[5];
  // AXS15231B reports touch in native panel pixels; drop out-of-range
  // (this also filters the finger-lift phantom reads).
  if(rx>=LCD_WIDTH||ry>=LCD_HEIGHT){gTouchPts=0;return false;}
  uint16_t px=rx, py=ry;
  switch(g_rot){                                   // inverse of fb_setPixel mapping
    case 1: gTouchX=px; gTouchY=py; break;
    case 2: gTouchX=py; gTouchY=(LCD_WIDTH-1)-px; break;
    case 3: gTouchX=(LCD_WIDTH-1)-px; gTouchY=(LCD_HEIGHT-1)-py; break;
    default: gTouchX=(LCD_HEIGHT-1)-py; gTouchY=px; break;
  }
  gTouchPts=1; return true;
}
static bool getTouchXY(uint16_t*x,uint16_t*y){if(!gTouchPts)return false;*x=constrain(gTouchX,0,gW-1);*y=constrain(gTouchY,0,gH-1);return true;}

// ════════════════════════════════════════════════════════════════════════════
// PART 1 — READ THE CARD'S OWN FILESYSTEM
// ════════════════════════════════════════════════════════════════════════════
// Everything here is READ-ONLY and goes through SD_MMC.readRAW(), which takes an
// ABSOLUTE card sector and bypasses FATFS entirely. We need this because FATFS
// will not tell us where a file physically lives, and that is the one fact the
// whole alias trick depends on.

#define SEC 512
static uint8_t g_sbuf[SEC];                 // one-sector scratch for FS parsing

struct FsInfo {
  bool     ok=false;
  bool     exfat=false;                     // detected but not supported in this build
  uint32_t part_lba=0;
  uint16_t sec_per_clus=0;
  uint32_t fat_lba=0, data_lba=0, root_clus=0;
  uint32_t sec_per_fat=0;
  uint8_t  num_fats=0;
  uint8_t  fat_type=0;                      // 12 / 16 / 32
  uint16_t root_ents=0;                     // FAT12/16 only
  uint32_t root_lba=0, root_secs=0;         // FAT12/16 fixed root area
  uint32_t clusters=0;                      // decides the type - see fsMount
  char     type[10]={0};
};
static FsInfo g_fs;
// Declared here, with the parser, because pathResolve() below is the first user.
// (They were down in PART 2 next to the volume state, which is AFTER this point.)
static String   g_fail_seg="";      // which path component pathResolve choked on
static bool     g_err=false;        // error screen is up; ANY tap dismisses it

static inline uint16_t rd16(const uint8_t*b,int o){return (uint16_t)(b[o]|(b[o+1]<<8));}
static inline uint32_t rd32(const uint8_t*b,int o){return (uint32_t)b[o]|((uint32_t)b[o+1]<<8)|((uint32_t)b[o+2]<<16)|((uint32_t)b[o+3]<<24);}

// Sector 0: either an MBR (partition table) or the BPB itself on a superfloppy card.
static bool mbrFind(uint32_t*partLba){
  if(!SD_MMC.readRAW(g_sbuf,0))return false;
  if(g_sbuf[510]!=0x55||g_sbuf[511]!=0xAA)return false;
  // A BPB in sector 0 starts with a jump instruction; a partition table does not.
  // ORDER MATTERS: a real MBR never begins with a jump instruction, but a BPB
  // always does. Checking the partition table first would happily read boot code
  // at offset 446 as a bogus partition entry on an unpartitioned card.
  if(g_sbuf[0]==0xEB||g_sbuf[0]==0xE9){*partLba=0;return true;}   // superfloppy
  for(int i=0;i<4;i++){
    const uint8_t*e=g_sbuf+446+16*i;
    if(e[4]==0)continue;                    // empty partition slot
    uint32_t lba=rd32(e,8);
    if(lba){*partLba=lba;return true;}
  }
  return false;
}

static bool fsMount(){
  g_fs=FsInfo();
  uint32_t p=0;
  if(!mbrFind(&p))return false;
  g_fs.part_lba=p;
  if(!SD_MMC.readRAW(g_sbuf,p))return false;
  if(!memcmp(g_sbuf+3,"EXFAT   ",8)){ g_fs.exfat=true; strcpy(g_fs.type,"exFAT"); return false; }
  uint16_t bps=rd16(g_sbuf,11);
  if(bps!=SEC)return false;                 // 4K-sector cards are out of scope here
  g_fs.sec_per_clus=g_sbuf[13];
  uint16_t reserved=rd16(g_sbuf,14);
  g_fs.num_fats=g_sbuf[16];
  uint16_t spf16=rd16(g_sbuf,22);
  g_fs.sec_per_fat=spf16?spf16:rd32(g_sbuf,36);
  g_fs.root_ents=rd16(g_sbuf,17);
  uint16_t tot16=rd16(g_sbuf,19);
  uint32_t totSec=tot16?tot16:rd32(g_sbuf,32);
  if(!g_fs.sec_per_clus||!g_fs.sec_per_fat||!totSec||!g_fs.num_fats)return false;
  g_fs.fat_lba  = p+reserved;
  g_fs.root_secs= ((uint32_t)g_fs.root_ents*32+SEC-1)/SEC;
  g_fs.root_lba = g_fs.fat_lba+(uint32_t)g_fs.num_fats*g_fs.sec_per_fat;   // FAT12/16 fixed root
  g_fs.data_lba = g_fs.root_lba+g_fs.root_secs;                            // FAT32: root_secs==0
  // The ONLY correct way to tell FAT12/16/32 apart is the cluster count. Every
  // other method (the "FAT16   " string in the BPB, the partition type byte) is
  // documented as unreliable and is wrong on real cards.
  uint32_t dataSec=totSec-(reserved+(uint32_t)g_fs.num_fats*g_fs.sec_per_fat+g_fs.root_secs);
  g_fs.clusters=dataSec/g_fs.sec_per_clus;
  g_fs.fat_type=(g_fs.clusters<4085)?12:((g_fs.clusters<65525)?16:32);
  if(g_fs.fat_type==32){
    g_fs.root_clus=rd32(g_sbuf,44);
    if(g_fs.root_clus<2)return false;
    strcpy(g_fs.type,"FAT32");
  }else{
    g_fs.root_clus=0;                        // 0 means "the fixed root area", not "invalid"
    strcpy(g_fs.type,g_fs.fat_type==16?"FAT16":"FAT12");
  }
  g_fs.ok=true;
  return true;
}

static inline uint32_t clusLba(uint32_t c){ return g_fs.data_lba+(c-2)*g_fs.sec_per_clus; }
static inline bool isEoc(uint32_t c){
  return (g_fs.fat_type==32)?(c>=0x0FFFFFF8u):((g_fs.fat_type==16)?(c>=0xFFF8u):(c>=0x0FF8u));
}
// Next cluster in the chain, for whichever FAT width this card uses.
static uint32_t fatNext(uint32_t c){
  if(g_fs.fat_type==32){
    uint32_t off=c*4, lba=g_fs.fat_lba+off/SEC;
    if(!SD_MMC.readRAW(g_sbuf,lba))return 0x0FFFFFFF;
    return rd32(g_sbuf,off%SEC)&0x0FFFFFFF;
  }
  if(g_fs.fat_type==16){
    uint32_t off=c*2, lba=g_fs.fat_lba+off/SEC;
    if(!SD_MMC.readRAW(g_sbuf,lba))return 0xFFFFu;
    return rd16(g_sbuf,off%SEC);
  }
  // FAT12: entries are 1.5 bytes and can STRADDLE a sector boundary, so read two.
  static uint8_t two[SEC*2];
  uint32_t off=c+(c>>1), lba=g_fs.fat_lba+off/SEC, so=off%SEC;
  if(!SD_MMC.readRAW(two,lba))return 0x0FFFu;
  if(!SD_MMC.readRAW(two+SEC,lba+1))memset(two+SEC,0,SEC);
  uint16_t v=(uint16_t)(two[so]|(two[so+1]<<8));
  return (c&1)?(uint32_t)(v>>4):(uint32_t)(v&0x0FFFu);
}
// Walk a directory one sector at a time, hiding the FAT32-chain vs FAT12/16-fixed
// -root difference. dirClus==0 means the fixed root area. Returns 0 at the end.
static uint32_t dirNextSector(uint32_t dirClus,uint32_t*clus,uint32_t*secInClus,uint32_t*rootLeft){
  if(dirClus==0){
    if(*rootLeft==0)return 0;
    uint32_t lba=g_fs.root_lba+(g_fs.root_secs-*rootLeft);
    (*rootLeft)--;
    return lba;
  }
  if(*clus<2||isEoc(*clus))return 0;
  uint32_t lba=clusLba(*clus)+*secInClus;
  (*secInClus)++;
  if(*secInClus>=g_fs.sec_per_clus){ *secInClus=0; *clus=fatNext(*clus); }
  return lba;
}

// ── long-filename reconstruction ────────────────────────────────────────────
// SD_MMC hands us long names, so matching on the 8.3 short name is not enough.
// LFN entries precede their short entry in reverse order, 13 UTF-16 chars each.
static void lfnChars(const uint8_t*e,char*out13){
  static const int offs[13]={1,3,5,7,9,14,16,18,20,22,24,28,30};
  for(int i=0;i<13;i++){ uint16_t w=rd16(e,offs[i]); out13[i]=(w==0||w==0xFFFF)?0:(w<128?(char)w:'?'); }
}

// Find `want` inside the directory that starts at cluster `dirClus`
// (dirClus==0 means the FAT16 fixed root, which this build does not walk).
// NOTE: plain out-params, NOT a struct. Arduino auto-generates prototypes and
// injects them ABOVE our declarations, so a user type in a free function's
// signature fails to compile ("'DirHit' has not been declared"). Project rule.
static bool dirFind(uint32_t dirClus,const String&want,
                    uint32_t*outFirstClus,uint32_t*outSize,bool*outIsDir){
  // dirClus==0 is the FAT12/16 FIXED ROOT, not an error. Only FAT32 roots are chains.
  if(!g_fs.ok)return false;
  if(dirClus==0&&g_fs.fat_type==32)return false;
  String w=want; w.toUpperCase();
  char lfn[261]; int lfnLen=0; bool haveLfn=false;
  uint32_t c=dirClus, sic=0, rl=(dirClus?0:g_fs.root_secs), lba;
  {
    {
      while((lba=dirNextSector(dirClus,&c,&sic,&rl))!=0){
      if(!SD_MMC.readRAW(g_sbuf,lba))return false;
      uint8_t sect[SEC]; memcpy(sect,g_sbuf,SEC);          // fatNext() reuses g_sbuf
      for(int o=0;o<SEC;o+=32){
        const uint8_t*e=sect+o;
        if(e[0]==0x00)return false;                        // end of directory
        if(e[0]==0xE5){haveLfn=false;continue;}            // deleted
        if((e[11]&0x0F)==0x0F){                            // LFN fragment
          int seq=e[0]&0x1F; char part[13]; lfnChars(e,part);
          if(seq>=1&&seq<=20){
            int base=(seq-1)*13;
            for(int i=0;i<13;i++) if(base+i<260) lfn[base+i]=part[i];
            if(e[0]&0x40){ lfnLen=base+13; while(lfnLen>0&&lfn[lfnLen-1]==0)lfnLen--; lfn[lfnLen]=0; }
            haveLfn=true;
          }
          continue;
        }
        if(e[11]&0x08){haveLfn=false;continue;}            // volume label
        String nm;
        if(haveLfn&&lfnLen>0){ nm=String(lfn); }
        else {
          char n[13];int k=0;
          for(int i=0;i<8&&e[i]!=' ';i++)n[k++]=e[i];
          if(e[8]!=' '){n[k++]='.';for(int i=8;i<11&&e[i]!=' ';i++)n[k++]=e[i];}
          n[k]=0; nm=String(n);
        }
        haveLfn=false;
        String u=nm; u.toUpperCase();
        if(u==w){
          *outFirstClus=((uint32_t)rd16(e,20)<<16)|rd16(e,26);
          *outSize=rd32(e,28);
          *outIsDir=(e[11]&0x10)!=0;
          return true;
        }
      }
      }
    }
  }
  return false;
}

// "/GENERIC/Game/Game.hfe" -> first cluster + size
static bool pathResolve(const String&path,uint32_t*firstClus,uint32_t*size){
  if(!g_fs.ok)return false;
  uint32_t clus=g_fs.root_clus;
  int i=0;
  while(i<(int)path.length()){
    while(i<(int)path.length()&&path[i]=='/')i++;
    if(i>=(int)path.length())break;
    int j=path.indexOf('/',i); if(j<0)j=path.length();
    String seg=path.substring(i,j);
    g_fail_seg=seg;
    uint32_t hClus=0,hSize=0; bool hDir=false;
    if(!dirFind(clus,seg,&hClus,&hSize,&hDir))return false;
    if(j>=(int)path.length()){
      if(hDir)return false;
      *firstClus=hClus; *size=hSize; return true;
    }
    if(!hDir)return false;
    clus=hClus; i=j+1;
  }
  return false;
}

// ── cluster chain -> runs of contiguous card sectors ────────────────────────
struct Extent { uint32_t lba; uint32_t sectors; };
static std::vector<Extent> g_ext;

static bool chainToExtents(uint32_t firstClus,uint32_t size){
  g_ext.clear();
  if(firstClus<2)return false;
  uint32_t c=firstClus, runStart=firstClus, runLen=1;
  uint32_t guard=0;
  while(++guard<200000){
    uint32_t n=fatNext(c);
    if(n==c+1){ runLen++; c=n; continue; }
    g_ext.push_back({clusLba(runStart),runLen*g_fs.sec_per_clus});
    if(isEoc(n)||n<2)break;
    runStart=n; runLen=1; c=n;
  }
  // trim the last run so the map covers exactly the file, not the cluster slack
  uint32_t need=(size+SEC-1)/SEC, acc=0;
  for(size_t k=0;k<g_ext.size();k++){
    if(acc>=need){ g_ext.resize(k); break; }
    uint32_t take=g_ext[k].sectors; if(take>need-acc)take=need-acc;
    g_ext[k].sectors=take; acc+=take;
  }
  return acc>=need;
}

// ════════════════════════════════════════════════════════════════════════════
// PART 2 — THE ALIAS VOLUME
// ════════════════════════════════════════════════════════════════════════════
// FAT12 metadata lives in RAM. The data area does not exist: every read past
// DATA_LBA is translated into a card sector and served with readRAW.

#define A_RESERVED 1
#define A_SPF      8          // 4096 bytes = 2730 FAT12 entries - enough at every size
#define A_ROOTSEC  4
#define A_DATA_LBA (A_RESERVED+A_SPF+A_ROOTSEC)   // 13
static uint8_t  g_meta[A_DATA_LBA*SEC];           // 6656 bytes - the ENTIRE RAM cost
static uint32_t g_vol_sectors=0;
static uint8_t  g_vol_spc=4;
static uint32_t g_img_size=0;
static String   g_img_name="";
static String   g_img_path="";      // full path on the card - which file is this, really
static volatile uint32_t g_reads=0, g_raw=0;
// STRESS: while a volume is presented, hammer FATFS from the main loop at the same
// time the USB task is calling readRAW. This is the single most important thing the
// sketch tests - the board has never had two things touching the card at once, so
// nothing has ever exercised the sdmmc driver's own locking. Off by default so a
// clean read can be confirmed first.
static bool g_stress=false;
static volatile uint32_t g_stress_ops=0, g_stress_err=0;
// The directory the stress loop walks. Derived from the MOUNTED FILE's own parent,
// so it is guaranteed to exist - a hardcoded "/GENERIC" counted its own absence as
// a concurrency error on any card that does not happen to have that folder.
static String g_stress_dir="/";
static volatile uint32_t g_bytes_served=0;
static bool     g_mounted=false;

static void w16(uint8_t*b,int o,uint16_t v){b[o]=v&0xFF;b[o+1]=v>>8;}
static void w32(uint8_t*b,int o,uint32_t v){b[o]=v&0xFF;b[o+1]=v>>8;b[o+2]=v>>16;b[o+3]=v>>24;}
static void fat12set(uint8_t*f,uint16_t cl,uint16_t v){
  uint32_t i=(cl*3)/2;
  if(!(cl&1)){f[i]=v&0xFF;f[i+1]=(f[i+1]&0xF0)|((v>>8)&0x0F);}
  else       {f[i]=(f[i]&0x0F)|((v<<4)&0xF0);f[i+1]=(v>>4)&0xFF;}
}
// smallest power-of-two cluster that keeps us inside FAT12 and inside our fixed FAT
static void volGeomFor(uint32_t sectors,uint8_t*spcOut,uint32_t*maxOut){
  uint32_t dataSec=sectors-A_DATA_LBA;
  uint32_t fatCap=((uint32_t)A_SPF*512u*2u)/3u;       // 2730
  uint32_t maxCl=fatCap-2; if(maxCl>4084u)maxCl=4084u;
  uint32_t spc=1; while((dataSec/spc)>maxCl&&spc<128u)spc<<=1;
  uint32_t cl=dataSec/spc;
  *spcOut=(uint8_t)spc; *maxOut=cl*spc*512u;          // WHOLE clusters only
}
static bool volBuild(const String&name,uint32_t fsz){
  uint32_t kb=(fsz+1023)/1024, sectors=kb*2+A_DATA_LBA;
  uint8_t spc=4; uint32_t mx=0;
  for(int g=0;g<64;g++){ volGeomFor(sectors,&spc,&mx); if(mx>=fsz)break; sectors+=spc; }
  if(sectors>65535u)return false;                     // BPB_TotSec16 is 16 bits
  g_vol_sectors=sectors; g_vol_spc=spc; g_img_size=fsz;
  memset(g_meta,0,sizeof(g_meta));
  uint8_t*bs=g_meta;
  bs[0]=0xEB;bs[1]=0x3C;bs[2]=0x90;memcpy(bs+3,"MSDOS5.0",8);
  w16(bs,11,SEC);bs[13]=spc;w16(bs,14,A_RESERVED);bs[16]=1;w16(bs,17,64);
  w16(bs,19,(uint16_t)sectors);bs[21]=0xF8;w16(bs,22,A_SPF);w16(bs,24,32);w16(bs,26,64);
  bs[36]=0x80;bs[38]=0x29;w32(bs,39,0x12345678);
  memcpy(bs+43,"GTi ALIAS  ",11);memcpy(bs+54,"FAT12   ",8);
  bs[510]=0x55;bs[511]=0xAA;
  uint8_t*fat=g_meta+A_RESERVED*SEC;
  fat[0]=0xF8;fat[1]=0xFF;fat[2]=0xFF;
  uint32_t clb=(uint32_t)spc*SEC, need=(fsz+clb-1)/clb;
  for(uint32_t i=0;i<need;i++)fat12set(fat,2+i,i==need-1?0x0FFF:3+i);
  uint8_t*root=g_meta+(A_RESERVED+A_SPF)*SEC;

  // ── the 8.3 entry ─────────────────────────────────────────────────────────
  // Split on the last dot of the FULL name BEFORE any truncation. The extension
  // is what FlashFloppy uses to pick a decoder, so it MUST survive even when the
  // long name is shown alongside - a host built without LFN support sees only this.
  uint8_t sfn[11]; memset(sfn,' ',11);
  int dotPos=name.lastIndexOf('.');
  String stem=(dotPos>0)?name.substring(0,dotPos):name;
  String ext =(dotPos>0)?name.substring(dotPos+1):String("");
  stem.toUpperCase(); ext.toUpperCase();
  {int k=0;
   for(int i=0;i<(int)stem.length()&&k<8;i++){
     char ch=stem[i];
     if(ch==' '||ch=='.')continue;                      // not legal in an 8.3 stem
     if(!(isalnum((unsigned char)ch)||strchr("_-~!@#$%^&(){}",ch)))ch='_';
     sfn[k++]=(uint8_t)ch;
   }
   if(k==0)sfn[0]='X';
   for(int i=0,e=0;i<(int)ext.length()&&e<3;i++){
     char ch=ext[i];
     if(!isalnum((unsigned char)ch))continue;
     sfn[8+e++]=(uint8_t)ch;
   }}

  // ── VFAT long-filename entries ───────────────────────────────────────────
  // LFN is an OVERLAY on the directory, not a property of FAT12/16/32 - it works
  // just as well on this tiny FAT12 wrapper. Entries are stored in REVERSE order
  // immediately before the 8.3 entry, 13 UTF-16 chars each, each carrying a
  // checksum of the short name that ties the two together.
  uint8_t chk=0;
  for(int i=0;i<11;i++) chk=(uint8_t)(((chk&1)?0x80:0)+(chk>>1)+sfn[i]);
  int L=(int)name.length(); if(L>255)L=255;
  int nEnt=(L+12)/13; if(nEnt>18)nEnt=18;               // 18*13=234 chars, well inside the 64-entry root
  static const int lfo[13]={1,3,5,7,9,14,16,18,20,22,24,28,30};
  uint8_t*e=root;
  for(int seq=nEnt;seq>=1;seq--){
    memset(e,0,32);
    e[0]=(uint8_t)(seq|((seq==nEnt)?0x40:0));           // 0x40 marks the LAST fragment, which comes FIRST
    e[11]=0x0F; e[12]=0; e[13]=chk; e[26]=0; e[27]=0;
    for(int i=0;i<13;i++){
      int ci=(seq-1)*13+i;
      uint16_t w = (ci<L)?(uint16_t)(uint8_t)name[ci] : ((ci==L)?0x0000:0xFFFF);
      e[lfo[i]]=(uint8_t)(w&0xFF); e[lfo[i]+1]=(uint8_t)(w>>8);
    }
    e+=32;
  }
  memcpy(e,sfn,11); e[11]=0x20; w16(e,26,2); w32(e,28,fsz);
  return true;
}

// virtual data sector -> real card sector (this IS the trick)
static inline uint32_t mapSector(uint32_t fsec,bool*ok){
  uint32_t acc=0;
  for(size_t i=0;i<g_ext.size();i++){
    if(fsec<acc+g_ext[i].sectors){*ok=true;return g_ext[i].lba+(fsec-acc);}
    acc+=g_ext[i].sectors;
  }
  *ok=false; return 0;
}

static int32_t onRead(uint32_t lba,uint32_t off,void*buf,uint32_t n){
  uint8_t*out=(uint8_t*)buf;
  uint64_t s=(uint64_t)lba*SEC+off;
  if(s+n>(uint64_t)g_vol_sectors*SEC)return 0;
  g_reads=g_reads+1;
  uint32_t done=0;
  const uint64_t metaEnd=(uint64_t)A_DATA_LBA*SEC;
  while(done<n&&s+done<metaEnd){
    uint32_t c=(uint32_t)((n-done)<(metaEnd-(s+done))?(n-done):(metaEnd-(s+done)));
    memcpy(out+done,g_meta+(size_t)(s+done),c); done+=c;
  }
  while(done<n){
    uint64_t fo=(s+done)-metaEnd;
    if(fo>=g_img_size){memset(out+done,0,n-done);done=n;break;}   // cluster slack past EOF
    uint32_t fsec=(uint32_t)(fo/SEC), so=(uint32_t)(fo%SEC);
    bool ok=false; uint32_t card=mapSector(fsec,&ok);
    if(!ok){memset(out+done,0,n-done);done=n;break;}
    static uint8_t tmp[SEC];        // static: the MSC callback runs on the USB task's stack
    if(!SD_MMC.readRAW(tmp,card))return (int32_t)done;
    g_raw=g_raw+1;
    uint32_t c=SEC-so;
    if(c>n-done)c=n-done;
    if((uint64_t)c>g_img_size-fo)c=(uint32_t)(g_img_size-fo);
    memcpy(out+done,tmp+so,c); done+=c;
  }
  g_bytes_served=g_bytes_served+done;
  return (int32_t)done;
}
// Read-only: a write is refused so a host reports an error rather than silently
// corrupting the source image on the card.
static int32_t onWrite(uint32_t,uint32_t,uint8_t*,uint32_t){ return 0; }

// One line of boot progress under the splash. The panel is up long before the
// card is, so a hang now has a name instead of being a frozen splash screen.

// Hash the image exactly as the alias serves it - same extent list, same
// mapSector() translation, same readRAW calls - but without the USB host in the
// loop. If this matches `certutil -hashfile <original> SHA256` then the mapping is
// provably correct and anything still wrong lives in the USB path, not here.
static String g_verify="",g_verify2="";
static void verifyMapped(){
  sha256_t sh; sha256_init(&sh);
  static uint8_t buf[SEC];
  uint64_t done=0; uint32_t bad=0;
  while(done<g_img_size){
    bool ok=false; uint32_t card=mapSector((uint32_t)(done/SEC),&ok);
    if(!ok){bad++;break;}
    if(!SD_MMC.readRAW(buf,card)){bad++;break;}
    uint32_t c=SEC; if((uint64_t)c>g_img_size-done)c=(uint32_t)(g_img_size-done);
    sha256_update(&sh,buf,c);
    done+=c;
    if(((done/SEC)&0x7F)==0){
      gfx_fillRect(0,gH-30,gW,12,COL_BG);
      gfx_setTextSize(1);gfx_setTextColor(COL_AMBER,COL_BG);
      String m="hashing "+String((unsigned long)(done>>10))+"K / "+String((unsigned long)(g_img_size>>10))+"K";
      gfx_setCursor((gW-gfx_textWidth(m))/2,gH-28);gfx_print(m);gfx_flush();
    }
  }
  uint8_t dig[32]; sha256_final(&sh,dig);
  if(bad){ g_verify="READ FAILED"; g_verify2=""; return; }
  char hx[65]; for(int i=0;i<32;i++) snprintf(hx+i*2,3,"%02x",dig[i]);
  hx[64]=0;
  g_verify =String(hx).substring(0,32);      // 64 hex chars will not fit on one
  g_verify2=String(hx).substring(32);        // 320px line, so show it as two
}

static void bootMsg(const String&m){
  gfx_fillRect(0,gH/2+18,gW,12,COL_BG);
  gfx_setTextSize(1);gfx_setTextColor(COL_AMBER,COL_BG);
  gfx_setCursor((gW-gfx_textWidth(m))/2,gH/2+20);gfx_print(m);
  gfx_flush();
}

// ════════════════════════════════════════════════════════════════════════════
// PART 3 — MINIMAL BROWSER (names and sizes ONLY - no sidecars, no caches)
// ════════════════════════════════════════════════════════════════════════════
struct Img { String path; String name; uint32_t size; };
static std::vector<Img> g_list;
static int g_sel=0, g_top=0;
static USBMSC MSC;
static bool g_usb_on=false;
static uint32_t g_rev=1;

#define SCAN_MAX 400
static uint32_t g_scan_seen=0, g_scan_t0=0;
static void scanDir(const char*root,int depth){
  if(g_list.size()>=SCAN_MAX)return;                  // checked on ENTRY too, so
  File d=SD_MMC.open(root); if(!d||!d.isDirectory())return;   // recursion unwinds fast
  File f;
  while((f=d.openNextFile())){
    // A 1000-game library is thousands of openNextFile calls over a 1-bit bus.
    // Without this the splash just sits there looking like a hang.
    if((++g_scan_seen % 25)==0){
      bootMsg(String("scanning ")+root+"   "+String(g_scan_seen)+" entries, "
              +String((int)g_list.size())+" images  "+String((millis()-g_scan_t0)/1000)+"s");
    }
    String nm=f.name(); int sl=nm.lastIndexOf('/'); if(sl>=0)nm=nm.substring(sl+1);
    String full=String(root)+"/"+nm;
    if(f.isDirectory()){ if(depth<2){f.close(); scanDir(full.c_str(),depth+1); continue;} }
    else {
      String u=nm; u.toUpperCase();
      bool skip = nm.startsWith(".")||u.endsWith(".JPG")||u.endsWith(".JPEG")||u.endsWith(".PNG")||
                  u.endsWith(".NFO")||u.endsWith(".RTFM")||u.endsWith(".TXT")||u.endsWith(".CFG")||
                  u.endsWith(".INDEX")||u.endsWith(".GAMECACHE")||u.indexOf(".SAV.")>=0;
      if(!skip&&f.size()>0) g_list.push_back({full,nm,(uint32_t)f.size()});
    }
    f.close();
    if(g_list.size()>=SCAN_MAX)break;
  }
  d.close();
}

static String humanSize(uint32_t b){
  char s[24];
  if(b>=1048576u)snprintf(s,sizeof s,"%.2f MB",b/1048576.0);
  else snprintf(s,sizeof s,"%u KB",(unsigned)(b/1024));
  return String(s);
}

#define ROW_H 26
#define LIST_Y 34
static void drawList(){
  gfx_fillScreen(COL_BG);
  gfx_fillRect(0,0,gW,LIST_Y-4,COL_PANEL);
  gfx_setTextSize(2);gfx_setTextColor(COL_ACCENT,COL_PANEL);
  gfx_setCursor(6,8);gfx_print("GTi ALIAS TEST");
  gfx_setTextSize(1);gfx_setTextColor(COL_DIM,COL_PANEL);
  {String s=String(g_fs.ok?g_fs.type:(g_fs.exfat?"exFAT - UNSUPPORTED":"FS?"))+"  "+String((int)g_list.size())+" images";
   gfx_setCursor(gW-6-gfx_textWidth(s),12);gfx_print(s);}
  int rows=(gH-LIST_Y-8)/ROW_H;
  if(g_sel<g_top)g_top=g_sel;
  if(g_sel>=g_top+rows)g_top=g_sel-rows+1;
  for(int i=0;i<rows;i++){
    int idx=g_top+i; if(idx>=(int)g_list.size())break;
    int y=LIST_Y+i*ROW_H;
    bool sel=(idx==g_sel);
    if(sel)gfx_fillRect(0,y,gW,ROW_H-2,COL_SEL);
    gfx_setTextSize(1);
    gfx_setTextColor(sel?TFT_WHITE:COL_LIT,sel?COL_SEL:COL_BG);
    String nm=g_list[idx].name;
    while(gfx_textWidth(nm)>gW-110&&nm.length()>4)nm=nm.substring(0,nm.length()-1);
    gfx_setCursor(8,y+6);gfx_print(nm);
    gfx_setTextColor(sel?TFT_WHITE:COL_MID,sel?COL_SEL:COL_BG);
    String sz=humanSize(g_list[idx].size);
    gfx_setCursor(gW-8-gfx_textWidth(sz),y+6);gfx_print(sz);
  }
  gfx_fillRect(0,gH-16,gW,16,COL_PANEL);
  gfx_setTextSize(1);gfx_setTextColor(COL_DIM,COL_PANEL);
  gfx_setCursor(6,gH-12);gfx_print("tap a row to present it over USB   " FW_VERSION);
  gfx_flush();
}

static void drawMounted(){
  gfx_fillScreen(COL_BG);
  gfx_setTextSize(2);gfx_setTextColor(COL_GREEN,COL_BG);
  {const char*t="PRESENTED OVER USB";gfx_setCursor((gW-gfx_textWidth(t))/2,10);gfx_print(t);}
  gfx_setTextSize(1);
  int y=44;
  auto row=[&](const char*k,const String&v,uint16_t c){
    gfx_setTextColor(COL_DIM,COL_BG);gfx_setCursor(20,y);gfx_print(k);
    gfx_setTextColor(c,COL_BG);gfx_setCursor(150,y);gfx_print(v); y+=14; };
  row("file",       g_img_name,COL_LIT);
  {char n83[13]; uint8_t*r=g_meta+(A_RESERVED+A_SPF)*SEC;
   while(r[11]==0x0F) r+=32;                            // skip the LFN entries
   int k=0;
   for(int i=0;i<8&&r[i]!=' ';i++)n83[k++]=r[i];
   if(r[8]!=' '){n83[k++]='.';for(int i=8;i<11&&r[i]!=' ';i++)n83[k++]=r[i];}
   n83[k]=0; row("shown as",String(n83)+"   (8.3)",COL_MID);}
  row("source",     g_img_path,COL_DIM);
  row("size",       humanSize(g_img_size)+"  ("+String(g_img_size)+" B)",COL_LIT);
  row("extents",    String((int)g_ext.size())+(g_ext.size()==1?"  (contiguous)":"  (FRAGMENTED)"),
                    g_ext.size()==1?COL_MID:COL_AMBER);
  row("volume",     String(g_vol_sectors)+" sec, "+String(g_vol_spc)+" sec/clus",COL_MID);
  row("RAM used",   String((int)sizeof(g_meta))+" bytes (metadata only)",COL_GREEN);
  row("first run",  g_ext.empty()?String("-"):("card LBA "+String(g_ext[0].lba)+" x"+String(g_ext[0].sectors)),COL_MID);
  y+=6;
  row("USB requests",String(g_reads),COL_ACCENT);
  row("card reads",  String(g_raw),COL_ACCENT);
  row("served",      humanSize(g_bytes_served),COL_ACCENT);
  row("stress FATFS", g_stress?("ON  "+String(g_stress_ops)+" ops / "+String(g_stress_err)+" err"):String("off (tap to arm)"),
                      g_stress?((g_stress_err&&!g_stress_ops)?COL_AMBER:(g_stress_err?TFT_RED:COL_GREEN)):COL_DIM);
  if(g_stress)row("  walking", g_stress_dir+(g_stress_ops?"":"   <- 0 ops: can this path be opened?"),
                  g_stress_ops?COL_DIM:COL_AMBER);
  row("SHA256",     g_verify.length()?g_verify:String("tap VERIFY (hashes via the alias map)"),
                    g_verify.length()?COL_GREEN:COL_DIM);
  if(g_verify2.length())row("",g_verify2,COL_GREEN);
  {int sw=130,sh=26,sx=12,sy=gH-88;                              // STRESS toggle
   gfx_fillRoundRect(sx,sy,sw,sh,6,g_stress?COL_GREEN:COL_BAR);
   gfx_setTextSize(1);uint16_t ic=g_stress?inkFor(COL_GREEN):COL_LIT;
   gfx_setTextColor(ic,g_stress?COL_GREEN:COL_BAR);
   const char*t=g_stress?"STRESS: ON":"STRESS: OFF";
   gfx_setCursor(sx+(sw-gfx_textWidth(t))/2,sy+(sh-8)/2);gfx_print(t);}
  {int vw=130,vh=26,vx=gW-12-vw,vy=gH-88;                        // VERIFY
   gfx_fillRoundRect(vx,vy,vw,vh,6,COL_ACCENT);
   gfx_setTextSize(1);gfx_setTextColor(inkFor(COL_ACCENT),COL_ACCENT);
   const char*t="VERIFY SHA256";
   gfx_setCursor(vx+(vw-gfx_textWidth(t))/2,vy+(vh-8)/2);gfx_print(t);}
  int bw=150,bh=34,bx=(gW-bw)/2,by=gH-52;
  gfx_fillRoundRect(bx,by,bw,bh,8,COL_AMBER);
  gfx_setTextSize(2);gfx_setTextColor(inkFor(COL_AMBER),COL_AMBER);
  {const char*e="EJECT";gfx_setCursor(bx+(bw-gfx_textWidth(e))/2,by+(bh-16)/2);gfx_print(e);}
  gfx_setTextSize(1);gfx_setTextColor(COL_DIM,COL_BG);
  {const char*h="copy the file off and checksum it against the card";
   gfx_setCursor((gW-gfx_textWidth(h))/2,by-14);gfx_print(h);}
  gfx_flush();
}

// List the first entries dirFind can actually see in a directory. When a lookup
// fails, what the parser sees beats any amount of theorising about why.
static void dumpDir(uint32_t dirClus,int x,int y,int maxRows){
  if(!g_fs.ok){gfx_setCursor(x,y);gfx_print("(filesystem not parsed)");return;}
  int row=0; char lfn[261]; int lfnLen=0; bool haveLfn=false;
  uint32_t c=dirClus, sic=0, rl=(dirClus?0:g_fs.root_secs), lba;
  {
    while(row<maxRows&&(lba=dirNextSector(dirClus,&c,&sic,&rl))!=0){
    {
      if(!SD_MMC.readRAW(g_sbuf,lba))return;
      uint8_t sect[SEC]; memcpy(sect,g_sbuf,SEC);
      for(int o=0;o<SEC&&row<maxRows;o+=32){
        const uint8_t*e=sect+o;
        if(e[0]==0x00)return;
        if(e[0]==0xE5){haveLfn=false;continue;}
        if((e[11]&0x0F)==0x0F){
          int seq=e[0]&0x1F; char part[13]; lfnChars(e,part);
          if(seq>=1&&seq<=20){int base=(seq-1)*13;
            for(int i=0;i<13;i++)if(base+i<260)lfn[base+i]=part[i];
            if(e[0]&0x40){lfnLen=base+13;while(lfnLen>0&&lfn[lfnLen-1]==0)lfnLen--;lfn[lfnLen]=0;}
            haveLfn=true;}
          continue;
        }
        if(e[11]&0x08){haveLfn=false;continue;}
        String nm;
        if(haveLfn&&lfnLen>0)nm=String(lfn);
        else{char n[13];int k=0;
          for(int i=0;i<8&&e[i]!=' ';i++)n[k++]=e[i];
          if(e[8]!=' '){n[k++]='.';for(int i=8;i<11&&e[i]!=' ';i++)n[k++]=e[i];}
          n[k]=0;nm=String(n);}
        haveLfn=false;
        gfx_setCursor(x,y+row*10);
        gfx_print(String((e[11]&0x10)?"[D] ":"    ")+nm);
        row++;
      }
    }
    }
  }
}

static void errScreen(const String&msg,const String&msg2){
  g_err=true;                       // so loop() knows a tap means "go back", not "mount again"
  gfx_fillScreen(COL_BG);
  gfx_setTextSize(2);gfx_setTextColor(TFT_RED,COL_BG);
  {String t="CANNOT MAP";gfx_setCursor((gW-gfx_textWidth(t))/2,8);gfx_print(t);}
  gfx_setTextSize(1);gfx_setTextColor(COL_LIT,COL_BG);
  gfx_setCursor(10,38);gfx_print(msg);
  gfx_setTextColor(COL_DIM,COL_BG);
  gfx_setCursor(10,50);gfx_print(msg2);
  // what the parser believes about the card
  gfx_setTextColor(COL_ACCENT,COL_BG);
  gfx_setCursor(10,68);
  gfx_print(String("fs=")+(g_fs.ok?g_fs.type:(g_fs.type[0]?g_fs.type:"none"))
            +"  part@"+String(g_fs.part_lba)
            +"  spc="+String(g_fs.sec_per_clus)
            +"  fat@"+String(g_fs.fat_lba));
  gfx_setCursor(10,80);
  gfx_print(String("data@")+String(g_fs.data_lba)+"  root_clus="+String(g_fs.root_clus)
            +"  root@"+String(g_fs.root_lba)+"+"+String(g_fs.root_secs)
            +"  clus="+String(g_fs.clusters));
  gfx_setCursor(10,92);
  gfx_print(String("failed on: ")+(g_fail_seg.length()?g_fail_seg:String("-")));
  gfx_setTextColor(COL_MID,COL_BG);
  gfx_setCursor(10,108);gfx_print("what the parser sees in the root:");
  gfx_setTextColor(COL_LIT,COL_BG);
  dumpDir(g_fs.root_clus,10,122,11);
  gfx_setTextColor(COL_DIM,COL_BG);
  gfx_setCursor(10,gH-16);gfx_print("tap to go back");
  gfx_flush();
}

static void usbDetach(){ MSC.mediaPresent(false); delay(80); tud_disconnect(); delay(400); g_usb_on=false; }
static void usbAttach(){ char r[8];snprintf(r,8,"%lu",(unsigned long)g_rev++);MSC.productRevision(r);
                         MSC.mediaPresent(true); delay(50); tud_connect(); delay(200); g_usb_on=true; }

static bool mountSelected(){
  const Img&im=g_list[g_sel];
  uint32_t fc=0,sz=0;
  if(!g_fs.ok){ errScreen(g_fs.exfat?"card is exFAT":"filesystem not recognised",
                          g_fs.exfat?"reformat as FAT32 to use this test":"FAT32 cards only in this build"); return false; }
  if(!pathResolve(im.path,&fc,&sz)){ errScreen("could not find "+im.name,"in the card's own directory tree"); return false; }
  if(sz!=im.size){ errScreen("size disagrees with FATFS","dir says "+String(sz)+", FATFS says "+String(im.size)); return false; }
  if(!chainToExtents(fc,sz)){ errScreen("cluster chain incomplete","file may be corrupt"); return false; }
  if(!volBuild(im.name,sz)){ errScreen("image too large for FAT12 wrapper","limit is about 32 MB"); return false; }
  // The volume is a different SIZE for every image, and the host caches capacity
  // from READ CAPACITY. Tear the device down and re-advertise while detached,
  // otherwise the second image you pick is read with the first one's geometry.
  if(g_usb_on)usbDetach();
  MSC.end();
  MSC.vendorID("OMEGA");MSC.productID("GTi ALIAS");
  MSC.onRead(onRead);MSC.onWrite(onWrite);
  MSC.mediaPresent(true);
  MSC.begin(g_vol_sectors,SEC);
  {int sl=im.path.lastIndexOf('/');
   g_stress_dir=(sl>0)?im.path.substring(0,sl):String("/");}   // the file's own folder: it exists by definition
  g_img_name=im.name; g_img_path=im.path; g_verify=""; g_verify2=""; g_reads=0; g_raw=0; g_bytes_served=0;
  g_mounted=true;
  usbAttach();
  return true;
}

// ════════════════════════════════════════════════════════════════════════════
void setup(){
  // Backlight first: it is the diagnostic channel (see blCode above), so it has
  // to be alive before anything that can fail or hang.
  ledcAttach(LCD_PIN_BL,5000,8); ledcWrite(LCD_PIN_BL,200);
  Serial.begin(115200); delay(200);
  displayInit(); touchInit();
  gfx_fillScreen(COL_BG);
  gfx_setTextSize(2);gfx_setTextColor(COL_ACCENT,COL_BG);
  {const char*t="GTi ALIAS TEST";gfx_setCursor((gW-gfx_textWidth(t))/2,gH/2-20);gfx_print(t);}
  gfx_setTextSize(1);gfx_setTextColor(COL_DIM,COL_BG);
  {const char*t="mounting card...";gfx_setCursor((gW-gfx_textWidth(t))/2,gH/2+8);gfx_print(t);}
  gfx_flush();

  SD_MMC.setPins(SD_CLK,SD_CMD,SD_D0); delay(100);
  bootMsg("SD_MMC.begin (1-bit, 20MHz)...");
  bool sdok=SD_MMC.begin("/sdcard",true,false,20000);
  if(!sdok){bootMsg("retrying mount...");delay(200);sdok=SD_MMC.begin("/sdcard",true,false,20000);}
  if(sdok){
    bootMsg("card mounted - reading MBR/BPB...");
    fsMount();                                   // parse the card's OWN filesystem
    bootMsg(String("card is ")+(g_fs.ok?g_fs.type:(g_fs.exfat?"exFAT (unsupported)":"unreadable")));
    delay(600);
    Serial.printf("[fs] %s part@%u spc=%u fat@%u data@%u root=%u\n",
      g_fs.ok?g_fs.type:"(none)",(unsigned)g_fs.part_lba,(unsigned)g_fs.sec_per_clus,
      (unsigned)g_fs.fat_lba,(unsigned)g_fs.data_lba,(unsigned)g_fs.root_clus);
    g_scan_t0=millis(); g_scan_seen=0;
    bootMsg("scanning /GENERIC ...");
    scanDir("/GENERIC",0);
    bootMsg("scanning /ADF ...");
    scanDir("/ADF",0);
    bootMsg(String("sorting ")+String((int)g_list.size())+" images...");
    std::sort(g_list.begin(),g_list.end(),[](const Img&a,const Img&b){
      String x=a.name,y=b.name; x.toUpperCase(); y.toUpperCase(); return x<y; });
  } else {
    gfx_fillScreen(COL_BG);gfx_setTextSize(2);gfx_setTextColor(TFT_RED,COL_BG);
    {const char*t="NO SD CARD";gfx_setCursor((gW-gfx_textWidth(t))/2,gH/2-8);gfx_print(t);}
    gfx_flush();
  }

  // Bring USB up detached; a volume only appears once you pick a file.
  volBuild("EMPTY.BIN",512);
  MSC.vendorID("OMEGA");MSC.productID("GTi ALIAS");MSC.productRevision("1.0");
  MSC.onRead(onRead);MSC.onWrite(onWrite);MSC.mediaPresent(false);
  MSC.begin(g_vol_sectors,SEC); USB.begin();
  MSC.mediaPresent(false); delay(100); tud_disconnect(); delay(500); g_usb_on=false;

  drawList();
}

void loop(){
  static bool pressed=false; static uint32_t lastDraw=0;
  static int rel=0;
  uint16_t tx=0,ty=0;
  bool have=Touch_ReadFrame()&&getTouchXY(&tx,&ty);

  if(have&&!pressed){
    pressed=true; rel=0;
    if(g_err){                      // dismiss the diagnostics, whatever was tapped
      g_err=false; drawList();
    } else if(g_mounted){
      int bw=150,bh=34,bx=(gW-bw)/2,by=gH-52;
      int sw=130,sh=26,sx=12,sy=gH-88;
      int vw=130,vh=26,vx=gW-12-vw,vy=gH-88;
      if(tx>=bx&&tx<bx+bw&&ty>=by&&ty<by+bh){
        usbDetach(); g_mounted=false; g_ext.clear(); g_stress=false; g_verify=""; g_verify2=""; drawList();
      } else if(tx>=sx&&tx<sx+sw&&ty>=sy&&ty<sy+sh){
        g_stress=!g_stress; g_stress_ops=0; g_stress_err=0; drawMounted();
      } else if(tx>=vx&&tx<vx+vw&&ty>=vy&&ty<vy+vh){
        g_verify="hashing..."; g_verify2=""; drawMounted(); verifyMapped(); drawMounted();
      }
    } else if(!g_list.empty()){
      if(ty>=LIST_Y&&ty<gH-16){
        int idx=g_top+(ty-LIST_Y)/ROW_H;
        if(idx>=0&&idx<(int)g_list.size()){
          if(idx==g_sel){ if(!mountSelected()){ delay(50); } else drawMounted(); }
          else { g_sel=idx; drawList(); }
        }
      }
    } else {
      drawList();
    }
  } else if(!have&&pressed){
    if(++rel>=3)pressed=false;
  }

  // STRESS: ordinary FATFS traffic, concurrent with the USB task's readRAW calls.
  if(g_mounted&&g_stress){
    File d=SD_MMC.open(g_stress_dir.c_str());
    if(d&&d.isDirectory()){
      int k=0; File f;
      while((f=d.openNextFile())&&k<12){ f.close(); k++; }
      d.close(); g_stress_ops=g_stress_ops+1;
    } else { g_stress_err=g_stress_err+1; if(d)d.close(); }
  }

  // live counters while a volume is presented
  if(g_mounted&&millis()-lastDraw>400){ lastDraw=millis(); drawMounted(); }
  delay(g_stress?2:30);
}
