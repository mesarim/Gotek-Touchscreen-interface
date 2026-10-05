// GTi simulator core: the virtual SD card (a FAT32 card built on the fly from a list of files)
// and the machine that runs the real GTi firmware (WebAssembly + asyncify).
// Runs in a Web Worker (FileReaderSync for picked folders) or in Node (tests).
'use strict';

// ─────────────────────────────── virtual FAT32 card ───────────────────────────────
// Files keep their own bytes (ArrayBuffer, Blob/File or a read callback). The card's FAT, directories and
// boot sectors are generated; every sector the firmware writes is kept in an overlay, so FatFs on the
// "board" sees an ordinary card it can read and write.
class VirtualCard {
  constructor(files, opts = {}) {
    // files: [{ path: 'ADF/Game/Game.adf', size, mtime (ms), src }]  src: ArrayBuffer | Uint8Array | Blob | (off,len)=>Uint8Array
    this.overlay = new Map();
    this.readSync = opts.readSync || null;          // (blob, off, len) => Uint8Array  (FileReaderSync in a worker)
    this.writes = 0; this.reads = 0;
    this.label = (opts.label || 'GTI SIM').toUpperCase().padEnd(11, ' ').slice(0, 11);
    this._build(files, opts);
  }
  _build(files, opts) {
    const SPC = 64, CLB = SPC * 512;                // 32 KB clusters
    // ── tree ──
    const root = { name: '', dir: true, kids: new Map(), parent: null };
    let used = 0;
    for (const f of files) {
      const parts = f.path.split('/').filter(Boolean);
      let d = root;
      for (let i = 0; i < parts.length - 1; i++) {
        const k = parts[i].toUpperCase();
        let n = d.kids.get(k);
        if (!n) { n = { name: parts[i], dir: true, kids: new Map(), parent: d, mtime: f.mtime }; d.kids.set(k, n); }
        d = n;
      }
      const leaf = parts[parts.length - 1];
      d.kids.set(leaf.toUpperCase(), { name: leaf, dir: false, size: f.size >>> 0, src: f.src, mtime: f.mtime || Date.now(), parent: d });
      used += Math.ceil((f.size || 1) / CLB) * CLB;
    }
    // ── card size: the smallest usual SD size with room to spare ──
    const want = used * 1.25 + 512 * 1024 * 1024;
    let gb = 8; while (gb * 1e9 < want && gb < 1024) gb *= 2;
    if (opts.cardGB) gb = opts.cardGB;
    const total = Math.floor(gb * 1e9 / 512) & ~0x1FFF;      // sectors, multiple of 4 MB
    const P = 8192;                                           // partition start (4 MB aligned, like SD Formatter)
    const part = total - P;
    let ncl = Math.floor((part - 32) / SPC), fatsz = 0;
    for (let k = 0; k < 6; k++) { fatsz = Math.ceil((ncl + 2) * 4 / 512); ncl = Math.floor((part - 32 - 2 * fatsz) / SPC); }
    Object.assign(this, { SPC, CLB, total, P, part, ncl, fatsz, fat1: P + 32, fat2: P + 32 + fatsz, data: P + 32 + 2 * fatsz });
    // ── give every directory and file a run of clusters ──
    this.fat = new Uint32Array(ncl + 2);
    this.fat[0] = 0x0FFFFFF8; this.fat[1] = 0x0FFFFFFF;
    this.ext = [];                                            // [startCluster, nClusters, node]
    let next = 2;
    const alloc = (node, bytes) => {
      const n = Math.max(1, Math.ceil(bytes / CLB));
      if (next + n > ncl + 2) throw new Error('card full');
      node.clus = next; node.ncl = n;
      for (let i = 0; i < n; i++) this.fat[next + i] = (i === n - 1) ? 0x0FFFFFFF : next + i + 1;
      this.ext.push([next, n, node]); next += n;
    };
    const t = this;
    (function walkDirs(d) {
      d.entries = t._dirEntries(d);                            // Uint8Array, 32 bytes per entry
      alloc(d, d.entries.length + 32);                         // +1 free entry (end marker) - FatFs extends the chain itself
      for (const k of d.kids.values()) if (k.dir) walkDirs(k);
    })(root);
    for (const k of (function* all(d) { for (const c of d.kids.values()) { if (c.dir) yield* all(c); else yield c; } })(root))
      if (k.size > 0) alloc(k, k.size); else { k.clus = 0; k.ncl = 0; }
    // now that every cluster is known, write the start clusters into the directory entries
    (function fix(d) {
      for (const [off, node] of d.slots) {
        const e = d.entries;
        const c = node === '.' ? d.clus : node === '..' ? (d.parent && d.parent.parent ? d.parent.clus : 0) : node.clus;
        e[off + 20] = (c >>> 16) & 0xFF; e[off + 21] = (c >>> 24) & 0xFF; e[off + 26] = c & 0xFF; e[off + 27] = (c >>> 8) & 0xFF;
      }
      for (const k of d.kids.values()) if (k.dir) fix(k);
    })(root);
    this.root = root; this.nextFree = next;
    this.freeClusters = ncl - (next - 2);
    this.ext.sort((a, b) => a[0] - b[0]);
    this.extStarts = Int32Array.from(this.ext.map(e => e[0]));
  }
  static _fatTime(ms) {
    const d = new Date(ms || Date.now());
    const y = Math.max(1980, d.getFullYear());
    return { date: ((y - 1980) << 9) | ((d.getMonth() + 1) << 5) | d.getDate(), time: (d.getHours() << 11) | (d.getMinutes() << 5) | (d.getSeconds() >> 1) };
  }
  _dirEntries(d) {
    const out = []; d.slots = [];
    const used = new Set();
    const put = (bytes, node) => { if (node !== undefined) d.slots.push([out.length * 32, node]); out.push(bytes); };
    const sfnEntry = (sfn11, attr, node, size, mtime) => {
      const e = new Uint8Array(32);
      for (let i = 0; i < 11; i++) e[i] = sfn11.charCodeAt(i);
      e[11] = attr;
      const { date, time } = VirtualCard._fatTime(mtime);
      e[14] = time & 0xFF; e[15] = time >> 8; e[16] = date & 0xFF; e[17] = date >> 8; e[18] = date & 0xFF; e[19] = date >> 8;
      e[22] = time & 0xFF; e[23] = time >> 8; e[24] = date & 0xFF; e[25] = date >> 8;
      e[28] = size & 0xFF; e[29] = (size >>> 8) & 0xFF; e[30] = (size >>> 16) & 0xFF; e[31] = (size >>> 24) & 0xFF;
      put(e, node);
    };
    if (d.parent) { sfnEntry('.          ', 0x10, '.', 0, d.mtime); sfnEntry('..         ', 0x10, '..', 0, d.mtime); }
    else { const e = new Uint8Array(32); for (let i = 0; i < 11; i++) e[i] = this.label.charCodeAt(i); e[11] = 0x08; put(e); }
    for (const k of d.kids.values()) {
      const { sfn, needLfn } = VirtualCard._sfn(k.name, used);
      if (needLfn) {
        const sum = VirtualCard._lfnSum(sfn);
        const u = Array.from(k.name).map(c => c.codePointAt(0) > 0xFFFF ? 0x5F : c.charCodeAt(0));
        const n = Math.ceil(u.length / 13);
        for (let s = n; s >= 1; s--) {
          const e = new Uint8Array(32);
          e[0] = s | (s === n ? 0x40 : 0); e[11] = 0x0F; e[13] = sum;
          const pos = [1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30];
          for (let j = 0; j < 13; j++) {
            const idx = (s - 1) * 13 + j;
            const v = idx < u.length ? u[idx] : (idx === u.length ? 0x0000 : 0xFFFF);
            e[pos[j]] = v & 0xFF; e[pos[j] + 1] = v >> 8;
          }
          put(e);
        }
      }
      sfnEntry(sfn, k.dir ? 0x10 : 0x20, k, k.dir ? 0 : k.size, k.mtime);
    }
    const buf = new Uint8Array(out.length * 32);
    out.forEach((e, i) => buf.set(e, i * 32));
    return buf;
  }
  static _lfnSum(sfn) { let s = 0; for (let i = 0; i < 11; i++) s = (((s & 1) << 7) | (s >> 1)) + sfn.charCodeAt(i) & 0xFF; return s; }
  static _sfn(name, used) {
    const ok = c => /[A-Z0-9$%'\-_@~`!(){}^#&]/.test(c);
    const up = name.toUpperCase();
    const dot = up.lastIndexOf('.');
    let base = dot > 0 ? up.slice(0, dot) : up, ext = dot > 0 ? up.slice(dot + 1) : '';
    let lossy = false;
    const clean = s => { let r = ''; for (const c of s) { if (c === ' ' || c === '.') { lossy = true; continue; } if (ok(c)) r += c; else { r += '_'; lossy = true; } } return r; };
    base = clean(base); ext = clean(ext);
    if (base.length > 8 || ext.length > 3) lossy = true;
    ext = ext.slice(0, 3);
    let needLfn = lossy || name !== up || (dot > 0 ? name.slice(0, dot) : name).length > 8;
    let b8 = base.slice(0, 8) || '_';
    let sfn = b8.padEnd(8, ' ') + ext.padEnd(3, ' ');
    if (lossy || used.has(sfn)) {
      needLfn = true;
      for (let n = 1; n < 1000000; n++) {
        const tail = '~' + n;
        const cand = (base.slice(0, 8 - tail.length) + tail).padEnd(8, ' ') + ext.padEnd(3, ' ');
        if (!used.has(cand)) { sfn = cand; break; }
      }
    }
    used.add(sfn);
    return { sfn, needLfn };
  }
  // ── sector access ──
  get sectors() { return this.total; }
  _clusterOwner(c) {
    let lo = 0, hi = this.extStarts.length - 1, best = -1;
    while (lo <= hi) { const m = (lo + hi) >> 1; if (this.extStarts[m] <= c) { best = m; lo = m + 1; } else hi = m - 1; }
    if (best < 0) return null;
    const e = this.ext[best];
    return c < e[0] + e[1] ? e : null;
  }
  _gen(s, out) {                                   // generated sector (no overlay)
    out.fill(0);
    if (s === 0) {                                 // MBR, one FAT32 (LBA) partition
      const p = 446; out[p] = 0x00; out[p + 4] = 0x0C;
      out[p + 1] = 0xFE; out[p + 2] = 0xFF; out[p + 3] = 0xFF; out[p + 5] = 0xFE; out[p + 6] = 0xFF; out[p + 7] = 0xFF;
      w32(out, p + 8, this.P); w32(out, p + 12, this.part); out[510] = 0x55; out[511] = 0xAA; return;
    }
    const r = s - this.P;
    if (r === 0 || r === 6) {                      // boot sector (+ backup)
      out.set([0xEB, 0x58, 0x90], 0); for (let i = 0; i < 8; i++) out[3 + i] = 'MSWIN4.1'.charCodeAt(i);
      w16(out, 11, 512); out[13] = this.SPC; w16(out, 14, 32); out[16] = 2; out[21] = 0xF8; w16(out, 24, 63); w16(out, 26, 255);
      w32(out, 28, this.P); w32(out, 32, this.part); w32(out, 36, this.fatsz); w32(out, 44, 2); w16(out, 48, 1); w16(out, 50, 6);
      out[64] = 0x80; out[66] = 0x29; w32(out, 67, 0x47544953);
      for (let i = 0; i < 11; i++) out[71 + i] = this.label.charCodeAt(i);
      for (let i = 0; i < 8; i++) out[82 + i] = 'FAT32   '.charCodeAt(i);
      out[510] = 0x55; out[511] = 0xAA; return;
    }
    if (r === 1 || r === 7) {                      // FSInfo
      w32(out, 0, 0x41615252); w32(out, 484, 0x61417272); w32(out, 488, this.freeClusters); w32(out, 492, this.nextFree); w32(out, 508, 0xAA550000); return;
    }
    if (r === 2 || r === 8) { out[510] = 0x55; out[511] = 0xAA; return; }
    if (s >= this.fat1 && s < this.data) {          // FAT (both copies)
      const k = (s - this.fat1) % this.fatsz;
      const first = k * 128;
      for (let i = 0; i < 128; i++) { const c = first + i; if (c < this.fat.length) w32(out, i * 4, this.fat[c]); }
      return;
    }
    if (s >= this.data) {
      const c = 2 + Math.floor((s - this.data) / this.SPC);
      const e = this._clusterOwner(c); if (!e) return;
      const node = e[2];
      const off = ((c - node.clus) * this.SPC + (s - this.data) % this.SPC) * 512;
      if (node.dir) { const src = node.entries; if (off < src.length) out.set(src.subarray(off, Math.min(off + 512, src.length))); return; }
      if (off >= node.size) return;
      const n = Math.min(512, node.size - off);
      out.set(this._fileBytes(node, off, n));
    }
  }
  _fileBytes(node, off, n) {
    const src = node.src;
    if (src instanceof ArrayBuffer) return new Uint8Array(src, off, n);
    if (ArrayBuffer.isView(src)) return src.subarray(off, off + n);
    if (typeof src === 'function') return src(off, n);
    // Blob / File: read ahead 256 KB at a time (one FileReaderSync per chunk, not per sector)
    const CH = 256 * 1024, base = Math.floor(off / CH) * CH;
    if (!node._cache || node._cacheBase !== base) {
      node._cache = this.readSync(src, base, Math.min(CH, node.size - base));
      node._cacheBase = base;
    }
    return node._cache.subarray(off - base, off - base + n);
  }
  read(s, count, dst) {                            // dst: Uint8Array(count*512)
    this.reads += count;
    for (let i = 0; i < count; i++) {
      const o = dst.subarray(i * 512, i * 512 + 512);
      const ov = this.overlay.get(s + i);
      if (ov) o.set(ov); else this._gen(s + i, o);
    }
    return true;
  }
  write(s, count, src) {
    this.writes += count;
    for (let i = 0; i < count; i++) this.overlay.set(s + i, src.slice(i * 512, i * 512 + 512));
    return true;
  }
}
function w16(b, o, v) { b[o] = v & 0xFF; b[o + 1] = (v >>> 8) & 0xFF; }
function w32(b, o, v) { b[o] = v & 0xFF; b[o + 1] = (v >>> 8) & 0xFF; b[o + 2] = (v >>> 16) & 0xFF; b[o + 3] = (v >>> 24) & 0xFF; }

// ─────────────────────────────── the machine ───────────────────────────────
// Runs sim_main() (setup + loop forever). Every delay() in the firmware unwinds to here (asyncify),
// waits on a timer, then rewinds - so the firmware runs unchanged and the browser stays responsive.
const RTC_VARS = ['g_sdaccess_magic', 'g_cap_magic', 'g_cap_images', 'g_cap_games', 'g_cap_fit', 'g_cap_pct', 'g_cap_atleast', 'g_bootMagic', 'g_bootCount',
  'g_bc_magic', 'g_bc_stage', 'g_bc_n', 'g_bc_psram', 'g_bc_int', 'g_bc_failsz', 'g_bc_failcaps'];
class GtiMachine {
  constructor(module, card, hooks) {
    this.module = module; this.card = card; this.hooks = hooks;
    this.touch = { down: false, x: 0, y: 0 };      // native panel pixels (320 x 480)
    this.usbHost = true;
    this.frame = new Uint8ClampedArray(320 * 480 * 4);
    this.dirtyY0 = 480; this.dirtyY1 = 0;
    this.rtc = null; this.resetReason = 1;         // ESP_RST_POWERON
    this.running = false; this.gen = 0;
    this.tzOff = new Date().getTimezoneOffset() * 60000;
    this.t0 = (typeof performance !== 'undefined' ? performance : { now: () => Number(process.hrtime.bigint()) / 1e6 }).now();
    this.now = () => (typeof performance !== 'undefined' ? performance.now() : Number(process.hrtime.bigint()) / 1e6);
  }
  async boot() {
    const gen = ++this.gen;
    const self = this;
    let mem = null; const U8 = () => new Uint8Array(mem.buffer), DV = () => new DataView(mem.buffer);
    const str = (p, n) => { const b = U8(); let e = p; if (n === undefined) { while (b[e]) e++; n = e - p; } return new TextDecoder().decode(b.subarray(p, p + n)); };
    let ex = null, DATA = 0; const STACK = 4 * 1024 * 1024;
    let sleepMs = -1, restarting = false;
    const ASYNC_NORMAL = 0, ASYNC_UNWIND = 1, ASYNC_REWIND = 2;
    const sleepImport = (ms) => {
      if (ex.asyncify_get_state() === ASYNC_REWIND) { ex.asyncify_stop_rewind(); return; }
      DV().setInt32(DATA, DATA + 8, true); DV().setInt32(DATA + 4, DATA + 8 + STACK, true);
      sleepMs = ms >>> 0; ex.asyncify_start_unwind(DATA);
    };
    const env = {
      js_now_ms: () => this.now() - this.t0,
      js_epoch_ms: () => Date.now() - this.tzOff,
      js_sleep: sleepImport,
      js_log: (p, n) => this.hooks.log && this.hooks.log(str(p, n)),
      js_present: (p, x0, y0, x1, y1) => {
        const b = U8(); const f = this.frame; const W = 320;
        for (let y = y0; y < y1; y++) {
          let si = p + ((y - y0) * (x1 - x0)) * 2, di = (y * W + x0) * 4;
          for (let x = x0; x < x1; x++, si += 2, di += 4) {
            const v = (b[si] << 8) | b[si + 1];      // the panel takes big-endian RGB565 (the firmware's swap16)
            f[di] = ((v >> 11) & 0x1F) * 255 / 31; f[di + 1] = ((v >> 5) & 0x3F) * 255 / 63; f[di + 2] = (v & 0x1F) * 255 / 31; f[di + 3] = 255;
          }
        }
        if (y0 < this.dirtyY0) this.dirtyY0 = y0; if (y1 > this.dirtyY1) this.dirtyY1 = y1;
        if (y1 >= 480) { this.hooks.frame && this.hooks.frame(this.frame); this.dirtyY0 = 480; this.dirtyY1 = 0; }
      },
      js_touch: (px, py) => { if (!this.touch.down) return 0; DV().setInt32(px, this.touch.x, true); DV().setInt32(py, this.touch.y, true); return 1; },
      js_disk_present: () => this.card ? 1 : 0,
      js_disk_sectors: () => this.card ? this.card.sectors : 0,
      js_disk_read: (p, s, n) => { if (!this.card) return 0; return this.card.read(s, n, U8().subarray(p, p + n * 512)) ? 1 : 0; },
      js_disk_write: (p, s, n) => { if (!this.card) return 0; return this.card.write(s, n, U8().subarray(p, p + n * 512)) ? 1 : 0; },
      js_restart: () => {
        if (ex.asyncify_get_state() === ASYNC_REWIND) { ex.asyncify_stop_rewind(); return; }
        restarting = true; DV().setInt32(DATA, DATA + 8, true); DV().setInt32(DATA + 4, DATA + 8 + STACK, true); ex.asyncify_start_unwind(DATA);
      },
      js_event: (k, a, b, s) => this.hooks.event && this.hooks.event(k, a, b, s ? str(s) : ''),
      js_usb_host: () => this.usbHost ? 1 : 0,
      js_random: () => (Math.random() * 4294967296) >>> 0,
      js_backlight: (d) => this.hooks.backlight && this.hooks.backlight(d),
    };
    const wasi = {
      fd_write: (fd, iovs, n, nw) => { const dv = DV(); let tot = 0, s = '';
        for (let i = 0; i < n; i++) { const p = dv.getUint32(iovs + i * 8, true), l = dv.getUint32(iovs + i * 8 + 4, true); s += str(p, l); tot += l; }
        dv.setUint32(nw, tot, true); if (this.hooks.log) this.hooks.log(s); return 0; },
      fd_close: () => 0, fd_seek: () => 70,
      fd_fdstat_get: (fd, p) => { const dv = DV(); dv.setUint8(p, 2); dv.setUint16(p + 2, 0, true); dv.setBigUint64(p + 8, 0n, true); dv.setBigUint64(p + 16, 0n, true); return 0; },
      clock_time_get: (id, prec, p) => { const ns = id === 0 ? BigInt(Math.round((Date.now() - this.tzOff) * 1e6)) : BigInt(Math.round(this.now() * 1e6)); DV().setBigUint64(p, ns, true); return 0; },
      random_get: (p, n) => { const b = U8(); for (let i = 0; i < n; i++) b[p + i] = (Math.random() * 256) | 0; return 0; },
      proc_exit: (c) => { throw new Error('firmware exit ' + c); },
      environ_get: () => 0, environ_sizes_get: (a, b) => { DV().setUint32(a, 0, true); DV().setUint32(b, 0, true); return 0; },
      args_get: () => 0, args_sizes_get: (a, b) => { DV().setUint32(a, 0, true); DV().setUint32(b, 0, true); return 0; },
    };
    const inst = await WebAssembly.instantiate(this.module, { env, wasi_snapshot_preview1: wasi });
    ex = inst.exports; mem = ex.memory; this.ex = ex; this.mem = mem;
    ex._initialize();
    DATA = ex.sim_malloc_raw(STACK + 16);
    // power-on vs software restart: NOINIT RAM survives a restart only
    if (this.rtc) { const dv = DV(); for (const v of RTC_VARS) if (ex[v]) dv.setUint32(ex[v].value, this.rtc[v] >>> 0, true); }
    ex.sim_set_reset_reason(this.resetReason);
    this.running = true;
    const step = () => {
      if (gen !== this.gen) return;                  // a newer boot took over
      sleepMs = -1;
      try { ex.sim_main(); }
      catch (e) { this.running = false; this.hooks.crash && this.hooks.crash(e); return; }
      if (ex.asyncify_get_state() !== ASYNC_UNWIND) { this.running = false; return; }   // sim_main returned for real (never)
      ex.asyncify_stop_unwind();
      if (restarting) {                              // ESP.restart(): keep NOINIT, boot a fresh instance
        const dv = DV(); this.rtc = {}; for (const v of RTC_VARS) if (ex[v]) this.rtc[v] = dv.getUint32(ex[v].value, true);
        this.resetReason = 3;                        // ESP_RST_SW
        this.hooks.restart && this.hooks.restart();
        this.boot(); return;
      }
      const resume = () => { if (gen !== this.gen) return; ex.asyncify_start_rewind(DATA); step(); };
      this.schedule(resume, sleepMs);
    };
    this.schedule(step, 0);
  }
  schedule(fn, ms) { if (ms <= 0 && typeof MessageChannel !== 'undefined') { if (!this._mc) { this._mc = new MessageChannel(); this._q = []; this._mc.port1.onmessage = () => { const f = this._q.shift(); f && f(); }; } this._q.push(fn); this._mc.port2.postMessage(0); } else setTimeout(fn, ms); }
  powerCycle() { this.rtc = null; this.resetReason = 1; this.gen++; this.boot(); }
  // ── helpers for the page (only between firmware steps) ──
  json(fn) { const p = this.ex[fn](); const b = new Uint8Array(this.mem.buffer); let e = p; while (b[e]) e++; return JSON.parse(new TextDecoder().decode(b.subarray(p, e))); }
  mscRead(lba, count) {
    const n = count * 512, p = this.ex.sim_malloc(n);
    const r = this.ex.sim_msc_read(lba, p, n);
    const out = new Uint8Array(this.mem.buffer, p, n).slice(); this.ex.sim_free(p);
    return r === n ? out : null;
  }
  mscWrite(lba, data) {
    const p = this.ex.sim_malloc(data.length); new Uint8Array(this.mem.buffer, p, data.length).set(data);
    const r = this.ex.sim_msc_write(lba, p, data.length); this.ex.sim_free(p); return r;
  }
}
if (typeof module !== 'undefined') module.exports = { VirtualCard, GtiMachine };
