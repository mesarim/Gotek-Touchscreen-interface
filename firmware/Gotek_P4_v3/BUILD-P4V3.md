# Gotek_P4_v3 - the 4.3" P4 (JC4880P443C) with an ESP32-P4 chip **v3.x**

Two sketches, one per chip family (Mez, 18 Oct 2026). Espressif: v1.x and v3.x chips cannot share a firmware image.

| Chip (esptool "Chip type", or the 2nd letter of the chip's manufacturing code) | Sketch |
|---|---|
| ESP32-P4 revision v1.x (code letter C or E) | `firmware/Gotek_P4` - Chip Variant **Before v3.00** |
| ESP32-P4 revision v3.x (code letter F, G, H or J) | `firmware/Gotek_P4_v3` - Chip Variant **v3.00 or after** |

A wrong Chip Variant stops the compile with an #error, so the two can't be mixed up.

Board settings (otherwise the same as Gotek_P4): ESP32P4 Dev Module, Chip Variant **v3.00 or after**, Flash 16MB,
Partition Scheme Custom, PSRAM Enabled, USB Mode OTG. Flashing a v3.2 needs esptool 5.3 or later (Arduino core 3.3.12
brings 5.3.1).

Differences from Gotek_P4 (keep this list short - every other change goes into both folders):
- `p4_display.c`: DSI PHY PLL clock = `MIPI_DSI_PHY_PLLREF_CLK_SRC_DEFAULT` (XTAL); the v1 macro's PLL_F20M aborts on v3.
- `FW_VERSION` ends in `-P4V3`; SD update files are `GTi-P4V3-<ver>-update.bin`; marker `OMEGAWARE.GTi.P4V3.fw`.
- The chip guard at the top of the .ino is inverted.
