#!/usr/bin/env node
// Cross-check of the web app's .gts code against the C side (gti-skins, Task 7).
// Runs the block between "// GTS-BEGIN" and "// GTS-END" in firmware/shared/webui.html in Node and:
//  (a) reads tools/test_skins/valid.gts with gtsUnpack (JS reads what the C tooling wrote), packs its atlas and
//      manifest plus bg.gts's 480x320 JPEG with gtsPack into tools/test_skins/js_made.gts, and writes the
//      lengths to js_made.txt for test_js_made_file_parses in tools/test_skin_gts.cpp;
//  (b) prints GTS_SLOTS and fails on any difference from skin::SLOTS in firmware/shared/skin_draw.h;
//  (c) negative cases: gtsPack / gtsUnpack throw with a message, gtsCheckAtlas / gtsCheckBg words are exact.
// Run from anywhere: node tools/gts_fixture.js && sh tools/test_skin.sh
'use strict';
const fs = require('fs');
const path = require('path');

const ROOT = path.join(__dirname, '..');
const SK = path.join(ROOT, 'tools', 'test_skins');
let fails = 0, runs = 0;
function check(ok, what) { ++runs; if (!ok) { ++fails; console.log('FAIL ' + what); } }
function throws(fn, what) {
  try { fn(); } catch (e) { check(e instanceof Error && e.message.length > 0, what + ' (message: ' + e.message + ')'); return e.message; }
  check(false, what + ' did not throw');
  return '';
}

const html = fs.readFileSync(path.join(ROOT, 'firmware', 'shared', 'webui.html'), 'utf8');
const m = html.match(/\/\/ GTS-BEGIN\r?\n([\s\S]*?)\/\/ GTS-END/);
if (!m) { console.log('FAIL no // GTS-BEGIN ... // GTS-END block in firmware/shared/webui.html'); process.exit(1); }
const G = new Function(m[1] + '\nreturn {GTS_SLOTS,GTS_ATLAS_W,GTS_ATLAS_H,GTS_SLOT_VER,gtsPack,gtsUnpack,gtsCheckAtlas,gtsCheckBg,' +
  'gtsPngInfo:typeof gtsPngInfo=="function"?gtsPngInfo:null,gtsJpegInfo:typeof gtsJpegInfo=="function"?gtsJpegInfo:null};')();

// (a) JS reads the C tooling's file, then writes one for the C parser.
const valid = G.gtsUnpack(new Uint8Array(fs.readFileSync(path.join(SK, 'valid.gts'))));
check(valid.manifest && valid.manifest.name === 'TESTSKIN', 'valid.gts manifest name');
check(valid.bg.length === 0, 'valid.gts has no background');
const withBg = G.gtsUnpack(new Uint8Array(fs.readFileSync(path.join(SK, 'bg.gts'))));
check(withBg.bg.length === 1 && withBg.bg[0].id === 1, 'bg.gts has one BG_480x320');
const man = Object.assign({}, valid.manifest, { name: 'JSMADE' });
const made = G.gtsPack(man, valid.atlas, withBg.bg);
fs.writeFileSync(path.join(SK, 'js_made.gts'), made);
const manLen = made[6] | (made[7] << 8);
const back = G.gtsUnpack(made);
check(back.manifest.name === 'JSMADE' && back.atlas.length === valid.atlas.length, 'js_made round trip');
const txt = [manLen, valid.atlas.length, back.bg.length].concat(back.bg.map(b => b.id + ' ' + b.bytes.length));
fs.writeFileSync(path.join(SK, 'js_made.txt'), txt.join('\n') + '\n');
console.log('js_made.gts ' + made.length + ' bytes: manifest ' + manLen + ', atlas ' + valid.atlas.length + ', nBg ' + back.bg.length);

// (b) The slot table equals skin::SLOTS.
const lines = G.GTS_SLOTS.map(s => [s.n, s.x, s.y, s.w, s.h, s.b].join(' '));
lines.forEach(l => console.log('slot ' + l));
const hdr = fs.readFileSync(path.join(ROOT, 'firmware', 'shared', 'skin_draw.h'), 'utf8');
const tbl = hdr.match(/SLOTS\[S_COUNT\]\s*=\s*\{([\s\S]*?)\n\};/);
check(!!tbl, 'SLOTS table found in skin_draw.h');
const cSlots = tbl ? [...tbl[1].matchAll(/\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\s*\},\s*\/\/\s*(\w+)/g)]
  .map(r => [r[6], r[1], r[2], r[3], r[4], r[5]].join(' ')) : [];
check(cSlots.length === 7, 'skin_draw.h has 7 slots (got ' + cSlots.length + ')');
check(JSON.stringify(cSlots) === JSON.stringify(lines), 'GTS_SLOTS equals skin::SLOTS\n  C:  ' + cSlots.join(' | ') + '\n  JS: ' + lines.join(' | '));
const ver = hdr.match(/SLOT_VER\s*=\s*(\d+)/), aw = hdr.match(/ATLAS_W\s*=\s*(\d+)/), ah = hdr.match(/ATLAS_H\s*=\s*(\d+)/);
check(!!(ver && aw && ah), 'SLOT_VER, ATLAS_W and ATLAS_H found in skin_draw.h');
if (ver && aw && ah) check(+ver[1] === G.GTS_SLOT_VER && +aw[1] === G.GTS_ATLAS_W && +ah[1] === G.GTS_ATLAS_H, 'SLOT_VER / ATLAS_W / ATLAS_H');

// (c) Negative cases.
const good = new Uint8Array(fs.readFileSync(path.join(SK, 'valid.gts')));
const jpg = withBg.bg[0].bytes;
check(/too short/.test(throws(() => G.gtsUnpack(good.subarray(0, 5)), 'unpack 5 bytes')), 'unpack 5 bytes says too short');
check(/chunk length/.test(throws(() => G.gtsUnpack(good.subarray(0, good.length >> 1)), 'unpack truncated')), 'truncated says bad chunk length');
check(/chunk length/.test(throws(() => G.gtsUnpack(new Uint8Array(fs.readFileSync(path.join(SK, 'lying.gts')))), 'unpack lying.gts')), 'lying says bad chunk length');
const bad = good.slice(); bad[3] = 50;
check(/not a GTS1/.test(throws(() => G.gtsUnpack(bad), 'unpack bad magic')), 'bad magic says not a GTS1 skin file');
const sv = good.slice(); sv[5] = 9;
check(/slot version/.test(throws(() => G.gtsUnpack(sv), 'unpack slot version 9')), 'slot version 9 refused');
const tail = new Uint8Array(good.length + 1); tail.set(good);
check(/chunk length/.test(throws(() => G.gtsUnpack(tail), 'unpack trailing byte')), 'trailing byte refused');
const withJ = G.gtsPack(man, valid.atlas, [{ id: 2, bytes: jpg }]);
const idAt = withJ.length - jpg.length - 5;
const bid = withJ.slice(); bid[idAt] = 7;
check(/background id/.test(throws(() => G.gtsUnpack(bid), 'unpack bg id 7')), 'unpack bg id 7 refused');
const bl = withJ.slice(); bl[idAt + 1] = 255; bl[idAt + 2] = 255;
check(/chunk length/.test(throws(() => G.gtsUnpack(bl), 'unpack bg length lies')), 'bg length lie refused');
const nb = withJ.slice(); nb[idAt - 1] = 7;
check(/too many/.test(throws(() => G.gtsUnpack(nb), 'unpack nBg 7')), 'nBg 7 refused');
check(/too big/.test(throws(() => G.gtsUnpack(new Uint8Array(262145)), 'unpack 262145 bytes')), 'file over 262144 refused');
const says = (fn, want, what) => { const msg = throws(fn, what); check(want instanceof RegExp ? want.test(msg) : msg === want, what + ': got "' + msg + '"'); };
says(() => G.gtsPack(man, valid.atlas, [{ id: 0, bytes: jpg }]), 'unknown background id', 'pack bg id 0');
says(() => G.gtsPack(man, valid.atlas, [{ id: 5, bytes: jpg }]), 'unknown background id', 'pack bg id 5');
says(() => G.gtsPack(man, valid.atlas, [{ id: 1, bytes: jpg }, { id: 1, bytes: jpg }]), 'duplicate background id', 'pack duplicate bg');
says(() => G.gtsPack(man, valid.atlas, [1, 2, 3, 4, 1].map(id => ({ id, bytes: jpg }))), 'too many backgrounds', 'pack 5 backgrounds');
says(() => G.gtsPack(man, valid.atlas, [{ id: 1, bytes: valid.atlas }]), 'background is not a JPEG', 'pack PNG as background');
says(() => G.gtsPack(man, jpg, []), 'atlas is not a PNG', 'pack JPEG as atlas');
says(() => G.gtsPack(Object.assign({}, man, { author: 'x'.repeat(1100) }), valid.atlas, []), /^manifest is \d+ bytes, the limit is 1024$/, 'pack manifest over 1024');
const huge = new Uint8Array(262144); huge.set([0xFF, 0xD8, 0xFF]);
says(() => G.gtsPack(man, valid.atlas, [{ id: 1, bytes: huge }]), /^skin is \d+ bytes, the limit is 262144$/, 'pack file over 262144');

// Inspection before sending: PNG IHDR and JPEG SOFn.
check(!!G.gtsPngInfo && !!G.gtsJpegInfo, 'gtsPngInfo and gtsJpegInfo exist');
if (G.gtsPngInfo && G.gtsJpegInfo) {
  const un = f => G.gtsUnpack(new Uint8Array(fs.readFileSync(path.join(SK, f))));
  const eq = (got, want, what) => check(JSON.stringify(got) === JSON.stringify(want), what + ': ' + JSON.stringify(got));
  eq(G.gtsPngInfo(valid.atlas), { w: 256, h: 192, depth: 8, type: 6, interlace: 0 }, 'gtsPngInfo valid.gts atlas');
  eq(G.gtsPngInfo(un('palette.gts').atlas), { w: 256, h: 192, depth: 8, type: 3, interlace: 0 }, 'gtsPngInfo palette.gts atlas');
  eq(G.gtsPngInfo(un('wide.gts').atlas), { w: 300, h: 192, depth: 8, type: 6, interlace: 0 }, 'gtsPngInfo wide.gts atlas');
  eq(G.gtsPngInfo(jpg), null, 'gtsPngInfo of a JPEG');
  eq(G.gtsPngInfo(valid.atlas.subarray(0, 20)), null, 'gtsPngInfo of a cut PNG');
  eq(G.gtsJpegInfo(jpg), { w: 480, h: 320, progressive: false }, 'gtsJpegInfo bg.gts');
  eq(G.gtsJpegInfo(un('bgprog.gts').bg[0].bytes), { w: 480, h: 320, progressive: true }, 'gtsJpegInfo bgprog.gts');
  eq(G.gtsJpegInfo(un('bgwrong.gts').bg[0].bytes), { w: 100, h: 100, progressive: false }, 'gtsJpegInfo bgwrong.gts');
  eq(G.gtsJpegInfo(valid.atlas), null, 'gtsJpegInfo of a PNG');
  eq(G.gtsJpegInfo(jpg.subarray(0, 40)), null, 'gtsJpegInfo of a cut JPEG');
}
check(G.gtsCheckAtlas(300, 192) === 'atlas is 300 px wide, expected 256', 'gtsCheckAtlas 300 wide: ' + G.gtsCheckAtlas(300, 192));
check(G.gtsCheckAtlas(256, 100) === 'atlas is 100 px high, expected 192', 'gtsCheckAtlas 100 high: ' + G.gtsCheckAtlas(256, 100));
check(G.gtsCheckAtlas(256, 192) === null, 'gtsCheckAtlas 256x192 ok');
check(G.gtsCheckBg(1, 480, 320) === null && G.gtsCheckBg(4, 272, 480) === null, 'gtsCheckBg right sizes ok');
check(G.gtsCheckBg(2, 480, 320) === 'background is 480x320, expected 320x480', 'gtsCheckBg wrong size: ' + G.gtsCheckBg(2, 480, 320));
check(G.gtsCheckBg(9, 480, 320) === 'unknown background id 9', 'gtsCheckBg id 9: ' + G.gtsCheckBg(9, 480, 320));

console.log(runs + ' checks, ' + fails + ' failed');
process.exit(fails ? 1 : 0);
