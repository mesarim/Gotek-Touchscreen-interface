# Why these boards — the ESP32-S3 choice, and how hard we push it

This page explains *why* the GTi runs on the hardware it does: why the ESP32-S3 is
the flagship, what it costs, how the graphics pipeline works, and how much of the
chip's ceiling we've actually reached. Short version up front, detail below.

## TL;DR

We chose the **ESP32-S3 primarily because of USB — not performance.** The standalone
GTi has to *become* the Gotek's USB drive, and the S3 is the cheapest chip that can
natively do that (USB-OTG device mode, via TinyUSB) **while also** having the dual
cores, PSRAM and radio to run the touchscreen UI and the wireless link at the same
time. Everything else — the screen, the graphics approach, the performance tuning —
follows from that one requirement.

## A GTi board does one of two jobs

The whole hardware decision falls out of the fact that a GTi unit plays one of two
very different roles:

1. **Standalone** — the board plugs straight into the Gotek's USB port and *is* the
   USB stick. The selected disk image is served to the host machine as a USB Mass
   Storage device, with the image held in RAM — a **virtual floppy living in PSRAM**.
   This *requires* native USB-OTG **device** mode.
2. **Wireless remote** — the board is just a touchscreen that browses the library and
   flings disk images over WiFi / ESP-NOW to a dongle (the Super Mini), which does the
   USB part. This needs only display + touch + radio — **no USB device mode at all.**

A chip that can do the standalone role can always also do the wireless role, so a
USB-capable screen is dual-role. A chip *without* USB device mode can only ever be a
remote. That single capability is the gate.

## Why ESP32-S3: USB is the gatekeeper

To be the drive, a chip must present itself as a USB **device** — specifically a Mass
Storage gadget. Most of the ESP32 family can't. Here's the field as it stood when the
GTi was designed:

| Chip | Can it *be* a USB drive? | Radio | Cores | Verdict for the GTi |
|---|---|---|---|---|
| **ESP32** (classic) | ❌ No native USB device (needs an external USB chip) | WiFi + BT | 2 | Wireless remote only — proven on the CYD |
| **ESP32-S2** | ✅ USB-OTG device | WiFi only — **no Bluetooth** | 1 | *Could* do the USB trick, but single-core with no BLE = a weak all-rounder for a device that must also drive a screen and talk wirelessly |
| **ESP32-S3** | ✅ USB-OTG device (TinyUSB) | WiFi + **BLE 5** | 2 | **The pick** — USB *plus* dual-core *plus* octal 8 MB PSRAM *plus* radio, all on one cheap chip |
| **ESP32-C3 / C6** | ❌ USB-Serial/JTAG only (not a Mass-Storage gadget) | WiFi (6) + BLE (+ 802.15.4 on C6) | 1 | Remote-ish at best — can't be the drive |
| **ESP32-P4** | ✅ USB 2.0 HS OTG | ❌ **None** (needs a companion radio chip) | 2 | Future premium tier — see below |

So among chips that can actually present as a USB drive, the field narrows to
**S2, S3, P4**:

- **S2** technically works but is single-core and has **no Bluetooth** — a poor fit for
  a product that also has to render a touchscreen UI and run a wireless link.
- **P4** has no radio of its own (it needs a companion chip for WiFi/BLE) and simply
  wasn't a practical option when the GTi was built.
- **S3** is the only one that gives you **native USB-drive emulation *and* the dual-core
  + PSRAM + WiFi/BLE** needed to be the entire product on a single, cheap part.

That is the primary reason for the S3, full stop. The graphics performance was never
the deciding factor — it's a happy consequence of a chip that was chosen for its USB.

## Cost — the "£4 rule"

The **Guition JC3248W535C (3.5", 480×320)** is an ESP32-S3 with capacitive touch, 8 MB
PSRAM, 16 MB flash and a sharp QSPI panel, for roughly **£16**. It does *both* roles.
It's so cheap that no tier beneath it is worth engineering: a crippled "budget remote"
would lose to the £16 do-everything flagship every time. So there's one recommendation
and no decision paralysis — the flagship already owns the budget niche. The 4.3"
**JC4827W543** is the same story on a slightly larger screen.

## Performance — how the GTi actually draws

The GTi does **not** use LVGL, and it has no GPU. It uses an **immediate-mode software
rasteriser into a full framebuffer**: our own drawing routines paint every frame into a
single RGB565 framebuffer in PSRAM, which is then flushed to the QSPI panel in
horizontal strips via the ESP-IDF LCD driver and DMA.

The important consequence: the bottleneck is **the CPU drawing every pixel, plus PSRAM
bandwidth — not the panel.** QSPI has plenty of headroom for a 300 KB frame; the work
is in rasterising it. That's also why cover-art loading was optimised the way it was
(tiny cover thumbnails held resident in RAM): on a chip with no 2D accelerator, the
cheapest pixel is the one you precomputed and never have to draw again.

## How much of the S3 ceiling have we reached?

**We've reached the *practical* ceiling — the most you can get while holding clean
full-frame rendering, stable operation, working USB, and sane code complexity.** That's
a deliberately bounded claim, and it's the defensible one: a synthetic benchmark can
always wring a few more FPS out of the raw panel, but only by sacrificing one of those
four things. For full-screen motion (the carousel), with a
single clean framebuffer, the only way to buy more frames-per-second on this chip is to
give up the clean-composite guarantee — draw straight to the live panel mid-flush
(→ tearing) or drop buffering without a hardware vsync lock (→ shear and shadow
artifacts). We could push the numbers higher, but it would look *worse*, not better.
We deliberately hold at a clean ~60 rather than chase an ugly 90.

There is still some **clean** headroom left, just not in the moving scenes:

- **Dirty-rectangle redraws** on the static screens (list, settings) — only repaint the
  region that changed instead of the whole 300 KB frame.
- **Tighter blit inner loops** — pure software wins in the hot drawing paths.

But the **hard wall is architectural: the S3 has no 2D accelerator.** The CPU touches
every pixel. No amount of cleverness gets you "more FPS *and* clean" simultaneously —
that needs hardware the S3 doesn't have.

Both JC boards sit in the same performance class: same S3, same 8 MB PSRAM, same QSPI
software-framebuffer pipeline. The 3.5" is 480×320 and the 4.3" is 480×272 — the 4.3"
actually has ~15% *fewer* pixels to push, so the experience is identical (if anything a
touch lighter on the bigger screen).

## Where the ESP32-P4 comes in

The **ESP32-P4** is the next tier, and the reason it's genuinely faster isn't clock
speed — it's silicon. It has a **PPA (a hardware 2D blitter)**, a **MIPI-DSI** display
interface and proper **double-buffering**. That combination does the one thing the S3
*architecturally cannot*: deliver **high frame-rate *and* clean, tear-free frames at the
same time**, because the blitter fills/rotates/blits in hardware (the CPU isn't touching
every pixel) and the double-buffered DSI path flips without tearing. We've bench-proven
the panel at **60–96 FPS** (at a stable 360 MHz).

The P4 costs modestly more, needs a companion radio chip for WiFi/BLE, and its GTi
firmware port is still in progress — so it's a **premium / future** tier, not a
replacement. It sits *above* the S3 JC boards, which remain the value sweet spot that
does everything a floppy-flinging UI actually needs.

---

*In one line: the S3 was chosen for USB, priced by the "£4 rule", tuned to a clean 60
because that's the practical ceiling while holding clean full-frame rendering, stability
and USB, and the P4 is where we go when we want to remove the "quality or framerate"
trade-off entirely.*
