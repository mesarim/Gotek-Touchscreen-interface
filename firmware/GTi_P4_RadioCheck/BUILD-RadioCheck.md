# GTi_P4_RadioCheck — standalone P4 radio diagnostic

Separate from the GTi firmware so it can't break the working touch build. Reports on the
screen (CDC is off): P4 build info, C6 radio state, and whether esp_now_init() links & runs.

## Board menu (same as Gotek_P4)
ESP32P4 Dev Module | Flash 16MB | PSRAM Enabled | CPU 360MHz | USB-OTG (TinyUSB) | CDC off |
Partition Scheme = Custom (uses the partitions.csv here). Flash the merged at 0x0.

## The two toggles at the top of the .ino
- **TRY_ESPNOW 1** — if the build **fails to link** on `esp_now_init` (undefined reference),
  that IS the answer: ESP-NOW is not in this Arduino P4 core. Set it to 0, rebuild, reflash
  (you'll still get the P4 + C6 report), and tell Claude "esp_now did not link".
- **TRY_C6VER 1** — if it won't compile complaining about `esp_hosted.h` /
  `esp_hosted_get_coprocessor_fwversion`, set it to 0 and rebuild. The Wi-Fi **scan** still
  proves the C6 is alive even without the version string.

## What to send back
A photo of the screen. It shows: P4 chip/rev/IDF/Arduino, C6 hosted FW version (if available),
Wi-Fi scan result (C6 alive?), and the ESP-NOW init line (green = it works).
