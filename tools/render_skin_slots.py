#!/usr/bin/env python3
"""Prints the skin slot table (markdown) from firmware/shared/skin_draw.h.

Usage: python tools/render_skin_slots.py [path/to/skin_draw.h]
The header is the single source of truth; docs/skins/README.md pastes this output.
"""
import re
import sys
from pathlib import Path

HEADER = Path(__file__).resolve().parent.parent / "firmware" / "shared" / "skin_draw.h"

USE = {
    "KEY_UP": "Key face, normal. Every button, stretched to its size (9-slice).",
    "KEY_DOWN": "Key face, pressed. Same size and border as KEY_UP.",
    "PANEL": "Cover and info panel background (9-slice).",
    "ROW_SEL": "The selected row in a list (9-slice).",
    "TILE": "Tiny repeating background, used when the skin has no background picture for the screen's size and orientation.",
    "ICONS": "Icon grid, 8 x 2 cells of 16 x 16. Reserved for a later version: the screen does not draw it yet.",
    "ICONS_2X": "Icon grid for large screens, 8 x 2 cells of 32 x 32. Reserved for a later version: the screen does not draw it yet.",
}


def main():
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else HEADER
    text = src.read_text(encoding="utf-8")
    ver = int(re.search(r"SLOT_VER\s*=\s*(\d+)", text).group(1))
    aw = int(re.search(r"ATLAS_W\s*=\s*(\d+)", text).group(1))
    ah = int(re.search(r"ATLAS_H\s*=\s*(\d+)", text).group(1))
    body = re.search(r"SLOTS\[S_COUNT\]\s*=\s*\{(.*?)\n\};", text, re.S).group(1)
    rows = re.findall(r"\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\},\s*//\s*(\w+)", body)
    print(f"Slot list version {ver}, atlas {aw} x {ah} pixels.\n")
    print("| Slot | x | y | width | height | 9-slice border | What it is |")
    print("|---|---|---|---|---|---|---|")
    for x, y, w, h, b, name in rows:
        print(f"| `{name}` | {x} | {y} | {w} | {h} | {b if int(b) else '-'} | {USE.get(name, '')} |")


if __name__ == "__main__":
    main()
