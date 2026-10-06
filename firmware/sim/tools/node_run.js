// Headless test: boot the real firmware in Node on a small virtual card, script touches, save screenshots.
//   node tools/node_run.js <script.json> [outdir]
// script: [{wait:ms} | {tap:[x,y]} | {drag:[x0,y0,x1,y1,ms]} | {shot:"name"} | {usb:0|1} | {log:true}]
const fs = require('fs'), path = require('path');
const { VirtualCard, GtiMachine } = require('../web/gti_core.js');
const sharp = require(require('child_process').execSync('npm root -g').toString().trim() + '/sharp');

async function main() {
  const script = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
  const out = process.argv[3] || '/tmp/gti_shots'; fs.mkdirSync(out, { recursive: true });
  const lib = require('./testlib.js');
  let files;
  if (script.library === 'demo') { const man = JSON.parse(fs.readFileSync(path.join(__dirname, '../web/library/manifest.json'))); files = man.files.map(f => { const b = new Uint8Array(fs.readFileSync(path.join(__dirname, '../web/library', f.url))); return { path: f.path, size: b.length, mtime: Date.now(), src: b }; }); }
  else files = await lib.build(script.library || 'small');
  if (script.drop) files = files.filter(f => !script.drop.some(d => f.path.startsWith(d)));
  for (const [cp, lf] of (script.extra || [])) { const b = new Uint8Array(fs.readFileSync(lf)); files.push({ path: cp, size: b.length, mtime: Date.now(), src: b }); }
  const card = new VirtualCard(files);
  console.log(`card: ${(card.total * 512 / 1e9).toFixed(0)} GB, ${files.length} files, data at sector ${card.data}`);
  const wasm = new WebAssembly.Module(fs.readFileSync(path.join(__dirname, '../build/gti.wasm')));
  let last = null, logbuf = '', frames = 0;
  const m = new GtiMachine(wasm, card, {
    log: s => { logbuf += s; if (script.verbose) process.stdout.write(s); },
    frame: f => { last = f; frames++; },
    event: (k, a, b, s) => { if (script.events) console.log('event', k, a, b, s); },
    crash: e => { console.log('CRASH', e); process.exit(2); },
    restart: () => console.log('-- restart --'),
  });
  // display rotation for taps: the script gives logical landscape coords (480x320) unless rot given
  const toPanel = (x, y) => (script.portrait ? [x, y] : [y, 479 - x]);   // landscape g_rot=0: gTouchX=(479-py), gTouchY=px
  await m.boot();
  const sleep = ms => new Promise(r => setTimeout(r, ms));
  for (const st of script.steps) {
    if (st.wait) await sleep(st.wait);
    if (st.tap) { const [px, py] = toPanel(...st.tap); Object.assign(m.touch, { down: true, x: px, y: py }); await sleep(st.hold || 120); m.touch.down = false; await sleep(250); }
    if (st.drag) { const f0 = frames, t0 = Date.now(); const [x0, y0, x1, y1, ms] = st.drag; const n = st.steps || 12; for (let i = 0; i <= n; i++) { const [px, py] = toPanel(x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n); Object.assign(m.touch, { down: true, x: Math.round(px), y: Math.round(py) }); await sleep((ms || 400) / n); } m.touch.down = false; if (st.steps) console.log('drag fps', ((frames - f0) * 1000 / (Date.now() - t0)).toFixed(1)); await sleep(300); }
    if (st.usb !== undefined) m.usbHost = !!st.usb;
    if (st.dongle) { const [op, i, on] = st.dongle; if (op === 'power') m.ex.sim_dongle_power(i, on); if (op === 'save') m.ex.sim_dongle_game_save(i); }
    if (st.gsave) { const b = m.mscRead(0, 1); const rsv = b[14] | b[15] << 8, nf = b[16], spf = b[22] | b[23] << 8, rootN = b[17] | b[18] << 8; const rootLba = rsv + nf * spf; const r = m.mscRead(rootLba, 1); const clus = r[26] | r[27] << 8; const data = rootLba + Math.ceil(rootN * 32 / 512); const lba = data + (clus - 2) * b[13] + 1300; const d = new Uint8Array(1536).fill(0x5A); console.log('gotek write', lba, m.mscWrite(lba, d)); }
    if (st.fps) { const f0 = frames, t0 = Date.now(); await sleep(st.fps); console.log('fps', ((frames - f0) * 1000 / (Date.now() - t0)).toFixed(1)); }
    if (st.json) console.log(JSON.stringify(m.json('sim_dongle_json')));
    if (st.shot) {
      if (!last) { console.log('no frame yet'); continue; }
      const img = sharp(Buffer.from(last.buffer.slice(0)), { raw: { width: 320, height: 480, channels: 4 } });
      const rot = script.portrait ? img : img.rotate(st.rot !== undefined ? st.rot : 90);
      await rot.png().toFile(path.join(out, st.shot + '.png'));
      console.log('shot', st.shot);
    }
    if (st.log) { console.log(logbuf.slice(-3000)); }
  }
  fs.writeFileSync(path.join(out, 'serial.log'), logbuf);
  console.log(`reads ${card.reads} sectors, writes ${card.writes}, overlay ${card.overlay.size}`);
  process.exit(0);
}
main();
