#!/usr/bin/env python3
"""Regenerate dongle_page.h from dongle_page.html (the dongle's own page at 192.168.4.1 and /classic).
Run from this folder, then run ../Gotek_Zero_Webby/make_zero.py so the S3-Zero gets the same page.
Stored gzip -9 as a PROGMEM byte array; the sketch serves it with a gzip Content-Encoding header."""
import gzip
src = open('dongle_page.html', 'rb').read()
gz = gzip.compress(src, 9, mtime=0)
out = ['// Auto-generated from dongle_page.html - do not edit manually.',
       '// Regenerate with: python make_dongle_page.py  (in firmware/Gotek_SuperMini_Webby/)',
       f'// Source: {len(src)} bytes, gzipped: {len(gz)} bytes',
       f'const unsigned int dongle_page_gz_len = {len(gz)};',
       'const unsigned char dongle_page_gz[] PROGMEM = {']
for i in range(0, len(gz), 12):
    out.append('    ' + ', '.join(f'0x{b:02x}' for b in gz[i:i+12]) + ',')
out += ['};', '']
open('dongle_page.h', 'w', encoding='utf-8', newline='\n').write('\n'.join(out))
print(f'dongle_page.h: {len(src)} -> {len(gz)} bytes')
