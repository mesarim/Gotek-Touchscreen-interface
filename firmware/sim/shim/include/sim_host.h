// imports provided by the web page (sim.js)
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SIM_IMPORT(name) __attribute__((import_module("env"), import_name(#name)))
SIM_IMPORT(js_now_ms) double js_now_ms(void);           // monotonic ms (performance.now)
SIM_IMPORT(js_epoch_ms) double js_epoch_ms(void);       // wall clock
SIM_IMPORT(js_sleep) void js_sleep(uint32_t ms);       // async (asyncify): yields to the browser
SIM_IMPORT(js_log) void js_log(const char *p, uint32_t n);
SIM_IMPORT(js_present) void js_present(const uint16_t *fb, int x0, int y0, int x1, int y1);
SIM_IMPORT(js_touch) int js_touch(int *x, int *y);       // panel coords (native 320x480); returns 1 while pressed
SIM_IMPORT(js_disk_sectors) uint32_t js_disk_sectors(void);
SIM_IMPORT(js_disk_read) int js_disk_read(uint8_t *buf, uint32_t sector, uint32_t count);
SIM_IMPORT(js_disk_write) int js_disk_write(const uint8_t *buf, uint32_t sector, uint32_t count);
SIM_IMPORT(js_disk_present) int js_disk_present(void);
SIM_IMPORT(js_restart) void js_restart(void);
SIM_IMPORT(js_event) void js_event(int kind, int a, int b, const char *s);   // UI notifications (USB, dongle, backlight...)
SIM_IMPORT(js_usb_host) int js_usb_host(void);           // 1 = a host (Gotek / PC) is plugged into the USB-C
SIM_IMPORT(js_random) uint32_t js_random(void);
SIM_IMPORT(js_backlight) void js_backlight(int duty);
#ifdef __cplusplus
}
#endif
