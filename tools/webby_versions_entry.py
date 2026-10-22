#!/usr/bin/env python3
# webby_versions_entry.py - the OTA fields of a docs/firmware/versions.json build entry for a Webby app image.
#
#   python tools/webby_versions_entry.py <app.bin> [--app-path firmware/supermini-beta/X-update.bin]
#                                        [--versions docs/firmware/versions.json --v Webby-1.6.14-supermini --add]
#
# Reads the app image (the *.ino.bin, NOT the merged one), checks it the way the dongle's OTA guard does
# (byte0 0xE9, chip id ESP32-S3, 4 MB flash nibble, GTIKIND:<board>; marker, 1.6.14+) and prints
# size, sha256, board_id, guard. With --versions/--v/--add it writes them into the existing entry named --v
# (device key found from the entry), so nobody types a hash by hand. It never creates an entry or touches other fields.
import argparse, hashlib, json, re, sys, pathlib

BOARD_ID = {"supermini": 1, "zero": 2, "xiao": 3}   # same numbers as pad[2] / board_id on the dongle
FLASH_MB = {1: 2, 2: 4, 3: 8, 4: 16}                 # flash-size nibble -> MB (esptool header: 0=1,1=2,2=4,3=8,4=16)

def inspect(data):
    if len(data) < 112:
        sys.exit("not a firmware image (too short)")
    if data[0] != 0xE9:
        sys.exit("not a firmware image (byte0 is not 0xE9)")
    chip = data[12] | (data[13] << 8)
    if chip != 0x0009:
        sys.exit("wrong chip: 0x%04x, expected 0x0009 (ESP32-S3)" % chip)
    mb = {0: 1, 1: 2, 2: 4, 3: 8, 4: 16}.get(data[3] >> 4, "?")
    marks = [m.group(1).decode() for m in re.finditer(rb"GTIKIND:([a-z0-9]+);", data)]
    return mb, marks

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("bin")
    ap.add_argument("--app-path", help="path of the app image under docs/ as versions.json will list it")
    ap.add_argument("--versions"); ap.add_argument("--v"); ap.add_argument("--add", action="store_true")
    a = ap.parse_args()
    data = pathlib.Path(a.bin).read_bytes()
    mb, marks = inspect(data)
    fields = {"size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
    if a.app_path:
        fields = {"app": a.app_path, **fields}
    if len(set(marks)) == 1 and marks[0] in BOARD_ID:
        fields["board_id"] = BOARD_ID[marks[0]]; fields["guard"] = True
    elif not marks:
        fields["guard"] = False   # pre-guard image: no marker, a 1.6.14+ dongle refuses it
        print("note: no GTIKIND marker - pre-guard image, board_id left out", file=sys.stderr)
    else:
        sys.exit("conflicting or unknown GTIKIND markers: %s" % sorted(set(marks)))
    print("flash %s MB, marker %s" % (mb, marks[0] if marks else "none"), file=sys.stderr)
    if not (a.versions and a.v and a.add):
        print(json.dumps(fields, indent=2)); return
    p = pathlib.Path(a.versions); vj = json.loads(p.read_text(encoding="utf-8"))
    hits = [(k, b) for k, d in vj.items() if isinstance(d, dict) for b in d.get("builds", []) if b.get("v") == a.v]
    if len(hits) != 1:
        sys.exit("entry %r found %d times in %s - nothing written" % (a.v, len(hits), a.versions))
    hits[0][1].update(fields)
    p.write_text(json.dumps(vj, indent=2, ensure_ascii=False) + "\n", encoding="utf-8", newline="")
    print("updated %s in %s (device %s)" % (a.v, a.versions, hits[0][0]), file=sys.stderr)

main()
