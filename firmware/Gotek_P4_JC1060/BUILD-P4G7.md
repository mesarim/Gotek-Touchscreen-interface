# GTi on the Guition JC1060P470C_I_W_Y (7", 1024x600, ESP32-P4 + C6) — build notes

Version **5.9.42-lab1-P4G7** = the 4.3" P4 `5.9.42-lab1-P4` (NEO look) + this board's layer. Bring-up build.

## Arduino IDE settings (core 3.3.12)
- Board: **ESP32P4 Dev Module**
- Chip Variant: **Before v3.00** (this board's P4 is v1.3 silicon, 360 MHz)
- USB Mode: **USB-OTG (TinyUSB)**, USB CDC On Boot: **Disabled**
- PSRAM: **Enabled**, Flash Size: **16MB**, Partition Scheme: **Custom** (uses partitions.csv in this folder)
- Close and reopen the sketch after copying the folder.

## Board layer (what differs from the 4.3" P4)
| Part | JC1060 7" | Source |
|---|---|---|
| Panel | JD9165 MIPI-DSI 1024x600, 2 lanes @ 750 Mbps, DPI 52 MHz, h 24/136/160 v 2/21/12, RST GPIO5 | Guition demo `lvgl_demo_v8` |
| Init table | NEW panel = driver default (`esp_lcd_jd9165.c`, Apache-2.0); OLD panel table in `p4_display.c` | demo New_Panel / Old_Panel |
| Backlight | GPIO23 -> MP3202 EN, active-high | demo + schematic |
| Touch | GT911, I2C SDA7/SCL8 (shared with ES8311 + RX8025T), raw 1024x600 | demo + schematic |
| microSD | 4-bit, CLK43 CMD44 D0-D3 39-42; card power = LDO ch 4 -> AO3401, always on (GPIO45 option not fitted) | schematic |
| USB | Full-speed USB-C (flash / serial) + High-speed USB-C (the Gotek, TinyUSB) | schematic |
| Wireless | ESP32-C6 on the JC-ESP32P4-M3 module (same module family as the 4.3") | schematic |

The GTi composes into a portrait-native 600x1024 buffer exactly like the 4.3" P4 (so every rotation path is unchanged);
`p4_display.c` turns it upright into the landscape panel on present (32x32 blocked copy into the back DPI frame buffer,
page flip on refresh-done) — the method proven on the round P4R port.

## Switches
- **Old glass** (no "V2" on the back label): set `#define JC1060_OLD_PANEL 1` in `p4_display.h` (both compile).
- `PANELTURN=180` in CONFIG.TXT turns picture + touch if the board is mounted upside down.
- SD update file: `GTi-P4G7-*.bin` (own tag + marker `OMEGAWARE.GTi.P4G7.fw`, never cross-installs with the 4.3" P4 or the round one).

## First-run checks
picture upright, touch lands where tapped (gti.log logs the first 16 raw touches: `[touch] raw x,y`),
`[sd] ... 4-bit` in gti.log, cover reel + list in landscape and portrait, a cable load on a Gotek (HS USB-C port),
then ESP-NOW: SCAN DONGLES (tells us if the C6 wiring matches the 4.3").
