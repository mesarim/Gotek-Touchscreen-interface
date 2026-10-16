#!/usr/bin/env python3
"""Writes the .gts test fixtures to tools/test_skins/ (standard library only).

valid.gts      256x192 8-bit RGBA atlas, NEO-like slots, one BG_TILE background
wide.gts       same, but the atlas is 300 px wide (loader: "atlas is 300x192, expected 256x192")
palette.gts    256x192 8-bit palette atlas (loader: not 8-bit RGBA)
truncated.gts  valid.gts cut in half (parser: bad chunk length)
lying.gts      valid.gts with atlasLen raised by 1000 (parser: bad chunk length)

The container layout mirrors gts::pack in firmware/shared/skin_gts.h, and the
slot rectangles mirror skin::SLOTS in firmware/shared/skin_draw.h.
Run from anywhere: python tools/make_test_skins.py
"""
import json
import os
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "test_skins")

SLOT_VER = 1
ATLAS_W, ATLAS_H = 256, 192
BG_TILE = 5

# skin::SLOTS (x, y, w, h, border), slot list version 1
SLOTS = {
    "KEY_UP": (0, 0, 48, 48, 10),
    "KEY_DOWN": (48, 0, 48, 48, 10),
    "PANEL": (96, 0, 48, 48, 10),
    "ROW_SEL": (144, 0, 48, 32, 8),
    "TILE": (0, 64, 64, 64, 0),
    "ICONS": (64, 64, 128, 32, 0),
    "ICONS_2X": (0, 128, 256, 64, 0),
}

MANIFEST = {
    "name": "TESTSKIN",
    "author": "fixtures",
    "fill": "#18203A",
    "light": "#4A5A8A",
    "dark": "#081020",
    "text": "#E8F0F8",
    "accent": "#F0B030",
}


def hexrgb(s):
    return tuple(int(s[i:i + 2], 16) for i in (1, 3, 5))


# --- PNG writer -------------------------------------------------------------

def _chunk(tag, data):
    c = struct.pack(">I", len(data)) + tag + data
    return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)


def png_rgba(w, h, px):
    """px: list of rows, each a list of (r, g, b, a). 8-bit, not interlaced."""
    raw = b"".join(b"\x00" + bytes(v for p in row for v in p) for row in px)
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + _chunk(b"IHDR", ihdr)
            + _chunk(b"IDAT", zlib.compress(raw, 9)) + _chunk(b"IEND", b""))


def png_rgb(w, h, px):
    raw = b"".join(b"\x00" + bytes(v for p in row for v in p[:3]) for row in px)
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + _chunk(b"IHDR", ihdr)
            + _chunk(b"IDAT", zlib.compress(raw, 9)) + _chunk(b"IEND", b""))


def png_palette(w, h, idx, pal):
    """idx: rows of palette indices; pal: list of (r, g, b). 8-bit indexed."""
    raw = b"".join(b"\x00" + bytes(row) for row in idx)
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 3, 0, 0, 0)
    plte = bytes(v for c in pal for v in c)
    return (b"\x89PNG\r\n\x1a\n" + _chunk(b"IHDR", ihdr) + _chunk(b"PLTE", plte)
            + _chunk(b"IDAT", zlib.compress(raw, 9)) + _chunk(b"IEND", b""))


# --- Pictures ---------------------------------------------------------------

def atlas_pixels(w, h):
    """NEO-like starter: see-through key faces with a 1 px edge, the rest clear."""
    fill = hexrgb(MANIFEST["fill"])
    light = hexrgb(MANIFEST["light"])
    acc = hexrgb(MANIFEST["accent"])
    text = hexrgb(MANIFEST["text"])
    px = [[(0, 0, 0, 0)] * w for _ in range(h)]

    def box(x0, y0, bw, bh, edge, face, a):
        for y in range(y0, y0 + bh):
            for x in range(x0, x0 + bw):
                if x >= w or y >= h:
                    continue
                on_edge = x in (x0, x0 + bw - 1) or y in (y0, y0 + bh - 1)
                px[y][x] = edge + (255,) if on_edge else face + (a,)

    box(*SLOTS["KEY_UP"][:4], light, fill, 196)
    box(*SLOTS["KEY_DOWN"][:4], acc, fill, 230)
    box(*SLOTS["PANEL"][:4], light, fill, 160)
    box(*SLOTS["ROW_SEL"][:4], acc, fill, 200)
    tx, ty, tw, th, _ = SLOTS["TILE"]
    for y in range(ty, ty + th):
        for x in range(tx, tx + tw):
            on = ((x - tx) // 8 + (y - ty) // 8) % 2
            px[y][x] = (fill if on else hexrgb(MANIFEST["dark"])) + (255,)
    for name, cell in (("ICONS", 16), ("ICONS_2X", 32)):
        sx, sy, sw, sh, _ = SLOTS[name]
        for cy in range(sy, sy + sh, cell):
            for cx in range(sx, sx + sw, cell):
                m = cell // 4
                for y in range(cy + m, cy + cell - m):
                    for x in range(cx + m, cx + cell - m):
                        px[y][x] = text + (255,)
    return px


def tile_pixels(n):
    a, b = hexrgb(MANIFEST["dark"]), hexrgb(MANIFEST["fill"])
    return [[(b if (x + y) % 16 == 0 else a) + (255,) for x in range(n)] for y in range(n)]


# --- Container (mirrors gts::pack) -----------------------------------------

def pack(manifest, atlas, bgs, slot_ver=SLOT_VER, atlas_len=None):
    out = bytearray(b"GTS1")
    out += bytes([0, slot_ver])
    out += struct.pack("<H", len(manifest)) + manifest
    out += struct.pack("<I", len(atlas) if atlas_len is None else atlas_len) + atlas
    out += bytes([len(bgs)])
    for bid, data in bgs:
        out += bytes([bid]) + struct.pack("<I", len(data)) + data
    return bytes(out)


def main():
    os.makedirs(OUT, exist_ok=True)
    man = json.dumps(MANIFEST, separators=(",", ":")).encode()
    bgs = [(BG_TILE, png_rgb(32, 32, tile_pixels(32)))]

    atlas = png_rgba(ATLAS_W, ATLAS_H, atlas_pixels(ATLAS_W, ATLAS_H))
    valid = pack(man, atlas, bgs)

    wide = pack(man, png_rgba(300, ATLAS_H, atlas_pixels(300, ATLAS_H)), bgs)

    pal = [hexrgb(MANIFEST[k]) for k in ("dark", "fill", "light", "accent", "text")]
    idx = [[((x // 16) + (y // 16)) % len(pal) for x in range(ATLAS_W)] for y in range(ATLAS_H)]
    palette = pack(man, png_palette(ATLAS_W, ATLAS_H, idx, pal), bgs)

    truncated = valid[: len(valid) // 2]
    lying = pack(man, atlas, bgs, atlas_len=len(atlas) + 1000)

    files = {"valid.gts": valid, "wide.gts": wide, "palette.gts": palette,
             "truncated.gts": truncated, "lying.gts": lying}
    for name, data in files.items():
        with open(os.path.join(OUT, name), "wb") as f:
            f.write(data)
        print("%-14s %6d bytes" % (name, len(data)))


if __name__ == "__main__":
    main()
