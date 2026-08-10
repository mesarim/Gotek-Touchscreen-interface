// ============================================================================
//  panel7b.h  —  LovyanGFX display/touch backend for the Waveshare 7B (1024x600)
// ----------------------------------------------------------------------------
//  The main sketch keeps its self-contained KGfx compose-buffer engine (its own
//  6x8 font, all drawing lands in `cb`). LovyanGFX is deliberately isolated in
//  panel7b.cpp so the sketch's dummy `lgfx::fonts` handles never collide with the
//  real library. This header exposes only three plain functions the KGfx shim
//  calls: bring-up, present a full frame, and read touch.
//
//  WHY LovyanGFX: the shipping 7" firmware uses it, and the 7B bring-up was
//  confirmed on it (correct colours, dead-on touch, double-buffered = no flash).
//  The earlier hand-rolled esp_lcd path mismatched LovyanGFX on colour-bit order
//  (inverted colours), present (flashing) and touch scaling (misalignment).
// ============================================================================
#pragma once
#include <stdint.h>

// Create the LovyanGFX device (panel + GT911 touch, double-buffered). Backlight
// is NOT handled here — it's the CH32V003 IO_EXTENSION PWM driven from the sketch.
// Returns false if panel init fails.
bool panel7b_init();

// Present one full 1024x600 RGB565 frame from the KGfx compose buffer.
// `cb` is native little-endian RGB565 (0xF800 = red). flip180 = screen mounted
// upside-down (LCD_ROTATION 2/3).
void panel7b_present(const uint16_t* cb, bool flip180);

// Read a single touch point (already mapped to panel orientation). Returns false
// when no finger is down.
bool panel7b_getTouch(int32_t* x, int32_t* y);
