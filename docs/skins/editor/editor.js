// GTi skin editor: the page. State, the controls, the live preview, the export.
// Needs gts.js (the .gts container and the starter, copied from the panel's web page) and skin-draw.js (the renderer).
'use strict';

const $ = id => document.getElementById(id);

// The sizes the format allows (GTS_BG), with what they are for
const BG_INFO = {
  1: { label: '480 × 320', note: 'landscape: shown in this preview', preview: true },
  2: { label: '320 × 480', note: 'portrait: shown in this preview (Portrait)', preview: true },
  3: { label: '480 × 272', note: 'landscape, for 480 × 272 screens: not shown here', preview: false },
  4: { label: '272 × 480', note: 'portrait, for 272 × 480 screens: not shown here', preview: false },
};

// ---- state -----------------------------------------------------------------------------------------------------------------------
const state = {
  man: {},                 // keys of a loaded manifest that the editor does not edit
  style: teNormalizeStyle(TE_PRESETS.NEO),
  atlasPng: null,          // Uint8Array: the atlas as it goes into the .gts
  atlasRGBA: null,         // Uint8ClampedArray 256 x 192 x 4, decoded from atlasPng
  bgs: {},                 // id -> { bytes, w, h, px (RGB565), url }
  screen: 'list', portrait: false, zoom: 1,
};

// ---- small helpers -----------------------------------------------------------------------------------------------------------------
function gtsCanvas(w, h) { const cv = document.createElement('canvas'); cv.width = w; cv.height = h; return cv; }
async function gtsBytes(cv, type, q) { return new Uint8Array(await (await new Promise(r => cv.toBlob(r, type, q))).arrayBuffer()); }
function skinDec(b) { return createImageBitmap(b instanceof Blob ? b : new Blob([b]), { premultiplyAlpha: 'none', colorSpaceConversion: 'none' }); }
function skinDraw(im, w, h) { const cv = gtsCanvas(w, h); cv.getContext('2d').drawImage(im, 0, 0); return cv; }
function say(text, kind) { const m = $('msg'); m.textContent = text || ''; m.className = 'msg ' + (kind || ''); }
function kb(n) { return n < 10240 ? (n / 1024).toFixed(1) + ' KB' : Math.round(n / 1024) + ' KB'; }
function download(bytes, file, type) {
  const a = document.createElement('a');
  a.href = URL.createObjectURL(new Blob([bytes], { type: type || 'application/octet-stream' }));
  a.download = file; document.body.appendChild(a); a.click(); a.remove();
  setTimeout(() => URL.revokeObjectURL(a.href), 10000);
}
// skinName of the panel page
function skinName() {
  const n = $('t-name').value.toUpperCase().trim();
  if (!/^[A-Z_][A-Z0-9_]{0,15}$/.test(n)) gtsErr('Skin name: A-Z, 0-9 and _ only, max 16, not starting with a digit');
  return n;
}

// ---- PNG -> RGBA -------------------------------------------------------------------------------------------------------------------
// Exact decode of an 8-bit RGBA PNG (what a skin atlas is), so a half-transparent pixel is not rounded by the browser's
// premultiplied canvas. Anything else, or a browser without DecompressionStream, goes through the canvas.
async function pngDecode(b) {
  const pi = gtsPngInfo(b);
  if (pi && pi.depth === 8 && pi.type === 6 && !pi.interlace && typeof DecompressionStream === 'function') {
    try {
      const v = new DataView(b.buffer, b.byteOffset, b.length), parts = [];
      for (let p = 8; p + 8 <= b.length;) {
        const len = v.getUint32(p), type = String.fromCharCode(b[p + 4], b[p + 5], b[p + 6], b[p + 7]);
        if (type === 'IDAT') parts.push(b.subarray(p + 8, p + 8 + len));
        if (type === 'IEND') break;
        p += 12 + len;
      }
      const raw = new Uint8Array(await new Response(new Blob(parts).stream().pipeThrough(new DecompressionStream('deflate'))).arrayBuffer());
      const w = pi.w, h = pi.h, stride = w * 4, out = new Uint8ClampedArray(h * stride);
      if (raw.length < h * (stride + 1)) throw new Error('short');
      for (let y = 0; y < h; y++) {
        const ft = raw[y * (stride + 1)], src = y * (stride + 1) + 1, o = y * stride;
        for (let i = 0; i < stride; i++) {
          const x = raw[src + i], a = i >= 4 ? out[o + i - 4] : 0, up = y ? out[o - stride + i] : 0, ul = (y && i >= 4) ? out[o - stride + i - 4] : 0;
          let r;
          if (ft === 0) r = x; else if (ft === 1) r = x + a; else if (ft === 2) r = x + up; else if (ft === 3) r = x + ((a + up) >> 1);
          else { const p = a + up - ul, pa = Math.abs(p - a), pb = Math.abs(p - up), pc = Math.abs(p - ul); r = x + (pa <= pb && pa <= pc ? a : pb <= pc ? up : ul); }
          out[o + i] = r & 255;
        }
      }
      return { w, h, data: out };
    } catch (e) { /* fall through to the canvas */ }
  }
  const im = await skinDec(b), cv = skinDraw(im, im.width, im.height);
  return { w: im.width, h: im.height, data: cv.getContext('2d').getImageData(0, 0, im.width, im.height).data };
}

// skinFix of the panel page: what the panel would refuse is fixed or refused before anything is used. Sizes are read from the files
// themselves; a PNG that is not 8-bit RGBA, or a JPEG that is not baseline, is re-encoded.
async function fixParts(atlas, bgs) {
  const pi = gtsPngInfo(atlas), chk = e => { if (e) gtsErr(e); }, out = [], notes = [];
  if (!pi) gtsErr('atlas is not a readable PNG');
  chk(gtsCheckAtlas(pi.w, pi.h));
  if (pi.depth !== 8 || pi.type !== 6 || pi.interlace) { atlas = await gtsBytes(skinDraw(await skinDec(atlas), 256, 192), 'image/png'); notes.push('the atlas was converted to 8-bit RGBA'); }
  for (const g of bgs) {
    const ji = gtsJpegInfo(g.bytes);
    if (!ji) gtsErr('background ' + GTS_BG[g.id].join('x') + ' is not a readable JPEG');
    chk(gtsCheckBg(g.id, ji.w, ji.h));
    if (ji.reencode) { out.push({ id: g.id, bytes: await gtsBytes(skinDraw(await skinDec(g.bytes), ji.w, ji.h), 'image/jpeg', .85) }); notes.push('background ' + GTS_BG[g.id].join('x') + ' was converted to a baseline JPEG'); }
    else out.push(g);
  }
  return { atlas, bgs: out, notes };
}

// ---- setting the parts ----------------------------------------------------------------------------------------------------------------
async function setAtlas(png) {
  const d = await pngDecode(png);
  if (d.w !== 256 || d.h !== 192) gtsErr(gtsCheckAtlas(d.w, d.h) || 'atlas is ' + d.w + 'x' + d.h + ', expected 256x192');
  state.atlasPng = png; state.atlasRGBA = d.data;
  state.atlasObj = GtiDraw.atlasFromRGBA(d.data, 256, 192);
}
async function setBg(id, bytes) {
  const [w, h] = GTS_BG[id], im = await skinDec(bytes), cv = skinDraw(im, w, h);
  if (state.bgs[id] && state.bgs[id].url) URL.revokeObjectURL(state.bgs[id].url);
  const d = cv.getContext('2d').getImageData(0, 0, w, h).data;
  state.bgs[id] = Object.assign(GtiDraw.bgFromRGBA(d, w, h), { bytes, url: URL.createObjectURL(new Blob([bytes], { type: 'image/jpeg' })) });
}
function clearBgs() { for (const id in state.bgs) if (state.bgs[id].url) URL.revokeObjectURL(state.bgs[id].url); state.bgs = {}; }

async function loadStarter() {
  const cv = gtsCanvas(256, 192);
  gtsDrawStarter(cv.getContext('2d'), false);
  await setAtlas(await gtsBytes(cv, 'image/png'));
  clearBgs();
  state.man = {}; state.style = teNormalizeStyle(TE_PRESETS.NEO);
  $('t-author').value = '';
}

// Open a .gts (everything in it) or an atlas PNG (the atlas only; colours and backgrounds stay as they are)
async function openFile(f) {
  const raw = new Uint8Array(await f.arrayBuffer());
  try {
    if (raw[0] === 71 && raw[1] === 84) {   // "GT": a .gts
      const u = gtsUnpack(raw), fx = await fixParts(u.atlas, u.bg);
      const nm = String(u.manifest.name || '').toUpperCase();
      await setAtlas(fx.atlas);
      clearBgs();
      for (const g of fx.bgs) await setBg(g.id, g.bytes);
      state.man = Object.assign({}, u.manifest); delete state.man.name; delete state.man.author;
      for (const k of ['fill', 'light', 'dark', 'text', 'accent', 'bevel', 'bevelW', 'radius', 'scale']) delete state.man[k];
      state.style = teNormalizeStyle(u.manifest);
      if (/^[A-Z_][A-Z0-9_]{0,15}$/.test(nm)) $('t-name').value = nm;
      else $('t-name').value = f.name.replace(/\.gts$/i, '').toUpperCase().replace(/[^A-Z0-9_]/g, '_').replace(/^[0-9]/, '_$&').slice(0, 16) || 'MY_SKIN';
      $('t-author').value = typeof u.manifest.author === 'string' ? u.manifest.author : '';
      say('Opened ' + f.name + (fx.notes.length ? ': ' + fx.notes.join('; ') : ''), 'ok');
    } else {
      const fx = await fixParts(raw, []);
      await setAtlas(fx.atlas);
      const empty = GtiDraw.SLOTS.map((s, i) => GtiDraw.slotEmpty(state.atlasObj, i) ? GTS_SLOTS[i].n : null).filter(Boolean);
      const real = empty.filter(n => n !== 'ICONS' && n !== 'ICONS_2X');
      say('Opened ' + f.name + (fx.notes.length ? ' (' + fx.notes.join('; ') + ')' : '') +
        (real.length ? `: slot ${real.join(', ')} ${real.length > 1 ? 'are' : 'is'} empty, ${real.length > 1 ? 'they' : 'it'} will use the default look` : ''), real.length ? 'info' : 'ok');
    }
  } catch (e) { say('Could not open ' + f.name + ': ' + e.message, 'err'); return; }
  syncFields(); refreshAll();
}

// ---- controls <-> state -----------------------------------------------------------------------------------------------------------------
const FIELDS = { fill: 'c-fill', light: 'c-light', dark: 'c-dark', text: 'c-text', accent: 'c-accent', bevel: 'c-bevel', bevelW: 'c-bevelw', radius: 'c-radius', scale: 'c-scale' };
function syncFields() { for (const k in FIELDS) $(FIELDS[k]).value = state.style[k]; }
function readFields() {
  const o = {};
  for (const k in FIELDS) o[k] = $(FIELDS[k]).value;
  state.style = teNormalizeStyle(o);
}

// ---- drawing the panels ---------------------------------------------------------------------------------------------------------------
let raf = 0;
function schedule() { if (!raf) raf = requestAnimationFrame(() => { raf = 0; drawPreview(); }); }
function drawPreview() {
  if (!state.atlasObj) return;
  const bg = {};
  for (const id in state.bgs) bg[id] = state.bgs[id];
  const f = GtiDraw.render(state.screen, { portrait: state.portrait, style: state.style, atlas: state.atlasObj, bg });
  const cv = $('pv');
  if (cv.width !== f.w || cv.height !== f.h) { cv.width = f.w; cv.height = f.h; }
  cv.getContext('2d').putImageData(new ImageData(GtiDraw.toRGBA(f), f.w, f.h), 0, 0);
  cv.style.width = f.w * state.zoom + 'px';
  $('pv-wrap').classList.toggle('z2', state.zoom > 1);
  $('pv-size').textContent = f.w + ' × ' + f.h + (state.zoom > 1 ? ' at ' + state.zoom + '×' : '');
}
function drawAtlas() {
  const cv = $('atlas'), c = cv.getContext('2d');
  if (cv.width !== 512) { cv.width = 512; cv.height = 384; }
  c.imageSmoothingEnabled = false; c.clearRect(0, 0, 512, 384);
  const src = gtsCanvas(256, 192); src.getContext('2d').putImageData(new ImageData(new Uint8ClampedArray(state.atlasRGBA), 256, 192), 0, 0);
  c.drawImage(src, 0, 0, 512, 384);
  if (!$('k-slots').checked) return;
  c.font = '11px ui-monospace, Consolas, monospace'; c.textBaseline = 'top'; c.lineWidth = 1;
  GTS_SLOTS.forEach((s, i) => {
    c.strokeStyle = `hsl(${i * 51},90%,65%)`; c.strokeRect(s.x * 2 + .5, s.y * 2 + .5, s.w * 2 - 1, s.h * 2 - 1);
    if (s.b) { c.setLineDash([3, 3]); c.strokeRect((s.x + s.b) * 2 + .5, (s.y + s.b) * 2 + .5, (s.w - 2 * s.b) * 2 - 1, (s.h - 2 * s.b) * 2 - 1); c.setLineDash([]); }
    const tw = c.measureText(s.n).width;
    c.fillStyle = 'rgba(0,0,0,.7)'; c.fillRect(s.x * 2 + 2, s.y * 2 + 2, tw + 6, 14);
    c.fillStyle = `hsl(${i * 51},90%,75%)`; c.fillText(s.n, s.x * 2 + 5, s.y * 2 + 4);
  });
}
// What each slot is in the calm screens (docs/skins/README.md)
const SLOT_NOTE = {
  KEY_UP: 'every key (9-slice)', KEY_DOWN: 'pressed key: reserved, no screen shows it yet', PANEL: 'cards and unselected rows (9-slice)',
  ROW_SEL: 'the selected row (9-slice)', TILE: 'background when there is no picture', ICONS: 'reserved for a later version', ICONS_2X: 'reserved for a later version',
};
function drawSlotList() {
  const ul = $('slot-list'); ul.innerHTML = '';
  GTS_SLOTS.forEach((s, i) => {
    const reserved = s.n === 'ICONS' || s.n === 'ICONS_2X' || s.n === 'KEY_DOWN', empty = GtiDraw.slotEmpty(state.atlasObj, i);
    const li = document.createElement('li');
    li.className = empty ? (reserved ? 'reserved' : 'empty') : 'ok';
    li.textContent = s.n + (empty ? (reserved ? ' · empty' : ' · empty: default look') : '');
    li.title = SLOT_NOTE[s.n] + ' (' + s.w + '×' + s.h + (s.b ? ', border ' + s.b : '') + ' at ' + s.x + ',' + s.y + ')';
    ul.appendChild(li);
  });
}
function drawBgList() {
  const host = $('bg-list'); host.innerHTML = '';
  for (const id of [1, 2, 3, 4]) {
    const info = BG_INFO[id], cur = state.bgs[id], row = document.createElement('div');
    row.className = 'bgrow';
    const sz = document.createElement('span'); sz.className = 'sz'; sz.textContent = info.label; row.appendChild(sz);
    if (cur) { const im = document.createElement('img'); im.src = cur.url; im.alt = ''; row.appendChild(im); }
    const st = document.createElement('span'); st.className = 'st'; st.textContent = (cur ? kb(cur.bytes.length) + ' · ' : 'none · ') + info.note; row.appendChild(st);
    const add = document.createElement('button'); add.type = 'button'; add.className = 'btn small'; add.textContent = cur ? 'Replace…' : 'Add…';
    const inp = document.createElement('input'); inp.type = 'file'; inp.accept = 'image/*'; inp.hidden = true;
    add.onclick = () => inp.click();
    inp.onchange = async () => {
      const f = inp.files[0]; inp.value = ''; if (!f) return;
      try {
        const [w, h] = GTS_BG[id], im = await skinDec(f);
        if (!im.width || !im.height) gtsErr('the background picture is empty');
        const cv = gtsCanvas(w, h), k = Math.max(w / im.width, h / im.height);   // cropped to fill the size, as the panel page does
        cv.getContext('2d').drawImage(im, (w - im.width * k) / 2, (h - im.height * k) / 2, im.width * k, im.height * k);
        await setBg(id, await gtsBytes(cv, 'image/jpeg', .85));
        say('Background ' + info.label + ' set (' + kb(state.bgs[id].bytes.length) + ')', 'ok');
        if (info.preview && (id === 1) === !state.portrait) { /* on screen now */ }
      } catch (e) { say('Background failed: ' + e.message, 'err'); }
      refreshAll();
    };
    row.append(add, inp);
    if (cur) {
      const rm = document.createElement('button'); rm.type = 'button'; rm.className = 'btn small ghost'; rm.textContent = 'Remove';
      rm.onclick = () => { if (cur.url) URL.revokeObjectURL(cur.url); delete state.bgs[id]; refreshAll(); };
      row.appendChild(rm);
    }
    host.appendChild(row);
  }
}
// The .gts as it would be exported; its size against the 256 KB limit
function packGts() {
  const name = skinName(), bgs = Object.keys(state.bgs).map(Number).sort().map(id => ({ id, bytes: state.bgs[id].bytes }));
  return { name, bytes: gtsPack(skinManifest(state.man, state.style, name, $('t-author').value.trim()), state.atlasPng, bgs) };
}
function drawSize() {
  const bar = $('size-bar'), txt = $('size-txt');
  let n = 0, err = '';
  try { n = packGts().bytes.length; } catch (e) {
    err = e.message;
    if (/limit is 262144/.test(err)) n = +(/skin is (\d+) bytes/.exec(err) || [0, 0])[1];
  }
  bar.style.width = Math.min(100, n * 100 / 262144) + '%'; bar.className = n > 262144 ? 'bad' : n > 200000 ? 'warn' : '';
  txt.textContent = (n ? 'The .gts is ' + n.toLocaleString('en') + ' bytes of the 262,144 allowed. ' : '') + (err && !/limit is 262144/.test(err) ? '(' + err + ')' : (/limit is/.test(err) ? err + '.' : ''));
}
function refreshAll() {
  syncFields();
  if (!state.atlasRGBA) return;
  drawAtlas(); drawSlotList(); drawBgList(); drawSize(); schedule();
}

// ---- the preview's tabs -------------------------------------------------------------------------------------------------------------------
function buildTabs() {
  const host = $('pv-tabs');
  GtiDraw.SCREENS.forEach(s => {
    const b = document.createElement('button'); b.type = 'button'; b.role = 'tab'; b.id = 'tab-' + s.id; b.textContent = s.label; b.dataset.screen = s.id;
    b.onclick = () => selectScreen(s.id);
    host.appendChild(b);
  });
  host.addEventListener('keydown', e => {
    const k = e.key === 'ArrowRight' ? 1 : e.key === 'ArrowLeft' ? -1 : 0;
    if (!k) return;
    const ids = GtiDraw.SCREENS.map(s => s.id), i = (ids.indexOf(state.screen) + k + ids.length) % ids.length;
    selectScreen(ids[i]); $('tab-' + ids[i]).focus(); e.preventDefault();
  });
  selectScreen('list');
}
function selectScreen(id) {
  state.screen = id;
  document.querySelectorAll('#pv-tabs button').forEach(b => { const on = b.dataset.screen === id; b.setAttribute('aria-selected', on); b.tabIndex = on ? 0 : -1; });
  schedule();
}
function setOrientation(p) { state.portrait = p; $('o-land').setAttribute('aria-pressed', !p); $('o-port').setAttribute('aria-pressed', p); schedule(); }
function setZoom(z) { state.zoom = z; $('z-1').setAttribute('aria-pressed', z === 1); $('z-2').setAttribute('aria-pressed', z === 2); schedule(); }

// ---- wiring --------------------------------------------------------------------------------------------------------------------------------
async function slotMapBytes() { const cv = gtsCanvas(256, 192); gtsDrawStarter(cv.getContext('2d'), true); return gtsBytes(cv, 'image/png'); }
function safely(fn) { return async () => { try { await fn(); } catch (e) { say(e.message, 'err'); } }; }

function init() {
  buildTabs();
  const ps = $('c-preset'); ps.innerHTML = '<option value="">Pick…</option>' + Object.keys(TE_PRESETS).map(n => `<option>${n}</option>`).join('');
  ps.onchange = () => { if (ps.value) { state.style = teNormalizeStyle(TE_PRESETS[ps.value]); syncFields(); drawSize(); schedule(); } };
  for (const k in FIELDS) $(FIELDS[k]).addEventListener('input', () => { readFields(); $('c-preset').value = ''; drawSize(); schedule(); });
  $('t-name').addEventListener('input', e => { const v = e.target.value.toUpperCase().replace(/[^A-Z0-9_]/g, ''); if (v !== e.target.value) e.target.value = v; drawSize(); });
  $('t-author').addEventListener('input', drawSize);
  $('o-land').onclick = () => setOrientation(false); $('o-port').onclick = () => setOrientation(true);
  $('z-1').onclick = () => setZoom(1); $('z-2').onclick = () => setZoom(2);
  $('k-slots').onchange = () => drawAtlas();
  $('b-starter').onclick = safely(async () => { await loadStarter(); say('NEO starter loaded', 'ok'); refreshAll(); });
  $('b-open').onclick = () => $('f-open').click();
  $('f-open').onchange = () => { const f = $('f-open').files[0]; $('f-open').value = ''; if (f) openFile(f); };
  $('b-atlas').onclick = () => $('f-atlas').click();
  $('f-atlas').onchange = () => { const f = $('f-atlas').files[0]; $('f-atlas').value = ''; if (f) openFile(f); };
  $('b-gts').onclick = safely(() => { const p = packGts(); download(p.bytes, p.name + '.gts'); say(p.name + '.gts: ' + p.bytes.length.toLocaleString('en') + ' bytes. Next: "Put it on the screen" below.', 'ok'); });
  $('b-png').onclick = safely(() => { const n = skinName(); download(state.atlasPng, n + '.png', 'image/png'); });
  $('b-map').onclick = safely(async () => download(await slotMapBytes(), 'GTI_SLOT_MAP.png', 'image/png'));
  $('b-slotmap').onclick = $('b-map').onclick;
  // drop a file anywhere
  let depth = 0;
  addEventListener('dragenter', e => { if (e.dataTransfer && [...e.dataTransfer.types].includes('Files')) { depth++; document.body.classList.add('drag'); } });
  addEventListener('dragleave', () => { if (--depth <= 0) { depth = 0; document.body.classList.remove('drag'); } });
  addEventListener('dragover', e => e.preventDefault());
  addEventListener('drop', e => { e.preventDefault(); depth = 0; document.body.classList.remove('drag'); const f = e.dataTransfer && e.dataTransfer.files[0]; if (f) openFile(f); });
  // #portrait, #2x and #screen=program (also: list, drives, settings, system, letters, choice, confirm) set the preview from the address
  const hash = location.hash.slice(1).split('&');
  if (hash.includes('portrait')) setOrientation(true);
  if (hash.includes('2x')) setZoom(2);
  const hs = hash.find(h => h.startsWith('screen='));
  if (hs && GtiDraw.SCREENS.some(s => s.id === hs.slice(7))) selectScreen(hs.slice(7));
  loadStarter().then(() => { say('', ''); refreshAll(); }, e => say('Could not draw the starter: ' + e.message, 'err'));
  syncFields();
}
init();
