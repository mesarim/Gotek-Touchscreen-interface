// p4_display.c — JD9365 800x800 ROUND MIPI-DSI backend for ESP32-P4
// (Waveshare ESP32-P4-WIFI6-Touch-LCD-3.4C).
// Panel profile from Waveshare's BSP waveshare/esp32_p4_wifi6_touch_lcd_xc v3.0.1 (3.4" choice) and their
// Arduino example displays_config.h (identical 197-command init list, checked 30 Sep):
//   JD9365, 800x800, 2 DSI lanes @ 1500 Mbps, DPI 80 MHz (~108 Hz), porches h 20/20/40 (pw/bp/fp)
//   v 4/12/24, RESET GPIO27, BACKLIGHT GPIO26 (ACTIVE-LOW), DSI-PHY power = internal LDO channel 3 @ 2.5 V.
// Same shape as the 7B's EK79007 backend; the difference is present(): the GTi's portrait compose buffer
// is turned upright into the middle of the square panel instead of being handed over whole.
#include "p4_display.h"
#include <string.h>
#include "driver/gpio.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_jd9365.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_attr.h"

#define PIN_LCD_RST   27
#define PIN_LCD_BL    26          // ACTIVE-LOW
#define DSI_LDO_CHAN  3
#define DSI_LDO_MV    2500
#define DSI_LANE_RATE 1500
#define DPI_CLK_MHZ   80

static esp_lcd_panel_handle_t    s_panel = NULL;
static esp_lcd_panel_io_handle_t s_io    = NULL;
static esp_lcd_dsi_bus_handle_t  s_dsi   = NULL;
static esp_ldo_channel_handle_t  s_ldo   = NULL;
static uint16_t* s_fb[2] = { NULL, NULL };
static int       s_front = 0;           // frame buffer on screen now; we draw into the other one
static int       s_turn  = 0;           // extra quarter turns (p4disp_set_turn)
static SemaphoreHandle_t s_vsync = NULL;
static bool      s_ring_dirty[2] = { false, false };   // P4R-2b: a full round screen went into this buffer - the ring outside
                                                        // the square canvas must be cleared before a square frame uses it

static const jd9365_lcd_init_cmd_t s_init_cmds[] = {
    {0xE0, (uint8_t[]){0x00}, 1, 0},

    {0xE1, (uint8_t[]){0x93}, 1, 0},
    {0xE2, (uint8_t[]){0x65}, 1, 0},
    {0xE3, (uint8_t[]){0xF8}, 1, 0},
    {0x80, (uint8_t[]){0x01}, 1, 0},

    {0xE0, (uint8_t[]){0x01}, 1, 0},

    {0x00, (uint8_t[]){0x00}, 1, 0},
    {0x01, (uint8_t[]){0x41}, 1, 0},
    {0x03, (uint8_t[]){0x10}, 1, 0},
    {0x04, (uint8_t[]){0x44}, 1, 0},

    {0x17, (uint8_t[]){0x00}, 1, 0},
    {0x18, (uint8_t[]){0xD0}, 1, 0},
    {0x19, (uint8_t[]){0x00}, 1, 0},
    {0x1A, (uint8_t[]){0x00}, 1, 0},
    {0x1B, (uint8_t[]){0xD0}, 1, 0},
    {0x1C, (uint8_t[]){0x00}, 1, 0},

    {0x24, (uint8_t[]){0xFE}, 1, 0},
    {0x35, (uint8_t[]){0x26}, 1, 0},

    {0x37, (uint8_t[]){0x09}, 1, 0},

    {0x38, (uint8_t[]){0x04}, 1, 0},
    {0x39, (uint8_t[]){0x08}, 1, 0},
    {0x3A, (uint8_t[]){0x0A}, 1, 0},
    {0x3C, (uint8_t[]){0x78}, 1, 0},
    {0x3D, (uint8_t[]){0xFF}, 1, 0},
    {0x3E, (uint8_t[]){0xFF}, 1, 0},
    {0x3F, (uint8_t[]){0xFF}, 1, 0},

    {0x40, (uint8_t[]){0x00}, 1, 0},
    {0x41, (uint8_t[]){0x64}, 1, 0},
    {0x42, (uint8_t[]){0xC7}, 1, 0},
    {0x43, (uint8_t[]){0x18}, 1, 0},
    {0x44, (uint8_t[]){0x0B}, 1, 0},
    {0x45, (uint8_t[]){0x14}, 1, 0},

    {0x55, (uint8_t[]){0x02}, 1, 0},
    {0x57, (uint8_t[]){0x49}, 1, 0},
    {0x59, (uint8_t[]){0x0A}, 1, 0},
    {0x5A, (uint8_t[]){0x1B}, 1, 0},
    {0x5B, (uint8_t[]){0x19}, 1, 0},

    {0x5D, (uint8_t[]){0x7F}, 1, 0},
    {0x5E, (uint8_t[]){0x56}, 1, 0},
    {0x5F, (uint8_t[]){0x43}, 1, 0},
    {0x60, (uint8_t[]){0x37}, 1, 0},
    {0x61, (uint8_t[]){0x33}, 1, 0},
    {0x62, (uint8_t[]){0x25}, 1, 0},
    {0x63, (uint8_t[]){0x2A}, 1, 0},
    {0x64, (uint8_t[]){0x16}, 1, 0},
    {0x65, (uint8_t[]){0x30}, 1, 0},
    {0x66, (uint8_t[]){0x2F}, 1, 0},
    {0x67, (uint8_t[]){0x32}, 1, 0},
    {0x68, (uint8_t[]){0x53}, 1, 0},
    {0x69, (uint8_t[]){0x43}, 1, 0},
    {0x6A, (uint8_t[]){0x4C}, 1, 0},
    {0x6B, (uint8_t[]){0x40}, 1, 0},
    {0x6C, (uint8_t[]){0x3D}, 1, 0},
    {0x6D, (uint8_t[]){0x31}, 1, 0},
    {0x6E, (uint8_t[]){0x20}, 1, 0},
    {0x6F, (uint8_t[]){0x0F}, 1, 0},

    {0x70, (uint8_t[]){0x7F}, 1, 0},
    {0x71, (uint8_t[]){0x56}, 1, 0},
    {0x72, (uint8_t[]){0x43}, 1, 0},
    {0x73, (uint8_t[]){0x37}, 1, 0},
    {0x74, (uint8_t[]){0x33}, 1, 0},
    {0x75, (uint8_t[]){0x25}, 1, 0},
    {0x76, (uint8_t[]){0x2A}, 1, 0},
    {0x77, (uint8_t[]){0x16}, 1, 0},
    {0x78, (uint8_t[]){0x30}, 1, 0},
    {0x79, (uint8_t[]){0x2F}, 1, 0},
    {0x7A, (uint8_t[]){0x32}, 1, 0},
    {0x7B, (uint8_t[]){0x53}, 1, 0},
    {0x7C, (uint8_t[]){0x43}, 1, 0},
    {0x7D, (uint8_t[]){0x4C}, 1, 0},
    {0x7E, (uint8_t[]){0x40}, 1, 0},
    {0x7F, (uint8_t[]){0x3D}, 1, 0},
    {0x80, (uint8_t[]){0x31}, 1, 0},
    {0x81, (uint8_t[]){0x20}, 1, 0},
    {0x82, (uint8_t[]){0x0F}, 1, 0},

    {0xE0, (uint8_t[]){0x02}, 1, 0},
    {0x00, (uint8_t[]){0x5F}, 1, 0},
    {0x01, (uint8_t[]){0x5F}, 1, 0},
    {0x02, (uint8_t[]){0x5E}, 1, 0},
    {0x03, (uint8_t[]){0x5E}, 1, 0},
    {0x04, (uint8_t[]){0x50}, 1, 0},
    {0x05, (uint8_t[]){0x48}, 1, 0},
    {0x06, (uint8_t[]){0x48}, 1, 0},
    {0x07, (uint8_t[]){0x4A}, 1, 0},
    {0x08, (uint8_t[]){0x4A}, 1, 0},
    {0x09, (uint8_t[]){0x44}, 1, 0},
    {0x0A, (uint8_t[]){0x44}, 1, 0},
    {0x0B, (uint8_t[]){0x46}, 1, 0},
    {0x0C, (uint8_t[]){0x46}, 1, 0},
    {0x0D, (uint8_t[]){0x5F}, 1, 0},
    {0x0E, (uint8_t[]){0x5F}, 1, 0},
    {0x0F, (uint8_t[]){0x57}, 1, 0},
    {0x10, (uint8_t[]){0x57}, 1, 0},
    {0x11, (uint8_t[]){0x77}, 1, 0},
    {0x12, (uint8_t[]){0x77}, 1, 0},
    {0x13, (uint8_t[]){0x40}, 1, 0},
    {0x14, (uint8_t[]){0x42}, 1, 0},
    {0x15, (uint8_t[]){0x5F}, 1, 0},

    {0x16, (uint8_t[]){0x5F}, 1, 0},
    {0x17, (uint8_t[]){0x5F}, 1, 0},
    {0x18, (uint8_t[]){0x5E}, 1, 0},
    {0x19, (uint8_t[]){0x5E}, 1, 0},
    {0x1A, (uint8_t[]){0x50}, 1, 0},
    {0x1B, (uint8_t[]){0x49}, 1, 0},
    {0x1C, (uint8_t[]){0x49}, 1, 0},
    {0x1D, (uint8_t[]){0x4B}, 1, 0},
    {0x1E, (uint8_t[]){0x4B}, 1, 0},
    {0x1F, (uint8_t[]){0x45}, 1, 0},
    {0x20, (uint8_t[]){0x45}, 1, 0},
    {0x21, (uint8_t[]){0x47}, 1, 0},
    {0x22, (uint8_t[]){0x47}, 1, 0},
    {0x23, (uint8_t[]){0x5F}, 1, 0},
    {0x24, (uint8_t[]){0x5F}, 1, 0},
    {0x25, (uint8_t[]){0x57}, 1, 0},
    {0x26, (uint8_t[]){0x57}, 1, 0},
    {0x27, (uint8_t[]){0x77}, 1, 0},
    {0x28, (uint8_t[]){0x77}, 1, 0},
    {0x29, (uint8_t[]){0x41}, 1, 0},
    {0x2A, (uint8_t[]){0x43}, 1, 0},
    {0x2B, (uint8_t[]){0x5F}, 1, 0},

    {0x2C, (uint8_t[]){0x1E}, 1, 0},
    {0x2D, (uint8_t[]){0x1E}, 1, 0},
    {0x2E, (uint8_t[]){0x1F}, 1, 0},
    {0x2F, (uint8_t[]){0x1F}, 1, 0},
    {0x30, (uint8_t[]){0x10}, 1, 0},
    {0x31, (uint8_t[]){0x07}, 1, 0},
    {0x32, (uint8_t[]){0x07}, 1, 0},
    {0x33, (uint8_t[]){0x05}, 1, 0},
    {0x34, (uint8_t[]){0x05}, 1, 0},
    {0x35, (uint8_t[]){0x0B}, 1, 0},
    {0x36, (uint8_t[]){0x0B}, 1, 0},
    {0x37, (uint8_t[]){0x09}, 1, 0},
    {0x38, (uint8_t[]){0x09}, 1, 0},
    {0x39, (uint8_t[]){0x1F}, 1, 0},
    {0x3A, (uint8_t[]){0x1F}, 1, 0},
    {0x3B, (uint8_t[]){0x17}, 1, 0},
    {0x3C, (uint8_t[]){0x17}, 1, 0},
    {0x3D, (uint8_t[]){0x17}, 1, 0},
    {0x3E, (uint8_t[]){0x17}, 1, 0},
    {0x3F, (uint8_t[]){0x03}, 1, 0},
    {0x40, (uint8_t[]){0x01}, 1, 0},
    {0x41, (uint8_t[]){0x1F}, 1, 0},

    {0x42, (uint8_t[]){0x1E}, 1, 0},
    {0x43, (uint8_t[]){0x1E}, 1, 0},
    {0x44, (uint8_t[]){0x1F}, 1, 0},
    {0x45, (uint8_t[]){0x1F}, 1, 0},
    {0x46, (uint8_t[]){0x10}, 1, 0},
    {0x47, (uint8_t[]){0x06}, 1, 0},
    {0x48, (uint8_t[]){0x06}, 1, 0},
    {0x49, (uint8_t[]){0x04}, 1, 0},
    {0x4A, (uint8_t[]){0x04}, 1, 0},
    {0x4B, (uint8_t[]){0x0A}, 1, 0},
    {0x4C, (uint8_t[]){0x0A}, 1, 0},
    {0x4D, (uint8_t[]){0x08}, 1, 0},
    {0x4E, (uint8_t[]){0x08}, 1, 0},
    {0x4F, (uint8_t[]){0x1F}, 1, 0},
    {0x50, (uint8_t[]){0x1F}, 1, 0},
    {0x51, (uint8_t[]){0x17}, 1, 0},
    {0x52, (uint8_t[]){0x17}, 1, 0},
    {0x53, (uint8_t[]){0x17}, 1, 0},
    {0x54, (uint8_t[]){0x17}, 1, 0},
    {0x55, (uint8_t[]){0x02}, 1, 0},
    {0x56, (uint8_t[]){0x00}, 1, 0},
    {0x57, (uint8_t[]){0x1F}, 1, 0},

    {0xE0, (uint8_t[]){0x02}, 1, 0},
    {0x58, (uint8_t[]){0x40}, 1, 0},
    {0x59, (uint8_t[]){0x00}, 1, 0},
    {0x5A, (uint8_t[]){0x00}, 1, 0},
    {0x5B, (uint8_t[]){0x30}, 1, 0},
    {0x5C, (uint8_t[]){0x01}, 1, 0},
    {0x5D, (uint8_t[]){0x30}, 1, 0},
    {0x5E, (uint8_t[]){0x01}, 1, 0},
    {0x5F, (uint8_t[]){0x02}, 1, 0},
    {0x60, (uint8_t[]){0x30}, 1, 0},
    {0x61, (uint8_t[]){0x03}, 1, 0},
    {0x62, (uint8_t[]){0x04}, 1, 0},
    {0x63, (uint8_t[]){0x04}, 1, 0},
    {0x64, (uint8_t[]){0xA6}, 1, 0},
    {0x65, (uint8_t[]){0x43}, 1, 0},
    {0x66, (uint8_t[]){0x30}, 1, 0},
    {0x67, (uint8_t[]){0x73}, 1, 0},
    {0x68, (uint8_t[]){0x05}, 1, 0},
    {0x69, (uint8_t[]){0x04}, 1, 0},
    {0x6A, (uint8_t[]){0x7F}, 1, 0},
    {0x6B, (uint8_t[]){0x08}, 1, 0},
    {0x6C, (uint8_t[]){0x00}, 1, 0},
    {0x6D, (uint8_t[]){0x04}, 1, 0},
    {0x6E, (uint8_t[]){0x04}, 1, 0},
    {0x6F, (uint8_t[]){0x88}, 1, 0},

    {0x75, (uint8_t[]){0xD9}, 1, 0},
    {0x76, (uint8_t[]){0x00}, 1, 0},
    {0x77, (uint8_t[]){0x33}, 1, 0},
    {0x78, (uint8_t[]){0x43}, 1, 0},

    {0xE0, (uint8_t[]){0x00}, 1, 0},

    {0x11, (uint8_t[]){0x00}, 1, 120},

    {0x29, (uint8_t[]){0x00}, 1, 20},
    {0x35, (uint8_t[]){0x00}, 1, 0},
};

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

    esp_lcd_dsi_bus_config_t busc = JD9365_PANEL_BUS_DSI_2CH_CONFIG();
    busc.lane_bit_rate_mbps = DSI_LANE_RATE;
    if (esp_lcd_new_dsi_bus(&busc, &s_dsi) != ESP_OK) return false;

    esp_lcd_dbi_io_config_t dbic = JD9365_PANEL_IO_DBI_CONFIG();
    if (esp_lcd_new_panel_io_dbi(s_dsi, &dbic, &s_io) != ESP_OK) return false;

    esp_lcd_dpi_panel_config_t dpic;
    memset(&dpic, 0, sizeof dpic);
    dpic.dpi_clk_src        = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
    dpic.dpi_clock_freq_mhz = DPI_CLK_MHZ;
    dpic.virtual_channel    = 0;
    dpic.pixel_format       = LCD_COLOR_PIXEL_FORMAT_RGB565;
    dpic.num_fbs            = 2;                       // double buffer -> page flip
    dpic.video_timing.h_size = P4DISP_PANEL_W;
    dpic.video_timing.v_size = P4DISP_PANEL_H;
    dpic.video_timing.hsync_pulse_width = 20;
    dpic.video_timing.hsync_back_porch  = 20;
    dpic.video_timing.hsync_front_porch = 40;
    dpic.video_timing.vsync_pulse_width = 4;
    dpic.video_timing.vsync_back_porch  = 12;
    dpic.video_timing.vsync_front_porch = 24;

    jd9365_vendor_config_t vcfg;
    memset(&vcfg, 0, sizeof vcfg);
    vcfg.init_cmds      = s_init_cmds;
    vcfg.init_cmds_size = sizeof(s_init_cmds) / sizeof(s_init_cmds[0]);
    vcfg.mipi_config.dsi_bus    = s_dsi;
    vcfg.mipi_config.dpi_config = &dpic;
    vcfg.mipi_config.lane_num   = 2;

    esp_lcd_panel_dev_config_t pcfg;
    memset(&pcfg, 0, sizeof pcfg);
    pcfg.reset_gpio_num = PIN_LCD_RST;
    pcfg.rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB;
    pcfg.bits_per_pixel = 16;                          // RGB565 (the GTi composes 16-bit)
    pcfg.vendor_config  = &vcfg;

    if (esp_lcd_new_panel_jd9365(s_io, &pcfg, &s_panel) != ESP_OK) return false;
    if (esp_lcd_panel_reset(s_panel) != ESP_OK) return false;
    if (esp_lcd_panel_init(s_panel)  != ESP_OK) return false;

    void *f0 = NULL, *f1 = NULL;
    if (esp_lcd_dpi_panel_get_frame_buffer(s_panel, 2, &f0, &f1) != ESP_OK || !f0 || !f1) return false;
    s_fb[0] = (uint16_t*)f0; s_fb[1] = (uint16_t*)f1;

    s_vsync = xSemaphoreCreateBinary();
    esp_lcd_dpi_panel_event_callbacks_t cbs;
    memset(&cbs, 0, sizeof cbs);
    cbs.on_refresh_done = dpi_refresh_done;
    esp_lcd_dpi_panel_register_event_callbacks(s_panel, &cbs, NULL);

    // Both buffers black (the ring outside the canvas is never drawn again), handed to the driver so it
    // writes the cache back; buffer 0 ends up on screen.
    memset(s_fb[1], 0, (size_t)P4DISP_PANEL_W * P4DISP_PANEL_H * 2);
    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, P4DISP_PANEL_W, P4DISP_PANEL_H, s_fb[1]);
    memset(s_fb[0], 0, (size_t)P4DISP_PANEL_W * P4DISP_PANEL_H * 2);
    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, P4DISP_PANEL_W, P4DISP_PANEL_H, s_fb[0]);
    s_front = 0;
    return true;
}

// Compose pixel (px,py) -> panel index, as an affine map idx = c0 + px*a + py*b.
//   The P4 composes portrait (rotation 0 = landscape canvas vx = CH-1-py, vy = px), so the upright
//   landscape point on the panel is X = OX + CH-1-py, Y = OY + px; then the extra turn is applied.
static void turned(int X, int Y, int* xo, int* yo)
{
    const int W = P4DISP_PANEL_W - 1, H = P4DISP_PANEL_H - 1;
    switch (s_turn & 3) {
        case 1:  *xo = W - Y; *yo = X;     break;      // 90 clockwise
        case 2:  *xo = W - X; *yo = H - Y; break;      // 180
        case 3:  *xo = Y;     *yo = H - X; break;      // 270
        default: *xo = X;     *yo = Y;     break;
    }
}
static long panelIndex(int px, int py)
{
    int X = P4DISP_OX + (P4DISP_CH - 1 - py), Y = P4DISP_OY + px, xo, yo;
    turned(X, Y, &xo, &yo);
    return (long)yo * P4DISP_PANEL_W + xo;
}

// Hand the finished back buffer to the panel (cache write-back + page flip at the next frame) and wait for the flip.
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
    if (s_ring_dirty[back]) {                          // P4R-2b: the round reel left pixels outside the square canvas
        memset(dst, 0, (size_t)P4DISP_PANEL_W * P4DISP_PANEL_H * 2);
        s_ring_dirty[back] = false;
    }
    const long c0 = panelIndex(0, 0), a = panelIndex(1, 0) - c0, b = panelIndex(0, 1) - c0;
    // Blocked copy (32x32 tiles): the destination walks a column when the source walks a row, so tiles
    // keep the written cache lines hot instead of touching a new line for every pixel.
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

// P4R-2: full-screen present for the round screens. Turn 0 = a straight copy; other turns use the same affine walk.
static long turnedIndex(int X, int Y)
{
    int xo, yo; turned(X, Y, &xo, &yo);
    return (long)yo * P4DISP_PANEL_W + xo;
}
void p4disp_present_full(const uint16_t* src)
{
    if (!s_panel || !src || !s_fb[0]) return;
    int back = s_front ^ 1;
    uint16_t* dst = s_fb[back];
    s_ring_dirty[back] = true;                         // P4R-2b: the next square frame in this buffer clears it first
    if ((s_turn & 3) == 0) {
        memcpy(dst, src, (size_t)P4DISP_PANEL_W * P4DISP_PANEL_H * 2);
    } else {
        const long c0 = turnedIndex(0, 0), a = turnedIndex(1, 0) - c0, b = turnedIndex(0, 1) - c0;
        for (int y0 = 0; y0 < P4DISP_PANEL_H; y0 += 32)
            for (int x0 = 0; x0 < P4DISP_PANEL_W; x0 += 32)
                for (int y = y0; y < y0 + 32; y++) {
                    const uint16_t* s = src + (long)y * P4DISP_PANEL_W + x0;
                    long d = c0 + (long)x0 * a + (long)y * b;
                    for (int x = x0; x < x0 + 32; x++) { dst[d] = *s++; d += a; }
                }
    }
    flip(back);
}

void p4disp_touch_unturn(int rx, int ry, int* x, int* y)
{
    const int W = P4DISP_PANEL_W - 1, H = P4DISP_PANEL_H - 1;
    switch (s_turn & 3) {
        case 1:  *x = ry;     *y = W - rx; break;
        case 2:  *x = W - rx; *y = H - ry; break;
        case 3:  *x = H - ry; *y = rx;     break;
        default: *x = rx;     *y = ry;     break;
    }
}

void p4disp_set_turn(int quarter)
{
    s_turn = ((quarter % 4) + 4) % 4;
    if (s_fb[0] && s_fb[1]) {                          // the canvas moves on a 90-degree turn: clear both buffers
        memset(s_fb[0], 0, (size_t)P4DISP_PANEL_W * P4DISP_PANEL_H * 2);
        memset(s_fb[1], 0, (size_t)P4DISP_PANEL_W * P4DISP_PANEL_H * 2);
    }
}

bool p4disp_touch_to_compose(int rx, int ry, int* cx, int* cy)
{
    // Undo the extra turn, then undo the upright placement.
    const int W = P4DISP_PANEL_W - 1, H = P4DISP_PANEL_H - 1;
    int X, Y;
    switch (s_turn & 3) {
        case 1:  X = ry;     Y = W - rx; break;
        case 2:  X = W - rx; Y = H - ry; break;
        case 3:  X = H - ry; Y = rx;     break;
        default: X = rx;     Y = ry;     break;
    }
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
    gpio_set_level(PIN_LCD_BL, on ? 0 : 1);           // ACTIVE-LOW: 0 = backlight on
}
