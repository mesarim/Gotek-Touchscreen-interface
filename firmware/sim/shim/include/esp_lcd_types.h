#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
typedef struct esp_lcd_panel_io_t *esp_lcd_panel_io_handle_t;
typedef struct esp_lcd_panel_t *esp_lcd_panel_handle_t;
typedef intptr_t esp_lcd_spi_bus_handle_t;
typedef enum { LCD_RGB_ELEMENT_ORDER_RGB, LCD_RGB_ELEMENT_ORDER_BGR } lcd_rgb_element_order_t;
typedef enum { LCD_RGB_ENDIAN_RGB, LCD_RGB_ENDIAN_BGR } lcd_rgb_endian_t;
