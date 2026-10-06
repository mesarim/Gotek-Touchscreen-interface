// p4_display.c — JD9165 1024x600 MIPI-DSI backend for ESP32-P4 (Guition JC1060P470C_I_W_Y, 7").
// Panel profile from Guition's own Arduino demo (JC1060P470C_I_W_Y_New_Panel / _Old_Panel, lvgl_demo_v8):
//   JD9165, 1024x600, 2 DSI lanes @ 750 Mbps, DPI 52 MHz (60 Hz), porches h 24/136/160 (pw/bp/fp) v 2/21/12,
//   RESET GPIO5, BACKLIGHT GPIO23 (MP3202 EN, active-high), DSI-PHY power = internal LDO channel 3 @ 2.5 V.
// The init table for the NEW panel is the driver's default (esp_lcd_jd9165.c, Apache-2.0, from the demo);
// the OLD panel's table is below. present() = the round port's blocked turn, with the canvas filling the panel.
#include "p4_display.h"
#include <string.h>
#include "driver/gpio.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_jd9165.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_attr.h"

#define PIN_LCD_RST   5
#define PIN_LCD_BL    23          // ACTIVE-HIGH (MP3202 boost enable)
#define DSI_LDO_CHAN  3
#define DSI_LDO_MV    2500

static esp_lcd_panel_handle_t    s_panel = NULL;
static esp_lcd_panel_io_handle_t s_io    = NULL;
static esp_lcd_dsi_bus_handle_t  s_dsi   = NULL;
static esp_ldo_channel_handle_t  s_ldo   = NULL;
static uint16_t* s_fb[2] = { NULL, NULL };
static int       s_front = 0;           // frame buffer on screen now; we draw into the other one
static int       s_turn  = 0;           // 0 or 2 (PANELTURN=180)
static SemaphoreHandle_t s_vsync = NULL;

#if JC1060_OLD_PANEL
// Guition JC1060P470C_I_W_Y_Old_Panel/lvgl_demo_v8/src/lcd/esp_lcd_jd9165.c (no "V2" on the back label)
static const jd9165_lcd_init_cmd_t s_old_panel_init[] = {
//  {cmd, { data }, data_size, delay_ms}
    {0x30, (uint8_t[]){0x00}, 1, 0},
    {0xF7, (uint8_t[]){0x49,0x61,0x02,0x00}, 4, 0},
    {0x30, (uint8_t[]){0x01}, 1, 0},
    {0x04, (uint8_t[]){0x0C}, 1, 0},
    {0x05, (uint8_t[]){0x00}, 1, 0},
    {0x06, (uint8_t[]){0x00}, 1, 0},
    {0x0B, (uint8_t[]){0x11}, 1, 0},
    {0x17, (uint8_t[]){0x00}, 1, 0},
    {0x20, (uint8_t[]){0x04}, 1, 0},
    {0x1F, (uint8_t[]){0x05}, 1, 0},
    {0x23, (uint8_t[]){0x00}, 1, 0},
    {0x25, (uint8_t[]){0x19}, 1, 0},
    {0x28, (uint8_t[]){0x18}, 1, 0},
    {0x29, (uint8_t[]){0x04}, 1, 0},
    {0x2A, (uint8_t[]){0x01}, 1, 0},
    {0x2B, (uint8_t[]){0x04}, 1, 0},
    {0x2C, (uint8_t[]){0x01}, 1, 0},
    {0x30, (uint8_t[]){0x02}, 1, 0},
    {0x01, (uint8_t[]){0x22}, 1, 0},
    {0x03, (uint8_t[]){0x12}, 1, 0},
    {0x04, (uint8_t[]){0x00}, 1, 0},
    {0x05, (uint8_t[]){0x64}, 1, 0},
    {0x0A, (uint8_t[]){0x08}, 1, 0},
    {0x0B, (uint8_t[]){0x0A,0x1A,0x0B,0x0D,0x0D,0x11,0x10,0x06,0x08,0x1F,0x1D}, 11, 0},
    {0x0C, (uint8_t[]){0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D}, 11, 0},
    {0x0D, (uint8_t[]){0x16,0x1B,0x0B,0x0D,0x0D,0x11,0x10,0x07,0x09,0x1E,0x1C}, 11, 0},
    {0x0E, (uint8_t[]){0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D}, 11, 0},
    {0x0F, (uint8_t[]){0x16,0x1B,0x0D,0x0B,0x0D,0x11,0x10,0x1C,0x1E,0x09,0x07}, 11, 0},
    {0x10, (uint8_t[]){0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D}, 11, 0},
    {0x11, (uint8_t[]){0x0A,0x1A,0x0D,0x0B,0x0D,0x11,0x10,0x1D,0x1F,0x08,0x06}, 11, 0},
    {0x12, (uint8_t[]){0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D}, 11, 0},
    {0x14, (uint8_t[]){0x00,0x00,0x11,0x11}, 4, 0},
    {0x18, (uint8_t[]){0x99}, 1, 0},
    {0x30, (uint8_t[]){0x06}, 1, 0},
    {0x12, (uint8_t[]){0x36,0x2C,0x2E,0x3C,0x38,0x35,0x35,0x32,0x2E,0x1D,0x2B,0x21,0x16,0x29}, 14, 0},
    {0x13, (uint8_t[]){0x36,0x2C,0x2E,0x3C,0x38,0x35,0x35,0x32,0x2E,0x1D,0x2B,0x21,0x16,0x29}, 14, 0},
    
    // {0x30, (uint8_t[]){0x08}, 1, 0},
    // {0x05, (uint8_t[]){0x01}, 1, 0},
    // {0x0C, (uint8_t[]){0x1A}, 1, 0},
    // {0x0D, (uint8_t[]){0x0E}, 1, 0},

    // {0x30, (uint8_t[]){0x07}, 1, 0},
    // {0x01, (uint8_t[]){0x04}, 1, 0},

    {0x30, (uint8_t[]){0x0A}, 1, 0},
    {0x02, (uint8_t[]){0x4F}, 1, 0},
    {0x0B, (uint8_t[]){0x40}, 1, 0},
    {0x12, (uint8_t[]){0x3E}, 1, 0},
    {0x13, (uint8_t[]){0x78}, 1, 0},
    {0x30, (uint8_t[]){0x0D}, 1, 0},
    {0x0D, (uint8_t[]){0x04}, 1, 0},
    {0x10, (uint8_t[]){0x0C}, 1, 0},
    {0x11, (uint8_t[]){0x0C}, 1, 0},
    {0x12, (uint8_t[]){0x0C}, 1, 0},
    {0x13, (uint8_t[]){0x0C}, 1, 0},
    {0x30, (uint8_t[]){0x00}, 1, 0},

    {0X3A, (uint8_t[]){0x55}, 1, 0},
    {0x11, (uint8_t[]){0x00}, 1, 120},
    {0x29, (uint8_t[]){0x00}, 1, 20},
};

#endif

// End of a refresh = the panel has started scanning the buffer we flipped to, so the old front is free.
static bool IRAM_ATTR dpi_refresh_done(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx)
{
    BaseType_t hp = pdFALSE;
    if (s_vsync) xSemaphoreGiveFromISR(s_vsync, &hp);
    return hp == pdTRUE;
}

bool p4disp_init(void)
{
    esp_ldo_channel_config_t ldocfg = { .chan_id = DSI_LDO_CHAN, .voltage_mv = DSI_LDO_MV };
    if (esp_ldo_acquire_channel(&ldocfg, &s_ldo) != ESP_OK) return false;

    esp_lcd_dsi_bus_config_t busc = JD9165_PANEL_BUS_DSI_2CH_CONFIG();
    if (esp_lcd_new_dsi_bus(&busc, &s_dsi) != ESP_OK) return false;

    esp_lcd_dbi_io_config_t dbic = JD9165_PANEL_IO_DBI_CONFIG();
    if (esp_lcd_new_panel_io_dbi(s_dsi, &dbic, &s_io) != ESP_OK) return false;

    esp_lcd_dpi_panel_config_t dpic;
    memset(&dpic, 0, sizeof dpic);
    dpic.dpi_clk_src        = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
    dpic.dpi_clock_freq_mhz = 52;
    dpic.virtual_channel    = 0;
    dpic.pixel_format       = LCD_COLOR_PIXEL_FORMAT_RGB565;
    dpic.num_fbs            = 2;                       // double buffer -> page flip (the demo uses 1 + DMA2D)
    dpic.video_timing.h_size = P4DISP_PANEL_W;
    dpic.video_timing.v_size = P4DISP_PANEL_H;
    dpic.video_timing.hsync_pulse_width = 24;
    dpic.video_timing.hsync_back_porch  = 136;
    dpic.video_timing.hsync_front_porch = 160;
    dpic.video_timing.vsync_pulse_width = 2;
    dpic.video_timing.vsync_back_porch  = 21;
    dpic.video_timing.vsync_front_porch = 12;

    jd9165_vendor_config_t vcfg;
    memset(&vcfg, 0, sizeof vcfg);
#if JC1060_OLD_PANEL
    vcfg.init_cmds      = s_old_panel_init;
    vcfg.init_cmds_size = sizeof(s_old_panel_init) / sizeof(s_old_panel_init[0]);
#endif                                                 // NEW panel: NULL = the driver's default table
    vcfg.mipi_config.dsi_bus    = s_dsi;
    vcfg.mipi_config.dpi_config = &dpic;

    esp_lcd_panel_dev_config_t pcfg;
    memset(&pcfg, 0, sizeof pcfg);
    pcfg.reset_gpio_num = PIN_LCD_RST;
    pcfg.rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB;
    pcfg.bits_per_pixel = 16;                          // RGB565 (the GTi composes 16-bit)
    pcfg.vendor_config  = &vcfg;

    if (esp_lcd_new_panel_jd9165(s_io, &pcfg, &s_panel) != ESP_OK) return false;
    if (esp_lcd_panel_reset(s_panel) != ESP_OK) return false;
    if (esp_lcd_panel_init(s_panel)  != ESP_OK) return false;

    void *f0 = NULL, *f1 = NULL;
    if (esp_lcd_dpi_panel_get_frame_buffer(s_panel, 2, &f0, &f1) != ESP_OK || !f0 || !f1) return false;
    s_fb[0] = (uint16_t*)f0; s_fb[1] = (uint16_t*)f1;

    s_vsync = xSemaphoreCreateBinary();
    esp_lcd_dpi_panel_event_callbacks_t cbs;
    memset(&cbs, 0, sizeof cbs);
    cbs.on_frame_buf_complete = dpi_refresh_done;   // same callback slot (a union with the deprecated on_refresh_done)
    esp_lcd_dpi_panel_register_event_callbacks(s_panel, &cbs, NULL);

    memset(s_fb[1], 0, (size_t)P4DISP_PANEL_W * P4DISP_PANEL_H * 2);
    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, P4DISP_PANEL_W, P4DISP_PANEL_H, s_fb[1]);
    memset(s_fb[0], 0, (size_t)P4DISP_PANEL_W * P4DISP_PANEL_H * 2);
    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, P4DISP_PANEL_W, P4DISP_PANEL_H, s_fb[0]);
    s_front = 0;
    return true;
}

// Compose pixel (px,py) -> panel index. The P4 composes portrait (rotation 0 = landscape canvas vx = CH-1-py,
// vy = px), so the upright point on the panel is X = CH-1-py, Y = px; PANELTURN=180 flips both.
static long panelIndex(int px, int py)
{
    int X = P4DISP_OX + (P4DISP_CH - 1 - py), Y = P4DISP_OY + px;
    if (s_turn == 2) { X = P4DISP_PANEL_W - 1 - X; Y = P4DISP_PANEL_H - 1 - Y; }
    return (long)Y * P4DISP_PANEL_W + X;
}

static void flip(int back)
{
    uint16_t* dst = s_fb[back];
    if (s_vsync) xSemaphoreTake(s_vsync, 0);          // drop a stale "refresh done"
    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, P4DISP_PANEL_W, P4DISP_PANEL_H, dst);   // own buffer = cache write-back + flip
    if (s_vsync) xSemaphoreTake(s_vsync, pdMS_TO_TICKS(40));   // wait for the flip (safety timeout: liveness over a tear)
    s_front = back;
}

void p4disp_present(const uint16_t* src)
{
    if (!s_panel || !src || !s_fb[0]) return;
    int back = s_front ^ 1;
    uint16_t* dst = s_fb[back];
    const long c0 = panelIndex(0, 0), a = panelIndex(1, 0) - c0, b = panelIndex(0, 1) - c0;
    // Blocked copy (32x32 tiles): the destination walks a column when the source walks a row, so tiles keep the
    // written cache lines hot instead of touching a new line for every pixel.
    for (int py0 = 0; py0 < P4DISP_CH; py0 += 32) {
        const int py1 = (py0 + 32 < P4DISP_CH) ? py0 + 32 : P4DISP_CH;
        for (int px0 = 0; px0 < P4DISP_CW; px0 += 32) {
            const int px1 = (px0 + 32 < P4DISP_CW) ? px0 + 32 : P4DISP_CW;
            for (int py = py0; py < py1; py++) {
                const uint16_t* s = src + (long)py * P4DISP_CW + px0;
                long d = c0 + (long)px0 * a + (long)py * b;
                for (int px = px0; px < px1; px++) { dst[d] = *s++; d += a; }
            }
        }
    }
    flip(back);
}

void p4disp_set_turn(int quarter)
{
    s_turn = (((quarter % 4) + 4) % 4) == 2 ? 2 : 0;   // only 0 / 180 make sense on a landscape panel
}

bool p4disp_touch_to_compose(int rx, int ry, int* cx, int* cy)
{
    int X = rx, Y = ry;
    if (s_turn == 2) { X = P4DISP_PANEL_W - 1 - rx; Y = P4DISP_PANEL_H - 1 - ry; }
    int px = Y - P4DISP_OY, py = (P4DISP_CH - 1) - (X - P4DISP_OX);
    if (px < 0 || px >= P4DISP_CW || py < 0 || py >= P4DISP_CH) return false;
    *cx = px; *cy = py;
    return true;
}

void p4disp_backlight(bool on)
{
    gpio_config_t io;
    memset(&io, 0, sizeof io);
    io.pin_bit_mask = 1ULL << PIN_LCD_BL;
    io.mode = GPIO_MODE_OUTPUT;
    gpio_config(&io);
    gpio_set_level(PIN_LCD_BL, on ? 1 : 0);           // ACTIVE-HIGH
}
