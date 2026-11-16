// p4_display.h — ESP32-P4 ST7701 480x800 MIPI-DSI backend for the GTi (KGfx swap).
// C API so the IDF esp_lcd macros compile as C; KGfx (C++) calls these.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

// Native panel geometry (portrait). The GTi UI composes 800x480 landscape and
// this backend rotates 90 deg into the portrait framebuffer on present.
#define P4DISP_NATIVE_W 480
#define P4DISP_NATIVE_H 800

// Bring up LDO -> DSI bus -> ST7701 DBI init -> DPI framebuffers (num_fbs=2) in PSRAM.
// Returns true on success. Leaves backlight OFF (call p4disp_backlight(true) after first draw).
bool p4disp_init(void);

// Get one of the two DPI framebuffers (idx 0/1). Persistent PSRAM pointer, RGB565.
uint16_t* p4disp_framebuffer(int idx);

// Present a full native-resolution (480x800) RGB565 buffer: page-flip via the panel.
void p4disp_present(const uint16_t* fb480x800);

// Rotate a 800x480 landscape compose buffer 90 deg CW into dst (480x800), then present.
// rot: 0 = 90deg CW, 1 = 90deg CCW (flip). Software transpose (PPA upgrade is a TODO).
void p4disp_present_rotated(const uint16_t* compose800x480, uint16_t* dst480x800, int ccw);

void p4disp_backlight(bool on);

#ifdef __cplusplus
}
#endif
