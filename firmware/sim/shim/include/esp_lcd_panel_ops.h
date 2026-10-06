#pragma once
#include "esp_lcd_types.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t esp_lcd_panel_reset(esp_lcd_panel_handle_t panel);
esp_err_t esp_lcd_panel_init(esp_lcd_panel_handle_t panel);
esp_err_t esp_lcd_panel_del(esp_lcd_panel_handle_t panel);
esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t panel, int x_start, int y_start, int x_end, int y_end, const void *color_data);
esp_err_t esp_lcd_panel_disp_on_off(esp_lcd_panel_handle_t panel, bool on_off);
esp_err_t esp_lcd_panel_invert_color(esp_lcd_panel_handle_t panel, bool invert);
esp_err_t esp_lcd_panel_mirror(esp_lcd_panel_handle_t panel, bool x, bool y);
esp_err_t esp_lcd_panel_swap_xy(esp_lcd_panel_handle_t panel, bool s);
#ifdef __cplusplus
}
#endif
