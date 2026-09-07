#!/usr/bin/env python3
# Bake the ESP32-C6 co-processor image into a P4 full-flash merged.bin so the
# firmware self-updates the C6 from its own flash (no SD file needed).
#
#   python3 bake_c6.py <in_merged.bin> <out_merged.bin> [c6_image.bin]
#
# Writes "C6FW" + uint32 LE length + the raw C6 image into the 'c6fw' partition.
# Offset/size MUST match partitions.csv (c6fw @ 0xEB0000, size 0x140000).
import sys
IN  = sys.argv[1]
OUT = sys.argv[2]
C6  = sys.argv[3] if len(sys.argv) > 3 else "../GTi_P4_RadioCheck/c6_network_adapter_2.12.13.bin"
OFF, PSIZE = 0xEB0000, 0x140000
img = open(C6, "rb").read()
payload = b"C6FW" + len(img).to_bytes(4, "little") + img
if len(payload) > PSIZE:
    sys.exit("C6 image (%d + 8 header) exceeds c6fw partition (%d)" % (len(img), PSIZE))
data = bytearray(open(IN, "rb").read())
if len(data) < OFF + PSIZE:
    sys.exit("merged.bin is %d bytes — not a full 16MB image covering 0x%X" % (len(data), OFF + PSIZE))
data[OFF:OFF+PSIZE] = b"\xff" * PSIZE          # clear the partition region
data[OFF:OFF+len(payload)] = payload           # then write header + image
open(OUT, "wb").write(data)
print("baked %d-byte C6 image into %s at 0x%X (payload %d / partition %d)"
      % (len(img), OUT, OFF, len(payload), PSIZE))
