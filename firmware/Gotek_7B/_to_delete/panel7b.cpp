// ============================================================================
//  panel7b.cpp  —  LovyanGFX backend for the Waveshare ESP32-S3-Touch-LCD-7B
// ----------------------------------------------------------------------------
//  1024x600 RGB panel + GT911 touch, double-buffered (use_psram=2). Pins and
//  timing are the CONFIRMED 7B bring-up values (the ones that lit the panel with
//  correct colours and accurate touch). This file is the ONLY place the real
//  LovyanGFX library is included, keeping it away from the sketch's dummy
//  lgfx::fonts handles.
// ============================================================================
#include "panel7b.h"

#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>

static const int PW = 1024;
static const int PH = 600;

class LGFX_7B : public lgfx::LGFX_Device {
public:
  lgfx::Bus_RGB     _bus;
  lgfx::Panel_RGB   _panel;
  lgfx::Touch_GT911 _touch;
  LGFX_7B() {
    {
      auto cfg = _panel.config();
      cfg.memory_width  = PW; cfg.memory_height = PH;
      cfg.panel_width   = PW; cfg.panel_height  = PH;
      cfg.offset_x = 0; cfg.offset_y = 0;
      _panel.config(cfg);
    }
    {
      auto cfg = _panel.config_detail();
      cfg.use_psram = 2;   // double buffer — present with display(); kills tearing/flash
      _panel.config_detail(cfg);
    }
    {
      auto cfg = _bus.config();
      cfg.panel = &_panel;
      // CONFIRMED 7B data pins (LovyanGFX d0..d15: B0..B4, G0..G5, R0..R4):
      cfg.pin_d0  = 10; cfg.pin_d1  = 17; cfg.pin_d2  = 38; cfg.pin_d3  = 14; cfg.pin_d4  = 15;   // blue
      cfg.pin_d5  = 21; cfg.pin_d6  = 47; cfg.pin_d7  = 48; cfg.pin_d8  = 45; cfg.pin_d9  = 0;  cfg.pin_d10 = 39;   // green
      cfg.pin_d11 = 40; cfg.pin_d12 = 41; cfg.pin_d13 = 42; cfg.pin_d14 = 2;  cfg.pin_d15 = 1;    // red
      cfg.pin_henable = 5; cfg.pin_vsync = 3; cfg.pin_hsync = 46; cfg.pin_pclk = 7;
      cfg.freq_write        = 12000000;   // 12 MHz — confirmed artifact-free. Bump toward 21MHz if you want smoother refresh.
      cfg.hsync_polarity    = 0; cfg.hsync_front_porch = 40; cfg.hsync_pulse_width = 48; cfg.hsync_back_porch = 40;
      cfg.vsync_polarity    = 0; cfg.vsync_front_porch = 4;  cfg.vsync_pulse_width = 20; cfg.vsync_back_porch = 4;
      cfg.pclk_active_neg   = 1; cfg.de_idle_high = 0; cfg.pclk_idle_high = 0;
      _bus.config(cfg);
    }
    _panel.setBus(&_bus);
    {
      auto cfg = _touch.config();
      cfg.x_min = 0; cfg.x_max = PW - 1; cfg.y_min = 0; cfg.y_max = PH - 1;
      cfg.pin_int = 4; cfg.pin_rst = -1;                 // reset handled by IO_EXTENSION IO1
      cfg.i2c_port = I2C_NUM_0; cfg.i2c_addr = 0x5D;
      cfg.pin_sda = 8; cfg.pin_scl = 9; cfg.freq = 400000;
      cfg.offset_rotation = 0;
      _touch.config(cfg);
    }
    _panel.setTouch(&_touch);
    setPanel(&_panel);
  }
};

static LGFX_7B  s_lcd;
static bool     s_ok  = false;
static int      s_rot = -1;

bool panel7b_init() {
  s_ok = s_lcd.init();
  if (s_ok) {
    s_lcd.setRotation(0);
    s_rot = 0;
    // cb is native little-endian RGB565, but LovyanGFX pushImage expects
    // big-endian (documented in the sketch as "the LovyanGFX pushImage quirk").
    // So swap bytes on push. >>> If colours look wrong, flip this to false. <<<
    s_lcd.setSwapBytes(true);
  }
  return s_ok;
}

void panel7b_present(const uint16_t* cb, bool flip180) {
  if (!s_ok || !cb) return;
  int want = flip180 ? 2 : 0;
  if (want != s_rot) { s_lcd.setRotation(want); s_rot = want; }
  s_lcd.pushImage(0, 0, PW, PH, cb);   // draw full frame into the back buffer
  s_lcd.display();                     // flip (double-buffered present)
}

bool panel7b_getTouch(int32_t* x, int32_t* y) {
  if (!s_ok) return false;
  return s_lcd.getTouch(x, y);         // already mapped to the current rotation
}
