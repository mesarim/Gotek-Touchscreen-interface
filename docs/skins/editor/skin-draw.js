// GTi skin editor: the preview renderer.
//
// A port of what the JC3248 firmware draws from a skin, so the preview shows the same pixels the screen does:
//   firmware/shared/skin_draw.h              slot table, 9-slice (slice9), RGB565 blend (blend565), drawSlice
//   firmware/Gotek_JC3248/calm_ui.h          the 8 px grid helpers (cuCols, cuStepper ...)
//   firmware/Gotek_JC3248/gti_targets.h      the frame: top bar, drive strip, Drive info, program screen layouts
//   firmware/Gotek_JC3248/calm_list.h        search row, list rows, letter grid, settings tabs, confirmation layouts
//   firmware/Gotek_JC3248/Gotek_JC3248.ino   gfx_* primitives, neoRing, skinSlot / skinPanel / skinKey / skinRow / skinBack*,
//                                            the calm UI primitives (calmKey, calmCard, calmRow, calmChip, calmToggle,
//                                            calmTabs, calmStepper) and the screens (drawCalmTopBar, drawCalmList,
//                                            drawDriveStrip, drawDriveInfoAll, drawProgram, doLetterGrid, doFilterPicker,
//                                            doSettings, calmConfirm)
// Every JS function says which firmware function it mirrors. The framebuffer is a Uint16Array of host-order RGB565
// (the firmware keeps it byte-swapped in memory; that changes nothing on screen). Landscape is 480 x 320, portrait 320 x 480:
// the panel's rotation only changes memory order, so the preview draws in logical coordinates.
//
// Needs GTS_SLOTS from gts.js (loaded first). No DOM: runs in Node too.
//
// Not drawn (the panel draws it, the preview does not): cover pictures (a drawn sample stands in), the reel, the battery gauge,
// the save / HD marks on a cover, the keyboard, the pressed look of a key (KEY_DOWN: no calm screen shows one yet) and the
// ICONS cells (reserved, no screen draws them yet).
var GtiDraw = (function () {
'use strict';

// calm_ui.h / gti_targets.h constants
const MARGIN = 8, GAP = 8, GROUP = 16, KEY = 48, TS_SMALL = 2, TS_MID = 3, TS_BIG = 4, CALM_R = 8;
const TOPBAR_H = 40, CONTENT_TOP = 48, STRIP_H = 48;
const N = { NORMAL: 0, PRIMARY: 1, DANGER: 2, SELECTED: 3 };   // CalmStyle
const SL = { KEY_UP: 0, KEY_DOWN: 1, PANEL: 2, ROW_SEL: 3, TILE: 4, ICONS: 5, ICONS_2X: 6 };   // skin::SlotId
// skin::SLOTS
const SLOTS = GTS_SLOTS.map(s => ({ x: s.x, y: s.y, w: s.w, h: s.h, border: s.b }));

// font6x8 of Gotek_JC3248.ino: 95 glyphs (ASCII 32..126) x 6 columns, bit 0 = top row
const FONT = [
  0,0,0,0,0,0,
  0,0,79,0,0,0,
  0,7,0,7,0,0,
  20,127,20,127,20,0,
  36,42,127,42,18,0,
  98,100,8,19,35,0,
  54,73,73,54,80,0,
  0,4,3,0,0,0,
  0,28,34,65,0,0,
  0,65,34,28,0,0,
  20,8,62,8,20,0,
  8,8,62,8,8,0,
  0,80,48,0,0,0,
  8,8,8,8,8,0,
  0,96,96,0,0,0,
  32,16,8,4,2,0,
  62,81,73,69,62,0,
  0,66,127,64,0,0,
  66,97,81,73,70,0,
  33,65,69,75,49,0,
  24,20,18,127,16,0,
  39,69,69,69,57,0,
  60,74,73,73,48,0,
  1,113,9,5,3,0,
  54,73,73,73,54,0,
  6,73,73,41,30,0,
  0,54,54,0,0,0,
  0,86,54,0,0,0,
  8,20,34,65,0,0,
  20,20,20,20,20,0,
  0,65,34,20,8,0,
  2,1,81,9,6,0,
  50,73,121,65,62,0,
  126,17,17,17,126,0,
  127,73,73,73,54,0,
  62,65,65,65,34,0,
  127,65,65,65,62,0,
  127,73,73,73,65,0,
  127,9,9,9,1,0,
  62,65,73,73,58,0,
  127,8,8,8,127,0,
  0,65,127,65,0,0,
  32,64,65,63,1,0,
  127,8,20,34,65,0,
  127,64,64,64,64,0,
  127,2,12,2,127,0,
  127,4,8,16,127,0,
  62,65,65,65,62,0,
  127,9,9,9,6,0,
  62,65,65,33,94,0,
  127,9,25,41,70,0,
  70,73,73,73,49,0,
  1,1,127,1,1,0,
  63,64,64,64,63,0,
  31,32,64,32,31,0,
  63,64,56,64,63,0,
  99,20,8,20,99,0,
  7,8,112,8,7,0,
  97,81,73,69,67,0,
  0,127,65,0,0,0,
  2,4,8,16,32,0,
  0,65,127,0,0,0,
  4,2,1,2,4,0,
  64,64,64,64,64,0,
  0,1,2,4,0,0,
  32,84,84,84,120,0,
  127,72,68,68,56,0,
  56,68,68,68,32,0,
  56,68,68,72,127,0,
  56,84,84,84,24,0,
  8,126,9,1,2,0,
  24,164,164,156,120,0,
  127,8,4,4,120,0,
  0,68,125,64,0,0,
  32,64,68,61,0,0,
  127,16,40,68,0,0,
  0,65,127,64,0,0,
  124,4,120,4,120,0,
  124,8,4,4,120,0,
  56,68,68,68,56,0,
  124,20,20,20,8,0,
  8,20,20,24,124,0,
  124,8,4,4,8,0,
  72,84,84,84,32,0,
  4,63,68,64,32,0,
  60,64,64,32,124,0,
  28,32,64,32,28,0,
  60,64,48,64,60,0,
  68,40,16,40,68,0,
  28,160,160,156,12,0,
  68,100,84,76,68,0,
  0,8,54,65,0,0,
  0,0,127,0,0,0,
  0,65,54,8,0,0,
  8,4,8,16,8,0
];

// Colours that are not part of the theme (Gotek_JC3248.ino)
const COL_STAR = 0xFEE0, NEO_RED = 0xB986, NEO_RED_INK = 0xFC92, COL_WHITE = 0xFFFF, COL_NOGAMES = 0xE8C4, COL_DANGER_FACE = 0x4000;

// ---- RGB565 helpers ---------------------------------------------------------------------------------------------------------
function rgb565(r, g, b) { return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); }
function c888to565(c) { return (((c >> 16) & 0xF8) << 8) | (((c >> 8) & 0xFC) << 3) | ((c & 0xFF) >> 3); }   // c888to565
function mix888(a, b, pct) {                                                                                 // mix888
  let r = 0;
  for (let sh = 0; sh < 24; sh += 8) { const x = (a >> sh) & 255, y = (b >> sh) & 255; r |= (x + Math.trunc((y - x) * pct / 100)) << sh; }
  return r >>> 0;
}
// skin::blend565: a = 0 gives dst, a = 255 gives src
function blend565(src, a, dst) {
  if (a === 0) return dst;
  if (a === 255) return src;
  const ia = 255 - a;
  const r = Math.floor((((src >> 11) & 31) * a + ((dst >> 11) & 31) * ia + 127) / 255);
  const g = Math.floor((((src >> 5) & 63) * a + ((dst >> 5) & 63) * ia + 127) / 255);
  const b = Math.floor(((src & 31) * a + (dst & 31) * ia + 127) / 255);
  return (r << 11) | (g << 5) | b;
}

// themeFromStyle: the screen's 16 colours from the manifest's five (fill, light, dark, text, accent). green / orange / blue stay
// NAVY's (the state colours). Returns RGB565 values.
function palette(st) {
  const hex = h => parseInt(h.slice(1), 16);
  const fill = hex(st.fill), light = hex(st.light), dark = hex(st.dark), text = hex(st.text), acc = hex(st.accent);
  return {
    bg: c888to565(dark), bar: c888to565(mix888(dark, 0, 25)), panel: c888to565(fill), sel: c888to565(mix888(fill, acc, 30)),
    sep: c888to565(light), dim: c888to565(mix888(text, fill, 45)), mid: c888to565(mix888(text, fill, 25)), lit: c888to565(text),
    green: 0x2D6B, orange: 0xFC60, amber: c888to565(acc), blue: 0x4C5F, now: c888to565(mix888(dark, acc, 20)),
    accent: c888to565(mix888(fill, light, 35)), circ: c888to565(mix888(fill, light, 20)),
  };
}

// ---- skin_draw.h --------------------------------------------------------------------------------------------------------------
// skin::slice9: splits slot s into up to 9 pieces covering a dw x dh destination exactly.
function slice9(s, dw, dh) {
  const out = [];
  if (dw <= 0 || dh <= 0) return out;
  let b = s.border;
  const m = Math.floor((dw < dh ? dw : dh) / 2);
  if (dw < 2 * b || dh < 2 * b) b = m < b ? m : b;
  const sxs = [0, b, s.w - b], sws = [b, s.w - 2 * b, b], sys = [0, b, s.h - b], shs = [b, s.h - 2 * b, b];
  const dxs = [0, b, dw - b], dws = [b, dw - 2 * b, b], dys = [0, b, dh - b], dhs = [b, dh - 2 * b, b];
  for (let r = 0; r < 3; ++r)
    for (let c = 0; c < 3; ++c) {
      if (dws[c] <= 0 || dhs[r] <= 0 || sws[c] <= 0 || shs[r] <= 0) continue;
      out.push({ sx: s.x + sxs[c], sy: s.y + sys[r], sw: sws[c], sh: shs[r], dx: dxs[c], dy: dys[r], dw: dws[c], dh: dhs[r] });
    }
  return out;
}

// ---- the drawing state (Gotek_JC3248.ino globals) -------------------------------------------------------------------------
let W = 480, H = 320, fb = null;                 // gW, gH, the framebuffer
let cx0 = 0, cy0 = 0, cx1 = 480, cy1 = 320;      // g_clip_x0 .. g_clip_y1
let tsz = 2, tfg = 0;                            // text_size, text_fg (text is always drawn without a box, as inside a NeoScope)
let P = null;                                    // the palette (COL_*)
let S = null;                                    // the skin: { atlas: {w, h, rgb, a}, bg: { id: {w, h, px} } }
let slotOk = 0;                                  // g_skin_slot_ok

// ---- gfx_* primitives ---------------------------------------------------------------------------------------------------------
function drawPixel(x, y, c) { if (x >= cx0 && x < cx1 && y >= cy0 && y < cy1) fb[y * W + x] = c; }   // gfx_drawPixel
function fillRect(x, y, w, h, c) {                                                                     // gfx_fillRect
  const vx0 = Math.max(cx0, x), vy0 = Math.max(cy0, y), vx1 = Math.min(cx1, x + w), vy1 = Math.min(cy1, y + h);
  if (vx0 >= vx1 || vy0 >= vy1) return;
  for (let yy = vy0; yy < vy1; yy++) fb.fill(c, yy * W + vx0, yy * W + vx1);
}
function hline(x, y, w, c) { fillRect(x, y, w, 1, c); }                                                // gfx_hline
function vline(x, y, h, c) { fillRect(x, y, 1, h, c); }                                                // gfx_vline
function fillCircle(cx, cy, r, c) { for (let y = -r; y <= r; y++) { const w = Math.trunc(Math.sqrt(r * r - y * y)); fillRect(cx - w, cy + y, 2 * w + 1, 1, c); } }   // gfx_fillCircle
function drawCircle(cx, cy, r, c) {                                                                    // gfx_drawCircle
  let x = 0, y = r, d = 3 - 2 * r;
  while (x <= y) {
    drawPixel(cx + x, cy + y, c); drawPixel(cx - x, cy + y, c); drawPixel(cx + x, cy - y, c); drawPixel(cx - x, cy - y, c);
    drawPixel(cx + y, cy + x, c); drawPixel(cx - y, cy + x, c); drawPixel(cx + y, cy - x, c); drawPixel(cx - y, cy - x, c);
    if (d < 0) d += 4 * x + 6; else { d += 4 * (x - y) + 10; y--; }
    x++;
  }
}
function fillRoundRect(x, y, w, h, r, c) {                                                             // gfx_fillRoundRect
  fillRect(x + r, y, w - 2 * r, h, c); fillRect(x, y + r, r, h - 2 * r, c); fillRect(x + w - r, y + r, r, h - 2 * r, c);
  for (let dy = -r; dy <= 0; dy++) {
    const dx = Math.trunc(Math.sqrt(r * r - dy * dy));
    fillRect(x + r - dx, y + r + dy, dx, 1, c); fillRect(x + w - r, y + r + dy, dx, 1, c);
    fillRect(x + r - dx, y + h - r - 1 - dy, dx, 1, c); fillRect(x + w - r, y + h - r - 1 - dy, dx, 1, c);
  }
}
function drawRoundRect(x, y, w, h, r, c) {                                                             // gfx_drawRoundRect (the corners stay open)
  hline(x + r, y, w - 2 * r, c); hline(x + r, y + h - 1, w - 2 * r, c); vline(x, y + r, h - 2 * r, c); vline(x + w - 1, y + r, h - 2 * r, c);
}
function fillStar(cx, cy, rO, col) {                                                                   // gfx_fillStar
  const rI = rO * 0.42, pts = [];
  for (let i = 0; i < 10; i++) { const a = -1.57080 + i * 0.628319, rr = (i & 1) ? rI : rO; pts.push([cx + Math.cos(a) * rr, cy + Math.sin(a) * rr]); }
  const y0 = Math.trunc(cy - rO - 1), y1 = Math.trunc(cy + rO + 1), x0 = Math.trunc(cx - rO - 1), x1 = Math.trunc(cx + rO + 1);
  for (let y = y0; y <= y1; y++) for (let x = x0; x <= x1; x++) {
    let inside = false;
    for (let i = 0, j = 9; i < 10; j = i++)
      if (((pts[i][1] > y) !== (pts[j][1] > y)) && (x < (pts[j][0] - pts[i][0]) * (y - pts[i][1]) / (pts[j][1] - pts[i][1]) + pts[i][0])) inside = !inside;
    if (inside) drawPixel(x, y, col);
  }
}
function setText(size, fg) { tsz = size < 1 ? 1 : size; tfg = fg; }                                      // gfx_setTextSize + gfx_setTextColor
function textWidth(s) { return s.length * 6 * tsz; }                                                    // gfx_textWidth
function print(s, x, y) {                                                                              // gfx_print (no box behind the glyphs)
  for (let i = 0; i < s.length; i++) {
    const ch = s.charCodeAt(i);
    if (ch < 32 || ch > 126) continue;
    for (let col = 0; col < 6; col++) {
      const bits = FONT[(ch - 32) * 6 + col];
      for (let row = 0; row < 8; row++) {
        if (!(bits & (1 << row))) continue;
        for (let dy = 0; dy < tsz; dy++) for (let dx = 0; dx < tsz; dx++) drawPixel(x + col * tsz + dx, y + row * tsz + dy, tfg);
      }
    }
    x += 6 * tsz;
  }
}

// ---- NEO ring ----------------------------------------------------------------------------------------------------------------
function neoInset(r, j, h) {                                                                            // neoInset
  if (r <= 0) return 0;
  const dy = (j < r) ? (r - j) : ((j >= h - r) ? (j - (h - r) + 1) : 0);
  if (dy <= 0) return 0;
  return r - Math.trunc(Math.sqrt(r * r - dy * dy));
}
function neoRing(x, y, w, h, r, c) {                                                                    // neoRing: a rounded outline WITH its corners
  if (r * 2 > h) r = Math.trunc(h / 2);
  if (r * 2 > w) r = Math.trunc(w / 2);
  hline(x + r, y, w - 2 * r, c); hline(x + r, y + h - 1, w - 2 * r, c); vline(x, y + r, h - 2 * r, c); vline(x + w - 1, y + r, h - 2 * r, c);
  for (let j = 0; j < r; j++) {
    const a = neoInset(r, j, h), b = neoInset(r, j + 1, h), len = Math.max(1, a - b);
    hline(x + b, y + j, len, c); hline(x + w - b - len, y + j, len, c); hline(x + b, y + h - 1 - j, len, c); hline(x + w - b - len, y + h - 1 - j, len, c);
  }
}

// ---- gti-skins: drawing from a loaded skin ---------------------------------------------------------------------------------
// skinSlotOk: the slot is inside the atlas and not fully transparent, worked out once per atlas
function computeSlotOk() {
  slotOk = 0;
  const at = S.atlas;
  for (let k = 0; k < SLOTS.length; k++) {
    const s = SLOTS[k];
    if (s.x + s.w > at.w || s.y + s.h > at.h) continue;
    let any = !at.a;
    for (let y = s.y; y < s.y + s.h && !any; y++) for (let x = s.x; x < s.x + s.w; x++) if (at.a[y * at.w + x]) { any = true; break; }
    if (any) slotOk |= (1 << k);
  }
}
function skinSlotOk(id) { return !!S && !!S.atlas && ((slotOk >> id) & 1) === 1; }
// skinSlot (skin::drawSlice with SkinPut): one atlas slot 9-sliced over (x, y, w, h), clipped, nearest neighbour, alpha blended
function skinSlot(id, x, y, w, h) {
  if (w <= 0 || h <= 0) return;
  const at = S.atlas, pcs = slice9(SLOTS[id], w, h);
  for (const p of pcs) {
    for (let yy = 0; yy < p.dh; yy++) {
      const sy = p.sy + Math.floor(yy * p.sh / p.dh), py = y + p.dy + yy;
      if (py < cy0 || py >= cy1) continue;
      for (let xx = 0; xx < p.dw; xx++) {
        const px = x + p.dx + xx;
        if (px < cx0 || px >= cx1) continue;
        const i = sy * at.w + p.sx + Math.floor(xx * p.sw / p.dw), a = at.a ? at.a[i] : 255;
        if (!a) continue;
        const o = py * W + px;
        fb[o] = a === 255 ? at.rgb[i] : blend565(at.rgb[i], a, fb[o]);
      }
    }
  }
}
// skinBackKind: 1 = a background picture of this size, 2 = a usable TILE slot, 0 = COL_BG
function skinBackKind() {
  const b = S && S.bg && S.bg[W > H ? 1 : 2];
  if (b && b.w === W && b.h === H) return 1;
  if (S && S.atlas && skinSlotOk(SL.TILE)) return 2;
  return 0;
}
function skinBackRect(vx0, vy0, vx1, vy1) {                                                              // skinBackRect (logical rect, already clipped)
  if (vx0 >= vx1 || vy0 >= vy1) return;
  const kind = skinBackKind();
  if (!kind) { fillRect(vx0, vy0, vx1 - vx0, vy1 - vy0, P.bg); return; }
  if (kind === 1) {
    const b = S.bg[W > H ? 1 : 2];
    for (let y = vy0; y < vy1; y++) for (let x = vx0; x < vx1; x++) fb[y * W + x] = b.px[y * W + x];
    return;
  }
  const ts = SLOTS[SL.TILE], at = S.atlas;
  for (let y = vy0; y < vy1; y++) for (let x = vx0; x < vx1; x++) {
    const i = (ts.y + y % ts.h) * at.w + ts.x + x % ts.w;
    fb[y * W + x] = blend565(at.rgb[i], at.a ? at.a[i] : 255, P.bg);
  }
}
function skinBackFill(x, y, w, h) { skinBackRect(Math.max(cx0, x), Math.max(cy0, y), Math.min(cx1, x + w), Math.min(cy1, y + h)); }   // skinBackFill
function fillBack(x, y, w, h) { skinBackFill(x, y, w, h); }                                              // fillBack with g_skin_on
function clearBack() { if (!skinBackKind()) fb.fill(P.bg); else skinBackRect(0, 0, W, H); }              // clearBack / skinBackAll
// skinPanelClip + skinPanel: the PANEL slot over (x, y, w, h) on a fresh background; no usable slot: the classic COL_PANEL fill
function skinPanel(x, y, w, h) {
  if (skinSlotOk(SL.PANEL)) { skinBackRect(Math.max(cx0, x), Math.max(cy0, y), Math.min(cx1, x + w), Math.min(cy1, y + h)); skinSlot(SL.PANEL, x, y, w, h); }
  else fillRect(x, y, w, h, P.panel);
}
// skinKey: a key face over what the caller cleared. false = no usable KEY_UP
function skinKey(x, y, w, h, pressed) {
  const id = (pressed && skinSlotOk(SL.KEY_DOWN)) ? SL.KEY_DOWN : SL.KEY_UP;
  if (!skinSlotOk(id)) return false;
  skinSlot(id, x, y, w, h); return true;
}
function skinRow(x, y, w, h) {                                                                           // skinRow: the selected list row
  if (skinSlotOk(SL.ROW_SEL)) skinSlot(SL.ROW_SEL, x, y, w, h);
  else { fillRoundRect(x, y, w, h, 4, P.sel); drawRoundRect(x, y, w, h, 4, P.amber); }
}
function skinKeys() { return skinSlotOk(SL.KEY_UP); }                                                    // skinKeys

// ---- calm UI primitives ----------------------------------------------------------------------------------------------------
const R = (x, y, w, h) => ({ x, y, w, h });                                                              // CuRect
const right = r => r.x + r.w, bottom = r => r.y + r.h;                                                   // cuRight, cuBottom
function cuCols(r, n, gap) {                                                                             // cuCols
  const out = [], room = r.w - gap * (n - 1), base = Math.floor(room / n), extra = room % n;
  let x = r.x;
  for (let i = 0; i < n; i++) { const w = base + (i < extra ? 1 : 0); out.push(R(x, r.y, w, r.h)); x += w + gap; }
  return out;
}
function cuRows(r, n, gap) {                                                                             // cuRows
  const out = [], room = r.h - gap * (n - 1), base = Math.floor(room / n), extra = room % n;
  let y = r.y;
  for (let i = 0; i < n; i++) { const h = base + (i < extra ? 1 : 0); out.push(R(r.x, y, r.w, h)); y += h + gap; }
  return out;
}
function cuStepper(r) {                                                                                  // cuStepper
  return [R(r.x, r.y, KEY, r.h), R(r.x + KEY + GAP, r.y, Math.max(0, r.w - 2 * (KEY + GAP)), r.h), R(r.x + r.w - KEY, r.y, KEY, r.h)];
}
// calmFit: the text size a label gets: want, or a step down (never below 2) so it fits w; at 2 a too long label is cut
function calmFit(s, w, want) {
  let sz = want < TS_SMALL ? TS_SMALL : (want > TS_BIG ? TS_BIG : want);
  while (sz > TS_SMALL && s.length * 6 * sz > w) sz--;
  while (s.length > 1 && s.length * 6 * sz > w) s = s.slice(0, -1);
  return [s, sz];
}
function calmLabel(x, y, w, h, s, size, fg) {                                                            // calmLabel: centred in (x, y, w, h)
  const [t, sz] = calmFit(s, w - 8, size);
  setText(sz, fg);
  print(t, x + Math.trunc((w - t.length * 6 * sz) / 2), y + Math.trunc((h - 8 * sz) / 2));
}
function calmKey(r, label, st, pressed, size) {                                                          // calmKey
  st = st || N.NORMAL; size = size || TS_MID;
  const edge = st === N.PRIMARY ? P.amber : st === N.DANGER ? NEO_RED : st === N.SELECTED ? P.amber : P.sep;
  let fg = st === N.DANGER ? NEO_RED_INK : st === N.PRIMARY ? P.amber : P.lit;
  if (skinKeys()) { skinKey(r.x, r.y, r.w, r.h, pressed); if (edge !== P.sep) neoRing(r.x, r.y, r.w, r.h, SLOTS[SL.KEY_UP].border, edge); }
  else {   // no usable KEY_UP: the classic coloured key
    let face = st === N.PRIMARY ? P.amber : st === N.DANGER ? COL_DANGER_FACE : P.panel;
    if (pressed) face = st === N.PRIMARY ? P.orange : P.sel;
    fillRoundRect(r.x, r.y, r.w, r.h, CALM_R, face); neoRing(r.x, r.y, r.w, r.h, CALM_R, edge);
    fg = st === N.PRIMARY ? P.bg : st === N.DANGER ? COL_NOGAMES : P.lit;
  }
  calmLabel(r.x, r.y, r.w, r.h, label, size, fg);
}
function calmCard(r) { skinPanel(r.x, r.y, r.w, r.h); }                                                  // calmCard (skin on)
function calmRow(r, selected) {                                                                          // calmRow (skin on)
  if (selected) skinRow(r.x, r.y, r.w, r.h); else skinPanel(r.x, r.y, r.w, r.h);
}
function calmChip(x, y, text, st) {                                                                      // calmChip: returns its width
  st = st || N.NORMAL;
  const w = text.length * 6 * TS_SMALL + 12, h = 24;
  const edge = st === N.PRIMARY ? P.amber : st === N.DANGER ? NEO_RED : st === N.SELECTED ? P.green : P.sep;
  const fg = st === N.PRIMARY ? P.bg : st === N.DANGER ? NEO_RED_INK : st === N.SELECTED ? P.green : P.mid;
  const face = st === N.PRIMARY ? P.amber : P.bg;
  fillRoundRect(x, y, w, h, 6, face); if (st !== N.PRIMARY) neoRing(x, y, w, h, 6, edge);
  setText(TS_SMALL, fg); print(text, x + 6, y + Math.trunc((h - 8 * TS_SMALL) / 2));
  return w;
}
function calmToggle(r, on) {                                                                             // calmToggle: a pill of 56 x 32 at the right end
  const w = 56, h = 32, x = r.x + r.w - w, y = r.y + Math.trunc((r.h - h) / 2);
  fillRoundRect(x, y, w, h, h / 2, on ? P.amber : P.sep);
  fillCircle(on ? x + w - h / 2 : x + h / 2, y + h / 2, h / 2 - 4, on ? COL_WHITE : P.mid);
}
function calmTabs(r, labels, active) {                                                                   // calmTabs
  const n = Math.min(labels.length, 8), c = cuCols(r, n, 0);
  hline(r.x, r.y + r.h - 1, r.w, P.sep);
  for (let i = 0; i < n; i++) {
    const a = (i === active);
    calmLabel(c[i].x - 4, c[i].y, c[i].w + 8, c[i].h - 3, labels[i], TS_SMALL, a ? P.amber : P.mid);
    if (a) fillRect(c[i].x + 8, r.y + r.h - 3, c[i].w - 16, 3, P.amber);
  }
}
function calmStepper(r, text, canPrev, canNext) {                                                        // calmStepper
  const [l, m, rt] = cuStepper(r);
  calmKey(l, '<', N.NORMAL, false, TS_MID); calmKey(rt, '>', N.NORMAL, false, TS_MID);
  if (canPrev === false) { fillRoundRect(l.x + 6, l.y + 6, l.w - 12, l.h - 12, 4, P.bg); calmLabel(l.x, l.y, l.w, l.h, '<', TS_MID, P.dim); }
  if (canNext === false) { fillRoundRect(rt.x + 6, rt.y + 6, rt.w - 12, rt.h - 12, 4, P.bg); calmLabel(rt.x, rt.y, rt.w, rt.h, '>', TS_MID, P.dim); }
  calmCard(m);
  calmLabel(m.x, m.y, m.w, m.h, text, TS_MID, P.lit);
}
function calmText(x, y, s, w, size, fg) { const [t, sz] = calmFit(s, w, size); setText(sz, fg); print(t, x, y); }   // calmText
function calmCut(s, w, size) { while (s.length > 1 && s.length * 6 * size > w) s = s.slice(0, -1); return s; }          // calmCut
function calmTextCut(x, y, s, w, size, fg) { setText(size, fg); print(calmCut(s, w, size), x, y); }                    // calmTextCut
// clSplitDisk: "<game> - Disk <n>" -> { game, d } (d = 0 when the name does not end like that)
function clSplitDisk(name) {
  const m = /^(.+) - Disk (\d{1,4})$/.exec(name);
  return m ? { has: true, game: m[1], d: +m[2] } : { has: false, game: name, d: 0 };
}
// calmDiskText: "<game> . D<n>" with a real dot; when too wide the game part is cut, never the " . D<n>"
function calmDiskText(x, y, name, w, size, fg) {
  const sp = clSplitDisk(name);
  if (!sp.has) { calmTextCut(x, y, sp.game, w, size, fg); return; }
  const cw = 6 * size, num = String(sp.d);
  const keep = Math.min(sp.game.length, Math.max(0, Math.floor(w / cw) - (3 + 1 + num.length)));   // clFitDisk
  const g = sp.game.slice(0, keep);
  setText(size, fg); print(g, x, y);
  x += g.length * cw;
  fillCircle(x + cw + Math.trunc(cw / 2), y + 4 * size, size >= 3 ? 2 : 1, fg);   // the middle dot: the font has none
  print('D' + num, x + 3 * cw, y);
}
// drawWrapped: greedy word wrap at the current text size, up to maxLines lines, never below bottomY
function drawWrapped(x, y, s, maxW, lineH, maxLines, bottomY, fg) {
  setText(tsz, fg);
  let line = '', word = '', gh = 8 * tsz, n = 0;
  for (let i = 0; i <= s.length; i++) {
    const c = i < s.length ? s[i] : ' ';
    if (c === ' ' || c === '\n' || i === s.length) {
      const cand = line.length ? line + ' ' + word : word;
      if (textWidth(cand) > maxW && line.length) {
        if (n >= maxLines || y + gh > bottomY) return y;
        print(line, x, y); y += lineH; n++; line = word;
      } else line = cand;
      word = '';
      if (c === '\n' && line.length) {
        if (n >= maxLines || y + gh > bottomY) return y;
        print(line, x, y); y += lineH; n++; line = '';
      }
    } else word += c;
  }
  if (line.length && n < maxLines && y + gh <= bottomY) { print(line, x, y); y += lineH; }
  return y;
}
// drawGear: a ring and eight teeth
function drawGear(cx, cy, c) {
  drawCircle(cx, cy, 4, c); drawCircle(cx, cy, 5, c); drawCircle(cx, cy, 6, c);
  const dx = [9, 6, 0, -6, -9, -6, 0, 6], dy = [0, 6, 9, 6, 0, -6, -9, -6];
  for (let k = 0; k < 8; k++) fillCircle(cx + dx[k], cy + dy[k], 2, c);
}
function drawMagnifier(cx, cy, col) {                                                                    // drawMagnifier
  drawCircle(cx - 1, cy - 1, 4, col); drawCircle(cx - 1, cy - 1, 5, col);
  fillRect(cx + 2, cy + 2, 2, 2, col); fillRect(cx + 4, cy + 4, 2, 2, col);
}
function dropMark(x, y, w, h) {                                                                          // the drop-down triangle of a filter key
  const ax = x + w - 14, ay = y + Math.trunc(h / 2) - 2;
  for (let k = 0; k < 5; k++) hline(ax - 4 + k, ay + k, 9 - 2 * k, P.amber);
}

// ---- layouts (gti_targets.h, calm_list.h) ------------------------------------------------------------------------------------
const stripY = h => h - MARGIN - STRIP_H, contentBottom = h => stripY(h) - GAP;                           // gtStripY, gtContentBottom
function stripArea(w, h) { return R(MARGIN, h - MARGIN - STRIP_H, w - 2 * MARGIN, STRIP_H); }              // gtStripArea
function stripLayout(area, n) {                                                                          // gtStripLayout
  if (n < 1) return { cards: [], more: false };
  if (n === 1) return { cards: [area], more: false };
  if (n === 2) return { cards: cuCols(area, 2, GAP), more: false };
  const moreKey = R(area.x + area.w - 72, area.y, 72, area.h), rest = R(area.x, area.y, area.w - 72 - GAP, area.h);
  return { cards: cuCols(rest, 2, GAP), more: true, moreKey, moreCount: n - 2 };
}
function topBarLayout(w) {                                                                               // gtTopBarLayout
  const tw = w >= 400 ? 80 : 64;
  const listTab = R(MARGIN, 0, tw, KEY), reelTab = R(MARGIN + tw, 0, tw, KEY), gear = R(w - MARGIN - KEY, 0, KEY, KEY);
  return { listTab, reelTab, gear };
}
function loadLayout(area, n, stack) {                                                                    // gtLoadLayout
  if (n < 1) return { keys: [], picker: false };
  if (n === 2) { const c = stack ? cuRows(area, 2, GAP) : cuCols(area, 2, GAP); return { keys: [{ r: c[0], target: 0 }, { r: c[1], target: 1 }], picker: false }; }
  const one = stack ? R(area.x, bottom(area) - KEY, area.w, KEY) : area;
  return { keys: [{ r: one, target: n === 1 ? 0 : -1 }], picker: n >= 3 };
}
function progLayout(w, h, disks) {                                                                       // gtProgLayout
  const L = {}, bot = contentBottom(h);
  L.back = R(MARGIN, 4, 104, KEY); L.more = R(w - MARGIN - KEY, 4, KEY, KEY);
  L.title = R(right(L.back) + GAP, 4, L.more.x - GAP - (right(L.back) + GAP), KEY);
  const top = bottom(L.back) + GAP;
  L.hasStepper = disks > 1;
  if (w >= 400) {
    L.cover = R(MARGIN, top, 176, 136);
    const rx = right(L.cover) + GAP, rw = w - MARGIN - rx;
    if (L.hasStepper) { L.details = R(rx, top, rw, 54); L.stepper = R(rx, top + 60, rw, KEY); L.status = R(rx, top + 60 + KEY + 6, rw, 16); }
    else { L.details = R(rx, top, rw, 108); L.status = R(rx, top + 108 + 6, rw, 16); }
    L.loadArea = R(MARGIN, bot - KEY, w - 2 * MARGIN, KEY);
  } else {
    const fw = w - 2 * MARGIN;
    L.cover = R(MARGIN, top, fw, 112);
    const dy = bottom(L.cover) + 6;
    if (L.hasStepper) { L.details = R(MARGIN, dy, fw, 54); L.stepper = R(MARGIN, dy + 54 + 6, fw, KEY); L.status = R(MARGIN, dy + 54 + 6 + KEY + 4, fw, 16); }
    else { L.details = R(MARGIN, dy, fw, 108); L.status = R(MARGIN, dy + 108 + 4, fw, 16); }
    L.loadArea = R(MARGIN, bot - (2 * KEY + GAP), fw, 2 * KEY + GAP);
  }
  return L;
}
function infoLayout(w, h, n) {                                                                           // gtInfoLayout (scroll 0)
  const L = { back: R(MARGIN, 4, 104, KEY), cards: [], eject: [], name: [] };
  const vy = bottom(L.back) + GAP;
  L.view = R(MARGIN, vy, w - 2 * MARGIN, h - MARGIN - vy);
  for (let i = 0; i < n; i++) {
    const y = L.view.y + i * (88 + GAP), card = R(L.view.x, y, L.view.w, 88);
    const eject = R(right(card) - GAP - 112, y + (88 - KEY) / 2, 112, KEY);
    L.cards.push(card); L.eject.push(eject); L.name.push(R(card.x + GAP, y + 26, eject.x - GAP - (card.x + GAP), 28));
  }
  return L;
}
function searchRow(area) {                                                                               // clSearchRow
  const filter = R(right(area) - 72, area.y, 72, area.h), az = R(filter.x - GAP - 72, area.y, 72, area.h);
  const star = R(az.x - GAP - KEY, area.y, KEY, area.h), search = R(area.x, area.y, star.x - GAP - area.x, area.h);
  return { search, star, az, filter };
}
const ROW_H = 48, ROW_PITCH = 52;                                                                          // CL_ROW_H, CL_ROW_PITCH
const rowRect = (i, scroll, listTop, x, w) => R(x, listTop + i * ROW_PITCH - scroll, w, ROW_H);          // clRowRect
function letterCell(i, w, top) {                                                                         // clLetterCell
  const cols = w >= 460 ? 7 : 4, col = i % cols, row = Math.floor(i / cols), x0 = Math.trunc((w - (cols * 64 + (cols - 1) * 2)) / 2);
  return R(x0 + col * (64 + 2), top + row * (KEY + 2), 64, KEY);
}
function settingsTabs(w) {                                                                               // clSettingsTabs
  const back = R(MARGIN, 4, KEY, KEY);
  let tab, listTop;
  if (w >= 400) { const area = R(right(back) + GAP, 4, w - MARGIN - (right(back) + GAP), KEY); tab = cuCols(area, 4, 0); listTop = 60; }
  else { const area = R(MARGIN, bottom(back) + 4, w - 2 * MARGIN, KEY); tab = cuCols(area, 4, 0); listTop = bottom(area) + GAP; }
  return { back, tab, listTop };
}
const SET_H = 56, SET_PITCH = 60;                                                                          // CL_SET_H, CL_SET_PITCH
function confirmLayout(w, h) {                                                                           // clConfirmLayout
  const back = R(MARGIN, 4, 104, KEY), ky = h - MARGIN - KEY, half = Math.trunc((w - 2 * MARGIN - GROUP) / 2);
  const cancel = R(MARGIN, ky, half, KEY), ok = R(w - MARGIN - half, ky, half, KEY);
  return { back, cancel, ok, msg: R(MARGIN, bottom(back) + GAP, w - 2 * MARGIN, ky - GAP - (bottom(back) + GAP)) };
}

// ---- the sample data (what the preview shows) ------------------------------------------------------------------------------
// A FluxDrive "fluxdrive-62a4" with Xenon 2 in both drives (disk 1 is being stored in DF0), as in the real screenshots.
const DATA = {
  count: '95 programs',
  rows: [
    { name: 'X-Out', fav: false, chip: '2 disks' },
    { name: 'Xenon', fav: true, chip: '' },
    { name: 'Xenon 2', fav: false, chip: 'DF0 DF1', loaded: true, sel: true },
    { name: 'Xybots', fav: false, chip: '' },
    { name: 'Zeewolf 1', fav: false, chip: '' },
    { name: 'Zool 1', fav: false, chip: '2 disks' },
  ],
  firstPos: 90,   // the list is scrolled to the 91st of 95 programs (only the scroll bar uses this)
  total: 95,
  drives: [
    { cap: 'DF0', holds: 'Xenon 2 - Disk 1', storing: true, wp: false, linked: true, status: 'storing' },
    { cap: 'DF1', holds: 'Xenon 2 - Disk 2', storing: false, wp: false, linked: true, status: 'stored' },
  ],
  peer: 'fluxdrive-62a4',
  game: 'Xenon 2', disks: 2, blurb: 'Developer: Mirrorsoft, Assembly Line, Bitmap Brothers',
};

// ---- the screens -------------------------------------------------------------------------------------------------------------
function drawCalmTopBar() {                                                                              // drawCalmTopBar (the list, not the reel)
  fillBack(0, 0, W, CONTENT_TOP);
  const b = topBarLayout(W);
  calmTabs(R(b.listTab.x, 0, right(b.reelTab) - b.listTab.x, TOPBAR_H), ['List', 'Reel'], 0);
  const gk = R(b.gear.x, 0, b.gear.w, TOPBAR_H);
  calmKey(gk, '', N.NORMAL, false); drawGear(gk.x + Math.trunc(gk.w / 2), Math.trunc(gk.h / 2), P.lit);
  const rx = b.gear.x - GAP;
  let ct = DATA.count;
  const room = rx - right(b.reelTab) - GAP;
  if (ct.length * 6 * TS_SMALL > room) { const sp = ct.indexOf(' '); if (sp > 0) ct = ct.slice(0, sp); }
  if (ct.length * 6 * TS_SMALL > room) ct = '';
  if (ct.length) { setText(TS_SMALL, P.mid); print(ct, rx - ct.length * 6 * TS_SMALL, Math.trunc((TOPBAR_H - 8 * TS_SMALL) / 2)); }
}
function drawCalmSearchRow(listTop) {                                                                    // drawCalmSearchRow
  const r = searchRow(R(MARGIN, CONTENT_TOP, W - 2 * MARGIN, KEY));
  fillBack(0, CONTENT_TOP, W, KEY + GAP);
  calmCard(r.search);
  drawMagnifier(r.search.x + (r.search.w >= 120 ? 22 : Math.trunc(r.search.w / 2)), r.search.y + Math.trunc(r.search.h / 2), P.amber);
  if (r.search.w >= 120) calmTextCut(r.search.x + 44, r.search.y + Math.trunc((r.search.h - 16) / 2), 'Search', r.search.w - 52, TS_SMALL, P.mid);
  calmKey(r.star, '', N.NORMAL, false, TS_SMALL);
  fillStar(r.star.x + Math.trunc(r.star.w / 2), r.star.y + Math.trunc(r.star.h / 2), 11.0, P.mid);
  calmKey(r.az, 'A-Z', N.NORMAL, false, TS_SMALL);
  calmKey(r.filter, '', N.NORMAL, false, TS_SMALL);
  setText(TS_SMALL, P.lit); print('ADF', r.filter.x + 8, r.filter.y + Math.trunc((r.filter.h - 16) / 2));
  dropMark(r.filter.x, r.filter.y, r.filter.w, r.filter.h);
}
function drawCalmList(listTop, listBottom) {                                                             // drawCalmList
  fillBack(0, listTop, W, listBottom - listTop);
  cy0 = listTop; cy1 = listBottom;
  DATA.rows.forEach((row, pos) => {
    const r = rowRect(pos, 0, listTop, MARGIN, W - 2 * MARGIN);
    if (r.y >= listBottom) return;
    calmRow(r, !!row.sel);
    const cy = r.y + Math.trunc(r.h / 2);
    if (row.fav) fillStar(r.x + 22, cy, 11.0, COL_STAR); else fillStar(r.x + 22, cy, 6.0, P.dim);
    let rt = r.x + r.w - GAP;
    if (row.chip) { const cw = row.chip.length * 6 * TS_SMALL + 12; calmChip(rt - cw, r.y + Math.trunc((r.h - 24) / 2), row.chip, row.loaded ? N.SELECTED : N.NORMAL); rt -= cw + GAP; }
    calmTextCut(r.x + 44, cy - 12, row.name, rt - (r.x + 44), TS_MID, P.lit);
  });
  cy0 = 0; cy1 = H;
  const vh = listBottom - listTop, mp = DATA.total * ROW_PITCH - vh, sc = DATA.firstPos * ROW_PITCH;   // the scroll bar
  const th = Math.max(16, Math.trunc(vh * vh / (vh + mp))), ty = listTop + Math.trunc((vh - th) * sc / mp);
  fillRect(W - 4, ty, 3, th, P.dim);
}
function drawDriveStrip() {                                                                              // drawDriveStrip
  const cb = contentBottom(H), area = stripArea(W, H), n = DATA.drives.length;
  fillBack(0, cb, W, H - cb);
  const s = stripLayout(area, n);
  for (let i = 0; i < s.cards.length; i++) {
    const t = DATA.drives[i], c = s.cards[i];
    calmCard(c);
    const rt = c.x + c.w - GAP - 14;
    const marks = (t.wp ? 'WP' : '') + (t.storing ? ((t.wp ? ' ' : '') + 'STORING') : '');
    const capW = t.cap.length * 6 * TS_SMALL, mw = marks.length * 6 * TS_SMALL;
    const showMarks = marks.length && (c.x + GAP + capW + 12 + mw <= rt);
    calmTextCut(c.x + GAP, c.y + 6, t.cap, rt - (c.x + GAP) - (showMarks ? mw + 12 : 0), TS_SMALL, P.orange);
    if (showMarks) calmTextCut(rt - mw, c.y + 6, marks, mw, TS_SMALL, P.amber);
    if (t.linked) fillCircle(c.x + c.w - GAP - 4, c.y + 14, 4, P.green); else drawCircle(c.x + c.w - GAP - 4, c.y + 14, 4, P.dim);
    calmDiskText(c.x + GAP, c.y + 26, t.holds, c.w - 2 * GAP, TS_SMALL, P.lit);
  }
}
function screenList() {
  clearBack();
  const listTop = CONTENT_TOP + KEY + GAP;
  drawCalmTopBar(); drawCalmSearchRow(); drawCalmList(listTop, contentBottom(H)); drawDriveStrip();
}
// programCover: the cover fitted into box, a ring around it. The panel draws the game's cover picture here; the preview draws a
// sample picture (a 4:3 "shooter" scene) so the frame, ring and the room it takes are right.
function programCover(box) {
  fillRoundRect(box.x, box.y, box.w, box.h, 5, P.bar);
  const bw = box.w - 4, bh = box.h - 4;
  let dw = bw, dh = Math.trunc(3 * bw / 4);
  if (dh > bh) { dh = bh; dw = Math.trunc(4 * bh / 3); }
  const ax = box.x + Math.trunc((box.w - dw) / 2), ay = box.y + Math.trunc((box.h - dh) / 2);
  for (let y = 0; y < dh; y++) for (let x = 0; x < dw; x++) {
    const u = x / dw, v = y / dh;
    let r = 10 + 24 * v, g = 12 + 18 * v, b = 40 + 60 * (1 - v);
    if (u < 0.12 || u > 0.88) { r = 70 + 40 * Math.sin(v * 25 + u * 9); g = 60 + 30 * Math.sin(v * 18); b = 50; }   // rocky walls
    const dx = u - 0.5, dy = v - 0.72;
    if (dx * dx * 40 + dy * dy * 160 < 1) { r = 220; g = 200; b = 240; }                                       // the ship
    const hsh = ((x * 73856093) ^ (y * 19349663)) >>> 0;                                                      // sparse stars, fixed pattern
    if (hsh % 61 === 0) { r = g = b = 255; } else if (hsh % 233 === 1 && u > 0.2 && u < 0.8) { r = 255; g = 150; b = 40; }
    fb[(ay + y) * W + ax + x] = rgb565(r, g, b);
  }
  drawRoundRect(ax - 1, ay - 1, dw + 2, dh + 2, 6, P.accent);
}
function screenProgram() {                                                                               // drawProgram
  clearBack();
  const L = progLayout(W, H, DATA.disks);
  calmKey(L.back, '< List', N.NORMAL, false, TS_MID);
  calmTextCut(L.title.x, L.title.y + Math.trunc((L.title.h - 24) / 2), DATA.game, L.title.w, TS_MID, P.amber);
  calmKey(L.more, '', N.NORMAL, false, TS_MID);
  for (let k = -1; k <= 1; k++) fillCircle(L.more.x + Math.trunc(L.more.w / 2) + k * 10, L.more.y + Math.trunc(L.more.h / 2), 3, P.lit);
  programCover(L.cover);
  setText(TS_SMALL, P.mid);
  drawWrapped(L.details.x, L.details.y, DATA.blurb, L.details.w, 18, Math.trunc(L.details.h / 18), L.details.y + L.details.h, P.mid);
  if (L.hasStepper) calmStepper(L.stepper, 'Disk 1 of 2');
  calmTextCut(L.status.x, L.status.y, 'Disk 1: in DF0', L.status.w, TS_SMALL, P.green);
  const lk = loadLayout(L.loadArea, DATA.drives.length, W < 400);
  lk.keys.forEach(k => {
    const here = k.target === 0;   // disk 1 sits in DF0
    const cap = DATA.drives[k.target].cap;
    if (here) calmKey(k.r, 'in ' + cap, N.SELECTED, false, TS_MID);
    else calmKey(k.r, 'Load ' + cap, N.PRIMARY, false, TS_MID);
  });
  drawDriveStrip();
}
function drawCalmHeader(title) {                                                                         // drawCalmHeader
  const back = R(MARGIN, 4, 104, KEY);
  calmKey(back, '< Back', N.NORMAL, false, TS_MID);
  calmTextCut(back.x + back.w + GAP * 2, back.y + Math.trunc((back.h - 24) / 2), title, W - (back.x + back.w + GAP * 3) - GAP, TS_MID, P.amber);
}
function screenDriveInfo() {                                                                             // drawDriveInfoAll
  clearBack();
  const n = DATA.drives.length, L = infoLayout(W, H, n);
  for (let i = 0; i < n; i++) {
    const t = DATA.drives[i], c = L.cards[i];
    calmCard(c);
    calmTextCut(c.x + GAP, c.y + 6, t.cap, Math.trunc(c.w / 2), TS_SMALL, P.orange);
    if (t.wp) calmChip(c.x + GAP + t.cap.length * 6 * TS_SMALL + GAP, c.y + 2, 'WP', N.PRIMARY);
    const nm = L.name[i];
    calmDiskText(nm.x, nm.y, t.holds, nm.w, TS_MID, P.green);
    calmTextCut(c.x + GAP, c.y + 58, t.status, c.w - 2 * GAP, TS_SMALL, t.storing ? P.amber : P.mid);
    calmKey(L.eject[i], 'Eject', N.DANGER, false, TS_MID);
  }
  fillBack(0, 0, W, L.view.y);
  fillBack(0, bottom(L.view), W, H - bottom(L.view));
  drawCalmHeader(DATA.peer);
}
// Settings: the rows of the Screen and System tabs (labels as in the real screenshots)
const SK = { VALUE: 0, TOGGLE: 1, INFO: 2, ACTION: 3 };
const SETTINGS = {
  screen: [
    { kind: SK.VALUE, label: 'Theme', desc: 'Colours and look of the screen', value: 'NEO' },
    { kind: SK.VALUE, label: 'Lang', desc: 'Language of the screens', value: 'English' },
    { kind: SK.VALUE, label: 'Font', desc: 'Text size in the readers', value: 'Normal' },
    { kind: SK.VALUE, label: 'Rotate', desc: 'Turn the screen around', value: 'Landscape' },
    { kind: SK.TOGGLE, label: 'Battery', desc: 'Show a gauge if one is fitted', on: false },
    { kind: SK.TOGGLE, label: 'Cover art', desc: 'Pictures in reel and list', on: true },
  ],
  system: [
    { kind: SK.INFO, label: 'Firmware', value: 'A600-calm8-JC3248' },
    { kind: SK.INFO, label: 'IP', value: '192.168.50.3' },
    { kind: SK.ACTION, label: 'Soft reset', danger: true },
    { kind: SK.ACTION, label: 'SD access', danger: true },
    { kind: SK.ACTION, label: 'Firmware update', danger: false },
  ],
};
function screenSettings(tab) {                                                                           // doSettings
  clearBack();
  const T6 = settingsTabs(W), listTop = T6.listTop, rows = SETTINGS[tab === 3 ? 'system' : 'screen'];
  cy0 = listTop; cy1 = H - MARGIN + 4;
  rows.forEach((r, i) => {
    const RR = R(MARGIN, listTop + i * SET_PITCH, W - 2 * MARGIN, SET_H);
    if (bottom(RR) <= listTop || RR.y >= H) return;
    if (r.kind === SK.ACTION) { calmKey(R(RR.x, RR.y + 4, RR.w, KEY), r.label, r.danger ? N.DANGER : N.NORMAL, false, TS_MID); return; }
    calmCard(RR);
    let vw = 0, v = r.value || '';
    if (v.length > 20) v = v.slice(0, 20);
    if (r.kind === SK.TOGGLE) vw = 64; else if (v.length) vw = v.length * 6 * TS_SMALL + 8; else if (r.kind === SK.VALUE) vw = 28;
    calmTextCut(RR.x + 12, RR.y + (r.desc ? 6 : 16), r.label, RR.w - 24 - vw, TS_MID, P.lit);
    if (r.desc) calmTextCut(RR.x + 12, RR.y + 34, r.desc, RR.w - 24 - (r.kind === SK.TOGGLE ? 64 : 0), TS_SMALL, P.mid);
    if (r.kind === SK.TOGGLE) calmToggle(RR, r.on);
    else if (v.length) { setText(TS_SMALL, r.kind === SK.INFO ? P.dim : P.amber); print(v, RR.x + RR.w - 12 - v.length * 6 * TS_SMALL, RR.y + (r.desc ? 10 : 20)); }
    else if (r.kind === SK.VALUE) { setText(TS_MID, P.amber); print('>', RR.x + RR.w - 12 - 18, RR.y + 16); }
  });
  cy0 = 0; cy1 = H;
  fillBack(0, 0, W, listTop);
  calmKey(T6.back, '<', N.NORMAL, false, TS_MID);
  calmTabs(R(T6.tab[0].x, T6.tab[0].y, right(T6.tab[3]) - T6.tab[0].x, T6.tab[0].h), ['Screen', 'Wi-Fi', 'Drives', 'System'], tab);
}
function screenLetters() {                                                                               // doLetterGrid
  clearBack();
  const LETTERS = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ#', missing = 'CIJQUVYZ#', cur = 'X', top = CONTENT_TOP + 4 + GAP;
  drawCalmHeader('Jump to');
  for (let i = 0; i < LETTERS.length; i++) {
    const c = letterCell(i, W, top), lb = LETTERS[i];
    if (missing.indexOf(lb) < 0) calmKey(c, lb, lb === cur ? N.SELECTED : N.NORMAL, false, TS_BIG);
    else { calmCard(c); calmLabel(c.x, c.y, c.w, c.h, lb, TS_BIG, P.dim); }
  }
}
function screenChoice() {                                                                                // doFilterPicker
  clearBack();
  const labels = ['ADF disks', 'DSK disks', 'Generic (GEN)', 'Categories', 'Own disks'], top = CONTENT_TOP + GAP;
  drawCalmHeader('Show');
  labels.forEach((lb, i) => {
    const r = R(MARGIN, top + i * (KEY + 4), W - 2 * MARGIN, KEY);
    calmRow(r, i === 0);
    calmTextCut(r.x + 16, r.y + Math.trunc((r.h - 24) / 2), lb, r.w - 32, TS_MID, P.lit);
  });
}
function screenConfirm() {                                                                               // calmConfirm
  clearBack();
  const c = confirmLayout(W, H);
  drawCalmHeader('Soft reset');
  setText(TS_SMALL, P.lit);
  drawWrapped(c.msg.x + GAP, c.msg.y + GAP, 'Restart the screen now?', c.msg.w - 2 * GAP, 22, Math.trunc(c.msg.h / 22), bottom(c.msg), P.lit);
  calmKey(c.cancel, 'Cancel', N.NORMAL, false, TS_MID);
  calmKey(c.ok, 'Soft reset', N.DANGER, false, TS_MID);
}

const SCREENS = [
  { id: 'list', label: 'List', draw: screenList },
  { id: 'program', label: 'Program', draw: screenProgram },
  { id: 'drives', label: 'Drive info', draw: screenDriveInfo },
  { id: 'settings', label: 'Settings', draw: () => screenSettings(0) },
  { id: 'system', label: 'System', draw: () => screenSettings(3) },
  { id: 'letters', label: 'A-Z', draw: screenLetters },
  { id: 'choice', label: 'Choice list', draw: screenChoice },
  { id: 'confirm', label: 'Confirm', draw: screenConfirm },
];

// ---- the public side -----------------------------------------------------------------------------------------------------------
// atlasFromRGBA: 256 x 192 RGBA bytes -> { w, h, rgb (RGB565), a (alpha plane) } as the firmware's PNG loader holds it
function atlasFromRGBA(data, w, h) {
  const rgb = new Uint16Array(w * h), a = new Uint8Array(w * h);
  for (let i = 0; i < w * h; i++) { rgb[i] = rgb565(data[i * 4], data[i * 4 + 1], data[i * 4 + 2]); a[i] = data[i * 4 + 3]; }
  return { w, h, rgb, a };
}
// A decoded background (RGBA, opaque) -> RGB565 pixels
function bgFromRGBA(data, w, h) {
  const px = new Uint16Array(w * h);
  for (let i = 0; i < w * h; i++) px[i] = rgb565(data[i * 4], data[i * 4 + 1], data[i * 4 + 2]);
  return { w, h, px };
}
// render(screenId, { portrait, style, atlas, bg }) -> { w, h, px: Uint16Array RGB565 }
//   style: the manifest colours (fill, light, dark, text, accent); atlas: from atlasFromRGBA; bg: { id: bgFromRGBA(..) }
function render(screenId, o) {
  W = o.portrait ? 320 : 480; H = o.portrait ? 480 : 320;
  fb = new Uint16Array(W * H); cx0 = 0; cy0 = 0; cx1 = W; cy1 = H;
  P = palette(o.style); S = { atlas: o.atlas, bg: o.bg || {} }; computeSlotOk();
  const sc = SCREENS.find(s => s.id === screenId) || SCREENS[0];
  sc.draw();
  return { w: W, h: H, px: fb };
}
// toRGBA: the framebuffer as 8-bit RGBA (the 565 channels widened the way the panel's screenshot does)
function toRGBA(f) {
  const out = new Uint8ClampedArray(f.w * f.h * 4);
  for (let i = 0; i < f.w * f.h; i++) {
    const c = f.px[i], r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    out[i * 4] = (r << 3) | (r >> 2); out[i * 4 + 1] = (g << 2) | (g >> 4); out[i * 4 + 2] = (b << 3) | (b >> 2); out[i * 4 + 3] = 255;
  }
  return out;
}
return { SCREENS, SLOTS, palette, slice9, blend565, atlasFromRGBA, bgFromRGBA, render, toRGBA, slotEmpty: (a, id) => { S = { atlas: a }; computeSlotOk(); return !skinSlotOk(id); } };
})();
