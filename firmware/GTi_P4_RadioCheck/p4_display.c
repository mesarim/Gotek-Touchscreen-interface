// p4_display.c — ST7701 480x800 MIPI-DSI backend for ESP32-P4 (Guition JC4880P443C).
// Proven config values from the working 60 FPS benchmark (see project recipe doc):
//   2 DSI lanes @ 500 Mbps, DPI 34 MHz, porches h 12/42/42 v 2/8/166,
//   RESET GPIO5, BACKLIGHT GPIO23, DSI-PHY power = internal LDO channel 3 @ 2.5 V.
#include "p4_display.h"
#include <string.h>
#include "driver/gpio.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7701.h"

#define PIN_LCD_RST   5
#define PIN_LCD_BL    23
#define DSI_LDO_CHAN  3
#define DSI_LDO_MV    2500

static esp_lcd_panel_handle_t   s_panel = NULL;
static esp_lcd_panel_io_handle_t s_io   = NULL;
static esp_lcd_dsi_bus_handle_t  s_dsi  = NULL;
static esp_ldo_channel_handle_t  s_ldo  = NULL;

bool p4disp_init(void)
{
    esp_ldo_channel_config_t ldocfg = { .chan_id = DSI_LDO_CHAN, .voltage_mv = DSI_LDO_MV };
    if (esp_ldo_acquire_channel(&ldocfg, &s_ldo) != ESP_OK) return false;

    esp_lcd_dsi_bus_config_t busc = ST7701_PANEL_BUS_DSI_2CH_CONFIG();
    if (esp_lcd_new_dsi_bus(&busc, &s_dsi) != ESP_OK) return false;

    esp_lcd_dbi_io_config_t dbic = ST7701_PANEL_IO_DBI_CONFIG();
    if (esp_lcd_new_panel_io_dbi(s_dsi, &dbic, &s_io) != ESP_OK) return false;

    esp_lcd_dpi_panel_config_t dpic = ST7701_480_360_PANEL_60HZ_DPI_CONFIG(LCD_COLOR_PIXEL_FORMAT_RGB565);
    dpic.num_fbs = 2;                       // double buffer -> page-flip

    st7701_vendor_config_t vcfg = { 0 };
    vcfg.mipi_config.dsi_bus    = s_dsi;
    vcfg.mipi_config.dpi_config = &dpic;
    vcfg.flags.use_mipi_interface = 1;

    esp_lcd_panel_dev_config_t pcfg = { 0 };
    pcfg.reset_gpio_num = PIN_LCD_RST;
    pcfg.rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB;
    pcfg.bits_per_pixel = 16;
    pcfg.vendor_config  = &vcfg;

    if (esp_lcd_new_panel_st7701(s_io, &pcfg, &s_panel) != ESP_OK) return false;
    if (esp_lcd_panel_reset(s_panel) != ESP_OK) return false;
    if (esp_lcd_panel_init(s_panel)  != ESP_OK) return false;
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
}

// 90-degree rotation: compose is 800 wide x 480 tall (landscape); dst is 480x800.
// CW: dst(x=479-cy, y=cx). CCW: dst(x=cy, y=799-cx).
void p4disp_present_rotated(const uint16_t* src, uint16_t* dst, int ccw)
{
    if (!dst || !src) return;
    const int SW = 800, SH = 480;           // source (compose) dims
    for (int cy = 0; cy < SH; cy++) {
        const uint16_t* srow = src + (size_t)cy * SW;
        for (int cx = 0; cx < SW; cx++) {
            int dx, dy;
            if (!ccw) { dx = (SH - 1 - cy); dy = cx; }
            else      { dx = cy;            dy = (SW - 1 - cx); }
            dst[(size_t)dy * P4DISP_NATIVE_W + dx] = srow[cx];
        }
    }
    p4disp_present(dst);
}

void p4disp_backlight(bool on)
{
    gpio_config_t io = { 0 };
    io.pin_bit_mask = 1ULL << PIN_LCD_BL;
    io.mode = GPIO_MODE_OUTPUT;
    gpio_config(&io);
    gpio_set_level(PIN_LCD_BL, on ? 1 : 0);
}
