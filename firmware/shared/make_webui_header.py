#!/usr/bin/env python3
"""Regenerate the gzipped page headers. Run from firmware/shared/.
  webui.html                              -> webui.h       (webui_gz)
  panel_landing.html + panel_files.html   -> panel_pages.h (panel_landing_gz, panel_files_gz)
Each page is stored gzip -9 as a PROGMEM byte array and served with Content-Encoding: gzip."""
import gzip, re

def minify(src):
    """Drop HTML comments, blank lines, indentation and whole-line // comments; keep every line break,
    so JavaScript's automatic semicolons and <pre> content behave exactly as before (size-trim: -12 KB)."""
    h = re.sub(r'<!--.*?-->', '', src.decode('utf-8'), flags=re.S)
    out = []
    for line in h.split('\n'):
        t = line.strip()
        if t and not t.startswith('//'):
            out.append(t)
    return '\n'.join(out).encode('utf-8')

def arr(name, src_name, src, gz):
    out = [f'// {src_name}: {len(src)} bytes, gzipped {len(gz)} bytes',
           f'const unsigned int {name}_len = {len(gz)};',
           f'const unsigned char {name}[] PROGMEM = {{']
    for i in range(0, len(gz), 12):
        out.append('    ' + ', '.join(f'0x{b:02x}' for b in gz[i:i+12]) + ',')
    return out + ['};', '']

def build(header, pages):
    out = [f'// Auto-generated from {" + ".join(p[1] for p in pages)} - do not edit manually.',
           '// Regenerate with: python make_webui_header.py  (in firmware/shared/)']
    for name, src_name in pages:
        src = open(src_name, 'rb').read()
        gz = gzip.compress(minify(src), 9, mtime=0)
        out += arr(name, src_name, src, gz)
        print(f'{header}: {src_name} {len(src)} -> {len(gz)} bytes')
    open(header, 'w', encoding='utf-8', newline='\n').write('\n'.join(out))

build('webui.h', [('webui_gz', 'webui.html')])
build('panel_pages.h', [('panel_landing_gz', 'panel_landing.html'), ('panel_files_gz', 'panel_files.html')])
