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
#include <JPEGDEC.h>
#include <Wire.h>
#include <vector>
#include <algorithm>
#include <ctype.h>
#include <sys/stat.h>

#define FW_VERSION "v3.8.3-JC3248"
#include "espnow_server.h"

extern "C" { bool tud_mounted(void); void tud_disconnect(void); void tud_connect(void); void* ps_malloc(size_t size); }

// ════════════════════════════════════════════════════════════════════════════
// HARDWARE
// ════════════════════════════════════════════════════════════════════════════
#define LCD_WIDTH  320
#define LCD_HEIGHT 480
#define gW 480
#define gH 320
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

static inline uint16_t swap16(uint16_t c){return(c>>8)|(c<<8);}
static inline void fb_setPixel(int vx,int vy,uint16_t color){
  int px=vy,py=(LCD_HEIGHT-1)-vx;
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
  for(int py=LCD_HEIGHT-vx1;py<=LCD_HEIGHT-1-vx0;py++){
    uint16_t*row=&framebuffer[py*LCD_WIDTH];
    for(int px=vy0;px<vy1;px++) row[px]=sc;
  }
}

static void gfx_drawRect(int x,int y,int w,int h,uint16_t c){gfx_fillRect(x,y,w,1,c);gfx_fillRect(x,y+h-1,w,1,c);gfx_fillRect(x,y,1,h,c);gfx_fillRect(x+w-1,y,1,h,c);}
static void gfx_hline(int x,int y,int w,uint16_t c){gfx_fillRect(x,y,w,1,c);}
static void gfx_vline(int x,int y,int h,uint16_t c){gfx_fillRect(x,y,1,h,c);}
static void gfx_fillCircle(int cx,int cy,int r,uint16_t c){for(int y=-r;y<=r;y++){int w=(int)sqrtf(r*r-y*y);gfx_fillRect(cx-w,cy+y,2*w+1,1,c);}}
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
static void gfx_drawJpgFile(const String&path,int x,int y,int maxW,int maxH){
  // Use VFS to get real file size (SD_MMC f.size() returns 0 for subdirectory files)
  String vfsPath="/sdcard"+path;
  struct stat st;
  if(stat(vfsPath.c_str(),&st)!=0||st.st_size==0||st.st_size>500000) return;
  size_t sz=(size_t)st.st_size;
  File f=SD_MMC.open(path.c_str(),"r"); if(!f){return;}
  uint8_t*buf=(uint8_t*)ps_malloc(sz); if(!buf){f.close();return;}
  f.read(buf,sz); f.close();
  if(!jpegdec.openRAM(buf,sz,jpeg_buf_cb)){free(buf);return;}
  int jw=jpegdec.getWidth(),jh=jpegdec.getHeight();
  if(jw<=0||jh<=0||jw>2000||jh>2000){jpegdec.close();free(buf);return;}
  jpeg_tmp_buf=(uint16_t*)ps_malloc(jw*jh*2);
  if(!jpeg_tmp_buf){jpegdec.close();free(buf);return;}
  memset(jpeg_tmp_buf,0,jw*jh*2); jpeg_tmp_w=jw; jpeg_tmp_h=jh;
  jpegdec.decode(0,0,0); jpegdec.close(); free(buf);
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
  gTouchX=(LCD_HEIGHT-1)-ry; gTouchY=rx; gTouchPts=1; return true;
}
static bool getTouchXY(uint16_t*x,uint16_t*y){if(!gTouchPts)return false;*x=constrain(gTouchX,0,gW-1);*y=constrain(gTouchY,0,gH-1);return true;}

// ════════════════════════════════════════════════════════════════════════════
// MSC RAM DISK
// ════════════════════════════════════════════════════════════════════════════
USBMSC MSC;static bool g_usb_online=false;
static const uint16_t SECTOR_SIZE=512;static const uint32_t TOTAL_SECTORS=2048;
static const uint16_t RESERVED_SECTORS=1,SECTORS_PER_FAT=6,ROOT_DIR_SECTORS=4;
static const uint8_t SECTORS_PER_CLUSTER=1,NUM_FATS=1;static const uint16_t ROOT_ENTRIES=64;
static const uint32_t FAT_LBA=1,ROOT_LBA=7,DATA_LBA=11;
static const uint32_t MAX_FILE_BYTES=(TOTAL_SECTORS-DATA_LBA)*512;
static const uint32_t ADF_DEFAULT_SIZE=901120;
enum DiskMode{MODE_ADF=0,MODE_DSK=1};static DiskMode g_mode=MODE_ADF;
static const char*getOutputFilename(){return g_mode==MODE_ADF?"DISK.ADF":"DISK.DSK";}
uint8_t*g_disk=nullptr;
static void wr16(uint8_t*p,int o,uint16_t v){p[o]=v;p[o+1]=v>>8;}
static void wr32(uint8_t*p,int o,uint32_t v){p[o]=v;p[o+1]=v>>8;p[o+2]=v>>16;p[o+3]=v>>24;}
static void build_boot_sector(uint8_t*bs){memset(bs,0,512);bs[0]=0xEB;bs[1]=0x3C;bs[2]=0x90;memcpy(bs+3,"MSDOS5.0",8);wr16(bs,11,512);bs[13]=1;wr16(bs,14,1);bs[16]=1;wr16(bs,17,64);wr16(bs,19,2048);bs[21]=0xF8;wr16(bs,22,6);wr16(bs,24,32);wr16(bs,26,64);bs[36]=0x80;bs[38]=0x29;wr32(bs,39,0x12345678);memcpy(bs+43,"ESP32MSC   ",11);memcpy(bs+54,"FAT12   ",8);bs[510]=0x55;bs[511]=0xAA;}
static void fat12_set(uint8_t*fat,uint16_t cl,uint16_t v){uint32_t i=(cl*3)/2;if(!(cl&1)){fat[i]=v&0xFF;fat[i+1]=(fat[i+1]&0xF0)|((v>>8)&0x0F);}else{fat[i]=(fat[i]&0x0F)|((v<<4)&0xF0);fat[i+1]=(v>>4)&0xFF;}}
static void build_fat(uint8_t*fat,uint32_t fsz){memset(fat,0,SECTORS_PER_FAT*512);fat[0]=0xF8;fat[1]=0xFF;fat[2]=0xFF;uint32_t need=(fsz+511)/512;for(uint32_t i=0;i<need;i++)fat12_set(fat,2+i,i==need-1?0x0FFF:3+i);}
static void build_root(uint8_t*root,const char*name,uint32_t fsz){memset(root,0,ROOT_DIR_SECTORS*512);char n[8],e[3];memset(n,' ',8);memset(e,' ',3);char tmp[32];size_t L=strlen(name);if(L>31)L=31;memcpy(tmp,name,L);tmp[L]=0;for(size_t i=0;i<L;i++)tmp[i]=toupper(tmp[i]);const char*dot=strrchr(tmp,'.');size_t nl=dot?(dot-tmp):strlen(tmp);size_t el=dot?strlen(dot+1):0;for(size_t i=0;i<nl&&i<8;i++)n[i]=tmp[i];for(size_t i=0;i<el&&i<3;i++)e[i]=dot[1+i];memcpy(root,n,8);memcpy(root+8,e,3);root[11]=0x20;wr16(root,26,2);wr32(root,28,fsz);}
static void build_volume(const char*outName,uint32_t fsz){if(fsz>MAX_FILE_BYTES)fsz=MAX_FILE_BYTES;memset(g_disk,0,TOTAL_SECTORS*512);build_boot_sector(g_disk);build_fat(g_disk+RESERVED_SECTORS*512,fsz);build_root(g_disk+(RESERVED_SECTORS+SECTORS_PER_FAT)*512,outName,fsz);}
static int32_t onRead(uint32_t lba,uint32_t off,void*buf,uint32_t n){uint32_t s=lba*512+off;if(s+n>TOTAL_SECTORS*512)return 0;memcpy(buf,g_disk+s,n);return n;}
static int32_t onWrite(uint32_t lba,uint32_t off,uint8_t*buf,uint32_t n){uint32_t s=lba*512+off;if(s+n>TOTAL_SECTORS*512)return 0;memcpy(g_disk+s,buf,n);return n;}
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
    if(gd.isDirectory()){File e;while((e=gd.openNextFile())){String fn=e.name();int sl=fn.lastIndexOf('/');if(sl>=0)fn=fn.substring(sl+1);String u=fn;u.toUpperCase();
      if(u.endsWith(ext)||u.endsWith(".IMG")||u.endsWith(".ADZ")){String fp=en+"/"+fn;if(!fp.startsWith("/"))fp="/"+fp;out.push_back(fp);count++;
        if(millis()-lastDraw>80){drawScanFrame(count);lastDraw=millis();}}e.close();}}
    else{String fn=en;int sl=fn.lastIndexOf('/');if(sl>=0)fn=fn.substring(sl+1);String u=fn;u.toUpperCase();
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

// ── Game cache — defined after STATE section below ──

// ════════════════════════════════════════════════════════════════════════════
// FILE HELPERS
// ════════════════════════════════════════════════════════════════════════════
static String basenameNoExt(const String&p){int s=p.lastIndexOf('/'),d=p.lastIndexOf('.');String b=s>=0?p.substring(s+1):p;if(d>s)b=b.substring(0,d-(s>=0?s+1:0));return b;}
static String filenameOnly(const String&p){int s=p.lastIndexOf('/');return s>=0?p.substring(s+1):p;}
static String parentDir(const String&p){int s=p.lastIndexOf('/');return s>0?p.substring(0,s):"/";}
static String getGameBaseName(const String&fp){String b=basenameNoExt(filenameOnly(fp));int d=b.lastIndexOf('-');if(d>0&&d<(int)b.length()-1){bool num=true;for(int i=d+1;i<(int)b.length();i++)if(!isDigit(b[i])){num=false;break;}if(num)return b.substring(0,d);}return b;}
static int getDiskNumber(const String&fp){String b=basenameNoExt(filenameOnly(fp));int d=b.lastIndexOf('-');if(d>0){int n=b.substring(d+1).toInt();if(n>0)return n;}return 0;}

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
  const char*exts[]={".jpg",".jpeg",".JPG",".JPEG"};
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

// ════════════════════════════════════════════════════════════════════════════
// STATE
// ════════════════════════════════════════════════════════════════════════════
static bool g_wireless_mode=false,g_loop_cracktro=false,g_info_showing=false;
// ── Item 4: load/eject behaviour toggles (all default OFF = safest) ──
static bool g_tapload=false;    // ON = tapping the already-selected row loads it (old double-tap behaviour)
static bool g_hotswap=false;    // ON = tapping another disk while loaded swaps to it instantly
static bool g_forceswap=false;  // ON = swap disk bytes in place without the USB eject/re-attach cycle
static int g_info_pair_btn_y=0;
static int g_info_rescan_btn_y=0,g_info_reset_btn_y=0,g_info_pair_now_btn_y=0,g_info_font_btn_y=0;
struct GameEntry{String name;int first_file_idx;int disk_count;String jpg_path;std::vector<int>disk_indices;};
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

static void parseNFO(const String&txt,String&t,String&b){
  t="";b="";if(!txt.length())return;
  std::vector<String>lines;int pos=0;while(pos<(int)txt.length()){int nl=txt.indexOf('\n',pos);if(nl<0)nl=txt.length();String L=txt.substring(pos,nl);L.trim();lines.push_back(L);pos=nl+1;}
  for(size_t i=0;i<lines.size();i++){
    if(!t.length()&&lines[i].startsWith("Title:")){t=lines[i].substring(6);t.trim();}
    if(!b.length()&&(lines[i].startsWith("Blurb:")||lines[i].startsWith("Description:"))){b=lines[i].substring(lines[i].indexOf(':')+1);b.trim();
      for(size_t j=i+1;j<lines.size()&&j<i+4;j++){if(lines[j].indexOf(':')>0)break;if(lines[j].length()){b+="\n"+lines[j];}}}}
  if(!t.length()&&lines.size())t=lines[0];
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

// Forward decls
static void applyFont(int f);   // defined with the layout section; used by loadConfig
static void drawFullUI();
static void drawListAndCover();
static bool doLoadSelected(const String&p);
static void doUnload();

static void cycleTheme(){applyTheme((g_theme_idx+1)%NUM_THEMES);saveConfigKey("THEME",String(g_theme_idx));drawFullUI();gfx_flush();}
static bool g_espnow_started=false;
static void ensureEspNow(){if(!g_espnow_started){espnowBegin();g_espnow_started=true;}}
static void setWirelessMode(bool w){g_wireless_mode=w;saveConfigKey("MODE",w?"WIRELESS":"STANDALONE");if(w)ensureEspNow();drawFullUI();gfx_flush();}

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
  f.println("# Font size: SMALL, NORMAL, LARGE");
  f.println("FONT=NORMAL");
  f.println("");
  f.println("# Load/eject behaviour (all OFF = safest: select, then press the button)");
  f.println("# TAPLOAD: ON = tapping the already-highlighted game row loads it (old double-tap)");
  f.println("TAPLOAD=OFF");
  f.println("# HOTSWAP: ON = tapping another disk while loaded swaps to it instantly");
  f.println("HOTSWAP=OFF");
  f.println("# FORCESWAP: ON = swap disk contents without the USB eject/re-attach cycle");
  f.println("FORCESWAP=OFF");
  f.println("");
  f.println("# Device name shown in status bar / scroller");
  f.println("DEVICE_NAME=OMEGAWARE");
  f.println("");
  f.println("# Wireless dongle MAC (auto-filled when you pair via INFO screen)");
  f.println("# XIAO_MAC=");
  f.close();
}

static void loadConfig(){
  applyTheme(0);
  File f=SD_MMC.open("/CONFIG.TXT",FILE_READ);if(!f)return;
  while(f.available()){String l=f.readStringUntil('\n');l.trim();if(l.startsWith("#"))continue;
    int eq=l.indexOf('=');if(eq<0)continue;String k=l.substring(0,eq),v=l.substring(eq+1);k.trim();v.trim();
    if(k=="THEME")applyTheme(v.toInt());else if(k=="LOOP")g_loop_cracktro=(v=="1");else if(k=="MODE")g_wireless_mode=(v=="WIRELESS");
    else if(k=="TAPLOAD")g_tapload=(v=="ON"||v=="1");else if(k=="HOTSWAP")g_hotswap=(v=="ON"||v=="1");else if(k=="FORCESWAP")g_forceswap=(v=="ON"||v=="1");
    else if(k=="FONT"){int f=1;if(v=="SMALL")f=0;else if(v=="LARGE")f=2;applyFont(f);}}
  f.close();
}

// ════════════════════════════════════════════════════════════════════════════
// LAYOUT — 480×320
// ════════════════════════════════════════════════════════════════════════════
#define VW 480
#define VH 320
#define STATUS_H 20
#define COVER_W 150
#define COVER_ART_X 4
#define COVER_ART_Y (STATUS_H+4)
#define COVER_ART_W 142
#define COVER_ART_H 130
#define AZ_W 30
#define AZ_X (VW-AZ_W)
#define LIST_X COVER_W
#define LIST_W (VW-COVER_W-AZ_W)
#define MODE_BAR_H 18
#define LIST_TOP (STATUS_H+MODE_BAR_H)
#define NOW_PLAY_H 22
#define BOTTOM_H 40
#define LIST_BOTTOM (VH-BOTTOM_H-NOW_PLAY_H)
// Font profile: 0=SMALL 1=NORMAL 2=LARGE. Runtime row height / rows-per-screen / name size.
static int g_font=1, g_item_h=(LIST_BOTTOM-LIST_TOP)/4, g_items_vis=4, g_name_sz=2;
#define LIST_ITEM_H g_item_h
#define ITEMS_VIS   g_items_vis
static void applyFont(int f){if(f<0||f>2)f=1;g_font=f;int rows;
  if(f==0){rows=6;g_name_sz=1;}else if(f==2){rows=3;g_name_sz=3;}else{rows=4;g_name_sz=2;}
  g_item_h=(LIST_BOTTOM-LIST_TOP)/rows;g_items_vis=rows;}
static const char* fontName(int f){return f==0?"SMALL":f==2?"LARGE":"NORMAL";}

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

static void drawCracktroSplash(){
  initStars();
  const char*scrollText="       GOTEK TOUCHSCREEN INTERFACE  *  CODED BY MEZ AND DIMMY OF OMEGAWARE  *  "
    "THE ULTIMATE RETRO DISK LOADER FOR AMIGA AND CPC  ...  KEEP THE SCENE ALIVE  ...  OMEGAWARE 2026  *         ";
  int scrollLen=strlen(scrollText),scrollPos=0;
  uint16_t copper[]={0xF800,0xF920,0xFAA0,0xFC00,0xFDE0,0xEFE0,0x87E0,0x07E0,0x07F0,0x07FF,0x041F,0x001F,0x801F,0xF81F,0xF810,0xF800};
  uint16_t sine[]={0xF800,0xFBE0,0xFFE0,0x07E0,0x07FF,0x001F,0xF81F,0xF800};
  int frame=0;unsigned long startMs=millis();
  gfx_fillScreen(TFT_BLACK);gfx_flush();
  while(true){
    if(Touch_ReadFrame()){unsigned long t0=millis();while(Touch_ReadFrame()&&millis()-t0<500)delay(10);break;}
    if(!g_loop_cracktro&&millis()-startMs>=6000)break;
    int copperY=gH/2-30+(int)(40.0f*sinf(frame*0.05f));
    // Clear previous frame areas
    gfx_fillRect(0,0,gW,gH-24,TFT_BLACK);
    // Stars
    for(int i=0;i<NUM_STARS;i++){star_x[i]-=star_speed[i];if(star_x[i]<0){star_x[i]=gW-1;star_y[i]=random(0,gH-30);}
      uint16_t c=star_speed[i]==3?TFT_WHITE:star_speed[i]==2?(uint16_t)0x7BEF:(uint16_t)0x4208;gfx_drawPixel(star_x[i],star_y[i],c);}
    // Copper bars
    for(int i=0;i<16;i++){int by=copperY+i*3;if(by>=0&&by<gH-30)gfx_fillRect(0,by,gW,2,copper[i]);}
    // Text
    gfx_setTextSize(2);gfx_setTextColor(sine[(frame/4)%8],TFT_BLACK);
    {const char*s="MEZ & DIMMY";int tw=gfx_textWidth(s);gfx_setCursor((gW-tw)/2,copperY-50);gfx_print(s);}
    gfx_setTextColor((frame%40<20)?TFT_CYAN:TFT_WHITE,TFT_BLACK);
    {const char*s="- OMEGAWARE -";int tw=gfx_textWidth(s);gfx_setCursor((gW-tw)/2,copperY+52);gfx_print(s);}
    gfx_setTextSize(1);gfx_setTextColor(0x7BEF,TFT_BLACK);
    {const char*s="GOTEK TOUCHSCREEN INTERFACE";int tw=gfx_textWidth(s);gfx_setCursor((gW-tw)/2,copperY+72);gfx_print(s);}
    gfx_setTextColor((frame/15)%2?TFT_BLACK:0x4A8A,TFT_BLACK);
    {const char*s="TAP TO CONTINUE";int tw=gfx_textWidth(s);gfx_setCursor((gW-tw)/2,gH-50);gfx_print(s);}
    // Scroll bar
    gfx_fillRect(0,gH-24,gW,24,0x0010);gfx_setTextSize(1);gfx_setTextColor(TFT_YELLOW,0x0010);
    {int sc=scrollPos/12,px2=scrollPos%12;gfx_setCursor(-px2,gH-20);
      for(int c=0;c<gW/12+3;c++){char buf[2]={scrollText[(sc+c)%scrollLen],0};gfx_print(buf);}}
    gfx_flush();scrollPos+=2;frame++;delay(33);
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
}

static void drawCoverPanel(){
  gfx_fillRect(0,STATUS_H,COVER_W,VH-STATUS_H-BOTTOM_H,COL_PANEL);if(g_games.empty())return;
  auto&game=g_games[g_sel];
  // Lazy load: find JPG on first view of this game
  if(!game.jpg_path.length()){
    String jpg;if(findJPGFor(g_files[game.first_file_idx],jpg)) game.jpg_path=jpg;
    else game.jpg_path="?"; // mark as checked so we don't search again
  }
  // Lazy load: NFO title on first view
  static int lastNfoSel=-1;static String cachedNfoBlurb="";
  if(lastNfoSel!=g_sel){
    lastNfoSel=g_sel;cachedNfoBlurb="";
    String nfoP,nT,nB;
    if(findNFOFor(g_files[game.first_file_idx],nfoP)){
      File nf=SD_MMC.open(nfoP,FILE_READ);if(nf){String txt;while(nf.available()&&txt.length()<512)txt+=(char)nf.read();nf.close();parseNFO(txt,nT,nB);
        if(nT.length()&&game.name==basenameNoExt(filenameOnly(g_files[game.first_file_idx]))) game.name=nT;
        cachedNfoBlurb=nB;}}
  }
  // Content bottom: title+blurb must not cross the disk grid (multi-disk) or the INSERT button (single-disk)
  int cb;
  if(game.disk_count>1){int nd=game.disk_count,pages=(nd+DISKS_PER_PAGE-1)/DISKS_PER_PAGE;int insertY=VH-BOTTOM_H-36;bool mp=(pages>1);int pbh=mp?16:0,pg=mp?4:0,gh=2*20+4;cb=insertY-gh-pbh-pg-12-2;}
  else cb=VH-BOTTOM_H-38;
  // Cover art + wrapped title + wrapped blurb
  gfx_fillRoundRect(COVER_ART_X,COVER_ART_Y,COVER_ART_W,COVER_ART_H,5,COL_BAR);
  gfx_drawRoundRect(COVER_ART_X-1,COVER_ART_Y-1,COVER_ART_W+2,COVER_ART_H+2,6,COL_ACCENT);
  if(game.jpg_path.length()>0&&game.jpg_path!="?") gfx_drawJpgFile(game.jpg_path,COVER_ART_X+2,COVER_ART_Y+2,COVER_ART_W-4,COVER_ART_H-4);
  else{char ib[2]={(char)toupper(game.name.charAt(0)),0};gfx_setTextSize(2);gfx_setTextColor(COL_LIT,COL_BAR);gfx_setCursor(COVER_ART_X+COVER_ART_W/2-6,COVER_ART_Y+COVER_ART_H/2-8);gfx_print(ib);}
  {int ty=COVER_ART_Y+COVER_ART_H+4;gfx_setTextSize(1);
   ty=drawWrapped(4,ty,game.name,COVER_W-8,10,2,cb,COL_LIT,COL_PANEL);
   if(cachedNfoBlurb.length()>0) drawWrapped(4,ty,cachedNfoBlurb,COVER_W-8,9,12,cb,COL_DIM,COL_PANEL);}
  // Disk selector — paginated, 6 disks per page (3 cols x 2 rows), NEXT button below
  if(game.disk_count>1){
    int nd=game.disk_count;
    int pages=(nd+DISKS_PER_PAGE-1)/DISKS_PER_PAGE;
    if(g_disk_page>=pages)g_disk_page=0;
    int pageStart=g_disk_page*DISKS_PER_PAGE;
    int pageEnd=min(pageStart+DISKS_PER_PAGE,nd);   // exclusive
    const int COLS=3;
    int dbw=44, dbh=20, dgap=4;
    int gridW=COLS*dbw+(COLS-1)*dgap;
    int gx=max(4,(COVER_W-gridW)/2);
    int insertY=VH-BOTTOM_H-36;
    bool multiPage=(pages>1);
    int pageBtnH=multiPage?16:0, pageGap=multiPage?4:0;
    int gridH=2*dbh+dgap;                            // always reserve 2 rows
    int labelY=insertY-gridH-pageBtnH-pageGap-12;
    int gridY=labelY+10;
    // Label with page indicator
    gfx_setTextSize(1);gfx_setTextColor(COL_DIM,COL_PANEL);gfx_setCursor(4,labelY);
    if(multiPage)gfx_print("DISK ("+String(g_disk_page+1)+"/"+String(pages)+"):");
    else gfx_print("DISK:");
    // Disk buttons for this page
    for(int d=pageStart;d<pageEnd;d++){
      int slot=d-pageStart;
      int col=slot%COLS, row=slot/COLS;
      int bx=gx+col*(dbw+dgap), by=gridY+row*(dbh+dgap);
      bool isSel=d==g_disk_sel, isLd=(g_loaded_game_idx==g_sel&&g_loaded_disk_idx==d);
      uint16_t bc=isLd?COL_GREEN:(isSel?COL_AMBER:COL_BAR);
      gfx_fillRoundRect(bx,by,dbw,dbh,4,bc);
      gfx_drawRoundRect(bx,by,dbw,dbh,4,isSel?COL_AMBER:COL_DIM);
      gfx_setTextColor(isLd||isSel?TFT_BLACK:COL_LIT,bc);
      String dl="D"+String(d+1);
      gfx_setCursor(bx+(dbw-gfx_textWidth(dl))/2,by+(dbh-8)/2);gfx_print(dl);
    }
    // NEXT-page button (full width under grid) when multipage
    if(multiPage){
      int pby=gridY+gridH+pageGap;
      gfx_fillRoundRect(gx,pby,gridW,pageBtnH,4,COL_ACCENT);
      gfx_setTextColor(TFT_WHITE,COL_ACCENT);
      String pl=(g_disk_page+1<pages)?("MORE D"+String(pageEnd+1)+"+  >"):("<  BACK TO D1");
      gfx_setCursor(gx+(gridW-gfx_textWidth(pl))/2,pby+(pageBtnH-8)/2);gfx_print(pl);
    }
  }
  // INSERT/EJECT
  int btnY=VH-BOTTOM_H-36;bool isL=g_loaded&&g_loaded_game_idx==g_sel;
  gfx_fillRoundRect(4,btnY,COVER_W-8,28,8,isL?(uint16_t)0x4000:(uint16_t)0x0340);
  gfx_drawRoundRect(4,btnY,COVER_W-8,28,8,isL?(uint16_t)0xE8C4:COL_GREEN);
  gfx_setTextSize(1);gfx_setTextColor(TFT_WHITE,isL?(uint16_t)0x4000:(uint16_t)0x0340);
  const char*lbl=isL?"EJECT":"INSERT";int tw=gfx_textWidth(lbl);gfx_setCursor(4+(COVER_W-8-tw)/2,btnY+10);gfx_print(lbl);
}

static void drawModeBar(){
  gfx_fillRect(LIST_X,STATUS_H,LIST_W+AZ_W,MODE_BAR_H,COL_BAR);gfx_setTextSize(1);
  bool isA=g_mode==MODE_ADF;
  gfx_fillRoundRect(LIST_X+4,STATUS_H+2,36,14,7,isA?COL_ACCENT:COL_BG);gfx_setTextColor(isA?COL_AMBER:COL_DIM,isA?COL_ACCENT:COL_BG);gfx_setCursor(LIST_X+10,STATUS_H+6);gfx_print("ADF");
  gfx_fillRoundRect(LIST_X+44,STATUS_H+2,36,14,7,!isA?COL_ACCENT:COL_BG);gfx_setTextColor(!isA?COL_AMBER:COL_DIM,!isA?COL_ACCENT:COL_BG);gfx_setCursor(LIST_X+50,STATUS_H+6);gfx_print("DSK");
  gfx_setTextColor(COL_MID,COL_BAR);gfx_setCursor(LIST_X+86,STATUS_H+6);gfx_print(String(g_games.size())+" games");
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
    gfx_fillCircle(cx,cy,r,sel?COL_AMBER:(ld?COL_GREEN:COL_CIRC));
    gfx_setTextSize(g_name_sz);gfx_setTextColor(sel||ld?TFT_BLACK:COL_CIRC_TEXT,sel?COL_AMBER:COL_CIRC);
    char ib[2]={(char)toupper(game.name.charAt(0)),0};gfx_setCursor(cx-gfx_textWidth(ib)/2,cy-4*g_name_sz);gfx_print(ib);
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
  int y=LIST_BOTTOM;
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
  int togY=LIST_BOTTOM+NOW_PLAY_H-AZ_TOG_H;         // toggle top; letters occupy the strip above it
  int barH=togY-LIST_TOP;
  gfx_fillRect(AZ_X,LIST_TOP,AZ_W,(LIST_BOTTOM+NOW_PLAY_H)-LIST_TOP,COL_PANEL);
  char p0[27],p1[27];int n0=azHalf(0,p0),n1=azHalf(1,p1);
  char*half=g_az_page==0?p0:p1;int hn=g_az_page==0?n0:n1;
  int slots=max(13,max(n0,n1));                    // enough rows for the bigger half ('#' can push it to 14)
  int letterH=barH/slots;if(letterH<7)letterH=7;
  int lsz=(letterH>=15)?2:1;
  gfx_setTextSize(lsz);
  for(int i=0;i<hn;i++){char letter=half[i];int ly=LIST_TOP+i*letterH;if(ly+letterH>togY)break;
    if(letter==g_active_letter){gfx_fillRect(AZ_X,ly,AZ_W,letterH,COL_AMBER);gfx_setTextColor(TFT_BLACK,COL_AMBER);}
    else gfx_setTextColor(COL_DIM,COL_PANEL);
    gfx_setCursor(AZ_X+(AZ_W-6*lsz)/2,ly+(letterH-8*lsz)/2);char lb[2]={letter,0};gfx_print(lb);}
  // Toggle button — taller, fills the strip bottom
  int togBottom=LIST_BOTTOM+NOW_PLAY_H;
  gfx_fillRoundRect(AZ_X+1,togY+1,AZ_W-2,togBottom-togY-2,5,COL_ACCENT);
  gfx_setTextSize(1);gfx_setTextColor(TFT_WHITE,COL_ACCENT);
  const char*blbl=g_az_page==0?"N-Z":"A-M";
  gfx_setCursor(AZ_X+(AZ_W-gfx_textWidth(blbl))/2,togY+(AZ_TOG_H-8)/2);gfx_print(blbl);
  int maxOff=(int)g_games.size()-ITEMS_VIS;if(maxOff>0){int thumbH=max(4,barH*ITEMS_VIS/(int)g_games.size());int thumbY=LIST_TOP+(barH-thumbH)*g_scroll/maxOff;gfx_fillRect(AZ_X-2,thumbY,2,thumbH,COL_BLUE);}
}

static bool handleAlphabetTouch(uint16_t px,uint16_t py){
  if(px<AZ_X||py<LIST_TOP||py>=(uint16_t)(LIST_BOTTOM+NOW_PLAY_H)||!active_letter_count)return false;
  int togY=LIST_BOTTOM+NOW_PLAY_H-AZ_TOG_H;
  // Toggle button (taller hit region at the strip bottom) — manual page peek
  if(py>=(uint16_t)togY){g_az_page=g_az_page?0:1;return true;}
  int barH=togY-LIST_TOP;
  char p0[27],p1[27];int n0=azHalf(0,p0),n1=azHalf(1,p1);
  char*half=g_az_page==0?p0:p1;int hn=g_az_page==0?n0:n1;
  if(hn==0){g_az_page=g_az_page?0:1;return true;}
  int slots=max(13,max(n0,n1));int letterH=barH/slots;if(letterH<7)letterH=7;
  int r=constrain((int)(py-LIST_TOP)/letterH,0,hn-1);
  char letter=half[r];
  int target=0;for(int i=0;i<(int)g_games.size();i++){if(bucketOf(g_games[i].name)>=letter){target=i;break;}}
  setActiveLetter(letter);
  g_sel=target;g_scrollPx=min((float)(target*LIST_ITEM_H),(float)maxScrollPx());g_inertia_on=false;
  return true;
}

static void drawBottomBar(){
  int y=VH-BOTTOM_H;gfx_fillRect(0,y,VW,BOTTOM_H,COL_BAR);gfx_hline(0,y,VW,COL_SEP);
  int bw=VW/4;
  struct{const char*icon;const char*label;uint16_t col;}btns[4]={{"<","PREV",COL_ORANGE},{">","NEXT",COL_BLUE},{"#","THEME",COL_AMBER},{"i","INFO",COL_MID}};
  for(int i=0;i<4;i++){int bx=i*bw;if(i>0)gfx_vline(bx,y+3,BOTTOM_H-6,COL_SEP);
    int cx=bx+bw/2,cy2=y+10;gfx_fillCircle(cx,cy2,8,(uint16_t)(btns[i].col>>2));gfx_drawCircle(cx,cy2,8,btns[i].col);
    gfx_setTextSize(1);gfx_setTextColor(btns[i].col,(uint16_t)(btns[i].col>>2));int tw=gfx_textWidth(btns[i].icon);gfx_setCursor(cx-tw/2,cy2-4);gfx_print(btns[i].icon);
    gfx_setTextColor(COL_DIM,COL_BAR);tw=gfx_textWidth(btns[i].label);gfx_setCursor(bx+(bw-tw)/2,y+22);gfx_print(btns[i].label);}
  gfx_setTextColor((uint16_t)(COL_AMBER>>1),COL_BAR);String tn=THEMES[g_theme_idx].name;int tw=gfx_textWidth(tn);gfx_setCursor(2*bw+(bw-tw)/2,y+33);gfx_print(tn);
}

static void drawFullUI(){gfx_fillScreen(COL_BG);drawStatusBar();drawCoverPanel();drawModeBar();drawFileList();drawNowPlayingBar();drawAZBar();drawBottomBar();}
static void drawListAndCover(){gfx_fillRect(0,STATUS_H,COVER_W,VH-STATUS_H-BOTTOM_H,COL_PANEL);gfx_fillRect(LIST_X,LIST_TOP,LIST_W,LIST_BOTTOM-LIST_TOP,COL_BG);drawCoverPanel();drawFileList();drawNowPlayingBar();drawAZBar();}

// ════════════════════════════════════════════════════════════════════════════
// LOAD / UNLOAD
// ════════════════════════════════════════════════════════════════════════════
static bool doLoadSelected(const String&adfPath){
  gfx_fillRect(0,STATUS_H,COVER_W,VH-STATUS_H-BOTTOM_H,COL_PANEL);
  gfx_setTextSize(1);gfx_setTextColor(TFT_CYAN,COL_PANEL);String tn=basenameNoExt(filenameOnly(adfPath));if(tn.length()>16)tn=tn.substring(0,16);
  gfx_setCursor(6,STATUS_H+16);gfx_print(tn);gfx_setTextColor(COL_LIT,COL_PANEL);gfx_setCursor(6,STATUS_H+28);gfx_print("Loading...");
  gfx_flush();
  // Clean swap: if a disk is already mounted, cleanly eject first so the host re-reads the new media.
  // FORCESWAP=ON skips this and swaps the bytes in place (faster, but the host may not notice).
  if(g_loaded && !g_forceswap) hardDetach();
  File f=SD_MMC.open(adfPath.c_str(),FILE_READ);if(!f){gfx_setTextColor(TFT_RED,COL_PANEL);gfx_setCursor(6,STATUS_H+40);gfx_print("FAILED");gfx_flush();delay(1000);drawFullUI();gfx_flush();return false;}
  // Use VFS to get real file size (SD_MMC f.size() returns 0 for subdirectory files)
  String vfsLoad="/sdcard"+adfPath;
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
  hardAttach();g_loaded=true;g_loaded_name=basenameNoExt(filenameOnly(adfPath));g_loaded_game_idx=g_sel;g_loaded_disk_idx=g_disk_sel;
  if(g_wireless_mode&&g_espnow_started&&espnowIsPaired()){espnowSendNotify(g_loaded_name,g_mode==MODE_ADF?"ADF":"DSK",copied);espnowSendDisk(copied);}
  drawStatusBar();drawListAndCover();gfx_flush();return true;
}

static void doUnload(){hardDetach();g_loaded=false;g_loaded_name="";g_loaded_game_idx=-1;g_loaded_disk_idx=-1;
  if(g_wireless_mode&&g_espnow_started&&espnowIsPaired())espnowSendEject();drawStatusBar();drawListAndCover();gfx_flush();}

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
  listImages(SD_MMC,g_files);buildGameList();buildActiveLetters();
  g_sel=0;g_scroll=0;g_disk_sel=0;g_disk_page=0;g_scrollPx=0;g_az_page=0;g_inertia_on=false;
  if(!g_games.empty())setActiveLetter(bucketOf(g_games[0].name));
  drawFullUI();gfx_flush();
}

// ════════════════════════════════════════════════════════════════════════════
// ESP-NOW PAIRING
// ════════════════════════════════════════════════════════════════════════════
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
      gfx_fillRect(0,0,VW,22,COL_BAR);gfx_setTextSize(1);gfx_setTextColor(COL_AMBER,COL_BAR);gfx_setCursor(6,7);gfx_print("NAME DONGLE");
      gfx_setTextColor(COL_DIM,COL_BAR);gfx_setCursor(VW-gfx_textWidth(macLabel)-6,7);gfx_print(macLabel);
      gfx_fillRoundRect(8,28,VW-16,34,6,COL_PANEL);gfx_drawRoundRect(8,28,VW-16,34,6,COL_AMBER);
      gfx_setTextSize(2);gfx_setTextColor(TFT_WHITE,COL_PANEL);
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
  // Results: tap a row to highlight, then USE (pair) / RENAME (keyboard) / BACK
  int rowH=40, listTop=26, btnBarY=VH-40;
  int maxRows=(btnBarY-listTop-4)/rowH; if(n>maxRows)n=maxRows;
  int sel=0; for(int i=0;i<n;i++){if(espnowIsPaired()&&espnowGetXiaoMac()==espnowScanGetMac(i)){sel=i;break;}}
  bool dirty=true;
  while(true){
    if(dirty){dirty=false;
      gfx_fillScreen(COL_BG);
      gfx_setTextSize(1);gfx_setTextColor(COL_ORANGE,COL_BG);gfx_setCursor(8,7);gfx_print("Dongles ("+String(n)+") - pick one:");
      for(int i=0;i<n;i++){int y=listTop+i*rowH;String mac=espnowScanGetMac(i),suffix=mac.substring(12),nm=getDongleName(mac);
        bool isSel=(i==sel),isActive=(espnowIsPaired()&&espnowGetXiaoMac()==mac);
        uint16_t bg=isSel?COL_SEL:COL_PANEL;
        gfx_fillRoundRect(8,y,VW-16,rowH-4,6,bg);gfx_drawRoundRect(8,y,VW-16,rowH-4,6,isSel?COL_AMBER:COL_ACCENT);
        gfx_setTextSize(1);gfx_setTextColor(isSel?inkFor(bg):COL_AMBER,bg);gfx_setCursor(18,y+7);gfx_print(nm.length()?nm:("Dongle "+String(i+1)));
        gfx_setTextColor(isSel?inkFor(bg):COL_MID,bg);gfx_setCursor(18,y+20);gfx_print("OMEGA-"+suffix);
        if(isActive){gfx_setTextColor(COL_GREEN,bg);gfx_setCursor(VW-64,y+13);gfx_print("ACTIVE");}
      }
      int bw=(VW-4*4)/3,bx=4;const char* BL[3]={"USE","RENAME","BACK"};uint16_t BC[3]={COL_GREEN,COL_ACCENT,COL_BAR};
      for(int i=0;i<3;i++){gfx_fillRoundRect(bx,btnBarY+2,bw,34,6,BC[i]);gfx_setTextColor(inkFor(BC[i]),BC[i]);gfx_setTextSize(1);gfx_setCursor(bx+(bw-gfx_textWidth(BL[i]))/2,btnBarY+14);gfx_print(BL[i]);bx+=bw+4;}
      gfx_flush();
    }
    if(Touch_ReadFrame()){uint16_t tx,ty;
      if(getTouchXY(&tx,&ty)){
        if(ty>=(uint16_t)listTop&&ty<(uint16_t)(listTop+n*rowH)){int idx=(ty-listTop)/rowH;if(idx>=0&&idx<n&&idx!=sel){sel=idx;dirty=true;}}
        else if(ty>=(uint16_t)btnBarY){int bw=(VW-4*4)/3;int i=((int)tx-4)/(bw+4);
          if(i==0){espnowScanSelect(sel);int y=listTop+sel*rowH;gfx_fillRoundRect(8,y,VW-16,rowH-4,6,COL_GREEN);gfx_setTextColor(TFT_BLACK,COL_GREEN);gfx_setTextSize(1);gfx_setCursor(18,y+13);gfx_print("PAIRED");gfx_flush();delay(700);break;}
          else if(i==1){String mac=espnowScanGetMac(sel);String label="OMEGA-"+mac.substring(12),nm=getDongleName(mac),out;if(onScreenKeyboard(label,nm,out)){setDongleName(mac,out);}dirty=true;}
          else break; // BACK
        }
        uint32_t r=millis();while(Touch_ReadFrame()&&millis()-r<400)delay(10);
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
    if(!SD_MMC.exists("/ADF"))SD_MMC.mkdir("/ADF");if(!SD_MMC.exists("/DSK"))SD_MMC.mkdir("/DSK");
    generateDefaultConfig();
    loadConfig();
    listImages(SD_MMC,g_files);
    if(!readGameCache()){buildGameList();}
    buildActiveLetters();
    if(!g_games.empty())setActiveLetter(bucketOf(g_games[0].name));
  } else {gfx_setTextColor(TFT_RED,TFT_BLACK);gfx_setCursor(8,200);gfx_print("SD MOUNT FAILED");gfx_flush();delay(2000);}
  if(g_wireless_mode){espnowBegin();g_espnow_started=true;}
  drawCracktroSplash();
  USB.onEvent(usbEventCB);MSC.vendorID("ESP32");MSC.productID("RAMDISK");MSC.productRevision("1.0");
  MSC.onRead(onRead);MSC.onWrite(onWrite);MSC.mediaPresent(true);
  MSC.begin(TOTAL_SECTORS,512);USB.begin();hardDetach();
  drawFullUI();gfx_flush();
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
static void handleTap(uint16_t px,uint16_t py){
  // ── A-Z bar (letters + toggle button) ──
  if(px>=AZ_X&&py>=LIST_TOP&&py<(uint16_t)(LIST_BOTTOM+NOW_PLAY_H)){if(handleAlphabetTouch(px,py)){drawListAndCover();gfx_flush();}return;}

  // ── INFO panel touches ──
  if(g_info_showing&&px<COVER_W){
    int pw=COVER_W-8,saY=g_info_pair_btn_y,wiY=saY+38;
    // STANDALONE tap (switch from wireless)
    if(py>=(uint16_t)saY&&py<(uint16_t)(saY+34)&&g_wireless_mode){setWirelessMode(false);g_info_showing=false;return;}
    // WIRELESS tap (switch from standalone)
    if(py>=(uint16_t)wiY&&py<(uint16_t)(wiY+34)&&!g_wireless_mode){setWirelessMode(true);g_info_showing=false;return;}
    // RESCAN button
    if(g_info_rescan_btn_y&&py>=(uint16_t)g_info_rescan_btn_y&&py<(uint16_t)(g_info_rescan_btn_y+22)){doRescan();return;}
    // SOFT RESET button
    if(g_info_reset_btn_y&&py>=(uint16_t)g_info_reset_btn_y&&py<(uint16_t)(g_info_reset_btn_y+22)){
      gfx_fillRoundRect(4,g_info_reset_btn_y,pw,22,6,0xE8C4);gfx_setTextColor(TFT_BLACK,0xE8C4);gfx_setCursor(12,g_info_reset_btn_y+7);gfx_print("RESTARTING...");gfx_flush();delay(800);ESP.restart();}
    // PAIR NOW button (wireless mode only)
    if(g_wireless_mode&&g_info_pair_now_btn_y&&py>=(uint16_t)g_info_pair_now_btn_y&&py<(uint16_t)(g_info_pair_now_btn_y+24)){doPairNow();return;}
    // FONT selector (cycle SMALL/NORMAL/LARGE) — keep INFO open, update the label
    // in place and re-render the list at the new size (don't drop back to the cover)
    if(g_info_font_btn_y&&py>=(uint16_t)g_info_font_btn_y&&py<(uint16_t)(g_info_font_btn_y+22)){
      applyFont((g_font+1)%3);saveConfigKey("FONT",fontName(g_font));
      int pw=COVER_W-8;gfx_fillRoundRect(4,g_info_font_btn_y,pw,22,6,COL_ACCENT);
      gfx_setTextColor(inkFor(COL_ACCENT),COL_ACCENT);{String fl=String("FONT: ")+fontName(g_font);int tw=gfx_textWidth(fl);gfx_setCursor(4+(pw-tw)/2,g_info_font_btn_y+7);gfx_print(fl);}
      redrawListArea();return;}
    return;
  }

  // ── INSERT/EJECT ──
  {int btnY=VH-BOTTOM_H-36;if(px<COVER_W&&py>=btnY&&py<VH-BOTTOM_H&&!g_games.empty()){
    auto&gm=g_games[g_sel];int idx=gm.disk_indices.empty()?gm.first_file_idx:gm.disk_indices[min(g_disk_sel,(int)gm.disk_indices.size()-1)];
    if(g_loaded&&g_loaded_game_idx==g_sel){
      if(g_loaded_disk_idx==g_disk_sel)doUnload();          // pressing on exactly what's mounted = eject
      else doLoadSelected(g_files[idx]);                     // a different disk is selected = clean-swap to it
    } else doLoadSelected(g_files[idx]);                     // load the selected game/disk
    return;}}

  // ── Disk selector (paginated, 6/page) ──
  if(px<COVER_W&&!g_games.empty()){auto&game=g_games[g_sel];if(game.disk_count>1){
    int nd=game.disk_count;
    int pages=(nd+DISKS_PER_PAGE-1)/DISKS_PER_PAGE;
    int pageStart=g_disk_page*DISKS_PER_PAGE;
    int pageEnd=min(pageStart+DISKS_PER_PAGE,nd);
    const int COLS=3;
    int dbw=44, dbh=20, dgap=4;
    int gridW=COLS*dbw+(COLS-1)*dgap;
    int gx=max(4,(COVER_W-gridW)/2);
    int insertY=VH-BOTTOM_H-36;
    bool multiPage=(pages>1);
    int pageBtnH=multiPage?16:0, pageGap=multiPage?4:0;
    int gridH=2*dbh+dgap;
    int gridY=insertY-gridH-pageBtnH-pageGap-12+10;
    // NEXT/BACK page button
    if(multiPage){
      int pby=gridY+gridH+pageGap;
      if(py>=(uint16_t)pby&&py<(uint16_t)(pby+pageBtnH)&&px>=gx&&px<gx+gridW){
        g_disk_page=(g_disk_page+1)%pages;   // cycle pages (wraps to 1 after last)
        drawCoverPanel();gfx_flush();return;
      }
    }
    // Disk buttons on current page
    for(int d=pageStart;d<pageEnd;d++){
      int slot=d-pageStart;
      int col=slot%COLS, row=slot/COLS;
      int bx=gx+col*(dbw+dgap), by=gridY+row*(dbh+dgap);
      if(px>=(uint16_t)bx&&px<(uint16_t)(bx+dbw)&&py>=(uint16_t)by&&py<(uint16_t)(by+dbh)){
        g_disk_sel=d;
        // Default: just select the disk (press INSERT to mount). HOTSWAP=ON = swap instantly while loaded.
        if(g_hotswap&&g_loaded&&g_loaded_game_idx==g_sel){doLoadSelected(g_files[game.disk_indices[g_disk_sel]]);}
        else{drawCoverPanel();gfx_flush();}
        return;
      }
    }
  }}

  // ── Mode bar ──
  if(py>=STATUS_H&&py<STATUS_H+MODE_BAR_H&&px>=LIST_X){
    if(px<LIST_X+40&&g_mode!=MODE_ADF){g_mode=MODE_ADF;g_files.clear();listImages(SD_MMC,g_files);if(!readGameCache())buildGameList();buildActiveLetters();g_sel=g_scroll=0;g_scrollPx=0;g_az_page=0;if(!g_games.empty())setActiveLetter(bucketOf(g_games[0].name));drawFullUI();gfx_flush();return;}
    if(px>=LIST_X+44&&px<LIST_X+80&&g_mode!=MODE_DSK){g_mode=MODE_DSK;g_files.clear();listImages(SD_MMC,g_files);if(!readGameCache())buildGameList();buildActiveLetters();g_sel=g_scroll=0;g_scrollPx=0;g_az_page=0;if(!g_games.empty())setActiveLetter(bucketOf(g_games[0].name));drawFullUI();gfx_flush();return;}}

  // ── File list ──
  if(px>=LIST_X&&px<AZ_X&&py>=LIST_TOP&&py<LIST_BOTTOM){
    g_info_showing=false;int gi=(int)((g_scrollPx+(py-LIST_TOP))/LIST_ITEM_H);if(gi>=0&&gi<(int)g_games.size()){
      if(gi==g_sel){
        // Default: tapping the already-selected row does nothing (load only via INSERT).
        // TAPLOAD=ON restores the old tap-again-to-load/eject behaviour.
        if(g_tapload){if(g_loaded&&g_loaded_game_idx==g_sel)doUnload();else{auto&gm=g_games[g_sel];g_disk_sel=0;g_disk_page=0;doLoadSelected(g_files[gm.disk_indices.empty()?gm.first_file_idx:gm.disk_indices[0]]);}}
      }
      else{g_sel=gi;setActiveLetter(bucketOf(g_games[gi].name));g_disk_sel=0;g_disk_page=0;drawListAndCover();gfx_flush();}}return;}

  // ── Bottom bar ──
  if(py>=VH-BOTTOM_H){int bw=VW/4,btn=px/bw;
    if(btn==0&&g_sel>0){g_sel--;g_disk_sel=0;g_disk_page=0;setActiveLetter(bucketOf(g_games[g_sel].name));if((float)(g_sel*LIST_ITEM_H)<g_scrollPx)g_scrollPx=g_sel*LIST_ITEM_H;drawListAndCover();gfx_flush();}
    else if(btn==1&&g_sel<(int)g_games.size()-1){g_sel++;g_disk_sel=0;g_disk_page=0;setActiveLetter(bucketOf(g_games[g_sel].name));if((float)((g_sel+1)*LIST_ITEM_H)>g_scrollPx+(LIST_BOTTOM-LIST_TOP))g_scrollPx=(g_sel+1)*LIST_ITEM_H-(LIST_BOTTOM-LIST_TOP);drawListAndCover();gfx_flush();}
    else if(btn==2){cycleTheme();}
    else if(btn==3){
      // INFO panel
      g_info_showing=true;gfx_fillRect(0,STATUS_H,COVER_W,VH-STATUS_H-BOTTOM_H,COL_PANEL);
      int y=STATUS_H+4,pw=COVER_W-8;
      gfx_setTextSize(1);gfx_setTextColor(COL_DIM,COL_PANEL);gfx_setCursor(6,y);gfx_print("TRANSFER MODE");y+=10;
      // STANDALONE
      uint16_t saCol=!g_wireless_mode?COL_GREEN:COL_BAR;gfx_fillRoundRect(4,y,pw,34,6,saCol);gfx_drawRoundRect(4,y,pw,34,6,!g_wireless_mode?COL_GREEN:COL_DIM);
      gfx_setTextColor(!g_wireless_mode?TFT_BLACK:COL_DIM,saCol);{int tw=gfx_textWidth("STANDALONE");gfx_setCursor(4+(pw-tw)/2,y+6);}gfx_print("STANDALONE");
      gfx_setTextColor(!g_wireless_mode?TFT_BLACK:COL_DIM,saCol);{int tw=gfx_textWidth("direct USB");gfx_setCursor(4+(pw-tw)/2,y+20);}gfx_print("direct USB");
      g_info_pair_btn_y=y;y+=38;
      // WIRELESS
      uint16_t wiCol=g_wireless_mode?COL_BLUE:COL_BAR;gfx_fillRoundRect(4,y,pw,34,6,wiCol);gfx_drawRoundRect(4,y,pw,34,6,g_wireless_mode?COL_BLUE:COL_DIM);
      gfx_setTextColor(g_wireless_mode?TFT_WHITE:COL_DIM,wiCol);{int tw=gfx_textWidth("WIRELESS");gfx_setCursor(4+(pw-tw)/2,y+6);}gfx_print("WIRELESS");
      gfx_setTextColor(g_wireless_mode?TFT_WHITE:COL_DIM,wiCol);{int tw=gfx_textWidth("WiFi to dongle");gfx_setCursor(4+(pw-tw)/2,y+20);}gfx_print("WiFi to dongle");
      y+=38;gfx_hline(6,y,pw,COL_SEP);y+=4;
      // System info
      gfx_setTextColor(COL_LIT,COL_PANEL);gfx_setCursor(6,y);gfx_print("Heap:"+String(ESP.getFreeHeap()/1024)+"K");y+=9;
      gfx_setCursor(6,y);gfx_print("PSRAM:"+String(ESP.getFreePsram()/1024)+"K");y+=9;
      gfx_setCursor(6,y);gfx_print("Games: "+String(g_games.size()));y+=10;
      // FONT size selector (tap to cycle SMALL/NORMAL/LARGE)
      gfx_hline(6,y,pw,COL_SEP);y+=4;
      gfx_fillRoundRect(4,y,pw,22,6,COL_ACCENT);
      gfx_setTextColor(inkFor(COL_ACCENT),COL_ACCENT);{String fl=String("FONT: ")+fontName(g_font);int tw=gfx_textWidth(fl);gfx_setCursor(4+(pw-tw)/2,y+7);gfx_print(fl);}
      g_info_font_btn_y=y;y+=26;
      // Wireless status
      if(g_wireless_mode){gfx_hline(6,y,pw,COL_SEP);y+=4;
        if(espnowIsPaired()){bool on=espnowXiaoOnline();gfx_setTextColor(on?COL_GREEN:COL_ORANGE,COL_PANEL);gfx_setCursor(6,y);gfx_print(on?"DONGLE: ONLINE":"DONGLE: OFFLINE");y+=9;
          String dn=getDongleName(espnowGetXiaoMac());
          gfx_setTextColor(COL_AMBER,COL_PANEL);gfx_setCursor(6,y);gfx_print(dn.length()?dn:espnowGetXiaoMac());y+=9;}
        else{gfx_setTextColor(COL_ORANGE,COL_PANEL);gfx_setCursor(6,y);gfx_print("Not paired");y+=9;}
        y+=2;
        // PAIR NOW button - stacked after wireless info
        gfx_fillRoundRect(4,y,pw,24,6,espnowIsPaired()?COL_GREEN:COL_AMBER);
        gfx_setTextColor(TFT_BLACK,espnowIsPaired()?COL_GREEN:COL_AMBER);const char*pl=espnowIsPaired()?"SWITCH DONGLE":"SCAN DONGLES";{int tw=gfx_textWidth(pl);gfx_setCursor(4+(pw-tw)/2,y+8);}gfx_print(pl);
        g_info_pair_now_btn_y=y;y+=28;}
      gfx_hline(6,y,pw,COL_SEP);y+=4;
      // RESCAN button
      gfx_fillRoundRect(4,y,pw,22,6,COL_BLUE);gfx_drawRoundRect(4,y,pw,22,6,COL_ACCENT);
      gfx_setTextColor(TFT_WHITE,COL_BLUE);{int tw=gfx_textWidth("RESCAN SD");gfx_setCursor(4+(pw-tw)/2,y+7);}gfx_print("RESCAN SD");
      g_info_rescan_btn_y=y;y+=26;
      // SOFT RESET button
      gfx_fillRoundRect(4,y,pw,22,6,0x8000);gfx_drawRoundRect(4,y,pw,22,6,0xE8C4);
      gfx_setTextColor(TFT_WHITE,0x8000);{int tw=gfx_textWidth("SOFT RESET");gfx_setCursor(4+(pw-tw)/2,y+7);}gfx_print("SOFT RESET");
      g_info_reset_btn_y=y;
      gfx_flush();
    }
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

  if(touch){
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
