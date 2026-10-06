// ESP32-P4 7" (Guition JC1060P470C_I_W_Y, 1024x600) - USB MSC RAM Disk + ADF/DSK Browser
// 5.9.42-lab1-P4G7 = the 4.3" P4 5.9.42-lab1-P4 source (NEO look) + the JC1060 board layer: JD9165 1024x600 DSI panel
// (p4_display.c: the P4's portrait compose buffer, 600x1024, turned upright into the landscape panel = a 1024x600
// canvas; the round P4R port's method), GT911 touch on SDA7/SCL8 (raw 1024x600), microSD 4-bit on CLK43/CMD44/D0-D3
// 39-42 (card power = the P4's LDO channel 4 through a FET, always on). Same JC-ESP32P4-M3 module family as the 4.3".
// Board: ESP32P4 Dev Module | Chip Variant "Before v3.00" (v1.3 silicon, 360 MHz) | USB-OTG (TinyUSB) | CDC DISABLED |
// PSRAM Enabled | 16MB Flash | Partition Custom | JD9165 1024x600 DSI | GT911 touch
// Old glass (no "V2" on the back label)? Set JC1060_OLD_PANEL 1 in p4_display.h.

#include <Arduino.h>
#include "USB.h"
#include "USBMSC.h"
// Merge step 1: the shared WebDAV client (see firmware/shared/README.md).
// Self-contained — WiFi.h/WiFiClientSecure only; settings arrive through
// DavConfig via configure(), logging through a callback. Nothing global.
#include "../shared/webdav_client.h"
#include <FS.h>
#include <SD_MMC.h>
#include "driver/sdmmc_host.h"     // 5.9.38: raw sector-0 peek when the card will not mount
#include "driver/sdmmc_defs.h"
#include "sdmmc_cmd.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_interface.h"
#include "p4_display.h"   // P4G7: JD9165 1024x600 MIPI-DSI backend (compose 600x1024 -> upright landscape)
// [P4] AXS15231B QSPI panel driver removed (DSI on P4)
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
#include <deque>      // 5.9.41-lab14b: g_games (no single multi-MB block needed)
#include <algorithm>
#include <set>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/time.h>   // lab15d: settimeofday (clock starts at the build time)
#include <errno.h>      // lab15d: save-failure detail
#include "gti_fatwalk.h"   // 5.9.41-lab14: read .nfo heads straight off the directory entry (no open-by-name)
#include "gti_sdguard.h"   // lab14g: FatFs metadata guard between FatFs and the SD driver
#include "gti_pathlist.h"  // lab15a: the disk-image list, compact (each folder once, each file name once)
#include "gti_bufio.h"    // lab15f: block reads/writes for the cache files
#include "gti_gamestore.h" // lab15b: a game record is 36 B with its text packed in one PSRAM arena (was ~177 B with 3-4 heap blocks)
#include "diskio_impl.h"   // lab14g: ff_diskio_register / ff_diskio_get_drive
#include "diskio_sdmmc.h"  // lab14g: ff_diskio_register_sdmmc / ff_diskio_get_pdrv_card
#include "driver/gpio.h"

#define FW_VERSION "5.9.42-lab6-P4G7"   // lab6-P4G7 (Mez, 5 Oct): NEO is a switch again - CONFIG.TXT NEO=ON/OFF and Settings > NEO: ON/OFF (default ON). NEO=OFF brings back the classic layout with the colour THEME (NAVY ... OMEGA; THEME= works again), COMPACT and BUTTONS; the top and bottom bars follow the look (NEO 40/58 px, classic 28/54 px as before 16b); switching is live, no reboot | lab5-P4G7 (kodak80, 4 Oct): MATRIX saver fills the whole width; top-left brand is the OMEGAWARE omega + GTi (was "GOTEK GTi"); SYNTHWAVE cracktro grid symmetric (was 6 lines left, 12 right) and scaled to the screen (columns were capped at 64 = 768 px of the 7"'s 1024; the 4.3" lost 32 px in landscape); gti.log now logs every disk insert / eject ([load] name, KB, mode, USB host yes/no; [eject]) and every USB host connect / disconnect ([usb]) - so "the laptop never saw it" can be read off the log | lab4-P4G7: every dongle join starts with a 0.3 s channel-6 scan (gti.log: joins without one always timed out after 12-15 s, joins straight after a scan took ~3.8 s) - a send or eject is ~15 s quicker | lab3-P4G7: wireless fixed on the P4 - the dongle's real Wi-Fi name (GotekOMEGA-XXXX) is remembered (CONFIG.TXT XIAO_SSID=) or looked up by a quick channel-6 scan before every join, two join tries; EJECT really ejects the dongle (it was a no-op on the P4) after pulling the game saves back; saves are also pulled before the next disk goes to the same dongle; the save pull used the old plain name and could never join a Webby 1.6.8+ dongle | lab2-P4G7: radio diagnosis - the C6 co-processor firmware version goes into gti.log at boot and on Settings > TEST TOOLS; every wireless step ([P4WIFI] scan RSSI, join time, bytes sent, dongle answer) goes into gti.log; the top-bar chip no longer says OFFLINE on the P4 (it cannot hear the dongle heartbeat - ESP-NOW receive is not available on the P4) | P4G7: Guition JC1060P470C 7" 1024x600 bring-up (4.3" P4 5.9.42-lab1 + JD9165 panel, GT911 touch, 4-bit SD; NEO look at 1024x600) | 5.9.42-lab1-P4 (4 Oct 2026) = 5.9.41-lab16f-P4, renamed only (no code change): the NEO look, first P4 lab release on the web flasher | lab16f-P4: PORTRAIT REEL RUNS UP AND DOWN - covers stacked vertically (next one below), swipe up/down, coast + snap as before; round up/down arrows on the right; tap above/below the centre cover steps one; title / DISK pills / dots in a fixed strip above the bottom buttons. Landscape unchanged | lab16e-P4: portrait reel centred - cover + title + disks + dots sit in the middle of the screen (was at the top with empty space under it); landscape unchanged | lab16d-P4: game panel cover is BIG (full panel width, picture centred), favourite = star badge in the cover's top-right corner (tap it), the .rtfm book button is back in its usual place on the cover's left edge (16b had moved it to a MANUAL pill), HD chip bottom-right; title + "n disks - ADF" under the cover, then the description and the DISK pills | lab16c-P4 (Mez's photos of 16b): NEO text at size 2+ is drawn bold (one pixel wider strokes) so dark text on the gold / cyan / green pills reads like the mock-up; reel covers drop the grey letterbox baked into the tile (picture cut out, frame hugs it, same cut on the micro-thumbs while it slides); long names wrap to two lines under the reel cover instead of being cut; in the game panel a long name moves under the cover, full width | lab16b-P4: NEO is the only look for now (THEME, BUTTONS and COMPACT are off the Settings list; CONFIG THEME= is ignored) and the screens follow the mock-ups: list on the LEFT in a rounded panel, A-Z strip, game panel on the RIGHT (cover, title, disks/format, FAV and MANUAL pills, description, DISK pills), bottom bar PREV / NEXT / INSERT / REEL / CONFIG; top bar 40 px with the firmware version under GOTEK GTi, tap the library name to switch library (or open categories); reel arrows; Settings = two columns of capsules + a row of action pills + BACK / PAGE / TEST TOOLS / NEXT; USER DISKS moved into Settings | lab16a-P4: NEW LOOK "NEO" (Settings > THEME > NEO, or THEME=NEO): navy-to-purple background with faint circuit lines; top bar GOTEK GTi + firmware version, library + game count, a CABLE/DONGLE state chip and a gear that opens Settings; reel: gold-framed glowing centre cover, title under it, DISK pills, page dots, bottom bar INSERT / < ALL > / LIST / ROLL; list: gold-framed selection, IN DRIVE tag, cyan INSERT; Settings: capsule rows with the value in a coloured chip, last tap outlined in gold. Other themes look and work as before | lab15q-P4 (from the 3.5" 15o + 15q): Polish (8-Bitz's text) and Czech on the screen (LANG=PL / CS, CZ accepted); the reel bar's ALL/FAV/MOST word shrinks when too long; STANDALONE banner centred on the translated word. (3.5" 15p, the GTi_Omega Wi-Fi, does not apply: the P4's C6 radio never runs an access point) | lab15n (P4 only): bigger disk buttons on the list page (86x34, text size 2; were the 3.5"'s 44x20) + bigger page button | lab15l+m (from the 4.3" S3): the list cover picture drawn as big as the frame allows (tile letterbox + black PAL bands cut off), frame hugs the picture | lab15j (from the 3.5"): tap the game text in the list = the whole .nfo full-screen in the manual reader | P4 port-sync: the 3.5" 5.9.41-lab15i (lab14 walker, SD guard, never-format, compact library, safe saves + SD lock, per-dongle saves, one LIBRARY button, wireless offset 13, take-over check over TCP) + the P4 board layer (ST7701 DSI, GT911, C6 radio + self-update, 800x480 layout) | was 5.9.13-P4
#include "retro_assets.h"
#include "omega_logo.h"   // the 1991 OMEGAWARE logo (Dimmy)
#include "espnow_server.h"
#include <WiFi.h>          // P4: brings up the C6 co-processor link (esp-hosted over SDIO)
#include <esp_hosted.h>     // P4: C6 firmware version query + slave OTA (self-update)
#include <esp_partition.h> // P4: read the embedded C6 image from the c6fw partition
#include <Update.h>            // v5.3: self-flash an app image off the SD (OTA)
#include "esp_ota_ops.h"       // v5.3: OTA slot query + rollback-validate handshake
#include "esp_log.h"           // 5.9.29: capture IDF OTA/image log lines into /gti.log
#include "esp_heap_caps.h"     // lab14e: failed-allocation hook for the crash breadcrumbs

extern "C" { bool tud_mounted(void); void tud_disconnect(void); void tud_connect(void); void* ps_malloc(size_t size); }

// ════════════════════════════════════════════════════════════════════════════
// HARDWARE
// ════════════════════════════════════════════════════════════════════════════
#define LCD_WIDTH  600    // P4G7: the compose buffer (portrait-native like the 4.3") = P4DISP_CW; landscape canvas 1024x600
#define LCD_HEIGHT 1024   //       = P4DISP_CH. p4_display.c turns it upright into the 1024x600 panel
// Virtual canvas + rotation. g_rot: 0=landscape, 1=portrait, 2=landscape-flipped,
// 3=portrait-flipped. Each ROTATE tap advances 90 degrees. gW/gH swap for portrait.
static int gW=LCD_HEIGHT, gH=LCD_WIDTH;   // P4G7: 1024x600 landscape canvas
static int g_rot=0;   // P4: default LANDSCAPE (panel mounted landscape); ROTATE cycles 0/1/2/3
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
#define SD_CLK 43   // P4G7 (JC1060): microSD CLK=43 CMD=44 D0-D3=39-42 (Guition demo + schematic); 4-bit first, 1-bit fallback
#define SD_CMD 44
#define SD_D0 39
#define SD_D1 40
#define SD_D2 41
#define SD_D3 42
static bool g_sd_4bit=false;   // P4G7: true when the 4-bit mount took
static int g_sd_freq=20000;   // 5.3.5: SDIO clock kHz. 20000=safe default, 40000=fast (SDSPEED= in CONFIG.TXT, auto-falls back)
#define ROWS_PER_STRIP 10
// ── 5.9.33-lab3: panel-push + reel profiling knobs ─────────────────────────
// gfx_flush() pushes the 307 KB PSRAM framebuffer to the panel in strips, with a
// fixed delayMicroseconds() after each one. At 10 rows that is 48 strips x 500us
// = 24 ms of pure SLEEP in every single frame, before a pixel is drawn — a hard
// ~27 FPS ceiling on the reel that no amount of caching can touch.
// Both are runtime now (STRIPROWS= / FLUSHUS= in CONFIG.TXT) and both DEFAULT TO
// TODAY'S VALUES, so an untouched card behaves exactly as 5.9.32 did.
//   STRIPROWS=40  -> 12 strips instead of 48 (24 ms of delay becomes 6 ms)
//   FLUSHUS=0     -> no inter-strip delay at all (fastest; watch for tearing)
// If the panel tears or shows torn bands, raise FLUSHUS or drop STRIPROWS back.
#define MAX_STRIP_ROWS 40
static int g_strip_rows=ROWS_PER_STRIP;   // STRIPROWS=  (clamped to g_strip_cap)
static int g_strip_cap =ROWS_PER_STRIP;   // what the DMA buffer was actually sized for
static int g_flush_us  =500;              // FLUSHUS=    (microseconds per strip)
// Reel frame profiler (REELPROF=ON / Settings). Costs two micros() calls a frame
// when off. When on it prints, every ~1.5 s, exactly where the frame time went.
static bool     g_reelprof=false;
static uint32_t g_rp_frames=0,g_rp_blit=0,g_rp_sav=0,g_rp_clear=0,g_rp_draw=0,g_rp_flush=0,g_rp_t0=0;

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
#if 0  // [P4] AXS15231B init table unused on DSI
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
#endif

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
static uint16_t *framebuffer = NULL;   // P4: the 480x800 compose buffer (PSRAM)
static JPEGDEC jpegdec;
static PNG     pngdec;   // v4.8.4 PNG cover support

static inline uint16_t swap16(uint16_t c){return c;}   // P4 DSI: native LE, no swap
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
// lab16a-P4 NEO look: while a NEO screen draws (g_neo_depth>0), text whose background is the theme
// background (g_txt_key) is drawn WITHOUT its box, so it sits on the background picture.
static bool     g_neo=false;          // the NEO look is active (set by applyTheme from g_neo_on)
static bool     g_neo_on=true;        // lab4-P4: CONFIG.TXT NEO=ON/OFF (default ON). OFF = classic layout + the colour THEME
static int      g_neo_depth=0;        // >0 while a NEO screen is drawing (NeoScope)
static uint16_t g_txt_key=0;          // = COL_BG, set by applyTheme
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
  const bool tr=(g_neo_depth>0&&text_bg==g_txt_key);   // lab16a-P4: NEO - no box behind text on the background picture
  if(g_neo_depth>0&&text_size>=2){   // lab16c-P4: bold - background first, then every lit pixel one pixel wider
    for(unsigned i=0;i<text.length();i++){char c=text[i];if(c<32||c>126)continue;
      const uint8_t*data=font6x8[c-32];
      if(!tr)gfx_fillRect(text_x,text_y,6*text_size,8*text_size,text_bg);
      for(int col=0;col<6;col++){uint8_t bits=pgm_read_byte(&data[col]);
        for(int row=0;row<8;row++)if(bits&(1<<row))gfx_fillRect(text_x+col*text_size,text_y+row*text_size,text_size+1,text_size,text_fg);}
      text_x+=6*text_size;}
    return; }
  for(unsigned i=0;i<text.length();i++){char c=text[i];if(c<32||c>126)continue;
    const uint8_t*data=font6x8[c-32];
    for(int col=0;col<6;col++){uint8_t bits=pgm_read_byte(&data[col]);
      for(int row=0;row<8;row++){bool on=(bits&(1<<row))!=0; if(tr&&!on)continue; uint16_t color=on?text_fg:text_bg;
        for(int dy=0;dy<text_size;dy++)for(int dx=0;dx<text_size;dx++)
          gfx_drawPixel(text_x+col*text_size+dx,text_y+row*text_size+dy,color);}}
    text_x+=6*text_size;}
}
static void gfx_print(const char*s){gfx_print(String(s));}

// ── DIAG-DISP: live diagnostic overlay (Settings toggle / CONFIG.TXT DIAGDISP=) ──────
// Drawn on top of the framebuffer at the end of every gfx_flush when enabled: FPS,
// free SRAM + PSRAM, uptime, CPU temp. FPS is measured from the flush interval.
static bool     g_diagdisp=false;   // DIAGDISP= / Settings toggle
// ── 5.9.32-lab2: EXPERIMENT SWITCHES (both default to normal behaviour) ─────
// NOCACHE=ON  : ignore EVERY on-SD cache (.index .gamecache .nfocache .gti_micro.pk .tnl).
//               Nothing is deleted — the files stay put and are simply not read or written,
//               so flipping back restores the cached behaviour instantly. This is the
//               "broken" control: every boot re-walks, re-builds and re-reads from scratch.
// COVERS=OFF  : no cover art at all. findJPGFor refuses, so nothing is ever decoded and the
//               list/reel fall back to letter placeholders. Isolates list + sidecar cost
//               from cover-decode noise. (The micro block still allocates, greyed — leaving
//               that path intact avoids NULL-tile handling just for an experiment.)
static bool     g_nocache=false;
static bool     g_covers_on=true;
static bool     g_reelborder=true;  // REELBORDER= / Settings (MasterTelly CR): ON=frame around reel covers (default), OFF=clean/frameless (the loaded game is still marked green)
static bool     g_lastused=false;   // LASTUSED= / Settings: ON=on boot, restore the selection to the game you last loaded (remembered in /.gtilastused)
static uint32_t g_diag_last=0;      // last gfx_flush millis (for FPS)
static float    g_diag_fps=0;       // smoothed frames/sec
static void drawDiagOverlay(){
  int bx=2,by=2,bw=98,bh=60;
  if(bw>gW-4)bw=gW-4; if(bh>gH-4)bh=gH-4;
  // draw at full-screen clip so a scroll/marquee clip left set can't crop us
  int cx0=g_clip_x0,cy0=g_clip_y0,cx1=g_clip_x1,cy1=g_clip_y1;
  g_clip_x0=0;g_clip_y0=0;g_clip_x1=gW;g_clip_y1=gH;
  gfx_fillRect(bx,by,bw,bh,0x0000);
  gfx_drawRect(bx,by,bw,bh,0x07E0);
  gfx_setTextSize(1);
  char l[40];
  gfx_setTextColor(0x07E0,0x0000); snprintf(l,sizeof l,"FPS %.1f",g_diag_fps);                       gfx_setCursor(bx+4,by+4);  gfx_print(l);
  gfx_setTextColor(0xFFFF,0x0000); snprintf(l,sizeof l,"SRAM %uK",(unsigned)(ESP.getFreeHeap()/1024)); gfx_setCursor(bx+4,by+14); gfx_print(l);
  snprintf(l,sizeof l,"PSRAM %uK",(unsigned)(ESP.getFreePsram()/1024));                                gfx_setCursor(bx+4,by+24); gfx_print(l);
  uint32_t up=millis()/1000;
  snprintf(l,sizeof l,"UP %02u:%02u:%02u",(unsigned)(up/3600),(unsigned)((up/60)%60),(unsigned)(up%60));gfx_setCursor(bx+4,by+34); gfx_print(l);
  gfx_setTextColor(0xFD20,0x0000); snprintf(l,sizeof l,"TEMP %.0fC",temperatureRead());                gfx_setCursor(bx+4,by+44); gfx_print(l);
  g_clip_x0=cx0;g_clip_y0=cy0;g_clip_x1=cx1;g_clip_y1=cy1;
}

static void gfx_flush(){
  if(!framebuffer)return;
  uint32_t _fl_t0=micros();
  { uint32_t now=millis(); if(g_diag_last){ float dt=(float)(now-g_diag_last); if(dt>0){ float f=1000.0f/dt; g_diag_fps = g_diag_fps>0 ? g_diag_fps*0.85f+f*0.15f : f; } } g_diag_last=now; }
  if(g_diagdisp) drawDiagOverlay();
  p4disp_present(framebuffer);   // P4: DSI page-flip of the 480x800 compose (STRIPROWS=/FLUSHUS= parse but do nothing here)
  g_rp_flush+=micros()-_fl_t0;
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
// ── Cover ingest: one size-agnostic, garbage-proof decode point (v1) ──────────
#define COVER_TILE_PX        220               // decode-budget long edge (== CAR_TILE); both panel + reel share this. P4: 220 (was 150 on the S3)
#define CAR_TILE  220                            // P4: bigger reel tiles for the 800x480 panel (S3 boards: 150)
#define COVER_FILE_CAP       (4u*1024u*1024u)  // max raw cover file loaded into PSRAM; bigger -> placeholder
#define COVER_DECODE_BUDGET  (4u*1024u*1024u)  // max decoded RGB565 bytes (post hardware-scale); bounds intermediate + resident cache
static int g_covermin=140;   // COVERMIN: skip covers whose short side < this many px (0=off, SD-editable)
static bool g_reelfilter=false;   // v5.9.2 REELFILTER: reel shows only covers that pass COVERMIN (A-Z list stays full)
static bool g_cover_flags_ready=false; static int g_cover_flags_n=-1;
static void ensureCoverFlags();   // fwd: set each game cover_ok from its cached thumb
// ── 5.9.34-lab4: the LIST cover panel draws from the reel's tile cache ──────
// drawCoverPanel() used to call gfx_drawJpgFile() on EVERY selection change: open
// the ~500 KB cover in the game folder (a FAT walk of a folder that can hold
// thousands of entries), decode the whole JPEG, then scale it down to 138x112.
// No cache of any kind — the same cover was re-decoded from scratch every time you
// moved the cursor onto it. Meanwhile the reel already had that exact cover sitting
// on the card as a 45 KB pre-decoded .tnl tile, and in PSRAM in a 48-slot LRU.
// COVER_TILE_PX == CAR_TILE == 150, so the panel was ALREADY looking at a 150px
// decode: the tile is the same pixels with the decode already paid for.
// LISTTILE=OFF restores the old decode-every-time path for an A/B.
static bool g_listtile=true;      // LISTTILE=
static uint16_t* carTileEx(int gi,bool mayDecode,bool*okOut);   // fwd (defined with the reel)
static void carBlit(uint16_t*tile,int srcDim,int cx,int cy,int w,int h,int dim);
static int g_cb_sx0=0,g_cb_sy0=0,g_cb_sw=0,g_cb_sh=0;   // lab15l: optional source rect inside the tile for carBlit (sw=0 = whole tile, the reel's case)
static void tilePicRect(const uint16_t*t,int n,int&x0,int&y0,int&w,int&h);   // lab15l: fwd

// A ".png" beside a ".jpg" cover (boxart ships both) — the progressive/failed-JPEG fallback.
static bool pngSiblingFor(const String& jpgPath, String& out){
  if(coverIsPng(jpgPath)) return false;
  int d=jpgPath.lastIndexOf('.'); if(d<0) return false;
  String cand=jpgPath.substring(0,d)+".png";
  struct stat st; String v="/sdcard"+cand;
  if(stat(v.c_str(),&st)==0 && st.st_size>0){ out=cand; return true; }
  return false;
}

// jpeg_tmp_buf is now a RESIDENT 1-entry natural-decode cache (was freed after every use).
static String   g_dec_path = "";       // cover path currently held in jpeg_tmp_buf ("" = none)
static uint16_t* g_dec_buf = NULL;      // pointer we cached (guards against external reuse of jpeg_tmp_buf)
static bool     g_dec_failed = false;   // last decode of g_dec_path failed (negative cache)

// ── lab14f: WHERE FILES START, remembered from the scan walk ──────────────────
// The cover-cache build opened every cover BY NAME: a stat() and an open(), each a search of
// a letter folder with ~5,000 entries (~1 MB of directory). Measured on the 26 GB card: 0.57 s
// a game, of which ~0.1 s is the real read + decode. The walk already passes every cover's
// directory entry, so it notes the start cluster, size and FAT date/time (fw_entry_clust), and
// the build opens the file from that (fw_open_at) - no search. Same for the .thumbs tiles,
// listed once per build. Anything not found here falls back to the normal by-name path.
// (struct FwLoc lives in gti_fatwalk.h so the Arduino prototype generator can see it)
static std::vector<FwLoc> g_coverloc;           // covers seen by the walk, sorted by h (lower-case VFS path hash)
static std::vector<FwLoc> g_thumbloc;           // .tnl tiles, listed at the start of buildThumbs
static FATFS* g_fw_fs=nullptr; static WORD g_fw_fsid=0;   // the card's volume + mount id at walk time
static FIL*   g_fw_fil=nullptr;                  // one reusable FIL (4 KB sector buffer inside)
static uint32_t g_fw_open_at=0, g_fw_open_at_fail=0;
static uint64_t coverHash(const String&s);       // fwd (defined with the walk harvest)
static const FwLoc* fwLocFind(const std::vector<FwLoc>& v,uint64_t h){
  auto it=std::lower_bound(v.begin(),v.end(),h,[](const FwLoc&a,uint64_t k){return a.h<k;});
  return (it!=v.end()&&it->h==h)?&*it:nullptr;
}
static const FwLoc* fwLocFor(const std::vector<FwLoc>& v,const String& vfsPath){
  if(v.empty()) return nullptr; String l=vfsPath; l.toLowerCase(); return fwLocFind(v,coverHash(l));
}
// Read the whole file (n bytes) from its recorded start cluster. False = use the normal path.
static bool fwReadAt(const FwLoc* L,uint8_t* dst,size_t n){
  if(!L||!g_fw_fs||n!=L->size) return false;
  if(!g_fw_fil){ g_fw_fil=(FIL*)ps_malloc(sizeof(FIL)); if(!g_fw_fil) return false; }
  if(!fw_open_at(g_fw_fil,g_fw_fs,g_fw_fsid,L->sclust,L->size)){ g_fw_open_at_fail++; return false; }
  UINT br=0; FRESULT r=f_read(g_fw_fil,dst,(UINT)n,&br); f_close(g_fw_fil);
  if(r!=FR_OK||br!=n){ g_fw_open_at_fail++; return false; }
  g_fw_open_at++; return true;
}
static void fwLocFree(){ std::vector<FwLoc>().swap(g_coverloc); std::vector<FwLoc>().swap(g_thumbloc); }

// Decode ONE file into jpeg_tmp_buf at natural aspect, long side scaled toward budgetLong.
// Frees its own file buffer. On failure: frees jpeg_tmp_buf, sets NULL, returns false.
static bool _coverDecodeRaw(const String& path, int budgetLong){
  uint8_t* buf=nullptr; size_t sz=0;
  { const FwLoc* L=fwLocFor(g_coverloc,path);     // lab14f: straight from the walk, no name search
    if(L && L->size>0 && L->size<=COVER_FILE_CAP){ buf=(uint8_t*)ps_malloc(L->size);
      if(buf && fwReadAt(L,buf,L->size)) sz=L->size; else { if(buf)free(buf); buf=nullptr; } } }
  if(!buf){                                        // the by-name path (runtime, or not in the walk)
  String vfsPath="/sdcard"+path; struct stat st;
  if(stat(vfsPath.c_str(),&st)!=0 || st.st_size==0 || (uint32_t)st.st_size>COVER_FILE_CAP) return false;
  sz=(size_t)st.st_size;
  File f=SD_MMC.open(path.c_str(),"r"); if(!f) return false;
  buf=(uint8_t*)ps_malloc(sz); if(!buf){ f.close(); return false; }
  if(f.read(buf,sz)!=(int)sz){ free(buf); f.close(); return false; } f.close();
  }
  int djw=0,djh=0;
  if(coverIsPng(path)){
    if(pngdec.openRAM(buf,sz,png_buf_cb)!=PNG_SUCCESS){ free(buf); return false; }
    int jw=pngdec.getWidth(), jh=pngdec.getHeight();
    if(jw<=0||jh<=0 || (size_t)jw*jh*2>COVER_DECODE_BUDGET){ pngdec.close(); free(buf); return false; } // PNGdec has no HW scale
    if(g_covermin>0&&(jw<g_covermin||jh<g_covermin)){ pngdec.close(); free(buf); return false; }   // COVERMIN: skip low-res cover
    djw=jw; djh=jh;
    jpeg_tmp_buf=(uint16_t*)ps_malloc((size_t)djw*djh*2);
    if(!jpeg_tmp_buf){ pngdec.close(); free(buf); return false; }
    memset(jpeg_tmp_buf,0,(size_t)djw*djh*2); jpeg_tmp_w=djw; jpeg_tmp_h=djh;
    int rc=pngdec.decode(NULL,0); pngdec.close(); free(buf);
    if(rc!=PNG_SUCCESS){ free(jpeg_tmp_buf); jpeg_tmp_buf=NULL; return false; }
  } else {
    if(!jpegdec.openRAM(buf,sz,jpeg_buf_cb)){ free(buf); return false; }        // progressive/corrupt header -> false
    int jw=jpegdec.getWidth(), jh=jpegdec.getHeight();
    if(jw<=0||jh<=0){ jpegdec.close(); free(buf); return false; }
    if(g_covermin>0&&(jw<g_covermin||jh<g_covermin)){ jpegdec.close(); free(buf); return false; }   // COVERMIN: skip low-res cover
    int longSide=max(jw,jh), div=1, opt=0;                                       // HW downscale so even 40 MP lands near budget
    if(longSide>=budgetLong*8){ opt=JPEG_SCALE_EIGHTH; div=8; }
    else if(longSide>=budgetLong*4){ opt=JPEG_SCALE_QUARTER; div=4; }
    else if(longSide>=budgetLong*2){ opt=JPEG_SCALE_HALF;  div=2; }
    djw=jw/div; djh=jh/div;
    if(djw<=0||djh<=0||(size_t)djw*djh*2>COVER_DECODE_BUDGET){ jpegdec.close(); free(buf); return false; }
    jpeg_tmp_buf=(uint16_t*)ps_malloc((size_t)djw*djh*2);
    if(!jpeg_tmp_buf){ jpegdec.close(); free(buf); return false; }
    memset(jpeg_tmp_buf,0,(size_t)djw*djh*2); jpeg_tmp_w=djw; jpeg_tmp_h=djh;
    int rc=jpegdec.decode(0,0,opt); jpegdec.close(); free(buf);                  // progressive that passed the header -> rc==0
    if(!rc){ free(jpeg_tmp_buf); jpeg_tmp_buf=NULL; return false; }
  }
  return true;
}

// Cached, garbage-proof cover decode shared by the cover panel AND the reel tile builder.
// budgetLong fixed at COVER_TILE_PX so both share one decode keyed by path.
static bool coverDecodeNatural(const String& path, int budgetLong, int* ow, int* oh){
  if(g_dec_path==path && jpeg_tmp_buf!=NULL && jpeg_tmp_buf==g_dec_buf){ *ow=jpeg_tmp_w; *oh=jpeg_tmp_h; return true; }
  if(g_dec_path==path && g_dec_failed && g_dec_buf==NULL){ *ow=*oh=0; return false; }
  if(jpeg_tmp_buf!=NULL && jpeg_tmp_buf==g_dec_buf){ free(jpeg_tmp_buf); }   // free only if it's still OURS
  jpeg_tmp_buf=NULL; g_dec_buf=NULL;
  g_dec_path=path; g_dec_failed=false;
  bool ok=_coverDecodeRaw(path,budgetLong);
  if(!ok){ String sib; if(pngSiblingFor(path,sib)) ok=_coverDecodeRaw(sib,budgetLong); }
  if(!ok){ if(jpeg_tmp_buf){ free(jpeg_tmp_buf); jpeg_tmp_buf=NULL; } g_dec_failed=true; g_dec_buf=NULL; *ow=*oh=0; return false; }
  g_dec_buf=jpeg_tmp_buf; *ow=jpeg_tmp_w; *oh=jpeg_tmp_h; return true;
}

// Fit-blit a cover into (x,y,maxW,maxH). Returns false when nothing legible was drawn
// (caller then paints the letter placeholder). Does NOT free jpeg_tmp_buf — the cache owns it.
static bool gfx_drawJpgFile(const String& path, int x, int y, int maxW, int maxH){
  int jw, jh;
  if(!coverDecodeNatural(path, COVER_TILE_PX, &jw, &jh)) return false;
  float scX=(float)maxW/jw, scY=(float)maxH/jh, sc=min(scX,scY);
  if(sc>1.0f)sc=1.0f;
  int dw=(int)(jw*sc), dh=(int)(jh*sc);
  if(dw<=0||dh<=0) return false;
  int ox=x+(maxW-dw)/2, oy=y+(maxH-dh)/2;
  for(int r=0;r<dh;r++){int srcY=(int)(r/sc);if(srcY>=jh)srcY=jh-1;
    for(int c=0;c<dw;c++){int srcX=(int)(c/sc);if(srcX>=jw)srcX=jw-1;
      int vx=ox+c,vy=oy+r;
      if(vx>=0&&vx<gW&&vy>=0&&vy<gH) fb_setPixel(vx,vy,jpeg_tmp_buf[srcY*jw+srcX]);}
    if(r%20==0)yield();}
  return true;
}

// ── Display init (from Dimi) ──
static void displayInit(){
  // P4: ST7701 480x800 MIPI-DSI (replaces the JC3248 AXS15231B QSPI panel).
  framebuffer=(uint16_t*)ps_malloc((size_t)LCD_WIDTH*LCD_HEIGHT*2);
  if(!framebuffer){Serial.println("FATAL: fb alloc");while(1)delay(1000);}
  if(!p4disp_init()){Serial.println("FATAL: JD9165 DSI bring-up");while(1)delay(1000);}   // P4G7
  p4disp_backlight(true);
  g_strip_cap=MAX_STRIP_ROWS;   // no DMA strip buffer on the P4; keeps the STRIPROWS= clamp satisfied
}

static int16_t g_tlog[16][2]; static volatile int g_tlog_n=0; static int g_tlog_done=0;   // P4G7 bring-up: first 16 raw touches for gti.log
                                                                                          // (logged from loop() only - never from the touch read)
// ── Touch (from Dimi) ──
static uint8_t gTouchPts=0;static uint16_t gTouchX=0,gTouchY=0;
// P4: GT911 on I2C SDA=7/SCL=8. Touch RST/INT are NC on this board (vendor BSP) - not driven.
#define GT_SDA 7
#define GT_SCL 8
#define GT_RST 22
#define GT_INT 21
static uint8_t GT_ADDR=0x5D;
static bool gtRd(uint16_t reg,uint8_t*d,int n){
  Wire.beginTransmission(GT_ADDR);Wire.write(reg>>8);Wire.write(reg&0xFF);
  if(Wire.endTransmission(false)!=0)return false;
  int g=Wire.requestFrom((int)GT_ADDR,n);for(int i=0;i<n&&Wire.available();i++)d[i]=Wire.read();return g==n;}
static bool gtWr8(uint16_t reg,uint8_t v){Wire.beginTransmission(GT_ADDR);Wire.write(reg>>8);Wire.write(reg&0xFF);Wire.write(v);return Wire.endTransmission()==0;}
static void touchInit(){
  // JC4880P443C: touch RST/INT are NOT wired to the MCU (vendor BSP: BSP_LCD_TOUCH_RST/INT = NC).
  // The GT911 powers up on its own on the shared I2C bus (SDA=7 / SCL=8) - no GPIO reset dance
  // (driving GPIO21/22 was the community guess; wrong pins, and can disturb the bus). Just probe.
  Wire.begin(GT_SDA,GT_SCL,400000);
  delay(60);                                   // settle after power-on
  Wire.beginTransmission(0x5D);
  if(Wire.endTransmission()!=0){Wire.beginTransmission(0x14);if(Wire.endTransmission()==0)GT_ADDR=0x14;}
}
static bool Touch_ReadFrame(){
  uint8_t st=0;
  if(!gtRd(0x814E,&st,1)){gTouchPts=0;return false;}
  if(!(st&0x80)||(st&0x0F)==0){ if(st&0x80)gtWr8(0x814E,0); gTouchPts=0; return false; }
  uint8_t buf[8]; bool ok=gtRd(0x8150,buf,8); gtWr8(0x814E,0);
  if(!ok){gTouchPts=0;return false;}
  int prx=buf[0]|(buf[1]<<8), pry=buf[2]|(buf[3]<<8);   // P4G7: GT911 raw = landscape panel coords (1024x600)
  if(g_tlog_n<16){ g_tlog[g_tlog_n][0]=(int16_t)prx; g_tlog[g_tlog_n][1]=(int16_t)pry; g_tlog_n=g_tlog_n+1; }   // bring-up: first 16 raw touches, logged from loop()
  int _cx,_cy; if(!p4disp_touch_to_compose(prx,pry,&_cx,&_cy)){gTouchPts=0;return false;}
  uint16_t rx=(uint16_t)_cx, ry=(uint16_t)_cy;          // -> compose-buffer coords, then the P4's usual rotation mapping
  // Map physical touch -> virtual canvas per g_rot (inverse of fb_setPixel), so touch tracks the
  // rotated display. Without this, landscape touches land 90 deg off.
  int vx,vy;
  switch(g_rot){
    case 1:  vx=rx;                vy=ry;                break;   // portrait
    case 2:  vx=ry;                vy=(LCD_WIDTH-1)-rx;  break;   // landscape flipped
    case 3:  vx=(LCD_WIDTH-1)-rx;  vy=(LCD_HEIGHT-1)-ry; break;   // portrait flipped
    default: vx=(LCD_HEIGHT-1)-ry; vy=rx;                break;   // 0 = landscape (matches g_rot=0 display)
  }
  gTouchX=(uint16_t)vx; gTouchY=(uint16_t)vy;
  gTouchPts=1; return true;
}
static bool getTouchXY(uint16_t*x,uint16_t*y){if(!gTouchPts)return false;*x=constrain(gTouchX,0,gW-1);*y=constrain(gTouchY,0,gH-1);return true;}

// ════════════════════════════════════════════════════════════════════════════
// MSC RAM DISK
// ════════════════════════════════════════════════════════════════════════════
USBMSC MSC;static bool g_usb_online=false;
// ── 5.9.35: the RAM disk is sized by CONFIG.TXT, not baked in ───────────────
// The Gotek sees a tiny FAT12 volume in PSRAM holding exactly one file. That
// volume was a hard 2 MB — fine for ADF (880 KB) and Amiga HD (1.76 MB), but
// Atari ST .HFE dumps routinely run past it. HFE stores the raw BITSTREAM, not
// sectors, so its size does not track the floppy's data capacity: an 880 KB ST
// disk can be a 1-2 MB HFE and a high-sample-rate or HFEv3 dump can exceed
// 2.88 MB. So there is no natural ceiling to bake in — it is a knob.
//
// lab14m: the RAM disk size now comes from BIGDISK= (DISKMAXKB is retired). Since 5.9.37 the
// cable path serves any image straight off the card (alias disk), so this buffer is only
//   * the save write-back space over the cable (writes past it are refused), and
//   * the whole image for a wireless send (the dongle must receive real bytes).
// BIGDISK=OFF (default) = 1760 KB = an Amiga HD disk, and the SuperMini's limit.
// BIGDISK=ON            = 2880 KB = room for large Atari ST .HFE images.
// The volume, cluster size and FAT are derived from it so the result is
// always valid FAT12 and the metadata layout is identical at every size.
//
//   image KB    volume    sec/clus   PSRAM cost
//     1760      3533 sec     2        1,808,896   <- BIGDISK=OFF (default, lab14m)
//     2048      4109 sec     2        2,103,808   (the old 5.9.34 behaviour)
//     2880      5773 sec     4        2,955,776   <- BIGDISK=ON (the default until lab14m)
//     4096      8205 sec     4        4,200,960   (ceiling)
#define DISK_IMG_MIN_KB  1024
#define DISK_IMG_DEF_KB  1760   // lab14m: BIGDISK=OFF
#define DISK_IMG_BIG_KB  2880   // lab14m: BIGDISK=ON
#define DISK_IMG_MAX_KB  4096
#define DISK_RESERVED_SECTORS 1
#define DISK_SECTORS_PER_FAT  8        // 4096 bytes = 2730 FAT12 entries; enough for every size above (was 6)
#define DISK_ROOT_DIR_SECTORS 4
#define DISK_DATA_LBA (DISK_RESERVED_SECTORS+DISK_SECTORS_PER_FAT+DISK_ROOT_DIR_SECTORS)   // 13
static_assert(DISK_DATA_LBA==ESPNOW_DATA_LBA,"espnow_server.h ESPNOW_DATA_LBA must match DISK_DATA_LBA (wireless sends read the image from there)");   // lab14r
#define DISK_SECTORS_CEIL ((uint32_t)DISK_IMG_MAX_KB*2+256)   // upper bound for the static dirty-map
static const uint16_t SECTOR_SIZE=512;
static const uint16_t RESERVED_SECTORS=DISK_RESERVED_SECTORS,SECTORS_PER_FAT=DISK_SECTORS_PER_FAT,ROOT_DIR_SECTORS=DISK_ROOT_DIR_SECTORS;
static const uint8_t NUM_FATS=1;static const uint16_t ROOT_ENTRIES=64;
static const uint32_t FAT_LBA=DISK_RESERVED_SECTORS,ROOT_LBA=DISK_RESERVED_SECTORS+DISK_SECTORS_PER_FAT,DATA_LBA=DISK_DATA_LBA;
static uint32_t g_img_max_kb=DISK_IMG_DEF_KB;   // lab14m: BIGDISK= (was DISKMAXKB=)
static bool     g_longname=false;               // lab14q: LONGNAME=ON - the presented disk carries the image's real file name
static uint32_t TOTAL_SECTORS=DISK_IMG_DEF_KB*2+DISK_DATA_LBA;   // real value set by diskGeomApply()
static uint8_t  SECTORS_PER_CLUSTER=4;
static uint32_t MAX_FILE_BYTES=0;
static uint32_t SV_IMG_MAX_SECTORS=0;
// Smallest power-of-two cluster that keeps this volume inside FAT12 AND inside our fixed FAT.
static void diskGeomFor(uint32_t sectors,uint8_t*spcOut,uint32_t*maxOut){
  uint32_t dataSec=sectors-DATA_LBA;
  uint32_t fatCap=((uint32_t)SECTORS_PER_FAT*512u*2u)/3u;      // FAT12 entries that fit: 2730
  uint32_t maxCl=fatCap-2; if(maxCl>4084u)maxCl=4084u;         // ...and FAT12's own ceiling
  uint32_t spc=1; while((dataSec/spc)>maxCl&&spc<128u)spc<<=1;
  uint32_t cl=dataSec/spc;
  *spcOut=(uint8_t)spc;
  *maxOut=cl*spc*512u;      // WHOLE clusters only. The old formula was (dataSec*512), which
}                           // ran the last cluster one sector past the end of the volume.
static void diskGeomApply(){
  uint32_t want=g_img_max_kb;
  if(want<DISK_IMG_MIN_KB)want=DISK_IMG_MIN_KB;
  if(want>DISK_IMG_MAX_KB)want=DISK_IMG_MAX_KB;
  g_img_max_kb=want;
  uint32_t target=want*1024u, sectors=want*2u+DATA_LBA;
  uint8_t spc=4; uint32_t mx=0;
  for(int guard=0;guard<64;guard++){
    diskGeomFor(sectors,&spc,&mx);
    if(mx>=target)break;
    sectors+=(uint32_t)spc;                                    // one more cluster
  }
  TOTAL_SECTORS=sectors;SECTORS_PER_CLUSTER=spc;MAX_FILE_BYTES=mx;SV_IMG_MAX_SECTORS=sectors-DATA_LBA;
}
static const uint32_t ADF_DEFAULT_SIZE=901120;             // standard DD ADF = 880KB
static const uint32_t ADF_HD_SIZE=1802240;                 // Amiga HD floppy = 1760KB (22 sectors/track, half-speed)
static const uint32_t HD_FLAG_BYTES=1258291;               // >1.2MB => flag as HD; extended-DD disks (~900-960KB) stay DD (A500-readable)
enum DiskMode{MODE_ADF=0,MODE_DSK=1,MODE_GEN=2};static DiskMode g_mode=MODE_ADF;   // v5.2: GEN = generic/any-machine library (/GENERIC)
static const char*getOutputFilename(){return g_mode==MODE_ADF?"DISK.ADF":"DISK.DSK";}
// lab14q: the name the Gotek sees for an image. GENERIC always uses the real name (FlashFloppy needs the
// extension); ADF/DSK use DISK.ADF / DISK.DSK unless LONGNAME=ON.
static String presentName(const String& imgPath){ int s=imgPath.lastIndexOf('/'); String fn=s>=0?imgPath.substring(s+1):imgPath;
  return (g_longname||g_mode==MODE_GEN) ? fn : String(getOutputFilename()); }
uint8_t*g_disk=nullptr;
// Allocate the volume, stepping the request down 256 KB at a time if PSRAM is
// short (a big library plus a big disk can be tighter than a blank card).
static bool diskAlloc(){
  for(;;){
    diskGeomApply();
    g_disk=(uint8_t*)ps_malloc((size_t)TOTAL_SECTORS*512);
    if(g_disk)return true;
    if(g_img_max_kb<=DISK_IMG_MIN_KB)return false;
    g_img_max_kb=(g_img_max_kb>DISK_IMG_MIN_KB+256)?(g_img_max_kb-256):DISK_IMG_MIN_KB;
  }
}
static void wr16(uint8_t*p,int o,uint16_t v){p[o]=v;p[o+1]=v>>8;}
static void wr32(uint8_t*p,int o,uint32_t v){p[o]=v;p[o+1]=v>>8;p[o+2]=v>>16;p[o+3]=v>>24;}
static void build_boot_sector(uint8_t*bs){memset(bs,0,512);bs[0]=0xEB;bs[1]=0x3C;bs[2]=0x90;memcpy(bs+3,"MSDOS5.0",8);wr16(bs,11,512);bs[13]=SECTORS_PER_CLUSTER;wr16(bs,14,RESERVED_SECTORS);bs[16]=NUM_FATS;wr16(bs,17,ROOT_ENTRIES);wr16(bs,19,(uint16_t)TOTAL_SECTORS);bs[21]=0xF8;wr16(bs,22,SECTORS_PER_FAT);wr16(bs,24,32);wr16(bs,26,64);bs[36]=0x80;bs[38]=0x29;wr32(bs,39,0x12345678);memcpy(bs+43,"ESP32MSC   ",11);memcpy(bs+54,"FAT12   ",8);bs[510]=0x55;bs[511]=0xAA;}
static void fat12_set(uint8_t*fat,uint16_t cl,uint16_t v){uint32_t i=(cl*3)/2;if(!(cl&1)){fat[i]=v&0xFF;fat[i+1]=(fat[i+1]&0xF0)|((v>>8)&0x0F);}else{fat[i]=(fat[i]&0x0F)|((v<<4)&0xF0);fat[i+1]=(v>>4)&0xFF;}}
static void build_fat(uint8_t*fat,uint32_t fsz){memset(fat,0,SECTORS_PER_FAT*512);fat[0]=0xF8;fat[1]=0xFF;fat[2]=0xFF;uint32_t clb=(uint32_t)SECTORS_PER_CLUSTER*512;uint32_t need=(fsz+clb-1)/clb;for(uint32_t i=0;i<need;i++)fat12_set(fat,2+i,i==need-1?0x0FFF:3+i);}
// lab14q: the root directory of the disk the Gotek sees.
// The 8.3 entry is always valid and takes its extension from the FULL name. It used to copy the name into a
// 32-byte buffer first, so a GENERIC file name of 32+ characters lost extension letters ("PROPHECY.HF") and
// FlashFloppy could not recognise the format (ERROR 34). With LONGNAME=ON the real name goes in front of it as
// VFAT long-name entries - what FlashFloppy shows on its display - and the 8.3 name becomes an alias (NAME~1.EXT).
// The root has 64 slots; a 255-character name needs 20, so it always fits.
static bool sfnChar(char c){ return (c>='A'&&c<='Z')||(c>='0'&&c<='9')||(c&&strchr("!#$%&'()-@^_`{}~",c)); }
static void build_root(uint8_t*root,const char*name,uint32_t fsz){
  memset(root,0,ROOT_DIR_SECTORS*512);
  const char* dot=strrchr(name,'.');
  size_t nl = dot ? (size_t)(dot-name) : strlen(name);
  char n[8],e[3]; memset(n,' ',8); memset(e,' ',3);
  int ni=0; bool lossy=false;
  for(size_t i=0;i<nl;i++){ char c=(char)toupper((unsigned char)name[i]);
    if(c==' '||c=='.'){ lossy=true; continue; }
    if(!sfnChar(c)){ c='_'; lossy=true; }
    if(ni<8) n[ni++]=c; else lossy=true; }
  if(ni==0){ memcpy(n,"DISK",4); ni=4; lossy=true; }
  if(dot){ int ei=0; for(const char*p=dot+1;*p;p++){ char c=(char)toupper((unsigned char)*p);
    if(c==' '){ lossy=true; continue; } if(!sfnChar(c)){ c='_'; lossy=true; } if(ei<3) e[ei++]=c; else lossy=true; } }
  bool hasLower=false; for(const char*p=name;*p;p++) if(*p>='a'&&*p<='z') hasLower=true;
  uint8_t* sfn=root;
  if(g_longname && (lossy||hasLower)){
    int keep=ni<6?ni:6; n[keep]='~'; n[keep+1]='1'; for(int i=keep+2;i<8;i++) n[i]=' ';   // alias NAME~1.EXT
    uint16_t u[255]; int ul=0;                          // the name as UCS-2 (UTF-8 decoded; anything else -> '_')
    for(const uint8_t*p=(const uint8_t*)name; *p && ul<255; ){
      uint32_t cp;
      if(*p<0x80){ cp=*p++; }
      else if((*p&0xE0)==0xC0 && (p[1]&0xC0)==0x80){ cp=((uint32_t)(p[0]&0x1F)<<6)|(p[1]&0x3F); p+=2; }
      else if((*p&0xF0)==0xE0 && (p[1]&0xC0)==0x80 && (p[2]&0xC0)==0x80){ cp=((uint32_t)(p[0]&0x0F)<<12)|((uint32_t)(p[1]&0x3F)<<6)|(p[2]&0x3F); p+=3; }
      else { cp='_'; p++; while((*p&0xC0)==0x80) p++; }
      u[ul++]=(uint16_t)cp;
    }
    int cnt=(ul+12)/13; if(cnt<1) cnt=1;
    uint8_t sn[11]; memcpy(sn,n,8); memcpy(sn+8,e,3);
    uint8_t sum=0; for(int i=0;i<11;i++) sum=(uint8_t)(((sum&1)<<7)+(sum>>1)+sn[i]);
    static const uint8_t off[13]={1,3,5,7,9,14,16,18,20,22,24,28,30};
    for(int k=0;k<cnt;k++){                             // highest part first, part 1 just before the 8.3 entry
      int ord=cnt-k; uint8_t* d=root+k*32;
      d[0]=(uint8_t)(ord|(k==0?0x40:0)); d[11]=0x0F; d[13]=sum;
      for(int jj=0;jj<13;jj++){ int ci=(ord-1)*13+jj; uint16_t ch= ci<ul ? u[ci] : (ci==ul ? 0x0000 : 0xFFFF);
        d[off[jj]]=(uint8_t)(ch&0xFF); d[off[jj]+1]=(uint8_t)(ch>>8); }
    }
    sfn=root+cnt*32;
  }
  memcpy(sfn,n,8); memcpy(sfn+8,e,3); sfn[11]=0x20; wr16(sfn,26,2); wr32(sfn,28,fsz);
}
static void build_volume(const char*outName,uint32_t fsz){if(fsz>MAX_FILE_BYTES)fsz=MAX_FILE_BYTES;memset(g_disk,0,TOTAL_SECTORS*512);build_boot_sector(g_disk);build_fat(g_disk+RESERVED_SECTORS*512,fsz);build_root(g_disk+(RESERVED_SECTORS+SECTORS_PER_FAT)*512,outName,fsz);}

// ════════════════════════════════════════════════════════════════════════════
// ALIAS DISK — present an SD file to the Gotek without copying it (5.9.37)
// ────────────────────────────────────────────────────────────────────────────
// The RAM disk can only ever hold the BIGDISK size, so anything larger used to be
// refused outright ("TOO BIG"). Instead we now build ONLY the FAT12 metadata in
// RAM and remap every read past DATA_LBA to the file's own sectors on the card,
// via its cluster chain. Nothing is copied, so the image size stops mattering.
//
// This is a block-level remap — a loop device, not a symlink. FAT has no such
// concept; we are lying to the host about where the data area lives.
//
// Proven on hardware in firmware/GTi_AliasTest: the mapped image hashed
// byte-identical to the original (SHA-256 vs certutil), 5,000+ operations with
// zero errors while the card was concurrently being walked, on FAT16 and FAT32.
//
// WRITES ARE REFUSED while aliased. The image is not ours to modify — saving
// needs the RAM-disk scratch, which is what the BIGDISK size now really bounds.
// ════════════════════════════════════════════════════════════════════════════

#define AL_SEC 512
static uint8_t g_sbuf[AL_SEC];                 // one-sector scratch for FS parsing

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
  if(bps!=AL_SEC)return false;                 // 4K-sector cards are out of scope here
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
  g_fs.root_secs= ((uint32_t)g_fs.root_ents*32+AL_SEC-1)/AL_SEC;
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
    uint32_t off=c*4, lba=g_fs.fat_lba+off/AL_SEC;
    if(!SD_MMC.readRAW(g_sbuf,lba))return 0x0FFFFFFF;
    return rd32(g_sbuf,off%AL_SEC)&0x0FFFFFFF;
  }
  if(g_fs.fat_type==16){
    uint32_t off=c*2, lba=g_fs.fat_lba+off/AL_SEC;
    if(!SD_MMC.readRAW(g_sbuf,lba))return 0xFFFFu;
    return rd16(g_sbuf,off%AL_SEC);
  }
  // FAT12: entries are 1.5 bytes and can STRADDLE a sector boundary, so read two.
  static uint8_t two[AL_SEC*2];
  uint32_t off=c+(c>>1), lba=g_fs.fat_lba+off/AL_SEC, so=off%AL_SEC;
  if(!SD_MMC.readRAW(two,lba))return 0x0FFFu;
  if(!SD_MMC.readRAW(two+AL_SEC,lba+1))memset(two+AL_SEC,0,AL_SEC);
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
      uint8_t sect[AL_SEC]; memcpy(sect,g_sbuf,AL_SEC);          // fatNext() reuses g_sbuf
      for(int o=0;o<AL_SEC;o+=32){
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
  uint32_t need=(size+AL_SEC-1)/AL_SEC, acc=0;
  for(size_t k=0;k<g_ext.size();k++){
    if(acc>=need){ g_ext.resize(k); break; }
    uint32_t take=g_ext[k].sectors; if(take>need-acc)take=need-acc;
    g_ext[k].sectors=take; acc+=take;
  }
  return acc>=need;
}
static inline uint32_t mapSector(uint32_t fsec,bool*ok){
  uint32_t acc=0;
  for(size_t i=0;i<g_ext.size();i++){
    if(fsec<acc+g_ext[i].sectors){*ok=true;return g_ext[i].lba+(fsec-acc);}
    acc+=g_ext[i].sectors;
  }
  *ok=false; return 0;
}

static int32_t onWrite(uint32_t lba,uint32_t off,uint8_t*buf,uint32_t n);   // fwd: mscAnnounce re-registers it
static void hardDetach();                                                     // fwd: mscAnnounce detaches before re-declaring capacity
// ── alias mount state ───────────────────────────────────────────────────────
static bool     g_alias=false;          // this mount is served from the card
static uint32_t g_alias_img=0;          // image size in bytes
static uint32_t g_alias_sectors=0;      // presented volume size while aliased
static uint8_t  g_alias_spc=0;          // its cluster size
static uint8_t  g_alias_tmp[512];       // static: the MSC callback runs on the USB task's stack
static uint32_t g_usb_announced=0;      // capacity USB currently believes

static int32_t onRead(uint32_t lba,uint32_t off,void*buf,uint32_t n);    // fwd: defined after the dirty map it consults
// lab15e: ONE user of the SD card at a time. Since 5.9.37 a mounted disk is read straight off the card
// by the USB task (onRead -> readRAW) while the main loop can be writing through FatFs (saves, gti.log,
// covers, settings). The SD driver keeps single commands apart but NOT a whole write: a write is the
// write command, then status polls while the card programs, and a read slipped in between lands while
// the card is busy. kodak80's gti.log, 27 Sep: a 2 MB .sav.hfe copy made while the Gotek was reading
// gave 16 metadata writes that read back different, one that never did - and that game's folder showed
// up EMPTY on the PC. Now every FatFs transfer (sgLowRead/sgLowWrite, under the SD guard) holds this
// lock for the length of the transfer, so they queue instead of interleaving. The Gotek's reads are
// among them: SD_MMC.readRAW() is disk_read() on the guarded drive (lab15h - never lock around it).
static SemaphoreHandle_t g_sdlock=nullptr;
static inline void sdLock(){ if(g_sdlock) xSemaphoreTake(g_sdlock,portMAX_DELAY); }
static inline void sdUnlock(){ if(g_sdlock) xSemaphoreGive(g_sdlock); }
// Re-declare capacity to USB. Only ever called when the size actually CHANGES,
// so a user who never loads an oversized image never exercises this path and
// their USB behaviour is bit-for-bit what it was before 5.9.37.
static void mscAnnounce(uint32_t sectors){
  if(sectors==g_usb_announced) return;
  if(g_usb_online) hardDetach();                 // never change capacity under a live host (FORCESWAP)
  // lab14t: NO MSC.end() here. In core 3.3.x end() also clears the LUN's "writable" flag and nothing set it
  // back, so after the first image that needed its own volume size (every HFE over the BIGDISK size - most
  // HFEs once BIGDISK=OFF made that 1.76 MB in lab14m) the Gotek saw a WRITE-PROTECTED drive until power-off:
  // FlashFloppy "FATFS error" on that HFE and on every image after it (kodak80, 26 Sep). begin() alone just
  // updates the block count; the writable flag is set explicitly as well.
  MSC.vendorID("ESP32");MSC.productID("RAMDISK");MSC.productRevision("1.0");
  MSC.onRead(onRead);MSC.onWrite(onWrite);MSC.isWritable(true);MSC.mediaPresent(true);
  MSC.begin(sectors,512);
  g_usb_announced=sectors;
}
// Pick a presented volume that holds exactly this image. Same routine the RAM
// disk uses, so the FAT12 rules (2730 entries in our fixed 8-sector FAT, 4084
// cluster ceiling, cluster up to 128 sectors) are applied identically.
// BPB_TotSec16 is 16 bits, which puts a hard 32 MB ceiling on what we can present.
static bool aliasGeom(uint32_t fsz,uint32_t*secOut,uint8_t*spcOut){
  uint32_t kb=(fsz+1023)/1024, sectors=kb*2+DATA_LBA;
  uint8_t spc=4; uint32_t mx=0;
  for(int g=0;g<64;g++){ diskGeomFor(sectors,&spc,&mx); if(mx>=fsz)break; sectors+=spc; }
  if(sectors>65535u) return false;
  *secOut=sectors; *spcOut=spc; return true;
}
// Mount `path` by reference. errOut is a plain out-param on purpose: a
// user-defined type in a free function's signature inside an .ino trips
// Arduino's auto-prototype generator.
static bool aliasMount(const String&path,uint32_t fsz,const char*volName,
                       uint32_t presentSectors,uint8_t presentSpc,String*errOut){
  g_alias=false;
  g_alias_sectors=presentSectors; g_alias_spc=presentSpc;
  if(!g_fs.ok && !fsMount()){
    if(errOut)*errOut=g_fs.exfat?String("card is exFAT"):String("cannot read card FS");
    return false;
  }
  uint32_t clus=0,sz=0;
  if(!pathResolve(path,&clus,&sz)){
    if(errOut)*errOut="not found: "+g_fail_seg; return false;
  }
  if(!chainToExtents(clus,sz)){ if(errOut)*errOut="cluster chain broken"; return false; }
  // Build ONLY the metadata, at the alias geometry. build_volume() is not usable
  // here: it memsets TOTAL_SECTORS*512, which for an aliased volume is far more
  // than g_disk actually is. Same metadata-only pattern the wireless path uses.
  uint32_t savT=TOTAL_SECTORS; uint8_t savS=SECTORS_PER_CLUSTER;
  TOTAL_SECTORS=g_alias_sectors; SECTORS_PER_CLUSTER=g_alias_spc;
  memset(g_disk,0,DATA_LBA*512);
  build_boot_sector(g_disk);
  build_fat(g_disk+RESERVED_SECTORS*512,fsz);
  build_root(g_disk+(RESERVED_SECTORS+SECTORS_PER_FAT)*512,volName,fsz);
  TOTAL_SECTORS=savT; SECTORS_PER_CLUSTER=savS;   // the RAM disk keeps its own geometry
  g_alias_img=fsz; g_alias=true;
  return true;                                    // caller disables save tracking
}

// ── Save-game persistence state (v4.8.0) ────────────────────────────────────
// STANDALONE: the Amiga writes to OUR RAM disk (we are the USB drive) — onWrite
// below ticks the dirty map; a settle timer + eject flush persist to .sav.adf.
// WIRELESS: the dongle ticks its own map and beacons; we pull + patch (espnow).
// SV_IMG_MAX_SECTORS is a runtime value now (see diskGeomApply); the dirty map is
// sized once for the largest disk the BIGDISK size can ever ask for.
#define SV_SETTLE_MS 3000
static int      g_saves_mode=1;                          // 0=OFF 1=COPY 2=OVERWRITE (SAVES=)
static uint8_t  g_sv_dirty[((DISK_SECTORS_CEIL-DISK_DATA_LBA)+7)/8];
static volatile uint16_t g_sv_dirty_count=0;
static volatile uint32_t g_sv_last_write=0;
static volatile uint32_t g_sv_total_writes=0;
static String   g_loaded_path="";                        // SD path of the mounted image ("" = diag/none)
static String   g_sv_wl_path="";                         // SD path of the disk last FLUNG to the dongle
static uint32_t g_sv_wl_loadid=0;                        // dongle load_id it acked with
// lab15i: per-dongle saves. In Wireless mode the same game is on the GTi's own USB AND on the dongle, so two
// Amigas can play it at once. Before, both saved into the same Game.sav.adf and it became a sector-by-sector
// mix of the two (an AmigaDOS save disk could end up unreadable). Now each save file only ever holds what ONE
// Amiga saw plus that Amiga's own writes:
//   cable  -> Game.sav.adf            (unchanged - every existing save keeps working)
//   dongle -> Game.sav.XXXX.adf       (XXXX = last 2 bytes of the paired dongle's MAC = its softAP MAC =
//                                      the GotekOMEGA-XXXX name from Webby 1.6.8; ".sav." kept in the name so
//                                      the walker, the save list and savPathFor all treat it as a save)
static String   g_loaded_orig="";                        // the library image behind g_loaded_path (never a .sav)
static bool     g_sv_cable_rebased=false;                // cable save already rebuilt from what was presented
static String   g_sv_wl_orig="";                         // the library image behind g_sv_wl_path
static String   g_sv_wl_tag="";                          // XXXX of the dongle it went to ("" = no own file: old behaviour)
static inline bool svGet(const uint8_t*m,uint32_t i){return (m[i>>3]>>(i&7))&1;}
static inline void svSet(uint8_t*m,uint32_t i){m[i>>3]|=(uint8_t)(1u<<(i&7));}
// lab15c: g_sv_dirty is the WRITE OVERLAY for the whole time a disk is in - every sector the host wrote
// stays in RAM and is served from there until eject/next load. Before, a save flush cleared it while the
// disk was still in, so the Gotek read those sectors back from the card file as they were BEFORE the save
// (and, after an in-place replace, from clusters that no longer belonged to any file). "Something new to
// save" is now a write counter instead: onWrite bumps g_sv_wseq, a good flush records it in g_sv_fseq.
static volatile uint32_t g_sv_wseq=0;
static uint32_t g_sv_fseq=0;
static inline bool svPending(){ return g_sv_wseq!=g_sv_fseq; }
static void svDirtyReset(){memset(g_sv_dirty,0,sizeof(g_sv_dirty));g_sv_dirty_count=0;g_sv_last_write=0;g_sv_wseq=0;g_sv_fseq=0;}
static uint32_t g_sv_img_size=0;                         // bytes of the mounted image (standalone tracking)

// ── the read path (5.9.37) ──────────────────────────────────────────────────
// Standalone mounts are ALIASED: the FAT12 metadata is in g_disk, the data area
// is the file on the card. g_disk's data region is a WRITE OVERLAY — any sector
// the host has written (dirty map set) is served from there, so the Amiga sees
// its own saves; everything else comes straight off the card via the extents.
// Wireless and diag mounts still copy into g_disk and take the old path.
static int32_t onRead(uint32_t lba,uint32_t off,void*buf,uint32_t n){
  uint32_t vol = g_alias ? g_alias_sectors : TOTAL_SECTORS;
  uint64_t s=(uint64_t)lba*512+off;
  if(s+n>(uint64_t)vol*512)return 0;
  if(!g_alias){ memcpy(buf,g_disk+(size_t)s,n); return (int32_t)n; }   // RAM-disk copy (wireless/diag)
  uint8_t*out=(uint8_t*)buf; uint32_t done=0;
  const uint64_t metaEnd=(uint64_t)DATA_LBA*512;
  while(done<n && s+done<metaEnd){                       // boot sector / FAT / root: RAM
    uint32_t c=(uint32_t)((n-done)<(metaEnd-(s+done))?(n-done):(metaEnd-(s+done)));
    memcpy(out+done,g_disk+(size_t)(s+done),c); done+=c;
  }
  while(done<n){                                          // data area
    uint64_t fo=(s+done)-metaEnd;
    if(fo>=g_alias_img){memset(out+done,0,n-done);done=n;break;}   // cluster slack past EOF
    uint32_t fsec=(uint32_t)(fo/512), so=(uint32_t)(fo%512);
    uint32_t c=512-so; if(c>n-done)c=n-done;
    if((uint64_t)c>g_alias_img-fo)c=(uint32_t)(g_alias_img-fo);
    if(fsec<SV_IMG_MAX_SECTORS && svGet(g_sv_dirty,fsec)){          // host wrote this sector: overlay wins
      memcpy(out+done,g_disk+(size_t)(DATA_LBA+fsec)*512+so,c); done+=c; continue;
    }
    bool ok=false; uint32_t card=mapSector(fsec,&ok);
    if(!ok){memset(out+done,0,n-done);done=n;break;}
    // lab15h: NO sdLock() here. SD_MMC.readRAW() is disk_read() on the card's FatFs drive, which is our
    // guard driver -> sgLowRead(), and that already holds the SD lock for the transfer. Taking it here as
    // well (lab15e-g) locked the non-recursive mutex twice in the USB task: the Gotek's first read of a
    // mounted disk never returned (kodak80: "insert does not mount", then EJECT froze the screen).
    if(!SD_MMC.readRAW(g_alias_tmp,card))return (int32_t)done;
    memcpy(out+done,g_alias_tmp+so,c); done+=c;
  }
  return (int32_t)done;
}

static int32_t onWrite(uint32_t lba,uint32_t off,uint8_t*buf,uint32_t n){
  // 5.9.37: under an alias mount this is the WRITE OVERLAY. The bound below is
  // now the scratch size (the BIGDISK size), not the image size: a write inside it is
  // captured and served back by onRead; one beyond it is refused, so the host
  // sees a write error — "mounts fine, too big to save".
  uint32_t s=lba*512+off;if(s+n>TOTAL_SECTORS*512)return 0;memcpy(g_disk+s,buf,n);
  // v4.8.0: tick the dirty scorecard for every image sector this write touches
  // (assignment form, not ++ — C++20 deprecates ++ on volatile)
  g_sv_total_writes=g_sv_total_writes+1;
  uint32_t first=s/512,last=(s+n-1)/512,imgSecs=(g_sv_img_size+511)/512;
  if(imgSecs>SV_IMG_MAX_SECTORS)imgSecs=SV_IMG_MAX_SECTORS;
  bool img=false;
  for(uint32_t l=first;l<=last;l++){
    if(l<DATA_LBA)continue;uint32_t i=l-DATA_LBA;if(i>=imgSecs)continue;
    img=true;
    if(!svGet(g_sv_dirty,i)){svSet(g_sv_dirty,i);g_sv_dirty_count=g_sv_dirty_count+1;}
  }
  if(img)g_sv_wseq=g_sv_wseq+1;   // lab15c: something new for the next save flush
  g_sv_last_write=millis();
  return n;}
static void usbEventCB(void*,esp_event_base_t,int32_t,void*){}
static uint32_t g_rev=1;
static void hardDetach(){MSC.mediaPresent(false);delay(100);tud_disconnect();delay(500);g_usb_online=false;}
static void hardAttach(){char r[8];snprintf(r,8,"%lu",(unsigned long)g_rev++);MSC.productRevision(r);MSC.mediaPresent(true);delay(50);tud_connect();delay(200);g_usb_online=true;}

// ── SD ACCESS (v5.1) ─────────────────────────────────────────────────────────
// Plug the GTi into a PC to add games without cracking the case. The native USB-MSC
// interface is fixed once USB.begin() runs, so we can't re-report the disk capacity
// live — so SD Access is a dedicated BOOT MODE. The info-panel button stamps an RTC
// flag and reboots; setup() sees it and brings MSC up backed by the SD card's RAW
// sectors at the card's TRUE capacity (before USB.begin()). Exit (PC eject or DONE)
// reboots to normal — the reboot also flushes any stale FATFS cache, so what the GTi
// re-scans is exactly what the PC left behind. The GTi does NO filesystem work while
// the PC holds the card: onReadSD/onWriteSD go straight to raw sectors, bypassing
// FATFS, so there is only ever ONE master on the volume (the whole-card-corruption trap).
RTC_NOINIT_ATTR uint32_t g_sdaccess_magic;           // NOINIT (not DATA): DATA is re-inited on a SW restart; NOINIT survives esp_restart(), cleared only on power loss
// Boot forensics (from Dimmy's omega-skin): a counter that survives a software
// reset but dies with the RTC domain on power loss. B:1 after POWERON = a clean
// cold boot; B:>1 = something restarted us since power (e.g. a bench PC's USB-JTAG
// probe pulsing DTR/RTS). Logged at boot; harmless at a Gotek.
// 5.9.41-lab14d: "library too big" facts, carried into SD ACCESS mode across the restart
#define CAP_MAGIC 0xCA9AC17Eu
RTC_NOINIT_ATTR uint32_t g_cap_magic, g_cap_images, g_cap_games, g_cap_fit, g_cap_pct, g_cap_atleast;
RTC_NOINIT_ATTR uint32_t g_bootMagic;
RTC_NOINIT_ATTR uint32_t g_bootCount;
// lab14e: CRASH BREADCRUMBS. A panic prints its backtrace to a serial port nobody can see
// on a Gotek, so each boot stage leaves a marker (plus free memory) in RTC memory, which a
// software/panic reset does not clear. The next boot writes "last boot died in <stage>"
// to /gti.log. A heap hook also records the first allocation that failed outright.
#define BC_MAGIC 0xBC14E0E1u
RTC_NOINIT_ATTR uint32_t g_bc_magic, g_bc_stage, g_bc_n, g_bc_psram, g_bc_int, g_bc_failsz, g_bc_failcaps;
enum { BC_NONE=0, BC_SCAN, BC_BLURBS, BC_INDEXREAD, BC_CACHEREAD, BC_GROUP, BC_SIDECARS, BC_SORT, BC_CACHEWRITE, BC_THUMBS, BC_NFOCACHE,
       BC_READY, BC_RAMDISK, BC_UISTART };
static const char* bcName(uint32_t s){
  static const char* n[]={"-","scan (pass 1)","blurbs (pass 2)",".index read",".gamecache read","multi-disk grouping","sidecars",
                          "name sort",".gamecache write","thumbnails / micro-thumbs","nfo cache","running (after boot)","RAM disk","starting the UI (stats, reel, USB)"};
  return s<sizeof(n)/sizeof(n[0])?n[s]:"?";
}
static inline bool bcIsLibrary(uint32_t s){ return (s>=BC_SCAN && s<=BC_NFOCACHE) || s==BC_UISTART; }   // a crash here = don't auto-retry
static void bcSet(uint32_t stage,uint32_t n){
  g_bc_magic=BC_MAGIC; g_bc_stage=stage; g_bc_n=n;
  g_bc_psram=(uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM); g_bc_int=(uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
}
static void IRAM_ATTR bcAllocFailed(size_t size,uint32_t caps,const char*){ if(!g_bc_failsz){ g_bc_failsz=size?(uint32_t)size:1u; g_bc_failcaps=caps; } }
struct BcPrev { bool crashed=false, valid=false; int reason=0; uint32_t stage=0,n=0,psram=0,intr=0,failsz=0,failcaps=0; };
static BcPrev g_bc_prev;   // what the LAST boot left behind (read once, first thing in setup)
#define SDACCESS_MAGIC 0x5DACCE55u
static uint32_t g_sd_sectors=0;                       // real card size, set at SD-access boot
static volatile uint32_t g_sd_rd=0,g_sd_wr=0;        // sector-op tallies for the activity readout
static volatile bool g_sd_eject=false;               // set by the SCSI START/STOP (eject) callback
static uint8_t g_sd_tmp[512];                         // scratch for the (rare) sub-sector transfer
static int32_t onReadSD(uint32_t lba,uint32_t off,void*buf,uint32_t n){
  uint8_t*out=(uint8_t*)buf;uint32_t done=0;
  while(done<n){uint32_t sec=lba+(off+done)/512,so=(off+done)%512,chunk=512-so;if(chunk>n-done)chunk=n-done;
    if(sec>=g_sd_sectors)return (int32_t)done;
    if(so==0&&chunk==512){if(!SD_MMC.readRAW(out+done,sec))return (int32_t)done;}
    else{if(!SD_MMC.readRAW(g_sd_tmp,sec))return (int32_t)done;memcpy(out+done,g_sd_tmp+so,chunk);}
    g_sd_rd=g_sd_rd+1;done+=chunk;}
  return (int32_t)done;}
static int32_t onWriteSD(uint32_t lba,uint32_t off,uint8_t*buf,uint32_t n){
  uint32_t done=0;
  while(done<n){uint32_t sec=lba+(off+done)/512,so=(off+done)%512,chunk=512-so;if(chunk>n-done)chunk=n-done;
    if(sec>=g_sd_sectors)return (int32_t)done;
    if(so==0&&chunk==512){if(!SD_MMC.writeRAW(buf+done,sec))return (int32_t)done;}
    else{if(!SD_MMC.readRAW(g_sd_tmp,sec))return (int32_t)done;memcpy(g_sd_tmp+so,buf+done,chunk);if(!SD_MMC.writeRAW(g_sd_tmp,sec))return (int32_t)done;}
    g_sd_wr=g_sd_wr+1;done+=chunk;}
  return (int32_t)done;}
static bool onStartStopSD(uint8_t,bool start,bool load_eject){if(load_eject&&!start)g_sd_eject=true;return true;}

// ════════════════════════════════════════════════════════════════════════════
// SD + INDEX CACHE
// ════════════════════════════════════════════════════════════════════════════
static String indexFilePath(){return g_mode==MODE_ADF?"/ADF/.index":g_mode==MODE_DSK?"/DSK/.index":"/GENERIC/.index";}
static void writeIndexCache(const PathList&v){ if(g_nocache)return;File f=SD_MMC.open(indexFilePath().c_str(),FILE_WRITE);if(!f)return;
  { BufWr w(f); w.str("#COUNT="); w.num((long)v.size()); w.nl(); for(size_t i=0;i<v.size();i++){ w.str(v[i].c_str()); w.nl(); } }   // lab15f: 16 KB blocks, same bytes as println
  f.close();}   // lab15a
static bool readIndexCache(PathList&out){ if(g_nocache){out.clear();return false;} bcSet(BC_INDEXREAD,0);out.clear();File f=SD_MMC.open(indexFilePath().c_str(),FILE_READ);if(!f){return false;}
  long declaredCount=-1; uint32_t _t0=millis(); bool _cut=false;
  { BufRd r(f); const size_t LM=1024; char* l=(char*)malloc(LM);    // lab15f: 16 KB block reads, no String per line
    if(!l){ f.close(); return false; }
    size_t n; bool cut;
    while(r.line(l,LM,n,cut)){ if(cut)_cut=true; if(!n)continue;
      if(!strncmp(l,"#COUNT=",7)){declaredCount=atol(l+7);if(declaredCount>0)out.reserve(declaredCount);continue;}
      out.push_back(l);}
    free(l); }
  f.close();
  if(_cut){ gLog("[index] a path longer than 1023 chars - rebuilding\n"); out.clear(); return false; }
  gLog("[index] %u paths read in %lums\n",(unsigned)out.size(),(unsigned long)(millis()-_t0));
  out.sort(); out.compact(); out.shrink_to_fit();   // lab15a: the .index is written sorted; pack it (same order)
  // Validate: declared count must match actual lines read (catches partial writes/corruption)
  if(declaredCount>=0&&declaredCount!=(long)out.size()){out.clear();return false;}
  return!out.empty();}
// ── 5.9.36-lab6: the scan screen used to cost HALF the scan ────────────────
// It animated a bouncing ball every 80ms, and every frame ended in gfx_flush(),
// which pushes the WHOLE 320x480 framebuffer: 48 strips, each with a hard-coded
// 500us delay, plus ~12ms of SPI. About 36ms. So in every 80ms of wall clock,
// ~38ms went on drawing a ball and ~42ms on actually scanning the card - the
// scan took roughly 1.9x as long as the work required.
//
// Now the ball is painted ONCE as decoration and only the counter is refreshed,
// driven by the file COUNT rather than a timer: every 50 files is roughly every
// 1.7s instead of 12 times a second. Same reassurance, ~2% overhead instead of
// ~47%, and the number climbing tells you more than the ball ever did.
//
// The time floor is the one piece of the old behaviour worth keeping: if a
// single slow directory stalls the walk, the count stops moving and a static
// screen reads as a hang. A redraw every 3s regardless proves it is still alive.
#define SCAN_EVERY_N   50
#define SCAN_FLOOR_MS  3000
static bool     g_scan_painted=false;
static uint32_t g_scan_lastdraw=0;
static int      g_scan_lastcount=-1;
static void drawScanBall(int cx,int cy){          // static decoration, drawn once
  for(int dy=-6;dy<=6;dy++)for(int dx=-6;dx<=6;dx++){
    if(dx*dx+dy*dy>36)continue;
    bool checker=((dx+6)/3+(dy+6)/3)%2==0;
    gfx_drawPixel(cx+dx,cy+dy,checker?TFT_RED:TFT_WHITE);
  }
}
static void drawScanFrame(int count){
  if((count%512)==0)gLog("[scan] files=%d int=%u psram=%u\n",count,(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),(unsigned)ESP.getFreePsram());
  if(!g_scan_painted){                            // full paint: background, ball, label
    g_scan_painted=true;
    gfx_fillScreen(0x1082);
    drawScanBall(gW/2,gH/2-46);
    gfx_setTextSize(2);gfx_setTextColor(0xFC60,0x1082);
    {const char*t="SCANNING CARD";int tw=gfx_textWidth(t);gfx_setCursor((gW-tw)/2,gH/2-20);gfx_print(t);}
  }
  gfx_fillRect(gW/2-110,gH/2+14,220,20,0x1082);   // counter only
  gfx_setTextSize(2);gfx_setTextColor(0x9BD6,0x1082);
  String msg=String(count)+" files";
  gfx_setCursor(gW/2-gfx_textWidth(msg)/2,gH/2+16);gfx_print(msg);
  gfx_flush();
  g_scan_lastdraw=millis(); g_scan_lastcount=count;
}
// Safe to call on EVERY directory entry. Gated on the DELTA since the last
// redraw, not "count % 50" — with a modulo, every entry seen while count sat on
// a multiple of 50 would redraw, which is the bug we are removing.
static inline bool scanTick(int count){
  if(count-g_scan_lastcount>=SCAN_EVERY_N)return true;
  return (millis()-g_scan_lastdraw)>SCAN_FLOOR_MS;     // liveness: a slow folder still shows a pulse
}
// Remaining-time string from work done so far. No user type in the signature
// (Arduino auto-prototype), so this is safe at file scope in a .ino.
static String etaStr(uint32_t elapsedMs,int done,int total){
  if(done<=0||done>=total||elapsedMs<1500)return String("");
  uint32_t rem=(uint32_t)(((uint64_t)elapsedMs*(uint64_t)(total-done))/(uint64_t)done)/1000UL;
  if(rem<60)return String(rem)+"s left";
  return String(rem/60)+"m "+String(rem%60)+"s left";
}

// v5.2 GEN mode: a "disk image" is any file that ISN'T a known sidecar (cover/info/manual/
// FlashFloppy config / index cache). Extension-agnostic by design — FlashFloppy identifies the
// real format from the file itself. (u = UPPERCASE filename, path already stripped.)
static bool isGenImage(const String&u){
  if(u.startsWith("."))return false;
  if(u.indexOf(".SAV.")>=0)return false;
  if(u=="FF.CFG")return false;
  if(u.endsWith(".JPG")||u.endsWith(".JPEG")||u.endsWith(".PNG"))return false;
  if(u.endsWith(".NFO")||u.endsWith(".RTFM")||u.endsWith(".TXT"))return false;
  if(u.endsWith(".INDEX")||u.endsWith(".GAMECACHE"))return false;
  return true;
}
static std::vector<uint64_t> g_coverset;
static uint64_t coverHash(const String&s){uint64_t h=1469598103934665603ULL;for(unsigned i=0;i<s.length();i++){h^=(uint8_t)s[i];h*=1099511628211ULL;}return h;}   // 5.3.7: lowercased cover-image paths (jpg/png) harvested during the scan walk, so buildThumbs resolves each cover from RAM instead of probing the card. Covers ONLY — .nfo/.rtfm are read on demand when you open a game, never in bulk, so they were dead weight here.
static inline bool isCoverExtU(const String&u){return u.endsWith(".JPG")||u.endsWith(".JPEG")||u.endsWith(".PNG");}
// lab14i: .sav files seen by the scan walk (lower-case VFS path hashes, sorted). The save badge on the
// reel and the list is answered from this instead of an SD_MMC.exists() per selection - on a miss that
// reads the whole folder (R7). Kept on the card as /GTI/.gtisaves_<mode> so a warm boot has it too, and
// appended to whenever the GTi writes a save. No list yet (first boot of lab14i, before a RESCAN) ->
// g_savset_ok=false -> the badge falls back to the old live check. Loading a disk always checks live.
static std::vector<uint64_t> g_savset;
static bool   g_savset_ok=false;
static String g_savset_cutdir;                 // the walk's cut folder: its badges stay live checks
static inline void savSetNoteWalk(const String&fp){String l=fp;l.toLowerCase();g_savset.push_back(coverHash(l));}
static void savSetWrite();                      // fwd - defined with the save engine
static void savSetLoad();
// ── 5.9.31-lab1: SIDECAR HARVEST ────────────────────────────────────────────
// Selecting a game used to cost ~10 full directory walks on a flat 1700-file card:
// findNFOFor (2 on a miss) + the NFO open + savExistsFor + isHDImage + manualFor (6 on
// a miss). That is FAT resolving names entry-by-entry - seek-bound, so faster silicon
// never helped. Fix: during the scan walk every entry is already in our hand, so record
// what we need then. Blurbs are read from the open handle; .rtfm presence and the HD flag
// fall out of the directory entry itself (e.size()) for free. Same hashed-vector +
// binary_search shape as g_coverset, so 1000 games cost 8 bytes each, not a String each.
// NOT harvested: savExistsFor - saves are created at runtime, so a scan-time answer goes
// stale. It stays a live check (one walk) until lab2 gives it an invalidation hook.
static inline uint64_t sideKey(const String&nameNoDir){          // lc basename of a name WITH an extension ("Game.nfo" -> "game")
  String k=nameNoDir; int d=k.lastIndexOf('.'); if(d>0)k=k.substring(0,d); k.toLowerCase(); return coverHash(k);
}
static inline uint64_t sideKeyRaw(const String&stem){            // lc of an ALREADY extension-less stem — must not strip again,
  String k=stem; k.toLowerCase(); return coverHash(k);           // or "Turrican II v1.2" keys as "turrican ii v1" and misses
}
struct NfoRec{ uint64_t k; String t,b; };
static std::vector<NfoRec>   g_nfoharvest;     // .nfo title+blurb by lc basename
static std::vector<uint64_t> g_manualset;      // games carrying a .rtfm
static std::vector<uint64_t> g_hdset;          // disk images over HD_FLAG_BYTES
static bool g_sidecars_harvested=false;        // this boot walked the tree -> the harvest is authoritative
#define NFO_BLURB_MAX 400                      // 3.5" PSRAM budget: 1000 games x 400B worst case = 400KB
                                               // (the 7B capped at 2000 with 26MB spare; we have ~5.6MB)
static void parseNFO(const String&txt,String&t,String&b);        // fwd: defined with the game-cache helpers
static void sideNoteNfo(const String&fname,const String&txt){
  String t,b; parseNFO(txt,t,b);
  if((int)b.length()>NFO_BLURB_MAX) b=b.substring(0,NFO_BLURB_MAX);
  if(t.length()||b.length()){ NfoRec r; r.k=sideKey(fname); r.t=t; r.b=b; g_nfoharvest.push_back(r); }
}
// ── v5.6.0 Library Categories (CONFIG.TXT gated; folders auto-classified by content) ──
static bool   g_categories=false;   // CATEGORIES=ON : show the category browse (a folder with no disk images but subfolders = a category)
static bool   g_nesting=false;      // NESTING=ON : allow category recursion beyond one level
static String g_libpath="";         // current category path under the mode root ("" = at the root)
static std::vector<String> g_cats;  // category sub-folder names at the current level (filled by the scan)
static void reloadLevel();          // fwd: fresh (uncached) rescan of mode root + g_libpath
// ── 5.9.41-lab14: ONE-PASS WALKER ────────────────────────────────────────────
// The walker below used File::openNextFile(), which stat()s and opens EVERY entry by
// path. On FAT an open-by-name searches the folder from the top, so in the 26 GB bulk
// card's letter folders (~5,000 entries with long TOSEC names = ~1 MB of directory)
// each entry re-read about half the folder: 3-7 entries/s, ~3 hours for the card.
// scanDirFast() reads each folder ONCE with FatFs f_readdir() (name, size and
// attributes come with the entry) and takes each .nfo's first sector straight off the
// entry it just read (gti_fatwalk.h) - measured on a copy of that layout (FatFs R0.15
// host build, 15,127 entries): 14,120 sector reads instead of 33.5 million, 0 blurb
// mismatches against a normal open+read. Output is identical to scanDirInto(): same
// paths, same cover/manual/HD harvest, same organisational-folder recursion.
// Differences, on purpose: .nfo files of disk 2+ of a multi-disk set are not read
// (only disk 1's blurb is ever used - saves ~half the blurb PSRAM on TOSEC sets), and
// the walk stops cleanly if PSRAM runs short instead of crashing later.
// FASTSCAN=OFF in CONFIG.TXT forces the old walker (A/B timing).
static bool     g_fastscan=true;
static int      g_fw_drv=-2;                    // FatFs drive of the card: -2 unknown, -1 none (use the old walker)
static uint8_t* g_fw_sec=nullptr;               // one DMA-capable 512-byte sector for raw .nfo heads
static uint32_t g_fw_nfo_raw=0,g_fw_nfo_open=0,g_fw_nfo_skip=0,g_fw_entries=0;
static bool     g_fw_lowmem=false;
// 5.9.41-lab14b: TWO PASSES. lab14 read .nfo blurbs during the walk and on the 26 GB card
// they ate the PSRAM before the walk reached T (25,800 of ~33,400 images), and grouping then
// ran out. Now pass 1 collects every disk image (the list is what matters), noting which
// folders hold .nfo files; pass 2 re-reads just those folders and takes blurbs only while
// PSRAM stays above what grouping will need for the images we now know we have.
static int      g_fw_pass=1;
static std::vector<String> g_fw_nfodirs;        // folders with .nfo files, in walk order
static uint32_t g_fw_disk2=0,g_fw_nfo_budget=0; // images that are disk 2+ (no game of their own); blurbs dropped for memory
static size_t   g_fw_floor=0;                   // pass 2: keep at least this much PSRAM free
// PSRAM that grouping + the UI will need for `files` images making `games` games: grouping
// scratch (~24 B/image), one GameEntry + name + disk list per game (GAME_BYTES, lab15b), plus 1.5 MB for the
// RAM disk's minimum (1 MB) and the tile cache. During the walk the game count is estimated at
// half the images (the bulk card is 0.46); after the walk it is counted exactly.
// lab14e: PSRAM the running GTi needs AFTER the library: reel tiles, cover decode, screensaver,
// UI. The RAM disk is no longer in here - it is allocated before the library is sized.
#define LIB_RUNTIME_RESERVE (1024u*1024u+64u*1024u)
static size_t g_fw_rt_reserve=LIB_RUNTIME_RESERVE;   // + the BIGDISK size only if the RAM disk isn't allocated yet
static uint32_t g_fw_nfodir_skip=0;              // lab14e: .nfo folders pass 2 never opened (no memory left for blurbs)
static bool   g_liblimit=true;                  // LIBLIMIT=OFF: load what fits (the rated amount) instead of halting (documented in CONFIG.TXT since lab14o)
static bool   g_devmode=true;                   // lab14p: DEVMODE=OFF hides Settings -> TEST TOOLS
static uint32_t g_fw_nfo1=0;                    // pass 1: .nfo files that would give a blurb (disk 1 / single)
// lab14d: once the list can't grow (LIBLIMIT on), the walk carries on COUNTING only, so the
// halt screen and GTI_CAPACITY.TXT report the card's real size and every folder, not "at least".
static bool     g_fw_countonly=false;
static String   g_fw_cutdir;                    // lab14i: the folder where a too-big walk stopped loading (later covers/saves in it were never seen)
static uint32_t g_fw_xtra=0,g_fw_xtra2=0;          // images (and disk-2+ images) counted but not listed
static std::vector<std::pair<String,uint32_t>> g_fw_xtra_per;   // counted-only images per top-level folder
static std::vector<uint64_t> g_fw_xkeys; static uint32_t g_fw_xsingles=0;   // counted-only: game keys (8 B each) -> exact game count
// lab15b: what one game costs once grouped: a 36 B record + its name (TOSEC names average ~55 chars)
// + its cover path when it has one + a multi-disk set's disk list, all packed in the game arena.
// Was 180 (measured 177 B on Mez's card before lab15b, cover paths not included).
#define GAME_BYTES 120
static inline size_t fwNeedAfter(size_t files,size_t games){ return files*24+games*GAME_BYTES+g_fw_rt_reserve; }
static size_t fwCountGames(const PathList& v){   // same key as buildGameList's grouping
  std::vector<uint64_t> keys; size_t singles=0;
  for(size_t i=0;i<v.size();i++){ const String f=v[i]; if(getDiskNumber(f)>0){ String k=parentDir(f)+"\x01"+getGameBaseName(f); keys.push_back(coverHash(k)); } else singles++; }
  std::sort(keys.begin(),keys.end());
  return singles+(size_t)(std::unique(keys.begin(),keys.end())-keys.begin());
}
static int fwFindDrive(const String& vdir){
  FF_DIR* t=(FF_DIR*)malloc(sizeof(FF_DIR)); if(!t) return -1;
  int found=-1;
  for(int d=0; d<FF_VOLUMES && found<0; d++){ String p=String(d)+":"+vdir; if(f_opendir(t,p.c_str())==FR_OK){ f_closedir(t); found=d; } }
  free(t); return found;
}
// Room for the next entry? Checked per entry (cheap) - the vectors double as they grow,
// so the next doubling must fit in one PSRAM block as well as leaving the reserve.
static bool fwMemOk(size_t outSize,size_t outCap){
  size_t freeP=heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  if(freeP<fwNeedAfter(outSize,outSize/2)) return false;   // lab14b: reserve scales with the library found so far
  size_t need=PathList::CHUNK;                                  // lab15a: the next 32 KB text block
  if(outSize+1>=outCap) need=std::max(need,(size_t)(outCap?outCap*2:1024)*8);   // the 8-byte entry table doubling
  if(g_nfoharvest.size()+1>=g_nfoharvest.capacity()) need=std::max(need,g_nfoharvest.capacity()*2*sizeof(NfoRec));
  if(need && heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)<need+65536) return false;
  return true;
}
// One entry of folder `pdir` (VFS path, e.g. "/ADF/A"), exactly as scanDirInto's loops treat it.
static void fwEntry(const String& pdir, FF_DIR* dp, FILINFO* fi, PathList& out, const String& ext, int& count){
  if(fi->fname[0]=='.'&&fi->fname[1]=='_') return;   // lab14h: macOS "._name" metadata files - never a game, cover or blurb
  String fn=fi->fname; String u=fn; u.toUpperCase();
  g_fw_entries++;
  if(g_fw_countonly){                                  // lab14d: list is full - just count what's left
    if(scanTick(count+(int)g_fw_xtra))drawScanFrame(count+(int)g_fw_xtra);
    if(u.endsWith(".NFO")){ if(!(fi->fattrib&AM_DIR)&&getDiskNumber(fn)<=1) g_fw_nfo1++; return; }
    if(u.indexOf(".SAV.")>=0) return;
    if(g_mode==MODE_GEN?isGenImage(u):(u.endsWith(ext)||u.endsWith(".IMG")||u.endsWith(".ADZ"))){
      g_fw_xtra++; int dn=getDiskNumber(fn); if(dn>1) g_fw_xtra2++;
      if(dn>0){ String k=pdir+"\x01"+getGameBaseName(fn); g_fw_xkeys.push_back(coverHash(k)); } else g_fw_xsingles++;   // same key as grouping
      int a=pdir.indexOf('/',1); int b=a>=0?pdir.indexOf('/',a+1):-1; String top=a>=0?(b>a?pdir.substring(a+1,b):pdir.substring(a+1)):String("(top level)");
      if(g_fw_xtra_per.empty()||g_fw_xtra_per.back().first!=top) g_fw_xtra_per.push_back({top,0});
      g_fw_xtra_per.back().second++;
    }
    return;
  }
  if(scanTick(count))drawScanFrame(count);
  String fp=pdir+"/"+fn; if(!fp.startsWith("/"))fp="/"+fp;
  if(isCoverExtU(u)){String ap=fp;ap.toLowerCase();uint64_t ch=coverHash(ap);g_coverset.push_back(ch);
    uint32_t cl=0;                                   // lab14f: and where it starts, for the cover-cache build
    if(!(fi->fattrib&AM_DIR) && fi->fsize>0 && fw_entry_clust(dp,fi,&cl)){
      g_coverloc.push_back({ch,cl,(uint32_t)fi->fsize,fi->fdate,fi->ftime});
      if(!g_fw_fs){ g_fw_fs=dp->obj.fs; g_fw_fsid=dp->obj.fs->id; } } }
  if(u.endsWith(".NFO")){
    if(fi->fattrib&AM_DIR) return;
    if(g_fw_pass==1){ if(g_fw_nfodirs.empty()||g_fw_nfodirs.back()!=pdir) g_fw_nfodirs.push_back(pdir); if(getDiskNumber(fn)<=1) g_fw_nfo1++; return; }   // lab14b: blurbs in pass 2
    if(getDiskNumber(fn)>1){ g_fw_nfo_skip++; return; }          // disk 2+ blurb is never shown
    char nb[513]; int nr=0; uint32_t n=0;
    if(g_fw_sec && fw_read_head(dp,fi,g_fw_sec,512,&n)){ nr=(int)(n>512?512:n); memcpy(nb,g_fw_sec,nr); g_fw_nfo_raw++; }
    else { File e=SD_MMC.open(fp.c_str(),FILE_READ); if(e){ nr=e.read((uint8_t*)nb,512); if(nr<0)nr=0; e.close(); } g_fw_nfo_open++; }
    nb[nr]=0; sideNoteNfo(fn,String(nb)); return;
  }
  if(u.endsWith(".RTFM")){g_manualset.push_back(sideKey(fn));return;}
  if(u.indexOf(".SAV.")>=0){ if(u.endsWith(".TMP"))SD_MMC.remove(fp); else if(!(fi->fattrib&AM_DIR))savSetNoteWalk(fp); return; }   // lab14i: note the save
  if(g_mode==MODE_GEN?isGenImage(u):(u.endsWith(ext)||u.endsWith(".IMG")||u.endsWith(".ADZ"))){out.push_back(fp);count++;
    if(getDiskNumber(fn)>1)g_fw_disk2++;
    if((uint32_t)fi->fsize>HD_FLAG_BYTES)g_hdset.push_back(sideKey(fn));}
}
// lab14b pass 2: the .nfo files of one folder, blurbs taken while the PSRAM budget allows.
static void fwNfoDir(const String& pdir,int count){
  FF_DIR* d=(FF_DIR*)malloc(sizeof(FF_DIR)); FILINFO* fi=(FILINFO*)malloc(sizeof(FILINFO));
  if(!d||!fi||f_opendir(d,(String(g_fw_drv)+":"+pdir).c_str())!=FR_OK){ free(d);free(fi); return; }
  while(f_readdir(d,fi)==FR_OK && fi->fname[0]){
    if(fi->fattrib&AM_DIR) continue;
    size_t ln=strlen(fi->fname); if(ln<4||strcasecmp(fi->fname+ln-4,".nfo")!=0) continue;
    if(scanTick(count))drawScanFrame(count);
    String fn=fi->fname;
    if(getDiskNumber(fn)>1){ g_fw_nfo_skip++; continue; }
    bool grow=g_nfoharvest.size()+1>=g_nfoharvest.capacity();
    if(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)<g_fw_floor ||
       (grow && heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)<g_nfoharvest.capacity()*2*sizeof(NfoRec)+65536)){ g_fw_nfo_budget++; continue; }
    char nb[513]; int nr=0; uint32_t n=0;
    if(g_fw_sec && fw_read_head(d,fi,g_fw_sec,512,&n)){ nr=(int)(n>512?512:n); memcpy(nb,g_fw_sec,nr); g_fw_nfo_raw++; }
    else { String fp=pdir+"/"+fn; File e=SD_MMC.open(fp.c_str(),FILE_READ); if(e){ nr=e.read((uint8_t*)nb,512); if(nr<0)nr=0; e.close(); } g_fw_nfo_open++; }
    nb[nr]=0; sideNoteNfo(fn,String(nb));
  }
  f_closedir(d); free(d); free(fi);
}
static void scanDirFast(const String& dir, PathList& out, const String& ext, int depth, int& count){
  if(depth>5||g_fw_lowmem) return;                                  // safety cap against pathological trees
  FF_DIR* d=(FF_DIR*)malloc(sizeof(FF_DIR)); FILINFO* fi=(FILINFO*)malloc(sizeof(FILINFO));
  FF_DIR* d2=(FF_DIR*)malloc(sizeof(FF_DIR)); FILINFO* f2=(FILINFO*)malloc(sizeof(FILINFO));
  String drv=String(g_fw_drv)+":";
  if(!d||!fi||!d2||!f2||f_opendir(d,(drv+dir).c_str())!=FR_OK){ free(d);free(fi);free(d2);free(f2); return; }
  while(!g_fw_lowmem && f_readdir(d,fi)==FR_OK && fi->fname[0]){
    String en=dir+"/"+fi->fname;
    if(fi->fattrib&AM_DIR){
      {String leaf=fi->fname;leaf.toUpperCase(); if(leaf=="SAMPLE"||leaf.startsWith("."))continue;}   // skip SAMPLE + dot-folders at every level
      size_t _imgs0=out.size()+g_fw_xtra;                           // lab14d: counted-only images count too
      if(f_opendir(d2,(drv+en).c_str())==FR_OK){
        while(f_readdir(d2,f2)==FR_OK && f2->fname[0]){
          if(!g_fw_countonly&&!fwMemOk(out.size(),out.capacity())){ if(!g_fw_cutdir.length())g_fw_cutdir=en; if(g_liblimit) g_fw_countonly=true; else { g_fw_lowmem=true; break; } }   // lab14i: remember the cut folder
          fwEntry(en,d2,f2,out,ext,count);
        }
        f_closedir(d2);
      }
      if(g_fw_lowmem) break;
      if(out.size()+g_fw_xtra==_imgs0){                              // image-less folder = organisational
        if(g_categories&&(g_libpath.length()==0||g_nesting)){ g_cats.push_back(String(fi->fname)); }
        else scanDirFast(en,out,ext,depth+1,count);                  // recurse-and-flatten
      }
    } else {
      if(!g_fw_countonly&&!fwMemOk(out.size(),out.capacity())){ if(!g_fw_cutdir.length())g_fw_cutdir=dir; if(g_liblimit) g_fw_countonly=true; else { g_fw_lowmem=true; break; } }   // lab14i: remember the cut folder
      fwEntry(dir,d,fi,out,ext,count);
    }
  }
  f_closedir(d); free(d);free(fi);free(d2);free(f2);
}
// Recurse the mode tree collecting disk images into `out` (flattened). A folder with
// NO direct disk images is an ORGANISATIONAL folder: in Categories mode it becomes a
// browsable bucket (g_cats); otherwise we recurse into it so games nested under
// letter/label folders (ADF/A/<game>/, ADF/Games/<game>/) still surface in the flat
// list — the card can be organised, the screen looks exactly the same.
static void scanDirInto(const String& dir, PathList& out, const String& ext,
                        int depth, int& count){
  if(depth>5) return;                                   // safety cap against pathological trees
  File root=SD_MMC.open(dir.c_str());
  if(!root||!root.isDirectory()){ if(root)root.close(); return; }
  File gd;while((gd=root.openNextFile())){
    String en=gd.name();if(!en.startsWith("/"))en=dir+"/"+en;
    if(gd.isDirectory()){
      {String leaf=en;int sl2=leaf.lastIndexOf('/');if(sl2>=0)leaf=leaf.substring(sl2+1);leaf.toUpperCase();
       if(leaf=="SAMPLE"||leaf.startsWith(".")){gd.close();continue;}}   // skip SAMPLE + dot-folders at every level
      size_t _imgs0=out.size();
      File e;while((e=gd.openNextFile())){String fn=e.name();int sl=fn.lastIndexOf('/');if(sl>=0)fn=fn.substring(sl+1);if(fn.startsWith("._")){e.close();continue;}String u=fn;u.toUpperCase();   // lab14h: skip macOS "._" files
      if(scanTick(count))drawScanFrame(count);   // every entry: a folder of 2000 covers must not look hung
      if(isCoverExtU(u)){String ap=en+"/"+fn;if(!ap.startsWith("/"))ap="/"+ap;ap.toLowerCase();g_coverset.push_back(coverHash(ap));}
      if(u.endsWith(".NFO")){char _nb[513];int _nr=e.read((uint8_t*)_nb,512);if(_nr<0)_nr=0;_nb[_nr]=0;sideNoteNfo(fn,String(_nb));e.close();continue;}   // 5.9.31-lab1: read the blurb while the handle is open (no extra dir-scan)
      if(u.endsWith(".RTFM")){g_manualset.push_back(sideKey(fn));e.close();continue;}
      if(u.indexOf(".SAV.")>=0){
        if(u.endsWith(".TMP")){String fp=en+"/"+fn;if(!fp.startsWith("/"))fp="/"+fp;SD_MMC.remove(fp);}
        else{String fp=en+"/"+fn;if(!fp.startsWith("/"))fp="/"+fp;savSetNoteWalk(fp);}   // lab14i
        e.close();continue;}
      if(g_mode==MODE_GEN?isGenImage(u):(u.endsWith(ext)||u.endsWith(".IMG")||u.endsWith(".ADZ"))){String fp=en+"/"+fn;if(!fp.startsWith("/"))fp="/"+fp;out.push_back(fp);count++;
        if((uint32_t)e.size()>HD_FLAG_BYTES)g_hdset.push_back(sideKey(fn));   // 5.9.31-lab1: HD flag straight off the directory entry — isHDImage's stat() was a whole walk
        }e.close();}
      if(out.size()==_imgs0){                            // image-less folder = organisational
        if(g_categories&&(g_libpath.length()==0||g_nesting)){
          String _cn=en;int _cs=_cn.lastIndexOf('/');if(_cs>=0)_cn=_cn.substring(_cs+1);g_cats.push_back(_cn);   // Categories: browsable bucket
        }else{
          scanDirInto(en,out,ext,depth+1,count);   // otherwise recurse-and-flatten
        }
      }
    }
    else{String fn=en;int sl=fn.lastIndexOf('/');if(sl>=0)fn=fn.substring(sl+1);if(fn.startsWith("._")){gd.close();continue;}String u=fn;u.toUpperCase();   // lab14h
      if(scanTick(count))drawScanFrame(count);   // per entry, as above
      if(isCoverExtU(u)){String ap=en;ap.toLowerCase();g_coverset.push_back(coverHash(ap));}
      if(u.endsWith(".NFO")){char _nb[513];int _nr=gd.read((uint8_t*)_nb,512);if(_nr<0)_nr=0;_nb[_nr]=0;sideNoteNfo(fn,String(_nb));gd.close();continue;}
      if(u.endsWith(".RTFM")){g_manualset.push_back(sideKey(fn));gd.close();continue;}
      if(u.indexOf(".SAV.")>=0){
        if(u.endsWith(".TMP"))SD_MMC.remove(en);
        else{String fp=en;if(!fp.startsWith("/"))fp="/"+fp;savSetNoteWalk(fp);}   // lab14i
        gd.close();continue;}
      if(g_mode==MODE_GEN?isGenImage(u):(u.endsWith(ext)||u.endsWith(".IMG")||u.endsWith(".ADZ"))){out.push_back(en);count++;
        if((uint32_t)gd.size()>HD_FLAG_BYTES)g_hdset.push_back(sideKey(fn));}}
    gd.close();}
  root.close();
}
// ── 5.9.41-lab14d: LIBRARY TOO BIG ─────────────────────────────────────────
// The card holds more games than this GTi's memory can list (with the RAM disk at its
// configured size). Say so loudly, change nothing, write a report to the card, and offer
// SD ACCESS so it can be trimmed from a PC. Never returns: SD ACCESS or RESCAN restart.
static void capReport(const PathList& files,uint32_t images,uint32_t games,uint32_t fit,uint32_t pct){
  const bool atLeast=false;
  std::vector<std::pair<String,int>> per;                 // disk images per top-level folder (listed + counted-only)
  auto add=[&per](const String& top,int n){ for(auto&p:per) if(p.first==top){p.second+=n;return;} per.push_back({top,n}); };
  for(size_t fi_=0;fi_<files.size();fi_++){ const String f=files[fi_]; int a=f.indexOf('/',1); int b=a>=0?f.indexOf('/',a+1):-1; String top=(a>=0&&b>a)?f.substring(a+1,b):String("(top level)");
    if(!per.empty()&&per.back().first==top) per.back().second++; else add(top,1); }
  for(auto&x:g_fw_xtra_per) add(x.first,(int)x.second);
  File r=SD_MMC.open("/GTI_CAPACITY.TXT",FILE_WRITE); if(!r) return;
  r.printf("GTi library capacity report - %s\r\n\r\n",FW_VERSION);
  r.printf("Your card         : %s%u games (%u disk images)\r\n",atLeast?"at least ":"",(unsigned)games,(unsigned)images);
  r.printf("GTi limit         : about %u games (with a %u KB disk buffer, BIGDISK=%s)\r\n",(unsigned)fit,(unsigned)g_img_max_kb,g_img_max_kb>DISK_IMG_DEF_KB?"ON":"OFF");
  if(fit<games) r.printf("Over by           : about %u games (roughly %u disk images)\r\n",(unsigned)(games-fit),(unsigned)((uint64_t)images*(games-fit)/(games?games:1)));
  r.printf("Memory needed     : %s%u%% of what this GTi has\r\n\r\n",atLeast?"over ":"",(unsigned)pct);
  r.printf("Disk images per folder%s:\r\n",atLeast?" (the scan stopped early)":"");
  for(auto&p:per) r.printf("  %-24s %6d\r\n",p.first.c_str(),p.second);
  r.printf("\r\nOptions: move whole folders off the card (e.g. a second card for N-Z),\r\n");
  r.printf("use a one-copy-per-title library (prep_library.py --mode library --1g1r),\r\n");
  r.printf("or BIGDISK=OFF if it is ON (smaller disk buffer, more room for the list).\r\n");
  r.printf("To load the first ~%u games instead of stopping, add LIBLIMIT=OFF to CONFIG.TXT.\r\n",(unsigned)fit);
  r.close();
}
// lab14e: one flashing halt screen for LIBRARY TOO BIG and LIBRARY LOAD CRASHED. `nt` lines are
// a left-aligned table (so the colons line up), the rest centred. SD ACCESS on the left; the
// right button (RESCAN / TRY AGAIN) restarts. Never returns.
static void haltScreen(const char* title,const String* lines,int nl,int nt,const char* b2){
  const int bw=150,bh=40,by=gH-bh-10,b1x=gW/2-bw-10,b2x=gW/2+10;
  bool on=false; uint32_t t=0; int pressed=0,rel=0;
  for(;;){
    if(millis()-t>450){ t=millis(); on=!on;
      uint16_t bg=0x0000, fr=on?(uint16_t)0xF800:(uint16_t)0x6000;
      gfx_fillScreen(bg); for(int k=0;k<6;k++) gfx_drawRect(k,k,gW-2*k,gH-2*k,fr);
      gfx_setTextSize(3);gfx_setTextColor(on?(uint16_t)0xF800:(uint16_t)0xFFFF,bg);
      gfx_setCursor((gW-gfx_textWidth(title))/2,18);gfx_print(title);
      gfx_setTextSize(1);
      int tw=0; for(int i=0;i<nt;i++){ int w=gfx_textWidth(lines[i].c_str()); if(w>tw)tw=w; }
      int y=62;
      for(int i=0;i<nl;i++){
        gfx_setTextColor(i<nt?(uint16_t)0xFFE0:(uint16_t)0xFFFF,bg);
        gfx_setCursor(i<nt?(gW-tw)/2:(gW-gfx_textWidth(lines[i].c_str()))/2,y); gfx_print(lines[i].c_str());
        y+=(i==nt-1)?22:16;
      }
      gfx_fillRoundRect(b1x,by,bw,bh,8,(uint16_t)0x05FF); gfx_setTextSize(2); gfx_setTextColor(0x0000,(uint16_t)0x05FF);
      {const char*s="SD ACCESS";gfx_setCursor(b1x+(bw-gfx_textWidth(s))/2,by+12);gfx_print(s);}
      gfx_fillRoundRect(b2x,by,bw,bh,8,(uint16_t)0x4208); gfx_setTextColor(0xFFFF,(uint16_t)0x4208);
      gfx_setCursor(b2x+(bw-gfx_textWidth(b2))/2,by+12);gfx_print(b2);
      gfx_flush();
    }
    uint16_t tx=0,ty=0; bool have=Touch_ReadFrame()&&getTouchXY(&tx,&ty);
    if(have){ rel=0; if(!pressed){ pressed=1;
      if(ty>=by&&ty<by+bh){
        if(tx>=b1x&&tx<b1x+bw){ gfx_fillScreen(0x0000); g_sdaccess_magic=SDACCESS_MAGIC; delay(300); ESP.restart(); }
        if(tx>=b2x&&tx<b2x+bw){ gfx_fillScreen(0x0000); g_cap_magic=0; delay(300); ESP.restart(); } } } }
    else if(pressed&&++rel>=3) pressed=0;
    delay(30);
  }
}
static void libTooBig(const PathList& files,uint32_t images,uint32_t games,uint32_t fit,uint32_t pct){
  gLog("[capacity] TOO BIG: %u images, %u games, fits ~%u games, %u%% - halted\n",(unsigned)images,(unsigned)games,(unsigned)fit,(unsigned)pct);
  capReport(files,images,games,fit,pct);
  g_cap_magic=CAP_MAGIC; g_cap_images=images; g_cap_games=games; g_cap_fit=fit; g_cap_pct=pct; g_cap_atleast=0;
  // lab14e: "This GTi can list about N" sat right under the card's total and read as the
  // card's own count. Label both, and say how far over it is.
  String L[6];
  L[0]=String("Your card : ")+String(games)+" games ("+String((unsigned)images)+" disk images)";
  L[1]=String("GTi limit : about ")+String(fit)+" games";
  L[2]=String("Over by   : about ")+String(games>fit?games-fit:0)+" games (needs "+String(pct)+"% of memory)";
  L[3]="Nothing on the card was changed.";
  L[4]="Remove games or split the card, then RESCAN.";
  L[5]="Details: GTI_CAPACITY.TXT on the card";
  haltScreen("LIBRARY TOO BIG",L,6,3,"RESCAN");
}
// lab14e: the last boot panicked while building the library. Don't try again on our own (that
// was the reboot loop) - say what happened and let the user choose.
static void libCrashed(){
  gLog("[crash] library load crashed last boot - halted instead of retrying\n");
  String L[6];
  L[0]=String("Died in   : ")+bcName(g_bc_prev.stage);
  L[1]=String("Working on: ")+String(g_bc_prev.n)+(g_bc_prev.stage==BC_THUMBS?" games":" items");
  L[2]=String("Free then : ")+String(g_bc_prev.psram/1024)+" KB PSRAM, "+String(g_bc_prev.intr/1024)+" KB internal";
  L[3]="The last boot crashed while loading the library.";
  L[4]="Nothing on the card was changed. Details: GTI/gti.log";
  L[5]="TRY AGAIN, or SD ACCESS to make the library smaller.";
  haltScreen("LOAD CRASHED",L,6,3,"TRY AGAIN");
}
static void showCardTooBig(int count){
  gfx_setTextSize(1);gfx_setTextColor(0xFD20,0x1082);
  {const char*s="LIBRARY TOO BIG FOR THIS GTi";int tw=gfx_textWidth(s);gfx_fillRect(0,gH/2+60,gW,32,0x1082);gfx_setCursor((gW-tw)/2,gH/2+64);gfx_print(s);}
  {String s2="loading the first "+String(count)+" disk images - the rest are left out";int tw=gfx_textWidth(s2.c_str());gfx_setCursor((gW-tw)/2,gH/2+78);gfx_print(s2.c_str());}
  gfx_flush();delay(4000);
}
static void scanImagesAnimated(PathList& out){
  out.clear();out.reserve(4096);   // lab15a: fills the caller's compact list (no String per image)
  g_coverset.clear();g_coverset.reserve(4096);g_cats.clear();
  fwLocFree(); g_fw_fs=nullptr; g_fw_fsid=0;         // lab14f: fresh walk, fresh locations
  g_nfoharvest.clear();g_manualset.clear();g_hdset.clear();g_sidecars_harvested=false;   // 5.9.31-lab1
  g_savset.clear(); g_savset_ok=false;   // lab14i: fresh walk, fresh save list
  String dir=g_mode==MODE_ADF?"/ADF":g_mode==MODE_DSK?"/DSK":"/GENERIC";if(g_categories&&g_libpath.length())dir+=g_libpath;String ext=g_mode==MODE_ADF?".ADF":".DSK";
  // Draw the scan screen ONCE, here. drawScanFrame() then only repaints the
  // counter, so a RESCAN gets immediate feedback without a second full paint.
  gfx_fillScreen(0x1082);
  gfx_setTextSize(2);gfx_setTextColor(0xFC60,0x1082);
  {const char*s="SCANNING";int tw=gfx_textWidth(s);gfx_setCursor((gW-tw)/2,gH/2-60);gfx_print(s);}
  drawScanBall(gW/2,gH/2-30);                     // static decoration - no longer animated
  gfx_setTextSize(1);gfx_setTextColor(0x4A8A,0x1082);
  {const char*s="Building game index...";int tw=gfx_textWidth(s);gfx_setCursor((gW-tw)/2,gH/2+40);gfx_print(s);}
  gfx_flush();
  g_scan_painted=true; g_scan_lastdraw=millis(); g_scan_lastcount=-1;   // reset per scan, so RESCAN works too
  int count=0;
  // 5.9.41-lab14: one-pass FatFs walker when the card's FatFs drive is found, else the old one.
  g_fw_nfo_raw=g_fw_nfo_open=g_fw_nfo_skip=g_fw_entries=0; g_fw_lowmem=false;
  g_fw_disk2=g_fw_nfo_budget=0; g_fw_nfodirs.clear(); g_fw_pass=1; g_fw_nfo1=0;
  g_fw_countonly=false; g_fw_cutdir=""; g_fw_xtra=g_fw_xtra2=0; g_fw_xtra_per.clear(); g_fw_xkeys.clear(); g_fw_xsingles=0;
  // lab14d: what the library may use = PSRAM free now minus the RAM disk at its configured size
  // (the BIGDISK size, allocated after the scan) and ~1 MB for cover tiles/UI.
  const size_t _cap_psram0=heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  // lab14e: the same budget whether LIBLIMIT is on or off. lab14d gave OFF a thin 1.5 MB margin
  // that ignored the 2.9 MB RAM disk, so OFF ran the board at 99% and panicked. Now OFF simply
  // loads what the halt screen says fits. The RAM disk is normally already allocated (setup does
  // it first); if not, its size is held back here too.
  g_fw_rt_reserve = (size_t)LIB_RUNTIME_RESERVE + (g_disk ? 0u : (size_t)g_img_max_kb*1024u);
  g_fw_nfodir_skip=0;
  bcSet(BC_SCAN,0);
  uint32_t _fw_t0=millis();
  if(g_fastscan && g_fw_drv==-2) g_fw_drv=fwFindDrive(dir);
  if(g_fastscan && g_fw_drv>=0 && !g_fw_sec) g_fw_sec=(uint8_t*)heap_caps_malloc(512,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);
  bool _fw_fast=g_fastscan && g_fw_drv>=0;
  uint32_t _fw_t1=0;
  if(_fw_fast){
    scanDirFast(dir,out,ext,0,count);                        // pass 1: every disk image
    _fw_t1=millis();
    { size_t b0=out.bytes(); out.sort(); bool ok=out.compact(); out.shrink_to_fit();   // lab15a: sort + pack the names BEFORE sizing the library
      gLog("[scan] list packed: %u images %u KB -> %u KB%s\n",(unsigned)out.size(),(unsigned)(b0/1024),(unsigned)(out.bytes()/1024),ok?"":" (no room to pack - kept as is)"); }
    // What grouping + UI will need for exactly these images. If the walk's estimate was
    // optimistic (a library with more games per image), drop images from the END of the walk
    // until it fits - a shorter list beats a crash in buildGameList.
    size_t games=fwCountGames(out);
    size_t freeP=heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    { // lab14d: capacity. core = file list (measured) + grouping; blurbs are optional extras.
      size_t budget=_cap_psram0>g_fw_rt_reserve?_cap_psram0-g_fw_rt_reserve:1;
      size_t listed=_cap_psram0>freeP?_cap_psram0-freeP:0;
      size_t imgs=out.size()+g_fw_xtra;                              // whole card (listed + counted-only)
      size_t gamesAll=games;
      if(g_fw_xtra){ std::sort(g_fw_xkeys.begin(),g_fw_xkeys.end()); gamesAll+=g_fw_xsingles+(size_t)(std::unique(g_fw_xkeys.begin(),g_fw_xkeys.end())-g_fw_xkeys.begin()); std::vector<uint64_t>().swap(g_fw_xkeys); }
      size_t listedAll=out.empty()?0:(size_t)((uint64_t)listed*imgs/out.size());   // list bytes scaled to the whole card
      size_t core=listedAll+imgs*24+gamesAll*GAME_BYTES, full=core+(size_t)g_fw_nfo1*175;
      uint32_t pctCore=(uint32_t)((uint64_t)core*100/budget), pctFull=(uint32_t)((uint64_t)full*100/budget);
      uint32_t fit=gamesAll?(uint32_t)((uint64_t)gamesAll*budget/(core?core:1)):0;
      gLog("[capacity] %u images, %u games, %u blurbs: list %u%% (+blurbs %u%%) of %u KB, fits ~%u games%s\n",(unsigned)imgs,(unsigned)gamesAll,(unsigned)g_fw_nfo1,
           (unsigned)pctCore,(unsigned)pctFull,(unsigned)(budget/1024),(unsigned)fit,g_fw_countonly?" (list full - rest counted)":"");
      if(g_liblimit && (g_fw_countonly||pctCore>100)) libTooBig(out,(uint32_t)imgs,(uint32_t)gamesAll,fit,pctCore);   // never returns
      if(g_liblimit){                                   // fits: show the meter on the scan screen
        uint16_t c=pctFull<85?(uint16_t)0x07E0:(pctFull<=100?(uint16_t)0xFD20:(uint16_t)0xFD20);
        String m="Library: "+String((unsigned)games)+" games - memory "+String(pctFull>100?pctCore:pctFull)+"%";
        if(pctFull>100) m+=" (some descriptions skipped)";
        gfx_setTextSize(1);gfx_setTextColor(c,0x1082);gfx_fillRect(0,gH/2+58,gW,12,0x1082);
        gfx_setCursor((gW-gfx_textWidth(m.c_str()))/2,gH/2+60);gfx_print(m.c_str());gfx_flush();
      }
      g_cap_magic=0;
    }
    if(!out.empty() && freeP<fwNeedAfter(out.size(),games)){
      size_t n=out.size(); double gpf=(double)games/n;          // games per image
      double per=(double)out.bytes()/n;                        // lab15a: what one image really costs in the list now (was ~112 B)
      double fit=((double)freeP+n*per-(double)g_fw_rt_reserve)/(per+24.0+(double)GAME_BYTES*gpf);   // lab14e: same reserve as the budget
      size_t keep=fit<0?0:(size_t)fit; if(keep<n){ out.resize(keep); count=(int)keep; g_fw_lowmem=true; games=fwCountGames(out); }
    }
    g_fw_floor=fwNeedAfter(out.size(),games);                // pass 2 keeps this much PSRAM free
    g_fw_pass=2; bcSet(BC_BLURBS,(uint32_t)out.size());
    for(size_t i=0;i<g_fw_nfodirs.size();i++){                     // pass 2: blurbs within budget
      // lab14e: on a card this full pass 2 spent 24 s reading 18,000 .nfo heads only to drop every
      // one for memory. Once there's no room left, stop opening folders.
      if(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)<g_fw_floor+64u*1024u){ g_fw_nfodir_skip=(uint32_t)(g_fw_nfodirs.size()-i); break; }
      fwNfoDir(g_fw_nfodirs[i],count);
    }
    g_fw_pass=1; std::vector<String>().swap(g_fw_nfodirs);
  }
  else scanDirInto(dir,out,ext,0,count);
  gLog("[scan] %s walker drv=%d: %u entries, %d images (%u are disk 2+) in %lums (blurbs %lums) | nfo raw=%u open=%u skipped(disk2+)=%u dropped(memory)=%u dirs-skipped=%u | psram=%u floor=%u int=%u%s\n",
       _fw_fast?"two-pass":"legacy",g_fw_drv,(unsigned)g_fw_entries,count,(unsigned)g_fw_disk2,(unsigned long)(millis()-_fw_t0),(unsigned long)(_fw_t1?millis()-_fw_t1:0),
       (unsigned)g_fw_nfo_raw,(unsigned)g_fw_nfo_open,(unsigned)g_fw_nfo_skip,(unsigned)g_fw_nfo_budget,(unsigned)g_fw_nfodir_skip,(unsigned)ESP.getFreePsram(),(unsigned)g_fw_floor,
       (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),g_fw_lowmem?" | TRIMMED: library too big, loaded what fits":"");
  std::sort(g_coverset.begin(),g_coverset.end());   // 5.8.8: sorted for binary_search
  std::sort(g_coverloc.begin(),g_coverloc.end(),[](const FwLoc&a,const FwLoc&b){return a.h<b.h;}); g_coverloc.shrink_to_fit();   // lab14f
  std::sort(g_manualset.begin(),g_manualset.end());
  std::sort(g_hdset.begin(),g_hdset.end());
  std::sort(g_nfoharvest.begin(),g_nfoharvest.end(),[](const NfoRec&a,const NfoRec&b){return a.k<b.k;});
  g_nfoharvest.shrink_to_fit(); g_coverset.shrink_to_fit(); g_hdset.shrink_to_fit(); g_manualset.shrink_to_fit();   // lab14b: drop vector slack before grouping
  g_sidecars_harvested=true;   // 5.9.31-lab1: tree walked -> harvest authoritative (an empty set means genuinely none, not "unknown")
  std::sort(g_savset.begin(),g_savset.end()); g_savset.erase(std::unique(g_savset.begin(),g_savset.end()),g_savset.end());   // lab14i
  g_savset_ok=true; g_savset_cutdir=g_fw_cutdir;
  if(!(g_categories&&g_libpath.length())) savSetWrite();   // lab14i: top level only (a category level holds only its own saves)
  // Final count
  drawScanFrame(count);delay(500);
  if(g_fw_lowmem) showCardTooBig(count);            // 5.9.41-lab14: say so instead of crashing later
  out.sort();out.compact();out.shrink_to_fit();   // lab14b: no doubling slack; lab15a: sorted + packed (no-op if pass 1 did it)
  gLog("[scan] image list: %u images, %u KB PSRAM (lab15a compact list)\n",(unsigned)out.size(),(unsigned)(out.bytes()/1024));
}

static String gameCachePath();   // fwd (lab14b)
static bool listImages(fs::FS&fs,PathList&out){
  // lab14b: the .index alone is not enough. Without a .gamecache the game list is rebuilt, and
  // that needs this boot's walk harvest (covers, blurbs, HD) - otherwise every game falls back to
  // per-game SD_MMC.exists() probes (hours on a big card). The one-pass walk is cheap; do it.
  if(SD_MMC.exists(gameCachePath().c_str())&&readIndexCache(out)){
    // lab15g: NO walk this time, so nothing gathered by an earlier walk (another library's, e.g. an empty DSK
    // folder scanned a minute ago) may be taken as this library's truth: that made every ADF game "no
    // description" and "no cover" after a DSK -> ADF switch. Back to "not walked": descriptions come from
    // .nfocache and covers from the .gamecache / a one-folder listing, exactly as on a warm boot.
    g_sidecars_harvested=false; g_coverset.clear(); g_nfoharvest.clear(); g_manualset.clear(); g_hdset.clear();
    savSetLoad();return!out.empty();}   // lab14i: no walk -> the save list from the card
  scanImagesAnimated(out);writeIndexCache(out);return!out.empty();
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

// lab14o: write data that lives in the firmware's FLASH (string constants, PROGMEM arrays) via a RAM copy.
// Handed straight to the card, a flash pointer reaches the SD driver's DMA path: the driver only asks
// "is this PSRAM?" (no), assumes internal RAM and DMAs from an address it cannot read - the card gets
// garbage. That is why every freshly written CONFIG.TXT had its first 4 KB blank (stdio passes 4 KB
// chunks straight through). The SD guard now catches this too; this keeps it right without the guard.
static void sdWriteFromFlash(File& f,const uint8_t* p,size_t n){
  uint8_t tmp[256];
  for(size_t o=0;o<n;o+=sizeof(tmp)){ size_t k=(n-o<sizeof(tmp))?(n-o):sizeof(tmp); memcpy(tmp,p+o,k); f.write(tmp,k); }
}
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
  if(f){sdWriteFromFlash(f,SAMPLE_JPG,sizeof(SAMPLE_JPG));f.close();}   // lab14o: via RAM
  // v4.9.4: a self-documenting manual — the reader opened by the book button explains itself.
  f=SD_MMC.open("/ADF/SAMPLE/Sample.rtfm",FILE_WRITE);
  if(f){f.print(
    "GTi MANUAL READER - sample card\r\n"
    "\r\n"
    "This is a .rtfm manual. Any line written in [square\r\n"
    "brackets] becomes a heading AND an entry in the jump\r\n"
    "list - tap the SECTIONS button below to try it.\r\n"
    "\r\n"
    "[About]\r\n"
    "Any game can have a manual. Drop a plain-text file\r\n"
    "named like the disk, ending .rtfm, beside it:\r\n"
    "\r\n"
    "  /ADF/YourGame/YourGame.rtfm\r\n"
    "\r\n"
    "A book button then appears on the cover art. Tap it\r\n"
    "to read whatever you put here.\r\n"
    "\r\n"
    "[Reader]\r\n"
    "  - Drag to scroll; flick for a fast spin.\r\n"
    "  - SIZE cycles SMALL / NORMAL / LARGE text.\r\n"
    "  - TOP jumps back to the start.\r\n"
    "  - SECTIONS opens the jump list (only shown when the\r\n"
    "    file has [headings]).\r\n"
    "  - CLOSE or tap the page returns to the library.\r\n"
    "  - It remembers where you were reading.\r\n"
    "\r\n"
    "[Sections]\r\n"
    "Put a heading on its own line in square brackets, like\r\n"
    "[Controls] or [Cheats]. It shows in accent colour and\r\n"
    "in the SECTIONS jump list. That is the whole trick -\r\n"
    "no other markup needed.\r\n"
    "\r\n"
    "[What to put]\r\n"
    "Controls, how to play, tips, and cheat codes. Best of\r\n"
    "all: copy-protection answers - the page/word lookups\r\n"
    "and code-wheel codes. The original manual is long gone,\r\n"
    "so saving them here is the most useful thing a .rtfm\r\n"
    "can hold. Add multi-disk swap notes too.\r\n"
    "\r\n"
    "[Format]\r\n"
    "  - Plain ASCII. Accents and smart quotes are cleaned\r\n"
    "    up for you, but plain is safest.\r\n"
    "  - A blank line starts a new paragraph.\r\n"
    "  - Do not hand-wrap - text reflows to the font.\r\n"
    "  - Keep it under ~16 KB (a page or two).\r\n"
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
  // lab14i: FAT name lookups ignore case, so ONE by-name lookup answers this. The old loop opened every
  // file in the folder (openNextFile = a stat and an open per entry) - minutes in a 2,000-file folder.
  String c=dir+"/"+target; if(SD_MMC.exists(c.c_str())){ out=c; return true; } return false;
}
static bool findNFOFor(const String&p,String&out){
  String b=basenameNoExt(filenameOnly(p)),d=parentDir(p),gb=getGameBaseName(p);
  if(findInDir(d,b+".nfo",out))return true;if(gb!=b&&findInDir(d,gb+".nfo",out))return true;return false;
}
// lab15a4: lazy cover lookups used to probe up to 9 names with SD_MMC.exists(), and every probe
// reads the WHOLE folder (FAT has no index). In a 3,000-entry letter folder that was 14-44 s per game
// (gti.log, 26 Sep: the micro-thumb build spent ~20 s on each /ADF/K game - hours for the folder).
// Now the folder is listed ONCE with f_readdir (one pass, like the scan walker) and its cover names
// kept as hashes; this and every later lookup in that folder is answered from memory (R7).
static String g_lz_dir; static std::vector<uint64_t> g_lz_set; static bool g_lz_ok=false;
static bool lazyDirCovers(const String& d){
  if(g_lz_ok && g_lz_dir==d) return true;
  g_lz_ok=false; std::vector<uint64_t>().swap(g_lz_set); g_lz_dir=d;
  if(g_fw_drv==-2) g_fw_drv=fwFindDrive(d);
  if(g_fw_drv<0) return false;
  FF_DIR* dp=(FF_DIR*)malloc(sizeof(FF_DIR)); FILINFO* fi=(FILINFO*)malloc(sizeof(FILINFO));
  if(!dp||!fi||f_opendir(dp,(String(g_fw_drv)+":"+d).c_str())!=FR_OK){ free(dp); free(fi); return false; }
  uint32_t t0=millis(), n=0;
  while(f_readdir(dp,fi)==FR_OK && fi->fname[0]){ n++;
    if(fi->fattrib&AM_DIR) continue;
    String u=fi->fname; u.toUpperCase(); if(!isCoverExtU(u)) continue;
    String ap=d+"/"+fi->fname; ap.toLowerCase(); g_lz_set.push_back(coverHash(ap)); }
  f_closedir(dp); free(dp); free(fi);
  std::sort(g_lz_set.begin(),g_lz_set.end());
  g_lz_ok=true;
  gLog("[covers] %s listed once for lookups: %u entries, %u covers, %lums\n",d.c_str(),(unsigned)n,(unsigned)g_lz_set.size(),(unsigned long)(millis()-t0));
  return true;
}
static bool findJPGFor(const String&p,String&out){ if(!g_covers_on){out="";return false;}
  if(g_sidecars_harvested&&g_coverset.empty()){out="";return false;}   // lab15f: this boot's walk found NO covers at all - nothing to look up (was ~2 ms a game building 9 names)
  String b=basenameNoExt(filenameOnly(p)),d=parentDir(p),gb=getGameBaseName(p);
  // 5.3.7: VFAT lookups are case-insensitive, so the upper-case variants were pure
  // redundancy (6 probes -> 3). Name stems tried in priority order: exact, game base
  // (multi-disk), folder name (/ADF/Game/Game.jpg pattern).
  const char*exts[]={".jpg",".jpeg",".png"};
  String stems[3];int ns=0;stems[ns++]=b;if(gb!=b)stems[ns++]=gb;
  String folderName=d;{int ls=folderName.lastIndexOf('/');if(ls>0)folderName=folderName.substring(ls+1);}
  if(folderName.length()&&folderName!=b&&folderName!=gb)stems[ns++]=folderName;
  bool harvested=g_sidecars_harvested||!g_coverset.empty();   // walked this boot (lab14: even if it found NO covers) or set populated -> authoritative, zero card I/O
  bool lazy=!harvested && lazyDirCovers(d);                    // lab15a4: no walk this boot -> list this folder once
  for(int si=0;si<ns;si++)for(auto e:exts){String c=d+"/"+stems[si]+e;
    if(harvested){String cl=c;cl.toLowerCase();if(std::binary_search(g_coverset.begin(),g_coverset.end(),coverHash(cl))){out=c;return true;}}
    else if(lazy){String cl=c;cl.toLowerCase();if(std::binary_search(g_lz_set.begin(),g_lz_set.end(),coverHash(cl))){out=c;return true;}}
    else if(SD_MMC.exists(c.c_str())){out=c;return true;}}   // last resort (folder unreadable): the old per-name probe
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
static bool g_info_test=false;   // lab14k: true = Settings is showing its TEST TOOLS sub-page (lab16b-P4: moved up)
static bool g_car_active=false;  // lab16b-P4: moved up from the reel section (the NEO top bar reads it)
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
// ── v5.7.2: screensaver SLIDESHOW — full-screen photo mode with transitions (Paul Dean CR).
//    SSMODE=SLIDES (default) shows real images fit-to-screen with fade/dissolve/slide;
//    SSMODE=BOUNCE forces the classic DVD-logo bounce. Slide pool = /screensaver/*.jpg
//    PLUS the cover art of every FAVOURITED game (SSFAV=ON). If the pool is empty
//    (no folder images, no favourites) it falls through to the sprite bounce as before.
static bool g_ss_slides=true;         // SSMODE: true=slideshow, false=classic bounce
static bool g_ss_matrix=false;        // SSMODE=MATRIX: falling-code screensaver (5.8.3)
static bool g_btn_pill=true;          // BTNSTYLE: coloured rounded pill reel buttons (5.8.3)
static int  g_ss_fx=0;                // SSFX: 0=SHUFFLE 1=FADE 2=DISSOLVE 3=SLIDE 4=CUT
static bool g_ss_fav=true;            // SSFAV: fold favourited game covers into the slide pool
static uint32_t g_ss_time_ms=6000UL;  // SSTIME: seconds each slide holds (2..120)
static uint16_t* g_slA=NULL;          // slideshow double-buffer: outgoing frame (raw physical fb layout)
static uint16_t* g_slB=NULL;          // slideshow double-buffer: incoming frame
static int g_dongle_cap=32;   // CONFIG.TXT CAP= : max wireless dongles to discover/cast (1..64)
static int g_hivemind=1;      // v4.8.1 (undocumented HIVEMIND=): 1 = FLING fans out to all MuCa dongles (classic), 0 = paired dongle only
static int g_cracktro=0;      // CONFIG.TXT CRACKTRO= : boot demo style 1..6, or 0 = pick one at random each boot (-1 = OFF)
static int g_cracktro_prev=0; // remembered ON style so the Settings CRACKTRO toggle can restore it after OFF
static int g_car_bootmode=0;  // CONFIG.TXT CAROUSEL= : default boot VIEW — 0/OFF=list, 1/ON=reel, 2=LAST (restore last view, remembered in /.gtiview). v4.8.5+: carousel is ALWAYS available via the flip toggle regardless.
// ── 5.8.6: home-WiFi dongle transport (LINK=HOMEWIFI) — route the FLING via the home router to a Webby dongle's gotek.local, instead of hopping to the dongle's own AP ──
static bool   g_link_home=false;                                    // LINK: false=ESP-NOW/AP (default), true=HOME WIFI
static String g_home_ssid="", g_home_pass="", g_dongle_home_ip="";  // HOME_SSID / HOME_PASS (set in CONFIG.TXT) + cached DONGLE_HOME_IP
static String g_dav_host="",g_dav_user="",g_dav_pass="",g_dav_path="/";static int g_dav_port=443;static bool g_dav_https=true,g_dav_on=false;   // DAV_* in CONFIG.TXT (merge step 1)
static String g_dav_test="";   // DAV_TEST= : smoke test — fetch this remote path once at boot. Proves the wiring without UI; remove the key (or the hook) once real UI exists.
static bool g_web_on=false;    // WEBUI= : serve the shared web interface over HOME_SSID (merge step 2)
static void davLogSerial(const String&m){Serial.println(m);}
static void davApplyConfig(){DavConfig c;c.host=g_dav_host;c.port=(uint16_t)g_dav_port;c.https=g_dav_https;c.user=g_dav_user;c.pass=g_dav_pass;c.basePath=g_dav_path;c.enabled=g_dav_on;davClient.configure(c,davLogSerial);}
// ── Item 4: load/eject behaviour toggles (all default OFF = safest) ──
static bool g_tapload=false;    // ON = tapping the already-selected row loads it (old double-tap behaviour)
static bool g_hotswap=false;    // ON = tapping another disk while loaded swaps to it instantly
static bool g_forceswap=false;  // ON = swap disk bytes in place without the USB eject/re-attach cycle
static int g_info_x=0,g_info_w=150,g_info_bottom=0;
static String g_manual_path=""; static int g_manual_bx=0,g_manual_by=0,g_manual_bw=0,g_manual_bh=0;  // v4.9.2 .rtfm book button rect
static int g_nfo_bx=0,g_nfo_by=0,g_nfo_bw=0,g_nfo_bh=0;   // lab15j: list-panel text area (title + description) - a tap opens the whole .nfo
// lab15b: 36 bytes, no heap blocks of its own - text + multi-disk lists live in g_gtext (gti_gamestore.h)
struct GameEntry{AStr name;AStr jpg_path;AStr blurb;DiskList disk_indices;int first_file_idx=0;int disk_count=0;uint16_t plays=0;bool fav=false;bool cover_ok=false;bool nfo_done=false;bool has_manual=false;bool is_hd=false;};   // 5.9.31-lab1: sidecar results cached in PSRAM (see .nfocache) so selecting a game costs ZERO directory walks
// 5.9.41-lab14b: g_games is a deque, not a vector. At 15k games a vector needs ONE 1.2 MB block
// (80 B x 15k); after tens of thousands of small String allocations PSRAM is too fragmented for
// that even with 3 MB free, and the failed reserve/grow threw bad_alloc -> reboot loop (seen on the
// 26 GB card: 15 boots, each dying in buildGameList with psram=2997416). A deque grows in 480 B
// blocks. Same interface for everything here (index, size, push_back, sort, range-for).
static PathList g_files;static std::deque<GameEntry>g_games;   // lab15a: g_files is the compact PathList (operator[] returns the path by value)
// lab15b: the ONLY way to empty g_games - its text lives in g_gtext and goes with it
static void gamesClear(){ g_games.clear(); g_games.shrink_to_fit(); g_gtext.release(); }
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
static String gameCachePath(){return g_mode==MODE_ADF?"/ADF/.gamecache":g_mode==MODE_DSK?"/DSK/.gamecache":"/GENERIC/.gamecache";}

// lab14i: a too-big library stops the walk part-way through one folder (g_fw_cutdir). Covers later in that
// folder were never seen, so a "?" there is only a guess - it is saved as unknown and looked up when shown.
static bool coverCutDir(int fileIdx){ return g_fw_cutdir.length() && fileIdx>=0 && fileIdx<(int)g_files.size() && parentDir(g_files[fileIdx]).equalsIgnoreCase(g_fw_cutdir); }
static void writeGameCache(){ if(g_nocache)return;
  File f=SD_MMC.open(gameCachePath().c_str(),FILE_WRITE);if(!f)return;
  { BufWr w(f);                                   // lab15f: 16 KB blocks - same bytes as the print/println calls it replaces
  w.str("#V=590"); w.nl();                        // 5.9.0: bump invalidates pre-shard caches -> one clean rebuild into the bucketed .thumbs
  w.str("#FILES="); w.num((long)g_files.size()); w.nl();   // bind to the index this was built from
  if(g_covers_on){ w.str("#NC=1"); w.nl(); }      // lab14i: a "?" below is a real "no cover" (found by a scan with covers on) - trusted until the next RESCAN
  for(auto&g:g_games){
    w.str(g.name.c_str()); w.str("|"); w.num(g.first_file_idx); w.str("|");
    w.num(g.disk_count); w.str("|"); if(!(g.jpg_path=="?"&&coverCutDir(g.first_file_idx))) w.str(g.jpg_path.c_str()); w.str("|");
    for(int i=0;i<(int)g.disk_indices.size();i++){if(i>0)w.str(",");w.num(g.disk_indices[i]);}
    w.nl();
  } }
  f.close();
}

static bool readGameCache(){ if(g_nocache){gamesClear();return false;}
  gamesClear(); bcSet(BC_CACHEREAD,(uint32_t)g_files.size());
  File f=SD_MMC.open(gameCachePath().c_str(),FILE_READ);
  if(!f){return false;}
  long declaredFiles=-1; int cacheVer=-1; bool ncOk=false;   // lab14i: ncOk = this cache's "?" marks are real
  uint32_t _t0=millis();
  const size_t LM=16384; char* line=(char*)malloc(LM);      // lab15f: 16 KB block reads, fields cut in place - no String per line
  if(!line){ f.close(); return false; }
  { BufRd r(f); size_t n; bool cut;
  while(r.line(line,LM,n,cut)){ if(!n)continue;
    if(cut){ free(line); f.close(); gLog("[games] cache line too long - rebuilding\n"); gamesClear(); return false; }
    if(!strncmp(line,"#V=",3)){cacheVer=atoi(line+3);continue;}
    if(!strncmp(line,"#FILES=",7)){declaredFiles=atol(line+7);continue;}
    if(!strncmp(line,"#NC=",4)){ncOk=(atol(line+4)==1);continue;}   // lab14i
    char* p1=strchr(line,'|');if(!p1)continue;
    char* p2=strchr(p1+1,'|');if(!p2)continue;
    char* p3=strchr(p2+1,'|');if(!p3)continue;
    char* p4=strchr(p3+1,'|');if(!p4)continue;
    GameEntry e;
    e.name.set(line,(size_t)(p1-line));
    e.first_file_idx=(int)atol(p1+1);
    e.disk_count=(int)atol(p2+1);
    e.jpg_path.set(p3+1,(size_t)(p4-p3-1));
    if(e.jpg_path=="?"&&!ncOk)e.jpg_path="";  // lab14i: an old cache's "?" is not trusted; a #NC=1 cache's is (until the next RESCAN) - a warm boot no longer re-searches the card for every cover-less game
    { static std::vector<int> di; di.clear();       // lab15b: parse, then pack into the arena in one piece
      const char* q=p4+1; while(*q){ di.push_back(atoi(q)); while(*q&&*q!=',')q++; if(*q==',')q++; }
      e.disk_indices.set(di.data(),di.size()); }
    if(e.first_file_idx>=0&&e.first_file_idx<(int)g_files.size()) g_games.push_back(e);
    if(g_gtext.failed()){ free(line); f.close(); gLog("[games] cache read: out of PSRAM at %u games - rebuilding\n",(unsigned)g_games.size()); gamesClear(); return false; }   // lab15b: never keep a half-read list
  } }
  free(line);
  f.close();
  gLog("[games] cache: %u games read in %lums\n",(unsigned)g_games.size(),(unsigned long)(millis()-_t0));
  // 5.9.0: a cache without the current version marker predates the sharded .thumbs — force one rebuild
  if(cacheVer!=590){gamesClear();return false;}
  // If the game cache was built from a different-sized index, it's stale — force rebuild
  if(declaredFiles>=0&&declaredFiles!=(long)g_files.size()){gamesClear();return false;}
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

// 5.9.31-lab1: fold the scan-walk harvest into g_games. Idempotent (nfo_done guards each
// row). Returns true if any display name was adopted from an NFO title, so the caller can
// re-persist the game cache. Authoritative after a walk: a row with no harvest entry is
// still marked nfo_done, which is the whole point — proving "this game has no .nfo" used to
// cost two complete directory walks, and now costs nothing.
static bool assignSidecarsFromHarvest(){
  if(!g_sidecars_harvested) return false;
  bool nameChanged=false;
  for(auto&g:g_games){
    if(g.nfo_done) continue;
    const String&fp=g_files[g.first_file_idx];
    String raw=basenameNoExt(filenameOnly(fp));
    uint64_t k1=sideKey(filenameOnly(fp));            // exact file basename
    uint64_t k2=sideKeyRaw(getGameBaseName(fp));      // multi-disk game base — already ext-less, so no second strip
    // blurb + title
    for(int pass=0;pass<2;pass++){
      uint64_t k=pass?k2:k1;
      auto it=std::lower_bound(g_nfoharvest.begin(),g_nfoharvest.end(),k,
                               [](const NfoRec&r,uint64_t kk){return r.k<kk;});
      if(it!=g_nfoharvest.end()&&it->k==k){
        if(it->t.length()&&g.name==raw){ g.name=it->t; nameChanged=true; }   // lab15b: copied into the game arena,
        g.blurb=it->b; it->t=String(); it->b=String(); break;                // then the harvest's own blocks are freed at once
      }
      if(k2==k1) break;
    }
    g.has_manual = std::binary_search(g_manualset.begin(),g_manualset.end(),k1)
                || (k2!=k1 && std::binary_search(g_manualset.begin(),g_manualset.end(),k2));
    g.is_hd      = std::binary_search(g_hdset.begin(),g_hdset.end(),k1);
    g.nfo_done=true;
  }
  return nameChanged;
}
// lab15b: the NFO title a single-disk game will get from the harvest - the same record
// assignSidecarsFromHarvest picks (exact file name first, then the set's base name). Taking it
// while grouping stores the display name ONCE; adopting it afterwards left the file name's copy
// behind in the game arena (~55 B a titled game on a TOSEC card).
static const String* harvestTitleFor(const String& fp){
  if(!g_sidecars_harvested||g_nfoharvest.empty()) return nullptr;
  auto find=[](uint64_t k)->const NfoRec*{ auto it=std::lower_bound(g_nfoharvest.begin(),g_nfoharvest.end(),k,[](const NfoRec&r,uint64_t kk){return r.k<kk;});
                                            return (it!=g_nfoharvest.end()&&it->k==k)?&*it:nullptr; };
  uint64_t k1=sideKey(filenameOnly(fp)); const NfoRec* r=find(k1);
  if(!r){ uint64_t k2=sideKeyRaw(getGameBaseName(fp)); if(k2!=k1) r=find(k2); }   // the set's base name only when the file name has none
  return (r&&r->t.length())?&r->t:nullptr;
}
static void buildGameList(){
  gamesClear();
  gLog("[buildGameList] files=%d int=%u psram=%u\n",(int)g_files.size(),(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),(unsigned)ESP.getFreePsram());
  uint32_t _bg0=millis();
  bcSet(BC_GROUP,(uint32_t)g_files.size());   // lab14e breadcrumb
  // 5.9.41-lab14: group multi-disk sets by sorting a hashed (folder, game base) key instead
  // of comparing every disk against every later file. That inner loop was O(n^2) String
  // work: fine at 1,000 files, ~500 million compares at the 26 GB card's 33,000. Same
  // games, same order and same disk order as before (representative = lowest index,
  // members in index order, then std::sort by disk number exactly as the old code did).
  // lab14b: if PSRAM still runs out while grouping (bad_alloc), don't reboot-loop: drop the last
  // 15% of the images and try again, up to 6 times, and say so on screen.
  bool _trimmed=false;
  for(int attempt=0;;attempt++){
    const int n=(int)g_files.size();
    try{
    std::vector<int> dnum(n);
    std::vector<std::pair<uint64_t,int>> keyed;
    for(int i=0;i<n;i++){ dnum[i]=getDiskNumber(g_files[i]); if(dnum[i]>0) keyed.push_back({0,i}); }
    for(auto&k:keyed){ const String&f=g_files[k.second]; String key=parentDir(f)+"\x01"+getGameBaseName(f); k.first=coverHash(key); }
    std::sort(keyed.begin(),keyed.end());
    std::vector<int> grp(n,-1);                         // multi-disk member -> its representative's index
    for(size_t a=0;a<keyed.size();){
      size_t b=a; while(b<keyed.size()&&keyed[b].first==keyed[a].first) b++;
      if(b-a==1){ grp[keyed[a].second]=keyed[a].second; }
      else for(size_t x=a;x<b;x++){                      // equal hash: confirm with the real strings (collision-proof)
        int ix=keyed[x].second; if(grp[ix]>=0) continue; grp[ix]=ix;
        String dx=parentDir(g_files[ix]),bx=getGameBaseName(g_files[ix]);
        for(size_t y=x+1;y<b;y++){ int iy=keyed[y].second; if(grp[iy]<0&&parentDir(g_files[iy])==dx&&getGameBaseName(g_files[iy])==bx) grp[iy]=ix; }
      }
      a=b;
    }
    std::vector<std::pair<uint64_t,int>>().swap(keyed);
    // lab15b: disks of a numbered set are collected as (game, image) pairs and packed per game at the end
    std::vector<std::pair<int,int>> mem;
    for(int i=0;i<n;i++){
      if(dnum[i]>0&&grp[i]!=i){ int gi=-(grp[grp[i]]+2); g_games[gi].disk_count++; mem.push_back({gi,i}); continue; }
      GameEntry e;e.first_file_idx=i;e.disk_count=1;e.disk_indices.setOne(i);
      if(dnum[i]>0) e.name=getGameBaseName(g_files[i]);
      else { const String fp=g_files[i]; const String* t=harvestTitleFor(fp); if(t) e.name=*t; else e.name=basenameNoExt(filenameOnly(fp)); }   // lab15b: title stored once
      if(dnum[i]>0){ grp[i]=-((int)g_games.size()+2); mem.push_back({(int)g_games.size(),i}); }  // representative now remembers its game index
      // NO NFO/JPG lookups here — done lazily in drawCoverPanel
      g_games.push_back(e);
    }
    // Disks in disk-number order (D1,D2,D3); first_file_idx -> lowest disk (cover/NFO lookup)
    std::sort(mem.begin(),mem.end(),[&dnum](const std::pair<int,int>&a,const std::pair<int,int>&b){
      if(a.first!=b.first) return a.first<b.first; if(dnum[a.second]!=dnum[b.second]) return dnum[a.second]<dnum[b.second]; return a.second<b.second; });
    { std::vector<int> run;
      for(size_t a=0;a<mem.size();){ size_t b=a; run.clear(); while(b<mem.size()&&mem[b].first==mem[a].first){ run.push_back(mem[b].second); b++; }
        GameEntry& e=g_games[mem[a].first]; e.disk_indices.set(run.data(),run.size()); e.first_file_idx=run[0]; a=b; } }
    std::vector<std::pair<int,int>>().swap(mem);
    std::vector<int>().swap(dnum); std::vector<int>().swap(grp);
    if(g_gtext.failed()) throw std::bad_alloc();       // lab15b: a name/disk list didn't fit - same path as any other out-of-PSRAM
      break;
    }catch(...){
      gamesClear();
      size_t keep=g_files.size()*85/100;
      gLog("[buildGameList] OUT OF PSRAM grouping %d images (attempt %d) - keeping %u\n",n,attempt+1,(unsigned)keep);
      if(attempt>=5||keep==0){ g_files.clear(); break; }
      g_files.resize(keep); g_fw_lowmem=true; _trimmed=true;
    }
  }
  if(_trimmed){ writeIndexCache(g_files); showCardTooBig((int)g_files.size()); }   // the .index must match the list we kept
  bcSet(BC_SIDECARS,(uint32_t)g_games.size());
  assignSidecarsFromHarvest();   // 5.9.31-lab1: adopt NFO titles BEFORE the sort so the list (and the A-Z buckets) order by the display name
  bcSet(BC_SORT,(uint32_t)g_games.size());
  // lab14b: same order as lower-casing both names and comparing, without two String copies per compare
  std::sort(g_games.begin(),g_games.end(),[](const GameEntry&a,const GameEntry&b){return strcasecmp(a.name.c_str(),b.name.c_str())<0;});
  gLog("[buildGameList] done games=%d in %lums int=%u psram=%u\n",(int)g_games.size(),(unsigned long)(millis()-_bg0),(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),(unsigned)ESP.getFreePsram());
  gLog("[games] %u records x %u B + arena %u KB (%u KB text/disk lists, %u blocks, %u refused) = %u B a game\n",(unsigned)g_games.size(),(unsigned)sizeof(GameEntry),
       (unsigned)(g_gtext.bytes()/1024),(unsigned)(g_gtext.used()/1024),(unsigned)g_gtext.blocks(),(unsigned)g_gtext.failed(),
       (unsigned)(g_games.empty()?0:(g_games.size()*sizeof(GameEntry)+g_gtext.bytes())/g_games.size()));   // lab15b
  bcSet(BC_CACHEWRITE,(uint32_t)g_games.size());
  if(g_libpath.length()==0)writeGameCache();   // v5.6.0: only cache the top level; category sub-levels scan fresh each time
}
// ── Per-game stats: favourites + play counts, keyed by name, survives RESCAN ──
static String statsPath(bool forRead){return gtiStatePath(".gtistats",forRead);}   // lab14g: /GTI/.gtistats
static void applyStats(){
  File f=SD_MMC.open(statsPath(true).c_str(),FILE_READ);if(!f)return;
  while(f.available()){String line=f.readStringUntil('\n');line.trim();if(!line.length())continue;
    int p1=line.indexOf('|');if(p1<0)continue;int p2=line.indexOf('|',p1+1);if(p2<0)continue;
    int fv=line.substring(0,p1).toInt();int pl=line.substring(p1+1,p2).toInt();String nm=line.substring(p2+1);
    for(auto&g:g_games){if(g.name==nm){g.fav=(fv!=0);g.plays=(uint16_t)pl;break;}}}
  f.close();
}
static void saveStats(){
  std::vector<String>names;std::vector<int>favs,plays;
  File f=SD_MMC.open(statsPath(true).c_str(),FILE_READ);
  if(f){while(f.available()){String line=f.readStringUntil('\n');line.trim();if(!line.length())continue;
    int p1=line.indexOf('|');if(p1<0)continue;int p2=line.indexOf('|',p1+1);if(p2<0)continue;
    names.push_back(line.substring(p2+1));favs.push_back(line.substring(0,p1).toInt());plays.push_back(line.substring(p1+1,p2).toInt());}f.close();}
  for(auto&g:g_games){if(!g.fav&&g.plays==0)continue;int idx=-1;for(size_t i=0;i<names.size();i++)if(names[i]==g.name){idx=(int)i;break;}
    if(idx<0){names.push_back(g.name);favs.push_back(g.fav?1:0);plays.push_back(g.plays);}else{favs[idx]=g.fav?1:0;plays[idx]=g.plays;}}
  File w=SD_MMC.open(statsPath(false).c_str(),FILE_WRITE);if(!w)return;
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
  {"OMEGA", 0x18C5,0x18E6,0x10A5,0x3A2E,0x2968,0x52CE,0x94B4,0xC63A,0x3ED0,0xFC67,0xFE2B,0x46FC,0x1126,0x4557,0x2148,TFT_WHITE},
  // lab16a-P4 NEO: navy bg, dark-navy panels, gold (amber) = selected / call to action, cyan (blue) = INSERT, slate (accent) = buttons
  {"NEO",   0x0884,0x10C6,0x0863,0x3962,0x42D1,0x9517,0x7C14,0xEF9F,0x568F,0xF466,0xF586,0x7E9E,0x1225,0x3A4C,0x298A,TFT_WHITE},
};
static const int NUM_THEMES=8;static int g_theme_idx=0;   // +OMEGA (Dimmy); default stays 0=NAVY
static uint16_t COL_BG,COL_PANEL,COL_BAR,COL_SEL,COL_SEP,COL_DIM,COL_MID,COL_LIT;
static uint16_t COL_GREEN,COL_ORANGE,COL_AMBER,COL_BLUE,COL_NOW,COL_ACCENT,COL_CIRC,COL_CIRC_TEXT;

// lab4-P4: NEO is a switch (g_neo_on, CONFIG.TXT NEO=), not one of the colour themes. g_theme_idx is always a
// colour theme (0..NUM_THEMES-2) - the one NEO=OFF shows - and is kept while NEO is on. THEME=NEO / 7 (16a cards) -> 0.
static void applyTheme(int idx){
  const int neoIdx=NUM_THEMES-1;                  // NEO is the last entry of THEMES[]
  if(idx<0||idx>=neoIdx)idx=0;
  g_theme_idx=idx;const Theme&t=THEMES[g_neo_on?neoIdx:idx];
  COL_BG=t.bg;COL_PANEL=t.panel;COL_BAR=t.bar;COL_SEL=t.sel;COL_SEP=t.sep;
  COL_DIM=t.dim;COL_MID=t.mid;COL_LIT=t.lit;COL_GREEN=t.green;COL_ORANGE=t.orange;
  COL_AMBER=t.amber;COL_BLUE=t.blue;COL_NOW=t.now;COL_ACCENT=t.accent;COL_CIRC=t.circ;COL_CIRC_TEXT=t.circ_text;
  g_neo=g_neo_on; g_txt_key=COL_BG;   // lab16a-P4 (lab4-P4: from the NEO switch)
}

// ════════════════════════════════════════════════════════════════════════════
// lab16a-P4: NEO LOOK — background picture, glow, gold frames (THEME=NEO only)
// ════════════════════════════════════════════════════════════════════════════
// The background is drawn once per rotation into the framebuffer and kept in PSRAM (480x800x2 = 750 KB).
// Screens then copy the part they need back (fillBack) instead of filling with COL_BG. The copy is a
// row memcpy in the panel's own layout, so it costs about what a fill does.
static uint16_t* g_neo_bg=NULL; static int g_neo_bg_rot=-1;
struct NeoScope{ bool on; NeoScope():on(g_neo){ if(on)g_neo_depth++; } ~NeoScope(){ if(on)g_neo_depth--; } };
static inline uint16_t mix565(uint16_t a,uint16_t b,int wa){   // wa 0..256 = weight of a
  int wb=256-wa;
  int r=(((a>>11)&31)*wa+((b>>11)&31)*wb)>>8, g=(((a>>5)&63)*wa+((b>>5)&63)*wb)>>8, bl=((a&31)*wa+(b&31)*wb)>>8;
  return (uint16_t)((r<<11)|(g<<5)|bl);
}
static inline uint16_t neoGrad(int x,int y){   // navy (10,16,38) -> purple (36,22,64) across + down
  int t=(x*154)/(gW>1?gW:1)+(y*102)/(gH>1?gH:1); if(t>256)t=256;
  int r=10+(26*t>>8), g=16+(6*t>>8), b=38+(26*t>>8);
  return (uint16_t)(((r>>3)<<11)|((g>>2)<<5)|(b>>3));
}
static void neoBuildBg(){
  if(!framebuffer||!g_neo_bg)return;
  int cx0=g_clip_x0,cy0=g_clip_y0,cx1=g_clip_x1,cy1=g_clip_y1; g_clip_x0=0;g_clip_y0=0;g_clip_x1=gW;g_clip_y1=gH;
  for(int y=0;y<gH;y++)for(int x=0;x<gW;x+=8){ uint16_t c=neoGrad(x+4,y); gfx_fillRect(x,y,8,1,c); }
  uint32_t r=0x6E0;                                     // fixed seed: the same picture every boot
  auto rnd=[&](int n){ r=r*1103515245u+12345u; return (int)((r>>16)%(uint32_t)n); };
  const uint16_t tr=((90>>3)<<11)|((120>>2)<<5)|(200>>3);
  for(int k=0;k<40;k++){
    int x=rnd(gW), y=rnd(gH), segs=2+rnd(3);
    for(int sgi=0;sgi<segs;sgi++){
      if(rnd(2)){ int L=(rnd(2)?1:-1)*(20+rnd(110)); int xa=min(x,x+L),xb=max(x,x+L);
        for(int xx=xa;xx<=xb;xx++) gfx_drawPixel(xx,y,mix565(tr,neoGrad(xx,y),40)); x+=L; }
      else { int L=(rnd(2)?1:-1)*(15+rnd(70)); int ya=min(y,y+L),yb=max(y,y+L);
        for(int yy=ya;yy<=yb;yy++) gfx_drawPixel(x,yy,mix565(tr,neoGrad(x,yy),40)); y+=L; }
    }
    gfx_drawCircle(x,y,3,mix565(tr,neoGrad(x,y),64));
  }
  memcpy(g_neo_bg,framebuffer,(size_t)LCD_WIDTH*LCD_HEIGHT*2);
  g_neo_bg_rot=g_rot;
  g_clip_x0=cx0;g_clip_y0=cy0;g_clip_x1=cx1;g_clip_y1=cy1;
}
static void neoBgEnsure(){                              // called at the start of every NEO full-screen draw
  if(!g_neo)return;
  if(!g_neo_bg){ g_neo_bg=(uint16_t*)ps_malloc((size_t)LCD_WIDTH*LCD_HEIGHT*2); g_neo_bg_rot=-1;
    if(!g_neo_bg){ gLog("[neo] no PSRAM for the background - NEO falls back to a flat colour\n"); return; } }
  if(g_neo_bg_rot!=g_rot) neoBuildBg();
}
static void gfx_fillBg(int x,int y,int w,int h){        // like gfx_fillRect, but copies the background picture
  int vx0=max(g_clip_x0,x),vy0=max(g_clip_y0,y),vx1=min(g_clip_x1,x+w),vy1=min(g_clip_y1,y+h);
  if(vx0>=vx1||vy0>=vy1)return;
  int px0,px1,py0,py1;
  switch(g_rot){
    case 1: px0=vx0;py0=vy0;px1=vx1;py1=vy1;break;
    case 2: px0=LCD_WIDTH-vy1;py0=vx0;px1=LCD_WIDTH-vy0;py1=vx1;break;
    case 3: px0=LCD_WIDTH-vx1;py0=LCD_HEIGHT-vy1;px1=LCD_WIDTH-vx0;py1=LCD_HEIGHT-vy0;break;
    default: px0=vy0;py0=LCD_HEIGHT-vx1;px1=vy1;py1=LCD_HEIGHT-vx0;break;
  }
  for(int py=py0;py<py1;py++) memcpy(&framebuffer[(size_t)py*LCD_WIDTH+px0],&g_neo_bg[(size_t)py*LCD_WIDTH+px0],(size_t)(px1-px0)*2);
}
static inline bool neoBgOk(){ return g_neo&&g_neo_bg&&g_neo_bg_rot==g_rot; }
static void fillBack(int x,int y,int w,int h){ if(neoBgOk())gfx_fillBg(x,y,w,h); else gfx_fillRect(x,y,w,h,COL_BG); }
static void clearBack(){ neoBgEnsure(); if(neoBgOk())memcpy(framebuffer,g_neo_bg,(size_t)LCD_WIDTH*LCD_HEIGHT*2); else gfx_fillScreen(COL_BG); }
// Rounded outline WITH corners (gfx_drawRoundRect only draws the straight edges).
static void gfx_strokeRoundRect(int x,int y,int w,int h,int r,uint16_t c){
  if(w<=0||h<=0)return; if(r*2>w)r=w/2; if(r*2>h)r=h/2; if(r<0)r=0;
  gfx_hline(x+r,y,w-2*r,c);gfx_hline(x+r,y+h-1,w-2*r,c);gfx_vline(x,y+r,h-2*r,c);gfx_vline(x+w-1,y+r,h-2*r,c);
  if(r==0)return;
  int cxa=x+r,cya=y+r,cxb=x+w-1-r,cyb=y+h-1-r,px=0,py=r,d=3-2*r;
  while(px<=py){
    gfx_drawPixel(cxb+px,cya-py,c);gfx_drawPixel(cxb+py,cya-px,c);gfx_drawPixel(cxa-px,cya-py,c);gfx_drawPixel(cxa-py,cya-px,c);
    gfx_drawPixel(cxb+px,cyb+py,c);gfx_drawPixel(cxb+py,cyb+px,c);gfx_drawPixel(cxa-px,cyb+py,c);gfx_drawPixel(cxa-py,cyb+px,c);
    if(d<0)d+=4*px+6; else{d+=4*(px-py)+10;py--;} px++;
  }
}
static void neoFrame(int x,int y,int w,int h,int r,uint16_t c,int thick){ for(int t=0;t<thick;t++)gfx_strokeRoundRect(x-t,y-t,w+2*t,h+2*t,r+t,c); }
static void neoGlow(int x,int y,int w,int h,int r,uint16_t c,int rings){   // soft halo: fading rings outside the box
  for(int k=rings;k>=1;k--){ int f=rings+1-k; int a=(150*f*f)/((rings+1)*(rings+1));
    gfx_strokeRoundRect(x-k,y-k,w+2*k,h+2*k,r+k,mix565(c,COL_BG,a)); }
}
static void neoGear(int cx,int cy,uint16_t c,uint16_t hole){
  for(int k=0;k<8;k++){ float a=k*0.785398f; int tx=cx+(int)lroundf(cosf(a)*9.0f), ty=cy+(int)lroundf(sinf(a)*9.0f); gfx_fillRect(tx-2,ty-2,5,5,c); }
  gfx_fillCircle(cx,cy,7,c); gfx_fillCircle(cx,cy,3,hole);
}
// One pill button: rounded capsule, text centred (size 2, or 1 if it does not fit)
static void neoPill(int x,int y,int w,int h,uint16_t bc,const String&lbl,bool outline){
  gfx_fillRoundRect(x,y,w,h,h/2,bc); if(outline)gfx_strokeRoundRect(x,y,w,h,h/2,COL_AMBER);
  uint16_t ic=inkFor(bc); int sz=2; gfx_setTextSize(sz); int tw=gfx_textWidth(lbl); if(tw>w-h/2-4){sz=1;gfx_setTextSize(1);tw=gfx_textWidth(lbl);}
  gfx_setTextColor(ic,bc); gfx_setCursor(x+(w-tw)/2,y+(h-8*sz)/2); gfx_print(lbl);
}

static void saveConfigKey(const String&key,const String&val){
  uint32_t _t0=millis();   // lab15a2
  String lines="";bool written=false;File fr=SD_MMC.open("/CONFIG.TXT",FILE_READ);
  if(fr){while(fr.available()){String l=fr.readStringUntil('\n');l.trim();if(l.startsWith(key+"=")){lines+=key+"="+val+"\n";written=true;}else lines+=l+"\n";}fr.close();}
  if(!written)lines+=key+"="+val+"\n";File fw=SD_MMC.open("/CONFIG.TXT",FILE_WRITE);if(fw){fw.print(lines);fw.close();}
  { uint32_t dt=millis()-_t0; if(dt>300) gLog("[slow] saveConfigKey %s: %lums, CONFIG.TXT %u bytes\n",key.c_str(),(unsigned long)dt,(unsigned)lines.length()); }
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
// Webby security: per-dongle LOCK flag, stored as a "DONGLE_<hex>.LOCK=1" line (mirrors setDongleName).
static bool getDongleLock(const String&mac){String key=macKey(mac)+".LOCK";File f=SD_MMC.open("/CONFIG.TXT",FILE_READ);if(!f)return false;bool r=false;
  while(f.available()){String l=f.readStringUntil('\n');l.trim();if(l.startsWith("#"))continue;if(l.startsWith(key+"=")){r=(l.substring(key.length()+1).toInt()!=0);break;}}f.close();return r;}
static void setDongleLock(const String&mac,bool on){String key=macKey(mac)+".LOCK";
  String lines="";bool written=false;File fr=SD_MMC.open("/CONFIG.TXT",FILE_READ);
  if(fr){while(fr.available()){String l=fr.readStringUntil('\n');l.trim();
    if(l.startsWith(key+"=")){lines+=key+"="+(on?"1":"0")+"\n";written=true;}else lines+=l+"\n";}fr.close();}
  if(!written)lines+=key+"="+(on?"1":"0")+"\n";
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
static bool savBadgeFor(const String&adfPath);   // lab14i: the badge (from the save list)
static void drawListAndCover();
static bool doLoadSelected(const String&p);
static void doUnload();

static bool g_espnow_started=false;
static bool g_c6_ready=false;   // P4 5.9.12: true only when the C6 is confirmed up-to-date and STA is up; gates arming the radio
static void ensureEspNow(){if(!g_espnow_started){espnowBegin();g_espnow_started=true;}}

// ============================================================================
// LANGUAGE / LOCALISATION (v5.5.0) — LANG= in CONFIG.TXT (SD-editable, persisted).
// The UI vocabulary is tiny, so this is a flat string table. ASCII-folded: the
// 6x8 font has no accent glyphs yet, and uppercase labels conventionally drop
// accents anyway. Add a language = add a column. CJK (zh/ja) is a separate job
// (needs a glyph font + multi-byte text), tracked in the roadmap.
// >>> Translations are a DRAFT — Mez to verify IT, Jan to verify DE, review FR/ES. <<<
// ============================================================================
enum { LANG_EN=0, LANG_FR, LANG_IT, LANG_ES, LANG_DE, LANG_NL, LANG_PL, LANG_CS, LANG_N };   // lab15q-P4 (from the 3.5" 15o/15q): + Polish, Czech
static int g_lang=0;
static const char* const LANG_NAMES[LANG_N]={"EN","FR","IT","ES","DE","NL","PL","CS"};
enum { L_PREV, L_NEXT, L_THEME, L_REEL, L_INFO, L_LIST, L_ROLL, L_INSERT, L_EJECT, L_SEARCH, L_SETTINGS, L_NOW_PLAYING, L_NO_GAMES, L_NO_FAVS, L_ALL, L_FAV, L_MOST, L_BUILDING, L_ONEOFF, L_LOADING, L_LOADING_DIAG, L_RESCAN_SD, L_SD_ACCESS, L_FW_UPDATE, L_SOFT_RESET, L_RESETTING, L_STANDALONE, L_WIRELESS, L_USER_DISKS, L_RENAME, L_BACK, L_CANCEL, L_ACTIVE, L_MANUAL, L_PAIRED, L_NOT_PAIRED, L_NAME_DONGLE, L_DONGLE_LINKED, L_CREATE_DISK, L_NONE_YET, L_PREFMT, L_CHECK_DONGLE, L_NO_DONGLES, L_NO_WIRELESS_DEV, L_USE_CABLE, L_IN_RANGE, L_AVAIL_HD, L_HD_NO_WIRELESS, L_MAX_DD, L_TOO_BIG, L_SIZE_ERR, L_FAILED, L_SD_MOUNT_FAIL, L_LOAD_DIAG, L_EJECT_DIAG, L_GAMES_TAP, L_CFG_MODE, L_CFG_FONT, L_CFG_LANG, L_CFG_ROTATE, L_CFG_COMPACT, L_CFG_LIBRARY, L_CFG_CATEG, L_CFG_BUTTONS, L_CFG_SAVER, L_CFG_FAVSAVER, L_CFG_HIVEMIND, L_ON, L_OFF, L_PORTRAIT, L_LANDSCAPE, L_FONT_SMALL, L_FONT_NORMAL, L_FONT_LARGE, L_PILL, L_FLAT, L_SLIDES, L_BOUNCE, L_MATRIX, L_SWITCH_DONGLE, L_SCAN_DONGLES, L_STR_N };
static const char* const LSTR[L_STR_N][LANG_N]={
  /*L_PREV          */ {"PREV","PREC","PREC","ANT","VORH","VORIG","POPRZ","PRED"},
  /*L_NEXT          */ {"NEXT","SUIV","SUCC","SIG","WEIT","VOLG","DALEJ","DALSI"},
  /*L_THEME         */ {"THEME","THEME","TEMA","TEMA","THEMA","THEMA","MOTYW","MOTIV"},
  /*L_REEL          */ {"REEL","REEL","REEL","REEL","REEL","REEL","ROLKA","REEL"},
  /*L_INFO          */ {"CONFIG","CONFIG","CONFIG","CONFIG","CONFIG","CONFIG","KONFIG","CONFIG"},
  /*L_LIST          */ {"LIST","LISTE","LISTA","LISTA","LISTE","LIJST","LISTA","SEZNAM"},
  /*L_ROLL          */ {"ROLL","DES","DADI","DADO","WUERF","DOBBEL","LOSUJ","KOSTKA"},
  /*L_INSERT        */ {"INSERT","INSERER","INSERISCI","INSERTAR","EINLEGEN","LADEN","WLOZ","VLOZIT"},
  /*L_EJECT         */ {"EJECT","EJECTER","ESPELLI","EXPULSAR","AUSWERF","UITWERP","WYSUN","VYSUNOUT"},
  /*L_SEARCH        */ {"SEARCH","RECHERCHE","CERCA","BUSCAR","SUCHE","ZOEKEN","SZUKAJ","HLEDAT"},
  /*L_SETTINGS      */ {"SETTINGS","REGLAGES","IMPOSTAZIONI","AJUSTES","OPTIONEN","INSTELLINGEN","USTAWIENIA","NASTAVENI"},
  /*L_NOW_PLAYING   */ {"NOW PLAYING","EN LECTURE","IN USO","EN USO","LAEUFT","SPEELT NU","TERAZ GRA","PRAVE HRAJE"},
  /*L_NO_GAMES      */ {"NO GAMES","AUCUN JEU","NESSUN GIOCO","SIN JUEGOS","KEINE SPIELE","GEEN SPELLEN","BRAK GIER","ZADNE HRY"},
  /*L_NO_FAVS       */ {"NO FAVOURITES YET","AUCUN FAVORI","NESSUN PREFERITO","SIN FAVORITOS","KEINE FAVORITEN","NOG GEEN FAVORIETEN","BRAK ULUBIONYCH","ZATIM ZADNE OBLIBENE"},
  /*L_ALL           */ {"ALL","TOUT","TUTTI","TODO","ALLE","ALLES","WSZYSTKIE","VSE"},
  /*L_FAV           */ {"FAV","FAV","PREF","FAV","FAV","FAV","ULUBIONE","OBLIB"},
  /*L_MOST          */ {"MOST","TOP","TOP","TOP","TOP","TOP","TOP","TOP"},
  /*L_BUILDING      */ {"BUILDING COVER CACHE","CREATION DU CACHE","CREAZIONE CACHE","CREANDO CACHE","CACHE ERSTELLEN","COVER-CACHE BOUWEN","GENEROWANIE OKLADEK","VYTVARIM CACHE OBALU"},
  /*L_ONEOFF        */ {"one-off: reel thumbnails (first launch / rescan)","unique: vignettes du reel (1er lancement)","una tantum: miniature reel (primo avvio)","una vez: miniaturas del reel (1er inicio)","einmalig: reel-vorschau (erststart)","eenmalig: reel-miniaturen (eerste start)","jednorazowo: miniatury rolek (1. uruch.)","jednorazove: nahledy reel (1. spusteni)"},
  /*L_LOADING       */ {"Loading...","Chargement...","Caricamento...","Cargando...","Laedt...","Laden...","Ladowanie...","Nacitani..."},
  /*L_LOADING_DIAG  */ {"Loading diag...","Chargement diag...","Caricamento diag...","Cargando diag...","Diag laedt...","Diag laden...","Ladowanie diag...","Nacitani diag..."},
  /*L_RESCAN_SD     */ {"RESCAN SD","RELIRE SD","RILEGGI SD","RELEER SD","SD NEU","SD OPNIEUW","SKANUJ SD","PROHLEDAT SD"},
  /*L_SD_ACCESS     */ {"SD ACCESS","ACCES SD","ACCESSO SD","ACCESO SD","SD ZUGRIFF","SD-TOEGANG","DOSTEP SD","PRISTUP SD"},
  /*L_FW_UPDATE     */ {"FW UPDATE","MAJ FW","AGG. FW","ACT. FW","FW UPDATE","FW UPDATE","AKTUAL. FW","AKTUAL. FW"},
  /*L_SOFT_RESET    */ {"SOFT RESET","REINIT","RIAVVIA","REINICIAR","NEUSTART","HERSTART","RESTART","RESTART"},
  /*L_RESETTING     */ {"RESET...","REINIT...","RIAVVIO...","REINICIO...","NEUSTART...","HERSTART...","RESET...","RESTART..."},
  /*L_STANDALONE    */ {"STANDALONE","AUTONOME","AUTONOMO","AUTONOMO","STANDALONE","STANDALONE","SAMODZIELNY","SAMOSTATNE"},
  /*L_WIRELESS      */ {"WIRELESS","SANS FIL","WIRELESS","INALAMB.","FUNK","DRAADLOOS","BEZPRZEW.","BEZDRATOVE"},
  /*L_USER_DISKS    */ {"USER DISKS","DISQUES","DISCHI","DISCOS","DISKETTEN","EIGEN DISKS","MOJE DYSKI","MOJE DISKY"},
  /*L_RENAME        */ {"RENAME","RENOMMER","RINOMINA","RENOMBRAR","UMBENENN","HERNOEM","ZMIEN NAZWE","PREJMENOVAT"},
  /*L_BACK          */ {"BACK","RETOUR","INDIETRO","ATRAS","ZURUECK","TERUG","WSTECZ","ZPET"},
  /*L_CANCEL        */ {"CANCEL","ANNULER","ANNULLA","CANCELAR","ABBRECH","ANNULEER","ANULUJ","ZRUSIT"},
  /*L_ACTIVE        */ {"ACTIVE","ACTIF","ATTIVO","ACTIVO","AKTIV","ACTIEF","AKTYWNY","AKTIVNI"},
  /*L_MANUAL        */ {"MANUAL","MANUEL","MANUALE","MANUAL","MANUELL","HANDMATIG","INSTRUKCJA","NAVOD"},
  /*L_PAIRED        */ {"PAIRED","APPAIRE","ABBINATO","VINCULADO","GEKOPPELT","GEKOPPELD","SPAROWANY","SPAROVANO"},
  /*L_NOT_PAIRED    */ {"Not paired","Non appaire","Non abbinato","No vinculado","Nicht gekoppelt","Niet gekoppeld","Niesparowany","Nesparovano"},
  /*L_NAME_DONGLE   */ {"NAME DONGLE","NOMMER DONGLE","NOMINA DONGLE","NOMBRAR DONGLE","DONGLE NAME","DONGLE NAAM","NAZWIJ DONGLE","POJMENOVAT DONGLE"},
  /*L_DONGLE_LINKED */ {"** DONGLE LINKED **","** DONGLE CONNECTE **","** DONGLE COLLEGATO **","** DONGLE CONECTADO **","** DONGLE VERBUNDEN **","** DONGLE VERBONDEN **","** DONGLE POLACZONY **","** DONGLE PRIPOJEN **"},
  /*L_CREATE_DISK   */ {"+  CREATE NEW DISK","+  NOUVEAU DISQUE","+  NUOVO DISCO","+  NUEVO DISCO","+  NEUE DISKETTE","+  NIEUWE DISK","+  NOWY DYSK","+  NOVY DISK"},
  /*L_NONE_YET      */ {"(none yet - tap CREATE NEW DISK)","(aucun - touchez NOUVEAU DISQUE)","(nessuno - tocca NUOVO DISCO)","(ninguno - toca NUEVO DISCO)","(keine - NEUE DISKETTE tippen)","(nog geen - tik NIEUWE DISK)","(brak - dotknij NOWY DYSK)","(zatim zadne - klepnete na NOVY DISK)"},
  /*L_PREFMT        */ {"pre-formatted save disks - tap to insert","disques de sauvegarde pre-formates - toucher","dischi di salvataggio pre-formattati - tocca","discos de guardado pre-formateados - toca","vorformatierte speicherdisks - tippen","voorgeformatteerde save-disks - tik om te laden","sformatowane dyski zapisu - dotknij, by wlozyc","predformatovane disky pro ulozeni - klepnete"},
  /*L_CHECK_DONGLE  */ {"Check dongle is powered","Verifiez l'alim. du dongle","Verifica alim. dongle","Comprueba alim. del dongle","Dongle-Strom pruefen","Check voeding van de dongle","Sprawdz zasilanie dongla","Zkontrolujte napajeni donglu"},
  /*L_NO_DONGLES    */ {"No dongles found","Aucun dongle trouve","Nessun dongle trovato","No se hallaron dongles","Keine Dongles gefunden","Geen dongles gevonden","Nie znaleziono dongli","Nenalezeny zadne dongly"},
  /*L_NO_WIRELESS_DEV*/ {"No wireless device","Aucun periph. sans fil","Nessun disp. wireless","Sin disp. inalambrico","Kein Funkgeraet","Geen draadloos apparaat","Brak urzadzen bezprzew.","Zadne bezdratove zarizeni"},
  /*L_USE_CABLE     */ {"Use the cable / standalone.","Utilisez le cable / autonome.","Usa il cavo / autonomo.","Usa el cable / autonomo.","Kabel / Standalone nutzen.","Gebruik de kabel / standalone.","Uzyj kabla / tryb samodzielny.","Pouzijte kabel / samostatne."},
  /*L_IN_RANGE      */ {"and in WIRELESS range.","et a portee sans fil.","e nel raggio wireless.","y en rango inalambrico.","und in Funkreichweite.","en binnen draadloos bereik.","i w zasiegu bezprzewodowym.","a v bezdratovem dosahu."},
  /*L_AVAIL_HD      */ {"available for HD.","disponible pour HD.","disponibile per HD.","disponible para HD.","verfuegbar fuer HD.","beschikbaar voor HD.","dostepny dla HD.","dostupne pro HD."},
  /*L_HD_NO_WIRELESS*/ {"HD - NO WIRELESS","HD - SANS FIL NON","HD - NO WIRELESS","HD - SIN INALAMB.","HD - KEIN FUNK","HD - NIET DRAADLOOS","HD - TYLKO KABEL","HD - JEN KABEL"},
  /*L_MAX_DD        */ {"Max is DD floppy","Max = disquette DD","Max = floppy DD","Max = disquete DD","Max = DD-Diskette","Max = DD diskette","Max to dyskietka DD","Max je disketa DD"},
  /*L_TOO_BIG       */ {"TOO BIG","TROP GROS","TROPPO GRANDE","MUY GRANDE","ZU GROSS","TE GROOT","ZA DUZY","PRILIS VELKY"},
  /*L_SIZE_ERR      */ {"SIZE ERR","ERR TAILLE","ERR DIMENS.","ERR TAMANO","GROESSENFEHL","MAATFOUT","BLAD ROZM.","CHYBA VELIK."},
  /*L_FAILED        */ {"FAILED","ECHEC","FALLITO","FALLIDO","FEHLER","MISLUKT","BLAD","CHYBA"},
  /*L_SD_MOUNT_FAIL */ {"SD MOUNT FAILED","ECHEC MONTAGE SD","MONTAGGIO SD FALLITO","FALLO MONTAJE SD","SD-MOUNT FEHLER","SD MOUNT MISLUKT","BLAD MONTOWANIA SD","CHYBA PRIPOJENI SD"},
  /*L_LOAD_DIAG     */ {"LOAD DIAG","CHARGER DIAG","CARICA DIAG","CARGAR DIAG","DIAG LADEN","DIAG LADEN","LADUJ DIAG","NACIST DIAG"},
  /*L_EJECT_DIAG    */ {"EJECT DIAG","EJECTER DIAG","ESPELLI DIAG","EXPULSAR DIAG","DIAG AUSWERF","DIAG UITWERP","WYSUN DIAG","VYSUNOUT DIAG"},
  /*L_GAMES_TAP     */ {" games - tap INSERT"," jeux - toucher INSERER"," giochi - tocca INSERISCI"," juegos - toca INSERTAR"," Spiele - INSERT tippen"," spellen - tik LADEN"," gier - dotknij WLOZ"," her - klepnete na VLOZIT"},
  /*L_CFG_MODE     */ {"MODE","MODE","MODE","MODO","MODUS","MODUS","TRYB","REZIM"},
  /*L_CFG_FONT     */ {"FONT","POLICE","FONT","FUENTE","SCHRIFT","LETTER","CZCIONKA","PISMO"},
  /*L_CFG_LANG     */ {"LANG","LANGUE","LANG","IDIOMA","SPRACHE","TAAL","JEZYK","JAZYK"},
  /*L_CFG_ROTATE   */ {"ROTATE","ROTATION","ROTATE","ROTAR","DREHEN","DRAAIEN","OBROT","OTOCIT"},
  /*L_CFG_COMPACT  */ {"COMPACT","COMPACT","COMPACT","COMPACTO","KOMPAKT","COMPACT","KOMPAKT","KOMPAKT"},
  /*L_CFG_LIBRARY  */ {"LIBRARY","BIBLIO.","LIBRARY","BIBLIOTECA","BIBLIOTHEK","BIBLIOTHEEK","BIBLIOTEKA","KNIHOVNA"},
  /*L_CFG_CATEG    */ {"CATEGORIES","CATEGORIES","CATEGORIES","CATEGORIAS","KATEGORIEN","CATEGORIEEN","KATEGORIE","KATEGORIE"},
  /*L_CFG_BUTTONS  */ {"BUTTONS","BOUTONS","BUTTONS","BOTONES","TASTEN","KNOPPEN","PRZYCISKI","TLACITKA"},
  /*L_CFG_SAVER    */ {"SAVER","VEILLE","SAVER","SALVAPANT.","SCHONER","SAVER","WYGASZACZ","SPORIC"},
  /*L_CFG_FAVSAVER */ {"FAV SAVER","FAV VEILLE","FAV SAVER","FAV SALVAP.","FAV SCHONER","FAV SAVER","WYGASZ. ULUB.","SPORIC OBLIB"},
  /*L_CFG_HIVEMIND */ {"HIVEMIND","HIVEMIND","HIVEMIND","HIVEMIND","HIVEMIND","HIVEMIND","HIVEMIND","HIVEMIND"},
  /*L_ON           */ {"ON","ON","ON","ON","EIN","AAN","WL","ZAP"},
  /*L_OFF          */ {"OFF","OFF","OFF","OFF","AUS","UIT","WYL","VYP"},
  /*L_PORTRAIT     */ {"PORTRAIT","PORTRAIT","PORTRAIT","VERTICAL","HOCHFORMAT","STAAND","PIONOWO","NA VYSKU"},
  /*L_LANDSCAPE    */ {"LANDSCAPE","PAYSAGE","LANDSCAPE","HORIZONTAL","QUERFORMAT","LIGGEND","POZIOMO","NA SIRKU"},
  /*L_FONT_SMALL   */ {"SMALL","PETIT","SMALL","PEQUENO","KLEIN","KLEIN","MALA","MALE"},
  /*L_FONT_NORMAL  */ {"NORMAL","NORMAL","NORMAL","NORMAL","NORMAL","NORMAAL","NORMALNA","NORMALNI"},
  /*L_FONT_LARGE   */ {"LARGE","GRAND","LARGE","GRANDE","GROSS","GROOT","DUZA","VELKE"},
  /*L_PILL         */ {"PILL","ARRONDI","PILL","REDOND.","RUND","ROND","OKRAGLE","ZAOBLENE"},
  /*L_FLAT         */ {"FLAT","PLAT","FLAT","PLANO","FLACH","VLAK","PLASKIE","PLOCHE"},
  /*L_SLIDES       */ {"SLIDES","DIAPO.","SLIDES","DIAPOS.","DIASHOW","DIA'S","SLAJDY","SNIMKY"},
  /*L_BOUNCE       */ {"BOUNCE","REBOND","BOUNCE","REBOTE","HUEPFEN","STUITER","ODBIJANIE","ODRAZ"},
  /*L_MATRIX       */ {"MATRIX","MATRIX","MATRIX","MATRIX","MATRIX","MATRIX","MATRIX","MATRIX"},
  /*L_SWITCH_DONGLE*/ {"SWITCH DONGLE","CHANGER DONGLE","SWITCH DONGLE","CAMBIAR DONGLE","DONGLE WECHSELN","WISSEL DONGLE","ZMIEN DONGLE","ZMENIT DONGLE"},
  /*L_SCAN_DONGLES */ {"SCAN DONGLES","SCAN DONGLES","SCAN DONGLES","BUSCAR DONGLES","DONGLES SUCHEN","ZOEK DONGLES","SKANUJ DONGLI","HLEDAT DONGLY"},
};
static inline const char* T(int id){ return LSTR[id][g_lang]; }

static void generateDefaultConfig(){
  if(SD_MMC.exists("/CONFIG.TXT"))return;  // never overwrite an existing config
  File f=SD_MMC.open("/CONFIG.TXT",FILE_WRITE);if(!f)return;
  static const char DEFAULT_CONFIG[] =
R"CFG(# ============================================================
#  Gotek Touchscreen Interface  -  OMEGAWARE
#  CONFIG.TXT   -   edit the values below, then reboot.
#
#  Lines starting with # are notes and are ignored.
#  Everything is grouped into sections:  VISUALS / BOOT /
#  LIBRARY / LOADING & SAVES / SCREENSAVER / NETWORKING /
#  WEBDAV / SYSTEM.   Change a KEY=VALUE line to set an option.
# ============================================================


# ============================================================
#  VISUALS      theme, fonts, layout, on-screen look
# ============================================================

# NEO: the NEO look (gold frames, navy-to-purple background, list left + game panel right). OFF = the classic layout + THEME
NEO=ON
# Theme: colours for the classic layout (NEO=OFF): 0=NAVY 1=EMBER 2=MATRIX 3=PAPER 4=SYNTH 5=GOLD 6=OMEGA
THEME=0

# Font size: SMALL, NORMAL, LARGE
FONT=NORMAL

# Screen rotation in degrees: 0 or 180 = landscape, 90 or 270 = portrait.
# Easiest to set with the ROTATE button on the INFO screen (each tap = +90).
ROTATE=0

# COMPACT: OFF = cover art + list, ON = maximise the game list (cover collapses to a strip)
COMPACT=OFF

# BTNSTYLE: reel button style. PILL=rounded coloured buttons (default), FLAT=flat bar.
BTNSTYLE=PILL

# REELBORDER: frame drawn around each cover in the reel. ON=framed (default),
#   OFF=clean/frameless look (the game you have loaded is still marked green).
REELBORDER=ON

# COVERMIN: hide covers whose short side is under N px (0 = show all) - keeps the reel + panel clean.
COVERMIN=140

# REELFILTER: ON = the reel (cover carousel) shows only games whose cover passes COVERMIN;
#             the A-Z list still shows every game. OFF = reel shows all games.
REELFILTER=OFF


# ============================================================
#  BOOT         startup view + cracktro splash
# ============================================================

# CAROUSEL: default boot view. OFF=game list, ON=cover reel, LAST=restore last view.
CAROUSEL=OFF

# Boot cracktro style: 0=random each boot, or pick one:
#   1=COPPER CLASSIC  2=STARFIELD  3=RAINBOW RASTER
#   4=PLASMA  5=BOING BALL  6=SYNTHWAVE  7=OMEGAWARE
CRACKTRO=0

# Loop cracktro splash: 1=loop until tapped, 0=auto-dismiss after 6s
LOOP=0

# LASTUSED: ON = on power-up, jump the selection back to the game you last loaded.
#   OFF (default) = always start at the top of the list.
LASTUSED=OFF


# ============================================================
#  LIBRARY      how the game collection is organised
# ============================================================

# CATEGORIES: OFF = one flat library (classic). ON = browse by category -
#   any top-level folder that holds NO disk images (only subfolders) becomes a category.
CATEGORIES=OFF

# NESTING: OFF = categories are one level deep. ON = allow sub-categories (folders within category folders).
NESTING=OFF

# LIBLIMIT: ON (default) = if the card holds more games than this GTi can list, stop at boot with a
#   LIBRARY TOO BIG screen and a GTI_CAPACITY.TXT report, so nothing is left out silently.
#   OFF = load as many games as fit and leave the rest out.
LIBLIMIT=ON


# ============================================================
#  LOADING & SAVES     load/eject behaviour + write-back
# ============================================================

# Load/eject behaviour (all OFF = safest: select, then press the button)
# TAPLOAD: ON = tapping the already-highlighted game row loads it (old double-tap)
TAPLOAD=OFF
# HOTSWAP: ON = tapping another disk while loaded swaps to it instantly
HOTSWAP=OFF
# FORCESWAP: ON = swap disk contents without the USB eject/re-attach cycle
FORCESWAP=OFF

# LONGNAME: OFF (default) = the Gotek sees the loaded disk as DISK.ADF / DISK.DSK (GENERIC images keep a
#   short 8.3 version of their own name, e.g. PROPHECY.HFE). ON = the Gotek sees the image's full file name,
#   e.g. "Prophecy I-The Viking Child-1.hfe" - shown on a FlashFloppy OLED/LCD display.
LONGNAME=OFF

# SAVES: save-game persistence when the Amiga writes to the disk.
#   OFF       = writes live only until eject/power-off (classic behaviour)
#   COPY      = writes are kept as GameName.sav.adf beside the master (recommended)
#   OVERWRITE = writes are patched straight into the master .adf
SAVES=COPY

# SDSPEED: SD card clock speed.
#   20 = safe default, works with every card.
#   40 = ~1.7x faster reads if your card can hold it (auto-falls back to 20 if it can't mount).
SDSPEED=20

# BIGDISK: OFF (default) = 1.76 MB set aside for save-game write-back over the cable
#   and for disks sent to a wireless dongle (an Amiga HD disk; the SuperMini's limit).
#   ON = 2.9 MB, for large Atari ST .HFE images (saving to them, or sending them wirelessly).
#   Loading any size over the cable works either way. ON costs ~1.1 MB of memory, so fewer
#   games fit on a big card. Takes effect on the next boot. (Replaces DISKMAXKB.)
BIGDISK=OFF


# ============================================================
#  SCREENSAVER      idle slideshow
# ============================================================

# SCREENSAVER: idle slideshow. ON = show it after a few minutes idle, OFF = never.
SCREENSAVER=ON
# SSMODE: SLIDES = full-screen photo slideshow (default), BOUNCE = bouncing logo, MATRIX = code rain.
SSMODE=SLIDES
# SSFX: transition between slides - SHUFFLE (random), FADE, DISSOLVE, SLIDE, or CUT.
SSFX=SHUFFLE
# SSTIME: seconds each slide is shown (2-120).
SSTIME=6
# SSFAV: ON = also slideshow the cover art of your favourited games;
#        OFF = only images you drop in the /screensaver/ folder.
SSFAV=ON


# ============================================================
#  NETWORKING      wireless mode, dongles, home WiFi
# ============================================================

# Transfer mode: STANDALONE (USB to Gotek) or WIRELESS (ESP-NOW / WiFi to a dongle)
MODE=STANDALONE

# CAP: max wireless dongles the scan will list (default 32, up to 64)
CAP=32

# HIVEMIND: wireless FLING fan-out. ON=send to all paired MuCa dongles (classic), OFF=only the selected dongle.
HIVEMIND=ON

# GTINAME: this screen's name, shown on another screen that shares the same dongle ("in use by DESK").
#   Up to 24 characters. Empty = GTi-XXXX (from this screen's radio address).
GTINAME=

# LINK: dongle transport. ESPNOW = the dongle's own AP + ESP-NOW (default).
#       HOMEWIFI = route the FLING via your home router to a Webby dongle's gotek.local.
LINK=ESPNOW
# HOME_SSID / HOME_PASS: your home WiFi (only used when LINK=HOMEWIFI).
HOME_SSID=
HOME_PASS=
# DONGLE_HOME_IP: auto-filled cache of the dongle's home IP (mDNS gotek.local is primary).
DONGLE_HOME_IP=

# WEBUI: serve the built-in web page over home WiFi (needs LINK=HOMEWIFI + HOME_SSID/PASS).
#        Off by default - uncomment the next line to switch it on.
# WEBUI=ON

# Wireless dongle MAC (auto-filled when you pair via the INFO screen)
# XIAO_MAC=


# ============================================================
#  WEBDAV       optional - point GTi at a WebDAV server
# ============================================================
# Off until you set DAV=ON and a DAV_HOST. HTTPS here is encrypted but NOT
# certificate-authenticated, and DAV_PASS is stored in plain text on this card.

# DAV: ON = enable the WebDAV client (DAV_ENABLED is accepted as the same switch).
DAV=OFF
# DAV_HOST: WebDAV server hostname or IP.
DAV_HOST=
# DAV_PORT: server port (default 443).
DAV_PORT=443
# DAV_HTTPS: ON = TLS (default), OFF = plain HTTP.
DAV_HTTPS=ON
# DAV_USER / DAV_PASS: Basic-Auth credentials (blank = none).
DAV_USER=
DAV_PASS=
# DAV_PATH: base path on the server (default /).
DAV_PATH=/


# ============================================================
#  SYSTEM       language + diagnostics
# ============================================================

# Language: EN, FR, IT, ES, DE, NL, PL, CS  (pull the SD and edit this line if you get stuck)
LANG=EN

# LOG: the GTI/gti.log diagnostic log is ON by default.
#      Uncomment the next line to turn it off.
# LOG=OFF

# DIAG-DISP: live diagnostic overlay box (FPS / free SRAM+PSRAM / uptime / CPU temp). ON or OFF.
DIAGDISP=OFF

# NOCACHE: ON = ignore every on-SD cache (.index .gamecache .nfocache micro .tnl).
#   Nothing is deleted; caches are just not read or written. Everything is rebuilt every
#   boot. Diagnostic/benchmark use only - leave OFF for normal running.
NOCACHE=OFF

# COVERS: OFF = no cover art at all (nothing decoded, letter placeholders instead).
#   Diagnostic: isolates list and .nfo cost from cover-decode time.
COVERS=ON

# STRIPROWS: how many screen rows go to the panel per SPI burst (1-40, default 10).
#   The push is 48 bursts at 10 rows, 12 at 40 - and each burst costs FLUSHUS below.
#   Higher = faster screen updates. Drop it back if you see torn bands.
STRIPROWS=10

# FLUSHUS: microseconds to wait after each burst (0-5000, default 500).
#   At the default that is 48 x 500us = 24ms of pure waiting in EVERY frame.
#   0 is fastest; raise it if the picture tears.
FLUSHUS=500

# LISTTILE: ON (default) = the list's cover panel reuses the reel's cached 45KB
#   thumbnail. OFF = re-decode the full JPEG on every selection change (the old way,
#   and much slower on a big library). Same picture either way.
LISTTILE=ON

# REELPROF: ON = print a reel frame-time breakdown to GTI/gti.log every ~1.5s
#   while the reel is on screen. Diagnostic only.
REELPROF=OFF

# DEVMODE: ON (default) = show the TEST TOOLS button in Settings (SD soak test, reel profiler,
#   no-cache, diagnostic overlay). OFF = hide the button. Their CONFIG.TXT keys still work either way.
DEVMODE=ON
)CFG";
  sdWriteFromFlash(f,(const uint8_t*)DEFAULT_CONFIG,sizeof(DEFAULT_CONFIG)-1);   // lab14o: via RAM (was f.print - first 4 KB came out blank)
  f.close();
}

// Self-heal: after a firmware update adds a new key, an existing CONFIG.TXT won't
// have it (generateDefaultConfig never overwrites). Append any documented key the
// file is missing — with its comment + default — so upgraders get an editable line
// without their other settings being touched. Only writes when something is missing;
// the fine-tuning SS_IDLE/SS_LOAD timers stay hidden, but the slideshow keys
// (SCREENSAVER/SSMODE/SSFX/SSTIME/SSFAV) are documented so upgraders discover them.
static void selfHealConfig(){
  if(!SD_MMC.exists("/CONFIG.TXT"))return;   // fresh cards already get the full template
  struct CfgKey{const char*key;const char*block;};
  static const CfgKey KEYS[]={
    {"NEO",      "\n# NEO: the NEO look (gold frames, navy-to-purple background, list left + game panel right). OFF = the classic layout + THEME\nNEO=ON\n"},
    {"THEME",    "\n# Theme: colours for the classic layout (NEO=OFF): 0=NAVY 1=EMBER 2=MATRIX 3=PAPER 4=SYNTH 5=GOLD 6=OMEGA\nTHEME=0\n"},
    {"MODE",     "\n# Transfer mode: STANDALONE (USB to Gotek) or WIRELESS (ESP-NOW to dongle)\nMODE=STANDALONE\n"},
    {"CAROUSEL", "\n# CAROUSEL: default boot view. OFF=game list, ON=cover reel, LAST=restore last view.\nCAROUSEL=OFF\n"},
    {"LOOP",     "\n# Loop cracktro splash: 1=loop until tapped, 0=auto-dismiss after 6s\nLOOP=0\n"},
    {"CRACKTRO", "\n# Boot cracktro style: 0=random each boot, or pick one:\n#   1=COPPER CLASSIC  2=STARFIELD  3=RAINBOW RASTER\n#   4=PLASMA  5=BOING BALL  6=SYNTHWAVE  7=OMEGAWARE\nCRACKTRO=0\n"},
    {"DIAGDISP", "\n# DIAG-DISP: live diagnostic overlay box (FPS / free SRAM+PSRAM / uptime / CPU temp). ON or OFF.\nDIAGDISP=OFF\n"},
    {"NOCACHE",  "\n# NOCACHE: ON = ignore every on-SD cache (.index .gamecache .nfocache micro .tnl).\n#   Nothing is deleted; caches are just not read or written. Everything is rebuilt every\n#   boot. Diagnostic/benchmark use only - leave OFF for normal running.\nNOCACHE=OFF\n"},
    {"COVERS",   "\n# COVERS: OFF = no cover art at all (nothing decoded, letter placeholders instead).\n#   Diagnostic: isolates list and .nfo cost from cover-decode time.\nCOVERS=ON\n"},
    {"STRIPROWS","\n# STRIPROWS: how many screen rows go to the panel per SPI burst (1-40, default 10).\n#   The push is 48 bursts at 10 rows, 12 at 40 - and each burst costs FLUSHUS below.\n#   Higher = faster screen updates. Drop it back if you see torn bands.\nSTRIPROWS=10\n"},
    {"FLUSHUS",  "# FLUSHUS: microseconds to wait after each burst (0-5000, default 500).\n#   At the default that is 48 x 500us = 24ms of pure waiting in EVERY frame.\n#   0 is fastest; raise it if the picture tears.\nFLUSHUS=500\n"},
    {"BIGDISK",  "\n# BIGDISK: OFF (default) = 1.76 MB set aside for save-game write-back over the cable\n#   and for disks sent to a wireless dongle (an Amiga HD disk; the SuperMini's limit).\n#   ON = 2.9 MB, for large Atari ST .HFE images (saving to them, or sending them wirelessly).\n#   Loading any size over the cable works either way. ON costs ~1.1 MB of memory, so fewer\n#   games fit on a big card. Takes effect on the next boot. (Replaces DISKMAXKB.)\nBIGDISK=OFF\n"},
    {"LIBLIMIT", "\n# LIBLIMIT: ON (default) = if the card holds more games than this GTi can list, stop at boot with a\n#   LIBRARY TOO BIG screen and a GTI_CAPACITY.TXT report, so nothing is left out silently.\n#   OFF = load as many games as fit and leave the rest out.\nLIBLIMIT=ON\n"},
    {"DEVMODE",  "\n# DEVMODE: ON (default) = show the TEST TOOLS button in Settings (SD soak test, reel profiler,\n#   no-cache, diagnostic overlay). OFF = hide the button. Their CONFIG.TXT keys still work either way.\nDEVMODE=ON\n"},
    {"LONGNAME", "\n# LONGNAME: OFF (default) = the Gotek sees the loaded disk as DISK.ADF / DISK.DSK (GENERIC images keep a\n#   short 8.3 version of their own name, e.g. PROPHECY.HFE). ON = the Gotek sees the image's full file name,\n#   e.g. \"Prophecy I-The Viking Child-1.hfe\" - shown on a FlashFloppy OLED/LCD display.\nLONGNAME=OFF\n"},
    {"LISTTILE", "\n# LISTTILE: ON (default) = the list's cover panel reuses the reel's cached 45KB\n#   thumbnail. OFF = re-decode the full JPEG on every selection change (the old way,\n#   and much slower on a big library). Same picture either way.\nLISTTILE=ON\n"},
    {"REELPROF", "# REELPROF: ON = print a reel frame-time breakdown to GTI/gti.log every ~1.5s\n#   while the reel is on screen. Diagnostic only.\nREELPROF=OFF\n"},
    {"REELBORDER","\n# REELBORDER: frame drawn around each cover in the reel. ON=framed (default),\n#   OFF=clean/frameless look (the game you have loaded is still marked green).\nREELBORDER=ON\n"},
    {"LASTUSED", "\n# LASTUSED: ON = on power-up, jump the selection back to the game you last loaded.\n#   OFF (default) = always start at the top of the list.\nLASTUSED=OFF\n"},
    {"FONT",     "\n# Font size: SMALL, NORMAL, LARGE\nFONT=NORMAL\n"},
    {"LANG",     "\n# Language: EN, FR, IT, ES, DE, NL, PL, CS  (pull the SD and edit this line if you get stuck)\nLANG=EN\n"},
    {"ROTATE",   "\n# Screen rotation in degrees: 0 or 180 = landscape, 90 or 270 = portrait.\nROTATE=0\n"},
    {"COVERMIN", "\n# COVERMIN: hide covers whose short side is under N px (0 = show all) - keeps the reel + panel clean.\nCOVERMIN=140\n"},
    {"REELFILTER", "\n# REELFILTER: ON = the reel (cover carousel) shows only games whose cover passes COVERMIN;\n#             the A-Z list still shows every game. OFF = reel shows all games.\nREELFILTER=OFF\n"},
    {"COMPACT",  "\n# COMPACT: OFF = cover art + list, ON = maximise the game list (cover collapses to a strip)\nCOMPACT=OFF\n"},
    {"BTNSTYLE", "\n# BTNSTYLE: reel button style. PILL=rounded coloured buttons (default), FLAT=flat bar.\nBTNSTYLE=PILL\n"},
    {"CAP",      "\n# CAP: max wireless dongles the scan will list (default 32, up to 64)\nCAP=32\n"},
    {"HIVEMIND", "\n# HIVEMIND: wireless FLING fan-out. ON=all paired MuCa dongles (classic), OFF=selected dongle only.\nHIVEMIND=ON\n"},
    {"GTINAME",  "\n# GTINAME: this screen's name, shown on another screen that shares the same dongle (\"in use by DESK\").\n#   Up to 24 characters. Empty = GTi-XXXX (from this screen's radio address).\nGTINAME=\n"},
    {"TAPLOAD",  "\n# TAPLOAD: ON = tapping the already-highlighted game row loads it (old double-tap)\nTAPLOAD=OFF\n"},
    {"HOTSWAP",  "# HOTSWAP: ON = tapping another disk while loaded swaps to it instantly\nHOTSWAP=OFF\n"},
    {"FORCESWAP","# FORCESWAP: ON = swap disk contents without the USB eject/re-attach cycle\nFORCESWAP=OFF\n"},
    {"SAVES",    "\n# SAVES: save-game persistence. OFF = classic (lost on eject),\n# COPY = kept as GameName.sav.adf beside the master, OVERWRITE = patch the master.\nSAVES=COPY\n"},
    {"SDSPEED",  "\n# SDSPEED: SD card clock. 20 = default, 40 = ~1.7x faster reads if your card holds it (auto-falls back to 20), 10 = slow and careful.\nSDSPEED=20\n"},
    {"CATEGORIES","\n# CATEGORIES: OFF = flat library. ON = browse by category (a top-level folder with no disk images, only subfolders, is a category).\nCATEGORIES=OFF\n"},
    {"NESTING",  "# NESTING: OFF = one category level. ON = allow sub-categories (folders within category folders).\nNESTING=OFF\n"},
    {"SCREENSAVER","\n# SCREENSAVER: idle slideshow. ON = show it after a few minutes idle, OFF = never.\nSCREENSAVER=ON\n"},
    {"SSMODE",   "# SSMODE: SLIDES = full-screen photo slideshow (default), BOUNCE = bouncing logo, MATRIX = code rain.\nSSMODE=SLIDES\n"},
    {"SSFX",     "# SSFX: transition between slides - SHUFFLE (random), FADE, DISSOLVE, SLIDE, or CUT.\nSSFX=SHUFFLE\n"},
    {"SSTIME",   "# SSTIME: seconds each slide is shown (2-120).\nSSTIME=6\n"},
    {"SSFAV",    "# SSFAV: ON = also slideshow favourited game covers; OFF = only /screensaver/ images.\nSSFAV=ON\n"},
    {"LINK",           "\n# LINK: dongle transport. ESPNOW = the dongle's own AP + ESP-NOW (default). HOMEWIFI = route the FLING via your home router to a Webby dongle's gotek.local.\nLINK=ESPNOW\n"},
    {"HOME_SSID",      "# HOME_SSID: your home WiFi name (only used when LINK=HOMEWIFI).\nHOME_SSID=\n"},
    {"HOME_PASS",      "# HOME_PASS: your home WiFi password (only used when LINK=HOMEWIFI).\nHOME_PASS=\n"},
    {"DONGLE_HOME_IP", "# DONGLE_HOME_IP: auto-filled cache of the dongle's home-network IP (mDNS gotek.local is the primary lookup).\nDONGLE_HOME_IP=\n"},
    {"DAV",       "\n# --- WebDAV client (optional) ---\n# HTTPS here is encrypted but NOT certificate-authenticated; DAV_PASS is stored in plain text on this card.\n# DAV: ON = enable the WebDAV client (DAV_ENABLED is accepted as the same switch).\nDAV=OFF\n"},
    {"DAV_HOST",  "# DAV_HOST: WebDAV server hostname or IP.\nDAV_HOST=\n"},
    {"DAV_PORT",  "# DAV_PORT: server port (default 443).\nDAV_PORT=443\n"},
    {"DAV_HTTPS", "# DAV_HTTPS: ON = TLS (default), OFF = plain HTTP.\nDAV_HTTPS=ON\n"},
    {"DAV_USER",  "# DAV_USER: Basic-Auth username (blank = none).\nDAV_USER=\n"},
    {"DAV_PASS",  "# DAV_PASS: Basic-Auth password (blank = none).\nDAV_PASS=\n"},
    {"DAV_PATH",  "# DAV_PATH: base path on the server (default /).\nDAV_PATH=/\n"},
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

// lab14g: the log and the GTi's small state files live in /GTI, not in the card's root. Every
// log line rewrites the directory sector that holds the log's entry (its size and time live
// there); on 25 Sep a bad read of the ROOT sector was written back that way and took the
// library folder with it. A folder of its own keeps that sector away from ADF/DSK/GENERIC.
#define GTI_DIR      "/GTI"
#define GTI_LOG_PATH "/GTI/gti.log"
static String gtiStatePath(const char* name,bool forRead){   // "/GTI/<name>"; reads fall back to the old root copy once
  String np=String(GTI_DIR)+"/"+name;
  if(forRead && !SD_MMC.exists(np.c_str())){ String op=String("/")+name; if(SD_MMC.exists(op.c_str())) return op; }
  return np;
}
static bool g_log_enabled=true;   // 5.8.8 test build: /gti.log ON by default; LOG=OFF in CONFIG.TXT disables
static void gLog(const char*fmt,...){
  char buf[320]; va_list ap; va_start(ap,fmt); vsnprintf(buf,sizeof buf,fmt,ap); va_end(ap);   // lab14e: was 192 - the [scan] line was being cut off
  Serial.print(buf);
  if(g_log_enabled){ File lf=SD_MMC.open(GTI_LOG_PATH,FILE_APPEND); if(lf){ lf.print(buf); lf.close(); } }
}
void p4wifiLog(const char* s){ gLog("%s",s); }   // lab2-P4G7: the radio layer's [P4WIFI] lines into gti.log
static char g_c6_ver[40]="C6 ?";                      // lab2-P4G7: shown on Settings > TEST TOOLS

// ── lab14g: SD GUARD install (see gti_sdguard.h for the why) ──────────────────
// The card's FatFs drive (pdrv) gets our checking driver; the real SD driver is registered a
// second time on a spare drive slot and our driver calls it. Everything above FatFs - VFS,
// SD_MMC, the one-pass walker - is untouched.
static SdGuard g_sdg; static BYTE g_sdg_pdrv=0xFF, g_sdg_lower=0xFF;
static bool g_sdguard_cfg=true;                  // SDGUARD=OFF (hidden): checks off, straight pass-through
static bool g_sdpullup_cfg=true;                 // SDPULLUP=OFF (hidden): no internal pull-ups on CMD/D0
struct SdmmcPeek : public fs::SDMMCFS { static sdmmc_card_t* card(fs::SDMMCFS& f){ return static_cast<SdmmcPeek&>(f)._card; } };
static DRESULT sgLowRead(BYTE* b,LBA_t s,UINT c){ sdLock(); DRESULT r=disk_read(g_sdg_lower,b,s,c); sdUnlock(); return r; }          // lab15e: whole transfer under the SD lock
static DRESULT sgLowWrite(const BYTE* b,LBA_t s,UINT c){ sdLock(); DRESULT r=disk_write(g_sdg_lower,b,s,c); sdUnlock(); return r; }   // (write + the card's busy polling)
static DSTATUS sgInit(BYTE){ return disk_initialize(g_sdg_lower); }
static DSTATUS sgStatus(BYTE){ return disk_status(g_sdg_lower); }
static DRESULT sgRead(BYTE,BYTE* b,LBA_t s,UINT c){ return sg_read(&g_sdg,b,s,c); }
static DRESULT sgWrite(BYTE,const BYTE* b,LBA_t s,UINT c){ return sg_write(&g_sdg,b,s,c); }
static DRESULT sgIoctl(BYTE,BYTE cmd,void* b){ return disk_ioctl(g_sdg_lower,cmd,b); }
static const ff_diskio_impl_t g_sgImpl={sgInit,sgStatus,sgRead,sgWrite,sgIoctl};
static bool sdGuardInstall(){
  sdmmc_card_t* card=SdmmcPeek::card(SD_MMC); if(!card) return false;
  BYTE p=ff_diskio_get_pdrv_card(card); if(p>=FF_VOLUMES) return false;
  BYTE lower=0xFF; if(ff_diskio_get_drive(&lower)!=ESP_OK||lower>=FF_VOLUMES) return false;   // a free slot for the real driver
  ff_diskio_register_sdmmc(lower,card);
  FF_DIR* d=(FF_DIR*)malloc(sizeof(FF_DIR)); FATFS* fs=nullptr;
  if(d){ char drv[4]={(char)('0'+p),':','/',0}; if(f_opendir(d,drv)==FR_OK){ fs=d->obj.fs; f_closedir(d); } free(d); }
  if(!fs){ ff_diskio_unregister(lower); return false; }
  memset(&g_sdg,0,sizeof g_sdg); g_sdg.fs=fs; g_sdg.lread=sgLowRead; g_sdg.lwrite=sgLowWrite; g_sdg.on=g_sdguard_cfg;
  g_sdg_pdrv=p; g_sdg_lower=lower;
  ff_diskio_register(p,&g_sgImpl);               // from here every FatFs transfer on the card goes through sg_read/sg_write
  return true;
}
static void sdGuardRemove(){                     // before SD_MMC.end(): give the drive back to the plain driver
  if(g_sdg_pdrv==0xFF) return;
  ff_diskio_register_sdmmc(g_sdg_pdrv,SdmmcPeek::card(SD_MMC));
  ff_diskio_unregister(g_sdg_lower);
  g_sdg_pdrv=g_sdg_lower=0xFF; g_sdg.fs=nullptr;
}
static void sdPullups(){                          // CMD + D0 (P4G7: + D1-D3) idle high between transfers
  if(g_sdpullup_cfg){ gpio_pullup_en((gpio_num_t)SD_CMD); gpio_pullup_en((gpio_num_t)SD_D0);
    gpio_pullup_en((gpio_num_t)SD_D1); gpio_pullup_en((gpio_num_t)SD_D2); gpio_pullup_en((gpio_num_t)SD_D3); }
  else { gpio_pullup_dis((gpio_num_t)SD_CMD); gpio_pullup_dis((gpio_num_t)SD_D0);
    gpio_pullup_dis((gpio_num_t)SD_D1); gpio_pullup_dis((gpio_num_t)SD_D2); gpio_pullup_dis((gpio_num_t)SD_D3); }
}
// P4G7: mount 4-bit first (all four data lines are wired), fall back to 1-bit. Pull-ups on during negotiation.
static bool sdMountTry(int freq){
  SD_MMC.setPins(SD_CLK,SD_CMD,SD_D0,SD_D1,SD_D2,SD_D3); sdPullups();
  if(SD_MMC.begin("/sdcard",false,false,freq)){ g_sd_4bit=true; return true; }
  SD_MMC.end(); delay(20);
  SD_MMC.setPins(SD_CLK,SD_CMD,SD_D0); sdPullups();
  if(SD_MMC.begin("/sdcard",true,false,freq)){ g_sd_4bit=false; return true; }
  return false;
}
// Called from loop() and after big jobs: anything the guard caught goes to the log (the guard
// itself can't log - logging is a card write that goes through the guard).
static void sdGuardReport(bool always){
  if(g_sdg_pdrv==0xFF) return;
  if(!always && !g_sdg.pending_report) return;
  g_sdg.pending_report=false;
  gLog("[sdguard] %s | metadata reads %u writes %u | bad reads fixed %u, odd-but-consistent %u, unreadable %u | writes refused %u | write verify: fixed %u failed %u | last: sector %u shift %d bits, first bytes %02X %02X %02X %02X %02X %02X %02X %02X\n",
       g_sdg.on?"ON":"OFF",(unsigned)g_sdg.meta_reads,(unsigned)g_sdg.meta_writes,(unsigned)g_sdg.reads_fixed,(unsigned)g_sdg.reads_odd,(unsigned)g_sdg.reads_failed,
       (unsigned)g_sdg.writes_refused,(unsigned)g_sdg.verify_fixed,(unsigned)g_sdg.verify_failed,(unsigned)g_sdg.last_sector,g_sdg.last_shift,
       g_sdg.last_bad[0],g_sdg.last_bad[1],g_sdg.last_bad[2],g_sdg.last_bad[3],g_sdg.last_bad[4],g_sdg.last_bad[5],g_sdg.last_bad[6],g_sdg.last_bad[7]);
}
static void loadConfig(){
  applyTheme(0);
  File f=SD_MMC.open("/CONFIG.TXT",FILE_READ);if(!f)return;
  while(f.available()){String l=f.readStringUntil('\n');l.trim();if(l.startsWith("#"))continue;
    int eq=l.indexOf('=');if(eq<0)continue;String k=l.substring(0,eq),v=l.substring(eq+1);k.trim();v.trim();
    if(k=="NEO"){String nv=v;nv.toUpperCase();g_neo_on=!(nv=="OFF"||nv=="0"||nv=="NO");applyTheme(g_theme_idx);}   // lab4-P4
    else if(k=="THEME"){int ti=-1;for(int i=0;i<NUM_THEMES;i++)if(v.equalsIgnoreCase(THEMES[i].name)){ti=i;break;}applyTheme(ti>=0?ti:((v.length()&&isDigit(v[0]))?v.toInt():0));}else if(k=="LOOP")g_loop_cracktro=(v=="1");else if(k=="MODE")g_wireless_mode=(v=="WIRELESS");else if(k=="CAROUSEL"){String cv=v;cv.toUpperCase();g_car_bootmode=(cv=="LAST")?2:((cv=="1"||cv=="ON"||cv=="TRUE")?1:0);}
    else if(k=="TAPLOAD")g_tapload=(v=="ON"||v=="1");else if(k=="HOTSWAP")g_hotswap=(v=="ON"||v=="1");else if(k=="FORCESWAP")g_forceswap=(v=="ON"||v=="1");
    else if(k=="FONT"){int f=1;if(v=="SMALL")f=0;else if(v=="LARGE")f=2;applyFont(f);}
    else if(k=="LANG"){String lu=v;lu.toUpperCase();if(lu=="CZ")lu="CS";for(int i=0;i<LANG_N;i++)if(lu==LANG_NAMES[i]){g_lang=i;break;}}
    else if(k=="ROTATE"){g_rot=((v.toInt()/90)%4+4)%4;}
    else if(k=="PANELTURN"){p4disp_set_turn(((v.toInt()/90)%4+4)%4);}   // P4G7 hidden key: PANELTURN=180 for a board mounted upside down
    else if(k=="COVERMIN"){g_covermin=v.toInt();if(g_covermin<0)g_covermin=0;}
    else if(k=="REELFILTER"){String ru=v;ru.trim();ru.toUpperCase();g_reelfilter=(ru=="ON"||ru=="1"||ru=="YES");}
    else if(k=="COMPACT"){g_compact=(v=="ON"||v=="1");}
    else if(k=="SCREENSAVER"){g_ss_enabled=(v!="OFF"&&v!="0");}
    else if(k=="SS_IDLE"){uint32_t s=(uint32_t)v.toInt(); if(s>0)g_ss_idle_ms=s*1000UL;}
    else if(k=="SS_LOAD"){uint32_t s=(uint32_t)v.toInt(); if(s>0)g_ss_load_ms=s*1000UL;}
    else if(k=="SSMODE"){String u=v;u.toUpperCase();g_ss_matrix=(u=="MATRIX"||u=="RAIN");g_ss_slides=!(u=="BOUNCE"||u=="0"||u=="SPRITES"||g_ss_matrix);}   // v5.7.2 slideshow; 5.8.3 matrix
    else if(k=="BTNSTYLE"){String u=v;u.toUpperCase();g_btn_pill=!(u=="FLAT"||u=="0"||u=="BAR");}   // 5.8.3 reel button style
    else if(k=="SSFX"){String u=v;u.toUpperCase();
      if(u=="FADE")g_ss_fx=1; else if(u=="DISSOLVE"||u=="DISS")g_ss_fx=2;
      else if(u=="SLIDE"||u=="PUSH")g_ss_fx=3; else if(u=="CUT"||u=="NONE")g_ss_fx=4; else g_ss_fx=0; }  // default SHUFFLE
    else if(k=="SSTIME"){uint32_t s=(uint32_t)v.toInt(); if(s<2)s=2; if(s>120)s=120; g_ss_time_ms=s*1000UL;}
    else if(k=="SSFAV"){g_ss_fav=(v!="OFF"&&v!="0");}
    else if(k=="CAP"){int c=v.toInt(); if(c>=1&&c<=64)g_dongle_cap=c;}
    else if(k=="CRACKTRO"){String cu=v;cu.trim();cu.toUpperCase(); if(cu=="OFF"||cu=="NONE")g_cracktro=-1; else if(cu=="OMEGA"||cu=="OMEGAWARE")g_cracktro=7; else if(cu=="DENISE")g_cracktro=8; else if(cu=="WRANGLER")g_cracktro=9; else if(cu=="RETRONAUT")g_cracktro=10; else{int c=v.toInt(); if(c>=0&&c<=7)g_cracktro=c; /* 7=OMEGAWARE; DENISE/WRANGLER/RETRONAUT are hidden, name-only */}}
    else if(k=="SAVES"){v.toUpperCase(); g_saves_mode=(v=="OVERWRITE")?2:(v=="OFF"||v=="0")?0:1;}
    else if(k=="SDSPEED"){int hz=v.toInt(); g_sd_freq=(hz>=40||hz>=40000)?40000:(hz==10||hz==10000)?10000:20000;}   // lab14g: 10 = slow and careful
    else if(k=="SDGUARD"){String nv=v;nv.toUpperCase();g_sdguard_cfg=!(nv=="OFF"||nv=="0"||nv=="FALSE");}     // lab14g (hidden)
    else if(k=="SDPULLUP"){String nv=v;nv.toUpperCase();g_sdpullup_cfg=!(nv=="OFF"||nv=="0"||nv=="FALSE");}   // lab14g (hidden)
    else if(k=="LOG"){String lu=v;lu.toUpperCase();g_log_enabled=(lu!="OFF"&&lu!="0");}
    else if(k=="HIVEMIND"){g_hivemind=(v=="OFF"||v=="0")?0:1;}
    else if(k=="GTINAME"){espnowSetScreenName(v.substring(0,24));}   // lab14s
    else if(k=="CATEGORIES"){String cv=v;cv.toUpperCase();g_categories=(cv=="ON"||cv=="1"||cv=="TRUE");}
    else if(k=="DIAGDISP"){String dv=v;dv.toUpperCase();g_diagdisp=(dv=="ON"||dv=="1"||dv=="TRUE");}
    else if(k=="NOCACHE"){String nv=v;nv.toUpperCase();g_nocache=(nv=="ON"||nv=="1"||nv=="TRUE");}
    else if(k=="LONGNAME"){String lv=v;lv.toUpperCase();g_longname=(lv=="ON"||lv=="1"||lv=="TRUE"||lv=="YES");}   // lab14q
    else if(k=="DEVMODE"){String dv=v;dv.toUpperCase();g_devmode=!(dv=="OFF"||dv=="0"||dv=="FALSE");}   // lab14p
    else if(k=="LIBLIMIT"){String nv=v;nv.toUpperCase();g_liblimit=!(nv=="OFF"||nv=="0"||nv=="FALSE");}   // 5.9.41-lab14d (hidden): OFF = load what fits instead of halting
    else if(k=="FASTSCAN"){String nv=v;nv.toUpperCase();g_fastscan=!(nv=="OFF"||nv=="0"||nv=="FALSE");}   // 5.9.41-lab14 (hidden): OFF = old per-entry walker
    else if(k=="COVERS"){String cv=v;cv.toUpperCase();g_covers_on=!(cv=="OFF"||cv=="0"||cv=="NO");}
    else if(k=="STRIPROWS"){int r=v.toInt(); if(r<1)r=1; if(r>g_strip_cap)r=g_strip_cap; g_strip_rows=r;}   // 5.9.33-lab3
    else if(k=="FLUSHUS"){int u=v.toInt(); if(u<0)u=0; if(u>5000)u=5000; g_flush_us=u;}                     // 5.9.33-lab3
    else if(k=="REELPROF"){String rv=v;rv.toUpperCase();g_reelprof=(rv=="ON"||rv=="1"||rv=="TRUE");}        // 5.9.33-lab3
    else if(k=="LISTTILE"){String lv=v;lv.toUpperCase();g_listtile=!(lv=="OFF"||lv=="0"||lv=="NO");}        // 5.9.34-lab4
    else if(k=="BIGDISK"){String bu=v;bu.trim();bu.toUpperCase(); if(!g_disk) g_img_max_kb=(bu=="ON"||bu=="1"||bu=="YES")?DISK_IMG_BIG_KB:DISK_IMG_DEF_KB;}   // lab14m: boot only (the RAM disk is sized once); DISKMAXKB= is no longer read
    else if(k=="REELBORDER"){String rv=v;rv.toUpperCase();g_reelborder=!(rv=="OFF"||rv=="0"||rv=="NO");}   // MasterTelly CR: default ON
    else if(k=="LASTUSED"){String lv=v;lv.toUpperCase();g_lastused=(lv=="ON"||lv=="1"||lv=="TRUE");}
    else if(k=="NESTING"){String nv=v;nv.toUpperCase();g_nesting=(nv=="ON"||nv=="1"||nv=="TRUE");}
    else if(k=="LINK"){String lv=v;lv.toUpperCase();g_link_home=(lv=="HOMEWIFI"||lv=="HOME"||lv=="WIFI");}
    else if(k=="HOME_SSID"||k=="WIFI_CLIENT_SSID"){if(v.length())g_home_ssid=v;}   // WIFI_CLIENT_SSID: the OMEGAWARE tree stores the same credential under this name; empty never erases a value another key already set
    else if(k=="HOME_PASS"||k=="WIFI_CLIENT_PASS"){if(v.length())g_home_pass=v;}
    else if(k=="DAV"||k=="DAV_ENABLED"){String dv=v;dv.toUpperCase();g_dav_on=(dv=="ON"||dv=="1");}   // DAV_ENABLED: the OMEGAWARE tree writes this name for the same switch — cards travel between firmwares, so accept both
    else if(k=="DAV_HOST"){g_dav_host=v;}
    else if(k=="DAV_PORT"){int p=v.toInt();g_dav_port=(p>0&&p<65536)?p:443;}
    else if(k=="DAV_USER"){g_dav_user=v;}
    else if(k=="DAV_PASS"){g_dav_pass=v;}
    else if(k=="DAV_PATH"){g_dav_path=v;}
    else if(k=="DAV_HTTPS"){String hv=v;hv.toUpperCase();g_dav_https=!(hv=="OFF"||hv=="0");}
    else if(k=="DAV_TEST"){g_dav_test=v;}
    else if(k=="WEBUI"){String wv=v;wv.toUpperCase();g_web_on=(wv=="ON"||wv=="1");}
    else if(k=="DONGLE_HOME_IP"){g_dongle_home_ip=v;}}
  f.close();
  davApplyConfig();   // hand the DAV_* settings to the shared client (merge step 1)
}

// ════════════════════════════════════════════════════════════════════════════
// LAYOUT — 480×320
// ════════════════════════════════════════════════════════════════════════════
#define VW gW
#define VH gH
#define STATUS_H   (g_neo?40:28)   // lab16b-P4: NEO top bar 40 (lab4-P4: classic back to its own 28)
#define MODE_BAR_H 24
#define NOW_PLAY_H 30
#define BOTTOM_H   (g_neo?58:54)   // lab16b-P4: NEO room for 44 px pills (lab4-P4: classic back to its own 54)
static int g_az_w=42;
#define AZ_W       g_az_w   // lab16b-P4: NEO uses a slim 26 px strip
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
static void applyFont(int f){if(f<0||f>2)f=1;g_font=f;g_name_sz=(f==0?2:f==2?4:3);   // P4: one text size up
  int target=(f==0?50:f==2?100:74),listH=LIST_BOTTOM-LIST_TOP,rows=listH/target;
  if(g_neo){ g_name_sz=(f==2?3:2); target=(f==0?44:f==2?66:52); rows=listH/target; }   // lab16b-P4: name + "n disks" line
  if(listH%target>=target/2)rows++; if(rows<1)rows=1;
  g_item_h=listH/rows; g_items_vis=rows;}
static const char* fontName(int f){return f==0?T(L_FONT_SMALL):f==2?T(L_FONT_LARGE):T(L_FONT_NORMAL);}
static const char* fontKey(int f){return f==0?"SMALL":f==2?"LARGE":"NORMAL";}   // canonical CONFIG.TXT token — NEVER localized (load parser matches these)
// lab16b-P4: NEO geometry (the mock-ups). Landscape: list panel left, A-Z strip, game panel right.
// Portrait: game panel on top, list panel + A-Z under it. INSERT lives in the bottom bar.
static int NEO_LP_X=8,NEO_LP_Y=48,NEO_LP_W=420,NEO_LP_H=360;   // the list's rounded panel
static void neoLayout(){
  COVER_ON=true; STRIP_ON=false; NOW_ON=false; g_az_w=26;
  int top=STATUS_H+8, bot=VH-BOTTOM_H-6;
  if(!g_portrait){
    COVER_W=330; COVER_X=VW-8-COVER_W; COVER_Y=top; COVER_H=bot-top;
    AZ_X=COVER_X-4-AZ_W; AZ_TOP=top; AZ_H=bot-top;
    NEO_LP_X=8; NEO_LP_Y=top; NEO_LP_W=AZ_X-4-NEO_LP_X; NEO_LP_H=bot-top;
    COVER_ART_W=COVER_W-24; COVER_ART_H=190;   // lab16d-P4: big cover
  }else{
    COVER_X=8; COVER_W=VW-16; COVER_Y=top; COVER_H=360;   // lab16d-P4: was 300
    int lt=COVER_Y+COVER_H+8;
    AZ_X=VW-8-AZ_W; AZ_TOP=lt; AZ_H=bot-lt;
    NEO_LP_X=8; NEO_LP_Y=lt; NEO_LP_W=AZ_X-4-NEO_LP_X; NEO_LP_H=bot-lt;
    COVER_ART_W=COVER_W-24; COVER_ART_H=190;
  }
  COVER_ART_X=COVER_X+12; COVER_ART_Y=COVER_Y+12;
  LIST_X=NEO_LP_X+6; LIST_W=NEO_LP_W-12; LIST_TOP=NEO_LP_Y+6; LIST_BOTTOM=NEO_LP_Y+NEO_LP_H-6;
  INS_X=INS_Y=INS_W=INS_H=0;   // no INSERT box on the panel: it is the middle pill of the bottom bar
  STRIP_Y=STRIP_H=0; NOW_Y=LIST_BOTTOM;
}
static void relayout(){
  if(g_portrait){gW=LCD_WIDTH;gH=LCD_HEIGHT;}else{gW=LCD_HEIGHT;gH=LCD_WIDTH;}   // P4G7: rotation-aware canvas (compose 600x1024)
  // Reset the clip window to the new canvas. The clip statics init to the
  // landscape 320 height; with an EMPTY game list drawFileList() early-returns
  // before its usual set/reset, so in portrait everything below y=320 —
  // including the whole bottom bar — was silently clipped (buttons invisible
  // but still tappable). Rotation must always re-sync the clip.
  g_clip_x0=0;g_clip_x1=gW;g_clip_y0=0;g_clip_y1=gH;
  if(g_neo){ neoLayout(); applyFont(g_font); return; }   // lab16b-P4
  g_az_w=42;
  AZ_X=VW-AZ_W; int mb=STATUS_H+MODE_BAR_H;
  if(!g_compact){
    if(!g_portrait){
      COVER_ON=true;COVER_X=0;COVER_Y=STATUS_H;COVER_W=292;COVER_H=VH-STATUS_H-BOTTOM_H;   // P4
      COVER_ART_X=8;COVER_ART_Y=STATUS_H+8;COVER_ART_W=276;COVER_ART_H=214;
      LIST_X=COVER_W;LIST_TOP=mb;LIST_W=AZ_X-COVER_W;
      NOW_ON=true;NOW_Y=VH-BOTTOM_H-NOW_PLAY_H;LIST_BOTTOM=NOW_Y;
      AZ_TOP=LIST_TOP;AZ_H=(VH-BOTTOM_H)-LIST_TOP;
      INS_X=8;INS_W=COVER_W-16;INS_H=40;INS_Y=VH-BOTTOM_H-52;STRIP_ON=false;   // P4
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
// ==== Custom wordmark bitmaps (1bpp, MSB-first, row-major) — hidden CRACKTRO themes (5.4.0) ====
#define GTI_DENISE_W 163
#define GTI_DENISE_H 57
static const uint8_t GTI_DENISE_BITS[] PROGMEM = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,255,192,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,31,255,248,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,127,255,254,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,248,0,255,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,7,224,0,63,128,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,15,128,0,31,192,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,31,7,128,7,224,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,62,15,128,7,224,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,124,31,128,3,240,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,120,63,0,1,240,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,248,63,0,1,248,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,240,126,0,1,248,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,240,126,0,0,248,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,240,126,0,0,252,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,240,252,0,0,252,0,0,0,0,0,0,0,3,224,0,0,0,0,0,0,0,240,252,0,0,252,0,0,0,0,0,0,0,15,224,0,0,0,0,0,0,0,112,248,0,0,252,0,0,0,0,0,0,0,15,224,0,0,0,0,0,0,0,1,248,0,0,252,0,0,0,0,0,0,0,31,224,0,0,0,0,0,0,0,1,248,0,0,252,0,0,0,0,0,0,0,15,192,0,0,0,0,0,0,0,1,240,0,0,252,0,0,0,0,0,0,0,7,128,0,0,0,0,0,0,0,3,240,0,0,252,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,240,0,0,252,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,224,0,0,252,0,0,0,0,0,0,0,0,0,12,0,0,0,0,0,0,7,224,0,1,248,0,0,0,0,0,0,0,0,0,30,0,0,0,0,0,0,7,224,0,1,248,0,126,0,0,0,0,0,0,0,60,0,0,15,192,0,0,7,192,0,1,248,1,255,0,0,0,0,0,112,0,60,0,0,63,224,0,0,7,192,0,1,248,7,255,128,0,0,0,0,248,0,60,0,0,255,240,0,0,15,192,0,3,248,15,199,128,0,0,0,1,248,0,62,0,1,248,240,0,0,15,128,0,3,240,31,131,128,0,31,0,1,240,0,126,0,3,240,112,0,0,15,128,0,3,240,63,7,131,192,127,128,3,240,0,126,0,7,224,240,0,0,31,128,0,7,240,126,7,135,193,255,192,7,224,0,223,0,15,192,240,0,0,31,128,0,7,224,252,15,7,227,255,192,7,224,1,223,0,31,129,224,0,0,31,0,0,15,225,248,15,7,199,255,224,15,192,1,159,0,63,1,224,0,0,63,0,0,15,193,240,30,7,207,7,224,15,192,3,15,128,62,3,192,0,0,63,0,0,31,195,240,60,15,222,7,224,31,128,7,15,128,126,7,128,0,0,62,0,0,31,131,224,248,15,252,7,224,31,128,14,15,128,124,31,0,0,0,62,0,0,63,7,225,240,15,248,7,224,63,0,60,15,128,252,62,1,0,0,126,0,0,127,7,199,224,31,240,7,192,63,0,120,15,192,248,252,3,0,0,124,0,0,126,7,255,128,31,224,7,192,126,0,252,7,193,255,240,3,0,0,124,0,0,252,15,254,0,63,192,15,192,126,0,254,7,193,255,192,6,0,0,252,0,1,248,15,224,0,127,192,15,128,254,0,247,7,195,252,0,14,0,0,252,0,3,248,15,128,0,255,128,15,129,254,0,227,135,199,240,0,28,0,0,248,0,3,240,15,128,1,255,0,15,131,252,1,225,255,221,240,0,56,0,0,248,0,7,224,15,128,3,255,0,31,7,126,3,240,127,249,240,0,120,0,1,248,0,15,128,15,128,7,254,0,15,142,62,7,240,31,225,240,0,240,0,1,248,0,63,0,15,192,31,124,0,15,252,63,30,120,15,129,248,3,224,0,1,240,0,126,0,7,224,124,124,0,7,248,63,252,124,63,0,252,15,128,0,1,240,1,252,0,3,255,248,56,0,3,224,31,240,63,254,0,127,255,0,0,1,240,7,240,0,1,255,224,0,0,0,0,7,192,31,252,0,63,252,0,0,1,240,63,192,0,0,127,0,0,0,0,0,0,0,7,240,0,15,224,0,0,0,255,255,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,127,248,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
#define GTI_VINCENT_W 187
#define GTI_VINCENT_H 57
static const uint8_t GTI_VINCENT_BITS[] PROGMEM = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,7,240,0,0,224,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,31,248,0,1,240,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,127,254,0,3,248,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,248,126,0,7,252,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,240,63,0,3,252,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,224,31,0,1,252,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,224,31,128,0,252,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,7,192,31,128,0,124,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,7,192,31,128,0,124,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,15,192,31,128,0,126,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,112,0,15,128,31,128,0,124,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,240,0,15,128,31,128,0,124,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,240,0,15,128,31,128,0,124,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,240,0,15,128,31,128,0,124,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,224,0,7,128,31,128,0,124,0,248,0,0,0,0,0,0,0,0,0,0,0,0,0,7,224,0,3,192,31,128,0,124,3,248,0,0,0,0,0,0,0,0,0,0,0,0,0,15,192,0,1,192,31,128,0,120,3,248,0,0,0,0,0,0,0,0,0,0,0,0,0,15,192,0,0,0,31,128,0,120,7,248,0,0,0,0,0,0,0,0,0,0,0,0,0,31,128,0,0,0,63,128,0,248,3,240,0,0,0,0,0,0,0,0,0,0,0,0,0,31,128,0,0,0,63,128,0,248,1,224,0,0,0,0,0,0,0,0,0,0,0,0,0,63,0,0,0,0,63,0,0,240,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,63,0,0,0,0,63,0,0,240,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,126,0,0,0,0,63,0,1,224,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,124,0,0,0,0,63,0,1,224,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,252,0,0,0,0,126,0,1,224,0,0,0,0,0,0,0,0,0,31,128,0,0,0,0,248,0,0,0,0,126,0,3,192,28,0,0,0,0,0,7,192,0,127,192,0,0,0,1,248,0,0,0,0,126,0,3,192,62,0,0,0,0,0,31,240,1,255,224,0,0,0,1,240,0,0,0,0,126,0,7,128,126,0,0,0,0,0,127,240,3,241,224,0,0,0,15,255,255,0,0,0,252,0,7,128,124,0,0,31,0,0,252,56,7,224,224,0,7,192,15,255,255,0,0,0,252,0,15,0,252,3,192,127,128,1,248,56,15,193,224,240,31,224,3,224,0,0,0,0,252,0,15,1,248,7,193,255,192,3,240,120,31,129,225,240,127,240,7,192,0,0,0,1,252,0,30,1,248,7,227,255,192,7,224,120,63,3,193,248,255,240,7,192,0,0,0,1,248,0,30,3,240,7,199,255,224,7,192,112,126,3,193,241,255,248,7,192,0,0,0,1,248,0,60,3,240,7,207,7,224,15,192,240,124,7,129,243,193,248,15,128,0,0,0,1,248,0,60,7,224,15,222,7,224,31,128,224,252,15,3,247,129,248,15,128,0,0,0,3,240,0,120,7,224,15,252,7,224,31,128,0,248,62,3,255,1,248,15,128,0,0,0,3,240,0,112,7,192,15,248,7,224,63,0,1,248,124,3,254,1,248,15,0,1,0,0,3,240,0,240,15,192,31,240,7,192,63,0,1,241,248,7,252,1,240,31,0,1,0,0,7,224,1,224,15,128,31,224,7,192,126,0,1,255,224,7,248,1,240,31,0,2,0,0,7,224,1,192,15,128,31,192,15,192,126,0,3,255,128,15,240,3,240,31,0,6,0,0,7,224,3,192,15,128,63,192,15,128,254,0,3,248,0,31,240,3,224,63,0,4,0,0,15,224,7,128,15,128,63,128,15,129,254,0,7,224,0,63,224,3,224,127,0,12,0,0,15,192,7,0,31,0,127,0,15,131,190,0,7,224,0,127,192,3,224,255,0,24,0,0,15,192,14,0,31,128,255,0,31,7,62,0,15,224,0,255,192,7,193,255,0,48,0,0,15,192,28,0,15,129,254,0,15,142,62,0,31,224,1,255,128,3,227,159,0,96,0,0,15,192,60,0,15,199,252,0,15,252,62,0,123,240,7,223,0,3,255,31,1,192,0,0,15,192,120,0,15,255,124,0,7,248,63,0,241,248,31,31,0,1,254,31,207,128,0,0,15,192,240,0,7,252,56,0,3,224,31,199,192,255,254,14,0,0,248,15,255,0,0,0,15,193,224,0,1,240,0,0,0,0,31,255,128,127,248,0,0,0,0,15,252,0,0,0,15,199,128,0,0,0,0,0,0,0,15,254,0,31,192,0,0,0,0,3,240,0,0,0,15,255,0,0,0,0,0,0,0,0,3,248,0,0,0,0,0,0,0,0,0,0,0,0,7,252,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,240,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
#define GTI_WRANGLER_W 285
#define GTI_WRANGLER_H 40
static const uint8_t GTI_WRANGLER_BITS[] PROGMEM = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,192,0,0,0,0,0,0,0,0,0,0,0,0,0,31,255,0,126,1,255,251,255,255,224,0,0,15,240,0,31,252,0,255,254,0,127,254,0,127,255,0,7,255,255,255,31,255,255,0,0,31,255,0,126,1,255,251,255,255,248,0,0,15,240,0,31,254,0,255,254,0,255,255,128,127,255,0,7,255,255,255,31,255,255,192,0,31,255,0,254,1,255,251,255,255,254,0,0,31,240,0,31,255,0,255,254,3,255,255,224,127,255,0,7,255,255,255,31,255,255,240,0,31,255,0,254,1,255,251,255,255,255,0,0,31,248,0,31,255,0,255,254,7,255,255,240,127,255,0,7,255,255,255,31,255,255,248,0,3,248,0,255,0,63,192,127,255,255,128,0,63,248,0,3,255,128,31,240,15,255,255,240,15,248,0,0,255,255,255,3,255,255,252,0,3,248,1,255,0,31,128,63,128,255,128,0,63,248,0,1,255,128,7,224,31,248,15,240,7,240,0,0,127,0,63,1,252,7,252,0,1,248,1,255,0,31,128,63,128,63,192,0,63,252,0,1,255,192,7,224,31,224,3,240,7,240,0,0,127,0,63,1,252,1,254,0,1,248,1,255,128,63,128,63,128,63,192,0,127,252,0,1,255,224,7,224,63,192,3,240,7,240,0,0,127,0,63,1,252,1,254,0,1,252,3,255,128,63,0,63,128,31,192,0,126,252,0,1,255,224,7,224,63,128,3,240,7,240,0,0,127,0,63,1,252,0,254,0,1,252,3,255,128,63,0,63,128,31,192,0,126,126,0,1,255,240,7,224,127,128,1,240,7,240,0,0,127,0,0,1,252,0,254,0,0,252,3,255,192,63,0,63,128,31,192,0,254,126,0,1,255,248,7,224,127,0,1,240,7,240,0,0,127,0,0,1,252,0,254,0,0,252,7,255,192,127,0,63,128,31,192,0,252,127,0,1,251,248,7,224,127,0,0,0,7,240,0,0,127,0,0,1,252,0,254,0,0,254,7,239,192,126,0,63,128,31,192,0,252,63,0,1,251,252,7,224,254,0,0,0,7,240,0,0,127,0,0,1,252,0,254,0,0,126,7,239,224,126,0,63,128,63,128,1,252,63,0,1,249,252,7,224,254,0,0,0,7,240,0,0,127,0,0,1,252,1,252,0,0,126,15,231,224,126,0,63,128,255,128,1,248,63,128,1,248,254,7,224,254,0,0,0,7,240,0,0,127,255,224,1,252,7,252,0,0,126,15,199,224,252,0,63,255,255,0,3,248,31,128,1,248,255,7,224,254,0,0,0,7,240,0,0,127,255,224,1,255,255,248,0,0,127,15,199,240,252,0,63,255,252,0,3,248,31,128,1,248,127,7,224,254,0,0,0,7,240,0,0,127,255,224,1,255,255,224,0,0,63,15,195,240,252,0,63,255,248,0,3,240,31,192,1,248,63,135,224,254,0,127,248,7,240,0,0,127,255,224,1,255,255,192,0,0,63,31,131,240,252,0,63,255,254,0,7,240,15,192,1,248,63,199,224,254,0,127,248,7,240,0,0,127,255,224,1,255,255,240,0,0,63,31,131,249,248,0,63,255,255,0,7,240,15,192,1,248,31,199,224,254,0,127,248,7,240,0,0,127,0,0,1,255,255,248,0,0,63,159,129,249,248,0,63,128,255,128,7,255,255,224,1,248,15,231,224,254,0,127,248,7,240,0,0,127,0,0,1,252,7,252,0,0,31,191,129,249,248,0,63,128,63,192,15,255,255,224,1,248,15,247,224,254,0,127,248,7,240,0,0,127,0,0,1,252,1,254,0,0,31,191,1,249,248,0,63,128,31,192,15,255,255,240,1,248,7,247,224,127,0,3,248,7,240,0,0,127,0,0,1,252,0,254,0,0,31,255,0,255,240,0,63,128,31,192,15,255,255,240,1,248,3,255,224,127,0,3,248,7,240,0,0,127,0,0,1,252,0,254,0,0,31,255,0,255,240,0,63,128,31,192,31,255,255,240,1,248,3,255,224,127,0,3,248,7,240,3,224,127,0,0,1,252,0,254,0,0,15,254,0,255,240,0,63,128,31,192,31,192,3,248,1,248,1,255,224,63,128,3,248,7,240,3,224,127,0,31,1,252,0,254,0,0,15,254,0,127,224,0,63,128,31,192,63,128,3,248,1,248,1,255,224,63,192,3,248,7,240,3,224,127,0,31,1,252,0,254,0,0,15,254,0,127,224,0,63,128,31,192,63,128,1,248,1,248,0,255,224,31,224,3,248,7,240,7,224,127,0,31,1,252,0,254,0,0,7,252,0,127,224,0,63,128,15,224,63,128,1,252,1,248,0,127,224,31,248,7,248,7,240,7,224,127,0,31,1,252,0,127,0,0,7,252,0,63,224,0,255,224,15,240,127,128,1,252,7,254,0,127,224,15,255,255,248,31,255,255,225,255,255,255,7,255,0,127,128,0,7,252,0,63,192,3,255,248,15,249,255,224,7,255,159,255,192,63,224,7,255,255,248,127,255,255,231,255,255,255,31,255,192,127,192,0,7,248,0,63,192,3,255,248,15,249,255,224,7,255,159,255,192,31,224,3,255,255,240,127,255,255,231,255,255,255,31,255,192,127,192,0,3,248,0,31,192,3,255,248,7,249,255,224,7,255,159,255,192,31,224,0,255,255,224,127,255,255,231,255,255,255,31,255,192,63,192,0,3,248,0,31,192,3,255,248,1,249,255,224,7,255,159,255,192,15,224,0,63,255,0,127,255,255,231,255,255,255,31,255,192,15,192,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,128,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
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

static void crk_mask(const uint8_t*bits,int mw,int mh,int x0,int y0,uint16_t col){
  int rb=(mw+7)/8;
  for(int r=0;r<mh;r++){int vy=y0+r; if(vy<0||vy>=gH)continue;
    for(int c=0;c<mw;c++){ if(pgm_read_byte(&bits[r*rb+(c>>3)])&(0x80>>(c&7))){int vx=x0+c; if(vx>=0&&vx<gW)gfx_drawPixel(vx,vy,col);} }}}
static void crk_maskC(const uint8_t*bits,int mw,int mh,int cx,int y0,uint16_t col){crk_mask(bits,mw,mh,cx-mw/2,y0,col);}
static const char* CRK_SCROLL="        OMEGAWARE PRESENTS ... THE GTi ... THE FLOPPY FLINGER THINGER ... CODED BY MEZ & DIMMY ... A LITTLE TRIBUTE TO THE AMIGA CRACKTRO LEGENDS ... GREETINGS TO EVERYONE KEEPING THE SCENE ALIVE ... NOW GO LOAD A GAME ...        ";
static void crk_scrollerT(float t,const char*STR,uint16_t col,float amp,bool rainbow){
  const int sz=2,cw=12; int slen=strlen(STR);
  long cs=(long)(t*0.13f); int sc=(int)(cs/cw), px=(int)(cs%cw);
  gfx_fillRect(0,gH-30,gW,30,CRK_RGB(5,7,15));
  for(int c=0;c<gW/cw+3;c++){char ch=STR[(((sc+c)%slen)+slen)%slen]; int x=-px+c*cw;
    int y=gH-26+(int)(sinf(x*0.035f+t*0.004f)*amp);
    crk_char(ch,x,y,sz, rainbow?crk_hue(x*1.2f+t*0.2f):col);}}
static void crk_scroller(float t,uint16_t col,float amp,bool rainbow){ crk_scrollerT(t,CRK_SCROLL,col,amp,rainbow); }
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
  // lab5-P4G7: scaled to the screen (it was laid out for a 320-tall one: on the 7" the grid stopped half way down) and the
  //   grid is symmetric - the old x=-6..12 put 6 lines left of centre and 12 right.
  const float s=gH/320.0f;
  for(int y=0;y<gH;y++){float f=(float)y/gH; uint16_t col;
    if(f<0.52f) col=crk_lerp(24,11,51, 90,26,110, f/0.52f);
    else        col=crk_lerp(11,10,26, 4,4,12, (f-0.53f)/0.47f);
    gfx_fillRect(0,y,gW,1,col);}
  const int hz=(int)(176*s);
  { int ns=26*gW/480, sh=hz-(int)(26*s); if(sh<1)sh=1; for(int i=0;i<ns;i++){int sx=(i*53+7)%gW, sy=(i*29)%sh; gfx_drawPixel(sx,sy,TFT_WHITE);} }
  int sunx=gW/2,suny=(int)(168*s),sr=(int)(58*s);
  for(int yy=-sr;yy<=0;yy++){int w=(int)sqrtf((float)(sr*sr-yy*yy)); gfx_fillRect(sunx-w,suny+yy,2*w,1,CRK_RGB(255,91,138));}
  for(int i=0;i<6;i++){int yy=suny+(int)((-50+i*8)*s); int h=(int)((3+i)*s); if(h<1)h=1; gfx_fillRect(sunx-sr-2,yy,2*sr+4,h,CRK_RGB(24,11,51));}
  uint16_t grc=CRK_RGB(0,229,255);
  for(int i=0;i<8;i++){int yy=hz+(int)(i*i*2.4f*s); if(yy<gH) gfx_hline(0,yy,gW,grc);}
  { const int sp=(int)(70*s); const int n=(gW/2)/sp+2;   // same count each side, enough to reach both bottom corners
    for(int x=-n;x<=n;x++){int px=gW/2+x*sp; int x0=gW/2+(int)((px-gW/2)*0.18f); crk_line(x0,hz,px,gH,grc);} }
  int ts=(int)(3*s+0.5f); if(ts<3)ts=3; while(ts>3&&crk_txtW("OMEGAWARE",ts)>gW-24)ts--;
  int ss=(int)(s+0.5f); if(ss<1)ss=1;
  crk_txtShadow(gW/2,(int)(40*s),"OMEGAWARE",ts,CRK_RGB(49,232,255));
  crk_txtC(gW/2,(int)(40*s)+8*ts+10*ss,"RETRO FUTURE",ss,CRK_RGB(255,122,176));
  crk_scroller(t,CRK_RGB(255,79,160),8,false);
}

// ── 5.4.0: hidden custom cracktros (CRACKTRO=DENISE / CRACKTRO=WRANGLER) ──
static const char* CRK_SCROLL_DENISE="        OMEGAWARE PRESENTS ...  DENISE  ...  BUILT BY VINAY DHIR ...  PURPLE DREAMS ...  GREETINGS FROM THE GTi CREW ...  NOW GO LOAD A GAME ...        ";
static void crkDenise(float t){
  for(int y=0;y<gH;y++) gfx_fillRect(0,y,gW,1,crk_lerp(30,15,58, 13,6,26, (float)y/gH));
  crk_stars();
  int lx=gW/2, ly=gH/2-72;
  uint16_t GLOW=CRK_RGB(158,58,208), CREAM=CRK_RGB(244,238,225);
  crk_maskC(GTI_DENISE_BITS,GTI_DENISE_W,GTI_DENISE_H,lx-1,ly,GLOW);
  crk_maskC(GTI_DENISE_BITS,GTI_DENISE_W,GTI_DENISE_H,lx+1,ly,GLOW);
  crk_maskC(GTI_DENISE_BITS,GTI_DENISE_W,GTI_DENISE_H,lx,ly-1,GLOW);
  crk_maskC(GTI_DENISE_BITS,GTI_DENISE_W,GTI_DENISE_H,lx,ly+1,GLOW);
  crk_maskC(GTI_DENISE_BITS,GTI_DENISE_W,GTI_DENISE_H,lx,ly,CREAM);
  crk_txtC(gW/2, ly+GTI_DENISE_H+16, "by Vinay Dhir",2,CRK_RGB(234,223,247));
  crk_txtC(gW/2, ly+GTI_DENISE_H+42, "~ PURPLE DREAMS ~",1,CRK_RGB(180,143,230));
  crk_scrollerT(t,CRK_SCROLL_DENISE,CRK_RGB(230,95,224),9,false);
}
static const char* CRK_SCROLL_WRANGLER="        BIG RESPECT TO WRANGLER_AMIGA ...  THE MAN WHO SHOWED THE GTi TO THE WORLD ...  A GENUINE GAMECHANGER - HIS WORDS, NOT OURS ...  CHEERS FOR THE COVERAGE, LEGEND ...  FIND HIM ON YOUTUBE @ WRANGLER_AMIGA ...  NOW GO WRANGLE A FLOPPY ...        ";
static void crkWrangler(float t){
  for(int y=0;y<gH;y++) gfx_fillRect(0,y,gW,1,crk_lerp(44,92,156, 15,47,88, (float)y/gH));
  int lx=gW/2, ly=gH/2-66;
  uint16_t ORA=CRK_RGB(229,138,52), TAN=CRK_RGB(236,220,180);
  crk_maskC(GTI_WRANGLER_BITS,GTI_WRANGLER_W,GTI_WRANGLER_H,lx-1,ly,ORA);
  crk_maskC(GTI_WRANGLER_BITS,GTI_WRANGLER_W,GTI_WRANGLER_H,lx+1,ly,ORA);
  crk_maskC(GTI_WRANGLER_BITS,GTI_WRANGLER_W,GTI_WRANGLER_H,lx,ly-1,ORA);
  crk_maskC(GTI_WRANGLER_BITS,GTI_WRANGLER_W,GTI_WRANGLER_H,lx,ly+1,ORA);
  crk_maskC(GTI_WRANGLER_BITS,GTI_WRANGLER_W,GTI_WRANGLER_H,lx,ly,TAN);
  crk_txtC(gW/2, ly+GTI_WRANGLER_H+14, "@WRANGLER_AMIGA",2,CRK_RGB(244,164,78));
  int pw=(gW-100)<300?(gW-100):300; int px=gW/2-pw/2, py=ly+GTI_WRANGLER_H+40;
  gfx_fillRect(px,py,pw,24,CRK_RGB(91,61,34)); gfx_drawRect(px,py,pw,24,CRK_RGB(201,138,74));
  crk_txtC(gW/2, py+8, "A GENUINE GAMECHANGER",1,CRK_RGB(240,224,192));
  crk_scrollerT(t,CRK_SCROLL_WRANGLER,CRK_RGB(244,164,78),7,false);
}
// -- P4.9: hidden Retronaut cracktro (CRACKTRO=RETRONAUT) -- spins his colour logo.
//    Exclusive tie-in for the Retronaut video; logo used with permission.
static const char* CRK_SCROLL_RETRO="        OMEGAWARE x RETRONAUT ...  AN EXCLUSIVE FIRST LOOK FOR THE CHANNEL ...  CHEERS FOR THE VIDEO, LEGEND ...  LOGO FLOWN WITH PERMISSION ...  NOW GO LOAD A GAME ...        ";
static const char* CRK_SCROLL_OMEGA="   OMEGAWARE PRESENTS ... GOTEK TOUCHSCREEN INTERFACE ... THIS LOGO WAS DRAWN ON PAPER IN 1991 AND WAITED 35 YEARS FOR ITS CRACKTRO ... CODE BY MEZ AND DIMMY AND A WHOLE LOT OF CLAUDE ... GREETINGS FLY OUT TO MEZ - THE FLASHFLOPPY CREW - AND EVERYONE STILL SWAPPING DISKS ... KEEP THE AMIGA SPINNING ...      ";
static uint16_t* g_retro_buf=NULL; static int g_retro_w=0,g_retro_h=0;
static void retroLogoFree(){ if(g_retro_buf){free(g_retro_buf);g_retro_buf=NULL;} g_retro_w=g_retro_h=0; }
static bool retroLogoLoad(){
  if(g_retro_buf)return true;
  size_t sz=RETRO_LOGO_JPG_LEN;
  uint8_t* tmp=(uint8_t*)malloc(sz); if(!tmp)return false;
  memcpy_P(tmp,RETRO_LOGO_JPG,sz);
  if(!jpegdec.openRAM(tmp,sz,jpeg_buf_cb)){free(tmp);return false;}
  int jw=jpegdec.getWidth(),jh=jpegdec.getHeight();
  if(jw<=0||jh<=0){jpegdec.close();free(tmp);return false;}
  jpeg_tmp_buf=(uint16_t*)ps_malloc((size_t)jw*jh*2);
  if(!jpeg_tmp_buf){jpegdec.close();free(tmp);return false;}
  memset(jpeg_tmp_buf,0,(size_t)jw*jh*2); jpeg_tmp_w=jw; jpeg_tmp_h=jh;
  jpegdec.decode(0,0,0); jpegdec.close(); free(tmp);
  g_retro_buf=jpeg_tmp_buf; g_retro_w=jw; g_retro_h=jh; jpeg_tmp_buf=NULL;
  return true;
}
static void crkRetronaut(float t){
  gfx_fillScreen(TFT_BLACK);
  if(g_retro_buf&&g_retro_w>0&&g_retro_h>0){
    float ang=t*0.0026f;
    float sw=fabsf(cosf(ang)); if(sw<0.05f)sw=0.05f;
    int baseW=(int)(gW*0.84f);
    int dh=(int)((float)g_retro_h*baseW/g_retro_w);
    if(dh>gH-96){dh=gH-96; baseW=(int)((float)g_retro_w*dh/g_retro_h);}
    int dw=(int)(baseW*sw); if(dw<2)dw=2;
    int x0=gW/2-dw/2, y0=gH/2-dh/2-14;
    for(int yy=0;yy<dh;yy++){int sy=yy*g_retro_h/dh; if(sy>=g_retro_h)sy=g_retro_h-1;
      int vy=y0+yy; if(vy<0||vy>=gH)continue; const uint16_t* srow=&g_retro_buf[sy*g_retro_w];
      for(int xx=0;xx<dw;xx++){int sx=xx*g_retro_w/dw; if(sx>=g_retro_w)sx=g_retro_w-1;
        int vx=x0+xx; if(vx>=0&&vx<gW)fb_setPixel(vx,vy,srow[sx]);}}
  } else {
    crk_txtC(gW/2,gH/2-8,"RETRONAUT",4,CRK_RGB(95,224,255));
  }
  crk_scrollerT(t,CRK_SCROLL_RETRO,CRK_RGB(255,150,40),8,false);
}
// Boot cracktro runner. style: 1..7 forces a style, 0 = random pick each boot.
static void crkOmega(float t){
  gfx_fillScreen(CRK_RGB(6,8,20));
  crk_stars();
  // copper rasterbars, behind everything
  static const struct { uint8_t r,g,b; float spd, ph; } bars[4]={
    {255,60,60,0.0011f,0.0f},{60,200,255,0.0009f,2.1f},{200,90,255,0.0013f,4.2f},{255,200,60,0.0007f,1.1f}};
  for(int b=0;b<4;b++){
    int cy=(int)(gH*0.5f + sinf(t*bars[b].spd+bars[b].ph)*(gH*0.36f));
    for(int dy=-10;dy<=10;dy++){
      float k=1.0f-fabsf((float)dy)/11.0f;
      int yy=cy+dy;
      if(yy>=0&&yy<gH-30) gfx_hline(0,yy,gW,CRK_RGB((int)(bars[b].r*k),(int)(bars[b].g*k),(int)(bars[b].b*k)));
    }
  }
  // the logo: chrome gradient + 2px drop shadow (shadow first, per pixel,
  // later logo pixels legitimately overdraw it)
  const int lw=OMEGA_LOGO_W, lh=OMEGA_LOGO_H, lx=(gW-lw)/2, ly=26;
  for(int yy=0;yy<lh;yy++){
    float f=(float)yy/lh;
    uint16_t col = f<0.5f ? crk_lerp(238,242,255, 148,168,205, f*2.0f)
                          : crk_lerp(148,168,205, 228,234,246, (f-0.5f)*2.0f);
    const uint8_t*row=&OMEGA_LOGO[yy*OMEGA_LOGO_BPR];
    for(int xx=0;xx<lw;xx++)
      if(row[xx>>3]&(0x80>>(xx&7))){
        gfx_drawPixel(lx+xx+2,ly+yy+2,CRK_RGB(4,5,10));
        gfx_drawPixel(lx+xx,ly+yy,col);
      }
  }
  // the boing ball: tilted checker, floor shadow
  const int r=34, floorY=gH-44;
  int bx=gW/2+(int)(sinf(t*0.0014f)*(gW/2-r-8));
  int topY=ly+lh+10+r;
  int amp=(floorY-r)-topY; if(amp<20)amp=20;
  int by=floorY-r-(int)(fabsf(sinf(t*0.0035f))*amp);
  for(int yy=-4;yy<=4;yy++){
    int w=(int)(r*0.85f*sqrtf(1.0f-((float)yy/4.0f)*((float)yy/4.0f)));
    gfx_fillRect(bx-w+6,floorY+yy,2*w,1,CRK_RGB(3,4,9));
  }
  const float cell=r/3.0f, ph=fmodf(t*0.05f,cell*2.0f);
  const float cs=cosf(0.31f), sn=sinf(0.31f);
  for(int yy=-r;yy<=r;yy++){
    int hw=(int)sqrtf((float)(r*r-yy*yy));
    for(int xx=-hw;xx<=hw;xx++){
      float rx=xx*cs-yy*sn, ry=xx*sn+yy*cs;
      int cc=(((int)floorf((rx+ph)/cell))+((int)floorf(ry/cell)))&1;
      gfx_drawPixel(bx+xx,by+yy, cc?CRK_RGB(255,42,42):CRK_RGB(244,244,244));
    }
  }
  gfx_drawCircle(bx,by,r,CRK_RGB(110,0,0));
  crk_scrollerT(t,CRK_SCROLL_OMEGA,CRK_RGB(255,200,80),10,false);
}
static void drawCracktro(int style){
  bool omega=(style==7), denise=(style==8), wrangler=(style==9), retronaut=(style==10);   // 7=OMEGAWARE (shown); DENISE/WRANGLER/RETRONAUT hidden (name-only)
  int s=(style>=1&&style<=6)?(style-1):(int)(esp_random()%6);
  initStars();
  if(retronaut)retroLogoLoad();
  unsigned long startMs=millis();
  gfx_fillScreen(TFT_BLACK);gfx_flush();
  while(true){
    if(Touch_ReadFrame()){unsigned long t0=millis();while(Touch_ReadFrame()&&millis()-t0<500)delay(10);break;}
    if(!g_loop_cracktro&&millis()-startMs>=6000)break;
    float t=(float)(millis()-startMs);
    if(omega)crkOmega(t);
    else if(denise)crkDenise(t);
    else if(wrangler)crkWrangler(t);
    else if(retronaut)crkRetronaut(t);
    else switch(s){case 0:crkCopper(t);break;case 1:crkStarfield(t);break;case 2:crkRaster(t);break;
      case 3:crkPlasma(t);break;case 4:crkBoing(t);break;default:crkSynth(t);break;}
    if(((int)(t/450.0f))%2) crk_txtC(gW/2,gH-46,"TAP TO CONTINUE",1,CRK_RGB(150,168,200));
    gfx_flush();delay(6);
  }
  if(retronaut)retroLogoFree();
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
// lab16b-P4 NEO top bar (40 px): (omega) GTi with the firmware version under it | library + games (tap = switch
// library, or open the categories) | CABLE/DONGLE state chip | gear (opens / closes Settings)
#define NEO_GEAR_W 44   // tap width of the gear, top-right
static int g_neo_ctr_x0=0,g_neo_ctr_x1=0;   // the tappable library text (list view only; 0/0 = none)
static String neoThousands(uint32_t v){ String s=String(v); for(int i=(int)s.length()-3;i>0;i-=3)s=s.substring(0,i)+","+s.substring(i); return s; }
// Print a string where every '|' becomes a drawn middle dot (the 6x8 font has none). Width: '|' counts as 3 characters.
static int neoDotWidth(const String&s){ int n=0; for(unsigned i=0;i<s.length();i++)n+=(s[i]=='|')?3:1; return n*6*text_size; }
static void neoDotPrint(const String&s,uint16_t col){
  int seg0=0;
  for(int i=0;i<=(int)s.length();i++){
    if(i==(int)s.length()||s[i]=='|'){
      if(i>seg0)gfx_print(s.substring(seg0,i));
      if(i<(int)s.length()){ int dx=text_x+9*text_size, dy=text_y+4*text_size-1; gfx_fillCircle(dx,dy,text_size>1?2:1,col); text_x+=18*text_size; }
      seg0=i+1; } }
}
// lab5-P4G7 (Mez): the OMEGAWARE omega, drawn in the 6x8 font's own pixel grid (7 rows = the capital height) at the current
//   text size and cursor, bold like the NEO text; advances the cursor one glyph cell.
static void gfx_printOmega(uint16_t col){
  static const uint8_t OM[7]={0x1C,0x22,0x41,0x41,0x41,0x22,0x63};   // ..###.. .#...#. #.....# x3 .#...#. ##...##
  const int s=text_size, bw=s+((g_neo_depth>0&&s>=2)?1:0);
  for(int r=0;r<7;r++)for(int c=0;c<7;c++)if(OM[r]&(0x40>>c))gfx_fillRect(text_x+c*s,text_y+r*s,bw,s,col);
  text_x+=8*s;
}
static void neoStatusBar(){
  gfx_fillRect(0,0,VW,STATUS_H,COL_BAR); gfx_hline(0,STATUS_H-1,VW,COL_SEP);
  gfx_setTextSize(2); gfx_setCursor(12,5); gfx_printOmega(COL_AMBER);   // lab5-P4G7: was "GOTEK" - Gotek isn't our brand, GTi already says it
  gfx_setTextColor(TFT_WHITE,COL_BAR); gfx_print(" GTi");
  int leftEnd=text_x;
  gfx_setTextSize(1); gfx_setTextColor(COL_DIM,COL_BAR); gfx_setCursor(12,STATUS_H-13); gfx_print(FW_VERSION);
  if(text_x>leftEnd)leftEnd=text_x;
  leftEnd+=12;
  neoGear(VW-22,STATUS_H/2,TFT_WHITE,COL_BAR);
  // state chip: same logic as the classic bar (cable: g_loaded; wireless: the dongle's heartbeat)
  String lw; uint16_t lc; bool fill;
  { bool wl=false;   // lab2-P4G7: the P4 cannot hear the dongle's heartbeat (no ESP-NOW receive) - show what the GTi sent, never OFFLINE
    bool alive = wl ? (millis()-g_espnow_xiao_last_seen < 8000UL) : true;
    bool ld, desync=false;
    if(wl){ ld=alive&&g_dongle_loaded; desync=g_loaded&&(!alive||!g_dongle_loaded); }
    else  { ld=g_loaded; }
    if(wl&&!alive){ lw="OFFLINE"; lc=0xFD20; fill=false; }
    else if(desync){ lw="DISK?"; lc=0xFD20; fill=false; }
    else if(ld){ bool multi=(g_loaded&&g_loaded_game_idx>=0&&g_loaded_game_idx<(int)g_games.size()&&g_games[g_loaded_game_idx].disk_count>1);
                 lw=multi?("DISK "+String(g_loaded_disk_idx+1)):String("DISK"); lc=COL_GREEN; fill=true; }
    else { lw="EMPTY"; lc=COL_DIM; fill=false; } }
  String chip=String(g_wireless_mode?"DONGLE ":"CABLE ")+lw;
  gfx_setTextSize(1); int ch=26, cw=gfx_textWidth(chip)+34, cx=VW-NEO_GEAR_W-6-cw, cy=(STATUS_H-ch)/2;
  gfx_fillRoundRect(cx,cy,cw,ch,ch/2,COL_PANEL); gfx_strokeRoundRect(cx,cy,cw,ch,ch/2,COL_SEP);
  if(fill) gfx_fillCircle(cx+13,STATUS_H/2,4,lc); else gfx_drawCircle(cx+13,STATUS_H/2,4,lc);
  gfx_setTextColor(lc==COL_DIM?COL_LIT:lc,COL_PANEL); gfx_setCursor(cx+23,(STATUS_H-8)/2); gfx_print(chip);
  // centre: Settings title, or library + game count
  g_neo_ctr_x0=g_neo_ctr_x1=0;
  String ct; bool tappable=false;
  if(g_info_showing) ct=g_info_test?String(T(L_SETTINGS))+" > TEST TOOLS":String(T(L_SETTINGS));
  else {
    String lib=g_mode==MODE_ADF?"ADF":g_mode==MODE_DSK?"DSK":"GEN";
    if(g_categories){ if(g_libpath.length()){ String c=g_libpath; while(c.endsWith("/"))c.remove(c.length()-1); int s=c.lastIndexOf('/'); if(s>=0)c=c.substring(s+1); lib="< "+c; } else lib="CATEGORIES"; }
    ct=lib+"|"+neoThousands((uint32_t)g_games.size())+" games"; tappable=!g_car_active; }
  for(int sz=2;sz>=1;sz--){ gfx_setTextSize(sz); int tw=neoDotWidth(ct), tx=(VW-tw)/2;
    if(tx<leftEnd)tx=leftEnd+((cx-10-leftEnd)-tw)/2;
    if(tx>=leftEnd&&tx+tw<cx-10){ gfx_setTextColor(TFT_WHITE,COL_BAR); gfx_setCursor(tx,(STATUS_H-8*sz)/2); neoDotPrint(ct,TFT_WHITE);
      if(tappable){ g_neo_ctr_x0=tx; g_neo_ctr_x1=tx+tw; } break; } }
}
static void drawStatusBar(){
  NeoScope _ns; if(g_neo){ neoStatusBar(); return; }   // lab16a-P4
  gfx_fillRect(0,0,VW,STATUS_H,COL_BAR);gfx_setTextSize(1);
  gfx_setTextColor(COL_ORANGE,COL_BAR);gfx_setCursor(6,6);gfx_print("OMEGAWARE");
  gfx_setTextColor(COL_MID,COL_BAR);gfx_print("  " FW_VERSION);
  if(g_wireless_mode){gfx_setTextColor(espnowIsPaired()?0x07E0:0xFD20,COL_BAR);gfx_setCursor(VW/2-40,6);gfx_print(espnowIsPaired()?"WIRELESS:PAIRED":"WIRELESS:PAIR");}
  else{gfx_setTextColor(0x07FF,COL_BAR);int tw=gfx_textWidth(T(L_STANDALONE));gfx_setCursor((VW-tw)/2,6);gfx_print(T(L_STANDALONE));}
  // v5.7.x: load-status indicator (top-right). Standalone reads g_loaded; wireless reads
  // the dongle's heartbeat so it reflects reality. green=disk present, dim=empty,
  // amber=OFFLINE (no beacon ~8s) or DISK? (GTi thinks loaded but the dongle disagrees).
  { bool wl=(g_wireless_mode&&g_espnow_started);
    bool alive = wl ? (millis()-g_espnow_xiao_last_seen < 8000UL) : true;
    bool ld, desync=false;
    if(wl){ ld=alive&&g_dongle_loaded; desync=g_loaded&&(!alive||!g_dongle_loaded); }
    else  { ld=g_loaded; }
    String lw; uint16_t lc; bool fill;
    if(wl&&!alive){ lw="OFFLINE"; lc=0xFD20; fill=false; }
    else if(desync){ lw="DISK?"; lc=0xFD20; fill=false; }
    else if(ld){ bool multi=(g_loaded&&g_loaded_game_idx>=0&&g_loaded_game_idx<(int)g_games.size()&&g_games[g_loaded_game_idx].disk_count>1);
                 lw=multi?("DISK "+String(g_loaded_disk_idx+1)):String("DISK"); lc=COL_GREEN; fill=true; }
    else { lw="EMPTY"; lc=COL_DIM; fill=false; }
    gfx_setTextSize(1); int lww=gfx_textWidth(lw); int wx=VW-6-lww, cx=wx-9;
    if(fill) gfx_fillCircle(cx,STATUS_H/2,3,lc); else gfx_drawCircle(cx,STATUS_H/2,3,lc);
    gfx_setTextColor(lc,COL_BAR); gfx_setCursor(wx,6); gfx_print(lw);
  }
}

// disk grid geometry (landscape cover) — shared by draw + touch (struct declared up top)
static DiskGrid diskGrid(int nd){DiskGrid L;L.pages=(nd+DISKS_PER_PAGE-1)/DISKS_PER_PAGE;if(g_disk_page>=L.pages)g_disk_page=0;
  L.pageStart=g_disk_page*DISKS_PER_PAGE;L.pageEnd=min(L.pageStart+DISKS_PER_PAGE,nd);L.COLS=3;L.dgap=8;L.dbw=(COVER_W-16-(L.COLS-1)*L.dgap)/L.COLS;L.dbh=34;   // lab15n P4: 86x34 on the 292 px panel (was 44x20, the 3.5" size)
  L.gridW=L.COLS*L.dbw+(L.COLS-1)*L.dgap;L.gx=max(4,(COVER_W-L.gridW)/2);L.multiPage=(L.pages>1);
  L.pageBtnH=L.multiPage?28:0;L.pageGap=L.multiPage?6:0;L.gridH=2*L.dbh+L.dgap;L.labelY=INS_Y-L.gridH-L.pageBtnH-L.pageGap-16;L.gridY=L.labelY+12;return L;}
static void drawDiskGrid(int nd){DiskGrid L=diskGrid(nd);
  gfx_setTextSize(1);gfx_setTextColor(COL_DIM,COL_PANEL);gfx_setCursor(4,L.labelY);
  gfx_print(L.multiPage?("DISK ("+String(g_disk_page+1)+"/"+String(L.pages)+"):"):"DISK:");
  for(int d=L.pageStart;d<L.pageEnd;d++){int slot=d-L.pageStart,col=slot%L.COLS,row=slot/L.COLS;int bx=L.gx+col*(L.dbw+L.dgap),by=L.gridY+row*(L.dbh+L.dgap);
    bool isSel=d==g_disk_sel,isLd=(g_loaded_game_idx==g_sel&&g_loaded_disk_idx==d);uint16_t bc=isLd?COL_GREEN:(isSel?COL_AMBER:(g_neo?COL_CIRC:COL_BAR));
    gfx_fillRoundRect(bx,by,L.dbw,L.dbh,4,bc);gfx_drawRoundRect(bx,by,L.dbw,L.dbh,4,isSel?COL_AMBER:COL_DIM);
    gfx_setTextSize(2);gfx_setTextColor(isLd||isSel?TFT_BLACK:COL_LIT,bc);String dl="D"+String(d+1);gfx_setCursor(bx+(L.dbw-gfx_textWidth(dl))/2,by+(L.dbh-16)/2);gfx_print(dl);gfx_setTextSize(1);}   // lab15n P4: size 2
  if(L.multiPage){int pby=L.gridY+L.gridH+L.pageGap;gfx_fillRoundRect(L.gx,pby,L.gridW,L.pageBtnH,6,COL_ACCENT);gfx_setTextSize(2);gfx_setTextColor(TFT_WHITE,COL_ACCENT);
    String pl=(g_disk_page+1<L.pages)?("MORE D"+String(L.pageEnd+1)+"+  >"):("<  BACK TO D1");gfx_setCursor(L.gx+(L.gridW-gfx_textWidth(pl))/2,pby+(L.pageBtnH-16)/2);gfx_print(pl);gfx_setTextSize(1);}}
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

// lab16b-P4: NEO game panel - cover top-left (gold frame + glow, green when in the drive), title / disks /
// format / FAV / MANUAL to its right, the description under it (tap = the whole .nfo), DISK pills at the bottom.
static int g_neo_fav_bx=0,g_neo_fav_by=0,g_neo_fav_bw=0,g_neo_fav_bh=0;   // FAV pill (tap = favourite on/off)
static int g_nd_n=0,g_nd_x=0,g_nd_y=0,g_nd_pb=0,g_nd_h=0,g_nd_first=0,g_nd_more_x=-1;   // DISK pills: shown count, first pill x, pitch, first disk, ">" pill x
static void neoInfoPanel(int gi,const String&blurb,bool hasSav,bool hd,const String&manual){
  auto&game=g_games[gi];
  const int X=COVER_X,Y=COVER_Y,W=COVER_W,H=COVER_H;
  bool isL=g_loaded&&g_loaded_game_idx==gi;
  fillBack(X,Y,W,H); gfx_fillRoundRect(X,Y,W,H,12,COL_PANEL); gfx_strokeRoundRect(X,Y,W,H,12,COL_SEP);
  g_manual_bw=0; g_nfo_bw=0; g_neo_fav_bw=0; g_nd_n=0; g_nd_more_x=-1;
  // lab16d-P4: BIG cover - the tile's picture (letterbox cut off) as big as the box allows, centred; else the JPEG; else the initial
  int AX=COVER_ART_X,AY=COVER_ART_Y,AW=COVER_ART_W,AH=COVER_ART_H, ax=AX,ay=AY,aw=AW,ah=AH;
  uint16_t*t=NULL; int px=0,py=0,pw=0,ph=0;
  bool art=game.jpg_path.length()>0&&game.jpg_path!="?";
  if(art&&g_listtile){ bool ok=false; t=carTileEx(gi,true,&ok); if(!(t&&ok))t=NULL;
    if(t){ tilePicRect(t,CAR_TILE,px,py,pw,ph); int dw=AW,dh=(ph*AW)/pw; if(dh>AH){dh=AH;dw=(pw*AH)/ph;} if(dw<1)dw=1; if(dh<1)dh=1; aw=dw; ah=dh; ax=AX+(AW-aw)/2; } }
  if(!t&&!art){ aw=min(AW,AH*4/3); ax=AX+(AW-aw)/2; }   // no picture: a 4:3 slate box with the initial
  uint16_t fc=isL?COL_GREEN:COL_AMBER;
  neoGlow(ax-4,ay-4,aw+8,ah+8,8,fc,6);
  gfx_fillRect(ax,ay,aw,ah,COL_BAR);
  bool drew=false;
  if(t){ g_cb_sx0=px;g_cb_sy0=py;g_cb_sw=pw;g_cb_sh=ph; carBlit(t,CAR_TILE,ax+aw/2,ay+ah/2,aw,ah,0); g_cb_sw=0;g_cb_sh=0; drew=true; }
  else if(art) drew=gfx_drawJpgFile(game.jpg_path,ax,ay,aw,ah);
  if(!drew){ char ib[2]={(char)toupper(game.name.charAt(0)),0}; gfx_setTextSize(8); gfx_setTextColor(COL_LIT,COL_BAR); gfx_setCursor(ax+(aw-48)/2,ay+(ah-64)/2); gfx_print(ib); }
  neoFrame(ax-1,ay-1,aw+2,ah+2,3,fc,3);
  // corners: save (top-left), FAV star (top-right, tap), A500-can't (bottom-left), HD (bottom-right); book = left edge, middle (as before)
  if(hasSav)drawSaveFloppy(ax+5,ay+5);
  { int cx=ax+aw-20, cy=ay+20; gfx_fillCircle(cx,cy,15,COL_PANEL); gfx_drawCircle(cx,cy,15,game.fav?COL_AMBER:COL_SEP); gfx_drawCircle(cx,cy,14,game.fav?COL_AMBER:COL_SEP);
    gfx_fillStar(cx,cy,9.0f,game.fav?COL_STAR:COL_CIRC);
    g_neo_fav_bx=ax+aw-44; g_neo_fav_by=ay; g_neo_fav_bw=44; g_neo_fav_bh=44; }
  if(hd){drawHDChip(ax+aw-26,ay+ah-18);drawNoA500(ax+18,ay+ah-18,13,TFT_RED);}
  if(manual.length()){ g_manual_bw=30;g_manual_bh=26;g_manual_bx=ax+4;g_manual_by=ay+(ah-g_manual_bh)/2;g_manual_path=manual;
    gfx_fillRoundRect(g_manual_bx,g_manual_by,g_manual_bw,g_manual_bh,4,COL_ACCENT);gfx_drawRoundRect(g_manual_bx,g_manual_by,g_manual_bw,g_manual_bh,4,COL_AMBER);
    drawBookIcon(g_manual_bx+g_manual_bw/2,g_manual_by+g_manual_bh/2,COL_LIT); }
  // title under the cover: one line of size 3, else up to two lines of size 2; then "n disks - ADF"
  int ry=ay+ah+12;
  { const int tw=W-24; String nm=game.name; gfx_setTextSize(3);
    g_clip_x1=X+W-6;
    if(gfx_textWidth(nm)<=tw){ gfx_setTextColor(TFT_WHITE,COL_PANEL); gfx_setCursor(X+12,ry); gfx_print(nm); ry+=28; }
    else { gfx_setTextSize(2); ry=drawWrapped(X+12,ry,nm,tw,22,2,Y+H-20,TFT_WHITE,COL_PANEL); }
    g_clip_x1=gW; }
  { gfx_setTextSize(1); gfx_setCursor(X+12,ry); gfx_setTextColor(COL_DIM,COL_PANEL);
    String meta=(game.disk_count>1?String(game.disk_count)+" disks":String("1 disk"))+"|"+(g_mode==MODE_ADF?(hd?"ADF HD":"ADF"):g_mode==MODE_DSK?"DSK":"GEN");
    if(hasSav)meta+="|save";
    neoDotPrint(meta,COL_DIM); ry+=14; }
  // DISK pills (bottom-right): pages of (fit-1) + a ">" pill when they do not all fit
  int nd=game.disk_count, rowY=Y+H-12-30, descBottom=Y+H-12;
  if(nd>1){
    const int ph2=30, pb=42, lblW=40, cntW=40; int fit=(W-24-lblW-cntW)/pb; if(fit<2)fit=2;
    int per=(nd<=fit)?nd:fit-1, pages=(nd+per-1)/per; if(g_disk_page>=pages)g_disk_page=0;
    int first=(nd<=fit)?0:g_disk_page*per, shown=min(per,nd-first), slots=shown+((nd>fit)?1:0);
    int total=lblW+slots*pb+cntW, x0=X+W-12-total;
    gfx_setTextSize(1); gfx_setTextColor(COL_DIM,COL_PANEL); gfx_setCursor(x0,rowY+(ph2-8)/2); gfx_print("DISK");
    int dx0=x0+lblW;
    g_nd_n=shown; g_nd_x=dx0; g_nd_y=rowY; g_nd_pb=pb; g_nd_h=ph2; g_nd_first=first;
    for(int k=0;k<shown;k++){ int d=first+k, bx=dx0+k*pb;
      bool dLd=(isL&&g_loaded_disk_idx==d), dSel=(d==g_disk_sel);
      uint16_t bc=dLd?COL_NOW:(dSel?COL_AMBER:COL_CIRC), oc=dLd?COL_GREEN:(dSel?(uint16_t)0xFEB0:COL_SEP), tc=dLd?COL_GREEN:(dSel?TFT_BLACK:TFT_WHITE);
      gfx_fillRoundRect(bx,rowY,pb-8,ph2,9,bc); neoFrame(bx,rowY,pb-8,ph2,9,oc,2);
      String dl=String(d+1); gfx_setTextSize(dl.length()>2?1:2); gfx_setTextColor(tc,bc); gfx_setCursor(bx+(pb-8-gfx_textWidth(dl))/2,rowY+(ph2-8*text_size)/2); gfx_print(dl); }
    if(nd>fit){ int bx=dx0+shown*pb; g_nd_more_x=bx; gfx_fillRoundRect(bx,rowY,pb-8,ph2,9,COL_ACCENT); gfx_setTextSize(2); gfx_setTextColor(TFT_WHITE,COL_ACCENT); gfx_setCursor(bx+(pb-8-12)/2,rowY+7); gfx_print(">"); }
    gfx_setTextSize(1); gfx_setTextColor(COL_DIM,COL_PANEL); gfx_setCursor(dx0+slots*pb+4,rowY+(ph2-8)/2); gfx_print(String(g_disk_sel+1)+"/"+String(nd));
    descBottom=rowY-8;
  }
  // description under the title (tap = the whole .nfo)
  int dy=ry+6;
  if(blurb.length()>0&&dy+12<=descBottom){
    gfx_setTextSize(1); int lh=13, maxL=(descBottom-dy-lh)/lh; if(maxL<1)maxL=1;
    int te=drawWrapped(X+12,dy,blurb,W-24,lh,maxL,descBottom-lh,COL_LIT,COL_PANEL);
    if(te+8<=descBottom){ gfx_setTextColor(COL_BLUE,COL_PANEL); gfx_setCursor(X+12,te+2); gfx_print("Tap here to read the whole .nfo"); te+=lh; }
    g_nfo_bx=X; g_nfo_by=dy-4; g_nfo_bw=W; g_nfo_bh=te-dy+6; }
  drawBottomBar();   // the INSERT pill names the chosen disk
}
static void drawCoverPanel(){
  NeoScope _ns;
  g_manual_bw=0;   // v4.9.2: cleared each draw; set below only if this game has a .rtfm
  g_nfo_bw=0;      // lab15j: same for the .nfo text tap area (set only when a description is drawn)
  if(!COVER_ON)return;
  if(g_neo){ g_neo_fav_bw=0; g_nd_n=0; if(g_games.empty()){ fillBack(COVER_X,COVER_Y,COVER_W,COVER_H); gfx_fillRoundRect(COVER_X,COVER_Y,COVER_W,COVER_H,12,COL_PANEL); gfx_strokeRoundRect(COVER_X,COVER_Y,COVER_W,COVER_H,12,COL_SEP); drawBottomBar(); return; } }
  else
  gfx_fillRect(COVER_X,COVER_Y,COVER_W,COVER_H,COL_PANEL);if(g_games.empty())return;
  auto&game=g_games[g_sel];
  if(!game.jpg_path.length()){String jpg;if(findJPGFor(g_files[game.first_file_idx],jpg))game.jpg_path=jpg;else game.jpg_path="?";}
  static int lastNfoSel=-1;static String cachedNfoBlurb="";static bool cachedHasSav=false;static bool cachedHD=false;static String cachedManual="";
  if(lastNfoSel!=g_sel){lastNfoSel=g_sel;cachedNfoBlurb="";cachedManual="";
    const String _fp=g_files[game.first_file_idx];
    if(game.nfo_done){                       // 5.9.31-lab1: harvested or loaded from .nfocache — ZERO directory walks
      cachedNfoBlurb=game.blurb;
      cachedHD=(g_mode==MODE_ADF)&&game.is_hd;
      if(game.has_manual){String mp;if(manualFor(_fp,mp))cachedManual=mp;}   // only resolve a path when one actually exists (and it hits early)
    }else{                                   // fallback: no harvest, no cache (pre-lab1 card) — old behaviour, minus the byte-at-a-time read
      String nfoP,nT,nB;
      if(findNFOFor(_fp,nfoP)){File nf=SD_MMC.open(nfoP,FILE_READ);if(nf){
        char _nb[513];int _nr=nf.read((uint8_t*)_nb,512);if(_nr<0)_nr=0;_nb[_nr]=0;nf.close();String txt(_nb);parseNFO(txt,nT,nB);
        if(nT.length()&&game.name==basenameNoExt(filenameOnly(_fp)))game.name=nT;cachedNfoBlurb=nB;}}
      cachedHD=(g_mode==MODE_ADF)&&isHDImage(_fp);
      {String mp;if(manualFor(_fp,mp))cachedManual=mp;}
      game.blurb=cachedNfoBlurb;game.has_manual=cachedManual.length()>0;game.is_hd=cachedHD;game.nfo_done=true;   // remember for the session even without a cache file
    }
    cachedHasSav=savBadgeFor(_fp);}   // lab14i: from the save list (kept current when the GTi writes a save) - no card lookup
  if(g_neo){ neoInfoPanel(g_sel,cachedNfoBlurb,cachedHasSav,cachedHD,cachedManual); return; }   // lab16b-P4
  // Cover art
  int ax=COVER_ART_X,ay=COVER_ART_Y,aw=COVER_ART_W,ah=COVER_ART_H;   // lab15l: the frame; shrinks to hug a tile picture
  gfx_fillRoundRect(COVER_ART_X,COVER_ART_Y,COVER_ART_W,COVER_ART_H,5,COL_BAR);
  if(g_neo)neoFrame(COVER_ART_X-1,COVER_ART_Y-1,COVER_ART_W+2,COVER_ART_H+2,3,COL_AMBER,3); else   // lab16a-P4: gold frame
  gfx_drawRoundRect(COVER_ART_X-1,COVER_ART_Y-1,COVER_ART_W+2,COVER_ART_H+2,6,COL_ACCENT);
  {bool _drew=false; uint32_t _cv0=micros();
   if(game.jpg_path.length()>0&&game.jpg_path!="?"){
     if(g_listtile){            // 5.9.34-lab4: 45 KB pre-decoded tile instead of a fresh ~500 KB JPEG decode, every single selection change
       bool _ok=false; uint16_t*_t=carTileEx(g_sel,true,&_ok);
       if(_t&&_ok){
         // lab15l: cut the tile's letterbox off and fit the picture itself as big as the frame allows (was: the whole
         // square tile, so a 4:3 cover showed at ~92x69 inside a 142x96 frame). Then the frame is redrawn around it.
         int px,py,pw,ph; tilePicRect(_t,CAR_TILE,px,py,pw,ph);
         int bw=COVER_ART_W-4,bh=COVER_ART_H-4,dw=bw,dh=(ph*bw)/pw; if(dh>bh){dh=bh;dw=(pw*bh)/ph;} if(dw<1)dw=1; if(dh<1)dh=1;
         aw=dw+4;ah=dh+4;ax=COVER_ART_X+(COVER_ART_W-aw)/2;ay=COVER_ART_Y+(COVER_ART_H-ah)/2;
         gfx_fillRect(COVER_ART_X-4,COVER_ART_Y-4,COVER_ART_W+8,COVER_ART_H+8,COL_PANEL);
         gfx_fillRoundRect(ax,ay,aw,ah,5,COL_BAR); if(g_neo)neoFrame(ax-1,ay-1,aw+2,ah+2,3,COL_AMBER,3); else gfx_drawRoundRect(ax-1,ay-1,aw+2,ah+2,6,COL_ACCENT);
         g_cb_sx0=px;g_cb_sy0=py;g_cb_sw=pw;g_cb_sh=ph;
         carBlit(_t,CAR_TILE,ax+aw/2,ay+ah/2,dw,dh,0);
         g_cb_sw=0;g_cb_sh=0;_drew=true;}
     }
     if(!_drew)_drew=gfx_drawJpgFile(game.jpg_path,COVER_ART_X+2,COVER_ART_Y+2,COVER_ART_W-4,COVER_ART_H-4);
   }
   if(g_reelprof)gLog("[cover] %s art %luus for %s\n",g_listtile?"tile":"jpeg",(unsigned long)(micros()-_cv0),game.name.c_str());
   if(!_drew){char ib[2]={(char)toupper(game.name.charAt(0)),0};gfx_setTextSize(2);gfx_setTextColor(COL_LIT,COL_BAR);gfx_setCursor(COVER_ART_X+COVER_ART_W/2-6,COVER_ART_Y+COVER_ART_H/2-8);gfx_print(ib);}}
  // v4.8.0: floppy icon — this game has a save-copy (INSERT will boot the save)
  if(cachedHasSav)drawSaveFloppy(ax+3,ay+3);                     // lab15l: markers follow the (possibly smaller) frame
  if(cachedHD){drawHDChip(ax+aw-23,ay+3);drawNoA500(ax+15,ay+ah-15,13,TFT_RED);}   // v4.9 HD markers
  if(cachedManual.length()){   // v4.9.2: book button, bottom-right of the cover art — only when a .rtfm exists
    g_manual_bw=26;g_manual_bh=22;g_manual_bx=ax+3;g_manual_by=ay+(ah-g_manual_bh)/2;g_manual_path=cachedManual;   // v4.9.3: bigger + left edge, clear of the fav/HD corners
    gfx_fillRoundRect(g_manual_bx,g_manual_by,g_manual_bw,g_manual_bh,3,COL_ACCENT);gfx_drawRoundRect(g_manual_bx,g_manual_by,g_manual_bw,g_manual_bh,3,COL_AMBER);
    drawBookIcon(g_manual_bx+g_manual_bw/2,g_manual_by+g_manual_bh/2,COL_LIT);
  }
  bool isL=g_loaded&&g_loaded_game_idx==g_sel;
  if(!g_portrait){
    int cb;
    if(game.disk_count>1){DiskGrid L=diskGrid(game.disk_count);cb=L.labelY-2;}
    else cb=INS_Y-2;
    int psz=g_font+1;int bsz=(psz>1?psz-1:1);int ty=COVER_ART_Y+COVER_ART_H+6;gfx_setTextSize(psz);   // P4: title tracks FONT; blurb one step smaller
    ty=drawWrapped(6,ty,game.name,COVER_W-12,10*psz,2,cb,COL_LIT,COL_PANEL);
    gfx_setTextSize(bsz);
    if(cachedNfoBlurb.length()>0){int te=drawWrapped(6,ty+2,cachedNfoBlurb,COVER_W-12,9*bsz+1,16,cb,COL_DIM,COL_PANEL);
      g_nfo_bx=COVER_X;g_nfo_by=COVER_ART_Y+COVER_ART_H+2;g_nfo_bw=COVER_W;g_nfo_bh=max(min(te+2,cb),g_nfo_by+20)-g_nfo_by;}   // lab15j: title + description = tap target
    if(game.disk_count>1)drawDiskGrid(game.disk_count);
  }else{
    int rx=COVER_ART_X+COVER_ART_W+8,rw=VW-rx-6;int ty=COVER_ART_Y;gfx_setTextSize(1);
    ty=drawWrapped(rx,ty,game.name,rw,10,3,COVER_ART_Y+COVER_ART_H,COL_LIT,COL_PANEL);
    if(cachedNfoBlurb.length()>0){drawWrapped(rx,ty+3,cachedNfoBlurb,rw,9,6,COVER_ART_Y+COVER_ART_H+2,COL_DIM,COL_PANEL);
      g_nfo_bx=rx-4;g_nfo_by=COVER_ART_Y;g_nfo_bw=VW-g_nfo_bx;g_nfo_bh=COVER_ART_H+2;}   // lab15j: text beside the cover = tap target
    if(game.disk_count>1)drawDiskStepper(8,COVER_Y+COVER_H-70,VW-16,26,game.disk_count);   // full-width disk row above INSERT
    else{gfx_setTextSize(1);gfx_setTextColor(cachedHD?COL_ORANGE:COL_DIM,COL_PANEL);gfx_setCursor(12,COVER_Y+COVER_H-58);gfx_print(cachedHD?"HD 1.76MB - needs A3000/A4000":g_mode==MODE_ADF?"Single disk  -  ADF 880KB":g_mode==MODE_DSK?"Single disk  -  DSK":"Single disk");}
  }
  // INSERT/EJECT
  if(g_neo){   // lab16a-P4: cyan INSERT pill (names the disk when it is not disk 1), red EJECT
    String il=isL?String(T(L_EJECT)):(game.disk_count>1&&g_disk_sel>0?String(T(L_INSERT))+" "+String(g_disk_sel+1):String(T(L_INSERT)));
    neoPill(INS_X,INS_Y,INS_W,INS_H,isL?(uint16_t)0xC1A6:COL_BLUE,il,false); return; }
  gfx_fillRoundRect(INS_X,INS_Y,INS_W,INS_H,8,isL?(uint16_t)0x4000:(uint16_t)0x0340);
  gfx_drawRoundRect(INS_X,INS_Y,INS_W,INS_H,8,isL?(uint16_t)0xE8C4:COL_GREEN);
  gfx_setTextSize(2);gfx_setTextColor(TFT_WHITE,isL?(uint16_t)0x4000:(uint16_t)0x0340);
  const char*lbl=isL?T(L_EJECT):T(L_INSERT);int tw=gfx_textWidth(lbl);gfx_setCursor(INS_X+(INS_W-tw)/2,INS_Y+(INS_H-16)/2);gfx_print(lbl);
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
  gfx_setTextSize(2);gfx_setTextColor(isL?TFT_WHITE:TFT_BLACK,isL?(uint16_t)0x4000:COL_GREEN);const char*lbl=isL?T(L_EJECT):T(L_INSERT);int tw=gfx_textWidth(lbl);gfx_setCursor(INS_X+(INS_W-tw)/2,INS_Y+(INS_H-16)/2);gfx_print(lbl);
}

// INFO / SETTINGS panel — left column (landscape) or full width (portrait). Stores button Ys for touch.
// ── v5.5.4: full-screen paginated INFO/settings model ──
enum { IA_NEOUI=41, IA_USRDSK=40, IA_NONE=0, IA_MODE, IA_FONT, IA_THEME, IA_LANG, IA_ROTATE, IA_COMPACT, IA_DONGLE, IA_HIVEMIND, IA_RESCAN, IA_RESET, IA_DIAG, IA_SDACCESS, IA_FWUPDATE, IA_LIBMODE, IA_CATEG, IA_BTNSTYLE, IA_SSMODE, IA_SSFAV, IA_LINK, IA_HOMEWIFI, IA_WEBUI, IA_WIFICHECK, IA_SAVER, IA_CRACKTRO, IA_DIAGDISP, IA_REELBORDER, IA_LASTUSED, IA_NOCACHE, IA_COVERS, IA_REELPROF, IA_LISTTILE, IA_SDSOAK, IA_TESTPAGE, IA_TESTBACK };
struct InfoItem { char lbl[32]; uint16_t bg,fg; uint8_t act; };
static InfoItem g_ii[32]; static int g_ii_n=0;
struct InfoRect { int x,y,w,h; uint8_t act; };
static InfoRect g_ir[20]; static int g_ir_n=0;
static int g_info_page=0, g_info_pages=1;
static void drawInfoFull();   // paginated settings + INFO bottom bar + flush
// v5.6.7: readable ink for a key's colour on the dim fill — dark key colours
// (COL_BAR OFF-states, the dark-red RESET) get promoted to light grey so they
// don't vanish; bright colours keep their hue.
static inline uint16_t keyInk(uint16_t c){
  int r=(c>>11)&31, g=(c>>5)&63, b=c&31;
  return ((r*2+g*3+b) < 60) ? COL_LIT : c;
}
static uint8_t g_info_last=0;   // lab16a-P4: the last Settings option tapped (NEO outlines it in gold)
static void drawInfoPanel(){
  NeoScope _ns;
  // v5.5.4: full-screen, single-column, paginated. Build the item list (dynamic
  // labels + conditional rows), then draw only the current page's buttons and
  // record their rects in g_ir[] so the tap handler hits exactly what's drawn.
  g_ii_n=0;
  auto add=[&](const String&l,uint16_t bg,uint16_t fg,uint8_t act){
    if(g_ii_n>=32)return; strncpy(g_ii[g_ii_n].lbl,l.c_str(),31); g_ii[g_ii_n].lbl[31]=0;
    g_ii[g_ii_n].bg=bg; g_ii[g_ii_n].fg=fg; g_ii[g_ii_n].act=act; g_ii_n++; };
  // lab14k: TEST TOOLS sub-page - the tools for testing the GTi itself, kept off the main Settings list.
  if(g_info_test){
    if(!g_neo)add("< BACK TO SETTINGS", COL_ACCENT, TFT_WHITE, IA_TESTBACK);   // lab16b-P4: NEO has BACK in the bottom bar
    add("SD SOAK TEST", COL_BLUE, TFT_WHITE, IA_SDSOAK);   // lab14g: read-only SD reliability test -> GTI/gti.log
    add(String("REEL PROF")+": "+(g_reelprof?T(L_ON):T(L_OFF)), g_reelprof?(uint16_t)0x8000:COL_BAR, g_reelprof?TFT_WHITE:COL_LIT, IA_REELPROF);   // 5.9.33-lab3 frame profiler -> gti.log
    add(String("NO-CACHE")+": "+(g_nocache?T(L_ON):T(L_OFF)), g_nocache?(uint16_t)0x8000:COL_BAR, g_nocache?TFT_WHITE:COL_LIT, IA_NOCACHE);   // 5.9.32-lab2 benchmark control -> CONFIG.TXT NOCACHE=
    add(String("DIAG-DISP")+": "+(g_diagdisp?T(L_ON):T(L_OFF)), g_diagdisp?COL_GREEN:COL_BAR, g_diagdisp?TFT_BLACK:COL_LIT, IA_DIAGDISP);   // live diagnostic overlay -> CONFIG.TXT DIAGDISP=
  } else {
  // 5.9.12: single 3-way MODE — STANDALONE (radio off) / ESP-NOW (blind dongles, no router) / WiFi (home router).
  {const char* mlbl = !g_wireless_mode ? "STANDALONE" : (g_link_home ? "WiFi" : "ESP-NOW");
   uint16_t     mcol = !g_wireless_mode ? COL_GREEN   : (g_link_home ? COL_BLUE : COL_ACCENT);
   add(String("MODE: ")+mlbl, mcol, TFT_BLACK, IA_MODE);}
  if(g_wireless_mode && !g_link_home){   // ESP-NOW: blind Webby dongles (their own AP, no router)
    add(espnowIsPaired()?String(T(L_SWITCH_DONGLE)):String(T(L_SCAN_DONGLES)), espnowIsPaired()?COL_GREEN:COL_AMBER, TFT_BLACK, IA_DONGLE);
    uint8_t mm[64][6]; int mcN=enumMuCaDongles(mm,g_dongle_cap);
    if(mcN>0) add(String(T(L_CFG_HIVEMIND))+": "+(g_hivemind?T(L_ON):T(L_OFF)), g_hivemind?COL_ACCENT:COL_BAR, g_hivemind?TFT_WHITE:COL_LIT, IA_HIVEMIND);
  }
  if(g_wireless_mode && g_link_home){    // WiFi: home router — web UI / SD access
    add(String("HOME WIFI: ")+(g_home_ssid.length()?g_home_ssid:String("set up")), COL_ACCENT, TFT_WHITE, IA_HOMEWIFI);
    add(String("WEB UI: ")+(g_web_on?(g_home_ssid.length()?String("ON"):String("ON *set wifi*")):String("OFF")), g_web_on?COL_GREEN:COL_BAR, g_web_on?TFT_BLACK:COL_LIT, IA_WEBUI);
    if(g_home_ssid.length()) add(String("WIFI CHECK"), COL_BLUE, TFT_WHITE, IA_WIFICHECK);
  }
  add(String(T(L_CFG_FONT))+": "+fontName(g_font), COL_AMBER, TFT_BLACK, IA_FONT);
  add(String("NEO: ")+(g_neo_on?T(L_ON):T(L_OFF)), g_neo_on?COL_AMBER:COL_BAR, g_neo_on?TFT_BLACK:COL_LIT, IA_NEOUI);   // lab4-P4: the NEO switch, always shown (so NEO can be left)
  if(!g_neo)add(String(T(L_THEME))+": "+THEMES[g_theme_idx].name, COL_ACCENT, TFT_WHITE, IA_THEME);   // lab16b-P4: colour themes are for the classic layout   // Vince test: moved off the bottom bar
  add(String(T(L_CFG_LANG))+": "+LANG_NAMES[g_lang], (uint16_t)0x79D6, TFT_WHITE, IA_LANG);
  add(String(T(L_CFG_ROTATE))+": "+(g_portrait?T(L_PORTRAIT):T(L_LANDSCAPE)), COL_BLUE, TFT_WHITE, IA_ROTATE);
  if(!g_neo)add(String(T(L_CFG_COMPACT))+": "+(g_compact?T(L_ON):T(L_OFF)), g_compact?COL_GREEN:COL_BAR, g_compact?TFT_BLACK:COL_LIT, IA_COMPACT);
  add(String(T(L_CFG_LIBRARY))+": "+(g_mode==MODE_ADF?"ADF":g_mode==MODE_DSK?"DSK":"GEN"), COL_ACCENT, TFT_BLACK, IA_LIBMODE);   // v5.6.0: disk-format mode moved here from the mode bar
  add(String(T(L_CFG_CATEG))+": "+(g_categories?T(L_ON):T(L_OFF)), g_categories?COL_GREEN:COL_BAR, g_categories?TFT_BLACK:COL_LIT, IA_CATEG);   // library/category browse toggle (mirrors CONFIG.TXT CATEGORIES=)
  if(!g_neo)add(String(T(L_CFG_BUTTONS))+": "+(g_btn_pill?T(L_PILL):T(L_FLAT)), g_btn_pill?COL_ACCENT:COL_BAR, g_btn_pill?TFT_WHITE:COL_LIT, IA_BTNSTYLE);   // 5.8.3 reel button style
  add(String(T(L_CFG_SAVER))+": "+(g_ss_enabled?T(L_ON):T(L_OFF)), g_ss_enabled?COL_GREEN:COL_BAR, g_ss_enabled?TFT_BLACK:COL_LIT, IA_SAVER);   // screensaver on/off -> CONFIG.TXT SCREENSAVER=
  if(g_ss_enabled) add(String(T(L_CFG_SAVER))+" FX: "+(g_ss_matrix?T(L_MATRIX):(g_ss_slides?T(L_SLIDES):T(L_BOUNCE))), COL_BLUE, TFT_WHITE, IA_SSMODE);   // 5.8.3 screensaver mode (only shown when ON)
  add(String(T(L_CFG_FAVSAVER))+": "+(g_ss_fav?T(L_ON):T(L_OFF)), g_ss_fav?COL_GREEN:COL_BAR, g_ss_fav?TFT_BLACK:COL_LIT, IA_SSFAV);   // 5.8.3 favourites into slideshow
  add(String("CRACKTRO")+": "+(g_cracktro>=0?T(L_ON):T(L_OFF)), g_cracktro>=0?COL_GREEN:COL_BAR, g_cracktro>=0?TFT_BLACK:COL_LIT, IA_CRACKTRO);   // boot intro on/off -> CONFIG.TXT CRACKTRO=
  add(String("REEL BORDER")+": "+(g_reelborder?T(L_ON):T(L_OFF)), g_reelborder?COL_GREEN:COL_BAR, g_reelborder?TFT_BLACK:COL_LIT, IA_REELBORDER);   // MasterTelly CR: frame around reel covers -> CONFIG.TXT REELBORDER=
  add(String("COVER ART")+": "+(g_covers_on?T(L_ON):T(L_OFF)), g_covers_on?COL_GREEN:COL_BAR, g_covers_on?TFT_BLACK:COL_LIT, IA_COVERS);   // 5.9.32-lab2 -> CONFIG.TXT COVERS=
  add(String("LIST TILE")+": "+(g_listtile?T(L_ON):T(L_OFF)), g_listtile?COL_GREEN:COL_BAR, g_listtile?TFT_BLACK:COL_LIT, IA_LISTTILE);   // 5.9.34-lab4 -> CONFIG.TXT LISTTILE=
  add(String("LAST USED")+": "+(g_lastused?T(L_ON):T(L_OFF)), g_lastused?COL_GREEN:COL_BAR, g_lastused?TFT_BLACK:COL_LIT, IA_LASTUSED);   // restore last-loaded game on boot -> CONFIG.TXT LASTUSED=
  if(g_devmode&&!g_neo) add("TEST TOOLS >", (uint16_t)0x4208, TFT_WHITE, IA_TESTPAGE);   // lab14p: DEVMODE=OFF hides it   // lab14k: SD SOAK TEST, REEL PROF, NO-CACHE, DIAG-DISP live here now
  add(T(L_RESCAN_SD), COL_BLUE, TFT_WHITE, IA_RESCAN);
  if(g_neo) add(T(L_USER_DISKS), COL_BLUE, TFT_WHITE, IA_USRDSK);   // lab16b-P4: was the USR-DSK button on the mode bar
  add(T(L_SOFT_RESET), (uint16_t)0x8000, TFT_WHITE, IA_RESET);
  {bool diagOn=(g_loaded&&g_loaded_name=="AMIGA TEST KIT");   // v5.6.1: ATK is Amiga-only — hide LOAD DIAG in DSK/GEN (keep EJECT DIAG if somehow still loaded)
   if(g_mode==MODE_ADF||diagOn) add(diagOn?T(L_EJECT_DIAG):T(L_LOAD_DIAG), diagOn?(uint16_t)0xE8C4:COL_ACCENT, diagOn?TFT_BLACK:TFT_WHITE, IA_DIAG);}
  add(T(L_SD_ACCESS), (uint16_t)0x05FF, TFT_BLACK, IA_SDACCESS);
  add(T(L_FW_UPDATE), COL_AMBER, TFT_BLACK, IA_FWUPDATE);
  }   // lab14k: end of the main Settings list
  if(g_neo){ neoInfoGrid(); return; }   // lab16b-P4
  int ix=0,iy=STATUS_H,iw=VW,ih=VH-STATUS_H-BOTTOM_H;
  fillBack(ix,iy,iw,ih);   // lab16a-P4: NEO background (flat COL_BG otherwise)
  gfx_setTextSize(1);gfx_setTextColor(COL_DIM,COL_BG);gfx_setCursor(8,iy+5);gfx_print(g_info_test?"SETTINGS > TEST TOOLS":T(L_SETTINGS));   // lab14k
  int headerH=18, footerH=14, pad=8, gap=6, colGap=8, bh=g_neo?40:34, cols=(g_portrait?1:2);   // lab16a-P4: NEO capsules 40 px   // v5.5.5: 2 cols landscape (half-width), 1 col portrait (full-width, paginates)
  int areaTop=iy+headerH, areaH=ih-headerH-footerH;
  int colW=(iw-pad*2-colGap*(cols-1))/cols;
  int rowsPP=(areaH+gap)/(bh+gap); if(rowsPP<1)rowsPP=1;
  int perPage=rowsPP*cols;
  g_info_pages=(g_ii_n+perPage-1)/perPage; if(g_info_pages<1)g_info_pages=1;
  if(g_info_page>=g_info_pages)g_info_page=g_info_pages-1; if(g_info_page<0)g_info_page=0;
  {String pn="PAGE "+String(g_info_page+1)+"/"+String(g_info_pages);gfx_setTextColor(COL_DIM,COL_BG);gfx_setCursor(iw-8-gfx_textWidth(pn),iy+5);gfx_print(pn);}
  int startI=g_info_page*perPage, endI=min(g_ii_n,startI+perPage);
  g_ir_n=0;
  for(int i2=startI;i2<endI;i2++){
    int idx=i2-startI, col=idx%cols, row=idx/cols;
    int bx=ix+pad+col*(colW+colGap), by=areaTop+row*(bh+gap);
    // 5.9.28: the settings grid now follows BTNSTYLE too. It used to be hardcoded to the
    // dim-fill+border look, so PILL only ever restyled the two bottom bars and this tab
    // stayed flat. PILL = solid coloured capsule + auto-contrast ink (same language as the
    // nav/reel bars); FLAT = the original v5.6.7 dim-fill + bright double border.
    if(g_neo){   // lab16a-P4: capsule row - label on the left, value in a coloured chip; actions are full pills
      uint16_t kc=g_ii[i2].bg; if(kc==COL_BAR)kc=COL_CIRC;
      bool hot=(g_info_last!=IA_NONE&&g_ii[i2].act==g_info_last);
      String L=g_ii[i2].lbl; int sp=L.indexOf(": ");
      if(sp>0){
        String lab=L.substring(0,sp), val=L.substring(sp+2);
        gfx_fillRoundRect(bx,by,colW,bh,bh/2,COL_PANEL);
        if(hot)neoFrame(bx,by,colW,bh,bh/2,COL_AMBER,2); else gfx_strokeRoundRect(bx,by,colW,bh,bh/2,COL_SEP);
        int sz=2; gfx_setTextSize(2); int lw2=gfx_textWidth(lab), vw2=gfx_textWidth(val);
        if(lw2+vw2+bh+40>colW){ sz=1; gfx_setTextSize(1); lw2=gfx_textWidth(lab); vw2=gfx_textWidth(val); }
        gfx_setTextColor(COL_LIT,COL_PANEL); gfx_setCursor(bx+bh/2,by+(bh-8*sz)/2); gfx_print(lab);
        int chH=bh-12, chW=vw2+2*(chH/2)+4, chX=bx+colW-8-chW, chY=by+6;
        gfx_fillRoundRect(chX,chY,chW,chH,chH/2,kc); gfx_setTextColor(inkFor(kc),kc);
        gfx_setTextSize(sz); gfx_setCursor(chX+(chW-vw2)/2,chY+(chH-8*sz)/2); gfx_print(val);
      } else neoPill(bx,by,colW,bh,kc,L,hot);
      if(g_ir_n<20){g_ir[g_ir_n].x=bx;g_ir[g_ir_n].y=by;g_ir[g_ir_n].w=colW;g_ir[g_ir_n].h=bh;g_ir[g_ir_n].act=g_ii[i2].act;g_ir_n++;}
      continue;
    }
    uint16_t kc=g_ii[i2].bg, kfill, kink;
    if(g_btn_pill){
      kfill=kc; kink=inkFor(kc);                 // inkFor (not the row's fg) so every theme stays readable
      gfx_fillRoundRect(bx,by,colW,bh,bh/2,kfill);
    }else{
      kfill=(uint16_t)((kc>>2)&0x39E7); kink=keyInk(kc);   // v5.6.7: dim-fill + bright border key
      gfx_fillRoundRect(bx,by,colW,bh,8,kfill);
      gfx_drawRoundRect(bx,by,colW,bh,8,kink);
      gfx_drawRoundRect(bx+1,by+1,colW-2,bh-2,7,kink);
    }
    int sz=2; gfx_setTextSize(sz); int tw=gfx_textWidth(g_ii[i2].lbl);
    int kinset=g_btn_pill?(bh/2):8;              // pill: keep the label clear of the rounded caps
    if(tw>colW-kinset){ sz=1; gfx_setTextSize(sz); tw=gfx_textWidth(g_ii[i2].lbl); }   // shrink an over-long label to fit the half-width cell
    gfx_setTextColor(kink,kfill);
    gfx_setCursor(bx+(colW-tw)/2,by+(bh-8*sz)/2);gfx_print(g_ii[i2].lbl);
    if(g_ir_n<20){g_ir[g_ir_n].x=bx;g_ir[g_ir_n].y=by;g_ir[g_ir_n].w=colW;g_ir[g_ir_n].h=bh;g_ir[g_ir_n].act=g_ii[i2].act;g_ir_n++;}
  }
  gfx_setTextSize(1);gfx_setTextColor(COL_DIM,COL_BG);
  gfx_setCursor(8,iy+ih-11);gfx_print("Heap:"+String(ESP.getFreeHeap()/1024)+"K PSRAM:"+String(ESP.getFreePsram()/1024)+"K  Games:"+String(g_games.size()));
}

// lab16b-P4: NEO Settings - toggles as capsules (two columns landscape, one portrait; 10 a page), the
// actions as a row of pills under them (4 a page). The value sits in a coloured chip: ON green, OFF slate,
// MODE gold, LANG / LIBRARY cyan. The last option tapped is outlined in gold.
static void neoInfoGrid(){
  int top=STATUS_H+10, barTop=VH-BOTTOM_H;
  fillBack(0,STATUS_H,VW,barTop-STATUS_H);
  int tl[32],al[32],nt=0,na=0;
  for(int i=0;i<g_ii_n;i++){ if(strstr(g_ii[i].lbl,": "))tl[nt++]=i; else al[na++]=i; }
  const int TPP=10, APP=4;
  int cols=g_portrait?1:2, rows=TPP/cols, pad=16, colGap=16, gap=g_portrait?8:10;
  int ah=40, acols=g_portrait?2:4, arows=APP/acols, agap=10;
  int aTop=barTop-8-arows*ah-(arows-1)*agap;
  int bh=((aTop-12-top)-(rows-1)*gap)/rows; if(bh>52)bh=52; if(bh<28)bh=28;
  g_info_pages=max(1,max((nt+TPP-1)/TPP,(na+APP-1)/APP));
  if(g_info_page>=g_info_pages)g_info_page=g_info_pages-1; if(g_info_page<0)g_info_page=0;
  int colW=(VW-2*pad-(cols-1)*colGap)/cols;
  g_ir_n=0;
  for(int k=0;k<TPP;k++){ int idx=g_info_page*TPP+k; if(idx>=nt)break; int i2=tl[idx];
    int col=k/rows, row=k%rows, bx=pad+col*(colW+colGap), by=top+row*(bh+gap);
    bool hot=(g_info_last!=IA_NONE&&g_ii[i2].act==g_info_last);
    String L=g_ii[i2].lbl; int sp=L.indexOf(": "); String lab=L.substring(0,sp), val=L.substring(sp+2);
    uint16_t kc=COL_CIRC; uint8_t a=g_ii[i2].act;
    if(val==T(L_ON))kc=COL_GREEN; else if(val==T(L_OFF))kc=COL_CIRC; else if(a==IA_MODE)kc=COL_AMBER; else if(a==IA_LANG||a==IA_LIBMODE)kc=COL_BLUE;
    gfx_fillRoundRect(bx,by,colW,bh,bh/2,COL_PANEL);
    if(hot)neoFrame(bx,by,colW,bh,bh/2,COL_AMBER,2); else gfx_strokeRoundRect(bx,by,colW,bh,bh/2,COL_SEP);
    int sz=2; gfx_setTextSize(2); int lw2=gfx_textWidth(lab), vw2=gfx_textWidth(val);
    if(lw2+vw2+bh+48>colW){ sz=1; gfx_setTextSize(1); lw2=gfx_textWidth(lab); vw2=gfx_textWidth(val); }
    gfx_setTextColor(COL_LIT,COL_PANEL); gfx_setCursor(bx+bh/2,by+(bh-8*sz)/2); gfx_print(lab);
    int chH=bh-14, chW=vw2+chH+8, chX=bx+colW-8-chW, chY=by+7;
    gfx_fillRoundRect(chX,chY,chW,chH,chH/2,kc); gfx_setTextColor(inkFor(kc),kc);
    gfx_setTextSize(sz); gfx_setCursor(chX+(chW-vw2)/2,chY+(chH-8*sz)/2); gfx_print(val);
    if(g_ir_n<20){g_ir[g_ir_n].x=bx;g_ir[g_ir_n].y=by;g_ir[g_ir_n].w=colW;g_ir[g_ir_n].h=bh;g_ir[g_ir_n].act=a;g_ir_n++;}
  }
  int aw=(VW-2*pad-(acols-1)*agap)/acols;
  for(int k=0;k<APP;k++){ int idx=g_info_page*APP+k; if(idx>=na)break; int i2=al[idx];
    int col=k%acols, row=k/acols, bx=pad+col*(aw+agap), by=aTop+row*(ah+agap);
    uint8_t a=g_ii[i2].act; bool hot=(g_info_last!=IA_NONE&&a==g_info_last);
    uint16_t kc=(a==IA_RESET)?(uint16_t)0x9124:(a==IA_DIAG&&g_ii[i2].bg==(uint16_t)0xE8C4)?(uint16_t)0x9124:COL_ACCENT;
    neoPill(bx,by,aw,ah,kc,g_ii[i2].lbl,hot);
    if(g_ir_n<20){g_ir[g_ir_n].x=bx;g_ir[g_ir_n].y=by;g_ir[g_ir_n].w=aw;g_ir[g_ir_n].h=ah;g_ir[g_ir_n].act=a;g_ir_n++;}
  }
  if(g_info_test){ gfx_setTextSize(1); gfx_setTextColor(COL_DIM,COL_BG);
    String hs="Heap:"+String(ESP.getFreeHeap()/1024)+"K  PSRAM:"+String(ESP.getFreePsram()/1024)+"K  Games:"+String(g_games.size())+"  "+String(g_c6_ver);   // lab2-P4G7
    gfx_setCursor((VW-gfx_textWidth(hs))/2,aTop-14); gfx_print(hs); }
}
static void drawModeBar(){
  if(g_neo)return;   // lab16b-P4: library = tap the top bar; USER DISKS = Settings
  int mbR=LIST_X+LIST_W+AZ_W;
  gfx_fillRect(LIST_X,STATUS_H,LIST_W+AZ_W,MODE_BAR_H,COL_BAR);gfx_setTextSize(1);
  if(g_categories){   // v5.6.0: mode moved to INFO; this slot becomes the Categories button
    gfx_fillRoundRect(LIST_X+4,STATUS_H+2,104,14,7,COL_AMBER);gfx_setTextColor(TFT_BLACK,COL_AMBER);gfx_setCursor(LIST_X+10,STATUS_H+6);gfx_print(g_libpath.length()?"< CATEGORY":"CATEGORIES");
  } else {
  // lab15g: ONE library button (Mez: "just put 1 button and change modes between it"). Tapping it cycles
  // ADF -> DSK -> GEN; Settings -> LIBRARY stays the main place to pick one. Same 104 px the three
  // 32 px pills used to share, so the whole slot is one target.
  { const char* nm=g_mode==MODE_ADF?"LIBRARY: ADF":g_mode==MODE_DSK?"LIBRARY: DSK":"LIBRARY: GEN";
    gfx_fillRoundRect(LIST_X+4,STATUS_H+2,104,14,7,COL_ACCENT);gfx_setTextColor(COL_AMBER,COL_ACCENT);
    gfx_setCursor(LIST_X+4+(104-gfx_textWidth(nm))/2,STATUS_H+6);gfx_print(nm); }
  }
  gfx_fillRoundRect(LIST_X+112,STATUS_H+2,62,14,7,COL_BLUE);gfx_setTextColor(TFT_WHITE,COL_BLUE);gfx_setCursor(LIST_X+118,STATUS_H+6);gfx_print("USR-DSK");   // v4.9.7 user-disk manager
  gfx_setTextColor(COL_MID,COL_BAR);String gt=String(g_games.size())+" games";gfx_setCursor(mbR-gfx_textWidth(gt)-6,STATUS_H+6);gfx_print(gt);
}

// lab16a/b-P4: width kept free at the right of a list row (the "nDSK" chip; NEO: IN DRIVE tag + favourite star)
static int neoThumbS(){ int s=LIST_ITEM_H-12; if(s>44)s=44; if(s<20)s=20; return s; }
static int rowChipW(int gi){ if(g_neo){ int w=0; if(g_loaded&&g_loaded_game_idx==gi)w+=80; if(g_games[gi].fav)w+=28; return w; } return g_games[gi].disk_count>1?36:0; }
static int rowNameX(){ return g_neo?(LIST_X+10+neoThumbS()+12):(LIST_X+6+2*(8+g_name_sz*3)+6); }
static int rowNameMaxW(int gi){ return LIST_W-(rowNameX()-LIST_X)-8-rowChipW(gi); }
static void neoFileList(){   // lab16b-P4: rounded panel; rows = cover thumbnail, name, "n disks"; gold selection
  neoMicroWarm();
  fillBack(NEO_LP_X,NEO_LP_Y,NEO_LP_W,NEO_LP_H);
  gfx_fillRoundRect(NEO_LP_X,NEO_LP_Y,NEO_LP_W,NEO_LP_H,12,COL_PANEL); gfx_strokeRoundRect(NEO_LP_X,NEO_LP_Y,NEO_LP_W,NEO_LP_H,12,COL_SEP);
  if(g_games.empty()){gfx_setTextSize(2);gfx_setTextColor(0xE8C4,COL_PANEL);gfx_setCursor(LIST_X+12,LIST_TOP+16);gfx_print(g_mode==MODE_ADF?"No .ADF files":g_mode==MODE_DSK?"No .DSK files":"No /GENERIC files");return;}
  if(g_scrollPx<0)g_scrollPx=0;int mp=maxScrollPx();if(g_scrollPx>mp)g_scrollPx=mp;
  int first=(int)(g_scrollPx/LIST_ITEM_H),off=(int)(g_scrollPx-(float)first*LIST_ITEM_H);
  g_scroll=first;
  g_clip_y0=LIST_TOP;g_clip_y1=LIST_BOTTOM;
  int ts=neoThumbS(), nx=rowNameX();
  for(int vi=0;vi<=ITEMS_VIS+1;vi++){int gi=first+vi;if(gi>=(int)g_games.size())break;
    auto&game=g_games[gi];bool sel=gi==g_sel,ld=g_loaded&&g_loaded_game_idx==gi;
    int y=LIST_TOP-off+vi*LIST_ITEM_H;if(y>=LIST_BOTTOM)break;
    int cy=y+LIST_ITEM_H/2; uint16_t rb=sel?COL_SEL:COL_PANEL;
    if(sel){ gfx_fillRoundRect(LIST_X,y+2,LIST_W,LIST_ITEM_H-4,10,COL_SEL); neoFrame(LIST_X,y+2,LIST_W,LIST_ITEM_H-4,10,COL_AMBER,2); }
    // thumbnail: the reel's micro-thumb when there is one, else the initial on a slate square
    int tx=LIST_X+10, ty=cy-ts/2, md=0; uint16_t*m=(game.jpg_path!="?")?neoMicroTile(gi,&md):NULL;
    if(m&&md>0){ carBlit(m,md,tx+ts/2,cy,ts,ts,0); gfx_strokeRoundRect(tx-1,ty-1,ts+2,ts+2,3,sel?COL_AMBER:COL_SEP); }
    else { uint16_t tc=ld?COL_GREEN:(sel?COL_AMBER:COL_CIRC); gfx_fillRoundRect(tx,ty,ts,ts,6,tc);
      char ib[2]={(char)toupper(game.name.charAt(0)),0}; int ls=ts>=36?3:2; gfx_setTextSize(ls); gfx_setTextColor(inkFor(tc),tc);
      gfx_setCursor(tx+(ts-6*ls)/2+1,ty+(ts-8*ls)/2); gfx_print(ib); }
    // name (+ "n disks" when the row is tall enough); the selected name scrolls when too long
    int nsz=g_name_sz; bool two=(LIST_ITEM_H>=8*nsz+22);
    int ny=two?cy-(8*nsz+5+8)/2:cy-4*nsz;
    gfx_setTextSize(nsz); gfx_setTextColor(sel?COL_AMBER:TFT_WHITE,rb);
    int maxNW=rowNameMaxW(gi);
    if(sel&&gfx_textWidth(game.name)>maxNW){ g_clip_x0=nx;g_clip_x1=nx+maxNW; gfx_setCursor(nx-g_marquee_off,ny);gfx_print(game.name); g_clip_x0=0;g_clip_x1=gW; }
    else { String nm=game.name;while(gfx_textWidth(nm)>maxNW&&nm.length()>3)nm=nm.substring(0,nm.length()-1); gfx_setCursor(nx,ny);gfx_print(nm); }
    if(two){ gfx_setTextSize(1); gfx_setTextColor(sel?(uint16_t)0xD5AE:COL_DIM,rb); gfx_setCursor(nx,ny+8*nsz+5);
      gfx_print(game.disk_count>1?String(game.disk_count)+" disks":String("1 disk")); }
    // right: favourite star, IN DRIVE tag
    int rx=LIST_X+LIST_W-10;
    if(game.fav){ gfx_fillStar(rx-9,cy,9.0f,COL_STAR); rx-=28; }
    if(ld){ int tw2=74; gfx_fillRoundRect(rx-tw2,cy-11,tw2,22,11,COL_NOW); gfx_strokeRoundRect(rx-tw2,cy-11,tw2,22,11,COL_GREEN);
      gfx_setTextSize(1); gfx_setTextColor(COL_GREEN,COL_NOW); gfx_setCursor(rx-tw2+(tw2-48)/2,cy-4); gfx_print("IN DRIVE"); }
  }
  g_clip_y0=0;g_clip_y1=gH;
}
static void drawFileList(){
  NeoScope _ns;
  if(g_neo){ neoFileList(); return; }   // lab16b-P4
  if(g_neo)fillBack(LIST_X,LIST_TOP,LIST_W,LIST_BOTTOM-LIST_TOP); else
  gfx_fillRect(LIST_X,LIST_TOP,LIST_W,LIST_BOTTOM-LIST_TOP,COL_BG);
  if(g_games.empty()){gfx_setTextSize(1);gfx_setTextColor(0xE8C4,COL_BG);gfx_setCursor(LIST_X+8,LIST_TOP+16);gfx_print(g_mode==MODE_ADF?"No .ADF files":g_mode==MODE_DSK?"No .DSK files":"No /GENERIC files");return;}
  if(g_scrollPx<0)g_scrollPx=0;int mp=maxScrollPx();if(g_scrollPx>mp)g_scrollPx=mp;
  int first=(int)(g_scrollPx/LIST_ITEM_H),off=(int)(g_scrollPx-(float)first*LIST_ITEM_H);
  g_scroll=first;                                  // keep integer scroll in sync (thumb, etc.)
  g_clip_y0=LIST_TOP;g_clip_y1=LIST_BOTTOM;         // clip partial rows to the list window
  for(int vi=0;vi<=ITEMS_VIS+1;vi++){int gi=first+vi;if(gi>=(int)g_games.size())break;
    auto&game=g_games[gi];bool sel=gi==g_sel,ld=g_loaded&&g_loaded_game_idx==gi;
    int y=LIST_TOP-off+vi*LIST_ITEM_H;if(y>=LIST_BOTTOM)break;
    if(g_neo){   // lab16a-P4: rounded rows on the background; the selected row gets a 2 px gold frame
      if(sel){gfx_fillRoundRect(LIST_X+4,y+2,LIST_W-8,LIST_ITEM_H-4,8,COL_SEL);neoFrame(LIST_X+4,y+2,LIST_W-8,LIST_ITEM_H-4,8,COL_AMBER,2);}
      else gfx_fillRoundRect(LIST_X+4,y+2,LIST_W-8,LIST_ITEM_H-4,8,COL_PANEL);
    } else {
    if(sel){gfx_fillRoundRect(LIST_X+2,y+1,LIST_W-4,LIST_ITEM_H-2,4,COL_SEL);gfx_drawRoundRect(LIST_X+2,y+1,LIST_W-4,LIST_ITEM_H-2,4,COL_AMBER);}
    else gfx_fillRoundRect(LIST_X+2,y+1,LIST_W-4,LIST_ITEM_H-2,3,COL_PANEL);
    }
    uint16_t acCol=ld?COL_GREEN:(sel?COL_AMBER:COL_ACCENT);gfx_fillRect(LIST_X+3,y+3,3,LIST_ITEM_H-4,acCol);
    int r=8+g_name_sz*3,cx=LIST_X+6+r,cy=y+LIST_ITEM_H/2;
    if(game.fav){gfx_fillStar(cx,cy,(float)r,COL_STAR);}
    else{
    gfx_fillCircle(cx,cy,r,sel?COL_AMBER:(ld?COL_GREEN:COL_CIRC));
    gfx_setTextSize(g_name_sz);gfx_setTextColor(sel||ld?TFT_BLACK:COL_CIRC_TEXT,sel?COL_AMBER:COL_CIRC);
    char ib[2]={(char)toupper(game.name.charAt(0)),0};gfx_setCursor(cx-gfx_textWidth(ib)/2,cy-4*g_name_sz);gfx_print(ib);
    }
    int nx=cx+r+6;gfx_setTextSize(g_name_sz);gfx_setTextColor(sel?TFT_WHITE:COL_LIT,sel?COL_SEL:COL_PANEL);
    int maxNW=LIST_W-(nx-LIST_X)-8-rowChipW(gi);
    if(sel&&gfx_textWidth(game.name)>maxNW){
      // marquee: bounce the full selected name within its lane (offset 0..max), clipped horizontally
      g_clip_x0=nx;g_clip_x1=nx+maxNW;
      gfx_setCursor(nx-g_marquee_off,cy-4*g_name_sz);gfx_print(game.name);
      g_clip_x0=0;g_clip_x1=gW;
    } else {
      String nm=game.name;while(gfx_textWidth(nm)>maxNW&&nm.length()>3)nm=nm.substring(0,nm.length()-1);
      gfx_setCursor(nx,cy-4*g_name_sz);gfx_print(nm);
    }
    if(g_neo&&ld){gfx_setTextSize(1);int tx=LIST_X+LIST_W-64;gfx_fillRoundRect(tx,cy-8,58,16,8,COL_NOW);gfx_strokeRoundRect(tx,cy-8,58,16,8,COL_GREEN);
      gfx_setTextColor(COL_GREEN,COL_NOW);gfx_setCursor(tx+5,cy-4);gfx_print("IN DRIVE");}   // lab16a-P4
    else if(game.disk_count>1){gfx_setTextSize(1);gfx_fillRoundRect(LIST_X+LIST_W-38,cy-6,34,12,4,COL_ACCENT);gfx_setTextColor(TFT_WHITE,COL_ACCENT);gfx_setCursor(LIST_X+LIST_W-34,cy-4);gfx_print(String(game.disk_count)+"DSK");}
  }
  g_clip_y0=0;g_clip_y1=gH;
}

static void drawNowPlayingBar(){
  NeoScope _ns;
  if(!NOW_ON)return;
  int y=NOW_Y;
  if(g_loaded&&g_loaded_name.length()){gfx_fillRect(LIST_X,y,LIST_W,NOW_PLAY_H,COL_NOW);gfx_drawRect(LIST_X,y,LIST_W,NOW_PLAY_H,COL_GREEN);
    gfx_fillCircle(LIST_X+8,y+NOW_PLAY_H/2,3,COL_GREEN);gfx_setTextSize(1);gfx_setTextColor(COL_GREEN,COL_NOW);gfx_setCursor(LIST_X+16,y+3);gfx_print(T(L_NOW_PLAYING));
    gfx_setTextColor(TFT_WHITE,COL_NOW);gfx_setCursor(LIST_X+16,y+12);String n=g_loaded_name;while(gfx_textWidth(n)>LIST_W-24&&n.length()>3)n=n.substring(0,n.length()-1);gfx_print(n);}
  else{fillBack(LIST_X,y,LIST_W,NOW_PLAY_H);gfx_setTextSize(1);gfx_setTextColor(COL_MID,COL_BG);gfx_setCursor(LIST_X+8,y+NOW_PLAY_H/2-4);gfx_print(String(g_games.size())+T(L_GAMES_TAP));}
}

// Split active letters into the two halves: page 0 = #/A-M, page 1 = N-Z
static int azHalf(int page,char*out){int n=0;for(int i=0;i<active_letter_count;i++){bool lo=active_letters[i]<='M';if((page==0&&lo)||(page==1&&!lo))out[n++]=active_letters[i];}return n;}
#define AZ_TOG_H 32   // A-M/N-Z toggle button height (taller = easier to hit, away from the INFO button)
static int neoAZLetterH(){ int letTop=AZ_TOP+AZ_SRCH_H+4, barH=AZ_TOP+AZ_H-letTop, n=active_letter_count<1?1:active_letter_count; int h=barH/n; if(h>26)h=26; if(h<8)h=8; return h; }
static void neoAZBar(){   // lab16b-P4
  fillBack(AZ_X,AZ_TOP,AZ_W,AZ_H);
  if(!active_letter_count)return;
  drawMagnifier(AZ_X+AZ_W/2,AZ_TOP+AZ_SRCH_H/2+2,COL_AMBER);
  int letTop=AZ_TOP+AZ_SRCH_H+4, lh=neoAZLetterH(), lsz=(lh>=22)?2:1;
  gfx_setTextSize(lsz);
  for(int i=0;i<active_letter_count;i++){ int ly=letTop+i*lh; if(ly+lh>AZ_TOP+AZ_H)break; char L=active_letters[i];
    if(L==g_active_letter){ gfx_fillRoundRect(AZ_X+3,ly,AZ_W-6,lh,4,COL_AMBER); gfx_setTextColor(TFT_BLACK,COL_AMBER); }
    else gfx_setTextColor(COL_DIM,COL_BG);
    char lb[2]={L,0}; gfx_setCursor(AZ_X+(AZ_W-6*lsz)/2+1,ly+(lh-8*lsz)/2); gfx_print(lb); }
}
static void drawAZBar(){
  NeoScope _ns;
  if(g_neo){ neoAZBar(); return; }   // lab16b-P4
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
  if(g_neo){   // lab16b-P4: one column of every letter
    if(px<AZ_X||px>=(uint16_t)(AZ_X+AZ_W)||py<AZ_TOP||py>=(uint16_t)(AZ_TOP+AZ_H)||!active_letter_count)return false;
    int r=((int)py-(AZ_TOP+AZ_SRCH_H+4))/neoAZLetterH(); if(r<0)r=0; if(r>=active_letter_count)r=active_letter_count-1;
    char letter=active_letters[r];
    int target=0;for(int i=0;i<(int)g_games.size();i++){if(bucketOf(g_games[i].name)>=letter){target=i;break;}}
    setActiveLetter(letter); g_sel=target;g_scrollPx=min((float)(target*LIST_ITEM_H),(float)maxScrollPx());g_inertia_on=false;
    return true; }
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
// Vince test: single bar split by divider lines, white-on-black on every theme.
static void drawBottomBar(){
  NeoScope _ns;
  if(g_neo){   // lab16b-P4: five pills - PREV / NEXT slate, INSERT cyan (EJECT red), REEL slate, CONFIG gold
    int y=VH-BOTTOM_H; fillBack(0,y,VW,BOTTOM_H); int bw=VW/5, pad=6, bh=BOTTOM_H-14, by=y+7;
    String il=T(L_INSERT); uint16_t ic0=COL_BLUE;
    if(!g_games.empty()){ auto&gm=g_games[g_sel];
      if(g_loaded&&g_loaded_game_idx==g_sel&&g_loaded_disk_idx==g_disk_sel){ il=T(L_EJECT); ic0=0xC1A6; }
      else if(gm.disk_count>1&&g_disk_sel>0) il=String(T(L_INSERT))+" "+String(g_disk_sel+1); }
    else ic0=COL_CIRC;
    neoPill(0*bw+pad,by,bw-2*pad,bh,COL_ACCENT,String("< ")+T(L_PREV),false);
    neoPill(1*bw+pad,by,bw-2*pad,bh,COL_ACCENT,String(T(L_NEXT))+" >",false);
    neoPill(2*bw+pad,by,bw-2*pad,bh,ic0,il,false);
    neoPill(3*bw+pad,by,bw-2*pad,bh,COL_ACCENT,T(L_REEL),false);
    neoPill(4*bw+pad,by,bw-2*pad,bh,COL_AMBER,T(L_INFO),false);
    return; }
  const uint16_t bg=TFT_BLACK, ink=TFT_WHITE;
  int y=VH-BOTTOM_H;gfx_fillRect(0,y,VW,BOTTOM_H,bg);gfx_hline(0,y,VW,COL_SEP);
  const int nb=4;int bw=VW/nb;
  String blbl[4]={String("< ")+T(L_PREV),String(T(L_NEXT))+" >",String(T(L_REEL)),String(T(L_INFO))};
  if(g_btn_pill){                                                   // 5.9.x: coloured pill buttons (matches the reel bar)
    static const uint16_t cols[4]={COL_BLUE,COL_BLUE,COL_AMBER,COL_GREEN};
    int pad=5, bh=BOTTOM_H-2*pad, r=bh/2, by=y+pad;
    int ts=2; for(int i=0;i<nb;i++){gfx_setTextSize(2); if(gfx_textWidth(blbl[i])>bw-2*pad-18){ts=1;break;}}
    gfx_setTextSize(ts);
    for(int i=0;i<nb;i++){
      uint16_t bc=cols[i], ic=inkFor(bc); int bx=i*bw+pad, w=bw-2*pad, tw=gfx_textWidth(blbl[i]), th=8*ts;
      gfx_fillRoundRect(bx,by,w,bh,r,bc);
      if(i==2){ int total=16+tw,sx=bx+(w-total)/2; drawCarouselIcon(sx+7,by+bh/2,ic);
        gfx_setTextColor(ic,bc); gfx_setCursor(sx+16,by+(bh-th)/2); gfx_print(blbl[i]); }
      else { gfx_setTextColor(ic,bc); gfx_setCursor(bx+(w-tw)/2,by+(bh-th)/2); gfx_print(blbl[i]); }
    }
    return;
  }
  for(int i=1;i<nb;i++)gfx_vline(i*bw,y+8,BOTTOM_H-16,ink);          // slot dividers (FLAT)
  int ts=2; for(int i=0;i<nb;i++){gfx_setTextSize(2); if(gfx_textWidth(blbl[i])>bw-12){ts=1;break;}}
  gfx_setTextSize(ts);gfx_setTextColor(ink,bg);
  for(int i=0;i<nb;i++){
    int bx=i*bw,tw=gfx_textWidth(blbl[i]),th=8*ts;
    if(i==2){ int total=16+tw,sx=bx+(bw-total)/2;
      drawCarouselIcon(sx+7,y+BOTTOM_H/2,ink);
      gfx_setCursor(sx+16,y+(BOTTOM_H-th)/2);gfx_print(blbl[i]);
    } else {
      gfx_setCursor(bx+(bw-tw)/2,y+(BOTTOM_H-th)/2);gfx_print(blbl[i]);
    }
  }
}

// ════════════════════════════════════════════════════════════════════════════
// CAROUSEL — "fake coverflow" reel (v4.8.5: first-class mode; CAROUSEL= sets default boot view)
// Center cover full-size, neighbours scaled+squashed+dimmed, looping reel.
// Sources cycle ALL -> FAV -> MOST -> RND (RND = shuffle-jump to one random
// cover). Tap center = INSERT/EJECT (deliberate; no automount). [LIST] exits.
// ════════════════════════════════════════════════════════════════════════════
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
static uint32_t g_car_die_rest_ms=0;   // v5.4.2: millis() when the die settled (for auto-hide)
#define DIE_HIDE_MS 2500               // v5.4.2: hide the rested die this many ms after the roll settles
static int g_car_ins_x=0,g_car_ins_y=0,g_car_ins_w=0,g_car_ins_h=0;   // INSERT button rect (set by drawCarousel)
static int g_car_disk_n=0,g_car_disk_x=0,g_car_disk_y=0,g_car_disk_bw=0,g_car_disk_h=0;   // v5.7.x: reel multi-disk button row rect
static void runScreensaver();   // defined below; the reel's idle tick can summon it
// CAR_TILE is defined up with COVER_TILE_PX — the list cover panel shares the reel's tile.
#define CAR_SLOTS 48                             // 5.9.0: LRU tile cache entries (PSRAM ~2.1 MB) — was 16; more cache = less re-reading when you scroll back. Dial down if PSRAM gets tight.
static uint16_t* car_buf[CAR_SLOTS]={0};         // NULL gates every read of car_game below, so the zero-init is safe at any CAR_SLOTS
static int      car_game[CAR_SLOTS]={0};         // set to -1 the instant a slot buffer is first allocated (see carTile)
static uint32_t car_stamp[CAR_SLOTS]={0};
static uint8_t  car_ok[CAR_SLOTS]={0};           // 5.9.34-lab4: 1 = this slot holds a real cover, 0 = a failed decode left it flat COL_BAR
static uint32_t car_tick_ctr=0;
#define CAR_BENCH 0
#if CAR_BENCH
static uint32_t g_bench_sharp=0,g_bench_micro=0,g_bench_square=0;
#endif

static int carN(){return (int)g_car_list.size();}
static int carWrap(int i){int n=carN();if(n<=0)return 0;i%=n;if(i<0)i+=n;return i;}
static const char* carSrcName(){return g_car_src==1?T(L_FAV):g_car_src==2?T(L_MOST):T(L_ALL);}

static void carBuildList(){
  g_car_list.clear();
  int n=(int)g_games.size();
  if(g_car_src==1){for(int i=0;i<n;i++)if(g_games[i].fav)g_car_list.push_back(i);}
  else if(g_car_src==2){for(int i=0;i<n;i++)if(g_games[i].plays>1)g_car_list.push_back(i);   // Vince test: MOST = played more than once
    std::sort(g_car_list.begin(),g_car_list.end(),[](int a,int b){
      if(g_games[a].plays!=g_games[b].plays)return g_games[a].plays>g_games[b].plays;
      String al=g_games[a].name,bl=g_games[b].name;al.toLowerCase();bl.toLowerCase();return al<bl;});}
  else{for(int i=0;i<n;i++)g_car_list.push_back(i);}   // ALL and RND share A-Z order
  if(g_reelfilter){   // v5.9.2 REELFILTER: keep only good-cover games; never empty the reel
    ensureCoverFlags();
    std::vector<int> keep;
    for(size_t k=0;k<g_car_list.size();k++)if(g_games[g_car_list[k]].cover_ok)keep.push_back(g_car_list[k]);
    if(!keep.empty())g_car_list.swap(keep);
  }
}

// Decode a game's cover into a CAR_TILE x CAR_TILE tile (aspect-fit, COL_BAR letterbox).
static bool carDecodeTile(int gi,uint16_t*dst){
  for(int i=0;i<CAR_TILE*CAR_TILE;i++)dst[i]=COL_BAR;
  auto&game=g_games[gi];
  if(!game.jpg_path.length()){String jpg;if(findJPGFor(g_files[game.first_file_idx],jpg))game.jpg_path=jpg;else game.jpg_path="?";}
  if(!(game.jpg_path.length()>0&&game.jpg_path!="?"))return false;
  int djw=0,djh=0;
  if(!coverDecodeNatural(game.jpg_path, COVER_TILE_PX, &djw, &djh)) return false;   // dst pre-filled COL_BAR; progressive->png sibling else placeholder
  float sc=min((float)CAR_TILE/djw,(float)CAR_TILE/djh);if(sc>1.0f)sc=1.0f;
  int dw=(int)(djw*sc),dh=(int)(djh*sc);
  int ox=(CAR_TILE-dw)/2,oy=(CAR_TILE-dh)/2;
  for(int r=0;r<dh;r++){int sy=(int)(r/sc);if(sy>=djh)sy=djh-1;
    for(int c=0;c<dw;c++){int sx=(int)(c/sc);if(sx>=djw)sx=djw-1;
      dst[(oy+r)*CAR_TILE+(ox+c)]=jpeg_tmp_buf[sy*djw+sx];}
    if(r%20==0)yield();}
  return true;
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
  char bkt[2]={hx[0],0};                                                            // 5.9.0: shard by first hex nibble -> ~n/16 files per dir
  return carThumbRoot(gi)+"/"+bkt+"/"+getGameBaseName(p)+"_"+hx+".tnl";
}
static bool carLoadThumb(int gi,uint16_t*dst){ if(g_nocache)return false;
  auto&g=g_games[gi];
  if(!(g.jpg_path.length()>0&&g.jpg_path!="?"))return false;
  // 5.9.0: open+read only. The two stat() calls each linearly walked the .thumbs
  // dir (FAT has no index) — the real "slow with many files" cost. Staleness is
  // re-checked at build time (buildThumbs `need`); a short/corrupt tile still fails
  // via got!=want below and self-heals through carDecodeTile.
  File f=SD_MMC.open(carThumbPath(gi).c_str(),"r");if(!f)return false;
  size_t want=(size_t)CAR_TILE*CAR_TILE*2;
  size_t got=f.read((uint8_t*)dst,want);f.close();
  return got==want;
}
// ── lab14f: the .thumbs folders, listed ONCE per build ─────────────────────────
// buildThumbs used to stat() every tile by name (a search of its bucket folder, ~480 files on
// a big card) and carSaveThumb checked the root and bucket folders exist before every write.
// Now the buckets are walked once: each tile's location + FAT date/time goes into g_thumbloc,
// and which folders exist into g_thumb_dirs. A tile is up to date when its FAT date/time
// EQUALS its cover's - carSaveThumb stamps it that way (f_utime). The old test was "tile newer
// than cover", but the GTi has no clock, so every tile it wrote was dated 1980 and looked stale:
// every RESCAN rebuilt every tile.
#define TILE_BYTES ((uint32_t)CAR_TILE*CAR_TILE*2)
static uint32_t g_thumb_dirs[2]={0,0};          // [0]=/ADF/.thumbs [1]=/DSK/.thumbs: bit 16 = root exists, bits 0-15 = bucket 0-F exists
static bool g_thumb_indexed=false;
static const FwLoc* g_tile_stamp=nullptr;        // set by buildThumbs around carSaveThumb: the cover whose date the tile takes
static int thumbNib(char c){ if(c>='0'&&c<='9')return c-'0'; if(c>='A'&&c<='F')return c-'A'+10; if(c>='a'&&c<='f')return c-'a'+10; return -1; }
static void thumbIndexBuild(){
  std::vector<FwLoc>().swap(g_thumbloc); g_thumb_dirs[0]=g_thumb_dirs[1]=0; g_thumb_indexed=false;
  if(g_fw_drv<0) return;                          // no FatFs drive: the by-name path does everything
  FF_DIR* d=(FF_DIR*)malloc(sizeof(FF_DIR)); FF_DIR* d2=(FF_DIR*)malloc(sizeof(FF_DIR));
  FILINFO* fi=(FILINFO*)malloc(sizeof(FILINFO)); FILINFO* f2=(FILINFO*)malloc(sizeof(FILINFO));
  if(!d||!d2||!fi||!f2){ free(d);free(d2);free(fi);free(f2); return; }
  const char* roots[2]={"/ADF/.thumbs","/DSK/.thumbs"};
  uint32_t unloc=0;
  for(int r=0;r<2;r++){
    if(f_opendir(d,(String(g_fw_drv)+":"+roots[r]).c_str())!=FR_OK) continue;
    g_thumb_dirs[r]|=1u<<16;
    while(f_readdir(d,fi)==FR_OK && fi->fname[0]){
      if(!(fi->fattrib&AM_DIR)) continue;
      int nb=(fi->fname[0]&&!fi->fname[1])?thumbNib(fi->fname[0]):-1; if(nb<0) continue;
      g_thumb_dirs[r]|=1u<<nb;
      String bdir=String(roots[r])+"/"+fi->fname;
      if(f_opendir(d2,(String(g_fw_drv)+":"+bdir).c_str())!=FR_OK) continue;
      while(f_readdir(d2,f2)==FR_OK && f2->fname[0]){
        if(f2->fattrib&AM_DIR) continue;
        uint32_t cl=0; if(!fw_entry_clust(d2,f2,&cl)||!cl){ cl=0xFFFFFFFFu; unloc++; }   // present, location unknown -> read it by name
        String tp=bdir+"/"+f2->fname; tp.toLowerCase();
        g_thumbloc.push_back({coverHash(tp),cl,(uint32_t)f2->fsize,f2->fdate,f2->ftime});
        if(!g_fw_fs){ g_fw_fs=d2->obj.fs; g_fw_fsid=d2->obj.fs->id; }
      }
      f_closedir(d2);
    }
    f_closedir(d);
  }
  free(d);free(d2);free(fi);free(f2);
  std::sort(g_thumbloc.begin(),g_thumbloc.end(),[](const FwLoc&a,const FwLoc&b){return a.h<b.h;});
  g_thumb_indexed=true;
  gLog("[thumbs] index: %u tiles listed (%u by name only), dirs ADF=%05X DSK=%05X\n",(unsigned)g_thumbloc.size(),(unsigned)unloc,(unsigned)g_thumb_dirs[0],(unsigned)g_thumb_dirs[1]);
}
static void carSaveThumb(int gi,uint16_t*src){ if(g_nocache)return;   // 5.9.32-lab2: NOCACHE - never write a .tnl either
  auto&g=g_games[gi];
  if(!(g.jpg_path.length()>0&&g.jpg_path!="?"))return;
  String root=carThumbRoot(gi); int ri=root.startsWith("/DSK")?1:0;
  if(!(g_thumb_dirs[ri]&(1u<<16))){ if(!SD_MMC.exists(root.c_str()))SD_MMC.mkdir(root.c_str()); g_thumb_dirs[ri]|=1u<<16; }   // lab14f: once, not per tile
  String tp=carThumbPath(gi);
  int sl=tp.lastIndexOf('/'); String dir=tp.substring(0,sl);          // 5.9.0: ensure the shard bucket (.thumbs/<nibble>) exists
  int nb=dir.length()?thumbNib(dir[dir.length()-1]):-1;
  if(nb<0||!(g_thumb_dirs[ri]&(1u<<nb))){ if(!SD_MMC.exists(dir.c_str()))SD_MMC.mkdir(dir.c_str()); if(nb>=0)g_thumb_dirs[ri]|=1u<<nb; }
  File f=SD_MMC.open(tp.c_str(),FILE_WRITE);if(!f)return;
  f.write((uint8_t*)src,(size_t)CAR_TILE*CAR_TILE*2);f.close();
  if(g_tile_stamp && g_fw_drv>=0){                // lab14f: the tile takes its cover's FAT date/time (see above)
    FILINFO fno; memset(&fno,0,sizeof fno); fno.fdate=g_tile_stamp->fdate; fno.ftime=g_tile_stamp->ftime;
    f_utime((String(g_fw_drv)+":"+tp).c_str(),&fno);
  }
}
// Fetch a game's tile (NULL if uncached and decoding isn't allowed right now).
static uint16_t* carTileEx(int gi,bool mayDecode,bool*okOut){
  if(okOut)*okOut=false;
  for(int s=0;s<CAR_SLOTS;s++)if(car_buf[s]&&car_game[s]==gi){car_stamp[s]=++car_tick_ctr;if(okOut)*okOut=(car_ok[s]!=0);return car_buf[s];}
  if(!mayDecode)return NULL;
  int slot=-1;uint32_t old=0xFFFFFFFF;
  for(int s=0;s<CAR_SLOTS;s++){
    if(!car_buf[s]){car_buf[s]=(uint16_t*)ps_malloc((size_t)CAR_TILE*CAR_TILE*2);if(!car_buf[s])continue;car_game[s]=-1;car_stamp[s]=0;}
    if(car_game[s]<0){slot=s;break;}
    if(car_stamp[s]<old){old=car_stamp[s];slot=s;}
  }
  if(slot<0)return NULL;
  car_game[slot]=gi;car_stamp[slot]=++car_tick_ctr;
  bool got=carLoadThumb(gi,car_buf[slot]);                   // fast path: 45 KB raw thumb, zero decode
  if(!got&&carDecodeTile(gi,car_buf[slot])){carSaveThumb(gi,car_buf[slot]);got=true;}   // self-heal fallback (rare)
  car_ok[slot]=got?1:0;
  if(okOut)*okOut=got;
  return car_buf[slot];
}
static uint16_t* carTile(int gi,bool mayDecode){return carTileEx(gi,mayDecode,NULL);}
// Build ALL cover thumbnails up-front — first launch of a card and RESCAN only
// (Michael's call: one predictable pass with a progress bar, never live jank).
// Fresh thumbs are stat-checked and skipped, so a re-run over a built card is
// seconds, not minutes. v4.8.5: carousel is first-class, so thumbs always build.
static void carMicroInit(); static void carMicroFromTile(int gi,const uint16_t*tile); static void carMicroSave(); static bool carMicroActive();  // 5.9.0 fwd-decls (definitions below)
// v5.9.2: mark which games have a real cover (cached thumb present = passed COVERMIN).
static void ensureCoverFlags(){
  if(g_cover_flags_ready && g_cover_flags_n==(int)g_games.size()) return;
  for(size_t i=0;i<g_games.size();i++){
    auto&g=g_games[i]; bool ok=false;
    if(g.jpg_path.length()>0&&g.jpg_path!="?"){
      String vT="/sdcard"+carThumbPath((int)i); struct stat st;
      ok=(stat(vT.c_str(),&st)==0 && st.st_size==(long)((size_t)CAR_TILE*CAR_TILE*2));
    }
    g.cover_ok=ok;
  }
  g_cover_flags_ready=true; g_cover_flags_n=(int)g_games.size();
}
static void buildThumbs(){ if(!g_covers_on){fwLocFree();return;}   // 5.9.32-lab2: COVERS=OFF - nothing to decode
  int n=(int)g_games.size(); if(!n){fwLocFree();return;}
  bcSet(BC_THUMBS,(uint32_t)n);
  thumbIndexBuild();                               // lab14f: every tile's location + date, one walk
  uint32_t nBuilt=0,nReused=0,nNoCover=0,nFast=0,nSlow=0; g_fw_open_at=g_fw_open_at_fail=0;
  uint16_t*tmp=(uint16_t*)ps_malloc((size_t)CAR_TILE*CAR_TILE*2);
  if(!tmp)return;
  carMicroInit();                          // 5.9.0: build the resident micro-thumb set in-line with the thumb pass
  // 5.9.36-lab6: was every 100ms. Each redraw is a full-screen gfx_flush (~36ms)
  // PLUS a gLog, which opens/appends/closes /gti.log on the SAME card the build
  // is reading covers from. Over a 30-minute build that was ~18,000 flushes and
  // ~18,000 file opens competing with the work. Now every 25 games, with an ETA.
  uint32_t t0=millis(); uint32_t lastDraw=0;
  for(int i=0;i<n;i++){
    auto&g=g_games[i];
    if(!g.jpg_path.length()){String jpg;if(findJPGFor(g_files[g.first_file_idx],jpg))g.jpg_path=jpg;else g.jpg_path="?";}
    bool need=false; const FwLoc* J=nullptr; const FwLoc* Tl=nullptr;
    if(g.jpg_path.length()>0&&g.jpg_path!="?"){
      String tp=carThumbPath(i);
      J=fwLocFor(g_coverloc,g.jpg_path);
      if(g_thumb_indexed && J){                    // lab14f: all from memory - no card I/O to decide
        nFast++; Tl=fwLocFor(g_thumbloc,tp);
        need = !Tl || Tl->size!=TILE_BYTES || Tl->fdate!=J->fdate || Tl->ftime!=J->ftime;
      } else {                                     // not in the walk: the old by-name checks
        nSlow++;
        String vT="/sdcard"+tp,vJ=String("/sdcard")+g.jpg_path.c_str();
        struct stat stT,stJ;
        if(stat(vT.c_str(),&stT)!=0)need=true;
        else if(stT.st_size!=(long)((size_t)CAR_TILE*CAR_TILE*2))need=true;
        else if(stat(vJ.c_str(),&stJ)==0&&stJ.st_mtime>stT.st_mtime)need=true;
      }
    } else nNoCover++;
    bool haveTile=false;                                     // 5.9.0: seed the micro-thumb in the SAME pass (no second 45 MB re-read)
    if(need){ g_tile_stamp=J; if(carDecodeTile(i,tmp)){carSaveThumb(i,tmp);haveTile=true;nBuilt++;} g_tile_stamp=nullptr; }
    else if(Tl && !carMicroActive()){ nReused++; }  // lab14f: tile is fresh and there are no micro-thumbs to seed - don't read it at all
    else if(Tl && fwReadAt(Tl,(uint8_t*)tmp,TILE_BYTES)){ haveTile=true; nReused++; }   // lab14f: tile read from its location
    else     { haveTile=carLoadThumb(i,tmp); if(haveTile)nReused++; }
    if(haveTile)carMicroFromTile(i,tmp);
    if((i%200)==0||i==n-1){ g_bc_n=(uint32_t)i; g_bc_psram=(uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM); }
    if((i%1000)==0||i==n-1)gLog("[thumbs] %d/%d %lums int=%u psram=%u\n",i+1,n,(unsigned long)(millis()-t0),(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),(unsigned)ESP.getFreePsram());
    if(millis()-lastDraw>=1000||i==n-1){   // lab15f: redraw once a second (was every 25 games: 800 full-screen flushes, ~29 s, on a 20k card with no covers); log every 1000 (was 200)
      lastDraw=millis();
      gfx_fillScreen(0x1082);
      gfx_setTextSize(2);gfx_setTextColor(0xFC60,0x1082);
      {const char*s=T(L_BUILDING);int tw=gfx_textWidth(s);gfx_setCursor((gW-tw)/2,gH/2-50);gfx_print(s);}
      gfx_setTextSize(1);gfx_setTextColor(0x9BD6,0x1082);
      {String m=String(i+1)+" / "+String(n);int tw=gfx_textWidth(m);gfx_setCursor((gW-tw)/2,gH/2-22);gfx_print(m);}
      int bw2=gW-120,bx=60,by=gH/2;
      gfx_drawRect(bx,by,bw2,12,0x4A8A);
      gfx_fillRect(bx+2,by+2,(int)((long)(bw2-4)*(i+1)/n),8,0x07E0);
      {String e=etaStr(millis()-t0,i+1,n);
       if(e.length()){gfx_setTextColor(0xFC60,0x1082);int tw=gfx_textWidth(e);gfx_setCursor((gW-tw)/2,gH/2+16);gfx_print(e);}}
      gfx_setTextColor(0x4A8A,0x1082);
      {const char*s=T(L_ONEOFF);int tw=gfx_textWidth(s);gfx_setCursor((gW-tw)/2,gH/2+34);gfx_print(s);}
      gfx_flush();
    }
    if((i&7)==0)yield();
  }
  free(tmp);
  gLog("[thumbs] done %d games in %lums: built %u, reused %u, no cover %u | decided from memory %u, by name %u | opened from walk %u (failed %u)\n",
       n,(unsigned long)(millis()-t0),(unsigned)nBuilt,(unsigned)nReused,(unsigned)nNoCover,(unsigned)nFast,(unsigned)nSlow,(unsigned)g_fw_open_at,(unsigned)g_fw_open_at_fail);
  fwLocFree();        // lab14f: locations have done their job
  carMicroSave();     // 5.9.0: persist /.gti_micro.pk now — the reel's "Preparing covers" pass never runs again
  writeGameCache();   // persist jpg paths resolved during the build (faster covers later too)
  g_coverset.clear();   // 5.3.7: cover set has done its job — free the PSRAM
}

static inline uint16_t carDim(uint16_t c,int lvl){
  if(lvl<=0)return c;
  if(lvl==1)return (uint16_t)((c>>1)&0x7BEF);    // ~50%
  return (uint16_t)((c>>2)&0x39E7);              // ~25%
}
// Blit a tile scaled to w x h centred at (cx,cy), dim level 0..2, nearest-neighbour.
// == V1: resident micro-thumbnail set =========================================
// A tiny RGB565 copy of every cover, always in PSRAM. On a cache miss the reel
// blits the upscaled micro instead of the COL_BAR grey square -> blurry-then-
// sharp, never a coloured square, independent of SD speed.
#ifndef SD_LOCK
#define SD_LOCK()   do{}while(0)
#define SD_UNLOCK() do{}while(0)
#endif
static uint16_t* car_micro_block=NULL;   // n*dim*dim RGB565, contiguous
static bool carMicroActive(){ return car_micro_block!=NULL; }   // lab14f
static int       g_car_micro_dim=0;      // 8/16/24/32 (0 = disabled) - lab14e: 8 when memory is tight
static int       g_car_micro_n=0;        // games covered
static uint32_t carGamesSig(){
  uint32_t h=2166136261u; int n=(int)g_games.size();
  h^=(uint32_t)n; h*=16777619u;
  for(int i=0;i<n;i++){const String&p=g_files[g_games[i].first_file_idx];
    for(unsigned k=0;k<p.length();k++){h^=(uint8_t)p[k]; h*=16777619u;}}
  return h;
}
// ── 5.9.31-lab1 .nfocache — the sidecar results, persisted per side ─────────
// Binary: [magic 'GTN1'][count][sig=carGamesSig()][reserved], then per game in g_games
// order: [u8 flags][u16 len][len bytes of blurb]. Keyed by the SAME signature as the reel
// micro cache, so an unchanged library reuses it and a rescan that changes the game set
// rebuilds it. Warm boots therefore restore every sidecar answer with ZERO card I/O.
#define NFOCACHE_MAGIC 0x47544E31u
#define NFOF_MANUAL 0x01
#define NFOF_HD     0x02
static String nfoCachePath(){return g_mode==MODE_ADF?"/ADF/.nfocache":g_mode==MODE_DSK?"/DSK/.nfocache":"/GENERIC/.nfocache";}
static void writeNfoCache(){ if(g_nocache)return;
  File f=SD_MMC.open(nfoCachePath().c_str(),FILE_WRITE); if(!f)return;
  uint32_t hdr[4]={NFOCACHE_MAGIC,(uint32_t)g_games.size(),carGamesSig(),0};
  { BufWr w(f);                                   // lab15f: 16 KB blocks, same bytes
  w.write(hdr,16);
  for(auto&g:g_games){
    uint8_t fl=(g.has_manual?NFOF_MANUAL:0)|(g.is_hd?NFOF_HD:0);
    uint32_t bl=g.blurb.length(); if(bl>(uint32_t)NFO_BLURB_MAX)bl=NFO_BLURB_MAX;
    uint16_t L=(uint16_t)bl;
    w.write(&fl,1); w.write(&L,2); if(L)w.write(g.blurb.c_str(),L);
  } }
  f.close();
  gLog("[nfocache] wrote %d entries sig=%08X\n",(int)g_games.size(),(unsigned)carGamesSig());
}
static void writeNfoCacheIfChanged(){   // the signature guard: an unchanged library is not rewritten
  File f=SD_MMC.open(nfoCachePath().c_str(),FILE_READ);
  if(f){uint32_t hdr[4]; bool ok=(f.read((uint8_t*)hdr,16)==16); f.close();
    if(ok&&hdr[0]==NFOCACHE_MAGIC&&(int)hdr[1]==(int)g_games.size()&&hdr[2]==carGamesSig())return;}
  writeNfoCache();
}
static bool loadNfoCache(){ if(g_nocache)return false;
  File f=SD_MMC.open(nfoCachePath().c_str(),FILE_READ); if(!f)return false;
  uint32_t hdr[4]; if(f.read((uint8_t*)hdr,16)!=16){f.close();return false;}
  if(hdr[0]!=NFOCACHE_MAGIC||(int)hdr[1]!=(int)g_games.size()||hdr[2]!=carGamesSig()){f.close();
    gLog("[nfocache] stale/mismatched - sidecars fall back to on-demand reads\n"); return false;}
  std::vector<char>buf(NFO_BLURB_MAX+1);
  uint32_t _t0=millis();
  { BufRd r(f);                                   // lab15f: 16 KB block reads instead of three tiny reads per game
  for(auto&g:g_games){
    uint8_t fl=0; uint16_t L=0;
    if(r.read(&fl,1)!=1||r.read((uint8_t*)&L,2)!=2){f.close();return false;}
    if(L>(uint16_t)NFO_BLURB_MAX){f.close();return false;}
    if(L){ size_t got=r.read((uint8_t*)buf.data(),L); if(got!=(size_t)L){f.close();return false;} buf[L]=0; g.blurb.set(buf.data(),strlen(buf.data())); }   // as String(buf): stops at a NUL
    else g.blurb="";
    g.has_manual=(fl&NFOF_MANUAL)!=0; g.is_hd=(fl&NFOF_HD)!=0; g.nfo_done=true;
  } }
  f.close();
  gLog("[nfocache] read in %lums\n",(unsigned long)(millis()-_t0));
  gLog("[nfocache] loaded %d entries - zero sidecar I/O this boot\n",(int)g_games.size());
  return true;
}
static void carMicroFree(){ if(car_micro_block){free(car_micro_block);car_micro_block=NULL;} g_car_micro_dim=0; g_car_micro_n=0; }
// lab14e: a RESCAN sizes the new library from free PSRAM, so first hand back what the old
// library's reel was holding (up to 2.1 MB of cached tiles + the micro-thumb block). Both
// refill on demand; every reader treats a NULL buffer as "not cached".
static void carRuntimeRelease(){
  carMicroFree();
  for(int s=0;s<CAR_SLOTS;s++){ if(car_buf[s]){ free(car_buf[s]); car_buf[s]=NULL; } car_game[s]=0; car_stamp[s]=0; car_ok[s]=0; }
}
static void carMicroInit(){
  carMicroFree();
  int n=(int)g_games.size(); if(!n)return;
  int dim=(n<=1000)?32:(n<=1800)?24:16;
  // lab14e: this block was never in the memory budget. At 16x16 it is 512 B a game - 3.9 MB
  // at 7,600 games, enough to starve the reel tiles, cover decode and screensaver. Take the
  // biggest size that still leaves the runtime reserve free, then a new 8x8 tier (128 B a
  // game), then none: the reel just shows its plain placeholder until a tile loads.
  size_t bytes=0;
  for(;;){
    bytes=(size_t)n*dim*dim*2;
    size_t fr=heap_caps_get_free_size(MALLOC_CAP_SPIRAM), big=heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    if(fr>bytes+(size_t)LIB_RUNTIME_RESERVE && big>=bytes){ car_micro_block=(uint16_t*)ps_malloc(bytes); if(car_micro_block)break; }
    if(dim>16)dim=16; else if(dim==16)dim=8;
    else { gLog("[micro] no room for micro-thumbs (%d games, psram %u) - reel uses placeholders\n",n,(unsigned)fr); return; }
  }
  g_car_micro_dim=dim; g_car_micro_n=n;
  gLog("[micro] %dx%d micro-thumbs for %d games: %u KB\n",dim,dim,n,(unsigned)(bytes/1024));
  size_t px=bytes/2; for(size_t i=0;i<px;i++)car_micro_block[i]=COL_BAR;
}
static void carMicroFromTile(int gi,const uint16_t*tile){
  if(!car_micro_block||gi<0||gi>=g_car_micro_n||!tile)return;
  int d=g_car_micro_dim; uint16_t*m=car_micro_block+(size_t)gi*d*d;
  for(int y=0;y<d;y++){int sy=y*CAR_TILE/d;
    for(int x=0;x<d;x++){int sx=x*CAR_TILE/d; m[y*d+x]=tile[sy*CAR_TILE+sx];}}
}
static inline uint16_t* carMicro(int gi){
  if(!car_micro_block||gi<0||gi>=g_car_micro_n)return NULL;
  return car_micro_block+(size_t)gi*g_car_micro_dim*g_car_micro_dim;
}
static bool carMicroLoad(){ if(g_nocache)return false;
  if(!car_micro_block)return false;
  File f=SD_MMC.open(gtiStatePath(".gti_micro.pk",true).c_str(),"r"); if(!f)return false;   // lab14g: /GTI
  uint32_t hdr[4]; if(f.read((uint8_t*)hdr,16)!=16){f.close();return false;}
  if(hdr[0]!=0x47544D31u||(int)hdr[1]!=g_car_micro_dim||(int)hdr[2]!=g_car_micro_n||hdr[3]!=carGamesSig()){f.close();return false;}
  size_t want=(size_t)g_car_micro_n*g_car_micro_dim*g_car_micro_dim*2;
  size_t got=f.read((uint8_t*)car_micro_block,want); f.close();
  return got==want;
}
static void carMicroSave(){ if(g_nocache)return;
  if(!car_micro_block)return;
  File f=SD_MMC.open(gtiStatePath(".gti_micro.pk",false).c_str(),FILE_WRITE); if(!f)return;
  uint32_t hdr[4]={0x47544D31u,(uint32_t)g_car_micro_dim,(uint32_t)g_car_micro_n,carGamesSig()};
  f.write((uint8_t*)hdr,16);
  f.write((uint8_t*)car_micro_block,(size_t)g_car_micro_n*g_car_micro_dim*g_car_micro_dim*2);
  f.close();
}
static void carMicroBuild(){
  int n=g_car_micro_n; if(!n||!car_micro_block)return;
  uint16_t*tmp=(uint16_t*)ps_malloc((size_t)CAR_TILE*CAR_TILE*2); if(!tmp)return;
  uint32_t t0=millis(); uint32_t lastDraw=0;   // 5.9.36-lab6: count-driven, was every 120ms; lab15f: once a second
  gLog("[micro] build start: %d games | int=%u largest-int=%u psram=%u\n",n,(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),(unsigned)ESP.getFreePsram());
  uint32_t nTile=0,nDec=0,nNone=0;
  for(int i=0;i<n;i++){
    uint32_t tg=millis();
    if(i%1000==0) gLog("[micro] %d/%d at %lus | tiles %u decoded %u none %u | int=%u psram=%u\n",i,n,(unsigned long)((millis()-t0)/1000),(unsigned)nTile,(unsigned)nDec,(unsigned)nNone,(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),(unsigned)ESP.getFreePsram());   // lab15a3
    SD_LOCK();
    bool ok=carLoadThumb(i,tmp);
    if(ok) nTile++;
    else {
      const String& jp=g_games[i].jpg_path;
      if(jp.length()&&jp!="?") gLog("[micro] game %d: no tile, decoding %s\n",i,jp.c_str());   // lab15a3: name it BEFORE the decode, so a hang points at the file
      else if(!jp.length()) gLog("[micro] game %d: cover unknown, searching by name (%s)\n",i,g_games[i].name.c_str());
      ok=carDecodeTile(i,tmp); if(ok) nDec++; else nNone++;
    }
    SD_UNLOCK();
    { uint32_t dt=millis()-tg; if(dt>2000) gLog("[micro] game %d took %lums (%s)\n",i,(unsigned long)dt,g_games[i].name.c_str()); }
    if(ok)carMicroFromTile(i,tmp);
    if(millis()-lastDraw>=1000||i==n-1){ lastDraw=millis();   // lab15f: was every 25 games (a ~36 ms full-screen flush each)
      gfx_fillScreen(0x1082);
      gfx_setTextSize(2);gfx_setTextColor(0xFC60,0x1082);
      {const char*s="Preparing covers";int tw=gfx_textWidth(s);gfx_setCursor((gW-tw)/2,gH/2-40);gfx_print(s);}
      gfx_setTextSize(1);gfx_setTextColor(0x9BD6,0x1082);
      {String m=String(i+1)+" / "+String(n);int tw=gfx_textWidth(m);gfx_setCursor((gW-tw)/2,gH/2-16);gfx_print(m);}
      int bw2=gW-120,bx=60,by=gH/2;
      gfx_drawRect(bx,by,bw2,12,0x4A8A);
      gfx_fillRect(bx+2,by+2,(int)((long)(bw2-4)*(i+1)/n),8,0x07E0);
      {String e=etaStr(millis()-t0,i+1,n);
       if(e.length()){gfx_setTextColor(0xFC60,0x1082);int tw=gfx_textWidth(e);gfx_setCursor((gW-tw)/2,gH/2+20);gfx_print(e);}}
      gfx_flush();
    }
    if((i&15)==0)yield();
  }
  free(tmp);
  gLog("[micro] build done: %d games in %lus | tiles %u decoded %u none %u\n",n,(unsigned long)((millis()-t0)/1000),(unsigned)nTile,(unsigned)nDec,(unsigned)nNone);   // lab15a3
  SD_LOCK(); carMicroSave(); SD_UNLOCK();
}
static uint16_t* neoMicroTile(int gi,int*dim){ uint16_t*m=carMicro(gi); *dim=m?g_car_micro_dim:0; return m; }   // lab16b-P4
static void neoMicroWarm(){   // lab16b-P4: list thumbnails = the reel's micro-thumbs. Load the saved pack once per library;
  static int tried=-1;        //   if there is none (or it is stale) the list shows initials until the reel has built it.
  if(!g_covers_on||car_micro_block||g_games.empty()||tried==(int)g_games.size())return;
  tried=(int)g_games.size();
  carMicroInit(); if(!car_micro_block)return;
  bool hit; SD_LOCK(); hit=carMicroLoad(); SD_UNLOCK();
  if(!hit){ carMicroFree(); gLog("[neo] no micro-thumb pack yet - list shows initials until the reel builds it\n"); }
}
static void carMicroEnsure(){
  if(car_micro_block&&g_car_micro_n==(int)g_games.size())return;
  carMicroInit();
  if(!car_micro_block)return;
  bool hit; SD_LOCK(); hit=carMicroLoad(); SD_UNLOCK();
  if(!hit)carMicroBuild();
}

// 5.9.33-lab3 — THE reel bottleneck, and it has nothing to do with covers.
//
// The old body did, for EVERY pixel: two 32-bit divides, a call to gfx_drawPixel,
// four clip compares, a call to fb_setPixel, a rotation switch, a swap16 and a
// store. ~46,000 of those per frame (150x150 centre + four side tiles). Worse: in
// the default landscape rotation a virtual ROW maps to a framebuffer COLUMN, so
// consecutive writes were 640 bytes apart in a 307 KB PSRAM framebuffer — a cache
// line fetched, two bytes written, evicted, and never reused. A miss on literally
// every pixel, every frame, whether the tile held artwork or flat grey.
//
// Now: the two axis maps are built once (w+h divides instead of w*h*2), and the
// INNER loop walks whichever axis is contiguous in the physical framebuffer for
// the current rotation, writing straight into the row. Same pixels, same output.
static int g_cb_sxm[CAR_TILE+8],g_cb_sym[CAR_TILE+8];   // UI core only — drawCarousel is never re-entered
// lab15l: the picture inside a square tile. Tiles are aspect-fit with a flat letterbox (the bar colour at build
// time), so rows/columns that are entirely the corner colour are letterbox. No letterbox (both corners differ, or
// the picture reaches the corner) = the whole tile. ~22k compares on a 150 px tile, once per selection.
static void tilePicRect(const uint16_t*t,int n,int&x0,int&y0,int&w,int&h){
  x0=0;y0=0;w=n;h=n; if(!t||n<16)return;
  const uint16_t pad=t[0]; if(t[(size_t)n*n-1]!=pad)return;
  auto rowPad=[&](int y){const uint16_t*r=t+(size_t)y*n;for(int x=0;x<n;x++)if(r[x]!=pad)return false;return true;};
  auto colPad=[&](int x){for(int y=0;y<n;y++)if(t[(size_t)y*n+x]!=pad)return false;return true;};
  int top=0,bot=n-1,lft=0,rgt=n-1;
  while(top<bot&&rowPad(top))top++; while(bot>top&&rowPad(bot))bot--;
  while(lft<rgt&&colPad(lft))lft++; while(rgt>lft&&colPad(rgt))rgt--;
  if(rgt-lft+1<8||bot-top+1<8)return;   // (almost) all one colour - keep the whole tile
  // lab15m: then trim black borders that are part of the picture itself - most Amiga screenshots are a 320x200 game in a
  // PAL 256-line (or overscan) screen, so the cover carries black bands the letterbox pass can't see. A row/column counts
  // when every pixel is near-black (JPEG noise allowed); at most 1/6 is trimmed from each side (PAL bands are ~11%), so a dark scene keeps most of its picture.
  auto blk=[](uint16_t c){return ((c>>11)&31)<=3&&((c>>5)&63)<=6&&(c&31)<=3;};
  auto rowBlk=[&](int y){const uint16_t*r=t+(size_t)y*n;for(int x=lft;x<=rgt;x++)if(!blk(r[x]))return false;return true;};
  auto colBlk=[&](int x){for(int y=top;y<=bot;y++)if(!blk(t[(size_t)y*n+x]))return false;return true;};
  const int capV=(bot-top+1)/6, capH=(rgt-lft+1)/6;
  for(int k=0;k<capV&&rowBlk(top);k++)top++;  for(int k=0;k<capV&&rowBlk(bot);k++)bot--;
  for(int k=0;k<capH&&colBlk(lft);k++)lft++;  for(int k=0;k<capH&&colBlk(rgt);k++)rgt--;
  x0=lft;y0=top;w=rgt-lft+1;h=bot-top+1;
}
static void carBlit(uint16_t*tile,int srcDim,int cx,int cy,int w,int h,int dim){
  if(w<=0||h<=0||srcDim<=0)return;
  int x0=cx-w/2,y0=cy-h/2;
  if(w>CAR_TILE+8||h>CAR_TILE+8||!framebuffer){          // paranoia fallback: the old, slow, always-correct path
    for(int dy=0;dy<h;dy++){int sy=g_cb_sw?g_cb_sy0+dy*g_cb_sh/h:dy*srcDim/h;
      for(int dx=0;dx<w;dx++){int sx=g_cb_sw?g_cb_sx0+dx*g_cb_sw/w:dx*srcDim/w;
        gfx_drawPixel(x0+dx,y0+dy,carDim(tile?tile[sy*srcDim+sx]:COL_BAR,dim));}}
    return;
  }
  int dx0=max(0,g_clip_x0-x0),dx1=min(w,g_clip_x1-x0);   // clip once, in virtual space
  int dy0=max(0,g_clip_y0-y0),dy1=min(h,g_clip_y1-y0);
  if(dx0>=dx1||dy0>=dy1)return;
  if(g_cb_sw){ for(int dx=dx0;dx<dx1;dx++)g_cb_sxm[dx]=g_cb_sx0+(dx*g_cb_sw)/w;      // lab15l: part of the tile
               for(int dy=dy0;dy<dy1;dy++)g_cb_sym[dy]=g_cb_sy0+(dy*g_cb_sh)/h; }
  else{
  for(int dx=dx0;dx<dx1;dx++)g_cb_sxm[dx]=(dx*srcDim)/w;
  for(int dy=dy0;dy<dy1;dy++)g_cb_sym[dy]=(dy*srcDim)/h;
  }
  const uint16_t flat=swap16(carDim(COL_BAR,dim));
  if(g_rot==0||g_rot==2){                                // landscape: virtual Y is the contiguous axis
    for(int dx=dx0;dx<dx1;dx++){
      int vx=x0+dx,sx=g_cb_sxm[dx];
      int py=(g_rot==0)?(LCD_HEIGHT-1-vx):vx;
      if((unsigned)py>=(unsigned)LCD_HEIGHT)continue;
      uint16_t*row=&framebuffer[(size_t)py*LCD_WIDTH];
      if(!tile){ for(int dy=dy0;dy<dy1;dy++){int vy=y0+dy;int px=(g_rot==0)?vy:(LCD_WIDTH-1-vy);
                   if((unsigned)px<(unsigned)LCD_WIDTH)row[px]=flat;} continue; }
      for(int dy=dy0;dy<dy1;dy++){
        int vy=y0+dy,px=(g_rot==0)?vy:(LCD_WIDTH-1-vy);
        if((unsigned)px>=(unsigned)LCD_WIDTH)continue;
        row[px]=swap16(carDim(tile[(size_t)g_cb_sym[dy]*srcDim+sx],dim));
      }
    }
  }else{                                                 // portrait: virtual X is the contiguous axis
    for(int dy=dy0;dy<dy1;dy++){
      int vy=y0+dy;size_t so=(size_t)g_cb_sym[dy]*srcDim;
      int py=(g_rot==1)?vy:(LCD_HEIGHT-1-vy);
      if((unsigned)py>=(unsigned)LCD_HEIGHT)continue;
      uint16_t*row=&framebuffer[(size_t)py*LCD_WIDTH];
      if(!tile){ for(int dx=dx0;dx<dx1;dx++){int vx=x0+dx;int px=(g_rot==1)?vx:(LCD_WIDTH-1-vx);
                   if((unsigned)px<(unsigned)LCD_WIDTH)row[px]=flat;} continue; }
      for(int dx=dx0;dx<dx1;dx++){
        int vx=x0+dx,px=(g_rot==1)?vx:(LCD_WIDTH-1-vx);
        if((unsigned)px>=(unsigned)LCD_WIDTH)continue;
        row[px]=swap16(carDim(tile[so+g_cb_sxm[dx]],dim));
      }
    }
  }
}

// The d6 overlay: pips while rolling, final face at rest — or "23" on the lucky roll.
static void carDrawDie(){
  int s=26,bw=g_neo?VW/4:VW/3,x=(g_neo?3:2)*bw+(bw-s)/2,y=VH-BOTTOM_H-s-14;   // v5.4.2: hover a few lines ABOVE the ROLL button (dice / gap / button); lab16a-P4: NEO ROLL is slot 4 of 4
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

// lab16c-P4: the picture inside a reel tile (tile pixels), found once per game from its full tile and kept in a small cache.
struct NeoCrop{ int gi; uint8_t x,y,w,h; };
static NeoCrop g_neo_crop[64];
static bool g_neo_crop_init=false;
static bool neoCropGet(int gi,uint16_t*tile,int&x,int&y,int&w,int&h){
  if(!g_neo_crop_init){ for(int i=0;i<64;i++)g_neo_crop[i].gi=-1; g_neo_crop_init=true; }
  NeoCrop&c=g_neo_crop[gi&63];
  if(c.gi!=gi){ if(!tile)return false;
    int px,py,pw,ph; tilePicRect(tile,CAR_TILE,px,py,pw,ph);
    if(pw<8||ph<8){px=0;py=0;pw=CAR_TILE;ph=CAR_TILE;}
    c.gi=gi; c.x=(uint8_t)px; c.y=(uint8_t)py; c.w=(uint8_t)pw; c.h=(uint8_t)ph; }
  x=c.x;y=c.y;w=c.w;h=c.h; return true;
}
// lab16e-P4: centre line of the reel covers. Landscape: just under the top bar (as before). NEO portrait: the cover + title +
// disks + dots block (about 120 above the centre line, 226 below) sits in the middle of the free space.
#define NEO_VINFO_H 120   // lab16f-P4: portrait reel - height of the title / disks / dots strip above the bottom buttons
static inline bool carVert(){ return g_neo&&g_portrait; }   // lab16f-P4: portrait reel runs up and down
static int carVInfoTop(){ return VH-BOTTOM_H-NEO_VINFO_H; }
static int carCcy(){ if(carVert()) return (STATUS_H+carVInfoTop())/2; return STATUS_H+12+CAR_TILE/2; }
static void drawCarousel(){
  NeoScope _ns; neoBgEnsure();   // lab16a-P4
  uint32_t _rp_f0=micros();
  if(g_reelprof&&g_rp_frames&&millis()-g_rp_t0>=1500){   // 5.9.33-lab3: where did the frame actually go?
    uint32_t f=g_rp_frames,span=millis()-g_rp_t0;
    unsigned long fps10=span?(unsigned long)((10000UL*f)/span):0UL;   // tenths - newlib-nano printf may have no %f
    gLog("[reel] %lu frames/%lums = %lu.%lu fps | frame draw %luus (clear %lu blit %lu sav %lu) + flush %luus\n",
         (unsigned long)f,(unsigned long)span,fps10/10,fps10%10,
         (unsigned long)(g_rp_draw/f),(unsigned long)(g_rp_clear/f),(unsigned long)(g_rp_blit/f),
         (unsigned long)(g_rp_sav/f),(unsigned long)(g_rp_flush/f));
    g_rp_frames=g_rp_draw=g_rp_clear=g_rp_blit=g_rp_sav=g_rp_flush=0; g_rp_t0=millis();
  }
  if(!g_rp_t0)g_rp_t0=millis();
  drawStatusBar();
  {uint32_t _c0=micros(); fillBack(0,STATUS_H,VW,VH-STATUS_H-BOTTOM_H); g_rp_clear+=micros()-_c0;}   // lab16a-P4: NEO background
  int n=carN();
  int ccx=VW/2, ccy=carCcy();                            // center cover (lab16e-P4: centred in NEO portrait)
  g_car_disk_n=0;                                        // reset reel disk-button rect each frame; set below if multi-disk
  if(n==0){
    gfx_setTextSize(2);gfx_setTextColor(COL_LIT,COL_BG);
    String m=(g_car_src==1)?T(L_NO_FAVS):T(L_NO_GAMES);
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
    // 5.9.33-lab3: ...but ONLY when the reel is settled. savExistsFor() is an
    // SD_MMC.exists() and it almost always MISSES, which on FAT costs a full walk
    // of that folder — and the centre game changes on nearly every frame of a
    // flip, so this was one whole directory walk PER FRAME. It is the only piece
    // of card I/O left in the reel path, and it is why the reel stayed slow with
    // cover art switched off entirely. The badge now appears when you stop.
    static int carSavSel=-1;static bool g_car_hasSav=false;
    if(!moving){uint32_t _s0=micros();
     int cgi=g_car_list[carWrap(ci)];
     if(carSavSel!=cgi){carSavSel=cgi;g_car_hasSav=savBadgeFor(g_files[g_games[cgi].first_file_idx]);}   // lab14i: badge from the save list, no card lookup
     g_rp_sav+=micros()-_s0;}
    const bool showSav=(!moving)&&g_car_hasSav;
    // Warm the cache CENTER-FIRST when settled (painter's order would decode
    // the side covers before the star of the show — backwards for the eye).
    if(!moving){
      carTile(g_car_list[carWrap(ci)],true);
      if(maxOff>=1){carTile(g_car_list[carWrap(ci-1)],true);carTile(g_car_list[carWrap(ci+1)],true);}
    }
    // far to near, center last (painter's order)
    const bool vert=carVert(); int _cy0=g_clip_y0,_cy1=g_clip_y1;
    if(vert){ g_clip_y0=STATUS_H; g_clip_y1=carVInfoTop()-4; }   // lab16f-P4: covers stay inside the reel area
    static const int order[5]={-2,2,-1,1,0};
    for(int oi=0;oi<5;oi++){
      int off=order[oi];if(abs(off)>maxOff)continue;
      int gi=g_car_list[carWrap(ci+off)];
      float rel=(float)off-frac;
      float ar=fabsf(rel);if(ar>2.6f)continue;
      int x=ccx+(int)(rel*161.0f*(1.0f-min(ar,1.0f)*0.22f));   // P4: wider reel spacing (220 px tiles)
      int cy=ccy;
      float scale=1.0f-ar*0.28f;if(scale<0.42f)scale=0.42f;
      float squash=1.0f-ar*0.20f;if(squash<0.55f)squash=0.55f;
      int h=(int)(CAR_TILE*scale),w=(int)(CAR_TILE*scale*squash);
      if(vert){ x=ccx; cy=ccy+(int)(rel*175.0f*(1.0f-min(ar,1.0f)*0.22f)); w=(int)(CAR_TILE*scale); h=(int)(CAR_TILE*scale*squash); }   // lab16f-P4: stacked, squashed in height
      int dim=(ar<0.5f)?0:((ar<1.6f)?1:2);
      // lab16c-P4 NEO: shrink the box to the picture inside the tile (no grey letterbox), as big as the square allows
      int cpx=0,cpy=0,cpw=CAR_TILE,cph=CAR_TILE; bool crop=false;
      if(g_neo&&g_covers_on&&g_games[gi].jpg_path!="?"){
        bool ok0=false; uint16_t*t0=carTileEx(gi,false,&ok0); if(!ok0)t0=NULL;   // only a tile that really decoded
        crop=neoCropGet(gi,t0,cpx,cpy,cpw,cph)&&(cpw<CAR_TILE||cph<CAR_TILE);
        if(crop){ float e=min((float)CAR_TILE/cpw,(float)CAR_TILE/cph); int nw=(int)(cpw*e*w/CAR_TILE), nh=(int)(cph*e*h/CAR_TILE);
          if(nw<4||nh<4)crop=false; else { w=nw; h=nh; } } }
      {uint32_t _b0=micros();
      if(!g_covers_on){
        // 5.9.33-lab3: COVERS=OFF now really means OFF. It used to still fetch a
        // tile (carTile hands back its COL_BAR-prefilled buffer even when the
        // decode fails) and run the full per-pixel scaler over it — so the reel
        // paid the entire cover-rendering cost to draw flat grey. A rect is a rect.
        gfx_fillRect(x-w/2,cy-h/2,w,h,carDim(COL_BAR,dim));
      }else{
      uint16_t*tile=carTile(gi,!moving&&ar<1.6f);
      if(crop&&tile){ g_cb_sx0=cpx;g_cb_sy0=cpy;g_cb_sw=cpw;g_cb_sh=cph; }
      if(tile){ carBlit(tile,CAR_TILE,x,cy,w,h,dim); g_cb_sw=0;g_cb_sh=0;
#if CAR_BENCH
        g_bench_sharp++;
#endif
      }else{ uint16_t*m=carMicro(gi);
        if(crop&&m){ int md=g_car_micro_dim; g_cb_sx0=cpx*md/CAR_TILE;g_cb_sy0=cpy*md/CAR_TILE;g_cb_sw=max(1,cpw*md/CAR_TILE);g_cb_sh=max(1,cph*md/CAR_TILE); }
        carBlit(m,m?g_car_micro_dim:CAR_TILE,x,cy,w,h,dim); g_cb_sw=0;g_cb_sh=0;
#if CAR_BENCH
        if(m)g_bench_micro++; else g_bench_square++;
#endif
      }
      }
      g_rp_blit+=micros()-_b0;}
      auto&gm=g_games[gi];
      bool isLd=(g_loaded&&g_loaded_game_idx==gi);
      if(g_neo){   // lab16a-P4: centre cover = gold frame + glow (green when it is in the drive); sides = thin slate line
        if(ar<0.5f){ uint16_t fc=isLd?COL_GREEN:COL_AMBER; neoGlow(x-w/2-4,cy-h/2-4,w+8,h+8,10,fc,8); neoFrame(x-w/2-1,cy-h/2-1,w+2,h+2,2,fc,4); }
        else if(g_reelborder) gfx_drawRect(x-w/2-1,cy-h/2-1,w+2,h+2,COL_SEP);
      } else {
      if(g_reelborder){   // MasterTelly CR: REELBORDER=OFF drops the per-cover frame for a clean, frameless reel
        uint16_t bord=(ar<0.5f)?(isLd?COL_GREEN:COL_AMBER):COL_ACCENT;
        gfx_drawRect(x-w/2-1,cy-h/2-1,w+2,h+2,bord);
      }
      if(isLd)gfx_drawRect(x-w/2-2,cy-h/2-2,w+4,h+4,COL_GREEN);   // loaded game stays marked green even with the border off
      }
      // no-art placeholder letter
      if(gm.jpg_path=="?"){int ls=(w>=110)?4:2;char ib[2]={(char)toupper(gm.name.charAt(0)),0};
        gfx_setTextSize(ls);gfx_setTextColor(carDim(COL_LIT,dim),carDim(COL_BAR,dim));
        gfx_setCursor(x-3*ls,cy-4*ls);gfx_print(ib);}
      // favourite star on the center cover
      if(ar<0.5f&&gm.fav)gfx_fillStar(x+w/2-13,cy-h/2+13,9.0f,COL_STAR);
      // v4.8.0: floppy icon on the center cover — a save-copy exists for this game
      if(ar<0.5f&&showSav)drawSaveFloppy(x-w/2+4,cy-h/2+4);
    }
    g_clip_y0=_cy0; g_clip_y1=_cy1;
    // center title + info
    int gi=g_car_list[carWrap(ci)];
    auto&game=g_games[gi];
    // v5.7.x: reset the reel disk selection when the centered game changes
    static int carDiskGi=-1;
    if(carDiskGi!=gi){carDiskGi=gi; g_disk_sel=(g_loaded&&g_loaded_game_idx==gi)?g_loaded_disk_idx:0;}
    int neoY=carVert()?carVInfoTop()+6:ccy+CAR_TILE/2+14;   // lab16a-P4: NEO puts title / disks / dots UNDER the cover (lab16f: portrait = the fixed strip)
    if(g_neo){ gfx_setTextColor(TFT_WHITE,COL_BG); const int maxW=VW-40; String t=game.name;   // lab16c-P4: no cut-off names
      gfx_setTextSize(3);
      if(gfx_textWidth(t)<=maxW){ gfx_setCursor((VW-gfx_textWidth(t))/2,neoY);gfx_print(t); }
      else { gfx_setTextSize(2);
        if(gfx_textWidth(t)<=maxW){ gfx_setCursor((VW-gfx_textWidth(t))/2,neoY+4);gfx_print(t); }
        else { int best=-1,bd=99999; for(int i=1;i<(int)t.length();i++) if(t[i]==' '){ int d=abs(i-(int)t.length()/2); if(d<bd){bd=d;best=i;} }
          String a=best>0?t.substring(0,best):t, b=best>0?t.substring(best+1):String("");
          while(gfx_textWidth(a)>maxW&&a.length()>3)a.remove(a.length()-1);
          while(gfx_textWidth(b)>maxW&&b.length()>3)b.remove(b.length()-1);
          gfx_setCursor((VW-gfx_textWidth(a))/2,neoY-6);gfx_print(a);
          gfx_setCursor((VW-gfx_textWidth(b))/2,neoY+14);gfx_print(b);
          neoY+=14; } } }
    else{
    gfx_setTextSize(2);gfx_setTextColor(COL_LIT,COL_BG);
    String t=game.name;while(gfx_textWidth(t)>VW-24&&t.length()>3)t=t.substring(0,t.length()-1);
    gfx_setCursor((VW-gfx_textWidth(t))/2,190);gfx_print(t);
    }
    // lazy NFO blurb (same pattern as the cover panel, keyed to the center game)
    static int carNfoSel=-1;static String carBlurb="";
    if(carNfoSel!=gi&&moving&&!game.nfo_done){carNfoSel=-1;carBlurb="";}   // lab14i: never search the card for a description while the reel moves - wait until it stops
    else if(carNfoSel!=gi){carNfoSel=gi;carBlurb="";
      if(game.nfo_done)carBlurb=game.blurb;   // 5.9.31-lab1: straight out of PSRAM
      else{String nfoP,nT,nB;
        if(findNFOFor(g_files[game.first_file_idx],nfoP)){File nf=SD_MMC.open(nfoP,FILE_READ);
          if(nf){char _nb[513];int _nr=nf.read((uint8_t*)_nb,512);if(_nr<0)_nr=0;_nb[_nr]=0;nf.close();String txt(_nb);parseNFO(txt,nT,nB);
            if(nT.length()&&game.name==basenameNoExt(filenameOnly(g_files[game.first_file_idx])))game.name=nT;carBlurb=nB;}}
        game.blurb=carBlurb;game.nfo_done=true;}}
    if(g_neo){   // lab16a-P4: DISK [1][2][3] n/N pills, or the description; then the page dots
      if(game.disk_count>1){
        int nd=game.disk_count, ph=26, pb=min(42,(VW-140)/nd), pw=pb-8, dy=neoY+40;
        gfx_setTextSize(1); int total=44+nd*pb+44, x0=(VW-total)/2;
        gfx_setTextColor(COL_DIM,COL_BG); gfx_setCursor(x0,dy+(ph-8)/2); gfx_print("DISK");
        int dx0=x0+44;
        g_car_disk_n=nd; g_car_disk_x=dx0; g_car_disk_y=dy; g_car_disk_bw=pb; g_car_disk_h=ph;
        for(int d=0;d<nd;d++){ int bx=dx0+d*pb;
          bool dLd=(g_loaded&&g_loaded_game_idx==gi&&g_loaded_disk_idx==d), dSel=(d==g_disk_sel);
          uint16_t bc=dLd?COL_NOW:(dSel?COL_AMBER:COL_CIRC), oc=dLd?COL_GREEN:(dSel?0xFEB0:COL_SEP), tc=dLd?COL_GREEN:(dSel?TFT_BLACK:TFT_WHITE);
          gfx_fillRoundRect(bx,dy,pw,ph,8,bc); neoFrame(bx,dy,pw,ph,8,oc,2);
          String dl=String(d+1); gfx_setTextSize(2); gfx_setTextColor(tc,bc); gfx_setCursor(bx+(pw-gfx_textWidth(dl))/2,dy+(ph-16)/2); gfx_print(dl); }
        gfx_setTextSize(1); gfx_setTextColor(COL_DIM,COL_BG); gfx_setCursor(dx0+nd*pb+6,dy+(ph-8)/2); gfx_print(String(g_disk_sel+1)+"/"+String(nd));
      } else if(carBlurb.length()){ gfx_setTextSize(1); drawWrapped(100,neoY+38,carBlurb,VW-200,11,2,neoY+66,COL_DIM,COL_BG); }
      { int dots=min(9,n), dyy=neoY+82, dsp=16, x0=VW/2-(dots-1)*dsp/2, cur=(n>1)?(carWrap(ci)*(dots-1)+(n-1)/2)/(n-1):0;
        for(int k=0;k<dots;k++){ if(k==cur) gfx_fillCircle(x0+k*dsp,dyy,4,COL_AMBER); else gfx_fillCircle(x0+k*dsp,dyy,3,COL_SEP); } }
    } else
    if(game.disk_count>1){
      // v5.7.x: reel disk buttons — pick a disk (unloaded) or clean-swap to it (loaded), without leaving the reel.
      int nd=game.disk_count, dh=20, dy=VH-BOTTOM_H-42-26;
      int dbw=min(40,(VW-16)/nd), totalW=dbw*nd, dx0=(VW-totalW)/2;
      g_car_disk_n=nd; g_car_disk_x=dx0; g_car_disk_y=dy; g_car_disk_bw=dbw; g_car_disk_h=dh;
      for(int d=0;d<nd;d++){
        int bx=dx0+d*dbw;
        bool isLd=(g_loaded&&g_loaded_game_idx==gi&&g_loaded_disk_idx==d);
        bool isSel=(d==g_disk_sel);
        uint16_t bc=isLd?COL_GREEN:(isSel?COL_AMBER:COL_BAR);
        uint16_t tc=(isLd||isSel)?TFT_BLACK:COL_LIT;
        gfx_fillRoundRect(bx+1,dy,dbw-2,dh,4,bc);
        String dl=String(d+1); gfx_setTextSize(1); gfx_setTextColor(tc,bc);
        gfx_setCursor(bx+(dbw-gfx_textWidth(dl))/2,dy+(dh-8)/2);gfx_print(dl);
      }
    } else if(carBlurb.length()){gfx_setTextSize(1);
      drawWrapped(70,210,carBlurb,VW-140,10,2,232,COL_MID,COL_BG);}
    if(g_neo&&n>1){   // lab16b-P4: round < > at the edges (a tap on either side already steps the reel)
      if(carVert()){ for(int s=0;s<2;s++){ int ax=VW-32, ay=s?ccy+56:ccy-56; gfx_fillCircle(ax,ay,17,COL_BAR); gfx_drawCircle(ax,ay,17,COL_SEP);   // lab16f-P4: up / down
          for(int k=-7;k<=7;k++){ int py2=s?(ay+4-abs(k)):(ay-4+abs(k)); gfx_fillRect(ax+k,py2-1,1,3,TFT_WHITE); } } }
      else
      for(int s=0;s<2;s++){ int ax=s?VW-24:24; gfx_fillCircle(ax,ccy,17,COL_BAR); gfx_drawCircle(ax,ccy,17,COL_SEP);
        for(int k=-7;k<=7;k++){ int px2=s?(ax+4-abs(k)):(ax-4+abs(k)); gfx_fillRect(px2-1,ccy+k,3,1,TFT_WHITE); } } }
    // reel position "i/n" top-right of the stage
    if(!g_neo){
    gfx_setTextSize(1);gfx_setTextColor(COL_DIM,COL_BG);
    String pn=String(carWrap(ci)+1)+"/"+String(n);
    gfx_setCursor(VW-8-gfx_textWidth(pn),STATUS_H+4);gfx_print(pn);
    }
#if CAR_BENCH
    {gfx_setTextSize(1);gfx_setTextColor(g_bench_square?0xF800:0x07E0,COL_BG);
     char _bb[40];snprintf(_bb,sizeof _bb,"sq%lu mi%lu sh%lu",(unsigned long)g_bench_square,(unsigned long)g_bench_micro,(unsigned long)g_bench_sharp);
     gfx_setCursor(8,STATUS_H+4);gfx_print(_bb);}
#endif
    // INSERT/EJECT — an explicit button under the nfo (v4.7.3: tap-the-cover-to-
    // load removed; in portrait the cover fills the width, so swipes grazed
    // into accidental loads. Mounting is always a deliberate button on the GTi.)
    if(g_neo){ g_car_ins_x=g_car_ins_y=g_car_ins_w=g_car_ins_h=0; }   // lab16a-P4: NEO's INSERT lives in the bottom bar
    else
    {bool isLd=(g_loaded&&g_loaded_game_idx==gi);
     g_car_ins_w=170;g_car_ins_h=34;
     g_car_ins_x=(VW-g_car_ins_w)/2;g_car_ins_y=VH-BOTTOM_H-42;
     uint16_t bf=isLd?(uint16_t)0x4000:(uint16_t)0x0340, bb=isLd?(uint16_t)0xE8C4:COL_GREEN;
     gfx_fillRoundRect(g_car_ins_x,g_car_ins_y,g_car_ins_w,g_car_ins_h,8,bf);
     gfx_drawRoundRect(g_car_ins_x,g_car_ins_y,g_car_ins_w,g_car_ins_h,8,bb);
     gfx_setTextSize(2);gfx_setTextColor(TFT_WHITE,bf);
     const char*lbl=isLd?T(L_EJECT):T(L_INSERT);int tw=gfx_textWidth(lbl);
     gfx_setCursor(g_car_ins_x+(g_car_ins_w-tw)/2,g_car_ins_y+(g_car_ins_h-16)/2);gfx_print(lbl);}
  }
  if(g_neo){   // lab16a-P4: INSERT n / EJECT (cyan / red) | < ALL > | LIST | ROLL (gold) - four pills on the background
    int y=VH-BOTTOM_H; fillBack(0,y,VW,BOTTOM_H); int bw=VW/4, pad=6, bh=BOTTOM_H-2*pad, by=y+pad;
    String il=T(L_INSERT); uint16_t ic0=COL_BLUE;
    if(n>0){ int cgi=g_car_list[carWrap((int)lroundf(g_car_pos))]; auto&cg=g_games[cgi];
      if(g_loaded&&g_loaded_game_idx==cgi){ il=T(L_EJECT); ic0=0xC1A6; }
      else if(cg.disk_count>1&&g_disk_sel>0) il=String(T(L_INSERT))+" "+String(g_disk_sel+1); }
    neoPill(0*bw+pad,by,bw-2*pad,bh,ic0,il,false);
    neoPill(1*bw+pad,by,bw-2*pad,bh,COL_ACCENT,String("< ")+carSrcName()+" >",false);
    neoPill(2*bw+pad,by,bw-2*pad,bh,COL_ACCENT,T(L_LIST),false);
    neoPill(3*bw+pad,by,bw-2*pad,bh,COL_AMBER,T(L_ROLL),false);
  } else
  if(g_btn_pill){                                             // 5.8.3: coloured rounded pill buttons
    int y=VH-BOTTOM_H; gfx_fillRect(0,y,VW,BOTTOM_H,COL_BG); gfx_hline(0,y,VW,COL_SEP);
    int bw=VW/3, pad=5, bh=BOTTOM_H-2*pad, r=bh/2, by=y+pad;
    gfx_setTextSize(2);
    { uint16_t bc=COL_BLUE, ic=inkFor(bc); int bx=0*bw+pad, w=bw-2*pad;
      gfx_fillRoundRect(bx,by,w,bh,r,bc);
      int tw=gfx_textWidth(T(L_LIST)),total=16+tw,sx=bx+(w-total)/2;
      drawListIcon(sx+7,by+bh/2,ic); gfx_setTextColor(ic,bc);
      gfx_setCursor(sx+16,by+(bh-16)/2); gfx_print(T(L_LIST)); }
    { uint16_t bc=COL_AMBER, ic=inkFor(bc); int bx=1*bw+pad, w=bw-2*pad;
      gfx_fillRoundRect(bx,by,w,bh,r,bc); String sl=carSrcName(); int ts=2; if(gfx_textWidth(sl)>w-bh){ts=1;gfx_setTextSize(1);}   // lab15q: long words fit the pill
      int tw=gfx_textWidth(sl);
      gfx_setTextColor(ic,bc); gfx_setCursor(bx+(w-tw)/2,by+(bh-8*ts)/2); gfx_print(sl); gfx_setTextSize(2); }
    { uint16_t bc=COL_GREEN, ic=inkFor(bc); int bx=2*bw+pad, w=bw-2*pad;
      gfx_fillRoundRect(bx,by,w,bh,r,bc);
      int tw=gfx_textWidth(T(L_ROLL)),ds=16,total=ds+4+tw,sx=bx+(w-total)/2;
      int dx=sx+ds/2,dy2=by+bh/2;
      gfx_fillRoundRect(dx-ds/2,dy2-ds/2,ds,ds,3,0xFFFF); gfx_drawRoundRect(dx-ds/2,dy2-ds/2,ds,ds,3,TFT_BLACK);
      int o=4; gfx_fillCircle(dx,dy2,1,TFT_BLACK);
      gfx_fillCircle(dx-o,dy2-o,1,TFT_BLACK); gfx_fillCircle(dx+o,dy2+o,1,TFT_BLACK);
      gfx_fillCircle(dx+o,dy2-o,1,TFT_BLACK); gfx_fillCircle(dx-o,dy2+o,1,TFT_BLACK);
      gfx_setTextColor(ic,bc); gfx_setCursor(sx+ds+4,by+(bh-16)/2); gfx_print(T(L_ROLL)); }
  } else {
  // carousel bottom bar: Vince test — single bar split by dividers [LIST] [source] [ROLL], white-on-black
  const uint16_t bg=TFT_BLACK, ink=TFT_WHITE;
  const uint16_t dieBody=0xFFFF, diePip=TFT_BLACK;
  int y=VH-BOTTOM_H;gfx_fillRect(0,y,VW,BOTTOM_H,bg);gfx_hline(0,y,VW,COL_SEP);
  int bw=VW/3;
  for(int i=1;i<3;i++)gfx_vline(i*bw,y+8,BOTTOM_H-16,ink);          // slot dividers
  gfx_setTextSize(2);gfx_setTextColor(ink,bg);
  // LIST — coverflow-list glyph + word
  { int bx=0*bw,tw=gfx_textWidth(T(L_LIST)),total=16+tw,sx=bx+(bw-total)/2;
    drawListIcon(sx+7,y+BOTTOM_H/2,ink);
    gfx_setCursor(sx+16,y+(BOTTOM_H-16)/2);gfx_print(T(L_LIST)); }
  // SOURCE — cycles ALL/FAV/MOST (word is the current source)
  { int bx=1*bw;String sl=carSrcName();int ts=2;if(gfx_textWidth(sl)>bw-12){ts=1;gfx_setTextSize(1);}   // lab15q: long words fit the slot
    int tw=gfx_textWidth(sl);
    gfx_setCursor(bx+(bw-tw)/2,y+(BOTTOM_H-8*ts)/2);gfx_print(sl);gfx_setTextSize(2); }
  // ROLL — die glyph + word
  { int bx=2*bw,tw=gfx_textWidth(T(L_ROLL)),ds=16,total=ds+4+tw,sx=bx+(bw-total)/2;
    int dx=sx+ds/2,dy2=y+BOTTOM_H/2;
    gfx_fillRoundRect(dx-ds/2,dy2-ds/2,ds,ds,3,dieBody);gfx_drawRoundRect(dx-ds/2,dy2-ds/2,ds,ds,3,ink);
    int o=4;
    gfx_fillCircle(dx,dy2,1,diePip);
    gfx_fillCircle(dx-o,dy2-o,1,diePip);gfx_fillCircle(dx+o,dy2+o,1,diePip);
    gfx_fillCircle(dx+o,dy2-o,1,diePip);gfx_fillCircle(dx-o,dy2+o,1,diePip);
    gfx_setCursor(sx+ds+4,y+(BOTTOM_H-16)/2);gfx_print(T(L_ROLL)); }
  }
  if(g_car_dieShow&&carN()>0)carDrawDie();   // dice overlay rides on top of everything
  g_rp_draw+=micros()-_rp_f0; g_rp_frames++;
}

// v4.8.6: remember the last view for CAROUSEL=LAST — a tiny 1-char file beside
// the stats, written on each list<->reel flip, read once at boot.
static void writeLastView(int carousel){File f=SD_MMC.open(gtiStatePath(".gtiview",false).c_str(),FILE_WRITE);if(f){f.print(carousel?'1':'0');f.close();}}   // lab14g: /GTI
static int  readLastView(){File f=SD_MMC.open(gtiStatePath(".gtiview",true).c_str(),FILE_READ);if(!f)return 0;int c=f.read();f.close();return (c=='1')?1:0;}
// LASTUSED — remember the last game you loaded so power-up jumps straight back to it.
// Stores the game's first-file path (unique) beside the other tiny state files.
static void writeLastUsed(const String&path){File f=SD_MMC.open(gtiStatePath(".gtilastused",false).c_str(),FILE_WRITE);if(f){f.print(path);f.close();}}   // lab14g: /GTI
static void restoreLastUsed(){
  File f=SD_MMC.open(gtiStatePath(".gtilastused",true).c_str(),FILE_READ); if(!f)return;
  String want=f.readStringUntil('\n'); f.close(); want.trim(); if(!want.length())return;
  for(int i=0;i<(int)g_games.size();i++){
    if(g_files[g_games[i].first_file_idx]==want){
      g_sel=i; g_disk_sel=0; g_disk_page=0;
      float view=(float)(LIST_BOTTOM-LIST_TOP);
      float target=(float)g_sel*LIST_ITEM_H - view/2.0f + LIST_ITEM_H/2.0f;   // centre the row in the list
      float maxs=(float)g_games.size()*LIST_ITEM_H - view; if(maxs<0)maxs=0;
      if(target<0)target=0; if(target>maxs)target=maxs;
      g_scrollPx=target;
      setActiveLetter(bucketOf(g_games[i].name));
      return;
    }
  }
}
static void carEnter(){
  carBuildList();                                        // stats/favs may have changed
  carMicroEnsure();                                      // V1: micro-set ready before first draw
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
  g_car_spin=true; g_car_dieShow=true; g_car_coast=false; g_car_die_rest_ms=0;
  g_car_die=1+(uint8_t)(esp_random()%6);
  g_car_die23=((esp_random()%23)==0);                // the impossible roll
  drawCarousel();gfx_flush();
}

static void carInsertTap(int ci){   // INSERT/EJECT for the centre game (classic button and NEO's bottom-bar slot)
  int gi=g_car_list[ci];
  g_sel=gi;g_disk_page=0;setActiveLetter(bucketOf(g_games[gi].name));
  if(g_loaded&&g_loaded_game_idx==gi)doUnload();
  else{auto&gm=g_games[gi];int d=(g_disk_sel>=0&&g_disk_sel<(int)gm.disk_indices.size())?g_disk_sel:0;doLoadSelected(g_files[gm.disk_indices.empty()?gm.first_file_idx:gm.disk_indices[d]]);}
  if(g_car_active){drawCarousel();gfx_flush();}        // repaint over the list redraw the loader did
}
static void carHandleTap(uint16_t px,uint16_t py){
  if(g_neo&&py<(uint16_t)STATUS_H&&px>=(uint16_t)(VW-NEO_GEAR_W)){   // lab16a-P4: gear -> Settings (CLOSE goes back to the list)
    g_car_active=false; if(g_car_bootmode==2)writeLastView(0);
    g_info_showing=true;g_info_page=0;g_info_test=false;g_info_last=IA_NONE;drawInfoFull();return; }
  if(g_neo&&py>=(uint16_t)(VH-BOTTOM_H)){   // lab16a-P4: INSERT | source | LIST | ROLL
    int slot=px/(VW/4); if(slot>3)slot=3;
    if(slot==0){ if(carN()>0)carInsertTap(carWrap((int)lroundf(g_car_pos))); }
    else if(slot==1)carCycleSrc();
    else if(slot==2)carExit();
    else carRollDice();
    return; }
  if(py>=(uint16_t)(VH-BOTTOM_H)){
    if(px<(uint16_t)(VW/3))carExit();
    else if(px<(uint16_t)(2*VW/3))carCycleSrc();
    else carRollDice();                                // every tap re-rolls
    return;}
  int n=carN();if(!n)return;
  int ci=carWrap((int)lroundf(g_car_pos));
  int ccx=VW/2,ccy=carCcy();   // lab16e-P4
  // v5.7.x: reel disk buttons (multi-disk centered game) — checked before INSERT
  if(g_car_disk_n>0 && py>=(uint16_t)g_car_disk_y && py<(uint16_t)(g_car_disk_y+g_car_disk_h) &&
     px>=(uint16_t)g_car_disk_x && px<(uint16_t)(g_car_disk_x+g_car_disk_bw*g_car_disk_n)){
    int d=(px-g_car_disk_x)/g_car_disk_bw; if(d<0)d=0; if(d>=g_car_disk_n)d=g_car_disk_n-1;
    int gi=g_car_list[ci]; auto&gm=g_games[gi];
    if(d<(int)gm.disk_indices.size()){
      if(g_loaded&&g_loaded_game_idx==gi){
        if(d!=g_loaded_disk_idx){                    // clean swap: flush saves + eject, then reload the chosen disk
          doUnload(); g_sel=gi;g_disk_sel=d;g_disk_page=0;
          doLoadSelected(g_files[gm.disk_indices[d]]);
        }
      } else { g_disk_sel=d; }                        // not loaded yet — just select; INSERT will mount it
      if(g_car_active){drawCarousel();gfx_flush();}
    }
    return;
  }
  // INSERT/EJECT button (checked FIRST — its corners overlap the side zones)
  if(px>=(uint16_t)g_car_ins_x&&px<(uint16_t)(g_car_ins_x+g_car_ins_w)&&
     py>=(uint16_t)g_car_ins_y&&py<(uint16_t)(g_car_ins_y+g_car_ins_h)){
    carInsertTap(ci);
    return;
  }
  // center cover = DEAD ZONE (v4.7.3: no tap-to-load — portrait thumbs kept
  // grazing it into accidental mounts; the button above is the only trigger)
  if(px>=(uint16_t)(ccx-CAR_TILE/2)&&px<(uint16_t)(ccx+CAR_TILE/2)&&
     py>=(uint16_t)(ccy-CAR_TILE/2)&&py<(uint16_t)(ccy+CAR_TILE/2))return;
  if(carVert()){   // lab16f-P4: portrait reel - above = previous, below = next (incl. the up/down arrows on the right)
    if(py<(uint16_t)STATUS_H||py>=(uint16_t)carVInfoTop())return;
    if(px>=(uint16_t)(ccx+CAR_TILE/2)) g_car_pos+=(py<(uint16_t)ccy)?-1.0f:1.0f;
    else if(py<(uint16_t)(ccy-CAR_TILE/2)) g_car_pos-=1.0f;
    else if(py>=(uint16_t)(ccy+CAR_TILE/2)) g_car_pos+=1.0f;
    else return;
    g_car_pos=(float)carWrap((int)lroundf(g_car_pos));
    g_car_coast=false;drawCarousel();gfx_flush();
    return; }
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
      g_car_touch=true;g_car_moved=false;g_car_x0=px;g_car_y0=py;g_car_lastX=carVert()?py:px;   // lab16f-P4: lastX = the drag axis
      g_car_pos0=g_car_pos;g_car_vel=0;g_car_coast=false;g_car_lastMs=now;
    }else{
      const bool vt=carVert(); const int ax=vt?(int)py:(int)px, a0=vt?g_car_y0:g_car_x0; const float sp=vt?175.0f:CAR_PX_PER_STEP;   // lab16f-P4
      if(abs(ax-a0)>DRAG_THRESH)g_car_moved=true;
      if(g_car_moved&&n>0&&(vt?g_car_y0:(int)py)<(int)(VH-BOTTOM_H)){   // portrait: judged by where the finger went down
        g_car_pos=g_car_pos0-((float)(ax-a0))/sp;
        uint32_t dt=now-g_car_lastMs;
        if(dt>0){g_car_vel=((float)(ax-g_car_lastX))/(float)dt;g_car_lastX=ax;g_car_lastMs=now;}
        drawCarousel();gfx_flush();
      }
    }
    return;
  }
  if(g_car_touch){
    if(++g_car_rel<RELEASE_FRAMES)return;
    g_car_touch=false;g_car_rel=0;
    if(g_car_moved){g_car_ivel=-g_car_vel*16.0f/(carVert()?175.0f:CAR_PX_PER_STEP);g_car_coast=(fabsf(g_car_ivel)>0.004f);if(!g_car_coast){g_car_coast=true;g_car_ivel=0;}}
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
      g_car_die_rest_ms=now;                             // v5.4.2: arm the auto-hide timer
    }
    drawCarousel();gfx_flush();
    return;
  }
  // v5.4.2: auto-hide the rested die a few seconds after the roll settles
  if(g_car_dieShow&&!g_car_spin&&g_car_die_rest_ms&&now-g_car_die_rest_ms>=DIE_HIDE_MS){
    g_car_dieShow=false;g_car_die_rest_ms=0;drawCarousel();gfx_flush();return;
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

static void drawFullUI(){NeoScope _ns;clearBack();drawStatusBar();drawCoverPanel();drawActionStrip();drawModeBar();drawFileList();drawNowPlayingBar();drawAZBar();drawBottomBar();}
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
// lab15i: the paired dongle's tag - last 4 hex of its MAC ("" when no dongle is paired / MAC unknown).
// From memory (espnow_server keeps the MAC it paired with) - no card access, fine for the badge.
static String dongleSavTag(){
  if(!g_wireless_mode||!g_espnow_started||!espnowIsPaired())return "";
  String m=espnowGetXiaoMac();                            // "AA:BB:CC:DD:EE:FF"
  if(m.length()<17||m=="00:00:00:00:00:00")return "";
  return m.substring(12,14)+m.substring(15,17);
}
// Game.adf + "80FE" -> Game.sav.80FE.adf
static String dongleSavPathFor(const String&adfPath,const String&tag){
  int dot=adfPath.lastIndexOf('.');int sl=adfPath.lastIndexOf('/');
  if(dot<0||dot<sl)return adfPath+".sav."+tag;
  return adfPath.substring(0,dot)+".sav."+tag+adfPath.substring(dot);
}
// lab14i: the save list (see g_savset). One small file per mode in /GTI:
// [u32 "GSV1"][u16 len][cut folder, len bytes][u64 path hash]... - new saves are appended.
static String savSetPath(){return String(GTI_DIR)+(g_mode==MODE_ADF?"/.gtisaves_adf":g_mode==MODE_DSK?"/.gtisaves_dsk":"/.gtisaves_gen");}
#define SAVSET_MAGIC 0x31565347u
static void savSetWrite(){ if(g_nocache)return;
  File f=SD_MMC.open(savSetPath().c_str(),FILE_WRITE); if(!f)return;
  uint32_t m=SAVSET_MAGIC; uint16_t L=(uint16_t)(g_savset_cutdir.length()>255?0:g_savset_cutdir.length());
  f.write((uint8_t*)&m,4); f.write((uint8_t*)&L,2); if(L)f.write((const uint8_t*)g_savset_cutdir.c_str(),L);
  if(!g_savset.empty())f.write((uint8_t*)g_savset.data(),g_savset.size()*8);
  f.close();
  gLog("[saves] %u save files listed%s\n",(unsigned)g_savset.size(),L?" (library trimmed: its last folder is checked live)":"");
}
static void savSetLoad(){
  g_savset.clear(); g_savset_ok=false; g_savset_cutdir="";
  if(g_nocache)return;
  File f=SD_MMC.open(savSetPath().c_str(),FILE_READ);
  if(!f){ gLog("[saves] no save list yet - the badge uses live checks until a RESCAN\n"); return; }
  uint32_t m=0; uint16_t L=0; size_t sz=f.size();
  if(f.read((uint8_t*)&m,4)!=4||m!=SAVSET_MAGIC||f.read((uint8_t*)&L,2)!=2||L>255||sz<6u+L){ f.close(); return; }
  if(L){ char b[256]; if(f.read((uint8_t*)b,L)!=L){ f.close(); return; } b[L]=0; g_savset_cutdir=String(b); }
  size_t n=(sz-6-L)/8; g_savset.resize(n);
  if(n && f.read((uint8_t*)g_savset.data(),n*8)!=n*8){ f.close(); g_savset.clear(); g_savset_cutdir=""; return; }
  f.close();
  std::sort(g_savset.begin(),g_savset.end()); g_savset.erase(std::unique(g_savset.begin(),g_savset.end()),g_savset.end());
  g_savset_ok=true;
  gLog("[saves] %u save files from the list - no card lookups for the badge\n",(unsigned)g_savset.size());
}
static void savSetAdd(const String&sav){   // the GTi just wrote a save: badge it now, and on the card for next boot
  String l=sav; l.toLowerCase(); uint64_t h=coverHash(l);
  auto it=std::lower_bound(g_savset.begin(),g_savset.end(),h);
  if(it!=g_savset.end()&&*it==h) return;
  g_savset.insert(it,h);
  if(!g_savset_ok||g_nocache) return;
  File f=SD_MMC.open(savSetPath().c_str(),FILE_APPEND); if(f){ f.write((uint8_t*)&h,8); f.close(); }
}
static bool savBadgeForOne(const String&adfPath,const String&sv);
static bool savBadgeFor(const String&adfPath){
  if(g_saves_mode!=1) return false;
  String sv=savPathFor(adfPath); if(sv==adfPath) return false;
  if(savBadgeForOne(adfPath,sv)) return true;
  String tag=dongleSavTag();                                   // lab15i: or the paired dongle's own save
  return tag.length() && savBadgeForOne(adfPath,dongleSavPathFor(adfPath,tag));
}
static bool savBadgeForOne(const String&adfPath,const String&sv){
  if(!g_savset_ok||(g_savset_cutdir.length()&&parentDir(adfPath).equalsIgnoreCase(g_savset_cutdir)))
    return SD_MMC.exists(sv);                   // no list yet, or the folder a too-big walk stopped in: live check
  String l=sv; l.toLowerCase(); return std::binary_search(g_savset.begin(),g_savset.end(),coverHash(l));
}
// Copy base→sav.tmp, patch dirty sectors, atomic rename. Sector source is either
// `packed` (k-th set bit = k-th 512B block; wireless) or `ram` (g_disk; standalone).
// lab15d: when a save fails, svErr() says which step and why (errno) - it goes into gti.log.
static char g_sv_err[96]="";
static void svErr(const char*step,uint32_t n=0xFFFFFFFFu){ int e=errno;
  if(n==0xFFFFFFFFu) snprintf(g_sv_err,sizeof g_sv_err,"%s (errno %d %s)",step,e,e?strerror(e):"-");
  else snprintf(g_sv_err,sizeof g_sv_err,"%s %u (errno %d %s)",step,(unsigned)n,e,e?strerror(e):"-"); }
static bool svPatchCore(const String&master,const String&sav,const uint8_t*map,uint32_t mapBits,
                        const uint8_t*packed,const uint8_t*ram,bool fromMaster=false){
  g_sv_err[0]=0; errno=0;
  String base=(!fromMaster&&SD_MMC.exists(sav))?sav:master;   // lab15i: fromMaster = rebuild the save from what the Amiga was given
  String tmp=sav+".tmp";
  SD_MMC.remove(tmp);
  {File in=SD_MMC.open(base,FILE_READ);if(!in){svErr("open image to copy");return false;}
   File out=SD_MMC.open(tmp,FILE_WRITE);if(!out){in.close();svErr("create .tmp");return false;}
   uint8_t*buf=(uint8_t*)malloc(16384);if(!buf){in.close();out.close();SD_MMC.remove(tmp);svErr("no 16 KB copy buffer");return false;}
   int rd; uint32_t tot=0; bool cok=true;
   while((rd=in.read(buf,16384))>0){ if((int)out.write(buf,rd)!=rd){cok=false;break;} tot+=rd; }   // lab15d: a short write (card full) fails the save instead of patching a cut-off copy
   free(buf);in.close();out.close();
   if(!cok){SD_MMC.remove(tmp);svErr("copy short at byte",tot);return false;}}
  File f=SD_MMC.open(tmp,"r+");if(!f){SD_MMC.remove(tmp);svErr("reopen .tmp");return false;}
  uint32_t k=0;bool ok=true;
  for(uint32_t i=0;i<mapBits;i++){
    if(!((map[i>>3]>>(i&7))&1))continue;
    const uint8_t*src=packed?(packed+(size_t)k*512):(ram+(size_t)(DATA_LBA+i)*512);
    if(!f.seek(i*512UL)){svErr("seek to sector",i);ok=false;break;}
    if(f.write(src,512)!=512){svErr("write sector",i);ok=false;break;}
    k++;
  }
  f.flush();f.close();
  if(!ok){SD_MMC.remove(tmp);return false;}
  SD_MMC.remove(sav);
  bool _ok=SD_MMC.rename(tmp,sav);
  if(_ok) savSetAdd(sav);   // lab14i: the badge (and next boot's list) know about it straight away
  else svErr("rename .tmp to the save file");
  return _ok;
}
static void svToast(const String&msg){
  gfx_fillRect(0,0,VW,STATUS_H,COL_GREEN);gfx_setTextSize(1);gfx_setTextColor(TFT_BLACK,COL_GREEN);
  int tw=gfx_textWidth(msg);gfx_setCursor((VW-tw)/2,(STATUS_H-8)/2);gfx_print(msg);gfx_flush();
  delay(1200);drawStatusBar();gfx_flush();
}
// lab15c: a save file that already exists (COPY: the .sav from an earlier save; OVERWRITE: the image
// itself) is patched IN PLACE - same file, same clusters, same size, only the written sectors change.
// So the card file the mounted disk is read from never moves while the disk is in, and there is no
// copy + delete + rename per save (fewer FAT/directory writes). A first save in COPY mode still makes
// the .sav by copying the image (the mounted image itself is never touched in COPY mode).
static bool svPatchInPlace(const String&path,const uint8_t*map,uint32_t mapBits,uint32_t fsz,const uint8_t*ram){
  g_sv_err[0]=0; errno=0;
  File f=SD_MMC.open(path,"r+");if(!f){svErr("open save file for update");return false;}
  bool ok=true;
  for(uint32_t i=0;i<mapBits;i++){
    if(!((map[i>>3]>>(i&7))&1))continue;
    uint32_t off=i*512UL; if(off>=fsz)break;
    uint32_t len=(fsz-off)<512?(fsz-off):512;       // never write past the end - the file keeps its size
    if(!f.seek(off)){svErr("seek to sector",i);ok=false;break;}
    if(f.write(ram+(size_t)(DATA_LBA+i)*512,len)!=len){svErr("write sector",i);ok=false;break;}
  }
  f.flush();f.close();
  if(ok) savSetAdd(path);
  return ok;
}
// Standalone flush: persist our own RAM disk's written sectors to SD.
static uint8_t g_sv_fail=0;
static void svFlushStandalone(){
  if(!svPending())return;
  if(g_saves_mode==0||!g_loaded||!g_loaded_path.length()){g_sv_fseq=g_sv_wseq;return;}   // OFF / diag disk: nothing written to the card (lab15c: the Gotek still sees its own writes until eject)
  uint32_t seq0=g_sv_wseq;                          // writes that land during the flush make it pending again
  String master=g_loaded_path;                      // what the Amiga on the cable was given
  String orig=g_loaded_orig.length()?g_loaded_orig:master;
  String sav=(g_saves_mode==2)?orig:savPathFor(orig);   // lab15i: the cable ALWAYS saves to the plain save (or the image), never to a dongle's
  uint32_t imgSecs=(g_sv_img_size+511)/512;if(imgSecs>SV_IMG_MAX_SECTORS)imgSecs=SV_IMG_MAX_SECTORS;
  // lab15i: in Wireless mode the cable can be given a DONGLE's save (Game.sav.XXXX). Patching only the written
  // sectors into Game.sav.adf would mix two games' states, so the first save of this load rebuilds
  // Game.sav.adf from the file the cable was given; later saves of the same load patch it in place.
  // OVERWRITE never rebuilds the image (patch in place, as before).
  bool rebase=(g_saves_mode==1)&&!g_sv_cable_rebased&&!master.equalsIgnoreCase(sav);
  bool inPlace=!rebase&&SD_MMC.exists(sav);
  bool ok=inPlace ? svPatchInPlace(sav,g_sv_dirty,imgSecs,g_sv_img_size,g_disk)
                  : svPatchCore(master,sav,g_sv_dirty,imgSecs,nullptr,g_disk,rebase);
  if(ok&&rebase)g_sv_cable_rebased=true;
  gLog("[saves] %s %s: %u sectors in the overlay, %s%s%s\n",ok?"saved":"FAILED",sav.c_str(),(unsigned)g_sv_dirty_count,inPlace?"patched in place":"new save file",
       ok?"":" - ",ok?"":(g_sv_err[0]?g_sv_err:"no detail"));   // lab15d: why
  if(ok){
    g_sv_fseq=seq0;g_sv_fail=0;svToast("SAVED: "+g_loaded_name);
  }else{
    g_sv_last_write=millis();                       // back off one settle window, then retry
    if(++g_sv_fail>=2){g_sv_fseq=g_sv_wseq;g_sv_fail=0;svToast("SAVE FAILED - GAVE UP");}   // lab15e: retry ONCE - a card that fails twice is not helped by more writes (the game still sees its data until eject)
  }
}
// Wireless persist callback — runs inside espnowFetchSave, between CRC-verify and ack.
static bool svPersistWireless(uint32_t load_id,uint32_t img_size,const uint8_t*map,uint16_t mapLen,const uint8_t*packed,uint32_t nSec){
  (void)img_size;
  if(g_saves_mode==0)return false;
  if(!g_sv_wl_path.length())return false;                             // no mapping (multicast / pre-save FLING)
  if(g_sv_wl_loadid&&load_id&&g_sv_wl_loadid!=load_id)return false;   // stale — not the disk we flung
  if(nSec==0)return true;                                             // nothing to write; ack quiets the beacon
  String master=g_sv_wl_path;                                         // the file that was SENT
  if(g_sv_wl_tag.length()){
    // lab15i: the dongle's own file, in COPY and OVERWRITE alike. First save: built from the file that was sent
    // (its own save, the cable save or the image); after that the dongle only sends new sectors -> patch it.
    String orig=g_sv_wl_orig.length()?g_sv_wl_orig:master;
    String sav=dongleSavPathFor(orig,g_sv_wl_tag);
    bool had=SD_MMC.exists(sav);
    bool ok=svPatchCore(master,sav,map,(uint32_t)mapLen*8,packed,nullptr);
    gLog("[saves] %s dongle %s: %s - %u sectors, %s%s%s\n",ok?"saved":"FAILED",g_sv_wl_tag.c_str(),sav.c_str(),(unsigned)nSec,
         had?"updated":"new save file",ok?"":" - ",ok?"":(g_sv_err[0]?g_sv_err:"no detail"));
    return ok;
  }
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

// lab14s: asked in the middle of a wireless send when the dongle already holds ANOTHER screen's disk.
// dirty = that screen's saves have not been handed back yet (taking over loses them). true = take over.
static bool claimAskUI(bool dirty, const char* who){
  uint32_t r0=millis(); while(Touch_ReadFrame()&&millis()-r0<1500) delay(10);   // let go of the tap that started the load
  gfx_fillScreen(COL_BG);
  gfx_setTextSize(2); gfx_setTextColor(COL_AMBER,COL_BG);
  {const char*s="DONGLE IN USE"; gfx_setCursor((VW-gfx_textWidth(s))/2,VH/2-86); gfx_print(s);}
  gfx_setTextSize(1); gfx_setTextColor(COL_LIT,COL_BG);
  {String a=String("It holds a disk from ")+who; gfx_setCursor((VW-gfx_textWidth(a))/2,VH/2-52); gfx_print(a);}
  if(dirty){ gfx_setTextColor((uint16_t)0xE8C4,COL_BG);
    {const char*s="Its game saves are NOT handed back yet."; gfx_setCursor((VW-gfx_textWidth(s))/2,VH/2-34); gfx_print(s);}
    {const char*s="Taking over now LOSES those saves."; gfx_setCursor((VW-gfx_textWidth(s))/2,VH/2-20); gfx_print(s);} }
  else { const char*s="Take it over for this game?"; gfx_setCursor((VW-gfx_textWidth(s))/2,VH/2-30); gfx_print(s); }
  int bw=(VW-36)/2, by=VH-70, bh=44;
  uint16_t cT=dirty?(uint16_t)0x8000:COL_ORANGE, cC=COL_BAR;
  gfx_fillRoundRect(12,by,bw,bh,8,cT); gfx_setTextColor(inkFor(cT),cT); {const char*s="TAKE OVER"; gfx_setCursor(12+(bw-gfx_textWidth(s))/2,by+18); gfx_print(s);}
  gfx_fillRoundRect(24+bw,by,bw,bh,8,cC); gfx_setTextColor(inkFor(cC),cC); {const char*s="CANCEL"; gfx_setCursor(24+bw+(bw-gfx_textWidth(s))/2,by+18); gfx_print(s);}
  gfx_flush();
  bool take=false, down=false; uint16_t lx=0,ly=0; uint32_t t0=millis();
  while(millis()-t0<30000){                         // no answer in 30 s = CANCEL
    bool t=Touch_ReadFrame(); uint16_t tx=0,ty=0; if(t)t=getTouchXY(&tx,&ty);
    if(t){down=true;lx=tx;ly=ty;}
    else if(down){down=false;
      if(ly>=by&&ly<by+bh){ if(lx<12+bw){take=true;break;} if(lx>=24+bw){break;} } }
    delay(15);
  }
  drawFullUI(); gfx_flush();
  return take;
}

static bool doLoadSelected(const String&adfPath){
  // v4.9 / v5.x: HD (1.76MB) over wireless is now gated by the dongle's advertised
  // capability (pad[1] of the pairing reply). An HD-capable XIAO (2MB ramdisk,
  // g_espnow_dongle_board==1) may receive it; the Super Mini and old DD-only
  // dongles (board 0) still can't hold it, so they stay blocked. Multicast/
  // Hivemind stays blocked too (a mixed fleet may include a DD dongle).
  // Standalone HD load (cable) is unaffected either way.
  bool hdDongleReady = espnowIsPaired() && g_espnow_dongle_board==1 && !g_hivemind;
  if(g_wireless_mode && g_mode==MODE_ADF && isHDImage(adfPath) && !hdDongleReady){
    gfx_fillRoundRect(COVER_X,COVER_Y,COVER_W,COVER_H,12,COL_PANEL);
    gfx_setTextSize(1);gfx_setTextColor(0xE8C4,COL_PANEL);gfx_setCursor(COVER_X+12,COVER_Y+16);gfx_print(T(L_HD_NO_WIRELESS));
    gfx_setTextColor(COL_LIT,COL_PANEL);
    gfx_setCursor(COVER_X+12,COVER_Y+30);gfx_print(T(L_NO_WIRELESS_DEV));
    gfx_setCursor(COVER_X+12,COVER_Y+42);gfx_print(T(L_AVAIL_HD));
    gfx_setCursor(COVER_X+12,COVER_Y+56);gfx_print(T(L_USE_CABLE));
    gfx_flush();delay(2200);drawFullUI();gfx_flush();return false;
  }
  // v4.8.0 interlocks: pending saves die when the RAM disk is rebuilt — drain first
  // (v4.8.1: own-disk flush runs in ANY mode — a wireless GTi can still be USB-attached)
  if(g_wireless_mode&&g_espnow_started&&g_espnow_dirty)svFetchWireless();
  if(svPending()&&g_loaded)svFlushStandalone();
  // Prefer the save-copy when one exists (COPY mode): saves accumulate in the .sav
  String loadPath=adfPath;
  if(g_saves_mode==1&&savExistsFor(adfPath))loadPath=savPathFor(adfPath);
  // lab15i: going to ONE dongle (not a Hivemind fan-out)? Then that dongle gets its OWN save if it has one
  // (Game.sav.XXXX.adf); if not, the cable save / the image as before - its first save starts from that.
  uint8_t mcMacs[64][6]; int mcN=0; String wlTag="";
  if(g_wireless_mode&&g_espnow_started){
    mcN=enumMuCaDongles(mcMacs,g_dongle_cap);                   // (was read further down; same call, moved up)
    bool fanOut=(mcN>0&&g_hivemind);
    if(!fanOut&&g_saves_mode!=0){
      wlTag=dongleSavTag();
      if(wlTag.length()){
        String ds=dongleSavPathFor(adfPath,wlTag);
        if(SD_MMC.exists(ds))loadPath=ds;
        gLog("[saves] dongle %s: %s\n",wlTag.c_str(),loadPath==ds?"sending its own save":"no own save yet - sending the cable save or the image");
      }
    }
  }
  gfx_fillRoundRect(COVER_X,COVER_Y,COVER_W,COVER_H,12,COL_PANEL);
  gfx_setTextSize(1);gfx_setTextColor(TFT_CYAN,COL_PANEL);String tn=basenameNoExt(filenameOnly(adfPath));if(tn.length()>16)tn=tn.substring(0,16);
  gfx_setCursor(COVER_X+12,COVER_Y+16);gfx_print(tn);gfx_setTextColor(COL_LIT,COL_PANEL);gfx_setCursor(COVER_X+12,COVER_Y+28);gfx_print(T(L_LOADING));
  gfx_flush();
  // Clean swap: if a disk is already mounted, cleanly eject first so the host re-reads the new media.
  // FORCESWAP=ON skips this and swaps the bytes in place (faster, but the host may not notice).
  if(g_loaded && !g_forceswap) hardDetach();
  File f=SD_MMC.open(loadPath.c_str(),FILE_READ);if(!f){gfx_setTextColor(TFT_RED,COL_PANEL);gfx_setCursor(COVER_X+12,COVER_Y+40);gfx_print(T(L_FAILED));gfx_flush();delay(1000);drawFullUI();gfx_flush();return false;}
  // Use VFS to get real file size (SD_MMC f.size() returns 0 for subdirectory files)
  String vfsLoad="/sdcard"+loadPath;
  struct stat stLoad;
  if(stat(vfsLoad.c_str(),&stLoad)!=0||stLoad.st_size==0) {f.close();gfx_setTextColor(TFT_RED,COL_PANEL);gfx_setCursor(COVER_X+12,COVER_Y+40);gfx_print(T(L_SIZE_ERR));gfx_flush();delay(1000);drawFullUI();gfx_flush();return false;}
  uint32_t fsz=(uint32_t)stLoad.st_size;
  uint32_t copied=0;
  if(g_wireless_mode){
    // ── WIRELESS: unchanged. The dongle receives the image bytes out of
    //    g_disk (espnowSendDisk reads from it), so it has to be copied there,
    //    and the BIGDISK size remains the hard ceiling for anything sent by radio.
    if(fsz>MAX_FILE_BYTES){
      f.close();
      gfx_fillRoundRect(COVER_X,COVER_Y,COVER_W,COVER_H,12,COL_PANEL);
      gfx_setTextSize(1);gfx_setTextColor(0xE8C4,COL_PANEL);
      gfx_setCursor(COVER_X+12,COVER_Y+16);gfx_print(T(L_TOO_BIG));
      gfx_setTextColor(COL_LIT,COL_PANEL);
      gfx_setCursor(COVER_X+12,COVER_Y+30);gfx_print(String(fsz/1024)+"KB > "+String(MAX_FILE_BYTES/1024)+"KB");
      gfx_setCursor(COVER_X+12,COVER_Y+44);gfx_print(T(L_HD_NO_WIRELESS));
      gfx_setTextColor(COL_DIM,COL_PANEL);
      gfx_setCursor(COVER_X+12,COVER_Y+58);gfx_print(T(L_USE_CABLE));
      gfx_flush();delay(2200);drawFullUI();gfx_flush();return false;
    }
    g_alias=false;
    {String pn=presentName(adfPath);build_volume(pn.c_str(),fsz);}   // v5.2: GEN keeps the real name+ext so FlashFloppy detects the format; lab14q: LONGNAME
    uint8_t*dst=g_disk+DATA_LBA*512;uint8_t*buf=(uint8_t*)malloc(16384);uint32_t remain=fsz;
    while(remain&&buf){size_t n=remain>16384?16384:remain;int rd=f.read(buf,n);if(rd<=0)break;memcpy(dst+copied,buf,rd);remain-=rd;copied+=rd;}
    if(buf)free(buf);f.close();
    // v4.8.0: fresh disk in the RAM disk = fresh save tracking
    g_sv_img_size=(g_mode==MODE_GEN)?0:fsz;svDirtyReset();   // v5.2: GEN has no Amiga save-writeback (0 = no dirty tracking)
  } else {
    // ── STANDALONE: ALIAS, always, any size (5.9.37). Nothing is copied; the
    //    data area IS the file on the card. g_disk holds the FAT12 metadata and
    //    acts as the write overlay for saves (see onRead / onWrite).
    f.close();
    // Presented geometry: an image that fits the RAM disk is presented at the
    // RAM disk's OWN geometry, so the volume the Gotek sees is byte-identical to
    // 5.9.36 — same size, same cluster size, same file — just served from the
    // card. Only an oversized image gets a volume sized to itself.
    uint32_t pS=TOTAL_SECTORS; uint8_t pC=SECTORS_PER_CLUSTER;
    String aerr;
    bool geomOk = (fsz<=MAX_FILE_BYTES) ? true : aliasGeom(fsz,&pS,&pC);
    if(!geomOk) aerr="over 32MB (FAT12 limit)";
    // Held in a named String: .c_str() on a temporary would dangle the moment
    // the full expression ended. GEN keeps the real name+ext so FlashFloppy can
    // detect the format, exactly as the RAM-disk path does.
    String vn = presentName(adfPath);   // lab14q: GEN real name, ADF/DSK DISK.xxx unless LONGNAME=ON
    if(!geomOk || !aliasMount(loadPath,fsz,vn.c_str(),pS,pC,&aerr)){
      g_alias=false;
      gfx_fillRoundRect(COVER_X,COVER_Y,COVER_W,COVER_H,12,COL_PANEL);
      gfx_setTextSize(1);gfx_setTextColor(0xE8C4,COL_PANEL);
      gfx_setCursor(COVER_X+12,COVER_Y+16);gfx_print(T(L_FAILED));
      gfx_setTextColor(COL_LIT,COL_PANEL);
      gfx_setCursor(COVER_X+12,COVER_Y+30);gfx_print(String(fsz/1024)+"KB");
      gfx_setTextColor(COL_DIM,COL_PANEL);
      gfx_setCursor(COVER_X+12,COVER_Y+44);gfx_print(aerr);
      gfx_flush();delay(2200);drawFullUI();gfx_flush();return false;
    }
    copied=fsz;
    // Save tracking is LIVE under alias: the dirty map is what makes the overlay
    // work, and svFlushStandalone patches those sectors into GameName.sav.<ext>
    // exactly as before. lab15c: GEN saves too (it had no writeback before).
    if(fsz>SV_IMG_MAX_SECTORS*512UL) gLog("[saves] %s is %u KB - only its first %u KB can be saved (the BIGDISK size; BIGDISK=ON = 2880 KB)\n",loadPath.c_str(),(unsigned)(fsz/1024),(unsigned)(SV_IMG_MAX_SECTORS/2));   // lab15c
    g_sv_img_size=fsz;svDirtyReset();   // lab15c: GENERIC too (HFE, ST, IMG...) - FlashFloppy writes into the image file itself, so the same sector patching saves it
  }
  mscAnnounce(g_alias?g_alias_sectors:TOTAL_SECTORS);
  hardAttach();g_loaded=true;g_loaded_name=basenameNoExt(filenameOnly(adfPath));g_loaded_path=loadPath;g_loaded_game_idx=g_sel;g_loaded_disk_idx=g_disk_sel;
  gLog("[load] %s (%u KB, %s) mode=%s - USB host: %s\n",loadPath.c_str(),(unsigned)(fsz/1024),g_alias?"from the card":"RAM disk",g_wireless_mode?"WIRELESS":"STANDALONE",tud_mounted()?"connected":"none yet");   // lab5-P4G7
  g_loaded_orig=adfPath;g_sv_cable_rebased=false;             // lab15i
  if(g_lastused&&g_loaded_game_idx>=0&&g_loaded_game_idx<(int)g_games.size())writeLastUsed(g_files[g_games[g_loaded_game_idx].first_file_idx]);   // remember this game for next boot
  if(g_sel>=0&&g_sel<(int)g_games.size()){if(g_games[g_sel].plays<65535)g_games[g_sel].plays++;saveStats();}
  if(g_wireless_mode&&g_espnow_started){
    // 1.6.3 wireless DSK fix: tell the dongle the FAT12 name+extension to build,
    // matching what standalone would use (getOutputFilename / real name for GEN),
    // so a CPC .dsk mounts as DISK.DSK not DISK.ADF (FlashFloppy Error 34).
    { String fn = (g_mode==MODE_GEN) ? filenameOnly(adfPath)
                                     : (basenameNoExt(filenameOnly(adfPath)) + (g_mode==MODE_ADF ? ".adf" : ".dsk"));
      espnowSetFlingName(fn); }
    if(mcN>0&&g_hivemind){                                  // multicast: fan the disk out to every MuCa- dongle in turn (v4.8.1: only when HIVEMIND=ON)
      g_sv_wl_path="";g_sv_wl_loadid=0;g_sv_wl_tag="";g_sv_wl_orig="";   // Hivemind saves: PINNED — no writeback mapping for multicast
      for(int i=0;i<mcN;i++){
        gfx_setTextSize(1);gfx_setTextColor(TFT_CYAN,COL_PANEL);gfx_fillRect(COVER_X+10,COVER_Y+24,150,12,COL_PANEL);
        gfx_setCursor(COVER_X+12,COVER_Y+26);gfx_print("Multicast "+String(i+1)+"/"+String(mcN));gfx_flush();
        espnowSendDiskTo(mcMacs[i],copied);
      }
    } else if(g_link_home && g_home_ssid.length()){         // 5.8.6: home-WiFi transport — route via the router to the dongle's gotek.local
      String prevIp=g_dongle_home_ip;
      if(espnowSendDiskHome(g_home_ssid,g_home_pass,g_dongle_home_ip,copied)){
        g_sv_wl_path=loadPath;g_sv_wl_loadid=g_espnow_load_id;g_sv_wl_orig=adfPath;g_sv_wl_tag=wlTag;   // lab15i
      } else if(espnowClaimCancelled()) svToast("NOT SENT - dongle kept for the other screen");   // lab14s
      if(g_dongle_home_ip!=prevIp&&g_dongle_home_ip.length())saveConfigKey("DONGLE_HOME_IP",g_dongle_home_ip);  // persist the resolved IP for next time
    } else if(espnowIsPaired()){                            // single paired dongle — unchanged
      espnowSendNotify(g_loaded_name,g_mode==MODE_ADF?"ADF":g_mode==MODE_DSK?"DSK":"GEN",copied);
      if(espnowSendDisk(copied)){                           // v4.8.0: remember what we flung, keyed by the dongle's load_id
        g_sv_wl_path=loadPath;g_sv_wl_loadid=g_espnow_load_id;g_sv_wl_orig=adfPath;g_sv_wl_tag=wlTag;   // lab15i
      } else if(espnowClaimCancelled()) svToast("NOT SENT - dongle kept for the other screen");   // lab14s
    }
  }
  drawStatusBar();drawListAndCover();gfx_flush();return true;
}

// ── WebDAV fetch into the RAM disk (merge step 1) ─────────────────────────
// Minimal wiring, no UI: call it from wherever the menu decides. Joins
// HOME_SSID, streams the remote file straight into the RAM disk's data
// region, builds the FAT metadata around the bytes, attaches. Refuses while
// the wireless dongle link is up — fetch-over-WiFi next to ESP-NOW is the
// radio-coexistence question deliberately parked for a later step.
static String g_dav_fail="";   // why the last doLoadWebdav gave up, for on-screen reporting
static bool doLoadWebdav(const String&remotePath,const String&showName){
  if(!g_dav_on||g_dav_host.length()==0){g_dav_fail="not configured (DAV=ON + DAV_HOST=)";Serial.println("[DAV] "+g_dav_fail);return false;}
  if(g_espnow_started){g_dav_fail="wireless dongle link active";Serial.println("[DAV] "+g_dav_fail);return false;}
  if(g_home_ssid.length()==0){g_dav_fail="HOME_SSID not set";Serial.println("[DAV] "+g_dav_fail);return false;}
  // When the web server already holds a live STA connection, use it and — the
  // important half — leave it standing afterwards. Joining is only for the
  // standalone case where the radio is otherwise off.
  const bool keepUp=(WiFi.status()==WL_CONNECTED);
  if(!keepUp){
    Serial.printf("[DAV] joining '%s'\n",g_home_ssid.c_str());
    WiFi.mode(WIFI_STA);WiFi.persistent(false);WiFi.setAutoReconnect(false);
    WiFi.disconnect(false,true);delay(200);
    WiFi.begin(g_home_ssid.c_str(),g_home_pass.c_str());
    uint32_t t0=millis();while(WiFi.status()!=WL_CONNECTED&&millis()-t0<15000)delay(200);
    if(WiFi.status()!=WL_CONNECTED){g_dav_fail="WiFi join failed";Serial.println("[DAV] "+g_dav_fail);WiFi.disconnect(false);return false;}   // P4: never switch the hosted radio off
  }
  davApplyConfig();
  if(g_loaded&&!g_forceswap)hardDetach();
  long got=davClient.streamToBuffer(remotePath,g_disk+DATA_LBA*512,MAX_FILE_BYTES,false);
  davClient.closeIdle();                          // the pooled TLS context is ~50KB of internal heap
  if(!keepUp){WiFi.disconnect(false);delay(100);}   // P4: leave the association, never switch the hosted radio off
  if(got<=0||davClient.lastTruncated()){
    g_dav_fail=davClient.lastError();
    Serial.printf("[DAV] fetch failed: %s\n",davClient.lastError().c_str());
    if(g_loaded)hardAttach();                     // put the previous disk back
    return false;
  }
  // build_volume() would memset the data region we just filled — build the
  // metadata around the bytes instead: the same three calls minus the wipe.
  memset(g_disk,0,DATA_LBA*512);
  build_boot_sector(g_disk);
  build_fat(g_disk+RESERVED_SECTORS*512,(uint32_t)got);
  String outn=(g_mode==MODE_GEN||g_longname)?showName:String(getOutputFilename());   // lab14q: LONGNAME
  build_root(g_disk+(RESERVED_SECTORS+SECTORS_PER_FAT)*512,outn.c_str(),(uint32_t)got);
  g_sv_img_size=0;svDirtyReset();                 // no SD path to write saves back to — tracking off for now
  hardAttach();g_loaded=true;g_loaded_name=showName;g_loaded_path="";g_loaded_orig="";g_loaded_game_idx=-1;g_loaded_disk_idx=-1;
  Serial.printf("[DAV] mounted %s (%ld bytes)\n",showName.c_str(),got);
  return true;
}

// Merge step 2: the shared web interface + OTA, served over HOME_SSID when
// WEBUI=ON. Placed here because it calls doLoadWebdav and the disk builders.
#define GTI_WEB_SD_FILES 1   // 5.9.9: WiFi SD file-access endpoints (JC3.5 only for now)
#include "../shared/web_panel.h"

static void doUnload(){
  // v4.8.0: EJECT is a save point — drain before the disk goes away
  // (v4.8.1: own-disk flush in any mode)
  if(svPending())svFlushStandalone();
  if(g_wireless_mode&&g_espnow_started&&g_espnow_dirty)svFetchWireless();
  if(g_loaded)gLog("[eject] %s\n",g_loaded_path.length()?g_loaded_path.c_str():g_loaded_name.c_str());   // lab5-P4G7
  hardDetach();g_loaded=false;g_loaded_name="";g_loaded_path="";g_loaded_orig="";g_loaded_game_idx=-1;g_loaded_disk_idx=-1;svDirtyReset();g_alias=false;   // 5.9.37: drop any alias mapping
  if(g_wireless_mode&&g_espnow_started&&espnowIsPaired()&&!g_link_home){   // lab3-P4G7: real eject over Wi-Fi (saves pulled first)
    gfx_fillRect(0,VH/2-24,VW,48,COL_ACCENT);gfx_setTextSize(2);gfx_setTextColor(TFT_WHITE,COL_ACCENT);
    {const char*m="EJECTING ON THE DONGLE...";int tw=gfx_textWidth(m);gfx_setCursor((VW-tw)/2,VH/2-8);gfx_print(m);}gfx_flush();
    String nm=g_sv_wl_path.length()?basenameNoExt(filenameOnly(g_sv_wl_path)):String("disk");
    espnowSendEject();
    drawStatusBar();drawListAndCover();gfx_flush();
    if(g_p4_saved_secs>0)svToast("SAVED: "+nm);
    else if(g_p4_saved_secs<0)svToast("SAVE PULL FAILED");
    if(g_p4_eject_result==0)svToast("DONGLE NOT REACHED - not ejected there");
    else if(g_p4_eject_result==2)svToast("DONGLE KEPT THE DISK (unsaved writes)");
    gfx_flush(); return; }
  drawStatusBar();drawListAndCover();gfx_flush();}

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
  gfx_fillRoundRect(COVER_X,COVER_Y,COVER_W,COVER_H,12,COL_PANEL);
  gfx_setTextSize(1);gfx_setTextColor(TFT_CYAN,COL_PANEL);gfx_setCursor(COVER_X+12,COVER_Y+16);gfx_print("AMIGA TEST KIT");
  gfx_setTextColor(COL_LIT,COL_PANEL);gfx_setCursor(COVER_X+12,COVER_Y+28);gfx_print(T(L_LOADING_DIAG));gfx_flush();
  if(g_loaded && !g_forceswap) hardDetach();
  g_alias=false;mscAnnounce(TOTAL_SECTORS);               // 5.9.37: diag disk is a RAM-disk mount
  build_volume("DISK.ADF",DIAG_ADF_SIZE);                 // force an .ADF image regardless of MODE
  diagInflate(DIAG_RLE,DIAG_RLE_LEN,g_disk+DATA_LBA*512);
  hardAttach();
  g_loaded=true;g_loaded_name="AMIGA TEST KIT";g_loaded_game_idx=-1;g_loaded_disk_idx=-1;
  g_loaded_path="";g_loaded_orig="";g_sv_img_size=0;svDirtyReset();   // diag disk: writes are never persisted
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
// P4.9: decode the embedded Retronaut helmet into the bounce buffer (no SD file needed).
static bool retroHelmDecode(){
  ssFree();
  size_t sz=RETRO_HELM_JPG_LEN;
  uint8_t* tmp=(uint8_t*)malloc(sz); if(!tmp)return false;
  memcpy_P(tmp,RETRO_HELM_JPG,sz);
  if(!jpegdec.openRAM(tmp,sz,jpeg_buf_cb)){free(tmp);return false;}
  int jw=jpegdec.getWidth(),jh=jpegdec.getHeight();
  if(jw<=0||jh<=0){jpegdec.close();free(tmp);return false;}
  jpeg_tmp_buf=(uint16_t*)ps_malloc((size_t)jw*jh*2);
  if(!jpeg_tmp_buf){jpegdec.close();free(tmp);return false;}
  memset(jpeg_tmp_buf,0,(size_t)jw*jh*2); jpeg_tmp_w=jw; jpeg_tmp_h=jh;
  jpegdec.decode(0,0,0); jpegdec.close(); free(tmp);
  g_ss_buf=jpeg_tmp_buf; g_ss_w=jw; g_ss_h=jh; jpeg_tmp_buf=NULL;
  return true;
}
static void scanScreensaver(){                               // arm iff /screensaver/ exists
  g_ss_paths.clear(); g_ss_have=false; g_ss_claude=false;
  File dir=SD_MMC.open("/screensaver"); if(!dir){ g_ss_claude=true; g_ss_have=true; return; }   // v5.5.1: no folder -> default bouncing sprites (that flip to contributor names)
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
  if(g_cracktro==8)g_ss_have=true;   // 5.4.0: Denise theme arms the saver even with no /screensaver folder
  if(g_cracktro==9)g_ss_have=true;   // v5.5.2: Wrangler theme (CRACKTRO=WRANGLER) arms it too
  if(g_cracktro==10)g_ss_have=true;   // P4.9: Retronaut helmet screensaver
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
// 5.4.0: hidden Denise theme screensaver — bounce the Denise / Vincent wordmarks,
// swapping on every wall hit (armed by CRACKTRO=DENISE).
static bool ssMakeName(bool vincent){
  const uint8_t*bits=vincent?GTI_VINCENT_BITS:GTI_DENISE_BITS;
  int w=vincent?GTI_VINCENT_W:GTI_DENISE_W, h=vincent?GTI_VINCENT_H:GTI_DENISE_H;
  if(g_ss_buf&&(g_ss_w!=w||g_ss_h!=h)) ssFree();
  if(!g_ss_buf){ g_ss_buf=(uint16_t*)ps_malloc((size_t)w*h*2); if(!g_ss_buf)return false; }
  const uint16_t CREAM=CRK_RGB(244,238,225); int rb=(w+7)/8;
  for(int y=0;y<h;y++)for(int x=0;x<w;x++)
    g_ss_buf[y*w+x]=(pgm_read_byte(&bits[y*rb+(x>>3)])&(0x80>>(x&7)))?CREAM:TFT_BLACK;
  g_ss_w=w; g_ss_h=h; return true;
}
// ── v5.5.1: contributor NAMES — extra "forms" the bouncing sprite shapeshifts into
//    on a wall hit (alongside the starburst / ghost / ice-cream). The sprites stay;
//    ~40% of bounces flip to a random name instead, then it keeps bouncing. EDIT FREELY.
static const char* const NAMES[]={ "Vincent", "BlindGuy", "Retronaut", "Mez", "Wrangler" };
static const int N_NAMES=(int)(sizeof(NAMES)/sizeof(NAMES[0]));

// ════════════════════════════════════════════════════════════════════════════
// v5.7.2: SCREENSAVER SLIDESHOW (Paul Dean CR) — full-screen photo mode.
//   Pool = /screensaver/*.jpg  +  cover art of every FAVOURITED game (SSFAV=ON),
//   shuffled. Each slide fit-to-screen (letterboxed) via gfx_drawJpgFile. Between
//   slides: fade / dissolve / slide-push (SSFX; SHUFFLE picks one at random).
//   Two full-screen PSRAM snapshots drive the blend; if they can't be allocated we
//   fall back to a hard CUT so the slideshow still runs on tight-PSRAM boards.
// ════════════════════════════════════════════════════════════════════════════
#define SSFX_FADE 1
#define SSFX_DISS 2
#define SSFX_SLIDE 3
#define SSFX_CUT  4
static bool ssSlideAlloc(){
  size_t px=(size_t)LCD_WIDTH*LCD_HEIGHT;
  if(!g_slA)g_slA=(uint16_t*)ps_malloc(px*2);
  if(!g_slB)g_slB=(uint16_t*)ps_malloc(px*2);
  return g_slA&&g_slB;
}
static void ssSlideFree(){ if(g_slA){free(g_slA);g_slA=NULL;} if(g_slB){free(g_slB);g_slB=NULL;} }
// blend two TRUE rgb565 values, t in 0..32
static inline uint16_t ssLerp565(uint16_t a,uint16_t b,int t){
  int ar=(a>>11)&0x1F, ag=(a>>5)&0x3F, ab=a&0x1F;
  int br=(b>>11)&0x1F, bg=(b>>5)&0x3F, bb=b&0x1F;
  int r=ar+((br-ar)*t)/32, g=ag+((bg-ag)*t)/32, bl=ab+((bb-ab)*t)/32;
  return (uint16_t)((r<<11)|(g<<5)|bl);
}
// render one image fit-to-screen into the live framebuffer (no flush); optionally snapshot it
static void ssSlideRender(const String&path,uint16_t*snap){
  gfx_fillScreen(TFT_BLACK);
  gfx_drawJpgFile(path,0,0,gW,gH);
  if(snap)memcpy(snap,framebuffer,(size_t)LCD_WIDTH*LCD_HEIGHT*2);
}
// hold on the current frame for ms; return true if a touch asks us to wake
static bool ssSlideHold(uint32_t ms){
  uint32_t t0=millis();
  while(millis()-t0<ms){ if(Touch_ReadFrame())return true; delay(12); }
  return false;
}
// transition g_slA (outgoing) -> g_slB (incoming), both raw physical fb words. true=touched
static bool ssSlideFx(int fx){
  const int N=LCD_WIDTH*LCD_HEIGHT;
  if(fx==SSFX_CUT){ memcpy(framebuffer,g_slB,(size_t)N*2); gfx_flush(); return false; }
  if(fx==SSFX_FADE){
    const int STEPS=16;
    for(int s=1;s<=STEPS;s++){ if(Touch_ReadFrame())return true;
      int t=s*32/STEPS;
      for(int i=0;i<N;i++){ uint16_t a=swap16(g_slA[i]),b=swap16(g_slB[i]);
        framebuffer[i]=swap16(ssLerp565(a,b,t)); if((i&4095)==0)yield(); }
      gfx_flush(); }
    return false;
  }
  if(fx==SSFX_DISS){
    const int STEPS=14;
    for(int s=1;s<=STEPS;s++){ if(Touch_ReadFrame())return true;
      uint32_t thr=(uint32_t)s*256/STEPS;
      for(int i=0;i<N;i++){ uint32_t h=((uint32_t)i*2654435761u)>>24;   // stable per-pixel 0..255
        framebuffer[i]=(h<thr)?g_slB[i]:g_slA[i]; if((i&4095)==0)yield(); }
      gfx_flush(); }
    return false;
  }
  // SSFX_SLIDE — push along the physical X axis (outgoing exits, incoming enters)
  { const int STEPS=16;
    for(int s=1;s<=STEPS;s++){ if(Touch_ReadFrame())return true;
      int off=s*LCD_WIDTH/STEPS; if(off>LCD_WIDTH)off=LCD_WIDTH;
      for(int py=0;py<LCD_HEIGHT;py++){
        uint16_t*fr=&framebuffer[py*LCD_WIDTH];
        uint16_t*ra=&g_slA[py*LCD_WIDTH];
        uint16_t*rb=&g_slB[py*LCD_WIDTH];
        for(int px=0;px<LCD_WIDTH;px++){ int sa=px+off;
          fr[px]=(sa<LCD_WIDTH)?ra[sa]:rb[sa-LCD_WIDTH]; }
        if((py&31)==0)yield();
      }
      gfx_flush(); }
    return false;
  }
  return false;
}
// build the shuffled slide pool: folder images + favourited game covers
static std::vector<String> ssBuildSlidePool(){
  std::vector<String> pool;
  for(auto&p:g_ss_paths) pool.push_back(p);
  if(g_ss_fav){
    for(auto&g:g_games){ if(!g.fav)continue;
      String jp=g.jpg_path;
      if(!jp.length()||jp=="?"){ String r; if(g.first_file_idx>=0&&g.first_file_idx<(int)g_files.size()&&findJPGFor(g_files[g.first_file_idx],r))jp=r; else jp=""; }
      if(jp.length()&&jp!="?") pool.push_back(jp);
    }
  }
  for(int i=(int)pool.size()-1;i>0;i--){ int j=(int)(esp_random()%(uint32_t)(i+1)); String t=pool[i];pool[i]=pool[j];pool[j]=t; }
  return pool;
}
// the slideshow loop; returns to caller (which restores the UI). Any touch exits.
static void runSlideshow(std::vector<String>&pool){
  bool dbl=ssSlideAlloc();
  int idx=0;
  ssSlideRender(pool[0], dbl?g_slA:NULL);
  gfx_flush();
  if(ssSlideHold(g_ss_time_ms)){ ssSlideFree(); return; }
  while(true){
    webPanelService();   // keep the web UI (and its queued loads) alive while the saver owns the screen
    if(pool.size()<=1){ if(ssSlideHold(g_ss_time_ms))break; else continue; }
    int ni=(idx+1)%(int)pool.size();
    if(dbl){
      ssSlideRender(pool[ni], g_slB);                 // decode incoming into g_slB (screen still shows outgoing)
      int fx = g_ss_fx? g_ss_fx : (SSFX_FADE+(int)(esp_random()%3));   // SHUFFLE -> fade/dissolve/slide
      bool touched=ssSlideFx(fx);
      memcpy(g_slA,g_slB,(size_t)LCD_WIDTH*LCD_HEIGHT*2);   // incoming becomes the new outgoing
      if(touched)break;
    } else {
      ssSlideRender(pool[ni], NULL); gfx_flush();      // tight PSRAM: hard cut
    }
    idx=ni;
    if(ssSlideHold(g_ss_time_ms))break;
  }
  ssSlideFree();
}

// 5.8.3: SSMODE=MATRIX — falling-code screensaver. Any touch exits (same wake
// contract as bounce/slideshow). Cheap: full-frame redraw of short per-column
// glyph trails at ~16 fps.
static void runMatrixRain(){
  const int CW=12, CH=16;                       // cell (text size 2)
  int cols=gW/CW; if(cols>128)cols=128; if(cols<1)cols=1;   // lab5-P4G7: was 64 (768 px) - the 7" is 1024 wide
  int rows=gH/CH; if(rows<1)rows=1;
  int head[128], spd[128];
  for(int c=0;c<cols;c++){ head[c]=-(int)(esp_random()%(uint32_t)(rows+1)); spd[c]=1+(int)(esp_random()%2); }
  static const char GL[]="0123456789ABCDEFGHKMNPRXZ<>[]=+*/";
  const int NG=(int)sizeof(GL)-1;
  gfx_setTextSize(2);
  gfx_fillScreen(TFT_BLACK); gfx_flush();
  uint32_t last=millis(), seed=1;
  while(true){
    webPanelService();   // keep the web UI (and its queued loads) alive while the saver owns the screen
    if(Touch_ReadFrame()){ uint32_t t0=millis(); while(Touch_ReadFrame()&&millis()-t0<400)delay(10); break; }
    uint32_t nf=millis();
    if(nf-last>=60){ last=nf; seed++;
      gfx_fillScreen(TFT_BLACK);
      for(int c=0;c<cols;c++){
        int hy=head[c];
        for(int tl=0;tl<14;tl++){
          int ry=hy-tl; if(ry<0||ry>=rows) continue;
          uint16_t col;
          if(tl==0) col=0xFFFF;                                  // bright head
          else { int g=60-tl*4; if(g<8)g=8; col=(uint16_t)((g&0x3F)<<5); }   // fading green trail
          uint32_t h=(uint32_t)c*928371u + (uint32_t)ry*1237u + (tl==0?seed:0u);
          char s[2]={ GL[h%(uint32_t)NG], 0 };
          gfx_setTextColor(col,TFT_BLACK);
          gfx_setCursor(c*CW, ry*CH);
          gfx_print(s);
        }
        head[c]+=spd[c];
        if(head[c]-14>rows){ head[c]=0; spd[c]=1+(int)(esp_random()%2); }
      }
      gfx_flush();
    } else delay(5);
  }
  g_touch_active=false; g_touch_release=0; g_last_touch_ms=millis();
  if(g_car_active){drawCarousel();gfx_flush();} else {drawFullUI();gfx_flush();}
}

static void runScreensaver(){                                // blocking bounce loop; any touch exits
  // v5.7.2: slideshow mode — real images (folder + favourited covers) with transitions.
  // Only engages when there's genuine content; otherwise falls through to the sprite bounce.
  if(g_ss_matrix){ runMatrixRain(); return; }                 // 5.8.3: falling-code saver
  if(g_ss_slides && g_cracktro!=7 && g_cracktro!=8){
    std::vector<String> pool=ssBuildSlidePool();
    if(!pool.empty()){
      gfx_fillScreen(TFT_BLACK); gfx_flush();
      runSlideshow(pool);
      { uint32_t t0=millis(); while(Touch_ReadFrame()&&millis()-t0<400)delay(10); }   // drain the wake touch
      g_touch_active=false; g_touch_release=0; g_last_touch_ms=millis();
      if(g_car_active){drawCarousel();gfx_flush();} else {drawFullUI(); gfx_flush();}
      return;
    }
  }
  int idx=0;
  bool deniseMode=(g_cracktro==8);                          // 5.4.0: hidden Denise theme
  bool vincent=false;
  bool wranglerMode=(g_cracktro==9);                        // v5.5.2: bounce the @wrangler_amiga wordmark
  bool retronautMode=(g_cracktro==10);                       // P4.9: bounce the Retronaut helmet
  bool claudeMode=(!deniseMode)&&(!wranglerMode)&&(!retronautMode)&&g_ss_paths.empty();
  int ssForm=0;                                              // v4.8.1 ghost + v4.9.6 lolly: cycles on every wall hit
  bool showName=false; const char* curName=NAMES[0]; int nameSz=3;   // v5.5.1: bounce can flip to a contributor name
  float ph=0;
  if(deniseMode){
    if(!ssMakeName(false)){g_ss_have=false;return;}
  } else if(wranglerMode){
    curName="@wrangler_amiga"; nameSz=3; gfx_setTextSize(nameSz);
    while(nameSz>1&&gfx_textWidth(curName)>gW-8){nameSz--;gfx_setTextSize(nameSz);}
    if(!ssMakeClaude(0)){g_ss_have=false;return;}   // v5.5.5: start on a sprite; wall hits flip @wrangler_amiga <-> sprite
  } else if(retronautMode){
    if(!retroHelmDecode()){g_ss_have=false;return;}
  } else if(claudeMode){
    // Empty /screensaver/ folder: bounce the (slowly spinning) Claude starburst
    if(!g_ss_claude||!ssMakeClaude(0)){g_ss_have=false;return;}
  } else {
    bool ok=false;
    for(int t=0;t<(int)g_ss_paths.size();t++){ if(ssDecode(g_ss_paths[t])){idx=t;ok=true;break;} }
    if(!ok){ssFree();g_ss_have=false;return;}   // no decodable image -> disarm, back to UI
  }
  int x,y,vx,vy;
  if(deniseMode||wranglerMode||retronautMode){             // 5.4.0/5.5.2/P4.9: random start pos + direction (no two-corner lock)
    x=(int)(esp_random()%(uint32_t)(gW-g_ss_w>0?gW-g_ss_w:1));
    y=(int)(esp_random()%(uint32_t)(gH-g_ss_h>0?gH-g_ss_h:1));
    vx=1+(int)(esp_random()%3); if(esp_random()&1)vx=-vx;
    vy=1+(int)(esp_random()%3); if(esp_random()&1)vy=-vy;
  } else { x=(gW-g_ss_w)/2; y=(gH-g_ss_h)/2; vx=2; vy=2; }
  gfx_fillScreen(TFT_BLACK);
  uint32_t last=millis();
  while(true){
    webPanelService();   // keep the web UI (and its queued loads) alive while the saver owns the screen
    if(Touch_ReadFrame()){ uint32_t t0=millis(); while(Touch_ReadFrame()&&millis()-t0<400)delay(10); break; }
    uint32_t nf=millis();
    if(nf-last>=33){ last=nf;
      x+=vx; y+=vy; bool hit=false;
      if(x<=0){x=0;vx=-vx;hit=true;} else if(x>=gW-g_ss_w){x=gW-g_ss_w;vx=-vx;hit=true;}
      if(y<=0){y=0;vy=-vy;hit=true;} else if(y>=gH-g_ss_h){y=gH-g_ss_h;vy=-vy;hit=true;}
      if(deniseMode&&hit){ vincent=!vincent; ssMakeName(vincent);   // 5.4.0: swap Denise <-> Vincent on each wall hit
        if(x>gW-g_ss_w)x=gW-g_ss_w; if(y>gH-g_ss_h)y=gH-g_ss_h; if(x<0)x=0; if(y<0)y=0; }
      else if(hit&&!claudeMode&&!wranglerMode&&g_ss_paths.size()>1){ int ni=(idx+1)%(int)g_ss_paths.size();
        if(ssDecode(g_ss_paths[ni]))idx=ni; else ssDecode(g_ss_paths[idx]);   // skip undecodable, keep a valid buffer
        if(x>gW-g_ss_w)x=gW-g_ss_w; if(y>gH-g_ss_h)y=gH-g_ss_h; if(x<0)x=0; if(y<0)y=0; }
      if((claudeMode||wranglerMode)&&hit){ if((esp_random()%5)<2){ showName=true; curName=wranglerMode?"@wrangler_amiga":NAMES[(int)(esp_random()%(uint32_t)N_NAMES)]; nameSz=3; gfx_setTextSize(nameSz); while(nameSz>1&&gfx_textWidth(curName)>gW-8){nameSz--;gfx_setTextSize(nameSz);} g_ss_w=gfx_textWidth(curName); g_ss_h=8*nameSz; if(x>gW-g_ss_w)x=gW-g_ss_w; if(y>gH-g_ss_h)y=gH-g_ss_h; if(x<0)x=0; if(y<0)y=0; } else { showName=false; ssForm=(int)(esp_random()%3); if(x>gW-96)x=gW-96; if(y>gH-96)y=gH-96; if(x<0)x=0; if(y<0)y=0; } }   // v5.5.1: ~40% of bounces flip to a name
      if((claudeMode||wranglerMode)&&!showName){ ph+=0.02f; if(ssForm==1)ssMakeGhost(ph); else if(ssForm==2)ssMakeLolly(ph); else ssMakeClaude(ph); }   // re-render each frame
      gfx_fillScreen(TFT_BLACK); if((claudeMode||wranglerMode)&&showName){ gfx_setTextSize(nameSz); gfx_setTextColor(CRK_RGB(244,238,225),TFT_BLACK); gfx_setCursor(x,y); gfx_print(curName); } else ssBlit(x,y); gfx_flush();
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
  uint32_t _t0=millis();   // lab15a2: how long before the SCANNING screen
  g_info_showing=false;
  g_cover_flags_ready=false;   // v5.9.2: covers may have changed -> recompute reel-filter flags
  // Delete all cache files
  SD_MMC.remove("/ADF/.index");SD_MMC.remove("/DSK/.index");
  SD_MMC.remove("/ADF/.gamecache");SD_MMC.remove("/DSK/.gamecache");
  // v5.7.2: re-read CONFIG.TXT so edits made on the card (theme, screensaver options,
  // categories, font, language, rotation, ...) take effect on a RESCAN without a reboot.
  // Runs before the library rebuild so CATEGORIES/NESTING changes apply. Transfer MODE is
  // deliberately preserved — flipping standalone/wireless wants a clean boot, not a rescan.
  uint32_t _t1=millis();
  { bool wl=g_wireless_mode; selfHealConfig(); loadConfig(); g_wireless_mode=wl; relayout(); }
  uint32_t _t2=millis();
  // Rescan with animation
  g_files.release();gamesClear();   // lab14e: free the buffers, not just the contents
  carRuntimeRelease();   // lab14e: give the old reel tiles + micro-thumbs back before the new library is sized
  gLog("[rescan] before the scan: delete caches %lums, config %lums, free library %lums | int=%u psram=%u\n",(unsigned long)(_t1-_t0),(unsigned long)(_t2-_t1),(unsigned long)(millis()-_t2),(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),(unsigned)ESP.getFreePsram());   // lab15a2
  heap_caps_malloc_extmem_enable(0);listImages(SD_MMC,g_files);buildGameList();buildThumbs();   // lab14e: 0, not 16 (see setup)
  writeNfoCacheIfChanged();   // 5.9.31-lab1: persist the freshly harvested sidecars
  g_nfoharvest.clear();g_nfoharvest.shrink_to_fit();fwLocFree();g_manualset.clear();g_manualset.shrink_to_fit();g_hdset.clear();g_hdset.shrink_to_fit();
  heap_caps_malloc_extmem_enable(4096);applyStats();buildActiveLetters();scanScreensaver();
  bcSet(BC_READY,(uint32_t)g_games.size());   // lab14e
  sdGuardReport(true);                          // lab14g
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
// ════════════════════════════════════════════════════════════════════════════
// HOME-WIFI ON-SCREEN SETUP (Increment 1a: keyboard + manual entry + save).
// No WiFi/ESP-NOW radio use here — pure text entry + CONFIG.TXT save, so it can
// never disturb an active dongle link. Scan-and-pick (radio) is Increment 1b.
// ════════════════════════════════════════════════════════════════════════════

// Full on-screen keyboard. Edits `io` in place (starts from its current text).
// Case-sensitive: SHIFT toggles letter case; ?123/ABC toggles alpha<->symbols.
// Returns true on OK, false on CANCEL. Blocking modal, same idiom as doSearch().
// Wait for a CLEAN finger release before returning from a keyboard: require several
// consecutive no-touch frames, so a single dropped touch frame (common on these
// panels) can't be read as a lift. Without this, tapping OK bleeds through to the
// next screen (the Home-WiFi SSID->password 'jumps past password' bug).
static void kbWaitRelease(uint32_t maxMs=900){
  int up=0; uint32_t t0=millis();
  while (up<4 && millis()-t0<maxMs){ if(Touch_ReadFrame()) up=0; else up++; delay(10); }
}

static bool kbInput(const char* title, String& io, int maxlen){
  String q = io;
  bool shift = false, sym = false;
  const char* AL[4] = {"1234567890","qwertyuiop","asdfghjkl","zxcvbnm"};
  const char* SY[4] = {"1234567890","!@#$%^&*()","-_=+.,:;?/","'[]{}<>~|`"};   // no backslash / doublequote
  const int gap = 4, kh = 34, bm = 6;
  const int kbBlock = 5*kh + 4*gap;          // 4 char rows + 1 control row
  const int kbTop = VH - bm - kbBlock;
  bool dirty = true, pressed = false; int rel = 0;
  kbWaitRelease(600);   // drain the tap that opened the keyboard (robust to dropped touch frames)

  while (true) {
    if (dirty) { dirty = false;
      gfx_fillScreen(COL_BG);
      bool liteBar = (inkFor(COL_BAR) == TFT_BLACK);
      gfx_fillRect(0,0,VW,22,COL_BAR);
      gfx_setTextSize(1); gfx_setTextColor(liteBar?TFT_BLACK:COL_AMBER, COL_BAR);
      gfx_setCursor(6,7); gfx_print(title);
      // input box
      gfx_fillRoundRect(8,26,VW-16,32,6,COL_PANEL); gfx_drawRoundRect(8,26,VW-16,32,6,COL_AMBER);
      gfx_setTextSize(2); gfx_setTextColor(inkFor(COL_PANEL), COL_PANEL);
      String shown = q; while (gfx_textWidth(shown) > VW-52 && shown.length() > 0) shown = shown.substring(1);
      gfx_setCursor(18,34); gfx_print(shown); gfx_print("_");
      // char rows
      const char** ROWS = sym ? SY : AL;
      int ky = kbTop;
      for (int r=0; r<4; r++) {
        int n = strlen(ROWS[r]); int kw = (VW-gap)/10 - gap; int kx = gap + ((10-n)*(kw+gap))/2;
        for (int i=0; i<n; i++) {
          char ch = ROWS[r][i];
          if (!sym && shift && ch>='a' && ch<='z') ch -= 32;
          gfx_fillRoundRect(kx,ky,kw,kh,5,COL_BAR);
          gfx_setTextSize(2); gfx_setTextColor(inkFor(COL_BAR), COL_BAR);
          char lb[2] = { ch, 0 }; gfx_setCursor(kx+(kw-gfx_textWidth(lb))/2, ky+(kh-16)/2); gfx_print(lb);
          kx += kw+gap;
        }
        ky += kh+gap;
      }
      // control row: SHIFT | ?123/ABC | SPACE | DEL | OK
      const char* CTL[5];
      CTL[0] = sym ? "" : (shift ? "shift*" : "shift");
      CTL[1] = sym ? "ABC" : "?123";
      CTL[2] = "space"; CTL[3] = "del"; CTL[4] = "OK";
      uint16_t cc[5] = { COL_BAR, COL_BAR, COL_BAR, (uint16_t)0x8000, COL_SEL };
      int cw = (VW - 6*gap)/5, cx = gap;
      for (int i=0; i<5; i++) {
        gfx_fillRoundRect(cx,ky,cw,kh,6,cc[i]);
        gfx_setTextSize(1); gfx_setTextColor(inkFor(cc[i]), cc[i]);
        gfx_setCursor(cx+(cw-gfx_textWidth(CTL[i]))/2, ky+(kh-8)/2); gfx_print(CTL[i]);
        cx += cw+gap;
      }
      gfx_flush();
    }

    uint16_t tx=0, ty=0; bool have = Touch_ReadFrame() && getTouchXY(&tx,&ty);
    if (have) { rel = 0;
      if (!pressed) { pressed = true; bool handled = false;
        const char** ROWS = sym ? SY : AL;
        int ky = kbTop;
        for (int r=0; r<4 && !handled; r++) {
          int n = strlen(ROWS[r]); int kw = (VW-gap)/10 - gap; int kx0 = gap + ((10-n)*(kw+gap))/2;
          if (ty>=ky && ty<ky+kh) {
            int i = ((int)tx - kx0)/(kw+gap); int within = ((int)tx - kx0) - i*(kw+gap);
            if ((int)tx>=kx0 && i>=0 && i<n && within<kw) {
              char ch = ROWS[r][i];
              if (!sym && shift && ch>='a' && ch<='z') ch -= 32;
              if ((int)q.length() < maxlen) q += ch;
              dirty = true; handled = true;
            }
          }
          ky += kh+gap;
        }
        if (!handled && ty>=ky && ty<ky+kh) {
          int cw = (VW - 6*gap)/5; int i = ((int)tx - gap)/(cw+gap); int cxi = gap + i*(cw+gap);
          if (i>=0 && i<5 && (int)tx>=cxi && (int)tx<cxi+cw) {
            if (i==0) { if (!sym) shift = !shift; dirty = true; }
            else if (i==1) { sym = !sym; if (sym) shift = false; dirty = true; }
            else if (i==2) { if ((int)q.length()<maxlen) q += ' '; dirty = true; }
            else if (i==3) { if (q.length()) q.remove(q.length()-1); dirty = true; }
            else if (i==4) { io = q; kbWaitRelease(); return true; }
          }
        }
      }
    } else { if (pressed && ++rel>=3) pressed = false; }
    delay(12);
  }
}

// A tiny centred message screen (save confirmation etc.), auto-dismiss after ms.
static void hwMsg(const char* l1, const char* l2, uint16_t col, uint32_t ms){
  gfx_fillScreen(COL_BG);
  gfx_setTextSize(2); gfx_setTextColor(col, COL_BG);
  gfx_setCursor((VW - gfx_textWidth(l1))/2, VH/2 - 20); gfx_print(l1);
  if (l2 && l2[0]) { gfx_setTextSize(1); gfx_setTextColor(COL_DIM, COL_BG);
    gfx_setCursor((VW - gfx_textWidth(l2))/2, VH/2 + 6); gfx_print(l2); }
  gfx_flush(); delay(ms);
}

// 5.9.13: scan for WiFi networks and let the user TAP one (kills SSID typos and
// case mismatches). Returns the chosen SSID, or "" if the user picks Type-manually.
static String doWifiScanPick(){
  for(;;){
    hwMsg("Scanning WiFi...", "", COL_ACCENT, 10);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false,false);      // 5.9.20: drop the active link so the scan actually runs (ESP32 returns 0 while associated+serving)
    delay(120);
    int n = WiFi.scanNetworks(false,true);   // blocking, include hidden
    if(n<0) n=0;
    String names[8]; int cnt=0;
    for(int i=0;i<n && cnt<8;i++){
      String sn = WiFi.SSID(i); if(sn.length()==0) continue;
      bool dup=false; for(int j=0;j<cnt;j++) if(names[j]==sn){dup=true;break;}
      if(!dup) names[cnt++]=sn;
    }
    WiFi.scanDelete();
    gfx_fillScreen(COL_BG);
    gfx_setTextSize(2); gfx_setTextColor(COL_ACCENT,COL_BG); gfx_setCursor(10,10); gfx_print("Pick WiFi network");
    gfx_setTextSize(1);
    const int top=40, rh=26;
    for(int i=0;i<cnt;i++){ int y=top+i*rh; gfx_drawRoundRect(8,y,VW-16,rh-4,5,COL_SEP); gfx_setTextColor(COL_LIT,COL_BG); gfx_setCursor(14,y+7); gfx_print(names[i]); }
    int listRows=cnt;
    if(cnt==0){ gfx_setTextColor(COL_AMBER,COL_BG); gfx_setCursor(14,top+7); gfx_print("(none found - tap Rescan)"); listRows=1; }
    const int ym=top+listRows*rh;     gfx_fillRoundRect(8,ym,VW-16,rh-4,5,COL_BAR); gfx_setTextColor(COL_LIT,COL_BAR); gfx_setCursor(14,ym+7); gfx_print("[ Type manually ]");
    const int yr=top+(listRows+1)*rh; gfx_fillRoundRect(8,yr,VW-16,rh-4,5,COL_BAR); gfx_setTextColor(COL_LIT,COL_BAR); gfx_setCursor(14,yr+7); gfx_print("[ Rescan ]");
    gfx_flush();
    bool pressed=false; int rel=0; bool rescan=false;
    for(;;){
      uint16_t tx=0,ty=0; bool have=Touch_ReadFrame()&&getTouchXY(&tx,&ty);
      if(have){ if(!pressed){ pressed=true;
        if((int)ty>=top){
          int idx=((int)ty-top)/rh;
          if(idx>=0 && idx<cnt){ kbWaitRelease(); return names[idx]; }
          if(idx==listRows){ kbWaitRelease(); return String(""); }
          if(idx==listRows+1){ kbWaitRelease(); rescan=true; break; }
        }
      }} else { if(pressed && ++rel>=3) pressed=false; }
      delay(12);
    }
    if(rescan) continue;
  }
}

// The Home-WiFi setup flow: enter SSID, enter password, save + enable HOMEWIFI.
// Reached from the settings screen (IA_HOMEWIFI).
static void doHomeWifiSetup(){
  String ssid = g_home_ssid, pass = g_home_pass;
  String picked = doWifiScanPick();                                     // 5.9.13: scan + tap (no typos / case mismatch)
  if (picked.length()) ssid = picked;
  else if (!kbInput("Home WiFi: network name (SSID)", ssid, 32)) return;   // manual fallback / cancel
  ssid.trim();
  if (ssid.length() == 0) { hwMsg("No SSID", "nothing saved", COL_AMBER, 1200); return; }
  if (!kbInput("Home WiFi: password (blank = open)", pass, 63)) return; // cancel

  g_home_ssid = ssid; g_home_pass = pass; g_link_home = true;
  saveConfigKey("HOME_SSID", ssid);
  saveConfigKey("HOME_PASS", pass);
  saveConfigKey("LINK", "HOMEWIFI");
  hwMsg("Home WiFi saved", ("SSID: " + ssid).c_str(), COL_ACCENT, 1600);
  if (g_web_on) webPanelBegin();   // 5.9.13: re-join with the new creds now (non-blocking, no reboot)
}

// 5.9.9: Web UI / WiFi SD-access setup (STANDALONE only — the web server can't
// coexist with ESP-NOW). Turning it on captures home-WiFi creds if not set,
// flips WEBUI=ON, and reboots so webPanelBegin() brings the server up.
// 5.9.17: switch the radio between the three MODE states LIVE (no reboot).
// STANDALONE = radio off, ESP-NOW = blind dongles, WiFi = home router + web UI.
static void applyRadioMode(){
  // P4: the radio lives on the C6 (esp-hosted). Switching it OFF and back on re-inits the hosted link, which
  // crashed it (5.9.12) - so on the P4 the STA stays up and MODE only decides what the GTi does with it.
  if(g_espnow_started){ espnowStop(); g_espnow_started=false; }   // P4: forgets the link state only; radio stays up
  webPanelStop();                                                 // stop the web server if it was up
  WiFi.disconnect(false,true); delay(60);                         // P4: drop the association, keep the radio on
  if(!g_wireless_mode){ }                                         // STANDALONE: radio idle
  else if(g_link_home){ webPanelBegin(); }                        // WiFi (non-blocking; server comes up in webPanelService)
  else if(g_c6_ready){ ensureEspNow(); }                          // wireless dongles (Wi-Fi-direct on the P4)
}

static void doWebUiSetup(){
  if(g_web_on){                                   // currently on -> turn off
    g_web_on=false; saveConfigKey("WEBUI","OFF");
    hwMsg("Web UI off", "reboot to apply", COL_AMBER, 1400);
    return;
  }
  if(g_home_ssid.length()==0){                    // need creds first
    String ssid=g_home_ssid, pass=g_home_pass;
    if(!kbInput("Web UI: home WiFi name (SSID)", ssid, 32)) return;   // cancel
    ssid.trim();
    if(ssid.length()==0){ hwMsg("No SSID","nothing saved",COL_AMBER,1200); return; }
    if(!kbInput("Web UI: WiFi password (blank = open)", pass, 63)) return;   // cancel
    g_home_ssid=ssid; g_home_pass=pass;
    saveConfigKey("HOME_SSID", ssid);
    saveConfigKey("HOME_PASS", pass);
  }
  g_web_on=true; saveConfigKey("WEBUI","ON");
  hwMsg("Web UI on", "rebooting to GTi.local", COL_ACCENT, 1400);
  delay(700); ESP.restart();
}

// 5.9.10: on-screen WiFi connection check for the web UI. Shows whether the
// panel joined home WiFi, its IP, signal and whether the server is up. If WiFi
// is fine but the server never started (WEBUI enabled after boot, or an earlier
// join failed), it brings the server up live rather than needing another reboot.
static void doWifiCheck(){
  auto draw=[&](const char* status, uint16_t scol){
    gfx_fillScreen(COL_BG);
    gfx_setTextSize(2); gfx_setTextColor(COL_ACCENT,COL_BG);
    gfx_setCursor(10,12); gfx_print("WiFi Check");
    gfx_setTextSize(1); int y=46;
    auto line=[&](const String& t, uint16_t c){ gfx_setTextColor(c,COL_BG); gfx_setCursor(10,y); gfx_print(t); y+=16; };
    line(String("SSID: ")+(g_home_ssid.length()?g_home_ssid:String("(none set)")), COL_LIT);
    line(String("Status: ")+status, scol);
    if(WiFi.status()==WL_CONNECTED){
      line(String("IP: ")+WiFi.localIP().toString(), COL_GREEN);
      line(String("Signal: ")+String((int)WiFi.RSSI())+" dBm", COL_LIT);
      line(String("Web server: ")+(g_web_up?"UP":"not started"), g_web_up?COL_GREEN:COL_AMBER);
      line("Open on your phone/PC:", COL_DIM);
      line(String("  http://GTi.local/files"), COL_LIT);
      line(String("  http://")+WiFi.localIP().toString()+"/files", COL_LIT);
    } else {
      line("Not on the network.", COL_AMBER);
      line("Check the password, and that it is a", COL_DIM);
      line("2.4GHz network (ESP32 is 2.4G only).", COL_DIM);
    }
    y+=8; line("Tap to close", COL_DIM);
    gfx_flush();
  };
  if(g_home_ssid.length()==0){ hwMsg("No home WiFi set","use WEB UI to set it up",COL_AMBER,1600); return; }

  draw("checking...", COL_AMBER);
  if(WiFi.status()!=WL_CONNECTED){
    WiFi.mode(WIFI_STA); WiFi.persistent(false); WiFi.setAutoReconnect(true);
    WiFi.begin(g_home_ssid.c_str(), g_home_pass.c_str());
    uint32_t t0=millis();
    while(WiFi.status()!=WL_CONNECTED && millis()-t0<12000) delay(200);
  }
  if(WiFi.status()==WL_CONNECTED && g_web_on && !g_web_up) webPanelBegin();   // bring the server up now

  const bool ok=(WiFi.status()==WL_CONNECTED);
  draw(ok?"CONNECTED":"FAILED", ok?COL_GREEN:(uint16_t)0x8000);

  bool pressed=false; int rel=0;                    // wait for a tap to dismiss
  while(true){
    uint16_t tx=0,ty=0; bool have = Touch_ReadFrame() && getTouchXY(&tx,&ty);
    if(have){ if(!pressed){ pressed=true; kbWaitRelease(); break; } }
    else { if(pressed && ++rel>=3) pressed=false; }
    delay(12);
  }
}

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
  kbWaitRelease(600);
  auto recompute=[&](){nMatch=0;totalMatch=0;if(!q.length())return;String ql=q;ql.toLowerCase();
    for(int i=0;i<(int)g_games.size();i++){String nm=g_games[i].name;nm.toLowerCase();
      if(nm.indexOf(ql)>=0){if(nMatch<resMax)matches[nMatch++]=i;totalMatch++;}}};
  while(true){
    if(dirty){dirty=false;
      gfx_fillScreen(COL_BG);
      bool liteBar=(inkFor(COL_BAR)==TFT_BLACK);
      gfx_fillRect(0,0,VW,22,COL_BAR);gfx_setTextSize(1);gfx_setTextColor(liteBar?TFT_BLACK:COL_AMBER,COL_BAR);gfx_setCursor(6,7);gfx_print(T(L_SEARCH));
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
            setActiveLetter(bucketOf(g_games[t].name));kbWaitRelease();return true;}}
        int ky=kbTop;
        for(int r=0;r<4&&!handled;r++){int n=strlen(SROWS[r]);int kw=(VW-gap)/10-gap;int kx0=gap+((10-n)*(kw+gap))/2;
          if(ty>=ky&&ty<ky+kh){int i=((int)tx-kx0)/(kw+gap);int within=((int)tx-kx0)-i*(kw+gap);
            if((int)tx>=kx0&&i>=0&&i<n&&within<kw){if(q.length()<24){q+=SROWS[r][i];recompute();}dirty=true;handled=true;}}
          ky+=kh+gap;}
        if(!handled&&ty>=ky&&ty<ky+kh){int cw=(VW-4*gap)/3;int i=((int)tx-gap)/(cw+gap);int cxi=gap+i*(cw+gap);
          if(i>=0&&i<3&&(int)tx>=cxi&&(int)tx<cxi+cw){
            if(i==0){if(q.length()){q.remove(q.length()-1);recompute();}dirty=true;}
            else if(i==1){if(q.length()<24){q+=' ';recompute();}dirty=true;}
            else if(i==2){kbWaitRelease();return false;}}}
      }
    } else { if(pressed&&++rel>=3)pressed=false; }
    delay(12);
  }
}

// ── In-game manual reader (.rtfm) — full-screen scrolling plain text, blocking modal. v4.9.2.
//    Text size FOLLOWS the game menu (g_font: SMALL/NORMAL/LARGE); the SIZE button cycles it
//    live. Drag to scroll with flick inertia. The loader strips/transliterates non-ASCII so a
//    messy file still renders clean on the 6x8 font. Capped ~16 KB (manuals are short by design).
// ── .rtfm v2 helpers (5.6.x): [SECTION] markers -> accent headings + a jump picker.
//    Backwards-compatible: markers are inert plain text to older firmware. See the .rtfm v2 contract.
static String g_rtfmLastPath=""; static float g_rtfmLastScroll=0;   // remember last read position (per game, session)
static bool rtfmIsSection(const String& s, String& label){          // a line that is exactly [text] => a heading
  int a=0,b=(int)s.length()-1;
  while(a<=b && (s[a]==' '||s[a]=='\t'))a++;
  while(b>=a && (s[b]==' '||s[b]=='\t'))b--;
  if(b-a>=1 && s[a]=='[' && s[b]==']'){ label=s.substring(a+1,b); label.trim(); return label.length()>0; }
  return false;
}
static int rtfmSectionMenu(const std::vector<String>& sec){         // full-screen jump picker; returns index or -1
  int n=(int)sec.size(); if(n<=0)return -1;
  const int rowH=36, listTop=26, botY=VH-40;
  int maxRows=(botY-listTop)/rowH; if(maxRows<1)maxRows=1;
  int scroll=0, maxSc=(n>maxRows)?(n-maxRows):0;
  bool down=false, moved=false; int dY=0, dScroll=0, lastY=0; bool dirty=true;
  {uint32_t t0=millis();while(Touch_ReadFrame()&&millis()-t0<400)delay(10);}
  while(true){
    if(dirty){ dirty=false;
      gfx_fillScreen(COL_BG);
      bool liteBar=(inkFor(COL_BAR)==TFT_BLACK);
      gfx_fillRect(0,0,VW,22,COL_BAR); gfx_setTextSize(1); gfx_setTextColor(liteBar?TFT_BLACK:COL_AMBER,COL_BAR);
      gfx_setCursor(6,7); gfx_print("JUMP TO SECTION");
      for(int r=0;r<maxRows && r+scroll<n;r++){ int i=r+scroll; int y=listTop+r*rowH;
        gfx_fillRoundRect(6,y+2,VW-12,rowH-4,6,COL_PANEL); gfx_drawRoundRect(6,y+2,VW-12,rowH-4,6,COL_ACCENT);
        gfx_setTextSize(2); gfx_setTextColor(inkFor(COL_PANEL),COL_PANEL);
        String s=sec[i]; while(gfx_textWidth(s)>VW-28&&s.length()>1)s=s.substring(0,s.length()-1);
        gfx_setCursor(14,y+(rowH-16)/2); gfx_print(s); }
      gfx_fillRoundRect(VW/2-60,VH-36,120,30,8,0x8000); gfx_drawRoundRect(VW/2-60,VH-36,120,30,8,COL_AMBER);
      gfx_setTextSize(1); gfx_setTextColor(TFT_WHITE,0x8000); { const char* c="CANCEL"; gfx_setCursor(VW/2-gfx_textWidth(c)/2,VH-36+11); gfx_print(c); }
      gfx_flush();
    }
    uint16_t tx=0,ty=0; bool have=Touch_ReadFrame()&&getTouchXY(&tx,&ty); (void)tx;
    if(have){ if(!down){down=true;moved=false;dY=ty;dScroll=scroll;lastY=ty;}
      else { if(abs((int)ty-dY)>DRAG_THRESH)moved=true;
        if(moved&&maxSc>0){ scroll=dScroll-((int)ty-dY)/rowH; if(scroll<0)scroll=0; if(scroll>maxSc)scroll=maxSc; dirty=true; } }
      lastY=ty;
    } else if(down){ down=false;
      if(!moved){ if(lastY>=VH-40) return -1;
        int r=((int)lastY-listTop)/rowH; int i=r+scroll;
        if((int)lastY>=listTop && r>=0 && r<maxRows && i<n) return i; }
    }
    delay(12);
  }
}
static void doManual(const String& path,const char* title=nullptr){   // lab15j: title (e.g. "NFO"); default = MANUAL
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
  if(!raw.length())raw=title?"(no text)":"(no manual text)";
  const int margin=8, topH=22, botH=30;
  const int areaTop=topH+3, areaBot=VH-botH-2, maxW=VW-2*margin;
  int sz=g_name_sz, lineH=8*sz+(sz>=2?5:3);
  std::vector<String> lines; std::vector<uint8_t> head; std::vector<int> secLine; std::vector<String> secName;   // v2: head[i]=heading; sec* = jump index
  auto rewrap=[&](){ lines.clear(); head.clear(); secLine.clear(); secName.clear(); sz=g_name_sz; lineH=8*sz+(sz>=2?5:3); gfx_setTextSize(sz);
    String para="";
    for(int i=0;i<=(int)raw.length();i++){ char c=i<(int)raw.length()?raw[i]:'\n';
      if(c=='\n'){ String lab;
        if(rtfmIsSection(para,lab)){ secLine.push_back((int)lines.size()); secName.push_back(lab); lines.push_back(String("[")+lab+"]"); head.push_back(1); }
        else { String line="",word="";
          for(int j=0;j<=(int)para.length();j++){ char d=j<(int)para.length()?para[j]:' ';
            if(d==' '||j==(int)para.length()){ String cand=line.length()?line+" "+word:word;
              if(gfx_textWidth(cand)>maxW&&line.length()){lines.push_back(line);head.push_back(0);line=word;} else line=cand; word=""; }
            else word+=d; }
          lines.push_back(line); head.push_back(0); }
        para=""; }
      else para+=c; } };
  rewrap();
  float scroll=0, vel=0; int y0=0; float s0=0; bool pressed=false, moved=false; int lastY=0, lastX=0, rel=0; bool dirty=true;
  auto maxScroll=[&](){ int t=(int)lines.size()*lineH-(areaBot-areaTop); return t<0?0.f:(float)t; };
  if(path==g_rtfmLastPath){ scroll=g_rtfmLastScroll; float ms=maxScroll(); if(scroll<0)scroll=0; if(scroll>ms)scroll=ms; }   // v2: restore last read position
  {uint32_t t0=millis();while(Touch_ReadFrame()&&millis()-t0<600)delay(10);}   // drain the entering tap
  while(true){
    int nbtn = secName.size()? 4 : 3;   // v2 buttons: SIZE, TOP, [SECTIONS], CLOSE
    int bw = VW/nbtn;
    if(dirty||vel!=0){ dirty=false;
      gfx_fillScreen(COL_BG);
      gfx_setTextSize(sz);
      int first=(int)(scroll/lineH); if(first<0)first=0;
      int yy=areaTop-((int)scroll-first*lineH);
      for(int i=first;i<(int)lines.size()&&yy<areaBot;i++){ gfx_setTextColor(head[i]?COL_ACCENT:COL_LIT,COL_BG); gfx_setCursor(margin,yy); gfx_print(lines[i]); yy+=lineH; }
      float ms=maxScroll();
      if(ms>0){ int trkH=areaBot-areaTop; int thH=max(18,(int)((float)trkH*trkH/((float)lines.size()*lineH))); int thY=areaTop+(int)((float)(trkH-thH)*(scroll/ms));
        gfx_fillRect(VW-4,areaTop,2,trkH,COL_PANEL); gfx_fillRect(VW-4,thY,2,thH,COL_AMBER); }
      bool liteBar=(inkFor(COL_BAR)==TFT_BLACK);
      gfx_fillRect(0,0,VW,topH,COL_BAR); gfx_setTextSize(1); gfx_setTextColor(liteBar?TFT_BLACK:COL_AMBER,COL_BAR);
      gfx_setCursor(6,7); gfx_print(title?title:T(L_MANUAL));
      { int pct=ms>0?(int)(scroll*100/ms):100; String s=String(pct)+"%"; gfx_setTextColor(liteBar?COL_MID:COL_DIM,COL_BAR); gfx_setCursor(VW/2-gfx_textWidth(s)/2,7); gfx_print(s); }   // v2: % moved to top bar
      { String nm=g_games.empty()?String(""):g_games[g_sel].name; while(gfx_textWidth(nm)>VW/2-24&&nm.length()>1)nm=nm.substring(0,nm.length()-1);
        gfx_setTextColor(liteBar?COL_MID:COL_DIM,COL_BAR); gfx_setCursor(VW-gfx_textWidth(nm)-6,7); gfx_print(nm); }
      int by=VH-botH; gfx_fillRect(0,by,VW,botH,COL_BAR); gfx_hline(0,by,VW,COL_SEP);
      auto btn=[&](int idx,const String& lab,uint16_t bg,uint16_t brd,uint16_t ink){ int x=idx*bw;
        gfx_fillRoundRect(x+3,by+4,bw-6,botH-8,6,bg); gfx_drawRoundRect(x+3,by+4,bw-6,botH-8,6,brd);
        gfx_setTextSize(1); gfx_setTextColor(ink,bg); gfx_setCursor(x+(bw-gfx_textWidth(lab))/2,by+(botH-8)/2); gfx_print(lab); };
      btn(0,String("SIZE:")+fontName(g_font),COL_BAR,COL_ACCENT,COL_LIT);
      btn(1,"TOP",COL_BAR,COL_ACCENT,COL_LIT);
      if(secName.size()) btn(2,"SECTIONS",COL_BAR,COL_AMBER,COL_LIT);
      btn(nbtn-1,"CLOSE",0x8000,COL_AMBER,TFT_WHITE);
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
        if(!moved){ int by=VH-botH;
          if(lastY>=by){ int idx=lastX/bw; if(idx>=nbtn)idx=nbtn-1;
            if(idx==0){ applyFont((g_font+1)%3); saveConfigKey("FONT",fontKey(g_font)); rewrap(); float ms=maxScroll(); if(scroll>ms)scroll=ms; dirty=true; }
            else if(idx==1){ scroll=0; vel=0; dirty=true; }                                            // TOP
            else if(secName.size()&&idx==2){ int pick=rtfmSectionMenu(secName);                        // SECTIONS jump
              if(pick>=0&&pick<(int)secLine.size()){ scroll=(float)secLine[pick]*lineH; float ms=maxScroll(); if(scroll<0)scroll=0; if(scroll>ms)scroll=ms; vel=0; }
              { int q=0; uint32_t t0=millis(); while(q<8 && millis()-t0<1200){ if(Touch_ReadFrame())q=0; else q++; delay(8); } } pressed=false; moved=false; rel=0; dirty=true; }   // fix: drain the section-pick tap so it is not read as a body-tap (which closes the reader)
            else { g_rtfmLastPath=path; g_rtfmLastScroll=scroll; return; }                             // CLOSE
          }
          else { g_rtfmLastPath=path; g_rtfmLastScroll=scroll; return; }                               // tap body closes (forgiving)
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
  kbWaitRelease(600);   // drain the tap that opened this keyboard (bleed-through fix)
  while(true){
    if(dirty){dirty=false;
      gfx_fillScreen(COL_BG);
      // Theme-aware inks: on light bars/panels (PAPER) amber/white/dim gray are
      // unreadable — fall back to dark ink via inkFor().
      bool liteBar=(inkFor(COL_BAR)==TFT_BLACK);
      gfx_fillRect(0,0,VW,22,COL_BAR);gfx_setTextSize(1);gfx_setTextColor(liteBar?TFT_BLACK:COL_AMBER,COL_BAR);gfx_setCursor(6,7);gfx_print(T(L_NAME_DONGLE));
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
            else if(i==2){kbWaitRelease();return false;}
            else if(i==3){out.trim();kbWaitRelease();return true;}}
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
    gfx_setTextSize(1);gfx_setTextColor(COL_ORANGE,COL_BG);gfx_setCursor(8,8);gfx_print(T(L_NO_DONGLES));
    gfx_setTextColor(COL_DIM,COL_BG);gfx_setCursor(8,30);gfx_print(T(L_CHECK_DONGLE));
    gfx_setCursor(8,42);gfx_print(T(L_IN_RANGE));
    gfx_fillRoundRect(VW/2-50,VH-44,100,32,8,COL_BAR);gfx_drawRoundRect(VW/2-50,VH-44,100,32,8,COL_DIM);
    gfx_setTextColor(COL_LIT,COL_BAR);{const char*s="BACK";gfx_setCursor(VW/2-gfx_textWidth(s)/2,VH-34);}gfx_print(T(L_BACK));
    gfx_flush();
    uint32_t w=millis();while(millis()-w<8000){if(Touch_ReadFrame()){uint32_t r=millis();while(Touch_ReadFrame()&&millis()-r<400)delay(10);break;}delay(20);}
    g_info_showing=true;return;   // v5.6.9: back to the INFO tab, not the games list (caller redraws drawInfoFull)
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
        {String iu=espnowScanInUseBy(i); if(iu.length()>12)iu=iu.substring(0,12); if(iu.length()){gfx_setTextColor(isSel?inkFor(bg):COL_ORANGE,bg);gfx_print("   in use: "+iu);}}   // lab14s
        if(isActive){gfx_setTextColor(COL_GREEN,bg);gfx_setCursor(VW-70,y+13);gfx_print(T(L_ACTIVE));}
      }
      if(n>maxRows){int trackY=listTop,trackH=maxRows*rowH-4,thumbH=trackH*maxRows/n;if(thumbH<10)thumbH=10;
        int thumbY=trackY+(trackH-thumbH)*scanScroll/maxScroll;
        gfx_fillRect(VW-4,trackY,3,trackH,COL_PANEL);gfx_fillRect(VW-4,thumbY,3,thumbH,COL_AMBER);}
      bool selLocked=(n>0)?getDongleLock(espnowScanGetMac(sel)):false;
      int bw=(VW-7*4)/6,bx=4;const char* BL[6]={"USE","RENAME","DEL","SHARE",selLocked?"UNLOCK":"LOCK","BACK"};uint16_t BC[6]={COL_GREEN,COL_ACCENT,(uint16_t)0x8000,COL_ORANGE,COL_AMBER,COL_BAR};   // lab14s: + SHARE
      for(int i=0;i<6;i++){gfx_fillRoundRect(bx,btnBarY+2,bw,34,6,BC[i]);gfx_setTextColor(inkFor(BC[i]),BC[i]);gfx_setTextSize(1);gfx_setCursor(bx+(bw-gfx_textWidth(BL[i]))/2,btnBarY+14);gfx_print(BL[i]);bx+=bw+4;}
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
        if(downY>=btnBarY){int bw=(VW-7*4)/6,i=(downX-4)/(bw+4);
          if(i==0){espnowScanSelect(sel);gfx_fillScreen(COL_BG);gfx_setTextSize(2);gfx_setTextColor(COL_GREEN,COL_BG);{const char*s="PAIRED";gfx_setCursor((VW-gfx_textWidth(s))/2,VH/2-8);}gfx_print(T(L_PAIRED));gfx_flush();delay(700);break;}
          else if(i==1){String mac=espnowScanGetMac(sel);String label="OMEGA-"+mac.substring(12),nm=getDongleName(mac),out;if(onScreenKeyboard(label,nm,out)){setDongleName(mac,out);}uint32_t r=millis();while(Touch_ReadFrame()&&millis()-r<500)delay(10);down=false;dirty=true;}
          else if(i==2){   // DEL — forget this dongle: tell it to drop us (over-air) + clear its name & active link
            uint8_t m[6]; espnowScanMacBytes(sel,m);
            espnowSendUnpair(m); espnowForgetActive(m); setDongleName(espnowScanGetMac(sel),"");
            gfx_fillScreen(COL_BG);gfx_setTextSize(2);gfx_setTextColor((uint16_t)0xE8C4,COL_BG);{const char*s="REMOVED";gfx_setCursor((VW-gfx_textWidth(s))/2,VH/2-8);}gfx_print("REMOVED");gfx_flush();delay(800);break;}
          else if(i==3){   // lab14s SHARE: this dongle accepts ONE more screen for 2 minutes (Webby 1.6.6+; only an owner can ask)
            uint8_t m[6]; espnowScanMacBytes(sel,m); espnowSendShare(m);
            gfx_fillScreen(COL_BG);gfx_setTextSize(2);gfx_setTextColor(COL_ORANGE,COL_BG);{const char*s="SHARED";gfx_setCursor((VW-gfx_textWidth(s))/2,VH/2-24);gfx_print(s);}
            gfx_setTextSize(1);gfx_setTextColor(COL_LIT,COL_BG);{const char*s="Within 2 minutes, on the other screen:";gfx_setCursor((VW-gfx_textWidth(s))/2,VH/2+4);gfx_print(s);}
            {const char*s="SCAN, pick this dongle, USE.";gfx_setCursor((VW-gfx_textWidth(s))/2,VH/2+18);gfx_print(s);}
            gfx_flush();delay(2200);
            uint32_t r=millis();while(Touch_ReadFrame()&&millis()-r<500)delay(10);down=false;dirty=true;}
          else if(i==4){   // LOCK / UNLOCK — Webby security: lock this dongle to this GTi, or open it up
            uint8_t m[6]; espnowScanMacBytes(sel,m); String mac=espnowScanGetMac(sel);
            bool now=!getDongleLock(mac);
            if(now) espnowSendLock(m); else espnowSendUnlock(m);
            setDongleLock(mac,now);
            gfx_fillScreen(COL_BG);gfx_setTextSize(2);gfx_setTextColor(COL_AMBER,COL_BG);{const char*s=now?"LOCKED":"UNLOCKED";gfx_setCursor((VW-gfx_textWidth(s))/2,VH/2-8);gfx_print(s);}gfx_flush();delay(800);
            uint32_t r=millis();while(Touch_ReadFrame()&&millis()-r<500)delay(10);down=false;dirty=true;}
          else break; // BACK (i==5)
        } else if(downY>=listTop&&downY<listTop+maxRows*rowH){int slot=(downY-listTop)/rowH,idx=scanScroll+slot; if(idx>=0&&idx<n&&idx!=sel){sel=idx;dirty=true;}}
      }
    }
    delay(15);
  }
  g_info_showing=true;   // v5.6.9: stay on the INFO tab after pairing (caller redraws drawInfoFull)
}

// Legacy single-pair (kept for compatibility, now routes to scan)
static void doPairNow(){ doScanDongles(); }

// ════════════════════════════════════════════════════════════════════════════
// SETUP
// ════════════════════════════════════════════════════════════════════════════
// ── SD ACCESS boot mode (see the note by onReadSD). Blocking; NEVER returns — reboots. ──
// ── lab14g: SD SOAK TEST (Settings) ───────────────────────────────────────────
// Re-reads a fixed set of sectors (boot, FSINFO, the first FAT sectors, the root directory and
// 40 sectors spread over the card) thousands of times, straight from the SD driver (under the
// guard), and compares every read with the first. Half the time the destination is internal
// RAM, half PSRAM (where FatFs keeps its buffers). Phase 2 repeats it while the other core
// hammers PSRAM - heavy load. Any mismatch is logged with its bit shift. Read-only: it never
// writes to the card. Tells us whether this GTi + card + clock can return a wrong sector.
static volatile bool g_soak_hammer=false;
static void soakHammerTask(void*){
  uint8_t* a=(uint8_t*)ps_malloc(96*1024); uint8_t* b=(uint8_t*)ps_malloc(96*1024);
  while(g_soak_hammer){ if(a&&b){ memcpy(a,b,96*1024); memcpy(b,a,96*1024); } vTaskDelay(1); }   // yield: core 0's idle task is watched
  if(a)free(a); if(b)free(b);
  g_soak_hammer=true; vTaskDelete(NULL);           // (true = "I'm gone", see the wait below)
}
static void sdSoakTest(){
  auto msg=[&](int y,const String& t,uint16_t c,int sz){ gfx_setTextSize(sz); gfx_setTextColor(c,COL_BG); gfx_setCursor((VW-gfx_textWidth(t.c_str()))/2,y); gfx_print(t.c_str()); };
  gfx_fillScreen(COL_BG); msg(18,"SD SOAK TEST",COL_LIT,3);
  if(g_sdg_pdrv==0xFF||!g_sdg.fs){ msg(VH/2,"SD guard not installed - no test",TFT_RED,1); gfx_flush(); delay(2500); return; }
  FATFS* fs=g_sdg.fs;
  std::vector<LBA_t> secs;
  secs.push_back(fs->volbase); secs.push_back(fs->volbase+1);
  for(int i=0;i<8;i++) secs.push_back(fs->fatbase+i);
  LBA_t rootS=fs->database+(LBA_t)(fs->dirbase-2)*fs->csize;
  for(int i=0;i<16;i++) secs.push_back(rootS+i);
  uint64_t span=(uint64_t)(fs->n_fatent-2)*fs->csize;
  for(int i=1;i<=40;i++) secs.push_back(fs->database+(LBA_t)(span*i/41));
  const int N=(int)secs.size();
  uint8_t* ref=(uint8_t*)ps_malloc((size_t)N*512); uint8_t* pb=(uint8_t*)heap_caps_malloc(512,MALLOC_CAP_SPIRAM);
  static uint8_t ib[512] __attribute__((aligned(4)));
  if(!ref||!pb){ if(ref)free(ref); if(pb)free(pb); msg(VH/2,"not enough memory",TFT_RED,1); gfx_flush(); delay(2500); return; }
  int refOdd=0;
  for(int i=0;i<N;i++){ uint8_t* r=ref+(size_t)i*512; g_sdg.lread(r,secs[i],1);
    for(int t=0;t<2;t++){ g_sdg.lread(ib,secs[i],1); if(memcmp(ib,r,512)){ refOdd++; gLog("[soak] reference read of sector %u disagreed (shift %d)\n",(unsigned)secs[i],sg_shift(r,ib)); } } }
  const int ROUNDS=800; uint32_t bad[2][2]={{0,0},{0,0}}, reads[2]={0,0}, rdErr[2]={0,0}; int shown=0;
  uint32_t t0=millis(); bool aborted=false;
  gLog("[soak] start: %d sectors x %d rounds x 2 phases, SD %d kHz, pull-ups %s, reference disagreements %d\n",N,ROUNDS,g_sd_freq,g_sdpullup_cfg?"ON":"OFF",refOdd);
  for(int ph=0;ph<2&&!aborted;ph++){
    if(ph==1){ g_soak_hammer=true; xTaskCreatePinnedToCore(soakHammerTask,"soakh",3072,NULL,1,NULL,0); }
    for(int r=0;r<ROUNDS&&!aborted;r++){
      for(int i=0;i<N;i++){
        bool ps=((r+i)&1); uint8_t* dst=ps?pb:ib;
        if(g_sdg.lread(dst,secs[i],1)!=RES_OK){ rdErr[ph]++; continue; }
        reads[ph]++;
        if(memcmp(dst,ref+(size_t)i*512,512)){ bad[ph][ps]++;
          if(shown<20){ shown++; gLog("[soak] %s: sector %u read into %s differs - shift %d bits, first bytes %02X %02X %02X %02X (ref %02X %02X %02X %02X)\n",
            ph?"under load":"quiet",(unsigned)secs[i],ps?"PSRAM":"internal",sg_shift(ref+(size_t)i*512,dst),dst[0],dst[1],dst[2],dst[3],ref[(size_t)i*512],ref[(size_t)i*512+1],ref[(size_t)i*512+2],ref[(size_t)i*512+3]); } }
      }
      if((r&7)==0){
        gfx_fillRect(0,56,VW,110,COL_BG);
        msg(60,String(ph?"phase 2 of 2: under PSRAM load":"phase 1 of 2: quiet")+"   round "+String(r+1)+"/"+String(ROUNDS),COL_MID,1);
        msg(80,String(reads[0]+reads[1])+" reads, "+String(bad[0][0]+bad[0][1]+bad[1][0]+bad[1][1])+" wrong, "+String(rdErr[0]+rdErr[1])+" read errors",
            (bad[0][0]+bad[0][1]+bad[1][0]+bad[1][1])?TFT_RED:COL_GREEN,1);
        msg(100,String("SD ")+String(g_sd_freq/1000)+" MHz  -  tap to stop",COL_DIM,1);
        gfx_flush();
        uint16_t tx,ty; if(Touch_ReadFrame()&&getTouchXY(&tx,&ty)) aborted=true;
      }
    }
    if(ph==1){ g_soak_hammer=false; uint32_t w=millis(); while(!g_soak_hammer&&millis()-w<500) delay(5); g_soak_hammer=false; }
  }
  free(ref); free(pb);
  uint32_t secsT=(millis()-t0)/1000;
  gLog("[soak] %s in %us | quiet: %u reads, %u wrong (internal %u, PSRAM %u), %u errors | under load: %u reads, %u wrong (internal %u, PSRAM %u), %u errors\n",
       aborted?"stopped":"done",(unsigned)secsT,(unsigned)reads[0],(unsigned)(bad[0][0]+bad[0][1]),(unsigned)bad[0][0],(unsigned)bad[0][1],(unsigned)rdErr[0],
       (unsigned)reads[1],(unsigned)(bad[1][0]+bad[1][1]),(unsigned)bad[1][0],(unsigned)bad[1][1],(unsigned)rdErr[1]);
  gfx_fillRect(0,56,VW,VH-56,COL_BG);
  uint32_t tot=bad[0][0]+bad[0][1]+bad[1][0]+bad[1][1];
  msg(66,tot?"WRONG DATA SEEN":"NO WRONG READS",tot?TFT_RED:COL_GREEN,2);
  msg(96,String("quiet: ")+String(reads[0])+" reads, "+String(bad[0][0]+bad[0][1])+" wrong, "+String(rdErr[0])+" errors",COL_LIT,1);
  msg(112,String("under load: ")+String(reads[1])+" reads, "+String(bad[1][0]+bad[1][1])+" wrong, "+String(rdErr[1])+" errors",COL_LIT,1);
  msg(128,String("SD ")+String(g_sd_freq/1000)+" MHz, pull-ups "+(g_sdpullup_cfg?"on":"off")+", "+String(secsT)+" s - details in GTI/gti.log",COL_DIM,1);
  msg(VH-30,"tap to return",COL_DIM,1); gfx_flush();
  delay(600); uint16_t tx,ty; while(Touch_ReadFrame()&&getTouchXY(&tx,&ty)) delay(30); while(!(Touch_ReadFrame()&&getTouchXY(&tx,&ty))) delay(30); delay(200);
}
static void sdAccessReboot(){
  gfx_fillScreen(COL_BG);gfx_setTextSize(2);gfx_setTextColor(COL_LIT,COL_BG);
  const char*m="RETURNING...";gfx_setCursor((VW-gfx_textWidth(m))/2,VH/2-8);gfx_print(m);gfx_flush();
  g_sdaccess_magic=0;delay(400);ESP.restart();
}
static void runSDAccessBoot(bool sdok){
  g_sdaccess_magic=0;                                   // consume NOW: a hang can't trap us in this mode
  {uint32_t t0=millis();while(Touch_ReadFrame()&&millis()-t0<600)delay(10);}   // drain the entry tap
  if(!sdok){
    gfx_fillScreen(COL_BG);gfx_setTextSize(2);gfx_setTextColor(TFT_RED,COL_BG);
    {const char*e="NO SD CARD";gfx_setCursor((VW-gfx_textWidth(e))/2,VH/2-30);gfx_print(e);}
    gfx_setTextSize(1);gfx_setTextColor(COL_DIM,COL_BG);
    {const char*h="insert a card, then tap to return";gfx_setCursor((VW-gfx_textWidth(h))/2,VH/2+4);gfx_print(h);}
    gfx_flush();
    while(true){uint16_t tx,ty;if(Touch_ReadFrame()&&getTouchXY(&tx,&ty))sdAccessReboot();delay(40);}
  }
  g_sd_sectors=(uint32_t)SD_MMC.numSectors();
  MSC.vendorID("OMEGA");MSC.productID("GTi LIBRARY");MSC.productRevision("1.0");
  MSC.onRead(onReadSD);MSC.onWrite(onWriteSD);MSC.onStartStop(onStartStopSD);
  MSC.mediaPresent(true);MSC.begin(g_sd_sectors,512);USB.begin();               // (no hardDetach — we WANT the PC to see it)
  uint64_t bytes=(uint64_t)g_sd_sectors*512ULL; char sz[24];
  if(bytes>=1000000000ULL)snprintf(sz,sizeof(sz),"%.1f GB",bytes/1e9); else snprintf(sz,sizeof(sz),"%u MB",(unsigned)(bytes/1000000ULL));
  const int NCELL=16, cellW=(VW-40)/NCELL, cellH=10, barY=VH/2+6;
  const int doneW=140, doneH=34, doneX=(VW-doneW)/2, doneY=VH-46;
  bool lastConn=false; int frame=0, pressed=0, rel=0;
  gfx_fillScreen(COL_BG);
  gfx_setTextSize(3);gfx_setTextColor(COL_LIT,COL_BG);{const char*t="SD ACCESS";gfx_setCursor((VW-gfx_textWidth(t))/2,18);gfx_print(t);}
  gfx_setTextSize(1);gfx_setTextColor(COL_DIM,COL_BG);{String c=String("microSD  ")+sz;gfx_setCursor((VW-gfx_textWidth(c))/2,46);gfx_print(c);}
  if(g_cap_magic==CAP_MAGIC){                          // 5.9.41-lab14d: we came here from LIBRARY TOO BIG - say what to fix
    String c1="LIBRARY TOO BIG FOR THIS GTi";
    String c2=String("your card: ")+(g_cap_atleast?"at least ":"")+String(g_cap_games)+" games  -  GTi limit: about "+String(g_cap_fit);   // lab14e: label both numbers
    String c3=String("over by about ")+String(g_cap_games>g_cap_fit?g_cap_games-g_cap_fit:0)+" games: remove some, eject, then tap DONE";
    String c4="details: GTI_CAPACITY.TXT on the card";
    int y=104; gfx_setTextColor((uint16_t)0xF800,COL_BG); gfx_setCursor((VW-gfx_textWidth(c1.c_str()))/2,y); gfx_print(c1.c_str());
    gfx_setTextColor(COL_LIT,COL_BG); for(const String* l: {&c2,&c3,&c4}){ y+=13; gfx_setCursor((VW-gfx_textWidth(l->c_str()))/2,y); gfx_print(l->c_str()); }
  }
  while(true){
    bool conn=(g_sd_rd>0);
    if(conn!=lastConn||frame==0){lastConn=conn;gfx_fillRect(0,62,VW,16,COL_BG);gfx_setTextSize(1);
      gfx_setTextColor(conn?COL_GREEN:COL_AMBER,COL_BG);const char*s=conn?"CONNECTED - drag disks onto the GTi drive":"WAITING FOR PC...";
      gfx_setCursor((VW-gfx_textWidth(s))/2,64);gfx_print(s);}
    gfx_fillRect(0,84,VW,14,COL_BG);gfx_setTextSize(1);gfx_setTextColor(COL_MID,COL_BG);
    {String a="read "+String(g_sd_rd/2)+"K   write "+String(g_sd_wr/2)+"K";gfx_setCursor((VW-gfx_textWidth(a))/2,86);gfx_print(a);}
    int pos=frame%(NCELL*2); if(pos>=NCELL)pos=(NCELL*2-1)-pos;                  // Cylon sweep (a nod to the strobe idea)
    for(int i=0;i<NCELL;i++){int d=abs(i-pos);uint16_t c=(d==0)?(uint16_t)0xF800:(d==1)?(uint16_t)0x8000:(d==2)?(uint16_t)0x3000:COL_PANEL;gfx_fillRoundRect(20+i*cellW,barY,cellW-3,cellH,2,c);}
    gfx_fillRect(0,doneY-16,VW,12,COL_BG);gfx_setTextSize(1);gfx_setTextColor(COL_DIM,COL_BG);
    {const char*h="eject from your PC first, then tap DONE";gfx_setCursor((VW-gfx_textWidth(h))/2,doneY-14);gfx_print(h);}
    gfx_fillRoundRect(doneX,doneY,doneW,doneH,8,COL_GREEN);gfx_setTextSize(2);gfx_setTextColor(TFT_BLACK,COL_GREEN);
    {const char*d="DONE";gfx_setCursor(doneX+(doneW-gfx_textWidth(d))/2,doneY+(doneH-16)/2);gfx_print(d);}
    gfx_flush();
    if(g_sd_eject)sdAccessReboot();
    uint16_t tx=0,ty=0;bool have=Touch_ReadFrame()&&getTouchXY(&tx,&ty);
    if(have){rel=0;if(!pressed){pressed=1;if(tx>=(uint16_t)doneX&&tx<(uint16_t)(doneX+doneW)&&ty>=(uint16_t)doneY&&ty<(uint16_t)(doneY+doneH))sdAccessReboot();}}
    else{if(pressed&&++rel>=3)pressed=0;}
    frame++;delay(45);
  }
}

// ── FIRMWARE UPDATE via SD (v5.3) ────────────────────────────────────────────
// Reads /GTi_update.bin off the SD and self-flashes the INACTIVE OTA slot via the
// Update library. The image is fully hash-verified before the boot pointer is
// flipped, so a corrupt/half-copied file (or a power cut mid-write) can't take
// over — the running firmware is untouched and simply boots again. Needs an A/B
// OTA partition table (Tools > Partition Scheme > a "2x…APP" 16M scheme); a
// single-slot "No OTA" build has no spare slot and says so instead of failing ugly.
#define FWUP_PATH "/GTi_update.bin"
#define FWUP_TAG  "P4G7"   // P4G7: own tag (GTi-P4G7-*.bin) so the 4.3" P4, the round one and the 7" never take each other's update | SD update auto-detects any GTi-P4-*.bin (was "JC35", which collided with the JC3248 build)
static void fwupMsg(int y,const char*s,uint16_t fg,uint16_t bg,int sz){gfx_setTextSize(sz);gfx_setTextColor(fg,bg);gfx_setCursor((VW-gfx_textWidth(s))/2,y);gfx_print(s);}
// 5.9.29: the old one-liner waited for "a touch" but never for a RELEASE first. The tap
// that got us here (plus the AXS15231B's stale idle frames) satisfied it instantly, so every
// UPDATE FAILED message painted and vanished in ~200ms - which is why the real reason has
// been invisible this whole time. Now: hold the screen readable, drain, then need a FRESH press.
static void fwupWait(){
  uint16_t tx,ty; gfx_flush();
  uint32_t t0=millis();
  while(millis()-t0<1500){ Touch_ReadFrame(); delay(30); }          // minimum readable time + drain stale frames
  while(Touch_ReadFrame()&&getTouchXY(&tx,&ty)) delay(30);          // wait for release
  while(!(Touch_ReadFrame()&&getTouchXY(&tx,&ty))) delay(30);       // fresh press
  delay(200);
}
// ── 5.9.29 FW-UPDATE DIAGNOSTICS ────────────────────────────────────────────
// The single USB-C is the TinyUSB MSC floppy, so there is NO serial console while the
// sketch runs - every OTA failure reason was being generated and thrown away. It all goes
// to /gti.log now. IDF lines (esp_image / esp_ota_ops carry the REAL verify reason) are
// captured into RAM during the OTA window and flushed afterwards - never written from
// inside the log callback itself, which would recurse if the SD driver logged.
// NOTE: needs Core Debug Level >= Error, or the IDF's ESP_LOGE calls aren't compiled in.
static char g_fwup_idf[2048]; static size_t g_fwup_idf_n=0;
static vprintf_like_t g_fwup_idf_prev=NULL;
static int g_fwup_cands=0;
static int fwupIdfCapture(const char*fmt, va_list ap){
  char b[192]; int n=vsnprintf(b,sizeof b,fmt,ap);
  size_t len=strlen(b);
  if(len && g_fwup_idf_n+len < sizeof(g_fwup_idf)-1){ memcpy(g_fwup_idf+g_fwup_idf_n,b,len); g_fwup_idf_n+=len; g_fwup_idf[g_fwup_idf_n]=0; }
  return n;
}
static void fwupIdfBegin(){ g_fwup_idf_n=0; g_fwup_idf[0]=0; g_fwup_idf_prev=esp_log_set_vprintf(fwupIdfCapture); }
static void fwupIdfEnd(){
  if(g_fwup_idf_prev){ esp_log_set_vprintf(g_fwup_idf_prev); g_fwup_idf_prev=NULL; }
  if(g_fwup_idf_n && g_log_enabled){
    File lf=SD_MMC.open(GTI_LOG_PATH,FILE_APPEND);
    if(lf){ lf.print("[fwup] --- IDF log ---\n"); lf.print(g_fwup_idf); lf.print("[fwup] --- end IDF ---\n"); lf.close(); }
  }
  g_fwup_idf_n=0;
}
// Board-ID guard. Arduino stamps EVERY ESP32 sketch with the same esp_app_desc
// project_name ("arduino-lib-builder"), so that field can't tell boards apart. Instead
// every JC build carries this unique marker in its .rodata (it's referenced below, so the
// linker always keeps it); a SuperMini/XIAO bin doesn't, so scanning the incoming image
// for it reliably refuses a cross-board flash. Marker spans chunk boundaries safely.
static const char GTI_FW_MARK[]="OMEGAWARE.GTi.P4G7.fw";   // P4G7: differs from the 4.3" P4 / round markers
// 5.9.30: a scannable version stamp. Arduino's esp_app_desc.version is the CORE's git hash
// ("ee57070"), not ours, so it can't tell you which GTi build a .bin is. This literal can be
// found in any 5.9.30+ image, letting the confirm screen show the INCOMING version before you
// commit to flashing it. (Referenced in doFirmwareUpdate so the linker keeps it.)
static const char GTI_FW_VERTAG[]="GTiFWVER=" FW_VERSION;
static bool fwupFindVer(File&f,char*out,size_t outsz){
  const char*pre="GTiFWVER="; size_t pl=strlen(pre);
  static uint8_t buf[4096]; uint8_t tail[64]; size_t tlen=0;
  out[0]=0; f.seek(0);
  while(true){
    memcpy(buf,tail,tlen);
    int n=f.read(buf+tlen,sizeof buf-tlen);
    if(n<=0)break;
    size_t total=tlen+(size_t)n;
    for(size_t i=0;i+pl<total;i++){
      if(memcmp(buf+i,pre,pl)==0){
        size_t j=i+pl,k=0;
        while(j<total&&k<outsz-1&&buf[j]>=32&&buf[j]<127) out[k++]=(char)buf[j++];
        out[k]=0; f.seek(0); return k>0;
      }
    }
    tlen=(total>=64)?64:total; memcpy(tail,buf+total-tlen,tlen);
  }
  f.seek(0); return false;
}
static bool fwupHasMarker(File&f,const char*mark){
  size_t ml=strlen(mark);if(ml==0||ml>32)return false;
  static uint8_t buf[4096];uint8_t tail[32];size_t tlen=0;
  f.seek(0);
  while(true){
    memcpy(buf,tail,tlen);
    int n=f.read(buf+tlen,sizeof buf-tlen);
    if(n<=0)break;
    size_t total=tlen+(size_t)n;
    for(size_t i=0;i+ml<=total;i++)if(memcmp(buf+i,mark,ml)==0){f.seek(0);return true;}
    tlen=(total>=ml-1)?ml-1:total;memcpy(tail,buf+total-tlen,tlen);}
  f.seek(0);return false;
}
// v5.5.3: locate the SD update image without forcing a rename. Pass 1 = any *.bin
// whose NAME carries this board tag (FWUP_TAG). Pass 2 = any *.bin whose CONTENTS carry
// our board marker (name-independent safety net). Else the legacy /GTi_update.bin.
static String fwupFindFile(){
  // 5.9.29: now scans the whole root so EVERY candidate is logged. Selection is deliberately
  // UNCHANGED (first match in directory order still wins) so this build debugs the same
  // behaviour you've been hitting - it just tells you what else was sitting there.
  String first=""; g_fwup_cands=0;
  for(int pass=0;pass<2;pass++){
    File root=SD_MMC.open("/"); if(!root)break;
    File e;
    while((e=root.openNextFile())){
      if(!e.isDirectory()){
        String nm=e.name(); String up=nm; up.toUpperCase();
        int sl=up.lastIndexOf(0x2F); String leaf=(sl>=0)?up.substring(sl+1):up;
        if(leaf.endsWith(".BIN")){
          bool hit=(pass==0)?(leaf.indexOf(FWUP_TAG)>=0):fwupHasMarker(e,GTI_FW_MARK);
          if(hit){ String pp=nm; if(!pp.startsWith("/"))pp=String("/")+pp;
            g_fwup_cands++;
            gLog("[fwup] candidate %d (pass %d) %s  %u bytes\n",g_fwup_cands,pass,pp.c_str(),(unsigned)e.size());
            if(!first.length()) first=pp;
          }
        }
      }
      e.close();
    }
    root.close();
    if(first.length())break;              // pass 2 only runs if pass 1 found nothing (unchanged)
  }
  if(first.length()){
    if(g_fwup_cands>1) gLog("[fwup] WARNING: %d candidates in root - using FIRST in directory order\n",g_fwup_cands);
    return first;
  }
  if(SD_MMC.exists(FWUP_PATH)){ gLog("[fwup] using legacy %s\n",FWUP_PATH); return String(FWUP_PATH); }
  gLog("[fwup] NO candidate .bin in SD ROOT (root is not searched recursively)\n");
  return String();
}
static void doFirmwareUpdate(){
  gfx_fillScreen(COL_BG);
  gLog("[fwup] ===== FW UPDATE requested, running %s =====\n",FW_VERSION);
  String fpath=fwupFindFile();
  File f; if(fpath.length())f=SD_MMC.open(fpath,FILE_READ);
  if(!f||f.isDirectory()){
    if(f)f.close();
    fwupMsg(VH/2-24,"NO UPDATE FILE",COL_ORANGE,COL_BG,2);
    fwupMsg(VH/2+2,"Drop a GTi-" FWUP_TAG "-*.bin on the SD",COL_DIM,COL_BG,1);
    fwupMsg(VH/2+16,"(no rename needed), then try again.",COL_DIM,COL_BG,1);
    fwupMsg(VH-22,"tap to return",COL_MID,COL_BG,1);fwupWait();return;}
  size_t fsz=f.size();
  uint8_t h0=0;f.read(&h0,1);f.seek(0);bool isImg=(h0==0xE9);   // ESP image magic
  bool idOK=isImg&&fwupHasMarker(f,GTI_FW_MARK);                // JC builds carry GTI_FW_MARK in .rodata
  gLog("[fwup] chosen=%s size=%u magic=%s marker=%s\n",fpath.c_str(),(unsigned)fsz,isImg?"E9-ok":"BAD",idOK?"ok":"MISSING");
  // 5.9.30: work out what this file ACTUALLY is before offering to flash it.
  // APP image    -> esp_app_desc_t sits at 0x20 (magic 0xABCD5432), version string at 0x30.
  // MERGED image -> starts with the BOOTLOADER and carries the partition table (AA 50) at 0x8000.
  // Writing a merged image into an OTA slot puts a bootloader where an app belongs: it writes
  // perfectly, then esp_ota_set_boot_partition parses the bootloader header as an app header
  // and refuses with a nonsense chip/efuse-revision complaint (err=9). Caught up front now.
  bool isApp=false,isMerged=false; char imgVer[33]={0};
  { uint8_t b4[4];
    f.seek(0x20);
    if(f.read(b4,4)==4){ uint32_t m=(uint32_t)b4[0]|((uint32_t)b4[1]<<8)|((uint32_t)b4[2]<<16)|((uint32_t)b4[3]<<24); isApp=(m==0xABCD5432u); }
    if(isApp) fwupFindVer(f,imgVer,sizeof imgVer);   // our stamp, not the core's git hash
    if(fsz>0x8100){ uint8_t p2[2]; f.seek(0x8000); if(f.read(p2,2)==2) isMerged=(p2[0]==0xAA&&p2[1]==0x50); }
    f.seek(0); }
  gLog("[fwup] kind: app=%d merged=%d imgver='%s' (this build stamps %s)\n",(int)isApp,(int)isMerged,imgVer,GTI_FW_VERTAG);
  if(isMerged||!isApp){
    f.close();
    gLog("[fwup] REFUSED: not a plain app image (merged/full-flash image)\n");
    gfx_fillScreen(COL_BG);
    fwupMsg(VH/2-46,isMerged?"MERGED IMAGE":"NOT AN APP IMAGE",COL_ORANGE,COL_BG,2);
    fwupMsg(VH/2-18,"This file starts with a bootloader,",COL_DIM,COL_BG,1);
    fwupMsg(VH/2-4,"so it cannot go in an OTA slot.",COL_DIM,COL_BG,1);
    fwupMsg(VH/2+14,"Use the APP image instead:",COL_DIM,COL_BG,1);
    fwupMsg(VH/2+28,"GTi-" FWUP_TAG "-<ver>-update.bin",COL_LIT,COL_BG,1);
    fwupMsg(VH/2+42,"(merged .bin is for the web flasher only)",COL_DIM,COL_BG,1);
    fwupMsg(VH-22,"tap to return",COL_MID,COL_BG,1);fwupWait();return;}
  { const esp_partition_t*run=esp_ota_get_running_partition();
    const esp_partition_t*nxt=esp_ota_get_next_update_partition(NULL);
    gLog("[fwup] running slot %s @0x%06X size=0x%06X\n", run?run->label:"?", run?(unsigned)run->address:0u, run?(unsigned)run->size:0u);
    gLog("[fwup] target  slot %s @0x%06X size=0x%06X\n", nxt?nxt->label:"NONE", nxt?(unsigned)nxt->address:0u, nxt?(unsigned)nxt->size:0u); }
  if(esp_ota_get_next_update_partition(NULL)==NULL){     // single-slot build: no spare OTA slot
    f.close();
    fwupMsg(VH/2-30,"SD UPDATE NOT ENABLED",COL_ORANGE,COL_BG,2);
    fwupMsg(VH/2-4,"This unit needs ONE more USB flash",COL_DIM,COL_BG,1);
    fwupMsg(VH/2+10,"(web flasher) to add the OTA layout.",COL_DIM,COL_BG,1);
    fwupMsg(VH/2+24,"After that, SD updates work forever.",COL_DIM,COL_BG,1);
    fwupMsg(VH-22,"tap to return",COL_MID,COL_BG,1);fwupWait();return;}
  // ── confirm ──
  gfx_fillScreen(COL_BG);
  fwupMsg(24,"FIRMWARE UPDATE",COL_LIT,COL_BG,2);
  {String leaf=fpath;int sl=leaf.lastIndexOf(0x2F);if(sl>=0)leaf=leaf.substring(sl+1);fwupMsg(40,leaf.c_str(),COL_MID,COL_BG,1);}
  {char l[80];snprintf(l,sizeof l,"File: %u KB   %s",(unsigned)(fsz/1024),imgVer[0]?imgVer:"(version unknown - pre-5.9.30 build)");fwupMsg(56,l,imgVer[0]?COL_GREEN:COL_DIM,COL_BG,1);}   // 5.9.30: show the INCOMING version, not just the running one
  fwupMsg(72,idOK?"Image: GTi-JC firmware  [OK]":(isImg?"Image: unrecognised (not GTi-JC)":"Image: not a firmware .bin"),idOK?COL_GREEN:COL_ORANGE,COL_BG,1);
  {char l[64];snprintf(l,sizeof l,"Now running: %s",FW_VERSION);fwupMsg(88,l,COL_DIM,COL_BG,1);}
  if(!idOK)fwupMsg(106,"! flash only a GTi-JC .bin here",COL_ORANGE,COL_BG,1);
  int bw=124,bbh=42,gap=22,by=VH-64,cx=VW/2-bw-gap/2,ox=VW/2+gap/2;
  gfx_fillRoundRect(cx,by,bw,bbh,8,COL_BAR);gfx_setTextColor(COL_LIT,COL_BAR);gfx_setTextSize(2);gfx_setCursor(cx+(bw-gfx_textWidth("CANCEL"))/2,by+13);gfx_print(T(L_CANCEL));
  {uint16_t okc=idOK?COL_GREEN:COL_ORANGE;const char*okl=idOK?"FLASH":"FLASH ANYWAY";int osz=idOK?2:1;
   gfx_fillRoundRect(ox,by,bw,bbh,8,okc);gfx_setTextColor(TFT_BLACK,okc);gfx_setTextSize(osz);gfx_setCursor(ox+(bw-gfx_textWidth(okl))/2,by+(idOK?13:17));gfx_print(okl);}
  gfx_flush();
  bool go=false;
  while(true){uint16_t tx,ty;if(Touch_ReadFrame()&&getTouchXY(&tx,&ty)){
    if(ty>=(uint16_t)(by-8)&&ty<(uint16_t)(by+bbh+8)){
      if(tx>=(uint16_t)(cx-8)&&tx<(uint16_t)(cx+bw+8)){go=false;break;}
      if(tx>=(uint16_t)(ox-8)&&tx<(uint16_t)(ox+bw+8)){go=true;break;}}}
    delay(25);}
  if(!go){f.close();return;}
  // ── flash ──
  gfx_fillScreen(COL_BG);fwupMsg(VH/2-46,"FLASHING - DO NOT UNPLUG",COL_AMBER,COL_BG,2);gfx_flush();
  fwupIdfBegin();                                   // 5.9.29: capture IDF esp_image/esp_ota complaints
  if(!Update.begin(fsz,U_FLASH)){
    gLog("[fwup] begin FAILED err=%d %s\n",(int)Update.getError(),Update.errorString());
    fwupIdfEnd();
    f.close();gfx_fillScreen(COL_BG);fwupMsg(VH/2-8,"UPDATE FAILED",COL_ORANGE,COL_BG,2);fwupMsg(VH/2+16,Update.errorString(),COL_DIM,COL_BG,1);fwupMsg(VH-22,"tap to return",COL_MID,COL_BG,1);fwupWait();return;}
  gLog("[fwup] begin ok, writing %u bytes\n",(unsigned)fsz);
  int pbx=30,pbw=VW-60,pby=VH/2,pbh=22;gfx_drawRoundRect(pbx,pby,pbw,pbh,5,COL_SEP);
  static uint8_t buf[4096];size_t wrote=0;bool err=false;int since=0;
  int errKind=0;                                    // 1=SD read short, 2=flash write short
  while(wrote<fsz){
    int n=f.read(buf,sizeof buf);
    if(n<=0){err=true;errKind=1;gLog("[fwup] SD READ failed at %u/%u (n=%d)\n",(unsigned)wrote,(unsigned)fsz,n);break;}
    size_t w=Update.write(buf,n);
    if(w!=(size_t)n){err=true;errKind=2;gLog("[fwup] FLASH WRITE short at %u/%u (asked %d got %u) err=%d %s\n",(unsigned)wrote,(unsigned)fsz,n,(unsigned)w,(int)Update.getError(),Update.errorString());break;}
    wrote+=n;
    if(++since>=8||wrote>=fsz){since=0;
      int fillw=(int)((uint64_t)(pbw-4)*wrote/fsz);gfx_fillRect(pbx+2,pby+2,fillw,pbh-4,COL_GREEN);
      char pc[12];snprintf(pc,sizeof pc,"%u%%",(unsigned)(100ULL*wrote/fsz));gfx_fillRect(0,pby+pbh+10,VW,14,COL_BG);fwupMsg(pby+pbh+10,pc,COL_LIT,COL_BG,1);gfx_flush();}}
  f.close();
  gLog("[fwup] write loop done: wrote=%u/%u err=%d\n",(unsigned)wrote,(unsigned)fsz,errKind);
  bool endOK = (!err) && Update.end(true);
  if(!endOK){
    int ec=(int)Update.getError(); const char*es=Update.errorString();
    gLog("[fwup] END FAILED errKind=%d updErr=%d %s\n",errKind,ec,es);
    fwupIdfEnd();                                   // flush the IDF reason to /gti.log
    Update.abort();gfx_fillScreen(COL_BG);fwupMsg(VH/2-14,"UPDATE FAILED",COL_ORANGE,COL_BG,2);
    fwupMsg(VH/2+12,err?(errKind==1?"SD read error - image unchanged":"flash write error - image unchanged"):es,COL_DIM,COL_BG,1);
    {char l[64];snprintf(l,sizeof l,"err=%d  see GTI/gti.log",ec);fwupMsg(VH/2+26,l,COL_DIM,COL_BG,1);}
    fwupMsg(VH/2+40,"current firmware kept.",COL_DIM,COL_BG,1);fwupMsg(VH-22,"tap to return",COL_MID,COL_BG,1);fwupWait();return;}
  fwupIdfEnd();
  gLog("[fwup] end ok - activating new image, rebooting\n");
  SD_MMC.rename(fpath.c_str(),(fpath+".installed").c_str());   // best-effort: don't re-offer the same file
  gfx_fillScreen(COL_BG);fwupMsg(VH/2-8,"UPDATE OK - REBOOTING",COL_GREEN,COL_BG,2);gfx_flush();delay(900);ESP.restart();
}

// ════════════════════════════════════════════════════════════════════════════
// 5.9.38 — exFAT / NTFS CARD DETECTION (lab14h: detection only - the GTi never formats)
// ────────────────────────────────────────────────────────────────────────────
// Windows formats anything over 32 GB as exFAT (or NTFS) and will not offer
// FAT32 — that is a limit of the Windows dialog, not of FAT32, which goes to
// 2 TB. The ESP32 cannot read exFAT (the Arduino core's FatFs is built without
// it), so such a card simply fails to mount. FlashFloppy has the same rule for
// its USB stick ("FAT32 — exFAT and NTFS are not supported").
//
// If the mount fails AND sector 0 positively identifies exFAT or NTFS, the GTi says so and
// explains how to format the card on a computer. lab14h: it never formats or erases a card
// itself (5.9.38 used to offer a FatFs quick format here - removed).
// ════════════════════════════════════════════════════════════════════════
// Returns 1 = exFAT, 2 = NTFS, 0 = something else (FAT or unknown), -1 = no card.
// Only ever called AFTER SD_MMC.begin() has failed and released the host.
static int sdPeekForeignFs(){
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.flags = SDMMC_HOST_FLAG_1BIT;
  host.max_freq_khz = 20000;
  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.width = 1;
  slot.clk=(gpio_num_t)SD_CLK; slot.cmd=(gpio_num_t)SD_CMD; slot.d0=(gpio_num_t)SD_D0;
  slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
  if(sdmmc_host_init()!=ESP_OK) return -1;
  if(sdmmc_host_init_slot(SDMMC_HOST_SLOT_1,&slot)!=ESP_OK){ sdmmc_host_deinit(); return -1; }
  sdmmc_card_t card; int r=-1;
  if(sdmmc_card_init(&host,&card)==ESP_OK){
    uint8_t* b=(uint8_t*)heap_caps_malloc(512,MALLOC_CAP_DMA);
    if(b && sdmmc_read_sectors(&card,b,0,1)==ESP_OK){
      r=0;
      // superfloppy layouts put the boot sector at 0 ...
      if(!memcmp(b+3,"EXFAT   ",8)) r=1;
      else if(!memcmp(b+3,"NTFS    ",8)) r=2;
      // ... but Windows puts an MBR at 0 and the volume in partition 1.
      else if(b[510]==0x55&&b[511]==0xAA){
        uint8_t ptype=b[446+4]; uint32_t plba=rd32(b,446+8);
        if((ptype==0x07||ptype==0x0F||ptype==0x0B||ptype==0x0C) && plba && plba<0x0FFFFFFF
           && sdmmc_read_sectors(&card,b,plba,1)==ESP_OK){
          if(!memcmp(b+3,"EXFAT   ",8)) r=1;
          else if(!memcmp(b+3,"NTFS    ",8)) r=2;
        }
      }
    }
    if(b) free(b);
  }
  sdmmc_host_deinit();
  return r;
}
// lab14h: THE GTi NEVER FORMATS OR ERASES A CARD (owner's rule, 25 Sep 2026). 5.9.38 offered
// an on-device FAT32 format behind two confirmations; it is gone. The GTi only says what the
// card is and how to fix it on a computer - and changes nothing.
static void sdForeignFsNotice(int kind){
  const char* fsn = (kind==2) ? "NTFS" : "exFAT";
  gfx_fillScreen(COL_BG);
  fwupMsg(22,"CARD NOT COMPATIBLE",COL_ORANGE,COL_BG,2);
  {char l[64];snprintf(l,sizeof l,"This card is formatted %s.",fsn);fwupMsg(56,l,COL_LIT,COL_BG,1);}
  fwupMsg(70,"The GTi and the Gotek need FAT32.",COL_LIT,COL_BG,1);
  fwupMsg(94,"Format it as FAT32 on a computer:",COL_LIT,COL_BG,1);
  fwupMsg(110,"Mac: Disk Utility > Erase > MS-DOS (FAT)",COL_DIM,COL_BG,1);
  fwupMsg(124,"Windows, 32 GB or less: Format > FAT32",COL_DIM,COL_BG,1);
  fwupMsg(138,"Windows, over 32 GB: a FAT32 tool such as guiformat",COL_DIM,COL_BG,1);
  fwupMsg(162,"Nothing on the card was changed.",COL_GREEN,COL_BG,1);
  fwupMsg(VH-22,"tap to continue",COL_MID,COL_BG,1);
  gfx_flush(); fwupWait();
}

// lab15d: the GTi has no clock, so every file it wrote (saves, gti.log, caches, tiles) got an invalid
// date - blank "Date modified" on a PC. Start the clock at this firmware's build time instead: dates are
// plausible and keep counting up while it runs (each boot starts again from the build time). Tile
// freshness does not depend on it (tiles take their cover's date via f_utime).
static void clockFromBuild(){
  if(time(nullptr)>1600000000) return;              // something already set a real time
  static const char M[]="JanFebMarAprMayJunJulAugSepOctNovDec";
  char mon[4]={0}; int d=1,y=2026,H=0,Mi=0,S=0;
  sscanf(__DATE__,"%3s %d %d",mon,&d,&y); sscanf(__TIME__,"%d:%d:%d",&H,&Mi,&S);
  const char* p=strstr(M,mon); struct tm t={}; t.tm_year=y-1900; t.tm_mon=p?(int)((p-M)/3):0; t.tm_mday=d; t.tm_hour=H; t.tm_min=Mi; t.tm_sec=S;
  time_t e=mktime(&t); if(e<=0) return;
  struct timeval tv={e,0}; settimeofday(&tv,nullptr);
}
// ════════════════════════════════════════════════════════════════════════════
// C6 (WiFi co-processor) SELF-UPDATE  (P4 only)
// The P4 has no radio of its own; WiFi/ESP-NOW live on the ESP32-C6 reached over
// SDIO (esp-hosted). The factory C6 ships esp-hosted ~2.3.x, far behind this
// Arduino core's 2.12.x host -> WiFi doesn't work. If a matching slave image is
// present on the SD card and the C6 is behind, offer to flash it via the
// esp-hosted slave-OTA API. Tap-gated. Absent file => skipped entirely.
// OTA writes the INACTIVE C6 slot and only switches on activate(), so an
// interrupted flash leaves the old C6 firmware intact (no brick).
// ════════════════════════════════════════════════════════════════════════════
static void c6SelfUpdate(){
  const char* C6BIN = "/c6_network_adapter_2.12.13.bin";

  WiFi.mode(WIFI_STA); delay(150);                          // spin up the hosted link to reach the C6
  esp_hosted_coprocessor_fwver_t v; memset(&v,0,sizeof v);
  bool haveVer = (esp_hosted_get_coprocessor_fwversion(&v)==ESP_OK);
  // Skip only if the C6 is already at 2.12.13 or newer; anything lower is offered the update.
  auto c6AtLeast=[&](int a,int b,int c){ if(v.major1!=a)return v.major1>a; if(v.minor1!=b)return v.minor1>b; return v.patch1>=c; };
  if(haveVer) snprintf(g_c6_ver,sizeof g_c6_ver,"C6 v%u.%u.%u",(unsigned)v.major1,(unsigned)v.minor1,(unsigned)v.patch1); else snprintf(g_c6_ver,sizeof g_c6_ver,"C6 version unknown");
  gLog("[c6] co-processor firmware %s (%s)\n", haveVer?g_c6_ver+3:"unknown - no answer", (haveVer&&c6AtLeast(2,12,13))?"OK, >= 2.12.13":"older than 2.12.13 - update offered if an image is present");   // lab2-P4G7
  if(haveVer && c6AtLeast(2,12,13)){ g_c6_ready=true; return; }   // 5.9.12: radio always-on; leave STA up so espnowBegin never re-inits the hosted radio (that OFF->STA re-init crashed)

  // Image source: an SD override wins (drop a newer c6_network_adapter_*.bin on the card),
  // otherwise the embedded 'c6fw' flash partition baked in at build time. Header = "C6FW" + u32 LE length.
  bool useSD = SD_MMC.exists(C6BIN);
  const esp_partition_t* c6part=NULL; size_t c6off=0, c6len=0;
  if(!useSD){
    c6part=esp_partition_find_first(ESP_PARTITION_TYPE_ANY,ESP_PARTITION_SUBTYPE_ANY,"c6fw");
    if(c6part){ uint8_t h[8];
      if(esp_partition_read(c6part,0,h,8)==ESP_OK && h[0]=='C'&&h[1]=='6'&&h[2]=='F'&&h[3]=='W'){
        c6len=(uint32_t)h[4]|((uint32_t)h[5]<<8)|((uint32_t)h[6]<<16)|((uint32_t)h[7]<<24); c6off=8;
      }
    }
  }
  if(!useSD && c6len==0){ gLog("[c6] no update image (SD %s missing, no embedded c6fw) - wireless stays off\n",C6BIN); WiFi.mode(WIFI_OFF); return; }     // no SD image, no embedded image -> normal boot

  auto msg=[&](const char*l1,const char*l2,uint16_t col){
    gfx_fillScreen(COL_BG);
    gfx_setTextSize(2); gfx_setTextColor(col,COL_BG);
    gfx_setCursor((VW-gfx_textWidth(l1))/2, VH/2-20); gfx_print(l1);
    if(l2&&l2[0]){ gfx_setTextSize(1); gfx_setTextColor(COL_LIT,COL_BG);
      gfx_setCursor((VW-gfx_textWidth(l2))/2, VH/2+8); gfx_print(l2); }
    gfx_flush();
  };

  char sub[72]; snprintf(sub,sizeof sub,"C6 is v%u.%u.%u  ->  update to 2.12.13  (enables WiFi)", (unsigned)v.major1, (unsigned)v.minor1, (unsigned)v.patch1);
  gfx_fillScreen(COL_BG);
  gfx_setTextSize(2); gfx_setTextColor(COL_AMBER,COL_BG);
  { const char*t="WiFi CO-PROCESSOR UPDATE"; gfx_setCursor((VW-gfx_textWidth(t))/2,VH/2-70); gfx_print(t); }
  gfx_setTextSize(1); gfx_setTextColor(COL_LIT,COL_BG);
  { gfx_setCursor((VW-gfx_textWidth(sub))/2,VH/2-34); gfx_print(sub); }
  { const char*t="TAP the screen to update   -   wait 25s to skip"; gfx_setTextColor(COL_ACCENT,COL_BG); gfx_setCursor((VW-gfx_textWidth(t))/2,VH/2+2); gfx_print(t); }
  gfx_flush();

  { uint32_t d0=millis(); while(Touch_ReadFrame()&&millis()-d0<600) delay(10); }
  bool go=false; uint32_t t0=millis();
  while(millis()-t0<25000){ uint16_t tx,ty; if(Touch_ReadFrame()&&getTouchXY(&tx,&ty)){ go=true; break; } delay(20); }
  if(!go){ WiFi.mode(WIFI_OFF); return; }

  File fw; size_t total, done=0;
  if(useSD){ fw=SD_MMC.open(C6BIN,"r"); if(!fw){ msg("SD read failed","",TFT_RED); delay(2500); WiFi.mode(WIFI_OFF); return; } total=fw.size(); }
  else total=c6len;

  if(esp_hosted_slave_ota_begin()!=ESP_OK){ if(useSD)fw.close(); msg("C6 OTA begin failed","power-cycle & retry",TFT_RED); delay(3000); WiFi.mode(WIFI_OFF); return; }
  static uint8_t buf[1400]; bool ok=true; uint32_t last=0;
  while(done<total){
    size_t want=total-done; if(want>sizeof buf) want=sizeof buf;
    int n;
    if(useSD) n=fw.read(buf,want);
    else n=(esp_partition_read(c6part,c6off+done,buf,want)==ESP_OK)?(int)want:-1;
    if(n<=0) break;
    if(esp_hosted_slave_ota_write(buf,(size_t)n)!=ESP_OK){ ok=false; break; }
    done+=n;
    if(millis()-last>120){ last=millis();
      gfx_fillScreen(COL_BG);
      gfx_setTextSize(2); gfx_setTextColor(COL_AMBER,COL_BG);
      { const char*t="UPDATING C6 - DO NOT POWER OFF"; gfx_setCursor((VW-gfx_textWidth(t))/2,VH/2-40); gfx_print(t); }
      int bw=VW-160,bx=80,by=VH/2; gfx_drawRect(bx,by,bw,16,COL_LIT);
      gfx_fillRect(bx+2,by+2,(int)((long)(bw-4)*done/(total?total:1)),12,COL_SEL);
      char pc[16]; snprintf(pc,sizeof pc,"%u%%",(unsigned)(done*100/(total?total:1)));
      gfx_setTextSize(1); gfx_setTextColor(COL_LIT,COL_BG); gfx_setCursor((VW-gfx_textWidth(pc))/2,by+24); gfx_print(pc);
      gfx_flush();
    }
    yield();
  }
  if(useSD) fw.close();
  esp_hosted_slave_ota_end();
  if(!ok){ msg("C6 OTA write failed","power-cycle & retry",TFT_RED); delay(3000); ESP.restart(); }
  msg("C6 updated - activating","rebooting...",COL_ACCENT);
  esp_hosted_slave_ota_activate();
  delay(3000);
  ESP.restart();
}

void setup(){
  Serial.begin(115200);delay(200);
  clockFromBuild();   // lab15d
  if(!g_sdlock) g_sdlock=xSemaphoreCreateMutex();   // lab15e: before the SD card or USB start
  // v5.1: SD-access is requested only when our NOINIT flag survived a *software* restart
  // (cold power-on => reset reason POWERON => never a false trigger from RTC garbage).
  bool sdAccessReq=(g_sdaccess_magic==SDACCESS_MAGIC && esp_reset_reason()==ESP_RST_SW);
  if(g_bootMagic!=0xB007C047u){g_bootMagic=0xB007C047u;g_bootCount=1;}else{g_bootCount++;}   // boot-forensics counter
  { // lab14e: read what the LAST boot left in RTC memory before anything overwrites it
    esp_reset_reason_t rr=esp_reset_reason();
    g_bc_prev.reason=(int)rr;
    g_bc_prev.crashed=(rr==ESP_RST_PANIC||rr==ESP_RST_INT_WDT||rr==ESP_RST_TASK_WDT||rr==ESP_RST_WDT);
    g_bc_prev.valid=(g_bc_magic==BC_MAGIC);
    if(g_bc_prev.valid){ g_bc_prev.stage=g_bc_stage; g_bc_prev.n=g_bc_n; g_bc_prev.psram=g_bc_psram; g_bc_prev.intr=g_bc_int;
                         g_bc_prev.failsz=g_bc_failsz; g_bc_prev.failcaps=g_bc_failcaps; }
    g_bc_magic=BC_MAGIC; g_bc_stage=BC_NONE; g_bc_n=0; g_bc_failsz=0; g_bc_failcaps=0;
    heap_caps_register_failed_alloc_callback(bcAllocFailed);
  }
  Serial.printf("[BOOT] rst=%d magic=%08X sdAccess=%d\n",(int)esp_reset_reason(),(unsigned)g_sdaccess_magic,(int)sdAccessReq);
  applyTheme(0);displayInit();touchInit();
  gfx_fillScreen(TFT_BLACK);gfx_flush();
  // 5.9.35: the RAM disk is allocated AFTER the config is read (see below) so
  // BIGDISK= can size it. Nothing between here and there touches g_disk.
  delay(100);
  bool sdok=sdMountTry(20000);if(!sdok){SD_MMC.end();delay(200);sdok=sdMountTry(20000);}   // P4G7: 4-bit first, 1-bit fallback
  if(!sdok && !sdAccessReq){                          // 5.9.38: is it an exFAT/NTFS card rather than no card?
    int fk=sdPeekForeignFs();
    if(fk==1||fk==2){
      Serial.printf("[sd] card present but %s - needs FAT32 (the GTi does not format cards)\n",fk==2?"NTFS":"exFAT");
      sdForeignFsNotice(fk);                           // lab14h: explain, change nothing
      gfx_fillScreen(TFT_BLACK);                       // carry on to the normal no-card path
    }
  }
  if(sdok){
    sdPullups();                                   // lab14g: CMD/D0 pull-ups, then the metadata guard before ANY card write
    bool _sg=sdGuardInstall();
    if(!SD_MMC.exists(GTI_DIR))SD_MMC.mkdir(GTI_DIR);   // lab14g: the log + state files live here, not in the root
    if(!SD_MMC.exists("/ADF")){SD_MMC.mkdir("/ADF");ensureSampleFolder();SD_MMC.mkdir("/screensaver");}   // blank card: SAMPLE example + arm the screensaver by default (v4.8.5 — DELETE /screensaver to disable it; empty = the bouncing starburst, drop in JPGs for a gallery)
    if(!SD_MMC.exists("/DSK"))SD_MMC.mkdir("/DSK");
    if(!SD_MMC.exists("/GENERIC"))SD_MMC.mkdir("/GENERIC");   // v5.2: generic/any-machine library
    generateDefaultConfig();
    selfHealConfig();           // append any documented keys an older CONFIG.TXT is missing
    loadConfig();
    g_sdg.on=g_sdguard_cfg; sdPullups();           // lab14g: SDGUARD= / SDPULLUP= from CONFIG.TXT
    if(g_sd_freq!=20000){           // 5.3.5: SDSPEED=40 opt-in (lab14g: or 10) — remount, fall back to 20 if it won't take
      sdGuardRemove();
      SD_MMC.end();delay(30);
      if(!sdMountTry(g_sd_freq)){g_sd_freq=20000;SD_MMC.end();delay(30);sdMountTry(20000);}   // P4G7
      sdPullups(); _sg=sdGuardInstall();
    }
    // lab14e: reserve the RAM disk FIRST, at its full BIGDISK size, so the library is sized
    // from what is really left. It used to be allocated after the library, and a big library
    // could leave it short (it stepped itself down) or panic the board before it got there.
    bcSet(BC_RAMDISK,g_img_max_kb);
    if(!g_disk && !diskAlloc()){gfx_setTextColor(TFT_RED,TFT_BLACK);gfx_setCursor(8,160);gfx_print("RAM ALLOC FAILED");gfx_flush();while(1)delay(1000);}
    espnowSetScanCap(g_dongle_cap);
    relayout();                 // apply ROTATE/COMPACT from config before first draw
    if(!sdAccessReq) c6SelfUpdate();   // P4: one-time C6 WiFi self-update (tap-gated); skipped in SD access so the radio stays down while the PC holds the card
    { // lab14l: "it's alive" screen - up within a second of power-on and held while the library loads
      // (a big card takes ~20 s before the cracktro). Drawn here, after CONFIG.TXT, so it has the right
      // rotation. Anything that follows (scan screen, cover build, TOO BIG, cracktro) simply draws over it.
      gfx_fillScreen(TFT_BLACK);
      gfx_setTextSize(3); gfx_setTextColor(COL_ORANGE,TFT_BLACK);
      gfx_setCursor((gW-gfx_textWidth("OMEGAWARE"))/2, gH/2-58); gfx_print("OMEGAWARE");
      gfx_setTextSize(6); gfx_setTextColor(TFT_WHITE,TFT_BLACK);
      gfx_setCursor((gW-gfx_textWidth("GTi"))/2, gH/2-16); gfx_print("GTi");
      gfx_flush();
    }
    gLog("[panel] strips=%d (cap %d) flushdelay=%dus  [lab3: STRIPROWS= / FLUSHUS=]\n",g_strip_rows,g_strip_cap,g_flush_us);
    gLog("\n=== BOOT %s === reset=%d boot=%u int=%u psram=%u ===\n",FW_VERSION,(int)esp_reset_reason(),(unsigned)g_bootCount,(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),(unsigned)ESP.getFreePsram());
    if(g_bc_prev.crashed){   // lab14e: where did the last boot die?
      if(g_bc_prev.valid) gLog("[crash] last boot died (reset=%d) in: %s | n=%u psram=%u int=%u | first failed alloc: %u B caps=0x%X%s\n",
                               g_bc_prev.reason,bcName(g_bc_prev.stage),(unsigned)g_bc_prev.n,(unsigned)g_bc_prev.psram,(unsigned)g_bc_prev.intr,
                               (unsigned)g_bc_prev.failsz,(unsigned)g_bc_prev.failcaps,g_bc_prev.failsz?"":" (none)");
      else gLog("[crash] last boot died (reset=%d) - no breadcrumb (older firmware or power loss)\n",g_bc_prev.reason);
    }
    gLog("[sd] clock %d kHz, %s, internal pull-ups on CMD/D0-D3 %s | guard %s (drive %d via %d)\n",g_sd_freq,g_sd_4bit?"4-bit":"1-bit",g_sdpullup_cfg?"ON":"OFF",
         _sg?(g_sdg.on?"ON":"installed, OFF (SDGUARD=OFF)"):"NOT INSTALLED",(int)g_sdg_pdrv,(int)g_sdg_lower);
    gLog("[ramdisk] reserved first: max image %luKB, %lu bytes PSRAM | psram now %u\n",(unsigned long)g_img_max_kb,(unsigned long)TOTAL_SECTORS*512UL,(unsigned)ESP.getFreePsram());
    if(!sdAccessReq){   // 5.9.41-lab14d: SD ACCESS never needs the library - and a too-big card must still be reachable
    if(g_bc_prev.crashed && g_bc_prev.valid && bcIsLibrary(g_bc_prev.stage)) libCrashed();   // lab14e: never returns - no reboot loop
    // 5.8.9 put the library in PSRAM with extmem_enable(16) - but that still sends every
    // allocation UNDER 16 bytes to internal RAM first. Each game's disk list is one 4-byte
    // block (20 B with heap overhead), so 13,550 games needed ~228 KB of internal RAM when
    // only ~144 KB was free (measured on a 32-bit host model of the real code). Internal RAM
    // ran dry, a driver's own allocation failed and the board panicked - with MB of PSRAM
    // still free. lab14e: limit 0 = every size goes to PSRAM first while the library is built.
    heap_caps_malloc_extmem_enable(0);
    uint32_t _tscan=millis();
    listImages(SD_MMC,g_files);
    uint32_t _tscanned=millis();
    gLog("[boot] scan %lums for %d files\n",(unsigned long)(_tscanned-_tscan),(int)g_files.size());
    if(!readGameCache()){buildGameList();buildThumbs();}   // fresh card: build reel thumbs up-front
    // 5.9.31-lab1 sidecars. Walked the SD this boot? the harvest is authoritative: fold it in and
    // persist (the sig guard skips an unchanged rewrite). buildGameList already folded on the cold
    // path; this also covers warm-gamecache-but-index-rebuilt. No walk -> restore from .nfocache.
    bcSet(BC_NFOCACHE,(uint32_t)g_games.size());   // lab14e breadcrumb
    if(g_sidecars_harvested){ if(assignSidecarsFromHarvest())writeGameCache(); writeNfoCacheIfChanged(); }
    else loadNfoCache();
    g_nfoharvest.clear(); g_nfoharvest.shrink_to_fit();   // harvest has done its job — give the PSRAM back
    fwLocFree();                                            // lab14f: warm path (no buildThumbs) - drop the cover locations
    g_manualset.clear();  g_manualset.shrink_to_fit();
    g_hdset.clear();      g_hdset.shrink_to_fit();
    heap_caps_malloc_extmem_enable(4096);   // restore: runtime allocations back to internal (fast UI)
    bcSet(BC_UISTART,(uint32_t)g_games.size());   // lab14e breadcrumb
    sdGuardReport(true);                            // lab14g: what the guard saw during the scan/build
    gLog("[boot] build %lums | scan+build %lums int=%u psram=%u\n",(unsigned long)(millis()-_tscanned),(unsigned long)(millis()-_tscan),(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),(unsigned)ESP.getFreePsram());
    applyStats();
    buildActiveLetters();
    if(!g_games.empty())setActiveLetter(bucketOf(g_games[0].name));
    if(g_lastused)restoreLastUsed();   // LASTUSED=ON: jump the selection back to the game loaded before power-off
    scanScreensaver();
    }
  } else {gfx_setTextColor(TFT_RED,TFT_BLACK);gfx_setCursor(8,200);gfx_print(T(L_SD_MOUNT_FAIL));gfx_flush();delay(2000);relayout();}   // no card: still init layout so INFO/LOAD DIAG work
  // 5.9.35: RAM disk sized from the BIGDISK size (default when there is no card to read).
  // lab14e: normally already allocated above, before the library; this covers the no-card path.
  if(!g_disk && !diskAlloc()){gfx_setTextColor(TFT_RED,TFT_BLACK);gfx_setCursor(8,160);gfx_print("RAM ALLOC FAILED");gfx_flush();while(1)delay(1000);}
  gLog("[ramdisk] max image %luKB -> %lu sectors, %u sec/clus, %lu bytes PSRAM (free %u)\n",
       (unsigned long)g_img_max_kb,(unsigned long)TOTAL_SECTORS,(unsigned)SECTORS_PER_CLUSTER,
       (unsigned long)TOTAL_SECTORS*512UL,(unsigned)ESP.getFreePsram());
  build_volume(getOutputFilename(),g_mode==MODE_ADF?ADF_DEFAULT_SIZE:64);
  espnowSetClaimAsk(claimAskUI);
  espnowSetSavePersist(svPersistWireless);   // lab3-P4G7: saves pulled back on EJECT and before the next send   // lab14s: take-over question for a shared dongle
  if(g_wireless_mode && !g_link_home && !sdAccessReq && g_c6_ready){espnowBegin();g_espnow_started=true;}   // v5.1: not in SD access. P4: only once the C6 is confirmed up to date (c6SelfUpdate)
  if(g_cracktro>=0)drawCracktro(g_cracktro);   // CRACKTRO=OFF/NONE (-1) skips the boot demo entirely
  USB.onEvent(usbEventCB);
  if(sdAccessReq){runSDAccessBoot(sdok);}   // v5.1: SD-access boot mode — never returns (reboots to normal)
  MSC.vendorID("ESP32");MSC.productID("RAMDISK");MSC.productRevision("1.0");
  MSC.onRead(onRead);MSC.onWrite(onWrite);MSC.mediaPresent(true);
  MSC.begin(TOTAL_SECTORS,512);g_usb_announced=TOTAL_SECTORS;USB.begin();hardDetach();
  bool bootCar=(g_car_bootmode==1)||(g_car_bootmode==2&&readLastView()==1);   // v4.8.6: CAROUSEL= 0=list / 1=reel / LAST=restore
  if(bootCar&&!g_games.empty())carEnter();else{drawFullUI();gfx_flush();}
  bcSet(BC_READY,(uint32_t)g_games.size());   // lab14e: boot finished - a crash from here on is not a library-load crash
  esp_ota_mark_app_valid_cancel_rollback();   // v5.3: confirm this image booted OK (satisfies the A/B rollback handshake; harmless no-op on non-rollback bootloaders)
  if(g_wireless_mode && g_link_home && !sdAccessReq) webPanelBegin();   // 5.9.12: web only in Wireless + WiFi (Standalone = radio off)
  // Merge step 1 smoke test: DAV_TEST=<remote path> in CONFIG.TXT fetches that
  // file over WebDAV right after boot and mounts it — the whole shared-client
  // wiring, visible on a Gotek, with zero UI. Skipped in wireless mode (the
  // coexistence question parked for a later step) and when unset.
  if(g_dav_test.length()&&!g_espnow_started){
    String tn=g_dav_test;int ls=tn.lastIndexOf('/');if(ls>=0)tn=tn.substring(ls+1);
    gfx_setTextSize(1);gfx_setTextColor(TFT_CYAN,TFT_BLACK);gfx_setCursor(8,300);gfx_print("DAV_TEST: "+tn);gfx_flush();
    // Failure must be readable on the PANEL: the serial port is plugged into
    // the Gotek, so a Serial-only message is a message to nobody.
    if(doLoadWebdav(g_dav_test,tn)){
      gfx_setTextColor(TFT_GREEN,TFT_BLACK);gfx_setCursor(8,312);gfx_print("DAV_TEST: mounted OK");gfx_flush();delay(1500);
    }else{
      gfx_setTextColor(TFT_RED,TFT_BLACK);gfx_setCursor(8,312);gfx_print("DAV_TEST failed: "+g_dav_fail);gfx_flush();delay(4000);
    }
    drawFullUI();gfx_flush();
  }
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
  auto&g=g_games[g_sel];int maxNW=rowNameMaxW(g_sel);   // lab16b-P4: shared lane maths
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
      gfx_setTextSize(2);gfx_setTextColor(COL_LIT,COL_BG);gfx_setCursor(8,STATUS_H+MODE_BAR_H+8);gfx_print(T(L_USER_DISKS));
      gfx_setTextSize(1);gfx_setTextColor(COL_DIM,COL_BG);gfx_setCursor(8,STATUS_H+MODE_BAR_H+26);gfx_print(T(L_PREFMT));
      int cy=listTop; gfx_fillRoundRect(8,cy,VW-16,rowH-4,6,COL_GREEN);gfx_setTextColor(TFT_BLACK,COL_GREEN);gfx_setCursor(16,cy+(rowH-12)/2);gfx_print(T(L_CREATE_DISK));
      int y=listTop+rowH;
      for(int i=scroll;i<(int)disks.size()&&y<VH-6;i++){
        String nm=disks[i];int s=nm.lastIndexOf('/');if(s>=0)nm=nm.substring(s+1);int dot=nm.lastIndexOf('.');if(dot>0)nm=nm.substring(0,dot);
        gfx_fillRoundRect(8,y,VW-16,rowH-4,6,COL_PANEL);gfx_setTextColor(COL_LIT,COL_PANEL);
        while(gfx_textWidth(nm)>VW-88&&nm.length()>1)nm=nm.substring(0,nm.length()-1);
        gfx_setCursor(16,y+(rowH-12)/2);gfx_print(nm);
        gfx_fillRoundRect(VW-68,y+3,60,rowH-10,5,COL_BAR);gfx_setTextColor(COL_AMBER,COL_BAR);gfx_setCursor(VW-60,y+(rowH-12)/2);gfx_print(T(L_RENAME));
        y+=rowH;
      }
      if(disks.empty()){gfx_setTextColor(COL_DIM,COL_BG);gfx_setCursor(16,listTop+rowH+8);gfx_print(T(L_NONE_YET));}
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

// v5.6.0: fresh (uncached) reload of the current library level = mode root + g_libpath.
// Used for category browsing so per-level scans aren't served/polluted by the per-mode cache.
static void reloadLevel(){
  // lab14n: hand back the current library FIRST, exactly as doRescan does (lab14e). This used to walk the
  // card into a new list while the old one (10,000 games + reel tiles) was still in memory: on a big card
  // the memory check tripped at once and the level rebuilt as an EMPTY list ("no .adf files found").
  g_files.release(); gamesClear();
  carRuntimeRelease(); g_cover_flags_ready=false;
  heap_caps_malloc_extmem_enable(0);       // library build: small blocks to PSRAM too (see setup)
  scanImagesAnimated(g_files);            // fills g_files (titles) + g_cats (sub-categories) + g_coverset
  buildGameList();buildThumbs();           // buildGameList's cache write is guarded to the top level
  g_nfoharvest.clear();g_nfoharvest.shrink_to_fit();fwLocFree();g_manualset.clear();g_manualset.shrink_to_fit();g_hdset.clear();g_hdset.shrink_to_fit();
  heap_caps_malloc_extmem_enable(4096);    // back to the runtime setting
  bcSet(BC_READY,(uint32_t)g_games.size());   // lab14n: out of the library stages (a later crash must not read as a library crash)
  applyStats();                            // applyStats restores fav/plays
  buildActiveLetters();g_sel=g_scroll=0;g_scrollPx=0;g_az_page=0;g_disk_sel=0;g_disk_page=0;
  if(!g_games.empty())setActiveLetter(bucketOf(g_games[0].name));
}

// v5.6.0: category folder browser (drill-down). Shows the sub-folders (categories) at the
// current level; tapping one descends (updates g_libpath + rescans). Returns when a level
// has no further categories (a leaf level of titles) or the user taps the bar to close.
static void doCategoryBrowse(){
  const int rowH=30, listTop=STATUS_H+MODE_BAR_H+34;
  bool dirty=true, pressed=false; int rel=0, scroll=0;
  {uint32_t t0=millis();while(Touch_ReadFrame()&&millis()-t0<500)delay(10);}
  while(true){
    if(g_cats.empty()) return;             // nothing to browse here -> back to the game list of this level
    if(dirty){dirty=false;
      gfx_fillScreen(COL_BG); drawStatusBar();
      gfx_fillRect(0,STATUS_H,VW,MODE_BAR_H,COL_BAR); gfx_setTextSize(1);
      gfx_setTextColor(COL_AMBER,COL_BAR);gfx_setCursor(COVER_X+12,COVER_Y+6);gfx_print("CATEGORIES");
      {String p=g_libpath.length()?g_libpath:"/";gfx_setTextColor(COL_MID,COL_BAR);gfx_setCursor(VW-6-gfx_textWidth(p),STATUS_H+6);gfx_print(p);}
      gfx_setTextSize(2);gfx_setTextColor(COL_LIT,COL_BG);gfx_setCursor(8,STATUS_H+MODE_BAR_H+8);gfx_print("CATEGORIES");
      int y=listTop;
      if(g_libpath.length()){gfx_fillRoundRect(8,y,VW-16,rowH-4,6,COL_BLUE);gfx_setTextColor(TFT_WHITE,COL_BLUE);gfx_setCursor(16,y+(rowH-12)/2);gfx_print("< ..");y+=rowH;}
      for(int i=scroll;i<(int)g_cats.size()&&y<VH-6;i++){
        gfx_fillRoundRect(8,y,VW-16,rowH-4,6,COL_PANEL);gfx_setTextColor(COL_LIT,COL_PANEL);
        String nm=g_cats[i];while(gfx_textWidth(nm)>VW-28&&nm.length()>1)nm=nm.substring(0,nm.length()-1);
        gfx_setCursor(16,y+(rowH-12)/2);gfx_print(nm);y+=rowH;
      }
      gfx_flush();
    }
    uint16_t tx=0,ty=0; bool have=Touch_ReadFrame()&&getTouchXY(&tx,&ty);
    if(have){ rel=0;
      if(!pressed){ pressed=true; bool handled=false;
        if(ty>=STATUS_H&&ty<STATUS_H+MODE_BAR_H) return;          // tap the bar closes to the game list
        int y=listTop;
        if(g_libpath.length()){
          if(ty>=y&&ty<y+rowH-4){int s=g_libpath.lastIndexOf('/');g_libpath=(s>0)?g_libpath.substring(0,s):"";reloadLevel();scroll=0;dirty=true;handled=true;}
          y+=rowH;
        }
        if(!handled){
          for(int i=scroll;i<(int)g_cats.size()&&y<VH-6;i++){
            if(ty>=y&&ty<y+rowH-4){g_libpath+="/"+g_cats[i];reloadLevel();scroll=0;if(g_cats.empty())return;dirty=true;break;}
            y+=rowH;
          }
        }
      }
    } else { if(pressed&&++rel>=3)pressed=false; }
    delay(12);
  }
}

// v5.2: switch the browser to a library mode (ADF / DSK / GEN) — reload list, rebuild games, redraw.
static void switchLib(int m){   // int, not DiskMode: Arduino auto-generates this prototype ABOVE the enum decl, so an enum param won't compile
  { // lab15g: say so AT ONCE - on a big card the load takes seconds and a silent screen reads as a missed tap
    const char* t=m==MODE_ADF?"Loading ADF library...":m==MODE_DSK?"Loading DSK library...":"Loading GEN library...";
    gfx_setTextSize(2); int tw=gfx_textWidth(t), bw=tw+32, bh=40, bx=(gW-bw)/2, by=(gH-bh)/2;
    gfx_fillRoundRect(bx,by,bw,bh,10,COL_ACCENT); gfx_setTextColor(TFT_WHITE,COL_ACCENT);
    gfx_setCursor(bx+16,by+12); gfx_print(t); gfx_flush(); }
  g_mode=(DiskMode)m;g_libpath="";
  if(g_categories){reloadLevel();}
  else{g_files.release();gamesClear();   // lab14n: free the old library first (clear() kept the games + the vector's block)
    carRuntimeRelease();g_cover_flags_ready=false;heap_caps_malloc_extmem_enable(0);listImages(SD_MMC,g_files);if(!readGameCache()){buildGameList();buildThumbs();}
    if(g_sidecars_harvested){ if(assignSidecarsFromHarvest())writeGameCache(); writeNfoCacheIfChanged(); }   // lab14p: descriptions for the new library,
    else loadNfoCache();                                                                                  //   exactly as setup() does
    g_nfoharvest.clear();g_nfoharvest.shrink_to_fit();g_manualset.clear();g_manualset.shrink_to_fit();g_hdset.clear();g_hdset.shrink_to_fit();   // lab14n
    fwLocFree();heap_caps_malloc_extmem_enable(4096);bcSet(BC_READY,(uint32_t)g_games.size());buildActiveLetters();g_sel=g_scroll=0;g_scrollPx=0;g_az_page=0;if(!g_games.empty())setActiveLetter(bucketOf(g_games[0].name));}
  drawFullUI();gfx_flush();
}
static void drawInfoBottomBar(){
  NeoScope _ns;
  if(g_neo){   // lab16b-P4: < BACK (page back, or out) | PAGE n / N | TEST TOOLS | NEXT > gold (DONE on the last page)
    int y=VH-BOTTOM_H; fillBack(0,y,VW,BOTTOM_H); int bw=VW/4, pad=6, bh=BOTTOM_H-14, by=y+7;
    neoPill(0*bw+pad,by,bw-2*pad,bh,COL_ACCENT,String("< ")+T(L_BACK),false);
    String pn="PAGE "+String(g_info_page+1)+" / "+String(g_info_pages);
    gfx_fillRoundRect(1*bw+pad,by,bw-2*pad,bh,bh/2,COL_BAR); gfx_setTextSize(2); if(gfx_textWidth(pn)>bw-2*pad-12)gfx_setTextSize(1); gfx_setTextColor(COL_DIM,COL_BAR);
    gfx_setCursor(1*bw+(bw-gfx_textWidth(pn))/2,by+(bh-8*text_size)/2); gfx_print(pn);
    if(g_devmode&&!g_info_test) neoPill(2*bw+pad,by,bw-2*pad,bh,COL_ACCENT,"TEST TOOLS",false);
    bool last=(g_info_page>=g_info_pages-1);
    neoPill(3*bw+pad,by,bw-2*pad,bh,COL_AMBER,last?String("DONE"):String(T(L_NEXT))+" >",false);
    return; }
  const uint16_t bg=TFT_BLACK, ink=TFT_WHITE;
  int y=VH-BOTTOM_H,bw=VW/5;
  gfx_fillRect(0,y,VW,BOTTOM_H,bg);gfx_hline(0,y,VW,COL_SEP);   // Vince test: single bar split by dividers, white-on-black
  struct{const char*l;bool on;}bb[5]={
    {"< PAGE",g_info_page>0},{"PAGE >",g_info_page<g_info_pages-1},
    {"",false},{"",false},{"CLOSE",true}};
  if(g_btn_pill){   // 5.9.30: this bar was hardcoded flat, so PILL left the settings screen half-styled
    static const uint16_t pc[5]={COL_BLUE,COL_BLUE,COL_BG,COL_BG,COL_ACCENT};
    int pad=5, bh2=BOTTOM_H-2*pad, r=bh2/2, by=y+pad;
    for(int i=0;i<5;i++){
      if(!bb[i].l[0])continue;
      uint16_t bc=bb[i].on?pc[i]:COL_BAR, ic=inkFor(bc);      // inactive PAGE key = dim capsule, not just dim text
      int bx=i*bw+pad, w=bw-2*pad;
      gfx_fillRoundRect(bx,by,w,bh2,r,bc);
      int sz=2; gfx_setTextSize(sz); int tw=gfx_textWidth(bb[i].l);
      if(tw>w-6){ sz=1; gfx_setTextSize(sz); tw=gfx_textWidth(bb[i].l); }
      gfx_setTextColor(ic,bc);
      gfx_setCursor(bx+(w-tw)/2,by+(bh2-8*sz)/2);gfx_print(bb[i].l);
    }
    return;
  }
  for(int i=1;i<5;i++){ if(bb[i-1].l[0]&&bb[i].l[0]) gfx_vline(i*bw,y+8,BOTTOM_H-16,ink); }   // dividers between adjacent populated slots
  for(int i=0;i<5;i++){
    if(!bb[i].l[0])continue;
    uint16_t fg=bb[i].on?ink:COL_DIM;   // inactive PAGE keys greyed
    int sz=2; gfx_setTextSize(sz); int tw=gfx_textWidth(bb[i].l);
    if(tw>bw-8){ sz=1; gfx_setTextSize(sz); tw=gfx_textWidth(bb[i].l); }
    gfx_setTextColor(fg,bg);
    gfx_setCursor(i*bw+(bw-tw)/2,y+(BOTTOM_H-8*sz)/2);gfx_print(bb[i].l);
  }
}
static void drawInfoFull(){
  {NeoScope _ns;clearBack();drawStatusBar();drawInfoPanel();drawInfoBottomBar();}gfx_flush();   // lab16a-P4: NEO background
}
static void infoAction(uint8_t act){
  switch(act){
    case IA_MODE: { int m=!g_wireless_mode?0:(g_link_home?2:1); m=(m+1)%3; g_wireless_mode=(m!=0); g_link_home=(m==2); saveConfigKey("MODE",g_wireless_mode?"WIRELESS":"STANDALONE"); saveConfigKey("LINK",g_link_home?"HOMEWIFI":"ESPNOW"); applyRadioMode(); drawInfoFull(); } break;   // 5.9.19: live switch, no reboot, no splash
    case IA_FONT: applyFont((g_font+1)%3);saveConfigKey("FONT",fontKey(g_font));drawInfoFull();break;
    case IA_NEOUI: g_neo_on=!g_neo_on;applyTheme(g_theme_idx);relayout();saveConfigKey("NEO",g_neo_on?"ON":"OFF");{float mp=(float)maxScrollPx();if(g_scrollPx>mp)g_scrollPx=mp;}g_info_page=0;drawInfoFull();break;   // lab4-P4: live switch, no reboot
    case IA_THEME: applyTheme((g_theme_idx+1)%(NUM_THEMES-1));saveConfigKey("THEME",String(g_theme_idx));drawInfoFull();break;   // Vince test: theme cycling lives in CONFIG now
    case IA_LANG: g_lang=(g_lang+1)%LANG_N;saveConfigKey("LANG",LANG_NAMES[g_lang]);drawInfoFull();break;
    case IA_ROTATE: g_rot=(g_rot+1)&3;relayout();saveConfigKey("ROTATE",String(g_rot*90));{float mp=(float)maxScrollPx();if(g_scrollPx>mp)g_scrollPx=mp;}drawInfoFull();break;
    case IA_COMPACT: g_compact=!g_compact;relayout();saveConfigKey("COMPACT",g_compact?"ON":"OFF");{float mp=(float)maxScrollPx();if(g_scrollPx>mp)g_scrollPx=mp;}drawInfoFull();break;
    case IA_DONGLE: doPairNow();drawInfoFull();break;
    case IA_HIVEMIND: g_hivemind=!g_hivemind;saveConfigKey("HIVEMIND",g_hivemind?"ON":"OFF");drawInfoFull();break;
    case IA_HOMEWIFI: doHomeWifiSetup(); drawInfoFull(); break;   // on-screen SSID/password entry
    case IA_RESCAN: doRescan();break;
    case IA_RESET: {gfx_fillScreen(COL_BG);gfx_setTextSize(2);gfx_setTextColor((uint16_t)0xE8C4,COL_BG);const char*m=T(L_RESETTING);gfx_setCursor((VW-gfx_textWidth(m))/2,VH/2-8);gfx_print(m);gfx_flush();delay(700);ESP.restart();}break;
    case IA_DIAG: if(g_loaded&&g_loaded_name=="AMIGA TEST KIT"){g_info_showing=false;doUnload();drawFullUI();gfx_flush();}else doLoadDiag();break;
    case IA_SDACCESS: {gLog("[sdguard] entering SD ACCESS after %lu min up - totals since this boot:\n",(unsigned long)(millis()/60000)); sdGuardReport(true);   // lab14j: the session's reads, before the restart wipes them
      gfx_fillScreen(COL_BG);gfx_setTextSize(2);gfx_setTextColor((uint16_t)0x05FF,COL_BG);const char*m="ENTERING SD ACCESS...";gfx_setCursor((VW-gfx_textWidth(m))/2,VH/2-8);gfx_print(m);gfx_flush();g_sdaccess_magic=SDACCESS_MAGIC;delay(350);ESP.restart();}break;
    case IA_FWUPDATE: doFirmwareUpdate();g_info_showing=false;drawFullUI();gfx_flush();break;
    case IA_LIBMODE: switchLib((g_mode+1)%3); if(g_info_showing)drawInfoFull(); break;   // v5.6.0: cycle ADF->DSK->GEN, stay in INFO
    case IA_CATEG: g_categories=!g_categories; saveConfigKey("CATEGORIES", g_categories?"ON":"OFF"); g_libpath=""; drawInfoFull(); break;   // flip+save like COMPACT/HIVEMIND; NO rescan (g_cats builds when the CATEGORIES button is tapped)
    case IA_BTNSTYLE: g_btn_pill=!g_btn_pill; saveConfigKey("BTNSTYLE", g_btn_pill?"PILL":"FLAT"); drawInfoFull(); break;   // 5.8.3
    case IA_SAVER: g_ss_enabled=!g_ss_enabled; saveConfigKey("SCREENSAVER", g_ss_enabled?"ON":"OFF"); drawInfoFull(); break;   // real on-screen screensaver ON/OFF
    case IA_CRACKTRO:   // boot intro ON/OFF -> CONFIG.TXT CRACKTRO=; OFF is -1, ON restores the remembered style
      if(g_cracktro>=0){ g_cracktro_prev=g_cracktro; g_cracktro=-1; saveConfigKey("CRACKTRO","OFF"); }
      else { g_cracktro=g_cracktro_prev;
             String cv; if(g_cracktro==8)cv="DENISE"; else if(g_cracktro==9)cv="WRANGLER"; else if(g_cracktro==10)cv="RETRONAUT"; else cv=String(g_cracktro);
             saveConfigKey("CRACKTRO", cv); }
      drawInfoFull(); break;
    case IA_TESTPAGE: g_info_test=true;  g_info_page=0; drawInfoFull(); break;   // lab14k: open the TEST TOOLS sub-page
    case IA_TESTBACK: g_info_test=false; g_info_page=0; drawInfoFull(); break;   // lab14k: back to the main Settings list
    case IA_DIAGDISP: g_diagdisp=!g_diagdisp; saveConfigKey("DIAGDISP", g_diagdisp?"ON":"OFF"); drawInfoFull(); break;   // live diagnostic overlay ON/OFF
    case IA_REELBORDER: g_reelborder=!g_reelborder; saveConfigKey("REELBORDER", g_reelborder?"ON":"OFF"); drawInfoFull(); break;   // MasterTelly CR: reel cover frame ON/OFF (applies next reel draw)
    case IA_LASTUSED: g_lastused=!g_lastused; saveConfigKey("LASTUSED", g_lastused?"ON":"OFF"); drawInfoFull(); break;
    case IA_NOCACHE: g_nocache=!g_nocache; saveConfigKey("NOCACHE", g_nocache?"ON":"OFF"); drawInfoFull(); break;   // takes effect on the next boot/rescan
    case IA_COVERS:  g_covers_on=!g_covers_on; saveConfigKey("COVERS", g_covers_on?"ON":"OFF"); drawInfoFull(); break;
    case IA_REELPROF: g_reelprof=!g_reelprof; saveConfigKey("REELPROF", g_reelprof?"ON":"OFF");
                      g_rp_frames=g_rp_draw=g_rp_clear=g_rp_blit=g_rp_sav=g_rp_flush=0; g_rp_t0=millis();
                      drawInfoFull(); break;   // 5.9.33-lab3: live frame breakdown into /gti.log
    case IA_SDSOAK: sdSoakTest(); drawInfoFull(); break;   // lab14g
    case IA_USRDSK: { g_info_showing=false; String p=doUserDisks();   // lab16b-P4: same as the old USR-DSK button
      if(p.length()){ if(doLoadSelected(p)){g_loaded_game_idx=-1;String nm=p;int s=nm.lastIndexOf('/');if(s>=0)nm=nm.substring(s+1);int d=nm.lastIndexOf('.');if(d>0)nm=nm.substring(0,d);g_loaded_name=nm;} }
      drawFullUI();gfx_flush(); } break;
    case IA_LISTTILE: g_listtile=!g_listtile; saveConfigKey("LISTTILE", g_listtile?"ON":"OFF"); drawInfoFull(); break;   // 5.9.34-lab4: list cover from the reel tile vs a fresh JPEG decode
    case IA_SSMODE:
      if(g_ss_slides){ g_ss_slides=false; g_ss_matrix=false; saveConfigKey("SSMODE","BOUNCE"); }
      else if(!g_ss_matrix){ g_ss_matrix=true; g_ss_slides=false; saveConfigKey("SSMODE","MATRIX"); }
      else { g_ss_slides=true; g_ss_matrix=false; saveConfigKey("SSMODE","SLIDES"); }
      drawInfoFull(); break;   // 5.8.3 cycle SLIDES->BOUNCE->MATRIX
    case IA_SSFAV: g_ss_fav=!g_ss_fav; saveConfigKey("SSFAV", g_ss_fav?"ON":"OFF"); drawInfoFull(); break;   // 5.8.3
    case IA_WEBUI: doWebUiSetup(); drawInfoFull(); break;   // 5.9.9
    case IA_WIFICHECK: doWifiCheck(); drawInfoFull(); break;   // 5.9.10
    default: break;
  }
}
static void handleTap(uint16_t px,uint16_t py){
  // ── v5.5.4: full-screen paginated INFO — swallow ALL taps while settings are open ──
  if(g_info_showing){
    if(g_neo&&py<(uint16_t)STATUS_H&&px>=(uint16_t)(VW-NEO_GEAR_W)){ g_info_showing=false;g_info_test=false;drawFullUI();gfx_flush();return; }   // lab16a-P4: gear closes Settings
    if(g_neo&&py>=(uint16_t)(VH-BOTTOM_H)){   // lab16b-P4: BACK | (page) | TEST TOOLS | NEXT / DONE
      int slot=px/(VW/4); if(slot>3)slot=3;
      if(slot==0){ if(g_info_page>0){g_info_page--;drawInfoFull();}
                   else if(g_info_test){g_info_test=false;g_info_page=0;g_info_last=IA_NONE;drawInfoFull();}
                   else {g_info_showing=false;drawFullUI();gfx_flush();} }
      else if(slot==2){ if(g_devmode&&!g_info_test){g_info_test=true;g_info_page=0;g_info_last=IA_NONE;drawInfoFull();} }
      else if(slot==3){ if(g_info_page<g_info_pages-1){g_info_page++;drawInfoFull();}
                        else {g_info_showing=false;g_info_test=false;drawFullUI();gfx_flush();} }
      return; }
    if(py>=(uint16_t)(VH-BOTTOM_H)){
      int bw=VW/5,bsel=px/bw; if(bsel>4)bsel=4;
      if(bsel==0){ if(g_info_page>0){g_info_page--;drawInfoFull();} }
      else if(bsel==1){ if(g_info_page<g_info_pages-1){g_info_page++;drawInfoFull();} }
      else if(bsel==4){ g_info_showing=false;drawFullUI();gfx_flush(); }
      return;
    }
    for(int i=0;i<g_ir_n;i++){
      if(px>=(uint16_t)g_ir[i].x&&px<(uint16_t)(g_ir[i].x+g_ir[i].w)&&py>=(uint16_t)g_ir[i].y&&py<(uint16_t)(g_ir[i].y+g_ir[i].h)){
        uint32_t t0=millis(); uint8_t a=g_ir[i].act; g_info_last=a; infoAction(a);   // lab16a-P4: remember it for the gold outline   // lab15a2: slow Settings taps go in the log
        uint32_t dt=millis()-t0; if(dt>300) gLog("[slow] settings action %u took %lums | int=%u psram=%u largest-int=%u\n",(unsigned)a,(unsigned long)dt,(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),(unsigned)ESP.getFreePsram(),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        return; }
    }
    return;
  }
  // ── lab16a-P4: NEO gear (top-right) opens Settings ──
  if(g_neo&&py<(uint16_t)STATUS_H&&px>=(uint16_t)(VW-NEO_GEAR_W)){ g_info_showing=true;g_info_page=0;g_info_test=false;g_info_last=IA_NONE;drawInfoFull();return; }
  // ── lab16b-P4: the library name in the top bar = the old mode-bar button (switch library, or open the categories) ──
  if(g_neo&&py<(uint16_t)STATUS_H&&g_neo_ctr_x1>g_neo_ctr_x0&&px+12>=(uint16_t)g_neo_ctr_x0&&px<(uint16_t)(g_neo_ctr_x1+12)){
    if(g_categories){ g_libpath=""; reloadLevel(); doCategoryBrowse(); drawFullUI(); gfx_flush(); }
    else switchLib((g_mode+1)%3);
    return; }
  // ── lab16b-P4: FAV pill and DISK pills on the game panel ──
  if(g_neo&&!g_games.empty()){
    if(g_neo_fav_bw&&px>=(uint16_t)g_neo_fav_bx&&px<(uint16_t)(g_neo_fav_bx+g_neo_fav_bw)&&py>=(uint16_t)(g_neo_fav_by-4)&&py<(uint16_t)(g_neo_fav_by+g_neo_fav_bh+4)){
      g_games[g_sel].fav=!g_games[g_sel].fav; saveStats(); drawListAndCover(); gfx_flush(); return; }
    if(g_nd_n>0&&py>=(uint16_t)(g_nd_y-6)&&py<(uint16_t)(g_nd_y+g_nd_h+6)){
      auto&game=g_games[g_sel];
      if(g_nd_more_x>=0&&px>=(uint16_t)g_nd_more_x&&px<(uint16_t)(g_nd_more_x+g_nd_pb)){ g_disk_page++; drawCoverPanel(); gfx_flush(); return; }
      if(px>=(uint16_t)g_nd_x&&px<(uint16_t)(g_nd_x+g_nd_n*g_nd_pb)){
        int d=g_nd_first+(px-g_nd_x)/g_nd_pb; if(d>=game.disk_count)d=game.disk_count-1;
        g_disk_sel=d; if(g_hotswap&&g_loaded&&g_loaded_game_idx==g_sel)doLoadSelected(g_files[game.disk_indices[g_disk_sel]]); else{drawCoverPanel();gfx_flush();}
        return; } } }
  // ── v4.9.2: book button on the cover — opens the .rtfm manual full-screen ──
  if(!g_info_showing&&g_manual_bw&&g_manual_path.length()&&px>=(uint16_t)g_manual_bx&&px<(uint16_t)(g_manual_bx+g_manual_bw)&&py>=(uint16_t)g_manual_by&&py<(uint16_t)(g_manual_by+g_manual_bh)){
    doManual(g_manual_path); drawFullUI(); gfx_flush(); return; }
  // ── lab15j: tap the game's text (title + description) — opens the whole .nfo full-screen in the manual reader ──
  // The list only keeps the first 400 characters (NFO_BLURB_MAX). The file is found ONCE, on the tap (one by-name
  // lookup, R7: never while scrolling) and read by doManual (up to 16 KB, same text clean-up as the .rtfm).
  if(!g_info_showing&&g_nfo_bw&&!g_games.empty()&&px>=(uint16_t)g_nfo_bx&&px<(uint16_t)(g_nfo_bx+g_nfo_bw)&&py>=(uint16_t)g_nfo_by&&py<(uint16_t)(g_nfo_by+g_nfo_bh)){
    String np; if(findNFOFor(g_files[g_games[g_sel].first_file_idx],np)){ gLog("[nfo] open %s\n",np.c_str()); doManual(np,"NFO"); drawFullUI(); gfx_flush(); }
    else gLog("[nfo] no .nfo file found for %s\n",g_games[g_sel].name.c_str());
    return; }

  // ── A-Z bar (letters + toggle button) — suppressed where the INFO panel covers it ──
  if(px>=AZ_X&&px<(uint16_t)(AZ_X+AZ_W)&&py>=AZ_TOP&&py<(uint16_t)(AZ_TOP+AZ_H)&&!(g_info_showing&&px<(uint16_t)(g_info_x+g_info_w)&&py<(uint16_t)g_info_bottom)){
    if(py<(uint16_t)(AZ_TOP+AZ_SRCH_H+(g_neo?4:0))&&active_letter_count){doSearch();drawFullUI();gfx_flush();return;}   // v4.8.2: magnifier
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
  if(!g_neo&&!g_games.empty()){auto&game=g_games[g_sel];if(game.disk_count>1){
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
  if(!g_neo&&py>=STATUS_H&&py<STATUS_H+MODE_BAR_H&&px>=LIST_X){
    if(g_categories){ if(px<LIST_X+110){ g_libpath=""; reloadLevel(); doCategoryBrowse(); drawFullUI(); gfx_flush(); return; } }   // v5.6.0: Categories button = jump to top + browse
    else{
    if(px<LIST_X+110){ switchLib((g_mode+1)%3); return; }   // lab15g: one button, cycles ADF->DSK->GEN (was three 36 px targets)
    }
    if(px<LIST_X+174){String p=doUserDisks(); if(p.length()){ if(doLoadSelected(p)){g_loaded_game_idx=-1;String nm=p;int s=nm.lastIndexOf('/');if(s>=0)nm=nm.substring(s+1);int d=nm.lastIndexOf('.');if(d>0)nm=nm.substring(0,d);g_loaded_name=nm;} } drawFullUI();gfx_flush();return;}}   // v4.9.7 USR-DSK

  // ── File list ──
  if(px>=LIST_X&&px<AZ_X&&py>=LIST_TOP&&py<LIST_BOTTOM){
    bool wasInfo=g_info_showing; g_info_showing=false;
    int gi=(int)((g_scrollPx+(py-LIST_TOP))/LIST_ITEM_H);if(gi>=0&&gi<(int)g_games.size()){
      {int r=8+g_name_sz*3;if(px<=(uint16_t)(g_neo?(LIST_X+10+neoThumbS()+6):(LIST_X+6+2*r+3))){g_games[gi].fav=!g_games[gi].fav;saveStats();if(wasInfo)drawFullUI();else drawFileList();gfx_flush();return;}}
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
  if(g_neo&&py>=VH-BOTTOM_H){   // lab16b-P4: PREV | NEXT | INSERT | REEL | CONFIG
    int btn=px/(VW/5); if(btn>4)btn=4;
    if(btn==2){ if(!g_games.empty()){
        auto&gm=g_games[g_sel];int idx=gm.disk_indices.empty()?gm.first_file_idx:gm.disk_indices[min(g_disk_sel,(int)gm.disk_indices.size()-1)];
        if(g_loaded&&g_loaded_game_idx==g_sel){ if(g_loaded_disk_idx==g_disk_sel)doUnload(); else doLoadSelected(g_files[idx]); }
        else doLoadSelected(g_files[idx]); }
      return; }
    if(btn>2)btn--;   // fall through to the classic 4-slot handling: 0 PREV, 1 NEXT, 2 REEL, 3 CONFIG
    if(btn==0&&g_sel>0){g_sel--;g_disk_sel=0;g_disk_page=0;setActiveLetter(bucketOf(g_games[g_sel].name));if((float)(g_sel*LIST_ITEM_H)<g_scrollPx)g_scrollPx=g_sel*LIST_ITEM_H;drawListAndCover();gfx_flush();}
    else if(btn==1&&g_sel<(int)g_games.size()-1){g_sel++;g_disk_sel=0;g_disk_page=0;setActiveLetter(bucketOf(g_games[g_sel].name));if((float)((g_sel+1)*LIST_ITEM_H)>g_scrollPx+(LIST_BOTTOM-LIST_TOP))g_scrollPx=(g_sel+1)*LIST_ITEM_H-(LIST_BOTTOM-LIST_TOP);drawListAndCover();gfx_flush();}
    else if(btn==2){ g_info_showing=false; carEnter(); }
    else if(btn==3){ g_info_showing=true; g_info_page=0;g_info_test=false;g_info_last=IA_NONE;drawInfoFull(); }
    return;
  }
  if(py>=VH-BOTTOM_H){
    const int nb=4;int bw=VW/nb,btn=px/bw;if(btn>=nb)btn=nb-1;   // Vince test: THEME removed from the bar (4 slots)
    if(btn==0&&g_sel>0){g_sel--;g_disk_sel=0;g_disk_page=0;setActiveLetter(bucketOf(g_games[g_sel].name));if((float)(g_sel*LIST_ITEM_H)<g_scrollPx)g_scrollPx=g_sel*LIST_ITEM_H;drawListAndCover();gfx_flush();}
    else if(btn==1&&g_sel<(int)g_games.size()-1){g_sel++;g_disk_sel=0;g_disk_page=0;setActiveLetter(bucketOf(g_games[g_sel].name));if((float)((g_sel+1)*LIST_ITEM_H)>g_scrollPx+(LIST_BOTTOM-LIST_TOP))g_scrollPx=(g_sel+1)*LIST_ITEM_H-(LIST_BOTTOM-LIST_TOP);drawListAndCover();gfx_flush();}
    else if(btn==2){ g_info_showing=false; carEnter(); }   // REEL — enter the carousel
    else if(btn==3){ g_info_showing=!g_info_showing; if(g_info_showing){g_info_page=0;g_info_test=false;g_info_last=IA_NONE;drawInfoFull();} else {drawFullUI();gfx_flush();} }
    return;
  }
}

// ════════════════════════════════════════════════════════════════════════════
// MAIN LOOP — touch state machine: tap vs drag-scroll with flick inertia
// ════════════════════════════════════════════════════════════════════════════

void loop(){
  while(g_tlog_done<g_tlog_n){ gLog("[touch] raw %d,%d (panel %dx%d)\n",g_tlog[g_tlog_done][0],g_tlog[g_tlog_done][1],P4DISP_PANEL_W,P4DISP_PANEL_H); g_tlog_done++; }   // P4G7 bring-up
  webPanelService();   // one web client + one queued DAV load per pass (merge step 2)
  { static uint32_t _sgT=0; if(g_sdg.pending_report && millis()-_sgT>2000){ _sgT=millis(); sdGuardReport(false); } }   // lab14g
  { static uint32_t _uT=0; static int _uS=-1; if(millis()-_uT>500){ _uT=millis(); int m=tud_mounted()?1:0;   // lab5-P4G7: [usb] host connect / disconnect
      if(m!=_uS){ if(_uS>=0||m) gLog("[usb] %s\n",m?"host connected (a Gotek or PC enumerated the GTi)":"host gone"); _uS=m; } } }
  if(g_espnow_link_just_established){g_espnow_link_just_established=false;
    gfx_fillRect(0,0,VW,STATUS_H,0x07E0);gfx_setTextSize(1);gfx_setTextColor(TFT_BLACK,0x07E0);
    gfx_setCursor(VW/2-57,(STATUS_H-8)/2);gfx_print(T(L_DONGLE_LINKED));gfx_flush();delay(2000);drawStatusBar();gfx_flush();}

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
    if(g_loaded&&svPending()&&g_sv_last_write&&now-g_sv_last_write>SV_SETTLE_MS)
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
    auto&g=g_games[g_sel];int maxNW=rowNameMaxW(g_sel);   // lab16b-P4
    gfx_setTextSize(g_name_sz);int mx=gfx_textWidth(g.name)-maxNW+8;if(mx<0)mx=0;
    static uint32_t lastMq=0;
    if(g_marquee_pause){ if(now-g_marquee_pause>=1000){g_marquee_pause=0;g_marquee_dir=(g_marquee_off<=0)?1:-1;lastMq=now;} }
    else if(now-lastMq>=45){ lastMq=now;g_marquee_off+=g_marquee_dir*5;
      if(g_marquee_off>=mx){g_marquee_off=mx;g_marquee_pause=now;}
      else if(g_marquee_off<=0){g_marquee_off=0;g_marquee_pause=now;}
      redrawListArea(); }
  }
}
