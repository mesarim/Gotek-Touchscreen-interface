#!/usr/bin/env python3
# Builds the simulator's demo SD card from the freely distributable games in lib_src/ -> web/library/
# (files + manifest.json). Every game keeps its licence/readme next to its disks.
import os, json, shutil, re, subprocess
from PIL import Image, ImageDraw, ImageFont, ImageFilter
HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.environ.get('LIBSRC', os.path.join(HERE, '../lib_src')); OUT = os.environ.get('LIBOUT', os.path.join(HERE, '../web/library'))
GF = '/usr/share/fonts/truetype/google-fonts/'
def F(n, s): return ImageFont.truetype(GF + n, s)

def cover_from(img_path, size=(300, 400), crop=None):
    im = Image.open(img_path).convert('RGB')
    if crop: im = im.crop(crop)
    # fit inside 300x400 on a dark background (the GTi crops letterbox bands itself)
    if im.width < size[0] and im.height < size[1]:   # small 8-bit art: scale up with clean pixels
        k = max(1, min(size[0] // im.width, size[1] // im.height)); im = im.resize((im.width * k, im.height * k), Image.NEAREST)
    im.thumbnail(size, Image.LANCZOS); return im

def cover_gen(title, sub, hue, motif):
    W, H = 300, 400
    im = Image.new('RGB', (W, H)); d = ImageDraw.Draw(im)
    import colorsys
    c0 = tuple(int(255 * v) for v in colorsys.hls_to_rgb(hue / 360, 0.10, 0.6))
    c1 = tuple(int(255 * v) for v in colorsys.hls_to_rgb(((hue + 40) % 360) / 360, 0.38, 0.75))
    for y in range(H):
        t = y / H; d.line([(0, y), (W, y)], fill=tuple(int(c0[i] + (c1[i] - c0[i]) * t) for i in range(3)))
    acc = tuple(int(255 * v) for v in colorsys.hls_to_rgb(((hue + 180) % 360) / 360, 0.62, 0.9))
    if motif == 'grid':
        for k in range(0, 9): yy = 250 + k * k * 2; d.line([(0, yy), (W, yy)], fill=acc, width=1)
        for k in range(-6, 7): d.line([(W // 2 + k * 8, 250), (W // 2 + k * 60, H)], fill=acc, width=1)
    elif motif == 'bricks':
        for r in range(5):
            for c in range(6): d.rectangle([12 + c * 47, 190 + r * 18, 12 + c * 47 + 42, 190 + r * 18 + 13], fill=tuple(int(255 * v) for v in colorsys.hls_to_rgb(((hue + r * 50) % 360) / 360, 0.55, 0.8)))
        d.ellipse([140, 320, 156, 336], fill=(255, 255, 255)); d.rectangle([110, 370, 190, 380], fill=acc)
    elif motif == 'cave':
        pts = [(0, H)] + [(x, 230 + int(40 * __import__('math').sin(x / 23.0) + 25 * __import__('math').sin(x / 7.0))) for x in range(0, W + 1, 10)] + [(W, H)]
        d.polygon(pts, fill=(20, 14, 10)); d.polygon([(150, 150), (138, 180), (162, 180)], fill=acc)
    elif motif == 'gold':
        for i in range(14): x = (i * 53) % W; y = 200 + (i * 37) % 170; d.ellipse([x, y, x + 18, y + 18], fill=(255, 200, 40), outline=(160, 110, 0))
    elif motif == 'trails':
        d.line([(20, 360), (20, 220), (140, 220), (140, 300), (260, 300)], fill=(0, 230, 255), width=5)
        d.line([(280, 200), (200, 200), (200, 340), (60, 340)], fill=(255, 120, 40), width=5)
    elif motif == 'blades':
        for k in range(6): d.line([(40 + k * 40, 380), (90 + k * 30, 200)], fill=acc, width=3)
    d.rectangle([6, 6, W - 7, H - 7], outline=acc, width=3)
    f = F('Poppins-Bold.ttf', 40)
    words = title.split(); lines = []; cur = ''
    for w in words:
        if d.textlength((cur + ' ' + w).strip(), font=f) > W - 34: lines.append(cur); cur = w
        else: cur = (cur + ' ' + w).strip()
    lines.append(cur)
    while any(d.textlength(l, font=f) > W - 30 for l in lines) and f.size > 20: f = F('Poppins-Bold.ttf', f.size - 2)
    y = 34
    for l in lines:
        tw = d.textlength(l, font=f); d.text(((W - tw) / 2 + 2, y + 2), l, font=f, fill=(0, 0, 0)); d.text(((W - tw) / 2, y), l, font=f, fill=(255, 255, 255)); y += f.size + 6
    fs = F('Poppins-Medium.ttf', 16); tw = d.textlength(sub, font=fs); d.text(((W - tw) / 2, y + 6), sub, font=fs, fill=acc)
    return im

G = [  # folder name, disks [(src,name)], cover, nfo, extra files [(src, name)], credit html
 dict(name='Nippon Safes Inc', disks=[f'nippon/nippon-disk{i}.adf' for i in range(1, 6)],
      cover=('pdf', 'nippon_man/NS-box.pdf'), extra=[('nippon/readme.txt', 'readme.txt')],
      nfo="Nippon Safes Inc.\n\nComedy point-and-click adventure from the Italian studio Dynabyte. Three luckless crooks, one city full of trouble, and the \"Parallaction\" system that lets you switch between them.\n\nYear: 1992\nPublisher: Dynabyte Software\nGenre: Adventure\nDisks: 5\n\nFreely distributable: released by the authors through ScummVM. Game content (C) Dynabyte Software. See readme.txt.\n",
      credit='<b>Nippon Safes Inc.</b> © Dynabyte Software, free distribution licence (readme.txt), via <a href="https://www.scummvm.org/games/">ScummVM</a>'),
 dict(name='Enemy - Tempest of Violence', disks=['ENEMY1_V2_EN_A.adf', 'ENEMY1_V2_EN_B.adf'],
      cover=('gen', 'Enemy', 'Tempest of Violence · v2.0', 0, 'blades'),
      nfo="Enemy: Tempest of Violence (v2.0)\n\nAction adventure in the style of the late Amiga years, remastered by its author in 2022.\n\nYear: 1997 / 2022\nAuthor: André Wüthrich, Anachronia\nGenre: Action adventure\nDisks: 2\n\nFreeware - non-commercial distribution only. www.auralis.ch/cms-anachronia-en/\n",
      credit='<b>Enemy: Tempest of Violence</b> and <b>Enemy 2</b> © André Wüthrich / <a href="https://www.auralis.ch/cms-anachronia-en/">Anachronia</a>, freeware (non-commercial)'),
 dict(name='Enemy 2 - Missing in Action', disks=['ENEMY2_V2_EN_A.adf', 'ENEMY2_V2_EN_B.adf'],
      cover=('gen', 'Enemy 2', 'Missing in Action · v2.0', 200, 'blades'),
      nfo="Enemy 2: Missing in Action (v2.0)\n\nThe follow-up to Enemy, remastered in 2022.\n\nYear: 2013 / 2022\nAuthor: André Wüthrich, Anachronia\nGenre: Action adventure\nDisks: 2\n\nFreeware - non-commercial distribution only. www.auralis.ch/cms-anachronia-en/\n", credit=None),
 dict(name='MegaBall 4', disks=['megaball/megaball4_1.adf', 'megaball/megaball4_2.adf', 'megaball/megaball4_3.adf'],
      cover=('gen', 'MegaBall 4', 'Ed & Al Mackey · 1995', 30, 'bricks'), extra=[('megaball/megaball_readme.html', 'megaball_readme.html'), ('MegaBall4.readme', 'MegaBall4.readme')],
      nfo="MegaBall 4\n\nThe famous Amiga breakout game by Ed and Al Mackey, registered version.\n\nNote: MegaBall installs to a hard drive (use the installer on disk 1); it does not boot straight from floppy.\n\nYear: 1995\nAuthors: Ed Mackey, Al Mackey\nGenre: Breakout\nDisks: 3\n\nLicence: Apache 2.0.\n",
      rtfm="[About]\nMegaBall 4 by Ed and Al Mackey - registered version, now under the Apache 2.0 licence.\n\n[Installing]\nMegaBall does not run straight from the floppies. Install it to a hard drive:\n1. Boot Workbench from a hard drive.\n2. Insert disk 1 and run its installer.\n3. Insert disks 2 and 3 when asked.\n\n[Emulators]\nThe authors note that only accurate sound emulation works, because of the sound set-up routines.\n\n[Licence]\nApache License 2.0. Original readme: megaball_readme.html in this folder.\n",
      credit='<b>MegaBall 4</b> © Ed &amp; Al Mackey, Apache 2.0'),
 dict(name='Gravity Force 2', disks=['gf2/Gf2.adf'], cover=('gen', 'Gravity Force 2', 'Andersson & Kronquist · 1994', 10, 'cave'),
      nfo="Gravity Force 2\n\nTwo-player cave-flying shooter: thrust, turn, fight gravity and each other.\n\nYear: 1994\nAuthors: Jens Andersson, Jan Kronquist\nGenre: Shooter (2 players)\nDisks: 1\n\nLicence: Creative Commons BY-SA. www.lysator.liu.se/~jensa/gf2/\n",
      credit='<b>Gravity Force 2</b> © Jens Andersson &amp; Jan Kronquist, <a href="https://www.lysator.liu.se/~jensa/gf2/">CC BY-SA</a>'),
 dict(name='Solid Gold', disks=['SolidGold.adf'], cover=('gen', 'Solid Gold', 'Night Owl Design · 2013', 45, 'gold'), extra=[('SolidGold.readme', 'SolidGold.readme')],
      nfo="Solid Gold\n\nA jump'n run with 8-way soft scrolling in the style of the early 90s: ten levels across four worlds, each hiding the artefact that leads to the next.\n\nYear: 2013\nAuthors: Frank Wille, Gerrit Wille, Pierre Horn (Night Owl Design)\nGenre: Platformer\nDisks: 1\n\nFreeware; source and data public domain.\n",
      credit='<b>Solid Gold</b> by Night Owl Design, freeware / public domain'),
 dict(name="L'Abbaye des Morts", disks=['LAbbayeDesMorts.adf'], cover=('img', 'abbaye_cover.jpg'), extra=[('LAbbayeDesMorts.readme', 'LAbbayeDesMorts.readme')],
      nfo="L'Abbaye des Morts\n\nPlatform adventure adapted from Locomalito's original: 23 screens to explore, riddles and hints to find items, about 30 minutes once mastered.\n\nYear: 2010 (Amiga port 2023)\nAuthors: Locomalito, Gryzor87; Amiga port by jeegfro, music by JMD\nGenre: Platformer\nDisks: 1\n\nLicence: GNU GPL (Amiga port), CC BY (original).\n",
      rtfm="[Controls]\nJoystick in port 2.\n\n[Keys - menu only]\ni - information\ns - sound effects only, or effects + music\nEsc - quit the game\n\n[Needs]\nAny Amiga with 1 MB of memory, Kickstart 1.3 or later.\n\n[Credits]\nOriginal game by Locomalito and Gryzor87. Amiga port by jeegfro, music by Simone \"JMD\" Bernacchia.\n",
      credit="<b>L'Abbaye des Morts</b> by <a href=\"https://www.locomalito.com/abbaye_des_morts.php\">Locomalito &amp; Gryzor87</a> (CC BY, cover art by Gryzor87); Amiga port by jeegfro, GPL - <a href=\"https://aminet.net/package/game/actio/LAbbayeDesMorts\">Aminet</a>"),
 dict(name='Uwol - Quest for Money', disks=['Uwol-Quest_for_money.adf'], cover=('img', 'uwol00.png'), extra=[('Uwol-Quest_for_money.readme', 'Uwol-Quest_for_money.readme')],
      nfo="Uwol: Quest for Money\n\nThe Mojon Twins' 8-bit platformer, ported to every Amiga with 1 MB.\n\nYear: 2018\nAuthors: The Mojon Twins; Amiga port by IceVAN (Purples Studios), music by Fireboy\nGenre: Platformer\nDisks: 1\n\nLicence: CC BY-NC-SA 3.0.\n",
      credit='<b>Uwol</b> and <b>Sir Ababol</b> by <a href="https://www.mojontwins.com/">The Mojon Twins</a> (CC BY-NC-SA 3.0, cover art from mojontwins.com); Amiga ports by IceVAN / Purples Studios and Tom &amp; Birra'),
 dict(name='Sir Ababol', disks=['SirAbabol.adf'], cover=('img', 'ababol01.png'), extra=[('SirAbabol.readme', 'SirAbabol.readme')],
      nfo="Sir Ababol\n\nThe Mojon Twins' platformer, ported to the Amiga in Blitz Basic. Runs on every Amiga with 512 KB.\n\nYear: 2018\nAuthors: The Mojon Twins; Amiga port by Tom & Birra, music by Zoltar\nGenre: Platformer\nDisks: 1\n\nLicence: CC BY-NC-SA 3.0.\n", credit=None),
 dict(name='cyberTRON', disks=['cyberTRON.adf'], cover=('gen', 'cyberTRON', 'Gustaf Stechmann · 1992', 190, 'trails'), extra=[('cyberTRON.readme', 'cyberTRON.readme')],
      nfo="cyberTRON\n\nLight-cycles for 2 to 4 players, written in Blitz Basic 2 in 1992 to go with a home-made four-player adapter.\n\nYear: 1992\nAuthor: Gustaf Stechmann\nGenre: Action (2-4 players)\nDisks: 1\n\nFreeware.\n",
      credit='<b>cyberTRON</b> © Gustaf Stechmann, freeware'),
]

shutil.rmtree(OUT, ignore_errors=True); os.makedirs(OUT)
man = {'files': [], 'credits': ''}
def put(path, src_bytes):
    url = 'f%03d%s' % (len(man['files']), os.path.splitext(path)[1].lower())
    open(os.path.join(OUT, url), 'wb').write(src_bytes)
    man['files'].append({'path': path, 'url': url, 'size': len(src_bytes), 'date': '2026-10-04'})
for g in G:
    base = 'ADF/' + g['name'] + '/'; n = len(g['disks'])
    for i, s in enumerate(g['disks']):
        nm = g['name'] + ('.adf' if n == 1 else ' (Disk %d of %d).adf' % (i + 1, n))
        put(base + nm, open(os.path.join(SRC, s), 'rb').read())
    c = g['cover']
    if c[0] == 'pdf':
        subprocess.run(['pdftoppm', '-r', '110', '-png', '-f', '1', '-l', '1', os.path.join(SRC, c[1]), '/tmp/_cov'], check=True)
        im = cover_from('/tmp/_cov-1.png')
    elif c[0] == 'img': im = cover_from(os.path.join(SRC, c[1]))
    else: im = cover_gen(c[1], c[2], c[3], c[4])
    from io import BytesIO
    b = BytesIO(); im.save(b, 'JPEG', quality=86, progressive=False); put(base + g['name'] + '.jpg', b.getvalue())
    put(base + g['name'] + '.nfo', g['nfo'].replace('\n', '\r\n').encode('latin-1', 'replace'))
    if g.get('rtfm'): put(base + g['name'] + '.rtfm', g['rtfm'].replace('\n', '\r\n').encode('latin-1', 'replace'))
    for s, nm in g.get('extra', []): put(base + nm, open(os.path.join(SRC, s), 'rb').read())
# demo card: the screensaver comes on after 45 s idle (hidden key SS_IDLE; the board default is 10 min)
put('CONFIG.TXT', b'# GTi simulator demo card\r\nSS_IDLE=45\r\n')
# the QR slides for the screensaver (flasher / GitHub / Discord)
qd = os.environ.get('QRDIR', os.path.join(HERE, 'qr_slides'))
for f in sorted(os.listdir(qd)): put('screensaver/' + f, open(os.path.join(qd, f), 'rb').read())
man['credits'] = 'Demo games: ' + '; '.join(g['credit'] for g in G if g.get('credit')) + '. All are freely distributable; their licences and readmes are on the card next to the disks.'
json.dump(man, open(os.path.join(OUT, 'manifest.json'), 'w'), indent=1)
print(len(man['files']), 'files', sum(f['size'] for f in man['files']) // 1024, 'KB')
