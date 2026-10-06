#!/usr/bin/env python3
"""Regenerate webui.h from webui.html (gzip -9, PROGMEM byte array). Run from firmware/shared/."""
import gzip, sys
src = open('webui.html', 'rb').read()
gz = gzip.compress(src, 9, mtime=0)
out = ['// Auto-generated from webui.html — do not edit manually.',
       '// Regenerate with: python make_webui_header.py  (in firmware/shared/)',
       f'// Source: {len(src)} bytes, Gzipped: {len(gz)} bytes',
       f'const unsigned int webui_gz_len = {len(gz)};',
       'const unsigned char webui_gz[] PROGMEM = {']
for i in range(0, len(gz), 12):
    out.append('    ' + ', '.join(f'0x{b:02x}' for b in gz[i:i+12]) + ',')
out += ['};', '']
open('webui.h', 'w', encoding='utf-8', newline='\n').write('\n'.join(out))
print(f'webui.h: {len(src)} -> {len(gz)} bytes')
