#!/usr/bin/env node
// Test of the GitHub Pages skin editor (docs/skin-editor.html, docs/skins/editor/*) against the panel's web page.
// The editor carries copies of the panel page's .gts code (docs/skins/editor/gts.js). This test fails when they drift:
//  (a) the block GTS-BEGIN..GTS-END and gtsDrawStarter (STARTER-BEGIN..STARTER-END) are the same text in both files;
//  (b) the starter skin (NEO colours, the manifest the panel page builds, the atlas of tools/test_skins/valid.gts, a 480x320
//      background) packs to identical bytes in the editor's code and in the panel page's code;
//  (c) unpack of tools/test_skins/valid.gts and bg.gts, and pack of what came out, give the file back byte for byte;
//  (d) the editor's colour presets and style normaliser give what the panel page's do;
//  (e) the renderer (skin-draw.js) loads in Node and draws every screen, landscape and portrait, and its slot table is the firmware's.
// Run: node tools/test_skin_editor.js
'use strict';
const fs = require('fs');
const path = require('path');

const ROOT = path.join(__dirname, '..');
const read = p => fs.readFileSync(path.join(ROOT, p), 'utf8').replace(/\r\n/g, '\n');
let fails = 0, runs = 0;
function check(ok, what) { ++runs; if (!ok) { ++fails; console.log('FAIL ' + what); } }
const same = (a, b) => Buffer.compare(Buffer.from(a), Buffer.from(b)) === 0;

const panel = read('firmware/shared/webui.html');
const editor = read('docs/skins/editor/gts.js');
const grab = (src, from, to) => { const m = src.match(new RegExp('// ' + from + '\\n[\\s\\S]*?// ' + to + '\\n')); return m ? m[0] : null; };

// (a) the copies are word for word
const pGts = grab(panel, 'GTS-BEGIN', 'GTS-END'), eGts = grab(editor, 'GTS-BEGIN', 'GTS-END');
const pSt = grab(panel, 'STARTER-BEGIN', 'STARTER-END'), eSt = grab(editor, 'STARTER-BEGIN', 'STARTER-END');
check(!!pGts && !!eGts, 'GTS-BEGIN..GTS-END found in webui.html and in gts.js');
check(!!pSt && !!eSt, 'STARTER-BEGIN..STARTER-END found in webui.html and in gts.js');
check(pGts === eGts, 'the GTS block of gts.js is the one of webui.html (they must stay in step)');
check(pSt === eSt, 'gtsDrawStarter of gts.js is the one of webui.html (they must stay in step)');
if (!pGts || !eGts || !pSt || !eSt) { console.log(runs + ' checks, ' + fails + ' failed'); process.exit(1); }

// The panel page's side: its GTS block, its TE_PRESETS and teNormalizeStyle, cut out of the page text and run here.
const grabFn = (src, start, endMarker) => { const a = src.indexOf(start); const b = src.indexOf(endMarker, a); return a < 0 || b < 0 ? null : src.slice(a, b); };
const pPresets = grabFn(panel, 'const TE_PRESETS = {', '};\n');
const pNorm = grabFn(panel, 'function teNormalizeStyle(s) {', '\nfunction teLoadStyle');
check(!!pPresets && !!pNorm, 'TE_PRESETS and teNormalizeStyle found in webui.html');
const PANEL = new Function(pGts + (pPresets || '') + '};\n' + (pNorm || '') +
  '\nreturn {gtsPack,gtsUnpack,TE_PRESETS,teNormalizeStyle,GTS_SLOTS};')();
// ... and the editor's: all of gts.js (the drawing function is only defined, never called, so no canvas is needed)
const EDITOR = new Function(editor + '\nreturn {gtsPack,gtsUnpack,TE_PRESETS,teNormalizeStyle,skinManifest,GTS_SLOTS,gtsDrawStarter};')();

// What the panel page packs: skinSend()'s expression, taken from the page text so a change there shows up here
const expr = panel.match(/gtsPack\((Object\.assign\(\{author:''\},man,asIs\?\{\}:teStyle\(\),\{name\}\)),atlas,bgs\)/);
check(!!expr, "skinSend's Object.assign({author:''},man,asIs?{}:teStyle(),{name}) found in webui.html");
const panelManifest = (man, style, name) => new Function('man', 'asIs', 'teStyle', 'name', 'return ' + expr[1] + ';')(man, false, () => style, name);

// (b) the starter skin packs to the same bytes
const SK = path.join(ROOT, 'tools', 'test_skins');
const valid = PANEL.gtsUnpack(new Uint8Array(fs.readFileSync(path.join(SK, 'valid.gts'))));
const withBg = PANEL.gtsUnpack(new Uint8Array(fs.readFileSync(path.join(SK, 'bg.gts'))));
const style = PANEL.teNormalizeStyle(PANEL.TE_PRESETS.NEO);
check(JSON.stringify(EDITOR.teNormalizeStyle(EDITOR.TE_PRESETS.NEO)) === JSON.stringify(style), 'the NEO style is the same in the editor and in the panel page');
const pm = panelManifest({}, style, 'NEO'), em = EDITOR.skinManifest({}, EDITOR.teNormalizeStyle(EDITOR.TE_PRESETS.NEO), 'NEO', '');
check(JSON.stringify(Object.keys(pm)) === JSON.stringify(Object.keys(em)), 'manifest key order: panel ' + Object.keys(pm) + ' / editor ' + Object.keys(em));
for (const bgs of [[], withBg.bg]) {
  const a = PANEL.gtsPack(pm, valid.atlas, bgs), b = EDITOR.gtsPack(em, valid.atlas, bgs);
  check(same(a, b), 'starter skin (' + bgs.length + ' background) packs to identical bytes: panel ' + a.length + ' / editor ' + b.length);
  check(a[0] === 71 && a[1] === 84 && a[2] === 83 && a[3] === 49, 'packed file starts with GTS1');
}
// the same with an author and an imported manifest's own keys: the editor puts author first, like the panel's {author:''} default
const odd = { custom: 'kept', author: 'old' };
const pm2 = panelManifest(odd, style, 'MINE'), em2 = EDITOR.skinManifest(odd, style, 'MINE', 'old');
check(same(PANEL.gtsPack(pm2, valid.atlas, []), EDITOR.gtsPack(em2, valid.atlas, [])), 'a manifest with extra keys packs to identical bytes');

// (c) unpack / pack round trip of the fixtures
for (const f of ['valid.gts', 'bg.gts']) {
  const orig = new Uint8Array(fs.readFileSync(path.join(SK, f)));
  const u = EDITOR.gtsUnpack(orig);
  check(u.atlas.length > 8 && u.manifest && typeof u.manifest === 'object', f + ' unpacks in the editor');
  const again = EDITOR.gtsPack(u.manifest, u.atlas, u.bg);
  check(same(again, orig), f + ' unpack then pack gives the file back byte for byte (' + orig.length + ' bytes)');
  const u2 = EDITOR.gtsUnpack(again);
  check(JSON.stringify(u2.manifest) === JSON.stringify(u.manifest) && same(u2.atlas, u.atlas) && u2.bg.length === u.bg.length, f + ' second round trip equal');
}
const v = EDITOR.gtsUnpack(new Uint8Array(fs.readFileSync(path.join(SK, 'valid.gts'))));
check(v.manifest.name === 'TESTSKIN' && v.bg.length === 0, 'valid.gts: name TESTSKIN, no background');
for (const f of ['wide.gts', 'truncated.gts', 'lying.gts']) {
  let msg = ''; try { EDITOR.gtsUnpack(new Uint8Array(fs.readFileSync(path.join(SK, f)))); } catch (e) { msg = e.message; }
  if (f === 'wide.gts') { check(msg === '', 'wide.gts unpacks (the size is checked by gtsCheckAtlas)'); }
  else check(/chunk length/.test(msg), f + ' is refused with "bad chunk length" (got "' + msg + '")');
}

// (d) presets: every preset the editor offers that the panel page has too is the same
for (const k of Object.keys(EDITOR.TE_PRESETS)) {
  if (!(k in PANEL.TE_PRESETS)) { check(false, 'preset ' + k + ' of the editor is not in the panel page'); continue; }
  check(JSON.stringify(EDITOR.TE_PRESETS[k]) === JSON.stringify(PANEL.TE_PRESETS[k]), 'preset ' + k + ' equals the panel page');
}
for (const s of [{}, null, { fill: 'red', bevelW: 99, radius: -4, scale: 'x', bevel: 'sunk' }, { fill: '#ABCDEF', text: '#12', scale: 9 }]) {
  check(JSON.stringify(EDITOR.teNormalizeStyle(s)) === JSON.stringify(PANEL.teNormalizeStyle(s)), 'teNormalizeStyle(' + JSON.stringify(s) + ') equals the panel page');
}

// (e) the renderer
const hdr = read('firmware/shared/skin_draw.h');
const cSlots = [...hdr.match(/SLOTS\[S_COUNT\]\s*=\s*\{([\s\S]*?)\n\};/)[1].matchAll(/\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\s*\},\s*\/\/\s*(\w+)/g)]
  .map(r => [r[6], r[1], r[2], r[3], r[4], r[5]].join(' '));
check(JSON.stringify(EDITOR.GTS_SLOTS.map(s => [s.n, s.x, s.y, s.w, s.h, s.b].join(' '))) === JSON.stringify(cSlots), 'GTS_SLOTS equals skin::SLOTS in skin_draw.h');
const Draw = new Function('GTS_SLOTS', read('docs/skins/editor/skin-draw.js') + '\nreturn GtiDraw;')(EDITOR.GTS_SLOTS);
const solid = (r, g, b, a) => { const d = new Uint8ClampedArray(256 * 192 * 4); for (let i = 0; i < 256 * 192; i++) d.set([r, g, b, a], i * 4); return d; };
const atlasEmpty = Draw.atlasFromRGBA(solid(0, 0, 0, 0), 256, 192), atlasFull = Draw.atlasFromRGBA(solid(200, 100, 50, 255), 256, 192);
for (const sc of Draw.SCREENS) for (const portrait of [false, true]) for (const atlas of [atlasEmpty, atlasFull]) {
  const f = Draw.render(sc.id, { portrait, style, atlas });
  check(f.w === (portrait ? 320 : 480) && f.h === (portrait ? 480 : 320) && f.px.length === f.w * f.h, sc.id + (portrait ? ' portrait' : ' landscape') + ' renders at its real size');
}
const pieces = Draw.slice9({ x: 0, y: 0, w: 48, h: 48, border: 10 }, 120, 48);
check(pieces.length === 9 && pieces.reduce((t, p) => t + p.dw * p.dh, 0) === 120 * 48, 'slice9 covers a 120 x 48 destination exactly');
check(Draw.blend565(0xFFFF, 0, 0x1234) === 0x1234 && Draw.blend565(0xFFFF, 255, 0x1234) === 0xFFFF, 'blend565 ends');
// a full atlas: the selected row of the list is drawn from ROW_SEL, a clear one falls back to the classic row (amber ring)
const lf = Draw.render('list', { portrait: false, style, atlas: atlasFull }), le = Draw.render('list', { portrait: false, style, atlas: atlasEmpty });
check(!same(new Uint8Array(lf.px.buffer), new Uint8Array(le.px.buffer)), 'a different atlas gives a different list');

console.log(runs + ' checks, ' + fails + ' failed');
process.exit(fails ? 1 : 0);
