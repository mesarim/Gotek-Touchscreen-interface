# Ideas

Concept mockups and explorations — **not shipped firmware, and not linked from the flasher page**.

## low-vision-big-mode.html
An interactive concept for a high-contrast, large-target **"BIG" accessibility layout** for the
Waveshare 7"/7B (prompted by a legally-blind first tester). Toggle device (7" / 7B), palette
(yellow-on-black / black-on-yellow / white-on-black), layout (big cover / big list), and a
Standard ↔ Big scale to see the before/after. Callout chips show the measured touch-target size
in mm and the contrast ratio for the chosen palette. Sized to WCAG 2.2 target-size guidance and
CNIB/ACB clear-print recommendations. Cover art is placeholder (tap the screen to cycle).

## cracktro-gti-format.md
A proposal for **custom cracktros as loadable `.gti` files** — a short list of RSI-Demomaker-style
"patterns", each one a registry effect plus your own text, colours and 1bpp logo, in a single
self-contained text file that fits in a Discord message. Also covers a fourth screensaver mode
(`SSMODE=CRACKTRO`), making effects pluggable via a registry instead of a `switch`, and a browser
builder whose preview runs the real engine through WebAssembly so it matches the panel exactly.
Draft for an upstream issue; not built yet.
