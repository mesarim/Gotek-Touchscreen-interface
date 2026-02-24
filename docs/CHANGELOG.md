
# Changelog

## v0.5.2 — 2026‑02‑24
- Hide ADF/DSK toggle in **Details** (visible only on Selection) to reduce confusion.
- Waveshare ESP32‑S3‑Touch‑LCD‑2.8 targeted explicitly.
- Compile robustness: forward declarations, `void loop(void)`, removed HTML entities.

## v0.5.1
- Forward declarations added to avoid Arduino prototype issues.
- Signature fix for `loop()`.

## v0.5.0
- Introduced ADF/DSK toggle, dynamic output filename, size‑aware FAT12 build.
