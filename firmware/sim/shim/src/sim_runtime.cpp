// sim: Arduino / ESP-IDF runtime pieces the GTi firmware calls, backed by the web page (sim_host.h imports).
#include <Arduino.h>
#include <stdarg.h>
#include <Wire.h>
#include <USB.h>
#include <USBMSC.h>
#include <Update.h>
#include <WiFi.h>
#include <SD_MMC.h>
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_axs15231b.h"

// ── time ──
static double s_t0 = -1;
static double s_last_yield = 0;
static inline double now_ms() { double t = js_now_ms(); if (s_t0 < 0) { s_t0 = t; s_last_yield = t; } return t - s_t0; }
static void maybe_yield() {          // busy loops that only poll millis()/touch still let the browser breathe
  double t = js_now_ms();
  if (t - s_last_yield > 40) { s_last_yield = t; js_sleep(0); s_last_yield = js_now_ms(); }
}
extern "C" {
unsigned long millis(void) { maybe_yield(); return (unsigned long)now_ms(); }
unsigned long micros(void) { return (unsigned long)(now_ms() * 1000.0); }
void sim_net_tick(void);
void delay(uint32_t ms) { sim_net_tick(); s_last_yield = js_now_ms(); js_sleep(ms); s_last_yield = js_now_ms(); sim_net_tick(); }
void delayMicroseconds(uint32_t us) { if (us >= 1000) delay(us / 1000); }
void yield(void) { maybe_yield(); }
void vTaskDelay(TickType_t t) { delay(t); }
TickType_t xTaskGetTickCount(void) { return (TickType_t)now_ms(); }
BaseType_t xTaskCreate(TaskFunction_t, const char *, uint32_t, void *, UBaseType_t, TaskHandle_t *h) { if (h) *h = NULL; return pdPASS; }
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t, const char *, uint32_t, void *, UBaseType_t, TaskHandle_t *h, BaseType_t) { if (h) *h = NULL; return pdPASS; }
void vTaskDelete(TaskHandle_t) {}
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return (TaskHandle_t)1; }
// one thread: a mutex is always free
static int s_sem_dummy;
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return &s_sem_dummy; }
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void) { return &s_sem_dummy; }
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return &s_sem_dummy; }
SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t, UBaseType_t) { return &s_sem_dummy; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t) { return pdTRUE; }
BaseType_t xSemaphoreGive(SemaphoreHandle_t) { return pdTRUE; }
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t, TickType_t) { return pdTRUE; }
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t) { return pdTRUE; }
void vSemaphoreDelete(SemaphoreHandle_t) {}

// ── memory: the board has 8 MB PSRAM; report it as such, allocate from the wasm heap ──
static size_t s_alloc_bytes = 0;
void *ps_malloc(size_t n) { return malloc(n); }
void *ps_calloc(size_t n, size_t s) { return calloc(n, s); }
void *ps_realloc(void *p, size_t n) { return realloc(p, n); }
bool psramFound(void) { return true; }
void *heap_caps_malloc(size_t n, uint32_t) { return malloc(n); }
void *heap_caps_calloc(size_t n, size_t s, uint32_t) { return calloc(n, s); }
void *heap_caps_realloc(void *p, size_t n, uint32_t) { return realloc(p, n); }
void *heap_caps_aligned_alloc(size_t a, size_t n, uint32_t) { return aligned_alloc(a, (n + a - 1) / a * a); }
void heap_caps_free(void *p) { free(p); }
// every allocation is counted (malloc & co are wrapped at link time), so "free PSRAM" behaves like the board's 8 MB
static size_t s_heap_used = 0;
extern "C" {
size_t malloc_usable_size(void *);
void *__real_malloc(size_t); void __real_free(void *); void *__real_calloc(size_t, size_t); void *__real_realloc(void *, size_t);
void *__real_aligned_alloc(size_t, size_t); int __real_posix_memalign(void **, size_t, size_t);
void *__wrap_malloc(size_t n) { void *p = __real_malloc(n); if (p) s_heap_used += malloc_usable_size(p); return p; }
void __wrap_free(void *p) { if (p) { s_heap_used -= malloc_usable_size(p); __real_free(p); } }
void *__wrap_calloc(size_t a, size_t b) { void *p = __real_calloc(a, b); if (p) s_heap_used += malloc_usable_size(p); return p; }
void *__wrap_realloc(void *o, size_t n) { size_t was = o ? malloc_usable_size(o) : 0; void *p = __real_realloc(o, n); if (p) { s_heap_used -= was; s_heap_used += malloc_usable_size(p); } return p; }
void *__wrap_aligned_alloc(size_t a, size_t n) { void *p = __real_aligned_alloc(a, n); if (p) s_heap_used += malloc_usable_size(p); return p; }
int __wrap_posix_memalign(void **pp, size_t a, size_t n) { int r = __real_posix_memalign(pp, a, n); if (!r && *pp) s_heap_used += malloc_usable_size(*pp); return r; }
}
static const size_t SIM_PSRAM = 8u * 1024 * 1024, SIM_INTERNAL_USED = 140u * 1024;
size_t heap_caps_get_free_size(uint32_t caps) {
  if (caps & MALLOC_CAP_SPIRAM) { size_t u = s_heap_used > SIM_INTERNAL_USED ? s_heap_used - SIM_INTERNAL_USED : 0; return u < SIM_PSRAM - 65536 ? SIM_PSRAM - u : 65536; }
  return 180 * 1024;
}
size_t heap_caps_get_largest_free_block(uint32_t caps) { return (caps & MALLOC_CAP_SPIRAM) ? heap_caps_get_free_size(caps) : 110 * 1024; }
size_t heap_caps_get_total_size(uint32_t caps) { return (caps & MALLOC_CAP_SPIRAM) ? 8u * 1024 * 1024 : 320 * 1024; }
size_t heap_caps_get_minimum_free_size(uint32_t caps) { return heap_caps_get_free_size(caps); }
void heap_caps_malloc_extmem_enable(size_t) {}
esp_err_t heap_caps_register_failed_alloc_callback(esp_alloc_failed_hook_t) { return ESP_OK; }
uint32_t esp_get_free_heap_size(void) { return 180 * 1024; }
uint32_t esp_get_minimum_free_heap_size(void) { return 150 * 1024; }

// ── misc ESP ──
uint32_t esp_random(void) { return js_random(); }
void esp_fill_random(void *buf, size_t len) { uint8_t *b = (uint8_t *)buf; for (size_t i = 0; i < len; i++) b[i] = (uint8_t)js_random(); }
static esp_reset_reason_t s_reset_reason = ESP_RST_POWERON;
__attribute__((export_name("sim_set_reset_reason"))) void sim_set_reset_reason(int r) { s_reset_reason = (esp_reset_reason_t)r; }
esp_reset_reason_t esp_reset_reason(void) { return s_reset_reason; }
void esp_restart(void) { js_restart(); for (;;) js_sleep(100000); }
esp_err_t esp_read_mac(uint8_t *mac, int) { static const uint8_t m[6] = {0x3C, 0x84, 0x27, 0xC0, 0x58, 0xB0}; memcpy(mac, m, 6); return ESP_OK; }
const char *esp_err_to_name(esp_err_t c) { return c == ESP_OK ? "ESP_OK" : "ESP_FAIL"; }
static vprintf_like_t s_vprintf = vprintf;
vprintf_like_t esp_log_set_vprintf(vprintf_like_t f) { vprintf_like_t o = s_vprintf; s_vprintf = f; return o; }
const char *pathToFileName(const char *path) { const char *p = strrchr(path, '/'); return p ? p + 1 : path; }
void pinMode(uint8_t, uint8_t) {}
void digitalWrite(uint8_t, uint8_t) {}
int digitalRead(uint8_t) { return 1; }
int analogRead(uint8_t) { return 2048; }
float temperatureRead(void) { return 41.0f + (float)(js_random() % 30) / 10.0f; }
bool ledcAttach(uint8_t, uint32_t, uint8_t) { return true; }
bool ledcWrite(uint8_t, uint32_t duty) { js_backlight((int)duty); return true; }

// ── OTA: the SD update screen really reads /GTi_update.bin; the "flash" just counts the bytes ──
static esp_partition_t s_part[2] = {{0, 0x10, 0x10000, 0x6E0000, "app0", false}, {0, 0x11, 0x6F0000, 0x6E0000, "app1", false}};
const esp_partition_t *esp_ota_get_running_partition(void) { return &s_part[0]; }
const esp_partition_t *esp_ota_get_boot_partition(void) { return &s_part[0]; }
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *) { return &s_part[1]; }
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *) { return ESP_OK; }
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void) { return ESP_OK; }

// ── LCD: the AXS15231B becomes a canvas ──
struct esp_lcd_panel_io_t { int x; };
struct esp_lcd_panel_t { int x; };
static esp_lcd_panel_io_t s_io; static esp_lcd_panel_t s_panel;
esp_err_t esp_lcd_new_panel_io_spi(esp_lcd_spi_bus_handle_t, const esp_lcd_panel_io_spi_config_t *, esp_lcd_panel_io_handle_t *r) { *r = &s_io; return ESP_OK; }
esp_err_t esp_lcd_panel_io_tx_param(esp_lcd_panel_io_handle_t, int, const void *, size_t) { return ESP_OK; }
esp_err_t esp_lcd_new_panel_axs15231b(const esp_lcd_panel_io_handle_t, const esp_lcd_panel_dev_config_t *, esp_lcd_panel_handle_t *r) { *r = &s_panel; return ESP_OK; }
esp_err_t esp_lcd_panel_reset(esp_lcd_panel_handle_t) { return ESP_OK; }
esp_err_t esp_lcd_panel_init(esp_lcd_panel_handle_t) { return ESP_OK; }
esp_err_t esp_lcd_panel_del(esp_lcd_panel_handle_t) { return ESP_OK; }
esp_err_t esp_lcd_panel_disp_on_off(esp_lcd_panel_handle_t, bool) { return ESP_OK; }
esp_err_t esp_lcd_panel_invert_color(esp_lcd_panel_handle_t, bool) { return ESP_OK; }
esp_err_t esp_lcd_panel_mirror(esp_lcd_panel_handle_t, bool, bool) { return ESP_OK; }
esp_err_t esp_lcd_panel_swap_xy(esp_lcd_panel_handle_t, bool) { return ESP_OK; }
static int s_panel_h = 480;
esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t, int x0, int y0, int x1, int y1, const void *data) {
  js_present((const uint16_t *)data, x0, y0, x1, y1);
  if (y1 >= s_panel_h) maybe_yield();     // a whole frame went out: let the browser show it
  return ESP_OK;
}
}  // extern "C"

long random(long howbig) { return howbig <= 0 ? 0 : (long)(js_random() % (uint32_t)howbig); }
long random(long lo, long hi) { return hi <= lo ? lo : lo + random(hi - lo); }
void randomSeed(unsigned long) {}
long map(long x, long in_min, long in_max, long out_min, long out_max) {
  const long d = in_max - in_min; if (d == 0) return out_min; return (x - in_min) * (out_max - out_min) / d + out_min;
}

// ── Serial -> the page's console ──
SimSerial Serial;
size_t SimSerial::write(const uint8_t *buf, size_t n) { js_log((const char *)buf, (uint32_t)n); return n; }
// printf/puts from C land (vprintf hooks etc.) also end up there
extern "C" int __sim_stdout_write(const char *p, size_t n) { js_log(p, (uint32_t)n); return (int)n; }

EspClass ESP;
uint32_t EspClass::getFreeHeap() { return 180 * 1024; }
uint32_t EspClass::getFreePsram() { return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM); }
void EspClass::restart() { esp_restart(); }
uint64_t EspClass::getEfuseMac() { return 0xB058C027843CULL; }

// ── I2C: the AXS15231B touch report, built from the mouse / finger on the page ──
TwoWire Wire;
size_t TwoWire::requestFrom(int addr, int n, int) {
  _rxi = 0; _rxn = 0;
  if (addr != 0x3B || n > 16) return 0;
  int x = 0, y = 0; int down = js_touch(&x, &y);
  memset(_rx, 0, sizeof _rx);
  if (down) { _rx[0] = 0; _rx[1] = 1; _rx[2] = (uint8_t)((x >> 8) & 0x0F); _rx[3] = (uint8_t)(x & 0xFF); _rx[4] = (uint8_t)((y >> 8) & 0x0F); _rx[5] = (uint8_t)(y & 0xFF); }
  else { memset(_rx, 0xCA, (size_t)n); }
  _rxn = n; return (size_t)n;
}

// ── Wi-Fi: no networks in range ──
WiFiClass WiFi;
int16_t WiFiClass::scanNetworks(bool, bool, bool, uint32_t, uint8_t, const char *, const uint8_t *) { delay(800); return 0; }
String WiFiClass::SSID(uint8_t) { return String(""); }
int32_t WiFiClass::RSSI(uint8_t) { return -100; }

// ── USB mass storage: the "Gotek" on the page reads the disk through these ──
ESPUSB USB;
static bool s_usb_begun = false, s_usb_conn = true, s_media = false;
static msc_read_cb s_rd = nullptr; static msc_write_cb s_wr = nullptr; static msc_start_stop_cb s_ss = nullptr;
static uint32_t s_blocks = 0; static uint16_t s_bsize = 512;
static char s_vid[12], s_pid[20], s_rev[8];
bool ESPUSB::begin() { s_usb_begun = true; js_event(10, 1, 0, ""); return true; }
extern "C" bool tud_mounted(void) { return s_usb_begun && s_usb_conn && js_usb_host(); }
extern "C" bool tud_ready(void) { return tud_mounted(); }
extern "C" void tud_connect(void) { s_usb_conn = true; js_event(11, 1, 0, ""); }
extern "C" void tud_disconnect(void) { s_usb_conn = false; js_event(11, 0, 0, ""); }
USBMSC::USBMSC() {}
bool USBMSC::begin(uint32_t n, uint16_t bs) { s_blocks = n; s_bsize = bs; js_event(12, (int)n, bs, s_pid); return true; }
void USBMSC::end() { s_media = false; js_event(13, 0, 0, ""); }
void USBMSC::vendorID(const char *v) { strncpy(s_vid, v, sizeof s_vid - 1); }
void USBMSC::productID(const char *p) { strncpy(s_pid, p, sizeof s_pid - 1); }
void USBMSC::productRevision(const char *r) { strncpy(s_rev, r, sizeof s_rev - 1); js_event(14, 0, 0, s_rev); }
void USBMSC::onStartStop(msc_start_stop_cb cb) { s_ss = cb; }
void USBMSC::onRead(msc_read_cb cb) { s_rd = cb; }
void USBMSC::onWrite(msc_write_cb cb) { s_wr = cb; }
void USBMSC::mediaPresent(bool m) { s_media = m; js_event(15, m ? 1 : 0, 0, s_pid); }
void USBMSC::isWritable(bool) {}
extern "C" {
__attribute__((export_name("sim_msc_state"))) int sim_msc_state(void) { return (s_usb_begun ? 1 : 0) | (s_usb_conn ? 2 : 0) | (s_media ? 4 : 0); }
__attribute__((export_name("sim_msc_blocks"))) uint32_t sim_msc_blocks(void) { return s_blocks; }
__attribute__((export_name("sim_msc_read"))) int32_t sim_msc_read(uint32_t lba, void *buf, uint32_t n) { return s_rd ? s_rd(lba, 0, buf, n) : -1; }
__attribute__((export_name("sim_msc_write"))) int32_t sim_msc_write(uint32_t lba, uint8_t *buf, uint32_t n) { return s_wr ? s_wr(lba, 0, buf, n) : -1; }
__attribute__((export_name("sim_msc_eject"))) int sim_msc_eject(void) { return s_ss ? (s_ss(0, false, true) ? 1 : 0) : 0; }
__attribute__((export_name("sim_malloc"))) void *sim_malloc(uint32_t n) { return malloc(n); }
__attribute__((export_name("sim_free"))) void sim_free(void *p) { free(p); }
void *__real_malloc(size_t);
__attribute__((export_name("sim_malloc_raw"))) void *sim_malloc_raw(uint32_t n) { return __real_malloc(n); }   // the simulator's own buffers: not counted as firmware PSRAM
}

// ── Update ──
UpdateClass Update;
bool UpdateClass::begin(size_t size, int, int, uint8_t, const char *) { _size = size; _done = 0; _err = 0; js_event(20, (int)size, 0, ""); return true; }
size_t UpdateClass::write(uint8_t *, size_t len) { _done += len; return len; }
bool UpdateClass::end(bool) { js_event(21, (int)_done, 0, ""); return _err == 0; }
const char *UpdateClass::errorString() { return _err ? "Aborted" : "No Error"; }
