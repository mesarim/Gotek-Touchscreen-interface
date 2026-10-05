// p4_display.h — ESP32-P4 EK79007 1024x600 MIPI-DSI backend for the GTi (KGfx swap).
// Waveshare ESP32-P4-WIFI6-Touch-LCD-7B. C API so the IDF esp_lcd macros compile
// as C; KGfx (C++) calls these.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

// Native panel geometry (LANDSCAPE). The 7B panel is native 1024x600 landscape,
// so the GTi UI composes 1024x600 and this backend presents it DIRECTLY — no
// rotation (unlike the 4.3" JC4880 which is native 480x800 portrait).
#define P4DISP_NATIVE_W 1024
#define P4DISP_NATIVE_H 600

// Bring up LDO -> DSI bus -> EK79007 DBI init -> DPI framebuffers (num_fbs=2) in PSRAM.
// Returns true on success. Leaves backlight OFF (call p4disp_backlight(true) after first draw).
bool p4disp_init(void);

// Get one of the two DPI framebuffers (idx 0/1). Persistent PSRAM pointer, RGB565.
uint16_t* p4disp_framebuffer(int idx);

// Present a full native-resolution (1024x600) RGB565 buffer: page-flip via the panel.
void p4disp_present(const uint16_t* fb1024x600);

// Backlight is active-LOW on the 7B (LEDC-inverted in the BSP; we drive the pin low = ON).
void p4disp_backlight(bool on);

#ifdef __cplusplus
}
#endif
