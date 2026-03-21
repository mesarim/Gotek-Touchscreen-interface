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
    ├── waveshare7/
    │   ├── manifest.json
    │   ├── bootloader.bin            ← built from Arduino IDE
    │   ├── partitions.bin
    │   ├── boot_app0.bin
    │   └── firmware.bin
    ├── xiao/
    │   ├── manifest.json
    │   ├── bootloader.bin
    │   ├── partitions.bin
    │   ├── boot_app0.bin
    │   └── firmware.bin
    └── cyd/
        ├── manifest.json
        ├── bootloader.bin
        ├── partitions.bin
        ├── boot_app0.bin
        └── firmware.bin
```

---

## How to build and update binaries

When you release a new firmware version, rebuild the binaries and commit them here.

### Step 1 — Export from Arduino IDE

1. Open the `.ino` file for the variant you want to build
2. Set all board settings as documented in the main README
3. Go to **Sketch → Export Compiled Binary**
4. Arduino saves the files to the same folder as the `.ino`

### Step 2 — Find the four required files

Arduino outputs several files. You need these four:

| File | What it is | Offset |
|---|---|---|
| `Gotek_xxx.ino.bootloader.bin` | Bootloader | 0x1000 (4096) |
| `Gotek_xxx.ino.partitions.bin` | Partition table | 0x8000 (32768) |
| `boot_app0.bin` | OTA boot selector | 0xe000 (57344) |
| `Gotek_xxx.ino.bin` | Main firmware | 0x10000 (65536) |

`boot_app0.bin` is not in the sketch output folder — find it here:
```
Windows: C:\Users\<you>\AppData\Local\Arduino15\packages\esp32\hardware\esp32\<version>\tools\partitions\boot_app0.bin
```

### Step 3 — Copy and rename

Copy the four files into the appropriate `docs/firmware/<variant>/` folder,
renaming them to match the manifest:

```
bootloader.bin   ← Gotek_xxx.ino.bootloader.bin
partitions.bin   ← Gotek_xxx.ino.partitions.bin
boot_app0.bin    ← boot_app0.bin  (no rename needed)
firmware.bin     ← Gotek_xxx.ino.bin
```

### Step 4 — Update the manifest version

Edit `docs/firmware/<variant>/manifest.json` and bump the `version` field.

Also update the version string in `docs/index.html` in the `deviceInfo` object.

### Step 5 — Commit and push

```bash
git add docs/
git commit -m "Release firmware vX.X.X — Waveshare7/XIAO/CYD"
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
