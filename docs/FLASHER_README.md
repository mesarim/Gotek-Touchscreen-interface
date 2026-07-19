# Web Flasher

Hosted at: `https://mesarim.github.io/Gotek-Touchscreen-interface/`

Built with [ESP Web Tools](https://esphome.github.io/esp-web-tools/) by ESPHome.
Works in Chrome and Edge only (Web Serial API requirement).

---

## Folder structure

```
docs/
├── index.html                        ← flasher page (GitHub Pages root)
└── firmware/
    ├── jc3248/
    │   ├── manifest.json
    │   └── Gotek_JC3248.ino.merged.bin   ← single merged image (offset 0)
    ├── waveshare7/
    │   ├── manifest.json
    │   └── Gotek_7inch.ino.merged.bin
    ├── waveshare28/
    │   ├── manifest.json
    │   └── merged.bin
    ├── xiao/
    │   ├── manifest.json
    │   └── Gotek_XIAO.ino.merged.bin
    └── cyd/
        ├── manifest.json
        └── Gotek_CYD.ino.merged.bin
```

Each manifest points at **one merged binary flashed at offset 0** — no separate
bootloader/partitions/boot_app0 parts. The ESP32 Arduino core builds this merged
image automatically on every compile, so releasing a new version is just a copy.

---

## How to build and update binaries

When you release a new firmware version, rebuild the binary and commit it here.

### Step 1 — Export from Arduino IDE

1. Open the `.ino` for the variant you want to build.
2. Set **all** board settings exactly as documented in the main README for that
   board (PSRAM / flash size / partition scheme differ per board — the wrong ones
   produce a binary that won't boot or won't be seen by the Gotek).
3. **Sketch → Export Compiled Binary.**

### Step 2 — Grab the merged binary

Arduino writes several files into a `build/` subfolder beside the `.ino`, e.g.:

```
build/esp32.esp32.esp32s3/Gotek_JC3248.ino.merged.bin
```

The `*.merged.bin` is the one you want — it already contains the bootloader,
partition table, boot_app0 selector and app in a single image. Ignore the
separate `.bootloader.bin` / `.partitions.bin` / `.bin` pieces; you don't need
to merge anything by hand.

### Step 3 — Copy into the flasher

Copy that `*.merged.bin` into `docs/firmware/<variant>/`, keeping the filename
the manifest references (see the manifest's `parts[].path`).

### Step 4 — Update the version

Edit `docs/firmware/<variant>/manifest.json` and bump the `version` field, then
update the matching entry in the `deviceInfo` object in `docs/index.html`.

### Step 5 — Commit and push

```bash
git add docs/
git commit -m "Release firmware vX.X.X — <variant>"
git push origin main
```

GitHub Pages will update automatically within a minute or two.

---

## Enabling GitHub Pages

In your repo settings:
1. Go to **Settings → Pages**
2. Set **Source** to `Deploy from a branch`
3. Set **Branch** to `main`, folder to `/docs`
4. Save — your flasher will be live at `https://mesarim.github.io/Gotek-Touchscreen-interface/`

---

## Notes

- The `new_install_prompt_erase: true` flag in each manifest asks the user
  whether to erase flash before installing. Recommended for first installs.
- The CYD uses `chipFamily: "ESP32"` (original ESP32, not S3).
  The Waveshare 7" and XIAO use `chipFamily: "ESP32-S3"`.
- ESP Web Tools auto-detects the chip family and will refuse to flash
  the wrong firmware to the wrong device.
