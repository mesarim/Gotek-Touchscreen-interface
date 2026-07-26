# GTi — CYD wireless remote (ESP32-2432S028R "Cheap Yellow Display")

Wireless-only remote control for the GTi ecosystem: browse and FLING disks to a
wireless dongle over ESP-NOW/WiFi. **No SD slot used, no USB disk role** — this
build is purely the remote-control screen. Pairs with a Super Mini or XIAO dongle.

## Build (Arduino IDE)

**Board support:** esp32 by Espressif Systems, core 3.x (Boards Manager).

**Board menu — ESP32 Dev Module:**

| Setting | Value |
|---|---|
| Partition Scheme | Huge APP (3MB No OTA) — **required** |
| Flash Frequency | **40MHz** — required (80MHz = white screen on many CYD units) |
| CPU Frequency | 240MHz |
| PSRAM | Disabled |

**Libraries (Library Manager):**
- **TFT_eSPI** (Bodmer)
- **XPT2046_Touchscreen** (Paul Stoffregen)
- **TJpg_Decoder** (Bodmer)

## The one extra step: User_Setup.h

TFT_eSPI is configured by a header INSIDE the library, not by the sketch.
**Copy the `User_Setup.h` from this folder to:**

```
Documents\Arduino\libraries\TFT_eSPI\User_Setup.h
```

(overwriting the library's default). Key settings it carries, if you prefer to
edit your own: `ILI9341_2_DRIVER`, `USE_HSPI_PORT`, and **no** `TOUCH_CS`
(touch is handled by XPT2046_Touchscreen directly, on its own pins).

If the screen is white or garbled after flashing: it is almost always this file
missing, or Flash Frequency left at 80MHz.

## Notes

- Display init pattern follows the confirmed-working lachimalaif/DataDisplayCYD
  sequence: `tft.init() → setRotation(1) → invertDisplay(true)`.
- Landscape 320×240. JPEG covers drawn via TJpg_Decoder from a RAM buffer.
- Wireless only: pair with a dongle from the settings screen, then FLING away.

_GTi firmware by Mez & Dimmy & Claude / OMEGAWARE._
