# Flashing a GTi (JC3248) from a Mac — step by step

This gets a **brand-new Mac** able to flash a Guition **JC3248** board with the GTi
firmware, using `esptool` and the ready-made `.merged.bin`. No coding, no Arduino.
Just copy-paste each block into **Terminal** (press ⌘-Space, type `Terminal`, hit Return).

> Every command below is run from your **Downloads** folder. Put both downloads there.

---

## What you need
1. The **JC3248 board** and a **USB-C data cable** (a charge-only cable will not work).
2. The firmware file **`Gotek_JC3248.ino.merged.bin`** — download it from the flash page
   (`firmware/jc3248-beta/` for the beta, `firmware/jc3248/` for stable) and leave it in **Downloads**.
3. **esptool** — download link:
   `https://github.com/espressif/esptool/releases/download/v5.3.1/esptool-v5.3.1-macos-amd64.tar.gz`
   (leave the downloaded `.tar.gz` in **Downloads** too).

> **Intel vs Apple-Silicon Mac:** the `-amd64` file is for **Intel** Macs. On an
> **Apple-Silicon** Mac (M1/M2/M3/M4) it still runs — if macOS pops up "install Rosetta,"
> click **Install**. For a native binary instead, grab the `-arm64` file from the same
> releases page and change `amd64` → `arm64` in the commands below.

---

## Step 1 — Install esptool (from the downloaded file)

Copy-paste this whole block:

```bash
cd ~/Downloads
tar -xzf esptool-v5.3.1-macos-amd64.tar.gz
xattr -dr com.apple.quarantine esptool-v5.3.1-macos-amd64
```

- Line 1 moves you into Downloads.
- Line 2 unpacks esptool into a folder called `esptool-v5.3.1-macos-amd64`.
- Line 3 tells macOS to **trust** it (without this, macOS blocks it as "unverified developer").

Now check it works:

```bash
./esptool-v5.3.1-macos-amd64/esptool version
```

✅ **You should see** a version line like `esptool v5.3.1`.
If instead you get *"cannot be opened / unverified developer,"* open
**System Settings → Privacy & Security**, scroll down, click **Open Anyway**, then re-run the line.

> If `tar` produced loose files instead of that folder (run `ls` to check), just drop the
> `esptool-v5.3.1-macos-amd64/` part and use `./esptool` in the commands below.

---

## Step 2 — Check the USB port (install a driver only if needed)

With the board **unplugged**, run:

```bash
ls /dev/cu.*
```

Now **plug the board into the Mac** with the data cable, wait 3 seconds, and run it again:

```bash
ls /dev/cu.*
```

Compare the two lists — the **new entry** that appeared is your board. It'll look like one of:

- `/dev/cu.wchusbserial####` or `/dev/cu.usbserial-####` → **you're ready**, go to Step 3.
- `/dev/cu.usbmodem####` → also fine (native USB), go to Step 3.
- **Nothing new appeared?** The board uses a CH340 chip and this Mac needs its driver.
  Install the **WCH CH34x** driver (guide: https://learn.adafruit.com/how-to-install-drivers-for-wch-usb-to-serial-chips-ch9102f-ch9102/overview ),
  **reboot**, then redo this step. (Also double-check it's a **data** cable, not charge-only.)

**Write down your port name** — you need it in the next step.

---

## Step 3 — Flash the firmware

Copy the command below, but **replace `/dev/cu.REPLACE-ME`** with your real port from Step 2:

```bash
cd ~/Downloads
./esptool-v5.3.1-macos-amd64/esptool --chip esp32s3 --port /dev/cu.REPLACE-ME --baud 921600 write_flash 0x0 Gotek_JC3248.ino.merged.bin
```

✅ **You should see** `Connecting....`, then `Writing at 0x...` with a percentage counting up,
then `Hash of data verified.` and `Hard resetting`. The board reboots straight into the GTi. **Done.**

Because the `.merged.bin` is a full-chip image written at `0x0`, this wipes and replaces
everything cleanly — no separate erase step, and it lands the correct partition layout on its own.

---

## If it doesn't work

- **`A fatal error occurred: Failed to connect`** — hold the **BOOT** button, tap **RESET**,
  release BOOT, then run the Step-3 command again. (Forces the board into flashing mode.)
- **Connects then fails partway / garbled** — the cable or port is marginal. Re-run Step 3 with
  a slower speed: change `--baud 921600` to `--baud 460800` (or `--baud 115200`).
- **No `/dev/cu.*` port ever appears** — charge-only cable, or missing CH340 driver (Step 2).
- **`command not found` / `No such file`** — you're not in Downloads, or the esptool folder name
  differs. Run `cd ~/Downloads` then `ls` and check the folder name matches the command.
- **"unverified developer" keeps blocking esptool** — System Settings → Privacy & Security → **Open Anyway**.

---

*GTi — OMEGAWARE. Questions/bugs: the OMEGAWARE Discord (link on the flash page).*
