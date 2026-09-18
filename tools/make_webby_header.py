#!/usr/bin/env python3
"""Generate both Webby UI headers from the shared, reviewable HTML source."""
from pathlib import Path
import argparse
import gzip

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--check', action='store_true', help='fail if a header is stale')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
source = (root / 'firmware/shared/webby_ui.html').read_bytes()
packed = gzip.compress(source, compresslevel=9, mtime=0)
text = '// Generated from firmware/shared/webby_ui.html.\n'
text += '// Regenerate with: python3 tools/make_webby_header.py\n'
text += f'// Source: {len(source)} bytes, gzipped: {len(packed)} bytes\n'
text += f'const unsigned int webui_gz_len = {len(packed)};\n'
text += 'const unsigned char webui_gz[] PROGMEM = {\n'
text += ''.join('    ' + ', '.join(f'0x{x:02x}' for x in packed[i:i+12]) + ',\n'
                for i in range(0, len(packed), 12))
text += '};\n'
for board in ['Gotek_SuperMini_Webby', 'Gotek_XIAO_Webby']:
    path = root / 'firmware' / board / 'webui.h'
    if args.check:
        assert path.read_text() == text, f'Stale generated header: {path}'
    else:
        path.write_text(text)
