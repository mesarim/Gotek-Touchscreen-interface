// p4_display.c — EK79007 1024x600 MIPI-DSI backend for ESP32-P4
// (Waveshare ESP32-P4-WIFI6-Touch-LCD-7B).
// Panel profile from waveshare/esp32_p4_wifi6_touch_lcd_7b BSP + Espressif's
// esp_lcd_ek79007 v2.0.2:
//   EK79007, 1024x600, 2 DSI lanes @ 1000 Mbps, DPI 52 MHz (60 Hz),
//   porches h 10/160/160 (pw/bp/fp) v 1/23/12, RESET GPIO33, BACKLIGHT GPIO32
//   (ACTIVE-LOW), DSI-PHY power = internal LDO channel 3 @ 2.5 V.
// Same shape as the 4.3" ST7701 backend — only the vendor driver + geometry differ.
#include "p4_display.h"
#include <string.h>
#include "driver/gpio.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_ek79007.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_attr.h"          // IRAM_ATTR

#define PIN_LCD_RST   33
#define PIN_LCD_BL    32          // 7B backlight is ACTIVE-LOW
#define DSI_LDO_CHAN  3
#define DSI_LDO_MV    2500
#define DSI_LANE_RATE 1000        // Waveshare 7B board profile (driver default macro is 900)

static esp_lcd_panel_handle_t    s_panel = NULL;
static esp_lcd_panel_io_handle_t s_io    = NULL;
static esp_lcd_dsi_bus_handle_t  s_dsi    = NULL;
static esp_ldo_channel_handle_t  s_ldo    = NULL;

// ── Tear fix ──────────────────────────────────────────────────────────────────
// GTi composes into one external PSRAM buffer and hands it to draw_bitmap every
// frame. On a DPI panel draw_bitmap kicks off an async DMA copy (external buffer
// -> internal frame buffer) and returns immediately (~0.4ms). If GTi starts
// drawing the next frame before that copy finishes, the DMA reads a buffer that's
// being overwritten -> a tear/corruption line that only shows while the screen is
// changing fast (scrolling). Waiting on the copy-done event makes present block
// until the buffer is safe to reuse, which removes the race. Costs ~the copy time
// per frame (a few ms) instead of tearing.
static SemaphoreHandle_t s_trans_done = NULL;

static bool IRAM_ATTR dpi_color_trans_done(esp_lcd_panel_handle_t panel,
                                           esp_lcd_dpi_panel_event_data_t *edata,
                                           void *user_ctx)
{
    BaseType_t hp = pdFALSE;
    if (s_trans_done) xSemaphoreGiveFromISR(s_trans_done, &hp);
    return hp == pdTRUE;
}

bool p4disp_init(void)
{
    esp_ldo_channel_config_t ldocfg = { .chan_id = DSI_LDO_CHAN, .voltage_mv = DSI_LDO_MV };
    if (esp_ldo_acquire_channel(&ldocfg, &s_ldo) != ESP_OK) return false;

    esp_lcd_dsi_bus_config_t busc = EK79007_PANEL_BUS_DSI_2CH_CONFIG();
    busc.lane_bit_rate_mbps = DSI_LANE_RATE;
    if (esp_lcd_new_dsi_bus(&busc, &s_dsi) != ESP_OK) return false;

    esp_lcd_dbi_io_config_t dbic = EK79007_PANEL_IO_DBI_CONFIG();
    if (esp_lcd_new_panel_io_dbi(s_dsi, &dbic, &s_io) != ESP_OK) return false;

    esp_lcd_dpi_panel_config_t dpic = EK79007_1024_600_PANEL_60HZ_CONFIG(LCD_COLOR_PIXEL_FORMAT_RGB565);
    dpic.num_fbs = 2;                       // double buffer -> page-flip

    ek79007_vendor_config_t vcfg = { 0 };
    vcfg.mipi_config.dsi_bus    = s_dsi;
    vcfg.mipi_config.dpi_config = &dpic;
    vcfg.mipi_config.lane_num   = 2;

    esp_lcd_panel_dev_config_t pcfg = { 0 };
    pcfg.reset_gpio_num = PIN_LCD_RST;
    pcfg.rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB;
    pcfg.bits_per_pixel = 16;               // RGB565 (GTi composes 16-bit)
    pcfg.vendor_config  = &vcfg;

    if (esp_lcd_new_panel_ek79007(s_io, &pcfg, &s_panel) != ESP_OK) return false;
    if (esp_lcd_panel_reset(s_panel) != ESP_OK) return false;
    if (esp_lcd_panel_init(s_panel)  != ESP_OK) return false;

    // Register the copy-done callback so p4disp_present can wait for it (tear fix).
    s_trans_done = xSemaphoreCreateBinary();
    esp_lcd_dpi_panel_event_callbacks_t cbs = { .on_color_trans_done = dpi_color_trans_done };
    esp_lcd_dpi_panel_register_event_callbacks(s_panel, &cbs, NULL);
    return true;
}

uint16_t* p4disp_framebuffer(int idx)
{
    void* fb = NULL;
    if (!s_panel) return NULL;
    if (esp_lcd_dpi_panel_get_frame_buffer(s_panel, idx + 1, &fb) != ESP_OK) return NULL;
    return (uint16_t*)fb;
}

void p4disp_present(const uint16_t* fb)
{
    if (!s_panel || !fb) return;
    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, P4DISP_NATIVE_W, P4DISP_NATIVE_H, (void*)fb);
    // Block until the DMA has finished reading `fb` (see tear-fix note above). The
    // timeout is a safety net: if the event never arrives we fall through rather
    // than hang, trading a possible tear for liveness.
    if (s_trans_done) xSemaphoreTake(s_trans_done, pdMS_TO_TICKS(100));
}

void p4disp_backlight(bool on)
{
    gpio_config_t io = { 0 };
    io.pin_bit_mask = 1ULL << PIN_LCD_BL;
    io.mode = GPIO_MODE_OUTPUT;
    gpio_config(&io);
    gpio_set_level(PIN_LCD_BL, on ? 0 : 1);   // ACTIVE-LOW: drive 0 = backlight ON
}
