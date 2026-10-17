// Battery telemetry for the JC3248W535C.
//
// Dimmy's module from the glucose display, ported. Everything that matters -
// the pin, the divider, the discharge curve, the presence window and the
// hysteresis - is his, measured on this exact board. Two things changed:
//
//   1. gfx->x() becomes the GTi's gfx_x() wrappers.
//   2. The charging bolt was two fillTriangle calls. The GTi has no triangle
//      primitive at all - its whole renderer is gfx_fillRect into a software
//      framebuffer - so the bolt is drawn from four stacked bars instead.
//
// His note, kept because it saves the next person a wasted afternoon: the
// IP5306 on this board is the NON-I2C variant, so the ADC is the only
// telemetry there is. Scanning I2C for 0x75 finds nothing.
//
// The charging itself is done by that IP5306 in hardware and needs no
// firmware at all. This only reads the result.
#ifndef BATTERY_H
#define BATTERY_H

#define BAT_ADC_PIN 5          // free on the GTi: display is 45/47/21/48/40/39/1, touch 4/8
#define BAT_DIVIDER 1.72f
#define BAT_WINDOW  4          // rolling average over recent polls

static bool  bat_present = false;
static int   bat_pct = -1;
static float bat_volt = 0.0f;
static bool  bat_charging = false, bat_full = false;
static bool  bat_unplugged = false;    // hysteresis state: on battery power

static float bat_win[BAT_WINDOW];
static int   bat_win_n = 0, bat_win_i = 0;

// LiPo curve: voltage -> percent, straight from the community calibration.
static const float BAT_CURVE[][2] = {
  { 3.00f, 0.0f }, { 3.35f, 12.0f }, { 3.40f, 20.0f },
  { 3.60f, 60.0f }, { 4.00f, 90.0f }, { 4.10f, 100.0f },
};
#define BAT_POINTS 6

static float bat_curve_pct(float v) {
  if (v <= BAT_CURVE[0][0]) return 0.0f;
  if (v >= BAT_CURVE[BAT_POINTS - 1][0]) return 100.0f;
  for (int i = 1; i < BAT_POINTS; i++) {
    if (v <= BAT_CURVE[i][0]) {
      float x0 = BAT_CURVE[i - 1][0], y0 = BAT_CURVE[i - 1][1];
      float x1 = BAT_CURVE[i][0],     y1 = BAT_CURVE[i][1];
      return y0 + (v - x0) / (x1 - x0) * (y1 - y0);
    }
  }
  return 100.0f;
}

// One reading: 16 calibrated samples averaged, times the divider.
static float bat_read_volt() {
  uint32_t mv = 0;
  for (int i = 0; i < 16; i++) mv += analogReadMilliVolts(BAT_ADC_PIN);
  return (mv / 16) / 1000.0f * BAT_DIVIDER;
}

static void battery_poll() {
  float v = bat_read_volt();
  bat_win[bat_win_i] = v;
  bat_win_i = (bat_win_i + 1) % BAT_WINDOW;
  if (bat_win_n < BAT_WINDOW) bat_win_n++;
  float sum = 0;
  for (int i = 0; i < bat_win_n; i++) sum += bat_win[i];
  bat_volt = sum / bat_win_n;

  // outside the plausible LiPo window: probably no battery attached
  bat_present = (bat_volt >= 2.8f && bat_volt <= 4.45f);
  if (!bat_present) { bat_pct = -1; return; }
  bat_pct = (int)(bat_curve_pct(bat_volt) + 0.5f);
  bat_full = bat_volt >= 4.15f && bat_pct >= 100;
  bat_charging = bat_volt >= 4.18f && !bat_full;
  // hysteresis keeps the state stable around the threshold
  if (bat_volt < 4.12f) bat_unplugged = true;
  else if (bat_volt > 4.19f) bat_unplugged = false;
}

static void battery_begin() {
  analogReadResolution(12);
  // seed the whole window so the state is right from the first minute
  float v = bat_read_volt();
  for (int i = 0; i < BAT_WINDOW; i++) bat_win[i] = v;
  bat_win_n = BAT_WINDOW;
  battery_poll();
  gLog("[batt] adc gpio%d %.2fV -> %s\n", BAT_ADC_PIN, bat_volt,
       bat_present ? String(String(bat_pct) + "%").c_str() : "no battery");
}

// On battery per the hysteresis state above; a full pack fresh off the
// charger may read as plugged for its first minutes under load.
static bool bat_on_battery() {
  return bat_present && bat_unplugged;
}

// Width this glyph occupies, so a caller can lay out around it. Zero when
// there is no battery, which is how the status bar stays unchanged for
// everyone who has not fitted one.
static int battery_width() { return bat_present && bat_pct >= 0 ? 33 : 0; }

// Small battery glyph with fill level, a bolt while charging.
static void battery_draw(int x, int y, uint16_t fg, uint16_t warn) {
  if (!bat_present || bat_pct < 0) return;
  const int w = 30, h = 14;
  uint16_t col = (bat_pct <= 25 && !bat_charging && !bat_full) ? warn : fg;
  gfx_drawRoundRect(x, y, w, h, 3, col);
  gfx_fillRect(x + w, y + 4, 3, h - 8, col);            // nub
  int fill = (w - 6) * bat_pct / 100;
  if (fill > 0) gfx_fillRect(x + 3, y + 3, fill, h - 6, col);
  if (bat_charging) {
    // A bolt out of bars: four steps leaning right, then left, which reads as
    // a lightning flash at this size and needs no triangle primitive.
    int bx = x + w / 2;
    gfx_fillRect(bx + 1, y + 2, 3, 3, warn);
    gfx_fillRect(bx - 1, y + 5, 3, 3, warn);
    gfx_fillRect(bx - 3, y + 8, 5, 2, warn);
    gfx_fillRect(bx - 1, y + 9, 3, 3, warn);
  }
}

#endif
