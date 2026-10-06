// GTi simulator worker: runs the firmware and the virtual SD card off the page's main thread.
'use strict';
importScripts('gti_core.js');
let machine = null, card = null, wasmModule = null;
const readSync = (blob, off, len) => new Uint8Array(new FileReaderSync().readAsArrayBuffer(blob.slice(off, off + len)));
let logBuf = '', logTimer = 0;
function flushLog() { if (logBuf) { postMessage({ t: 'log', s: logBuf }); logBuf = ''; } logTimer = 0; }
let lastFramePost = 0, framePending = null, frameTimer = 0;
function postFrame() { frameTimer = 0; if (!framePending) return; const f = framePending; framePending = null; lastFramePost = performance.now(); postMessage({ t: 'frame', f }, [f.buffer]); }
const hooks = {
  log: s => { logBuf += s; if (!logTimer) logTimer = setTimeout(flushLog, 100); },
  frame: f => {                                   // at most ~40 frames a second to the page
    framePending = f.slice();
    if (!frameTimer) frameTimer = setTimeout(postFrame, Math.max(0, 25 - (performance.now() - lastFramePost)));
  },
  event: (k, a, b, s) => postMessage({ t: 'ev', k, a, b, s }),
  backlight: d => postMessage({ t: 'bl', d }),
  restart: () => postMessage({ t: 'restart' }),
  crash: e => postMessage({ t: 'crash', s: String(e && e.stack || e) }),
};
function status() {
  if (!machine || !machine.ex) return;
  let dongles = null; try { dongles = machine.json('sim_dongle_json'); } catch (e) {}
  postMessage({ t: 'status', msc: machine.ex.sim_msc_state(), blocks: machine.ex.sim_msc_blocks(), dongles, card: card ? { gb: Math.round(card.total * 512 / 1e9), writes: card.writes, overlay: card.overlay.size } : null });
}
onmessage = async (ev) => {
  const m = ev.data;
  try {
    if (m.t === 'init') { wasmModule = await WebAssembly.compileStreaming(fetch(m.wasm)).catch(async () => WebAssembly.compile(await (await fetch(m.wasm)).arrayBuffer())); postMessage({ t: 'ready' }); }
    else if (m.t === 'card') {           // a new SD card: [{path,size,mtime,src}]
      card = m.files ? new VirtualCard(m.files, { readSync, label: m.label }) : null;
      if (machine) machine.card = card;
      postMessage({ t: 'cardok', gb: card ? Math.round(card.total * 512 / 1e9) : 0, files: m.files ? m.files.length : 0 });
    }
    else if (m.t === 'power') {          // power on / power cycle
      if (machine) machine.gen++;
      machine = new GtiMachine(wasmModule, card, hooks);
      await machine.boot();
    }
    else if (m.t === 'touch') { if (machine) { machine.touch.down = m.down; if (m.down) { machine.touch.x = m.x; machine.touch.y = m.y; } } }
    else if (m.t === 'usb') { if (machine) machine.usbHost = !!m.on; }
    else if (m.t === 'status') status();
    else if (m.t === 'msceject') { if (machine) machine.ex.sim_msc_eject(); }
    else if (m.t === 'dongle') {
      if (!machine) return;
      if (m.op === 'power') machine.ex.sim_dongle_power(m.i, m.on ? 1 : 0);
      if (m.op === 'save') machine.ex.sim_dongle_game_save(m.i);
      status();
    }
    else if (m.t === 'msc') {            // the Gotek reads (or writes) the disk on the USB cable
      if (!machine) return;
      if (m.op === 'read') { const d = machine.mscRead(m.lba, m.n); postMessage({ t: 'msc', id: m.id, d }, d ? [d.buffer] : []); }
      if (m.op === 'write') { const r = machine.mscWrite(m.lba, m.d); postMessage({ t: 'msc', id: m.id, r }); }
    }
    else if (m.t === 'cardread') {       // the page reads files off the card (overlay included) for downloads
      if (!card) return postMessage({ t: 'cardread', id: m.id, d: null });
      const d = new Uint8Array(m.n * 512); card.read(m.s, m.n, d); postMessage({ t: 'cardread', id: m.id, d }, [d.buffer]);
    }
  } catch (e) { postMessage({ t: 'crash', s: String(e && e.stack || e) }); }
};
