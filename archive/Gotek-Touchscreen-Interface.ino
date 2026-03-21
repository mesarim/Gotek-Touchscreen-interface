/*
  Gotek-Touchscreen-Interface
  Board: Waveshare ESP32-S3-Touch-LCD-2.8

  Board settings:
    Board: waveshare_esp32_s3_touch_lcd_28
    USB CDC On Boot → Enabled
    PSRAM → OPI PSRAM
    Flash Size → 16MB (qio120)
    Partition → app3M_fat9M_16MB
*/

#include <Arduino.h>
#include <Wire.h>
#include <SD_MMC.h>
#include <FS.h>
#include <sys/stat.h>
#include <JPEGDEC.h>
#include <PNGdec.h>
#include <vector>
#include <algorithm>
#include <ctype.h>

#include <USB.h>
#include <USBMSC.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <LovyanGFX.hpp>
#include "default_theme.h"

using std::vector;
using std::sort;
using std::swap;




extern "C" {
  extern bool tud_mounted(void);
  extern void tud_disconnect(void);
  extern void tud_connect(void);
  extern void* ps_malloc(size_t size);
}

#define FW_VERSION "v0.8.0-WebServer"

// ============================================================================
// SD tail-log  — writes to /LOG.TXT, mirrors to Serial
// Defined early so LOG() is available everywhere in the file
// ============================================================================
#define LOG_FILE "/LOG.TXT"
#define LOG_MAX_BYTES (64UL * 1024UL)

// Forward declaration — defined later with other config vars
static bool cfg_log_enabled = false;

static bool g_sd_log_ready = false;

static void SD_Log(const String& msg) {
  Serial.println(msg);
  if (!g_sd_log_ready || !cfg_log_enabled) return;
  if (SD_MMC.exists(LOG_FILE)) {
    File chk = SD_MMC.open(LOG_FILE, FILE_READ);
    if (chk) {
      size_t sz = chk.size(); chk.close();
      if (sz > LOG_MAX_BYTES) {
        File rd = SD_MMC.open(LOG_FILE, FILE_READ);
        if (rd) {
          size_t keep = LOG_MAX_BYTES / 2;
          rd.seek(sz - keep);
          uint8_t *buf = (uint8_t*)malloc(keep + 1);
          if (buf) {
            size_t got = rd.read(buf, keep); rd.close();
            SD_MMC.remove(LOG_FILE);
            File wr = SD_MMC.open(LOG_FILE, FILE_WRITE);
            if (wr) { wr.write(buf, got); wr.close(); }
            free(buf);
          } else { rd.close(); }
        }
      }
    }
  }
  File f = SD_MMC.open(LOG_FILE, FILE_APPEND);
  if (!f) return;
  f.println(msg);
  f.close();
}

#define LOG(x) SD_Log(String(x))


// ============================================================================
// DISPLAY CONFIGURATION
// ============================================================================

#define LCD_WIDTH   320
#define LCD_HEIGHT  240
#define gW 320
#define gH 240

// Waveshare ESP32-S3-Touch-LCD-2.8 SD pins
#define SD_CLK  14
#define SD_CMD  17
#define SD_D0   16


#define ROWS_PER_STRIP 10

// Colors (RGB565)
#define TFT_BLACK      0x0000
#define TFT_WHITE      0xFFFF
#define TFT_RED        0xF800
#define TFT_GREEN      0x07E0
#define TFT_BLUE       0x001F
#define TFT_CYAN       0x07FF
#define TFT_DARKGREY   0x7BEF
#define TFT_YELLOW     0xFFE0
#define TFT_ORANGE     0xFD20
#define TFT_GREY       0x8410

// ============================================================================
// LCD INIT COMMANDS
// ============================================================================


// ============================================================================
// FONT DATA - 6x8 bitmap font (ASCII 32-126) - SHARED
// ============================================================================

static const uint8_t font6x8[95][6] PROGMEM = {
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 32 space
  {0x00, 0x00, 0x4F, 0x00, 0x00, 0x00}, // 33 !
  {0x00, 0x07, 0x00, 0x07, 0x00, 0x00}, // 34 "
  {0x14, 0x7F, 0x14, 0x7F, 0x14, 0x00}, // 35 #
  {0x24, 0x2A, 0x7F, 0x2A, 0x12, 0x00}, // 36 $
  {0x62, 0x64, 0x08, 0x13, 0x23, 0x00}, // 37 %
  {0x36, 0x49, 0x49, 0x36, 0x50, 0x00}, // 38 &
  {0x00, 0x04, 0x03, 0x00, 0x00, 0x00}, // 39 '
  {0x00, 0x1C, 0x22, 0x41, 0x00, 0x00}, // 40 (
  {0x00, 0x41, 0x22, 0x1C, 0x00, 0x00}, // 41 )
  {0x14, 0x08, 0x3E, 0x08, 0x14, 0x00}, // 42 *
  {0x08, 0x08, 0x3E, 0x08, 0x08, 0x00}, // 43 +
  {0x00, 0x50, 0x30, 0x00, 0x00, 0x00}, // 44 ,
  {0x08, 0x08, 0x08, 0x08, 0x08, 0x00}, // 45 -
  {0x00, 0x60, 0x60, 0x00, 0x00, 0x00}, // 46 .
  {0x20, 0x10, 0x08, 0x04, 0x02, 0x00}, // 47 /
  {0x3E, 0x51, 0x49, 0x45, 0x3E, 0x00}, // 48 0
  {0x00, 0x42, 0x7F, 0x40, 0x00, 0x00}, // 49 1
  {0x42, 0x61, 0x51, 0x49, 0x46, 0x00}, // 50 2
  {0x21, 0x41, 0x45, 0x4B, 0x31, 0x00}, // 51 3
  {0x18, 0x14, 0x12, 0x7F, 0x10, 0x00}, // 52 4
  {0x27, 0x45, 0x45, 0x45, 0x39, 0x00}, // 53 5
  {0x3C, 0x4A, 0x49, 0x49, 0x30, 0x00}, // 54 6
  {0x01, 0x71, 0x09, 0x05, 0x03, 0x00}, // 55 7
  {0x36, 0x49, 0x49, 0x49, 0x36, 0x00}, // 56 8
  {0x06, 0x49, 0x49, 0x29, 0x1E, 0x00}, // 57 9
  {0x00, 0x36, 0x36, 0x00, 0x00, 0x00}, // 58 :
  {0x00, 0x56, 0x36, 0x00, 0x00, 0x00}, // 59 ;
  {0x08, 0x14, 0x22, 0x41, 0x00, 0x00}, // 60 <
  {0x14, 0x14, 0x14, 0x14, 0x14, 0x00}, // 61 =
  {0x00, 0x41, 0x22, 0x14, 0x08, 0x00}, // 62 >
  {0x02, 0x01, 0x51, 0x09, 0x06, 0x00}, // 63 ?
  {0x32, 0x49, 0x79, 0x41, 0x3E, 0x00}, // 64 @
  {0x7E, 0x11, 0x11, 0x11, 0x7E, 0x00}, // 65 A
  {0x7F, 0x49, 0x49, 0x49, 0x36, 0x00}, // 66 B
  {0x3E, 0x41, 0x41, 0x41, 0x22, 0x00}, // 67 C
  {0x7F, 0x41, 0x41, 0x41, 0x3E, 0x00}, // 68 D
  {0x7F, 0x49, 0x49, 0x49, 0x41, 0x00}, // 69 E
  {0x7F, 0x09, 0x09, 0x09, 0x01, 0x00}, // 70 F
  {0x3E, 0x41, 0x49, 0x49, 0x3A, 0x00}, // 71 G
  {0x7F, 0x08, 0x08, 0x08, 0x7F, 0x00}, // 72 H
  {0x00, 0x41, 0x7F, 0x41, 0x00, 0x00}, // 73 I
  {0x20, 0x40, 0x41, 0x3F, 0x01, 0x00}, // 74 J
  {0x7F, 0x08, 0x14, 0x22, 0x41, 0x00}, // 75 K
  {0x7F, 0x40, 0x40, 0x40, 0x40, 0x00}, // 76 L
  {0x7F, 0x02, 0x0C, 0x02, 0x7F, 0x00}, // 77 M
  {0x7F, 0x04, 0x08, 0x10, 0x7F, 0x00}, // 78 N
  {0x3E, 0x41, 0x41, 0x41, 0x3E, 0x00}, // 79 O
  {0x7F, 0x09, 0x09, 0x09, 0x06, 0x00}, // 80 P
  {0x3E, 0x41, 0x41, 0x21, 0x5E, 0x00}, // 81 Q
  {0x7F, 0x09, 0x19, 0x29, 0x46, 0x00}, // 82 R
  {0x46, 0x49, 0x49, 0x49, 0x31, 0x00}, // 83 S
  {0x01, 0x01, 0x7F, 0x01, 0x01, 0x00}, // 84 T
  {0x3F, 0x40, 0x40, 0x40, 0x3F, 0x00}, // 85 U
  {0x1F, 0x20, 0x40, 0x20, 0x1F, 0x00}, // 86 V
  {0x3F, 0x40, 0x38, 0x40, 0x3F, 0x00}, // 87 W
  {0x63, 0x14, 0x08, 0x14, 0x63, 0x00}, // 88 X
  {0x07, 0x08, 0x70, 0x08, 0x07, 0x00}, // 89 Y
  {0x61, 0x51, 0x49, 0x45, 0x43, 0x00}, // 90 Z
  {0x00, 0x7F, 0x41, 0x00, 0x00, 0x00}, // 91 [
  {0x02, 0x04, 0x08, 0x10, 0x20, 0x00}, // 92 
  {0x00, 0x41, 0x7F, 0x00, 0x00, 0x00}, // 93 ]
  {0x04, 0x02, 0x01, 0x02, 0x04, 0x00}, // 94 ^
  {0x40, 0x40, 0x40, 0x40, 0x40, 0x00}, // 95 _
  {0x00, 0x01, 0x02, 0x04, 0x00, 0x00}, // 96 `
  {0x20, 0x54, 0x54, 0x54, 0x78, 0x00}, // 97 a
  {0x7F, 0x48, 0x44, 0x44, 0x38, 0x00}, // 98 b
  {0x38, 0x44, 0x44, 0x44, 0x20, 0x00}, // 99 c
  {0x38, 0x44, 0x44, 0x48, 0x7F, 0x00}, // 100 d
  {0x38, 0x54, 0x54, 0x54, 0x18, 0x00}, // 101 e
  {0x08, 0x7E, 0x09, 0x01, 0x02, 0x00}, // 102 f
  {0x18, 0xA4, 0xA4, 0x9C, 0x78, 0x00}, // 103 g
  {0x7F, 0x08, 0x04, 0x04, 0x78, 0x00}, // 104 h
  {0x00, 0x44, 0x7D, 0x40, 0x00, 0x00}, // 105 i
  {0x20, 0x40, 0x44, 0x3D, 0x00, 0x00}, // 106 j
  {0x7F, 0x10, 0x28, 0x44, 0x00, 0x00}, // 107 k
  {0x00, 0x41, 0x7F, 0x40, 0x00, 0x00}, // 108 l
  {0x7C, 0x04, 0x78, 0x04, 0x78, 0x00}, // 109 m
  {0x7C, 0x08, 0x04, 0x04, 0x78, 0x00}, // 110 n
  {0x38, 0x44, 0x44, 0x44, 0x38, 0x00}, // 111 o
  {0x7C, 0x14, 0x14, 0x14, 0x08, 0x00}, // 112 p
  {0x08, 0x14, 0x14, 0x18, 0x7C, 0x00}, // 113 q
  {0x7C, 0x08, 0x04, 0x04, 0x08, 0x00}, // 114 r
  {0x48, 0x54, 0x54, 0x54, 0x20, 0x00}, // 115 s
  {0x04, 0x3F, 0x44, 0x40, 0x20, 0x00}, // 116 t
  {0x3C, 0x40, 0x40, 0x20, 0x7C, 0x00}, // 117 u
  {0x1C, 0x20, 0x40, 0x20, 0x1C, 0x00}, // 118 v
  {0x3C, 0x40, 0x30, 0x40, 0x3C, 0x00}, // 119 w
  {0x44, 0x28, 0x10, 0x28, 0x44, 0x00}, // 120 x
  {0x1C, 0xA0, 0xA0, 0x9C, 0x0C, 0x00}, // 121 y
  {0x44, 0x64, 0x54, 0x4C, 0x44, 0x00}, // 122 z
  {0x00, 0x08, 0x36, 0x41, 0x00, 0x00}, // 123 {
  {0x00, 0x00, 0x7F, 0x00, 0x00, 0x00}, // 124 |
  {0x00, 0x41, 0x36, 0x08, 0x00, 0x00}, // 125 }
  {0x08, 0x04, 0x08, 0x10, 0x08, 0x00}, // 126 ~
};

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================

// LovyanGFX display class definition
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel_instance;
  lgfx::Bus_SPI _bus_instance;
  lgfx::Light_PWM _light_instance;
  // Touch handled via raw I2C (Touch_CST328 not in lgfx 1.2.19)
public:
  LGFX(void) {
    // Bus config
    auto cfg = _bus_instance.config();
    cfg.spi_host = SPI3_HOST;   // v0.5.2: SPI3_HOST
    cfg.spi_mode = 0;
    cfg.freq_write = 40000000;  // v0.5.2: 40MHz
    cfg.freq_read = 16000000;
    cfg.pin_sclk = 40;          // v0.5.2 pins
    cfg.pin_mosi = 45;
    cfg.pin_miso = -1;
    cfg.pin_dc = 41;
    _bus_instance.config(cfg);
    _panel_instance.setBus(&_bus_instance);

    // Panel config
    auto pcfg = _panel_instance.config();
    pcfg.pin_cs = 42;
    pcfg.pin_rst = 39;          // v0.5.2: rst=39
    pcfg.pin_busy = -1;
    pcfg.memory_width = 240;
    pcfg.memory_height = 320;
    pcfg.panel_width = 240;
    pcfg.panel_height = 320;
    pcfg.offset_x = 0;
    pcfg.offset_y = 0;
    pcfg.offset_rotation = 0;
    pcfg.readable = true;
    pcfg.invert = true;
    pcfg.rgb_order = false;
    pcfg.dlen_16bit = false;
    pcfg.bus_shared = true;
    _panel_instance.config(pcfg);

    // Backlight
    auto bl = _light_instance.config();
    bl.pin_bl = 5;              // v0.5.2: GPIO 5
    bl.invert = false;
    bl.freq = 44100;
    bl.pwm_channel = 7;
    _light_instance.config(bl);
    _panel_instance.setLight(&_light_instance);

    setPanel(&_panel_instance);
  }
};
LGFX lcd;

JPEGDEC jpegdec;


uint16_t text_fg = TFT_WHITE;
uint16_t text_bg = TFT_BLACK;
bool text_transparent = false;
int text_size = 1;
int text_x = 0, text_y = 0;

// CONFIG system
String cfg_display = "";
String cfg_lastfile = "";
String cfg_lastmode = "";
String cfg_theme = "DEFAULT";   // active theme folder name
// cfg_log_enabled declared early (before SD_Log) — do not redeclare here

// WiFi config — AP (always-on hotspot)
bool   cfg_wifi_enabled = true;
String cfg_wifi_ssid    = "Gotek-Setup";
String cfg_wifi_pass    = "retrogaming";
uint8_t cfg_wifi_channel = 6;

// WiFi config — Client (connect to home network for internet)
bool   cfg_wifi_client_enabled = false;
String cfg_wifi_client_ssid    = "";
String cfg_wifi_client_pass    = "";

// Remote dongle config — send disk images to a WiFi Dongle instead of local USB
bool   cfg_remote_enabled  = false;
String cfg_remote_ssid     = "Gotek-Dongle";   // dongle's WiFi AP name
String cfg_remote_pass     = "retrogaming";     // dongle's WiFi password
String cfg_remote_host     = "192.168.4.1";     // dongle's IP (default AP gateway)
int    cfg_remote_port     = 80;

// WiFi state (defined here so drawInfoScreen can use them before webserver.h)
bool wifi_ap_active = false;
String wifi_ap_ip = "";
bool wifi_sta_connected = false;
String wifi_sta_ip = "";
bool isWiFiActive() { return wifi_ap_active; }

// Remote dongle state
bool remote_connected = false;
String remote_dongle_status = "";  // last status from dongle
String remote_dongle_file = "";    // currently loaded file on dongle

// Theme system
String theme_path = "/THEMES/DEFAULT";  // resolved path to active theme
vector<String> theme_list;              // available theme names

// RAM disk variables
#define RAM_DISK_SIZE (2880 * 512)
uint8_t *ram_disk = NULL;
const char *ram_mount_point = "/ramdisk";

// Disk mode
enum DiskMode { MODE_ADF = 0, MODE_DSK = 1 };
DiskMode g_mode = MODE_ADF;

// USB MSC
USBMSC msc;
uint32_t msc_block_count;

// UI state
enum Screen { SCR_SELECTION = 0, SCR_DETAILS = 1, SCR_INFO = 2 };
Screen current_screen = SCR_SELECTION;

// File list
vector<String> file_list;
vector<String> display_names;
int selected_index = 0;

// Game list (merged multi-disk entries)
struct GameEntry {
  String name;              // display name (game base name)
  String jpg_path;          // cover art path (empty if none)
  int first_file_index;     // index into file_list for disk 1 (or only disk)
  int disk_count;           // number of disks (1 = single disk game)
};
vector<GameEntry> game_list;
int game_selected = 0;      // selected index in game_list
int scroll_offset = 0;      // scroll offset for list view

// Details screen
String detail_filename = "";
String detail_nfo_text = "";
String detail_jpg_path = "";

// Multi-disk support
vector<int> disk_set;         // file_list indices for all disks of current game
int loaded_disk_index = -1;   // file_list index of currently loaded disk (-1 = none)

// Touch state
bool touch_available = false;
uint16_t last_touch_x = 0, last_touch_y = 0;
unsigned long last_touch_time = 0;

// Swipe tracking
bool touch_active = false;
uint16_t touch_start_x = 0, touch_start_y = 0;
uint16_t touch_last_x = 0, touch_last_y = 0;
unsigned long touch_start_time = 0;

// ============================================================================
// GRAPHICS LAYER
// ============================================================================


// Waveshare LovyanGFX wrapper functions
void gfx_fillScreen(uint16_t color) {
  lcd.fillScreen(color);
}

void gfx_drawPixel(int x, int y, uint16_t color) {
  lcd.drawPixel(x, y, color);
}

void gfx_fillRect(int x, int y, int w, int h, uint16_t color) {
  lcd.fillRect(x, y, w, h, color);
}

void gfx_drawRect(int x, int y, int w, int h, uint16_t color) {
  lcd.drawRect(x, y, w, h, color);
}

void gfx_fillCircle(int cx, int cy, int r, uint16_t color) {
  lcd.fillCircle(cx, cy, r, color);
}

void gfx_drawCircle(int cx, int cy, int r, uint16_t color) {
  lcd.drawCircle(cx, cy, r, color);
}

void gfx_setTextColor(uint16_t fg, uint16_t bg) {
  text_fg = fg;
  text_bg = bg;
  lcd.setTextColor(fg, bg);
}

// Text scale factor — reduce all sizes by ~20% for the 2.8" screen
// Size 1 stays 1.0, size 2 → 1.6, size 3 → 2.4
static const float TEXT_SCALE = 0.8f;

void gfx_setTextSize(int s) {
  text_size = s;
  float scaled = (s == 1) ? 1.0f : s * TEXT_SCALE;
  lcd.setTextSize(scaled);
}

void gfx_setCursor(int x, int y) {
  text_x = x;
  text_y = y;
  lcd.setCursor(x, y);
}

int gfx_fontHeight() {
  float scaled = (text_size == 1) ? 1.0f : text_size * TEXT_SCALE;
  return (int)(8 * scaled);
}

int gfx_textWidth(const String& text) {
  float scaled = (text_size == 1) ? 1.0f : text_size * TEXT_SCALE;
  return (int)(text.length() * 6 * scaled);
}

void gfx_print(const String& text) {
  lcd.print(text);
}

void gfx_flush() {
  // LovyanGFX writes directly, no-op
}

// JPEG decode-to-PSRAM callback
static uint16_t *jpeg_tmp_buf = NULL;
static int jpeg_tmp_w = 0;
static int jpeg_tmp_h = 0;

int jpeg_buf_cb(JPEGDRAW *pDraw) {
  if (!jpeg_tmp_buf) return 0;
  uint16_t *src = pDraw->pPixels;
  int dx = pDraw->x, dy = pDraw->y;
  int w  = pDraw->iWidth, h = pDraw->iHeight;
  int srcStride = w;
  for (int yy = 0; yy < h; yy++) {
    int row = dy + yy;
    if (row < 0 || row >= jpeg_tmp_h) continue;
    int copyW = w;
    if (dx < 0) continue;
    if (dx + copyW > jpeg_tmp_w) copyW = jpeg_tmp_w - dx;
    if (copyW <= 0) continue;
    memcpy(&jpeg_tmp_buf[row * jpeg_tmp_w + dx], &src[yy * srcStride], copyW * 2);
  }
  return 1;
}

void gfx_drawJpgFile(fs::FS &fs, const char* path, int x, int y, int maxW, int maxH) {
  LOG("JPG: path=[" + String(path) + "]");

  String vfsPath = String("/sdcard") + path;
  LOG("JPG: vfsPath=[" + vfsPath + "]");

  FILE *fp = fopen(vfsPath.c_str(), "rb");
  LOG("JPG: fopen=" + String(fp ? "OK" : "FAIL"));
  if (!fp) return;

  const size_t MAX_JPG = 512 * 1024;
  uint8_t *buf = (uint8_t *)ps_malloc(MAX_JPG);
  LOG("JPG: ps_malloc=" + String(buf ? "OK" : "FAIL"));
  if (!buf) { fclose(fp); return; }

  size_t sz = 0;
  uint8_t tmp[4096];
  size_t got;
  while ((got = fread(tmp, 1, sizeof(tmp), fp)) > 0) {
    if (sz + got > MAX_JPG) got = MAX_JPG - sz;
    memcpy(buf + sz, tmp, got);
    sz += got;
    if (sz >= MAX_JPG) break;
  }
  fclose(fp);
  LOG("JPG: read sz=" + String(sz));
  if (sz == 0) { free(buf); LOG("JPG: ABORT sz=0"); return; }

  // Decode to get dimensions first
  bool opened = jpegdec.openRAM(buf, sz, jpeg_buf_cb);
  LOG("JPG: openRAM=" + String(opened ? "OK" : "FAIL"));
  if (!opened) { free(buf); return; }

  int jpgW = jpegdec.getWidth();
  int jpgH = jpegdec.getHeight();
  LOG("JPG: dims=" + String(jpgW) + "x" + String(jpgH));

  if (jpgW <= 0 || jpgH <= 0 || jpgW > 4000 || jpgH > 4000) {
    jpegdec.close(); free(buf); LOG("JPG: ABORT bad dims"); return;
  }

  // Allocate full decode buffer in PSRAM
  jpeg_tmp_buf = (uint16_t *)ps_malloc(jpgW * jpgH * 2);
  LOG("JPG: decode buf=" + String(jpeg_tmp_buf ? "OK" : "FAIL"));
  if (!jpeg_tmp_buf) { jpegdec.close(); free(buf); return; }
  jpeg_tmp_w = jpgW;
  jpeg_tmp_h = jpgH;
  memset(jpeg_tmp_buf, 0, jpgW * jpgH * 2);

  jpegdec.decode(0, 0, 0);
  jpegdec.close();
  free(buf);
  LOG("JPG: decoded OK");

  // Scale to fit maxW x maxH maintaining aspect ratio
  float scaleX = (float)maxW / jpgW;
  float scaleY = (float)maxH / jpgH;
  float scale  = (scaleX < scaleY) ? scaleX : scaleY;
  if (scale > 1.0f) scale = 1.0f;

  int drawW = (int)(jpgW * scale);
  int drawH = (int)(jpgH * scale);
  int offX  = x + (maxW - drawW) / 2;
  int offY  = y + (maxH - drawH) / 2;
  LOG("JPG: drawing " + String(drawW) + "x" + String(drawH) + " at " + String(offX) + "," + String(offY));

  // Nearest-neighbour scale and push pixels row by row
  lcd.startWrite();
  for (int dy2 = 0; dy2 < drawH; dy2++) {
    int srcY = (int)(dy2 / scale);
    if (srcY >= jpgH) srcY = jpgH - 1;
    for (int dx2 = 0; dx2 < drawW; dx2++) {
      int srcX = (int)(dx2 / scale);
      if (srcX >= jpgW) srcX = jpgW - 1;
      uint16_t px = jpeg_tmp_buf[srcY * jpgW + srcX];
      // LovyanGFX writePixel handles byte order internally; pass raw JPEGDEC value
      lcd.writePixel(offX + dx2, offY + dy2, px);
    }
  }
  lcd.endWrite();

  free(jpeg_tmp_buf);
  jpeg_tmp_buf = NULL;
  LOG("JPG: done");
}


// ============================================================================
// DISPLAY INITIALIZATION (Backend-specific)
// ============================================================================


void displayInit() {
  lcd.init();
  lcd.setRotation(1);
  lcd.setBrightness(200);
}


// ============================================================================
// TOUCH INTERFACE (Backend-specific)
// ============================================================================


// Raw I2C CST328 driver — exact v0.5.2 implementation
#define CST328_SDA_PIN   1
#define CST328_SCL_PIN   3
#define CST328_INT_PIN   4
#define CST328_RST_PIN   2
#define CST328_ADDR      0x1A
#define I2C_TCH_FREQ_HZ  400000
#define CST328_REG_NUM   0xD005
#define CST328_REG_XY    0xD000
#define HYN_REG_DBG_MODE  0xD101
#define HYN_REG_NORM_MODE 0xD109
#define HYN_REG_BOOT_TIME 0xD1FC
#define CST328_MAX_POINTS 5
#define TOUCH_RAW_X_MIN  0
#define TOUCH_RAW_X_MAX  240
#define TOUCH_RAW_Y_MIN  0
#define TOUCH_RAW_Y_MAX  320
static bool TOUCH_SWAP_XY  = true;
static bool TOUCH_INVERT_X = false;
static bool TOUCH_INVERT_Y = true;

static TwoWire &WireT = Wire1;

static bool TCH_Write(uint16_t reg, const uint8_t *data, uint32_t len) {
  WireT.beginTransmission(CST328_ADDR);
  WireT.write((uint8_t)(reg >> 8)); WireT.write((uint8_t)reg);
  for (uint32_t i = 0; i < len; i++) WireT.write(data[i]);
  return WireT.endTransmission(true) == 0;
}
static bool TCH_Read(uint16_t reg, uint8_t *data, uint32_t len) {
  WireT.beginTransmission(CST328_ADDR);
  WireT.write((uint8_t)(reg >> 8)); WireT.write((uint8_t)reg);
  if (WireT.endTransmission(true) != 0) return false;
  if (WireT.requestFrom((int)CST328_ADDR, (int)len) != (int)len) return false;
  for (uint32_t i = 0; i < len; i++) data[i] = WireT.read();
  return true;
}
static void CST328_Reset() {
  pinMode(CST328_RST_PIN, OUTPUT);
  digitalWrite(CST328_RST_PIN, HIGH); delay(50);
  digitalWrite(CST328_RST_PIN, LOW);  delay(5);
  digitalWrite(CST328_RST_PIN, HIGH); delay(50);
}

struct cst328_state_t { uint8_t points; uint16_t rawX, rawY; };
static cst328_state_t gCST328 = {0, 0, 0};

static bool CST328_ReadFrame() {
  uint8_t cnt = 0;
  if (!TCH_Read(CST328_REG_NUM, &cnt, 1)) return false;
  uint8_t touches = cnt & 0x0F;
  if (touches == 0 || touches > CST328_MAX_POINTS) {
    uint8_t clr = 0; TCH_Write(CST328_REG_NUM, &clr, 1);
    gCST328.points = 0; return false;
  }
  uint8_t buf[32] = {0};
  if (!TCH_Read(CST328_REG_XY, &buf[1], 27)) return false;
  uint8_t clr = 0; TCH_Write(CST328_REG_NUM, &clr, 1);
  gCST328.points = touches;
  gCST328.rawX = ((uint16_t)buf[2] << 4) | ((buf[4] & 0xF0) >> 4);
  gCST328.rawY = ((uint16_t)buf[3] << 4) |  (buf[4] & 0x0F);
  return true;
}

void touchInit() {
  WireT.begin(CST328_SDA_PIN, CST328_SCL_PIN, I2C_TCH_FREQ_HZ);
  pinMode(CST328_INT_PIN, INPUT_PULLUP);
  CST328_Reset();
  uint8_t d = 0;
  TCH_Write(HYN_REG_DBG_MODE, nullptr, 0);
  TCH_Read(HYN_REG_BOOT_TIME, &d, 1);
  TCH_Write(HYN_REG_NORM_MODE, nullptr, 0);
  touch_available = true;
}

bool touchRead(uint16_t *x, uint16_t *y) {
  if (!CST328_ReadFrame() || gCST328.points == 0) return false;
  uint16_t rx = gCST328.rawX, ry = gCST328.rawY;
  uint16_t mx, my;
  if (TOUCH_SWAP_XY) {
    mx = (uint16_t)map((long)ry, TOUCH_RAW_Y_MIN, TOUCH_RAW_Y_MAX, 0, gW - 1);
    my = (uint16_t)map((long)rx, TOUCH_RAW_X_MIN, TOUCH_RAW_X_MAX, 0, gH - 1);
  } else {
    mx = (uint16_t)map((long)rx, TOUCH_RAW_X_MIN, TOUCH_RAW_X_MAX, 0, gW - 1);
    my = (uint16_t)map((long)ry, TOUCH_RAW_Y_MIN, TOUCH_RAW_Y_MAX, 0, gH - 1);
  }
  if (TOUCH_INVERT_X) mx = (gW - 1) - mx;
  if (TOUCH_INVERT_Y) my = (gH - 1) - my;
  *x = mx; *y = my;
  return true;
}


// ============================================================================
// CONFIG SYSTEM
// ============================================================================

void loadConfig() {
  cfg_display = "";
  cfg_lastfile = "";
  cfg_lastmode = "";

  File f = SD_MMC.open("/CONFIG.TXT", "r");
  if (!f) return;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    if (line.startsWith("#")) continue;  // skip comments

    int eqIdx = line.indexOf('=');
    if (eqIdx < 0) continue;

    String key = line.substring(0, eqIdx);
    String val = line.substring(eqIdx + 1);
    key.trim();
    val.trim();

    if (key == "DISPLAY") {
      cfg_display = val;
    } else if (key == "LASTFILE") {
      cfg_lastfile = val;
    } else if (key == "LASTMODE") {
      cfg_lastmode = val;
    } else if (key == "THEME") {
      cfg_theme = val;
    } else if (key == "LOG_ENABLED") {
      cfg_log_enabled = (val == "1" || val == "true");
    } else if (key == "WIFI_ENABLED") {
      cfg_wifi_enabled = (val == "1" || val == "true");
    } else if (key == "WIFI_SSID") {
      cfg_wifi_ssid = val;
    } else if (key == "WIFI_PASS") {
      cfg_wifi_pass = val;
    } else if (key == "WIFI_CHANNEL") {
      cfg_wifi_channel = (uint8_t)val.toInt();
      if (cfg_wifi_channel < 1 || cfg_wifi_channel > 13) cfg_wifi_channel = 6;
    } else if (key == "WIFI_CLIENT_ENABLED") {
      cfg_wifi_client_enabled = (val == "1" || val == "true");
    } else if (key == "WIFI_CLIENT_SSID") {
      cfg_wifi_client_ssid = val;
    } else if (key == "WIFI_CLIENT_PASS") {
      cfg_wifi_client_pass = val;
    } else if (key == "REMOTE_ENABLED") {
      cfg_remote_enabled = (val == "1" || val == "true");
    } else if (key == "REMOTE_SSID") {
      cfg_remote_ssid = val;
    } else if (key == "REMOTE_PASS") {
      cfg_remote_pass = val;
    } else if (key == "REMOTE_HOST") {
      cfg_remote_host = val;
    } else if (key == "REMOTE_PORT") {
      cfg_remote_port = val.toInt();
      if (cfg_remote_port <= 0) cfg_remote_port = 80;
    }
  }
  f.close();

  // Resolve theme path
  if (cfg_theme.length() == 0) cfg_theme = "DEFAULT";
  theme_path = "/THEMES/" + cfg_theme;
}

void saveConfig() {
  File f = SD_MMC.open("/CONFIG.TXT", "w");
  if (!f) return;

  if (cfg_display.length() > 0) {
    f.println("DISPLAY=" + cfg_display);
  }
  if (cfg_lastfile.length() > 0) {
    f.println("LASTFILE=" + cfg_lastfile);
  }
  if (cfg_lastmode.length() > 0) {
    f.println("LASTMODE=" + cfg_lastmode);
  }
  f.println("THEME=" + cfg_theme);
  f.println("LOG_ENABLED=" + String(cfg_log_enabled ? "1" : "0"));

  // WiFi settings
  f.println("WIFI_ENABLED=" + String(cfg_wifi_enabled ? "1" : "0"));
  f.println("WIFI_SSID=" + cfg_wifi_ssid);
  f.println("WIFI_PASS=" + cfg_wifi_pass);
  f.println("WIFI_CHANNEL=" + String(cfg_wifi_channel));
  f.println("WIFI_CLIENT_ENABLED=" + String(cfg_wifi_client_enabled ? "1" : "0"));
  if (cfg_wifi_client_ssid.length() > 0) {
    f.println("WIFI_CLIENT_SSID=" + cfg_wifi_client_ssid);
    f.println("WIFI_CLIENT_PASS=" + cfg_wifi_client_pass);
  }

  // Remote dongle settings
  f.println("REMOTE_ENABLED=" + String(cfg_remote_enabled ? "1" : "0"));
  if (cfg_remote_ssid.length() > 0) {
    f.println("REMOTE_SSID=" + cfg_remote_ssid);
    f.println("REMOTE_PASS=" + cfg_remote_pass);
  }
  f.println("REMOTE_HOST=" + cfg_remote_host);
  f.println("REMOTE_PORT=" + String(cfg_remote_port));

  f.close();
}

// Scan /THEMES/ folder for available theme subfolders
void scanThemes() {
  theme_list.clear();
  File root = SD_MMC.open("/THEMES");
  if (!root || !root.isDirectory()) {
    LOG("scanThemes: /THEMES not found or not a directory");
    theme_list.push_back("DEFAULT");
    return;
  }
  File entry;
  while ((entry = root.openNextFile())) {
    if (entry.isDirectory()) {
      String name = entry.name();
      int lastSlash = name.lastIndexOf('/');
      if (lastSlash >= 0) name = name.substring(lastSlash + 1);
      if (name.length() > 0 && !name.startsWith(".")) {
        LOG("scanThemes: found theme [" + name + "]");
        theme_list.push_back(name);
      }
    }
    entry.close();
  }
  root.close();
  // Sort alphabetically
  for (int i = 0; i < (int)theme_list.size() - 1; i++) {
    for (int j = i + 1; j < (int)theme_list.size(); j++) {
      if (theme_list[j] < theme_list[i]) {
        String tmp = theme_list[i];
        theme_list[i] = theme_list[j];
        theme_list[j] = tmp;
      }
    }
  }
  if (theme_list.empty()) {
    theme_list.push_back("DEFAULT");
  }
}

// Switch to the next available theme
void cycleTheme() {
  if (theme_list.empty()) scanThemes();
  int idx = 0;
  for (int i = 0; i < (int)theme_list.size(); i++) {
    if (theme_list[i] == cfg_theme) { idx = i; break; }
  }
  idx = (idx + 1) % theme_list.size();
  cfg_theme = theme_list[idx];
  theme_path = "/THEMES/" + cfg_theme;
  saveConfig();
}

// ============================================================================
// SD CARD INTERFACE
// ============================================================================

void init_sd_card() {
  SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0);
  if (!SD_MMC.begin("/sdcard", true)) {
    LOG("SD card mount failed!");
  }
}

// ============================================================================
// First-boot SD scaffold
// If CONFIG.TXT is missing we assume the card is blank/new and create the
// full folder structure plus a commented default CONFIG.TXT as a guide.
// ============================================================================
void firstBootScaffold() {
  // Already set up if CONFIG.TXT exists
  if (SD_MMC.exists("/CONFIG.TXT")) return;

  LOG("First boot — creating SD folder structure...");

  // Core game folders
  SD_MMC.mkdir("/ADF");
  SD_MMC.mkdir("/DSK");

  // Theme folders
  SD_MMC.mkdir("/THEMES");
  SD_MMC.mkdir("/THEMES/DEFAULT");

  // README in each game folder so they're not confusing when empty
  auto writeReadme = [](const char *path, const char *text) {
    File f = SD_MMC.open(path, FILE_WRITE);
    if (f) { f.print(text); f.close(); }
  };

  writeReadme("/ADF/README.TXT",
    "Place Amiga disk images here.\r\n"
    "Use subfolders per game:\r\n"
    "\r\n"
    "  /ADF/Speedball 2/Speedball 2.adf\r\n"
    "  /ADF/Speedball 2/Speedball 2.jpg   (optional cover art)\r\n"
    "  /ADF/Speedball 2/Speedball 2.nfo   (optional info text)\r\n"
    "\r\n"
    "Multi-disk games:\r\n"
    "  /ADF/Monkey Island/Monkey Island - Disk 1.adf\r\n"
    "  /ADF/Monkey Island/Monkey Island - Disk 2.adf\r\n"
  );

  writeReadme("/DSK/README.TXT",
    "Place CPC/ZX Spectrum disk images here.\r\n"
    "Use subfolders per game:\r\n"
    "\r\n"
    "  /DSK/Dizzy/Dizzy.dsk\r\n"
    "  /DSK/Dizzy/Dizzy.jpg   (optional cover art)\r\n"
    "  /DSK/Dizzy/Dizzy.nfo   (optional info text)\r\n"
  );

  writeReadme("/THEMES/DEFAULT/README.TXT",
    "The AMIGA_WB2 theme is bundled and written automatically on first boot.\r\n"
    "To create your own theme, make a new subfolder here e.g. /THEMES/MYTHEME/\r\n"
    "\r\n"
    "Required filenames:\r\n"
    "  BTN_UP.png      BTN_DOWN.png    BTN_INFO.png\r\n"
    "  BTN_BACK.png    BTN_LOAD.png    BTN_UNLOAD.png\r\n"
    "  BTN_THEME.png   BTN_ADF.png     BTN_DSK.png\r\n"
    "\r\n"
    "PNGs can be any size — they will be clipped to fit the button slot.\r\n"
    "RGBA transparency is supported.\r\n"
    "\r\n"
    "Optional:\r\n"
    "  BANNER.JPG      — header image for ADF list screen\r\n"
    "  DSKBANNER.JPG   — header image for DSK list screen\r\n"
  );

  // Write default theme PNGs from embedded PROGMEM data
  SD_MMC.mkdir("/THEMES/AMIGA_WB2");
  LOG("Writing default theme files...");
  for (int i = 0; i < default_theme_files_count; i++) {
    const DefaultThemeFile &tf = default_theme_files[i];
    File f = SD_MMC.open(tf.filename, FILE_WRITE);
    if (f) {
      const size_t CHUNK = 512;
      size_t written = 0;
      uint8_t buf[CHUNK];
      while (written < tf.len) {
        size_t n = min(CHUNK, tf.len - written);
        memcpy_P(buf, tf.data + written, n);
        f.write(buf, n);
        written += n;
      }
      f.close();
      LOG("  wrote " + String(tf.filename));
    } else {
      LOG("  FAIL: " + String(tf.filename));
    }
  }

  // Set active theme to the bundled one
  cfg_theme = "AMIGA_WB2";
  theme_path = "/THEMES/AMIGA_WB2";

  // Write default CONFIG.TXT with all options documented
  File cfg = SD_MMC.open("/CONFIG.TXT", FILE_WRITE);
  if (cfg) {
    cfg.print(
      "# Gotek Touchscreen Interface — Configuration\r\n"
      "# Edit this file on your PC, then reinsert the SD card.\r\n"
      "# Lines starting with # are comments and are ignored.\r\n"
      "\r\n"
      "# --- Display ---\r\n"
      "# DISPLAY=WAVESHARE   (auto-detected, do not change)\r\n"
      "\r\n"
      "# --- Theme ---\r\n"
      "# Folder name inside /THEMES/ to use as the active theme.\r\n"
      "THEME=AMIGA_WB2\r\n"
      "\r\n"
      "# --- Mode ---\r\n"
      "# LASTMODE=ADF   or   LASTMODE=DSK\r\n"
      "LASTMODE=ADF\r\n"
      "\r\n"
      "# --- WiFi Access Point ---\r\n"
      "# The Gotek creates its own hotspot for web-based disk management.\r\n"
      "WIFI_ENABLED=1\r\n"
      "WIFI_SSID=Gotek-Setup\r\n"
      "WIFI_PASS=retrogaming\r\n"
      "WIFI_CHANNEL=6\r\n"
      "\r\n"
      "# --- WiFi Client (connect to your home network) ---\r\n"
      "WIFI_CLIENT_ENABLED=0\r\n"
      "# WIFI_CLIENT_SSID=YourNetwork\r\n"
      "# WIFI_CLIENT_PASS=YourPassword\r\n"
      "\r\n"
      "# --- Remote Dongle ---\r\n"
      "REMOTE_ENABLED=0\r\n"
      "# REMOTE_SSID=GoitekDongle\r\n"
      "# REMOTE_PASS=dongle123\r\n"
      "# REMOTE_HOST=192.168.5.1\r\n"
      "# REMOTE_PORT=8080\r\n"
      "\r\n"
      "# --- SD Logging ---\r\n"
      "# Set to 1 to write debug log to /LOG.TXT on the SD card.\r\n"
      "LOG_ENABLED=0\r\n"
    );
    cfg.close();
    LOG("First boot scaffold complete.");
  } else {
    LOG("First boot: failed to write CONFIG.TXT!");
  }
}

// ============================================================================
// File system scanning
// ============================================================================
// Folder structure:
//   /ADF/<GameName>/<GameName>.adf   (+ optional .nfo, .jpg)
//   /DSK/<GameName>/<GameName>.dsk   (+ optional .nfo, .jpg)
//
// file_list stores full paths like "/ADF/Giganoid/Giganoid.adf"
// Also supports legacy flat layout: /*.adf / *.dsk in root
// ============================================================================

// Find a file by name (case-insensitive) inside a given directory.
// Returns the full path if found, empty string otherwise.
String findFileInDir(const String &dirPath, const String &targetName) {
  File dir = SD_MMC.open(dirPath.c_str());
  if (!dir || !dir.isDirectory()) return "";

  String targetUpper = targetName;
  targetUpper.toUpperCase();

  File entry;
  while ((entry = dir.openNextFile())) {
    String fname = entry.name();
    // entry.name() may return full path on some ESP32 cores
    int slash = fname.lastIndexOf('/');
    if (slash >= 0) fname = fname.substring(slash + 1);

    String upper = fname;
    upper.toUpperCase();
    if (upper == targetUpper) {
      entry.close();
      dir.close();
      return dirPath + "/" + fname;
    }
    entry.close();
  }
  dir.close();
  return "";
}

// List disk images by scanning /<MODE>/ subfolders.
// Each subfolder represents one game/program.
// Falls back to flat root scanning for legacy layout.
vector<String> listImages() {
  vector<String> images;
  String modeDir = (g_mode == MODE_ADF) ? "/ADF" : "/DSK";
  String ext1 = (g_mode == MODE_ADF) ? ".ADF" : ".DSK";

  File root = SD_MMC.open(modeDir.c_str());
  if (root && root.isDirectory()) {
    File gameDir;
    while ((gameDir = root.openNextFile())) {
      String entryName = gameDir.name();
      // Ensure full path
      if (!entryName.startsWith("/")) entryName = modeDir + "/" + entryName;

      if (gameDir.isDirectory()) {
        // Subfolder layout: /<MODE>/<GameFolder>/<file>.adf|dsk
        File entry;
        while ((entry = gameDir.openNextFile())) {
          String fname = entry.name();
          int slash = fname.lastIndexOf('/');
          if (slash >= 0) fname = fname.substring(slash + 1);

          String upper = fname;
          upper.toUpperCase();
          if (upper.endsWith(ext1) || upper.endsWith(".IMG")) {
            String fullPath = entryName + "/" + fname;
            if (!fullPath.startsWith("/")) fullPath = "/" + fullPath;
            images.push_back(fullPath);
          }
          entry.close();
        }
      } else {
        // Flat layout: /<MODE>/<file>.adf|dsk (no subfolder)
        String fname = entryName;
        int slash = fname.lastIndexOf('/');
        if (slash >= 0) fname = fname.substring(slash + 1);

        String upper = fname;
        upper.toUpperCase();
        if (upper.endsWith(ext1) || upper.endsWith(".IMG")) {
          images.push_back(entryName);
        }
      }
      gameDir.close();
    }
    root.close();
  }

  // Fallback: also scan root for flat layout (legacy compatibility)
  File rootDir = SD_MMC.open("/");
  if (rootDir) {
    File entry;
    while ((entry = rootDir.openNextFile())) {
      if (entry.isDirectory()) { entry.close(); continue; }
      String fname = entry.name();
      int slash = fname.lastIndexOf('/');
      if (slash >= 0) fname = fname.substring(slash + 1);
      String upper = fname;
      upper.toUpperCase();
      if (upper.endsWith(ext1) || upper.endsWith(".IMG")) {
        String fullPath = "/" + fname;
        // Avoid duplicates
        bool dup = false;
        for (const auto &existing : images) {
          if (existing == fullPath) { dup = true; break; }
        }
        if (!dup) images.push_back(fullPath);
      }
      entry.close();
    }
    rootDir.close();
  }

  sort(images.begin(), images.end());
  return images;
}

// Forward declaration (used by findNFOFor/findJPGFor before definition)
String getGameBaseName(const String &fullPath);
void handleTap(uint16_t px, uint16_t py);
void handleSwipe(int16_t dx, int16_t dy, uint16_t startX, uint16_t startY);

// Get the parent directory of a file path
String parentDir(const String &path) {
  int slash = path.lastIndexOf('/');
  if (slash <= 0) return "/";
  return path.substring(0, slash);
}

// Get just the filename from a full path
String filenameOnly(const String &path) {
  int slash = path.lastIndexOf('/');
  if (slash < 0) return path;
  return path.substring(slash + 1);
}

// Find the .nfo file for a given disk image (full path).
// Looks in the same directory as the image file.
// For multi-disk games also tries the game base name.
String findNFOFor(const String &imagePath) {
  String dir = parentDir(imagePath);
  String base = filenameOnly(imagePath);
  base = base.substring(0, base.lastIndexOf('.'));

  // Try <base>.nfo in the same folder
  String result = findFileInDir(dir, base + ".nfo");
  if (result.length() > 0) return result;

  // For multi-disk files, try the game base name
  String gameName = getGameBaseName(imagePath);
  if (gameName != base) {
    result = findFileInDir(dir, gameName + ".nfo");
    if (result.length() > 0) return result;
  }

  // Also try just "info.nfo" or "readme.nfo"
  result = findFileInDir(dir, "info.nfo");
  if (result.length() > 0) return result;
  return "";
}

// Find the cover image for a given disk image (full path).
// Looks in the same directory as the image file.
// For multi-disk games (GameName-1.adf) also tries the base game name (GameName.jpg).
String findJPGFor(const String &imagePath) {
  String dir = parentDir(imagePath);
  String base = filenameOnly(imagePath);
  base = base.substring(0, base.lastIndexOf('.'));

  // Try <base>.jpg, .jpeg, .png in the same folder
  for (const char *ext : {".jpg", ".jpeg", ".png"}) {
    String result = findFileInDir(dir, base + ext);
    if (result.length() > 0) return result;
  }

  // For multi-disk files, try the game base name (without -N suffix)
  String gameName = getGameBaseName(imagePath);
  if (gameName != base) {
    for (const char *ext : {".jpg", ".jpeg", ".png"}) {
      String result = findFileInDir(dir, gameName + ext);
      if (result.length() > 0) return result;
    }
  }

  // Also try generic names
  for (const char *name : {"cover.jpg", "cover.png", "art.jpg"}) {
    String result = findFileInDir(dir, name);
    if (result.length() > 0) return result;
  }
  return "";
}

// Find banner image (in root or mode folder)
String findBannerImage() {
  // First try theme-specific banner
  String result = findFileInDir(theme_path, "BANNER.JPG");
  if (result.length() > 0) return result;
  result = findFileInDir(theme_path, "BANNER_ADF.JPG");
  if (result.length() > 0) return result;
  // Fallback to global banner
  result = findFileInDir("/", "BANNER.JPG");
  if (result.length() > 0) return result;
  result = findFileInDir("/ADF", "BANNER.JPG");
  return result;
}

String findDSKBanner() {
  // First try theme-specific DSK banner
  String result = findFileInDir(theme_path, "DSKBANNER.JPG");
  if (result.length() > 0) return result;
  result = findFileInDir(theme_path, "BANNER_DSK.JPG");
  if (result.length() > 0) return result;
  result = findFileInDir(theme_path, "BANNER.JPG");
  if (result.length() > 0) return result;
  // Fallback to global banner
  result = findFileInDir("/", "DSKBANNER.JPG");
  if (result.length() > 0) return result;
  result = findFileInDir("/DSK", "BANNER.JPG");
  return result;
}

String readSmallTextFile(const char *path, int maxSize = 2048) {
  File f = SD_MMC.open(path, "r");
  if (!f) return "";

  String result = "";
  while (f.available() && result.length() < maxSize) {
    result += (char)f.read();
  }
  f.close();
  return result;
}

// ============================================================================
// NFO PARSING
// ============================================================================

void parseNFO(const String &nfoText, String &title, String &blurb) {
  title = "";
  blurb = "";

  int lines = 0;
  int pos = 0;
  while (pos < (int)nfoText.length() && lines < 2) {
    int eol = nfoText.indexOf('\n', pos);
    if (eol < 0) eol = nfoText.length();
    String line = nfoText.substring(pos, eol);
    line.trim();
    if (line.length() > 0) {
      if (lines == 0) {
        title = line;
      } else {
        blurb = line;
      }
      lines++;
    }
    pos = eol + 1;
  }
}

String basenameNoExt(const String &path) {
  int lastSlash = path.lastIndexOf('/');
  int lastDot = path.lastIndexOf('.');
  if (lastSlash < 0) lastSlash = -1;
  if (lastDot < 0) lastDot = path.length();
  return path.substring(lastSlash + 1, lastDot);
}

// Multi-disk helpers: detect "GameName-1.adf", "GameName-2.adf" series
String getGameBaseName(const String &fullPath) {
  String base = basenameNoExt(filenameOnly(fullPath));
  // Strip trailing "-N" if present (e.g., "MonkeyIsland-1" -> "MonkeyIsland")
  int dash = base.lastIndexOf('-');
  if (dash > 0 && dash < (int)base.length() - 1) {
    String suffix = base.substring(dash + 1);
    bool isNum = true;
    for (int i = 0; i < (int)suffix.length(); i++) {
      if (!isDigit(suffix[i])) { isNum = false; break; }
    }
    if (isNum) return base.substring(0, dash);
  }
  return base;
}

int getDiskNumber(const String &fullPath) {
  String base = basenameNoExt(filenameOnly(fullPath));
  int dash = base.lastIndexOf('-');
  if (dash > 0 && dash < (int)base.length() - 1) {
    String suffix = base.substring(dash + 1);
    int num = suffix.toInt();
    if (num > 0) return num;
  }
  return 0;  // not part of a numbered set
}

// Find all disks belonging to the same game, in the same folder
void findRelatedDisks(int currentIndex) {
  disk_set.clear();
  if (currentIndex < 0 || currentIndex >= (int)file_list.size()) return;

  String baseName = getGameBaseName(file_list[currentIndex]);
  String dir = parentDir(file_list[currentIndex]);

  for (int i = 0; i < (int)file_list.size(); i++) {
    if (parentDir(file_list[i]) == dir &&
        getGameBaseName(file_list[i]) == baseName &&
        getDiskNumber(file_list[i]) > 0) {
      disk_set.push_back(i);
    }
  }
  // Sort by disk number
  for (int i = 0; i < (int)disk_set.size(); i++) {
    for (int j = i + 1; j < (int)disk_set.size(); j++) {
      if (getDiskNumber(file_list[disk_set[j]]) < getDiskNumber(file_list[disk_set[i]])) {
        swap(disk_set[i], disk_set[j]);
      }
    }
  }
}

String getOutputFilename() {
  if (selected_index >= 0 && selected_index < (int)file_list.size()) {
    return filenameOnly(file_list[selected_index]);
  }
  return (g_mode == MODE_ADF) ? "DEFAULT.ADF" : "DEFAULT.DSK";
}

// ============================================================================
// FAT12 FILESYSTEM EMULATION
// ============================================================================

void build_boot_sector(uint8_t *buf) {
  memset(buf, 0, 512);
  buf[0x00] = 0xEB; buf[0x01] = 0x3C; buf[0x02] = 0x90;
  memcpy(&buf[0x03], "MSDOS5.0", 8);
  *(uint16_t *)&buf[0x0B] = 512;
  buf[0x0D] = 1;
  *(uint16_t *)&buf[0x0E] = 1;
  buf[0x10] = 2;
  *(uint16_t *)&buf[0x11] = 224;
  *(uint16_t *)&buf[0x13] = 2880;
  buf[0x15] = 0xF0;
  *(uint16_t *)&buf[0x16] = 9;
  *(uint16_t *)&buf[0x18] = 18;
  *(uint16_t *)&buf[0x1A] = 2;
  *(uint32_t *)&buf[0x20] = 0;
  buf[0x24] = 0x00;
  buf[0x25] = 0x00;
  buf[0x26] = 0x29;
  // Randomize volume serial so Windows doesn't use cached FAT
  uint32_t serial = (uint32_t)millis() ^ (uint32_t)esp_random();
  *(uint32_t *)&buf[0x27] = serial;
  memcpy(&buf[0x2B], "GOTEK      ", 11);
  memcpy(&buf[0x36], "FAT12   ", 8);
  buf[510] = 0x55;
  buf[511] = 0xAA;
}

void fat12_set(uint8_t *fat, int idx, uint16_t val) {
  if (idx % 2 == 0) {
    fat[idx * 3 / 2] = val & 0xFF;
    fat[idx * 3 / 2 + 1] = (fat[idx * 3 / 2 + 1] & 0xF0) | ((val >> 8) & 0x0F);
  } else {
    fat[idx * 3 / 2] = (fat[idx * 3 / 2] & 0x0F) | ((val & 0x0F) << 4);
    fat[idx * 3 / 2 + 1] = (val >> 4) & 0xFF;
  }
}

void build_fat(uint8_t *fat) {
  memset(fat, 0, 4608);
  fat12_set(fat, 0, 0xFF0);
  fat12_set(fat, 1, 0xFFF);
  // Cluster 2+ left as 0x000 (free) until a file is loaded
}

void make_83_name(const char *src, uint8_t *dst) {
  memset(dst, ' ', 11);
  // Find last dot for extension
  const char *dot = strrchr(src, '.');
  int nameLen = dot ? (int)(dot - src) : (int)strlen(src);
  // Copy name part (max 8 chars)
  for (int i = 0, j = 0; i < nameLen && j < 8; i++) {
    dst[j++] = toupper(src[i]);
  }
  // Copy extension (max 3 chars)
  if (dot) {
    dot++;
    for (int j = 8; *dot && j < 11; dot++) {
      dst[j++] = toupper(*dot);
    }
  }
}

void build_root(uint8_t *root) {
  memset(root, 0, 7168); // 14 sectors × 512 = 7168 bytes for 224 entries
  uint8_t fname[11];
  make_83_name(getOutputFilename().c_str(), fname);
  memcpy(&root[0], fname, 11);
  root[11] = 0x20;             // Archive attribute
  *(uint16_t *)&root[26] = 0;  // Start cluster = 0 (no data yet)
  *(uint32_t *)&root[28] = 0;  // File size = 0
}

// FAT12 layout for 1.44MB floppy (2880 sectors):
// Sector 0:      Boot sector          → offset 0
// Sectors 1-9:   FAT1 (9 sectors)     → offset 512
// Sectors 10-18: FAT2 (9 sectors)     → offset 5120
// Sectors 19-32: Root dir (14 sectors) → offset 9728
// Sectors 33+:   Data area            → offset 16896
#define FAT1_OFFSET   512
#define FAT2_OFFSET   5120
#define ROOTDIR_OFFSET 9728
#define DATA_OFFSET   16896

void build_volume_with_file() {
  memset(ram_disk, 0, RAM_DISK_SIZE);
  build_boot_sector(&ram_disk[0]);
  build_fat(&ram_disk[FAT1_OFFSET]);
  build_fat(&ram_disk[FAT2_OFFSET]);
  build_root(&ram_disk[ROOTDIR_OFFSET]);

  msc_block_count = RAM_DISK_SIZE / 512;
}

// Bump SCSI inquiry revision so Windows sees a new device and re-reads the disk
static uint32_t g_rev_counter = 1;
void bumpInquiryRevision() {
  char rev[8];
  snprintf(rev, sizeof(rev), "%lu", (unsigned long)g_rev_counter++);
  msc.productRevision(rev);
}

static int32_t onRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
  uint32_t addr = lba * 512 + offset;
  if (ram_disk && addr + bufsize <= RAM_DISK_SIZE) {
    memcpy(buffer, &ram_disk[addr], bufsize);
    return bufsize;
  }
  return -1;
}

static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
  uint32_t addr = lba * 512 + offset;
  if (ram_disk && addr + bufsize <= RAM_DISK_SIZE) {
    memcpy(&ram_disk[addr], buffer, bufsize);
    return bufsize;
  }
  return -1;
}

// ============================================================================
// UI FUNCTIONS
// ============================================================================

void uiInit() {
  displayInit();
}

void uiClr() {
  gfx_fillScreen(TFT_BLACK);
}

// ── Amiga Workbench 2.x theme colors ──
// These are used when no PNG theme images are present
#define WB_GREY      0xAD55   // RGB565 for (170,170,170)
#define WB_BLUE      0x02B5   // RGB565 for (0,85,170)
#define WB_LIGHT     0xFFFF   // white highlight
#define WB_MED_GREY  0x7BEF   // medium grey shadow
#define WB_ORANGE    0xFC40   // RGB565 for (255,136,0)

void uiLine(int y, uint16_t c) {
  gfx_fillRect(0, y, gW, 1, c);
}

// Draw Amiga-style loading screen with themed progress bar
void drawThemedLoadingScreen(const String &filename) {
  gfx_fillScreen(TFT_BLACK);

  // Title
  gfx_setTextColor(TFT_CYAN, TFT_BLACK);
  gfx_setTextSize(2);
  String lt = "LOADING";
  gfx_setCursor((gW - gfx_textWidth(lt)) / 2, 50);
  gfx_print(lt);

  // Filename
  gfx_setTextColor(TFT_WHITE, TFT_BLACK);
  gfx_setTextSize(2);
  int fnW = gfx_textWidth(filename);
  gfx_setCursor((gW - fnW) / 2, 80);
  gfx_print(filename);

  // Reading indicator
  gfx_setTextColor(WB_ORANGE, TFT_BLACK);
  gfx_setTextSize(2);
  gfx_setCursor((gW - gfx_textWidth("[ READING DISK ]")) / 2, 110);
  gfx_print("[ READING DISK ]");

  // Progress bar frame (Amiga 3D bevel)
  int barX = 40, barY = 160, barW = gW - 80, barH = 26;
  // Outer shadow (dark bottom-right)
  gfx_fillRect(barX + 1, barY + barH, barW, 1, WB_MED_GREY);
  gfx_fillRect(barX + barW, barY + 1, 1, barH, WB_MED_GREY);
  // Outer highlight (white top-left)
  gfx_fillRect(barX, barY, barW, 1, WB_LIGHT);
  gfx_fillRect(barX, barY, 1, barH, WB_LIGHT);
  // Inner recessed area
  gfx_fillRect(barX + 1, barY + 1, barW - 2, barH - 2, 0x1082); // very dark grey

  gfx_flush();
}

// Update the progress bar fill during loading
void drawThemedProgressBar(int pct) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  int barX = 40, barY = 160, barW = gW - 80, barH = 26;
  int innerW = barW - 4;
  int fillW = (innerW * pct) / 100;
  if (fillW > innerW) fillW = innerW;  // clamp to prevent overflow
  if (fillW > 0) {
    // Amiga-style green fill with slight gradient
    gfx_fillRect(barX + 2, barY + 2, fillW, barH - 4, 0x07E0); // bright green
    // Highlight on top of fill
    gfx_fillRect(barX + 2, barY + 2, fillW, 1, 0x47E8);  // lighter green
  }
  // Percentage text
  gfx_setTextColor(TFT_WHITE, 0x1082);
  gfx_setTextSize(2);
  String pctStr = String(pct) + "%";
  int tw = gfx_textWidth(pctStr);
  gfx_setCursor(barX + (barW - tw) / 2, barY + 5);
  gfx_print(pctStr);
  gfx_flush();
}

void uiSection(int x, int y, const String &title, uint16_t color) {
  gfx_setTextColor(color, TFT_BLACK);
  gfx_setTextSize(1);
  gfx_setCursor(x, y);
  gfx_print(title);
}

void uiOK(int x, int y) {
  gfx_setTextColor(TFT_GREEN, TFT_BLACK);
  gfx_setTextSize(1);
  gfx_setCursor(x, y);
  gfx_print("[OK]");
}

void uiERR(int x, int y, const String &msg) {
  gfx_setTextColor(TFT_RED, TFT_BLACK);
  gfx_setTextSize(1);
  gfx_setCursor(x, y);
  gfx_print(msg);
}

void drawWrappedText(const String &text, int x, int y, int maxWidth, uint16_t color) {
  gfx_setTextColor(color, TFT_BLACK);
  gfx_setTextSize(1);

  int lineY = y;
  String line = "";
  for (char c : text) {
    if (c == '\n') {
      gfx_setCursor(x, lineY);
      gfx_print(line);
      line = "";
      lineY += 10;
    } else {
      line += c;
      if (gfx_textWidth(line) >= maxWidth) {
        gfx_setCursor(x, lineY);
        gfx_print(line);
        line = "";
        lineY += 10;
      }
    }
  }
  if (line.length() > 0) {
    gfx_setCursor(x, lineY);
    gfx_print(line);
  }
}

void drawWrappedTextBG(const String &text, int x, int y, int maxWidth,
                       uint16_t fg, uint16_t bg) {
  gfx_setTextColor(fg, bg);
  gfx_setTextSize(1);

  int lineY = y;
  String line = "";
  for (char c : text) {
    if (c == '\n') {
      int w = gfx_textWidth(line);
      gfx_fillRect(x, lineY, w, 8, bg);
      gfx_setCursor(x, lineY);
      gfx_print(line);
      line = "";
      lineY += 10;
    } else {
      line += c;
      if (gfx_textWidth(line) >= maxWidth) {
        int w = gfx_textWidth(line);
        gfx_fillRect(x, lineY, w, 8, bg);
        gfx_setCursor(x, lineY);
        gfx_print(line);
        line = "";
        lineY += 10;
      }
    }
  }
  if (line.length() > 0) {
    int w = gfx_textWidth(line);
    gfx_fillRect(x, lineY, w, 8, bg);
    gfx_setCursor(x, lineY);
    gfx_print(line);
  }
}

// ============================================================================
// THEME SYSTEM — Load button images from /THEMES/<name>/ on SD card
// ============================================================================
// Theme system — PNG buttons with alpha blending.
// Folder structure: /THEMES/<theme_name>/
//   BTN_INFO.png, BTN_UP.png, BTN_DOWN.png        — list page
//   BTN_BACK.png, BTN_LOAD.png, BTN_UNLOAD.png    — detail page
//   BTN_THEME.png, BTN_ADF.png, BTN_DSK.png       — info page
//
// Active theme is set via THEME= in CONFIG.TXT or via INFO screen.
// PNG files can be 32-bit RGBA (with transparency) or 24-bit RGB.
// If a PNG is missing, a simple drawn fallback button is used.
// ============================================================================

static PNG png;
static int png_draw_x = 0;   // top-left X for current PNG draw
static int png_draw_y = 0;   // top-left Y for current PNG draw

// Alpha-blend a single pixel: fg over bg
static inline uint16_t alphaBlend565(uint16_t bg, uint16_t fg, uint8_t alpha) {
  if (alpha == 255) return fg;
  if (alpha == 0)   return bg;
  uint8_t bgR = (bg >> 11) & 0x1F;
  uint8_t bgG = (bg >> 5)  & 0x3F;
  uint8_t bgB =  bg        & 0x1F;
  uint8_t fgR = (fg >> 11) & 0x1F;
  uint8_t fgG = (fg >> 5)  & 0x3F;
  uint8_t fgB =  fg        & 0x1F;
  uint8_t inv = 255 - alpha;
  uint8_t rr = (fgR * alpha + bgR * inv + 127) / 255;
  uint8_t gg = (fgG * alpha + bgG * inv + 127) / 255;
  uint8_t bb = (fgB * alpha + bgB * inv + 127) / 255;
  return (rr << 11) | (gg << 5) | bb;
}

// Does the current PNG have alpha? (set after png.open)
static bool png_has_alpha = false;

// PNGdec draw callback — called for each scanline of decoded pixels.
// Must return 1 to continue decoding, 0 to abort.
int pngDrawCB(PNGDRAW *pDraw) {
  uint16_t lineBuffer[500];
  int w = pDraw->iWidth;
  if (w > 500) w = 500;

  // Convert to native-endian RGB565 (our gfx functions handle byte-swap)
  png.getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_LITTLE_ENDIAN, 0x00000000);

  int drawY = png_draw_y + pDraw->y;
  if (drawY < 0 || drawY >= gH) return 1;

  if (png_has_alpha) {
    // Use PNGdec's alpha mask (1-bit per pixel, threshold 128)
    uint8_t alphaMask[64];  // ceil(500/8) = 63 bytes
    png.getAlphaMask(pDraw, alphaMask, 128);

    for (int i = 0; i < w; i++) {
      int byteIdx = i >> 3;
      int bitIdx  = 7 - (i & 7);
      if (!(alphaMask[byteIdx] & (1 << bitIdx))) continue;  // transparent pixel
      int drawX = png_draw_x + i;
      if (drawX < 0 || drawX >= gW) continue;
      gfx_drawPixel(drawX, drawY, lineBuffer[i]);
    }
  } else {
    // No alpha: draw all pixels directly
    for (int i = 0; i < w; i++) {
      int drawX = png_draw_x + i;
      if (drawX < 0 || drawX >= gW) continue;
      gfx_drawPixel(drawX, drawY, lineBuffer[i]);
    }
  }
  return 1;
}

// File I/O callbacks for PNGdec (reads from SD via File object)
static File pngFile;

void *pngOpen(const char *filename, int32_t *pFileSize) {
  pngFile.close();  // close any previously open file
  pngFile = SD_MMC.open(filename, "r");
  if (!pngFile) return NULL;
  *pFileSize = pngFile.size();
  return &pngFile;
}
void pngClose(void *pHandle) {
  if (pHandle) ((File *)pHandle)->close();
}
int32_t pngRead(PNGFILE *pFile, uint8_t *pBuf, int32_t iLen) {
  return pngFile.read(pBuf, iLen);
}
int32_t pngSeek(PNGFILE *pFile, int32_t iPosition) {
  return pngFile.seek(iPosition);
}

// Draw a PNG from SD at position (x,y) with alpha blending.
// Returns true if drawn, false if file missing or invalid.
bool drawPngFile(const char *path, int x, int y) {
  int rc = png.open(path, pngOpen, pngClose, pngRead, pngSeek, pngDrawCB);
  if (rc != PNG_SUCCESS) {
    LOG("PNG open FAIL: " + String(path) + " rc=" + String(rc));
    return false;
  }

  png_draw_x = x;
  png_draw_y = y;
  png_has_alpha = png.hasAlpha();
  rc = png.decode(NULL, 0);
  png.close();
  if (rc != PNG_SUCCESS) {
    LOG("PNG decode FAIL: " + String(path) + " rc=" + String(rc));
  }
  return (rc == PNG_SUCCESS);
}

// Get dimensions of a PNG file without drawing it.
bool getPngSize(const char *path, int *w, int *h) {
  int rc = png.open(path, pngOpen, pngClose, pngRead, pngSeek, pngDrawCB);
  if (rc != PNG_SUCCESS) return false;
  *w = png.getWidth();
  *h = png.getHeight();
  png.close();
  return (*w > 0 && *h > 0);
}

// Draw a themed button: try PNG from /THEME/, fallback to simple rect+text.
// setClipRect ensures an oversized PNG never bleeds outside the button bounds.
void drawThemedButton(int x, int y, int w, int h,
                      const char *pngName, const char *label,
                      uint16_t borderColor) {
  String path = theme_path + "/" + String(pngName) + ".png";

  int imgW = 0, imgH = 0;
  if (getPngSize(path.c_str(), &imgW, &imgH)) {
    lcd.setClipRect(x, y, w, h);
    int bx = x + (w - imgW) / 2;
    int by = y + (h - imgH) / 2;
    drawPngFile(path.c_str(), bx, by);
    lcd.clearClipRect();
  } else {
    // Fallback: filled rectangle with border and text
    gfx_fillRect(x, y, w, h, TFT_BLACK);
    gfx_drawRect(x, y, w, h, borderColor);
    gfx_setTextColor(borderColor, TFT_BLACK);
    gfx_setTextSize(2);
    int tw = gfx_textWidth(String(label));
    int th = gfx_fontHeight();
    gfx_setCursor(x + (w - tw) / 2, y + (h - th) / 2);
    gfx_print(String(label));
  }
}

// ============================================================================
// Cracktro splash screen (Amiga demoscene style)
// ============================================================================

// Simple starfield: pre-generate star positions
#define NUM_STARS 60
int16_t star_x[NUM_STARS], star_y[NUM_STARS], star_speed[NUM_STARS];

void initStars() {
  for (int i = 0; i < NUM_STARS; i++) {
    star_x[i] = random(0, gW);
    star_y[i] = random(0, gH);
    star_speed[i] = random(1, 4);
  }
}

void drawCracktroSplash() {
  initStars();

  // Use a sprite as back-buffer so the display updates in one DMA push — no flicker
  LGFX_Sprite sprite(&lcd);
  bool useSprite = sprite.createSprite(gW, gH);  // ~150KB in PSRAM

  // Helper lambdas that draw to sprite when available, else direct
  auto spFill  = [&](uint16_t c)                           { if (useSprite) sprite.fillScreen(c);               else lcd.fillScreen(c); };
  auto spPixel = [&](int x, int y, uint16_t c)             { if (useSprite) sprite.drawPixel(x, y, c);          else lcd.drawPixel(x, y, c); };
  auto spRect  = [&](int x, int y, int w, int h, uint16_t c){ if (useSprite) sprite.fillRect(x, y, w, h, c);    else lcd.fillRect(x, y, w, h, c); };
  auto spText  = [&](const String &s, int x, int y, int sz, uint16_t fg, bool transp) {
    if (useSprite) {
      sprite.setTextSize(sz * TEXT_SCALE);
      sprite.setTextColor(fg, transp ? (uint16_t)0x0000 : (uint16_t)0x0000);
      sprite.setTextDatum(textdatum_t::top_left);
      sprite.drawString(s, x, y);
    } else {
      gfx_setTextSize(sz);
      gfx_setTextColor(fg, TFT_BLACK);
      text_transparent = transp;
      gfx_setCursor(x, y);
      gfx_print(s);
      text_transparent = false;
    }
  };

  const char *scrollText =
    "       GOTEK TOUCHSCREEN INTERFACE  ...  "
    "THE ULTIMATE RETRO DISK LOADER FOR AMIGA AND CPC  ...  "
    "ORIGINAL CODE BY DIMMY  ...  "
    "ACTIVE THEME ENGINE - PNG BUTTON SUPPORT - FAT12 RAM DISK  ...  "
    "GREETINGS TO ALL RETRO COMPUTING ENTHUSIASTS!  ...       ";
  int scrollLen = strlen(scrollText);
  int scrollPos = 0;
  int charW = (int)(12 * TEXT_SCALE);

  uint16_t copperColors[] = {
    0xF800, 0xF920, 0xFAA0, 0xFC00, 0xFDE0, 0xEFE0, 0x87E0, 0x07E0,
    0x07F0, 0x07FF, 0x041F, 0x001F, 0x801F, 0xF81F, 0xF810, 0xF800
  };
  int numCopper = 16;

  unsigned long startTime = millis();
  int frame = 0;

  while (millis() - startTime < 7000) {
    uint16_t tx, ty;
    if (touchRead(&tx, &ty)) break;

    spFill(TFT_BLACK);

    // ── Starfield ──
    for (int i = 0; i < NUM_STARS; i++) {
      uint16_t col = (star_speed[i] == 3) ? TFT_WHITE :
                     (star_speed[i] == 2) ? (uint16_t)TFT_GREY : (uint16_t)TFT_DARKGREY;
      spPixel(star_x[i], star_y[i], col);
      star_x[i] -= star_speed[i];
      if (star_x[i] < 0) {
        star_x[i] = gW - 1;
        star_y[i] = random(0, gH);
      }
    }

    // ── Copper bars ──
    int copperY = 80 + (int)(40.0 * sin((float)frame * 0.08));
    for (int i = 0; i < numCopper; i++) {
      int barY = copperY + i * 3;
      if (barY >= 0 && barY < gH)
        spRect(0, barY, gW, 2, copperColors[i]);
    }

    // ── Title text ──
    if (useSprite) {
      sprite.setTextColor(TFT_WHITE);
      sprite.setTextSize(3 * TEXT_SCALE);
      String title = "GOTEK";
      int tw = sprite.textWidth(title);
      sprite.drawString(title, (gW - tw) / 2, copperY + 6);
      sprite.setTextSize(2 * TEXT_SCALE);
      String sub = "TOUCHSCREEN";
      tw = sprite.textWidth(sub);
      sprite.drawString(sub, (gW - tw) / 2, copperY + 32);
    } else {
      text_transparent = true;
      gfx_setTextSize(3); gfx_setTextColor(TFT_WHITE, TFT_BLACK);
      String title = "GOTEK";
      gfx_setCursor((gW - gfx_textWidth(title)) / 2, copperY + 6);
      gfx_print(title);
      gfx_setTextSize(2);
      String sub = "TOUCHSCREEN";
      gfx_setCursor((gW - gfx_textWidth(sub)) / 2, copperY + 32);
      gfx_print(sub);
      text_transparent = false;
    }

    // ── Scroll text ──
    int barY   = gH - 32;
    int barH   = 32;  // tall enough for scaled text + padding
    int textY  = barY + (barH - (int)(8 * 2 * TEXT_SCALE)) / 2;  // vertically centred
    spRect(0, barY, gW, barH, 0x0010);
    if (useSprite) {
      sprite.setTextSize(2 * TEXT_SCALE);
      sprite.setTextColor(TFT_YELLOW);
      sprite.setTextWrap(false);  // prevent wrapping to next line
      // Set clip to bar area so text can't bleed outside
      sprite.setClipRect(0, barY, gW, barH);
      int startChar = scrollPos / charW;
      int pixOffset = scrollPos % charW;
      sprite.setCursor(-pixOffset, textY);
      for (int c = 0; c < (gW / charW) + 2; c++) {
        char buf[2] = { scrollText[(startChar + c) % scrollLen], 0 };
        sprite.print(buf);
      }
      sprite.clearClipRect();
    } else {
      gfx_setTextSize(2); gfx_setTextColor(TFT_YELLOW, 0x0010);
      int startChar = scrollPos / charW;
      int pixOffset = scrollPos % charW;
      gfx_setCursor(-pixOffset, textY);
      for (int c = 0; c < (gW / charW) + 2; c++) {
        char buf[2] = { scrollText[(startChar + c) % scrollLen], 0 };
        gfx_print(String(buf));
      }
    }

    scrollPos += 3;
    frame++;

    // Push sprite — hold SPI bus across the transfer to minimise tearing
    if (useSprite) {
      lcd.startWrite();
      sprite.pushSprite(0, 0);
      lcd.endWrite();
    }

    delay(20);  // ~50fps
  }

  if (useSprite) sprite.deleteSprite();

  gfx_fillScreen(TFT_WHITE); gfx_flush(); delay(50);
  gfx_fillScreen(TFT_BLACK); gfx_flush();
}

// ============================================================================
// Boot loading screen (shown during SD scan and init)
// ============================================================================
void drawBootProgress(const String &status, int pct) {
  // Don't clear full screen — just update status area
  // Bar area: y=180..210
  int barX = 60, barY = 190, barW = gW - 120, barH = 16;

  // Status text
  gfx_fillRect(0, 160, gW, 20, TFT_BLACK);  // clear old text
  gfx_setTextSize(1);
  gfx_setTextColor(TFT_GREY, TFT_BLACK);
  int sw = gfx_textWidth(status);
  gfx_setCursor((gW - sw) / 2, 165);
  gfx_print(status);

  // Progress bar
  gfx_drawRect(barX, barY, barW, barH, TFT_GREY);
  int fillW = ((barW - 4) * pct) / 100;
  if (fillW > 0) {
    gfx_fillRect(barX + 2, barY + 2, fillW, barH - 4, TFT_CYAN);
  }

  gfx_flush();
}

void drawBootScreen() {
  gfx_fillScreen(TFT_BLACK);

  // Logo text
  gfx_setTextSize(3);
  gfx_setTextColor(TFT_CYAN, TFT_BLACK);
  String t = "GOTEK";
  gfx_setCursor((gW - gfx_textWidth(t)) / 2, 80);
  gfx_print(t);

  gfx_setTextSize(2);
  gfx_setTextColor(TFT_WHITE, TFT_BLACK);
  t = "Touchscreen Interface";
  gfx_setCursor((gW - gfx_textWidth(t)) / 2, 120);
  gfx_print(t);

  gfx_flush();
}

// ============================================================================
// Button press feedback — spinner/loading overlay
// ============================================================================
bool ui_busy = false;  // when true, touch input is ignored

void showBusyIndicator(const String &msg = "LOADING...") {
  ui_busy = true;

  // Semi-transparent overlay: dark bar across the middle
  int boxW = 200, boxH = 50;
  int bx = (gW - boxW) / 2;
  int by = (gH - boxH) / 2;

  // Dark box with border
  gfx_fillRect(bx, by, boxW, boxH, TFT_BLACK);
  gfx_drawRect(bx, by, boxW, boxH, TFT_CYAN);
  gfx_drawRect(bx + 1, by + 1, boxW - 2, boxH - 2, TFT_DARKGREY);

  // Spinning dots (static frame — just show a loading text with dots)
  gfx_setTextSize(2);
  gfx_setTextColor(TFT_WHITE, TFT_BLACK);
  int mw = gfx_textWidth(msg);
  gfx_setCursor(bx + (boxW - mw) / 2, by + (boxH - 16) / 2);
  gfx_print(msg);

  gfx_flush();
}

void hideBusyIndicator() {
  ui_busy = false;
}

// ============================================================================
// Info / Status screen
// ============================================================================
void drawInfoScreen() {
  gfx_fillScreen(TFT_BLACK);

  // Title — size 2 is fine for just the header
  gfx_setTextColor(TFT_CYAN, TFT_BLACK);
  gfx_setTextSize(2);
  gfx_setCursor(8, 6);
  gfx_print("SYSTEM INFO");

  // All body text at size 1 to fit the 2.8" screen
  gfx_setTextSize(1);
  int y = 28;
  int lineH = 16;

  // --- Free heap ---
  gfx_setTextColor(TFT_GREEN, TFT_BLACK);
  gfx_setCursor(8, y);
  gfx_print("Heap: ");
  gfx_setTextColor(TFT_WHITE, TFT_BLACK);
  gfx_print(String(ESP.getFreeHeap() / 1024) + " KB free");
  y += lineH;

  // --- Free PSRAM ---
  gfx_setTextColor(TFT_GREEN, TFT_BLACK);
  gfx_setCursor(8, y);
  gfx_print("PSRAM: ");
  gfx_setTextColor(TFT_WHITE, TFT_BLACK);
  gfx_print(String(ESP.getFreePsram() / 1024) + " KB free");
  y += lineH;

  // --- SD card info ---
  uint64_t totalBytes = SD_MMC.totalBytes();
  uint64_t usedBytes  = SD_MMC.usedBytes();
  gfx_setTextColor(TFT_GREEN, TFT_BLACK);
  gfx_setCursor(8, y);
  gfx_print("SD: ");
  gfx_setTextColor(TFT_WHITE, TFT_BLACK);
  gfx_print(String((uint32_t)(usedBytes / (1024*1024))) + " / " +
            String((uint32_t)(totalBytes / (1024*1024))) + " MB");
  y += lineH;

  // --- File count ---
  gfx_setTextColor(TFT_GREEN, TFT_BLACK);
  gfx_setCursor(8, y);
  gfx_print("Files: ");
  gfx_setTextColor(TFT_WHITE, TFT_BLACK);
  gfx_print(String(file_list.size()) + " " +
            String((g_mode == MODE_ADF) ? "ADF" : "DSK"));
  y += lineH;

  // --- Currently loaded file ---
  gfx_setTextColor(TFT_GREEN, TFT_BLACK);
  gfx_setCursor(8, y);
  gfx_print("Loaded: ");
  gfx_setTextColor(TFT_YELLOW, TFT_BLACK);
  if (cfg_lastfile.length() > 0) {
    gfx_print(basenameNoExt(filenameOnly(cfg_lastfile)));
  } else {
    gfx_print("(none)");
  }
  y += lineH;

  // --- Display type ---
  gfx_setTextColor(TFT_GREEN, TFT_BLACK);
  gfx_setCursor(8, y);
  gfx_print("Display: ");
  gfx_setTextColor(TFT_WHITE, TFT_BLACK);
  gfx_print("Waveshare 320x240");
  y += lineH;

  // --- Active theme ---
  gfx_setTextColor(TFT_GREEN, TFT_BLACK);
  gfx_setCursor(8, y);
  gfx_print("Theme: ");
  gfx_setTextColor(WB_ORANGE, TFT_BLACK);
  gfx_print(cfg_theme);
  y += lineH;

  // --- WiFi AP status ---
  gfx_setTextColor(TFT_GREEN, TFT_BLACK);
  gfx_setCursor(8, y);
  gfx_print("AP: ");
  if (isWiFiActive()) {
    gfx_setTextColor(TFT_CYAN, TFT_BLACK);
    gfx_print(wifi_ap_ip);
    gfx_setTextColor(0x7BEF, TFT_BLACK);
    gfx_print(" (" + String(WiFi.softAPgetStationNum()) + ")");
  } else {
    gfx_setTextColor(0x7BEF, TFT_BLACK);
    gfx_print("Off");
  }
  y += lineH;

  // --- Remote dongle / WiFi client status ---
  if (cfg_remote_enabled) {
    gfx_setTextColor(TFT_GREEN, TFT_BLACK);
    gfx_setCursor(8, y);
    gfx_print("Dongle: ");
    if (remote_connected) {
      gfx_setTextColor(TFT_CYAN, TFT_BLACK);
      gfx_print("Connected");
      gfx_setTextColor(0x7BEF, TFT_BLACK);
      gfx_print(" (" + cfg_remote_ssid + ")");
    } else {
      gfx_setTextColor(TFT_YELLOW, TFT_BLACK);
      gfx_print("Connecting...");
    }
    y += lineH;
    if (remote_connected && remote_dongle_file.length() > 0) {
      gfx_setTextColor(TFT_GREEN, TFT_BLACK);
      gfx_setCursor(8, y);
      gfx_print("Remote: ");
      gfx_setTextColor(TFT_YELLOW, TFT_BLACK);
      gfx_print(basenameNoExt(remote_dongle_file));
    }
  } else {
    gfx_setTextColor(TFT_GREEN, TFT_BLACK);
    gfx_setCursor(8, y);
    gfx_print("Net: ");
    if (wifi_sta_connected) {
      gfx_setTextColor(TFT_CYAN, TFT_BLACK);
      gfx_print(wifi_sta_ip);
      gfx_setTextColor(0x7BEF, TFT_BLACK);
      gfx_print(" (" + cfg_wifi_client_ssid + ")");
    } else if (cfg_wifi_client_enabled && cfg_wifi_client_ssid.length() > 0) {
      gfx_setTextColor(TFT_YELLOW, TFT_BLACK);
      gfx_print("Connecting...");
    } else {
      gfx_setTextColor(0x7BEF, TFT_BLACK);
      gfx_print("Off");
    }
  }

  // Bottom buttons — slim, well-spaced, clear of text (9 rows × 16px + 28 = 172px used, buttons at y=206)
  int btnH = 26, marginX = 4, gap = 4;
  int btnW = (gW - 2 * marginX - 2 * gap) / 3;
  int btnY = gH - btnH - 4;
  int btn1X = marginX;
  int btn2X = btn1X + btnW + gap;
  int btn3X = btn2X + btnW + gap;
  drawThemedButton(btn1X, btnY, btnW, btnH, "BTN_BACK",  "BACK",  TFT_CYAN);
  drawThemedButton(btn2X, btnY, btnW, btnH, "BTN_THEME", "THEME", WB_ORANGE);
  if (g_mode == MODE_ADF) {
    drawThemedButton(btn3X, btnY, btnW, btnH, "BTN_ADF", "ADF", TFT_CYAN);
  } else {
    drawThemedButton(btn3X, btnY, btnW, btnH, "BTN_DSK", "DSK", TFT_CYAN);
  }

  gfx_flush();
}

// List layout constants (no header bar — full screen list)
#define LIST_START_Y   4
#define LIST_ITEM_H    52
#define LIST_THUMB_W   46
#define LIST_THUMB_H   46
#define LIST_BOTTOM    (gH - 48)

int items_per_page() {
  return (LIST_BOTTOM - LIST_START_Y) / LIST_ITEM_H;
}

void drawList() {
  gfx_fillScreen(TFT_BLACK);

  int perPage = items_per_page();

  // Clamp scroll_offset (no auto-scroll — scroll buttons control position freely)
  if (scroll_offset > (int)game_list.size() - perPage)
    scroll_offset = (int)game_list.size() - perPage;
  if (scroll_offset < 0) scroll_offset = 0;

  // Draw visible game entries
  for (int vi = 0; vi < perPage && (scroll_offset + vi) < (int)game_list.size(); vi++) {
    int gi = scroll_offset + vi;
    const GameEntry &g = game_list[gi];
    int y = LIST_START_Y + vi * LIST_ITEM_H;
    bool isSel = (gi == game_selected);

    // Selection highlight bar
    if (isSel) {
      gfx_fillRect(0, y, gW, LIST_ITEM_H, 0x1082);  // dark highlight
    }

    // Thumbnail (cover art)
    int thumbX = 6;
    int thumbY = y + (LIST_ITEM_H - LIST_THUMB_H) / 2;
    bool thumbDrawn = false;
    if (g.jpg_path.length() > 0) {
      String lp = g.jpg_path;
      lp.toLowerCase();
      LOG("JPG callsite(list): jpg_path=[" + g.jpg_path + "] lp=[" + lp + "]");
      if (lp.endsWith(".jpg") || lp.endsWith(".jpeg")) {
        gfx_drawJpgFile(SD_MMC, g.jpg_path.c_str(), thumbX, thumbY, LIST_THUMB_W, LIST_THUMB_H);
        thumbDrawn = true;
      } else if (lp.endsWith(".png")) {
        // PNG — draw scaled via drawPngFile (no scaling, draw at offset)
        thumbDrawn = drawPngFile(g.jpg_path.c_str(), thumbX, thumbY);
      }
    }
    if (!thumbDrawn) {
      // No cover art — draw a placeholder
      gfx_drawRect(thumbX, thumbY, LIST_THUMB_W, LIST_THUMB_H, 0x4208);  // grey border
      gfx_setTextColor(0x4208, TFT_BLACK);
      gfx_setTextSize(1);
      gfx_setCursor(thumbX + 12, thumbY + 18);
      gfx_print("?");
    }

    // Game name
    int textX = thumbX + LIST_THUMB_W + 8;
    gfx_setTextColor(isSel ? TFT_CYAN : TFT_WHITE, isSel ? 0x1082 : TFT_BLACK);
    gfx_setTextSize(2);
    String dispName = truncateToWidth(g.name, gW - textX - 54);
    gfx_setCursor(textX, y + 8);
    gfx_print(dispName);

    // Disk count indicator (if multi-disk)
    if (g.disk_count > 1) {
      gfx_setTextSize(1);
      gfx_setTextColor(TFT_YELLOW, isSel ? 0x1082 : TFT_BLACK);
      gfx_setCursor(textX, y + 30);
      gfx_print(String(g.disk_count) + " disks");
    }

    // Separator line
    gfx_fillRect(6, y + LIST_ITEM_H - 1, gW - 12, 1, 0x2104);
  }

  // Scroll buttons (right edge — large touch targets)
  if ((int)game_list.size() > perPage) {
    int btnW = 44;
    int btnX = gW - btnW;
    int listH = LIST_BOTTOM - LIST_START_Y;
    int btnH = listH / 2;

    // UP button (themed)
    int downY = LIST_START_Y + btnH + 1;
    int maxOff = (int)game_list.size() - perPage;
    if (maxOff < 0) maxOff = 0;

    uint16_t upBorderColor = (scroll_offset > 0) ? TFT_WHITE : 0x3186;
    drawThemedButton(btnX, LIST_START_Y, btnW, btnH - 1, "BTN_UP", "^", upBorderColor);

    // DOWN button (themed)
    uint16_t dnBorderColor = (scroll_offset < maxOff) ? TFT_WHITE : 0x3186;
    drawThemedButton(btnX, downY, btnW, btnH - 1, "BTN_DOWN", "v", dnBorderColor);

    // Thin scrollbar between buttons
    int trackH = LIST_BOTTOM - LIST_START_Y;
    int thumbH = max(8, trackH * perPage / (int)game_list.size());
    int thumbY = LIST_START_Y + (trackH - thumbH) * scroll_offset / max(1, maxOff);
    gfx_fillRect(btnX - 4, thumbY, 3, thumbH, 0x4208);
  }

  // Bottom bar: status text left, INFO button right
  gfx_setTextSize(1);
  if (loaded_disk_index >= 0 && loaded_disk_index < (int)file_list.size()) {
    String loadedName = getGameBaseName(file_list[loaded_disk_index]);
    gfx_setTextColor(TFT_GREEN, TFT_BLACK);
    gfx_setCursor(8, gH - 38);
    gfx_print("Now playing:");
    gfx_setTextColor(TFT_WHITE, TFT_BLACK);
    gfx_setCursor(8, gH - 26);
    gfx_print(loadedName.substring(0, 32));
  } else {
    gfx_setTextColor(TFT_GREY, TFT_BLACK);
    gfx_setCursor(8, gH - 32);
    gfx_print(String((int)game_list.size()) + " games");
  }

  // INFO button — same width as scroll chevrons, aligned right
  int infoBtnX = gW - 44;
  drawThemedButton(infoBtnX, gH - 42, 44, 36, "BTN_INFO", "i", TFT_YELLOW);

  gfx_flush();
}

// Draw the disk selector row for multi-disk games.
// diskY = top Y position of the disk button row.
void drawDiskSelector(int diskY) {
  if (disk_set.size() <= 1) return;

  int btnW = 44;
  int btnH = 28;
  int gap = 4;
  int numDisks = disk_set.size();
  int totalW = numDisks * btnW + (numDisks - 1) * gap;
  int startX = (gW - totalW) / 2;  // center the row

  // "DISK:" label before the buttons
  gfx_setTextColor(TFT_GREY, TFT_BLACK);
  gfx_setTextSize(1);
  gfx_setCursor(startX - 35, diskY + 10);
  gfx_print("DISK:");
  startX += 0;  // buttons right after label

  for (int i = 0; i < numDisks; i++) {
    int bx = startX + i * (btnW + gap);
    int diskNum = getDiskNumber(file_list[disk_set[i]]);
    bool isLoaded = (disk_set[i] == loaded_disk_index);

    if (isLoaded) {
      gfx_fillRect(bx, diskY, btnW, btnH, TFT_GREEN);
      gfx_drawRect(bx, diskY, btnW, btnH, TFT_WHITE);
      gfx_setTextColor(TFT_BLACK, TFT_GREEN);
    } else {
      gfx_fillRect(bx, diskY, btnW, btnH, 0x1082);
      gfx_drawRect(bx, diskY, btnW, btnH, TFT_GREY);
      gfx_setTextColor(TFT_WHITE, 0x1082);
    }

    // Number label centered in button (textSize 2 fits: "1" = 12px in 44px)
    gfx_setTextSize(2);
    String label = String(diskNum);
    int tw = gfx_textWidth(label);
    gfx_setCursor(bx + (btnW - tw) / 2, diskY + (btnH - 16) / 2);
    gfx_print(label);
  }
}

void drawDetailsFromNFO(const String &filename) {
  gfx_fillScreen(TFT_BLACK);

  // Detect multi-disk set
  findRelatedDisks(selected_index);
  bool multiDisk = (disk_set.size() > 1);

  detail_nfo_text = "";
  detail_jpg_path = "";
  String nfoPath = findNFOFor(filename);
  if (nfoPath.length() > 0) {
    detail_nfo_text = readSmallTextFile(nfoPath.c_str(), 500);
  }
  detail_jpg_path = findJPGFor(filename);

  // Layout: no header, no top title — maximum space for cover art
  int diskRowH = multiDisk ? 34 : 0;
  int btnTop = gH - 42;
  int diskTop = multiDisk ? (btnTop - diskRowH) : btnTop;
  int contentBottom = diskTop - 3;

  // Cover art starts from the very top
  int imgTop = 4;
  int imgW = gW - 60;  // leave room for nav arrows

  String title = "", blurb = "";
  parseNFO(detail_nfo_text, title, blurb);
  int textSpace = 0;
  if (title.length() > 0) textSpace += 18;
  if (blurb.length() > 0) textSpace += 14;

  int imgH = contentBottom - imgTop - textSpace - 4;
  if (imgH < 60) imgH = 60;

  if (detail_jpg_path.length() > 0) {
    String lp = detail_jpg_path;
    lp.toLowerCase();
    LOG("JPG callsite(detail): detail_jpg_path=[" + detail_jpg_path + "] lp=[" + lp + "]");
    if (lp.endsWith(".png")) {
      drawPngFile(detail_jpg_path.c_str(), (gW - imgW) / 2, imgTop);
    } else {
      gfx_drawJpgFile(SD_MMC, detail_jpg_path.c_str(),
                     (gW - imgW) / 2, imgTop, imgW, imgH);
    }
  }

  int textY = imgTop + imgH + 3;
  if (title.length() > 0) {
    gfx_setTextColor(TFT_YELLOW, TFT_BLACK);
    gfx_setTextSize(2);
    gfx_setCursor(20, textY);
    gfx_print(truncateToWidth(title, gW - 40));
    textY += 18;
  }
  if (blurb.length() > 0) {
    gfx_setTextSize(1);
    drawWrappedText(blurb, 20, textY, gW - 40, TFT_WHITE);
  }

  // Disk selector row (if multi-disk)
  if (multiDisk) {
    drawDiskSelector(diskTop);
  }

  // Navigation arrows (left/right edges)
  int arrowY = imgTop + imgH / 2 - 8;
  gfx_setTextSize(2);
  if (game_selected > 0) {
    gfx_setTextColor(TFT_GREY, TFT_BLACK);
    gfx_setCursor(4, arrowY);
    gfx_print("<");
  }
  if (game_selected < (int)game_list.size() - 1) {
    gfx_setTextColor(TFT_GREY, TFT_BLACK);
    gfx_setCursor(gW - 16, arrowY);
    gfx_print(">");
  }

  // Bottom action buttons: BACK + INSERT/EJECT — uniform 148x36, evenly spaced
  int detBtnW = 148, detBtnH = 36;
  int detBtn1X = 10;
  int detBtn2X = gW - 10 - detBtnW;  // right-aligned
  drawThemedButton(detBtn1X, btnTop, detBtnW, detBtnH, "BTN_BACK", "BACK", TFT_CYAN);

  // LOAD/UNLOAD toggle — one button, changes based on state
  bool isCurrentLoaded = (loaded_disk_index == selected_index && loaded_disk_index >= 0);
  if (isCurrentLoaded) {
    drawThemedButton(detBtn2X, btnTop, detBtnW, detBtnH, "BTN_UNLOAD", "EJECT", TFT_RED);
  } else {
    drawThemedButton(detBtn2X, btnTop, detBtnW, detBtnH, "BTN_LOAD", "INSERT", TFT_GREEN);
  }

  // Loaded status indicator at top-right
  if (loaded_disk_index >= 0) {
    gfx_setTextSize(1);
    gfx_setTextColor(TFT_GREEN, TFT_BLACK);
    gfx_setCursor(gW - 80, 4);
    if (isCurrentLoaded) {
      gfx_print("[LOADED]");
    } else {
      gfx_setTextColor(TFT_DARKGREY, TFT_BLACK);
      gfx_print("[OTHER]");
    }
  }

  // Remote mode indicator at top-left
  if (cfg_remote_enabled) {
    gfx_setTextSize(1);
    if (remote_connected) {
      gfx_setTextColor(TFT_CYAN, TFT_BLACK);
      gfx_setCursor(4, 4);
      gfx_print("[REMOTE]");
    } else {
      gfx_setTextColor(TFT_RED, TFT_BLACK);
      gfx_setCursor(4, 4);
      gfx_print("[NO LINK]");
    }
  }

  gfx_flush();
}

String truncateToWidth(const String &text, int maxWidth) {
  String result = text;
  gfx_setTextSize(2);
  while (gfx_textWidth(result) > maxWidth && result.length() > 0) {
    result = result.substring(0, result.length() - 1);
  }
  return result;
}

void buildDisplayNames(const vector<String> &files) {
  display_names.clear();
  for (const auto &f : files) {
    // Show the filename without extension and path
    String name = basenameNoExt(filenameOnly(f));
    display_names.push_back(truncateToWidth(name, gW - 40));
  }
}

void sortByDisplay() {
  for (int i = 0; i < (int)file_list.size(); i++) {
    for (int j = i + 1; j < (int)file_list.size(); j++) {
      if (display_names[i].compareTo(display_names[j]) > 0) {
        swap(file_list[i], file_list[j]);
        swap(display_names[i], display_names[j]);
      }
    }
  }
}

// Find the game_list index for a given file_list index
int findGameIndex(int fileIndex) {
  for (int i = 0; i < (int)game_list.size(); i++) {
    // Check if this game entry contains the file
    String baseName = game_list[i].name;
    String dir = parentDir(file_list[fileIndex]);
    String fileBase = getGameBaseName(file_list[fileIndex]);
    if (game_list[i].first_file_index == fileIndex) return i;
    if (game_list[i].disk_count > 1 && fileBase == baseName) return i;
  }
  return 0;
}

// Build the merged game list from file_list.
// Groups multi-disk games (GameName-1.adf, GameName-2.adf) into one entry.
// Also finds cover art (JPG/PNG) for each game.
void buildGameList() {
  game_list.clear();
  game_selected = 0;
  scroll_offset = 0;

  // Track which file_list indices we've already grouped
  vector<bool> used(file_list.size(), false);

  for (int i = 0; i < (int)file_list.size(); i++) {
    if (used[i]) continue;

    String baseName = getGameBaseName(file_list[i]);
    int diskNum = getDiskNumber(file_list[i]);
    String dir = parentDir(file_list[i]);

    GameEntry entry;
    entry.first_file_index = i;
    entry.disk_count = 1;

    if (diskNum > 0) {
      // This is a multi-disk file — find all related disks
      entry.name = baseName;
      int count = 1;
      for (int j = i + 1; j < (int)file_list.size(); j++) {
        if (used[j]) continue;
        if (parentDir(file_list[j]) == dir &&
            getGameBaseName(file_list[j]) == baseName &&
            getDiskNumber(file_list[j]) > 0) {
          used[j] = true;
          count++;
          // Keep first_file_index pointing to disk 1 (lowest number)
          if (getDiskNumber(file_list[j]) < getDiskNumber(file_list[entry.first_file_index])) {
            entry.first_file_index = j;
          }
        }
      }
      entry.disk_count = count;
    } else {
      // Single disk game
      entry.name = basenameNoExt(filenameOnly(file_list[i]));
    }

    used[i] = true;

    // Find cover art — try the game folder
    entry.jpg_path = findJPGFor(file_list[entry.first_file_index]);

    // If multi-disk and no cover found with disk-1 name, try base name
    if (entry.jpg_path.length() == 0 && diskNum > 0) {
      String tryBase = dir + "/" + baseName;
      for (const char *ext : {".jpg", ".jpeg", ".png"}) {
        String tryPath = tryBase + ext;
        if (SD_MMC.exists(tryPath.c_str())) {
          entry.jpg_path = tryPath;
          break;
        }
      }
    }

    game_list.push_back(entry);
  }

  // Sort alphabetically by name
  for (int i = 0; i < (int)game_list.size(); i++) {
    for (int j = i + 1; j < (int)game_list.size(); j++) {
      if (game_list[i].name.compareTo(game_list[j].name) > 0) {
        swap(game_list[i], game_list[j]);
      }
    }
  }
}

// Core file loading: reads SD file into RAM disk, builds FAT chain.
// Returns bytes loaded, or 0 on error. Does NOT touch USB.
size_t loadFileToRam(int index) {
  if (index < 0 || index >= (int)file_list.size()) return 0;

  // file_list contains full paths like "/ADF/Giganoid/Giganoid.adf"
  String filepath = file_list[index];

  // Show themed loading UI
  drawThemedLoadingScreen(basenameNoExt(filenameOnly(file_list[index])));

  // Rebuild FAT volume (empty)
  build_volume_with_file();

  // Open via SD_MMC first, fall back to direct VFS fopen for deep subfolder paths
  File f = SD_MMC.open(filepath.c_str(), FILE_READ);
  size_t fileSize = 0;

  // Try fopen via VFS — more reliable for 2-level deep paths on ESP32
  String vfsPath = "/sdcard" + filepath;
  FILE *fp = fopen(vfsPath.c_str(), "rb");
  if (fp) {
    fseek(fp, 0, SEEK_END);
    fileSize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
  }

  // DEBUG
  if (!fp) {
    gfx_setTextColor(TFT_RED, TFT_BLACK);
    gfx_setTextSize(2);
    gfx_setCursor(4, 80); gfx_print("OPEN FAILED");
    gfx_setCursor(4, 104); gfx_print(vfsPath.substring(0, 30));
    gfx_flush();
    if (f) f.close();
    delay(2000);
    return 0;
  }

  size_t maxData = RAM_DISK_SIZE - DATA_OFFSET;
  size_t toRead = (fileSize > 0 && fileSize < maxData) ? fileSize : maxData;
  size_t totalRead = 0;
  int lastPctDrawn = -1;
  uint8_t buf[4096];
  size_t got;

  drawThemedLoadingScreen(basenameNoExt(filenameOnly(filepath)));

  while ((got = fread(buf, 1, sizeof(buf), fp)) > 0) {
    if (totalRead + got > maxData) got = maxData - totalRead;
    memcpy(&ram_disk[DATA_OFFSET + totalRead], buf, got);
    totalRead += got;
    if (totalRead >= maxData) break;
    if (toRead > 0) {
      int pct = (int)((totalRead * 100) / toRead);
      if (pct / 5 != lastPctDrawn / 5) { lastPctDrawn = pct; drawThemedProgressBar(pct); }
    }
  }
  fclose(fp);
  if (f) f.close();

  // Build FAT chain
  uint16_t clusters_needed = (totalRead + 511) / 512;
  for (int c = 2; c < 2 + clusters_needed; c++) {
    if (c < 2 + clusters_needed - 1) {
      fat12_set(&ram_disk[FAT1_OFFSET], c, c + 1);
      fat12_set(&ram_disk[FAT2_OFFSET], c, c + 1);
    } else {
      fat12_set(&ram_disk[FAT1_OFFSET], c, 0xFFF);
      fat12_set(&ram_disk[FAT2_OFFSET], c, 0xFFF);
    }
  }
  *(uint16_t *)&ram_disk[ROOTDIR_OFFSET + 26] = 2;
  *(uint32_t *)&ram_disk[ROOTDIR_OFFSET + 28] = totalRead;

  drawThemedProgressBar(100);
  gfx_setTextColor(TFT_GREEN, TFT_BLACK);
  gfx_setTextSize(2);
  String okMsg = "OK! " + String(totalRead / 1024) + " KB";
  gfx_setCursor((gW - gfx_textWidth(okMsg)) / 2, 200);
  gfx_print(okMsg);
  gfx_flush();
  delay(500);

  return totalRead;
}

// ============================================================================
// REMOTE DONGLE FUNCTIONS — send disk images to WiFi Dongle via HTTP
// ============================================================================

// Send a disk image file from SD to the remote dongle via POST /api/load
// Returns true on success. Shows progress on the themed loading screen.
bool remoteSendFile(int index) {
  if (index < 0 || index >= (int)file_list.size()) return false;
  if (!remote_connected) {
    LOG("Remote: not connected to dongle");
    return false;
  }

  String filepath = file_list[index];
  String filename = filenameOnly(filepath);

  // Open file from SD
  File f = SD_MMC.open(filepath.c_str(), "r");
  if (!f) {
    LOG("Remote: cannot open " + filepath);
    return false;
  }

  size_t fileSize = f.size();
  LOG("Remote: sending " + filename + " (" + String(fileSize) + " bytes)");

  // Show themed loading screen
  drawThemedLoadingScreen(basenameNoExt(filename));

  // Build URL
  String url = "http://" + cfg_remote_host + ":" + String(cfg_remote_port) + "/api/load";

  // Use raw WiFiClient for streaming (HTTPClient buffers everything)
  WiFiClient tcpClient;
  if (!tcpClient.connect(cfg_remote_host.c_str(), cfg_remote_port)) {
    LOG("Remote: cannot connect to " + cfg_remote_host);
    f.close();
    return false;
  }

  // Send HTTP headers
  tcpClient.println("POST /api/load HTTP/1.1");
  tcpClient.println("Host: " + cfg_remote_host);
  tcpClient.println("X-Filename: " + filename);
  tcpClient.println("Content-Type: application/octet-stream");
  tcpClient.println("Content-Length: " + String(fileSize));
  tcpClient.println("Connection: close");
  tcpClient.println();

  // Stream file data in chunks
  uint8_t buf[4096];
  size_t totalSent = 0;
  int lastPctDrawn = -1;

  while (totalSent < fileSize) {
    size_t chunk = fileSize - totalSent;
    if (chunk > sizeof(buf)) chunk = sizeof(buf);
    size_t got = f.read(buf, chunk);
    if (got == 0) break;

    size_t written = tcpClient.write(buf, got);
    if (written != got) {
      LOG("Remote: write error at " + String(totalSent));
      f.close();
      tcpClient.stop();
      return false;
    }
    totalSent += got;

    // Update progress bar
    int pct = (totalSent * 100) / fileSize;
    if (pct / 5 != lastPctDrawn / 5) {
      lastPctDrawn = pct;
      drawThemedProgressBar(pct);
    }
  }
  f.close();

  // Read response
  unsigned long timeout = millis();
  while (!tcpClient.available() && millis() - timeout < 5000) {
    delay(10);
  }

  bool success = false;
  if (tcpClient.available()) {
    String responseLine = tcpClient.readStringUntil('\n');
    success = responseLine.indexOf("200") >= 0;
    LOG("Remote: response: " + responseLine);
  }
  tcpClient.stop();

  if (success) {
    drawThemedProgressBar(100);
    gfx_setTextColor(TFT_GREEN, TFT_BLACK);
    gfx_setTextSize(2);
    String okMsg = "SENT! " + String(totalSent / 1024) + " KB";
    gfx_setCursor((gW - gfx_textWidth(okMsg)) / 2, 200);
    gfx_print(okMsg);
    gfx_flush();
    delay(500);
    remote_dongle_file = filename;
  } else {
    gfx_setTextColor(TFT_RED, TFT_BLACK);
    gfx_setTextSize(2);
    String errMsg = "SEND FAILED!";
    gfx_setCursor((gW - gfx_textWidth(errMsg)) / 2, 200);
    gfx_print(errMsg);
    gfx_flush();
    delay(1000);
  }

  return success;
}

// Send eject command to the remote dongle
bool remoteEject() {
  if (!remote_connected) return false;

  WiFiClient tcpClient;
  if (!tcpClient.connect(cfg_remote_host.c_str(), cfg_remote_port)) return false;

  tcpClient.println("POST /api/eject HTTP/1.1");
  tcpClient.println("Host: " + cfg_remote_host);
  tcpClient.println("Content-Length: 0");
  tcpClient.println("Connection: close");
  tcpClient.println();

  unsigned long timeout = millis();
  while (!tcpClient.available() && millis() - timeout < 3000) { delay(10); }

  bool success = false;
  if (tcpClient.available()) {
    String resp = tcpClient.readStringUntil('\n');
    success = resp.indexOf("200") >= 0;
  }
  tcpClient.stop();

  if (success) {
    remote_dongle_file = "";
    LOG("Remote: ejected");
  }
  return success;
}

// Query dongle status via GET /api/status
bool remoteGetStatus() {
  if (!remote_connected) return false;

  WiFiClient tcpClient;
  if (!tcpClient.connect(cfg_remote_host.c_str(), cfg_remote_port)) return false;

  tcpClient.println("GET /api/status HTTP/1.1");
  tcpClient.println("Host: " + cfg_remote_host);
  tcpClient.println("Connection: close");
  tcpClient.println();

  unsigned long timeout = millis();
  while (!tcpClient.available() && millis() - timeout < 3000) { delay(10); }

  String body = "";
  bool headersDone = false;
  while (tcpClient.available()) {
    String line = tcpClient.readStringUntil('\n');
    if (!headersDone && line.length() <= 2) { headersDone = true; continue; }
    if (headersDone) body += line;
  }
  tcpClient.stop();

  // Simple JSON parsing for "filename" field
  int fnIdx = body.indexOf("\"filename\":\"");
  if (fnIdx >= 0) {
    int start = fnIdx + 12;
    int end = body.indexOf("\"", start);
    if (end > start) remote_dongle_file = body.substring(start, end);
  }

  remote_dongle_status = body;
  return true;
}

// ============================================================================
// LOCAL DISK OPERATIONS
// ============================================================================

// Unload: remove media from USB drive so host sees no disk.
void doUnload() {
  // Remote mode: send eject to dongle
  if (cfg_remote_enabled && remote_connected) {
    remoteEject();
    loaded_disk_index = -1;
    cfg_lastfile = "";
    saveConfig();
    LOG("Remote: drive ejected on dongle");
    return;
  }

  tud_disconnect();
  delay(500);

  build_volume_with_file();
  bumpInquiryRevision();
  msc.mediaPresent(false);

  tud_connect();
  delay(200);

  // Clear last file from config so it won't auto-load next boot
  cfg_lastfile = "";
  loaded_disk_index = -1;
  saveConfig();

  LOG("Drive unloaded");
}

// Full load: disconnects USB, loads file, reconnects USB, saves config.
// In remote mode: sends file to dongle instead of local RAM disk.
void doLoadSelected() {
  if (selected_index < 0 || selected_index >= (int)file_list.size()) return;

  // Remote mode: send file to dongle over WiFi
  if (cfg_remote_enabled && remote_connected) {
    bool ok = remoteSendFile(selected_index);
    if (ok) {
      loaded_disk_index = selected_index;
      cfg_lastfile = file_list[selected_index];
      cfg_lastmode = (g_mode == MODE_ADF) ? "ADF" : "DSK";
      saveConfig();
    }
    return;
  }

  // Local mode: load to RAM disk + USB
  tud_disconnect();
  delay(500);

  size_t loaded = loadFileToRam(selected_index);

  bumpInquiryRevision();
  msc.mediaPresent(loaded > 0);
  tud_connect();
  delay(200);

  if (loaded > 0) {
    loaded_disk_index = selected_index;
    cfg_lastfile = file_list[selected_index];
    cfg_lastmode = (g_mode == MODE_ADF) ? "ADF" : "DSK";
    saveConfig();
  }
}

// ============================================================================
// WiFi Web Server (include after all game/theme functions are defined)
// ============================================================================
#include "webserver.h"

void setup() {
  Serial.begin(115200);
  delay(500);
  LOG("Gotek Touchscreen Interface starting...");

  uiInit();
  gfx_fillScreen(TFT_BLACK);
  gfx_flush();
  LOG("Display initialized");

  touchInit();
  LOG("Touch initialized");

  // ── Cracktro splash screen ──
  drawCracktroSplash();

  // ── Boot loading screen ──
  drawBootScreen();
  drawBootProgress("Initializing SD card...", 5);

  init_sd_card();
  g_sd_log_ready = true;   // SD mounted — log file writes now active
  LOG("SD card initialized");
  firstBootScaffold();     // create folder structure if card is blank
  drawBootProgress("Loading configuration...", 15);

  loadConfig();
  scanThemes();
  drawBootProgress("Scanning themes...", 25);

  if (cfg_lastmode == "DSK") {
    g_mode = MODE_DSK;
  } else {
    g_mode = MODE_ADF;
  }

  drawBootProgress("Scanning disk images...", 35);
  file_list = listImages();
  buildDisplayNames(file_list);
  sortByDisplay();
  buildGameList();

  LOG("Found " + String(file_list.size()) + " images (" + String(game_list.size()) + " games)");
  drawBootProgress("Found " + String(game_list.size()) + " games", 50);

  drawBootProgress("Allocating RAM disk...", 60);
  ram_disk = (uint8_t *)ps_malloc(RAM_DISK_SIZE);
  if (!ram_disk) {
    LOG("Failed to allocate RAM disk!");
    drawBootProgress("ERROR: RAM alloc failed!", 60);
    while (1);
  }

  build_volume_with_file();
  LOG("RAM disk initialized");
  drawBootProgress("RAM disk ready", 70);

  // Auto-load last file BEFORE USB starts (no tud_disconnect needed)
  bool autoloaded = false;
  if (cfg_lastfile.length() > 0) {
    drawBootProgress("Auto-loading last disk...", 80);
    for (int i = 0; i < (int)file_list.size(); i++) {
      if (file_list[i] == cfg_lastfile) {
        selected_index = i;
        break;
      }
    }
    size_t loaded = loadFileToRam(selected_index);
    autoloaded = (loaded > 0);
    if (autoloaded) {
      loaded_disk_index = selected_index;
      game_selected = findGameIndex(selected_index);
    }
    LOG("Auto-loaded: " + file_list[selected_index] + " (" + String(loaded) + " bytes)");
  }

  // ── WiFi Access Point + Web Server + Remote Dongle ──
  if (cfg_wifi_enabled || cfg_remote_enabled) {
    drawBootProgress(cfg_remote_enabled ? "Connecting to dongle..." : "Starting WiFi AP...", 85);
    if (initWiFiAP()) {
      if (cfg_wifi_enabled) {
        startWebServer();
        LOG("Web server ready at http://" + wifi_ap_ip);
      }
      if (cfg_remote_enabled) {
        LOG("Remote mode: connecting to " + cfg_remote_ssid + "...");
      }
    }
  }

  drawBootProgress("Starting USB...", 90);

  msc.vendorID("Gotek");
  msc.productID("Disk");
  msc.productRevision("1.0");
  msc.onRead(onRead);
  msc.onWrite(onWrite);
  msc.mediaPresent(autoloaded);
  msc.begin(msc_block_count, 512);
  USB.onEvent([](void*, esp_event_base_t, int32_t, void*){});
  USB.begin();
  if (!autoloaded) { tud_disconnect(); delay(100); }
  LOG("USB MSC initialized");

  drawBootProgress("Ready!", 100);
  delay(300);

  // Show appropriate screen
  if (autoloaded) {
    detail_filename = file_list[selected_index];
    current_screen = SCR_DETAILS;
    drawDetailsFromNFO(detail_filename);
  } else {
    current_screen = SCR_SELECTION;
    drawList();
  }

  LOG("Setup complete!");
}


// ============================================================================
// Touch handling — tap & swipe detection
// ============================================================================
// Swipe: finger moved > threshold pixels between touch-down and touch-up.
// Tap: finger stayed within threshold. Processed on release for reliability.
// Buttons are always checked first (priority over swipe/edge gestures).

// Navigate to previous/next game from detail page
void detailGoToPrev() {
  if (game_selected <= 0) return;
  game_selected--;
  selected_index = game_list[game_selected].first_file_index;
  detail_filename = file_list[selected_index];
  drawDetailsFromNFO(detail_filename);
}

void detailGoToNext() {
  if (game_selected >= (int)game_list.size() - 1) return;
  game_selected++;
  selected_index = game_list[game_selected].first_file_index;
  detail_filename = file_list[selected_index];
  drawDetailsFromNFO(detail_filename);
}

// Check if touch (px,py) hits a button rectangle
bool hitBtn(uint16_t px, uint16_t py, int bx, int by, int bw, int bh) {
  return (px >= bx && px <= bx + bw && py >= by && py <= by + bh);
}

// ============================================================================
// Touch helpers
// ============================================================================

void waitForRelease(unsigned long timeout_ms = 2000) {
  uint16_t dummy_x, dummy_y;
  unsigned long start = millis();
  while (touchRead(&dummy_x, &dummy_y)) {
    if (millis() - start > timeout_ms) break;
    delay(20);
  }
  last_touch_time = millis();
}

// ============================================================================
// Main loop — ORIGINAL WORKING touch model (release-based)
// ============================================================================
// This is the exact approach that worked before the UI changes:
//   - Track touch_active for press/release detection
//   - On release: determine tap vs swipe, then process
//   - waitForRelease() after heavy operations to prevent phantom taps
//   - Simple 200ms debounce

#define SWIPE_THRESHOLD 30

void loop() {
  // Process incoming HTTP requests (non-blocking)
  handleWebServer();

  uint16_t px = 0, py = 0;
  bool haveTouch = touchRead(&px, &py);

  if (ui_busy) {
    delay(10);
    return;
  }

  if (haveTouch) {
    if (!touch_active) {
      // Touch DOWN — start tracking
      touch_active = true;
      touch_start_x = px;
      touch_start_y = py;
      touch_start_time = millis();
    }
    // Update last known position while dragging
    touch_last_x = px;
    touch_last_y = py;
  } else if (touch_active) {
    // Touch UP — determine if it was a tap or swipe
    touch_active = false;
    unsigned long now = millis();

    // Debounce: ignore very quick phantom touches
    if (now - last_touch_time < 200) {
      delay(10);
      return;
    }

    int16_t dx = (int16_t)touch_last_x - (int16_t)touch_start_x;
    int16_t dy = (int16_t)touch_last_y - (int16_t)touch_start_y;

    if (abs(dx) > SWIPE_THRESHOLD || abs(dy) > SWIPE_THRESHOLD) {
      handleSwipe(dx, dy, touch_start_x, touch_start_y);
    } else {
      handleTap(touch_start_x, touch_start_y);
    }
    last_touch_time = millis();
  }

  delay(10);
}

// ============================================================================
// Handle swipe gestures
// ============================================================================
void handleSwipe(int16_t dx, int16_t dy, uint16_t startX, uint16_t startY) {
  int16_t absDx = abs(dx);
  int16_t absDy = abs(dy);

  if (current_screen == SCR_SELECTION) {
    // Vertical swipe in list area → scroll
    if (startY >= LIST_START_Y && startY < LIST_BOTTOM && absDy > absDx) {
      int scrollItems = absDy / LIST_ITEM_H;
      if (scrollItems < 1) scrollItems = 1;
      if (dy > 0) scroll_offset -= scrollItems;
      else        scroll_offset += scrollItems;
      int maxOff = (int)game_list.size() - items_per_page();
      if (maxOff < 0) maxOff = 0;
      if (scroll_offset > maxOff) scroll_offset = maxOff;
      if (scroll_offset < 0) scroll_offset = 0;
      drawList();
    }
  } else if (current_screen == SCR_DETAILS) {
    // Horizontal swipe → previous/next game
    if (absDx > absDy) {
      if (dx > 0) detailGoToPrev();
      else        detailGoToNext();
    }
  }
}

// ============================================================================
// Handle tap gestures
// ============================================================================
void handleTap(uint16_t px, uint16_t py) {

  // ══════════════════════════════════════
  // SELECTION SCREEN
  // ══════════════════════════════════════
  if (current_screen == SCR_SELECTION) {

    // Scroll UP/DOWN buttons (right edge, large touch targets)
    int scrollBtnX = gW - 44;
    int listH = LIST_BOTTOM - LIST_START_Y;
    int halfH = listH / 2;
    if (px >= scrollBtnX && py >= LIST_START_Y && py < LIST_BOTTOM) {
      int perPage = items_per_page();
      int maxOff = (int)game_list.size() - perPage;
      if (maxOff < 0) maxOff = 0;
      if (py < LIST_START_Y + halfH) {
        // UP button
        if (scroll_offset > 0) {
          scroll_offset--;
          drawList();
        }
      } else {
        // DOWN button
        if (scroll_offset < maxOff) {
          scroll_offset++;
          drawList();
        }
      }
      return;
    }

    // INFO button (bottom-right, 44px wide — same as chevrons)
    if (px >= gW - 44 && py >= gH - 42) {
      current_screen = SCR_INFO;
      drawInfoScreen();
      return;
    }

    // Game list area — tap item → go to detail page
    if (py >= LIST_START_Y && py < LIST_BOTTOM && px < gW - 44) {
      int idx = (py - LIST_START_Y) / LIST_ITEM_H + scroll_offset;
      if (idx >= 0 && idx < (int)game_list.size()) {
        game_selected = idx;
        selected_index = game_list[idx].first_file_index;
        detail_filename = file_list[selected_index];
        current_screen = SCR_DETAILS;
        drawDetailsFromNFO(detail_filename);
      }
      return;
    }
  }

  // ══════════════════════════════════════
  // DETAIL SCREEN
  // ══════════════════════════════════════
  else if (current_screen == SCR_DETAILS) {

    // BACK button (left-aligned, 148px wide)
    { int detBtnW = 148, detBtn1X = 10;
    if (px >= detBtn1X && px <= detBtn1X + detBtnW && py >= gH - 42) {
      current_screen = SCR_SELECTION;
      drawList();
      return;
    }}

    // INSERT/EJECT toggle button (right-aligned, 148px wide)
    { int detBtnW = 148, detBtn2X = gW - 10 - detBtnW;
    if (px >= detBtn2X && px <= detBtn2X + detBtnW && py >= gH - 42) {
      bool isCurrentLoaded = (loaded_disk_index == selected_index && loaded_disk_index >= 0);
      if (isCurrentLoaded) {
        // EJECT (unload)
        showBusyIndicator(cfg_remote_enabled ? "EJECTING REMOTE..." : "EJECTING...");
        waitForRelease();
        doUnload();
        hideBusyIndicator();
        drawDetailsFromNFO(detail_filename);
      } else {
        // INSERT (load) — remote mode sends to dongle, local loads to RAM
        showBusyIndicator(cfg_remote_enabled ? "SENDING..." : "INSERTING...");
        waitForRelease();
        doLoadSelected();
        hideBusyIndicator();
        drawDetailsFromNFO(detail_filename);
      }
      return;
    }}

    // Left/right edge tap: navigate to previous/next game
    if (py >= LIST_START_Y && py < gH - 48) {
      if (px <= 40) { detailGoToPrev(); return; }
      if (px >= gW - 40) { detailGoToNext(); return; }
    }

    // Disk selector buttons (multi-disk games)
    if (disk_set.size() > 1) {
      int diskRowH = 34;
      int diskTop = gH - 42 - diskRowH;
      if (py >= diskTop && py <= diskTop + 28) {
        int btnW = 44, gap = 4;
        int numDisks = disk_set.size();
        int totalW = numDisks * btnW + (numDisks - 1) * gap;
        int startX = (gW - totalW) / 2;
        int hitIdx = (px - startX) / (btnW + gap);
        int btnLeft = startX + hitIdx * (btnW + gap);
        if (hitIdx >= 0 && hitIdx < numDisks &&
            px >= btnLeft && px <= btnLeft + btnW) {
          selected_index = disk_set[hitIdx];
          detail_filename = file_list[selected_index];
          showBusyIndicator(cfg_remote_enabled ? "SENDING DISK..." : "SWITCHING DISK...");
          waitForRelease();
          doLoadSelected();
          hideBusyIndicator();
          drawDetailsFromNFO(detail_filename);
        }
      }
    }
  }

  // ══════════════════════════════════════
  // INFO SCREEN
  // ══════════════════════════════════════
  else if (current_screen == SCR_INFO) {
    // Must match drawInfoScreen() button layout exactly
    int iMarginX = 4, iGap = 4, iBtnH = 26;
    int iBtnW = (gW - 2 * iMarginX - 2 * iGap) / 3;
    int iBtnY = gH - iBtnH - 4;
    int iBtn1X = iMarginX;
    int iBtn2X = iBtn1X + iBtnW + iGap;
    int iBtn3X = iBtn2X + iBtnW + iGap;
    if (px >= iBtn1X && px <= iBtn1X + iBtnW && py >= iBtnY) {
      current_screen = SCR_SELECTION;
      drawList();
      return;
    }
    if (px >= iBtn2X && px <= iBtn2X + iBtnW && py >= iBtnY) {
      cycleTheme();
      drawInfoScreen();
      return;
    }
    if (px >= iBtn3X && px <= iBtn3X + iBtnW && py >= iBtnY) {
      showBusyIndicator("SCANNING...");
      g_mode = (g_mode == MODE_ADF) ? MODE_DSK : MODE_ADF;
      file_list = listImages();
      buildDisplayNames(file_list);
      sortByDisplay();
      buildGameList();
      selected_index = 0;
      game_selected = 0;
      scroll_offset = 0;
      hideBusyIndicator();
      drawInfoScreen();
      return;
    }
  }
}
