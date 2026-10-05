#pragma once
#include "esp_lcd_types.h"
typedef struct {
  int cs_gpio_num; int dc_gpio_num; int spi_mode; unsigned int pclk_hz; size_t trans_queue_depth;
  void *on_color_trans_done; void *user_ctx; int lcd_cmd_bits; int lcd_param_bits;
  struct { unsigned int dc_high_on_cmd:1, dc_low_on_data:1, dc_low_on_param:1, octal_mode:1, quad_mode:1, sio_mode:1, lsb_first:1, cs_high_active:1; } flags;
} esp_lcd_panel_io_spi_config_t;
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t esp_lcd_new_panel_io_spi(esp_lcd_spi_bus_handle_t bus, const esp_lcd_panel_io_spi_config_t *cfg, esp_lcd_panel_io_handle_t *ret_io);
esp_err_t esp_lcd_panel_io_tx_param(esp_lcd_panel_io_handle_t io, int lcd_cmd, const void *param, size_t param_size);
#ifdef __cplusplus
}
#endif
