// p4_display.h — ESP32-P4 JD9165 1024x600 MIPI-DSI backend for the GTi.
// Guition JC1060P470C_I_W_Y (7", 1024x600 IPS, ESP32-P4 + C6 module JC-ESP32P4-M3). C API so the IDF esp_lcd
// macros compile as C; the sketch (C++) calls these.
//
// Model (5.9.42-lab1-P4G7, same as the round P4R port): the GTi keeps composing exactly as on the 4.3" P4 - into a
// PORTRAIT-native compose buffer (P4DISP_CW x P4DISP_CH = 600x1024), so all of the P4's rotation code is unchanged
// and the landscape canvas is 1024x600. present() turns that buffer upright into the (landscape-native) panel.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

#define P4DISP_PANEL_W 1024           // native panel: landscape
#define P4DISP_PANEL_H 600
#define P4DISP_CW 600                 // compose buffer width  (= the sketch's LCD_WIDTH,  portrait-native)
#define P4DISP_CH 1024                // compose buffer height (= the sketch's LCD_HEIGHT)
#define P4DISP_OX 0                   // the canvas fills the whole panel
#define P4DISP_OY 0

// Panel version: Guition ships two glass versions with different init tables. "V2" on the back label = NEW panel.
// 0 = New Panel (default), 1 = Old Panel. Compile-time: the panel is brought up before CONFIG.TXT is read.
#ifndef JC1060_OLD_PANEL
#define JC1060_OLD_PANEL 0
#endif

// LDO -> DSI bus -> JD9165 DBI init -> DPI panel with two frame buffers in PSRAM (both cleared to black).
// Returns true on success. Leaves the backlight OFF (call p4disp_backlight(true) after the first frame).
bool p4disp_init(void);

// Present the compose buffer (P4DISP_CW x P4DISP_CH, RGB565): turned upright into the back frame buffer,
// then a page flip at the next frame. Blocks until the flip has happened (tear-free).
void p4disp_present(const uint16_t* compose);

// Extra half turn of the whole picture (PANELTURN=180) for a board mounted upside down. 0 or 2 (quarter turns).
void p4disp_set_turn(int quarter);

// Raw touch (panel-native 0..1023, 0..599) -> compose-buffer coordinates (0..CW-1, 0..CH-1).
bool p4disp_touch_to_compose(int rx, int ry, int* cx, int* cy);

// Backlight: GPIO23 -> MP3202 EN (active-high).
void p4disp_backlight(bool on);

#ifdef __cplusplus
}
#endif
