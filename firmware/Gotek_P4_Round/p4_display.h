// p4_display.h — ESP32-P4 JD9365 800x800 ROUND MIPI-DSI backend for the GTi.
// Waveshare ESP32-P4-WIFI6-Touch-LCD-3.4C (3.4" round, 800x800). C API so the IDF esp_lcd macros
// compile as C; the sketch (C++) calls these.
//
// Bring-up model (5.9.41-lab15n-P4R): the GTi keeps composing exactly as on the 4.3" P4 - into a
// PORTRAIT-native compose buffer (P4DISP_CW x P4DISP_CH = 464x624), so all of the P4's rotation code
// is unchanged and the landscape UI canvas is 624x464. present() turns that buffer upright into the
// middle of the 800x800 panel. 624x464 is the largest 4:3 box that fits inside the round glass with
// a margin (corners 389 px from the centre, the glass edge is at 400); everything outside stays black.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

#define P4DISP_PANEL_W 800            // native panel (square; only the inscribed circle is glass)
#define P4DISP_PANEL_H 800
#define P4DISP_CW 464                 // compose buffer width  (= the sketch's LCD_WIDTH,  portrait-native)
#define P4DISP_CH 624                 // compose buffer height (= the sketch's LCD_HEIGHT)
#define P4DISP_OX ((P4DISP_PANEL_W-P4DISP_CH)/2)   // 88  : left edge of the landscape canvas on the panel
#define P4DISP_OY ((P4DISP_PANEL_H-P4DISP_CW)/2)   // 168 : top edge

// LDO -> DSI bus -> JD9365 DBI init -> DPI panel with two frame buffers in PSRAM (both cleared to black).
// Returns true on success. Leaves the backlight OFF (call p4disp_backlight(true) after the first frame).
bool p4disp_init(void);

// Present the compose buffer (P4DISP_CW x P4DISP_CH, RGB565): copied upright into the back frame buffer's
// middle, then a page flip at the next frame. Blocks until the flip has happened (tear-free).
void p4disp_present(const uint16_t* compose);

// Extra quarter turns of the whole picture (0..3 = 0/90/180/270 degrees clockwise) for the case where the
// panel's native "up" is not the board's "up". Touch is turned the same way by p4disp_touch_to_compose().
void p4disp_set_turn(int quarter);

// Raw touch (panel-native 0..799, 0..799) -> compose-buffer coordinates (0..CW-1, 0..CH-1).
// Returns false when the touch is outside the canvas (the black ring).
bool p4disp_touch_to_compose(int rx, int ry, int* cx, int* cy);

// P4R-2: present a full 800x800 compose (the ROUND screens: reel, touch test) - identity, plus the PANELTURN turn.
void p4disp_present_full(const uint16_t* src800x800);

// P4R-2: undo PANELTURN only: raw touch -> upright panel coordinates (0..799). Round screens use these directly.
void p4disp_touch_unturn(int rx, int ry, int* x, int* y);

// Backlight: GPIO26, ACTIVE-LOW on this board (the vendor BSP drives it through an inverted LEDC channel).
void p4disp_backlight(bool on);

#ifdef __cplusplus
}
#endif
