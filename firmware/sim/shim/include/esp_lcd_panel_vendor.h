#pragma once
#include "esp_lcd_panel_io.h"
typedef struct {
  int reset_gpio_num; union { lcd_rgb_element_order_t rgb_ele_order; lcd_rgb_endian_t rgb_endian; };
  int data_endian; uint32_t bits_per_pixel; struct { unsigned int reset_active_high:1; } flags; void *vendor_config;
} esp_lcd_panel_dev_config_t;
